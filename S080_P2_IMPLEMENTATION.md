# S080 P2 — Defect Remediation & Phase G Implementation Schedule

Every change cited by **file**, **line**, and **add/remove/modify**. Comment-block
descriptions are written to serve as documentation-in-place adjacent to the changes
in both `.c` and `.h` files. Organized by implementation step from the defect plan
(S080_P2_PERF_EXPANSION_DEFECTS.md).

---

## Step 1 — Remove duplicate pcr parameter + reorder compressor page (Defects 5 + 6)

### 1.1 ParameterArray.h — Remove PAR_SCENE_PC_RESET

**File:** `Core/Bank/Scene/Preset/ParameterArray.h`
**Line:** 246
**Action:** REMOVE

Remove the `PAR_SCENE_PC_RESET,` line. Update the preceding comment block
(lines 230–241) to remove all references to pcr / PAR_SCENE_PC_RESET and
change "scd, qcd, qac, pcr" to "scd, qcd, qac".

Old (lines 230–246):
```c
	/*
	 * S080 scene queue / change division globals (scd, qcd, qac, pcr).
	 *
	 * What: PAR_SCENE_CHANGE_DIV (scd) is the musical division at which direct
	 * scene changes take effect; PAR_SCENE_QUEUE_DIV (qcd) is the same for queued
	 * (SHIFT+SEQ) changes; PAR_SCENE_QUEUE_ACTION (qac) selects the action on a
	 * queued fire (normal / reset mutes / reset morphs / both / reload);
	 * PAR_SCENE_PC_RESET (pcr) forces tracks to restart on scene change. Why:
	 * ...
	 */
	PAR_SCENE_CHANGE_DIV,
	PAR_SCENE_QUEUE_DIV,
	PAR_SCENE_QUEUE_ACTION,
	PAR_SCENE_PC_RESET,
```

New:
```c
	/*
	 * S080 scene queue / change division globals (scd, qcd, qac).
	 *
	 * What: PAR_SCENE_CHANGE_DIV (scd) is the musical division at which direct
	 * scene changes take effect; PAR_SCENE_QUEUE_DIV (qcd) is the same for queued
	 * (SHIFT+SEQ) changes; PAR_SCENE_QUEUE_ACTION (qac) selects the action on a
	 * queued fire (normal / reset mutes / reset morphs / both / reload). Why:
	 * adds musical timing and options to scene changes. Inputs: global settings
	 * page edits and settings load/default. Outputs: sequencer scene-change
	 * deferral and the foreground scene-change executor. Affiliates: filesystem
	 * settings writer/parser (S080 F5/F6), MENU_MIDI_PAGE sub-page 2 (S080 F3).
	 */
	PAR_SCENE_CHANGE_DIV,
	PAR_SCENE_QUEUE_DIV,
	PAR_SCENE_QUEUE_ACTION,
```

```
/*
 * S080 P2 1.1: remove duplicate PAR_SCENE_PC_RESET.
 *
 * What:       PAR_BAR_RESET_MODE (pcr) is the single PC reset parameter.
 *             The S080 F3 implementation incorrectly added a duplicate
 *             PAR_SCENE_PC_RESET.  This removal deletes the duplicate
 *             and leaves the original intact.
 * Why:        there is only one PC reset parameter.  It pre-dates S080
 *             and is already persisted as settings key "bar_reset_mode".
 * Affiliates: menu.h (1.2), MenuText.h (1.3), menu.c (1.4, 1.5),
 *             filesystem.c (1.6), menuPages.h (1.7).
 */
```

---

### 1.2 menu.h — Remove TEXT_SCENE_PC_RESET, SHORT_SCENE_PC_RESET, LONG_SCENE_PC_RESET

**File:** `Core/Menu/menu.h`
**Line:** 162
**Action:** REMOVE

Remove `TEXT_SCENE_PC_RESET,` from the NamesEnum (line 162).  The preceding
comment (line 160) changes from "scene change/queue division, queue action,
PCR globals" to "scene change/queue division, queue action globals".

Old (lines 160–162):
```c
    /* S080 F2: scene change/queue division, queue action, PCR globals. */
    TEXT_SCENE_CHANGE_DIV, TEXT_SCENE_QUEUE_DIV, TEXT_SCENE_QUEUE_ACTION,
    TEXT_SCENE_PC_RESET,
```

New:
```c
    /* S080 F2: scene change/queue division, queue action globals. */
    TEXT_SCENE_CHANGE_DIV, TEXT_SCENE_QUEUE_DIV, TEXT_SCENE_QUEUE_ACTION,
```

**Line:** 225
**Action:** REMOVE

Remove `LONG_SCENE_PC_RESET,` from the longNamesEnum.

Old (lines 224–225):
```c
    LONG_SCENE_CHANGE_DIV, LONG_SCENE_QUEUE_DIV, LONG_SCENE_QUEUE_ACTION,
    LONG_SCENE_PC_RESET,
```

New:
```c
    LONG_SCENE_CHANGE_DIV, LONG_SCENE_QUEUE_DIV, LONG_SCENE_QUEUE_ACTION,
```

**Line:** 266
**Action:** REMOVE

Remove `SHORT_SCENE_PC_RESET,` from the shortNamesEnum.

Old (lines 265–266):
```c
    SHORT_SCENE_CHANGE_DIV, SHORT_SCENE_QUEUE_DIV, SHORT_SCENE_QUEUE_ACTION,
    SHORT_SCENE_PC_RESET,
```

New:
```c
    SHORT_SCENE_CHANGE_DIV, SHORT_SCENE_QUEUE_DIV, SHORT_SCENE_QUEUE_ACTION,
```

---

### 1.3 MenuText.h — Remove duplicate "pcr" shortName and "PCReset" longName

**File:** `Core/Menu/MenuText.h`
**Line:** 171
**Action:** MODIFY

Remove `{"pcr"},` from the S080 scene globals cluster in shortNames.

Old (line 171):
```c
    {"scd"},{"qcd"},{"qac"},{"pcr"},
```

New:
```c
    {"scd"},{"qcd"},{"qac"},
```

**Line:** 228
**Action:** MODIFY

Remove `{"PCReset"},` from the S080 scene globals cluster in longNames.

Old (line 228):
```c
    {"ChngDiv"},{"QueDiv"},{"QueActn"},{"PCReset"},
```

New:
```c
    {"ChngDiv"},{"QueDiv"},{"QueActn"},
```

---

### 1.4 menu.c — Remove dtype, valueNames, and static-param entries for PAR_SCENE_PC_RESET

