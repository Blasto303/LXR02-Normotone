# Session 079 — Handoff Log

```
DATE: 2026-10-10
SESSION GOAL: Step trigger conditions, per-step roll/rhythm/fill, and
              micro-timing specials (P1 design + implementation); P2
              remediation for four hardware-testing issues; SD_CARD
              PAT4 v1 → v2 conversion.
COMPLETED: P1 design and implementation (16 files), P1 addendum (3
           post-testing fixes), P2 remediation design, implementation,
           and post-implementation assessment (9 files). SD_CARD
           conversion (194 PAT4 files across 3 banks). All code builds
           clean. Hardware testing pending.
VERIFIED ON HARDWARE:
  - P1 initial implementation: tested by user; three issues found,
    fixed in the P1 addendum.
  - P2: not yet hardware tested (assessment pass only).

CHANGES THIS SESSION (detail in §2–§6):
- Core/Bank/Scene/Pattern/PatternData.c/.h: 7-special pool block
  format, widened struct/encode/decode/setters, PAT_RAW_BLOCK_MAX
  132→136, PAT_POOL_SWAP_CHUNKS 33→34
- Core/Bank/Scene/Pattern/PatternStackService.c/.h: 5 new ops and
  service setters for condition/roll/timing/rhythm/fill
- Core/Bank/Scene/Preset/ParameterArray.h: PAR_STEP_CONDITION,
  PAR_STEP_ROLL, PAR_STEP_TIMING, PAR_STEP_RHYTHM, PAR_STEP_FILL
- Core/DSP/Instruments/InstrumentManager.c/.h: per-slot gate fade
  arrays, requestGateDuck/gateGain APIs; removed
  instrumentManager_applyTriggerGateDecay
- Core/DSPAudio/mixer.c: gate gain fold into dry + send taps
- Core/Hardware/frontPanel/buttonHandler.c: STEP re-press toggle,
  PERF fill mode arm/disarm
- Core/MIDI/MidiVoiceControl.c: removed voiceControl_noteOnGated,
  restored plain processPending dequeue
- Core/Menu/menu.c/.h: toggleStepEditSubpage, subpage chain,
  automation predicate, scroll sign, rhythm decoder/list/stepper,
  encoder/pot hooks, clamp, condition evaluator display, fill mode
- Core/Menu/MenuText.h: cdn/tim/rtm short labels, Conditin/%Delay/
  Rhythm long names, conditionNames extended to 39
- Core/Menu/menuPages.h: SEQ subpage 2 definition
- Core/Sequencer/sequencer.c/.h: condition evaluator (39 entries),
  self-mute bypass, repeat counter, fill flag, per-step roll engine,
  micro-timing deferral, duck scheduling, roll+duck resets;
  removed seq_triggerVoiceGated
- config.h: ROLL_FILL_FADE_MS 5u
- tools/convert_pattern_specials.py: PAT4 probability→condition
  offline converter (new)
- SD_CARD/Bank/*: 194 PAT4 files converted v1→v2

KNOWN ISSUES INTRODUCED:
- Roll fill duck is not yet hardware tested.
- P1 addendum fix 3 (7th-track mute condition) is not yet hardware
  tested.
KNOWN ISSUES RESOLVED:
- Step probability was a featureless random gate since S070 (now a
  full 39-entry condition system).
- Roll fill gate mutated voice decay (made it ~10× slower, not
  shorter), poisoning subsequent triggers until Scene reload. Replaced
  with per-slot output-gain fade.
- STEP subpage 2 (roll settings) was unreachable (P1 addendum fix 1).
- Step-special display labels used wrong abbreviations (P1 addendum
  fix 2).
- Mute conditions covered only tracks 1–6; track 7 (HiHat open) had
  no mute condition pair (P1 addendum fix 3).

NEXT SESSION RECOMMENDED GOAL:
1. Hardware test P2 (4-item checklist) and the P1 addendum fixes.
2. Decide the STEP marker retry one-liner (carried from S078).
3. Clear remaining retest rows (3.10/3.14/3.15, C1–C6).
4. Re-measure the production build.

BLOCKERS: P2 hardware testing.

CRITICAL REMINDERS FOR NEXT SESSION:
- PAT4 version is now 2; old v1 files are rejected by the reader.
  SD cards must be converted with tools/convert_pattern_specials.py
  before use.
- The rolled-back decay mutation (instrumentManager_applyTriggerGateDecay)
  is gone; any code referencing it will fail to link.
- seq_triggerVoiceGated and voiceControl_noteOnGated are removed.
- PATTERN_FILE_VERSION is 2 (not 5 as the P1 plan initially proposed;
  the plan's "v5" was a placeholder that landed as v2 in the code).
```

---

## 1. Session overview

Session 079 ran on `dev-ph6-cleanup` from the S078 close (text 539,856,
data 416, bss 427,616; headroom 213,392 B) to final build text=544,072,
data=408, bss=427,776 (payload 544,480 B of 753,664, headroom 209,184 B).

