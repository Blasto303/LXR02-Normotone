# S080 P2 — PERF Expansion Defect Plan

Testing feedback from first round of S080 implementation.
Twelve items total — nine functional bugs, one duplicate parameter removal,
one layout issue, one new parameter addition.  Plus Phase G full implementation.
No code implementation in this document; remediation steps only.

---

## Defect 1 — SHIFT+SEQ queued scene change never fires at instant division

### Observed behaviour

In MODE PERF, pressing SHIFT+SEQ arms a queued scene change (queue icon
appears), but the queued change never fires when `PAR_SCENE_QUEUE_DIV`
is at its power-on default of 0 (instant).  At non-zero division values
the queue fires correctly at the musical boundary.  A normal (unqueued)
scene change works at all division values including instant.

### Root cause

`seq_queueSceneChange()` (sequencer.c:469–477) arms the queue by setting
`seq_queuedScene` and `seq_queueArmed`, then delegates timing to the ISR
boundary check at sequencer.c:2594–2602:

```c
if (units != 0u && seq_masterStepClock != 0u &&
    (seq_masterStepClock % units) == 0u)
    seq_sceneFireFlag = 1u;
```

The division index for queued changes comes from `PAR_SCENE_QUEUE_DIV`.
Its power-on default is 0 (instant).  `seq_sceneChangeDivUnits(0)` hits
the `default:` branch and returns 0.  With `units == 0u`, the ISR guard
`units != 0u` blocks the fire flag from ever being set.

The non-queued direct path (`seq_requestSceneChange()`) has a separate
instant fast path that bypasses the ISR entirely, so direct changes work
at division 0.

### Fix

Add an instant fast path inside `seq_queueSceneChange()`: when
`seq_sceneChangeDivUnits(parameter_values[PAR_SCENE_QUEUE_DIV]) == 0u`,
set `seq_sceneFireFlag = 1u` immediately instead of deferring to the ISR.
This mirrors the existing fast path in `seq_requestSceneChange()`.

Alternatively, fix `seq_sceneChangeDivUnits()` so `case 0:` returns 1
(fire on every master tick), but that changes the semantics for all
callers — the instant fast path in the queue function is safer.

### Follow-up decision

**None required.**  The instant fast path is the clear correct fix.

---

## Defect 2 — SHIFT+PLAY+SEQ chain menu does not open

### Observed behaviour

In MODE PERF, pressing SHIFT+PLAY then a SEQ button does not open the
chain-options menu for that scene.

### Root cause

Phase G (scene chaining) was not implemented during the S080 session.
The chain state variables (`bh_chainMenuArmed`, `bh_chainMenuScene`,
`bh_chainMenuActive`) were removed from the build.  There is no chain
menu code path to enter.

The plan document (S080_P1_PERF_MODE_UPDATES.md §4) fully specifies the
feature.  The implementation schedule (S080_P1_IMPLEMENTATION.md,
Phase G) contains 12 steps (G1–G12).

### Fix

Implement Phase G in full as part of this defect remediation.  The
twelve steps from the schedule are:

*  G1: chain fields in SceneData.h (chain_condition, chain_next × 2)
*  G2: chain defaults
*  G3: chain getter/setter API
*  G4: chain menu state variables in buttonHandler.c
*  G5: SHIFT+PLAY arm, SHIFT+PLAY+SEQ chain menu enter/exit
*  G6: chain page table and display
*  G7: chain evaluation in the ISR / foreground executor
*  G8: chain condition enum and renderer
*  G9: chain clear on Scene save/copy (no orphan references)
*  G10: TEXT_/SHORT_/LONG_ entries (already in menu.h/MenuText.h)
*  G11: chain storage in storageTypes.c
*  G12: chain cancel on manual scene change

### Follow-up decision

**None required.**  Full implementation from the existing schedule.

---

## Defect 3 — SHIFT+REC overlay menu closes on SHIFT release

### Observed behaviour

Pressing SHIFT+REC opens the BAR overlay config page.  Releasing SHIFT
immediately closes it, instead of keeping the menu open until the user
presses SHIFT or REC again.

### Root cause

