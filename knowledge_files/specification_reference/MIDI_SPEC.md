# MIDI Specification

> **STATUS: DRAFT — WORK TO BE IMPLEMENTED (planned for Session 083).**
>
> This document specifies MIDI behaviour that is **not yet built**. As of
> 2026-10-17 the firmware does **not** behave as described here. For current
> behaviour, read `Core/MIDI/MidiParser.c`; §15 lists the gaps.
>
> Until implementation, this document is the agreed **target**, not an
> as-built reference. The usual rule "where this document and the code
> disagree, the code wins" does **not** apply yet. When S083 implements it,
> replace this banner with a "Current as of" line, close §15, and from then
> on treat the code as authoritative.

The LXR-02 MIDI interface: global MIDI settings, the note-to-track map,
control changes (NRPN, reserved CCs, MIDI Learn), program and Bank change,
realtime and transport, routing, and transmit. Written for the developer
who implements it, and later maintains it, in `Core/MIDI/`.

- **Decided:** 2026-10-16/17, during Session 082 planning. All behaviour
  below is a user decision unless marked *(proposal)*.
- **Planning record:** `S083_P1_MIDI_IMPLEMENTATION_PLAN.md` (requirements,
  phases, risks, decision log F1–F51). The background Bank load and Scene 17
  are in `S082_P1_LOAD_SAVE.md` (D1–D24).
- **Related documents:**
  - `PATTERN_DYNAMIC_STACK.md` §6.2a (write priority) and §4 (9-bit target
    IDs);
  - `BANK_PRESET_ARCHITECTURE.md` (Scene settings, Morph, edit masks);
  - `FILESYSTEM_SPEC.md` (`settings.cfg`, `sceneset.scg`);
  - `MODULE_INTERCHANGE_SPEC.md` (MidiParser, PERF surface);
  - `STORAGE_SRAM_MANIFEST.md`.

## Contents

1. Terms
2. Settings
3. Receive pipeline: filtering, consumption, routing
4. Notes
5. Control changes
6. MIDI Learn
7. Program change and Bank change
8. Realtime and transport
9. Transmit
10. Scene 17
11. Storage
12. Code ownership and contexts
13. RAM
14. Binding rules
15. Implementation status (gaps against current code)
16. Maintaining this document

---

## 1. Terms

| Term | Meaning |
|------|---------|
| Track | Sequencer track T1..T7. |
| Voice / slot | Instrument slot V1..V6. T6 and T7 both play V6 (closed/open HiHat). |
| G | The global MIDI channel (`ch`), 1..16, or off. |
| Track channel | A track's MIDI channel (`chn`, per Scene): 1..16, or unassigned. |
| Effective channel `ec(t)` | The track channel if assigned, else G. With G off and the track unassigned, the track has no MIDI channel. |
| Voice channel | A channel that is some track's effective channel and is not G. |
| Root | The lowest note of a track's note range. It plays the voice's default pitch. |
| Internal note | The note value the voice receives: 63 = default pitch; pitch offset = note − 63 semitones (`Oscillator.c`). |
| Target ID | The canonical 9-bit parameter ID shared with step automation and LiveRecord (§5.4). |
| Consumed | The LXR acted on a received message (§3.2). |
| Active Scene | The Scene being viewed and written to (MEMORY.md definition). |
| Scene 17 | The background Scene that keeps playback going through a Bank load (§10). |

---

## 2. Settings

### 2.1 Global (MENU_MIDI_PAGE; `settings.cfg`)

| Cell | Key | Values | Meaning |
|------|-----|--------|---------|
| `snc` | `ext_sync` | `off usb din pls aut` | Sync source. MIDI realtime is used for `usb`, `din` and `aut` (AUTO priority, low to high: internal, USB, DIN, jack pulse). |
| `ch` | `midi_chan_global` | `off`, 1..16 | G. With `off`, nothing is received or sent on a global channel. |
| `TXF` | `midi_filt_tx` | 16 combinations of N R C P (`off N R RN … all`) | What the LXR **sends** from itself (§9). It never affects pass-through. |
| `RXF` | `midi_filt_rx` | same | What the LXR **consumes** (§3). |
| `MRT` | `midi_routing` | `off UtD DtD DtU bth DtA AtD all` | Pass-through routing (§3.3). |

Filter letters:
- **N** notes;
- **R** realtime (clock and transport);
- **C** control (NRPN, Learn CCs, CC1, CC2; on TX, the learned CC if one
  exists, else NRPN);
