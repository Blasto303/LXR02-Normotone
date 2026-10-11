# S079 P2 — Roll Follow-Up Remediation

Session 079, Part 2 — remediation plan and implementation schedule for four
issues found while hardware-testing the S079 P1 step-special / roll feature.

Scope is limited to the S079 roll and step-special surface. No storage-format
change: the packed roll-rhythm byte (S079 §5.1) and the pool block layout are
unchanged. The only persistent change is behavioural (which rhythm bytes the
UI exposes, and the roll-fill gate mechanism).

Status tags: (DECIDED) = fixed as specified here; (OPEN) = needs a user
decision (see §8).

---

## 1. Summary

| # | Issue | Area | Tag |
|---|-------|------|-----|
| 1 | Re-pressing a step button does not toggle the STEP page | buttonHandler.c, menu.c/h | (DECIDED) |
| 2 | No `>` scroll marker on the roll subpage | menu.c | (DECIDED) |
| 3 | Rhythm label / category / order wrong | MenuText.h, menu.c, menu.h | (DECIDED) |
| 4 | Roll fill mutates voice decay (stuck, very long) | InstrumentManager, mixer.c, sequencer.c, MidiVoiceControl | (DECIDED) |

---

## 2. Point 1 — Step-button press toggles the two STEP pages

### Problem
In STEP mode a step-button press always calls
`buttonHandler_selectActiveStep()`, which calls `menu_showStepEditPage()` and
forces SEQ subpage 1. The roll subpage (2) is only reachable by scrolling right
past subpage 1's last cell, so repeated step presses never reach it.

### Desired
Pressing the already-selected step button again alternates between subpage 1
(`vel nte cdn tim`) and subpage 2 (`rol rtm rfl`). Pressing a different step
always lands on subpage 1.

### Design
Add `menu_toggleStepEditSubpage()` (menu.h / menu.c) that flips SEQ_PAGE
subpage 1 <-> 2, selects parameter 0, resets the automation editor state, and
repaints. Route the STEP-mode press in `buttonHandler_seqButtonPressed()`
through it when the pressed step is already selected and the current page is
SEQ subpage 1 or 2.

menu.h declaration and comment block:
```text
/* Toggle the STEP edit page between the two step-special subpages (S079 P2).
 *
 * What: switches SEQ_PAGE between subpage 1 (vel/note/condition/timing) and
 * subpage 2 (roll/rhythm/fill), selects parameter 0, resets the automation
 * editor cursor, and repaints. Why: re-pressing the selected step button must
 * reach the roll settings without the scroll gesture. Inputs: current SEQ
 * subpage. Output: menuIndex moves to the other step-special subpage; endless
 * pot snapshots refresh. Affiliates: buttonHandler_seqButtonPressed().
 */
void menu_toggleStepEditSubpage(void);
```

buttonHandler.c change in `buttonHandler_seqButtonPressed()`, SELECT_MODE_STEP
non-shift arm:
```c
case SELECT_MODE_STEP:
    if (buttonHandler_visibleStep(seqButtonPressed) ==
            buttonHandler_selectedStep &&
        menu_activePage == SEQ_PAGE &&
        (menu_getSubPage() == 1u || menu_getSubPage() == 2u)) {
        /* Re-press of the selected step toggles the two step pages (S079 P2). */
        menu_toggleStepEditSubpage();
    } else {
        led_clearAllBlinkLeds();
        buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
    }
    break;
```
The SHIFT+step arm (`buttonHandler_setRemoveStep`) is untouched.

---

## 3. Point 2 — `>` scroll marker and scroll entry on the roll subpage

### Problem
`checkScrollSign()` returns `>` for SEQ subpage 1 positions 0..3 but falls
through for subpage 2, so the roll page gives no cue that the automation list
follows. Additionally, the scroll transition from subpage 1 into the automation
list (`menu_moveToMenuItem()`) is hardcoded to `activePage == 1`, so scrolling
right from subpage 2 hits the empty `PAR_NONE` cell and stops — the `>`
affordance would be a dead end without a matching transition.