Two implementation parts, a P1 addendum, and SD_CARD conversion:

- **P1** — Step trigger conditions, per-step roll (rate, rhythm, fill), and
  micro-timing delay. Design document: `S079_P1_STEP_CONDITIONS_SPECIALS.md`.
  Implementation schedule: `S079_P1_IMPLEMENTATION.md`. 16 files changed,
  ~4 KB text growth, ~80 B BSS growth.
- **P1 addendum** — Three fixes after hardware testing: subpage 2
  unreachable, display text corrections, 7th-track mute condition.
- **P2** — Roll follow-up remediation for four issues found during P1
  hardware testing. Design: `S079_P2_ROLL_FOLLOWUP_REMEDIATION.md`.
  Schedule: `S079_P2_IMPLEMENTATION.md`. 9 files, +168 B text, +80 B BSS.
- **SD_CARD** — Offline PAT4 v1→v2 conversion of 194 files.

Build at S079 close (DEV config):

| Metric | Value |
|---|---|
| text / data / bss | 544,072 / 408 / 427,776 |
| Flash payload | 544,480 B of 753,664 B |
| Headroom | 209,184 B |
| BSS growth from S078 | +160 B (P1 +80 B, P2 +80 B) |
| Branch | `dev-ph6-cleanup`, HEAD uncommitted |

---

## 2. P1 — Step Trigger Conditions, Roll, and Micro-Timing

### 2.1 Design decisions (from `S079_P1_STEP_CONDITIONS_SPECIALS.md`)

The dynamic pool block was expanded from 3 specials (note, velocity,
probability) to 7 specials using flag bits 0–6:

| Bit | Special | Stored byte | Range | Default |
|-----|---------|-------------|-------|---------|
| 0 | Note | note | 0–127 | 63 |
| 1 | Velocity | velocity | 0–127 | 100 |
| 2 | Trigger condition | condition | 0–38 | 0 (`-`, always fire) |
| 3 | Roll rate | roll | 0–8 | 0 (off) |
| 4 | Micro-timing delay | timing | 0–99 | 0 (none) |
| 5 | Roll rhythm | rhythm | packed byte | 0 (off) |
| 6 | Roll fill | fill | 0–100 | 100 (full) |
| 7 | *reserved* | — | — | — |

**No backward compatibility.** Old PAT4 v1 files with probability bytes are
rejected. The offline converter `tools/convert_pattern_specials.py` maps
old probability values (0–127) to condition indices (0–10) before flashing.
PAT4 version bumped from 1 to 2.

#### Trigger conditions (39 entries, indices 0–38)

**Probability (1–10):** r10 through r95, mapping to 10%–95% random gate
via hardware RNG. Thresholds: 13, 25, 38, 51, 64, 76, 89, 102, 114, 121
(out of 127).

**Repeat count X:Y (11–20):** Step fires on the Xth iteration of a track
repeating Y times. `seq_trackRepeatCount[track]` increments on step-index
wrap. Pairs: 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4. Value 20 =
`1:-` (first play only). Evaluation: `(repeatCount % Y) == (X - 1)`.
Reset on: transport start, Scene change, per-track Scene change. Once modes
never wrap (counter stays 0).

**Fill / Not Fill (21–22):** `fil` fires only during fill mode; `!fl`
fires only outside fill. Fill mode activates when PERF button is held past
`DOUBLE_CLICK_TIMEOUT` (300 ms), preventing inadvertent triggers during
mode-switch press. `seq_fillActive` flag set/cleared by buttonHandler.

**Last / Not Last (23–24):** `lst` fires if the most recent conditional
step (on this track, any condition 1–38) also fired. `!ls` fires if it
didn't. Per-track flag `seq_trackLastConditionFired[7]`, default 0 (not
fired). Updated on every conditional step evaluation.

**Mute / Not Mute (25–38):** `Nmu` fires if voice N is muted; `!Nm` fires
if not muted. Covers all 7 tracks (indices 25–31 = muted, 32–38 = not
muted). Self-mute bypass: when a step on a muted track tests its own
track's mute state, the mute gate is bypassed and the condition evaluated.
Implementation reads the address entry (bit 14), checks specials, and
evaluates if the condition targets the track itself. Track 7 maps voice
index via `% 7u`, boundary `cond <= 31u`.

#### Roll rate (per-step, values 0–8)

Absolute note values independent of track step scale: off, 1/8 (12 PPQ),
8th triplet (8), 16th (6), 16th triplet (4), 32nd (3), 32nd triplet (2),
48th (2), 64th (~1.5, quantized). Roll carry-over: adjacent steps with
matching rate+rhythm+fill continue the roll seamlessly. Condition gates the
entire step (roll suppressed if condition fails). Per-step roll takes
priority over live performance roll.