- **P** program change and Bank change (CC0).

`RXF P` governs both PC and CC0; there is no separate Bank-change enable.

### 2.2 Per track, per Scene (STEP track settings; `sceneset.scg`; AutoSave Scene cells)

| Cell | Values | Default |
|------|--------|---------|
| `chn` | `glb` (0 = unassigned, follows G), 1..16 | `glb` |
| `nte` | `def` (0 = unassigned), 1..127 | `def` |

MIDI note 0 cannot be a track's root: 0 means unassigned. Channel and note
are Scene settings, so the note map can change on a Scene change.

---

## 3. Receive pipeline

### 3.1 Order

1. Frame the message from its port (DIN byte stream, or USB event packet).
2. If its type is blocked by `RXF`, it is not consumed.
3. Otherwise offer it to the consumers (notes §4, CCs §5–§6, PC/CC0 §7).
4. If no consumer acted, forward it according to `MRT` (§3.3).

Realtime messages (clock, Start, Continue, Stop) take a separate,
timestamped path (§8). They are **always** forwarded according to `MRT`,
whether or not they were consumed.

### 3.2 Consumed

A message is consumed when it is:
- a note that resolved to a track (§4.1);
- an NRPN protocol CC (6, 38, 96..101) on G or a voice channel, while
  `RXF C` is on;
- CC0, CC1 or CC2 that acted;
- a CC that matched a Learn entry;
- a PC 0..15 that acted.

Everything else (unmapped notes, CCs on unused channels, PC 16..127,
sysex, other messages) is forwarded.

### 3.3 Routing (`MRT`)

| Value | Routes |
|-------|--------|
| `off` | none |
| `UtD` | USB in → DIN out |
| `DtD` | DIN in → DIN out |
| `DtU` | DIN in → USB out |
| `bth` | USB → DIN and DIN → USB |
| `DtA` | DIN → DIN and DIN → USB |
| `AtD` | USB → DIN and DIN → DIN |
| `all` | USB → DIN, DIN → DIN, DIN → USB |

USB → USB is never routed. Forwarded system-common and sysex messages must
be re-framed correctly for USB (cable 0, correct code index).

---

## 4. Notes

### 4.1 Note → track map

The map is computed from the active Scene's track settings and G. For an
incoming note (channel c, note n):

- **S** = the tracks whose effective channel is c, in track order. A track
  set explicitly to G's channel number counts as a G track.
- **A** = the tracks in S with an assigned note; **U** = the rest.

**Rule 1 — assigned ranges (any channel).** Sort A by note. Each assigned
track owns the notes from its own note up to one below the next assigned
note, or up to 127 for the last one. Internal note = 63 + (n − its note).
If two tracks have the same note, the lower-numbered track owns the range
and the other owns nothing.

**Rule 2 — the region below the lowest assigned note** (all of 0..127 if A
is empty):

- **c = G (stripe):** note n triggers `U[n mod |U|]` at internal note 63.
  Every note in the stripe plays the default pitch.
- **c ≠ G (split):** the region is divided into |U| contiguous blocks, in
  track order. Block sizes differ by at most one, and the lower tracks get
  the extra notes. Each block starts at its track's root. Internal note =
  63 + (n − block start).
- **U empty:** the region is unmapped (not consumed, so forwarded).

**Rule 3 — one voice per note.** A note triggers at most one track.

**Rule 4 — pitch clamp.** The internal note is clamped to 127, so a range
rises at most +64 semitones above its root. Higher notes in the range play
at +64.

Tracks with their own channel never take part in G's stripe. With G off,
the G rules do not apply and unassigned tracks are unmapped.

### 4.2 Worked examples

| Setup | Result |
|-------|--------|
| G = 10; no track settings | Ch 10: note 0 → T1, 6 → T7, 7 → T1, … 36 → T2, 60 → T5. All at default pitch. |
| G = 10; T3 `nte` 48, T5 `nte` 60 | Ch 10: notes 0..47 stripe over T1 T2 T4 T6 T7 (n mod 5). 48..59 → T3, +0..+11 semitones. 60..127 → T5, +0..+64 (notes 124..127 all +64). |
| T1, T2, T3 `chn` 2; no notes | Ch 2: T1 0..42 (43 notes), T2 43..85 (43), T3 86..127 (42). Note 50 → T2, +7. |
| Ch 2: T1 (no note), T2 `nte` 36, T3 (no note), T4 `nte` 60 | Below 36: T1 0..17, T3 18..35 (each chromatic from its root). T2 36..59. T4 60..127. |