**File:** `Core/Menu/menu.c`
**Line:** 1213
**Action:** REMOVE

Remove the dtype entry.

Old (line 1213):
```c
    [PAR_SCENE_PC_RESET] = DTYPE_ON_OFF,
```

Remove this line.  Update the comment at line 1199 to remove the reference to
"DTYPE_ON_OFF for pcr".

**Line:** 1340
**Action:** REMOVE

Remove the valueNames entry.

Old (line 1340):
```c
    {SHORT_SCENE_PC_RESET,CAT_SCENE,LONG_SCENE_PC_RESET},
```

Remove this line.

---

### 1.5 menu.c — Change scene-change PCR check to use PAR_BAR_RESET_MODE

**File:** `Core/Menu/menu.c`
**Line:** 8276
**Action:** MODIFY

Old (line 8276):
```c
    if (parameter_values[PAR_SCENE_PC_RESET])
```

New:
```c
    if (parameter_values[PAR_BAR_RESET_MODE])
```

```
/*
 * S080 P2 1.5: use the original PC reset parameter.
 *
 * What:       the scene-change executor checks PAR_BAR_RESET_MODE (pcr)
 *             to decide whether to restart all tracks from pattern start.
 *             Previously referenced the duplicate PAR_SCENE_PC_RESET.
 * Why:        there is only one pcr parameter.
 * Affiliates: seq_resetToPatternStart().
 */
```

---

### 1.6 filesystem.c — Remove PAR_SCENE_PC_RESET settings key, default, and writer

**File:** `Core/Hardware/SD/filesystem.c`
**Line:** 2606
**Action:** REMOVE

Remove the default initialiser.

Old (line 2606):
```c
    parameter_values[PAR_SCENE_PC_RESET] = 0u;
```

**Line:** 2795
**Action:** REMOVE

Remove the parser entry.

Old (lines 2794–2795):
```c
    else if (strcmp(key, "qac") == 0) *param = PAR_SCENE_QUEUE_ACTION;
    else if (strcmp(key, "pcr") == 0) *param = PAR_SCENE_PC_RESET;
```

New:
```c
    else if (strcmp(key, "qac") == 0) *param = PAR_SCENE_QUEUE_ACTION;
```

**Line:** 17489–17491
**Action:** REMOVE

Remove the writer case 24u (the "pcr" line).

Old (lines 17489–17491):
```c
    case 24u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "pcr", parameter_values[PAR_SCENE_PC_RESET]);
```

The former case 25u (terminator) becomes case 24u.  Update the terminator
count from `lines=25` to `lines=24`.

**Line:** 17492–17499
**Action:** MODIFY

Renumber the terminator case and update the count comment.

Old:
```c
    case 25u:
        /* ... where 25 is the count of preceding data lines
         * (cases 0u..24u: 17 original + 4 overlay (S080 E10)
         * + 4 scene globals (S080 F5)). */
```

New:
```c
    case 24u:
        /* ... where 24 is the count of preceding data lines
         * (cases 0u..23u: 17 original + 4 overlay (S080 E10)
         * + 3 scene globals (S080 P2)). */
```

Update the format string to emit `lines=24`.

---

### 1.7 menuPages.h — Rewrite MENU_MIDI_PAGE sub-page 2 layout

**File:** `Core/Menu/menuPages.h`
**Lines:** 49–62
**Action:** MODIFY

Rewrite the comment and the sub-page 2 row.  Scene globals (scd, qcd, qac)
occupy columns 0–2; bus compressor (cmp, cam, ctm, csc) occupies columns 3–6;
column 7 is the TEXT_EMPTY/PAR_NONE terminator.

Old (lines 49–62):
```c
  /*
   * S080 F3: the Scene sub-page keeps the S074 bus compressor in its first
   * half and adds the scene change/queue division globals (scd, qcd, qac) plus
   * PCR in the second half, ...
   */
  {TEXT_BUS_COMP_MODE,TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,
    TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,TEXT_SCENE_PC_RESET,
    PAR_BUS_COMP_MODE,PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,
    PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,PAR_SCENE_PC_RESET},
```

New:
```c
  /*
   * S080 P2 1.7: scene globals (scd, qcd, qac) first, then S074 bus
   * compressor (cmp, cam, ctm, csc) last. Column 7 is the terminator.
   * The duplicate pcr parameter has been removed (P2 defect 5); the
   * original PAR_BAR_RESET_MODE stays on sub-page 1.
   */
  {TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,
    TEXT_BUS_COMP_MODE,TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,TEXT_EMPTY,
    PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,
    PAR_BUS_COMP_MODE,PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,PAR_NONE},
```

---

## Step 2 — SELECT1 LED permanently lit in PERF (Defect 4)

### 2.1 ledHandler.c — Remove unconditional SELECT1 light

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** 1161
**Action:** MODIFY

Old (lines 1158–1162):
```c
void led_initPerformanceLeds(void)
{
    led_clearSelectLeds();
    led_setValue(1, LED_PART_SELECT1);
}
```

New:
```c
void led_initPerformanceLeds(void)
{
    led_clearSelectLeds();
}
```

```
/*
 * S080 P2 2.1: remove unconditional SELECT1 light.
 *
 * What:       led_initPerformanceLeds() no longer forces SELECT1 on.
 *             In MODE PERF, no SELECT LED is lit unless the looper is
 *             engaged, in which case the looper engage path lights the
 *             correct division LED.
 * Why:        the S080 A5 sub-page indication is handled by
 *             menu_setPerfSelectLed().  The unconditional SELECT1 write
 *             pre-dated the sub-page system and is now incorrect.
 * Inputs:     called on PERF entry and after SHIFT release in PERF.
 * Outputs:    SELECT row is dark after clearing.
 * Affiliates: menu_setPerfSelectLed() (A5), looper engage LED path (C5).
 */
```

---

## Step 3 — SEQ press during active loop routes to per-track assignment (Defect 9)

### 3.1 buttonHandler.c — Add looper guard to SEQ press routing

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 1310
**Action:** MODIFY

Old (lines 1310–1313):
```c
            if (perf_heldVoiceMask != 0u)
                buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
            else
                buttonHandler_perfScenePressed(seqButtonPressed);
```

New:
```c
            if (perf_heldVoiceMask != 0u && !seq_loopActive)
                buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
            else
                buttonHandler_perfScenePressed(seqButtonPressed);
```

