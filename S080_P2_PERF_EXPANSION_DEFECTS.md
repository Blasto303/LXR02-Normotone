# S080 P2 — PERF Expansion Defect Plan

Testing feedback from S080 implementation rounds.
Sixteen items total — eleven functional bugs, one duplicate parameter removal,
one layout issue, one new parameter addition, one spec correction, one
option-set change.  Plus Phase G full implementation.
No code implementation in this document; remediation steps only.

Defects 1–11 and 8a: identified after P1 (first implementation round).
Defect 14: spec correction (loop start on scene change), user, after P2.
Defect 15: queued scene change gets its own PC reset inside the queue action
options (user, after P2).
Defects 12–13: identified during P2 hardware testing.  P2 implemented all
original defect fixes; hardware testing revealed two new defects and two
incomplete fix specifications (defects 7 and 10 amended below).  All P2
code was rolled back.

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
4.  For each track in `removed = seq_loopTrackMask & ~newMask` (tracks
    being dropped): call `seq_realignTrackToMasterClock(track)` to
    restore the track's step index and DDA accumulator to the master
    clock position.  Without this, dropped tracks remain frozen at
    their last loop-wrap position and play out of sync.  Clear their
    start points after realignment.
5.  Update `seq_loopTrackMask = newMask`.
6.  Do NOT reset `seq_loopTickCount` — the loop period continues
    uninterrupted.

Replace the `seq_loopDisengage()` + `seq_loopEngage()` call in the VOICE
press handler (buttonHandler.c:1891–1893) with
`seq_loopUpdateMask(mask)`.

### Follow-up decision

**None required.**  The incremental-snapshot semantics are fully specified
by the user.

### P2 testing note

P2 implemented `seq_loopUpdateMask()` per the original fix but treated
dropped-track cleanup as cosmetic (clearing start points only).  Hardware
testing revealed that dropped tracks must be realigned to the master clock
via `seq_realignTrackToMasterClock()`.  Additionally, the VOICE release
path calls `seq_loopUpdateMask(mask & ~bit)` BEFORE
`buttonHandler_loopCheckDisengage()`, which means `seq_loopDisengage()`
reads the already-reduced mask and skips realignment for the just-dropped
track.  The amended step 4 above resolves this by performing realignment
inside `seq_loopUpdateMask()` itself, so it executes before the mask is
committed.  `seq_loopDisengage()` remains the full-termination path for
when all buttons are released.

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

**Critical: layer flag isolation.**  The standard blink off-path
(`led_setBlinkLed(ledNr, 0u)`) clears `LED_LAYER_BLINK` for that LED.
`led_renderFromStack()` checks this flag — if cleared, it falls through
to `led_reset()`, which restores the base LED state and overrides the
fast blink toggle.  `menu_refreshPerfSceneLeds()` clears ALL standard
blinks on every beat-pulse refresh, so fast blink on any SEQ LED will
be killed within one beat.

The fast-blink mechanism must isolate its layer state from standard
blink.  Two approaches:

**Approach 1 — guard the clear path:**  In `led_setBlinkLed(ledNr, 0u)`,
before clearing `LED_LAYER_BLINK`, check whether the LED is in the
fast-blink table (`led_fastBlinkLeds`).  If so, leave `LED_LAYER_BLINK`
set — the fast blink still needs it.

**Approach 2 — re-assert in tick handler:**  In `led_tickHandler()`'s
fast-blink block, unconditionally re-assert `LED_LAYER_BLINK` for
each active fast-blink LED on every toggle cycle.

Recommendation: **Approach 1** — a single guard in the standard blink
clear path is cleaner than periodic re-assertion and prevents the
one-tick glitch that approach 2 would allow between clear and re-assert.

### Follow-up decision

**None required.**

### P2 testing note

P2 implemented Option A (per-slot `led_fastBlinkLeds` bitset with
`led_setFastBlinkLed()` and 125 ms toggle) but did not address the
`LED_LAYER_BLINK` sharing.  Hardware testing confirmed that the queued
scene LED still did not flash at double rate.  The layer flag isolation
above is required for the fast blink to survive
`menu_refreshPerfSceneLeds()` refresh cycles.

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

## Defect 12 — Scene change restarts from bar 0 instead of master clock position

### Observed behaviour

When a scene change fires (direct or queued), the new scene restarts
from the beginning (bar 0, step 0) instead of continuing from the
sequencer's current master clock position.  Before S080 P2, scene
changes continued seamlessly from the current position.  This is a
regression introduced by P2 code.

### Root cause

The scene-change execution path passes through
`seq_selectActivePattern()` (sequencer.c:~1587) and may call
`seq_resetToPatternStart()` unconditionally or under conditions that
are too broad.

Two P2-era changes are suspect:

