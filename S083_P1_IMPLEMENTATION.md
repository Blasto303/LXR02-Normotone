# S083 P1 — MIDI Implementation Schedule

*Written 2026-10-21 (Session 083) from a code deep dive of the tree at
`f10e797` (code unchanged since; only planning documents moved). Planning
document: `S083_P1_MIDI_IMPLEMENTATION_PLAN.md` revision 5 with the Q13
answer. Target specification: `knowledge_files/specification_reference/MIDI_SPEC.md`.
No code has changed. This document lists every code change the plan needs,
by file, line and action, with the comment block that goes beside it.*

---

## 0. How to use this schedule

### 0.1 Conventions

- **Line numbers** are those of the current tree (`f10e797`). Phases are
  applied in order A → H, and earlier phases move later lines. Every change
  therefore also quotes an **anchor** (a function name or a unique line of
  text). Re-locate by the anchor when a number has moved. Inside one file,
  applying edits bottom-up keeps the remaining numbers valid.
- **Actions:** **ADD** (new code; the line given is where it goes),
  **REMOVE** (delete the lines given), **MODIFY** (replace the lines given),
  **NEW FILE**, **DELETE FILE**, **MOVE FILE**.
- **Comment blocks** are written to be pasted as they stand. Each one states
  What, Why, Inputs, Outputs, Accessors (callers) and Affiliates (code it
  works with). The `.h` declaration carries the full contract block; the
  `.c` definition carries a short block that says "Contract in X.h" plus any
  implementation notes. That is the project's existing two-file pattern
  (`presetManager.h`/`.c`, `EffectsManager.h`/`.c`).
- **Code** is given in full for new files and new functions. For edits to
  long existing functions, the replacement lines are given in full and the
  unchanged context is described.
- **Copyright headers** of new files use the project's "Modifications
  Copyright 2026 Brendan Clarke" block (as in `FIFO.h` lines 6-16). Files
  derived from Julian Schmidt's originals (`MidiDin` from `Uart.c`,
  `MidiBuffer` from `FIFO.c`, `MidiIn`/`MidiTransport` from `MidiParser.c`,
  `MidiDefs.h` from `MidiMessages.h`, `MidiPitch.h` from
  `MidiNoteNumbers.h`) keep the original LXR licence block above it.
- **Implementation notes:** per the working method, append notes to §12 of
  this document as each step lands (deviations, measured sizes, test
  results).

### 0.2 Terms

As in the plan: **track** T1..T7 (index 0..6), **voice/slot** V1..V6
(index 0..5; tracks 6 and 7 both play slot 5), **G** the global MIDI channel
(1..16, 0 = off), **ec(t)** a track's effective channel (its own channel, or
G when unassigned), **internal note** (63 = default pitch), **consumed**
(the LXR acted on a received message), **origin** (panel or MIDI).

Channel numbers inside the new code are **1..16 with 0 = off/unassigned**.
The wire channel nibble is `channel − 1`. This removes the zero-based /
one-based mixing of the old `midi_MidiChannels[]` and the
`scene_getTrackMidiChannel() − 1` calls.

---

## 1. Items to confirm before implementing

The deep dive produced the items below. Items marked **RAM** need an
acknowledgement under the RAM Allocation Approval Policy. The others are
consequences of decisions already taken, applied in this schedule as
stated. Each can be vetoed; the schedule then changes in the step named.

### 1.1 RAM deltas beyond the approved figures

| Id | Item | Approved | Scheduled | Delta | Why | Alternative |
|----|------|---------:|----------:|------:|-----|-------------|
| R-A | Per-track overlay repeat table (Q5) plus held note | 42 + 7 = 49 B | 7 × 8 B = **56 B** | **+7 B** | The plan's Q5 row lists five fields (velocity 1, count 1, interval 2, next 2, flags 1) = 7 B per track but wrote 6 B. With the held MIDI note that is 8 B per track. The flags byte is the internal note with the `def` flag in bit 7, so there is no padding. | None smaller without losing a field. |
| R-B | TX note-off per track | 7 B | 7 × 2 B = **14 B** | **+7 B** | A note-off must go to the channel its note-on used. The channel can change between them (Scene change, `chn`/`ch` edit, Scene Load). One byte per track holds the note (bit 7 = sounding); a second holds the channel. Still 4 B less than the 18 B it replaces. | 7 B: send every track's note-off whenever any channel can change (whole/per-track Scene change, `ch`/`chn` edits, Scene Load apply). More hooks, and a missed hook leaves a hanging note downstream. |
| R-C | Resume position for Continue and SPP (T1) | — | **4 B** (`seq_resumeTick`, `uint32_t`, Sequencer) | **+4 B** | Stop resets the master clock to 0 (`seq_resetStepScheduler()`), so Continue has nothing to resume from, and SPP needs somewhere to write while stopped. A 16-bit count of 16th notes would wrap after 1,024 bars and lose track phase for lengths that do not divide 16,384. | None that keeps Continue exact. |
| R-D | DIN framer's in-progress message | — | **+2 B** | **+2 B** | `MidiMsg` grows from 4 B to 6 B with the arrival stamp (Q13). The framer keeps one static in-progress message (`midiMsg_tmp` today). | None; the stamp must live somewhere while a message is assembled. |

**Total over the approved list: +20 B SRAM1, static, runtime lifetime.**
Owners as named. Please acknowledge R-A to R-D (or choose an alternative)
before Phase A starts. Sizes are re-measured at each phase and recorded in
§12 and `STORAGE_SRAM_MANIFEST.md`.

### 1.2 Consequences applied (veto if not wanted)

| Id | Consequence | Where | Reason |
|----|-------------|-------|--------|
| C-1 | **A track's `nte` is a MIDI root only.** The overlay `def` note (`perf_note_lt/rt` = 0) and the stopped-transport VOICE preview play the default pitch (internal 63), no longer the track's MIDI note. Before S083, a track with `nte` 36 played its overlay and preview hits at internal note 36, which is 27 semitones down. | B-16, G-6 | The note map (§3.3) makes `nte` the receive root, with pitch = 63 + offset. Using `nte` as a pitch elsewhere would make the same setting mean two things. |
| C-2 | The sequencer's outgoing note is the **inverse map**, not "`nte` if set, else the step note". | G-5 | §3.3/§3.9. |
| C-3 | MIDI-in note triggers **are transmitted** (TX N), on the inverse-mapped note. | B-12 | MIDI_SPEC §9 N lists "MIDI-in triggers"; the no-echo rule covers parameter changes, PC and CC0. |
| C-4 | While Learn waits for a target, touching a cell **without** a MIDI address (BPM, settings, step data) edits it normally; only an addressed cell is captured without an edit (Q10). | F-9, F-10 | MIDI_SPEC §6.3 says such a touch "is ignored, and learn keeps waiting"; ignoring it for Learn while still editing is the least surprising reading. |
| C-5 | A MIDI PC on G outside PERF mode repaints the PERF Scene LEDs **only in PERF mode**. `menu_perfSceneChangeExecute()` calls `menu_refreshPerfSceneLeds()` unconditionally today, which also affects chain and queued changes fired outside PERF. | D-6 | P2 says "in any state"; without the guard, a PC in STEP mode would overwrite the SEQ row. This also fixes the same effect for chain and queued changes fired outside PERF (reported as found, not a side quest: the PC path cannot work without it). |
| C-6 | The TX inverse uses each track's **played** Scene's channel/note settings (S077 P2 per-track output), while RX uses the **active** Scene (R10). | G-5 | Keeps the S077 rule that a track sends on the channel of the Scene it plays. With no per-track override the two are the same Scene. |
| C-7 | NRPN data CCs (6, 96, 97) are consumed only when the selected number is a valid target on that channel in the active Scene. CC 98, 99, 100, 101 and 38 are always consumed on G or a voice channel. | E-4 | Reconciles §3.2 ("protocol CCs always consumed") with C1 ("unused IDs are ignored and forwarded"). |
| C-8 | A per-track PC is not transmitted on a channel equal to G. | G-8 | A PC on G means a whole-Scene change to a receiver. |
| C-9 | When both a held BAR (PERF) and CC2 select an overlay, the **held BAR wins**. | B-10 | N6 lists both sources but no order. |
| C-10 | SysEx forwarded to USB goes as single-byte packets (USB-MIDI code index 0xF), one byte per packet. | A-24 | No packing state (no RAM). The DIN framer delivers SysEx one byte at a time. |
| C-11 | Start/Stop/Continue received while slaved are forwarded per MRT but **not** re-sent by the LXR's own transport TX (no duplicate downstream). | C-3 | §3.2: "While slaved, the LXR does not generate its own clock". The same applies to transport. |

---

## 2. RAM ledger (all SRAM1, static, runtime lifetime unless noted)

| Item | Owner | Bytes | Status |
|------|-------|------:|--------|
| Learn table 128 × 4 B + count 1 + state 1 + target 2 + FUL deadline 2 | MidiLearn | 518 | Approved 2026-10-17 (~519) |
| Per-track overlay table 7 × 8 B (held MIDI note, internal note/def, velocity, count, interval, next) | buttonHandler | 56 | 49 approved; **+7 R-A** |
| Retired overlay single slot (`bh_overlayTrack/Note/Vel/Count/Interval/Next/NoteDefault`) | buttonHandler | −9 | Release |
| NRPN RX contexts 2 ports × 8 × 3 B | MidiControl | 48 | Approved |
| NRPN TX number cache 2 × 2 B + pending target 2 B + last send ms 2 B | MidiOut | 8 | Approved |
| CC2 state | MidiNotes | 1 | Approved |
| TX note-off per track 7 × 2 B | MidiOut | 14 | 7 approved; **+7 R-B** |
| Retired `midi_chan_notes[16]` + `midi_notes_on` | Sequencer | −18 | Release |
| DIN RX arrival stamps 64 × 2 B | MidiBuffer (DIN RX) | 128 | Approved (Q13) |
| DIN framer in-progress `MidiMsg` 4 → 6 B | MidiIn | +2 | **R-D** |
| USB RX ring `MIDI_USB_RX_RING_SIZE` (128) × 6 B, was 256 × 4 B | usb_midi_core | 768 (−256) | Approved (Q13 (b)) |
| Queued Scene change MIDI origin | Sequencer | 1 | Approved (Q13) |
| Resume position | Sequencer | 4 | **R-C** |
| Retired `midiParser_originalCcValues[255]` | — | −255 | Release |
| Retired `midi_NoteOverride[7]` | — | −7 | Release |
| `midi_MidiChannels[8]` → `midiIn_config.global` (1 B) | MidiIn | −7 | Release |
| Retired `midiParser_activeNrpnNumber` (2), `midiParser_selectedLfoVoice[6]` | — | −8 | Release |
| Unchanged, moved: realtime ring 32 × 8 B + head/tail/drop (MidiTransport); DIN TX/RT-TX buffers 2 × 66 B; RX data 66 B (MidiBuffer); routing/filter bytes (MidiIn config, 3 B total incl. G); MTC state 6 B and AUTO sync 5 B (MidiTransport) | various | 0 | Move |

Net SRAM1 change: about **+220 B**. New: 780 B (Learn 518, overlay table
56, NRPN 48, TX cache 8, CC2 1, note-off 14, DIN stamps 128, framer +2, PC
origin 1, resume 4). Released: 560 B (USB ring 256, legacy arrays 277,
channel note cache 18, overlay single slot 9). Of the +220 B, +20 B are
the R-A..R-D items above; the rest was approved on 2026-10-17 and
2026-10-21. A release does not authorise reuse (policy). The exact
`bss`/`data` numbers are recorded per phase in §12.

Flash: about +12 KB new code, about −9 KB removed (the legacy CC map and
`midiParser_ccDispatch()` alone are ~1,000 lines). The S082 close has
155,124 B free.

---

## 3. File map

### 3.1 `Core/MIDI/` after S083

| File | Action | Prefix | Phase |
|------|--------|--------|-------|
| `MidiDefs.h` | NEW FILE (replaces `MidiMessages.h`) | — | A |
| `MidiBuffer.c/h` | NEW FILE (replaces `FIFO.c/h`) | `midiBuffer_` | A |
| `MidiDin.c/h` | NEW FILE (replaces `Uart.c/h`) | `midiDin_` | A |
| `MidiTransport.c/h` | NEW FILE (replaces `MidiRealtime.c/h` + realtime/MTC parts of `MidiParser.c`) | `midiTransport_` | A (C adds Continue/SPP) |
| `MidiIn.c/h` | NEW FILE (receive half of `MidiParser.c`) | `midiIn_` | A |
| `MidiOut.c/h` | NEW FILE | `midiOut_` | A (send primitives, forward, realtime, legacy-equivalent notes/PC); G (full TX) |
| `MidiPitch.h` | NEW FILE (replaces `MidiNoteNumbers.h`) | `midiPitch_` | A |
| `MidiNoteMap.c/h` | NEW FILE | `midiNoteMap_` | B |
| `MidiNotes.c/h` | NEW FILE | `midiNotes_` | B |
| `MidiProgram.c/h` | NEW FILE | `midiProgram_` | D |
| `MidiControl.c/h` | NEW FILE | `midiControl_` | E |
| `MidiLearn.c/h` | NEW FILE | `midiLearn_` | F |
| `MidiVoiceControl.c/h` | KEEP (one include line changes) | `voiceControl_` | A |
| `FIFO.c/h`, `Uart.c/h`, `MidiParser.c/h`, `MidiRealtime.c/h`, `MidiMessages.h`, `MidiNoteNumbers.h` | DELETE FILE | — | A |
| `valueShaper.h` | MOVE FILE → `Core/DSPAudio/valueShaper.h` (content unchanged) | — | A |

Phases B, D, E and F add their modules then; Phase A creates no stubs, so
every phase builds on its own.

### 3.2 Host test

| File | Action | Phase |
|------|--------|-------|
| `tools/midi_test/notemap_test.c` | NEW FILE | B |
| `tools/midi_test/Makefile` | NEW FILE | B |

### 3.3 Files outside `Core/MIDI/` that change

| File | Phases | What |
|------|--------|------|
| `main.c` | A | includes, init, `midi_service()` body |
| `config.h` | A, B, E, F, G | new MIDI configuration macros |
| `Makefile` | A, B, D, E, F | MIDI source list |
| `Core/Hardware/USB/App/usb_midi_core.c/h`, `usb_manager.c/h` | A | ring size from `config.h`, stamp, drop-newest, packet send |
| `Core/Sequencer/sequencer.c/h` | A, C, D, G | TX moves to MidiOut, transport Continue/SPP, per-track Scene helper, PC origin |
| `Core/Sequencer/sequencerTimer.c` | A | realtime call |
| `Core/Sequencer/clockSync.c/h` | C | Continue |
| `Core/Sequencer/LiveRecord.c/h` | B | arrival age |
| `Core/Hardware/frontPanel/buttonHandler.c/h` | A, B, D, F | legacy remnant removal, per-track overlay executor, per-track PC helper, Learn gesture and LEDs |
| `Core/Menu/menu.c/h` | A, D, E, F, G | MIDI settings, shared commit, A1 fix, Learn capture/display, parameter TX hook |
| `Core/Menu/MenuText.h` | A | MRT names, `glb`/`def` text |
| `Core/Bank/Scene/SceneData.c/h` | A | channel 0 = unassigned, note 0 = `def` defaults |
| `Core/Hardware/SD/storageTypes.c` | A | `midi_channel` accepts 0 |
| `Core/Hardware/SD/filesystem.c` | A, F | defaults, G off sanitize, `learn=` lines, terminator |
| `Core/Bank/Scene/Preset/presetManager.c/h` | A, E | remove legacy CC application and the MIDI endpoint setter |
| `Core/DSPAudio/Oscillator.c` | A | detune helper |
| `Core/Bank/Scene/Pattern/SomGenerator.c`, `Core/DSP/Instruments/InstrumentManager.c`, `Core/DSPAudio/lfo.c`, `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c` | A | include renames |

---

## 4. Phase A — Rebuild `Core/MIDI/`, cleanup, consume-then-route

**Goal.** Replace the old MIDI files with the new module layout, keeping
clock, transport, MTC and pass-through behaviour, and make the settings
changes the later phases depend on (G off, track channel unassigned, the
new MRT list). Remove the legacy CC map and its callers.

**State after Phase A.** Clock sync, Start/Stop, MTC and routing work as
before (routing now per the new MRT list, and after consumption). Received
notes, CCs, NRPN and PC are not acted on yet: there are no consumers until
Phases B, D and E, so these messages are forwarded per MRT. Sequencer note,
clock, transport and PC output work through `MidiOut`.

**Order inside the phase.** A-1 (config) → A-2..A-8 (new files) → A-9..A-11
(old files, Makefile) → A-12..A-30 (callers). The build is broken between
A-9 and A-30; build only at the end of the phase.

### A-1 `config.h` — MIDI configuration block

**Where:** `config.h:290`, after `#define LIVEREC_AUTOMATION_FIRST_CHANGE 1`.
**Action:** ADD.

```c
/*
 * MIDI configuration (S083).
 *
 * What: compile-time sizes and budgets for the rebuilt Core/MIDI modules.
 *  - MIDI_USB_RX_RING_SIZE: entries in the USB-in message ring
 *    (usb_midi_core.c). Each entry is one 6-byte MidiMsg. Power of two,
 *    at most 256 (the read/write indices are uint8_t).
 *  - MIDI_USB_DRAIN_PER_PASS / MIDI_DIN_DRAIN_PER_PASS: messages (USB) and
 *    bytes (DIN) midiIn_service() takes per main-loop pass.
 *  - MIDI_ARRIVAL_MAX_AGE_US: oldest arrival age (µs) used for live-record
 *    placement; older notes fall back to processing time (the 16-bit stamp
 *    wraps every 65.5 ms).
 * Why: S083 Q13 (b) keeps the USB ring scalable here. 128 entries hold
 * eight full USB packets of 16 events; with drop-newest on overflow and a
 * 16-message drain per pass a dense DAW stream is not lost.
 * RAM: MIDI_USB_RX_RING_SIZE x 6 B SRAM1 (768 B at 128, accepted
 * 2026-10-21). Raising it is a new RAM allocation and needs the user's
 * acknowledgement (MEMORY.md RAM Allocation Approval Policy).
 * Affiliates: usb_midi_core.c/h, MidiIn.c, MidiNotes.c (B), LiveRecord.c.
 */
#define MIDI_USB_RX_RING_SIZE      128u
#define MIDI_USB_DRAIN_PER_PASS    16u
#define MIDI_DIN_DRAIN_PER_PASS    16u
#define MIDI_ARRIVAL_MAX_AGE_US    20000u
```

Later phases append to this block: B-1 (none), E-1 (`MIDI_NRPN_CONTEXTS`),
F-1 (`MIDI_LEARN_MAX_ENTRIES`, `MIDI_LEARN_FULL_SHOW_MS`), G-1
(`MIDI_OUT_PARAM_INTERVAL_MS`).

### A-2 `Core/MIDI/MidiDefs.h` — NEW FILE (replaces `MidiMessages.h`)

Licence: the original LXR block from `MidiMessages.h` lines 1-35, then the
Brendan Clarke modifications block.

```c
#ifndef MIDIDEFS_H_
#define MIDIDEFS_H_

/*
 * MidiDefs.h — shared MIDI vocabulary for the LXR-02 (S083).
 *
 * What: the message type passed between the MIDI modules, status bytes,
 * controller numbers, the reserved-CC rule, the RXF/TXF filter bits, the
 * MRT routing table, channel and origin conventions, and the target-ID
 * range shared with step automation and LiveRecord.
 * Why: replaces MidiMessages.h, whose legacy LXR CC/CC2 parameter maps are
 * retired with the NRPN/Learn rework (plan §3.1, Phase A). Every Core/MIDI
 * module and the USB driver include this header; nothing in it allocates.
 * Conventions: channel numbers are 1..16 with 0 = off/unassigned; the wire
 * nibble is channel - 1. Target IDs are the canonical 9-bit IDs (0..383
 * voices, 384..447 Scene, 448..510 Effect, 511 off).
 * Affiliates: MidiIn, MidiOut, MidiTransport, MidiNotes, MidiControl,
 * MidiLearn, MidiProgram, usb_midi_core.c/h, usb_manager.c/h.
 */

#include <stdint.h>

/* Source port of a received message (stored in MidiBits.source). */
enum MidiSource {
	midiSourceMIDI = 0,	/* DIN */
	midiSourceUSB  = 1
};

/* Output port mask for midiOut_sendMsg(). */
#define MIDI_PORT_DIN   0x01u
#define MIDI_PORT_USB   0x02u
#define MIDI_PORT_BOTH  (MIDI_PORT_DIN | MIDI_PORT_USB)

/*
 * Framing bits of one message.
 *
 * source: the port it arrived on. sysxbyte: 1 when the message is SysEx
 * payload passed through byte-wise (DIN) or packet-wise (USB, length = the
 * packet's byte count - 1); consumers ignore it, routing forwards it.
 * length: data bytes after the status byte (0..2).
 */
struct MidiBits {
	enum MidiSource source:1;
	unsigned sysxbyte:1;
	unsigned length:2;
	unsigned :4;
};

/*
 * One MIDI message (S083: 6 bytes, was 4).
 *
 * stamp: low 16 bits of TIM2 µs at arrival (S083 Q6/Q13). DIN: the first
 * byte of the message; USB: the packet. Only live-record placement reads
 * it (MidiNotes -> liveRec_triggerIn() age). LXR-generated messages leave
 * it 0.
 */
typedef struct MidiStruct {
	uint8_t status;
	uint8_t data1;
	uint8_t data2;
	struct MidiBits bits;
	uint16_t stamp;
} MidiMsg;

/* Channel voice status bytes (high nibble). */
#define NOTE_OFF            0x80u
#define NOTE_ON             0x90u
#define MIDI_AT             0xA0u	/* polyphonic key pressure */
#define MIDI_CC             0xB0u
#define PROG_CHANGE         0xC0u
#define CHANNEL_PRESSURE    0xD0u
#define MIDI_PITCH_WHEEL    0xE0u

/* System common and realtime. */
#define SYSEX_START         0xF0u
#define MIDI_MTC_QFRAME     0xF1u
#define MIDI_SONG_POSITION  0xF2u	/* S083 Phase A: framed (2 data bytes) */
#define MIDI_SONG_SEL       0xF3u
#define MIDI_TUNE_REQUEST   0xF6u
#define SYSEX_END           0xF7u
#define MIDI_CLOCK          0xF8u
#define MIDI_START          0xFAu
#define MIDI_CONTINUE       0xFBu
#define MIDI_STOP           0xFCu
#define MIDI_ACTIVE_SENSE   0xFEu
#define MIDI_SYSTEM_RESET   0xFFu

/* Controller numbers with a fixed meaning (MIDI_SPEC §5.1). */
#define CC_BANK_CHANGE       0u
#define CC_MOD_WHEEL         1u
#define CC_NOTE_ACTION       2u	/* S083 C8: 0 single, 1 BAR <, 2 BAR > */
#define CC_DATA_ENTRY_MSB    6u
#define CC_BANK_SELECT_LSB   32u
#define CC_DATA_ENTRY_LSB    38u
#define CC_DATA_INCREMENT    96u
#define CC_DATA_DECREMENT    97u
#define CC_NRPN_LSB          98u
#define CC_NRPN_MSB          99u
#define CC_RPN_LSB           100u
#define CC_RPN_MSB           101u
#define CC_CHANNEL_MODE_FIRST 120u
#define CC_ALL_SOUND_OFF     120u
#define CC_ALL_NOTES_OFF     123u

/*
 * Reserved controllers (MIDI_SPEC §5.1, plan C6).
 *
 * What: 1 for a CC that can never be learned: 0, 1, 2, 6, 32, 38,
 * 96..101 and the channel-mode CCs 120..127. Why: these CCs have fixed
 * meanings (Bank change, Morph, note action, NRPN/RPN protocol, Bank LSB,
 * channel mode). Inputs: controller number 0..127. Output: 0/1.
 * Accessors: MidiLearn (capture and lookup), MidiControl.
 */
static inline uint8_t midiDefs_ccReserved(uint8_t cc)
{
	return (uint8_t)(cc <= CC_NOTE_ACTION || cc == CC_DATA_ENTRY_MSB ||
	                 cc == CC_BANK_SELECT_LSB || cc == CC_DATA_ENTRY_LSB ||
	                 (cc >= CC_DATA_INCREMENT && cc <= CC_RPN_MSB) ||
	                 cc >= CC_CHANNEL_MODE_FIRST);
}

/* RXF/TXF filter bits (settings midi_filt_rx/tx, menu midiFilterNames). */
#define MIDI_FILTER_NOTE      0x01u
#define MIDI_FILTER_REALTIME  0x02u
#define MIDI_FILTER_CONTROL   0x04u
#define MIDI_FILTER_PROGRAM   0x08u

/*
 * MRT routing (MIDI_SPEC §3.3, plan F22).
 *
 * What: the eight menu values off UtD DtD DtU bth DtA AtD all and the three
 * routes they combine. USB -> USB is never routed. Inputs: menu value 0..7
 * (out of range = off). Output: MIDI_ROUTE_* mask. Accessors:
 * midiIn_setRouting(). Affiliates: midiRoutingNames (MenuText.h),
 * midiOut_forward(), midiOut_forwardRealtime().
 */
#define MIDI_ROUTE_USB_TO_DIN  0x01u
#define MIDI_ROUTE_DIN_TO_DIN  0x02u
#define MIDI_ROUTE_DIN_TO_USB  0x04u

static inline uint8_t midiDefs_routeMask(uint8_t mrt)
{
	static const uint8_t table[8] = {
		0u,                                             /* off */
		MIDI_ROUTE_USB_TO_DIN,                          /* UtD */
		MIDI_ROUTE_DIN_TO_DIN,                          /* DtD */
		MIDI_ROUTE_DIN_TO_USB,                          /* DtU */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_USB,  /* bth */
		MIDI_ROUTE_DIN_TO_DIN | MIDI_ROUTE_DIN_TO_USB,  /* DtA */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_DIN,  /* AtD */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_DIN |
		    MIDI_ROUTE_DIN_TO_USB                       /* all */
	};
	return (mrt < 8u) ? table[mrt] : 0u;
}

/* Channels: 1..16, 0 = off (G) / unassigned (track). */
#define MIDI_CHANNEL_NONE   0u
#define MIDI_CHANNEL_COUNT  16u

/* Origin of a change: echo suppression and record exclusions (S083 Q1/Q9). */
#define MIDI_ORIGIN_PANEL   0u
#define MIDI_ORIGIN_MIDI    1u

/* 9-bit target IDs (plan C1). 511 is "off" and never a target. */
#define MIDI_TARGET_OFF     511u

#endif /* MIDIDEFS_H_ */
```

### A-3 `Core/MIDI/MidiBuffer.h` / `MidiBuffer.c` — NEW FILES (replace `FIFO.h/.c`)

Licence: the `FIFO.c` block (lines 1-15).

**`MidiBuffer.h`:**

```c
#ifndef MIDIBUFFER_H_
#define MIDIBUFFER_H_

/*
 * MidiBuffer — single-producer single-consumer byte buffers (S083).
 *
 * What: the DIN transmit buffers (normal and realtime lanes) and the DIN
 * receive buffer, which also holds a 16-bit arrival stamp per byte.
 * Why: replaces FIFO.c/h (user request, S083 Q7) and adds the receive
 * stamps that live-record placement needs (S083 Q6, Q13).
 * Contract: one producer and one consumer per buffer. The producer writes
 * the payload (and stamp) before publishing the write index; the consumer
 * reads before publishing the read index. One slot is kept free to tell
 * full from empty, so a 64-entry buffer holds 63 bytes (as FIFO.c did).
 * RAM: midiBuffer_t 66 B; midiBuffer_rx_t 194 B (66 + 128 stamp bytes,
 * accepted 2026-10-21, Q13). Instances live in MidiDin.c.
 * Affiliates: MidiDin.c (ISR side), MidiIn.c (receive consumer).
 */

#include <stdint.h>

#define MIDI_BUFFER_SIZE  64u
#define MIDI_BUFFER_MASK  (MIDI_BUFFER_SIZE - 1u)

typedef struct {
	volatile uint8_t data[MIDI_BUFFER_SIZE];
	volatile uint8_t read;
	volatile uint8_t write;
} midiBuffer_t;

typedef struct {
	midiBuffer_t bytes;
	volatile uint16_t stamp[MIDI_BUFFER_SIZE];
} midiBuffer_rx_t;

/* Empty the buffer. Call before the producer or consumer runs (boot). */
void    midiBuffer_init(midiBuffer_t *buf);
/* Append one byte. Returns 1 when stored, 0 when full (byte dropped). */
uint8_t midiBuffer_in(midiBuffer_t *buf, uint8_t byte);
/* Take the oldest byte. Returns 1 with *byte set, 0 when empty. */
uint8_t midiBuffer_out(midiBuffer_t *buf, uint8_t *byte);
/* Free slots (0..MIDI_BUFFER_SIZE-1). Exact only under the producer's
** lock; used by MidiDin to queue whole messages or nothing. */
uint8_t midiBuffer_free(const midiBuffer_t *buf);
/* Receive variants: the stamp travels with its byte. */
void    midiBuffer_rxInit(midiBuffer_rx_t *buf);
uint8_t midiBuffer_rxIn(midiBuffer_rx_t *buf, uint8_t byte, uint16_t stamp);
uint8_t midiBuffer_rxOut(midiBuffer_rx_t *buf, uint8_t *byte,
                         uint16_t *stamp);

#endif /* MIDIBUFFER_H_ */
```

**`MidiBuffer.c`:**

```c
#include "MidiBuffer.h"
#include <string.h>

/* Contracts in MidiBuffer.h. Logic carried over from FIFO.c. */
void midiBuffer_init(midiBuffer_t *buf)
{
	memset((void *)buf->data, 0, MIDI_BUFFER_SIZE);
	buf->read = 0u;
	buf->write = 0u;
}

uint8_t midiBuffer_in(midiBuffer_t *buf, uint8_t byte)
{
	uint8_t next = (uint8_t)((buf->write + 1u) & MIDI_BUFFER_MASK);

	if (next == buf->read)
		return 0u;
	buf->data[buf->write] = byte;
	buf->write = next;
	return 1u;
}

uint8_t midiBuffer_out(midiBuffer_t *buf, uint8_t *byte)
{
	if (buf->read == buf->write)
		return 0u;
	*byte = buf->data[buf->read];
	buf->read = (uint8_t)((buf->read + 1u) & MIDI_BUFFER_MASK);
	return 1u;
}

uint8_t midiBuffer_free(const midiBuffer_t *buf)
{
	return (uint8_t)((buf->read - buf->write - 1u) & MIDI_BUFFER_MASK);
}

void midiBuffer_rxInit(midiBuffer_rx_t *buf)
{
	midiBuffer_init(&buf->bytes);
	memset((void *)buf->stamp, 0, sizeof(buf->stamp));
}

/*
 * The stamp is written into the slot before midiBuffer_in() publishes the
 * write index, so the consumer never reads a byte with a stale stamp.
 */
uint8_t midiBuffer_rxIn(midiBuffer_rx_t *buf, uint8_t byte, uint16_t stamp)
{
	buf->stamp[buf->bytes.write] = stamp;
	return midiBuffer_in(&buf->bytes, byte);
}

uint8_t midiBuffer_rxOut(midiBuffer_rx_t *buf, uint8_t *byte,
                         uint16_t *stamp)
{
	uint8_t slot = buf->bytes.read;

	if (!midiBuffer_out(&buf->bytes, byte))
		return 0u;
	*stamp = buf->stamp[slot];
	return 1u;
}
```

### A-4 `Core/MIDI/MidiDin.h` / `MidiDin.c` — NEW FILES (replace `Uart.h/.c`)

Licence: the `Uart.c` blocks (lines 1-33).

**`MidiDin.h`:**

```c
#ifndef MIDIDIN_H_
#define MIDIDIN_H_

/*
 * MidiDin — the MIDI DIN port driver (USART3, PB10 TX / PB11 RX, 31250
 * baud, AF7; S083, was Uart.c).
 *
 * What: interrupt-driven receive and transmit. The RX ISR stamps every byte
 * with TIM2 µs; realtime bytes (0xF8..0xFF) go to the MidiTransport ring
 * with their full stamp, every other byte goes to the receive buffer with
 * the low 16 bits of the stamp. Transmit has a realtime lane that drains
 * before the normal lane, so clock is never queued behind notes.
 * Why: S083 Q7 rebuild; Q6/Q13 arrival stamps; whole-message queuing so a
 * full buffer never leaves half a message on the wire (new, replaces the
 * byte-wise uart_sendMidi()).
 * Context: RX/TX in USART3_IRQHandler (priority 5); senders from the
 * foreground and TIM3 under a short PRIMASK section.
 * Affiliates: MidiBuffer, MidiTransport (realtime push), MidiIn (receive
 * consumer), MidiOut (only sender).
 */

#include <stdint.h>

void     midiDin_init(void);
/*
 * Queue `count` bytes (1..3, or one realtime byte) as one unit.
 * Inputs: bytes and count. Output: 1 when all bytes were queued, 0 when the
 * lane had too little room (nothing queued; the drop counter counts it).
 * A single byte >= 0xF8 goes to the realtime lane. Context: any (PRIMASK).
 * Accessor: MidiOut only.
 */
uint8_t  midiDin_sendBytes(const uint8_t *bytes, uint8_t count);
/* Free bytes in the normal transmit lane (MidiOut's parameter throttle). */
uint8_t  midiDin_txFree(void);
/* Take one received byte and its 16-bit arrival stamp (MidiIn). */
uint8_t  midiDin_rxPop(uint8_t *byte, uint16_t *stamp);
/* Diagnostics: messages refused for lack of room, per lane. */
uint32_t midiDin_txDropCount(void);
uint32_t midiDin_realtimeTxDropCount(void);

#endif /* MIDIDIN_H_ */
```

**`MidiDin.c`:** port `Uart.c` lines 36-275 with these changes:

| `Uart.c` | `MidiDin.c` |
|----------|-------------|
| 36-40 includes | `#include "MidiDin.h"`, `"MidiBuffer.h"`, `"MidiDefs.h"`, `"MidiTransport.h"`, `"timebase.h"` (no `MidiParser.h`) |
| 42-99 register/GPIO/NVIC defines | unchanged |
| 104-106 `static Fifo ...` | `static midiBuffer_t midiDin_tx; static midiBuffer_t midiDin_realtimeTx; static midiBuffer_rx_t midiDin_rx;` |
| 108-109 drop counters | `midiDin_txDrops`, `midiDin_realtimeTxDrops` |
| 111-121 `uart_irqSave/Restore` | `midiDin_irqSave/Restore` (same bodies) |
| 123-129 `uart_isRealtimeByte` | `midiDin_isRealtimeByte` |
| 134-166 ISR | see below |
| 173 `initMidiUart` | `midiDin_init` (body unchanged except the buffer inits: `midiBuffer_init(&midiDin_tx); midiBuffer_init(&midiDin_realtimeTx); midiBuffer_rxInit(&midiDin_rx);`) |
| 220-254 `uart_sendMidiByte`/`uart_sendMidi` | `midiDin_sendBytes()` below |
| 259-265 `uart_processMidi` | REMOVE (MidiIn drains through `midiDin_rxPop()`) |
| 267-275 drop getters | renamed |

ISR receive branch (replaces `Uart.c` 144-155):

```c
	if (isr & ISR_RXNE) {
		uint32_t timestampUs = timebase_tim2Now();
		data = (uint8_t)USART3_RDR;
		/*
		 * Receive one DIN byte (S019 realtime path; S083 Q6/Q13 stamps).
		 *
		 * What: realtime bytes go to the TIM3 realtime ring with their full
		 * stamp and never enter the byte framer (running status survives
		 * them). Every other byte is stored with the low 16 bits of its
		 * arrival time. Why: live-record placement uses the arrival time of
		 * a note, not the time the foreground framed it. A full buffer drops
		 * the byte (as FIFO did). Affiliates: midiTransport_push(),
		 * midiBuffer_rxIn(), midiIn_service().
		 */
		if (midiTransport_isRealtime(data))
			midiTransport_push(data, MIDI_REALTIME_SOURCE_DIN, timestampUs);
		else
			(void)midiBuffer_rxIn(&midiDin_rx, data, (uint16_t)timestampUs);
	}
```

ISR transmit branch (`Uart.c` 157-165): rename the buffers only.

New senders (replace `Uart.c` 217-254):

```c
/* Contract in MidiDin.h. */
uint8_t midiDin_sendBytes(const uint8_t *bytes, uint8_t count)
{
	uint32_t primask;
	midiBuffer_t *lane;
	uint8_t i;

	if (!bytes || count == 0u || count > 3u)
		return 0u;
	lane = (count == 1u && midiDin_isRealtimeByte(bytes[0]))
	           ? &midiDin_realtimeTx : &midiDin_tx;
	primask = midiDin_irqSave();
	if (midiBuffer_free(lane) < count) {
		if (lane == &midiDin_realtimeTx)
			midiDin_realtimeTxDrops++;
		else
			midiDin_txDrops++;
		midiDin_irqRestore(primask);
		return 0u;
	}
	for (i = 0u; i < count; i++)
		(void)midiBuffer_in(lane, bytes[i]);
	USART3_CR1 |= CR1_TXEIE;
	midiDin_irqRestore(primask);
	return 1u;
}

uint8_t midiDin_txFree(void)
{
	return midiBuffer_free(&midiDin_tx);
}

uint8_t midiDin_rxPop(uint8_t *byte, uint16_t *stamp)
{
	return midiBuffer_rxOut(&midiDin_rx, byte, stamp);
}
```

