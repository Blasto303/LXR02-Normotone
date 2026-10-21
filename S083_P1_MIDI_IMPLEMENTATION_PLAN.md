# S083 P1 — MIDI Implementation Plan (revision 4)

*Written during Session 082 (2026-10-16/17). Planning only: no code has
changed. Revision 4 folds in the answers to all three rounds of
follow-ups (decision log §11). The background Bank load now has its own
plan, `S082_P1_LOAD_SAVE.md`; §4 here keeps only what MIDI needs from it.
No questions remain open (§12).*

*Update 2026-10-21 (Session 082 close): the background Bank load, Scene 17,
the MIDI deferral, the drop filter and the result byte are built (code
`b25ffbe`, `8a83df0`). §13 lists what S082 changed for this plan; the inline
notes marked "S082" below point to it. `S082_P1_LOAD_SAVE.md` is superseded
by `knowledge_files/log_archive/082_SESSION_HANDOFF_LOG.md`.*

*Update 2026-10-21 (S081 close-out): Session 081 is now logged in
`knowledge_files/log_archive/081_SESSION_HANDOFF_LOG.md`, which supersedes
the `S081_*` root documents cited below. §14 lists what S081 changed for
this plan, and §12 has one open question (Q1, MIDI write priority under
R12).*

Scope: `SCOPING_TARGETS.md` Phase 6 §6.1 (MIDI and External Control
Cleanup), deferred item A20 (MIDI mapping), and the MIDI items deferred
from S081 (`S081_P1_LIVE_RECORD.md` §16; now `081_SESSION_HANDOFF_LOG.md`
§4.13 and §16).

Terms: **track** T1..T7; **voice** (instrument slot) V1..V6. T6 and T7 both
play V6 (closed/open HiHat). **G** is the global MIDI channel. "Consumed"
means the LXR acted on a received message. **Scene 17** is the background
Scene that keeps playback going through a Bank load (§4).

---

## 1. Requirements

### 1.1 Global settings (MENU_MIDI_PAGE)

| Id | Cell | Requirement |
|----|------|-------------|
| M1 | `snc` | MIDI realtime (clock/transport) is a sync source for the `usb`, `din` and `aut` settings. |
| M2 | `ch` | Global MIDI channel, 1..16 plus **0 = off**. |
| M3 | `TXF` | What the LXR **transmits** from itself: N note, R realtime (clock + transport), C control (the learned CC if one is set, else NRPN), P program + bank change. It does not govern pass-through; that follows `MRT`. |
| M4 | `RXF` | What the LXR **consumes**. Same letters. A message blocked by RXF is not consumed, so it is forwarded. |
| M5 | `MRT` | DIN/USB routing of messages that are not consumed: `off UtD DtD DtU bth DtA AtD all` (§3.10). Clock and transport are forwarded per MRT even when consumed. |

### 1.2 Note → track map (per Scene: each track's MIDI channel and note)

| Id | Situation | Rule |
|----|-----------|------|
| N1 | Only G assigned; no track notes | Tracks are striped across G: note 0 → T1 … note 6 → T7, repeating up to note 127. Every note plays the voice's default pitch. |
| N2 | Only G; some tracks have a note | A track with a note leaves the stripe. It owns its note at default pitch, and every higher note plays it one semitone higher, up to the next assignment or 127. |
| N3 | Some tracks have a channel; no notes | On each channel, its tracks share 0..127 in contiguous, near-equal ranges in voice order. Each range starts at the voice's root and is chromatic. Lower tracks get the remainder notes. |
| N4 | Channel and note assignments | As N3, but each assigned track starts at its note and fills up to the next assignment or 127. Notes below the lowest assignment are filled by the channel's tracks that have no note. |
| N5 | Priority | A note never triggers more than one voice. The lowest-numbered track wins. |
| N6 | Action | A note is one trigger at its velocity. If a BAR `<`/`>` overlay is selected (CC2, or a held BAR in PERF), the note performs that overlay's action instead. |

A track set explicitly to G's channel number follows the G rules (stripe).
Tracks with their own channel never join G's stripe. Pitch above the
recordable range is clamped (§3.3).

### 1.3 Realtime

| Id | Requirement |
|----|-------------|
| T1 | Clock and as many transport messages as practicable: Start, Continue (resume), Stop; SPP while stopped; MTC (existing). Start/Stop are ignored when `snc = off`. MMC comes later. |

### 1.4 Program and Bank change

| Id | Requirement |
|----|-------------|
| P1 | A program change is a Scene change. Only PC 0..15 is data. |
| P2 | PC on G changes the whole Scene, exactly like a PERF SEQ press (`scd` division, `pcr`), in any state. |
| P3 | PC on a track's assigned channel changes only that track, like hold VOICE + SEQ. No queue. |
| P4 | CC0 on G loads Bank 000..127 through the background Bank load (`S082_P1_LOAD_SAVE.md`). The UI goes to Load/Save and stays there afterwards, exactly as if the load had been made from the Menu. PC and CC0 arriving during a load are dropped. *(S082 P3 amendment: once the load unlocks, the user may leave Load/Save; §13.2 item 1.)* |

### 1.5 Control