#### Roll rhythm (packed byte encoding)

Byte = `(cycle << 4) | (dir << 3) | full`. Dir 0 = halve (`-`), dir 1 =
double (`+`). Full count = retriggers at step velocity before changing.
Example: `2-5` = 100, 100, 50, 50, 50, repeat. Halved velocity floors at
1; doubled clamps at 127. Carry-over position preserved across matching
steps.

The ordered list (S079 P2 final): 21 entries — `off`, then 10 halve
patterns (1-2, 1-3, 2-3, 1-4, 2-4, 3-4, 1-5, 2-5, 3-5, 4-5), then the
same 10 as double (+). Equal pairs (full==cycle) excluded (equivalent to
off). Cycles 2–5 only.

Byte values in list order: `0x00, 0x21, 0x31, 0x32, 0x41, 0x42, 0x43,
0x51, 0x52, 0x53, 0x54, 0x29, 0x39, 0x3A, 0x49, 0x4A, 0x4B, 0x59, 0x5A,
0x5B, 0x5C`.

#### Roll fill (0–100%)

Controls what fraction of the step the roll occupies. Fill 100 = full step
(default). Fill 50 = retriggers in first half only, then duck. Fill 0 =
single hit ducked immediately. The duck mechanism (P2 fix 4): a ~5 ms
output-gain fade via `instrumentManager_requestGateDuck()`, not a decay
mutation.

#### Micro-timing delay (0–99%)

Per-step nudge: `delay_ticks = (step_interval_ticks * value) / 100`.
Additive with shuffle. Combined delay can extend into next step; deferred
trigger dropped if next step fires first. Automation fires at delayed time.
Roll starts at delayed position. Merged with existing shuffle deferral
system in `seq_processSchedulerTick()`.

### 2.2 STEP page layout

Two step-special subpages added to SEQ_PAGE:

| Subpage | Cell 0 | Cell 1 | Cell 2 | Cell 3 |
|---------|--------|--------|--------|--------|
| 0 | len | scl | shf | run |
| 1 | vel 0–127 | not 0–127 | cdn label list | tim 0–99 |
| 2 | rol rate list | rtm rhythm | rfl 0–100 | *(empty)* |

Navigation chain: sub 1 pos 3 → right → sub 2 pos 0; sub 2 pos 2 → right
→ automations; left exit from automations → sub 2 pos 2; sub 2 pos 0 →
left → sub 1 pos 3.

Step-button re-press (P2 fix 1): pressing the already-selected step toggles
sub 1 ↔ sub 2. Pressing a different step always opens sub 1.

### 2.3 Implementation details (from `S079_P1_IMPLEMENTATION.md`)

**Step 0 — Constants and types:** `PAT_SPECIAL_PROB_BIT` →
`PAT_SPECIAL_COND_BIT`; 4 new bit definitions; `PAT_SPECIAL_FLAGS_MASK` =
`0x7Fu`; `pat_step_specials_t` widened to 8 fields; `PAT_RAW_BLOCK_MAX`
132 → 136 (34 chunks); `PAT_POOL_SWAP_CHUNKS` 33 → 34; `pat_rawEncode()`
signature widened from 3 positional values to `const pat_step_specials_t *`;
4 new setter declarations + `pat_setStepProbability` →
`pat_setStepCondition`.

**Step 1 — PatternData.c:** `pat_blockChunks()` counts 7 flag bits.
`pat_blockWrite()` takes specials struct pointer. `pat_blockRead()` reads
7 specials with defaults (condition 0, roll 0, timing 0, rhythm 0, fill
100). `pat_blockReadAutomations()` and `pat_tryAppendAutomation()` count
7 value bytes. `pat_writeDynamic()` takes specials struct.
`pat_writeSpecials()` updated. `pat_readStepSpecials()` defaults updated.
`pat_applyStepToMenu()` writes all 7 PAR_STEP_* entries. All 7 setters
implemented (pattern: read current specials, modify one field, toggle flag
bit, call `pat_writeSpecials()`). `pat_rawDecode()` and `pat_rawEncode()`
extended. Default for fill = 100 (clear flag when value == 100, not 0).

**Step 2 — PatternStackService.c:** `PATSVC_OP_SET_PROBABILITY` →
`PATSVC_OP_SET_CONDITION`; 4 new ops. `patSvc_blockChunksFor()` extended.
`patSvc_requiredChunks()` flag-toggle cases for all 5 step-special ops.
`patSvc_executeEvent()` dispatch cases. 5 service setter functions.