### A-5 `Core/MIDI/MidiPitch.h` — NEW FILE (replaces `MidiNoteNumbers.h`)

Licence: the `MidiNoteNumbers.h` block. Content:

```c
#ifndef MIDIPITCH_H_
#define MIDIPITCH_H_

/*
 * MidiPitch.h — pitch constants shared by MIDI, the Sequencer and the DSP
 * oscillators (S083, was MidiNoteNumbers.h).
 *
 * What: the default internal note (63 = the voice's default pitch), the
 * MIDI-note frequency table, and the fine-tune helper.
 * Why: MidiNoteNumbers.h is renamed with the Core/MIDI rebuild (Q7), and
 * midiParser_calcDetune() moves here because MidiParser is deleted and the
 * helper is pure pitch maths (Oscillator.c is its only user).
 * Affiliates: Oscillator.c (frequency, detune), sequencer.c, SceneData.c,
 * filesystem.c, SomGenerator.c, buttonHandler.c, MidiNoteMap.c (63 base).
 */

#include <stdint.h>

/*
 * Default internal note (unchanged value).
 *
 * Inputs: Sequencer, overlay, preview, SOM and the note map use this when
 * no pitch offset applies. Output: 63; the oscillator plays its default
 * pitch, and note - 63 is the offset in semitones (Oscillator.c
 * osc_setBaseNote()). S083: a track's MIDI note (`nte`) is no longer a
 * pitch; it is only the root of the track's receive range (plan §3.3, C-1).
 */
#define MIDI_DEFAULT_TRIGGER_NOTE 63u

/* 2^(1/12): one semitone. */
#define MIDI_PITCH_SEMITONE_UP 1.0594630943592952645618252949463f

/* [copy MidiNoteNumbers.h's MidiNoteFrequencies[] table unchanged] */

/*
 * Fine-tune factor from a 7-bit value (was midiParser_calcDetune()).
 *
 * What: 0..127 maps linearly to about -1/2 .. +1/2 semitone around 1.0.
 * Inputs: value 0..127 (the oscillator's fine-tune byte). Output: frequency
 * multiplier. The old function had two identical branches; the result is
 * the same expression. Accessors: Oscillator.c osc_setBaseNote(),
 * osc_recalcFreq().
 */
static inline float midiPitch_detune(uint8_t value)
{
	const float frac = ((float)value / 127.f) - 0.5f;

	return 1.0f + frac * (MIDI_PITCH_SEMITONE_UP - 1.0f);
}

#endif /* MIDIPITCH_H_ */
```

### A-6 `Core/MIDI/MidiTransport.h` / `MidiTransport.c` — NEW FILES

Licence: the `MidiRealtime.c` and `MidiParser.c` LXR blocks.

**`MidiTransport.h`:**

```c
#ifndef MIDITRANSPORT_H_
#define MIDITRANSPORT_H_

/*
 * MidiTransport — clock, transport and position from MIDI (S083).
 *
 * What: the timestamped realtime ring (was MidiRealtime.c), the TIM3
 * realtime handler with the `snc` source gate (was
 * midiParser_processRealtimeEvents()), Song Position Pointer (Phase C) and
 * MIDI Time Code start/timeout (was in midiParser_parseMidiMessage() and
 * midiParser_checkMtc()).
 * Why: S083 Q7 rebuild. Realtime stays on its own timestamped path so clock
 * and transport never wait behind SysEx, channel messages, LCD or SD work.
 * Context: producers in the USART3 and OTG ISRs; the realtime handler in
 * TIM3 only (binding: never call it from the main loop); SPP/MTC from the
 * foreground dispatcher; midiTransport_checkMtc() from TIM3 and the
 * foreground.
 * Affiliates: MidiDin.c and usb_midi_core.c (push), sequencerTimer.c
 * (TIM3 call), clockSync.c, sequencer.c, MidiIn.c (system common),
 * MidiOut.c (realtime forwarding per MRT).
 */

#include <stdint.h>
#include "MidiDefs.h"

#define MIDI_REALTIME_SOURCE_DIN    ((uint8_t)midiSourceMIDI)
#define MIDI_REALTIME_SOURCE_USB    ((uint8_t)midiSourceUSB)
#define MIDI_REALTIME_SOURCE_PULSE  2u

typedef struct {
	uint8_t status;
	uint8_t source;
	uint32_t timestampUs;
} MidiRealtimeEvent;

/* 1 for Clock, Start, Continue, Stop (the bytes the ring carries). */
uint8_t  midiTransport_isRealtime(uint8_t status);
/* ISR producer: queue one realtime byte; oldest dropped when full. */
void     midiTransport_push(uint8_t status, uint8_t source,
                            uint32_t timestampUs);
uint32_t midiTransport_dropCount(void);
/*
 * TIM3 consumer (binding: TIM3 only).
 * What: for each queued event, forwards it per MRT (always, consumed or
 * not), then, when RXF R allows it and the source is the one `snc`
 * selects, applies it: Clock -> clockSync, Start -> start from the Pattern
 * start, Continue -> (Phase C) resume, Stop -> stop. Budget: 32 events per
 * call. Accessor: TIM3_IRQHandler().
 */
void     midiTransport_processRealtime(void);
/*
 * Foreground: one framed system-common message (MTC quarter frame, Song
 * Position Pointer, Song Select). Output: 1 when it acted (MTC start,
 * SPP stored). Routing is the caller's: transport messages are always
 * forwarded. Accessor: midiIn_dispatch().
 */
uint8_t  midiTransport_handleSystemCommon(const MidiMsg *msg);
/* MTC timeout: stops the sequencer when MTC that started it goes away. */
void     midiTransport_checkMtc(void);

#endif /* MIDITRANSPORT_H_ */
```

**`MidiTransport.c`:** includes `MidiTransport.h`, `MidiIn.h` (RXF),
`MidiOut.h` (forwarding), `sequencer.h`, `clockSync.h`, `buttonHandler.h`
(MTC start/stop LED), `triggerJacks.h` (AUTO jack priority), `timebase.h`.
Assemble from the old code:

| Source | Destination |
|--------|-------------|
| `MidiRealtime.c` 46-121 (ring, `isStatus`, `push`, `pop`, drop count) | `midiTransport_ring`, `midiTransport_isRealtime()`, `midiTransport_push()`, static `midiTransport_pop()`, `midiTransport_dropCount()` (logic unchanged) |
| `MidiParser.c` 63-66 (MTC state) | static `midiTransport_mtcIgnore`, `midiTransport_lastMtcReceived`, `midiTransport_mtcIsRunning` |
| `MidiParser.c` 82-83 (`lastDinSyncUs`, `haveDinSync`) and 333 (`MIDI_AUTO_SYNC_SOURCE_HOLD_US`) | static, unchanged |
| `MidiParser.c` 1682-1700 `midiParser_routeRealtimeEvent()` | REPLACED by `midiOut_forwardRealtime(event->status, event->source)` |
| `MidiParser.c` 1702-1747 `midiParser_realtimeSourceAllowed()` | static `midiTransport_sourceAllowed()`, unchanged |
| `MidiParser.c` 1749-1772 `midiParser_handleRealtimeEvent()` | static `midiTransport_handleEvent()`: filter test becomes `midiIn_rxAllows(MIDI_FILTER_REALTIME)`; Continue stays mapped to Start until C-2 |
| `MidiParser.c` 1774-1781 | `midiTransport_processRealtime()` |
| `MidiParser.c` 1868-1925 (MTC QFRAME case) | inside `midiTransport_handleSystemCommon()` |
| `MidiParser.c` 1509-1538 `midiParser_checkMtc()` | `midiTransport_checkMtc()` |

`midiTransport_handleSystemCommon()` in Phase A:

```c
/* Contract in MidiTransport.h. SPP handling is added in Phase C (C-4). */
uint8_t midiTransport_handleSystemCommon(const MidiMsg *msg)
{
	if (!msg || !midiIn_rxAllows(MIDI_FILTER_REALTIME))
		return 0u;
	switch (msg->status) {
	case MIDI_MTC_QFRAME:
		/* [body of MidiParser.c 1877-1924, with the filter test removed
		** (done above), midiParser_ -> midiTransport_ renames, and
		** `return 1u;` where it calls seq_setRunning(1)] */
		return 0u;
	default:
		return 0u;
	}
}
```

### A-7 `Core/MIDI/MidiOut.h` / `MidiOut.c` — NEW FILES (Phase A scope)

Licence: Brendan Clarke block only (new code).

**`MidiOut.h` (Phase A content; Phase G extends it):**

```c
#ifndef MIDIOUT_H_
#define MIDIOUT_H_

/*
 * MidiOut — everything the LXR transmits (S083).
 *
 * What: the only module that writes to the DIN and USB ports. It frames
 * messages for each port (USB: cable 0 and the correct code index for
 * channel, system-common, SysEx and realtime messages), forwards received
 * messages per MRT, sends LXR-generated realtime, note, program-change,
 * Bank-change and parameter messages under the TXF filter, keeps one
 * note-off record per track, and (Phase G) suppresses echoes and coalesces
 * parameter output.
 * Why: plan §3.1/§3.9. Before S083 the sequencer and the parser each wrote
 * the ports, USB system messages used code index = status >> 4 (wrong for
 * SPP and SysEx), and the note-off cache was one note per channel, so two
 * tracks on one channel could leave a hanging note.
 * Context: foreground and TIM3 (sequencer notes, clock, realtime
 * forwarding). Every port write runs under the driver's PRIMASK section.
 * RAM: per-track note-off 14 B (R-B); Phase G adds 8 B.
 * Affiliates: MidiDin.c, usb_manager.c, MidiIn.c (configuration),
 * sequencer.c, MidiTransport.c, MidiNoteMap.c (G).
 */

#include <stdint.h>
#include "MidiDefs.h"

/* Send one message to the ports in portMask, framed per port. Bypasses TXF:
** callers apply the filter. Context: any. */
void midiOut_sendMsg(const MidiMsg *msg, uint8_t portMask);
/*
 * Forward a received message per MRT (plan §3.2).
 * Inputs: message with bits.source set. Output: copies to the routed
 * ports; never to its own source port's same direction pair for USB
 * (USB -> USB never), DIN -> DIN only when routed. TXF does not apply
 * (pass-through follows MRT only, plan M3). Accessor: midiIn_dispatch().
 */
void midiOut_forward(const MidiMsg *msg);
/* Forward one realtime byte per MRT (TIM3; always, consumed or not). */
void midiOut_forwardRealtime(uint8_t status, uint8_t source);
/* LXR-generated realtime (Clock, Start, Continue, Stop) to both ports,
** under TXF R. Context: TIM3 and foreground. */
void midiOut_realtime(uint8_t status);
/*
 * Track note output (plan §3.9 N).
 * What: sends the track's previous note-off first (if one is sounding),
 * then note-on (channel, note, velocity) under TXF N, and records note and
 * channel for the next note-off. channel 0 sends nothing (no channel).
 * Context: TIM3 and foreground (PRIMASK around the record update).
 * Phase A callers pass the old channel/note rule; Phase G passes the
 * inverse-mapped note (midiOut_trackTrigger()).
 */
void midiOut_trackNoteOn(uint8_t track, uint8_t channel, uint8_t note,
                         uint8_t velocity);
/* Note-off for one track's sounding note, or every track (0xFF). Sent
** regardless of TXF so a note started before a filter change still ends. */
void midiOut_trackNoteOff(uint8_t track);
/* Program change on `channel` (1..16) under TXF P. */
void midiOut_programChange(uint8_t channel, uint8_t program);
/* Foreground service (Phase G: parameter coalescing, CC0 after a load). */
void midiOut_service(void);

#endif /* MIDIOUT_H_ */
```

**`MidiOut.c` (Phase A):**

```c
#include "MidiOut.h"
#include "MidiIn.h"
#include "MidiDin.h"
#include "MidiTransport.h"
#include "usb_manager.h"

/*
 * Per-track note-off record (plan §3.9, R-B).
 *
 * What: midiOut_trackNote[t] bit 7 = a note is sounding, bits 0..6 the note;
 * midiOut_trackChannel[t] the 1..16 channel it was sent on. Why: a
 * note-off must reach the channel and note of its own note-on even when
 * the track's channel or the note map changed in between, and several
 * tracks may share a channel. RAM: 14 B SRAM1 (replaces the sequencer's
 * 16 + 2 B channel cache). Writers: midiOut_trackNoteOn/Off() under
 * PRIMASK (TIM3 and foreground both call them).
 */
static uint8_t midiOut_trackNote[7];
static uint8_t midiOut_trackChannel[7];

static uint32_t midiOut_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void midiOut_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

/*
 * USB-MIDI code index for one message (USB-MIDI 1.0 table 4-1).
 *
 * What: channel messages use status >> 4; MTC and Song Select 0x2; Song
 * Position 0x3; Tune Request 0x5; realtime and SysEx bytes 0xF (single
 * byte, C-10). Why: usb_sendMidi() used status >> 4 for every message, so
 * forwarded SPP, MTC and SysEx were malformed on USB (plan §2 Framing).
 * Inputs: message. Output: code index 0..15.
 */
static uint8_t midiOut_usbCin(const MidiMsg *msg)
{
	if (msg->bits.sysxbyte)
		return 0x0Fu;
	if (msg->status < 0xF0u)
		return (uint8_t)(msg->status >> 4);
	switch (msg->status) {
	case MIDI_MTC_QFRAME:
	case MIDI_SONG_SEL:      return 0x02u;
	case MIDI_SONG_POSITION: return 0x03u;
	case MIDI_TUNE_REQUEST:  return 0x05u;
	default:                 return 0x0Fu;
	}
}

/* Contract in MidiOut.h. */
void midiOut_sendMsg(const MidiMsg *msg, uint8_t portMask)
{
	uint8_t bytes[3];
	uint8_t count;

	if (!msg)
		return;
	bytes[0] = msg->status;
	bytes[1] = msg->data1;
	bytes[2] = msg->data2;
	count = (uint8_t)(1u + msg->bits.length);
	if (portMask & MIDI_PORT_DIN)
		(void)midiDin_sendBytes(bytes, count);
	if (portMask & MIDI_PORT_USB) {
		/*
		 * A USB SysEx packet received from USB is never re-sent to USB
		 * (no USB -> USB route); a DIN SysEx byte goes as one single-byte
		 * packet. Every other message is one packet, unused bytes 0.
		 */
		usb_sendMidiPacket(midiOut_usbCin(msg), bytes[0],
		                   (count > 1u) ? bytes[1] : 0u,
		                   (count > 2u) ? bytes[2] : 0u);
	}
}

void midiOut_forward(const MidiMsg *msg)
{
	uint8_t routes = midiIn_routeMask();
	uint8_t ports = 0u;

	if (!msg || routes == 0u)
		return;
	if (msg->bits.source == midiSourceUSB) {
		if (routes & MIDI_ROUTE_USB_TO_DIN)
			ports |= MIDI_PORT_DIN;
	} else {
		if (routes & MIDI_ROUTE_DIN_TO_DIN)
			ports |= MIDI_PORT_DIN;
		if (routes & MIDI_ROUTE_DIN_TO_USB)
			ports |= MIDI_PORT_USB;
	}
	if (ports)
		midiOut_sendMsg(msg, ports);
}

void midiOut_forwardRealtime(uint8_t status, uint8_t source)
{
	MidiMsg msg = {0};

	msg.status = status;
	msg.bits.source = (source == MIDI_REALTIME_SOURCE_USB)
	                      ? midiSourceUSB : midiSourceMIDI;
	midiOut_forward(&msg);
}
```

```c
void midiOut_realtime(uint8_t status)
{
	MidiMsg msg = {0};

	if (!midiIn_txAllows(MIDI_FILTER_REALTIME))
		return;
	msg.status = status;
	midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
}

void midiOut_trackNoteOff(uint8_t track)
{
	uint8_t first = (track == 0xFFu) ? 0u : track;
	uint8_t last = (track == 0xFFu) ? 6u : track;
	uint8_t t;

	if (track != 0xFFu && track > 6u)
		return;
	for (t = first; t <= last; t++) {
		uint32_t primask = midiOut_irqSave();
		uint8_t rec = midiOut_trackNote[t];
		uint8_t ch = midiOut_trackChannel[t];

		midiOut_trackNote[t] = 0u;
		midiOut_irqRestore(primask);
		if ((rec & 0x80u) && ch >= 1u && ch <= 16u) {
			MidiMsg msg = {0};

			/* 0x80 note-off, velocity 0 (some gear ignores 0x90 v0). */
			msg.status = (uint8_t)(NOTE_OFF | (ch - 1u));
			msg.data1 = (uint8_t)(rec & 0x7Fu);
			msg.bits.length = 2u;
			midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
		}
	}
}

void midiOut_trackNoteOn(uint8_t track, uint8_t channel, uint8_t note,
                         uint8_t velocity)
{
	MidiMsg msg = {0};
	uint32_t primask;

	if (track > 6u)
		return;
	midiOut_trackNoteOff(track);
	if (channel < 1u || channel > 16u || note > 127u ||
	    !midiIn_txAllows(MIDI_FILTER_NOTE))
		return;
	msg.status = (uint8_t)(NOTE_ON | (channel - 1u));
	msg.data1 = note;
	msg.data2 = velocity;
	msg.bits.length = 2u;
	midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
	primask = midiOut_irqSave();
	midiOut_trackNote[track] = (uint8_t)(0x80u | note);
	midiOut_trackChannel[track] = channel;
	midiOut_irqRestore(primask);
}

void midiOut_programChange(uint8_t channel, uint8_t program)
{
	MidiMsg msg = {0};

	if (channel < 1u || channel > 16u || program > 127u ||
	    !midiIn_txAllows(MIDI_FILTER_PROGRAM))
		return;
	msg.status = (uint8_t)(PROG_CHANGE | (channel - 1u));
	msg.data1 = program;
	msg.bits.length = 1u;
	midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
}

void midiOut_service(void)
{
	/* Phase G: parameter coalescing and the Bank-change result (G-9). */
}
```

### A-8 `Core/MIDI/MidiIn.h` / `MidiIn.c` — NEW FILES (Phase A scope)

Licence: the `MidiParser.c` LXR block, then the Brendan Clarke block.

**`MidiIn.h`:**

```c
#ifndef MIDIIN_H_
#define MIDIIN_H_

/*
 * MidiIn — MIDI receive: framing, filters, consumers, routing (S083).
 *
 * What: owns the MIDI settings the receive and transmit sides share
 * (G, RXF/TXF, MRT); frames DIN bytes into messages (running status,
 * SysEx pass-through, SPP and Song Select); takes USB messages from the
 * USB ring; offers each message to its consumers (notes, CCs, PC/CC0,
 * system common) after the RXF gate and the S082 Bank-load drop filter;
 * forwards per MRT every message no consumer acted on.
 * Why: plan §3.2 consume-then-route. Before S083 every message was
 * forwarded before it was parsed, so a consumed note also went out.
 * Context: foreground (midiIn_service() from the main loop).
 * RAM: 3 B configuration + 1 B framer state + 6 B in-progress message.
 * Affiliates: MidiDin (DIN bytes), usb_manager (USB ring), MidiTransport
 * (system common), MidiNotes (B), MidiProgram (D), MidiControl (E),
 * MidiOut (forwarding), menu.c (settings), preset (Bank-load state).
 */

#include <stdint.h>
#include "MidiDefs.h"

/* Menu: G = 0..16 (0 off). Ends every sounding track note first, because
** tracks following G change channel. Caller: menu_parseGlobalParam(). */
void    midiIn_setGlobalChannel(uint8_t channel);
uint8_t midiIn_globalChannel(void);
/* Menu: RXF (isTx 0) or TXF (isTx 1) as the 4-bit N R C P mask. */
void    midiIn_setFilter(uint8_t isTx, uint8_t value);
/* 1 when the RXF (or TXF) mask has `bit` (MIDI_FILTER_*). */
uint8_t midiIn_rxAllows(uint8_t bit);
uint8_t midiIn_txAllows(uint8_t bit);
/* Menu: MRT value 0..7 (midiDefs_routeMask()). */
void    midiIn_setRouting(uint8_t mrt);
uint8_t midiIn_routeMask(void);
/*
 * Main-loop service (replaces main.c midi_service()'s body).
 * What: frames up to MIDI_DIN_DRAIN_PER_PASS DIN bytes, takes up to
 * MIDI_USB_DRAIN_PER_PASS USB messages, dispatches each, then runs
 * midiOut_service() and usb_tick(). Context: foreground only.
 */
void    midiIn_service(void);

#endif /* MIDIIN_H_ */
```

**`MidiIn.c`:**

```c
#include "MidiIn.h"
#include "MidiDin.h"
#include "MidiOut.h"
#include "MidiTransport.h"
#include "usb_manager.h"
#include "presetManager.h"
#include "config.h"

/*
 * Shared MIDI settings (S083).
 *
 * What: global channel (1..16, 0 = off), the packed filter byte (RX low
 * nibble, TX high nibble, MIDI_FILTER_* bits) and the MRT route mask.
 * Why: one owner for settings both directions read. Replaces
 * midi_MidiChannels[8] (8 B), midiParser_txRxFilter and midiParser_routing.
 * Writers: midiIn_set*() from menu_parseGlobalParam() (settings load and
 * menu edits). Readers: MidiIn, MidiOut, MidiTransport, MidiNoteMap loader
 * (B), MidiControl (E), MidiLearn (F), MidiProgram (D). Single-byte reads
 * are atomic, so TIM3 readers need no lock.
 */
static struct {
	uint8_t global;
	uint8_t filter;
	uint8_t route;
} midiIn_config = { 1u, 0xFFu, 0u };

/* DIN framer state (from MidiParser.c 324-330, 461-468). */
enum {
	MIDI_IN_STATE_STATUS = 0,	/* waiting for status or running data */
	MIDI_IN_STATE_DATA1,
	MIDI_IN_STATE_DATA2,
	MIDI_IN_STATE_SYSEX,
	MIDI_IN_STATE_IGNORE		/* unknown status: skip data bytes */
};
static uint8_t midiIn_state = MIDI_IN_STATE_IGNORE;
static MidiMsg midiIn_dinMsg;	/* in-progress DIN message (6 B, R-D) */

void midiIn_setGlobalChannel(uint8_t channel)
{
	if (channel > 16u)
		channel = 16u;
	if (channel != midiIn_config.global)
		midiOut_trackNoteOff(0xFFu);
	midiIn_config.global = channel;
}

uint8_t midiIn_globalChannel(void) { return midiIn_config.global; }

void midiIn_setFilter(uint8_t isTx, uint8_t value)
{
	if (isTx)
		midiIn_config.filter = (uint8_t)(((value & 0x0Fu) << 4) |
		                                 (midiIn_config.filter & 0x0Fu));
	else
		midiIn_config.filter = (uint8_t)((value & 0x0Fu) |
		                                 (midiIn_config.filter & 0xF0u));
}

uint8_t midiIn_rxAllows(uint8_t bit)
{
	return (uint8_t)((midiIn_config.filter & bit) != 0u);
}

uint8_t midiIn_txAllows(uint8_t bit)
{
	return (uint8_t)((midiIn_config.filter & (uint8_t)(bit << 4)) != 0u);
}

void midiIn_setRouting(uint8_t mrt)
{
	midiIn_config.route = midiDefs_routeMask(mrt);
}

uint8_t midiIn_routeMask(void) { return midiIn_config.route; }
```

The S082 drop filter (moved verbatim from `MidiParser.c` 1785-1813, with
its comment block; only the name changes):

```c
static uint8_t midiIn_acceptedDuringBankLoad(const MidiMsg *msg);
```

The dispatcher:

```c
/*
 * Offer one received message to its consumers, then route it (plan §3.2).
 *
 * What, in order:
 *  1. SysEx bytes/packets are pass-through only: forward and return.
 *  2. While a background Bank load runs, a message the S082 filter rejects
 *     is not consumed (S082 outcome note §13.2 item 4) and is forwarded.
 *  3. System common (MTC quarter frame, SPP, Song Select) goes to
 *     MidiTransport. These are transport messages: forwarded whether or
 *     not they acted (plan M5).
 *  4. Channel messages go to the consumers allowed by RXF:
 *     notes (N, MidiNotes, Phase B), CC0 (P, MidiProgram, Phase D),
 *     other CCs (C, MidiControl/MidiLearn, Phase E/F),
 *     program change (P, MidiProgram, Phase D).
 *  5. A message no consumer acted on is forwarded per MRT.
 * Why: consumed messages must not also reach the downstream device.
 * Inputs: one framed message with bits.source and stamp set.
 * Outputs: consumer actions; forwarding. Accessors: midiIn_frameDinByte(),
 * midiIn_service() (USB). Affiliates: midiOut_forward(),
 * midiIn_acceptedDuringBankLoad(), preset_backgroundBankLoadRunning().
 */
static void midiIn_dispatch(const MidiMsg *msg)
{
	uint8_t consumed = 0u;
	uint8_t type;

	if (msg->bits.sysxbyte) {
		midiOut_forward(msg);
		return;
	}
	if (preset_backgroundBankLoadRunning() &&
	    !midiIn_acceptedDuringBankLoad(msg)) {
		midiOut_forward(msg);
		return;
	}
	if (msg->status >= 0xF0u) {
		(void)midiTransport_handleSystemCommon(msg);
		midiOut_forward(msg);
		return;
	}
	type = (uint8_t)(msg->status & 0xF0u);
	switch (type) {
	case NOTE_ON:
	case NOTE_OFF:
		/* B-13: consumed = midiNotes_handle(msg); (RXF N) */
		break;
	case MIDI_CC:
		/* D-3: CC0 -> midiProgram_bankChange(); E-12: other CCs. */
		break;
	case PROG_CHANGE:
		/* D-3: consumed = midiProgram_programChange(); (RXF P) */
		break;
	default:
		break;
	}
	if (!consumed)
		midiOut_forward(msg);
}
```

The DIN framer, from `MidiParser.c` 1990-2121, with these changes (full
code):

```c
/*
 * Frame one DIN byte (from midiParser_parseUartData(), S083 changes).
 *
 * What: builds channel messages with running status, passes SysEx through
 * one byte at a time (sysxbyte), frames MTC quarter frame and Song Select
 * (1 data byte) and, new in S083, Song Position Pointer (2 data bytes) and
 * Tune Request (0). Each message carries the arrival stamp of its first
 * byte: the status byte, or under running status the first data byte.
 * Why: SPP was framed as a 0-byte message (plan §2 Framing); Q6 needs the
 * arrival time. Realtime bytes never reach here (MidiDin diverts them), so
 * running status is undisturbed. Inputs: byte and its 16-bit stamp.
 * Output: midiIn_dispatch() per complete message. Accessor:
 * midiIn_service().
 */
static void midiIn_frameDinByte(uint8_t data, uint16_t stamp)
{
	if (data & 0x80u) {
		midiIn_dinMsg.bits.sysxbyte = 0u;
		midiIn_dinMsg.bits.source = midiSourceMIDI;
		midiIn_dinMsg.stamp = stamp;
		if ((data & 0xF0u) == 0xF0u) {
			midiIn_dinMsg.status = data;
			midiIn_dinMsg.data1 = 0u;
			midiIn_dinMsg.data2 = 0u;
			switch (data) {
			case SYSEX_START:
				midiIn_state = MIDI_IN_STATE_SYSEX;
				midiIn_dinMsg.bits.length = 0u;
				midiIn_dinMsg.bits.sysxbyte = 1u;
				midiIn_dispatch(&midiIn_dinMsg);
				return;
			case SYSEX_END:
				/* a stray end outside SysEx is ignored (as before) */
				if (midiIn_state == MIDI_IN_STATE_SYSEX) {
					midiIn_state = MIDI_IN_STATE_STATUS;
					midiIn_dinMsg.bits.length = 0u;
					midiIn_dinMsg.bits.sysxbyte = 1u;
					midiIn_dispatch(&midiIn_dinMsg);
				}
				return;
			case MIDI_MTC_QFRAME:
			case MIDI_SONG_SEL:
				midiIn_state = MIDI_IN_STATE_DATA1;
				midiIn_dinMsg.bits.length = 1u;
				return;
			case MIDI_SONG_POSITION:
				midiIn_state = MIDI_IN_STATE_DATA1;
				midiIn_dinMsg.bits.length = 2u;
				return;
			case MIDI_TUNE_REQUEST:
				midiIn_dinMsg.bits.length = 0u;
				midiIn_state = MIDI_IN_STATE_STATUS;
				midiIn_dispatch(&midiIn_dinMsg);
				return;
			default:	/* 0xF4/0xF5 undefined: ignore their data */
				midiIn_state = MIDI_IN_STATE_IGNORE;
				midiIn_dinMsg.bits.length = 0u;
				return;
			}
		}
		/* channel status (from midiParser_handleStatusByte()) */
		midiIn_dinMsg.status = data;
		switch (data & 0xF0u) {
		case NOTE_OFF:
		case NOTE_ON:
		case MIDI_AT:
		case MIDI_CC:
		case MIDI_PITCH_WHEEL:
			midiIn_dinMsg.bits.length = 2u;
			midiIn_state = MIDI_IN_STATE_DATA1;
			break;
		default:	/* PROG_CHANGE, CHANNEL_PRESSURE */
			midiIn_dinMsg.bits.length = 1u;
			midiIn_state = MIDI_IN_STATE_DATA1;
			break;
		}
		return;
	}
	switch (midiIn_state) {
	case MIDI_IN_STATE_STATUS:
		/* running status: only after a channel message */
		if (midiIn_dinMsg.bits.length == 0u || midiIn_dinMsg.status >= 0xF0u)
			return;
		midiIn_dinMsg.stamp = stamp;
		midiIn_dinMsg.data1 = data;
		if (midiIn_dinMsg.bits.length == 2u) {
			midiIn_state = MIDI_IN_STATE_DATA2;
			return;
		}
		midiIn_dinMsg.data2 = 0u;
		break;
	case MIDI_IN_STATE_DATA1:
		midiIn_dinMsg.data1 = data;
		if (midiIn_dinMsg.bits.length == 2u) {
			midiIn_state = MIDI_IN_STATE_DATA2;
			return;
		}
		midiIn_dinMsg.data2 = 0u;
		break;
	case MIDI_IN_STATE_DATA2:
		midiIn_dinMsg.data2 = data;
		break;
	case MIDI_IN_STATE_SYSEX:
		{
			MidiMsg sx = {0};

			sx.status = data;	/* payload byte, sent as-is */
			sx.bits.sysxbyte = 1u;
			sx.bits.source = midiSourceMIDI;
			sx.stamp = stamp;
			midiIn_dispatch(&sx);
		}
		return;
	default:
		return;
	}
	midiIn_state = MIDI_IN_STATE_STATUS;
	midiIn_dispatch(&midiIn_dinMsg);
	/* system common messages do not establish running status */
	if (midiIn_dinMsg.status >= 0xF0u)
		midiIn_dinMsg.bits.length = 0u;
}

/* Contract in MidiIn.h. */
void midiIn_service(void)
{
	uint8_t budget;
	uint8_t byte;
	uint16_t stamp;
	MidiMsg msg;

	budget = MIDI_DIN_DRAIN_PER_PASS;
	while (budget-- && midiDin_rxPop(&byte, &stamp))
		midiIn_frameDinByte(byte, stamp);
	budget = MIDI_USB_DRAIN_PER_PASS;
	while (budget-- && usb_getMidi(&msg))
		midiIn_dispatch(&msg);
	midiOut_service();
	usb_tick();
}
```

Note for the SysEx case of `midiOut_sendMsg()`: a DIN SysEx payload
message has `length = 0`, so one byte goes out; on DIN its `status` field
carries the payload byte, which `midiDin_sendBytes()` queues as-is (a data
byte below 0x80 is never mistaken for realtime).

### A-9 `Core/MIDI/MidiVoiceControl.c` — include

**Where:** `MidiVoiceControl.c:54` `#include "MidiMessages.h"`.
**Action:** MODIFY → `#include "MidiDefs.h"`. Nothing else changes in the
module (plan §3.1).

### A-10 Delete and move old files

| Action | File |
|--------|------|
| DELETE FILE | `Core/MIDI/FIFO.c`, `FIFO.h`, `Uart.c`, `Uart.h`, `MidiParser.c`, `MidiParser.h`, `MidiRealtime.c`, `MidiRealtime.h`, `MidiMessages.h`, `MidiNoteNumbers.h` |
| MOVE FILE | `Core/MIDI/valueShaper.h` → `Core/DSPAudio/valueShaper.h` (byte-identical; `-ICore/DSPAudio` is already on the include path, so the four `#include "valueShaper.h"` lines need no edit) |

What goes away with `MidiParser.c` (the R12 caller audit, plan R12):

| Removed | Former external caller | Replacement |
|---------|-----------------------|-------------|
| `midiParser_ccHandler()`, `midiParser_ccDispatch()` (~1,000 lines, 499-1498), `midiParser_applyTaggedInstrumentCc()`, `midiParser_writeTaggedRuntime()`, `midiParser_enterTaggedParameter()` | `preset_applySoundParameter()` (A-19) | none: dead (`END_OF_SOUND_PARAMETERS` = 1, so only id 0 reached it) |
| `midiParser_originalCcValues[255]`, `midiParser_activeNrpnNumber`, `midiParser_selectedLfoVoice[6]`, `midi_NoteOverride[7]` | none | none |
| `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()` | none (dead) | — |
| `midiParser_setMorphFromModWheel()`, `midiParser_setVoiceMorphFromModWheel()` | parser only | Phase E (E-9) |
| `midiParser_getVoiceMidiNote()` | parser only | Phase B note map |
| `midi_MidiChannels[8]` | `sequencer.c:6292, 6449`, menu | `midiIn_globalChannel()`; A-14 |
| `midiParser_txRxFilter` | `sequencer.c:6392, 6404, 6439` | `midiIn_rx/txAllows()` |
| `midiParser_calcDetune()` | `Oscillator.c:999, 1019` | `midiPitch_detune()` (A-17) |
| `midiParser_checkMtc()` | `sequencer.c:5926, 5938, 6017, 6256` | `midiTransport_checkMtc()` |
| `midiParser_processRealtimeEvents()` | `sequencerTimer.c:100` | `midiTransport_processRealtime()` |
| `midiParser_parseMidiMessage()` | `main.c:272` | `midiIn_service()` |
| `midiParser_setChannel/Routing/Filter()` | `menu.c:17719, 18127, 18135, 18143` | `midiIn_set*()` (A-20) |
| `uart_*`, `initMidiUart()` | `main.c:269, 611`, `sequencer.c:1751` | `midiDin_init()`, MidiOut |
| `midiRealtime_*` | `usb_midi_core.c:436, 440` | `midiTransport_*` (A-16) |

### A-11 `Makefile` — sources

**Where:** `Makefile:120-124`.
**Action:** MODIFY → replace the five lines with:

```make
  Core/MIDI/MidiBuffer.c \
  Core/MIDI/MidiDin.c \
  Core/MIDI/MidiTransport.c \
  Core/MIDI/MidiIn.c \
  Core/MIDI/MidiOut.c \
  Core/MIDI/MidiVoiceControl.c \
```