| Id | Requirement |
|----|-------------|
| C1 | NRPN number = canonical 9-bit target ID: voices 0..383 (`slot*64 + descriptor index`), Scene 384..447, Effect 448..510. Unused IDs are ignored and forwarded. |
| C2 | NRPN works on G. Voice parameters, including the per-voice Scene targets (`1vm`, `1fx`, `1ou`, track length/scale/shuffle), also work on that voice's channel, using the same absolute numbers (only that voice's IDs accepted there). Other Scene and FX targets work on G only. |
| C3 | **Values are instrument-agnostic and absolute.** The 7-bit value goes to the parameter unchanged, with no per-parameter special casing. The only exception is the Morph amounts, which use the existing 7-bit automation translation (`value*2`, 127 → 255; `sequencer.c:3246`). |
| C4 | **MIDI Learn is global** (device-wide), stored in `settings.cfg`, up to **128 entries**. Each entry carries the instrument type (voice targets) or Effect type (FX targets). An entry applies only while that type is loaded in the target slot. One CC may control several parameters (one row each). **A parameter may be controlled by only one CC:** an entry is unique on (target slot/space, type, parameter). |
| C5 | Learn gesture: SHIFT+PLAY+REC, then touch a parameter. The parameter shows `LRN`, and SHIFT, PLAY and REC flash until a valid CC arrives. Pressing any of the three cancels. There is no remove in the UI (delete the line from `settings.cfg`). Learning a parameter that already has an entry replaces that entry's CC and channel. |
| C6 | Reserved, never learnable: CC0 (Bank), CC1 (Morph), CC2 (overlay), 6/38/96..101 (NRPN/RPN), 32 (bank LSB), 120..127 (channel mode). |
| C7 | CC1: global Morph on G (never recorded, G-1). On a voice channel it is that voice's Morph for every voice on the channel (recorded as an individual Morph edit). |
| C8 | CC2 on G only, in any UI mode: 0 = single trigger, 1 = BAR `<` overlay, 2 = BAR `>` overlay; other values ignored. Overlays use the incoming note's pitch. |
| C9 | MIDI value writes go to the active Scene and fan out through the VOICE edit mask like knob edits, at the lowest priority (automation, then menu, then MIDI). *S081 R12 lets a menu edit beat Instrument automation until the voice's next trigger; whether MIDI follows is §12 Q1.* |

---

## 2. Current code baseline (checked 2026-10-16)

| Area | What exists | Gap |
|------|-------------|-----|
| Framing | `MidiParser.c` DIN framing (running status, sysex pass-through). USB packets are framed in `usb_midi_core.c`. Realtime bytes from both ports go to the timestamped `MidiRealtime` ring (TIM3). | SPP (`0xF2`) and Song Select are not framed. `usb_sendMidi()` uses `status>>4` as the CIN, so forwarded system-common and sysex packets are malformed on USB. The USB RX ring (256) overwrites when full. |
| Context | `midi_service()` in the main loop: `uart_processMidi()` and up to 8 USB messages per pass. | Note timing depends on foreground latency. Channel messages carry no timestamp. |
| Routing | Every message is forwarded **before** it is parsed. | M5 needs forwarding decided after consumption, plus the new MRT list. |
| Filters | `midiParser_txRxFilter` (RX low nibble, TX high nibble, N=1 R=2 C=4 P=8). | Fits M3/M4. |
| Channels | `midi_MidiChannels[8]`: [7] = G (no off). [0..6] are a legacy mirror. Track channel/note live in `scene_settings_t::midi_channel/midi_note[7]`. The getter falls back to **track+1**. | No off for G. No "unassigned" for track channels. Two sources of truth. |
| Note input | On G, plays the **menu-active voice**. Elsewhere, every exact (channel, note) match fires (stacked). | N1..N5 not implemented. |
| Pitch | Internal note 63 = default pitch (`Oscillator.c:1007`). Step note specials are 7-bit. | Map to 63 + offset, clamp. |
| Live record | Notes go to `liveRec_triggerIn()`. CC goes to the stub `liveRec_paramIn(0xFFFF, MIDI)`. | Real target IDs. |
| CC | CC1 global/voice Morph exists. Other CCs on G use the legacy LXR CC/CC2 map into the active Scene's Normal endpoint (S075 F3), with no fan-out. `modNode_originalValueChanged(data1-1)` is a legacy flat-id call. | Replace with NRPN + Learn; add fan-out. |
| NRPN | The 14-bit number is truncated to 8 bits into the legacy CC2 page. | Replace. |
| Automation clamp | Scene-target automation clamps to `descriptor->max_value` and translates Morph `value*2` (`sequencer.c:3239–3246`). | Reuse this for C3. |
| PC | RX `seq_setNextPattern()` is a no-op. TX `seq_sendProgChg()` on Scene change (on G). | P1..P3; skip TX when G = off. |
| Realtime | Continue = Start. No SPP. Clock out only while internal. | T1. |
| TX | Always both ports. Note-off cache is one note per channel (`midi_chan_notes[16]`). | Per-track note-off cache. |
| `settings.cfg` | Streamed one line at a time (`filesystem_nextSettingsLine()`, no file buffer). 1 s debounce, temp + sync + promote. The `lines=N` terminator is checked only by the `settings.tmp` boot recovery; normal load ignores it. | Learn lines fit the existing writer. |
| Bank Load | **Built in S082:** `preset_requestBackgroundBankLoad()` (Menu and MIDI origin), Scene 17 (`scene_background` + `pat_background_region`), progressive load with the panel unlocked from phase 5, MIDI deferral and drop filter, result byte (§13). | CC0 RX → the request API (Phase D). |
| Dead code | `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()` (no callers), `preset_applySoundParameter(…recordAutomation)` and the STEP armed-automation remnant (both left by S081 for this rework), `midi_NoteOverride[7]`, the dead realtime cases in `midiParser_parseMidiMessage()`, ~1,000 lines of `#if 0`. | MEMORY.md "MIDI / Clock Reminders" (BAR1/BAR2 path, default notes 36..42) are stale. |

---

## 3. Architecture

### 3.1 Modules

| Module | Owns |
|--------|------|
| `MidiParser.c` (slimmed) | DIN framing, entry from both ports, the consume-then-route dispatcher, realtime handling (TIM3). |
| `MidiNoteMap.c/h` (new) | The note → (track, internal note) resolver (N1–N5) and its TX inverse. Stateless. |
| `MidiControl.c/h` (new) | NRPN state machine, CC1/CC2, Learn table and Learn capture, the MIDI parameter write path. |
| `MidiOut.c/h` (new, or kept in Sequencer) | TX filters, per-track note-off cache, parameter TX coalescing, PC/CC0 TX, echo suppression. |

