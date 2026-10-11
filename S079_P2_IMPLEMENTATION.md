# S079 P2 — Roll Follow-Up Remediation Implementation Schedule

Full code implementation schedule for the four S079 P2 remediation points, per
`S079_P2_ROLL_FOLLOWUP_REMEDIATION.md` and the resolved decisions (Q1-Q5):

- Q1/Q2: rhythm list is cycles 2..5 only, equal pairs excluded, in the literal
  order `off, 1-2, 1-3, 2-3, 1-4, 2-4, 3-4, 1-5, 2-5, 3-5, 4-5` then the
  ten `+` versions (21 entries).
- Q3: block-ramped fade (no new per-sample loop).
- Q4: fade duration is a config.h constant `ROLL_FILL_FADE_MS` (default 5).
- Q5: the fade ducks both the dry output and the FX send.

Scope: STEP-page navigation, roll-rhythm presentation/editing, and the roll
fill gate. No storage-format change (the packed rhythm byte and pool block are
unchanged). Every code change lands with its adjacent contract comment.

Estimated: text roughly +1.5-2.5 KB, BSS +~30 B (fade arrays + duck state).
Current headroom ~209 KB.

---

## Step 1 — config.h: fade duration constant

Insert after `#define DOUBLE_CLICK_TIMEOUT                 300u`:

```c
/*
 * Per-step roll fill gate fade time (S079 P2).
 *
 * What: duration in milliseconds of the fast output-gain fade that ducks a
 * voice when a per-step roll's fill window ends before the step does
 * (roll fill < 100). Why: the roll fill must cut the sounding voice with a
 * short fade instead of altering the voice's decay parameter; keeping the
 * duration in config.h makes the feel easy to tune. Inputs: none. Outputs:
 * InstrumentManager's per-block fade step
 * (OUTPUT_DMA_SIZE / (ROLL_FILL_FADE_MS / 1000 * REAL_FS)). Affiliates:
 * instrumentManager_calcSlotAsync(), instrumentManager_requestGateDuck(),
 * InstrumentManager.c. RAM: none (compile-time).
 */
#define ROLL_FILL_FADE_MS       5u
```

---

## Step 2 — Fix 1: step-button press toggles the two STEP pages

### S2.1 — `Core/Menu/menu.h`: add the toggle declaration

Insert after `void menu_showStepEditPage(void);`:

```c
/*
 * Toggle the STEP edit page between the two step-special subpages (S079 P2).
 *
 * What: switches SEQ_PAGE between subpage 1 (vel/note/condition/timing) and
 * subpage 2 (roll/rhythm/fill), selects parameter 0, resets the automation
 * editor cursor, refreshes endless-pot snapshots, and repaints. Why:
 * re-pressing the already-selected step button must reach the roll settings
 * without the scroll gesture. Inputs: current SEQ subpage. Output: menuIndex
 * moves to the other step-special subpage. Affiliates:
 * buttonHandler_seqButtonPressed().
 */
void menu_toggleStepEditSubpage(void);
```

### S2.2 — `Core/Menu/menu.c`: implement the helper

Add immediately after `menu_showStepEditPage()` (which ends with
`menu_repaintAll();`):

```c
void menu_toggleStepEditSubpage(void)
{
    /*
     * Alternate the STEP edit page between subpage 1 and subpage 2 (S079 P2).
     *
     * What: picks the other step-special subpage, selects its first cell, and
     * repaints; the automation editor cursor is reset so a later scroll into
     * the automation list starts clean. Why: repeated presses of the selected
     * step button must toggle the two pages. Inputs: current SEQ subpage.
     * Output: menuIndex on subpage 1 or 2 at parameter 0. Affiliates:
     * buttonHandler_seqButtonPressed(), menu_moveToMenuItem() (the scroll
     * chain is the other route between these subpages).
     */
    uint8_t sub = menu_getSubPage();
    uint8_t next = (uint8_t)((sub == 1u) ? 2u : 1u);

    menu_stepAutomationReset();
    menuIndex = (uint8_t)((next << PAGE_SHIFT) | 0u);
    menu_endlessPotMappingChanged();
    menu_repaintAll();
}
```

### S2.3 — `Core/Hardware/frontPanel/buttonHandler.c`: route the STEP press

In `buttonHandler_seqButtonPressed()`, the non-shift `SELECT_MODE_STEP`
arm.

Old:
```c
        case SELECT_MODE_STEP:
            led_clearAllBlinkLeds();
            buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            break;
```

New:
```c
        case SELECT_MODE_STEP:
            /*
             * Re-press of the selected step toggles the two step pages
             * (S079 P2 fix 1).
             *
             * What: a press on the step that is already selected while the
             * STEP edit page is showing alternates SEQ subpage 1 and 2; any
             * other press selects the step and opens subpage 1 as before. Why:
             * the roll settings must be reachable from the step row without
             * the scroll gesture. Inputs: the visible step, the selected step,
             * and the current subpage. Output: menu_toggleStepEditSubpage() or
             * buttonHandler_selectActiveStep(). Affiliates:
             * menu_toggleStepEditSubpage(), menu_showStepEditPage().
             */
            if (buttonHandler_visibleStep(seqButtonPressed) ==
                    buttonHandler_selectedStep &&
                menu_activePage == SEQ_PAGE &&
                (menu_getSubPage() == 1u || menu_getSubPage() == 2u)) {
                menu_toggleStepEditSubpage();
            } else {
                led_clearAllBlinkLeds();
                buttonHandler_selectActiveStep(ledNr, seqButtonPressed);
            }
            break;
```

The SHIFT+step arm (`buttonHandler_setRemoveStep`) is untouched.

---

## Step 3 — Fix 2: `>` scroll marker, scroll entry, and automation page update

### S3.1 — `Core/Menu/menu.c`: `checkScrollSign()`

Immediately after the existing SEQ subpage-1 block:

```c
    if (menu_activePage == SEQ_PAGE && activePage == 1u &&
        !menu_stepAutoActive && activeParameter <= 3u)
        return '>';
```

insert:

```c
    /*
     * Roll subpage leads right into the automation list (S079 P2 fix 2).
     *
     * What: shows '>' on SEQ subpage 2 positions 0..2, matching the subpage-1
     * affordance. Why: the roll page must advertise that the automation list
     * follows to its right. Affiliates: menu_moveToMenuItem() subpage chain.
     */
    if (menu_activePage == SEQ_PAGE && activePage == 2u &&
        !menu_stepAutoActive && activeParameter <= 2u)
        return '>';
```