```
/*
 * S080 P2 3.1: looper overrides per-track SEQ routing.
 *
 * What:       while the looper is active, a SEQ press is always a full
 *             scene change, even if VOICE buttons are held.  Per-track
 *             assignment is only available when no loop is running.
 * Why:        the looper holds VOICE buttons as part of its own gesture.
 *             A scene change during looper use is the expected action;
 *             per-track assignment would conflict with the loop mask.
 * Inputs:     seq_loopActive, perf_heldVoiceMask, seqButtonPressed.
 * Outputs:    buttonHandler_perfScenePressed() called when looping.
 * Affiliates: seq_loopActive (C1), buttonHandler_perfVoiceHeldSeqPressed().
 */
```

---

## Step 4 — Mute LEDs not restored on queue action (Defect 11)

### 4.1 buttonHandler.c — Add buttonHandler_clearAllMutes()

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after `buttonHandler_showMuteLEDs()` (line 436)
**Action:** ADD

```c
void buttonHandler_clearAllMutes(void)
{
    buttonHandler_mutedVoices = 0u;
    buttonHandler_showMuteLEDs();
}
```

```
/*
 * S080 P2 4.1: bulk unmute for queued scene-change action.
 *
 * What:       clears the LED-side mute mirror and refreshes the VOICE
 *             LED row.  Called by the queue action handler after
 *             seq_setMute(7, 0) to synchronise the LED state with the
 *             sequencer's internal mute state.
 * Why:        seq_setMute(7, 0) clears seq_mutedTracks but does not
 *             touch buttonHandler_mutedVoices.  Normal mute toggles
 *             always call both; the queue action bypassed the LED side.
 * Inputs:     none.
 * Outputs:    buttonHandler_mutedVoices = 0, VOICE LED row refreshed.
 * Affiliates: menu_applyQueuedSceneAction() (4.2), buttonHandler_showMuteLEDs().
 */
```

### 4.2 buttonHandler.h — Declare buttonHandler_clearAllMutes()

**File:** `Core/Hardware/frontPanel/buttonHandler.h`
**Line:** near existing extern declarations for buttonHandler_showMuteLEDs()
**Action:** ADD

```c
void buttonHandler_clearAllMutes(void);
```

### 4.3 menu.c — Call buttonHandler_clearAllMutes() after seq_setMute(7, 0)

**File:** `Core/Menu/menu.c`
**Lines:** 8298–8304
**Action:** MODIFY

Old:
```c
static void menu_applyQueuedSceneAction(void)
{
    uint8_t qac = parameter_values[PAR_SCENE_QUEUE_ACTION];

    if (qac == 1u || qac == 3u || qac == 4u)     /* mut / m+m / rld */
        seq_setMute(7u, 0u);
}
```

New:
```c
static void menu_applyQueuedSceneAction(void)
{
    uint8_t qac = parameter_values[PAR_SCENE_QUEUE_ACTION];

    if (qac == 1u || qac == 3u || qac == 4u) {   /* mut / m+m / rld */
        seq_setMute(7u, 0u);
        buttonHandler_clearAllMutes();
    }
}
```

```
/*
 * S080 P2 4.3: synchronise mute LEDs with sequencer unmute.
 *
 * What:       after the sequencer unmutes all tracks, the LED-side mute
 *             mirror is cleared and the VOICE LED row is refreshed.
 * Why:        without this call, the VOICE LEDs show the old mute
 *             pattern even though all tracks are now playing.
 * Affiliates: buttonHandler_clearAllMutes() (4.1).
 */
```

---

## Step 5 — SHIFT+REC overlay closes on SHIFT release (Defect 3)

### 5.1 buttonHandler.c — Guard SHIFT release exit

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 2574
**Action:** MODIFY

Old (lines 2573–2578):
```c
    case BUT_SHIFT:
        if (bh_shiftRecMenuActive) {
            /* S080 E7: SHIFT release exits the SHIFT+REC settings menu. */
            buttonHandler_shiftRecMenuExit();
            break;
        }
```

New:
```c
    case BUT_SHIFT:
```

Remove the `bh_shiftRecMenuActive` guard and its body from the SHIFT release
handler entirely.  The overlay menu no longer exits on SHIFT release; it
persists until a subsequent SHIFT press or REC press.

```
/*
 * S080 P2 5.1: SHIFT release no longer exits the overlay menu.
 *
 * What:       the SHIFT release handler in processRelease() no longer
 *             calls buttonHandler_shiftRecMenuExit().  The overlay menu
 *             latches open after SHIFT+REC and is closed by a subsequent
 *             SHIFT press (5.2) or REC press (existing code at line 2193).
 * Why:        the design intent is latch-on-open / close-on-repress.
 *             Closing on SHIFT release prevented the user from accessing
 *             the menu — they could never release SHIFT without losing it.
 * Affiliates: SHIFT press handler (5.2), REC press handler (line 2193).
 */
```

### 5.2 buttonHandler.c — SHIFT press closes overlay when active

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 2336 (case BUT_SHIFT in processPress)
**Action:** MODIFY

Add an early exit at the top of the SHIFT press handler: if the overlay menu
is already active, close it and consume the press.

Old (lines 2336–2338):
```c
    case BUT_SHIFT:
        /* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */
        led_setValue(1, LED_SHIFT);
```

New:
```c
    case BUT_SHIFT:
        if (bh_shiftRecMenuActive) {
            buttonHandler_shiftRecMenuExit();
            return;
        }
        /* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */
        led_setValue(1, LED_SHIFT);
```

```
/*
 * S080 P2 5.2: SHIFT press closes the overlay when it is active.
 *
 * What:       if bh_shiftRecMenuActive is set when SHIFT is pressed,
 *             call buttonHandler_shiftRecMenuExit() and return.  The
 *             press is consumed as a close action — it does not light
 *             the SHIFT LED or enter any SHIFT+X mode.
 * Why:        the overlay menu latches open on SHIFT+REC.  The user
 *             closes it by pressing SHIFT again (here) or REC (line 2193).
 * Inputs:     bh_shiftRecMenuActive.
 * Outputs:    overlay menu closed, SHIFT press consumed.
 * Affiliates: SHIFT release guard removal (5.1), REC press (line 2193).
 */
```

---

## Step 6 — Queued scene change never fires at instant division (Defect 1)

### 6.1 sequencer.c — Add instant fast path in seq_queueSceneChange()

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 469–477
**Action:** MODIFY

Old:
```c
void seq_queueSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingQueueScene = scene_index;
    seq_pendingQueueDiv = division;
    seq_pendingSceneQueue = 1u;

    /* Queueing the playing scene replaces any pending queued scene, and the
     * queued change fires regardless of the current menu (plan §5.3). */
}
```