1.  **`seq_selectActivePattern()` modifications**: Chain counter resets
    and `seq_previousScene` recording were added for Phase G.  If
    these interact with the step-index state or trigger a pattern-start
    reset, all tracks restart from step 0.

2.  **`seq_requestSceneChange()` instant fast path**: Setting
    `seq_sceneFireFlag = 1u` when division units == 0 fires the
    foreground executor immediately.  The normal deferred path
    (ISR-aligned) preserves the master clock position because it
    fires at a musical boundary.  The instant path may bypass the
    alignment logic that keeps tracks at their current position.

The exact mechanism must be traced before implementation.  The fix
must ensure that scene changes (both deferred and instant) preserve
`seq_elapsedPpqTicks` and each track's DDA phase, recomputing step
positions from the master clock rather than resetting to zero.

### Fix

1.  **Audit `seq_selectActivePattern()`**: Verify that no code path
    unconditionally calls `seq_resetToPatternStart()`.  The reset
    must only fire when `PAR_BAR_RESET_MODE` is explicitly set to
    a reset-on-change value AND the user has not overridden it.

2.  **Audit the instant fast path**: If `seq_sceneFireFlag = 1u` in
    the foreground fires `menu_perfSceneChangeExecute()`, verify
    that the execute function calls `seq_realignTrackToMasterClock()`
    for each track (or the equivalent pattern-change alignment) rather
    than `seq_resetToPatternStart()`.

3.  **Preserve master clock continuity**: After a scene change,
    `seq_elapsedPpqTicks` must NOT be reset.  Each track's step
    index and DDA accumulator must be recomputed from the master
    clock using `seq_realignTrackToMasterClock()` (same function
    used by the looper disengage path).

4.  **Test matrix**: Verify scene changes at all `PAR_SCENE_CHANGE_DIV`
    values (0/instant through bar) preserve position.  Verify queued
    changes at all `PAR_SCENE_QUEUE_DIV` values likewise.

### Follow-up decision

**Resolved by code audit (S080 P2 implementation schedule).**  Neither
suspect is the cause.  `seq_selectActivePattern()` realigns to the master
clock and never resets.  The cause is the Defect 5 merge itself: P2 changed
the scene-change reset check in `menu_perfSceneChangeExecute()` from
`PAR_SCENE_PC_RESET` (default off) to `PAR_BAR_RESET_MODE`, whose
pre-S080 meaning was only "reset the bar counter at a boundary pattern
commit".  With `bar_reset_mode=1` in settings.cfg, every scene change ran
`seq_resetToPatternStart()`, which zeroes `seq_elapsedPpqTicks`, the master
step clock and every track cursor.  With pcr off, scene changes realign to
the master clock as before.  With pcr on, a direct or chain-fired change is
supposed to restart the tracks (§5.2), so the restart is the specified
behaviour, not a regression.  Queued changes no longer read pcr at all
(Defect 15).  `seq_resetToPatternStart()` must also stop disengaging the
looper (Defect 14).

---

## Defect 13 — Chain editor viewed scene LED killed by refresh

### Observed behaviour

In the chain editor (SHIFT+PLAY+SEQ), the viewed scene's SEQ LED
does not flash when the transport is stopped.  It should blink at
the standard rate to indicate which scene the editor is viewing,
regardless of transport state.

### Root cause

`menu_refreshPerfSceneLeds()` (menu.c:8192–8256) runs a sweep that
clears ALL blinks on the SEQ LED row:

```c
for (uint8_t i = 0u; i < NUM_SCENES; i++)
    led_setBlinkLed((uint8_t)(LED_SEQ1 + i), 0u);
```

It then re-blinks only the active scene.  It has no knowledge of
the chain editor state (`bh_chainMenuActive`, `bh_chainMenuScene`)
and does not re-blink the viewed scene.

This function is called from multiple paths:
*  `led_tickHandler()` beat-pulse refresh (every beat)
*  Scene change execution
*  Transport start/stop

Every call wipes the chain editor's viewed-scene blink.
`menu_chainMenuOpen()` sets the blink once on entry, but the next
`menu_refreshPerfSceneLeds()` call clears it.

### Fix

At the end of `menu_refreshPerfSceneLeds()`, after the active-scene
blink is set, add a chain-editor guard:

```c
if (bh_chainMenuActive) {
    led_setBlinkLed((uint8_t)(LED_SEQ1 + bh_chainMenuScene), 1u);
}
```

This re-asserts the viewed-scene blink on every refresh cycle,
so it survives beat-pulse refreshes and transport state changes.

If the viewed scene IS the active scene, both blinks target the
same LED — no conflict.  If they differ, both blink at standard
rate, which is the intended behaviour (active scene blinks, viewed
scene blinks, performer can tell them apart by context).

### Follow-up decision

**None required.**  The guard is a one-line addition to an existing
function.

---

## Defect 14 — Loop start point on scene change (spec correction)