(B, D, E and F each add their module's line here.)

### A-12 `main.c`

| Line | Action | Change |
|------|--------|--------|
| 67-68 | MODIFY | `#include "Uart.h"` / `#include "MidiParser.h"` → `#include "MidiDin.h"` / `#include "MidiIn.h"` |
| 264-276 | MODIFY | body of `midi_service()` → `midiIn_service();` with the block below |
| 611 | MODIFY | `initMidiUart();` → `midiDin_init();` |

```c
static void midi_service(void)
{
	/*
	 * MIDI receive/transmit service (S083: body moved to MidiIn).
	 *
	 * What: frames DIN input, takes USB input, dispatches consume-then-
	 * route, runs MidiOut's foreground service and flushes USB.
	 * Context: main loop only. Affiliates: midiIn_service().
	 */
	midiIn_service();
}
```

### A-13 `Core/Sequencer/sequencerTimer.c`

| Line | Action | Change |
|------|--------|--------|
| 48 | MODIFY | `#include "MidiParser.h"` → `#include "MidiTransport.h"` |
| 100 | MODIFY | `midiParser_processRealtimeEvents();` → `midiTransport_processRealtime();` (the comment at 94-97 stays) |

### A-14 `Core/Sequencer/sequencer.c` — MIDI output moves to MidiOut

| Line(s) | Anchor | Action | Change |
|---------|--------|--------|--------|
| 43-45, 51-52 | includes | MODIFY | `Uart.h`, `MidiMessages.h`, `MidiParser.h`, `MidiNoteNumbers.h` → `MidiDefs.h`, `MidiIn.h`, `MidiOut.h`, `MidiTransport.h`, `MidiPitch.h` (keep `MidiVoiceControl.h`) |
| 744-747 | `midi_chan_notes[16]`, `midi_notes_on` | REMOVE | replaced by MidiOut's per-track record |
| 1449 | `static void seq_sendMidi(MidiMsg msg);` | REMOVE | |
| 1726-1738 | `seq_setNextPattern()` | REMOVE | deprecated no-op; its only caller was the parser |
| 1745-1752 | `seq_sendMidi()` | REMOVE | |
| 1950-1969 | in `seq_triggerVoice()`, from `/* Per-track MIDI output (S077 P2 §6.5).` to `seq_sendMidiNoteOn(midiChan, midiNote, vol);` | MODIFY | block A-14a |
| 2009-2015 | in `seq_previewVoice()`, `note = scene_getTrackMidiNote(...)` … `seq_sendMidiNoteOn(midiChan, note, ROLL_VOLUME);` | MODIFY | block A-14b (note rule changes in G-6) |
| 2159 | `voiceControl_noteOff(0xFF);` after `seq_sendProgChg(seq_activePattern);` | ADD after | `midiOut_trackNoteOff(0xFFu);` with comment A-14c |
| 4334 | same pair in the master-boundary commit | ADD after | `midiOut_trackNoteOff(0xFFu);` |
| 5926, 5938, 6017, 6256 | `midiParser_checkMtc();` | MODIFY | `midiTransport_checkMtc();` |
| 6251 | stop branch `voiceControl_noteOff(0xFF);` | ADD after | `midiOut_trackNoteOff(0xFFu);` |
| 6290-6292 | `seq_setMute()` mute branch | MODIFY | block A-14d |
| 6344-6386 | `seq_midiNoteOff()` | REMOVE | |
| 6388-6396 | `seq_sendRealtime()` | MODIFY | body → `midiOut_realtime(status);` (block A-14e) |
| 6398-6428 | `seq_sendMidiNoteOn()` | REMOVE | |
| 6430-6453 | `seq_sendProgChg()` | MODIFY | block A-14f |

**A-14a** (`seq_triggerVoice()`):

```c
	/*
	 * Track MIDI output (S077 P2 §6.5; S083 Phase A).
	 *
	 * What: the note goes out on the track's effective channel in the Scene
	 * it plays (its own channel, or G when unassigned; none when both are
	 * off) through MidiOut, which ends the track's previous note first.
	 * Phase A keeps the pre-S083 note rule (a track MIDI note, else the
	 * step note); Phase G replaces it with the inverse note map (G-5).
	 * Inputs: seq_perTrackPattern[voiceNr], note, vol. Output:
	 * midiOut_trackNoteOn(). Context: TIM3 and foreground.
	 * Affiliates: scene_getTrackMidiChannel/Note(), midiIn_globalChannel().
	 */
	midiChan = scene_getTrackMidiChannel(seq_perTrackPattern[voiceNr], voiceNr);
	if (midiChan == MIDI_CHANNEL_NONE)
		midiChan = midiIn_globalChannel();
	midiNote = scene_getTrackMidiNote(seq_perTrackPattern[voiceNr], voiceNr);
	if (midiNote == 0u)
		midiNote = note;
	midiOut_trackNoteOn(voiceNr, midiChan, midiNote, vol);
```

**A-14b** (`seq_previewVoice()`): the same three-line channel rule, then
`midiOut_trackNoteOn(voiceNr, midiChan, note, ROLL_VOLUME);`. The preview's
`note` lookup is unchanged here and changes in G-6 (C-1).

**A-14c** (after both `voiceControl_noteOff(0xFF)` lines in whole-Scene
changes):

```c
	/* S083: end every track's MIDI note; the new Scene may map tracks to
	** other channels and notes. Affiliate: midiOut_trackNoteOff(). */
	midiOut_trackNoteOff(0xFFu);
```

**A-14d** (`seq_setMute()`, replaces line 6292):

```c
			/*
			 * End the muted track's sound and MIDI note (S083 fix).
			 *
			 * What: stops the track's voice and sends its MIDI note-off.
			 * Why: the old call passed midi_MidiChannels[trackNr] (a zero-
			 * based channel) as a voice index, so it stopped the wrong voice
			 * or none. Affiliates: voiceControl_noteOff(),
			 * midiOut_trackNoteOff().
			 */
			voiceControl_noteOff(trackNr);
			midiOut_trackNoteOff(trackNr);
```

**A-14e:**

```c
/* LXR-generated realtime (Clock, Start, Stop; S083: TXF in MidiOut). */
static void seq_sendRealtime(const uint8_t status)
{
	midiOut_realtime(status);
}
```

**A-14f:**

```c
/*
 * Program change for a whole-Scene change (S080; S083 Phase A).
 *
 * What: sends the Scene number as a PC on G under TXF P. Scene 17 has no
 * PC number (S082 C9/D8). G off sends nothing. Phase D adds the MIDI-origin
 * suppression (no echo, D-8). Inputs: Scene index. Affiliates:
 * midiOut_programChange(), midiIn_globalChannel().
 */
static void seq_sendProgChg(const uint8_t ptn)
{
	if (ptn >= SCENE_COUNT)
		return;
	midiOut_programChange(midiIn_globalChannel(), ptn);
}
```

### A-15 `Core/Sequencer/sequencer.h`

| Line | Action | Change |
|------|--------|--------|
| 251 | REMOVE | `void seq_setNextPattern(const uint8_t patNr);` |
| 549-550 | REMOVE | `seq_midiNoteOff()`, `seq_sendMidiNoteOn()` declarations |

### A-16 USB driver (`Core/Hardware/USB/App/`)

**`usb_midi_core.h`:**

| Line | Action | Change |
|------|--------|--------|
| 39 | MODIFY | `#include "MidiMessages.h"` → `#include "MidiDefs.h"` and add `#include "config.h"` |
| 55-56 | MODIFY | block below |

```c
/*
 * USB-in message ring size (S083 Q13 (b)).
 *
 * What: entries of 6-byte MidiMsg, set in config.h. Why: Q13 halves the
 * ring (256 x 4 B -> 128 x 6 B) to carry the arrival stamp and keeps the
 * size scalable. The indices are uint8_t, so the size is a power of two
 * up to 256. RAM: MIDI_USB_RX_RING_SIZE x 6 B SRAM1.
 */
#define USB_MIDI_INPUT_BUFFER_SIZE  MIDI_USB_RX_RING_SIZE
#define USB_MIDI_INPUT_BUFFER_MASK  (USB_MIDI_INPUT_BUFFER_SIZE - 1u)
#if (USB_MIDI_INPUT_BUFFER_SIZE > 256u) || \
    ((USB_MIDI_INPUT_BUFFER_SIZE & USB_MIDI_INPUT_BUFFER_MASK) != 0u)
#error "MIDI_USB_RX_RING_SIZE must be a power of two no larger than 256"
#endif
```

**`usb_midi_core.c`:**

| Line | Action | Change |
|------|--------|--------|
| 34 | MODIFY | `#include "MidiRealtime.h"` → `#include "MidiTransport.h"` |
| 384-424 | MODIFY | in the code-index switch: `CIN_SYSEX_START` (0x4) keeps length 3 and the three SysEx end cases keep 1/2/3, and all four set a local `sysex = 1u` (declare `uint8_t sysex = 0u;` beside `length`) |
| 436-441 | MODIFY | `midiRealtime_isStatus` → `midiTransport_isRealtime`; `midiRealtime_push` → `midiTransport_push` |
| 445-457 | MODIFY | block below |

```c
    		/*
    		 * Store one USB message (S083 Q12/Q13).
    		 *
    		 * What: drops the NEW message when the ring is full (was: overwrite
    		 * the oldest unread entry, which also lost the read position);
    		 * stores the low 16 bits of the packet's arrival time; marks SysEx
    		 * packets as pass-through (sysxbyte) so no consumer parses them.
    		 * Why: Q12 drop-newest; Q6/Q13 arrival stamps; SysEx packets were
    		 * stored as if their first byte were a status. Affiliates:
    		 * usb_getMidi(), midiIn_service().
    		 */
    		{
    			uint8_t next = (uint8_t)((usb_MidiMessagesWrite + 1u) &
    			                         USB_MIDI_INPUT_BUFFER_MASK);
    			MidiMsg *slot;

    			if (next == usb_MidiMessagesRead)
    				continue;
    			slot = &usb_MidiMessages[usb_MidiMessagesWrite];
    			slot->status = usbData.status;
    			slot->data1 = usbData.data1;
    			slot->data2 = usbData.data2;
    			slot->bits.length = (uint8_t)(length - 1u);
    			slot->bits.sysxbyte = sysex;
    			slot->bits.source = midiSourceUSB;
    			slot->stamp = (uint16_t)packetTimestampUs;
    			usb_MidiMessagesWrite = next;
    		}
```

**`usb_manager.h`:** line 29 `MidiMessages.h` → `MidiDefs.h`; line 34
`void usb_sendMidi(MidiMsg msg);` → the declaration below.

```c
/*
 * Queue one 4-byte USB-MIDI event packet (S083).
 * Inputs: code index (cable 0) and the three MIDI bytes (unused bytes 0).
 * Output: bytes in the USB IN buffer, flushed by usb_tick(). Context: any
 * (PRIMASK). Why: MidiOut now computes the code index (usb_sendMidi() used
 * status >> 4 for every message). Accessor: midiOut_sendMsg() only.
 */
void usb_sendMidiPacket(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2);
```

**`usb_manager.c` 177-191:** MODIFY `usb_sendMidi()` → `usb_sendMidiPacket()`:

```c
void usb_sendMidiPacket(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2)
{
	uint32_t primask = usb_irqSave();

	usb_sendByte((uint8_t)(cin & 0x0Fu));	/* cable 0 */
	usb_sendByte(b0);
	usb_sendByte(b1);
	usb_sendByte(b2);
	usb_irqRestore(primask);
}
```

### A-17 `Core/DSPAudio/Oscillator.c`

| Line | Action | Change |
|------|--------|--------|
| 53-54 | MODIFY | `#include "MidiParser.h"` / `"MidiNoteNumbers.h"` → `#include "MidiPitch.h"` |
| 999, 1019 | MODIFY | `midiParser_calcDetune(` → `midiPitch_detune(` |

### A-18 Include renames elsewhere

| File:line | Old | New |
|-----------|-----|-----|
| `Core/Hardware/SD/filesystem.c:84` | `MidiNoteNumbers.h` | `MidiPitch.h` |
| `Core/Hardware/SD/filesystem.c:85` | `MidiMessages.h` | REMOVE (no symbol used); F-15 adds `MidiLearn.h` |
| `Core/Hardware/frontPanel/buttonHandler.c:37` | `MidiNoteNumbers.h` | `MidiPitch.h` |
| `Core/Hardware/frontPanel/buttonHandler.c:40` | `MidiParser.h` | REMOVE (B-9 adds `MidiNotes.h` uses where needed) |
| `Core/Bank/Scene/SceneData.c:4` | `MidiNoteNumbers.h` | `MidiPitch.h` |
| `Core/Bank/Scene/Pattern/SomGenerator.c:40` | `MidiNoteNumbers.h` | `MidiPitch.h` |
| `Core/Bank/Scene/Preset/presetManager.c:38` | `MidiParser.h` | REMOVE (after A-19) |
| `Core/Menu/menu.c:63` | `MidiParser.h` | `MidiIn.h` and `MidiOut.h` |

If a file relied on `MidiParser.h`'s indirect includes (`MidiVoiceControl.h`,
`globals.h`), add the direct include the build asks for.

### A-19 Legacy sound-parameter path (plan R12 audit)

**`presetManager.c:894-942`** `preset_applySoundParameter()` — REMOVE.
**`presetManager.h:420-430`** its comment block and declaration — REMOVE.

**`menu.c:817-840`** `menu_sendSoundParameter()` — MODIFY the body:

```c
static void menu_sendSoundParameter(uint16_t paramNr, uint8_t value)
{
    /*
     * Route a flat menu parameter to its owner (S083: legacy sound path
     * removed).
     *
     * What: every flat ParameterArray id runs its typed owner path in
     * menu_parseGlobalParam(). Why: instrument sound parameters left the
     * flat namespace (END_OF_SOUND_PARAMETERS is 1), so the old
     * preset_applySoundParameter() branch only ever saw id 0 and fed the
     * retired legacy MIDI CC map. Kept as a function because
     * menu_sendEditedParameter() still separates Morph-endpoint edits from
     * active edits. Inputs: id and clamped value. Affiliates:
     * menu_parseGlobalParam(), menu_sendEditedParameter().
     */
    menu_parseGlobalParam(paramNr, value);
}
```

**`buttonHandler.c`** STEP armed-automation reset-lock remnant:

| Line(s) | Action | Change |
|---------|--------|--------|
| 201-203 | REMOVE | `buttonHandler_originalParameter`, `buttonHandler_originalValue`, `buttonHandler_resetLock` (never set to a restoring state anywhere: `buttonHandler_resetLock` is only ever 0) |
| 421 | REMOVE | `buttonHandler_getArmedAutomationStep()` (no callers) |
| 806-845 | MODIFY | in `buttonHandler_disarmTimerActionStep()`: delete the two `if (buttonHandler_resetLock == 1)` blocks and the comment sentences about reset-lock restoration; keep the blink-LED stop and `buttonHandler_armedAutomationStep = NO_STEP_SELECTED;` |
| `buttonHandler.h:120, 133-135` | REMOVE | the getter and the three `extern`s |

Replacement comment for the trimmed function:

```c
    /*
     * Stop the long-press STEP blink (S083: reset-lock remnant removed).
     *
     * What: clears the armed step and stops its STEP LED blink. Why: the
     * reset-lock restore that followed (S028 parser bridge) could never run
     * (buttonHandler_resetLock was never set) and routed through the retired
     * legacy MIDI CC map (plan R12). Inputs: buttonHandler_armedAutomationStep.
     * Outputs: blink stopped, step disarmed. Callers: step release, timer
     * actions. Affiliates: buttonHandler_armTimerActionStep().
     */
```

### A-20 `Core/Menu/menu.c` — MIDI settings

**A-20a `menu_parseGlobalParam()` 17694-17721** — MODIFY (the
`PAR_MIDI_CHAN_7` … `PAR_MIDI_CHAN_GLOBAL` cases):

```c
    case PAR_MIDI_CHAN_1:
    case PAR_MIDI_CHAN_2:
    case PAR_MIDI_CHAN_3:
    case PAR_MIDI_CHAN_4:
    case PAR_MIDI_CHAN_5:
    case PAR_MIDI_CHAN_6:
    case PAR_MIDI_CHAN_7:
    case PAR_MIDI_MODE:
        /*
         * Retired no-op slots (S083 plan §3.10).
         *
         * What: these ids keep their enum positions (no renumbering, the
         * S080 mirror precedent) but do nothing. Why: track channels live in
         * Scene settings (`chn`), and the trigger/note mode is gone; the
         * legacy per-voice channel mirror was a second source of truth.
         * menu_sendAllGlobals() still replays the ids; this case absorbs it.
         */
        break;

    case PAR_MIDI_CHAN_GLOBAL:
        /*
         * Global MIDI channel G (S083 M2).
         *
         * What: 1..16, 0 = off. With G off nothing is received or sent on a
         * global channel, and unassigned tracks have no channel. Inputs:
         * menu/settings value. Output: MidiIn configuration (MidiIn ends
         * every sounding track note when G changes). Affiliates:
         * midiIn_setGlobalChannel(), filesystem_sanitizeLoadedGlobals().
         */
        midiIn_setGlobalChannel(value);
        break;
```

**A-20b `PAR_TRACK_MIDI_CHAN` 17777-17796** — MODIFY:

```c
    case PAR_TRACK_MIDI_CHAN:
    {
        /*
         * Track MIDI channel `chn` for the STEP track settings (S083).
         *
         * What: stores 0 (`glb`: follow G) or 1..16 in the viewed Scene's
         * track settings, and ends the track's sounding MIDI note when the
         * value changes. Why: the legacy PAR_MIDI_CHAN_* mirror and the
         * MidiParser channel copy are retired (plan §3.10); Scene settings
         * are the only source. Inputs: active track, viewed Scene, value.
         * Affiliates: scene_setTrackMidiChannel(), midiOut_trackNoteOff().
         */
        uint8_t track = menu_getActiveVoice();
        uint8_t scene = menu_getViewedPattern();

        if (scene_getTrackMidiChannel(scene, track) != value)
            midiOut_trackNoteOff(track);
        scene_setTrackMidiChannel(scene, track, value);
        break;
    }
```

**A-20c `PAR_TRACK_MIDI_NOTE` 17798-17810** — MODIFY the comment only:

```c
        /*
         * Track MIDI note `nte` (S083): 0 = `def` (unassigned), 1..127 the
         * root of the track's receive range (plan §3.3). Not a pitch: the
         * root plays the default pitch. Affiliate: scene_setTrackMidiNote().
         */
```

**A-20d routing/filters 18122-18144** — MODIFY the three calls:
`midiParser_setRouting(value)` → `midiIn_setRouting(value)`;
`midiParser_setFilter(1u, value)` → `midiIn_setFilter(1u, value)`;
`midiParser_setFilter(0u, value)` → `midiIn_setFilter(0u, value)`. In each
comment, "MidiParser" → "MidiIn".

**A-20e Value domains — `menu_clampCellValue()`**, in the static-param
`switch` that starts at 6069 (anchor `case PAR_PERF_BTN_LT:`), ADD:

```c
        /*
         * S083: MIDI channel cells accept 0 (plan §3.10).
         *
         * What: G (`ch`) and the track channel (`chn`) are 0..16; 0 shows
         * `off` (G) or `glb` (track). Why: DTYPE_1B16 clamps to 1..16 and
         * the dtype space is full, so the zero is a static-param special
         * case (the run-mode precedent). Affiliates: menu_getMidiStaticName().
         */
        case PAR_MIDI_CHAN_GLOBAL:
        case PAR_TRACK_MIDI_CHAN:
            if (*value > 16u) *value = 16u;
            return;
```

**A-20f Display — new helper** (ADD before `menu_formatCellValue3()`,
line 5900), and calls to it:

```c
/*
 * Names for the MIDI static cells (S083).
 *
 * What: `ch` 0 -> `off`; `chn` 0 -> `glb`; `nte` 0 -> `def`. Returns 1 when
 * it wrote buf (3 chars), else 0 and the generic formatter runs. Why: the
 * zero values are special cases of DTYPE_1B16/DTYPE_NOTE_NAME (dtype space
 * full). `nte` 0 showed `any` before S083; it now means "no note assigned"
 * (`def`). Inputs: cell, value. Accessors: menu_formatCellValue3(),
 * menu_repaintGeneric().
 */
static uint8_t menu_getMidiStaticName(const menu_cell_t *cell, uint16_t value,
                                      char *buf)
{
    if (!cell || cell->kind != MENU_CELL_STATIC || value != 0u)
        return 0u;
    switch (cell->static_param) {
    case PAR_MIDI_CHAN_GLOBAL: memcpy(buf, menuText_off, 3); return 1u;
    case PAR_TRACK_MIDI_CHAN:  memcpy(buf, menuText_glb, 3); return 1u;
    case PAR_TRACK_MIDI_NOTE:  memcpy(buf, menuText_def, 3); return 1u;
    default:                   return 0u;
    }
}
```

| Line | Action | Change |
|------|--------|--------|
| 5933 (before `if (menu_getS080StaticName(cell, value, valueAsText))`) | ADD | `if (menu_getMidiStaticName(cell, value, valueAsText)) return;` |
| 5998-6004 (`case DTYPE_NOTE_NAME:` in `menu_formatCellValue3()`) | MODIFY | `setNoteName(value, valueAsText);` only (the zero case is handled above) |
| 13594 (before `switch (dtype) {` in `menu_repaintGeneric()`) | ADD | `if (menu_getMidiStaticName(&cell, value, &editDisplayBuffer[1][13])) { /* S083 zero names */ } else` before the `switch` |
| 13641-13646 (`case DTYPE_NOTE_NAME:` in `menu_repaintGeneric()`) | MODIFY | `setNoteName(value, &editDisplayBuffer[1][13]);` only |

### A-21 `Core/Menu/MenuText.h`

| Line | Action | Change |
|------|--------|--------|
| 47 (after `menuText_any`) | ADD | `static const char menuText_glb[]   = "glb";` and `static const char menuText_def[]   = "def";` |
| 136-138 | MODIFY | the routing table below |

```c
/*
 * MRT routing names (S083 F22, MIDI_SPEC §3.3).
 * Order matches midiDefs_routeMask(): off UtD DtD DtU bth DtA AtD all.
 * Stored values change meaning (no migration, standing rule).
 */
static const char midiRoutingNames[][6] = {
    {8}, {"off"},{"UtD"},{"DtD"},{"DtU"},{"bth"},{"DtA"},{"AtD"},{"all"},
};
```

### A-22 `Core/Bank/Scene/SceneData.c/h` — channel 0 = unassigned, note 0 = `def`

| Line(s) | Anchor | Action | Change |
|---------|--------|--------|--------|
| `SceneData.c:286-308` | `scene_setTrackMidiChannel()` | MODIFY | clamp to 0..16 (block A-22a) |
| `SceneData.c:310-328` | `scene_getTrackMidiChannel()` | MODIFY | return 0..16, no track+1 fallback (A-22b) |
| `SceneData.c:1149-1150` | `scene_settingsDefaults()` | MODIFY | `out->midi_channel[i] = MIDI_CHANNEL_NONE;` and `out->midi_note[i] = 0u;` (needs `#include "MidiDefs.h"`) |
| `SceneData.c:1261-1262` | Scene reset loop `midi_channel[track] = track + 1` | REMOVE | the `memset()` above already writes 0 |
| `SceneData.c:1312-1314` | `scene_initAll()` loop | REMOVE | `memset(scenes, 0, …)` already writes 0 |
| `SceneData.h:426-451` | the two setter/getter comment blocks | MODIFY | text below |

**A-22a:**

```c
    /*
     * Store a track MIDI channel in Scene settings (S083: 0 = unassigned).
     *
     * Inputs: Scene index, track, channel 0..16 (0 = `glb`: the track
     * follows G). Values above 16 clamp to 16. Output: a changed byte marks
     * the track's AutoSave Scene cell. Affiliates: scene_storeParameterByte(),
     * menu_parseGlobalParam(PAR_TRACK_MIDI_CHAN), AutoSave restore.
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    if (channel > 16u)
        channel = 16u;
```

**A-22b:**

```c
    /*
     * Read a track MIDI channel (S083).
     *
     * Output: 1..16, or 0 when the track is unassigned (it follows G), the
     * coordinates are invalid, or the byte is out of range. The pre-S083
     * fallback to track+1 is gone: an unassigned track has no channel of
     * its own. Accessors: MidiNoteMap loader, MidiOut callers, menu.
     */
    if (!scene || track >= NUM_TRACKS)
        return 0u;
    channel = scene->settings.midi_channel[track];
    return (channel <= 16u) ? channel : 0u;
```

### A-23 `Core/Hardware/SD/storageTypes.c:642-645`

**Action:** REMOVE the loop rejecting `midi_channel` values below 1 (keep the
`storage_parseCsvU8(…, 16u)` call, which already bounds 0..16). Comment
above the parse:

```c
        /* S083: 0 = unassigned (`glb`), 1..16 a channel. */
```

### A-24 `Core/Hardware/SD/filesystem.c` — G off and defaults

| Line(s) | Action | Change |
|---------|--------|--------|
| 2768-2771 | MODIFY | `filesystem_sanitizeLoadedGlobals()`: `if (ch > 16u) parameter_values[PAR_MIDI_CHAN_GLOBAL] = 1u;` with comment "S083: 0 = G off is valid; out of range falls back to 1." |
| 17692-17693 | MODIFY | Scene stage defaults → `MIDI_CHANNEL_NONE` and `0u` (needs `MidiDefs.h`) |

Defaults that stay: `PAR_MIDI_CHAN_GLOBAL = 1u` at 2865, 2884 (G defaults
to channel 1). `PAR_MIDI_ROUTING = 0u` at 2887 now means `off`.

### A-25 Phase A verification

1. `make all && make img`; `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`; record text/data/bss in §12.
2. Clock: LXR slaved to DIN and to USB clock (`snc` din/usb/aut): tempo follows, Start/Stop work, no drift (compare with S082).
3. MTC start at 0:0:0:0 and timeout stop.
4. MRT matrix: for each of the 8 values, send notes/CC/SysEx/clock/SPP on DIN (loopback) and USB (DAW); check only the routed paths carry them, SysEx and SPP arrive intact on USB (a USB MIDI monitor shows the right code index), and nothing loops USB → USB.
5. LXR output: sequencer notes on the channel rule of A-14a; a muted track stops sounding (A-14d); Stop sends note-offs; a Scene change sends note-offs and the PC on G.
6. Settings: `ch` shows `off` at 0, `chn` shows `glb`, `nte` shows `def`; values survive reboot; MRT shows the new names.
7. Bank load running: a PC sent during the load is forwarded and not acted on (S082 drop filter).

---

## 5. Phase B — Note map, note actions, per-track overlay, arrival age

**Goal.** N1–N6: received notes resolve to one track and pitch, trigger it
(or perform the selected overlay action), CC2 selects the overlay, rolls run
per track, live record places MIDI hits at their arrival time. The panel
overlay moves to the same per-track executor.

### B-1 `Core/MIDI/MidiNoteMap.h` / `MidiNoteMap.c` — NEW FILES

**`MidiNoteMap.h`:**

```c
#ifndef MIDINOTEMAP_H_
#define MIDINOTEMAP_H_

/*
 * MidiNoteMap — note <-> (track, internal note) for one Scene (S083,
 * plan §3.3, MIDI_SPEC §4.1).
 *
 * What: resolves a received (channel, note) to at most one track and the
 * internal note the voice plays, and the inverse for transmit. Rules:
 *  1. assigned ranges: a track with a note owns [note, next assigned - 1]
 *     (or 127), internal = 63 + (n - note); on equal notes the lower track
 *     owns the range and the other owns nothing;
 *  2. below the lowest assigned note (all of 0..127 if none), the channel's
 *     unassigned tracks share the notes: on G a stripe (n mod |U|, default
 *     pitch), on another channel contiguous near-equal blocks in track
 *     order (lower tracks take the remainder), chromatic from each block
 *     start;
 *  3. one voice per note; 4. internal notes clamp at 127 (+64).
 * A track's effective channel is its own channel, else G; a track set
 * explicitly to G's number follows the G rules; with G off unassigned
 * tracks have no channel.
 * Why: N1-N5. Stateless and table-free: at most seven tracks per lookup,
 * no RAM. The pure functions take a settings snapshot, so the host test
 * (tools/midi_test/) links this file alone (MIDI_NOTEMAP_HOST_TEST).
 * Affiliates: MidiNotes (receive), MidiOut (transmit, Phase G),
 * MidiControl/MidiProgram/MidiLearn (tracks on a channel).
 */

#include <stdint.h>

/* Internal note for the voice's default pitch (= MIDI_DEFAULT_TRIGGER_NOTE). */
#define MIDI_NOTEMAP_ROOT_INTERNAL  63u
#define MIDI_NOTEMAP_TRACKS         7u

/*
 * One Scene's note-map inputs (stack only, 15 B).
 * global: G 0..16 (0 off). channel[t]: 0 unassigned or 1..16.
 * note[t]: 0 unassigned or 1..127 (MIDI note 0 cannot be a root).
 */
typedef struct {
	uint8_t global;
	uint8_t channel[MIDI_NOTEMAP_TRACKS];
	uint8_t note[MIDI_NOTEMAP_TRACKS];
} midiNoteMap_settings_t;

/* Effective channel of track t: own channel, else G; 0 = none. */
uint8_t midiNoteMap_effectiveChannel(const midiNoteMap_settings_t *s,
                                     uint8_t track);
/* Bit t set for every track whose effective channel is `channel` (1..16). */
uint8_t midiNoteMap_tracksOnChannel(const midiNoteMap_settings_t *s,
                                    uint8_t channel);
/*
 * Receive: (channel 1..16, note 0..127) -> track and internal note.
 * Output: 1 with *track/*internal set, 0 when unmapped (the caller then
 * does not consume the note).
 */
uint8_t midiNoteMap_resolve(const midiNoteMap_settings_t *s, uint8_t channel,
                            uint8_t note, uint8_t *track, uint8_t *internal);
/*
 * Transmit: (track, internal note) -> (channel, note) such that resolve()
 * returns the same track and internal note. Output: 0 when no such note
 * exists (stripe pitch other than default, below the default pitch, past
 * the range end, a tie loser, no channel): nothing is sent (S083 Q4).
 */
uint8_t midiNoteMap_inverse(const midiNoteMap_settings_t *s, uint8_t track,
                            uint8_t internal, uint8_t *channel, uint8_t *note);

#ifndef MIDI_NOTEMAP_HOST_TEST
/* Fill *out from Scene `scene_index`'s track settings and G (firmware). */
void midiNoteMap_load(uint8_t scene_index, midiNoteMap_settings_t *out);
#endif

#endif /* MIDINOTEMAP_H_ */
```

**`MidiNoteMap.c`:**

```c
#include "MidiNoteMap.h"
#ifndef MIDI_NOTEMAP_HOST_TEST
#include "MidiIn.h"
#include "MidiPitch.h"
#include "SceneData.h"
_Static_assert(MIDI_NOTEMAP_ROOT_INTERNAL == MIDI_DEFAULT_TRIGGER_NOTE,
               "note map root must be the default trigger note");
#endif

/* Contracts in MidiNoteMap.h. */
uint8_t midiNoteMap_effectiveChannel(const midiNoteMap_settings_t *s,
                                     uint8_t track)
{
	uint8_t ch;

	if (!s || track >= MIDI_NOTEMAP_TRACKS)
		return 0u;
	ch = s->channel[track];
	if (ch > 16u)
		ch = 0u;
	return (ch != 0u) ? ch : ((s->global <= 16u) ? s->global : 0u);
}

uint8_t midiNoteMap_tracksOnChannel(const midiNoteMap_settings_t *s,
                                    uint8_t channel)
{
	uint8_t mask = 0u;
	uint8_t t;

	if (channel < 1u || channel > 16u)
		return 0u;
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if (midiNoteMap_effectiveChannel(s, t) == channel)
			mask |= (uint8_t)(1u << t);
	}
	return mask;
}

/*
 * Lowest assigned note among `set` (128 when none): the top of the
 * unassigned region (rule 2).
 */
static uint8_t midiNoteMap_lowestAssigned(const midiNoteMap_settings_t *s,
                                          uint8_t set)
{
	uint8_t low = 128u;
	uint8_t t;

	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if ((set & (1u << t)) && s->note[t] != 0u && s->note[t] < low)
			low = s->note[t];
	}
	return low;
}

/*
 * Rule 2 block of unassigned track `u` (index among U in track order) on a
 * non-G channel: start and size of its contiguous block in 0..region-1.
 */
static void midiNoteMap_splitBlock(uint8_t region, uint8_t count,
                                   uint8_t index, uint8_t *start,
                                   uint8_t *size)
{
	uint8_t base = (uint8_t)(region / count);
	uint8_t rem = (uint8_t)(region % count);

	*start = (uint8_t)(index * base + ((index < rem) ? index : rem));
	*size = (uint8_t)(base + ((index < rem) ? 1u : 0u));
}

static uint8_t midiNoteMap_clampInternal(uint16_t internal)
{
	return (uint8_t)((internal > 127u) ? 127u : internal);
}

uint8_t midiNoteMap_resolve(const midiNoteMap_settings_t *s, uint8_t channel,
                            uint8_t note, uint8_t *track, uint8_t *internal)
{
	uint8_t set;
	uint8_t owner = 0xFFu;
	uint8_t ownerNote = 0u;
	uint8_t unassigned = 0u;
	uint8_t count = 0u;
	uint8_t region;
	uint8_t t;

	if (!s || !track || !internal || note > 127u)
		return 0u;
	set = midiNoteMap_tracksOnChannel(s, channel);
	if (set == 0u)
		return 0u;
	/* Rule 1: the assigned track with the greatest root <= note; on equal
	** roots the first (lowest) track wins (rule 3 / N5). */
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		uint8_t root;

		if ((set & (1u << t)) == 0u)
			continue;
		root = s->note[t];
		if (root == 0u) {
			unassigned |= (uint8_t)(1u << t);
			count++;
			continue;
		}
		if (root <= note && (owner == 0xFFu || root > ownerNote)) {
			owner = t;
			ownerNote = root;
		}
	}
	if (owner != 0xFFu) {
		*track = owner;
		*internal = midiNoteMap_clampInternal(
		    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL + (note - ownerNote)));
		return 1u;
	}
	/* Rule 2: the note lies below every assigned root. */
	if (count == 0u)
		return 0u;
	region = midiNoteMap_lowestAssigned(s, set);
	if (channel == s->global) {
		uint8_t want = (uint8_t)(note % count);
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			if ((unassigned & (1u << t)) == 0u)
				continue;
			if (i++ == want) {
				*track = t;
				*internal = MIDI_NOTEMAP_ROOT_INTERNAL;
				return 1u;
			}
		}
		return 0u;
	}
	{
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			uint8_t start;
			uint8_t size;

			if ((unassigned & (1u << t)) == 0u)
				continue;
			midiNoteMap_splitBlock(region, count, i++, &start, &size);
			if (size != 0u && note >= start &&
			    note < (uint16_t)start + size) {
				*track = t;
				*internal = midiNoteMap_clampInternal(
				    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL +
				               (note - start)));
				return 1u;
			}
		}
	}
	return 0u;
}

uint8_t midiNoteMap_inverse(const midiNoteMap_settings_t *s, uint8_t track,
                            uint8_t internal, uint8_t *channel, uint8_t *note)
{
	uint8_t ch;
	uint8_t set;
	uint16_t candidate;
	uint8_t gotTrack;
	uint8_t gotInternal;

	if (!s || !channel || !note || track >= MIDI_NOTEMAP_TRACKS ||
	    internal < MIDI_NOTEMAP_ROOT_INTERNAL || internal > 127u)
		return 0u;
	ch = midiNoteMap_effectiveChannel(s, track);
	if (ch == 0u)
		return 0u;
	set = midiNoteMap_tracksOnChannel(s, ch);
	if (s->note[track] != 0u) {
		candidate = (uint16_t)(s->note[track] +
		                       (internal - MIDI_NOTEMAP_ROOT_INTERNAL));
	} else {
		uint8_t region = midiNoteMap_lowestAssigned(s, set);
		uint8_t count = 0u;
		uint8_t index = 0xFFu;
		uint8_t t;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			if ((set & (1u << t)) == 0u || s->note[t] != 0u)
				continue;
			if (t == track)
				index = count;
			count++;
		}
		if (index == 0xFFu)
			return 0u;
		if (ch == s->global) {
			if (internal != MIDI_NOTEMAP_ROOT_INTERNAL)
				return 0u;	/* stripe notes play default pitch only */
			candidate = index;	/* lowest stripe note of this track */
		} else {
			uint8_t start;
			uint8_t size;

			midiNoteMap_splitBlock(region, count, index, &start, &size);
			candidate = (uint16_t)(start +
			                       (internal - MIDI_NOTEMAP_ROOT_INTERNAL));
		}
	}
	if (candidate > 127u)
		return 0u;
	/* Q4: send only what plays back as the same track and pitch. */
	if (!midiNoteMap_resolve(s, ch, (uint8_t)candidate, &gotTrack,
	                         &gotInternal) ||
	    gotTrack != track || gotInternal != internal)
		return 0u;
	*channel = ch;
	*note = (uint8_t)candidate;
	return 1u;
}

#ifndef MIDI_NOTEMAP_HOST_TEST
/*
 * Snapshot one Scene's map inputs (S083).
 * What: G from MidiIn, channel and note per track from SceneData. Scene
 * index 16 (Scene 17) resolves through the SceneData accessors (S082 §5).
 * Read per message, never cached (S082 R14). Accessors: MidiNotes, MidiOut,
 * MidiControl, MidiProgram, MidiLearn.
 */
void midiNoteMap_load(uint8_t scene_index, midiNoteMap_settings_t *out)
{
	uint8_t t;

	out->global = midiIn_globalChannel();
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		out->channel[t] = scene_getTrackMidiChannel(scene_index, t);
		out->note[t] = scene_getTrackMidiNote(scene_index, t);
	}
}
#endif
```

Remark on the stripe rule: a stripe track's lowest note is its index among
the unassigned tracks (`n mod |U| == index`); it must also lie below the
lowest assigned root, which `resolve()` in the final check confirms.

### B-2 `tools/midi_test/notemap_test.c` — NEW FILE