**Step 3 — Sequencer.c:** Per-track state: `seq_trackRepeatCount[7]`,
`seq_trackLastConditionFired[7]`, `seq_fillActive`,
`seq_rollTickAccum/VelPos/LastRate/LastRhythm/LastFill/StepVel/
Remaining/Active[7]` (~67 B). `seq_evaluateStepCondition()` — full
dispatch with probability thresholds, X:Y table, fill/last/mute arms.
Self-mute bypass in `seq_advanceTrackStep()` with address-bit-14 check
and pool read. Repeat counter increments at step-index wrap for each play
mode. Roll engine: `seq_rollBegin()` computes PPQ interval, slot count,
filled ticks; `seq_processRollTicks()` services the per-track retrigger
countdown; `seq_rollComputeVelocity()` applies the rhythm pattern. Micro-
timing merged with shuffle deferral. State reset at transport start,
`seq_selectActivePattern()`, `seq_setTrackPlayedScene()`, and
`seq_setStepIndexToStart()`.

**Step 4 — Menu (STEP page) and buttonHandler:** SEQ subpage 2 in
`menuPages.h`. SEQ text/long name entries in `MenuText.h`:
`cdn/tim/rol/rtm/rfl` and `Conditin/% Delay/RollRate/Rhythm/RollFill`.
`conditionNames[]` extended (39 entries: `-`, r10–r95, 1:2–4:4, 1:-, fil,
!fl, lst, !ls, 1mu–6mu, !1m–!6m, 7mu, !7m). Roll rate labels, rhythm
decoder, condition label renderer. Cell value type routing for
`DTYPE_CONDITION`, `DTYPE_ROLL_RATE`. Clamp function extended. Subpage
chain in `menu_moveToMenuItem()`.

**Step 5 — PERF fill mode:** `seq_fillActive` set by
`buttonHandler_tick()` when PERF held past `DOUBLE_CLICK_TIMEOUT`; cleared
on release.

**Step 6 — PAT4 version and converter:** `PATTERN_FILE_VERSION` → 2.
Reader rejects version 1. `tools/convert_pattern_specials.py`: reads PAT4
files, converts probability bytes to condition indices via 10% buckets,
writes v2 files.

### 2.4 P1 addendum (3 post-testing fixes)

**Fix 1 — Subpage 2 unreachable.** Problem: `menu_showStepEditPage()`
always enters sub 1; scrolling right from sub 1 pos 3 entered automations
directly. Fix: chained sub 1 → sub 2 → automations in
`menu_moveToMenuItem()`. Changed `menu_stepAutomationPageActive()` from
`activePage == 1u` to `== 2u`. Added left transition from sub 2 pos 0 →
sub 1 pos 3. Left-exit from automations → sub 2 pos 2. Pot-clear guard
unchanged (both 1 and 2 are > 0).

**Fix 2 — Display text corrections.** `MenuText.h`: short labels `con` →
`cdn`, `dly` → `tim`; long names `Conditn` → `Conditin`, `MicTimng` →
`% Delay`. No enum renumbering.

**Fix 3 — 7th-track mute condition.** Extended condition table from 37
(indices 0–36) to 39 (0–38). Added `7mu` (index 37) and `!7m` (index 38)
to `conditionNames[]`. `seq_evaluateStepCondition()`: guard `> 36u` →
`> 38u`; modulus `% 6u` → `% 7u`; muted boundary `<= 30u` → `<= 31u`
(25–31 = muted voices 1–7, 32–38 = not muted). Self-mute bypass updated
for ranges 25–31 and 32–38. `menu_clampCellValue()` upper bound 36 → 38.
`menu_getConditionName()` boundary 36 → 38.
`convert_pattern_specials.py` docstrings updated (0..38 range; mapping
itself unchanged — only emits 0–10).

Post-addendum build: text 543,904, bss 427,696, headroom 209,352 B
(+72 B from new nav blocks and two condition labels).

### 2.5 P1 ISR state budget

| Variable | Size | Region | Owner |
|---|---|---|---|
| `seq_trackRepeatCount[7]` | 7 B | SRAM1 | Sequencer |
| `seq_trackLastConditionFired[7]` | 7 B | SRAM1 | Sequencer |
| `seq_fillActive` | 1 B | SRAM1 | ButtonHandler |
| `seq_rollTickAccum[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollVelPos[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollLastRate[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollLastRhythm[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollLastFill[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollStepVel[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollRemaining[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollActive[7]` | 7 B | SRAM1 | Sequencer |
| **Total** | **~71 B** | SRAM1 | |

Measured BSS growth (P1): +80 B (71 B + alignment padding).

---

## 3. P2 — Roll Follow-Up Remediation

### 3.1 Design (from `S079_P2_ROLL_FOLLOWUP_REMEDIATION.md`)

Four issues found during P1 hardware testing:

**Point 1 — Step-button press does not toggle STEP pages.** Re-pressing
the selected step always calls `buttonHandler_selectActiveStep()`, forcing
sub 1. Fix: `menu_toggleStepEditSubpage()` flips sub 1 ↔ 2 on re-press.