### S3.2 — `Core/Menu/menu.c`: `menu_moveToMenuItem()` — scroll from subpage 2 into automations

Immediately after the existing subpage-1 automation-entry block (the block
ending with `return;` after `menuIndex = (uint8_t)(1u << PAGE_SHIFT);`),
insert:

```c
    /*
     * Roll fill is the last fixed cell on subpage 2 (S079 P2). Scrolling right
     * from it enters the same variable automation list as subpage 1's
     * micro-timing transition. menuIndex stays on subpage 2 so the automation
     * left-exit returns here.
     */
    if (menu_activePage == SEQ_PAGE && activePage == 2 &&
        !menu_stepAutoActive && activeParameter == 2 && inc > 0) {
        menu_stepAutoActive = 1u;
        menu_stepAutoPageIndex = 0u;
        menuIndex = (uint8_t)(2u << PAGE_SHIFT);
        return;
    }
```

### S3.3 — `Core/Menu/menu.c`: `menu_stepAutomationPageActive()` — recognise subpage 2

Change the `activePage == 1u` guard to accept either step-special subpage:

Old:
```c
    return (uint8_t)(menu_stepAutoActive && menu_activePage == SEQ_PAGE &&
                     (((menuIndex & MASK_PAGE) >> PAGE_SHIFT) == 1u));
```

New:
```c
    return (uint8_t)(menu_stepAutoActive && menu_activePage == SEQ_PAGE &&
                     (((menuIndex & MASK_PAGE) >> PAGE_SHIFT) >= 1u));
```

### S3.4 — `Core/Menu/menu.c`: `menu_moveToMenuItem()` — automation left-exit lands on subpage 2

In the automation left-exit path (the `else` block that sets
`menu_stepAutoActive = 0u` and writes menuIndex), change the target:

Old:
```c
            menuIndex = (uint8_t)((1u << PAGE_SHIFT) | 3u);
```

New:
```c
            menuIndex = (uint8_t)((2u << PAGE_SHIFT) | 2u);
```

This exits to subpage 2 position 2 (roll fill) so the user is on the roll
page after scrolling left out of the automation list.

---

## Step 4 — Fix 3: roll rhythm parameter

### S4.1 — `Core/Menu/MenuText.h`: short and long labels

shortNames S079 block (includes the P1 text corrections for condition and
micro-timing so this schedule is self-contained):
```
{"con"},{"dly"},{"rol"},{"rrt"},{"rfl"},   ->   {"cdn"},{"tim"},{"rol"},{"rtm"},{"rfl"},
```

longNames S079 block:
```
{"Conditn"},{"MicTimng"},{"RollRate"},{"RlRhythm"},{"RollFill"},
   ->   {"Conditin"},{"% Delay"},{"RollRate"},{"Rhythm"},{"RollFill"},
```

Update the block comment to read `cdn/tim/rol/rtm/rfl`. No enum renumbering:
`TEXT_CONDITION`, `TEXT_MICROTIMING`, `TEXT_ROLL_RHYTHM` are unchanged.

### S4.2 — `Core/Menu/menu.c`: rhythm valueNames category

Old:
```c
    {SHORT_ROLL_RHYTHM,CAT_STEP,LONG_ROLL_RHYTHM},
```
New:
```c
    {SHORT_ROLL_RHYTHM,CAT_VELOCITY,LONG_ROLL_RHYTHM},
```
(the `LONG_ROLL_RHYTHM` string is now `"Rhythm"` from S4.1).

### S4.3 — `Core/Menu/menu.c`: `menu_getRollRhythmName()` decoder

Replace the body so `off` covers every value that never changes velocity:

```c
/*
 * Format one roll rhythm byte as the N-M/N+M cycle label (S079 §5.1; S079 P2
 * fix 3).
 *
 * What: decodes bits 7..4 (cycle), bit 3 (0 halve, 1 double), bits 2..0 (full
 * count), and writes "<full><dir><cycle>" for a listed pairing. Byte 0, cycle
 * 0, full 0, and full >= cycle never change velocity and display as "off"
 * (1-1/1+1 are equivalent to off and are not offered). Inputs: packed rhythm
 * byte and a three-character output. Outputs: exactly three characters.
 * Affiliates: stepRollRhythmBytes[], menu_rollRhythmStep().
 */
static void menu_getRollRhythmName(uint16_t value, char *buf)
{
    uint8_t r = (uint8_t)(value & 0xFFu);
    uint8_t cycle = (uint8_t)((r >> 4u) & 0x0Fu);
    uint8_t dir = (uint8_t)((r >> 3u) & 1u);
    uint8_t full = (uint8_t)(r & 0x07u);

    if (cycle < 2u || full == 0u || full >= cycle) {
        memcpy(buf, "off", 3);
        return;
    }
    buf[0] = (char)('0' + full);
    buf[1] = (dir == 0u) ? '-' : '+';
    buf[2] = (char)('0' + cycle);   /* cycle is 2..5 */
}
```

### S4.4 — `Core/Menu/menu.c`: ordered list and stepper

Add near `menu_getRollRhythmName()` (and add a forward declaration with
the other static prototypes):

```c
/*
 * Ordered roll-rhythm list (S079 P2 fix 3).
 *
 * What: the packed rhythm bytes the STEP rhythm cell cycles through: off,
 * then the ten halve ("-") patterns (cycle 2..5, full < cycle) in the requested
 * order, then the same ten as double ("+"). Why: equal pairs (full == cycle)
 * never change velocity and are omitted; the packed encoding is sparse, so the
 * editor steps this list instead of using raw value arithmetic. Inputs: none.
 * Affiliates: menu_rollRhythmStep(), menu_getRollRhythmName().
 */
static const uint8_t stepRollRhythmBytes[] = {
    0x00u,                                   /* off */
    0x21u, 0x31u, 0x32u, 0x41u, 0x42u,       /* 1-2 1-3 2-3 1-4 2-4 */
    0x43u, 0x51u, 0x52u, 0x53u, 0x54u,       /* 3-4 1-5 2-5 3-5 4-5 */
    0x29u, 0x39u, 0x3Au, 0x49u, 0x4Au,       /* 1+2 1+3 2+3 1+4 2+4 */
    0x4Bu, 0x59u, 0x5Au, 0x5Bu, 0x5Cu        /* 3+4 1+5 2+5 3+5 4+5 */
};

/*
 * Step the packed roll-rhythm byte along the ordered list (S079 P2 fix 3).
 *
 * What: finds the byte's list position (an unlisted byte maps to off), applies
 * the signed delta, clamps to the list ends, and returns the new packed byte.
 * Why: the rhythm must move through the offered list, not raw byte values.
 * Inputs: current packed byte and a signed delta. Output: the new packed byte.
 * Affiliates: menu_encoderChangeParameter(), menu_parseKnobDelta(),
 * menu_clampCellValue().
 */
static uint16_t menu_rollRhythmStep(uint16_t byte, int32_t delta)
{
    const int32_t count = (int32_t)(sizeof(stepRollRhythmBytes) /
                                    sizeof(stepRollRhythmBytes[0]));
    int32_t index = 0;
    int32_t i;

    for (i = 0; i < count; i++) {
        if (stepRollRhythmBytes[i] == (uint8_t)byte) {
            index = i;
            break;
        }
    }
    index += delta;
    if (index < 0)
        index = 0;
    else if (index >= count)
        index = count - 1;
    return (uint16_t)stepRollRhythmBytes[index];
}
```