```c
/*
 * notemap_test.c — host test for Core/MIDI/MidiNoteMap.c (S083 Phase B).
 *
 * Build and run:  make -C tools/midi_test run
 * (host cc only; no new utilities, the tools/dsp_test precedent).
 *
 * What it checks:
 *  1. the four worked examples of MIDI_SPEC §4.2, note by note;
 *  2. N5 (equal roots: the lower track wins, the other owns nothing);
 *  3. G off (unassigned tracks unmapped);
 *  4. for 200,000 pseudo-random settings: every (channel, note) maps to at
 *     most one track whose effective channel is that channel, and every
 *     inverse() result resolves back to the same (track, internal note)
 *     (the S083 Q4 transmit rule).
 * Exit status 0 = pass; failures print the first mismatches.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "MidiNoteMap.h"

static unsigned failures;

#define CHECK(cond, ...) do { if (!(cond)) { \
	if (failures < 20u) { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } failures++; } } while (0)

static void expect(const midiNoteMap_settings_t *s, uint8_t ch, uint8_t n,
                   int track, int internal)
{
	uint8_t t = 0xFF, i = 0xFF;
	uint8_t ok = midiNoteMap_resolve(s, ch, n, &t, &i);

	if (track < 0) {
		CHECK(!ok, "ch %u note %u: expected unmapped, got T%u/%u",
		      ch, n, t + 1u, i);
		return;
	}
	CHECK(ok && t == (uint8_t)track && i == (uint8_t)internal,
	      "ch %u note %u: expected T%d/%d, got %s T%u/%u", ch, n,
	      track + 1, internal, ok ? "" : "(unmapped)", t + 1u, i);
}

static void base(midiNoteMap_settings_t *s, uint8_t g)
{
	memset(s, 0, sizeof(*s));
	s->global = g;
}

static void examples(void)
{
	midiNoteMap_settings_t s;
	unsigned n;

	/* §4.2 row 1: G = 10, no track settings: stripe. */
	base(&s, 10u);
	expect(&s, 10u, 0u, 0, 63);
	expect(&s, 10u, 6u, 6, 63);
	expect(&s, 10u, 7u, 0, 63);
	expect(&s, 10u, 36u, 1, 63);
	expect(&s, 10u, 60u, 4, 63);
	expect(&s, 9u, 36u, -1, 0);

	/* Row 2: G = 10; T3 nte 48, T5 nte 60. */
	base(&s, 10u);
	s.note[2] = 48u;
	s.note[4] = 60u;
	{
		static const int stripe[5] = {0, 1, 3, 5, 6};
		for (n = 0u; n < 48u; n++)
			expect(&s, 10u, (uint8_t)n, stripe[n % 5u], 63);
	}
	for (n = 48u; n < 60u; n++)
		expect(&s, 10u, (uint8_t)n, 2, (int)(63u + n - 48u));
	for (n = 60u; n < 128u; n++)
		expect(&s, 10u, (uint8_t)n, 4,
		       (int)((63u + n - 60u > 127u) ? 127u : 63u + n - 60u));

	/* Row 3: T1, T2, T3 chn 2, no notes: 43 / 43 / 42 notes. */
	base(&s, 1u);
	s.channel[0] = s.channel[1] = s.channel[2] = 2u;
	expect(&s, 2u, 0u, 0, 63);
	expect(&s, 2u, 42u, 0, 105);
	expect(&s, 2u, 43u, 1, 63);
	expect(&s, 2u, 50u, 1, 70);
	expect(&s, 2u, 85u, 1, 105);
	expect(&s, 2u, 86u, 2, 63);
	expect(&s, 2u, 127u, 2, 104);

	/* Row 4: ch 2: T1 -, T2 nte 36, T3 -, T4 nte 60. */
	base(&s, 1u);
	s.channel[0] = s.channel[1] = s.channel[2] = s.channel[3] = 2u;
	s.note[1] = 36u;
	s.note[3] = 60u;
	expect(&s, 2u, 0u, 0, 63);
	expect(&s, 2u, 17u, 0, 80);
	expect(&s, 2u, 18u, 2, 63);
	expect(&s, 2u, 35u, 2, 80);
	expect(&s, 2u, 36u, 1, 63);
	expect(&s, 2u, 59u, 1, 86);
	expect(&s, 2u, 60u, 3, 63);

	/* N5: equal roots on ch 3. */
	base(&s, 1u);
	s.channel[1] = s.channel[3] = 3u;
	s.note[1] = s.note[3] = 40u;
	expect(&s, 3u, 40u, 1, 63);
	{
		uint8_t c, nn;
		CHECK(!midiNoteMap_inverse(&s, 3u, 63u, &c, &nn),
		      "N5 tie loser must have no transmit note");
	}

	/* G off: unassigned tracks unmapped; assigned channel still works. */
	base(&s, 0u);
	s.channel[0] = 5u;
	expect(&s, 1u, 36u, -1, 0);
	expect(&s, 5u, 36u, 0, 99);

	/* Q4 transmit rule on row 2. */
	base(&s, 10u);
	s.note[2] = 48u;
	s.note[4] = 60u;
	{
		uint8_t c = 0, nn = 0;
		CHECK(midiNoteMap_inverse(&s, 4u, 63u, &c, &nn) && c == 10u &&
		      nn == 60u, "T5 root");
		CHECK(midiNoteMap_inverse(&s, 2u, 70u, &c, &nn) && nn == 55u,
		      "T3 +7");
		CHECK(!midiNoteMap_inverse(&s, 2u, 75u, &c, &nn),
		      "T3 +12 lies in T5's range: no TX");
		CHECK(!midiNoteMap_inverse(&s, 0u, 65u, &c, &nn),
		      "stripe pitch: no TX");
		CHECK(!midiNoteMap_inverse(&s, 0u, 62u, &c, &nn),
		      "below default pitch: no TX");
	}
}

static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t m) { lcg = lcg * 1664525u + 1013904223u;
	return (lcg >> 8) % m; }

static void randomised(void)
{
	unsigned iter;

	for (iter = 0u; iter < 200000u; iter++) {
		midiNoteMap_settings_t s;
		unsigned t, ch, n, i;

		s.global = (uint8_t)rnd(17u);
		for (t = 0u; t < 7u; t++) {
			s.channel[t] = (uint8_t)(rnd(3u) ? 0u : 1u + rnd(4u));
			s.note[t] = (uint8_t)(rnd(3u) ? 0u : 1u + rnd(127u));
		}
		for (ch = 1u; ch <= 16u; ch++) {
			for (n = 0u; n < 128u; n++) {
				uint8_t tr, in;
				if (midiNoteMap_resolve(&s, (uint8_t)ch, (uint8_t)n,
				                        &tr, &in))
					CHECK(midiNoteMap_effectiveChannel(&s, tr) == ch &&
					      in >= 63u && in <= 127u,
					      "iter %u ch %u n %u -> T%u ec %u in %u", iter,
					      ch, n, tr + 1u,
					      midiNoteMap_effectiveChannel(&s, tr), in);
			}
		}
		for (t = 0u; t < 7u; t++) {
			for (i = 0u; i < 128u; i++) {
				uint8_t c, nn, tr, in;
				if (!midiNoteMap_inverse(&s, (uint8_t)t, (uint8_t)i,
				                         &c, &nn))
					continue;
				CHECK(midiNoteMap_resolve(&s, c, nn, &tr, &in) &&
				      tr == t && in == i,
				      "iter %u inverse T%u/%u -> ch %u n %u", iter,
				      t + 1u, i, c, nn);
			}
		}
	}
}

int main(void)
{
	examples();
	randomised();
	printf("%s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
```

### B-3 `tools/midi_test/Makefile` — NEW FILE

```make
# Host test for Core/MIDI/MidiNoteMap.c (S083). Host cc only.
CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SRC     = ../../Core/MIDI/MidiNoteMap.c

notemap_test: notemap_test.c $(SRC) ../../Core/MIDI/MidiNoteMap.h
	$(CC) $(CFLAGS) -DMIDI_NOTEMAP_HOST_TEST -I../../Core/MIDI \
	    -o $@ notemap_test.c $(SRC)

run: notemap_test
	./notemap_test

clean:
	rm -f notemap_test

.PHONY: run clean
```

### B-4 `Core/MIDI/MidiNotes.h` / `MidiNotes.c` — NEW FILES

**`MidiNotes.h`:**

```c
#ifndef MIDINOTES_H_
#define MIDINOTES_H_

/*
 * MidiNotes — what a received note does (S083, plan §3.4, N6, C8).
 *
 * What: resolves a note-on/off through the active Scene's note map, then
 * either triggers the track once at the note's velocity and pitch, or,
 * when a BAR overlay is selected (a held BAR in PERF, else CC2), performs
 * that overlay's action through the per-track overlay executor. Reports
 * each single trigger to LiveRecord with the note's arrival age. Holds the
 * CC2 note-action state.
 * Why: N1-N6; Q5 (concurrent rolls per track); Q6 (arrival time).
 * Context: foreground (midiIn_dispatch()).
 * RAM: CC2 state 1 B SRAM1 (approved). Held notes live in the overlay
 * table (buttonHandler.c).
 * Affiliates: MidiNoteMap, buttonHandler overlay executor, sequencer
 * (seq_triggerVoice(), seq_isTrackMuted()), LiveRecord, MidiIn.
 */

#include <stdint.h>
#include "MidiDefs.h"

/*
 * Act on one note-on/off (RXF N already checked by the caller).
 * Output: 1 = consumed (it resolved to a track, even a muted one),
 * 0 = unmapped (the caller forwards it). Accessor: midiIn_dispatch().
 */
uint8_t midiNotes_handle(const MidiMsg *msg);
/*
 * CC2 on G (C8): 0 single trigger, 1 BAR `<` overlay, 2 BAR `>` overlay.
 * Output: 1 consumed (0..2), 0 other values (ignored and forwarded).
 * The value persists until the next CC2; reset at boot. A change
 * re-configures MIDI-held overlay entries while no BAR is held.
 * Accessors: midiIn_dispatch() (B), midiControl_handleCc() (E).
 */
uint8_t midiNotes_setNoteAction(uint8_t value);
/* Current CC2 state (0..2). */
uint8_t midiNotes_noteAction(void);

#endif /* MIDINOTES_H_ */
```

**`MidiNotes.c`:**

```c
#include "MidiNotes.h"
#include "MidiNoteMap.h"
#include "MidiIn.h"
#include "buttonHandler.h"
#include "sequencer.h"
#include "LiveRecord.h"
#include "SceneData.h"
#include "timebase.h"
#include "config.h"

/* CC2 note action (C8): 0 single, 1 BAR <, 2 BAR >. Reset at boot. 1 B. */
static uint8_t midiNotes_action = 0u;

/*
 * Arrival age of a message in µs (S083 Q6).
 * What: now - stamp in the 16-bit µs domain; ages above
 * MIDI_ARRIVAL_MAX_AGE_US (or a stale stamp after a long stall) return 0,
 * which means "use processing time". Inputs: 16-bit stamp. Output: age.
 */
static uint16_t midiNotes_ageUs(uint16_t stamp)
{
	uint16_t age = (uint16_t)((uint16_t)timebase_tim2Now() - stamp);

	return (age > MIDI_ARRIVAL_MAX_AGE_US) ? 0u : age;
}

/* Contract in MidiNotes.h. */
uint8_t midiNotes_setNoteAction(uint8_t value)
{
	if (value > 2u)
		return 0u;
	if (value != midiNotes_action) {
		midiNotes_action = value;
		buttonHandler_overlayMidiActionChanged(value);
	}
	return 1u;
}

uint8_t midiNotes_noteAction(void) { return midiNotes_action; }

uint8_t midiNotes_handle(const MidiMsg *msg)
{
	midiNoteMap_settings_t map;
	uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);
	uint8_t on = (uint8_t)((msg->status & 0xF0u) == NOTE_ON &&
	                       msg->data2 != 0u);
	uint8_t track;
	uint8_t internal;
	uint8_t overlay;

	/*
	 * Resolve on the active Scene (Scene 17 while it is active), read per
	 * message (S082 R14). An unmapped note is not consumed.
	 */
	midiNoteMap_load(scene_getActiveIndex(), &map);
	if (!midiNoteMap_resolve(&map, channel, msg->data1, &track, &internal))
		return 0u;
	if (!on) {
		/* Note-off ends a MIDI-held roll; single triggers ignore it. */
		buttonHandler_overlayMidiNoteOff(track, msg->data1);
		return 1u;
	}
	if (seq_isTrackMuted(track))
		return 1u;	/* resolved, so consumed; a muted track is silent */
	/* C-9: a held BAR in PERF wins over CC2. */
	overlay = buttonHandler_overlayHeldBar();
	if (overlay == 0u)
		overlay = midiNotes_action;
	if (overlay != 0u) {
		buttonHandler_overlayMidiNoteOn(track, overlay, internal,
		                                msg->data1,
		                                midiNotes_ageUs(msg->stamp));
		return 1u;
	}
	/*
	 * Single trigger (N6): one resetting trigger at the note's velocity and
	 * resolved pitch, through the sequencer's trigger path (trigger jack and
	 * TX N as for any LXR trigger, C-3), reported to LiveRecord with the
	 * arrival age. note_is_default: a default-pitch hit records no note
	 * special (S081 Q12 rule, as for the overlay `def`).
	 */
	seq_triggerVoice(track, msg->data2, internal, 1u);
	{
		liveRec_trigger_t lr;

		lr.track = track;
		lr.note = internal;
		lr.velocity = msg->data2;
		lr.kind = LIVEREC_HIT_SINGLE;
		lr.roll = 0u;
		lr.source = LIVEREC_SRC_MIDI;
		lr.note_is_default =
		    (uint8_t)(internal == MIDI_NOTEMAP_ROOT_INTERNAL);
		lr.age_us = midiNotes_ageUs(msg->stamp);
		liveRec_triggerIn(&lr);
	}
	return 1u;
}
```

### B-5 `Core/MIDI/MidiIn.c` — note and CC2 consumers

**Where:** `midiIn_dispatch()`, the `switch (type)` written in A-8.
**Action:** MODIFY the `NOTE_ON/NOTE_OFF` and `MIDI_CC` cases; ADD
`#include "MidiNotes.h"`.

```c
	case NOTE_ON:
	case NOTE_OFF:
		/* N1-N6: notes resolve through the active Scene's note map. */
		if (midiIn_rxAllows(MIDI_FILTER_NOTE))
			consumed = midiNotes_handle(msg);
		break;
	case MIDI_CC:
		/*
		 * Phase B: CC2 (note action) on G only (C8). Phase D adds CC0;
		 * Phase E replaces this branch with midiControl_handleCc().
		 */
		if (midiIn_rxAllows(MIDI_FILTER_CONTROL) &&
		    msg->data1 == CC_NOTE_ACTION &&
		    midiIn_config.global != 0u &&
		    (uint8_t)((msg->status & 0x0Fu) + 1u) == midiIn_config.global)
			consumed = midiNotes_setNoteAction(msg->data2);
		break;
```

### B-6 `Makefile`

**Where:** the MIDI source list from A-11. **Action:** ADD
`  Core/MIDI/MidiNoteMap.c \` and `  Core/MIDI/MidiNotes.c \`.

### B-7 `Core/Sequencer/LiveRecord.h` — arrival age in the trigger record

**Where:** `LiveRecord.h:63-78` (`liveRec_trigger_t` and its comment).
**Action:** MODIFY.

```c
/*
 * One trigger hit reported when it sounds (S081 §4.2; S083 Q6 age).
 *
 * Inputs: track 0..6, note, velocity, kind (LIVEREC_HIT_*), roll (the step
 * roll index for a ROLL_START; 0 otherwise), source (LIVEREC_SRC_*),
 * note_is_default (1 when the note came from the track default, so a 'def'
 * records no special), age_us (S083: how long ago the hit actually
 * happened, in µs; 0 = now). A MIDI note's age is its arrival-to-processing
 * delay, so placement uses the arrival time (Q6). Panel, overlay repeat and
 * sequencer hits pass 0. Stack only (+2 B per record).
 */
typedef struct {
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t kind;
    uint8_t roll;
    uint8_t source;
    uint8_t note_is_default;
    uint16_t age_us;
} liveRec_trigger_t;
```

Also update the accessor comment at `LiveRecord.h:103`
("Triggers (buttonHandler overlay now, MidiParser later)") →
"Triggers (buttonHandler overlay, MidiNotes)".

### B-8 `Core/Sequencer/LiveRecord.c` — place MIDI hits at arrival time

**Where:** `liveRec_triggerIn()`, lines 1307-1327, from
`seq_captureTrackTime(track, &snap);` to the end of the quantise `switch`.
**Action:** MODIFY.

```c
    seq_captureTrackTime(track, &snap);
    frac8 = liveRec_frac8(&snap);
    setFil = seq_fillActive;

    /*
     * Arrival-time correction (S083 Q6).
     *
     * What: posQ8 is the hit's position relative to the captured tick in
     * Q8 ticks: the sub-tick fraction now, minus the hit's age (t->age_us
     * converted with the current tick period). The shift is limited to one
     * step interval of the track, so a placement never reaches further back
     * than the step before the current one (seq_placeFromSnapshot()'s rel
     * range). Why: a MIDI note waits in a buffer while the foreground
     * renders; placed at processing time it lands late and can quantise to
     * the next step. Panel and repeat hits have age 0 and are unchanged.
     * Inputs: snap.periodUs (tick period), snap.interval (step, Q8 ticks).
     * Output: posQ8 (signed), used by every quantise mode below.
     * Affiliates: MidiNotes (age), seq_quantiseTick(), seq_placeFromSnapshot().
     */
    {
        int32_t ageQ8 = 0;
        int32_t posQ8;
        int32_t whole;

        if (t->age_us != 0u && snap.periodUs != 0u) {
            ageQ8 = (int32_t)(((uint32_t)t->age_us * 256u) / snap.periodUs);
            if (ageQ8 > (int32_t)snap.interval)
                ageQ8 = (int32_t)snap.interval;
        }
        posQ8 = (int32_t)frac8 - ageQ8;
        whole = (posQ8 >= 0) ? (posQ8 >> 8) : -((-posQ8 + 255) >> 8);

        /* Quantise: master grid for 1/8..1/64, own steps for trk, none for off. */
        switch (seq_getQuantisation()) {
        case QUANT_TRACK:
            deltaQ8 = posQ8;
            snapToStep = 1u;
            break;
        case QUANT_8:
        case QUANT_16:
        case QUANT_32:
        case QUANT_64: {
            uint32_t tick = ((int32_t)snap.elapsedTick + whole >= 0)
                ? (uint32_t)((int32_t)snap.elapsedTick + whole) : 0u;
            uint8_t f = (uint8_t)(posQ8 - whole * 256);
            uint32_t q = seq_quantiseTick(tick, f);

            deltaQ8 = (int32_t)(((int32_t)q - (int32_t)snap.elapsedTick) * 256);
            break; }
        default:
            deltaQ8 = posQ8;
            break;
        }
    }
```

Also update the comment at `LiveRecord.c:1266` ("Accessors: buttonHandler
overlay (E-2), midiParser_noteOn()") → "buttonHandler overlay (E-2),
midiNotes_handle() (S083)", and at 1457 `midiParser_enterTaggedParameter()`
→ `midiControl_writeTarget() (S083)`.

### B-9 `Core/Hardware/frontPanel/buttonHandler.c` — per-track overlay executor (Q5)

**B-9a State.** **Where:** 366-410 (`bh_overlayActive` through
`bh_overlayNoteDefault`). **Action:** MODIFY: keep `bh_overlayActive`,
`bh_overlayVoiceMask` and `bh_overlayHeldType`; REMOVE `bh_overlayTrack`,
`bh_overlayNote`, `bh_overlayVel`, `bh_overlayCount`, `bh_overlayInterval`,
`bh_overlayNext`, `bh_overlayNoteDefault`; ADD the table:

```c
/*
 * Per-track overlay repeat state (S083 Q5; was the S080 E12 single slot).
 *
 * What: one entry per track. heldMidiNote: the MIDI note whose note-off
 * ends this track's roll, stored as note + 1 (0 = panel or none, so the
 * zero-initialised table needs no init code). note: internal note, bit 7
 * set when it came from `def` (records no note special, S081 Q12). vel:
 * repeat velocity. count: remaining repeats (0 idle, 1 = flam main pending,
 * 0xFF = held roll). interval: repeat period in ms (time_sysTick). next:
 * next deadline. Why: MIDI notes on several tracks (and several held
 * VOICE buttons) roll at the same time; the S080 single slot let the
 * latest overlay take over the only timer. RAM: 7 x 8 B = 56 B SRAM1,
 * static (Q5 approved; R-A +7 B), replaces 9 B. Writers: overlay start,
 * release, switch and tick below (all foreground). Affiliates:
 * buttonHandler_tick(), MidiNotes.
 */
typedef struct {
    uint8_t  heldMidiNote;
    uint8_t  note;
    uint8_t  vel;
    uint8_t  count;
    uint16_t interval;
    uint16_t next;
} bh_overlay_track_t;

#define BH_OVERLAY_NOTE_DEFAULT  0x80u
#define BH_OVERLAY_NO_MIDI_NOTE  0u   /* heldMidiNote stores note + 1 */

static bh_overlay_track_t bh_overlay[NUM_TRACKS];
```

The table is zero-initialised (`.bss`); 0 means "no MIDI note", so no
init code is needed (buttonHandler has no init function).

**B-9b Report.** **Where:** 945-961 `buttonHandler_overlayReport()`.
**Action:** MODIFY: add `uint8_t source, uint16_t ageUs` parameters;
set `t.source = source; t.age_us = ageUs;`. Comment addition: "S083:
source is LIVEREC_SRC_OVERLAY for panel overlays and LIVEREC_SRC_MIDI for
MIDI-started ones; ageUs is the MIDI arrival age of the first hit (0 for
repeats and panel hits)."

**B-9c Start.** **Where:** 963-1035 `buttonHandler_overlayTrigger()`.
**Action:** MODIFY into a shared start function plus the panel wrapper:

```c
/*
 * Start an overlay action on one track (S080 E12; S083 Q5 per-track).
 *
 * What: reads the trigger type of overlay `which` (1 = BAR <, 2 = BAR >),
 * fires the first hit (single: one trigger at the type's velocity; flam:
 * soft lead hit now, main 1/32 later; roll: first hit now, repeats until
 * release) and fills the track's entry. Inputs: track, which, internal
 * note (bit 7 = `def`), heldMidiNote (MIDI note + 1 that ends a roll, or
 * BH_OVERLAY_NO_MIDI_NOTE), source and age for LiveRecord. Outputs:
 * seq_triggerVoice(), LiveRecord report, bh_overlay[track]. Callers:
 * buttonHandler_overlayTrigger() (panel), buttonHandler_overlayMidiNoteOn()
 * (MidiNotes). Affiliates: buttonHandler_overlayTick(), _Release().
 */
static void buttonHandler_overlayStart(uint8_t track, uint8_t which,
                                       uint8_t note, uint8_t heldMidiNote,
                                       uint8_t source, uint16_t ageUs)
{
    bh_overlay_track_t *e = &bh_overlay[track];
    uint8_t type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                                 : parameter_values[PAR_PERF_BTN_RT];
    uint8_t pitch = (uint8_t)(note & 0x7Fu);
    uint8_t isDef = (uint8_t)((note & BH_OVERLAY_NOTE_DEFAULT) != 0u);
    uint16_t interval;

    if (type > 11u)
        type = 0u;
    if (type <= 3u) {
        static const uint8_t singleVel[4] = {127u, 100u, 64u, 32u};

        seq_triggerVoice(track, singleVel[type], pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, singleVel[type],
                                    LIVEREC_HIT_SINGLE, 0u, source, ageUs);
        e->count = 0u;
        e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
        return;
    }
    if (type == 4u || type == 5u) {
        seq_triggerVoice(track, 64u, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, 64u,
                                    LIVEREC_HIT_FLAM_GRACE, 0u, source, ageUs);
        interval = buttonHandler_overlayDivisionMs(5u);
        e->vel = 100u;
        e->count = 1u;
    } else {
        static const uint8_t rollDiv[6] = {3u, 4u, 5u, 6u, 6u, 7u};
        static const uint8_t rollIdx[6] = {1u, 3u, 5u, 7u, 8u, 9u};
        uint8_t idx = (uint8_t)(type - 6u);

        interval = buttonHandler_overlayDivisionMs(rollDiv[idx]);
        if (idx == 4u)                                  /* r6t triplet */
            interval = (uint16_t)((interval * 2u) / 3u);
        seq_triggerVoice(track, 100u, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, 100u,
                                    LIVEREC_HIT_ROLL_START, rollIdx[idx],
                                    source, ageUs);
        e->vel = 100u;
        e->count = 0xFFu;
    }
    e->note = note;
    e->heldMidiNote = heldMidiNote;
    e->interval = (interval != 0u) ? interval : 1u;
    e->next = (uint16_t)(time_sysTick + e->interval);
}

/*
 * Panel VOICE overlay press (S080 E12; S083 C-1 `def` note).
 *
 * What: the held VOICE fires the active BAR overlay. A `def` note
 * (perf_note_lt/rt = 0) plays the default pitch (internal 63); before
 * S083 it played the track's MIDI note as a pitch, which made `nte` mean
 * two things (C-1). Inputs: voiceNr, bh_overlayActive, overlay params.
 * Outputs: bh_overlayVoiceMask, bh_overlayHeldType, the track's entry.
 */
static void buttonHandler_overlayTrigger(uint8_t voiceNr)
{
    uint8_t note = (bh_overlayActive == 1u)
                   ? parameter_values[PAR_PERF_NOTE_LT]
                   : parameter_values[PAR_PERF_NOTE_RT];
    uint8_t type = (bh_overlayActive == 1u)
                   ? parameter_values[PAR_PERF_BTN_LT]
                   : parameter_values[PAR_PERF_BTN_RT];

    if (note == 0u)
        note = (uint8_t)(MIDI_DEFAULT_TRIGGER_NOTE | BH_OVERLAY_NOTE_DEFAULT);
    bh_overlayVoiceMask |= (uint8_t)(1u << voiceNr);
    bh_overlayHeldType = (type > 11u) ? 0u : type;
    buttonHandler_overlayStart(voiceNr, bh_overlayActive, note,
                               BH_OVERLAY_NO_MIDI_NOTE,
                               LIVEREC_SRC_OVERLAY, 0u);
}
```

**B-9d Release.** **Where:** 1037-1053. **Action:** MODIFY:

```c
/*
 * End one track's overlay gesture (S080 E12; S083 per-track).
 *
 * What: stops the track's repeats, sends the voice and MIDI note-off, and
 * ends any recorded roll. Panel: only for a VOICE that fired an overlay
 * (bh_overlayVoiceMask). Inputs: voiceNr. Outputs: entry idle, masks.
 * Callers: processRelease() (VOICE up), the looper engage, MIDI note-off
 * (through buttonHandler_overlayEndTrack()). Affiliates: liveRec_rollRelease(),
 * midiOut_trackNoteOff().
 */
static void buttonHandler_overlayEndTrack(uint8_t track)
{
    bh_overlay[track].count = 0u;
    bh_overlay[track].heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
    voiceControl_noteOff(track);
    midiOut_trackNoteOff(track);
    liveRec_rollRelease(track);
}

static void buttonHandler_overlayRelease(uint8_t voiceNr)
{
    uint8_t bit = (uint8_t)(1u << voiceNr);

    if ((bh_overlayVoiceMask & bit) == 0u)
        return;
    bh_overlayVoiceMask = (uint8_t)(bh_overlayVoiceMask & ~bit);
    if (bh_overlayVoiceMask == 0u)
        bh_overlayHeldType = 0xFFu;
    buttonHandler_overlayEndTrack(voiceNr);
}
```

**B-9e Switch.** **Where:** 1083-1150 `buttonHandler_overlaySwitchHeld()`.
**Action:** MODIFY into a per-track reconfigure called for the panel
(BAR press, unchanged call site 2997) and for MIDI (CC2 change):

```c
/*
 * Re-configure active overlay entries after an overlay change (S081 §5.7
 * R19/F-B/F-O; S083 per-track).
 *
 * What: for every entry selected by `midiOnly` (0: panel-held and
 * MIDI-held entries, after a BAR press; 1: MIDI-held entries only, after
 * a CC2 change while no BAR is held) applies the S081 rules per track,
 * judging "was a roll" by the entry's own count (0xFF):
 *  - overlay off: repeats stop, no extra hit, recorded roll ends;
 *  - roll -> roll: new interval from the last hit time;
 *  - single/flam -> roll: repeats start one interval from now;
 *  - roll -> single/flam: repeats stop, recorded roll ends.
 * Inputs: which (0 off, 1 <, 2 >), midiOnly. Outputs: entries, LiveRecord
 * rate changes. Callers: BAR press (processPress), CC2 change
 * (buttonHandler_overlayMidiActionChanged()).
 */
static void buttonHandler_overlayReconfigure(uint8_t which, uint8_t midiOnly)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        bh_overlay_track_t *e = &bh_overlay[track];
        uint8_t midiHeld = (uint8_t)(e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE);
        uint8_t panelHeld = (uint8_t)((bh_overlayVoiceMask >> track) & 1u);
        uint8_t type;
        uint8_t oldRoll = (uint8_t)(e->count == 0xFFu);

        if (!(midiHeld || (!midiOnly && panelHeld)))
            continue;
        if (which == 0u) {
            e->count = 0u;
            liveRec_rollRelease(track);
            continue;
        }
        type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                             : parameter_values[PAR_PERF_BTN_RT];
        if (type > 11u)
            type = 0u;
        if (type < 6u) {
            e->count = 0u;
            liveRec_rollRateChange(track, 0u);
            continue;
        }
        {
            static const uint8_t rollDiv[6] = {3u, 4u, 5u, 6u, 6u, 7u};
            static const uint8_t rollIdx[6] = {1u, 3u, 5u, 7u, 8u, 9u};
            uint8_t idx = (uint8_t)(type - 6u);
            uint16_t interval = buttonHandler_overlayDivisionMs(rollDiv[idx]);

            if (idx == 4u)
                interval = (uint16_t)((interval * 2u) / 3u);
            if (interval == 0u)
                interval = 1u;
            if (oldRoll) {
                uint16_t lastHit = (uint16_t)(e->next - e->interval);

                e->interval = interval;
                e->next = (uint16_t)(lastHit + interval);
            } else {
                e->vel = 100u;
                e->interval = interval;
                e->next = (uint16_t)(time_sysTick + interval);
                e->count = 0xFFu;
            }
            liveRec_rollRateChange(track, rollIdx[idx]);
        }
    }
}

static void buttonHandler_overlaySwitchHeld(void)
{
    /* Panel BAR change: the held type follows the new overlay (S081). */
    if (bh_overlayVoiceMask != 0u) {
        uint8_t type = (bh_overlayActive == 1u)
                       ? parameter_values[PAR_PERF_BTN_LT]
                       : parameter_values[PAR_PERF_BTN_RT];
        bh_overlayHeldType = (bh_overlayActive == 0u) ? 0xFFu
                             : ((type > 11u) ? 0u : type);
    }
    buttonHandler_overlayReconfigure(bh_overlayActive, 0u);
}
```

The old note re-read in the switch (lines 1101-1110, the `def` →
track-MIDI-note lookup) is removed: a switch keeps each entry's note.

**B-9f Tick.** **Where:** 1185-1210 `buttonHandler_overlayTick()`.
**Action:** MODIFY:

```c
/*
 * Fire due overlay repeats on every track (S080 E12; S083 per-track).
 *
 * What: for each entry with count != 0 whose deadline passed (signed
 * 16-bit distance, S081 P4 rule), fires the flam main (resetting) or a
 * roll repeat (non-resetting) and reports it (age 0; source MIDI when the
 * entry is MIDI-held). Context: buttonHandler_tick(), foreground.
 */
static void buttonHandler_overlayTick(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        bh_overlay_track_t *e = &bh_overlay[track];
        uint8_t pitch = (uint8_t)(e->note & 0x7Fu);
        uint8_t isDef = (uint8_t)((e->note & BH_OVERLAY_NOTE_DEFAULT) != 0u);
        uint8_t source = (e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE)
                         ? LIVEREC_SRC_MIDI : LIVEREC_SRC_OVERLAY;

        if (e->count == 0u)
            continue;
        if ((uint16_t)(time_sysTick - e->next) >= 32768u)
            continue;
        seq_triggerVoice(track, e->vel, pitch, (uint8_t)(e->count != 0xFFu));
        buttonHandler_overlayReport(track, pitch, isDef, e->vel,
                                    (e->count == 0xFFu)
                                        ? LIVEREC_HIT_ROLL_HIT
                                        : LIVEREC_HIT_FLAM_MAIN,
                                    0u, source, 0u);
        if (e->count != 0xFFu) {
            e->count--;
            if (e->count == 0u) {
                e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
                continue;
            }
        }
        e->next = (uint16_t)(e->next + e->interval);
    }
}
```

**B-9g Public MIDI entry points.** ADD after `buttonHandler_overlayTick()`:

```c
/* Contracts in buttonHandler.h. */
uint8_t buttonHandler_overlayHeldBar(void)
{
    return (bh_state.selectButtonMode == SELECT_MODE_PERF)
           ? bh_overlayActive : 0u;
}

void buttonHandler_overlayMidiNoteOn(uint8_t track, uint8_t which,
                                     uint8_t internal, uint8_t midiNote,
                                     uint16_t ageUs)
{
    if (track >= NUM_TRACKS || which == 0u || which > 2u)
        return;
    buttonHandler_overlayStart(track, which,
                               (uint8_t)(internal & 0x7Fu),
                               (uint8_t)((midiNote & 0x7Fu) + 1u),
                               LIVEREC_SRC_MIDI, ageUs);
}

void buttonHandler_overlayMidiNoteOff(uint8_t track, uint8_t midiNote)
{
    uint8_t key = (uint8_t)((midiNote & 0x7Fu) + 1u);
    uint8_t t;

    /*
     * The resolved track first; if it does not hold this note (the map
     * changed between note-on and note-off), the first track that does.
     * A roll ends; a flam keeps its pending main hit (B-12).
     */
    if (track < NUM_TRACKS && bh_overlay[track].heldMidiNote == key) {
        t = track;
    } else {
        for (t = 0u; t < NUM_TRACKS; t++) {
            if (bh_overlay[t].heldMidiNote == key)
                break;
        }
        if (t >= NUM_TRACKS)
            return;
    }
    if (bh_overlay[t].count == 0xFFu)
        buttonHandler_overlayEndTrack(t);
    else
        bh_overlay[t].heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
}

void buttonHandler_overlayMidiActionChanged(uint8_t which)
{
    /* C-9: while a BAR is held the BAR's overlay governs. */
    if (buttonHandler_overlayHeldBar() != 0u)
        return;
    buttonHandler_overlayReconfigure(which, 1u);
}
```

**B-9h Includes.** ADD `#include "MidiOut.h"` in `buttonHandler.c`.

### B-10 `Core/Hardware/frontPanel/buttonHandler.h` — MIDI overlay API

**Where:** after `void buttonHandler_clearAllMutes(void);` (line 125).
**Action:** ADD.

```c
/*
 * Overlay executor entry points for MIDI notes (S083 Q5, N6, C8).
 *
 * buttonHandler_overlayHeldBar(): 1/2 while BAR </> is held in PERF, else
 * 0. MidiNotes gives a held BAR priority over CC2 (C-9).
 * buttonHandler_overlayMidiNoteOn(): start overlay `which` (1/2) on `track`
 * at `internal` pitch; `midiNote` is remembered so its note-off ends a
 * roll; ageUs is the arrival age for the first hit's record placement.
 * buttonHandler_overlayMidiNoteOff(): end the roll held by `midiNote`
 * (the resolved track first, then any track holding that note); a flam's
 * main hit still plays.
 * buttonHandler_overlayMidiActionChanged(): CC2 changed to `which`;
 * re-configures MIDI-held entries unless a BAR is held.
 * Context: foreground. Accessor: MidiNotes.c. Affiliates: the per-track
 * overlay table, LiveRecord.
 */
uint8_t buttonHandler_overlayHeldBar(void);
void    buttonHandler_overlayMidiNoteOn(uint8_t track, uint8_t which,
                                        uint8_t internal, uint8_t midiNote,
                                        uint16_t ageUs);
void    buttonHandler_overlayMidiNoteOff(uint8_t track, uint8_t midiNote);
void    buttonHandler_overlayMidiActionChanged(uint8_t which);
```

### B-11 `buttonHandler.c` — other uses of the removed single slot

| Line(s) | Anchor | Action |
|---------|--------|--------|
| 3616-3618 area (`buttonHandler_processEvents()` overflow recovery) | `bh_overlayActive = 0u;` | ADD after it: `buttonHandler_overlayReconfigure(0u, 0u);` (stop every repeat when the event ring overflowed, comment "S083: per-track entries stop with the overlay") |
| 3278-3282 | VOICE release calls `buttonHandler_overlayRelease()` | unchanged |
| 2236-2241 | looper engage releases panel overlays | unchanged (MIDI-held rolls are not VOICE holds and keep running; documented in B-12) |

### B-12 Phase B behaviour notes (for the user and MIDI_SPEC at Phase H)

- MIDI-held rolls are not VOICE holds: they do not hold or join the
  looper, and a looper engage does not end them.
- A MIDI flam ignores the note-off (the main hit always plays); a roll
  ends at its note-off.
- MIDI single triggers are sent out again on the track's inverse-mapped
  note (C-3) once Phase G lands; until then they use the A-14a rule.

### B-13 Phase B verification

1. `make -C tools/midi_test run` → PASS (all §4.2 examples, N5, G off, 200,000 random settings).
2. Hardware note matrix: each §4.2 example on DIN and USB; a note to a muted track is silent and not forwarded; an unmapped note is forwarded per MRT.
3. CC2 0/1/2 on G with each overlay type (single vmx/v10/v64/v32, flams, rolls r/8..128): rolls on two and three tracks at once from MIDI; panel VOICE rolls on two tracks at once (R-A, R23); BAR held overrides CC2.
4. Live record (LATCH, `qnt` off/16/`trk`) at 160 BPM with MIDI notes on step boundaries while the CPU widget is high: hits land on the played step (Q6). Compare with age forced to 0 (temporary build) to see the difference; record both in §12.
5. Phase A checks 2-5 again (overlay changes touch the panel path).

---

## 6. Phase C — Transport: Continue, SPP, `snc` gate

**Goal.** T1: Start restarts from the Pattern start, Continue resumes from
where Stop left off (or from the last SPP), SPP is honoured while stopped
from the source `snc` selects, and transport received while slaved is not
re-sent by the LXR (C-11; MRT forwarding still carries it).

### C-1 `Core/Sequencer/sequencer.c` — resume position and Continue

**C-1a State.** **Where:** after `seq_elapsedPpqTicks` (line 87).
**Action:** ADD.

```c
/*
 * Resume position for MIDI Continue and Song Position Pointer (S083 T1).
 *
 * What: the master tick (96 PPQ) at which a Continue resumes. Written at
 * every stop (the tick playback reached), by an SPP while stopped
 * (SPP x 24), and cleared by Start. Why: a stop resets seq_elapsedPpqTicks
 * to 0, so without this Continue could only restart. RAM: 4 B SRAM1
 * (R-C). Writers: seq_setRunningEx() (stop/start), seq_setResumeTick()
 * (SPP), seq_continueRunning() (consumes). Context: TIM3 and foreground
 * (single aligned 32-bit word; Cortex-M7 reads/writes it atomically).
 */
static uint32_t seq_resumeTick = 0u;
```