`MidiVoiceControl.c` is unchanged by S083 (trigger funnel, S081 §19.15).
S081 changed its API: the per-voice pending-reset mask
`voiceControl_pendingReset`, `voiceControl_noteOnNoReset()`,
`voiceControl_resetOnly()` and `voiceControl_triggerNow(…, reset)` (§14).

### 3.2 Message flow

```
DIN byte ─► framer ─┐
USB packet ─────────┴─► MidiMsg{source} ─► RXF gate ─► consumers ─► consumed?
                                                                     │ no
                                                                     ▼
                                                   MRT forward (never back to source)
realtime ─► MidiRealtime ring ─► TIM3: snc/RXF gate ─► sync; MRT forward (always)
```

- **Consumed:** a note that resolved to a track; NRPN; CC0/1/2; a Learn
  hit; a PC 0..15 that acted.
- NRPN protocol CCs on G or a voice channel are always consumed while RXF C
  is on.
- While slaved, the LXR does not generate its own clock. LXR-generated
  output goes to both ports.

### 3.3 Note map resolver

Inputs come from the active Scene: G; per track `ch(t)` ∈ {0 = unassigned,
1..16} and `nt(t)` ∈ {unassigned, 0..127}. Effective channel `ec(t) =
ch(t) ? ch(t) : G`. Tracks with `ec = 0` are not mapped.

For an incoming (c, n): S = tracks with `ec = c`; A = those in S with a
note; U = the rest.

1. **Assigned ranges:** sort A by note. Each track owns
   `[nt(a), next − 1]` (or up to 127). Internal note `63 + (n − nt(a))`. If
   two tracks share a note, the lower track wins.
2. **Region below** the lowest assignment (or all of 0..127):
   - c = G: track `U[n mod |U|]`, internal note 63.
   - c ≠ G: contiguous blocks in track order, remainder to the lower tracks,
     internal note `63 + (n − block start)`.
   - U empty: not mapped, so not consumed (forwarded).
3. Clamp the internal note to 127 (+64 semitones at most).

No table and no RAM; at most 7 tracks per lookup. The TX inverse sends a
track's notes back on (`ec(t)`, root + (internal − 63)), so recorded LXR
output plays back identically.

### 3.4 Triggers, overlays and recording

- A single trigger goes through `voiceControl_noteOn()` then
  `liveRec_triggerIn()`. Each MIDI note-on is a resetting trigger: it
  restores the voice's held automation and ends an R12 override (S081 step
  reset rule, F12). Roll repeats must not reset (`noteOnNoReset()`); the BAR
  overlay executor already passes reset only for the first hit and the flam
  main (§14).
- Overlay actions reuse the S080 BAR overlay executor. Rolls repeat until
  note-off, which needs a 7 B held-note record per track.
- CC2 state is 1 B.
- The `seq_isTrackMuted()` gate stays.

### 3.5 Parameter writes (C1–C3, C7, C9)

- **Value:** the 7-bit value, clamped to the parameter's max, the same rule
  step automation applies. Morph targets use the 7-bit translation. The
  clamp is the parameter's own range, not a special case: it keeps an
  enumerated parameter from reading past its table.
- **NRPN:** per (port, channel) context. CC99/98 set the number; CC6 carries
  the value; CC38 is ignored (F26); CC96/97 step ±1; RPN null deselects.
- **Write path:**
  - voice targets: `preset_setInstrumentParameterFromMidi()` (Normal
    endpoint, Morph sweep), with the menu's edit-mask fan-out added;
  - Scene and Effect targets: equivalent MIDI entries through SceneData and
    the EffectsManager edit API (which already fans out).
- **Priority (S081):** today a MIDI write stores the Normal endpoint and sets
  no override, so a parameter that automation holds stays inaudible until
  the voice's next trigger. A menu edit now calls `seq_menuOverrideSet()`
  and sounds at once (R12). Which rule MIDI follows is §12 Q1.
- **Recording:** `liveRec_paramIn(id, MIDI)` after each store. It carries no
  value: LiveRecord reads the stored value, so the store must come first.
  Global Morph is never recorded (G-1). Ownership, the first-change rule and
  the CC1 capture are in §14.
- **LCD:** `menu_notifyExternalParamChanged()` with a rate limit. This
  closes the S075 open item.

### 3.6 MIDI Learn (C4–C6)

**Entry (4 B):**

| Byte | Content |
|------|---------|
| 0 | CC (bits 0..6) + channel kind (bit 7: 0 = G, 1 = the target voice's channel) |
| 1 | Target space: 0..5 voice slot, 6 Scene, 7 Effect |
| 2 | Type: `instrument_type_t` (voice), Effect type id (FX), `0xFF` (Scene targets) |
| 3 | Index in the space: descriptor index 0..63, Scene-local 0..63, or Effect-local 0..62 |

The channel *kind* is stored, not the channel number, so entries follow later
channel edits.

**SRAM estimate (F36):**

| Item | Bytes |
|------|------:|
| Table, 128 × 4 B | 512 |
| Entry count | 1 |
| Learn capture state (armed, target, page/cell, flash) | ~6 |
| **Total, SRAM1, static, owner `MidiControl`** | **~519** |
| Alternative: 3 B packed (9-bit ID + 7-bit type + CC/kind) | ~391 |

No AutoSave, Scene, copy/clear or fan-out cost: the table is global.

**Lookup:** per incoming CC, scan the used entries (≤128). An entry matches
when:
- CC and channel kind match the arrival channel; and
- for a voice target, the active Scene's slot type equals the entry type;
  for an FX target, the active Scene's Effect type equals it.

Every match is applied (macros).

**Learn capture (C5; details F29–F33):** the chord arms learn, and the
touched parameter becomes the target and shows `LRN`. A valid CC is then:
- not reserved;
- on G, or on the target voice's effective channel (voice targets only).

On a valid CC, the parameter's existing entry is replaced (new CC and
channel), or a new entry is added. `settings.cfg` is marked dirty and the
display returns. A full table refuses a new entry (F29).