New:
```c
void seq_queueSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingQueueScene = scene_index;
    seq_pendingQueueDiv = division;
    seq_pendingSceneQueue = 1u;

    if (seq_sceneChangeDivUnits(division) == 0u)
        seq_sceneFireFlag = 1u;
}
```

```
/*
 * S080 P2 6.1: instant fast path for queued scene changes.
 *
 * What:       when the queue division is instant (div 0, units == 0),
 *             set seq_sceneFireFlag immediately so the foreground
 *             executor fires on the next service call.  Non-instant
 *             divisions continue to defer to the ISR boundary check.
 * Why:        seq_sceneChangeDivUnits(0) returns 0.  The ISR guard
 *             (units != 0u) blocks the fire flag from ever being set,
 *             so instant-division queued changes never fired.  This
 *             mirrors the instant fast path in seq_requestSceneChange().
 * Inputs:     division index from PAR_SCENE_QUEUE_DIV.
 * Outputs:    seq_sceneFireFlag = 1 when instant.
 * Affiliates: seq_sceneChangeDivUnits() (line 444),
 *             menu_serviceDeferredSceneChange() (menu.c:8317).
 */
```

---

## Step 7 — Queue LED blinks at standard rate, not double-speed (Defect 10)

### 7.1 ledHandler.c — Add fast-blink mechanism

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** 75 (after LED_BLINK_TIME_MS)
**Action:** ADD

```c
#define LED_FAST_BLINK_TIME_MS 125
```

**Line:** 172 (after led_blinkingLeds)
**Action:** ADD

Add a parallel fast-blink slot table.

```c
static uint16_t led_nextFastBlinkTime;
static volatile uint8_t led_fastBlinkLedNumber[NUM_OF_BLINKABLE_LEDS];
static volatile uint8_t led_fastBlinkingLeds;
```

```
/*
 * S080 P2 7.1: fast-blink slots for double-speed visual feedback.
 *
 * What:       a parallel blink system that toggles at LED_FAST_BLINK_TIME_MS
 *             (125 ms, double the standard 250 ms rate).  The slot structure
 *             mirrors the standard blink table: led_fastBlinkLedNumber stores
 *             logical LED IDs, led_fastBlinkingLeds is the active-slot bitset,
 *             led_nextFastBlinkTime is the next toggle deadline.
 * Why:        the queued scene LED must blink at double speed to visually
 *             distinguish a queued change from the active-scene standard blink.
 *             Adding a separate fast timer avoids changing existing blink
 *             behaviour.
 * Inputs:     led_setFastBlinkLed() allocates/frees slots.
 * Outputs:    led_tickHandler() toggles fast-blink slots at the fast rate.
 * Affiliates: led_setBlinkLed() (standard blink), led_tickHandler(),
 *             led_clearAllBlinkLeds().
 */
```

### 7.2 ledHandler.c — led_setFastBlinkLed() implementation

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after led_setBlinkLed() (line 816)
**Action:** ADD

Implement `led_setFastBlinkLed()` following the same pattern as
`led_setBlinkLed()` but using the fast-blink slot table.  The function
allocates/frees fast-blink slots identically to the standard blink
function, using `led_fastBlinkLedNumber[]` and `led_fastBlinkingLeds`.
Uses `LED_LAYER_BLINK` for the same layer priority.

### 7.3 ledHandler.c — Update led_tickHandler() for fast-blink slots

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 885–898 (the standard blink toggle block)
**Action:** ADD (after the standard blink block, before the closing `}`)

Add a second toggle block for fast-blink slots, identical in structure
to the standard blink block but using `led_nextFastBlinkTime`,
`LED_FAST_BLINK_TIME_MS`, `led_fastBlinkingLeds`, and
`led_fastBlinkLedNumber[]`.

```c
    if ((time_sysTick > led_nextFastBlinkTime) ||
        ((led_nextFastBlinkTime - time_sysTick) > LED_FAST_BLINK_TIME_MS*2)) {
        led_nextFastBlinkTime = (uint16_t)(time_sysTick + LED_FAST_BLINK_TIME_MS);
        for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
            if (led_fastBlinkingLeds & (1<<i)) {
                uint8_t ledNr = led_fastBlinkLedNumber[i];
                uint8_t physLed = led_toPhysicalNumber(ledNr);
                uint8_t index = (physLed == LED_BAR1) ? 40u : physLed;
                if (index >= LED_LAYER_COUNT ||
                    !(led_activeLayers[index] &
                      (LED_LAYER_PULSE | LED_LAYER_FLASH)))
                    led_toggleTemp(ledNr);
            }
        }
    }
```

### 7.4 ledHandler.c — Update led_clearAllBlinkLeds() for fast-blink slots

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 828–840 (led_clearAllBlinkLeds)
**Action:** MODIFY

After the existing loop that clears standard blink slots, add a second
loop that clears all fast-blink slots:

```c
    for (i=0;i<NUM_OF_BLINKABLE_LEDS;i++) {
        if (led_fastBlinkingLeds & (1<<i)) {
            uint8_t ledNr = led_fastBlinkLedNumber[i];
            uint8_t physLed = led_toPhysicalNumber(ledNr);
            uint8_t index = (physLed == LED_BAR1) ? 40u : physLed;
            if (index < LED_LAYER_COUNT)
                led_activeLayers[index] &= (uint8_t)~LED_LAYER_BLINK;
            led_renderFromStack(ledNr);
            led_fastBlinkingLeds &= (uint8_t)~(1<<i);
        }
    }
```

### 7.5 ledHandler.h — Declare led_setFastBlinkLed()

**File:** `Core/Hardware/frontPanel/ledHandler.h`
**Line:** 140 (after led_clearAllBlinkLeds declaration)
**Action:** ADD

```c
void led_setFastBlinkLed(uint8_t ledNr, uint8_t onOff);
```

### 7.6 buttonHandler.c — Use fast blink for queued scene LED

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 1259
**Action:** MODIFY

Old:
```c
                led_setBlinkLed((uint8_t)(LED_SEQ1 + seqButtonPressed), 1u);
```

New:
```c
                led_setFastBlinkLed((uint8_t)(LED_SEQ1 + seqButtonPressed), 1u);
```

Update the comment at lines 1252–1253 to remove the "left for a later
LED-layer change" note:

Old (lines 1252–1253):
```c
             * Note: the LED blinks at the standard rate (a double-rate queue
             * blink is left for a later LED-layer change).
```

New:
```c
             * Note: the LED fast-blinks at double speed (P2 defect 10).
```

---

## Step 8 — Looper state-machine rework (Defects 7 + 8)

### 8.1 sequencer.c — Add seq_loopUpdateMask()