**C-1b `seq_setRunning()` → `seq_setRunningEx()`.** **Where:**
6226-6276 (anchor `void seq_setRunning(uint8_t isRunning)`).
**Action:** MODIFY: rename the function to
`void seq_setRunningEx(uint8_t isRunning, uint8_t sendRealtime)` and:

| Inside | Change |
|--------|--------|
| stop branch, before `seq_running = 0u;` | ADD `if (seq_running) seq_resumeTick = seq_elapsedPpqTicks;` |
| stop branch `seq_sendRealtime(MIDI_STOP);` | MODIFY → `if (sendRealtime) seq_sendRealtime(MIDI_STOP);` |
| start branch `seq_sendRealtime(MIDI_START);` | MODIFY → `if (sendRealtime) seq_sendRealtime(MIDI_START);` and ADD `seq_resumeTick = 0u;` |

Add to the function's comment block: "S083: `sendRealtime` = 0 when the
change came from received MIDI transport (C-11: MRT forwarding carries the
received message; the LXR does not send a second copy). A stop records the
resume position for Continue (R-C)." ADD after the function:

```c
/* Front panel, MTC, menu: transport change with LXR transport output. */
void seq_setRunning(uint8_t isRunning)
{
	seq_setRunningEx(isRunning, 1u);
}

/*
 * Resume playback at seq_resumeTick (S083 T1, MIDI Continue).
 *
 * What: the start sequence of seq_setRunningEx() (scheduler reset, trigger
 * reset, chain counts, automation restore through
 * seq_setStepIndexToStart()), then, for a non-zero resume tick, places the
 * master clock one tick before it and realigns every track and the FX
 * sequence to it through the master-clock anchors (the same mapping as the
 * per-track realign, PATTERN_DYNAMIC_STACK.md §6.5), so the next scheduler
 * tick plays position `resume`. A zero resume tick is a plain start.
 * Why: Continue must resume, not restart (plan §3.8). Inputs:
 * sendRealtime (0 when the Continue was received, C-11), seq_resumeTick.
 * Outputs: transport running at the resume position; seq_resumeTick
 * cleared. Context: TIM3 (sync_midiContinue()); the realign helpers are
 * context-safe (pat_sceneRegion() SRAM reads, FX realign under PRIMASK).
 * Affiliates: seq_realignActivePatternToMasterClock(), clockSync.c.
 */
void seq_continueRunning(uint8_t sendRealtime)
{
	uint32_t resume = seq_resumeTick;

	if (seq_running)
		return;
	seq_playbackEpoch++;
	seq_rampBreakAll();
	seq_resetStepScheduler();
	if (sendRealtime)
		seq_sendRealtime(MIDI_CONTINUE);
	trigger_reset(1);
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	seq_setStepIndexToStart();
	if (resume != 0u) {
		seq_elapsedPpqTicks = resume - 1u;
		seq_masterStepClock = (uint16_t)(seq_elapsedPpqTicks /
		                                 SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP);
		seq_masterStepCnt = (uint8_t)seq_masterStepClock;
		seq_initialSchedulerTick = 0u;
		seq_realignActivePatternToMasterClock();
	}
	seq_resumeTick = 0u;
	seq_running = 1u;
}

/*
 * Song Position Pointer while stopped (S083 T1).
 * What: stores `tick` (SPP x 24) as the resume position; ignored while the
 * transport runs. Accessor: midiTransport_handleSystemCommon().
 */
void seq_setResumeTick(uint32_t tick)
{
	if (!seq_running)
		seq_resumeTick = tick;
}
```

### C-2 `Core/Sequencer/sequencer.h`

**Where:** after `void seq_realignActivePatternToMasterClock(void);`
(line 230). **Action:** ADD the three declarations (`seq_setRunningEx`,
`seq_continueRunning`, `seq_setResumeTick`) with one-line summaries
pointing to the contracts in `sequencer.c` (the file's existing style for
transport functions).

### C-3 `Core/Sequencer/clockSync.c/h` — received Start/Stop/Continue

**Where:** `clockSync.c:123-136` (`sync_midiStartStop()`).
**Action:** MODIFY `seq_setRunning(1)` / `seq_setRunning(0)` →
`seq_setRunningEx(1u, 0u)` / `seq_setRunningEx(0u, 0u)`; ADD after it:

```c
/*
 * MIDI Continue (S083 T1).
 *
 * What: like Start, arms clock counting from the next MIDI clock, but the
 * sequencer resumes at the stored position (seq_continueRunning()). The
 * Continue is not re-sent (C-11). Context: TIM3 (realtime handler).
 * Accessor: MidiTransport.c. Affiliates: seq_continueRunning().
 */
void sync_midiContinue(void)
{
	sync_clockCnt = 0;
	seq_continueRunning(0u);
	sync_timeLastUs = timebase_tim2Now();
	sync_haveTimestamp = 0;
}
```

`clockSync.h:64`: ADD `void sync_midiContinue(void);`.

### C-4 `Core/MIDI/MidiTransport.c` — Continue and SPP

**C-4a** `midiTransport_handleEvent()` (from A-6): MODIFY the switch:

```c
	case MIDI_START:
		sync_midiStartStop(1);
		break;
	case MIDI_CONTINUE:
		/* S083 T1: resume, not restart. */
		sync_midiContinue();
		break;
```

**C-4b** ADD a side-effect-free source test for the foreground (SPP):

```c
/*
 * Is `source` the MIDI source `snc` selects? (S083 Q11)
 *
 * What: the foreground twin of midiTransport_sourceAllowed() without its
 * activity side effects: din/usb accept their port; aut accepts DIN, and
 * USB only while no DIN sync was seen within MIDI_AUTO_SYNC_SOURCE_HOLD_US;
 * off and pls accept nothing. Inputs: source port. Output: 0/1.
 * Accessor: midiTransport_handleSystemCommon() (SPP).
 */
static uint8_t midiTransport_sourceSelected(uint8_t source);
```

**C-4c** `midiTransport_handleSystemCommon()`: ADD the SPP case:

```c
	case MIDI_SONG_POSITION:
		/*
		 * Song Position Pointer (S083 T1, Q11).
		 *
		 * What: while stopped, from the `snc` source, stores the position
		 * (in 16th notes, 14 bits) as the resume tick: x 6 MIDI clocks x 4
		 * internal ticks = x 24. The next Continue starts there. Ignored
		 * while running (plan §3.8). Output: 1 when stored.
		 */
		{
			uint8_t source = (msg->bits.source == midiSourceUSB)
			                 ? MIDI_REALTIME_SOURCE_USB
			                 : MIDI_REALTIME_SOURCE_DIN;
			uint32_t pos = (uint32_t)msg->data1 |
			               ((uint32_t)msg->data2 << 7);

			if (seq_isRunning() || !midiTransport_sourceSelected(source))
				return 0u;
			seq_setResumeTick(pos * 24u);
			return 1u;
		}
```

### C-5 Phase C verification

1. DAW as master (DIN and USB in turn, `snc` din/usb/aut): Start plays from the Pattern start; Stop; Continue resumes where it stopped (track positions match a free-running reference on a second track length, e.g. 7 and 16 steps).
2. DAW locate while stopped (sends SPP), then Continue: playback starts at the located bar/beat; per-track lengths and scales land on the expected steps; FX sequence aligned.
3. SPP from the non-selected source, or with `snc = off`: ignored. Start/Stop with `snc = off`: ignored.
4. MRT forwarding of Start/Continue/Stop to a second device: exactly one copy arrives (C-11).
5. LXR as master (`snc = off`): PLAY sends Start; stop sends Stop; clock out unchanged.

---

## 7. Phase D — Program change, Bank change, the A1 fix

**Goal.** P1–P4: PC 0..15 on G changes the whole Scene like a PERF SEQ
press in any mode; PC on a voice channel moves that channel's tracks; CC0
on G requests the background Bank load; queued actions never touch an
unavailable Scene (Q2).

### D-1 `Core/Menu/menu.c` — A1 fix (Q2)

**Where:** `menu_serviceDeferredSceneChange()` 9261-9283, the `if
(isQueue) {` branch. **Action:** MODIFY.

```c
        if (isQueue) {
            /*
             * Skip a queued change to an unavailable Scene whole (S082 P3 A1,
             * S083 Q2).
             *
             * What: when the queued target is not available (not resident, or
             * still being written by a Bank load), neither the qac mute/Morph
             * parts nor the change run. Why: user rule (S083 Q2, binding):
             * "Scenes or tracks from Scenes are not automatable unless they
             * are changed via a voice edit mask from an active Scene or
             * track." Before this fix a queue armed before a direct Merge load
             * still cleared the mutes and wrote Morph 0 into the replaced
             * Scene although the change itself was dropped. Inputs: scene,
             * bank_sceneAvailable(). Affiliates: menu_applyQueuedSceneAction(),
             * menu_perfSceneChangeExecute() (which already drops the change).
             */
            if (scene < 16u && bank_sceneAvailable(scene)) {
                uint8_t flags = menu_applyQueuedSceneAction(scene);

                menu_perfSceneChangeExecute(
                    scene, (uint8_t)((flags & MENU_QAC_RESET) != 0u));
            }
        } else if (isChain && scene == scene_getActiveIndex()) {
```

### D-2 `Core/Menu/menu.c` — PERF LED guard (C-5)

**Where:** `menu_perfSceneChangeExecute()`, line `menu_refreshPerfSceneLeds();`
near its end (~9160). **Action:** MODIFY.

```c
    /*
     * PERF Scene LEDs only in PERF mode (S083 C-5).
     * What: the SEQ row shows PERF Scene state only while PERF is the mode;
     * elsewhere it belongs to that mode, which led_notifyPatternChanged()
     * (called by seq_selectActivePattern()) refreshes. Why: a MIDI PC (any
     * mode, plan P2) and chain/queued changes fired outside PERF overwrote
     * the STEP row. Affiliates: buttonHandler_getMode().
     */
    if (buttonHandler_getMode() == SELECT_MODE_PERF)
        menu_refreshPerfSceneLeds();
```

### D-3 `Core/Menu/menu.c/h` — Scene change with an origin

**Where:** `menu.c:9098` `menu_perfModeSceneButtonPressed()`.
**Action:** MODIFY into the origin-aware function plus the old name as the
panel wrapper:

```c
/*
 * Whole-Scene change with an origin (S080 F9; S083 P2, Q9).
 *
 * What: as a PERF SEQ press: deferred to the scd division while running,
 * else executed now with the pcr reset. The origin is handed to the
 * Sequencer so the program change this change would transmit is suppressed
 * for a MIDI-originated change (no echo, Q9), including when a deferred
 * change fires later. Inputs: Scene 0..15, origin (MIDI_ORIGIN_*).
 * Accessors: menu_perfModeSceneButtonPressed() (panel),
 * midiProgram_programChange() (MIDI PC on G). Affiliates:
 * seq_setSceneChangeOrigin(), seq_requestSceneChange(),
 * menu_perfSceneChangeExecute().
 */
void menu_perfModeSceneChangeFrom(uint8_t scene_index, uint8_t origin)
{
    uint8_t div = parameter_values[PAR_SCENE_CHANGE_DIV];

    seq_setSceneChangeOrigin(origin);
    if (div != 0u && seq_isRunning()) {
        seq_requestSceneChange(scene_index, div);
        return;
    }
    menu_perfSceneChangeExecute(scene_index,
                                (uint8_t)(parameter_values[PAR_BAR_RESET_MODE] != 0u));
}

void menu_perfModeSceneButtonPressed(uint8_t scene_index)
{
    /* [keep the existing comment block] */
    menu_perfModeSceneChangeFrom(scene_index, MIDI_ORIGIN_PANEL);
}
```

`menu.h:465`: ADD `void menu_perfModeSceneChangeFrom(uint8_t scene_index,
uint8_t origin);` with a one-line summary. `menu.c` needs `MidiDefs.h` for
`MIDI_ORIGIN_*` (already pulled in through `MidiIn.h`, A-18).

### D-4 `Core/Sequencer/sequencer.c/h` — PC origin flag (Q9, Q13)

**D-4a** ADD after `seq_pendingChainFire` (line 402):

```c
/*
 * Origin of the next whole-Scene change (S083 Q9; RAM approved Q13).
 *
 * What: 1 while the pending or executing whole-Scene change came from a
 * received MIDI PC; seq_sendProgChg() then sends nothing and clears it.
 * Why: MIDI-originated changes are never echoed (plan §3.9 P), also when a
 * deferred change fires at the next boundary. Writers:
 * seq_setSceneChangeOrigin() (menu_perfModeSceneChangeFrom(), panel or
 * MIDI), seq_chainEvaluate() (a chain fire is LXR-generated: cleared),
 * seq_sendProgChg() (consumed). RAM: 1 B SRAM1.
 */
static uint8_t seq_sceneChangeFromMidi = 0u;

void seq_setSceneChangeOrigin(uint8_t origin)
{
	seq_sceneChangeFromMidi = (uint8_t)(origin == MIDI_ORIGIN_MIDI);
}
```

**D-4b** `seq_sendProgChg()` (A-14f): ADD at the top:

```c
	/* S083 Q9: a MIDI-originated change is not echoed. */
	if (seq_sceneChangeFromMidi) {
		seq_sceneChangeFromMidi = 0u;
		return;
	}
```

**D-4c** `seq_chainEvaluate()` at `seq_pendingChainFire = 1u;` (line 5682):
ADD `seq_sceneChangeFromMidi = 0u;` with the comment "S083: a chain fire
is LXR-generated; its PC is sent."

**D-4d** `sequencer.h`: ADD `void seq_setSceneChangeOrigin(uint8_t origin);`
near `seq_requestSceneChange()`.

### D-5 `Core/Sequencer/sequencer.c/h` — shared per-track Scene assignment

**Where:** after `seq_getTrackPlayedScene()` (~1910). **Action:** ADD.

```c
/*
 * Move tracks to play another Scene (S077 P2 §4.1; S083 P3).
 *
 * What: for each track in trackMask: seq_setTrackPlayedScene(),
 * seq_realignTrackToMasterClock() (which also advances the Scene epoch, so
 * LiveRecord starts a new take, S081 §14.3 / R20), ends the track's MIDI
 * note (the new Scene may map it to another channel or note), and arms the
 * slot's instrument apply once per slot (tracks 6 and 7 share slot 5; the
 * HiHat pair switches together inside seq_setTrackPlayedScene()).
 * Why: one implementation for the panel gesture (hold VOICE + SEQ) and a
 * MIDI PC on a voice channel; the MIDI path must not skip the realign
 * (plan R20). Inputs: trackMask (bit per track), Scene 0..15, already
 * validated available by the caller. Context: foreground.
 * Accessors: buttonHandler_perfVoiceHeldSeqPressed(),
 * midiProgram_programChange(). Affiliates: preset_startSingleVoiceApply(),
 * midiOut_trackNoteOff().
 */
void seq_assignTracksToScene(uint8_t trackMask, uint8_t scene_index)
{
	uint8_t applied = 0u;
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t slot;

		if ((trackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_setTrackPlayedScene(track, scene_index);
		seq_realignTrackToMasterClock(track);
		midiOut_trackNoteOff(track);
		slot = (track >= INSTRUMENT_SLOT_COUNT)
		       ? (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u) : track;
		if ((applied & (uint8_t)(1u << slot)) == 0u) {
			applied |= (uint8_t)(1u << slot);
			preset_startSingleVoiceApply(slot, scene_index);
		}
	}
}
```

`sequencer.h`: ADD the declaration beside `seq_setTrackPlayedScene()`.

**`buttonHandler.c` 1440-1450** (`buttonHandler_perfVoiceHeldSeqPressed()`
loop): MODIFY → `seq_assignTracksToScene(perf_heldVoiceMask,
seqButtonPressed);` (the per-track loop and its locals go; the comment
above the function keeps its text with "via seq_assignTracksToScene()"
added). G-8 adds the PC transmit after it.

### D-6 `Core/MIDI/MidiProgram.h` / `MidiProgram.c` — NEW FILES

**`MidiProgram.h`:**

```c
#ifndef MIDIPROGRAM_H_
#define MIDIPROGRAM_H_

/*
 * MidiProgram — received program change and Bank change (S083, plan §3.7,
 * P1-P4, MIDI_SPEC §7).
 *
 * What: PC 0..15 on G changes the whole Scene exactly like a PERF SEQ
 * press, in any mode (scd division, pcr reset, releases Scene 17); PC on a
 * voice channel moves the tracks whose effective channel it is (no queue,
 * Scene 17 stays); CC0 on G requests the background Bank load of slot
 * 0..127. Only available Scenes are selected (S082 P3 D52). PC and CC0
 * arriving during a Bank load never reach here (S082 drop filter).
 * Why: P1-P4. RXF P governs both (F37; the caller checks it).
 * Context: foreground. RAM: none.
 * Affiliates: MidiIn (caller), menu_perfModeSceneChangeFrom(),
 * seq_assignTracksToScene(), preset_requestBackgroundBankLoad(),
 * bank_sceneAvailable(), MidiNoteMap.
 */

#include <stdint.h>

/* PC (channel 1..16, program 0..127). Output: 1 = acted (consumed). */
uint8_t midiProgram_programChange(uint8_t channel, uint8_t program);
/* CC0 (channel 1..16, value 0..127). Output: 1 = load accepted. */
uint8_t midiProgram_bankChange(uint8_t channel, uint8_t value);

#endif /* MIDIPROGRAM_H_ */
```

**`MidiProgram.c`:**

```c
#include "MidiProgram.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiNoteMap.h"
#include "menu.h"
#include "sequencer.h"
#include "BankData.h"
#include "SceneData.h"
#include "presetManager.h"

/* Contracts in MidiProgram.h. */
uint8_t midiProgram_programChange(uint8_t channel, uint8_t program)
{
	uint8_t global = midiIn_globalChannel();
	midiNoteMap_settings_t map;
	uint8_t tracks;

	if (program > 15u || !bank_sceneAvailable(program))
		return 0u;
	/* G wins when a channel is both G and a voice channel (§3.7). */
	if (global != 0u && channel == global) {
		menu_perfModeSceneChangeFrom(program, MIDI_ORIGIN_MIDI);
		return 1u;
	}
	midiNoteMap_load(scene_getActiveIndex(), &map);
	tracks = midiNoteMap_tracksOnChannel(&map, channel);
	if (tracks == 0u)
		return 0u;
	seq_assignTracksToScene(tracks, program);
	return 1u;
}

uint8_t midiProgram_bankChange(uint8_t channel, uint8_t value)
{
	uint8_t global = midiIn_globalChannel();

	if (global == 0u || channel != global || value > 127u)
		return 0u;
	/*
	 * S082 request API: MIDI origin is always Replace with all 16 Scenes
	 * (masks ignored, Q15); held as the one pending request until Preset,
	 * the facade and the Menu are idle (D33); it then switches the UI to
	 * Load/Save -> Bank (D7, which also cancels a waiting Learn, F-12).
	 */
	return preset_requestBackgroundBankLoad(value, 0u, 0u,
	                                        PRESET_BANK_ORIGIN_MIDI);
}
```

### D-7 `Core/MIDI/MidiIn.c` — PC and CC0 consumers

**Where:** `midiIn_dispatch()` switch. **Action:** MODIFY the `MIDI_CC`
case (B-5) and the `PROG_CHANGE` case; ADD `#include "MidiProgram.h"`.

```c
	case MIDI_CC:
		/*
		 * CC0 = Bank change under RXF P (F37); CC32 (Bank LSB) is reserved
		 * and not consumed. Other CCs under RXF C (CC2 here until Phase E).
		 */
		if (msg->data1 == CC_BANK_CHANGE) {
			if (midiIn_rxAllows(MIDI_FILTER_PROGRAM))
				consumed = midiProgram_bankChange(
				    (uint8_t)((msg->status & 0x0Fu) + 1u), msg->data2);
		} else if (/* B-5 CC2 condition, unchanged */) {
			consumed = midiNotes_setNoteAction(msg->data2);
		}
		break;
	case PROG_CHANGE:
		if (midiIn_rxAllows(MIDI_FILTER_PROGRAM))
			consumed = midiProgram_programChange(
			    (uint8_t)((msg->status & 0x0Fu) + 1u), msg->data1);
		break;
```

### D-8 `Makefile`

ADD `  Core/MIDI/MidiProgram.c \` to the MIDI list.

### D-9 Phase D verification

1. PC 0..15 on G, stopped and running, `scd` instant and 1 bar, `pcr` on/off; in PERF, STEP, VOICE and FX modes: the Scene changes like a SEQ press; the STEP row is not overwritten outside PERF (C-5); no PC is echoed on the LXR's output (Q9) while a panel SEQ press still sends one.
2. PC 16..127 and PC to a non-resident Scene: not consumed, forwarded per MRT.
3. PC on a voice channel (T1+T2 on ch 3): only those tracks move, realigned, a new live-record take starts (R20); T6/T7 move together.
4. Scene 17 active (background load running): PC on G is dropped by the filter during the load; after the load it releases 17; a voice-channel PC moves tracks without releasing 17.
5. CC0 on G: background load of that Bank, UI to Load/Save; CC0 bursts coalesce (latest wins); CC0 during a load is dropped; with RXF P off nothing happens and CC0/PC are forwarded.
6. A1: SHIFT+SEQ queue to Scene 5, then a direct Merge Bank load replacing Scene 5: when the queue fires, no mute clear and no Morph 0 (Q2).

---

## 8. Phase E — NRPN, CC1, and MIDI writes as menu edits

**Goal.** C1–C3, C7, C9 with Q1: NRPN addresses every 9-bit target; a MIDI
write has the same effect as a menu edit (shared commit; R12/R13/Scene
target clears; recording; fan-out); the visible cell repaints; CC1 works on
G and voice channels.

### E-1 `config.h`

**Where:** the MIDI block (A-1). **Action:** ADD.

```c
/*
 * NRPN receive contexts per port (S083, plan §3.5). One context holds the
 * selected NRPN number of one channel (3 B). Only G and voice channels use
 * NRPN, so at most 8 channels are meaningful. RAM: 2 x 8 x 3 = 48 B SRAM1
 * (approved). Affiliate: MidiControl.c.
 */
#define MIDI_NRPN_CONTEXTS         8u
```

### E-2 `Core/Menu/menu.c` — one commit path for panel and MIDI

**E-2a `menu_cellCommitValue()` → `menu_cellCommitValueEx()`.**
**Where:** 5061-5379 (whole function) and the prototype at 2122.
**Action:** MODIFY: rename to
`static uint8_t menu_cellCommitValueEx(const menu_cell_t *cell, uint16_t value, uint8_t panel)`
and replace every read of the panel's view state with a local:

```c
    /*
     * Panel or external edit (S083 Q1).
     *
     * What: `panel` = 1 for an encoder/pot edit: the live view flags decide
     * the image (VOICE SHIFT Morph view, Effect page Morph view, STEP Morph
     * view) and Global-page edits mark settings.cfg. `panel` = 0 for a MIDI
     * write: always the Normal image and no page rules, because MIDI never
     * edits a Morph endpoint (S083 §3.5). Everything else - fan-out through
     * bank_editTargetMask(), seq_menuOverrideSet() (R12), the Scene-target
     * override clears, menuHold for Effects (R13) - is identical, which is
     * what makes a MIDI write a menu edit. Callers: menu_cellCommitValue()
     * (panel), menu_commitTargetEdit() (MIDI).
     */
    const uint8_t morphView = (uint8_t)(panel && voiceModeShowMorph);