**`settings.cfg` (F34):** one line per entry after the fixed keys, with
readable descriptor keys so hand-editing works:

```
learn=020,g,v1,drm,filter_freq
learn=021,v,v3,snr,noise_freq
learn=022,g,fx,flt,cutoff
learn=023,g,scn,1vm
```

A malformed or unknown learn line is skipped; the rest of the file still
loads. The writer emits only used entries. `lines=N` counts them (the
`uint8_t` count still fits: 24 + 128 = 152). The load resolves each key to
its index once, through `instrumentManager_descriptorIndexByKey()` and the
Effect and Scene target tables.

### 3.7 Program change and Bank change

- **PC on G:** `menu_perfModeSceneButtonPressed()`. PCs 0..15 select
  Scenes 1..16, which also leaves Scene 17 permanently.
- **PC on a voice channel:** `seq_setTrackPlayedScene()` +
  `preset_startSingleVoiceApply()` for every track on that channel (T6/T7
  together). If c = G, G wins.
- **CC0 on G:** Bank slot = value (000..127), handed to
  `preset_requestBackgroundBankLoad(slot, MIDI)` (S082). Dropped while a
  load runs. `RXF P` governs both PC and CC0; there is no separate
  Bank-change enable (F37).
- **PC during a load:** dropped.
- **PC while Scene 17 is active:** a PC on G is a whole-Scene change, so it
  releases 17. A PC on a voice channel moves only those tracks; 17 stays.

### 3.8 Realtime and transport

| Message | Behaviour |
|---------|-----------|
| Clock | Existing. |
| Start | Restart from the Pattern start. |
| Continue | Resume from the current position. |
| Stop | Existing. |
| SPP | While stopped only: sets the master tick (SPP × 24); tracks re-project through the master-clock anchors (`PATTERN_DYNAMIC_STACK.md` §6.5). |
| MTC | Existing. |
| Start/Stop with `snc = off` | Ignored. |
| MMC | Later. |

### 3.9 Transmit

- **N:**
  - notes go to (`ec(t)`, inverse-mapped note); no TX when G is off and the
    track is unassigned;
  - per-track note-off cache (7 B, replacing the 16 + 2 B channel cache).
- **R:** clock while master; Start/Continue/Stop.
- **C:** locally made changes (knob/encoder; not step-automation playback)
  go out as the parameter's Learn CC (at most one, matched on the current
  type), else as NRPN on G or the voice channel.
  - Changes that came in over MIDI are never echoed.
  - Coalesced per target; NRPN number CCs are resent only when the number
    changes (per port).
  - Throttled: a full NRPN is ~12 B, about 3.8 ms on DIN, and the normal
    DIN FIFO is 64 B.
- **P:** PC on Scene change (whole Scene on G, per track on the track's
  channel). CC0 on a Bank load. None when the relevant channel is off.

### 3.10 Menu

- `ch` 0..16, 0 shown as `off`. Uses `DTYPE_1B16` + a static-param special
  case (the dtype space is full).
- Track `chn`: 0 = unassigned (`glb`), 1..16; the default changes from
  track+1 to 0. Track `nte`: 0 stays `def` (MIDI note 0 cannot be a root).
- `MRT` (3 characters):

  | Value | Routes |
  |-------|--------|
  | `off` | none |
  | `UtD` | USB → DIN |
  | `DtD` | DIN → DIN |
  | `DtU` | DIN → USB |
  | `bth` | USB → DIN + DIN → USB |
  | `DtA` | DIN → DIN + DIN → USB |
  | `AtD` | USB → DIN + DIN → DIN |
  | `all` | USB → DIN + DIN → DIN + DIN → USB |

- Retire `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_MODE` as no-op slots (the
  mirror precedent; no renumbering).

### 3.11 Storage (no migration, standing rule)

- `settings.cfg`: `midi_chan_global` gains 0; `midi_routing` changes
  meaning; Learn lines are added.
- Scene: `midi_channel` gains 0 = unassigned.
- Docs at close: `FILESYSTEM_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`,
  `PATTERN_DYNAMIC_STACK.md` §6.2a, `MODULE_INTERCHANGE_SPEC.md`,
  `STORAGE_SRAM_MANIFEST.md`, and the stale MEMORY.md MIDI reminders.
  AutoSave and copy/clear are untouched by MIDI (Scene 17 is covered in §4).

---

## 4. What MIDI needs from S082 (built; see §13)

The background Bank load and Scene 17 were decided in S082 (D1–D53) and are
**built**; the decisions are in `082_SESSION_HANDOFF_LOG.md` §4–§6, the
mechanism in `MODULE_INTERCHANGE_SPEC.md` "Background Bank load (S082)". For
MIDI (status per row in §13.1):

| Need | Where |
|------|-------|
| Foreground, non-blocking `preset_requestBackgroundBankLoad(slot, origin)` returning accepted/refused | S082 §4.1 |
| Origin MIDI performs the UI switch to Load/Save → Bank at the slot | S082 D7, §4.1 |
| Completion/failure signal, so MIDI can send CC0 TX | S082 §4.5 |
| A "load running" query, so PC and CC0 can be dropped (D13) | S082 §4.1 |
| Scene index 16 resolves through Scene/Pattern accessors: note map, Learn type matching and MIDI writes act on Scene 17 while it is active | S082 §5 |
| Scene 17 has no PC number: no PC TX at the switch | S082 §4.3 |
| `settings.cfg` (Learn lines) keeps writing while on 17 | S082 Q8 |