**File:** `Core/Sequencer/sequencer.c`
**Line:** after seq_loopUpdateDivision() (line 376)
**Action:** ADD

```c
void seq_loopUpdateMask(uint8_t newMask)
{
    uint8_t added = (uint8_t)(newMask & ~seq_loopTrackMask);
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        if ((added & (uint8_t)(1u << track)) != 0u) {
            seq_loopStartStep[track] = seq_stepIndex[track];
            seq_loopStartAccum[track] = seq_trackAccumulator[track];
        }
    }
    seq_loopTrackMask = newMask;
}
```

```
/*
 * S080 P2 8.1: incremental loop track mask update.
 *
 * What:       updates the loop track mask without disturbing existing
 *             tracks' start points.  Only tracks that are being newly
 *             added (in newMask but not in the current mask) get their
 *             step cursor and DDA accumulator snapshotted.  Tracks that
 *             continue looping keep their original start points.  Tracks
 *             being dropped are simply excluded from the mask.
 * Why:        the user requires that adding or dropping tracks from a
 *             loop must never change the start points of other looping
 *             tracks.  The previous code called seq_loopDisengage() +
 *             seq_loopEngage(), which re-snapshotted all tracks.
 * Inputs:     newMask (7-bit track bitfield).
 * Outputs:    seq_loopTrackMask updated, new tracks snapshotted.
 *             seq_loopTickCount is NOT reset — the loop period continues.
 * Context:    foreground (called from buttonHandler VOICE press/release).
 * Affiliates: seq_loopEngage() (initial engage), seq_loopProcessTick()
 *             (ISR wrap service), buttonHandler VOICE handlers (8.4, 8.6).
 */
```

### 8.2 sequencer.h — Declare seq_loopUpdateMask()

**File:** `Core/Sequencer/sequencer.h`
**Line:** after seq_loopUpdateDivision() declaration (line 103)
**Action:** ADD

```c
void seq_loopUpdateMask(uint8_t newMask);
```

### 8.3 buttonHandler.c — Add buttonHandler_loopCheckDisengage()

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after bh_loopBarModifier definition (line 287)
**Action:** ADD

```c
static void buttonHandler_loopCheckDisengage(void)
{
    if (bh_loopSelectHeldMask != 0u) return;
    if ((perf_heldVoiceMask & bh_loopTrackMask) != 0u) return;
    if (btn_held[BUT_BAR1] || btn_held[BUT_BAR2]) return;

    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
    menu_syncPerfSelectLed();
    led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
    led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
}
```

```
/*
 * S080 P2 8.3: multi-button loop disengage check.
 *
 * What:       the loop disengages only when ALL three button groups are
 *             released: SELECT buttons (bh_loopSelectHeldMask), VOICE
 *             buttons of looped tracks (perf_heldVoiceMask & bh_loopTrackMask),
 *             and BAR buttons (btn_held[BUT_BAR1/2]).  If any group
 *             still has a button held, the loop continues.
 * Why:        the loop must remain active as long as the performer is
 *             holding any button involved in the loop gesture.  Releasing
 *             SELECT while holding a track button or BAR button should
 *             not end the loop.
 * Inputs:     bh_loopSelectHeldMask, perf_heldVoiceMask, bh_loopTrackMask,
 *             btn_held[BUT_BAR1], btn_held[BUT_BAR2].
 * Outputs:    seq_loopDisengage() if all released; SELECT LED restored.
 * Callers:    SELECT release (8.4), VOICE release (8.5), BAR release (8.6).
 * Affiliates: seq_loopDisengage(), menu_syncPerfSelectLed().
 */
```

### 8.4 buttonHandler.c — SELECT release uses buttonHandler_loopCheckDisengage()

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1711–1736
**Action:** MODIFY

Replace the inline `bh_loopSelectHeldMask == 0u` block with the new helper.

Old (lines 1711–1736):
```c
        bh_loopSelectHeldMask = (uint8_t)(bh_loopSelectHeldMask & ~bit);
        if (bh_loopSelectHeldMask == 0u) {
            if (seq_loopActive)
                seq_loopDisengage();
            bh_loopTrackMask = 0u;
            bh_loopBarModifier = 0u;
            menu_syncPerfSelectLed();
            led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
            led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
        } else {
            if (partNr == bh_loopLastSelectButton) {
                ...
            }
            seq_loopUpdateDivision(bh_loopLastSelectButton, bh_loopBarModifier);
            led_setActiveSelectButton(bh_loopLastSelectButton);
        }
```

New:
```c
        bh_loopSelectHeldMask = (uint8_t)(bh_loopSelectHeldMask & ~bit);
        if (bh_loopSelectHeldMask != 0u) {
            if (partNr == bh_loopLastSelectButton) {
                uint8_t i;
                for (i = 8u; i > 0u; i--) {
                    if ((bh_loopSelectHeldMask &
                         (uint8_t)(1u << (i - 1u))) != 0u) {
                        bh_loopLastSelectButton = (uint8_t)(i - 1u);
                        break;
                    }
                }
            }
            seq_loopUpdateDivision(bh_loopLastSelectButton, bh_loopBarModifier);
            led_setActiveSelectButton(bh_loopLastSelectButton);
        } else {
            buttonHandler_loopCheckDisengage();
        }
```

```
/*
 * S080 P2 8.4: SELECT release delegates disengage to the shared helper.
 *
 * What:       when the last SELECT button is released, the disengage
 *             decision is delegated to buttonHandler_loopCheckDisengage()
 *             instead of unconditionally killing the loop.  The loop
 *             survives if VOICE buttons of looped tracks or BAR buttons
 *             are still held.  The SELECT LED stays on the last division.
 * Affiliates: buttonHandler_loopCheckDisengage() (8.3).
 */
```

### 8.5 buttonHandler.c — VOICE release checks for loop disengage

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1205–1226 (buttonHandler_perfVoiceReleased)
**Action:** MODIFY

After clearing the voice bit (line 1211), add a loop disengage check.
Also, if the loop is active and this track was in the loop mask, drop it
via seq_loopUpdateMask().

Old (lines 1209–1225):
```c
    if ((perf_heldVoiceMask & bit) == 0u)
        return;
    perf_heldVoiceMask = (uint8_t)(perf_heldVoiceMask & ~bit);
    if (perf_voiceActionOccurred == 0u) {
        ...mute toggle...
    }
    if (perf_heldVoiceMask == 0u)
        perf_voiceActionOccurred = 0u;
    buttonHandler_perfRefreshHeldSceneLeds();
```