The SHIFT release handler at buttonHandler.c:2574 calls
`buttonHandler_shiftRecMenuExit()` unconditionally as part of its
general SHIFT-cleanup sweep.  The design intent (S080 E7/E8) is that the
overlay page latches open:

* **Entry:** SHIFT held + REC pressed → `buttonHandler_shiftRecMenuEnter()`
* **Exit:**  while overlay is active, a new SHIFT press OR a REC press
  calls `buttonHandler_shiftRecMenuExit()`

The current code triggers exit on SHIFT *release*, not on a subsequent
SHIFT *press*.

### Fix

1.  In the SHIFT release handler (buttonHandler.c:2574 region), remove
    the call to `buttonHandler_shiftRecMenuExit()` (or guard it behind
    `!bh_shiftRecMenuActive` so it only runs when the overlay is not
    latched).

2.  In the SHIFT *press* handler, add: if `bh_shiftRecMenuActive` is
    already set, call `buttonHandler_shiftRecMenuExit()` and return
    early (consume the press as a close action).

3.  In the REC press handler (buttonHandler.c:2193), the existing exit
    path (`buttonHandler_shiftRecMenuExit()` when the menu is active)
    is already correct.  Verify it still works after step 1.

### Follow-up decision

**None required.**  The latch-on-open / close-on-repress semantics are
already specified in the plan.

---

## Defect 4 — SELECT1 LED permanently lit in PERF mode

### Observed behaviour

Entering PERF mode lights the first SELECT LED and it stays on.  In
MODE PERF, no SELECT LED should be lit unless the looper is engaged, in
which case the LED of the current loop division is lit.

### Root cause

`led_initPerformanceLeds()` at ledHandler.c:1161 unconditionally
executes:

```c
led_setValue(1, LED_PART_SELECT1);
```

This function is called on every PERF entry and after every SHIFT
release.  The S080 A5 subpage indication was added via
`menu_setPerfSelectLed()`, which correctly uses SELECT LEDs only when
relevant, but `led_initPerformanceLeds()` was never updated to stop
forcing SELECT1 on.

### Fix

Remove (or zero-guard) the `led_setValue(1, LED_PART_SELECT1)` call in
`led_initPerformanceLeds()`.  The looper engage path already lights the
correct SELECT LED when it activates; clearing them by default makes the
idle state dark as intended.

Verify that removing this call does not break any other mode that calls
`led_initPerformanceLeds()` — it is only called from the PERF entry
path and SHIFT cleanup, so there should be no side effects.

### Follow-up decision

**None required.**

---

## Defect 5 — Duplicate pcr parameter in global settings

### Observed behaviour

Navigating the global settings pages (MENU_MIDI_PAGE) shows two "pcr"
entries: the original on sub-page 1 (PAR_BAR_RESET_MODE, settings key
`"bar_reset_mode"`) and a duplicate on sub-page 2 (PAR_SCENE_PC_RESET,
settings key `"pcr"`).

There is only one PC reset parameter.  It is `PAR_BAR_RESET_MODE`,
short name "pcr", category "Sequencr", long name "PCReset", as it was
before S080.

### Root cause

The S080 F3 implementation incorrectly created a second parameter
(`PAR_SCENE_PC_RESET`) for the same function.  This added a duplicate
enum value in ParameterArray.h, a duplicate TEXT_/SHORT_/LONG_ set in
menu.h/MenuText.h, a duplicate shortNames entry, a duplicate settings
key, and a duplicate slot on the menu page.

### Fix

Remove `PAR_SCENE_PC_RESET` and all of its supporting entries:

1.  **ParameterArray.h:** remove `PAR_SCENE_PC_RESET` from the enum.
2.  **menu.h:** remove `TEXT_SCENE_PC_RESET`, `SHORT_SCENE_PC_RESET`,
    `LONG_SCENE_PC_RESET` from their respective enums.
3.  **MenuText.h:** remove the "pcr" entry at shortNames line 171
    (the S080-added one; the original at index 63 stays).  Remove the
    corresponding longNames and catNames entries.
4.  **menu.c:** remove the valueNames entry for TEXT_SCENE_PC_RESET.
    Change the scene-change PCR check (menu.c:8276) from
    `parameter_values[PAR_SCENE_PC_RESET]` to
    `parameter_values[PAR_BAR_RESET_MODE]`.