### Design
Three changes:

1. `checkScrollSign()`: add a subpage-2 arm after the existing subpage-1 block:
```c
    /* Roll subpage leads right into the automation list (S079 P2). */
    if (menu_activePage == SEQ_PAGE && activePage == 2u &&
        !menu_stepAutoActive && activeParameter <= 2u)
        return '>';
```

2. `menu_moveToMenuItem()`: add a right-scroll transition from subpage 2 pos 2
   into the automation list (mirrors the existing subpage 1 pos 3 entry), and
   change the left-exit from automations to land on subpage 2 pos 2 instead of
   subpage 1 pos 3. Update `menu_stepAutomationPageActive()` to recognise
   subpage 2 (change `activePage == 1u` to `activePage >= 1u`).

3. The existing subpage 1 pos 3 right-scroll transition is kept — entering
   automations is reachable from either subpage via scrolling.

---

## 4. Point 3 — Roll rhythm parameter

### 4a. Label, category, long name (DECIDED)
- short label `rrt` -> `rtm`
- category `CAT_STEP` -> `CAT_VELOCITY` ("Velocity")
- long name: `RlRhythm` -> `Rhythm`

Also includes the P1 text corrections (self-contained in this schedule):
- short label `con` -> `cdn`, long name `Conditn` -> `Conditin`
- short label `dly` -> `tim`, long name `MicTimng` -> `% Delay`

Files: `MenuText.h` (`shortNames[]`, `longNames[]`), `menu.c`
(`valueNames[TEXT_ROLL_RHYTHM]` category becomes `CAT_VELOCITY`).
Note the long-name string is used by the clicked-in full view; the three-char
short name is the cell label.

### 4b. List semantics (DECIDED)
- value 0 = `off` (the lowest parameter).
- no `1-1` / `1+1` entries: `full == cycle` never changes velocity, so it is
  equivalent to off and is not listed.
- order (final, per Q1/Q2): `off`, then the ten halve (`-`) entries in this
  exact order - 1-2, 1-3, 2-3, 1-4, 2-4, 3-4, 1-5, 2-5, 3-5, 4-5 - then the
  same ten as double (`+`) entries (1+2, 1+3, 2+3, 1+4, 2+4, 3+4, 1+5,
  2+5, 3+5, 4+5). Equal pairs (1-1, 2-2, ...) are excluded: they never change
  velocity and are equivalent to off.

Encoding reminder (S079 §5.1, unchanged): byte =
`(cycle << 4) | (dir << 3) | full`; dir 0 (`-`) / 1 (`+`), full 1..7. A listed
entry uses cycle 2..5 and `1 <= full <= cycle-1`.

The list is therefore 21 entries (`off` + 10 `-` + 10 `+`).

### 4c. Decoder change
`menu_getRollRhythmName()`: show `off` when the byte is 0, when cycle is 0, or
when `full >= cycle`; otherwise show `N<dir>M` as today. (This replaces the
old "byte 0 -> 1-1" behaviour.)

### 4d. Editing
Rhythm stays a packed byte in the pool and in `parameter_values[]` (no
load-side conversion, no invariant). Add an ordered byte list
`menu_rollRhythmBytes[]` and a stepper helper, called from both edit paths
(`menu_encoderChangeParameter()` and `menu_parseKnobDelta()`) immediately
before `menu_clampCellValue()`:
```c
/* Step the packed roll-rhythm byte along the ordered list (S079 P2).
 *
 * What: maps the current byte to a list position, applies the signed delta,
 * and returns the list byte at the new position. Bytes not in the list (off
 * equivalents, malformed encodings) map to position 0 (off). Why: the rhythm
 * is a sparse packed encoding, so the generic value+delta arithmetic would land
 * on undefined bytes; the parameter must scroll through the offered list.
 * Inputs: current packed byte and a signed delta. Output: the new packed byte.
 * Affiliates: menu_encoderChangeParameter(), menu_parseKnobDelta(),
 * menu_getRollRhythmName().
 */
static uint16_t menu_rollRhythmStep(uint16_t byte, int32_t delta);
```