### Corrected requirement

S080 P1 plan §3.6 said a scene change during a loop resets the loop start
to the very beginning of the new scene.  That was a misstatement.  The
correct rule:

* On a scene change while the looper is engaged, each looped track's loop
  start point stays at the **same position relative to the master clock**
  that it had in the outgoing scene.
* The incoming scene's track is placed at that master-clock point using the
  incoming scene's own effective step scale and length.
* Example: outgoing track at 1/16 with the loop start at the beginning of
  step 5 (master tick 4 × 24 = 96).  Incoming track at 1/8 (48 ticks per
  step): 96 / 48 = 2 → the loop start is the beginning of step 3.
* If the master-clock point falls mid-step in the incoming scene (e.g. an
  outgoing 1/16 start at step 6 = tick 120, incoming 1/8 → 2.5 steps), the
  loop starts mid-way through step 3.  The loop start is a master-clock
  position, not a step boundary.
* The loop length (division + modifier) and the wrap timing do not change.
  Wraps continue at the same master-clock times as before the scene change.
* This applies to every scene change that can happen during a loop: direct
  (SEQ), queued (SHIFT+SEQ), and chain-fired.  Per-track scene changes
  (TRACK+SEQ) cannot occur during a loop (Defect 9).

### Implementation notes

1.  The looper stores each looped track's loop start as a master-clock tick
    (a per-track anchor in `seq_elapsedPpqTicks` units) plus the track's
    state just before that tick (step index, DDA accumulator, PIP
    direction).  Wraps fire when `(elapsed − anchor) % loopTicks == 0`,
    before the tick's DDA advance.  The anchor is what this rule keeps
    fixed across a scene change.

2.  On scene change, the anchors are **not** touched.  Only each looped
    track's snapshot is recomputed for the incoming scene, using the
    master-clock position mapping already used by
    `seq_realignTrackToMasterClock()` (`position = (tick << 8) / interval`,
    accumulator = remainder, play mode applied to the index), evaluated at
    the anchor tick rather than at the current tick.  The snapshot is the
    state just before the anchor tick's advance, i.e. the mapping evaluated
    at `anchor − 1`; an anchor of 0 uses the transport-start seed (index −1,
    accumulator `interval − 256`).