New (add after line 1211):
```c
    if ((perf_heldVoiceMask & bit) == 0u)
        return;
    perf_heldVoiceMask = (uint8_t)(perf_heldVoiceMask & ~bit);

    if (seq_loopActive && (bh_loopTrackMask & bit) != 0u) {
        seq_loopUpdateMask((uint8_t)(bh_loopTrackMask & ~bit));
        bh_loopTrackMask = (uint8_t)(bh_loopTrackMask & ~bit);
        perf_voiceActionOccurred = 1u;
        buttonHandler_loopCheckDisengage();
        if (perf_heldVoiceMask == 0u)
            perf_voiceActionOccurred = 0u;
        buttonHandler_perfRefreshHeldSceneLeds();
        return;
    }

    if (perf_voiceActionOccurred == 0u) {
        ...mute toggle (unchanged)...
    }
    if (perf_heldVoiceMask == 0u)
        perf_voiceActionOccurred = 0u;
    buttonHandler_perfRefreshHeldSceneLeds();
```

```
/*
 * S080 P2 8.5: VOICE release drops track from loop and checks disengage.
 *
 * What:       when a VOICE button of a looped track is released, the track
 *             is dropped from the loop mask via seq_loopUpdateMask() (which
 *             preserves other tracks' start points).  Then the shared
 *             disengage check runs.  If the loop survives (SELECT or BAR
 *             still held), the loop continues with the reduced mask.  The
 *             mute toggle is suppressed (perf_voiceActionOccurred = 1)
 *             because the release was consumed by the loop mask change.
 * Affiliates: seq_loopUpdateMask() (8.1), buttonHandler_loopCheckDisengage() (8.3).
 */
```

### 8.6 buttonHandler.c — VOICE press uses seq_loopUpdateMask() instead of re-engage

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1887–1897
**Action:** MODIFY

Old:
```c
        if (seq_loopActive && bh_loopSelectHeldMask != 0u) {
            uint8_t mask = (uint8_t)(perf_heldVoiceMask |
                                     (uint8_t)(1u << voiceNr));

            seq_loopDisengage();
            bh_loopTrackMask = mask;
            seq_loopEngage(mask, bh_loopLastSelectButton, bh_loopBarModifier);
            perf_heldVoiceMask |= (uint8_t)(1u << voiceNr);
            perf_voiceActionOccurred = 1u;
            dblclick_cancel();
            return;
        }
```

New:
```c
        if (seq_loopActive) {
            uint8_t mask = (uint8_t)(bh_loopTrackMask |
                                     (uint8_t)(1u << voiceNr));
            seq_loopUpdateMask(mask);
            bh_loopTrackMask = mask;
            perf_heldVoiceMask |= (uint8_t)(1u << voiceNr);
            perf_voiceActionOccurred = 1u;
            dblclick_cancel();
            return;
        }
```

```
/*
 * S080 P2 8.6: VOICE press during loop uses incremental mask update.
 *
 * What:       pressing a VOICE button during an active loop adds the
 *             track to the loop mask via seq_loopUpdateMask().  Only
 *             the newly added track gets a fresh start-point snapshot;
 *             existing looping tracks keep their original start points.
 * Why:        the previous code called seq_loopDisengage() then
 *             seq_loopEngage(), which re-snapshotted ALL tracks.
 *             Removing the bh_loopSelectHeldMask guard allows track
 *             addition even when SELECT buttons have been released
 *             (loop sustained by VOICE or BAR holds per defect 8).
 * Affiliates: seq_loopUpdateMask() (8.1).
 */
```

### 8.7 buttonHandler.c — BAR release checks for loop disengage

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2543–2556 (BUT_BAR1 release in PERF)
**Action:** MODIFY

After the existing `seq_loopUpdateDivision()` call and break, add a fallthrough
to the disengage check when SELECT is no longer held.

Old (lines 2547–2551):
```c
            if (seq_loopActive && bh_loopSelectHeldMask != 0u) {
                bh_loopBarModifier = btn_held[BUT_BAR2] ? 1u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                break;
            }
```

New:
```c
            if (seq_loopActive) {
                bh_loopBarModifier = btn_held[BUT_BAR2] ? 1u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                buttonHandler_loopCheckDisengage();
                break;
            }
```

Apply the same change to the BUT_BAR2 release handler (lines 2561–2565):

Old:
```c
            if (seq_loopActive && bh_loopSelectHeldMask != 0u) {
                bh_loopBarModifier = btn_held[BUT_BAR1] ? 2u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                break;
            }
```

New:
```c
            if (seq_loopActive) {
                bh_loopBarModifier = btn_held[BUT_BAR1] ? 2u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                buttonHandler_loopCheckDisengage();
                break;
            }
```

```
/*
 * S080 P2 8.7: BAR release participates in loop disengage.
 *
 * What:       after updating the loop modifier on BAR release, the shared
 *             disengage check runs.  The bh_loopSelectHeldMask guard is
 *             removed so the BAR release path runs whenever the loop is
 *             active, not only when SELECT is still held.
 * Why:        BAR buttons can sustain the loop after SELECT release.  When
 *             the last BAR is released and no SELECT/VOICE is held, the
 *             loop must end.
 * Affiliates: buttonHandler_loopCheckDisengage() (8.3).
 */
```

---

## Step 9 — Per-track loopable parameter (Defect 8a)

### 9.1 ParameterArray.h — Add PAR_TRACK_LOOPABLE

**File:** `Core/Bank/Scene/Preset/ParameterArray.h`
**Line:** after PAR_TRACK_PLAY_MODE (line 133)
**Action:** ADD

```c
	/*
	 * Per-track loopable flag (S080 P2 defect 8a).
	 *
	 * What: on/off flag determining whether a track can participate in
	 * the looper.  Default = 1 (loopable).  When 0, the looper excludes
	 * this track from the loop mask even if the user's VOICE buttons
	 * select it. Input: Menu SEQ_PAGE track settings. Output:
	 * parameter_values[PAR_TRACK_LOOPABLE] consulted by the looper
	 * engage/update mask path in buttonHandler.c. Affiliates:
	 * pat_applyTrackSettingsToMenu(), pat_setTrackLoopable().
	 */
	PAR_TRACK_LOOPABLE,
```

### 9.2 menu.h — Add TEXT_TRACK_LOOPABLE, SHORT_TRACK_LOOPABLE, LONG_TRACK_LOOPABLE

**File:** `Core/Menu/menu.h`

**NamesEnum** — after TEXT_SCENE_QUEUE_ACTION (now the last scene global
TEXT_ entry after pcr removal):