5.  **filesystem.c:** remove the `"pcr"` settings key parser
    (line 2795), the writer (line 17491), and the default initialiser
    (line 2606).  Decrement the `lines=N` terminator.
6.  **menuPages.h:** remove the 4th cell (TEXT_SCENE_PC_RESET /
    PAR_SCENE_PC_RESET) from the scene globals on sub-page 2.  The
    scene globals become three items: scd, qcd, qac.

### Follow-up decision

**None required.**

---

## Defect 6 — Compressor page not last in global settings

### Observed behaviour

On the scene globals sub-page (MENU_MIDI_PAGE sub-page 2), the bus
compressor occupies columns 0–3 and the scene change/queue division
globals occupy columns 4–7.  The compressor should always be the last
group of settings in the global menu.

### Root cause

The S080 F3 implementation appended scene globals to the second half of
the existing compressor sub-page, putting compressor *first* and scene
globals *second*.

### Fix

With defect 5's removal of the duplicate pcr, the scene globals reduce
to three items (scd, qcd, qac).  Rewrite sub-page 2 in menuPages.h
as seven contiguous cells plus an empty terminator:

**Current layout (columns 0–7):**

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| cmp | cam | ctm | csc | scd | qcd | qac | pcr |

**New layout:**

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| scd | qcd | qac | cmp | cam | ctm | csc | EMPTY |

Scene globals occupy columns 0–2; the compressor block occupies
columns 3–6; column 7 is the TEXT_EMPTY/PAR_NONE terminator.  The
encoder traversal walks columns 0→6, placing the compressor last.

### Follow-up decision

**None required.**

---

## Defect 7 — Track mask changes re-snapshot all loop start points

### Observed behaviour

During an active loop, pressing a VOICE button to add a track to the
loop resets all tracks' loop start points to their current positions,
losing the original loop start alignment.  Similarly, releasing a VOICE
button (dropping a track) could re-snapshot remaining tracks.  The user
expects:

* **Adding** a track: only the newly added track gets a fresh start-point
  snapshot; all existing looping tracks keep their original start points.
* **Dropping** a track: the dropped track resumes normal playback; all
  remaining looping tracks keep their original start points unchanged.
* **Changing interval** (SELECT division or BAR modifier): start points
  must never change.  *(Already correct:* `seq_loopUpdateDivision()`
  only writes division/modifier without touching start-point arrays.*)*

### Root cause

The VOICE press handler at buttonHandler.c:1887–1893 calls:

```c
seq_loopDisengage();
seq_loopEngage(mask, bh_loopLastSelectButton, bh_loopBarModifier);
```

`seq_loopEngage()` (sequencer.c:324–356) unconditionally snapshots every
track in the new mask:

```c
for (track = 0; track < NUM_TRACKS; track++) {
    if (trackMask & (1u << track)) {
        seq_loopStartStep[track] = seq_stepIndex[track];        // re-snapshot
        seq_loopStartAccum[track] = seq_trackAccumulator[track];
    } else {
        seq_loopStartStep[track] = 0;
        seq_loopStartAccum[track] = 0u;
    }
}
```

There is no distinction between tracks already in the loop (which should
keep their existing start points) and tracks being freshly added.

### Fix

Add a new function `seq_loopUpdateMask(uint8_t newMask)` in sequencer.c
that preserves existing start points:

1.  Compute `added = newMask & ~seq_loopTrackMask` (tracks being added).
2.  For each track in `added`: snapshot current step/accum as the new
    start point.
3.  For tracks in `newMask & seq_loopTrackMask` (continuing): leave
    their start points untouched.
4.  For tracks not in `newMask` (dropped or never looped): optionally
    clear their start points (cosmetic; the ISR only wraps tracks in
    the mask).
5.  Update `seq_loopTrackMask = newMask`.
6.  Do NOT reset `seq_loopTickCount` — the loop period continues
    uninterrupted.

Replace the `seq_loopDisengage()` + `seq_loopEngage()` call in the VOICE
press handler (buttonHandler.c:1891–1893) with
`seq_loopUpdateMask(mask)`.

### Follow-up decision

**None required.**  The incremental-snapshot semantics are fully specified
by the user.

---