Non-S082 prep before S083: answers to §12 (done); RAM approvals (Learn ~519 B,
held-note 7 B, NRPN state ~48 B, TX caches ~8 B, CC2 1 B); the S081
hardware lists passing (`081_SESSION_HANDOFF_LOG.md` §13.2, R21; MIDI
recording builds on LiveRecord).

---

## 5. S083 phases

| Phase | Content | Verify |
|-------|---------|--------|
| A | Cleanup: legacy CC map, `midi_MidiChannels[0..6]`, `midi_NoteOverride`, `midiParser_originalCcValues`, dead helpers. G off; track channel unassigned. SPP/Song Select framing; USB TX CIN. Consume-then-route; new MRT. | Build; MRT matrix (DIN loopback + DAW); clock sync unchanged. |
| B | Note map (§3.3), N5, pitch clamp, note-off, overlays + CC2, LiveRecord. Measure foreground note latency (F25). | Host table test of every N-case; hardware note matrix. |
| C | Transport: Continue, SPP, `snc = off` rule. | DAW start/stop/continue/locate. |
| D | PC (Scene, per track); CC0 via S082. | Running/stopped; CC0 bursts; PC held during a load. |
| E | NRPN RX, value clamp + Morph translation, CC1 per voice, Scene/Effect write paths, fan-out, `liveRec_paramIn` targets, LCD refresh. | NRPN sweep of every ID per type; recording; G-1. |
| F | Learn: table, capture gesture, `settings.cfg` lines, type matching. | Learn/replace/full/cancel; hand-deleted line; reboot. |
| G | TX: note-off cache, parameter TX, coalescing, PC/CC0 TX, echo suppression. | DAW records LXR output; DIN FIFO stress with clock running. |
| H | Docs and memory. | — |

---

## 6. Resource estimate (MIDI only; S082's Scene 17 allocations are in `082_SESSION_HANDOFF_LOG.md` §7)

| Item | Bytes | Region | Status |
|------|------:|--------|--------|
| Learn table + state (128 × 4 B + count + capture), owner `MidiControl` | ~519 | SRAM1, static | **Accepted 2026-10-17** |
| Held note per track, owner `MidiControl` | 7 | SRAM1, static | **Accepted 2026-10-17** |
| TX note-off per track (replaces 18 B) | 7 | SRAM1 | Net −11 |
| NRPN RX state (8 contexts × 2 ports × 3 B), owner `MidiControl` | ~48 | SRAM1, static | **Accepted 2026-10-17** |
| NRPN TX cache + coalesce, owner `MidiOut` | ~8 | SRAM1, static | **Accepted 2026-10-17** |
| CC2 state, owner `MidiControl` | 1 | SRAM1, static | **Accepted 2026-10-17** |
| Retired legacy arrays | −269 | SRAM1 | Release does not authorise reuse |
| Flash | ~+6..10 KB new, ~−4 KB legacy map removed | Flash (155,124 B free at S082 close) | OK |

The user acknowledged and accepted these SRAM allocations on 2026-10-17,
under the MEMORY.md RAM Allocation Approval Policy. Record the measured
sizes in `STORAGE_SRAM_MANIFEST.md` at implementation. Any allocation larger
than listed here needs a new acknowledgement.

---

## 7. Risks

| Id | Risk | Mitigation |
|----|------|-----------|
| R1 | Scene 17 is a 17th Scene in code that assumes 16 everywhere. This is the largest S082 risk, and the MIDI note map, Learn and writes depend on it resolving correctly. | **S082:** the index-16 audit was done (every site has a rule) and a missed class was found and fixed (cached Scene indices, EffectsManager). Not hardware-verified for MIDI paths. New MIDI code that caches a Scene index must follow playback onto 17 (§13.3). |
| R2 | While Scene 17 exists, AutoSave is off (S082 D11), so edits are not protected against power loss until a Scene change. | Accepted by design. |
| R3 | A stray CC0 from a DAW replaces all 16 Scenes. Unsaved edits are discarded by design. | Accepted: turn `RXF P` off to block both PC and CC0 (F37). |
| R4 | The note map is per Scene, so a Scene change can remap a DAW drum track. | Accepted (F12); edit-mask fan-out keeps Scenes equal. |
| R5 | DIN bandwidth with NRPN TX during knob sweeps. | Coalescing, number cache, throttle. |
| R6 | Feedback loops with DAW echo (CC/NRPN fights, PC re-sends). | Never echo MIDI-originated changes; document DAW setup. |
| R7 | NRPN numbers are instrument-agnostic by design (C3): the same NRPN means a different parameter on another type. Learn avoids this through its type tag. | Publish one NRPN chart per instrument and Effect type. |
| R8 | Foreground latency delays MIDI notes and LiveRecord placement. | Measure in Phase B (F25). S081 Q3 left MIDI note timestamps (processing time today) and queue-skip priority for realtime MIDI to this rework (§14). |
| R9 | A note-on forwarded, then its note-off consumed after a map change (or the reverse), leaves a hanging note downstream. | Accept. |
| R10 | Per-track playback: MIDI writes and Learn type matching use the active Scene, while a track may play another Scene. | Accepted (F5). |
| R11 | The settings writer index and the `lines` count grow (≤152), and a learn line is the longest settings line. | Check the `op_line_buf` size and the writer's index width in Phase F. |
| R12 | Removing the legacy map may expose hidden internal callers (`preset_applySoundParameter()` → `midiParser_ccHandler()`). | Phase A caller audit first. Include `preset_applySoundParameter(…recordAutomation)` and the STEP armed-automation remnant left by S081. |
| R13 | P4 cannot be verified until S082 is hardware-proven. | Phase D after the RX phases. **S082 status:** audio during a background load confirmed by the user (P2); the P1 matrix and all of P3 are untested, so run those checks before Phase D. |