```c
    /* S080 P2: per-track loopable flag. */
    TEXT_TRACK_LOOPABLE,
```

**longNamesEnum** — after LONG_SCENE_QUEUE_ACTION:

```c
    LONG_TRACK_LOOPABLE,
```

**shortNamesEnum** — after SHORT_SCENE_QUEUE_ACTION:

```c
    SHORT_TRACK_LOOPABLE,
```

### 9.3 MenuText.h — Add "lop" shortName and "Loopable" longName

**File:** `Core/Menu/MenuText.h`

**shortNames** — after `{"qac"},` (the last scene global short name after
pcr removal, line 171):

Add `{"lop"},` before the chain labels.

**longNames** — after `{"QueActn"},` (line 228 after pcr removal):

Add `{"Loopable"},` before the chain long names.

### 9.4 menu.c — Add dtype, valueNames for PAR_TRACK_LOOPABLE

**File:** `Core/Menu/menu.c`

**parameterTypes** — after PAR_SCENE_QUEUE_ACTION entry:

```c
    [PAR_TRACK_LOOPABLE] = DTYPE_ON_OFF,
```

**valueNames** — after the scene globals block (formerly at line 1340,
now shifted after pcr removal):

```c
    {SHORT_TRACK_LOOPABLE,CAT_TRACK,LONG_TRACK_LOOPABLE},
```

### 9.5 menuPages.h — Add loopable cell to SEQ_PAGE sub-page 0

**File:** `Core/Menu/menuPages.h`
**Line:** 128
**Action:** MODIFY

SEQ_PAGE sub-page 0 has two empty cells (columns 6–7).  Add the loopable
parameter to column 6.

Old (line 128):
```c
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_SHUFFLE,TEXT_TRACK_PLAY_MODE,TEXT_MIDI_CHANNEL,TEXT_NOTE,TEXT_EMPTY,TEXT_EMPTY, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_SHUFFLE,PAR_TRACK_PLAY_MODE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_NONE,PAR_NONE},
```

New:
```c
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_SHUFFLE,TEXT_TRACK_PLAY_MODE,TEXT_MIDI_CHANNEL,TEXT_NOTE,TEXT_TRACK_LOOPABLE,TEXT_EMPTY, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_SHUFFLE,PAR_TRACK_PLAY_MODE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_TRACK_LOOPABLE,PAR_NONE},
```

```
/*
 * S080 P2 9.5: per-track loopable parameter on SEQ_PAGE sub-page 0.
 *
 * What:       column 6 shows "lop" (Loopable, on/off).  Default is on
 *             (yes).  When off, the looper silently excludes this track.
 * Why:        gives the performer a persistent per-track opt-out from
 *             looper operations without changing the VOICE hold gesture.
 * Affiliates: PAR_TRACK_LOOPABLE (9.1), looper mask filtering (9.7).
 */
```

### 9.6 PatternData / storageTypes — Track loopable storage

**File:** Pattern storage layer (PatternData.c / storageTypes.c)
**Action:** ADD

Add per-track loopable byte storage following the same pattern as
PAR_TRACK_PLAY_MODE:

1. `pat_setTrackLoopable(track, value)` — stores the value in the track's
   Pattern data and updates parameter_values[PAR_TRACK_LOOPABLE] when the
   track is active.
2. `pat_getTrackLoopable(track)` — returns the stored value.
3. `pat_applyTrackSettingsToMenu()` — add a line to load PAR_TRACK_LOOPABLE
   from the active track's stored value.
4. `storage_patternParseLine()` — add `"track_loopable"` CSV key
   (7 values, one per track, 0 or 1).
5. `filesystem_nextPatternLine()` — add writer case for `"track_loopable"`.
6. Default = 1 (all tracks loopable).

```
/*
 * S080 P2 9.6: per-track loopable storage.
 *
 * What:       one byte per track in the Pattern data, persisted in the
 *             pattern storage as a CSV row.  The menu swap-in/swap-out
 *             mirrors the PAR_TRACK_PLAY_MODE mechanism.
 * Why:        the loopable flag must survive save/load cycles and scene
 *             changes.  A track's loopable state is part of its Pattern
 *             settings, consistent with length/scale/shuffle/play mode.
 * Affiliates: PAR_TRACK_LOOPABLE (9.1), menuPages.h (9.5).
 */
```

### 9.7 buttonHandler.c — Filter non-loopable tracks from loop mask

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 1646 (SELECT press, track mask computation)
**Action:** MODIFY

After computing the trackMask, mask out non-loopable tracks.

Old (lines 1646–1647):
```c
        uint8_t trackMask = (perf_heldVoiceMask != 0u) ? perf_heldVoiceMask
                                                       : 0x7Fu;
```

New:
```c
        uint8_t trackMask = (perf_heldVoiceMask != 0u) ? perf_heldVoiceMask
                                                       : 0x7Fu;
        trackMask = buttonHandler_filterLoopableTracks(trackMask);
```

Add a static helper above the SELECT press handler:

```c
static uint8_t buttonHandler_filterLoopableTracks(uint8_t mask)
{
    uint8_t track;
    for (track = 0u; track < NUM_TRACKS; track++) {
        if (!pat_getTrackLoopable(track))
            mask &= (uint8_t)~(1u << track);
    }
    return mask;
}
```

Also apply the filter in the VOICE press handler (step 8.6, line 1888)
when building the new mask for seq_loopUpdateMask():

```c
        if (seq_loopActive) {
            if (!pat_getTrackLoopable(voiceNr))
                goto voice_press_no_loop;
            ...
```

If the track is not loopable, skip the loop mask update and fall through
to normal VOICE press handling (label `voice_press_no_loop:` or just
restructure as an `else` branch).

```
/*
 * S080 P2 9.7: filter non-loopable tracks from loop mask.
 *
 * What:       when building the loop track mask (SELECT press or VOICE
 *             press during loop), tracks with PAR_TRACK_LOOPABLE == 0
 *             are silently excluded.  No LED feedback or error.
 * Why:        the per-track loopable flag (defect 8a) gives the performer
 *             a persistent opt-out.  Utility tracks (hi-hat, click) are
 *             never accidentally captured by the looper.
 * Inputs:     pat_getTrackLoopable(track).
 * Outputs:    mask with non-loopable bits cleared.
 * Affiliates: PAR_TRACK_LOOPABLE (9.1), pat_getTrackLoopable() (9.6).
 */
```

---

## Step 10 — Phase G: Scene Chaining (Defect 2)