**Point 2 — No `>` scroll marker on roll subpage.** `checkScrollSign()`
had no subpage-2 arm. Additionally, scroll transition from sub 2 was
missing. Fix: subpage-2 `>` arm, scroll entry from sub 2 pos 2 into
automations, left-exit returns to sub 2.

**Point 3 — Rhythm label/category/order wrong.**
- Short label `rrt` → `rtm`; long name `RlRhythm` → `Rhythm`
- Category `CAT_STEP` → `CAT_VELOCITY`
- Value 0 = `off` (not `1-1`); equal pairs excluded
- Ordered list: 21 entries (off + 10 halve + 10 double)
- Decoder, stepper, clamp updated

**Point 4 — Roll fill mutates voice decay.** Root cause:
`instrumentManager_applyTriggerGateDecay()` did `ampEg->decay *= 0.1f`.
Since `slopeEg2_calcTime()` returns a per-sample *rate* (larger = faster),
`* 0.1` made the envelope ~10× SLOWER, not shorter. The mutation also
persisted through subsequent triggers until a Scene change reloaded the
parameter. Fix: remove the decay mutation entirely; duck the sounding voice
with a ~5 ms output-gain fade at the fill boundary.

### 3.2 Implementation (from `S079_P2_IMPLEMENTATION.md`)

**Step 1 — config.h:** `ROLL_FILL_FADE_MS 5u` after `DOUBLE_CLICK_TIMEOUT`.

**Step 2 — Step-button toggle:** `menu_toggleStepEditSubpage()` in menu.h/
menu.c (reads `menu_getSubPage()`, picks other, resets automation editor,
sets `menuIndex`, refreshes pot snapshots, repaints). `buttonHandler.c`:
SELECT_MODE_STEP arm calls toggle when `buttonHandler_visibleStep(
seqButtonPressed) == buttonHandler_selectedStep` and page is SEQ sub 1/2.

**Step 3 — Scroll marker and automation entry:** `checkScrollSign()`
subpage-2 arm (positions 0–2). Already present from P1 addendum: sub 1
pos 3 → sub 2 pos 0, sub 2 pos 2 → automations, left-exit → sub 2 pos 2,
predicate `== 2u`. Verified correct for the three-step chain architecture.

**Step 4 — Rhythm presentation:** `MenuText.h` short/long labels updated.
`menu.c`: `CAT_VELOCITY` category. Decoder: `off` for byte 0, cycle<2,
full 0, full≥cycle. `stepRollRhythmBytes[21]` ordered list with exact
hex encoding. `menu_rollRhythmStep()` finds current byte in list, applies
delta, clamps. Forward declaration. Encoder hook: tests
`MENU_CELL_STATIC + PAR_STEP_RHYTHM`, calls stepper, commits. Endless-pot
hook: same, with `menu_knobs_dirty`. Clamp: snaps unlisted byte to list
via `menu_rollRhythmStep(*value, 0)`.

**Step 5 — Roll fill duck:**

Removals:
- `instrumentManager_applyTriggerGateDecay()` (InstrumentManager.c/.h)
- `voiceControl_noteOnGated()` (MidiVoiceControl.c/.h)
- `seq_triggerVoiceGated()` (sequencer.c)
- Bit-7 velocity packing/decode (MidiVoiceControl.c)

Additions:
- InstrumentManager state: `float gateFade[6]` (1.0 = open),
  `float gateFadeStep[6]` (0 = idle), `volatile uint8_t gateDuck[6]`.
  `gateFadeStepRate = OUTPUT_DMA_SIZE / (ROLL_FILL_FADE_MS/1000 * REAL_FS)`.
- Init: `instrumentManager_runtimeInit()` seeds open.
  `instrumentManager_resetRuntimeSlotFromScene()` resets on Scene/slot
  change. `instrumentManager_triggerTrack()` resets to open on every
  trigger (single funnel — sequencer, MIDI, preview all pass through).
- Duck consume: `instrumentManager_calcSlotAsync()` (once/block/slot)
  consumes `gateDuck`, arms `gateFadeStep`, advances `gateFade -= step`
  clamped to 0.
- API: `instrumentManager_requestGateDuck(trigger_track)` — maps track
  to slot, sets `gateDuck`. Called from TIM3 ISR; single-byte store is
  atomic on Cortex-M7. `instrumentManager_gateGain(slot)` — returns live
  fade multiplier 0.0–1.0.
- Mixer: `mixer_faderGains()` folds `instrumentManager_gateGain(slot)` into
  `*mix_gain` and `*send_gain`. Existing per-block gain interpolation
  smooths each step.
- Sequencer: `seq_rollDuckDelta[7]` (PPQ ticks from last retrigger to fill
  end), `seq_rollDuckDelay[7]` (live countdown), `seq_rollDuckActive[7]`.
  `seq_rollArmDuck(track)` starts countdown when roll active and fill<100.
  `seq_rollBegin()` computes delta = `filled - (slots-1) * ppq`; arms
  single-slot case. `seq_processRollTicks()` services countdown at top of
  per-track loop; arms duck when last retrigger fires. No-roll step cancels
  pending duck.
