# S083 P1 — MIDI Implementation Plan (initial draft)

*Written during Session 082 (2026-10-16), before the S082 background Bank
load is implemented. Planning only: no code has changed. This draft records
the requirements as given, how they map onto the current code, a proposed
architecture, the S082 prerequisites, and the risks, ambiguities and open
questions. §11 lists the questions whose answers settle the architecture.*

Scope: `SCOPING_TARGETS.md` Phase 6 §6.1 (MIDI and External Control
Cleanup) and the deferred item A20 (MIDI mapping), plus the MIDI items
deferred from S081 (`S081_P1_LIVE_RECORD.md` §16).

Terms: **track** T1..T7; **voice** (instrument slot) V1..V6. T6 and T7 both
play V6 (closed/open HiHat). **G** is the global MIDI channel. "Consumed"
means the LXR acted on a received message.

---

## 1. Requirements as stated (numbered for reference)

### 1.1 Global settings (MENU_MIDI_PAGE)

| Id | Cell | Requirement |
|----|------|-------------|
| M1 | `snc` | MIDI realtime (clock/transport) is a sync source for the `usb`, `din` and `aut` settings. |
| M2 | `ch` | Global MIDI channel, 1..16 plus **0 = off**. |
| M3 | `TXF` | What the LXR **transmits** from itself: N note, R realtime (clock + transport), C control (the learned CC if one is set, else NRPN), P program + bank change. It does not govern pass-through of messages the LXR does not consume; those follow `MRT`. |
| M4 | `RXF` | What the LXR **consumes** to change its own state. Same letters as TXF. |
| M5 | `MRT` | Routing between DIN and USB. Messages that are not consumed are re-transmitted according to MRT. Clock and transport are always re-transmitted (see A1). |

### 1.2 Note → track map

Each track has a MIDI channel and a MIDI note setting.

| Id | Situation | Rule |
|----|-----------|------|
| N1 | Only G assigned; no track notes | Tracks are striped across G: note 0 → T1 … note 6 → T7, repeating up to note 127. Every note plays the voice's default pitch. |
| N2 | Only G; some tracks have a note | A track with a note leaves the stripe. It owns its note at default pitch, and every higher note plays it one semitone higher, up to the next track's assigned note or 127. |
| N3 | Some tracks have a channel; no notes | On each channel, the tracks on it share notes 0..127 in contiguous, near-equal ranges, ordered by voice number. Each range starts at the voice's root (default pitch) and rises one semitone per note. |
| N4 | Channel and note assignments | As N3, but each assigned track starts at its note and fills up to the next assignment or 127. Notes below the lowest assignment may be filled by the channel's tracks that have no note. |
| N5 | Priority | A note never triggers more than one voice. The lowest-numbered track wins. |
| N6 | Action | A note is one trigger at its velocity. If a BAR `<`/`>` overlay is set in PERF mode, the note performs that overlay's action instead. |

### 1.3 Realtime

| Id | Requirement |
|----|-------------|
| T1 | Respond to MIDI clock and as many transport messages as practicable. |

### 1.4 Program and Bank change

| Id | Requirement |
|----|-------------|
| P1 | A program change is a Scene change. Only PC 0..15 is data. |
| P2 | PC on G changes the whole Scene. |
| P3 | PC on a track's assigned channel changes only that track, as if its TRACK (VOICE) button were held and that Scene's SEQ button pressed. No queue. |
| P4 | A Bank change (CC0) loads a Bank from the library. The UI switches to Load/Save, shows the incoming Bank's name at its slot as if selected, and runs a background load. If playback was running, it continues. G only. Needs the S082 background Bank load. |

### 1.5 Control

| Id | Requirement |
|----|-------------|
| C1 | Every parameter can be addressed by NRPN: about 480 assignments covering every voice parameter for V1..V6, then the FX parameters, then the Scene parameters. |
| C2 | NRPN works on G (if assigned). Voice parameters also work on that voice's assigned channel. FX and Scene parameters cannot be addressed on voice channels. |
| C3 | Each Scene holds up to 100 MIDI Learn assignments. They are stored as Scene parameters and are copied with the Scene settings. |
| C4 | A Learn assignment is on G, or (for a voice parameter) on that voice's channel. Any normal non-NRPN CC may be assigned to any voice, FX or Scene parameter, with the exceptions C5..C8. |
| C5 | CC0 is Bank change and can never be learned. |
| C6 | CC1 is always the global Morph on G. On a voice channel it is that voice's Morph, for every voice on that channel. Never learnable. |
| C7 | CC2 selects the note action: 0 = single trigger (default), 1 = the BAR `<` overlay, 2 = the BAR `>` overlay. |
| C8 | CCs used by the NRPN protocol, or common in other NRPN implementations, can never be learned. |
| C9 | A Learn assignment fans out through the VOICE edit mask to every Scene in the mask when it is made. |

---

## 2. Current code baseline (checked 2026-10-16)