### 4e. Clamp
`menu_clampCellValue()` for PAR_STEP_RHYTHM keeps its 0..255 bound (the stepper
only emits list entries; the decoder tolerates anything else).

---

## 5. Point 4 — Roll fill duck (fast fade-out)

### Root cause (confirmed)
`instrumentManager_applyTriggerGateDecay()` did `ampEg->decay *= 0.1f`.
`slopeEg2_calcTime()` returns a per-sample *rate* (larger = faster decay), so
`* 0.1` makes the envelope ~10x SLOWER (very long), not shorter. The mutation
also persists because the amp decay is re-applied from the runtime parameter at
trigger setup only when the parameter is written, not after this call — so the
poisoned value survives later triggers until a Scene change reloads it.

### Fix
Remove the decay mutation and the gated-trigger plumbing entirely; duck the
sounding voice with a ~5 ms fade-out that starts at the fill-window boundary.

Removal:
- `instrumentManager_applyTriggerGateDecay()` (InstrumentManager.c/h)
- `voiceControl_noteOnGated()` and the bit-7 velocity packing/decode
  (MidiVoiceControl.c/h, `voiceControl_processPending()`)
- `seq_triggerVoiceGated()` (sequencer.c); the roll engine fires normal triggers

Add a per-slot fade in InstrumentManager, applied by the mixer to the per-slot
dry and send gains. This is constant cost (two multiplies per slot per block)
and needs no new per-sample loop because the existing `voiceGain` /
`mixer_voice_last_gain` interpolation smooths each block.

New InstrumentManager state: `gateFade[6]` (1.0 = open),
`gateFadeStep[6]` (0 = not ramping), `volatile uint8_t gateDuck[6]`.

New API and comment blocks:
```text
/* Arm a ~5 ms fade-out on a track's voice (S079 P2 §5).
 *
 * What: maps a visible trigger track to its slot and sets that slot's duck
 * request. Why: the roll fill boundary must duck the sounding voice with a
 * fast fade instead of altering the voice's decay parameter. Inputs: visible
 * trigger track 0..6. Output: gateDuck[slot] set; the audio path arms the fade
 * at the next block. Context: called from the TIM3 sequencer ISR; a single
 * byte store is atomic on Cortex-M7. Affiliates: seq_processRollTicks().
 */
void instrumentManager_requestGateDuck(uint8_t trigger_track);

/* Current per-slot gate fade multiplier, 1.0 = open, 0.0 = fully ducked.
 *
 * Read by the mixer each block and multiplied into the slot's dry and send
 * gains. Affiliates: mixer_calcNextSampleBlock().
 */
float instrumentManager_gateGain(uint8_t slot);
```

Behaviour:
- `instrumentManager_triggerTrack()` sets `gateFade[slot] = 1.0f` and
  `gateFadeStep[slot] = 0.0f` on every trigger. This is the single trigger
  funnel (all sequencer, MIDI, and preview triggers pass through
  `voiceControl_triggerNow()` -> `instrumentManager_triggerTrack()`), so any
  new trigger restores full gain.
- `instrumentManager_calcSlotAsync()` (once per block per slot) consumes
  `gateDuck[slot]`, arms `gateFadeStep = 32 / (0.005 x SR)`, and advances
  `gateFade -= gateFadeStep` (clamped at 0, step cleared when done).
- Scene instrument replacement resets the slot fade to open.

Mixer change (`mixer_calcNextSampleBlock()` render loop): multiply
`voiceGain` and `sendGain` by `instrumentManager_gateGain(slot)` before the
add calls.

Roll engine change (sequencer.c):
- `seq_rollBegin()`: still computes `ppq`, `slots`, `filled`; the #1 slot
  always fires normally (no gated variant); clears any pending duck.