## Defect 8 — Loop released too early / multi-button hold semantics

### Observed behaviour

The loop disengages as soon as all SELECT buttons are released, even if
VOICE buttons (of looped tracks) or BAR buttons are still held.  The
user expects:

* The loop stays active as long as **any** of the following is held:
  a SELECT button, a VOICE button of a track that is in the loop, or a
  BAR button.
* The SELECT LED must continue showing the active looping division while
  track or BAR buttons sustain the loop.
* The loop is released only when **none** of these three groups has a
  button held.

Example sequence:
1.  Hold VOICE 1 → hold SELECT 3 (loop engages on track 1, 1/8 div)
2.  Release SELECT 3 → loop continues because VOICE 1 is still held
3.  Release VOICE 1 → loop disengages (no buttons held)

Same applies with BAR buttons sustaining the loop after SELECT release.

### Root cause

The SELECT release handler at buttonHandler.c:1711–1718 checks only
`bh_loopSelectHeldMask`:

```c
bh_loopSelectHeldMask = (uint8_t)(bh_loopSelectHeldMask & ~bit);
if (bh_loopSelectHeldMask == 0u) {
    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
    menu_syncPerfSelectLed();
    ...
}
```

It does not consider `perf_heldVoiceMask` or `btn_held[BUT_BAR1/2]`.
The VOICE release handler (`buttonHandler_perfVoiceReleased()` at
buttonHandler.c:1205) does not check for loop disengage at all.  The BAR
release handlers (buttonHandler.c:2543–2571) update the modifier but do
not participate in disengage logic.

### Fix

1.  **Extract a loop-disengage check** into a helper:

    ```c
    static void buttonHandler_loopCheckDisengage(void)
    {
        if (bh_loopSelectHeldMask != 0u) return;
        if ((perf_heldVoiceMask & bh_loopTrackMask) != 0u) return;
        if (btn_held[BUT_BAR1] || btn_held[BUT_BAR2]) return;
        // All released — disengage
        if (seq_loopActive)
            seq_loopDisengage();
        bh_loopTrackMask = 0u;
        bh_loopBarModifier = 0u;
        menu_syncPerfSelectLed();
        led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
        led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
    }
    ```

2.  **SELECT release** (buttonHandler.c:1712): replace the inline
    `bh_loopSelectHeldMask == 0u` block with a call to
    `buttonHandler_loopCheckDisengage()`.  If the loop survives (still
    active), keep the SELECT LED on the last division.

3.  **VOICE release** (`buttonHandler_perfVoiceReleased()`): after
    clearing the voice bit, if `seq_loopActive`, call
    `buttonHandler_loopCheckDisengage()`.

4.  **BAR release** (buttonHandler.c:2543–2571): after updating the
    modifier, if `bh_loopSelectHeldMask == 0u`, call
    `buttonHandler_loopCheckDisengage()`.

5.  **SELECT LED**: when the loop survives a SELECT release (because
    VOICE or BAR is still held), do NOT call `menu_syncPerfSelectLed()`.
    Instead, keep `led_setActiveSelectButton(bh_loopLastSelectButton)`
    so the division indicator persists.

### Follow-up decision