| Area | What exists | Gap vs requirements |
|------|-------------|---------------------|
| Framing | `MidiParser.c` `midiParser_parseUartData()` (DIN running status, sysex pass-through). USB packets are framed in `usb_midi_core.c`. Realtime bytes from both ports go to the timestamped `MidiRealtime` ring (TIM3 drains it). | SPP (`0xF2`) and Song Select are not framed as messages. `usb_sendMidi()` uses `status>>4` as the CIN, so forwarded system-common and sysex packets are malformed on USB. The USB RX ring (256 msgs) overwrites when full. |
| Context | `midi_service()` in the main loop. It runs `uart_processMidi()` and up to 8 USB messages per pass. Note/CC handling runs in the foreground. | Note timing depends on foreground latency. Channel messages carry no timestamp (deferred from S081 Q3). |
| Routing | `midiParser_routing` bits; `MRT` = `off U2M M2M A2M M2U M2A`. Every message is forwarded **before** it is parsed, whether or not it is consumed. | M5 needs forwarding decided **after** consumption. There is no DIN↔USB bidirectional option. |
| Filters | `midiParser_txRxFilter`: RX low nibble, TX high nibble, bits N=1 R=2 C=4 P=8. Display `off N R RN … all`. | Fits M3/M4 as is. |
| Channels | `midi_MidiChannels[8]`. [7] = G (0-based, no off). [0..6] are a legacy mirror of `PAR_MIDI_CHAN_1..7`, still used by the live-note echo path. Track channel/note live in `scene_settings_t::midi_channel/midi_note[7]`. The getter falls back to **track+1** (channels 1..7) for unset values. | No off for G. No "unassigned" for track channels. There are two sources of truth for voice channels. |
| Note input | `midiParser_handleNoteMessage()`: on G, plays the **menu-active voice**. On other channels, every track whose (channel, note) matches exactly fires (stacked). Default note 36+track. | N1..N5 not implemented. Stacking breaks N5. |
| Pitch | Internal note 63 = default pitch; offset = note − 63 (`Oscillator.c:1007`). Step note specials are 7-bit. | Chromatic ranges must map to 63 + offset. |
| Live record | `midiParser_noteOn()` calls `liveRec_triggerIn(kind SINGLE, source MIDI)`. `midiParser_enterTaggedParameter()` calls `liveRec_paramIn(0xFFFF, MIDI)`, a stub with no target. | Needs real target IDs (§3.5). |
| CC | CC1 global → `preset_morph()`; CC1 on another channel → matching voices' Morph. Other CCs on G go through the legacy LXR CC/CC2 map (`midiParser_applyTaggedInstrumentCc()`) to the **active Scene's** Normal endpoint (S075 F3 priority). No fan-out. `modNode_originalValueChanged(data1-1)` treats CC numbers as legacy flat ids. | Legacy map to be replaced by NRPN + Learn. `midiParser_originalCcValues[255]` is legacy. |
| NRPN | CC99/98 build a 14-bit number, but `MidiMsg.data1` truncates it to 8 bits. CC6 dispatches into the legacy CC2 page. No CC38/96/97. | Replace. |
| PC | RX: `seq_setNextPattern()` is a no-op. TX: `seq_sendProgChg(seq_activePattern)` on Scene change, on G. | P1..P3 not implemented. TX ignores G = off (because there is no off). |
| Bank | CC0 is ignored. | P4 depends on S082. |
| Realtime | Clock → `sync_tickTimestamp()`. Start **and Continue** → `sync_midiStartStop(1)`. MTC start at 0:0:0:0 exists. Source gating by `snc` (AUTO: jack > DIN > USB). Clock out only while internal. | Continue is treated as Start. No SPP. Clock re-transmit while slaved is mrt-only. |
| TX | `seq_sendMidi()` always goes to both USB and DIN. Note-off cache is one note per **channel** (`midi_chan_notes[16]`). | Tracks that share a channel can leave hanging notes downstream. |
| Dead code | `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()` (no callers), `midi_NoteOverride[7]`, the `MIDI_CLOCK/START/STOP` cases in `midiParser_parseMidiMessage()` (realtime never reaches it now). | MEMORY.md "MIDI / Clock Reminders" still says BAR1/BAR2 use `midiParser_playVoiceMidiNote()` and that default notes are 36..42. Both are stale. |
| Target IDs | Canonical 9-bit `instrument_param_id_t`. Voices: `slot*64 + descriptor_index` (0..383). Scene block 384..447 (`1vm..6vm`, `7dc`, `1ou..6ou`, `1fx..6fx`, `fxm` 404, track length/scale/shuffle 405..425). Effect-local 448..510. 511 = off. | This is the natural NRPN address space (§3.5). |

---

## 3. Proposed architecture

### 3.1 Module split

`MidiParser.c` is 2,169 lines, most of it the retired fixed-engine map
inside `#if 0`. Proposal:

| Module | Owns |
|--------|------|
| `MidiParser.c` (kept, slimmed) | DIN framing, entry from both ports, the consume-then-route dispatcher, realtime handling (TIM3). |
| `MidiNoteMap.c/h` (new) | The note → (track, internal note) resolver for N1..N5, its inverse for TX, and validation of the channel/note settings. Stateless: no table. |
| `MidiControl.c/h` (new) | The NRPN state machine (per port × channel), CC1/CC2, the Learn table lookup, value-domain conversion, the parameter write path, and Learn capture. |
| `MidiOut.c/h` (new, or kept inside the Sequencer) | TX filters, port selection, the per-track note-off cache, NRPN/CC TX coalescing, PC/CC0 TX, echo suppression. |