---

## 8. Remaining ambiguities

All resolved; the remaining questions are in §12 and in `S082_P1_LOAD_SAVE.md` §7.

---

## 9. Architectural decisions (settled)

| Topic | Decision |
|-------|----------|
| Address space | One 9-bit ID space for automation, LiveRecord, NRPN and Learn. |
| Values | Absolute 7-bit, instrument-agnostic; Morph translation only. |
| Learn | Global, 128 type-tagged entries in `settings.cfg`; no fan-out, no Scene storage, no UI remove. |
| Note map | Per Scene, resolved on the fly. |
| MIDI writes | Active Scene, edit-mask fan-out, lowest priority (§12 Q1: under S081 R12). |
| Bank change | Background load through Scene 17; UI stays on Load/Save. |
| Routing | Consume first, then forward; realtime always forwarded per MRT. |

---

## 10. Out of scope

External MIDI sequencing tracks (§6.8), one-shot LFOs (§6.2), MMC, sysex
dump/restore, MIDI 2.0, multi-cable USB.

---

## 11. Decision log (2026-10-16/17)

| F | Outcome |
|---|---------|
| F1 | Canonical 9-bit IDs. |
| F2 | Scene target block only; per-voice Scene targets allowed on the voice channel. |
| F3 | Absolute numbering on voice channels. |
| F4 | **Absolute values**, instrument-agnostic, no special casing; Morph uses its 7-bit translation. |
| F5 | Fan out; active Scene. |
| F6 | **Changed:** Learn is global in `settings.cfg`, 128 entries, type-tagged (§3.6). |
| F7 | One CC → several targets; each is one of the 128 entries. First match on TX. |
| F8 | **Changed:** SHIFT+PLAY+REC+parameter, `LRN`, flashing, cancel with any of the three, no UI remove, re-learn may replace. |
| F9 | Not needed (no fan-out). |
| F10 | Not needed; applies device-wide by voice and type. |
| F11 | Yes. |
| F12 | **Channel and note stay per Scene.** |
| F13 | Yes. |
| F14 | Yes. |
| F15 | Yes. |
| F16 | Keep stripe from note 0. |
| F17 | Yes. |
| F18 | Banks 000..127 only. **The UI stays on Load/Save.** No separate Bank enable (F37). |
| F19 | **Background load design, now `S082_P1_LOAD_SAVE.md`.** |
| F20 | Discard (fastest); AutoSaves are stale after a Bank load. |
| F21 | Yes. |
| F22 | **`off UtD DtD DtU bth DtA AtD all`.** |
| F23 | Yes. |
| F24 | Yes. |
| F25 | Yes. |

Second round (2026-10-17):

| F | Outcome |
|---|---------|
| F26 | Accepted (CC6 value, CC38 ignored, CC96/97 ±1, clamp to max). |
| F27 | **One CC → many parameters (one row each); one parameter → at most one CC.** Re-learning a parameter replaces its CC and channel. |
| F28–F36 | Accepted (F36: 4 B entries, ~519 B). |
| F37 | **`RXF P` governs both PC and CC0.** No `bnk` setting. |
| F38–F48 | Moved to `S082_P1_LOAD_SAVE.md` as decisions D1–D16. Changes from the recommendations: no deferral, AutoSave and pattern operations are dropped (F39); T6 is kept when T7 does not fit (F40); no chain on 17, it loops itself (F42); PC and CC0 during a load are dropped (F46); a running copy/clear is dropped or completed (F47); 17 is released only by a Scene change, never by per-track changes (F48). |

---

Third round (2026-10-17):

| F | Outcome |
|---|---------|
| F49 | Accepted: re-learning a parameter on a different channel kind takes the new channel kind. |
| F50 | Accepted: a learned CC applies only to its entry's slot, even on a shared voice channel. CC1 is the only CC that reaches every voice on a shared channel. |
| F51 | Accepted: while Scene 17 is active, Learn type matching uses Scene 17's slot types (captured at the copy). |
| S082 Q1–Q8 | Accepted as recommended; recorded as D17–D24 in `S082_P1_LOAD_SAVE.md`. |

---

## 12. Open questions

None at revision 4. Questions that come up during implementation go here.

**Q1 (from the S081 close-out, 2026-10-21): does a MIDI parameter write take
the R12 menu priority?** C9 and `MIDI_SPEC.md` §14 put MIDI last. S081 R12
moved menu edits ahead of Instrument automation until the voice's next
trigger, so "automation, then menu, then MIDI" no longer describes one
order.

- (a) **Keep MIDI lowest.** MIDI stores the Normal endpoint only. On a
  parameter that automation holds, a CC or NRPN move is inaudible until the
  voice's next trigger, and the next automated step overwrites it.
- (b) **MIDI acts like a menu edit.** For Instrument targets, MIDI also
  calls `seq_menuOverrideSet()`, so the move sounds at once and holds until
  the voice's next trigger, a transport change or a Scene change. Scene and
  Effect targets already behave this way (R12/R13: the value applies until
  their next automation).

Recommendation: (b). A Learned CC is a remote knob, and (b) makes it sound
like the knob. It needs no RAM; the override bitmap exists. It changes the
`MIDI_SPEC.md` §14 binding rule, so it needs the user's decision before
Phase E.

---

## 13. S082 outcome notes (2026-10-21)

What Session 082 built, and how it changes this plan. Nothing here reopens
a decision; it records facts, API names and new constraints.

### 13.1 Status of the §4 needs