```

| Line | Old | New |
|------|-----|-----|
| 5064-5065 | `return menuEffects_cellCommit(&cell->fx, value);` | `if (panel) return menuEffects_cellCommit(&cell->fx, value);` then, for `cell->fx.kind == MENU_FX_CELL_PARAM`: `return effects_setParameter(scene_getActiveIndex(), cell->fx.index, EFFECT_IMAGE_NORMAL, (uint8_t)value);`, for `MENU_FX_CELL_MORPH_AMOUNT`: `return menu_commitEffectMorphParam((uint8_t)value);`, else `return 0u;` |
| 5091, 5094 | `voiceModeShowMorph` | `morphView` |
| 5153, 5156 | `voiceModeShowMorph` | `morphView` |
| 5188 | `voiceModeShowMorph` | `morphView` |
| 5303-5304 | `if (cell->kind == MENU_CELL_STATIC && menu_patternTrackMorphEndpointActive(cell->static_param)) {` | `if (panel && cell->kind == MENU_CELL_STATIC && menu_patternTrackMorphEndpointActive(cell->static_param)) {` |
| 5356-5361 | the settings-dirty test `if (old_value != *paramValue && …` | add `panel &&` as the first condition |

ADD the wrapper right after the function:

```c
/* Panel edit commit (encoder, pots): live view state. Contract above. */
static uint8_t menu_cellCommitValue(const menu_cell_t *cell, uint16_t value)
{
    return menu_cellCommitValueEx(cell, value, 1u);
}
```

and change the prototype at 2122 to `menu_cellCommitValueEx()` plus the
wrapper prototype.

**E-2b `menu_clearTrackTimingOverride()` takes the track.** **Where:**
4017-4028. **Action:** MODIFY into
`static void menu_clearTrackTimingOverrideFor(uint8_t track, uint8_t cell_position, presetMorph_trackParam_t param)`
(body uses the `track` argument instead of `menu_getActiveVoice()`), and
ADD the old name as a wrapper passing `menu_getActiveVoice()`; its seven
callers stay unchanged. Comment: "S083: MIDI writes a track timing target
of any track (plan C2), not only the active one."

**E-2c New: the cell's MIDI target.** ADD after `menu_cellClearTarget()`
(15444):

```c
/*
 * MIDI target ID of a menu cell (S083, Learn and parameter TX).
 *
 * What: like menu_cellClearTarget(), but every addressable parameter has an
 * ID, automatable or not: any Instrument descriptor (slot x 64 + index),
 * the Scene targets (Nou, Nfx, Nvm, 7dc, fxm, STEP len/scl/shf of the
 * active track), and Effect rows 0..62 and the Effect Morph. Cells with no
 * MIDI address (BPM, settings, step data, fader mode, `typ`) return 0.
 * Why: NRPN and Learn address the whole canonical ID space (plan C1);
 * recording still uses the automatable-only resolver. Inputs: cell.
 * Output: 1 with *id. Accessors: menu_cellMidiTargetAt(), the Learn
 * capture hook (F-9/F-10), the LRN display (F-11), the parameter TX hook
 * (G-7), menu_targetVisible().
 */
static uint8_t menu_cellMidiTarget(const menu_cell_t *cell, uint16_t *id)
{
    uint16_t target = INSTRUMENT_PARAM_INVALID;

    if (!cell || !id)
        return 0u;
    switch (cell->kind) {
    case MENU_CELL_INSTRUMENT:
        if (cell->descriptor)
            target = instrumentParam_make(cell->slot, cell->descriptor_index);
        break;
    case MENU_CELL_SCENE_SETTING:
        /* Nou, Nfx, Nvm; the fader mode has no target. */
        target = (uint16_t)menu_sceneSettingAutomationTarget(cell);
        break;
    case MENU_CELL_KIT_SETTING:
        if (cell->kit_setting == MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY)
            target = sceneModTarget_slot6DecayId();
        break;
    case MENU_CELL_STATIC:
        if (cell->static_param >= PAR_VOICE1_MORPH &&
            cell->static_param <= PAR_VOICE6_MORPH)
            target = sceneModTarget_voiceMorphId(
                (uint8_t)(cell->static_param - PAR_VOICE1_MORPH));
        else if (cell->static_param == PAR_EFFECT_MORPH)
            target = sceneModTarget_effectMorphId();
        else if (menu_activePage == SEQ_PAGE &&
                 (cell->static_param == PAR_TRACK_LENGTH ||
                  cell->static_param == PAR_TRACK_SCALE ||
                  cell->static_param == PAR_SHUFFLE))
            /* STEP len/scl/shf of the active track (cell positions 0/1/2). */
            target = (uint16_t)menu_seqCellToTrackTarget(
                (cell->static_param == PAR_TRACK_LENGTH) ? 0u :
                (cell->static_param == PAR_TRACK_SCALE) ? 1u : 2u,
                menu_activeVoice);
        break;
    case MENU_CELL_EFFECT:
        if (cell->fx.kind == MENU_FX_CELL_MORPH_AMOUNT)
            target = sceneModTarget_effectMorphId();
        else if (cell->fx.kind == MENU_FX_CELL_PARAM &&
                 cell->fx.index < EFFECT_TARGET_PATTERN_LOCAL_LIMIT &&
                 effects_descriptor(effects_activeType(), cell->fx.index))
            target = effectTarget_id(cell->fx.index);
        break;
    default:
        break;
    }
    if (target == INSTRUMENT_PARAM_INVALID || target >= MIDI_TARGET_OFF)
        return 0u;
    *id = target;
    return 1u;
}

/* Position form (active page, cell position) of menu_cellMidiTarget(). */
static uint8_t menu_cellMidiTargetAt(uint8_t active_page, uint8_t position,
                                     uint16_t *id)
{
    menu_cell_t cell = menu_resolveCell(active_page, position);

    return menu_cellMidiTarget(&cell, id);
}
```

`menu_cellClearTarget()` stays as it is: recording and pot clears keep
the automatable-only rule. `menu_cellMidiTarget()` covers the same Scene
and Morph cells from the cell itself (no position needed), so the LRN
formatter can call it with only the cell. Confirm at implementation that
`menu_seqCellToTrackTarget()` maps positions 0/1/2 to len/scl/shf (its
comment in `menu_cellClearTarget()` says so).

**E-2d New: visibility by target.** ADD after `menu_paramVisible()` (15192):

```c
/*
 * Is target `id` shown on the current page? (S083 Q1 display rule)
 *
 * What: the target-ID twin of menu_paramVisible(): true when one of the
 * visible cells (the edited cell in edit mode) resolves to `id`.
 * Why: "if the parameter is being shown in the menu, the parameter should
 * update" (Q1); MIDI writes Instrument, Scene and Effect targets that have
 * no flat ParameterArray id. Inputs: id. Output: 0/1. Accessor:
 * menu_notifyTargetChanged().
 */
static uint8_t menu_targetVisible(uint16_t id)
{
    const uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    uint16_t cellId;
    uint8_t i;

    if (menu_activePage >= LOAD_PAGE && menu_activePage <= SAVE_PAGE)
        return 0u;
    if (editModeActive)
        return (uint8_t)(menu_cellMidiTargetAt(activePage, activeParameter,
                                               &cellId) && cellId == id);
    {
        const uint8_t is2ndPage = menu_isScreenPage(menu_activePage)
            ? 0u : (uint8_t)((activeParameter > 3) ? 4 : 0);

        for (i = 0u; i < 4u; i++) {
            if (menu_cellMidiTargetAt(activePage, (uint8_t)(i + is2ndPage),
                                      &cellId) && cellId == id)
                return 1u;
        }
    }
    return 0u;
}
```

**E-2e `menu_notifyExternalParamChanged()` → target form.** **Where:**
15195-15203 and `menu.h:728`. **Action:** MODIFY:

```c
/*
 * Repaint a target changed from outside the panel (S083 Q1; closes the
 * S075 open item "MIDI-entered values reach the LCD only at the next
 * repaint").
 *
 * What: sets the coalesced knob-repaint flag when the target is visible;
 * the main loop's LCD rate limit coalesces fast CC streams. Inputs: target
 * ID. Accessors: menu_commitTargetEdit(), menu_commitGlobalMorphFromMidi().
 * Affiliates: menu_targetVisible(), menu_serviceKnobRepaint().
 */
void menu_notifyTargetChanged(uint16_t id)
{
    if (menu_targetVisible(id))
        menu_knobs_dirty = 1;
}
```

(`menu_notifyExternalParamChanged()` had only MidiParser callers; it is
removed. Its static helper `menu_paramVisible()` stays for
`menu_commitGlobalMorphFromMidi()`.)

**E-2f New: the shared commit entry.** ADD after `menu_cellCommitValue()`:

```c
/*
 * Track timing target from MIDI (S083 C2; plan §3.5 Scene targets).
 *
 * What: writes length/scale/shuffle of track `d->voice_slot` in the active
 * Scene (Pattern data: never fanned out, edit-mask rule), clears that
 * track's step override and Scene automation bit like a STEP menu edit
 * (S081 §5.2), and refreshes the STEP mirrors when that track and Scene
 * are on screen. Inputs: descriptor, 7-bit value. Output: 1.
 */
static uint8_t menu_commitTrackTimingTarget(
    const scene_mod_target_descriptor_t *d, uint8_t value)
{
    const uint8_t scene = scene_getActiveIndex();
    const uint8_t track = d->voice_slot;

    if (track >= NUM_TRACKS)
        return 0u;
    if (value < d->min_value) value = (uint8_t)d->min_value;
    if (value > d->max_value) value = (uint8_t)d->max_value;
    switch (d->kind) {
    case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        pat_setTrackLength(scene, track, value);
        menu_clearTrackTimingOverrideFor(track, 0u, PRESETMORPH_TRACK_LENGTH);
        break;
    case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        pat_setTrackScale(scene, track, value);
        menu_clearTrackTimingOverrideFor(track, 1u, PRESETMORPH_TRACK_SCALE);
        break;
    default:
        pat_setTrackShuffle(scene, track, value);
        menu_clearTrackTimingOverrideFor(track, 2u, PRESETMORPH_TRACK_SHUFFLE);
        break;
    }
    if (scene == menu_getViewedPattern() && track == menu_getActiveVoice())
        pat_applyTrackSettingsToMenu(scene, track);
    return 1u;
}

/* 7-bit -> 8-bit Morph amount (the automation translation, plan C3). */
static uint8_t menu_morphFrom7(uint8_t v)
{
    return (v < 127u) ? (uint8_t)(v * 2u) : 255u;
}

/*
 * Commit one MIDI parameter write as a menu edit (S083 Q1, C3, C9).
 *
 * What: resolves a 9-bit target in the ACTIVE Scene (Scene 17 while
 * active) to the same commit a menu cell performs, with the panel's view
 * state switched off (Normal image), then records it and repaints it:
 *  - Instrument 0..383: an Instrument cell for the active slot type; the
 *    value is clamped to the descriptor's menu domain (dtype, lists,
 *    target tokens); seq_menuOverrideSet() + fan-out (R12);
 *  - Scene 384..447: Nvm/fxm through their static cells with the 7-bit
 *    Morph translation; Nou/Nfx through the Scene-setting cell (override
 *    clears); 7dc through the Kit-setting cell; track len/scl/shf through
 *    menu_commitTrackTimingTarget(); retired/unused IDs refused;
 *  - Effect 448..510: the Effect row of the active type, WIDE8 rows
 *    through the type's expand7 (the automation rule), Normal image,
 *    EffectsManager fan-out and menuHold (R13).
 * Then, in LATCH/OVERDUB, liveRec_paramIn(id, MIDI) for automatable
 * targets except track length (as menu_liveRecRecordCell() minus its
 * panel-view exclusions, which cannot apply to MIDI), and
 * menu_notifyTargetChanged(id).
 * Why: Q1: MIDI has the same effect as a menu edit, same priority, same
 * actions against automation and recording; the display follows.
 * Inputs: id, value 0..127, origin (MIDI_ORIGIN_MIDI; reserved for later
 * external sources). Output: 1 when the ID is a valid target in the active
 * Scene (the caller counts the message as consumed), else 0.
 * Context: foreground. Accessors: midiControl_writeTarget().
 * Affiliates: menu_cellCommitValueEx(), menu_clampCellValue(),
 * liveRec_paramIn(), effects_setParameter(), sceneModTarget_descriptor().
 */
uint8_t menu_commitTargetEdit(uint16_t id, uint8_t value, uint8_t origin)
{
    menu_cell_t cell;
    uint16_t v = value;
    uint8_t recordable = 0u;

    (void)origin;
    if (value > 127u)
        value = 127u;
    memset(&cell, 0, sizeof(cell));
    if (instrumentParam_isVoiceParameter(id)) {
        const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
            scene_getActiveIndex(), instrumentParam_slot(id));

        if (!instr)
            return 0u;
        cell.kind = MENU_CELL_INSTRUMENT;
        cell.slot = instrumentParam_slot(id);
        cell.descriptor_index = instrumentParam_local(id);
        cell.descriptor = instrumentManager_descriptor(instr->type,
                                                       cell.descriptor_index);
        if (!cell.descriptor)
            return 0u;
        menu_clampCellValue(&cell, &v);
        (void)menu_cellCommitValueEx(&cell, v, 0u);
        recordable = (uint8_t)((cell.descriptor->flags &
                                INSTRUMENT_PARAM_FLAG_AUTOMATABLE) != 0u);
    } else if (sceneModTarget_isSceneTarget(id)) {
        const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

        if (!d)
            return 0u;
        if (value < d->min_value) value = (uint8_t)d->min_value;
        if (value > d->max_value) value = (uint8_t)d->max_value;
        switch (d->kind) {
        case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
            cell.kind = MENU_CELL_STATIC;
            cell.static_param = (uint16_t)(PAR_VOICE1_MORPH + d->voice_slot);
            v = menu_morphFrom7(value);
            break;
        case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
            cell.kind = MENU_CELL_STATIC;
            cell.static_param = PAR_EFFECT_MORPH;
            v = menu_morphFrom7(value);
            break;
        case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
            cell.kind = MENU_CELL_SCENE_SETTING;
            cell.scene_setting = MENU_SCENE_SETTING_AUDIO_OUT;
            cell.slot = d->voice_slot;
            v = value;
            break;
        case SCENE_MOD_TARGET_KIND_FX_SEND:
            cell.kind = MENU_CELL_SCENE_SETTING;
            cell.scene_setting = MENU_SCENE_SETTING_FX_SEND_AMOUNT;
            cell.slot = d->voice_slot;
            v = value;
            break;
        case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            cell.kind = MENU_CELL_KIT_SETTING;
            cell.kit_setting = MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY;
            v = value;
            break;
        case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE:
            (void)menu_commitTrackTimingTarget(d, value);
            break;
        default:
            return 0u;	/* retired or unused */
        }
        if (cell.kind != MENU_CELL_EMPTY)
            (void)menu_cellCommitValueEx(&cell, v, 0u);
        recordable = (uint8_t)((d->use_flags &
                                SCENE_MOD_TARGET_USE_AUTOMATION) != 0u &&
                               d->kind != SCENE_MOD_TARGET_KIND_TRACK_LENGTH);
    } else if (effectTarget_isEffectId(id)) {
        uint8_t local = effectTarget_local(id);
        effect_type_id_t type = effects_activeType();
        const effect_param_descriptor_t *d = effects_descriptor(type, local);

        if (!d || local >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
            return 0u;
        v = ((d->effect_flags & EFFECT_PARAM_FLAG_WIDE8) && d->expand7)
            ? d->expand7(value) : value;
        (void)effects_setParameter(scene_getActiveIndex(), local,
                                   EFFECT_IMAGE_NORMAL, (uint8_t)v);
        recordable = effects_paramAutomatable(type, local);
    } else {
        return 0u;
    }
    if (recordable)
        liveRec_paramIn(id, LIVEREC_SRC_MIDI);
    menu_notifyTargetChanged(id);
    return 1u;
}

/*
 * Global Morph from MIDI CC1 on G (S083 C7, Q1; G-1 binding).
 *
 * What: commits PAR_MORPH through its static cell exactly like the PERF
 * `mrp` edit (edit-mask fan-out through preset_morphScene()), never records
 * (global Morph is never recorded, G-1), and repaints `mrp` and the six
 * per-voice Morph cells when visible. Inputs: 7-bit value (translated
 * 0..126 -> x2, 127 -> 255). Accessor: midiControl_handleCc().
 */
void menu_commitGlobalMorphFromMidi(uint8_t value)
{
    menu_cell_t cell;
    uint8_t slot;

    memset(&cell, 0, sizeof(cell));
    cell.kind = MENU_CELL_STATIC;
    cell.static_param = PAR_MORPH;
    (void)menu_cellCommitValueEx(&cell, menu_morphFrom7(value), 0u);
    if (menu_paramVisible(PAR_MORPH))
        menu_knobs_dirty = 1;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        menu_notifyTargetChanged(sceneModTarget_voiceMorphId(slot));
}
```

(`MENU_CELL_EMPTY` is the zero value of `menu_cell_kind_t`, `menu.c:2040`,
so a memset cell is empty until a branch sets its kind.)

**E-2g `menu.h`.** **Where:** line 728 (`menu_notifyExternalParamChanged`).
**Action:** MODIFY → declare `menu_notifyTargetChanged()`,
`menu_commitTargetEdit()` and `menu_commitGlobalMorphFromMidi()`, each
with its contract block (copy from E-2e/E-2f).

**E-2h `menu_clampInstrumentValue()`** (`menu.c:6276-6316`,
`menu.h:565-580`): its only caller was MidiParser. **Action:** REMOVE both
(the shared commit clamps through `menu_clampCellValue()` directly).

### E-3 `Core/Bank/Scene/Preset/presetManager.c/h` — remove the MIDI endpoint setter

| Where | Action |
|-------|--------|
| `presetManager.c:1177-1228` (comment block + `preset_setInstrumentParameterFromMidi()`) | REMOVE |
| `presetManager.h:472-498` (comment block + declaration) | REMOVE |

Why (for the commit message and §12): Q1 retires "MIDI lowest priority";
MIDI uses the menu commit (`preset_setInstrumentParameter()` with
`seq_menuOverrideSet()`), so the store-only path has no caller.

### E-4 `Core/MIDI/MidiControl.h` / `MidiControl.c` — NEW FILES

**`MidiControl.h`:**

```c
#ifndef MIDICONTROL_H_
#define MIDICONTROL_H_

/*
 * MidiControl — received control changes (S083, plan §3.5, C1-C3, C7, C8).
 *
 * What: for one CC under RXF C: CC2 (note action, G only) -> MidiNotes;
 * CC1 (Morph: global on G, never recorded; per voice on a voice channel,
 * recorded as an individual voice Morph edit); the NRPN protocol (CC 99/98
 * number, CC6 value, CC38 ignored, CC96/97 +/-1, RPN selects/deselects);
 * then MIDI Learn (capture, then lookup, Phase F). Every parameter write
 * goes through menu_commitTargetEdit() (a MIDI write is a menu edit, Q1).
 * Channel rule (C2): every ID works on G; on a voice channel only that
 * channel's voices' IDs (voice block and per-voice Scene targets: Nvm, Nou,
 * Nfx, 7dc, and len/scl/shf of its tracks). Consumption (C-7): CC98/99/
 * 100/101/38 are consumed on G or a voice channel; CC6/96/97 only when the
 * selected number is a valid target there.
 * Why: plan §3.5; replaces the legacy LXR CC/CC2 map (retired, Phase A).
 * Context: foreground. RAM: NRPN contexts 48 B SRAM1 (approved).
 * Affiliates: MidiIn (caller), MidiNoteMap (channels), MidiNotes (CC2),
 * MidiLearn (F), menu.c (commit), SceneModTargets, EffectsManager.
 */

#include <stdint.h>
#include "MidiDefs.h"

/* One CC (not CC0/CC32, which MidiProgram owns). Output: 1 = consumed. */
uint8_t  midiControl_handleCc(const MidiMsg *msg);
/*
 * 1 when target `id` may be written from `channel` in the active Scene:
 * on G every ID; on a voice channel the IDs of its voices (C2).
 * Accessors: midiControl_handleCc(), MidiLearn (kind v), MidiOut (G).
 */
uint8_t  midiControl_targetOnChannel(uint16_t id, uint8_t channel);
/* Write one target as a menu edit; 1 = valid target. (MidiLearn, NRPN.) */
uint8_t  midiControl_writeTarget(uint16_t id, uint8_t value7);
/*
 * Current 7-bit value of a target in Scene `scene_index` (Normal image;
 * Morph amounts translated back 255 -> 127, else /2; WIDE8 Effect rows
 * through effects_storeValue7()). Accessors: CC96/97 (+/-1), MidiOut
 * parameter TX (G). Mirrors liveRec_targetValue7() plus track length.
 */
uint8_t  midiControl_targetValue7(uint8_t scene_index, uint16_t id);
/* Voice slots (bit per slot 0..5) whose tracks use `channel` and it is
** not G. 0 = not a voice channel. */
uint8_t  midiControl_voiceSlotsOnChannel(uint8_t channel);

#endif /* MIDICONTROL_H_ */
```

**`MidiControl.c`:**

```c
#include "MidiControl.h"
#include "MidiIn.h"
#include "MidiNotes.h"
#include "MidiNoteMap.h"
#include "menu.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "PatternData.h"
#include "config.h"

/*
 * NRPN receive context (plan §3.5, F26).
 *
 * key: bit 7 in use, bit 6 MSB seen, bit 5 LSB seen, bit 4 RPN selected
 * (data entry is then ignored), bits 0..3 channel - 1. msb/lsb: the
 * selected number (CC99/CC98). One table per port (index = MidiSource).
 * A context is allocated by CC98..101 on G or a voice channel: the first
 * free one, else the slot (channel - 1) & 7 is reused. RAM: 48 B SRAM1.
 */
typedef struct {
	uint8_t key;
	uint8_t msb;
	uint8_t lsb;
} midiControl_nrpn_t;

#define MIDI_NRPN_USED     0x80u
#define MIDI_NRPN_MSB      0x40u
#define MIDI_NRPN_LSB      0x20u
#define MIDI_NRPN_RPN      0x10u
#define MIDI_NRPN_CHANNEL  0x0Fu

static midiControl_nrpn_t midiControl_nrpn[2][MIDI_NRPN_CONTEXTS];

static midiControl_nrpn_t *midiControl_context(uint8_t port, uint8_t channel,
                                               uint8_t allocate)
{
	midiControl_nrpn_t *table = midiControl_nrpn[port & 1u];
	uint8_t i;

	for (i = 0u; i < MIDI_NRPN_CONTEXTS; i++) {
		if ((table[i].key & MIDI_NRPN_USED) &&
		    (table[i].key & MIDI_NRPN_CHANNEL) == (uint8_t)(channel - 1u))
			return &table[i];
	}
	if (!allocate)
		return 0;
	for (i = 0u; i < MIDI_NRPN_CONTEXTS; i++) {
		if ((table[i].key & MIDI_NRPN_USED) == 0u)
			break;
	}
	if (i >= MIDI_NRPN_CONTEXTS)
		i = (uint8_t)((channel - 1u) % MIDI_NRPN_CONTEXTS);
	table[i].key = (uint8_t)(MIDI_NRPN_USED | ((channel - 1u) & 0x0Fu));
	table[i].msb = 0u;
	table[i].lsb = 0u;
	return &table[i];
}

/* Contracts in MidiControl.h. */
uint8_t midiControl_voiceSlotsOnChannel(uint8_t channel)
{
	midiNoteMap_settings_t map;
	uint8_t tracks;
	uint8_t slots = 0u;
	uint8_t t;

	if (channel == 0u || channel == midiIn_globalChannel())
		return 0u;
	midiNoteMap_load(scene_getActiveIndex(), &map);
	tracks = midiNoteMap_tracksOnChannel(&map, channel);
	for (t = 0u; t < 7u; t++) {
		if (tracks & (1u << t))
			slots |= (uint8_t)(1u << ((t >= 6u) ? 5u : t));
	}
	return slots;
}

uint8_t midiControl_targetOnChannel(uint16_t id, uint8_t channel)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t slots;

	if (global != 0u && channel == global)
		return 1u;
	slots = midiControl_voiceSlotsOnChannel(channel);
	if (slots == 0u)
		return 0u;
	if (instrumentParam_isVoiceParameter(id))
		return (uint8_t)((slots >> instrumentParam_slot(id)) & 1u);
	if (sceneModTarget_isSceneTarget(id)) {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

		if (!d)
			return 0u;
		switch (d->kind) {
		case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
		case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
		case SCENE_MOD_TARGET_KIND_FX_SEND:
			return (uint8_t)((slots >> d->voice_slot) & 1u);
		case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
			return (uint8_t)((slots >> 5u) & 1u);
		case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
		case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
		case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE: {
			midiNoteMap_settings_t map;

			midiNoteMap_load(scene_getActiveIndex(), &map);
			return (uint8_t)(midiNoteMap_effectiveChannel(&map,
			                 d->voice_slot) == channel);
		}
		default:
			return 0u;	/* fxm and unused: G only */
		}
	}
	return 0u;	/* Effect rows: G only */
}

uint8_t midiControl_writeTarget(uint16_t id, uint8_t value7)
{
	return menu_commitTargetEdit(id, value7, MIDI_ORIGIN_MIDI);
}

uint8_t midiControl_targetValue7(uint8_t scene_index, uint16_t id)
{
	/*
	 * [Port of LiveRecord.c liveRec_targetValue7() 577-640 with
	 *  scene_index for `a`, plus:
	 *  - SCENE_MOD_TARGET_KIND_TRACK_LENGTH: region->track_length[slot]
	 *    (clamped to 127);
	 *  - Effect IDs: effects_storeValue7(scene_index, local,
	 *    effects_getParameter(scene_index, local, EFFECT_IMAGE_NORMAL)).]
	 */
}

uint8_t midiControl_handleCc(const MidiMsg *msg)
{
	const uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);
	const uint8_t cc = msg->data1;
	const uint8_t value = msg->data2;
	const uint8_t global = midiIn_globalChannel();
	const uint8_t onG = (uint8_t)(global != 0u && channel == global);
	const uint8_t voiceSlots = midiControl_voiceSlotsOnChannel(channel);
	const uint8_t port = (uint8_t)msg->bits.source;
	midiControl_nrpn_t *ctx;

	switch (cc) {
	case CC_NOTE_ACTION:
		return onG ? midiNotes_setNoteAction(value) : 0u;
	case CC_MOD_WHEEL:
		/* C7: G = global Morph (never recorded, G-1); voice channel =
		** that channel's voices' Morph, recorded as individual edits. */
		if (onG) {
			menu_commitGlobalMorphFromMidi(value);
			return 1u;
		}
		if (voiceSlots != 0u) {
			uint8_t slot;

			for (slot = 0u; slot < 6u; slot++) {
				if (voiceSlots & (1u << slot))
					(void)midiControl_writeTarget(
					    sceneModTarget_voiceMorphId(slot), value);
			}
			return 1u;
		}
		return 0u;
	case CC_NRPN_MSB:
	case CC_NRPN_LSB:
	case CC_RPN_MSB:
	case CC_RPN_LSB:
	case CC_DATA_ENTRY_LSB:
		if (!onG && voiceSlots == 0u)
			return 0u;
		if (cc == CC_DATA_ENTRY_LSB)
			return 1u;	/* F26: ignored, consumed */
		ctx = midiControl_context(port, channel, 1u);
		if (cc == CC_NRPN_MSB) {
			ctx->msb = value;
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_MSB) & ~MIDI_NRPN_RPN);
		} else if (cc == CC_NRPN_LSB) {
			ctx->lsb = value;
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_LSB) & ~MIDI_NRPN_RPN);
		} else {
			/* Any RPN selection, incl. the RPN null (127/127), deselects
			** the NRPN: data entry is ignored until a new NRPN number. */
			ctx->key = (uint8_t)((ctx->key | MIDI_NRPN_RPN) &
			                     ~(MIDI_NRPN_MSB | MIDI_NRPN_LSB));
		}
		return 1u;
	case CC_DATA_ENTRY_MSB:
	case CC_DATA_INCREMENT:
	case CC_DATA_DECREMENT: {
		uint16_t id;
		uint8_t v = value;

		ctx = midiControl_context(port, channel, 0u);
		if (!ctx || (ctx->key & MIDI_NRPN_RPN) ||
		    (ctx->key & (MIDI_NRPN_MSB | MIDI_NRPN_LSB)) !=
		        (MIDI_NRPN_MSB | MIDI_NRPN_LSB))
			return 0u;
		id = (uint16_t)(((uint16_t)ctx->msb << 7) | ctx->lsb);
		if (id >= MIDI_TARGET_OFF || !midiControl_targetOnChannel(id, channel))
			return 0u;	/* C1/C-7: unused or not here -> forwarded */
		if (cc != CC_DATA_ENTRY_MSB) {
			v = midiControl_targetValue7(scene_getActiveIndex(), id);
			if (cc == CC_DATA_INCREMENT && v < 127u) v++;
			if (cc == CC_DATA_DECREMENT && v > 0u) v--;
		}
		return midiControl_writeTarget(id, v);
	}
	default:
		break;
	}
	/* Phase F: Learn capture, then Learn lookup (F-5). */
	return 0u;
}
```

Note: `ctx` returned by `midiControl_context(…, 1u)` is never null (it
reuses a slot when the table is full).

### E-5 `Core/MIDI/MidiIn.c` — CC branch

**Where:** `midiIn_dispatch()`, the `MIDI_CC` case (D-7). **Action:**
MODIFY the non-CC0 branch:

```c
		} else if (msg->data1 != CC_BANK_SELECT_LSB &&
		           midiIn_rxAllows(MIDI_FILTER_CONTROL)) {
			/* E: CC1, CC2, NRPN, Learn (plan §3.5/§3.6). */
			consumed = midiControl_handleCc(msg);
		}
```

ADD `#include "MidiControl.h"`.

### E-6 `Makefile`

ADD `  Core/MIDI/MidiControl.c \`.

### E-7 Phase E verification

1. NRPN sweep (a DAW script): every ID 0..510 on G for each instrument type in each slot and each Effect type: the right parameter moves, values clamp at the descriptor maximum, Morph IDs translate (127 → 255), unused IDs are forwarded; the visible cell follows within the LCD refresh.
2. Q1 against automation: a parameter with step automation; while playing, a MIDI CC/NRPN move sounds at once and holds until the voice's next trigger, as a knob move does (R12). An Effect parameter with an FX lock: MIDI beats the lock until the next automation (R13). A Scene target (Nou/Nfx) with automation: the MIDI value applies until its next automation.
3. Voice channel: T3 on ch 5: NRPN for slot 3's IDs works on ch 5; slot 1's IDs and Effect IDs are forwarded; `3vm`, `3ou`, `3fx` and track 3 len/scl/shf work.
4. Fan-out: VOICE edit mask covering Scenes 1-3: a MIDI write reaches all three; during an unlocked Bank load it skips Scenes still loading; on Scene 17 only 17 (S082 §13.2 item 3).
5. Recording (LATCH and OVERDUB, `rtk`): NRPN moves are recorded like knob moves, first-change rule; track length is not recorded. Measure the `liveRec_paramIn()` cost with a 1 kHz controller sweep (S081 note).
6. CC1: on G moves `mrp` (fan-out) and is never recorded (S081 G-1 check 4); on a voice channel moves that voice's Morph and is recorded as an individual Morph edit.
7. CC96/97 step the selected NRPN; RPN null stops data entry.

---

## 9. Phase F — MIDI Learn

**Goal.** C4–C6, F27–F33, F49–F51, Q10: a device-wide table of up to 128
type-tagged entries in `settings.cfg`; the SHIFT+PLAY+REC gesture;
capture, replacement, full table, cancel; type matching on the active
Scene; learned CCs written as menu edits.

### F-1 `config.h`

ADD to the MIDI block:

```c
/*
 * MIDI Learn (S083 C4, F36). MIDI_LEARN_MAX_ENTRIES: table rows (4 B each,
 * 512 B SRAM1 at 128, approved). Raising it is a new RAM allocation and
 * also grows settings.cfg (24 + entries lines, uint8_t line count: at most
 * 231). MIDI_LEARN_FULL_SHOW_MS: how long `FUL` shows when a learn is
 * refused because the table is full. Affiliates: MidiLearn.c, filesystem.c.
 */
#define MIDI_LEARN_MAX_ENTRIES     128u
#define MIDI_LEARN_FULL_SHOW_MS    1000u
```

### F-2 `Core/MIDI/MidiLearn.h` — NEW FILE

```c
#ifndef MIDILEARN_H_
#define MIDILEARN_H_

/*
 * MidiLearn — learned CCs and the Learn gesture (S083, plan §3.6, C4-C6,
 * MIDI_SPEC §6).
 *
 * What: the device-wide table (up to MIDI_LEARN_MAX_ENTRIES rows of 4 B:
 * CC + channel kind, target space, type, index); the capture state of the
 * SHIFT+PLAY+REC gesture; the lookup that routes a received CC to its
 * targets (all matching rows, macros); the TX lookup; and the settings.cfg
 * `learn=` line format. It is the module that coordinates CCs into the
 * other modules: matches are written through MidiControl, which commits
 * them as menu edits (Q1).
 * Rules: one CC -> many parameters (one row each); one parameter -> at most
 * one CC (rows are unique on target space + type + index; re-learning
 * replaces CC and channel kind, F49); a row applies only while its type is
 * loaded in the active Scene's slot / Effect (Scene 17 while active, F51);
 * kind `g` = on G, kind `v` = on the target voice's own channel (voice and
 * per-voice Scene targets only); a `v` row affects only its own slot even
 * when several voices share the channel (F50); reserved CCs never learn.
 * Why: plan F6/F27; no fan-out, no Scene storage, no UI remove (delete the
 * line from settings.cfg).
 * Context: foreground. RAM: 518 B SRAM1 static (approved ~519).
 * Affiliates: MidiControl (capture/lookup caller, writes), menu.c (target
 * selection, LRN/FUL display, page-change cancel), buttonHandler (gesture,
 * LEDs), filesystem.c (lines), MidiOut (TX CC).
 */

#include <stdint.h>

/* Empty the table (settings load start, filesystem defaults). */
void     midiLearn_clearAll(void);
uint8_t  midiLearn_count(void);

/* Gesture (buttonHandler, menu). */
void     midiLearn_arm(void);		/* SHIFT+PLAY+REC */
void     midiLearn_cancel(void);	/* SHIFT/PLAY/REC press, page change */
uint8_t  midiLearn_active(void);	/* armed, waiting or showing FUL */
/* 1 while a touched cell becomes the target (armed, or re-target). */
uint8_t  midiLearn_selectingTarget(void);
void     midiLearn_selectTarget(uint16_t id);	/* menu: touched cell */
/* Target shown as `LRN` (or `FUL`), MIDI_TARGET_OFF when none. */
uint16_t midiLearn_displayTarget(void);
uint8_t  midiLearn_showingFull(void);
void     midiLearn_service(void);	/* FUL timeout (foreground) */

/*
 * Receive (MidiControl, non-reserved CCs only).
 * captureCc(): while a target waits for its CC, a valid CC (not reserved;
 * on G, or on the target voice's channel for voice targets) stores or
 * replaces the row and is consumed (1). applyCc(): writes every matching
 * row's target; 1 when any was written (consumed).
 */
uint8_t  midiLearn_captureCc(uint8_t channel, uint8_t cc);
uint8_t  midiLearn_applyCc(uint8_t channel, uint8_t cc, uint8_t value7);

/* Transmit: the first row for `id` matching the active type. Output: 1
** with *cc and *voiceKind (1 = send on the voice's channel). */
uint8_t  midiLearn_ccForTarget(uint16_t id, uint8_t *cc,
                               uint8_t *voiceKind);

/*
 * settings.cfg (F34): line `index` of the table as
 * "learn=CCC,<g|v>,<v1..v6|fx|scn>,<type>,<key>\n" (Scene rows have no
 * type field). A row whose names cannot be resolved writes a "#" comment
 * line so the line count stays exact. parse: one `learn=` value; a
 * malformed or unresolvable line is skipped (0), the file still loads.
 */
uint8_t  midiLearn_formatSettingsLine(uint8_t index, char *dst, uint16_t cap);
uint8_t  midiLearn_parseSettingsValue(const char *value);

#endif /* MIDILEARN_H_ */
```

### F-3 `Core/MIDI/MidiLearn.c` — NEW FILE

```c
#include "MidiLearn.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiControl.h"
#include "menu.h"
#include "buttonHandler.h"
#include "filesystem.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "timebase.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/*
 * One learned CC (plan §3.6, F36).
 * ccKind: bits 0..6 CC, bit 7 channel kind (0 = G, 1 = target voice's
 * channel). space: 0..5 voice slot, 6 Scene, 7 Effect. type:
 * instrument_type_t (voice), effect_type_id_t (Effect), 0xFF (Scene).
 * index: descriptor 0..63, Scene-local index, or Effect-local 0..62.
 */
typedef struct {
	uint8_t ccKind;
	uint8_t space;
	uint8_t type;
	uint8_t index;
} midiLearn_entry_t;

#define MIDI_LEARN_KIND_VOICE   0x80u
#define MIDI_LEARN_SPACE_SCENE  6u
#define MIDI_LEARN_SPACE_EFFECT 7u
#define MIDI_LEARN_TYPE_NONE    0xFFu

enum {
	MIDI_LEARN_IDLE = 0,
	MIDI_LEARN_WAIT_TARGET,	/* armed: touch a parameter */
	MIDI_LEARN_WAIT_CC,	/* target shows LRN: send a CC */
	MIDI_LEARN_FULL		/* refused: FUL shows briefly */
};

static midiLearn_entry_t midiLearn_table[MIDI_LEARN_MAX_ENTRIES];
static uint8_t  midiLearn_used;
static uint8_t  midiLearn_state;
static uint16_t midiLearn_target = MIDI_TARGET_OFF;
static uint16_t midiLearn_fullUntil;

/* Row key of target `id` in the active Scene; 0 when not addressable. */
static uint8_t midiLearn_keyOf(uint16_t id, uint8_t *space, uint8_t *type,
                               uint8_t *index)
{
	if (instrumentParam_isVoiceParameter(id)) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), instrumentParam_slot(id));

		if (!instr)
			return 0u;
		*space = instrumentParam_slot(id);
		*type = (uint8_t)instr->type;
		*index = instrumentParam_local(id);
		return 1u;
	}
	if (sceneModTarget_isSceneTarget(id)) {
		*space = MIDI_LEARN_SPACE_SCENE;
		*type = MIDI_LEARN_TYPE_NONE;
		return sceneModTarget_indexFromId(id, index);
	}
	if (effectTarget_isEffectId(id)) {
		*space = MIDI_LEARN_SPACE_EFFECT;
		*type = (uint8_t)effects_activeType();
		*index = effectTarget_local(id);
		return (uint8_t)(*index < EFFECT_TARGET_PATTERN_LOCAL_LIMIT);
	}
	return 0u;
}

static uint16_t midiLearn_targetOf(const midiLearn_entry_t *e)
{
	if (e->space < MIDI_LEARN_SPACE_SCENE)
		return (uint16_t)instrumentParam_make(e->space, e->index);
	if (e->space == MIDI_LEARN_SPACE_SCENE)
		return sceneModTarget_idFromIndex(e->index);
	return effectTarget_id(e->index);
}

/* F51: a row applies only while its type is loaded (active Scene). */
static uint8_t midiLearn_typeMatches(const midiLearn_entry_t *e)
{
	if (e->space < MIDI_LEARN_SPACE_SCENE) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), e->space);
		return (uint8_t)(instr && (uint8_t)instr->type == e->type);
	}
	if (e->space == MIDI_LEARN_SPACE_EFFECT)
		return (uint8_t)((uint8_t)effects_activeType() == e->type);
	return 1u;
}

static int16_t midiLearn_find(uint8_t space, uint8_t type, uint8_t index)
{
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		if (e->space == space && e->type == type && e->index == index)
			return (int16_t)i;
	}
	return -1;
}

/* Leave the gesture: LEDs back to their own state, display refreshed. */
static void midiLearn_end(void)
{
	midiLearn_state = MIDI_LEARN_IDLE;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(0u);
	menu_learnDisplayChanged();
}

/* Contracts in MidiLearn.h. */
void midiLearn_clearAll(void)
{
	midiLearn_used = 0u;
}

uint8_t midiLearn_count(void) { return midiLearn_used; }

void midiLearn_arm(void)
{
	midiLearn_state = MIDI_LEARN_WAIT_TARGET;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(1u);
	menu_learnDisplayChanged();
}

void midiLearn_cancel(void)
{
	if (midiLearn_state != MIDI_LEARN_IDLE)
		midiLearn_end();
}

uint8_t midiLearn_active(void)
{
	return (uint8_t)(midiLearn_state != MIDI_LEARN_IDLE);
}

uint8_t midiLearn_selectingTarget(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_WAIT_TARGET ||
	                 midiLearn_state == MIDI_LEARN_WAIT_CC);
}

void midiLearn_selectTarget(uint16_t id)
{
	if (!midiLearn_selectingTarget() || id >= MIDI_TARGET_OFF)
		return;
	midiLearn_target = id;
	midiLearn_state = MIDI_LEARN_WAIT_CC;
	menu_learnDisplayChanged();
}

uint16_t midiLearn_displayTarget(void)
{
	return (midiLearn_state == MIDI_LEARN_WAIT_CC ||
	        midiLearn_state == MIDI_LEARN_FULL) ? midiLearn_target
	                                            : MIDI_TARGET_OFF;
}

uint8_t midiLearn_showingFull(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_FULL);
}

void midiLearn_service(void)
{
	if (midiLearn_state == MIDI_LEARN_FULL &&
	    (uint16_t)(time_sysTick - midiLearn_fullUntil) < 32768u)
		midiLearn_end();
}

uint8_t midiLearn_captureCc(uint8_t channel, uint8_t cc)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t space, type, index;
	int16_t row;

	if (midiLearn_state != MIDI_LEARN_WAIT_CC || midiDefs_ccReserved(cc))
		return 0u;
	/* Valid on G, or on the target voice's own channel (voice targets). */
	if (!onG && !midiControl_targetOnChannel(midiLearn_target, channel))
		return 0u;
	if (!midiLearn_keyOf(midiLearn_target, &space, &type, &index))
		return 0u;
	row = midiLearn_find(space, type, index);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES) {
			/* F29: a full table refuses a new row; FUL shows briefly. */
			midiLearn_state = MIDI_LEARN_FULL;
			midiLearn_fullUntil =
			    (uint16_t)(time_sysTick + MIDI_LEARN_FULL_SHOW_MS);
			menu_learnDisplayChanged();
			return 1u;
		}
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)((cc & 0x7Fu) | (onG ? 0u : MIDI_LEARN_KIND_VOICE));
	midiLearn_table[row].space = space;
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	filesystem_markSettingsDirty();	/* normal debounced settings write */
	midiLearn_end();
	return 1u;
}

uint8_t midiLearn_applyCc(uint8_t channel, uint8_t cc, uint8_t value7)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t consumed = 0u;
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		uint8_t voiceKind = (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		uint16_t id;

		if ((uint8_t)(e->ccKind & 0x7Fu) != cc)
			continue;
		if (voiceKind ? onG : !onG)
			continue;
		if (!midiLearn_typeMatches(e))
			continue;
		id = midiLearn_targetOf(e);
		/* F50: a `v` row moves only its own slot on a shared channel. */
		if (voiceKind && !midiControl_targetOnChannel(id, channel))
			continue;
		consumed |= midiControl_writeTarget(id, value7);
	}
	return consumed;
}

uint8_t midiLearn_ccForTarget(uint16_t id, uint8_t *cc, uint8_t *voiceKind)
{
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];

		if (midiLearn_targetOf(e) != id || !midiLearn_typeMatches(e))
			continue;
		*cc = (uint8_t)(e->ccKind & 0x7Fu);
		*voiceKind = (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		return 1u;
	}
	return 0u;
}
```

Settings lines (same file):

```c
/*
 * Append up to maxLen characters of `text` at dst[pos] (no printf: the
 * firmware links no printf family, and S083 does not add it). Returns the
 * new position; stops one byte short of cap for the terminator.
 */
static uint16_t midiLearn_append(char *dst, uint16_t cap, uint16_t pos,
                                 const char *text, uint8_t maxLen)
{
	while (text && *text != '\0' && maxLen-- != 0u && (uint16_t)(pos + 1u) < cap)
		dst[pos++] = *text++;
	return pos;
}

uint8_t midiLearn_formatSettingsLine(uint8_t index, char *dst, uint16_t cap)
{
	const midiLearn_entry_t *e;
	const char *space = 0;
	const char *type = 0;
	const char *key = 0;
	char slotText[3] = { 'v', '1', '\0' };
	char ccText[4];
	uint8_t cc;
	uint16_t pos = 0u;

	if (index >= midiLearn_used || !dst || cap < 32u)
		return 0u;
	e = &midiLearn_table[index];
	cc = (uint8_t)(e->ccKind & 0x7Fu);
	ccText[0] = (char)('0' + cc / 100u);
	ccText[1] = (char)('0' + (cc / 10u) % 10u);
	ccText[2] = (char)('0' + cc % 10u);
	ccText[3] = '\0';
	if (e->space < MIDI_LEARN_SPACE_SCENE) {
		const instrument_registry_entry_t *reg =
		    instrumentManager_registryEntry((instrument_type_t)e->type);
		const ParamDescriptor *d =
		    instrumentManager_descriptor((instrument_type_t)e->type, e->index);

		slotText[1] = (char)('1' + e->space);
		space = slotText;
		type = reg ? reg->type_text : 0;
		key = d ? d->file_key : 0;
	} else if (e->space == MIDI_LEARN_SPACE_EFFECT) {
		const effect_param_descriptor_t *d =
		    effects_descriptor((effect_type_id_t)e->type, e->index);

		space = "fx";
		type = effects_typeToken((effect_type_id_t)e->type);
		key = d ? d->base.file_key : 0;
	} else {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(
		    sceneModTarget_idFromIndex(e->index));

		space = "scn";
		key = d ? d->short_name : 0;
	}
	if (key && (type || e->space == MIDI_LEARN_SPACE_SCENE)) {
		pos = midiLearn_append(dst, cap, pos, "learn=", 6u);
		pos = midiLearn_append(dst, cap, pos, ccText, 3u);
		pos = midiLearn_append(dst, cap, pos,
		                       (e->ccKind & MIDI_LEARN_KIND_VOICE) ? ",v," : ",g,",
		                       3u);
		pos = midiLearn_append(dst, cap, pos, space, 3u);
		if (type) {
			pos = midiLearn_append(dst, cap, pos, ",", 1u);
			pos = midiLearn_append(dst, cap, pos, type, 3u);
		}
		pos = midiLearn_append(dst, cap, pos, ",", 1u);
		pos = midiLearn_append(dst, cap, pos, key, 40u);
		if ((uint16_t)(pos + 2u) <= cap && key[0] != '\0') {
			dst[pos++] = '\n';
			dst[pos] = '\0';
			return 1u;
		}
	}
	/* Unresolvable or too long: a comment line keeps `lines=` exact. */
	pos = midiLearn_append(dst, cap, 0u, "# learn row unresolved\n", 24u);
	dst[pos] = '\0';
	return 1u;
}

/*
 * Split "CCC,k,space,type,key" (Scene rows: "CCC,k,scn,key") into fields
 * in a local copy; resolve names to indices once (F34). Duplicate keys:
 * the later line replaces the earlier row (one parameter -> one CC).
 */
uint8_t midiLearn_parseSettingsValue(const char *value)
{
	char buf[64];
	char *field[5];
	uint8_t count = 0u;
	uint16_t cc = 0u;
	uint8_t voiceKind;
	uint8_t space, type = MIDI_LEARN_TYPE_NONE, index = 0u;
	uint8_t i;
	int16_t row;
	char *p;

	if (!value || strlen(value) >= sizeof(buf))
		return 0u;
	strcpy(buf, value);
	for (p = buf; count < 5u; ) {
		field[count++] = p;
		p = strchr(p, ',');
		if (!p)
			break;
		*p++ = '\0';
	}
	if (count < 4u)
		return 0u;
	for (i = 0u; field[0][i] != '\0'; i++) {
		if (field[0][i] < '0' || field[0][i] > '9' || i > 2u)
			return 0u;
		cc = (uint16_t)(cc * 10u + (uint16_t)(field[0][i] - '0'));
	}
	if (i == 0u || cc > 127u || midiDefs_ccReserved((uint8_t)cc))
		return 0u;
	if (strcmp(field[1], "g") == 0) voiceKind = 0u;
	else if (strcmp(field[1], "v") == 0) voiceKind = 1u;
	else return 0u;
	if (field[2][0] == 'v' && field[2][1] >= '1' && field[2][1] <= '6' &&
	    field[2][2] == '\0' && count == 5u) {
		instrument_type_t t = instrumentManager_typeFromText(field[3]);

		if (t == INSTRUMENT_TYPE_UNKNOWN ||
		    !instrumentManager_descriptorIndexByKey(t, field[4], &index))
			return 0u;
		space = (uint8_t)(field[2][1] - '1');
		type = (uint8_t)t;
	} else if (strcmp(field[2], "fx") == 0 && count == 5u) {
		effect_type_id_t t;

		if (strlen(field[3]) != 3u || voiceKind ||
		    !effects_typeFromToken(field[3], &t) ||
		    !effects_descriptorByKey(t, field[4], &index) ||
		    index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
			return 0u;
		space = MIDI_LEARN_SPACE_EFFECT;
		type = (uint8_t)t;
	} else if (strcmp(field[2], "scn") == 0 && count == 4u) {
		uint8_t n = sceneModTarget_count();

		for (i = 0u; i < n; i++) {
			const scene_mod_target_descriptor_t *d =
			    sceneModTarget_descriptor(sceneModTarget_idFromIndex(i));
			if (d && d->kind != SCENE_MOD_TARGET_KIND_RETIRED &&
			    d->short_name && strcmp(d->short_name, field[3]) == 0)
				break;
		}
		if (i >= n)
			return 0u;
		/* kind v only for per-voice Scene targets (not fxm). */
		if (voiceKind && sceneModTarget_descriptor(
		        sceneModTarget_idFromIndex(i))->kind ==
		        SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
			return 0u;
		space = MIDI_LEARN_SPACE_SCENE;
		index = i;
	} else {
		return 0u;
	}
	row = midiLearn_find(space, type, index);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES)
			return 0u;
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)(cc | (voiceKind ? MIDI_LEARN_KIND_VOICE : 0u));
	midiLearn_table[row].space = space;
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	return 1u;
}
```

Implementation checks for F-3: the firmware links no `printf` family
(checked 2026-10-21: no `snprintf`/`sprintf` in `Core/` or `main.c`), so
the writer appends text by hand; `effects_typeToken()` may return a
3-character token without a terminator (the append copies at most 3);
`effects_descriptorByKey()` matches `base.file_key`; check whether
`filesystem_formatAssignmentU16Line()` expects the line to end in `\n`
and match it (the existing lines end in `\n`).

### F-4 `Core/MIDI/MidiControl.c` — Learn in the CC path

**Where:** the end of `midiControl_handleCc()` (E-4, comment "Phase F").
**Action:** MODIFY:

```c
	/* Learn (plan §3.6): a waiting capture takes the CC first; otherwise
	** every matching row is written (macros). Non-reserved CCs only. */
	if (midiDefs_ccReserved(cc))
		return 0u;
	if (midiLearn_captureCc(channel, cc))
		return 1u;
	return midiLearn_applyCc(channel, cc, value);
```

ADD `#include "MidiLearn.h"`.

### F-5 `Core/MIDI/MidiIn.c` — Learn timeout

**Where:** `midiIn_service()`, before `midiOut_service();`.
**Action:** ADD `midiLearn_service();` (and the include).