`MidiVoiceControl.c` stays the trigger funnel (S081 §19.15) and is not
changed.

### 3.2 Message flow (M3–M5)

```
DIN byte ─► framer ─┐
USB packet ─────────┴─► MidiMsg{source} ─► RXF gate ─► consumers ─► consumed?
                                                                    │ no
                                                                    ▼
                                                         MRT forward (never back
                                                         to the source port)
realtime ─► MidiRealtime ring ─► TIM3: snc/RXF gate ─► sync; forward per MRT (always)
```

- **Consumed** means: a note resolved to a track; a CC handled as NRPN,
  CC0/1/2, or a Learn hit; a PC 0..15 that acted. A message blocked by RXF
  is not consumed, so it is forwarded (A2).
- NRPN protocol CCs (6, 38, 96..101) on G or a voice channel are always
  consumed while RXF C is on. Forwarding half of an NRPN sequence would
  confuse downstream gear.
- Each source port forwards only to the other port, or DIN→DIN. USB→USB is
  never offered.

### 3.3 Note map resolver (N1–N5)

Inputs come from the active Scene (see Q3): G; per track `ch(t)` ∈ {0
unassigned, 1..16}; `nt(t)` ∈ {unassigned, 0..127}. A track's effective
channel is `ec(t) = ch(t) ? ch(t) : G`. If `ec(t) = 0`, the track is not
mapped.

For an incoming (c, n), let S = the tracks with `ec(t) = c`, in track
order. A = the tracks in S with a note; U = the rest.

1. **Assigned ranges** (both channel kinds): sort A by note. Each track owns
   `[nt(a), next assigned note − 1]`, or up to 127 for the last one. Its
   internal note is `63 + (n − nt(a))`. If two tracks share a note, the
   lower track wins and the other owns nothing (N5).
2. **Region below** the lowest assigned note (or all of 0..127 if A is
   empty):
   - c = G (stripe, N1/N2): track `U[n mod |U|]`, internal note 63.
   - c ≠ G (split, N3/N4): split the region into |U| contiguous blocks in
     track order. Lower tracks get the remainder notes. Internal note is
     `63 + (n − block start)`.
   - If U is empty, the region is not mapped (not consumed, so forwarded).
3. Return at most one track (N5).

Cost: at most 7 tracks, a few comparisons per note. No lookup table, so no
RAM. The inverse `midiMap_txNote(track, internalNote)` gives the TX channel
and note (Q9).

Internal note range: the offset can reach +127, but step note specials are
7-bit and the oscillator note table is finite. Proposal: clamp the internal
note to 127, so a range plays at most +64 semitones (A6).

### 3.4 Triggers, overlays and recording (N6, C7)

- A single trigger goes through the existing funnel (`voiceControl_noteOn()`),
  then to `liveRec_triggerIn()` with the resolved track, internal note and
  velocity.
- The overlay action reuses the S080 BAR overlay executor (12 trigger
  types). Roll types (`r/8`…`128`) repeat while the note is held and stop at
  its note-off. That needs a held-note record per track: 7 B (approval
  needed).
- The CC2 overlay state is 1 B. Q6 covers whether CC2 is global or per
  channel, and whether it applies only while the UI is in PERF.
- Mute: the current `seq_isTrackMuted()` check stays.

### 3.5 Parameter address space (C1, C2)

Proposal: **NRPN number = the canonical 9-bit target ID** (MSB = id>>7,
LSB = id&0x7F). That gives voices 0..383 (6 × 64 descriptor indices),
Scene 384..447 and Effect 448..510. Automation, LiveRecord and the Learn
table use the same ID, so the `liveRec_paramIn(0xFFFF, MIDI)` stub gets a
real target with no extra mapping. Unused IDs are ignored (and forwarded).
The space has 511 IDs, not 480; Q2 covers the difference.

- **Type-relative addresses.** Voice IDs are descriptor indices of whatever
  type is loaded in the slot, and Effect IDs are relative to the Effect
  type. NRPN 17 means a different parameter on a `drm` slot than on a `snr`
  slot. A published NRPN chart therefore has one table per instrument type
  and per Effect type (R7).
- **Value domain.** One resolver, `midiTarget_domain(id)`, built from the
  existing descriptors (instrument rows, Effect rows, Scene target table).
  It returns min/max/dtype; no new table. 14-bit NRPN gives full resolution
  to 0..255 targets (Morph amounts, Effect 0..255 cells). For 7-bit CCs,
  Q7 decides scaled vs raw.
- **Write path.** Keep the S075 F3 priority: automation, then menu, then
  MIDI. Voice targets keep `preset_setInstrumentParameterFromMidi()`
  (Normal endpoint, Morph sweep). Scene targets and Effect targets need
  equivalent MIDI entries through SceneData/EffectsManager. Fan-out: Q5.