Forward declaration (with the other static prototypes near
`menu_getRollRhythmName`):

```c
static uint16_t menu_rollRhythmStep(uint16_t byte, int32_t delta);
```

### S4.5 — `Core/Menu/menu.c`: encoder edit hook

In `menu_encoderChangeParameter()`, after
`value = menu_cellDisplayValue(&cell);` and the LFO/velocity target
early-returns, and before the `DTYPE_TARGET_SELECTION` block, insert:

```c
    /*
     * Roll rhythm is a sparse packed byte, so it scrolls its own list (S079 P2
     * fix 3) instead of the generic value+delta arithmetic.
     */
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_STEP_RHYTHM) {
        value = menu_rollRhythmStep(value, inc);
        (void)menu_cellCommitValue(&cell, value);
        return;
    }
```

### S4.6 — `Core/Menu/menu.c`: endless-pot edit hook

In `menu_parseKnobDelta()`, at the same point (after
`value = menu_cellDisplayValue(&cell);` and the target early-returns,
before the `DTYPE_TARGET_SELECTION` block), insert:

```c
    /* Roll rhythm scrolls its ordered list (S079 P2 fix 3). */
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_STEP_RHYTHM) {
        value = menu_rollRhythmStep(value, delta);
        if (menu_cellCommitValue(&cell, value))
            menu_knobs_dirty = 1;
        return;
    }
```

### S4.7 — `Core/Menu/menu.c`: clamp snaps to the list

Replace the `PAR_STEP_RHYTHM` case in `menu_clampCellValue()`:

Old:
```c
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_STEP_RHYTHM) {
        if (*value > 255u)
            *value = 255u;
        return;
    }
```
New:
```c
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_STEP_RHYTHM) {
        /* Snap any unlisted byte to a list entry (off by default, S079 P2). */
        *value = menu_rollRhythmStep(*value, 0);
        return;
    }
```

---

## Step 5 — Fix 4: roll fill duck (fast fade-out)

### S5.1 — Remove the decay mutation and the gated-trigger path

`Core/DSP/Instruments/InstrumentManager.c` / `.h`: delete
`instrumentManager_applyTriggerGateDecay()` (declaration + definition).

`Core/MIDI/MidiVoiceControl.c` / `.h`: delete
`voiceControl_noteOnGated()` (declaration + definition) and restore the
plain dequeue in `voiceControl_processPending()`:

```c
void voiceControl_processPending(void)
{
    VoiceTriggerEvent ev;

    while (voiceControl_popPending(&ev)) {
        voiceControl_triggerNow(ev.voice, ev.note, ev.vel);
    }
}
```

`Core/Sequencer/sequencer.c`: delete `seq_triggerVoiceGated()` and its
two calls in `seq_rollBegin()` (see S5.7).

### S5.2 — `InstrumentManager.h`: duck API

Add (replacing the removed gate-decay declaration):

```c
/*
 * Arm a fast gain fade-out on a track's voice (S079 P2 fix 4).
 *
 * What: maps a visible trigger track to its storage slot and sets that slot's
 * duck request. Why: a per-step roll's fill boundary must duck the sounding
 * voice with a short output fade instead of altering the voice's decay
 * parameter (which was the P1 bug). Inputs: visible trigger track 0..6.
 * Output: the slot's duck request is set; the audio path arms the fade at the
 * next rendered block. Context: called from the TIM3 sequencer ISR; a single
 * byte store is atomic on Cortex-M7. Affiliates: seq_processRollTicks(),
 * instrumentManager_calcSlotAsync().
 */
void instrumentManager_requestGateDuck(uint8_t trigger_track);

/*
 * Current per-slot roll fill gate gain, 1.0 = open, 0.0 = fully ducked
 * (S079 P2 fix 4).
 *
 * What: returns the live fade multiplier for one slot. Why: the mixer folds it
 * into the slot's dry and send gains. Inputs: zero-based slot. Output:
 * 0.0..1.0. Affiliates: mixer_faderGains().
 */
float instrumentManager_gateGain(uint8_t slot);
```

### S5.3 — `InstrumentManager.c`: fade state

Add with the other file-scope state (near `runtime_slots[]`):

```c
/*
 * Per-slot roll fill gate fade (S079 P2 fix 4).
 *
 * What: gateFade is the live multiplier (1.0 open, 0.0 ducked); gateFadeStep
 * is the per-block decrement while a fade runs (0 when idle); gateDuck is the
 * request set by the sequencer and consumed once per block. Why: the roll fill
 * boundary ducks the voice with a ~ROLL_FILL_FADE_MS fade applied by the mixer
 * to the dry and send gains. The mixer's existing per-block gain interpolation
 * smooths each step, so no per-sample loop is added. Inputs:
 * instrumentManager_requestGateDuck(), instrumentManager_triggerTrack() (resets
 * to open). Outputs: instrumentManager_gateGain(). RAM: 6 x (4 + 4 + 1) B.
 * Affiliates: mixer_faderGains(), instrumentManager_calcSlotAsync().
 */
static float instrumentManager_gateFade[INSTRUMENT_SLOT_COUNT];
static float instrumentManager_gateFadeStep[INSTRUMENT_SLOT_COUNT];
static volatile uint8_t instrumentManager_gateDuck[INSTRUMENT_SLOT_COUNT];

/* Per-block fade decrement for ROLL_FILL_FADE_MS at the real sample rate. */
static const float instrumentManager_gateFadeStepRate =
    (float)OUTPUT_DMA_SIZE / ((float)ROLL_FILL_FADE_MS * 0.001f * REAL_FS);
```