- New `seq_rollDuckDelay[7]` (PPQ ticks). When the last retrigger of a
  fill < 100 window fires, set
  `seq_rollDuckDelay = filled - (slots - 1) * ppq`. The tick engine decrements
  it; at 0 it calls `instrumentManager_requestGateDuck(track)`.
- Carry-over cancels `seq_rollDuckDelay` (the chain continues with no interior
  duck). This removes the P1 fill < 100 carry deviation entirely.
- fill 0 gives `filled = 0` and a zero duck delay: a single hit ducked within
  the first block.

Worked example (the report): step = 1/4 (96 ticks), roll = 1/8 (ppq 48),
fill = 75. `filled = 72`, `slots = 2`; retriggers at t = 0 and t = 48;
duck delay = 72 - 48 = 24 ticks (a 1/16). The second hit plays 1/16 then fades
out over ~5 ms. Matches the requested behaviour.

Resolved (Q3-Q5): block-ramped fade (about 7 blocks at 5 ms; the mixer's
existing per-block gain interpolation smooths each step), a duration constant
`ROLL_FILL_FADE_MS` in config.h (default 5), and both the dry and the send
gain are ducked.

---

## 6. Affected files

| File | Changes |
|------|---------|
| `Core/Menu/menu.h` | `menu_toggleStepEditSubpage` decl; `LONG_RHYTHM` enum |
| `Core/Menu/menu.c` | toggle helper; scroll-sign arm; scroll entry + automation page predicate; rhythm list/stepper; decoder; `valueNames` |
| `Core/Menu/MenuText.h` | `cdn`/`tim`/`rtm` short labels; `Conditin`/`% Delay`/`Rhythm` long names |
| `Core/Hardware/frontPanel/buttonHandler.c` | STEP-mode re-press toggle |
| `Core/DSP/Instruments/InstrumentManager.c/h` | fade state + duck API; remove gate-decay fn |
| `Core/MIDI/MidiVoiceControl.c/h` | remove gated note-on + bit-7 decode |
| `Core/Sequencer/sequencer.c` | remove gated trigger; duck scheduling + state; roll + duck resets at Scene change sites |
| `Core/DSPAudio/mixer.c` | multiply voice/send gain by the gate fade |

Sequencer reset sites: `seq_selectActivePattern()` and
`seq_setTrackPlayedScene()` do not currently reset the base roll state
(`seq_rollActive`, etc.). Both now receive roll + duck resets so that a
Scene change cancels any running roll and its pending duck.

No change to PatternData, PatternStackService, ParameterArray, the pool format,
or the PAT4 version.

---

## 7. Verification

- Build: `make all` then `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
- DSP: the mixer gain multiply and the fade advance are new per-block work;
  measure and classify with `tools/dsp_test` per the project DSP policy. The
  fade is constant cost (the multiply runs every block, ramping or not).
- Hardware:
  1. STEP page: press a step, press it again -> subpage 1 <-> 2 toggles;
     a different step always opens subpage 1.
  2. Roll page shows `>` in the last column.
  3. Rhythm cell reads `rtm`, full view reads "Velocity / Rhythm", and the pot
     steps off -> (all `-`) -> (all `+`) with no `1-1`/`1+1`.
  4. Roll with fill < 100: the last hit is ducked with a fast fade and the
     voice's decay is unchanged; subsequent steps and Scene changes are
     unaffected.

---

## 8. Resolved decisions

- Q1 (list size) and Q2 (order): see S4b - cycles 2..5 only, equal pairs
  excluded, the ten literal `-` entries in the given order followed by the
  ten `+` entries. 21 entries total including `off`.
- Q3 (ramp granularity): block-ramped.
- Q4 (fade duration): a config.h constant `ROLL_FILL_FADE_MS`, default 5 ms.
- Q5 (FX send): duck both the dry output and the send.

The full implementation schedule is in `S079_P2_IMPLEMENTATION.md`.