- **Recording.** After a successful store, call `liveRec_paramIn(id, MIDI)`.
  Global Morph (CC1 on G) is **never recorded** (G-1, binding). CC1 on a
  voice channel is an individual voice Morph edit, so it is recorded through
  `liveRec_morphValue[]` like `1VM..6VM`.
- **LCD.** Fix the S075 open item ("MIDI-entered values reach the LCD only
  at the next repaint") through `menu_notifyExternalParamChanged()`, with a
  rate limit.
- **NRPN state** is held per (port, channel) for G and up to 7 voice
  channels. Handling: CC6 applies MSB (LSB 0), CC38 refines, CC96/97 step
  by ±1, and CC101/100 = 127/127 (RPN null) deselects. Q8 decides whether
  voice channels use absolute or voice-relative numbering.

### 3.6 MIDI Learn table (C3–C9)

**Entry.** CC (7 bits), channel kind (G, or the target voice's channel — 1
bit), target ID (9 bits) = 17 bits. Store the channel kind, not the channel
number, so assignments follow later channel edits.

| Packing | Per Scene | ×16 Scenes (SRAM1) |
|---------|-----------|--------------------|
| 3 B/entry × 100 | 300 B | **4,800 B** |
| 2 B/entry + a 100-bit kind bitmap | 213 B | **3,408 B** |
| 64 entries, 2 B + bitmap | 136 B | 2,176 B |

All of these need approval under the RAM policy (free SRAM1 is reserved for
Pattern data). AutoSave also grows: the record grows by the same bytes, and
the RAM-resident canonical mutation mask grows by about 1/8 of that (~430..600
B). See R1/R2 and Q4.

**Lookup.** Per CC, scan the active Scene's table (≤100 entries). Several
targets per CC are allowed unless Q10 says otherwise.

**Reserved (never learnable):** 0 (Bank), 1 (Morph), 2 (overlay), 6/38
(data entry), 32 (bank LSB, recommended), 96..101 (inc/dec, NRPN, RPN),
120..127 (channel mode). Everything else in 3..119 is assignable.

**Lifecycle.** Assign and remove fan out through `bank_sceneFanoutMask()`
(C9). Copy/clear Scene settings carries the table (COPYCLEAR §, edit-mask
rules). `sceneset.scg` gets new lines and AutoSave gets new Scene cells
(format change, no migration).

**Learn UI:** not specified (Q11). Proposal: hold SHIFT+REC-menu entry
`lrn`, touch a parameter (knob or encoder), send a CC. List and delete on a
RECORDING_PAGE sub-page (sub-pages 2+ are reserved).

### 3.7 Program change and Bank change (P1–P4)

- **PC on G:** run exactly what a PERF SEQ press runs
  (`menu_perfModeSceneButtonPressed()`): `scd` timing, the `pcr` reset,
  instant when stopped (Q12). This already runs in the foreground, the same
  context as MIDI parsing.
- **PC on a voice channel:** `seq_setTrackPlayedScene()` +
  `preset_startSingleVoiceApply()`, the same path as hold VOICE + SEQ. Every
  track whose effective channel is c moves; T6/T7 move together (existing
  rule). If c = G, G wins.
- **Bank (CC0 on G):** coalesce to the latest value, map it to a slot (Q13),
  then call the S082 entry point (§4).
- **Refusals:** a copy/clear operation, a Load/Save command in flight, or a
  latched-menu edit. A refused request does nothing (not forwarded) and
  leaves a trace record. Q12 covers PC while live recording.

### 3.8 Realtime and transport (T1)

| Message | Proposal |
|---------|----------|
| Clock | Existing path. |
| Start `FA` | Restart from the Pattern start (existing). |
| Continue `FB` | Resume from the current position. Today it equals Start. |
| Stop `FC` | Existing. |
| SPP `F2` | Honoured while stopped only. Sets the master tick (SPP × 24 internal ticks). Tracks re-project through the existing master-clock anchors (`PATTERN_DYNAMIC_STACK.md` §6.5). Needs DIN framing and USB CIN 3. |
| MTC | Keep the existing start-at-zero behaviour. |
| MMC (sysex) | Optional (Q14): Play/Stop/Locate need a small sysex state machine. |

Re-transmit: realtime always follows MRT, even when consumed (A1). While
slaved, the LXR does not generate its own clock as well, to avoid doubled
clocks (Q15).

### 3.9 Transmit

- **N:** sequencer, overlay, preview and MIDI-in-triggered notes go to the
  track's effective channel. G off and the track unassigned means no TX.
  Note number: Q9. The note-off cache becomes per track (7 B instead of the
  16-channel cache), so tracks that share a channel cannot leave hanging
  notes.
- **R:** clock while master; Start/Continue/Stop/SPP on transport changes.
- **C:** a locally made parameter change (knob/encoder; Q16 for step
  automation) is sent as its learned CC(s) on the entry's channel, else as
  NRPN on G or the voice channel. Changes that came in over MIDI are never
  echoed. Coalesce per target to the last value, and send the NRPN number
  CCs only when the number changes (per port). A full NRPN is ~12 bytes,
  about 3.8 ms on DIN, and the DIN normal FIFO is 64 B, so TX must be
  throttled (R5).
- **P:** PC on a Scene change (exists; skip when G = off). PC on a track
  channel for a per-track change (Q17). CC0 on a Bank load (Q17).
- **Ports:** all LXR-generated messages go to both USB and DIN, as today
  (Q15).

### 3.10 Menu

- `ch`: 0..16 with 0 shown as `off`. The dtype space is full (S078), so use
  `DTYPE_1B16` plus a static-param special case (the run-mode precedent).
- Track `chn`: 0 = unassigned (`glb`), 1..16. The default changes from
  track+1 to 0. Track `nte`: "unassigned" keeps 0 = `def`, which makes MIDI
  note 0 unassignable (A7).
- `MRT`: new value list (Q15). Proposal: `off U>D D>D D>U U<>D D>A A>D all`.
- Retire `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_MODE`. Their ids sit inside the
  Globals block, so `menu_sendAllGlobals()` replays them; keep the slots as
  no-ops rather than renumbering, following the mirror precedent.

### 3.11 Storage

- `settings.cfg`: G (`midi_chan_global`) gains 0 = off; `MRT` values change
  meaning. No migration.
- Scene (`sceneset.scg`, AutoSave cells): `midi_channel` gains 0 =
  unassigned, plus the Learn table. No migration.
- Docs to update at close: `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`,
  `BANK_PRESET_ARCHITECTURE.md`, `PATTERN_DYNAMIC_STACK.md` §6.2a,
  `MODULE_INTERCHANGE_SPEC.md` (MidiParser, new modules),
  `STORAGE_SRAM_MANIFEST.md`, `COPYCLEAR_UTILITIES.md`, and the stale
  MEMORY.md MIDI reminders.

---

## 4. Prerequisites — what S082 (background Bank load) must provide

S082 is the only prerequisite that blocks P4. The MIDI side needs these from
it. Each is cheap to design in now and expensive to retrofit.

| Id | Need | Why |
|----|------|-----|
| S1 | A non-UI request entry, e.g. `preset_requestBackgroundBankLoad(slot)`. Foreground-callable, non-blocking, returns accepted / refused (+reason). It must not depend on Menu cursor or page state. | MidiParser calls it from `midi_service()`. |
| S2 | A defined playback contract: the transport keeps running; what plays while the 16 Scenes are replaced; **when** the active Scene swaps (immediately, at a bar, at the `scd` division); which Scene becomes active (the incoming Bank's `active_scene`, or keep the index). | P4 says playback is maintained. A swap mid-bar is audible. |
| S3 | A UI presentation call, e.g. `menu_presentExternalBankLoad(slot)`: switch to Load/Save with type Bank, cursor on the slot, the name from `/Bank/.hcindex`, progress `...`. It must work from any mode and define what happens to latched menus (SHIFT+REC, chain editor) and the previous mode afterwards. | P4 UI requirement. |
| S4 | Request coalescing: one pending slot, latest wins; ignore a repeat of the slot already loading. | A controller knob on CC0 sends a burst. |
| S5 | Queryable busy/refusal predicates: Load/Save command active, copy/clear active, AutoSave write in flight (wait or refuse?), live record armed. | MIDI must refuse cleanly, without a stuck `menu_storageBusy`. |
| S6 | An outgoing-Bank policy for unsaved edits: flush AutoSave first, or discard. | A remote Bank change must not silently lose live edits (R3). |
| S7 | A completion/failure hook (callback or status poll). | MIDI then re-reads channel/Learn state, sends CC0 TX (Q17), clears the pending slot, and handles a failure or empty slot. |
| S8 | Empty and invalid slot behaviour: show and refuse? Show "empty" and keep the current Bank? | P4 wording assumes a valid slot. |
| S9 | Defined interaction with the looper, queued/chained Scene changes, per-track Scene overrides (`seq_perTrackPattern`), LiveRecord, and held notes (all-notes-off TX?). | Every one of these holds a Scene index that the load invalidates. |
| S10 | PC during a load: refuse, defer until commit, or apply to the still-resident Scenes. | DAWs often send CC0 + PC together. |
| S11 | `pat_background_region` arbitration: it is shared by AutoSave snapshot, overlap paste and Bank Load staging. A background load must not race a pending AutoSave snapshot. | Existing three-way sharing (S077 P1). |
| S12 | Works with the transport stopped as well as running. | — |

Prep that is not S082 code, needed before S083 starts:

- Answers to §11 (at least F1–F6, F12, F13).
- RAM approvals (§6): Learn table, AutoSave growth, the 7 B held-note and 7 B
  note-off caches, NRPN state.
- The S081 hardware checks (P3/P4) should pass first, since MIDI note
  recording builds on LiveRecord.

---

## 5. Proposed S083 phases

| Phase | Content | Verify |
|-------|---------|--------|
| A | Cleanup and foundation: retire the legacy CC/CC2 map, `midi_MidiChannels[0..6]`, `midi_NoteOverride`, `midiParser_originalCcValues`, dead helpers and dead realtime cases. G 0 = off; track channel 0 = unassigned. SPP/Song Select framing; USB TX CIN fix. Consume-then-route dispatcher; new MRT list. | Build; MRT matrix with a DIN loopback and a DAW; no regressions in clock sync. |
| B | Note map resolver (§3.3), N5 priority, pitch, note-off, overlay actions and CC2, LiveRecord. | Table-driven host test of the resolver (every N-case); hardware note matrix. |
| C | Transport: Continue, SPP, start/stop policy (Q14), clock re-transmit rule. | DAW start/stop/continue/locate. |
| D | PC (Scene, per track) and Bank change through S082. | PC from DAW, running and stopped; CC0 bursts; refusals. |
| E | Target domain resolver, NRPN RX (14-bit), CC1 per voice, Scene/Effect MIDI write paths, `liveRec_paramIn` targets, LCD refresh. | NRPN sweep of every ID per instrument type; record CC automation; G-1 check. |
| F | Learn: Scene storage, `sceneset.scg`, AutoSave cells, copy/clear, fan-out, UI. | Assign, fan out, copy, clear, reboot restore, 100-entry full case. |
| G | TX: per-track note-off cache, parameter TX (learned CC/NRPN), coalescing, PC/CC0 TX, echo suppression. | DAW record of LXR output; DIN FIFO stress with a knob sweep while clock runs. |
| H | Docs and memory. | — |

---

## 6. Resource estimate

| Item | Bytes | Region | Status |
|------|------:|--------|--------|
| Learn table | 2,176–4,800 | SRAM1 (`scenes[16]`) | **Approval needed** (Q4) |
| AutoSave mask growth | ~270–600 | SRAM1 | **Approval needed** |
| AutoSave record growth | same as the Learn table | SD only | Format change |
| Held-note per track (overlay rolls) | 7 | SRAM1 | Approval needed |
| TX note-off per track (replaces 16 + 2) | 7 | SRAM1 | Net −11 |
| NRPN RX state (8 contexts × 2 ports × 3 B) | ~48 | SRAM1 | Approval needed |
| NRPN TX number cache + coalesce slot | ~8 | SRAM1 | Approval needed |
| CC2 overlay state, Learn capture state | ~4 | SRAM1 | Approval needed |
| Retired: `midiParser_originalCcValues` 255, `midi_NoteOverride` 7, `midi_MidiChannels[0..6]` 7 | −269 | SRAM1 | Release does not authorise reuse (policy) |
| Flash | ~+6..10 KB new, about −4 KB from removing the legacy map | Flash (197 KB free at S080) | OK |

---

## 7. Risks

| Id | Risk | Mitigation |
|----|------|-----------|
| R1 | The Learn table is the largest new RAM in Phase 6 (up to 4.8 KB) and competes with the Pattern-data reservation. | Smaller packing or fewer entries (Q4). |
| R2 | AutoSave record, mask, validator and `tools/verify_bank_autosave.py` all change. AutoSave has a history of subtle defects. | A dedicated AutoSave sub-phase with the existing validators. |
| R3 | A remote Bank change (CC0) can replace 16 Scenes during a performance and lose unsaved edits. A stray CC0 from a DAW is enough. | S6 policy; an option to make RXF P cover only PC (Q13). |
| R4 | Per-Scene note maps and Learn tables change on a Scene change, so a DAW drum track can start hitting different voices mid-song. | Edit-mask fan-out keeps Scenes equal by default; Q3. |
| R5 | DIN bandwidth: NRPN TX (12 B each) during a knob sweep can delay notes, and the 64 B normal FIFO can overflow. Realtime has its own FIFO. | Coalescing, number caching, throttle (§3.9). |
| R6 | Feedback loops: a DAW echoing LXR output back (MIDI thru on the track) fights the knob (CC/NRPN), re-triggers notes, or re-sends PC. | Never echo MIDI-originated changes; document DAW setup; optional ignore window. |
| R7 | Type-relative NRPN/Learn targets: changing a slot's instrument type (or the Effect type) silently retargets every NRPN and Learn entry for it. | Document it. Option: Learn stores the descriptor **key** hash instead of the index (more bytes). |
| R8 | Foreground MIDI latency: Load/Save, LCD and copy/clear work in the main loop delay note triggers and LiveRecord placement. | Measure first. Options: carry the DIN byte timestamp into LiveRecord; or parse notes in a low-priority interrupt (Q18). |
| R9 | Hanging notes downstream: a note-on forwarded (unmapped) whose note-off is consumed after a map change, or the reverse. | Accept, or track forwarded note-ons (16×128 bits = 256 B, not proposed). |
| R10 | Per-track Scene playback: MIDI parameter writes, the note map and Learn use the **active** Scene, while a track may play another Scene. Writes can be inaudible. | Q3/Q5. |
| R11 | Chromatic ranges exceed the recordable 7-bit note; clamping changes pitch above +64. | A6. |
| R12 | The global stripe makes GM note 36 trigger T2 (36 mod 7 = 1), which will surprise users with GM drum maps. | Intended by N1; document it, or add a stripe base (F-A3). |
| R13 | Bank change via S082 is new code under test at the same time; P4 can only be verified after S082 is hardware-proven. | Phase D last among RX phases. |
| R14 | `MidiParser.c` carries ~1,000 lines inside `#if 0` and legacy flat-parameter calls (`modNode_originalValueChanged(data1-1)`). Removing them may expose hidden dependencies (`preset_applySoundParameter()` → `midiParser_ccHandler()`). | Phase A audit of every internal caller first. |

---

## 8. Ambiguities

| Id | Text | Readings | Proposed reading |
|----|------|----------|------------------|
| A1 | "Clock and transport are always re-transmitted." | (a) forwarded per MRT even when consumed; (b) forwarded to every port regardless of MRT; (c) the LXR always emits its own clock. | (a) |
| A2 | Is a message blocked by RXF "not consumed", and so forwarded? | yes / no | yes |
| A3 | A track whose channel is set to the same number as G. | global rules (stripe) / channel rules (split) | global rules: identity by effective channel |
| A4 | Do tracks with their own channel also take part in G's stripe? | yes / no | no |
| A5 | N3 remainder: "as equal as possible (lowest priority)". | Lower tracks get the extra notes / higher tracks do | lower tracks get them |
| A6 | Pitch above the recordable range. | clamp / do not play / wrap octaves | clamp at internal 127 |
| A7 | Track note 0 = "unassigned" today, so MIDI note 0 cannot be a track's root. | accept / use 0xFF for unassigned | accept |
| A8 | CC2 "toggled". | value sets a mode / each 1 or 2 flips that overlay on/off | value sets a mode; 3..127 ignored |
| A9 | N6 "if a BAR overlay is set in PERF mode". | only while the UI is in PERF / CC2 works in any mode / also a held physical BAR | CC2 works in any mode; a held physical BAR applies only in PERF |
| A10 | The overlay's own note setting (`perf_note_lt/rt`) vs the incoming note's pitch. | incoming pitch / overlay note | incoming pitch |
| A11 | "About 480" NRPN assignments vs the 511-ID target space. | a dense renumbered table / the canonical IDs with gaps | canonical IDs |
| A12 | Which "Scene parameters" are addressable: only the Scene target block (Morph, outs, sends, `fxm`, track timing), or also bus compressor, fader modes, chain settings, `scd`… | target block only / extend the block | target block, extended only on request |
| A13 | Are voice-associated Scene targets (`1vm`, `1fx`, `1ou`, track timing) "voice parameters" for C2 (allowed on the voice channel)? | yes / no | yes |
| A14 | PC "No Queue": immediate, or at the `scd` division like a SEQ press? | immediate / `scd` | `scd` (same as a SEQ press) |
| A15 | Bank slot range: CC0 gives 0..127; the library has 000..999. | 000..127 only / CC0×128 + CC32 | CC0 only (000..127); CC32 reserved for later |
| A16 | After a Bank load, does the UI stay in Load/Save or return? | stay / return | return to the previous mode on completion |
| A17 | TXF C "CC if set": if a parameter has several learned CCs, which one is sent? | all / the first | the first (lowest table index) |
| A18 | Learn "on assignment" fan-out when a target Scene's table is full. | partial fan-out / refuse the whole assignment | refuse the whole assignment and show it |

---

## 9. Open architectural questions

| Id | Question | Options | Recommendation |
|----|----------|---------|----------------|
| Q1 | NRPN address scheme | (a) canonical 9-bit target ID; (b) a new dense 0..479 table | (a): one ID space for automation, LiveRecord, Learn and NRPN. |
| Q2 | Unused IDs within 0..510 | ignore and forward / reject | ignore and forward |
| Q3 | Where the MIDI map (track channel/note) and the Learn table live | per Scene (as now; edit-mask fan-out) / per Bank / global (`settings.cfg`) | Learn per Scene (as specified). Channel/note: **ask** — per Bank would make R4 go away. |
| Q4 | Learn table size and packing | 100×3 B / 100×2 B + bitmap / fewer entries | 100 × 2 B + bitmap (3,408 B) if approved |
| Q5 | Do MIDI value writes fan out through the edit mask (like menu edits), and to the active or the played Scene? | active only (now) / fan out / played Scene | fan out like menu edits, active Scene |
| Q6 | CC2 scope | G only / per channel | G only, any UI mode |
| Q7 | 7-bit CC → parameter scaling | raw clamp (now) / scaled across the domain | scaled for Learn; NRPN raw (it has 14 bits) |
| Q8 | NRPN on a voice channel | absolute IDs (accept only that voice's block) / voice-relative 0..63 | absolute IDs |
| Q9 | TX note for a track | the inverse of the input map (round trip) / root note only / current rule (track note or triggered note) | inverse map |
| Q10 | One CC driving several targets (macros) | allow / one target per (CC, channel) | allow |
| Q11 | Learn UI gesture and list/delete page | see §3.6 | §3.6 proposal |
| Q12 | PC while live recording, looping, or with a chain armed | same as a SEQ press in that state / refuse | same as a SEQ press |
| Q13 | Bank change safety | always on with RXF P / a separate enable / require a second CC0 within N ms | a separate global on/off (`bnk`) defaulting off (R3) |
| Q14 | Transport when `snc = off`; MMC | follow Start/Stop without clock / ignore; MMC yes/no | ignore when off; MMC later |
| Q15 | Port roles: MRT values; TX port selection; own clock while slaved | §3.10 list; TX to both; no own clock while slaved | as proposed |
| Q16 | Is step automation playback transmitted as CC/NRPN? | yes / no | no (knob and encoder only) |
| Q17 | TX of a per-track PC and CC0 on Bank load | yes / no | yes, under TXF P |
| Q18 | MIDI note processing context | foreground (now) / low-priority IRQ / foreground + DIN timestamps for LiveRecord | measure first; then timestamps |

---

## 10. Out of scope here

External MIDI sequencing tracks (§6.8), one-shot LFOs (§6.2), sysex
dump/restore, MIDI 2.0, multi-cable USB.

---

## 11. Follow-up questions to settle the architecture

Each has a recommended answer; "yes" accepts it.

**Address space and control**

- **F1 (Q1, A11).** Use the canonical 9-bit target ID as the NRPN number
  (voices 0..383, Scene 384..447, Effect 448..510), instead of a new dense
  480-entry table? *Recommend yes.*
- **F2 (A12, A13).** Should NRPN/Learn reach only the existing Scene target
  block? Are per-voice Scene targets (`1vm`, `1fx`, `1ou`, track
  length/scale/shuffle) allowed on that voice's channel? *Recommend: block
  only; yes on the voice channel.*
- **F3 (Q8).** On a voice channel, does NRPN use the same absolute numbers
  (only that voice's block accepted), or voice-relative 0..63? *Recommend
  absolute.*
- **F4 (Q7).** For a learned 7-bit CC on a parameter with a small range
  (e.g. filter type 0..7), scale 0..127 across the range, or pass the raw
  value clamped? *Recommend scaled.*
- **F5 (Q5, R10).** Should MIDI value changes fan out through the VOICE edit
  mask like knob edits? Should they write the active Scene even when that
  track plays another Scene? *Recommend: fan out; active Scene.*
- **F6 (Q4, R1).** Learn table: approve 100 entries × 2 B + a kind bitmap =
  3,408 B SRAM1 (plus ~430 B AutoSave mask)? Or fewer entries?
- **F7 (Q10, A17).** May one CC drive several targets? On TX, send only the
  first learned CC? *Recommend yes / yes.*
- **F8 (Q11).** Learn gesture: is "SHIFT+REC menu `lrn`, touch a parameter,
  send a CC" acceptable, with list/delete on a RECORDING_PAGE sub-page?
- **F9 (A18).** If fan-out would overflow a Scene's 100 entries, refuse the
  whole assignment? *Recommend yes.*
- **F10 (R7).** Accept that NRPN and Learn targets are instrument-type
  relative (re-targeted when a slot's type changes)? *Recommend yes,
  documented.*

**Notes**

- **F11 (A3, A4).** A track set explicitly to G's channel number follows the
  global stripe rules, and channel-assigned tracks never join G's stripe?
  *Recommend yes / yes.*
- **F12 (Q3, R4).** Should track MIDI channel/note stay per Scene, or move to
  the Bank (or `settings.cfg`) so the map cannot change on a Scene change?
- **F13 (A5, A6).** Remainder notes go to the lower tracks; pitch above the
  recordable range clamps? *Recommend yes / yes.*
- **F14 (A8, A9, A10, Q6).** CC2: G only; values set a mode (0/1/2, others
  ignored); works in any UI mode; the overlay uses the incoming note's
  pitch? *Recommend yes to all.*
- **F15 (Q9).** Sequencer note TX uses the inverse of the input map, so a
  recording of LXR output plays back the same? *Recommend yes.*
- **F16 (R12).** Keep the stripe starting at note 0 (36 → T2), or add a
  stripe base note setting? *Recommend: keep, as specified.*

**Program, Bank, transport, routing**

- **F17 (A14, Q12).** PC follows the `scd` division and `pcr`, exactly like a
  PERF SEQ press, in any state? *Recommend yes.*
- **F18 (A15, A16, Q13).** CC0 maps to Bank slots 000..127; the UI returns to
  the previous mode when done; a separate `bnk` enable defaults off?
- **F19 (S2, S10).** For S082: when does the active Scene swap during a
  background load (immediately / bar / `scd`)? Which Scene is active
  afterwards? Does a PC during a load wait for the commit?
- **F20 (S6).** For S082: on a remote Bank change, flush AutoSave first or
  discard unsaved edits?
- **F21 (A1, A2, Q15).** Clock/transport are forwarded per MRT even when
  consumed; RXF-blocked messages count as not consumed (forwarded); no own
  clock while slaved; LXR output goes to both ports? *Recommend yes to all.*
- **F22 (Q15).** New MRT list `off U>D D>D D>U U<>D D>A A>D all`?
- **F23 (Q14).** Ignore Start/Stop when `snc = off`? Honour SPP only while
  stopped? MMC later? *Recommend yes / yes / yes.*
- **F24 (Q16, Q17).** Step automation playback is not transmitted;
  per-track PC and CC0 are transmitted under TXF P? *Recommend yes / yes.*
- **F25 (Q18).** Measure foreground note latency in Phase B before choosing
  a processing context? *Recommend yes.*