### F-6 `Core/Hardware/frontPanel/buttonHandler.c` — the gesture

**F-6a SHIFT+PLAY never toggles transport.** **Where:** `processPress()`
`case BUT_START_STOP:` 2874-2895. **Action:** MODIFY:

```c
    case BUT_START_STOP:
        /*
         * START/STOP is transport state (buttonHandler owns the LED and UI
         * bit; Sequencer owns playback).
         *
         * S080 G8 / S083 F8: SHIFT+PLAY arms the SHIFT+PLAY chord in every
         * mode and never toggles the transport (MIDI_SPEC §6.3): +SEQ in
         * PERF opens the chain editor, +REC arms MIDI Learn.
         */
        if (buttonHandler_getShift()) {
            bh_chainPlayArmed = 1u;
            break;
        }
        buttonHandler_setRunStopState((uint8_t)(1u - bh_state.seqRunning));
        seq_setRunning((uint8_t)bh_state.seqRunning);
        break;
```

**F-6b Arm.** **Where:** `case BUT_REC:` after the live-erase test
(`if (liveRec_mode() == LIVEREC_MODE_ERASE) break;`, ~2912). **Action:** ADD:

```c
        /*
         * SHIFT+PLAY+REC arms MIDI Learn (S083 C5, F8).
         *
         * What: with SHIFT and PLAY held (the chord armed by F-6a), REC
         * performs none of its own functions (no record toggle, no SHIFT+REC
         * menu) and arms Learn; SHIFT, PLAY and REC flash until a valid CC
         * arrives or Learn is cancelled. Affiliates: midiLearn_arm(),
         * buttonHandler_setLearnLeds().
         */
        if (buttonHandler_getShift() && bh_chainPlayArmed) {
            midiLearn_arm();
            break;
        }
```

**F-6c Cancel.** **Where:** top of `processPress()` (2738), before
`if (copyClear_buttonPressed(buttonNr))`. **Action:** ADD:

```c
    /*
     * Learn cancel (S083 C5): while Learn is active, a press of SHIFT, PLAY
     * or REC cancels it and is consumed (no transport, record or menu
     * action). SHIFT keeps its hold flag and its release is consumed, as
     * for a latched-menu close. Affiliates: midiLearn_cancel(),
     * bh_shiftReleaseConsume.
     */
    if (midiLearn_active() &&
        (buttonNr == BUT_SHIFT || buttonNr == BUT_START_STOP ||
         buttonNr == BUT_REC)) {
        midiLearn_cancel();
        if (buttonNr == BUT_SHIFT) {
            bh_shiftHeld = 1u;
            bh_shiftReleaseConsume = 1u;
        }
        return;
    }
```

**F-6d LEDs.** ADD (and declare in `buttonHandler.h` with this comment):

```c
/*
 * Learn LED flash (S083 C5).
 * What: on = SHIFT, PLAY and REC flash (continuous; the code calls this a
 * blink). off = the three LEDs return to their own state: SHIFT to its
 * hold, PLAY to the transport, REC to the live-record LATCH state.
 * Inputs: on/off. Accessor: MidiLearn.c (arm, end). Affiliates:
 * led_setBlinkLed(), liveRec_mode().
 */
void buttonHandler_setLearnLeds(uint8_t on)
{
    led_setBlinkLed(LED_SHIFT, on);
    led_setBlinkLed(LED_START_STOP, on);
    led_setBlinkLed(LED_REC, on);
    if (!on) {
        led_setValue(bh_shiftHeld, LED_SHIFT);
        led_setValue((uint8_t)bh_state.seqRunning, LED_START_STOP);
        led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC);
    }
}
```

ADD `#include "MidiLearn.h"` to `buttonHandler.c`.

### F-7 `Core/Menu/menu.c` — capture, display, cancel

**F-7a Pot capture.** **Where:** `menu_parseKnobDelta()`, after the SEQ
refusal (`return;` at ~15549). **Action:** ADD:

```c
    /*
     * Learn target selection by pot (S083 C5, Q10, C-4).
     *
     * What: while Learn waits for a target, turning a pot over a cell with
     * a MIDI address makes it the target (it shows LRN) without changing
     * its value; a touch elsewhere edits normally and Learn keeps waiting.
     * Inputs: knobNr, visible half. Affiliates: menu_cellMidiTargetAt(),
     * midiLearn_selectTarget().
     */
    if (midiLearn_selectingTarget() && knobNr < ENDLESS_POT_COUNT) {
        const uint8_t lrnPage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
        const uint8_t lrnParam = (uint8_t)(menuIndex & MASK_PARAMETER);
        const uint8_t lrnHalf = menu_isScreenPage(menu_activePage)
            ? 0u : (uint8_t)((lrnParam > 3u) ? 4u : 0u);
        uint16_t id;

        if (menu_cellMidiTargetAt(lrnPage, (uint8_t)(knobNr + lrnHalf), &id)) {
            midiLearn_selectTarget(id);
            return;
        }
    }
```

**F-7b Encoder capture.** **Where:** `menu_encoderChangeParameter()`, after
the step-automation block (~13846). **Action:** ADD the same rule with
`menu_cellMidiTargetAt(activePage, activeParameter, &id)`.

**F-7c `LRN`/`FUL` display.** ADD:

```c
/*
 * Learn display (S083 C5, F29).
 * What: the cell whose MIDI target is Learn's target shows `LRN` while a
 * CC is awaited and `FUL` while a refused learn is shown. Inputs: cell,
 * buf (3 chars). Output: 1 when written. Accessors:
 * menu_formatCellValue3(), menu_repaintGeneric().
 */
static uint8_t menu_learnName(const menu_cell_t *cell, char *buf)
{
    uint16_t want = midiLearn_displayTarget();
    uint16_t id;

    if (want == MIDI_TARGET_OFF || !menu_cellMidiTarget(cell, &id) ||
        id != want)
        return 0u;
    memcpy(buf, midiLearn_showingFull() ? "FUL" : "LRN", 3);
    return 1u;
}

/* MidiLearn: state changed; repaint through the coalesced path. */
void menu_learnDisplayChanged(void)
{
    menu_knobs_dirty = 1;
}
```

| Line | Action | Change |
|------|--------|--------|
| `menu_formatCellValue3()` first statement after the locals (5906) | ADD | `if (menu_learnName(cell, valueAsText)) return;` |
| `menu_repaintGeneric()` before the A-20f MIDI-name call (13594) | ADD | `if (menu_learnName(&cell, &editDisplayBuffer[1][13])) { } else` chained before it |
| `menu.h` | ADD | `void menu_learnDisplayChanged(void);` with the comment above |

**F-7d Cancel on a page or mode change.** **Where:** `menu_switchPage()`
17161, after the `preset_backgroundBankLoadLocked()` early return
(~17180). **Action:** ADD:

```c
    /*
     * A page or mode change cancels Learn (S083 MIDI_SPEC §6.3). This also
     * covers a MIDI-started Bank load, which switches to Load/Save (S082 D7).
     */
    if (pageNr != menu_activePage)
        midiLearn_cancel();
```

ADD `#include "MidiLearn.h"` to `menu.c`.

### F-8 `Core/Hardware/SD/filesystem.c` — `learn=` lines

| Line(s) | Action | Change |
|---------|--------|--------|
| 85 (after A-18) | ADD | `#include "MidiLearn.h"` |
| 191 area | ADD | `#define FS_SETTINGS_FIXED_LINES 24u` with comment "settings.cfg data lines before the learn= lines (cases 0..23)" |
| 3061 (after the `active_bank` block in `filesystem_parseSettingsLine()`) | ADD | block F-8a |
| 2870 (`filesystem_resetSettingsToDefaults()`, first statement) | ADD | `midiLearn_clearAll();` with comment F-8b |
| 18535-18552 (`case 24u:` terminator) | MODIFY | block F-8c (before the `switch`, and remove `case 24u`) |

**F-8a:**

```c
    /*
     * MIDI Learn row (S083 F34). A malformed or unresolvable line is
     * skipped: the rest of the file still loads (MIDI_SPEC §6.4), so the
     * line is always accepted here.
     */
    if (strcmp(key, "learn") == 0) {
        (void)midiLearn_parseSettingsValue(value);
        return FS_STATUS_DONE;
    }
```

**F-8b:**

```c
    /* S083: the Learn table belongs to settings.cfg; a load starts empty
    ** (the recovery pass's parse is discarded with the other values). */
    midiLearn_clearAll();
```

**F-8c** (at the top of `filesystem_nextSettingsLine()`, before the
`switch`):

```c
    /*
     * Learn rows and the terminator (S083 F34, R11).
     *
     * What: after the FS_SETTINGS_FIXED_LINES fixed lines come one line per
     * Learn row, then "lines=N" with N = fixed + rows. Rows are only ever
     * appended or replaced in place at runtime (no UI remove), so a row
     * learned during a write is either included or written next time; the
     * live count keeps the terminator equal to the lines written. A row
     * whose names cannot be resolved writes a comment line, keeping the
     * count exact. Line length: at most ~50 characters (< FS_TEXT_LINE_MAX).
     * Line count: at most 24 + 128 = 152 (uint8_t recovery counter).
     * Affiliates: midiLearn_formatSettingsLine(), the recovery prelude.
     */
    if (op_write_line_index >= FS_SETTINGS_FIXED_LINES) {
        uint16_t row = (uint16_t)(op_write_line_index - FS_SETTINGS_FIXED_LINES);
        uint8_t rows = midiLearn_count();

        if (row < rows)
            return midiLearn_formatSettingsLine((uint8_t)row, dst, cap);
        if (row == rows)
            return filesystem_formatAssignmentU16Line(
                dst, cap, "lines", (uint16_t)(FS_SETTINGS_FIXED_LINES + rows));
        return 0u;
    }
```

Check at implementation: Pass B of the settings load calls
`filesystem_resetSettingsToDefaults()` (line 21842) before its read pass,
so learn rows parsed during the recovery pass are discarded (as the
comment at 21713 says for the other values).

### F-9 `Makefile`

ADD `  Core/MIDI/MidiLearn.c \`.

### F-10 Phase F verification

1. Learn: SHIFT+PLAY+REC (all modes) → three LEDs flash; touch a VOICE pot → `LRN`, value unchanged; send CC 20 on G → row stored, display back, `settings.cfg` gains `learn=020,g,v1,drm,<key>` after its debounce.
2. Re-learn the same parameter on a voice channel → the row's CC and kind change (`v`), no new row (F49).
3. One CC to several parameters (macros) → all move; the parameter keeps one CC.
4. Type matching: change slot 1's instrument type → the row stops applying; change back → it applies again (F51 on Scene 17 as well).
5. Shared voice channel (T1+T2 on ch 4): a `v` row for slot 1 moves only slot 1 (F50); CC1 moves both.
6. Reserved CCs (0,1,2,6,32,38,96..101,120..127) never capture.
7. Cancel: SHIFT, PLAY or REC press (consumed), a mode/page change, a MIDI CC0 Bank load.
8. Full table (128 rows; script it): the 129th learn shows `FUL` for 1 s and stores nothing.
9. Hand edits: delete a line, corrupt a line (bad key, unknown type) → the rest loads; reboot keeps the table; `lines=` equals the line count; power-cut test of the `settings.tmp` recovery with learn lines present.
10. SHIFT+PLAY without REC does not toggle the transport in any mode; SHIFT+PLAY+SEQ still opens the chain editor in PERF.

---

## 10. Phase G — Transmit

**Goal.** Plan §3.9 with Q4, Q8, Q9: notes go out on the inverse map (and
not at all when the pitch cannot be expressed); locally made parameter
changes go out as the learned CC or NRPN, coalesced and throttled, never
echoing MIDI-originated changes; PC per track; CC0 after a Menu Bank load.

### G-1 `config.h`

ADD to the MIDI block:

```c
/*
 * Parameter transmit interval (S083 plan §3.9 C, R5): the minimum time
 * between two parameter messages of a knob sweep (the latest value of the
 * pending target is sent). A full NRPN is 9 bytes, ~2.9 ms on DIN; 10 ms
 * keeps clock and notes undisturbed in the 64-byte DIN FIFO.
 */
#define MIDI_OUT_PARAM_INTERVAL_MS 10u
```

### G-2 `Core/MIDI/MidiControl.c/h` — the channel a target transmits on

ADD (and declare in `MidiControl.h`):

```c
/*
 * Effective channel of a target's voice in the active Scene (S083 Q8).
 * What: voice IDs and the per-voice Scene targets -> the effective channel
 * of the voice's track (slot 5 -> track 6 (T6)); track timing targets ->
 * that track's effective channel; Effect and fxm -> 0. Output: 1..16 or 0.
 * Accessors: MidiOut parameter TX (learned kind `v`; NRPN with G off).
 */
uint8_t midiControl_targetChannel(uint16_t id)
{
	midiNoteMap_settings_t map;
	uint8_t track = 0xFFu;

	if (instrumentParam_isVoiceParameter(id)) {
		track = instrumentParam_slot(id);
	} else if (sceneModTarget_isSceneTarget(id)) {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(id);

		if (!d || d->kind == SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
			return 0u;
		track = (d->kind == SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY)
		        ? 6u : d->voice_slot;
	}
	if (track >= 7u)
		return 0u;
	midiNoteMap_load(scene_getActiveIndex(), &map);
	return midiNoteMap_effectiveChannel(&map, track);
}
```

### G-3 `Core/MIDI/MidiOut.h` — Phase G API

**Action:** ADD to `MidiOut.h` (contracts):

```c
/*
 * Track trigger output (plan §3.9 N, S083 Q4, C-2, C-6).
 * What: maps (track, internal note) through the inverse note map of the
 * Scene the track plays and sends it with midiOut_trackNoteOn(); when the
 * pitch has no note on the track's range nothing is sent (the previous
 * note still ends). Inputs: track 0..6, played Scene, internal note,
 * velocity. Context: TIM3 and foreground. Accessors: seq_triggerVoice(),
 * seq_previewVoice() (every LXR trigger: sequencer, overlay, preview,
 * MIDI-in, C-3).
 */
void midiOut_trackTrigger(uint8_t track, uint8_t scene_index,
                          uint8_t internal, uint8_t velocity);
/*
 * A panel edit changed target `id` (plan §3.9 C).
 * What: marks it pending; midiOut_service() sends its current value as the
 * learned CC (first row for the current type) or as NRPN, at most every
 * MIDI_OUT_PARAM_INTERVAL_MS and only with room in the DIN FIFO. A change
 * to another target sends the pending one first. MIDI-originated writes
 * never call this (no echo, R6). Accessor: menu.c panel edit paths (G-7).
 */
void midiOut_parameterChanged(uint16_t id);
/*
 * Per-track Scene change PCs (plan §3.9 P, C-8): one PC (Scene number) on
 * each distinct effective channel of the moved tracks, except G and none.
 * Accessor: buttonHandler_perfVoiceHeldSeqPressed() (panel only; a MIDI
 * per-track PC is not echoed).
 */
void midiOut_trackProgramChange(uint8_t trackMask, uint8_t scene_index);
```

### G-4 `Core/MIDI/MidiOut.c` — Phase G implementation

ADD includes `MidiNoteMap.h`, `MidiControl.h`, `MidiLearn.h`,
`SceneData.h`, `BankData.h`, `presetManager.h`, `timebase.h`, `config.h`;
ADD the state and functions:

```c
/*
 * Parameter TX state (S083 plan §3.9 C; RAM 8 B approved).
 * midiOut_nrpnKey[port]: last NRPN number sent per port as
 * ((channel - 1) << 9 | id) + 1, 0 = none, so the CC99/98 pair is resent
 * only when the number or channel changes. midiOut_pending: target + 1
 * waiting to be sent (0 = none). midiOut_lastSendMs: time_sysTick of the
 * last parameter send (throttle).
 */
static uint16_t midiOut_nrpnKey[2];
static uint16_t midiOut_pending;
static uint16_t midiOut_lastSendMs;

void midiOut_trackTrigger(uint8_t track, uint8_t scene_index,
                          uint8_t internal, uint8_t velocity)
{
	midiNoteMap_settings_t map;
	uint8_t channel;
	uint8_t note;

	midiNoteMap_load(scene_index, &map);
	if (!midiNoteMap_inverse(&map, track, internal, &channel, &note)) {
		midiOut_trackNoteOff(track);	/* Q4: no note for this pitch */
		return;
	}
	midiOut_trackNoteOn(track, channel, note, velocity);
}

static void midiOut_cc(uint8_t channel, uint8_t cc, uint8_t value,
                       uint8_t portMask)
{
	MidiMsg msg = {0};

	msg.status = (uint8_t)(MIDI_CC | (channel - 1u));
	msg.data1 = cc;
	msg.data2 = value;
	msg.bits.length = 2u;
	midiOut_sendMsg(&msg, portMask);
}

/*
 * Send one target's current value (learned CC, else NRPN; Q8 channel
 * rule). Inputs: id. Output: messages on both ports under TXF C.
 */
static void midiOut_sendParameter(uint16_t id)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t value = midiControl_targetValue7(scene_getActiveIndex(), id);
	uint8_t cc;
	uint8_t voiceKind;
	uint8_t channel;
	uint8_t port;

	if (!midiIn_txAllows(MIDI_FILTER_CONTROL))
		return;
	if (midiLearn_ccForTarget(id, &cc, &voiceKind)) {
		channel = voiceKind ? midiControl_targetChannel(id) : global;
		if (channel != 0u)
			midiOut_cc(channel, cc, value, MIDI_PORT_BOTH);
		return;
	}
	channel = (global != 0u) ? global : midiControl_targetChannel(id);
	if (channel == 0u)
		return;
	for (port = 0u; port < 2u; port++) {
		uint8_t mask = (port == 0u) ? MIDI_PORT_DIN : MIDI_PORT_USB;
		uint16_t key = (uint16_t)((((uint16_t)(channel - 1u) << 9) | id) + 1u);

		if (midiOut_nrpnKey[port] != key) {
			midiOut_cc(channel, CC_NRPN_MSB, (uint8_t)(id >> 7), mask);
			midiOut_cc(channel, CC_NRPN_LSB, (uint8_t)(id & 0x7Fu), mask);
			midiOut_nrpnKey[port] = key;
		}
		midiOut_cc(channel, CC_DATA_ENTRY_MSB, value, mask);
	}
}

void midiOut_parameterChanged(uint16_t id)
{
	if (id >= MIDI_TARGET_OFF)
		return;
	if (midiOut_pending != 0u && midiOut_pending != (uint16_t)(id + 1u)) {
		/* another target is waiting: send it now so no change is lost */
		uint16_t older = (uint16_t)(midiOut_pending - 1u);

		midiOut_pending = 0u;
		midiOut_sendParameter(older);
		midiOut_lastSendMs = time_sysTick;
	}
	midiOut_pending = (uint16_t)(id + 1u);
}

void midiOut_trackProgramChange(uint8_t trackMask, uint8_t scene_index)
{
	midiNoteMap_settings_t map;
	uint16_t sent = 0u;
	uint8_t global = midiIn_globalChannel();
	uint8_t t;

	if (scene_index >= 16u)
		return;
	midiNoteMap_load(scene_getActiveIndex(), &map);
	for (t = 0u; t < 7u; t++) {
		uint8_t ch;

		if ((trackMask & (1u << t)) == 0u)
			continue;
		ch = midiNoteMap_effectiveChannel(&map, t);
		if (ch == 0u || ch == global || (sent & (1u << (ch - 1u))))
			continue;
		sent |= (uint16_t)(1u << (ch - 1u));
		midiOut_programChange(ch, scene_index);
	}
}
```

`midiOut_service()` (replaces the A-7 empty body):

```c
/*
 * Foreground transmit service (S083 plan §3.9).
 * What: (1) sends the pending parameter when the interval passed and the
 * DIN FIFO has room for a full NRPN (9 B); (2) after a Menu-origin Bank
 * load completes successfully, sends CC0 = Bank slot on G under TXF P
 * (a MIDI-origin load is not echoed, Q9; a failed load sends nothing).
 * Context: foreground (midiIn_service()). Affiliates:
 * preset_takeBackgroundBankLoadResult() (sole consumer of the S082 result
 * byte), bank_restoreBankSlot().
 */
void midiOut_service(void)
{
	uint8_t result;

	if (midiOut_pending != 0u &&
	    (uint16_t)(time_sysTick - midiOut_lastSendMs) >=
	        MIDI_OUT_PARAM_INTERVAL_MS &&
	    midiDin_txFree() >= 9u) {
		uint16_t id = (uint16_t)(midiOut_pending - 1u);

		midiOut_pending = 0u;
		midiOut_sendParameter(id);
		midiOut_lastSendMs = time_sysTick;
	}
	result = preset_takeBackgroundBankLoadResult();
	if ((result & PRESET_BG_RESULT_CODE) == PRESET_BG_RESULT_DONE &&
	    (result & PRESET_BG_RESULT_MIDI) == 0u &&
	    midiIn_txAllows(MIDI_FILTER_PROGRAM) &&
	    midiIn_globalChannel() != 0u && bank_restoreBankSlot() <= 127u)
		midiOut_cc(midiIn_globalChannel(), CC_BANK_CHANGE,
		           (uint8_t)bank_restoreBankSlot(), MIDI_PORT_BOTH);
}
```

### G-5 `Core/Sequencer/sequencer.c` — triggers through the inverse map

**Where:** `seq_triggerVoice()`, the A-14a block. **Action:** MODIFY:

```c
	/*
	 * Track MIDI output (S077 P2 §6.5; S083 C-2, C-6, Q4).
	 *
	 * What: the internal note goes out as the note that plays it back on
	 * this track (inverse note map of the Scene the track plays); a pitch
	 * the track's range cannot express sends nothing. Inputs:
	 * seq_perTrackPattern[voiceNr], note, vol. Affiliates:
	 * midiOut_trackTrigger(), MidiNoteMap.
	 */
	midiOut_trackTrigger(voiceNr, seq_perTrackPattern[voiceNr], note, vol);
```

Remove the now unused locals `midiChan`, `midiNote` (1915-1916).

### G-6 `Core/Sequencer/sequencer.c` — preview pitch (C-1)

**Where:** `seq_previewVoice()` 2009-2015 (A-14b). **Action:** MODIFY:

```c
	/*
	 * Preview plays the default pitch (S083 C-1): a track's MIDI note is the
	 * root of its receive range, not a pitch. MIDI output goes through the
	 * inverse map like any trigger.
	 */
	note = MIDI_DEFAULT_TRIGGER_NOTE;
	[trigger-jack and voice lines unchanged]
	midiOut_trackTrigger(voiceNr, seq_perTrackPattern[voiceNr], note,
	                     ROLL_VOLUME);
```

Remove the local `midiChan`; update the function comment ("PatternData
supplies the optional track MIDI note override" → "plays the default
pitch; the track's MIDI note only names its receive root").

### G-7 `Core/Menu/menu.c` — panel parameter TX hooks

**G-7a** `menu_parseKnobDelta()`, after the final commit (15725-15729,
before `menu_liveRecRecordCell(...)`). **Action:** ADD:

```c
    /*
     * Parameter TX of a panel edit (S083 plan §3.9 C).
     *
     * What: a pot edit of a cell with a MIDI address is offered to MidiOut,
     * which sends the target's Normal value (learned CC or NRPN). Not in a
     * Morph view (the edit changed a Morph endpoint, not the value MIDI
     * addresses). MIDI-originated writes never come through here, so they
     * are never echoed. Affiliates: menu_cellMidiTargetAt(),
     * midiOut_parameterChanged().
     */
    if (!voiceModeShowMorph && !menu_patternTrackMorphViewActive() &&
        !(menu_activePage == EFFECT_PAGE && menuEffects_showMorph())) {
        uint16_t id;

        if (menu_cellMidiTargetAt(activePage, (uint8_t)(knobNr + is2ndPage),
                                  &id))
            midiOut_parameterChanged(id);
    }
```

**G-7b** `menu_encoderChangeParameter()`, after
`(void)menu_cellCommitValue(&cell, value);` (13962). **Action:** ADD the
same block with `(activePage, activeParameter)`.

### G-8 `Core/Hardware/frontPanel/buttonHandler.c` — per-track PC TX

**Where:** `buttonHandler_perfVoiceHeldSeqPressed()`, after
`seq_assignTracksToScene(...)` (D-5). **Action:** ADD:

```c
    /* S083 plan §3.9 P: PC on the moved tracks' channels (panel only). */
    midiOut_trackProgramChange(perf_heldVoiceMask, seqButtonPressed);
```

### G-9 Phase G verification

1. DAW records the LXR output while a pattern plays with stripe, split and assigned tracks; played back into the LXR (sequencer stopped) it sounds the same tracks at the same pitches (§3.3 inverse). Steps with a pitch a stripe track cannot express send no note (Q4).
2. Two tracks on one channel never leave a hanging note; Stop, mute, Scene change and `ch`/`chn` edits send the note-offs (on the channel each note used, R-B).
3. Knob sweeps: NRPN out on G, CC99/98 only when the number changes, values follow at ≤ 100 Hz; with a learned CC the CC goes out instead; G off: voice targets as NRPN on the voice channel, Effect targets send nothing (Q8). DIN FIFO stress with clock out running: no clock jitter, `midiDin_txDropCount()` stays 0 (record in §12).
4. No echo: NRPN/CC received from a DAW is not sent back; a PC received is not sent back; a MIDI-started Bank load sends no CC0; a Menu Bank load sends CC0 = slot after it completes (not after a failure).
5. Panel per-track Scene change sends PC on each moved track's channel (not G).
6. Overlay `def` and VOICE preview play the default pitch (C-1).

---

## 11. Phase H — Documents and memory

| Document | Update |
|----------|--------|
| `knowledge_files/specification_reference/MIDI_SPEC.md` | Replace the DRAFT banner with "Current as of S083"; close §15 row by row; fold in C-1..C-11 and the RAM rows (R-A..R-D); §12 module table to as built. |
| `MODULE_INTERCHANGE_SPEC.md` | Replace the MidiParser section with the Core/MIDI modules and their APIs; buttonHandler overlay MIDI entry points and Learn LEDs; menu `menu_commitTargetEdit()`, `menu_commitGlobalMorphFromMidi()`, `menu_notifyTargetChanged()`, `menu_perfModeSceneChangeFrom()`, `menu_learnDisplayChanged()`; sequencer `seq_setRunningEx()`, `seq_continueRunning()`, `seq_setResumeTick()`, `seq_assignTracksToScene()`, `seq_setSceneChangeOrigin()`; removed `preset_applySoundParameter()`, `preset_setInstrumentParameterFromMidi()`, `seq_setNextPattern()`, `seq_midiNoteOff()`, `seq_sendMidiNoteOn()`; `usb_sendMidiPacket()`. |
| `FILESYSTEM_SPEC.md` | `settings.cfg`: `midi_chan_global` 0 = off, `midi_routing` 0..7 (new list), `learn=` lines, `lines=24+N`; `sceneset.scg`: `midi_channel` 0 = unassigned, `midi_note` 0 = `def`, defaults 0. |
| `PATTERN_DYNAMIC_STACK.md` | §6.2a: a MIDI write is a menu edit (Q1); §6.5: Continue/SPP resume through the master-clock anchors; §6.7: MIDI hits placed at arrival time (age). |
| `BANK_PRESET_ARCHITECTURE.md` | §3 write priority (MIDI = menu edit); MIDI fan-out through `bank_editTargetMask()`. |
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | The "automation, menu, MIDI" priority line. |
| `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | R13: MIDI writes set menuHold like menu edits. |
| `STORAGE_SRAM_MANIFEST.md` | Measured MIDI allocations and releases (§2 ledger), the new linker totals. |
| `MEMORY.md` | Rewrite "MIDI / Clock Reminders" (remove the BAR1/BAR2 path and default notes 36..42; add the module map, the consume-then-route rule, the channel convention, the note map rule, C-1); session state. |
| `SCOPING_TARGETS.md` | Phase 6 §6.1 and A20 done; accomplished/open at the close. |
| `knowledge_files/log_archive/083_SESSION_HANDOFF_LOG.md`, `000_SESSION_INDEX.md` | At the session close, per the template. |

---

## 12. Implementation notes (append while implementing)

Record per step: deviations from this schedule (with reason), measured
`text/data/bss` after each phase (`make all`, `link_budget.py`), the RAM
items as linked (with alignment padding reported separately), host test
output, and hardware results per verification list.

| Phase | Build (text / data / bss) | RAM as linked | Notes |
|-------|---------------------------|---------------|-------|
| baseline (S082 close) | 598,120 / 420 / 430,928 | — | |
| A | 594,400 / 428 / 430,268 | — | Core/MIDI rebuilt (MidiDefs, MidiBuffer, MidiDin, MidiTransport, MidiIn, MidiOut, MidiPitch); old parser/FIFO/UART/realtime files removed; valueShaper.h moved to Core/DSPAudio; consume-then-route with the S082 drop filter kept; MRT/menu/settings/Scene channel changes applied. Flash headroom 158,836 B. bss −660 B vs baseline (legacy parser state released, new receive buffers added). Build and image PASS; hardware items await the user. |
| B | 595,352 / 424 / 430,312 | +47 B vs A (overlay table 56 B − 9 B single slot, alignment included) | MidiNoteMap (N1–N5, TX inverse Q4), MidiNotes (single trigger, CC2, overlay dispatch), per-track overlay executor with panel/MIDI entry points, LiveRecord `age_us` placement. Host test `make -C tools/midi_test run` PASS (examples, N5, G off, 200,000 random settings). Flash headroom 157,888 B. Hardware matrix pending. |
| C | 595,736 / 424 / 430,312 | +4 B (`seq_resumeTick`, R-C) inside the existing alignment | `seq_setRunningEx` with selectable realtime output, `seq_continueRunning`/`seq_setResumeTick`, `sync_midiContinue`, Continue handled separately from Start, SPP framed and applied while stopped and `snc`-selected. No LXR re-send for received transport (C-11). Flash headroom 157,504 B. Hardware items pending. |
| D | 595,864 / 428 / 430,328 | +1 B origin flag (Q13) plus 3 B alignment/data | A1 queued-action fix (unavailable target skipped whole), PERF LED guard (C-5), `menu_perfModeSceneChangeFrom()` with origin, `seq_sceneChangeFromMidi` no-echo flag (cleared by chain fires), shared `seq_assignTracksToScene()` used by panel and MIDI, `MidiProgram` (PC whole Scene/per-track with availability, CC0 background Bank request). Flash headroom 157,372 B. Hardware items pending. |
| E | 599,608 / 428 / 430,376 | +48 B (NRPN contexts 2 × 8 × 3 B, exact) | One shared commit path: `menu_cellCommitValueEx(panel)` with `menu_cellCommitValue()` panel wrapper; MIDI = `menu_commitTargetEdit()` (Normal image, fan-out, R12/R13, recording, target-ID repaint); `menu_commitGlobalMorphFromMidi()` (G-1, no recording); `menu_cellMidiTarget()`/`menu_targetVisible()`; track timing by target ID; `MidiControl` with NRPN contexts, CC1, CC2 and the channel rule; `preset_setInstrumentParameterFromMidi()` and `menu_clampInstrumentValue()` removed. Host test PASS. Flash headroom 153,628 B. Hardware items pending. |
| F | 602,688 / 424 / 430,904 | +528 B (Learn table 512 B + 16 B state, alignment included) | `MidiLearn` (128 type-tagged rows, capture/replace/full/cancel, type matching, `learn=` parse/format), SHIFT+PLAY+REC gesture with SHIFT/PLAY/REC flashing, pot/encoder target selection without value change, `LRN`/`FUL` display, page/mode cancel, `settings.cfg` writer (`lines=24+N`) and load. Flash headroom 150,552 B. Hardware items pending. |
| G | 603,984 / 424 / 430,896 | −8 B vs F (TX state filled existing alignment) | `MidiOut_trackTrigger()` through the inverse map (Q4: no TX outside the range), per-track PC TX (C-8), parameter TX (learned CC else NRPN, number cache, 10 ms throttle, 9 B FIFO check), CC0 on a successful Menu-origin Bank load only, no echo for MIDI-originated changes; preview/`def` play internal 63 (C-1). Flash headroom 149,256 B at that measurement. Hardware items pending. |
| G+ (size fix) | 603,896 / 424 / 430,120 | −784 B vs G (measured after the fix) | Deviation from A-2: the planned `struct MidiBits` used an `enum MidiSource` bitfield plus `unsigned` fields, which GCC stores in 4 B, so `MidiMsg` measured 12 B and the USB ring would have been 1,536 B. `MidiBits` is now one byte of `uint8_t` bitfields (`_Static_assert` in `MidiIn.c`), giving the planned 6 B `MidiMsg` and a 768 B USB ring. All final numbers above are post-fix. |
| H | 603,896 / 424 / 430,120 | — | Documents and memory updated (see the list below). No code change. |

### 12.1 Deviations and findings while implementing

- **A:** the new module set is exactly the planned files. The old receive
  framer's `IGNORE` initial state is kept, so a stray data byte before any
  status is discarded. `valueShaper.h` moved to `Core/DSPAudio/`; no include
  edits were needed (`-ICore/DSPAudio` is already on the path). The old
  fixed-point/legacy parser statics (`midiParser_originalCcValues[255]`,
  `midi_NoteOverride[7]`, `midi_MidiChannels[8]`) are gone with the file.
- **B:** the single-trigger path calls `seq_triggerVoice()` and then reports
  the same hit to LiveRecord, so the trigger jack/TX and the record agree,
  and the existing reset rule applies. Roll repeats pass `reset = 0`.
  `MidiNoteMap` is table-free; the host test also exercises the Q4 inverse
  rule on the §4.2 examples.
- **C:** the SPP source test is the side-effect-free twin of the realtime
  gate (Q11); it deliberately does not touch AUTO activity flags.
- **D:** a chain fire clears the MIDI-origin bit, so a chain-raised change
  does send its PC; the A1 queue fix skips the whole action (mute and Morph
  included) for an unavailable target.
- **E:** `menu_cellCommitValueEx()` keeps the live Morph-image decision only
  for the flat sound ids that `menu_paramUsesMorphView()` admits; the MIDI
  targets are static Scene/instrument/Effect cells and therefore always
  commit the Normal image even when a panel Morph view is open. This is the
  intended Q1 result and was verified by reading the commit path.
- **F:** the `settings.cfg` writer has no `printf`; `midiLearn_append()`
  builds each line by hand (the firmware still links no printf family). A
  row whose names no longer resolve is written as a `#` comment so `lines=`
  stays exact.
- **G:** `midiOut_service()` is the sole consumer of the S082 one-shot Bank
  result byte. A Menu-origin DONE sends CC0; a MIDI-origin or failed load
  sends nothing. Parameter TX reserves 9 B of DIN room for a full NRPN.