In `instrumentManager_runtimeInit()`, inside the existing slot loop, seed
the fade open:

```c
        instrumentManager_gateFade[slot] = 1.0f;
        instrumentManager_gateFadeStep[slot] = 0.0f;
        instrumentManager_gateDuck[slot] = 0u;
```

In `instrumentManager_resetRuntimeSlotFromScene()`, after the switch,
reset the slot fade (a Scene/slot change opens the gate):

```c
    /* S079 P2 fix 4: a rebind opens the slot's gate fade. */
    instrumentManager_gateFade[slot] = 1.0f;
    instrumentManager_gateFadeStep[slot] = 0.0f;
    instrumentManager_gateDuck[slot] = 0u;
```

### S5.4 — `InstrumentManager.c`: reset the fade on every trigger

In `instrumentManager_triggerTrack()`, right after `slot` is resolved
and before the type switch:

```c
    /*
     * S079 P2 fix 4: a new trigger restores the slot's gate fade to open. This
     * is the single trigger funnel (sequencer, MIDI, and preview all arrive
     * through voiceControl_triggerNow()), so any retrigger cancels a duck.
     */
    instrumentManager_gateFade[slot] = 1.0f;
    instrumentManager_gateFadeStep[slot] = 0.0f;
```

### S5.5 — `InstrumentManager.c`: consume the duck and advance the fade

In `instrumentManager_calcSlotAsync()` (called once per block per slot),
before the type switch:

```c
    /*
     * S079 P2 fix 4: roll fill gate fade. Consume a pending duck request, then
     * advance the per-block ramp. When no fade runs the step is 0 and the gain
     * stays 1.0; the multiply cost is paid in the mixer every block either way.
     */
    if (instrumentManager_gateDuck[slot]) {
        instrumentManager_gateDuck[slot] = 0u;
        instrumentManager_gateFade[slot] = 1.0f;
        instrumentManager_gateFadeStep[slot] =
            instrumentManager_gateFadeStepRate;
    }
    if (instrumentManager_gateFadeStep[slot] > 0.0f) {
        instrumentManager_gateFade[slot] -=
            instrumentManager_gateFadeStep[slot];
        if (instrumentManager_gateFade[slot] <= 0.0f) {
            instrumentManager_gateFade[slot] = 0.0f;
            instrumentManager_gateFadeStep[slot] = 0.0f;
        }
    }
```

### S5.6 — `InstrumentManager.c`: the two API functions

Add near `instrumentManager_triggerTrack()`:

```c
void instrumentManager_requestGateDuck(uint8_t trigger_track)
{
    uint8_t slot;

    if (trigger_track > INSTRUMENT_SLOT_COUNT)
        return;
    slot = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u)
        : trigger_track;
    instrumentManager_gateDuck[slot] = 1u;
}

float instrumentManager_gateGain(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 1.0f;
    return instrumentManager_gateFade[slot];
}
```

### S5.7 — `Core/DSPAudio/mixer.c`: fold the fade into the gains

In `mixer_faderGains()`, after the existing mode branches and before the
closing brace:

```c
    /*
     * S079 P2 fix 4: fold the roll fill gate fade into both taps. The dry and
     * the send follow the same duck; the caller's last-gain interpolation
     * smooths the per-block steps.
     */
    {
        const float gate = instrumentManager_gateGain(slot);
        *mix_gain *= gate;
        *send_gain *= gate;
    }
```

### S5.8 — `Core/Sequencer/sequencer.c`: duck scheduling and gated-trigger removal

Add three state arrays beside the other roll state (S079 P1 S3.1):

```c
/*
 * Per-track roll fill duck countdown (S079 P2 fix 4).
 *
 * What: seq_rollDuckDelta is the number of PPQ ticks from the last retrigger
 * of a fill < 100 step to the fill-window end; seq_rollDuckDelay is the live
 * countdown and seq_rollDuckActive marks it running. Why: the fill boundary
 * must duck the sounding voice with a fast fade instead of the removed gated
 * trigger. Inputs: set by seq_rollBegin()/seq_rollArmDuck(). Outputs:
 * instrumentManager_requestGateDuck() when the countdown expires. RAM: 21 B
 * SRAM1 static. Affiliates: seq_processRollTicks().
 */
static uint8_t seq_rollDuckDelta[NUM_TRACKS];
static uint8_t seq_rollDuckDelay[NUM_TRACKS];
static uint8_t seq_rollDuckActive[NUM_TRACKS];
```

Add the arm helper before `seq_rollBegin()`:

```c
/*
 * Arm the fill-window duck for one track (S079 P2 fix 4).
 *
 * What: starts the duck countdown from the step's stored delta when the roll
 * is active and the fill is below 100. Why: the duck is scheduled when the
 * last retrigger of the fill window has fired. Inputs: per-track roll state.
 * Output: seq_rollDuckDelay/Active armed. Affiliates: seq_processRollTicks().
 */
static void seq_rollArmDuck(uint8_t track)
{
    if (!seq_rollActive[track] || seq_rollLastFill[track] >= 100u)
        return;
    seq_rollDuckDelay[track] = seq_rollDuckDelta[track];
    seq_rollDuckActive[track] = 1u;
}
```

In `seq_rollBegin()`:

- Replace the two `seq_triggerVoiceGated(...)` call sites with plain
  `seq_triggerVoice(...)` (the #1 retrigger always plays normally).
- Replace the old final-gate comment/branch with the delta computation and
  duck-cancel:

```c
    /* S079 P2 fix 4: the fill-window end schedules a duck, not a gated hit. */
    seq_rollDuckActive[track] = 0u;              /* cancel any interior duck */
    if (sp->fill < 100u)
        seq_rollDuckDelta[track] = (uint8_t)(
            filled - (uint16_t)((slots - 1u) * ppq));
    else
        seq_rollDuckDelta[track] = 0u;
```

- After the #1 retrigger is fired (non-carry) or the slot count is extended
  (carry), and after `seq_rollRemaining[track]` is set, add:

```c
    if (seq_rollRemaining[track] == 0u)
        seq_rollArmDuck(track);                  /* slots == 1 case */
```

In `seq_processRollTicks()`:

- At the top of the per-track loop, before the retrigger logic, service the
  duck countdown:

```c
        if (seq_rollDuckActive[track]) {
            if (seq_rollDuckDelay[track] == 0u) {
                instrumentManager_requestGateDuck(track);
                seq_rollDuckActive[track] = 0u;
            } else {
                seq_rollDuckDelay[track]--;
            }
        }
```

- Replace the "gate trigger" branch after the last retrigger with a plain
  trigger plus duck arming:

```c
        vel = seq_rollComputeVelocity(track);
        seq_rollRemaining[track]--;
        if (seq_rollRemaining[track] == 0u)
            seq_rollArmDuck(track);
        seq_triggerVoice(track, vel, seq_rollStepNote[track]);
        seq_rollTickAccum[track] = (uint8_t)(ppq - 1u);
        seq_rollVelPos[track]++;
```

In `seq_advanceTrackStep()`, where a step with no roll clears the chain,
also cancel a pending duck:

```c
		/* A step with no roll ends any running per-step roll and duck
		 * (S079 §4.2, S079 P2 fix 4). */
		if (sp.roll == 0u) {
			seq_rollActive[track] = 0u;
			seq_rollDuckActive[track] = 0u;
		}
```

### S5.9 — `Core/Sequencer/sequencer.c`: reset the roll and duck state

**Sites that already reset roll state** — `seq_init()` (memset block at
line 775) and `seq_setStepIndexToStart()` (per-track loop at line 3253):
add the three duck arrays alongside the existing roll resets:

```c
		seq_rollDuckActive[i] = 0u;
		seq_rollDuckDelay[i] = 0u;
		seq_rollDuckDelta[i] = 0u;
```

**Sites that do NOT currently reset roll state** —
`seq_selectActivePattern()` (after the `memset(seq_trackLastConditionFired)`
block) and `seq_setTrackPlayedScene()` (after the per-track condition
resets): add both the base roll resets AND the duck resets. A Scene change
must cancel any running roll and its pending duck.

In `seq_selectActivePattern()`, after the condition-state resets, add:

```c
	/* S079 P2 fix 4: a Scene change cancels running rolls and pending ducks. */
	memset(seq_rollActive, 0, sizeof(seq_rollActive));
	memset(seq_rollTickAccum, 0, sizeof(seq_rollTickAccum));
	memset(seq_rollVelPos, 0, sizeof(seq_rollVelPos));
	memset(seq_rollRemaining, 0, sizeof(seq_rollRemaining));
	memset(seq_rollLastRate, 0, sizeof(seq_rollLastRate));
	memset(seq_rollLastRhythm, 0, sizeof(seq_rollLastRhythm));
	memset(seq_rollLastFill, 0, sizeof(seq_rollLastFill));
	memset(seq_rollDuckActive, 0, sizeof(seq_rollDuckActive));
	memset(seq_rollDuckDelay, 0, sizeof(seq_rollDuckDelay));
	memset(seq_rollDuckDelta, 0, sizeof(seq_rollDuckDelta));
```

In `seq_setTrackPlayedScene()`, after the per-track condition resets (using
the per-track/two-track form that matches the existing HiHat pair logic):

```c
	/* S079 P2 fix 4: per-track Scene change cancels the track's roll + duck. */
	seq_rollActive[track] = 0u;
	seq_rollTickAccum[track] = 0u;
	seq_rollVelPos[track] = 0u;
	seq_rollRemaining[track] = 0u;
	seq_rollLastRate[track] = 0u;
	seq_rollLastRhythm[track] = 0u;
	seq_rollLastFill[track] = 0u;
	seq_rollDuckActive[track] = 0u;
	seq_rollDuckDelay[track] = 0u;
	seq_rollDuckDelta[track] = 0u;
	if (track >= 5u) {
		uint8_t pair = (track == 5u) ? 6u : 5u;
		seq_rollActive[pair] = 0u;
		seq_rollTickAccum[pair] = 0u;
		seq_rollVelPos[pair] = 0u;
		seq_rollRemaining[pair] = 0u;
		seq_rollLastRate[pair] = 0u;
		seq_rollLastRhythm[pair] = 0u;
		seq_rollLastFill[pair] = 0u;
		seq_rollDuckActive[pair] = 0u;
		seq_rollDuckDelay[pair] = 0u;
		seq_rollDuckDelta[pair] = 0u;
	}
```

### S5.10 — Includes

`sequencer.c` already includes `InstrumentManager.h`;
`mixer.c` already includes `InstrumentManager.h`. No new includes.

---

## Affected files summary

| File | Changes |
|------|---------|
| `config.h` | `ROLL_FILL_FADE_MS` constant |
| `Core/Menu/menu.h` | `menu_toggleStepEditSubpage` declaration |
| `Core/Menu/menu.c` | toggle helper; scroll-sign arm; subpage-2 automation entry + left-exit + predicate; rhythm decoder, list, stepper, edit hooks, clamp |
| `Core/Menu/MenuText.h` | `cdn`/`tim`/`rtm` short labels; `Conditin`/`% Delay`/`Rhythm` long names |
| `Core/Hardware/frontPanel/buttonHandler.c` | STEP-mode re-press toggle |
| `Core/DSP/Instruments/InstrumentManager.c/h` | gate-fade state + duck/gateGain API; remove gate-decay |
| `Core/MIDI/MidiVoiceControl.c/h` | remove gated note-on + bit-7 decode |
| `Core/Sequencer/sequencer.c` | remove gated trigger; duck state/scheduling; roll + duck resets at all four sites |
| `Core/DSPAudio/mixer.c` | fold the gate fade into dry + send gains |

No change to PatternData, PatternStackService, ParameterArray, the pool format,
or the PAT4 version.

---

## Build verification

After all changes: `make all`, then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

Expected: text roughly +1.5-2.5 KB, BSS +~30 B, no linker errors.

DSP: the mixer now multiplies two gains per slot per block by
`instrumentManager_gateGain()`; the fade advances in
`instrumentManager_calcSlotAsync()`. Classify the change with
`tools/dsp_test` per the project DSP policy (the multiply runs every block
whether or not a fade is active, so cost is constant).

Hardware checklist:

1. STEP page: press a step, press it again -> subpage 1 <-> 2 toggles; a
   different step always opens subpage 1.
2. Roll page shows `>` in the last column; scrolling right enters the
   automation list; scrolling left from automations returns to the roll page.
3. Condition cell reads `cdn`, timing reads `tim`; rhythm cell reads `rtm`;
   the clicked-in view shows "Conditin" / "% Delay" / "Velocity /
   Rhythm"; the pot steps `off, 1-2, 1-3, 2-3, 1-4, 2-4, 3-4, 1-5, 2-5, 3-5,
   4-5, 1+2 ... 4+5` and clamps at both ends.
4. Roll with fill < 100: the last hit is ducked by a fast ~5 ms fade, the
   voice decay is NOT changed, and later steps / Scene changes are unaffected.
   Adjust `ROLL_FILL_FADE_MS` to taste.

---

## Implementation progress log (agent, 2026-10-10)

Baseline before any P2 edit: `make all` OK; text 543,904 / data 408 / bss
427,696; flash 544,312 / 753,664 B used, headroom 209,352 B (ITCM 4,168 B,
DTCM statics 4,472 B, FX arena margin 3,712 B). This matches the S079 P1
post-fix build, so the P1 working tree is the starting point.

Pre-existing working-tree context: the P1 addendum already landed the STEP
subpage chain (sub 1 -> sub 2 -> automation), the
`menu_stepAutomationPageActive()` `== 2u` predicate, the left-exit to sub 2
pos 2, the `cdn`/`tim` short labels, the `Conditin`/`% Delay` long names, and
the track-7 mute pair. Those P2 schedule items (S3.2/S3.3/S3.4, S4.1 partially)
were therefore already in tree; the remaining work is listed per step below.

### Step 1 — config.h `ROLL_FILL_FADE_MS` — DONE

Added the `ROLL_FILL_FADE_MS` (5 ms) constant with its contract comment directly
after `DOUBLE_CLICK_TIMEOUT`. No RAM cost.

### Step 2 — STEP-page re-press toggle — DONE

- `Core/Menu/menu.h`: `menu_toggleStepEditSubpage()` declaration with its
  contract comment, immediately after `menu_showStepEditPage()`.
- `Core/Menu/menu.c`: helper implemented after `menu_showStepEditPage()`
  (`menu_getSubPage()` -> other subpage, `menu_stepAutomationReset()`,
  `menuIndex`, `menu_endlessPotMappingChanged()`, `menu_repaintAll()`).
- `Core/Hardware/frontPanel/buttonHandler.c`: the non-shift
  `SELECT_MODE_STEP` arm in `buttonHandler_seqButtonPressed()` now calls the
  toggle when the pressed visible step equals `buttonHandler_selectedStep` and
  the page is SEQ subpage 1 or 2; otherwise it keeps the existing select
  path. The SHIFT arm (`buttonHandler_setRemoveStep`) is untouched.

Incremental build after Step 2 (before Step 3/4): OK, no new warnings.

### Step 3 — roll-subpage `>` marker and automation entry — DONE (partial
pre-existing)

- S3.1: `checkScrollSign()` gained the subpage-2 arm (positions 0..2 return
  `>`), with its contract comment.
- S3.2 / S3.3 / S3.4: already present in the working tree from the S079 P1
  addendum (right from sub 2 pos 2 enters automations; left-exit returns to
  sub 2 pos 2; `menu_stepAutomationPageActive()` uses `== 2u`; left from
  sub 2 pos 0 returns to sub 1 pos 3). Verified against the schedule; no
  further change needed. The schedule's "old" snippets (`== 1u`) describe the
  pre-P1-addendum tree.

### Step 4 — roll rhythm presentation and editing — DONE

- S4.1 `MenuText.h`: short label `rrt` -> `rtm`; long name `RlRhythm` ->
  `Rhythm`; block comments updated. (`cdn`/`tim` and `Conditin`/`% Delay`
  were already done by the P1 addendum.)
- S4.2 `menu.c` `valueNames[]`: `SHORT_ROLL_RHYTHM` category
  `CAT_STEP` -> `CAT_VELOCITY` (clicked-in view now "Velocity / Rhythm").
- S4.3 `menu_getRollRhythmName()`: byte 0, cycle 0, full 0, or full >= cycle
  now render `off` (the old 1-1 default is gone).
- S4.4 `stepRollRhythmBytes[21]` ordered list + `menu_rollRhythmStep()`
  added beside the decoder, with a forward declaration in the static
  prototype block (needed by `menu_clampCellValue()`).
- S4.5 / S4.6: encoder and endless-pot edit hooks call `menu_rollRhythmStep()`
  and commit, before the generic value arithmetic.
- S4.7 `menu_clampCellValue()`: `PAR_STEP_RHYTHM` now snaps any unlisted byte
  to a list entry via `menu_rollRhythmStep(*value, 0)`; domain comment updated.

Note: the packed rhythm byte and the pool layout are unchanged, as the
remediation requires.

### Step 5 — roll fill duck (fast fade-out) — DONE

Removals:
- `InstrumentManager.c/.h`: `instrumentManager_applyTriggerGateDecay()`
  deleted (declaration replaced by the two duck/gain declarations).
- `MidiVoiceControl.c/.h`: `voiceControl_noteOnGated()` deleted;
  `voiceControl_processPending()` restored to the plain
  `voiceControl_triggerNow(ev.voice, ev.note, ev.vel)` dequeue (no bit-7
  packing).
- `sequencer.c`: `seq_triggerVoiceGated()` deleted; the roll engine fires
  only `seq_triggerVoice()`.

Additions:
- `InstrumentManager.c`: file-scope `gateFade[6]` / `gateFadeStep[6]` (float)
  and `volatile uint8_t gateDuck[6]`, plus the `gateFadeStepRate` constant
  (`OUTPUT_DMA_SIZE / (ROLL_FILL_FADE_MS/1000 * REAL_FS)`).
  `instrumentManager_runtimeInit()` and
  `instrumentManager_resetRuntimeSlotFromScene()` open the gate;
  `instrumentManager_triggerTrack()` re-opens it on every trigger;
  `instrumentManager_calcSlotAsync()` consumes the duck once per block and
  advances the per-block ramp. `instrumentManager_requestGateDuck()` and
  `instrumentManager_gateGain()` added near `instrumentManager_triggerTrack()`.
- `mixer.c` `mixer_faderGains()`: folds `instrumentManager_gateGain(slot)`
  into both the dry and the send tap; the caller's existing last-gain
  interpolation smooths each block step.
- `sequencer.c`: `seq_rollDuckDelta/Delay/Active[7]`; `seq_rollArmDuck()`
  before `seq_rollBegin()`; `seq_rollBegin()` computes the fill-window delta
  and arms the single-slot case; `seq_processRollTicks()` services the
  countdown at the top of the per-track loop (so it can expire after the
  remaining count hits zero) and arms the duck when the last retrigger fires;
  a step with no roll cancels a pending duck. Roll + duck resets added to
  `seq_init()`, `seq_setStepIndexToStart()`, `seq_selectActivePattern()`
  (now also resets the base roll state), and `seq_setTrackPlayedScene()`.

Implementation deviation (justified): the schedule's arm call in
`seq_rollBegin()` was placed after `seq_rollActive[track] = 1u` and the
`seq_rollLast*` stores, because `seq_rollArmDuck()` reads
`seq_rollActive`/`seq_rollLastFill`. Placing it earlier, as the schedule's
prose implies, would have made the guard fail and the duck never arm.

### Final build (all P2 changes) — DONE

`make all` succeeds with no new source warnings. Sizes:

| Metric | S079 P1 baseline | After S079 P2 | Delta |
|--------|------------------|---------------|-------|
| text   | 543,904 | 544,072 | +168 |
| data   | 408 | 408 | 0 |
| bss    | 427,696 | 427,776 | +80 |
| flash used | 544,312 | 544,480 | +168 |
| headroom | 209,352 | 209,184 | -168 |

BSS +80 B is InstrumentManager's gate arrays (6 x 4 + 6 x 4 + 6 x 1 = 54 B,
padded to 56 B) plus the sequencer duck arrays (3 x 7 = 21 B, padded to 24 B).
This is SRAM1 runtime state in the normal (non-DTCM) region; it lands in the
Pattern/reserved pool side unless the linker map shows otherwise. Note the
schedule's headline estimate said "BSS +~30 B", but its own per-array RAM notes
(54 B + 21 B) match the measured +80 B; the headline was the low figure.

Flash grew only +168 B because the removed gated-trigger path (sequencer
helper, MIDI gated note-on, InstrumentManager decay mutation) offset most of
the new code.

### DSP classification (S079 P2 fix 4)

The mixer change is a *new* feature multiplier, not a refactor of an existing
DSP algorithm, so it is classified by construction:

- **Steady state (no duck active): S0 bit-identical.** `instrumentManager_gateGain()`
  returns exactly `1.0f` (init, rebind, and every trigger set it to `1.0f`),
  and IEEE-754 multiplication by `1.0f` is exact, so `*mix_gain` and
  `*send_gain` reach the combined add functions unchanged. The tested surface
  (`mixer_addVoiceInt16ToOutputAndFx`) is untouched.
- **Duck active: S4 audible, by design** (remediation Q5). The gains are
  intentionally scaled; this is the requested choked fill behaviour, not an
  accidental change.
- **Constant cost:** the gate read and the two multiplies run every block per
  slot whether or not a fade is running; the per-block ramp advance in
  `instrumentManager_calcSlotAsync()` is a couple of compares. No CPU is saved
  when idle, per the DSP CPU policy.

Regression check: `make -C tools/dsp_test mixer` still reports
`mixer differing samples: 0`. The existing harness does not cover
`mixer_faderGains()` (it feeds gains in as test inputs), so the gate multiply
cannot be exercised there; no harness extension was in scope. The ARM
`fpseq.py` control-flow check likewise covers the combined add functions only,
which are unchanged.

### RAM note (project RAM Allocation Approval Policy)

New on-chip RAM, all static and SRAM1 (normal, not DTCM):

- `InstrumentManager.c`: `gateFade[6]` (24 B) + `gateFadeStep[6]` (24 B) +
  `volatile uint8_t gateDuck[6]` (6 B) = 54 B, padded to 56 B. Owner:
  InstrumentManager; lifetime: boot-resident forever.
- `sequencer.c`: `seq_rollDuckDelta/Delay/Active[7]` = 21 B, padded to 24 B.
  Owner: Sequencer; lifetime: boot-resident forever.

Total +80 B BSS. These allocations are the ones the accepted P2 schedule
specified (S5.3 "RAM: 6 x (4 + 4 + 1) B", S5.8 "RAM: 21 B SRAM1 static"), so
they are covered by the plan's acceptance; reported here for the exact
measured figure.

### Comment-only clarifications (no behavioural change)

Byte 0 previously meant "1-1 default" in two decoder doc comments; under P2 it
means "off". Updated for consistency:

- `sequencer.c` `seq_rollComputeVelocity()`: doc comment and the inline
  invalid-encoding note now say off / step velocity instead of 1-1.
- `PatternData.h` `pat_setStepRhythm()`: doc comment now says value 0 is off
  (no velocity change) instead of "the default 1-1 pattern".

These are documentation-only edits; the schedule's "no change to PatternData"
refers to functional/storage changes, none of which were made (the packed byte,
flag bit, pool geometry, and PAT4 version are untouched).

### Status

All five schedule steps are implemented and the tree builds clean with only the
pre-existing warnings (unused `filesystem.c` helpers, the `PatternData.c`
packed-member notice, the `EuklidGenerator.c` sign-compare). Remaining work is
the on-device hardware checklist in the verification section above.

---

## Post-implementation assessment (agent, 2026-10-10)

Full code-vs-schedule verification of every schedule item, reading the
working-tree source against the specification above.

### Step 1 — config.h `ROLL_FILL_FADE_MS` — PASS

`config.h:463` defines `ROLL_FILL_FADE_MS 5u` immediately after
`DOUBLE_CLICK_TIMEOUT`, with the specified contract comment. Matches S1.

### Step 2 — STEP-page re-press toggle — PASS

- `menu.h`: `menu_toggleStepEditSubpage()` declared with contract comment,
  after `menu_showStepEditPage()`. Matches S2.1.
- `menu.c:15870`: helper implemented — reads `menu_getSubPage()`, picks the
  other subpage, calls `menu_stepAutomationReset()`, sets `menuIndex`,
  refreshes pot snapshots, repaints. Matches S2.2.
- `buttonHandler.c:1050–1073`: `SELECT_MODE_STEP` arm tests
  `buttonHandler_visibleStep(seqButtonPressed) == buttonHandler_selectedStep`
  and `menu_getSubPage() == 1u || 2u`; calls toggle on match, falls through
  to `buttonHandler_selectActiveStep()` otherwise. SHIFT arm untouched.
  Matches S2.3.

### Step 3 — `>` marker, scroll entry, automation predicate — PASS (with noted P1 architecture difference)

- **S3.1** (`checkScrollSign`): `menu.c:9822–9831` adds the subpage-2 arm
  (`activePage == 2u`, `activeParameter <= 2u` → returns `>`). Matches.
- **S3.2–S3.4**: the P1 addendum implemented a three-step subpage chain
  rather than the schedule's two-entry-point model:
  - Sub 1 pos 3 → right → sub 2 pos 0 (not automations). (`menu.c:11996`)
  - Sub 2 pos 2 → right → automations, `menuIndex = (2u << PAGE_SHIFT)`. (`menu.c:12001`)
  - Sub 2 pos 0 → left → sub 1 pos 3. (`menu.c:12008`)
  - Left-exit from automations → sub 2 pos 2. (`menu.c:12052`)
  - `menu_stepAutomationPageActive()` uses `== 2u`. (`menu.c:10465`)

  The schedule specified `>= 1u` and separate entry from both subpages.
  The P1 implementation routes sub 1 through sub 2 first, so automations
  are always entered from sub 2 and `== 2u` is the correct predicate for
  that architecture. The result is functionally equivalent: automations
  are reachable from both subpages (by scrolling), and left-exit always
  returns to the roll page. The chain approach is arguably cleaner because
  there is exactly one automation entry point and one exit target.

  **Verdict**: correct; the schedule's "old" snippet (`== 1u`) describes the
  pre-P1 tree, and the implementation log at line 866 documents the
  divergence.

### Step 4 — Roll rhythm presentation and editing — PASS

- **S4.1** (`MenuText.h`):
  - `shortNames` at line 168: `{"cdn"},{"tim"},{"rol"},{"rtm"},{"rfl"}`. The
    `cdn`/`tim` changes were applied by the P1 addendum; `rtm` is the P2
    change. Matches.
  - `longNames` at line 217: `{"Conditin"},{"% Delay"},{"RollRate"},{"Rhythm"},{"RollFill"}`.
    Same split. Matches.
- **S4.2** (`menu.c:1311`): `{SHORT_ROLL_RHYTHM,CAT_VELOCITY,LONG_ROLL_RHYTHM}`.
  Matches.
- **S4.3** (`menu.c:9501–9515`): decoder shows `off` for byte 0, cycle < 2,
  full 0, or full >= cycle. Otherwise `full<dir>cycle`. Matches.
- **S4.4** (`menu.c:9527–9563`): `stepRollRhythmBytes[21]` with the exact
  byte sequence (0x00, then the ten halve, then the ten double).
  `menu_rollRhythmStep()` searches the list, applies delta, clamps. Forward
  declaration at line 1906. Matches.
- **S4.5** (`menu.c:11934–11943`): encoder hook before generic arithmetic,
  tests `MENU_CELL_STATIC` + `PAR_STEP_RHYTHM`, calls stepper, commits.
  Matches.
- **S4.6** (`menu.c:13384–13391`): endless-pot hook, same pattern with
  `menu_knobs_dirty`. Matches.
- **S4.7** (`menu.c:5657–5661`): clamp snaps to the list via
  `menu_rollRhythmStep(*value, 0)`. Matches.

### Step 5 — Roll fill duck (fast fade-out) — PASS

Removals verified (grep returns no hits):
- `instrumentManager_applyTriggerGateDecay` — gone from .c and .h.
- `voiceControl_noteOnGated` — gone from .c and .h.
- `seq_triggerVoiceGated` — gone from sequencer.c.
- `0x80u` velocity packing — gone from MidiVoiceControl.c.
- `voiceControl_processPending()` restored to plain
  `voiceControl_triggerNow(ev.voice, ev.note, ev.vel)`.

Additions verified:
- **S5.2** (`InstrumentManager.h:663,673`): `instrumentManager_requestGateDuck()`
  and `instrumentManager_gateGain()` with contract comments. Matches.
- **S5.3** (`InstrumentManager.c:203–209`): `gateFade[6]`, `gateFadeStep[6]`,
  `volatile uint8_t gateDuck[6]`, `gateFadeStepRate` constant. Init at
  line 1364 (runtimeInit), rebind reset at line 1515. Matches.
- **S5.4** (`InstrumentManager.c:1854–1855`): trigger resets fade to open,
  placed after slot resolution and before the type switch. Matches.
- **S5.5** (`InstrumentManager.c:1704–1722`): duck consume + fade advance in
  `instrumentManager_calcSlotAsync()`, before the type switch. Matches.
- **S5.6** (`InstrumentManager.c:1879–1896`): both API functions. The
  `requestGateDuck` guard (`> INSTRUMENT_SLOT_COUNT`) and the track-to-slot
  mapping match. Matches.
- **S5.7** (`mixer.c:555–564`): fold `instrumentManager_gateGain(slot)` into
  `*mix_gain` and `*send_gain` at end of `mixer_faderGains()`. Matches.
- **S5.8** (sequencer.c): duck arrays at lines 296–298; `seq_rollArmDuck()`
  at line 1587; delta computation in `seq_rollBegin()` at line 1655; slots-1
  arm at line 1662; duck countdown in `seq_processRollTicks()` at line 2575;
  retrigger arm at line 2597; no-roll cancel at line 1875. All match, with
  the justified placement deviation noted in the implementation log.
- **S5.9** (sequencer.c): roll + duck resets at all four sites:
  `seq_init()` (line 790–799), `seq_setStepIndexToStart()` (line 3315–3317),
  `seq_selectActivePattern()` (line 1170–1179, includes base roll resets),
  `seq_setTrackPlayedScene()` (line 936–958, includes HiHat pair). Matches.
- **S5.10**: no new includes needed — confirmed.

### Comment-only clarifications — PASS

- `sequencer.c` `seq_rollComputeVelocity()` doc (line 1529): says "off (no
  velocity change: every retrigger at step velocity)".
- `PatternData.h` `pat_setStepRhythm()` doc (line 509): says "Value 0 is off
  (no velocity change, the default)".
- Both are documentation-only; no functional change.

### Build — PASS

Implementation log reports `make all` clean with +168 text / +80 BSS. The
+80 BSS matches the per-array RAM notes (InstrumentManager 54 B padded to
56 B + sequencer 21 B padded to 24 B). The headline estimate of ~30 B was
low; the per-array notes in the schedule body were accurate.

### Affected files summary — PASS

The table (lines 779–789) matches the actual scope of changes across all
nine file groups. The `sequencer.c` row includes "roll + duck resets at all
four sites".

### Hardware checklist — PASS

All four items (lines 811–821) are testable descriptions of the implemented
behaviour. Item 2 includes the scroll transitions. Item 3 includes the P1
text corrections.

### Issues found: 0

No discrepancies between the schedule and the implementation beyond the
documented S3.2–S3.4 architecture divergence (P1 three-step chain vs.
schedule's two-entry-point model), which is functionally equivalent and
documented in the implementation log. No missing code, no extra code, no
wrong values.