### 4.3 What a note does

- **Note-on:** one trigger of the resolved track at the note's velocity,
  through the trigger funnel (`voiceControl_noteOn()`), with the internal
  note as pitch. A muted track does not sound.
- **Overlay action:** when the overlay type is BAR `<` or BAR `>` (selected
  by CC2 §5.3, or by a physically held BAR in PERF), the note performs that
  overlay's action instead (S080 overlay: 12 trigger types). It plays at
  the note's resolved pitch, not the overlay's note setting. Roll types
  repeat until that note's note-off.
- **Note-off:** stops an overlay roll. A single trigger ignores it.
- **Recording:** each trigger is reported to LiveRecord (`liveRec_triggerIn()`,
  source MIDI), with the overlay kind where one applies. Note pitch is
  recorded like an overlay trigger's.

---

## 5. Control changes

### 5.1 Reserved CCs (never learnable)

| CC | Use |
|----|-----|
| 0 | Bank change (§7.2) |
| 1 | Morph (§5.2) |
| 2 | Note action / overlay type (§5.3) |
| 6, 38 | Data entry MSB/LSB (NRPN) |
| 32 | Bank select LSB (reserved, unused) |
| 96, 97 | Data increment/decrement (NRPN) |
| 98, 99 | NRPN number LSB/MSB |
| 100, 101 | RPN number LSB/MSB |
| 120..127 | Channel mode messages |

Every other CC (3..5, 7..31, 33..95, 102..119) may be learned (§6).

### 5.2 CC1 — Morph

- **On G:** the global Morph (`mrp`), which sets all six voice Morph
  amounts. **Never recorded** (G-1, binding).
- **On a voice channel:** the Morph amount of every voice whose track has
  that effective channel. It is recorded as an individual voice Morph edit
  (`1VM`..`6VM` path, `liveRec_morphValue[]`).
- **Value:** 0..126 → value × 2; 127 → 255 (the existing 7-bit Morph
  translation).

### 5.3 CC2 — note action

On G only, in any UI mode. Values: 0 = single trigger (default), 1 = BAR `<`
overlay, 2 = BAR `>` overlay. The value persists until the next CC2; other
values are ignored. The trigger types are the PERF overlay settings
(`perf_btn_lt`, `perf_btn_rt`).

### 5.4 NRPN — parameter address space

**NRPN number = the canonical 9-bit target ID.** CC99 = ID >> 7, CC98 = ID &
0x7F.

| IDs | Targets |
|-----|---------|
| 0..383 | Voice parameters: `slot × 64 + descriptor index` (slot 0..5) |
| 384..447 | Scene targets: `1vm..6vm`, `7dc`, `1ou..6ou`, `1fx..6fx`, `fxm` 404, track length 405..411, scale 412..418, shuffle 419..425; 426..447 unused |
| 448..510 | Effect parameters: Effect-local index 0..62 |
| 511 | Off (ignored) |

- **Instrument-agnostic.** A voice ID addresses whatever descriptor sits at
  that index in the slot's current instrument type. An Effect ID addresses
  the current Effect type's parameter. The same NRPN means different
  parameters on different types; the NRPN chart is published per
  instrument type and per Effect type. There is no special casing.
- **Unused IDs** are ignored and the message is forwarded.
- **Channels:** every ID works on G. On a voice channel, only that voice's
  IDs work: its voice block and its per-voice Scene targets (`Nvm`, `Nfx`,
  `Nou`, and the track length/scale/shuffle of its tracks). Numbering is
  absolute everywhere. Other Scene and Effect IDs are ignored on voice
  channels.
- **Protocol** (state per port and channel):
  - CC99/98 select the number;
  - CC6 carries the value;
  - CC38 is ignored;
  - CC96/97 step the current value ±1;
  - RPN null (CC101/100 = 127/127) deselects.

### 5.5 Values

All MIDI parameter values are **absolute 7-bit** and pass unchanged to the
parameter (identity, as in step automation). They are clamped to the
parameter's maximum, the same rule step automation applies. The only
translation is the Morph amounts (§5.2), which use the 7-bit Morph mapping.

### 5.6 Write rules

- **Priority:** step automation, then menu edits, then MIDI (S075 F3).
  Voice parameters store the active Scene's Normal endpoint; the Morph sweep
  applies it and skips a parameter that automation holds. MIDI never writes
  the runtime value directly.