- **Verification run:** `make all`, `make img` and
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` pass;
  `make -C tools/midi_test run` prints `PASS (0 failures)`. The A–G hardware
  matrices remain to be reported by the user.

---

## 13. Assessment of the implementation (2026-10-21)

*Written for the user and the implementer. Scope: the working tree on
`dev-ph6-midi` after the S083 implementation (uncommitted on top of
`f9aaeec`), checked against this schedule (§1–§11) and the plan
(revision 5 + Q13). Method: clean rebuild of every MIDI object, the host
test, linked symbol sizes (`arm-none-eabi-nm -S`), and a line-by-line read
of all new `Core/MIDI/` modules and every changed caller (buttonHandler,
menu, sequencer, clockSync, LiveRecord, filesystem, SceneData,
storageTypes, USB, presetManager, main, Makefile, config).*

### 13.1 Gates

| Gate | Result |
|------|--------|
| `make all`, `make img` | PASS. No compiler warnings in project sources; the only warnings are the existing newlib syscall stubs (`_close`, `_lseek`, `_read`, `_write`) and the LTO serial-compilation note. |
| Link | `text=603,896`, `data=424`, `bss=430,120`; flash 604,320 / 753,664 B (**149,344 B free**). ITCM 4,168 B; DTCM statics 4,472 B; FXBUF margin 3,712 B (unchanged). |
| Host test `make -C tools/midi_test run` | `PASS (0 failures)` (§4.2 examples, N5, G off, 200,000 random settings, Q4 inverse). |
| Static asserts | `sizeof(MidiMsg) == 6`, `sizeof(struct MidiBits) == 1` (`MidiIn.c`). |
| Hardware | Not run (verification lists A-25, B-13, C-5, D-9, E-7, F-10, G-9 remain). |

### 13.2 RAM as linked (SRAM1, static)

| Item | Planned | Linked | Note |
|------|--------:|-------:|------|
| `midiLearn_table` | 512 | 512 | + count/state/target/deadline 6 B |
| `bh_overlay` (per-track table) | 56 (R-A) | 56 | |
| `midiControl_nrpn` | 48 | 48 | |
| `midiOut_nrpnKey` + `_pending` + `_lastSendMs` | 8 | 8 | |
| `midiOut_trackNote` + `_trackChannel` | 14 (R-B) | 14 | |
| `midiDin_rx` (bytes + stamps) | 194 | 194 | |
| `usb_MidiMessages` | 768 | 768 | |
| `midiIn_dinMsg` | 6 (R-D) | 6 | |
| `seq_resumeTick` | 4 (R-C) | 4 | |
| `seq_sceneChangeFromMidi`, `midiNotes_action` | 1 + 1 | 1 + 1 | |

Net `bss` versus the S082 close: **−808 B** (the plan expected about +220 B).
The difference is the old USB ring: the pre-S083 `MidiMsg` was **8 B**,
not 4 B (its `enum` bitfield takes an `int` container on this ABI;
measured with the old struct definition on `arm-none-eabi-gcc -mcpu=cortex-m7`),
so the old ring was 256 × 8 = **2,048 B** and Q13 (b) released 1,280 B,
not 256 B. For the same reason R-D is −2 B (the old `midiMsg_tmp` was
8 B), not +2 B. The implementer's G+ note found the same packing effect in
the new struct and fixed it with `uint8_t` bitfields. See finding D-1 for
the documents that still say 1,024 B.

### 13.3 Conformance with the schedule

Every scheduled step is present: A-1..A-24, B-1..B-11, C-1..C-4,
D-1..D-7, E-1..E-5, F-1..F-8, G-1..G-8 and the Phase H document list.
Deviations found:

| Step | Deviation | Verdict |
|------|-----------|---------|
| A-2 | `MidiBits` uses `uint8_t` bitfields (6 B `MidiMsg`). | Correct; better than the schedule (§12 G+). |
| F-6b | The Learn arm test is the first statement of `case BUT_REC:`, before the SHIFT+REC-menu and live-erase tests (the schedule put it after the erase test). | Accepted. While erase is on, the erase router consumes PLAY before the chord can arm, so the order cannot change behaviour. |
| G-4 | `midiOut_trackProgramChange()` maps channels with the target Scene's settings, not the active Scene's. | Accepted, consistent with C-6 (a track sends on the channel of the Scene it plays). Its comment is wrong (finding D-2). |
| G-7 | The TX hook is skipped by the early-return edit paths (LFO/velocity target cells, roll rhythm, held-step overlays). | Accepted: those are not plain parameter values. |

### 13.4 Findings

Ordered by severity. "Fix" is a proposal; nothing was changed.

**F-1 (medium) — Continue replays the last tick played before Stop.**
`seq_setRunningEx()` stores `seq_resumeTick = seq_elapsedPpqTicks` (the
tick already processed), and `seq_continueRunning()` sets
`seq_elapsedPpqTicks = resume − 1`, so the first scheduler tick after
Continue processes that same tick again. When a step boundary (master or a
track's DDA crossing) fell on that tick, its trigger and its step
automation fire twice: an audible double hit on resume. SPP is not
affected (`pos × 24` is the next position, as intended).
*Fix:* at stop store the next tick:
`seq_resumeTick = seq_initialSchedulerTick ? 0u : seq_elapsedPpqTicks + 1u;`
(`sequencer.c`, stop branch of `seq_setRunningEx()`). Verify with C-5 item 1
on a pattern whose steps fall on every 1/16.

**F-2 (low-medium) — Track note record can be overwritten across TIM3 and
the foreground.** `midiOut_trackNoteOn()` sends the note-off, the note-on,
and only then records note and channel under PRIMASK. If TIM3 (sequencer)
and the foreground (MIDI-in, overlay, preview) trigger the same track at
the same moment, the preempted caller writes its record last and the
other note is never turned off downstream.
*Fix:* hold one PRIMASK section across off + send + record in
`midiOut_trackNoteOn()` and across read + clear + send in
`midiOut_trackNoteOff()` (both drivers already nest PRIMASK; the section is
a few µs: at most 2 DIN queue inserts and 2 USB packet writes).

**F-3 (low) — Turning the BAR overlay off stops MIDI rolls although CC2
still selects an overlay.** `buttonHandler_overlaySwitchHeld()` calls
`buttonHandler_overlayReconfigure(bh_overlayActive, 0u)`; with
`bh_overlayActive == 0` it stops every entry, MIDI-held ones included. By
C-9 a BAR only wins while active; once it is off, MIDI-held entries should
follow CC2.
*Fix:* when `bh_overlayActive == 0`, reconfigure panel entries with 0 and
MIDI-held entries with `midiNotes_noteAction()` (add the accessor call;
`MidiNotes.h` already exposes it).

**F-4 (low) — Parameter TX can carry a data byte above 127.**
`midiControl_targetValue7()` returns the raw Instrument byte, and the
target-selection descriptors (`DTYPE_TARGET_SELECTION_*`,
`DTYPE_VOICE_LFO`) hold tokens up to 0xFF. `midiOut_cc()` does not mask,
and a byte ≥ 0x80 on the wire is a status byte (0xFF is System Reset).
Today no panel path reaches it (those cells return before the TX hook),
but a CC96/97 step on such a target reads 0xFF and a later caller could
send it.
*Fix:* clamp to 127 at the end of `midiControl_targetValue7()` and mask
`data1`/`data2` with 0x7F in `midiOut_cc()`.

**F-5 (low) — Learn is not cancelled by a MIDI Bank load started while
already on Load/Save.** The cancel lives in `menu_switchPage()` and runs
only on a page change; `menu_backgroundBankLoadUiBegin()` does not switch
pages when Load/Save is already shown. Learn can be armed there (the
gesture works in every mode), so it stays waiting through the load.
*Fix:* call `midiLearn_cancel()` at the start of the MIDI-origin UI begin
path.

**F-6 (low) — Stamp read after the read index is published.**
`midiBuffer_rxOut()` advances `read` and then reads `stamp[slot]`; with the
buffer full, the USART ISR can write a new stamp into that slot first. The
result is a wrong age (bounded by `MIDI_ARRIVAL_MAX_AGE_US`), never a wrong
byte.
*Fix:* read `stamp[read]` before `midiBuffer_out()`.

**F-7 (low, pre-existing pattern) — A per-track Scene change of T6 or T7
alone.** `seq_setTrackPlayedScene()` moves the HiHat pair together, but
`seq_assignTracksToScene()` realigns and ends the MIDI note only for the
tracks in the mask. A voice-channel PC that maps only T7 (or a panel hold
of only one HiHat VOICE) leaves the partner track unaligned until its next
realign. The panel loop behaved the same before S083.
*Fix:* when bit 5 or 6 is set, set both bits in the mask at the start of
`seq_assignTracksToScene()`.

**D-1 (documentation) — Old USB ring size.** The plan (§2 table row
"Arrival time", §6 Q13 row, §12 Q13 text), `MIDI_SPEC.md` §13 and the
comment in `usb_midi_core.h:60` say the old ring was 256 × 4 B = 1,024 B
and the saving 256 B; it was 256 × 8 B = 2,048 B and the saving is
1,280 B (§13.2). `STORAGE_SRAM_MANIFEST.md` should record the measured
values.

**D-2 (documentation) — `midiOut_trackProgramChange()` comment.** It says
the panel and the MIDI assignment helpers both call it after
`seq_assignTracksToScene()`. Only the panel does (`buttonHandler.c:1484`),
which is correct: a MIDI per-track PC must not be echoed (Q9).
`MidiProgram.c` correctly does not call it.

### 13.5 Observations for hardware testing

- **Filters default to off.** `filesystem_resetSettingsToDefaults()` sets
  `midi_filt_rx` and `midi_filt_tx` to 0. On a card without these keys,
  the LXR consumes nothing and sends nothing (everything is forwarded per
  MRT, which also defaults to `off`). Set RXF and TXF to `all` before the
  hardware matrices. This default predates S083; changing it is a product
  decision.
- **SHIFT+PLAY no longer toggles the transport in any mode** (MIDI_SPEC
  §6.3, F-6a). Expect this during the S080/S081 regression rows.
- **A MIDI flam ignores its note-off; a MIDI roll ends at it**; MIDI-held
  rolls are not VOICE holds for the looper (B-12).
- **First hardware order:** A-25 (clock, MRT matrix, settings display),
  then B-13 with the host-test examples on DIN and USB, then C-5 after
  F-1 is fixed.

### 13.6 Verdict

The implementation follows the schedule closely and builds clean; the
module layout, consume-then-route, the note map, the shared menu commit
(Q1), Learn and the transmit rules are all present and read correctly. Fix
F-1 before testing Continue on hardware. F-2 to F-7 are small, local fixes
that can go in one pass with D-1/D-2. No finding needs new RAM.

---

## 14. Fixes after the assessment (2026-10-21)

*Targeted fixes for every §13.4 finding, with the comment blocks beside
each change amended to describe the new behaviour. No new RAM.*

| Finding | File(s) | Change | Comment blocks amended |
|---------|---------|--------|------------------------|
| **F-1** Continue replayed the last tick | `sequencer.c` `seq_setRunningEx()` stop branch | `seq_resumeTick = seq_initialSchedulerTick ? 0 : seq_elapsedPpqTicks + 1` (the first unplayed tick; 0 when no tick was played). `seq_continueRunning()` is unchanged: it places the clock one tick before `resume`, so the next tick is the first unplayed one and nothing is processed twice. SPP already stored the next position. | `seq_resumeTick` state block; the stop-branch block; `seq_setRunningEx()` outputs; `seq_continueRunning()` what. |
| **F-2** Note record race (TIM3 vs foreground) | `MidiOut.c/h` | New static `midiOut_trackNoteOffLocked()` (read, clear, send under the caller's PRIMASK). `midiOut_trackNoteOff()` holds one PRIMASK section per track; `midiOut_trackNoteOn()` holds one section across the previous note-off, the note-on and the record update. The note-on velocity is masked to 7 bits. | Per-track record block (writers); new helper block; both function notes; `MidiOut.h` contracts of `midiOut_trackNoteOn/Off()`. |
| **F-3** BAR overlay off stopped MIDI rolls | `buttonHandler.c/h`, `MidiNotes.c` | `buttonHandler_overlayReconfigure()` takes a selector (`BH_OVERLAY_SEL_ALL`, `_MIDI`, `_PANEL`). `buttonHandler_overlaySwitchHeld()`: when the BAR overlay turns off and CC2 selects an overlay, panel entries stop and MIDI-held entries take the CC2 overlay; otherwise every entry takes the BAR state (BAR active wins, C-9). CC2 changes and the event-ring overflow use `_MIDI` and `_ALL`. `buttonHandler.c` includes `MidiNotes.h` for `midiNotes_noteAction()`. | Reconfigure block (selector, callers); new `buttonHandler_overlaySwitchHeld()` block; `buttonHandler.h` overlay API block ("active", F-3); the C-9 line in `midiNotes_handle()`. |
| **F-4** Data bytes above 127 on parameter TX | `MidiControl.c/h`, `MidiOut.c` | `midiControl_targetValue7()` clamps Instrument bytes to 127 (target-selection tokens reach 0xFF). `midiOut_cc()` masks channel to 4 bits and both data bytes to 7 bits. | Inline block in `midiControl_targetValue7()`; `MidiControl.h` contract ("always 0..127"); new `midiOut_cc()` block. |
| **F-5** Learn not cancelled by a MIDI Bank load on Load/Save | `menu.c` | `menu_backgroundBankLoadUiBegin()` calls `midiLearn_cancel()` first. | Its contract (cancel, why, affiliate); the Learn-cancel block in `menu_switchPage()` (points to the new site). |
| **F-6** Stamp read after the slot was released | `MidiBuffer.c/h` | `midiBuffer_rxOut()` reads the stamp before `midiBuffer_out()` publishes the read index (empty check first). | New block above `midiBuffer_rxOut()`; the `MidiBuffer.h` contract ("byte and stamp"). |
| **F-7** HiHat pair moved but only one track realigned | `sequencer.c/h`, `MidiOut.c/h` | `seq_assignTracksToScene()` and `midiOut_trackProgramChange()` set both bits 5 and 6 when either is set, so both tracks are realigned, have their MIDI note ended and are counted for the per-track PC. | `seq_assignTracksToScene()` contract and its `sequencer.h` line; `midiOut_trackProgramChange()` inline block and `MidiOut.h` contract. |
| **D-1** Old USB ring size | `usb_midi_core.h`, `STORAGE_SRAM_MANIFEST.md` (ledger row), `MIDI_SPEC.md` §13, plan (§2 row, §6 row, §12 Q13 text and table) | 256 × 8 B = 2,048 B before S083 (measured); Q13 (b) released 1,280 B. The manifest's ledger row now reads 768 B. | The USB ring block in `usb_midi_core.h`. |
| **D-2** Wrong caller list | `MidiOut.c` | `midiOut_trackProgramChange()` comment: panel only; a received per-track PC is not echoed (Q9). | As stated. |

### 14.1 Gates after the fixes

| Gate | Result |
|------|--------|
| `make all`, `make img` | PASS. Image copied to `build/LXRV2_lxr02.img`. |
| Warnings | None in the files S083 touched. A full rebuild of `filesystem.c` (it changed in Phase F) shows seven `-Wunused-function` warnings for older helpers (`filesystem_applyLegacy22Globals`, `filesystem_blockRename`, `filesystem_reportBootSubstep`, `filesystem_bootLoggingSetKitDetail`, `filesystem_applyStaleGlobalsFallback`, `filesystem_staleMetaPrefixLen`, `filesystem_metaHasStoredGlobalsLen`). They predate S083: each name has the same reference count at `HEAD`. This corrects §13.1, which saw no warnings only because `filesystem.o` was not rebuilt there. Left as is (outside S083's scope). |
| Link | `text=604,200`, `data=424`, `bss=430,112`; flash 604,624 / 753,664 B (**149,040 B free**). Versus §13.1: text +304 B, bss −8 B (layout/alignment; no new statics). |
| Host test | `make -C tools/midi_test run` → `PASS (0 failures)`. |

### 14.2 Hardware checks added by the fixes

- **F-1:** C-5 item 1 with a pattern that has a step on every 1/16: Stop
  and Continue repeatedly; no step plays twice at the resume point.
- **F-2:** play a track from a DAW (MIDI notes) while the sequencer also
  triggers it; a downstream monitor shows every note-on paired with a
  note-off.
- **F-3:** CC2 = 1, hold MIDI notes on two tracks with a roll type, toggle
  the BAR `>` overlay on and off in PERF: with it on, the rolls follow `>`;
  with it off, they return to `<` (CC2) and keep rolling.
- **F-5:** arm Learn on the Load/Save page, send CC0: Learn ends (LEDs stop
  flashing) and the load runs.
- **F-7:** per-track PC (or hold one HiHat VOICE + SEQ) that maps only T7:
  T6 and T7 both move, realigned.

---

## 15. Follow-up: track `ch` / `nte` lists and the `rmp` label (2026-10-22)

*User corrections after §14. The STEP track settings second page holds
`ch`, `nte`, `lop`, `asm`. Requested: `ch` gains `off` below `glb`; `nte`
moves to category Track, gains `off` below `def`, and `def` is relabelled
`on` (still the default); `asm` becomes `rmp` with category `Automtn` and
long name `Ramp`. No new RAM.*

### 15.1 Meaning chosen for the new `off` values

The request named the values but not their effect. Applied as follows;
correct me if a different effect was meant.

| Setting | Effect |
|---------|--------|
| `ch off` | The track has no MIDI channel at all: no notes in or out, no voice-channel CC1/NRPN/Learn/PC, no per-track PC out. (`glb` = follow G, as before.) |
| `nte on` (default, was `def`) | No root: the track shares its channel's notes (stripe on G, split elsewhere). Unchanged behaviour. |
| `nte off` | The track is left out of the note map: no notes received, none sent. It keeps its channel for CC1/NRPN/Learn/PC. |

### 15.2 Storage and menu order

The defaults stay stored as 0, so no default changes:

| Setting | Stored (Scene, `sceneset.scg`, AutoSave) | Menu order |
|---------|-------------------------------------------|------------|
| Track `ch` | 0 `glb`, 1..16, **17 `off`** (`MIDI_TRACK_CHANNEL_OFF`) | 0 `off`, 1 `glb`, 2..17 = 1..16 |
| Track `nte` | 0 `on`, 1..127, **128 `off`** (`MIDI_TRACK_NOTE_OFF`) | 0 `off`, 1 `on`, 2..128 = notes 1..127 |

`menu.c` converts between the two orders (`menu_trackMidiChan/NoteToMenu()`,
`…FromMenu()`).

### 15.3 Changes

| File | Change |
|------|--------|
| `MidiDefs.h` | ADD `MIDI_TRACK_CHANNEL_OFF` (17), `MIDI_TRACK_NOTE_OFF` (128) with a contract block. |
| `SceneData.c/h` | Setters clamp to 0..17 / 0..128 (above clamps to `off`); getters return `off` values; contract blocks updated. |
| `storageTypes.c` | `midi_channel` parses 0..17, `midi_note` 0..128 (includes `MidiDefs.h`). |
| `MidiNoteMap.c/h` | `effectiveChannel()` returns 0 for `ch off` (not G). New static `midiNoteMap_noteTracksOnChannel()` drops `nte off` tracks; `resolve()` and `inverse()` use it, and `inverse()` refuses a `nte off` track. `tracksOnChannel()` still includes `nte off` tracks (CC/NRPN/PC). Host-test-local copies of the two values with a firmware `_Static_assert`. Header and settings-struct blocks updated. |
| `tools/midi_test/notemap_test.c` | New cases: `ch off` leaves the stripe and has no channel; `nte off` leaves the stripe, keeps its channel, sends nothing, and alone on a channel is unmapped. The random settings include both `off` values; a new invariant checks a `nte off` track never resolves. |
| `menu.c` | `menu_getParameterDisplayValue()` reads track `ch`/`nte` from the viewed Scene in menu order (fixes a pre-existing staleness: their `parameter_values[]` mirror was never refreshed from the Scene). Clamp: G 0..16, track `ch` 0..17, track `nte` 0..128. `menu_getMidiStaticName()` names the full lists (`off`/`glb`/channel; `off`/`on`/note). `menu_parseGlobalParam()` converts menu → stored, and also ends the track's MIDI note on a `nte` change. Prototypes added before first use. |
| `menu.h`, `menu.c` (`valueNames`), `menuPages.h` | New `TEXT_TRACK_NOTE` (`nte`, category Track) for the STEP track page; the step-special note keeps `TEXT_NOTE` (category Step). New `CAT_AUTOMATION`; `TEXT_TRACK_AUTO_SMOOTH` uses it. |
| `MenuText.h` | Short name `asm` → `rmp`; long name `Automtn` → `Ramp`; category list gains `Automtn`; `menuText_def` removed (unused). |
| Comments | Every comment naming the cell `asm` now says `rmp` (`PatternData.c/h`, `ParameterArray.h`, `menu.c`, `menuPages.h`, `sequencer.c/h`). Code and storage identifiers (`auto_smooth`, the PAT4 byte) keep their names. |
| Specs | `MIDI_SPEC.md` §2.2 (lists, stored values, effects) and §4.1 (`off` tracks); `FILESYSTEM_SPEC.md` `sceneset.scg` rows 4/5; `MODULE_INTERCHANGE_SPEC.md` SEQ_PAGE row; `PATTERN_DYNAMIC_STACK.md` §6.2b heading and label; `MEMORY.md` binding note under MIDI / Clock Reminders. |

### 15.4 Gates

| Gate | Result |
|------|--------|
| `make all`, `make img` | PASS. New warnings: none. Rebuilt units also show the older `PatternData.c:201` packed-member and `EuklidGenerator.c:54` sign-compare warnings (lines not touched; they show because header comment edits forced a recompile) and the §14.1 `filesystem.c` unused-function set. |
| Link | `text=605,784`, `data=424`, `bss=430,112`; flash 606,208 / 753,664 B (**147,456 B free**). Text +1,584 B versus §14 (menu list formatting and conversions, note-map changes); bss unchanged. |
| Host test | `PASS (0 failures)`. |

### 15.5 Hardware checks

1. STEP track settings second page reads `ch nte lop rmp`. Turning `ch`
   down past `glb` gives `off`; `nte` lists `off`, `on`, then note names; the
   full views read "Voice Channel", "Track Note" and "Automtn Ramp".
2. Change track, change viewed Scene: `ch`/`nte` show that track's stored
   setting (staleness fix).
3. `ch off` on T3: notes on G skip T3 (the stripe has six tracks), T3 sends
   no notes, CC1/NRPN on any channel do not reach slot 3 through a voice
   channel.
4. `nte off` on T3 with `ch` 5: notes on ch 5 do nothing for T3 (forwarded),
   T3 sends no notes, NRPN for slot 3 on ch 5 works, a PC on ch 5 moves T3.
5. Settings survive reboot (`sceneset.scg` holds 17/128 for `off`).

## 16. Follow-up: MIDI Learn persistence in `midilrn.cfg` (2026-10-22)

**Problem.** Learn assignments did not survive a reboot.

**Root cause.** `midiLearn_formatSettingsLine()` returned `1` instead of the
line length. `filesystem_writeTextLine()` writes exactly the returned number
of bytes, so every learn row reached `settings.cfg` as the single character
`l`. On load, the first `l` line (a line without `=`) was a parse error. The
24 fixed lines before it had already loaded, but the load stopped there and
ended with ERROR, so no row was restored. This was a defect in the
schedule's F-3 text.

**User decision.** Learn rows move to their own root file, `midilrn.cfg`. It
is safely rewritten after **every** Learn, so `settings.cfg` is no longer
rewritten for Learn. The seven follow-up decisions were all accepted
("yes to all"):

1. **Format:** `format=helicase.midilearn`, `version=1`, the existing
   `learn=` rows, then `lines=N`, where N counts every line before it
   (2 + rows). The file can be edited by hand.
2. **`settings.cfg`:** returns to 24 fixed lines plus `lines=24`. An old
   `learn=` line is an unknown key and is ignored (no migration).
3. **Safe write:** the `settings.cfg` method: `midilrn.tmp`, sync, remove
   the old file, rename. At boot, a leftover tmp is checked by its
   terminator, then promoted or discarded. Both files are in the card root.
4. **Write timing:** immediate, with no debounce, whenever a row is stored or
   replaced. `FUL` writes nothing.
   - The write waits for an idle facade (AutoSave, a Load/Save command, a
     background Bank load).
   - A Learn made during a write queues one more write.
   - Learns made during an unlocked Bank load are kept.
5. **Boot:** the load runs right after `settings.cfg`, before audio.
   - A missing file means an empty table.
   - Malformed rows are skipped.
   - A settings load no longer clears the table.
6. **RAM:** 1 B request flag, SRAM1, static, owned by the filesystem.
7. **Trace:** a DEV_MODE_LOGGING record (stage `m`), in logging builds only.

### 16.1 Changes

| File | Change |
|------|--------|
| `Core/MIDI/MidiLearn.c` | **Fix:** both returns of the line formatter now give the line length (`(uint8_t)pos`), both for a real row and for the `#` comment fallback. Renamed `midiLearn_formatSettingsLine()` → `midiLearn_formatFileLine()` and `midiLearn_parseSettingsValue()` → `midiLearn_parseFileValue()`. `midiLearn_captureCc()` calls `filesystem_requestMidiLearnWrite()` instead of `filesystem_markSettingsDirty()`; the `FUL` path still writes nothing. |
| `Core/MIDI/MidiLearn.h` | Module block and the format/parse contract now describe `midilrn.cfg`: the return value is the length, plus the callers. `midiLearn_clearAll()` comment: the start of the boot load. |
| `Core/Hardware/SD/storageTypes.h` | ADD `STORAGE_MIDILEARN_FILENAME` `"midilrn.cfg"` and `STORAGE_MIDILEARN_TEMP_FILENAME` `"midilrn.tmp"`, with a contract block. |
| `Core/Hardware/SD/filesystem.h` | ADD `filesystem_requestMidiLearnWrite()` and `filesystem_requestLoadMidiLearn(cb)`, with a contract block (gates, callers, outputs). |
| `filesystem.c`: `settings.cfg` revert | REMOVE `FS_SETTINGS_FIXED_LINES`, the learn block in `filesystem_nextSettingsLine()`, the `learn` branch in `filesystem_parseSettingsLine()` (now a comment: ignored as an unknown key) and `midiLearn_clearAll()` in `filesystem_resetSettingsToDefaults()`. Restore `case 24u` → `lines=24`; its comment says Learn rows are in `midilrn.cfg`. |
| `filesystem.c`: ops | ADD `FS_INTERNAL_OP_LOAD_MIDILEARN` and `FS_INTERNAL_OP_SAVE_MIDILEARN`, appended at the end of `fs_internal_op_t` so trace op numbers stay stable. Boot code `MLRNLOAD`, error names `MlrL`/`MlrS`. Dispatch cases in the op switch. |
| `filesystem.c`: state | ADD `fs_midilearn_write` (1 B: bit 0 PENDING, bit 1 RETRIED) with a contract block. It is cleared in the facade reset and in `filesystem_initAfterCardReady()`. The `op_settings_recovery_*` comment records that the Learn load reuses those 4 B. |
| `filesystem.c`: parser | ADD `filesystem_parseMidiLearnLine()`. Blank lines, `#` comments, lines without `=`, `lines=`, unknown keys and malformed `learn=` rows return DONE (skipped). Only a wrong `format`/`version` returns ERROR. |
| `filesystem.c`: line generator | ADD `filesystem_nextMidiLearnLine()`: header, rows, `lines=2+rows` (at most 131 lines). |
| `filesystem.c`: load | ADD `filesystem_loadMidiLearn_tick()`. Pass A (0-8) mirrors the `settings.tmp` prelude for `midilrn.tmp`. Pass B (10-14) clears the table and reads `midilrn.cfg`. The load always finishes DONE: a missing file is an empty table, a format/version error empties the table and stops, and a read error keeps the rows read so far. |
| `filesystem.c`: save | ADD `filesystem_saveMidiLearn_tick()`: the `saveGlobals` safe-write phases 0-10 on the Learn files. Its stall observer is shared with the settings writer (code `MlWr`, site 8). |
| `filesystem.c`: scheduler | ADD `filesystem_midiLearnWriterSchedule_tick()` and `filesystem_midiLearnWriterCompleted()`. The scheduler starts on PENDING plus the boot gate (`fs_settings_runtime_ready`), a READY volume and no active Load/Save command. PENDING is cleared at the start of a write, so a Learn during the write queues exactly one more. On the first error the completion requeues once (PENDING\|RETRIED); after that it waits for the next Learn, so a failing card cannot loop. It then acks privately. `filesystem_tick()` runs it before the settings writer, under the same idle and copy/clear guard. ADD the request functions next to `filesystem_requestScanKits()`. |
| `main.c` | After the settings load and `filesystem_setAutosaveEnabled()`: `filesystem_requestLoadMidiLearn(NULL)`, poll while BUSY under the boot-logging timeout, then ack. |
| `Core/Bank/Scene/AutosaveTrace.h` | ADD stage `m` `AUTOSAVE_TRACE_STAGE_MIDI_LEARN_FILE` with events `WRITE` 0x01, `RECOVERY` 0x02 and `LOAD` 0x03 (layouts in the block). |
| `tools/decode_devlogs.py` | Stage `m` name, producer and decoder branch; boot code `MLRNLOAD`; the two new op names. **Also fixed** a pre-existing drift: `FS_INTERNAL_OP_PATTERN_TRACE_FLUSH` was missing from the op-name list, so every op from `AUTOSAVE_PATTERN_DRAIN` onward decoded as its neighbour. The list now matches the enum (54 entries, checked by script). |
| Specs | `FILESYSTEM_SPEC.md`: root file lists, `settings.cfg` schema back to 24 + `lines=24`, the new "`midilrn.cfg`" section, and the copy/clear suspension list. `MIDI_SPEC.md`: §6.1, §6.3 step 3, §6.4 rewritten for `midilrn.cfg`, §11 and the related-documents/as-built rows. `DEV_MODES.md`: stage `m` table and the `MlWr` stall row. `STORAGE_SRAM_MANIFEST.md`: +1 B entry. `MODULE_INTERCHANGE_SPEC.md`: MidiLearn row. `MEMORY.md`: tree line and the MIDI Learn reminder. |

### 16.2 Gates

| Gate | Result |
|------|--------|
| `make all`, `make img` | PASS. New warnings: none; only the §14.1 `filesystem.c` unused-function set remains. `DEV_MODE_LOGGING` and `DEV_STALL_DETECTION` are both 1 in `config.h`, so the trace and stall paths were compiled. |
| Link | `text=608,424`, `data=424`, `bss=430,112`; flash 608,848 / 753,664 B (**144,816 B free**). Text is +2,640 B versus §15. bss is unchanged: the 1 B flag fits in alignment padding. |
| Host test | `PASS (0 failures)`. The Learn file path is firmware-only; no host test covers it. |

### 16.3 Hardware checks

1. **First Learn on a clean card.**
   - Steps: Learn a CC, then look at the card root.
   - Expect: `midilrn.cfg` with the header, one `learn=` row and `lines=3`;
     no `midilrn.tmp`. A trace `m` WRITE record shows rows=1, ok, pending=0.
2. **Survives reboot.**
   - Steps: power-cycle.
   - Expect: the CC moves its parameter with no new Learn. A trace `m` LOAD
     record shows present=1, result ok and the row count.
3. **Re-learn and macros.**
   - Steps: re-learn the same parameter to another CC, and learn one CC to
     a second parameter.
   - Expect: each Learn rewrites the file (one WRITE record per Learn), and
     the file shows the replaced CC and the new row.
4. **Learn during a busy card.**
   - Steps: Learn while AutoSave or a background Bank load is running.
   - Expect: the write happens after it finishes. Two quick Learns can show
     a WRITE with pending=1, followed by a second WRITE.
5. **`FUL`.**
   - Steps: with 128 rows, Learn a new parameter.
   - Expect: `FUL` is shown and no WRITE record appears.
6. **`settings.cfg` is clean.**
   - Steps: make a settings change and check the file.
   - Expect: 24 data lines and `lines=24`. Old `l`/`learn=` lines from the
     first S083 build are gone after the next settings write.
7. **Recovery.**
   - Steps: copy a valid `midilrn.cfg` to `midilrn.tmp`, change one row, and
     boot.
   - Expect: the tmp is promoted (RECOVERY outcome 1) and its rows load.
   - Then: truncate the tmp's `lines=` line and boot.
   - Expect: it is discarded (outcome 2) and the old file loads.
8. **Hand edits.**
   - Steps: delete a row, then add a row with an unknown key.
   - Expect: both load without error, and the bad row is skipped.
   - Steps: set `format=` to something else.
   - Expect: the table is empty (LOAD result 1).

## 17. Follow-up: FX Morph from global Morph, `pv<`/`pv>`, CC2 sets the BAR overlay (2026-10-22)

The user reported three problems:

1. FX Morph does not follow the global Morph. Global Morph sets all Morph,
   and like the voice Morphs, the FX Morph it sets is not recorded.
2. SHIFT+REC needs `pv<` and `pv>`: the velocity rhythm of the overlay roll,
   with the same list as the step special (`1-2`, `1-3`, …). Its second page
   becomes `pv< pv> qnt rtk`.
3. Check the MIDI assignment for the PERF roll/trigger overlay, which should
   also update the LEDs. The user later confirmed: CC2, on G only, for both
   the trigger and the overlay change.

### 17.1 Findings

- **FX Morph.** `preset_morphScene()` wrote only the stored byte
  (`scene_setEffectMorphAmount()`). Three things kept the old amount:
  - it never cleared a Pattern `fxm` override (S076 Rule A);
  - it never set the menu hold, so an FX Morph lock step still won;
  - it never refreshed the `PAR_EFFECT_MORPH` mirror or the `fxm` display.

  The panel `mrp` edit and MIDI CC1 on G both go through this function.
- **CC2.** CC2 is the assignment (MIDI_SPEC §5.3: on G, 0 single, 1 BAR `<`,
  2 BAR `>`). As built, it kept its own state in `MidiNotes` that only shaped
  received MIDI notes. It never changed `bh_overlayActive`, so the panel
  VOICE overlay and the BAR LEDs did not follow, and a BAR overlay in PERF
  overrode it (C-9).

### 17.2 Changes

| File | Change |
|------|--------|
| `EffectsManager.c/h` | ADD `effects_setMorphAmountFromGlobal(scene, amount)` (contract block). It sets one Scene, with no fan-out because the caller already fans out per Scene. Through `effects_setMorphAmountScene()` it stores the amount and clears the `fxm` override; on the active Scene it also sets `menuHoldMorph`, the `fxm` menu-edit rule. It never records. |
| `presetManager.c` | `preset_morphScene()` calls it instead of the bare `scene_setEffectMorphAmount()`. For the active Scene it also calls `preset_syncEffectMorphMirror()`. Bulk Global apply starts at `PAR_BEGINNING_OF_GLOBALS` (135) and `PAR_MORPH` is 78, so a settings load never reaches this path. |
| `menu.c` | `menu_commitGlobalMorphFromMidi()` also repaints `fxm` (`sceneModTarget_effectMorphId()`). |
| `buttonHandler.c/h` | One overlay state. ADD `buttonHandler_overlayActive()` (any mode) and `buttonHandler_setOverlay(which)`. `setOverlay` makes the same change as a BAR press: state, BAR LEDs (in PERF while the looper does not own them) and held entries. REMOVE `buttonHandler_overlayHeldBar()`, `buttonHandler_overlayMidiActionChanged()` and the `BH_OVERLAY_SEL_*` split: `buttonHandler_overlayReconfigure(which)` now re-configures every held entry, and `buttonHandler_overlaySwitchHeld()` lost its CC2 branch. Comment blocks updated. |
| `MidiNotes.c/h` | REMOVE `midiNotes_action` (−1 B) and `midiNotes_noteAction()`. `midiNotes_setNoteAction()` calls `buttonHandler_setOverlay()`. Notes use `buttonHandler_overlayActive()` in any mode. The G-only check stays in `midiControl_handleCc()`. |
| `sequencer.c/h` | ADD the pure `seq_rollRhythmVelocity(rhythm, vel, pos)`, the former body of `seq_rollComputeVelocity()`, which now calls it. One rule for step and overlay rolls. |
| `ParameterArray.h` | ADD `PAR_PERF_VEL_LT`/`PAR_PERF_VEL_RT`, appended after `PAR_TRACK_AUTO_SMOOTH` so no id is renumbered, plus a `_Static_assert`. |
| `menu.h`, `MenuText.h`, `menu.c` | ADD `TEXT_`/`SHORT_`/`LONG_PERF_VEL_LT/RT`: `pv<`/`pv>`, Prfrmnc, `Rhythm<`/`Rhythm>`. Dtype `DTYPE_0B255`. ADD `menu_paramIsRollRhythm()` (step `rtm` + `pv<`/`pv>`). The six `PAR_STEP_RHYTHM` cell tests (two value-name paths, clamp, full view, encoder, pot) use it, so the new cells show `off`/`1-2`… and step the same ordered list. |
| `menuPages.h` | RECORDING_PAGE sub-page 1 is `pv< pv> qnt rtk`; sub-page 2 (formerly `rtk`) is reserved. The generic cross-sub-page step needs no change. |
| `buttonHandler.c` (roll) | The overlay entry field `vel` (always 100) became `pos`, the roll hit position, so there is no RAM change. Roll repeats play `seq_rollRhythmVelocity(pv of bh_overlayActive, 100, pos)`. Hit 0 is always 100. A change from single/flam to roll continues at position 1. Flam main hits stay at 100. `pos` saturation wraps 255 → 16 (256 mod 60), keeping the phase for cycles 2..5. |
| `filesystem.c` | `settings.cfg` keys `perf_vel_lt`/`perf_vel_rt` (cases 24/25, defaults 0, parser keys); terminator case 26 → `lines=26`. A RECORDING_PAGE edit already marks settings dirty. |
| Specs | MIDI_SPEC: §4.3, §5.2 (global Morph sets FX Morph, never recorded), §5.3 rewritten (CC2 sets the BAR overlay; C-9 retired), §10 RAM, §11, the as-built row. MODULE_INTERCHANGE_SPEC: overlay entry points, MidiNotes row, RECORDING_PAGE row, settings keys, overlay table fields. FILESYSTEM_SPEC: 26 lines + `lines=26`. STORAGE_SRAM_MANIFEST: −1 B entry. MEMORY.md: CC2, global Morph and SHIFT+REC notes. |

§16.3 check 6 now expects 26 data lines and `lines=26`.

### 17.3 Gates

| Gate | Result |
|------|--------|
| `make all`, `make img` | PASS. No new warnings. Rebuilt units show the older `PatternData.c:201` and `EuklidGenerator.c:54` warnings, recompiled because of the `ParameterArray.h` edit. |
| Link | `text=608,744`, `data=424`, `bss=430,112`. Text is +320 B versus §16; bss is unchanged (−1 B absorbed by padding). |
| Host test | `PASS (0 failures)`. |

### 17.4 Hardware checks

1. **FX Morph follows global Morph.**
   - Steps: on an Effect with a Morph image, turn PERF `mrp` (and send CC1
     on G).
   - Expect: the Effect sound and `fxm` follow. With `fxm` automation in
     the Pattern, `mrp` still moves FX Morph until the next `fxm` step.
   - Steps: record with REC latched.
   - Expect: no `fxm` (and no `1vm`..`6vm`) lane is written by `mrp`.
2. **CC2.**
   - Steps: in PERF, send CC2 = 1, then 2, then 0 on G.
   - Expect: the BAR `<` LED, then `>`, then neither. VOICE buttons fire the
     overlay and MIDI notes perform it.
   - Steps: press BAR after a CC2.
   - Expect: the last action wins.
   - Steps: send CC2 on a non-G channel.
   - Expect: it is ignored and forwarded.
   - Steps: send CC2 outside PERF, then enter PERF.
   - Expect: the LEDs show the state. While the looper is engaged, the LEDs
     stay the looper's.
3. **`pv<`/`pv>`.**
   - Steps: SHIFT+REC, page 2.
   - Expect: `pv< pv> qnt rtk`.
   - Steps: set `pb<` = `r16`, `pv<` = `1-2`, and hold a VOICE in PERF with
     `<` on.
   - Expect: roll velocities alternate 100 and 50. `1+3` gives 100, 127, 127.
   - Steps: MIDI-held rolls.
   - Expect: they use it too.
   - Steps: reboot.
   - Expect: `pv<`/`pv>` survive (`settings.cfg` has `perf_vel_lt/rt` and
     `lines=26`).