| Need | Status |
|------|--------|
| Non-blocking request returning accepted/refused | Built: `preset_requestBackgroundBankLoad(slot, act, empty, origin)`. For `PRESET_BANK_ORIGIN_MIDI` the masks are ignored (Replace, all 16, Q15). A MIDI request is **dropped** while a load is in flight (D13); otherwise it becomes the one pending request (latest wins) and starts when Preset, the facade and the Menu are idle (D33). A Menu Bank OK discards a pending MIDI request. |
| MIDI origin switches the UI to Load/Save → Bank | Built: closes every latched surface and session (D39), enters Load:[Bank] at the slot, waits for its preview, then acts as OK; a missing slot ends FAILED. |
| Completion/failure signal for CC0 TX | Built: `preset_takeBackgroundBankLoadResult()` returns `PRESET_BG_RESULT_DONE` (1) / `FAILED` (2) (code mask 3) with `PRESET_BG_RESULT_MIDI` (0x80) once per load. Child failures inside a DONE load still count as DONE. Phase G polls it. |
| "Load running" query | Built: `preset_backgroundBankLoadRunning()` (phases 1–6), plus `preset_backgroundBankLoadUnlocked()` / `Locked()` (§13.2). |
| PC/CC0 dropped during a load | **Already enforced** by `midiParser_acceptedDuringBankLoad()`: while a load runs only realtime, note on/off and CC except CC0/CC32 act; PC, pitch bend, pressure, MTC QF and song position/select are dropped. |
| Scene index 16 resolves through the accessors | Built (`scene_get*()`, `pat_regionFor()`); Scene 17's edit mask is itself only (`bank_editTargetMask()` = bit 16). |
| No PC TX for Scene 17 | Built: `seq_sendProgChg()` refuses index ≥ 16. |
| `settings.cfg` keeps writing on 17 | Unchanged (D24): Learn lines are safe during a load. |

### 13.2 Changes to the plan's assumptions

1. **P4 "the UI stays on Load/Save afterwards" is amended (S082 P3, D40).**
   - From phase 5 of a load that runs on Scene 17, or of a direct load with the transport running, the panel unlocks: the user may leave Load/Save while the load finishes.
   - The "stays there" completion applies only when the user is on Load/Save at completion. A MIDI-origin load follows the same rule.
   - A load started with the transport stopped keeps the UI on Load:[Bank] until it ends.
2. **The active Scene can change during a load.** After the unlock the user may switch from Scene 17 to any loaded (available) Scene, and between loaded Scenes. The note map (§3.3), Learn type matching (§3.6) and MIDI writes (§3.5) must therefore read the active Scene at the time of each message, never a value cached at load start.
3. **Fan-out during a load (C9, §3.5):** MIDI writes must fan out through `bank_editTargetMask()` (or `bank_sceneFanoutMask32()`), not the raw edit-mask entry.
   - Those helpers return Scene 17 alone while 17 is active.
   - During an unlocked load they exclude Scenes the load has not finished (`bank_load_unavailable_mask`).
   - `preset_setInstrumentParameterFromMidi()` has no fan-out today; Phase E adds it through these helpers.
4. **Consume-then-route (§3.2, Phase A).** The S082 drop filter sits **after** the current thru routing in `midiParser_parseMidiMessage()`, so a dropped message is still forwarded. In the new dispatcher a message dropped by the filter must count as "not consumed" and be forwarded per MRT. Keep the filter's message classes exactly.
5. **PC on G during and after a load (§3.7):**
   - dropped during the load by the filter;
   - afterwards it is a normal whole-Scene change (and releases Scene 17);
   - chain conditions do not evaluate during any Bank load (S082 D50), so a PC is the only external Scene change then.
6. **Per-track PC (P3) while 17 is active:** a voice-channel PC moves those tracks to a resident Scene without releasing 17 (D10). During an unlocked load the target must be available; S082's panel gesture refuses unavailable Scenes, so the MIDI path should use `bank_sceneAvailable()` too.
7. **MIDI-origin request timing (Phase D):** CC0 RX calls the request and returns; nothing blocks. A CC0 burst is coalesced by the pending slot (latest wins) and dropped while a load runs.
8. **The CC2 overlay, note triggers and live record keep working during a load.** Notes and CCs pass the filter; live record follows the played Scene (Scene 17 included, D35).

### 13.3 New risks and constraints