**None required.**  Releasing a looped track's VOICE button drops it from
the loop mask immediately (via defect 7's `seq_loopUpdateMask()`), so
other tracks' start points are preserved and the performer has real-time
control over which tracks are looping.

---

## Defect 8a — Per-track loopable parameter

### Requirement

Add a per-track parameter `lop` (cat "Track", long "Loopable",
default = yes).  When set to "no", the track is excluded from the
looper regardless of which VOICE buttons are held.

### Rationale

Gives the performer a persistent per-track opt-out from looper
operations, so utility tracks (e.g. a hi-hat running free, or a
click track) are never accidentally captured.

### Implementation

1.  **ParameterArray.h:** add `PAR_TRACK_LOOPABLE` to the per-track
    parameter range (same region as PAR_TRACK_PLAY_MODE etc.).

2.  **menu.h:** add `TEXT_TRACK_LOOPABLE`, `SHORT_TRACK_LOOPABLE`,
    `LONG_TRACK_LOOPABLE` to the respective enums.

3.  **MenuText.h:** add shortNames entry `"lop"`, longNames entry
    `"Loopable"`.

4.  **menu.c:** add valueNames entry
    `{SHORT_TRACK_LOOPABLE, CAT_TRACK, LONG_TRACK_LOOPABLE}`.
    Add on/off renderer case for `PAR_TRACK_LOOPABLE`.  Default
    value = 1 (yes).

5.  **menuPages.h:** add TEXT_TRACK_LOOPABLE / PAR_TRACK_LOOPABLE
    to a cell on the SEQ_PAGE track-settings sub-page.

6.  **storageTypes.c:** add parser/writer for the per-track loopable
    field in Pattern storage (one byte per track).

7.  **sequencer.c / buttonHandler.c:** when building the loop track
    mask (SELECT press at buttonHandler.c:1646 and VOICE press at
    buttonHandler.c:1888), mask out tracks where
    `parameter_values[PAR_TRACK_LOOPABLE + track * VOICE_PARAM_STRIDE]`
    (or equivalent indexed access) is 0.  The looper ignores non-loopable
    tracks silently — no LED feedback or error.

---

## Defect 9 — SEQ press during active loop routes to per-track assignment

### Observed behaviour

When the looper is active (VOICE buttons held + SELECT engaged), pressing
a SEQ button performs a per-track scene assignment instead of a full
scene change.  SHIFT+SEQ should also always perform a full queued scene
change during a loop.

The user's requirement: while the looper is active, SEQ must always be
a full scene change, and SHIFT+SEQ must always be a full queued scene
change, regardless of held track buttons.

### Root cause

The non-SHIFT SEQ press router at buttonHandler.c:1310–1313:

```c
if (perf_heldVoiceMask != 0u)
    buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
else
    buttonHandler_perfScenePressed(seqButtonPressed);
```

When VOICE buttons are held (which they are during a loop),
`perf_heldVoiceMask` is nonzero, so the press routes to per-track
assignment instead of scene change.

The SHIFT+SEQ path (buttonHandler.c:1240–1261) does NOT check
`perf_heldVoiceMask` — it always queues.  So SHIFT+SEQ already works
correctly during a loop.  The defect is solely in the non-SHIFT path.

### Fix

Add a looper guard to the routing condition at buttonHandler.c:1310:

```c
if (perf_heldVoiceMask != 0u && !seq_loopActive)
    buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
else
    buttonHandler_perfScenePressed(seqButtonPressed);
```

When the looper is active, SEQ always routes to the scene-change path.
Per-track assignment is only available when no loop is running.

### Follow-up decision

**None required.**

---

## Defect 10 — Queued scene LED blinks at standard rate, not double-speed

### Observed behaviour

When a scene is queued via SHIFT+SEQ, the queued scene's SEQ LED blinks
at the standard rate (250 ms toggle / ~2 Hz visible).  The plan
specifies a double-speed blink to visually distinguish a queued change
from the active-scene blink.

### Root cause

The blink at buttonHandler.c:1259 uses the standard `led_setBlinkLed()`:

```c
led_setBlinkLed((uint8_t)(LED_SEQ1 + seqButtonPressed), 1u);
```

The LED layer has a single blink timer at `LED_BLINK_TIME_MS = 250`
(ledHandler.c:75).  There is no per-slot rate and no fast-blink
category.  The comment at buttonHandler.c:1252–1253 explicitly defers
the double-rate blink to a "later LED-layer change."

### Fix

Add a fast-blink mechanism to ledHandler.c.  Two approaches:

**Option A — per-slot rate bit:**  Add a parallel bitset
`led_fastBlinkLeds` alongside `led_blinkingLeds`.  In
`led_tickHandler()`, toggle fast-blink slots every
`LED_FAST_BLINK_TIME_MS` (125 ms = ~4 Hz visible, double the standard
rate).  Add `led_setFastBlinkLed()` for callers.

**Option B — separate fast timer:**  Add a second blink-tick counter
and toggle function.  Conceptually simpler but duplicates logic.

Recommendation: **Option A** — a single bitset and a second counter in
the existing tick handler keeps the change minimal.

Then change buttonHandler.c:1259 to call `led_setFastBlinkLed()` for the
queued scene LED.

### Follow-up decision

**None required.**

---

## Defect 11 — Mute LEDs not restored when queue action unmutes tracks

### Observed behaviour

When a queued scene change fires with a queue action that resets mutes
(qac = mut / m+m / rld), the sequencer unmutes all tracks but the
VOICE LED row continues showing the old mute pattern.  The LEDs of
unmuted tracks are not restored to their lit state.

### Root cause

`menu_applyQueuedSceneAction()` (menu.c:8298–8304) calls:

```c
seq_setMute(7u, 0u);
```

This clears `seq_mutedTracks` in sequencer.c (the sequencer's internal
mute state).  However, `buttonHandler_mutedVoices` (the LED-side mirror
at buttonHandler.c:174) is **not** cleared, and
`buttonHandler_showMuteLEDs()` is **not** called.  The VOICE LEDs remain
in their pre-unmute state.

Normal mute toggles (VOICE release in PERF) always call both
`buttonHandler_muteVoice()` and `seq_setMute()` in parallel, keeping
the LED mirror in sync.  The queue action bypasses the LED side.

### Fix

After the `seq_setMute(7u, 0u)` call in `menu_applyQueuedSceneAction()`,
synchronize the LED mirror.  Two options:

**Option A — call an existing function per-track:**

```c
for (uint8_t i = 0u; i < NUM_TRACKS; i++)
    buttonHandler_muteVoice(i, 0u);
```

This clears each bit in `buttonHandler_mutedVoices` and calls
`led_setActiveVoiceLeds()` on each iteration (redundant but correct).

**Option B — add a bulk unmute function** to buttonHandler.c:

```c
void buttonHandler_clearAllMutes(void)
{
    buttonHandler_mutedVoices = 0u;
    buttonHandler_showMuteLEDs();
}
```

Call it from `menu_applyQueuedSceneAction()` after the sequencer unmute.

Recommendation: **Option B** — one call, no loop, and
`buttonHandler_showMuteLEDs()` already handles the LED refresh and sets
`menu_muteModeActive`.

### Follow-up decision

**None required.**

---

## Implementation order

| Step | Defect(s) | Scope | Risk |
|------|-----------|-------|------|
| 1    | 5 + 6     | Remove PAR_SCENE_PC_RESET + supporting entries; rewrite sub-page 2 layout (3 scene globals + 4 compressor) | Low — removal + display reorder |
| 2    | 4         | ledHandler.c `led_initPerformanceLeds()` | Low — removes one unconditional LED write |
| 3    | 9         | buttonHandler.c SEQ press router, one-line looper guard | Low — single condition addition |
| 4    | 11        | menu.c `menu_applyQueuedSceneAction()`, buttonHandler.c new `buttonHandler_clearAllMutes()` | Low — LED sync, no state-machine change |
| 5    | 3         | buttonHandler.c SHIFT release/press handlers | Medium — state-machine change, needs careful testing of SHIFT interactions |
| 6    | 1         | sequencer.c `seq_queueSceneChange()` instant fast path | Medium — ISR-adjacent code, needs transport-running test |
| 7    | 10        | ledHandler.c fast-blink bitset + timer, buttonHandler.c queue blink call | Medium — LED layer addition, needs visual verification |
| 8    | 7 + 8     | sequencer.c new `seq_loopUpdateMask()`, buttonHandler.c `buttonHandler_loopCheckDisengage()`, SELECT/VOICE/BAR release rework | Medium–High — looper state-machine, must preserve start points, needs extensive hold-combination testing |
| 9    | 8a        | New PAR_TRACK_LOOPABLE per-track param, SEQ_PAGE cell, storage, mask filtering in looper engage | Medium — new parameter plumbing, storage format change |
| 10   | 2         | Phase G full implementation (G1–G12): chain fields, chain menu, chain evaluation, chain storage | High — new feature, 12 sub-steps across SceneData, buttonHandler, sequencer, storageTypes |

Steps 1–7 are independent and can be implemented in any order.
Step 8 is the looper state-machine rework (defects 7 + 8 together).
Step 9 (loopable parameter) depends on step 8's mask infrastructure.
Step 10 (Phase G) is independent of steps 1–9.

---

## Summary of follow-up decisions needed

**None.**  All decisions have been resolved.