- **Scope:** the active Scene, fanned out through the VOICE edit mask
  exactly like a knob edit. A track playing another Scene (per-track
  playback) is not written.
- **Recording:** each successful store is reported with
  `liveRec_paramIn(targetId, MIDI)`. Global Morph is never recorded.
- **Display:** a visible parameter changed over MIDI repaints promptly
  (rate-limited), through `menu_notifyExternalParamChanged()`.
- **No echo:** a change that came in over MIDI is never transmitted (§9).

---

## 6. MIDI Learn

### 6.1 Model

Learn is **global** (device-wide), held in RAM and stored in
`settings.cfg`, with up to **128 entries**. Each entry maps one CC on one
channel kind to one parameter:

| Field | Values |
|-------|--------|
| CC | 3..119, excluding the reserved CCs (§5.1) |
| Channel kind | `g`: on G. `v`: on the target voice's effective channel (voice targets and per-voice Scene targets only). |
| Target | Voice slot 1..6 + descriptor; Effect + parameter; or Scene target |
| Type | Instrument type (voice targets) or Effect type (Effect targets); none for Scene targets |

The channel *kind* is stored, not the number, so entries follow later
channel edits.

### 6.2 Rules

- **One CC → many parameters:** each is its own entry.
- **One parameter → at most one CC.** An entry is unique on (target, type).
  Learning a parameter that already has an entry replaces that entry's CC
  and channel kind.