- Reset sites: `seq_init()`, `seq_setStepIndexToStart()`,
  `seq_selectActivePattern()` (now also resets base roll state),
  `seq_setTrackPlayedScene()` (with HiHat pair).

Worked example: step = 1/4 (96 ticks), roll = 1/8 (ppq 48), fill = 75%.
`filled = 72`, `slots = 2`; retriggers at t=0, t=48; duck delay =
72 − 48 = 24 ticks. Second hit plays 24 ticks then fades over ~5 ms.

### 3.3 Build results (P2)

| Metric | S079 P1 baseline | After S079 P2 | Delta |
|---|---|---|---|
| text | 543,904 | 544,072 | +168 |
| data | 408 | 408 | 0 |
| bss | 427,696 | 427,776 | +80 |
| flash used | 544,312 | 544,480 | +168 |
| headroom | 209,352 | 209,184 | -168 |

BSS +80 B: InstrumentManager gate arrays (6×4 + 6×4 + 6×1 = 54 B, padded
to 56 B) + sequencer duck arrays (3×7 = 21 B, padded to 24 B).

Flash +168 B: removed gated-trigger path offset most new code.

### 3.4 DSP classification (P2 fix 4)

- **No duck active (steady state): S0 bit-identical.**
  `instrumentManager_gateGain()` returns `1.0f` (IEEE-754 multiply by 1.0f
  is exact); gains reach the add functions unchanged.
- **Duck active: S4 audible, by design.** Gains intentionally scaled for
  the choked fill behaviour.
- **Constant cost:** gate read + two multiplies per block per slot run
  always. Per-block ramp advance is a couple of compares.
- `make -C tools/dsp_test mixer` reports `differing samples: 0` (harness
  does not cover `mixer_faderGains()`; the tested add functions are
  unchanged).

### 3.5 RAM note (P2)

| Variable | Size | Region | Owner |
|---|---|---|---|
| `gateFade[6]` | 24 B | SRAM1 | InstrumentManager |
| `gateFadeStep[6]` | 24 B | SRAM1 | InstrumentManager |
| `gateDuck[6]` | 6 B | SRAM1 | InstrumentManager |
| `seq_rollDuckDelta[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollDuckDelay[7]` | 7 B | SRAM1 | Sequencer |
| `seq_rollDuckActive[7]` | 7 B | SRAM1 | Sequencer |
| **Total** | **75 B** | SRAM1 | (padded to 80 B) |

### 3.6 Post-implementation assessment