Phase G implements the full scene chaining feature as specified in
S080_P1_IMPLEMENTATION.md §Phase G (lines 1931–2494).  All twelve
sub-steps (G1–G12) are reproduced here by reference; the P1 schedule
contains the complete code-level detail for each step.

### 10.1 G1 — SceneData.h: chain fields in scene_settings_t

**File:** `Core/Bank/Scene/SceneData.h`
**Line:** 261 (before `} scene_settings_t;`)
**Action:** ADD

Add four bytes: `chain_condition1`, `chain_next1`, `chain_condition2`,
`chain_next2`.

See P1 schedule G1 (lines 1933–1968) for full comment block and code.

---

### 10.2 G2 — SceneData.c: chain getters/setters

**File:** `Core/Bank/Scene/SceneData.c`
**Line:** after scene_getTrackMorphShuffle()
**Action:** ADD

Eight accessor functions (get/set × 4 fields) using
`scene_storeParameterByte()`.  Clamping: conditions 0–78, next 0–43.

See P1 schedule G2 (lines 1972–2006) for full comment block and code.

---

### 10.3 G3 — SceneData.c: scene_commitSettings() chain data

**File:** `Core/Bank/Scene/SceneData.c`
**Lines:** in scene_commitSettings(), after bus comp loop
**Action:** ADD

Four setter calls to commit chain parameters on scene copy/clear/load.

See P1 schedule G3 (lines 2010–2042) for full comment block.

---

### 10.4 G4 — SceneData.c: chain defaults (implicit zero via memset)

No code change needed.  `scene_settingsDefaults()` uses `memset(out, 0, ...)`.
Chain defaults of 0 (nul condition, slf next-scene) mean "no chaining."

See P1 schedule G4 (lines 2046–2070).

---

### 10.5 G5 — filesystem.c: sceneset writer chain keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** in filesystem_nextScenesetLine(), after case 17u
**Action:** ADD

Two new cases: `chain_condition` (CSV pair) and `chain_next` (CSV pair).

See P1 schedule G5 (lines 2074–2110) for full code and comment block.

---

### 10.6 G6 — storageTypes.c: sceneset parser chain keys

**File:** `Core/Hardware/SD/storageTypes.c`
**Lines:** in storage_scenesetParseLine(), before final return
**Action:** ADD

Two new `else if` branches parsing `chain_condition` and `chain_next` CSV pairs.

See P1 schedule G6 (lines 2114–2156) for full code and comment block.

---

### 10.7 G7 — sequencer.c: chain condition evaluator

**File:** `Core/Sequencer/sequencer.c`
**Line:** after scene change state (new function)
**Action:** ADD

Two functions:
- `seq_evaluateChainCondition(cond, scene_index)` — 79-condition evaluator
- `seq_evaluateSceneChains(at_bar, at_rep)` — called from seq_handleMasterBoundary()

New state variables: `seq_sceneBarCount`, `seq_sceneRepeatCount`,
`seq_previousScene`.

See P1 schedule G7 (lines 2160–2228) for full condition table and comment block.

---

### 10.8 G8 — buttonHandler.c: SHIFT+PLAY+SEQ chain menu

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

**New state variables:**
```c
static uint8_t bh_chainMenuArmed  = 0u;
static uint8_t bh_chainMenuScene  = 0u;
static uint8_t bh_chainMenuActive = 0u;
```

**BUT_START_STOP press:** add SHIFT guard — if SHIFT is held, arm the chain
menu instead of toggling transport.

**SEQ press in PERF (SHIFT held):** if `bh_chainMenuArmed`, open the chain
menu for the pressed SEQ button's scene.

**Exit:** SHIFT release while chain menu is active closes it.

See P1 schedule G8 (lines 2232–2287) for full state machine and comment block.

---

### 10.9 G9 — MenuText.h: chain condition and next-scene name tables

**File:** `Core/Menu/MenuText.h`
**Line:** after stepRollRateNames
**Action:** ADD

Two name tables: `chainConditionNames` (79 entries) and `chainNextNames`
(44 entries).

See P1 schedule G9 (lines 2291–2347) for full table contents and comment block.

---

### 10.10 G10 — menu.h / menu.c: TEXT_ and valueNames for chain params

Already partially present in the current code (TEXT_CHAIN_COND1/NEXT1/COND2/NEXT2,
SHORT_CHAIN_*, LONG_CHAIN_*, CAT_CHAIN, and the valueNames entries were added
during the initial S080 session).

**Verify** these are all in place.  If any are missing, add per the P1 schedule
G10 (lines 2351–2407).

---

### 10.11 G11 — sequencer.c: track stop for `stp` next-scene

**File:** `Core/Sequencer/sequencer.c`
**Lines:** in seq_advanceTrackStep()
**Action:** ADD

Add `SEQ_PLAY_STATE_CHAIN_STOPPED` flag.  When the chain evaluator resolves
`stp`, all tracks freeze in place.  The next non-stp chain condition resumes.

See P1 schedule G11 (lines 2411–2451) for full code and comment block.

---

### 10.12 G12 — sequencer.c: clear chain state on transport stop / manual switch

**File:** `Core/Sequencer/sequencer.c`
**Lines:** in seq_setRunning() stop branch
**Action:** MODIFY

Clear all chain-related state: pending scene change flags, repeat/bar counters,
previous-scene tracker.  Also clear chain-stopped flag.

Manual scene switch (menu.c menu_perfSceneChangeExecute, already has
`seq_clearPendingSceneChange()` at line 8274) cancels chain transitions.

See P1 schedule G12 (lines 2455–2494) for full code and comment block.

---

## Build Verification

After all 10 steps, verify:

1.  **Compile** — clean build with no warnings. PAR_SCENE_PC_RESET references
    must be fully eliminated; any remaining reference is a missed removal.

2.  **settings.cfg backward compatibility** — old configs with a `pcr=N` line
    will hit the `return 0u` fallthrough in the parser (unknown key, silently
    skipped).  The original `bar_reset_mode` key continues to load correctly.

3.  **Pattern storage backward compatibility** — old Patterns without
    `track_loopable` will default to 1 (all tracks loopable).

4.  **Sceneset backward compatibility** — old scenesets without `chain_condition`
    / `chain_next` will default to 0 (no chaining).

5.  **NUM_PARAMS** — PAR_SCENE_PC_RESET removal shifts one enum value.
    `PAR_TRACK_LOOPABLE` is added after PAR_TRACK_PLAY_MODE, well within the
    384 capacity.  Verify `NUM_PARAMS = 384` static assert passes.

6.  **Flash cost** — chain name tables add ~500 bytes.  Fast-blink slots add
    ~18 bytes SRAM.  All well within budget.