3.  Ordering inside `seq_selectActivePattern()`: the snapshot recompute must
    run after `seq_refreshTrackEffectiveParams()` (so it uses the incoming
    scene's effective scale and length) and in the same TIM3-masked section
    as the realignment, so the ISR never wraps a track to a snapshot from
    the outgoing scene.

4.  Between the scene change and the next wrap, the looped track plays the
    incoming scene from its realigned master-clock position (the same as a
    non-looped track).  At the next wrap it jumps to the incoming scene's
    content at the anchor.

5.  Loop release after a scene change: the track resumes where it would
    have been without the loop.  Projecting the recomputed snapshot forward
    from the anchor gives the incoming scene's master-clock position, so
    the same projection path serves both the no-scene-change and the
    scene-change cases.  No separate "resume by realign" flag is needed.

6.  The previous `seq_setStepIndexToStart()` path clears `seq_loopActive`.
    A scene change must not use that path to disengage the loop; the loop
    survives the change with its anchors intact.

### Resetting scene changes (decided)

A resetting scene change restarts the master clock at 0
(`seq_resetToPatternStart()`), so the outgoing anchor tick no longer exists
in the new timeline.  **Decision (user):** the anchor becomes tick 0 of the
restarted clock, i.e. the loop start is the beginning of the incoming scene.
A change is resetting when it is a direct or chain-fired change with pcr on,
or a queued change whose queue action includes reset (Defect 15).

---

## Defect 15 — Separate PC reset for queued scene changes (option change)

### Requirement (user, after P2)

A queued scene change (SHIFT+SEQ) has its own PC reset, separate from the
pcr setting that governs normal (direct) scene changes.  The queued reset is
selected through the queue action (`qac`) options, which become:

| Value | Display | Action on the queued change |
|-------|---------|-----------------------------|
| 0     | `nrm`   | Normal change (default, unchanged). |
| 1     | `mut`   | Reset all mutes to off. |
| 2     | `mrp`   | Reset all morphs of the incoming scene to 0. |
| 3     | `res`   | Reset: tracks and master clock restart from the beginning. |
| 4     | `u+r`   | Mute reset + reset. |
| 5     | `o+r`   | Morph reset + reset. |
| 6     | `mmr`   | Mute reset + morph reset + reset. |
| 7     | `rld`   | Reload the scene from its source; also resets mutes and morphs. |
| 8     | `r+r`   | Reload + reset (also resets mutes and morphs). |

`nrm` is kept as value 0 so the power-on default stays a plain change.  The
previous `m+m` (mute + morph, no reset) is not in the new list and is
removed.

### Rules

* **Direct changes (SEQ)** and **chain-fired changes** reset only when pcr
  (`PAR_BAR_RESET_MODE`) is on.
* **Queued changes (SHIFT+SEQ)** ignore pcr.  They reset only when the queue
  action is `res`, `u+r`, `o+r`, `mmr` or `r+r`.
* Mute reset clears both the sequencer mute state and the VOICE LED mirror
  (Defect 11).
* Morph reset applies `preset_morphScene(target, 0)` to the incoming scene
  only (overall Morph, all six voice Morphs and the Effect Morph).  It does
  not fan out through the VOICE edit mask.  It is applied before the change
  executes, so the incoming scene starts at Morph 0.
* A resetting change while the looper is engaged moves every looped track's
  loop start to the beginning of the incoming scene (Defect 14).
* Reload: the queue fires the scene change with mute and morph reset; the
  actual reload of the scene from its library source is **deferred** (it
  needs a performance-safe Scene Load path from the queue executor).  Until
  then `rld` behaves as mute + morph reset and `r+r` as mute + morph +
  reset.  This matches plan §9.1-8 ("degrades to a normal change").

### Storage

`qac` stays one settings.cfg key with values 0..8.  Old settings files that
stored 3 (`m+m`) or 4 (`rld`) now read as `res` and `u+r`; backward
compatibility is not a concern (user).

---

## Implementation order

| Step | Defect(s) | Scope | Risk |
|------|-----------|-------|------|
| 1    | 5 + 6     | Remove PAR_SCENE_PC_RESET + supporting entries; rewrite sub-page 2 layout (3 scene globals + 4 compressor) | Low — removal + display reorder |
| 2    | 4         | ledHandler.c `led_initPerformanceLeds()` | Low — removes one unconditional LED write |
| 3    | 9         | buttonHandler.c SEQ press router, one-line looper guard | Low — single condition addition |
| 4    | 11 + 15   | menu.c `menu_applyQueuedSceneAction()` rework (9 queue actions, separate queued reset, morph reset), buttonHandler.c new `buttonHandler_clearAllMutes()`, MenuText.h names, clamp 0..8 | Low–Medium — executor signature gains a reset argument |
| 5    | 3         | buttonHandler.c SHIFT release/press handlers | Medium — state-machine change, needs careful testing of SHIFT interactions |
| 6    | 1         | sequencer.c `seq_queueSceneChange()` instant fast path | Medium — ISR-adjacent code, needs transport-running test |
| 7    | 12        | Audit `seq_selectActivePattern()` and instant fast path for scene restart regression; fix to preserve master clock position | Medium–High — requires code audit first, regression risk if wrong |
| 8    | 10        | ledHandler.c fast-blink bitset + timer + **layer flag isolation** in `led_setBlinkLed()` clear path, buttonHandler.c queue blink call | Medium–High — LED layer rework, must resolve `LED_LAYER_BLINK` sharing (P2 amended) |
| 9    | 7 + 8 + 14 | sequencer.c `seq_loopUpdateMask()` with **dropped-track realignment**, buttonHandler.c `buttonHandler_loopCheckDisengage()`, SELECT/VOICE/BAR release rework; scene change keeps loop anchors and recomputes snapshots for the incoming scene (Defect 14) | High — looper state-machine, realignment in mask-update path is critical (P2 amended) |
| 10   | 8a        | New PAR_TRACK_LOOPABLE per-track param, SEQ_PAGE cell, storage, mask filtering in looper engage | Medium — new parameter plumbing, storage format change |
| 11   | 2 + 13    | Phase G full implementation (G1–G12) + chain editor LED guard in `menu_refreshPerfSceneLeds()` | High — new feature, 12 sub-steps; defect 13 LED guard is naturally part of Phase G |

Steps 1–6 are independent and can be implemented in any order.
Step 7 (scene restart regression) requires a code audit before writing fixes.
Step 8 (fast-blink) is independent but now carries higher risk due to
layer isolation requirement discovered in P2.
Step 9 is the looper state-machine rework (defects 7 + 8 together), now
including dropped-track realignment discovered in P2 and the Defect 14
rule that a scene change keeps each looped track's master-clock loop start.
Step 10 (loopable parameter) depends on step 9's mask infrastructure.
Step 11 (Phase G + defect 13) is independent of steps 1–10; the chain
editor LED fix (defect 13) is folded into Phase G because it modifies
`menu_refreshPerfSceneLeds()` which must be aware of chain editor state
variables that Phase G introduces.

---

## Summary of follow-up decisions needed

**None.**  Defect 12's root cause is confirmed (the pcr merge, see
Defect 12).  The Defect 14 resetting-change rule is decided (loop start
becomes the beginning of the incoming scene).  The reload part of `rld` /
`r+r` (Defect 15) is deferred and degrades as described.