All 5 schedule steps verified against working-tree source. Each step PASS.
0 discrepancies beyond the documented S3.2–S3.4 architecture divergence
(P1 three-step subpage chain vs. schedule's two-entry-point model), which
is functionally equivalent and documented. No missing code, no extra code,
no wrong values.

### 3.7 Hardware checklist (P2, pending)

1. STEP page: press a step, press it again → sub 1 ↔ 2 toggles; a
   different step always opens sub 1.
2. Roll page shows `>` in the last column; scrolling right enters the
   automation list; scrolling left from automations returns to the roll
   page.
3. Condition cell reads `cdn`, timing reads `tim`; rhythm cell reads `rtm`;
   clicked-in view shows "Conditin"/"% Delay"/"Velocity / Rhythm"; pot
   steps off, 1-2, 1-3, 2-3, 1-4, 2-4, 3-4, 1-5, 2-5, 3-5, 4-5, 1+2 …
   4+5; clamps at both ends.
4. Roll with fill < 100: last hit ducked by fast ~5 ms fade; voice decay
   NOT changed; later steps and Scene changes unaffected.

---

## 4. SD_CARD Conversion

`tools/convert_pattern_specials.py` converted 194 PAT4 v1 files to v2
across three banks: Bank 000 (FluxTime, 16 presets), Bank 002 (FluxTim2,
16 presets), Bank 003 (16 presets). The converter maps old probability
bytes (0–127) to condition indices (0–10) using 10% buckets:

- 127 → remove condition byte (default = always fire)
- 114–126 → r95, 102–113 → r90, 89–101 → r80, …, 0–12 → r10

Fine-grained probability values (e.g. 73%) lose precision — accepted.

All Python tools in `tools/` verified S079-compatible:
`convert_pattern_specials.py`, `convert_scene_scale.py`, `link_budget.py`,
`decode_devlogs.py`, `dsp_test/`.

---

## 5. Affected files summary

### P1 (design + implementation + addendum)

| File | Changes |
|---|---|
| `config.h` | `PAT_POOL_SWAP_CHUNKS` 33→34 |
| `Core/Bank/Scene/Pattern/PatternData.h` | 7-special struct, flag bits, `PAT_RAW_BLOCK_MAX` 132→136, 7 setter declarations |
| `Core/Bank/Scene/Pattern/PatternData.c` | Block reader/writer/encoder/decoder for 7 specials, 7 setters |
| `Core/Bank/Scene/Pattern/PatternStackService.h` | 5 service setter declarations |
| `Core/Bank/Scene/Pattern/PatternStackService.c` | 5 new ops, service setters, chunk counter |
| `Core/Bank/Scene/Preset/ParameterArray.h` | 5 new `PAR_STEP_*` parameter IDs |
| `Core/Sequencer/sequencer.c` | Condition evaluator, self-mute bypass, repeat counter, fill flag, roll engine, micro-timing, state resets |
| `Core/Sequencer/sequencer.h` | `seq_fillActive` extern |
| `Core/Menu/menu.c` | Subpage chain, condition/roll/rhythm display+edit, encoder hooks, clamp, PERF fill |
| `Core/Menu/menu.h` | `menu_toggleStepEditSubpage()` declaration |
| `Core/Menu/MenuText.h` | Short/long labels, `conditionNames[]` (39 entries) |
| `Core/Menu/menuPages.h` | SEQ subpage 2 page table entry |
| `Core/Hardware/frontPanel/buttonHandler.c` | PERF fill mode arm/disarm |
| `Core/Menu/CopyClear/copyClearService.c` | Updated `pat_rawEncode()` calls |
| `Core/Menu/CopyClear/copyOps.c` | Updated specials references |
| `tools/convert_pattern_specials.py` | New: offline PAT4 converter |

### P2 (remediation)

| File | Changes |
|---|---|
| `config.h` | `ROLL_FILL_FADE_MS 5u` |
| `Core/Menu/menu.h` | `menu_toggleStepEditSubpage()` (shared with P1) |
| `Core/Menu/menu.c` | Toggle helper, scroll-sign arm, rhythm list/stepper/decoder, edit hooks, clamp, `valueNames` category |
| `Core/Menu/MenuText.h` | `rtm` short label, `Rhythm` long name (shared with P1) |
| `Core/Hardware/frontPanel/buttonHandler.c` | STEP re-press toggle |
| `Core/DSP/Instruments/InstrumentManager.c/.h` | Gate-fade state + duck/gateGain API; removed gate-decay |
| `Core/MIDI/MidiVoiceControl.c` | Removed gated note-on + bit-7 decode |
| `Core/Sequencer/sequencer.c` | Removed gated trigger; duck scheduling; roll + duck resets at 4 sites |
| `Core/DSPAudio/mixer.c` | Gate gain fold into dry + send |

---

## 6. Removed APIs and symbols

| Removed | File | Reason |
|---|---|---|
| `instrumentManager_applyTriggerGateDecay()` | InstrumentManager.c/.h | Decay mutation was the root cause of the stuck-decay bug; replaced by output-gain fade |
| `voiceControl_noteOnGated()` | MidiVoiceControl.c/.h | Part of the gated-trigger plumbing; no longer needed |
| `seq_triggerVoiceGated()` | sequencer.c | Part of the gated-trigger plumbing; roll engine now fires plain triggers |
| `pat_setStepProbability()` | PatternData.c/.h | Renamed to `pat_setStepCondition()` |
| `patSvc_setStepProbability()` | PatternStackService.c/.h | Renamed to `patSvc_setStepCondition()` |
| `PAT_SPECIAL_PROB_BIT` | PatternData.h | Renamed to `PAT_SPECIAL_COND_BIT` |
| `PATSVC_OP_SET_PROBABILITY` | PatternStackService.c | Renamed to `PATSVC_OP_SET_CONDITION` |

---

## 7. New APIs and symbols

| Added | File | Purpose |
|---|---|---|
| `instrumentManager_requestGateDuck(track)` | InstrumentManager.c/.h | Arm a ~5 ms output-gain fade on a track's voice |
| `instrumentManager_gateGain(slot)` | InstrumentManager.c/.h | Read the live fade multiplier (0.0–1.0) for the mixer |
| `menu_toggleStepEditSubpage()` | menu.c/.h | Toggle SEQ sub 1 ↔ sub 2 |
| `pat_setStepCondition()` | PatternData.c/.h | Set/clear one step's trigger condition |
| `pat_setStepRoll()` | PatternData.c/.h | Set/clear one step's roll rate |
| `pat_setStepTiming()` | PatternData.c/.h | Set/clear one step's micro-timing delay |
| `pat_setStepRhythm()` | PatternData.c/.h | Set/clear one step's roll rhythm |
| `pat_setStepFill()` | PatternData.c/.h | Set/clear one step's roll fill |
| `patSvc_setStepCondition()` | PatternStackService.c/.h | Service setter for condition |
| `patSvc_setStepRoll()` | PatternStackService.c/.h | Service setter for roll |
| `patSvc_setStepTiming()` | PatternStackService.c/.h | Service setter for timing |
| `patSvc_setStepRhythm()` | PatternStackService.c/.h | Service setter for rhythm |
| `patSvc_setStepFill()` | PatternStackService.c/.h | Service setter for fill |
| `seq_fillActive` | sequencer.c/.h | Fill mode flag (PERF hold) |
| `ROLL_FILL_FADE_MS` | config.h | Fade duration constant (5 ms) |

---

## 8. Specification reference updates (S079 close)

- **`PATTERN_DYNAMIC_STACK.md`**: authority line updated to S079. §2 (pool
  block format) updated for 7 specials and new flag bits. §3 (swap block)
  updated: 34 chunks / 136 B. §6 (specials) updated: condition replaces
  probability, 4 new specials, 39-entry condition table. §7 (PAT4 format)
  updated: version 2, 7 specials in the block byte stream.
- **`MODULE_INTERCHANGE_SPEC.md`**: S079 additions listed. New APIs
  documented. Removed APIs noted.
- **`STORAGE_SRAM_MANIFEST.md`**: authority line updated to S079.
  S079 BSS changes documented (+160 B: P1 +80 B, P2 +80 B). Build numbers
  updated.

---

## 9. Disposable documents

The following root documents are superseded by this log and the updated
specification references. The user will delete them:

- `S079_P1_STEP_CONDITIONS_SPECIALS.md` — P1 design (647 lines)
- `S079_P1_IMPLEMENTATION.md` — P1 implementation schedule + addendum
  (~2208 lines)
- `S079_P2_ROLL_FOLLOWUP_REMEDIATION.md` — P2 design (316 lines)
- `S079_P2_IMPLEMENTATION.md` — P2 implementation schedule + assessment
  (1158 lines)

Earlier root documents (`S078_*`, `S077_*`, `S076_*`, `S075_*`) that the
user has not yet deleted are also superseded by their respective session
logs.

---

## 10. Carry-forward

1. **Hardware testing (S079):** P2 checklist (§3.7) not yet run. P1
   addendum fixes (subpage 2 reachability, display text, 7mu/!7m) need
   on-device confirmation.
2. **STEP marker retry gap (S078):** the one-liner
   (`|| menu_activePage == SEQ_PAGE` in `menu_serviceRuntimeWidgets()`)
   was offered, not applied. Still open.
3. **Retest rows still blank:** 3.10/3.14/3.15 (S077 P4 Scene morph
   fan-out), C1–C6. S077 P5 (bar-to-step) and P6 (PERF morph underline)
   never had checklist rows.
4. **Production build:** last measured at S075 F1; needs rebuild.
5. **Open defects carried (not fixed):** Pattern Load fan-out `memcpy` can
   tear one playback tick; O1 Settings Load per-voice Morph equalisation;
   F4 AutoSave trace ring drops lifecycle records during dirty bursts;
   underline limitations (live erase race, deferred clear race); S077 P2
   limitation: Scene-namespace LFO/velocity target tokens resolve through
   the active Scene in InstrumentManager.
6. **Accepted limits (S079):** flags byte bits 0–6 all assigned; only
   bit 7 reserved. Pool capacity under extreme use (7 specials × 128 steps
   × 7 tracks = 8,960 B > 8,192 B pool). Roll timing quantization at 64th
   notes (1.5 PPQ). Combined shuffle + micro-timing can drop deferred
   trigger. No retrigger cap on slow tracks.
7. **Card preparation:** SD cards must be converted with
   `tools/convert_pattern_specials.py` before use with S079+ firmware.
   Delete `.hcprms1`/`.hcprms2` after flashing S078+ firmware on older
   cards (S078 carry-over).
8. **Stale tools/small debt (carried from S078):** `decode_devlogs.py`
   misreads `pattrace.bin`; Makefile echoed comments and bare-`make`
   default goal; duplicated comment lines in `mixer.c` and
   `ResonantFilter.c`; `presetManager.c` comment indentation; unreachable
   Scene Save phases 33–36; `AUTOSAVE_TRACE_RECORD_COUNT` still at the
   temporary 2,048.
9. **Deferred features (carried from S078):** `/Effect/` browser and
   Load/Save item (A35); MIDI mapping (A20); live record of FX moves (A22)
   and of automation; Patgen/Euklid revert; triplet `12a/12b` scale mode
   (SCOPING §4.10); one-shot LFOs; looper; external MIDI sequencing tracks.
10. **Still open from earlier sessions (carried):** Phase 5 hardware
    acceptance for S072 Steps 6–10; slow Load type switching; D-C1 (boot
    image check); LFO noise range (suspected); optional AutoSave old-format
    mask guard; filesystem budget primitive extraction.
11. **S074 items still open (carried):** the unexplained boot timeout; the
    `cpu` widget with `cmp` on; CrumpBit minimum-share run; BC11; saturator
    α 0.35; stale comments.