- **Type match:** an entry applies only while its type is loaded in the
  target slot (or the active Scene's Effect has its type), checked against
  the active Scene (Scene 17 while active). A slot can therefore hold
  different entries for different instrument types, using the same CC and
  channel.
- **Slot-specific:** a learned CC on a voice channel shared by several
  voices affects only its entry's slot. CC1 is the only CC that reaches
  every voice on a shared channel.
- **Several matches:** all matching entries are applied (macros).
- **Values:** §5.5. **Writes:** §5.6.

### 6.3 Learn gesture

1. Press SHIFT, then PLAY, then REC, holding the earlier buttons. These
   presses perform none of their own functions: no transport toggle, no
   record toggle, no SHIFT+REC menu, no chain arm. SHIFT+PLAY no longer
   toggles transport in any mode. Learn latches; SHIFT, PLAY and REC
   flash.
2. Touch a parameter: turn an RV1–RV4 knob, or edit a cell with the encoder.
   It must have a MIDI address (voice, Scene target, Effect). That
   parameter becomes the target and shows `LRN` as its value. Touching a
   cell with no address (BPM, settings, step data) is ignored, and learn
   keeps waiting.
3. Send a CC. A valid CC is not reserved and arrives on G, or (voice
   targets only) on the target voice's effective channel. The entry is
   stored, `settings.cfg` is marked for its normal debounced write, and the
   display returns to normal.
4. **Cancel:**
   - pressing SHIFT, PLAY or REC cancels; the press is consumed;
   - a MODE or page change cancels;
   - a Bank load cancels learn.

   While learn waits, any other CC is processed normally and notes keep
   playing.
5. **Full table:** a learn that would need a new entry when 128 are used is
   refused (`FUL` shown briefly) and ends.

There is no remove in the UI. To remove an entry, delete its line from
`settings.cfg`.

### 6.4 `settings.cfg` lines

One line per entry, after the fixed keys and before the `lines=N`
terminator. The keys are readable, so the file can be edited by hand:

```
learn=020,g,v1,drm,filter_freq
learn=021,v,v3,snr,noise_freq
learn=022,g,fx,flt,filter_freq
learn=023,g,scn,1vm
```

Format: `learn=<CCC>,<g|v>,<v1..v6|fx|scn>,<type token>,<descriptor key>`.
Scene targets have no type token; their key is the Scene target token.

- On load, keys resolve to indices once (instrument descriptors, Effect
  rows, Scene target table).
- A malformed or unresolvable `learn=` line is **skipped**; the rest of the
  file still loads.
- `lines=N` counts the learn lines like any other line. Only the
  `settings.tmp` boot recovery checks it, so deleting a line from
  `settings.cfg` by hand is safe.
- `settings.cfg` keeps being written while Scene 17 is active (it is not
  AutoSave).

---

## 7. Program change and Bank change

### 7.1 Program change (`RXF P`)

Only PC 0..15 is data, selecting Scenes 1..16. Other values are not
consumed.

- **On G:** a whole-Scene change, exactly like a PERF SEQ press, in any
  state: `scd` division timing, the `pcr` reset. With the transport stopped
  it is instant. It releases Scene 17 if that is active.
- **On a voice channel:** every track whose effective channel is that
  channel switches to play that Scene, exactly like hold VOICE + SEQ. T6
  and T7 switch together. No queue. Scene 17 is not released.
- If a channel is both G and a voice channel, G wins.
- **During a Bank load:** dropped.

### 7.2 Bank change (CC0 on G, `RXF P`)

- CC0 value v loads Bank slot v (000..127) with the background Bank load
  (`S082_P1_LOAD_SAVE.md`). CC32 is ignored.
- The UI locks and switches to Load/Save → Bank, showing slot v exactly as
  if it had been selected in the Menu and OK pressed. When the load
  completes, the UI unlocks and stays on Load/Save, exactly as after a
  Menu Bank load.
- Playback continues through Scene 17 (§10) when the transport is running.
- **Dropped:** CC0 arriving during a load; CC0 for an empty or invalid slot
  (refused as in the Menu).
- A running copy/clear operation is dropped or completed, then the load
  runs. A waiting learn is cancelled.

---

## 8. Realtime and transport (`RXF R`, `snc`)

| Message | Behaviour |
|---------|-----------|
| Clock | Drives the sequencer when `snc` selects its source (24 PPQ → 96 PPQ internal). |
| Start | Restart from the Pattern start. |
| Continue | Resume from the current position. |
| Stop | Stop. |
| Song Position Pointer | Honoured while stopped only: sets the master position (SPP × 24 internal ticks). Tracks re-project through the master-clock anchors (`PATTERN_DYNAMIC_STACK.md` §6.5). |
| MTC | Starts the sequencer at 0:0:0:0 and stops on timeout (existing behaviour). |
| Start/Stop with `snc = off` | Ignored. |
| MMC | Not supported (later). |

Realtime bytes are timestamped at arrival and handled in TIM3. While slaved
to external clock, the LXR does not generate its own clock as well.

---

## 9. Transmit (`TXF`)

LXR-generated messages go to **both** USB and DIN. A message whose channel
is off (G off, track unassigned) is not sent. Changes that arrived over MIDI
are never echoed back out.

| Letter | Sent |
|--------|------|
| N | Every trigger the LXR plays (sequencer, overlay, preview, and MIDI-in triggers). Channel = the track's effective channel. Note = the inverse of §4.1 (root + internal − 63), so recorded LXR output plays back identically. Note-off is tracked per track, so tracks sharing a channel cannot leave hanging notes. |
| R | Clock while the LXR is clock master; Start, Continue, Stop on transport changes. |
| C | Locally made parameter changes (knob/encoder; not step-automation playback). They are sent as the parameter's Learn CC (on G, or the voice channel for kind `v`) if it has one for the current type, else as NRPN (§5.4) on G. *(Proposal: when G is off, voice targets go as NRPN on the voice channel.)* Coalesced per target to the latest value; the NRPN number CCs are resent only when the number changes per port; throttled so DIN clock and notes are not delayed. |
| P | PC on G on a whole-Scene change. PC on the track's channel on a per-track change. CC0 on G after a Bank load. Scene 17 has no PC number: nothing is sent for it. |

---

## 10. Scene 17

Authority: `S082_P1_LOAD_SAVE.md` D1–D24. For MIDI:

- While Scene 17 is active it is the active Scene. The note map, Learn type
  matching, NRPN and CC writes act on it.
- PC on G leaves it; PC on a voice channel moves only those tracks.
- PC and CC0 that arrive during the load are dropped.
- Scene 17 has no PC number, so nothing is transmitted for it.
- Its track MIDI channel/note settings are copied from each track's played
  Scene at the copy.

---

## 11. Storage

| Data | Where | Notes |
|------|-------|-------|
| `snc ch TXF RXF MRT` | `settings.cfg` | `midi_chan_global` 0 = off; `midi_routing` uses the §3.3 values. |
| Learn table | `settings.cfg` `learn=` lines | §6.4. |
| Track `chn` / `nte` | Scene settings: `sceneset.scg`, AutoSave Scene cells | `chn` 0 = unassigned. |
| CC2 state, NRPN state | RAM only | Reset at boot. |

No migration of older files (standing project rule).

---

## 12. Code ownership and contexts *(proposal)*

| Module | Owns | Context |
|--------|------|---------|
| `MidiParser.c` | DIN framing, entry from both ports, consume-then-route dispatch, realtime handling | Foreground (`midi_service()`); realtime in TIM3 |
| `MidiNoteMap.c/h` | §4.1 resolver and its TX inverse; stateless | Foreground |
| `MidiControl.c/h` | NRPN, CC1/CC2, Learn table, Learn capture, MIDI write path | Foreground |
| `MidiOut.c/h` | TX filters, per-track note-off, coalescing, PC/CC0 TX | Foreground, TIM3 (sequencer notes and clock) |
| `MidiVoiceControl.c` | Trigger funnel (unchanged) | Producer any; consumer audio render |

Foreground TX into the DIN FIFO and USB must use the existing short
critical sections (TIM3 also sends).

---

## 13. RAM (acknowledged and accepted by the user, 2026-10-17)

| Item | Bytes | Region / owner |
|------|------:|----------------|
| Learn table (128 × 4 B) + count + capture state | ~519 | SRAM1 static, `MidiControl` |
| Held note per track (overlay rolls) | 7 | SRAM1 static, `MidiControl` |
| NRPN RX state (8 contexts × 2 ports × 3 B) | ~48 | SRAM1 static, `MidiControl` |
| NRPN TX number cache + coalesce | ~8 | SRAM1 static, `MidiOut` |
| CC2 state | 1 | SRAM1 static, `MidiControl` |
| TX note-off per track (replaces the 16 + 2 B channel cache) | 7 | SRAM1 static, `MidiOut` |
| Retired: `midiParser_originalCcValues` 255, `midi_NoteOverride` 7, `midi_MidiChannels[0..6]` 7 | −269 | Release does not authorise reuse |

Record the measured sizes in `STORAGE_SRAM_MANIFEST.md` at implementation.
Anything larger than listed needs a new acknowledgement.

---

## 14. Binding rules

- MIDI is the lowest write priority. It stores the Normal endpoint and never
  writes a held voice parameter's runtime value (S075 F3).
- Global Morph is never recorded, from any MIDI path (G-1).
- One note triggers at most one voice.
- One parameter has at most one Learn CC.
- Values are absolute and instrument-agnostic, clamped to the parameter's
  maximum; Morph is the only translation.
- Realtime bytes never disturb the channel-message running-status parser.
- `midiParser_processRealtimeEvents()` runs in TIM3 only.
- No USB → USB routing, and nothing is routed back to its source port.
- MIDI-originated changes are never transmitted.

---

## 15. Implementation status (gaps against current code, 2026-10-17)

| Spec | Current code |
|------|--------------|
| §2 G off; track `chn` unassigned | G has no off. Track channel getter falls back to track+1. Legacy `midi_MidiChannels[0..6]` mirror. |
| §2 MRT values | `off U2M M2M A2M M2U M2A`. |
| §3 consume then route | Every message is forwarded before parsing. |
| §4 note map | G plays the menu-active voice. Exact (channel, note) matches stack. Default note 36+track. |
| §5.2 CC1 | Implemented (global and per-voice); per-voice recording not wired. |
| §5.3 CC2 | Not implemented. |
| §5.4 NRPN | 14-bit number truncated to 8 bits into the legacy LXR CC2 map. |
| §5.6 fan-out, recording, display | No fan-out. `liveRec_paramIn(0xFFFF)` stub. LCD updates at the next repaint. |
| §6 Learn | Not implemented. The legacy LXR CC map is active on G. |
| §7 PC / CC0 | PC RX is a no-op; CC0 ignored. PC TX on Scene change exists. |
| §8 | Continue = Start; no SPP framing. |
| §9 | Note-off cache per channel; no parameter TX; USB re-framing of system-common/sysex wrong (`usb_sendMidi()` CIN = status >> 4). |
| §10 | Scene 17 does not exist (S082). |

---

## 16. Maintaining this document

- When S083 implements a section, mark §15's row closed. At the end of S083,
  replace the status banner with a "Current as of" line, and from then on
  treat the code as authoritative.
- Update in the same change: `MODULE_INTERCHANGE_SPEC.md` (MidiParser and
  the new modules), `FILESYSTEM_SPEC.md` (`settings.cfg` keys and `learn=`,
  `sceneset.scg` `chn`), `PATTERN_DYNAMIC_STACK.md` §6.2a,
  `STORAGE_SRAM_MANIFEST.md`, and the MEMORY.md "MIDI / Clock Reminders"
  (the BAR1/BAR2 path and the default notes 36..42 there are stale).