| Id | Risk / constraint | Mitigation |
|----|-------------------|-----------|
| R14 | A MIDI module that caches a Scene index (for example the note map's resolved Scene, or a Learn lookup cache) would keep reading the home Scene after the Scene 17 switch, or a Scene the user left. This is the class S082 P2-1 found in EffectsManager. | Resolve the active Scene per message; if anything is cached, rebind it at the switch (pattern: `effects_rebindSourceScene()`). |
| R15 | NRPN/Learn writes into a Scene the load is replacing would be lost or race the loader. | Fan out only through the availability-aware helpers (§13.2 item 3); the active Scene is always available. |
| R16 | S082 P3 A1 (open): a queued Scene change armed before a direct (Merge, running) load whose target the load replaces still applies its qac mute/morph parts when it fires, although the change itself is dropped. A MIDI-driven queue would inherit it. | Decide/apply the one-line A1 fix before Phase D. |
| R17 | Hardware: S082 P1 (Scene 17 build, rebuild, MIDI-origin entry, the AutoSave abort allow-list) and all of P3 are untested. | Run the S082 checks (`082_SESSION_HANDOFF_LOG.md` §11.2) before Phase D. |
| R18 | The DEV trace flush is suppressed while a Load/Save command runs, so a MIDI-started load's records stay in RAM until the next flush. | Copy the card only after the command ends; P2-5 runtime Bank Load records are still unbuilt. |

### 13.4 Resources

S082 used Scene 17 1,658 B, P0 16 B, P1 93 B and P3 4 B of SRAM1. Flash
headroom at the S082 close is 155,124 B. The §6 MIDI allocations (Learn
~519 B, held note 7 B, NRPN ~48 B, TX ~8 B, CC2 1 B) remain as approved on
2026-10-17; re-check them against the S082 close numbers in
`STORAGE_SRAM_MANIFEST.md` when Phase F/G lands.

---

## 14. S081 outcome notes (2026-10-21)

What Session 081 (live record, logged in `081_SESSION_HANDOFF_LOG.md`)
changed for this plan. Nothing here reopens a decision except §12 Q1.

### 14.1 Recording hooks (§3.4, §3.5)

- **Two call sites (R11).** `midiParser_noteOn()` builds a
  `liveRec_trigger_t` (SINGLE, `LIVEREC_SRC_MIDI`) and calls
  `liveRec_triggerIn()`. `midiParser_enterTaggedParameter()` calls
  `liveRec_paramIn(0xFFFF, LIVEREC_SRC_MIDI)`, which is a stub. Phase E
  passes the real 9-bit ID (the CC → target mapping is this session's job).
- **`liveRec_paramIn()` has no value argument.** It reads the stored value:
  the Instrument Normal endpoint, `scene_get*()` for Scene targets, and the
  retained Normal through `effects_storeValue7()` for Effect rows. Call it
  only after the store, on the Scene the store wrote.
- **Ownership** (`liveRec_targetOwned()`; T = the played Scene of `rtk`,
  A = the active Scene):
  - **Instrument parameter of slot s:** recorded only if the track playing s
    plays T and the type in A equals the type in T.
  - **Scene and Effect targets:** recorded only if T == A.
  - With per-track playback (R10), a MIDI write lands on A but may not be
    recorded. That is accepted; the drop is silent.
- **First-change rule.** `LIVEREC_AUTOMATION_FIRST_CHANGE` (default 1)
  writes a parameter's first change in a step at once. LATCH latches and
  dirty bits work as for knob edits.
  - A dense CC stream costs one `liveRec_paramIn()` call per message.
  - Measure this in Phase E against a fast controller sweep.
- **Morph (C7, G-1).**
  - CC1 on G goes through `preset_morph()` and must never call
    `liveRec_paramIn()`.
  - CC1 on a voice channel is an individual Morph edit. It stores the
    voice's Morph amount, then calls `liveRec_paramIn()` with
    `sceneModTarget_voiceMorphId(slot)`. LiveRecord captures that value in
    `liveRec_morphValue[]`, so a later global Morph change cannot leak into
    the take.
  - S081 G-1 hardware check 4 (CC1 on G during LATCH records nothing)
    belongs in the Phase E tests.
- **Loop while record (Q2).** A MIDI hit recorded onto a replayed step
  sounds again on the next loop pass. Accepted in S081.

### 14.2 Triggers and reset (§3.1, §3.4)

- **Reset at each press.** A MIDI note is a live trigger, so each note-on
  resets the voice's held automation and ends its R12 override. Scheduled
  steps reset only at their first trigger. The reset travels in the
  per-voice mask `voiceControl_pendingReset`, not in the queue entry.
  The reset therefore survives `voiceControl_noteOff()`, which deletes the
  voice's queued entries.
- **Ramps (S081 P2 D3).** A resetting trigger, MIDI included, does not
  restore a parameter that an active `asm` ramp holds.
- **Legacy writes during a ramp.** With writes-on-change, an internal legacy
  MIDI write holds until the ramp's value next changes. Removing the legacy
  map in Phase A also removes that bypass of the priority rule.
- **Roll table.** CC2 overlay rolls use the S081 96-PPQ roll table
  (`off 1/8 8tr 16 16t 32 32t 64 64t 128`). `PAR_ROLL` is retired.

### 14.3 Scene changes, takes and per-track PC (§3.7, P3)

- **New take.** A whole-Scene change starts a new live-record take and
  clears every R12 override. It goes through `seq_selectActivePattern()`
  or `seq_alignActivePatternToScene()`, which bump `seq_sceneEpoch`. A MIDI
  PC on G is such a change.
- **Per-track PC (P3) must realign.** The per-track epoch bump is in
  `seq_realignTrackToMasterClock()`, not in `seq_setTrackPlayedScene()`. The
  code comment there says to move the bump if a second caller appears
  without a realign. The MIDI per-track PC is that second caller. It must
  call `seq_setTrackPlayedScene()` then `seq_realignTrackToMasterClock()`,
  as the panel does, or move the bump. Otherwise LiveRecord keeps the old
  take across the change.
- **Scene 17.** The switch keeps the epochs, so a take continues onto 17
  (S082 D35). Recorded steps on 17 are lost at its release unless the
  Scene is saved from 17 (`082_SESSION_HANDOFF_LOG.md` §13 item 8).

### 14.4 Risks and constraints

| Id | Risk / constraint | Mitigation |
|----|-------------------|-----------|
| R19 | §12 Q1 is undecided, so the write path's override call cannot be finalised. | Decide before Phase E. |
| R20 | A MIDI per-track PC without a realign starts no new take (§14.3). | Use the panel sequence, or move the epoch bump into `seq_setTrackPlayedScene()`. |
| R21 | S081 hardware lists are unrun: the live-record matrix, G-1 checks, ramps and the 64-ramp CPU worst case (`081_SESSION_HANDOFF_LOG.md` §13.2). MIDI recording builds on them. | Run the S081 lists before Phase E. |
| R22 | A MIDI note's recorded position uses processing time (S081 Q3), not arrival time. | Phase B timestamps (R8). |

### 14.5 Resources

S081 closed at text 573,296, data 416, bss 429,112, which is the baseline
S082 built on. S081 itself used +7 B of SRAM1 for G-1
(`liveRec_morphValue[7]`). None of the §6 MIDI allocations changes.
