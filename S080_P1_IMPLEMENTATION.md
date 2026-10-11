# S080 P1 — Full Code Implementation Schedule

Every change cited by **file**, **line**, and **add/remove/modify**. Comment-block
descriptions are written to serve as documentation-in-place adjacent to the changes
in both `.c` and `.h` files. Organized by implementation phase from the plan
(S080_P1_PERF_MODE_UPDATES.md §7).

---

## Phase A — Menu Re-order + Page-Entry Bug Fix

### A1. menuPages.h — PERFORMANCE_PAGE table rewrite

**File:** `Core/Menu/menuPages.h`
**Line:** 72
**Action:** MODIFY

Replace the single populated subpage row with two subpage rows.

Old (line 72):
```
{TEXT_X_FADE,TEXT_VOICE1_MORPH,TEXT_VOICE2_MORPH,TEXT_VOICE3_MORPH,
 TEXT_VOICE4_MORPH,TEXT_VOICE5_MORPH,TEXT_VOICE6_MORPH,TEXT_EFFECT_MORPH,
 PAR_MORPH,PAR_VOICE1_MORPH,PAR_VOICE2_MORPH,PAR_VOICE3_MORPH,
 PAR_VOICE4_MORPH,PAR_VOICE5_MORPH,PAR_VOICE6_MORPH,PAR_EFFECT_MORPH},
```

New (line 72, replacing single row; line 73 shifts to blank-subpage padding):
```
/* Subpage 0: mrp, fxm, 1VM, 2VM.  Cells 4–7 are terminators. */
{TEXT_X_FADE,TEXT_EFFECT_MORPH,TEXT_VOICE1_MORPH,TEXT_VOICE2_MORPH,
 TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 PAR_MORPH,PAR_EFFECT_MORPH,PAR_VOICE1_MORPH,PAR_VOICE2_MORPH,
 PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
/* Subpage 1: 3VM, 4VM, 5VM, 6VM.  Cells 4–7 are terminators. */
{TEXT_VOICE3_MORPH,TEXT_VOICE4_MORPH,TEXT_VOICE5_MORPH,TEXT_VOICE6_MORPH,
 TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 PAR_VOICE3_MORPH,PAR_VOICE4_MORPH,PAR_VOICE5_MORPH,PAR_VOICE6_MORPH,
 PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

Remaining subpage rows (currently lines 73–79) shift down by one line and the
last empty-subpage row is removed to keep the total at `NUM_SUB_PAGES` (8).

```
/*
 * S080 A1: PERF page split into two subpages.
 *
 * What:       subpage 0 = mrp (overall Morph), fxm (Effect Morph),
 *             1VM, 2VM; subpage 1 = 3VM, 4VM, 5VM, 6VM.  Four
 *             active cells per subpage; cells 4–7 are TEXT_EMPTY/
 *             PAR_NONE terminators.
 * Why:        the prior 8-cell single page required subpage scrolling
 *             to reach later voices.  The new layout groups the two
 *             global controls (mrp, fxm) on the landing subpage and
 *             moves per-voice Morph controls to a second subpage.
 * Inputs:     static compile-time table consumed by
 *             menu_repaintGeneric() and encoder/subpage traversal.
 * Outputs:    the generic page renderer reads cells 0–3 on the active
 *             subpage; the TEXT_EMPTY terminator signals the end.
 * Accessors:  menu_resolveCell(), menu_switchSubPage(),
 *             menu_getSubPage(), menu_moveToMenuItem().
 * Affiliates: menu_applyPerfMarkers() (A3), va_searchRestart (A4),
 *             buttonHandler handleModeButtons (A2).
 */
```

---

### A2. buttonHandler.c — PERF page-entry bug fix

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 1220 (after `menu_switchSubPage(0)`)
**Action:** ADD

Add a call to `menu_resetActiveParameter()` after `menu_switchSubPage(0)` to
ensure the within-subpage parameter cursor is at cell 0.

Current sequence (lines 1219–1221):
```c
menu_switchPage(PERFORMANCE_PAGE);
menu_switchSubPage(0);
menu_repaintAll();
```

Insert between lines 1220 and 1221:
```c
menu_resetActiveParameter();
```

Note: `menu_resetActiveParameter()` is also called inside `menu_switchPage()` at
line 15006 of menu.c, but that call runs after the LED update block and only
covers the generic-voice case. The explicit call here ensures the PERF page
cursor lands on cell 0 (mrp) regardless of the prior page's cursor position.

```
/*
 * S080 A2: reset parameter cursor after PERF page subpage switch.
 *
 * What:       calls menu_resetActiveParameter() so the display
 *             opens on cell 0 (mrp) of PERFORMANCE_PAGE subpage 0.
 * Why:        without this, if the previous page visit left menuIndex
 *             at cell 4+, the display would start mid-page (e.g.
 *             showing 4VM instead of mrp).  menu_switchPage() calls
 *             menu_resetActiveParameter() at line 15006 of menu.c,
 *             but the PERFORMANCE_PAGE case shares the SEQ/PATTERN
 *             path and the reset may not re-zero when the page enum
 *             hasn't changed.
 * Inputs:     none.  Reads menuIndex internally.
 * Outputs:    menuIndex cursor set to cell 0 of the active subpage.
 * Accessors:  handleModeButtons() SELECT_MODE_PERF case (here),
 *             SHIFT release PERF case at line 2093.
 * Affiliates: menu_switchSubPage() (menu.c:14539),
 *             menu_resetActiveParameter() (menu.c:14639).
 */
```

**Also apply at line 2096 (SHIFT release, PERF case):**

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** 2096 (inside SHIFT release SELECT_MODE_PERF case, after
`menu_switchPage(PERFORMANCE_PAGE)`)
**Action:** ADD

Insert `menu_resetActiveParameter()` after line 2096's
`led_initPerformanceLeds()` and before the return.

---

### A3. menu.c — Update menu_applyPerfMarkers() for new cell positions

**File:** `Core/Menu/menu.c`
**Lines:** 3398–3458
**Action:** MODIFY

The automation-presence marker logic currently assumes the old 8-cell
single-subpage layout. The new 2-subpage layout changes which cell indices
hold which morph parameters. Update the column-to-bitmask mapping.

Old mapping (subpage 0, 8 cells):
- Column 0: mrp (global Morph)
- Columns 1–6: voice Morph 1–6 → bits 0–5 of `va_searchPerfMorphMask`
- Column 7: fxm → `VA_PERF_MORPH_EFFECT_BIT` (0x40)

New mapping:
- **Subpage 0:** column 0 = mrp, column 1 = fxm, columns 2–3 = 1VM/2VM
- **Subpage 1:** columns 0–3 = 3VM/4VM/5VM/6VM

The function must read `menu_getSubPage()` to determine which subpage is active
and adjust the column-to-bitmask mapping accordingly.

```
/*
 * S080 A3: adapt PERF automation markers to 2-subpage layout.
 *
 * What:       menu_applyPerfMarkers() underlines cells whose morph
 *             parameter has Scene automation.  The column-to-bit
 *             mapping now depends on the active subpage:
 *             subpage 0 → col 0 = mrp (no mask bit), col 1 = fxm
 *             (VA_PERF_MORPH_EFFECT_BIT), cols 2–3 = 1VM/2VM
 *             (bits 0–1).
 *             subpage 1 → cols 0–3 = 3VM/4VM/5VM/6VM (bits 2–5).
 * Why:        the prior single-subpage mapping hard-coded column
 *             offsets that no longer match the page table.
 * Inputs:     menu_getSubPage(), va_searchComplete,
 *             va_searchPerfMorphMask.
 * Outputs:    underline characters written to the display buffer
 *             under automated PERF cells.
 * Accessors:  menu_repaintGeneric() calls this at line 11815.
 * Affiliates: va_searchRestart() (A4), PERFORMANCE_PAGE table (A1).
 */
```

---

### A4. menu.c — Update va_searchRestart() for PERF morph scan

**File:** `Core/Menu/menu.c`
**Lines:** 2128–2138
**Action:** MODIFY

The automation-presence scanner currently scans all 8 cells of the single PERF
subpage. With two subpages, the scan must cover both subpages (cells from both
subpage 0 and subpage 1) to set the correct `va_searchPerfMorphMask` bits.

```
/*
 * S080 A4: extend PERF morph automation scan to two subpages.
 *
 * What:       va_searchRestart() on the PERFORMANCE_PAGE must scan
 *             all morph parameters across both subpages to populate
 *             va_searchPerfMorphMask correctly.  The scan iterates
 *             PAR_MORPH, PAR_EFFECT_MORPH, PAR_VOICE1_MORPH through
 *             PAR_VOICE6_MORPH directly (parameter-based, not cell-
 *             position-based), setting the appropriate mask bit for
 *             each parameter that has active automation.
 * Why:        the prior scan walked subpage 0's 8 cells; with the
 *             split, a cell-walk would only see 4 cells per subpage.
 *             Switching to parameter-based scan is robust against
 *             future cell rearrangement.
 * Inputs:     automation data from PatternData via the automation
 *             scanner.
 * Outputs:    va_searchPerfMorphMask bits and va_searchComplete flag.
 * Accessors:  menu_switchPage(PERFORMANCE_PAGE) triggers this at
 *             line 14857.
 * Affiliates: menu_applyPerfMarkers() (A3), PAR_VOICE1_MORPH..
 *             PAR_VOICE6_MORPH (ParameterArray.h:180–185).
 */
```

---

### A5. menu.c — PERF LED update for two-subpage SELECT indication

**File:** `Core/Menu/menu.c`
**Lines:** 14982–14994
**Action:** MODIFY

After the page switch, the SELECT LED group must indicate the active subpage.
Currently `led_initPerformanceLeds()` lights only SELECT1. With two subpages,
SELECT1 or SELECT2 should illuminate to indicate the current subpage.

```
/*
 * S080 A5: PERF entry LED updates for multi-subpage indication.
 *
 * What:       after PERFORMANCE_PAGE switch, light SELECT1 for
 *             subpage 0 or SELECT2 for subpage 1.  Only one SELECT
 *             LED is lit at a time (no blink, no flash).
 * Why:        with two subpages the user needs a visual indicator of
 *             which subpage is active.  This mirrors the SELECT LED
 *             behavior on multi-subpage voice pages.
 * Inputs:     menu_getSubPage().
 * Outputs:    LED_PART_SELECT1 or LED_PART_SELECT2 set via
 *             led_setValue(); all other SELECT LEDs cleared.
 * Accessors:  menu_switchPage() LED block (here), SHIFT release
 *             PERF case (buttonHandler.c:2093).
 * Affiliates: led_clearSelectLeds() (ledHandler.c:1073),
 *             led_initPerformanceLeds() (ledHandler.c:1158).
 */
```

---

## Phase B — New Trigger Conditions (lop / !lp)

### B1. MenuText.h — Insert lop/!lp in conditionNames

**File:** `Core/Menu/MenuText.h`
**Lines:** 224–234
**Action:** MODIFY

Insert `{"lop"},{"!lp"}` after `{"!fl"}` (currently at position 22 in the data,
index 22). Bump the count byte from `{39}` to `{41}`.

Old (line 225–230):
```c
{39},
...
{"fil"},{"!fl"},{"lst"},{"!ls"},
```

New:
```c
{41},
...
{"fil"},{"!fl"},{"lop"},{"!lp"},{"lst"},{"!ls"},
```

```
/*
 * S080 B1: add loop-active/inactive trigger conditions.
 *
 * What:       two new conditions inserted after !fl (index 22):
 *             index 23 = "lop" (loop active), index 24 = "!lp"
 *             (loop inactive).  Existing conditions at old indices
 *             23+ shift up by 2.  Total count: 39 → 41.
 * Why:        allows step triggers to fire only when the PERF looper
 *             is engaged or disengaged, enabling performance-aware
 *             pattern programming.
 * Inputs:     static const table consumed by menu rendering and
 *             step-condition formatters.
 * Outputs:    conditionNames[24] = "lop", conditionNames[25] = "!lp".
 * Accessors:  menu.c condition name formatting, menu_repaintGeneric()
 *             step-specials condition display.
 * Affiliates: seq_evaluateStepCondition() (B2), the 6-bit condition
 *             field in pat_step_specials_t (PatternData.h).
 */
```

---

### B2. sequencer.h — Declare seq_loopActive extern

**File:** `Core/Sequencer/sequencer.h`
**Line:** after line 50 (after `seq_fillActive` extern block)
**Action:** ADD

```c
extern uint8_t seq_loopActive;
```

```
/*
 * S080 B2: extern declaration for loop-active ISR flag.
 *
 * What:       single-byte flag, 0 = no loop, 1 = looper engaged.
 *             Written by the foreground looper (Phase C) as the
 *             final atomic gate after publishing loop config.  Read
 *             by the TIM3 ISR in seq_evaluateStepCondition() and
 *             seq_advanceTrackStep().
 * Why:        single-byte reads/writes are atomic on Cortex-M7,
 *             identical to the seq_fillActive pattern (line 50).
 *             The ISR ignores all loop config while this flag is 0.
 * Inputs:     foreground looper engage/disengage.
 * Outputs:    ISR condition evaluation and loop-wrap logic.
 * Accessors:  sequencer.c (definition), buttonHandler.c (writer),
 *             seq_evaluateStepCondition() (reader).
 * Affiliates: seq_fillActive (sequencer.h:50, sequencer.c:256).
 */
```

---

### B3. sequencer.c — Define seq_loopActive

**File:** `Core/Sequencer/sequencer.c`
**Line:** after line 256 (after `seq_fillActive` definition block)
**Action:** ADD

```c
uint8_t seq_loopActive = 0u;
```

```
/*
 * S080 B3: loop-active flag definition.
 *
 * What:       single-byte atomic flag paralleling seq_fillActive.
 *             0 = no loop, 1 = looper is engaged.  Published by the
 *             foreground looper after all per-track loop config
 *             (division, mask, start points) is coherent.
 * Why:        the ISR reads this flag before consulting any loop
 *             config fields.  The foreground writes config first,
 *             then sets this flag to 1, ensuring the ISR never
 *             sees partial config.  Same pattern as seq_fillActive.
 * Inputs:     foreground looper (Phase C buttonHandler/menu code).
 * Outputs:    ISR paths in seq_evaluateStepCondition() and
 *             seq_advanceTrackStep().
 * Accessors:  extern in sequencer.h.
 * Affiliates: seq_fillActive (line 256), Phase C looper engine.
 */
```

---

### B4. sequencer.c — Add lop/!lp dispatch in seq_evaluateStepCondition()

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 1476–1478 (after the `!fl` case at line 1476–1477)
**Action:** ADD

Insert two new cases after the `!fl` block and before the `lst` block. The
existing condition numbers at old indices 23+ all shift by 2.

```c
    /* S080 B4: loop active */
    } else if (cond == 23u) {
        result = seq_loopActive;
    /* S080 B4: loop inactive */
    } else if (cond == 24u) {
        result = !seq_loopActive;
```

Update all subsequent magic numbers in the function: `lst` moves from 23→25,
`!ls` from 24→26, mute range 25–31→27–33, not-mute range 32–38→34–40. The
out-of-range guard at line 1444 changes from `cond > 38u` to `cond > 40u`.

```
/*
 * S080 B4: loop-active/inactive condition dispatch.
 *
 * What:       condition index 23 ("lop") fires when seq_loopActive
 *             is nonzero; index 24 ("!lp") fires when it is zero.
 *             Both evaluate instantaneously — no boundary timing.
 * Why:        allows pattern steps to be conditionally triggered
 *             based on whether the PERF looper is engaged.  The ISR
 *             reads seq_loopActive (single byte, Cortex-M7 atomic).
 * Inputs:     seq_loopActive (foreground-written), step condition
 *             index from pat_step_specials_t.
 * Outputs:    result = 1 (fire) or 0 (skip), fed into
 *             seq_trackLastConditionFired[] at line 1499.
 * Accessors:  seq_advanceTrackStep() calls this function at the
 *             step-fire decision point.
 * Affiliates: conditionNames[] (B1), seq_loopActive (B3), mute-
 *             bypass logic at lines 1835–1857 (shift indices).
 */
```

Also update the **mute-bypass self-referential check** in seq_advanceTrackStep():

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 1849–1854
**Action:** MODIFY

Shift the mute condition index ranges by +2:
- Self-mute: `cond >= 25u && cond <= 31u` → `cond >= 27u && cond <= 33u`
- Self-not-mute: `cond >= 32u && cond <= 38u` → `cond >= 34u && cond <= 40u`
- Voice index: `(cond - 25u) % 7u` → `(cond - 27u) % 7u`

```
/*
 * S080 B4 (affiliate): shift mute-bypass condition indices.
 *
 * What:       the self-referential mute condition check uses magic
 *             numbers for the mute/not-mute condition ranges.  The
 *             insertion of lop/!lp at indices 23–24 shifts all mute
 *             conditions by +2.
 * Why:        without this shift, the mute bypass would trigger on
 *             the wrong condition indices after the table insertion.
 * Inputs:     condition index from step specials.
 * Outputs:    process_step override for self-referential mute
 *             conditions on muted tracks.
 * Accessors:  seq_advanceTrackStep() mute-bypass block.
 * Affiliates: seq_evaluateStepCondition() (B4 main change).
 */
```

---

### B5. sequencer.c — Clear seq_loopActive on transport stop

**File:** `Core/Sequencer/sequencer.c`
**Line:** 2885 (inside seq_setRunning() stop branch, after `seq_deltaT = 0`)
**Action:** ADD

```c
seq_loopActive = 0u;
```

```
/*
 * S080 B5: clear looper flag on transport stop.
 *
 * What:       seq_loopActive is forced to 0 when transport stops.
 * Why:        the looper is a runtime-only performance feature that
 *             must not persist across transport cycles.  Clearing
 *             here ensures seq_evaluateStepCondition() sees no loop
 *             when the sequencer restarts.
 * Inputs:     seq_setRunning(0).
 * Outputs:    seq_loopActive = 0.
 * Accessors:  seq_setRunning() (line 2877).
 * Affiliates: Phase C looper disengage, buttonHandler fill deactivate
 *             at line 2057.
 */
```

---

## Phase C — Looper Core Engine

### C1. sequencer.h — Looper state declarations

**File:** `Core/Sequencer/sequencer.h`
**Line:** after the seq_loopActive block (added in B2)
**Action:** ADD

```c
extern uint8_t  seq_loopTrackMask;
extern uint8_t  seq_loopDivision;
extern uint8_t  seq_loopModifier;
extern int16_t  seq_loopStartStep[NUM_TRACKS];
extern uint16_t seq_loopStartAccum[NUM_TRACKS];
void seq_loopEngage(uint8_t trackMask, uint8_t division, uint8_t modifier);
void seq_loopDisengage(void);
void seq_loopUpdateDivision(uint8_t division, uint8_t modifier);
```

```
/*
 * S080 C1: looper state and API declarations.
 *
 * What:       the foreground looper publishes loop configuration
 *             through these fields.  seq_loopTrackMask (7-bit, one
 *             per track) selects which tracks loop.
 *             seq_loopDivision is the SELECT-button division index
 *             (0 = 1 bar, 7 = 1/128).  seq_loopModifier is 0 =
 *             none, 1 = dot (+50%), 2 = triplet (×2/3).
 *             seq_loopStartStep[] and seq_loopStartAccum[] record
 *             each track's DDA position at the moment of loop engage.
 * Why:        the ISR needs a complete, coherent snapshot of loop
 *             config to perform the wrap in seq_advanceTrackStep().
 *             The foreground writes all fields, then sets
 *             seq_loopActive = 1 as the final atomic gate.
 * Inputs:     foreground SELECT/VOICE button handlers.
 * Outputs:    ISR wrap logic in seq_advanceTrackStep().
 * Accessors:  seq_loopEngage() publishes config then sets flag.
 *             seq_loopDisengage() clears flag then restores tracks.
 *             seq_loopUpdateDivision() changes division mid-loop.
 * Affiliates: seq_loopActive (B2/B3), seq_stepIndex[] (line 172),
 *             seq_trackAccumulator[] (line 188),
 *             stepScale_ticksQ8() (StepScale.c:82).
 */
```

---

### C2. sequencer.c — Looper state definitions and functions

**File:** `Core/Sequencer/sequencer.c`
**Line:** after seq_loopActive definition (B3)
**Action:** ADD

Define the looper state variables and the three API functions.

```c
uint8_t  seq_loopTrackMask   = 0u;
uint8_t  seq_loopDivision    = 0u;
uint8_t  seq_loopModifier    = 0u;
int16_t  seq_loopStartStep[NUM_TRACKS];
uint16_t seq_loopStartAccum[NUM_TRACKS];

static uint16_t seq_loopLengthTicks(uint8_t division, uint8_t modifier);
```

```
/*
 * S080 C2: looper state definitions.
 *
 * What:       runtime-only looper state.  seq_loopTrackMask is a
 *             7-bit mask (one bit per track 0–6) selecting which
 *             tracks are looped.  seq_loopDivision is the SELECT
 *             button index: 0 = 1 bar (16 steps × default scale),
 *             1 = 1/2 bar, ... 7 = 1/128.  seq_loopModifier:
 *             0 = none, 1 = dot, 2 = triplet.
 *             seq_loopStartStep[] and seq_loopStartAccum[] snapshot
 *             each track's DDA state at engage time.
 * Why:        the foreground must publish a coherent config before
 *             setting seq_loopActive.  These fields are only read
 *             by the ISR when seq_loopActive == 1.
 * Inputs:     foreground looper API.
 * Outputs:    ISR wrap in seq_advanceTrackStep().
 * Accessors:  seq_loopEngage(), seq_loopDisengage(),
 *             seq_loopUpdateDivision().
 * Affiliates: seq_loopActive (B3), DDA accumulators (line 188).
 */
```

**seq_loopLengthTicks():** static helper that converts a division index and
modifier to a tick count in the master step clock domain.

Division mapping: 0 → 16×24 = 384 ticks (1 bar), 1 → 192 (1/2), 2 → 96 (1/4),
3 → 48 (1/8), 4 → 24 (1/16), 5 → 12 (1/32), 6 → 6 (1/64), 7 → 3 (1/128).

Modifier: dot multiplies by 3/2 (×1.5), triplet multiplies by 2/3.

**seq_loopEngage():**
1. Snapshot `seq_stepIndex[track]` and `seq_trackAccumulator[track]` for each
   track in `trackMask`.
2. Write `seq_loopTrackMask`, `seq_loopDivision`, `seq_loopModifier`.
3. Set `seq_loopActive = 1u` as the final atomic gate.
4. If recording is active, disable it: `seq_recordActive = 0u`.

**seq_loopDisengage():**
1. Set `seq_loopActive = 0u`.
2. For each track that was looping, call `seq_realignTrackToMasterClock()`.
3. Clear `seq_loopTrackMask`.

**seq_loopUpdateDivision():**
1. Write `seq_loopDivision` and `seq_loopModifier`.
2. No flag toggle needed — the ISR reads these fields continuously while
   `seq_loopActive == 1`.

```
/*
 * S080 C2: looper engage/disengage/update API.
 *
 * What:       seq_loopEngage() captures per-track DDA state,
 *             publishes config, and atomically arms the loop.
 *             seq_loopDisengage() clears the flag and realigns all
 *             looped tracks to the master clock.
 *             seq_loopUpdateDivision() hot-swaps the division and
 *             modifier while the loop is active.
 * Why:        the foreground/ISR boundary requires atomic gating.
 *             The foreground writes config, then sets the flag.
 *             The ISR never reads config while the flag is 0.
 *             On disengage, the flag is cleared first, then tracks
 *             are realigned — the ISR stops wrapping immediately.
 * Inputs:     trackMask (7-bit), division (0–7), modifier (0–2).
 * Outputs:    seq_loopActive flag, per-track start-point snapshots.
 *             On disengage: tracks realigned via
 *             seq_realignTrackToMasterClock().
 * Accessors:  buttonHandler SELECT/VOICE handlers (C4, C5).
 * Affiliates: seq_realignTrackToMasterClock() (sequencer.h:110),
 *             seq_setRecordingMode() (sequencer.c:3124).
 */
```

---

### C3. sequencer.c — Loop wrap in seq_advanceTrackStep()

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 1802–1809 (forward play mode wrap point — similar changes needed
for all play modes)
**Action:** MODIFY

After the normal step-advance and wrap logic in each play mode case, add a loop
boundary check. When `seq_loopActive && (seq_loopTrackMask & (1u << track))`,
compute whether the track has advanced past the loop length. If so, wrap the
step index back to the loop start point.

The loop wrap replaces the normal pattern-length wrap for looped tracks. The
implementation uses the master-clock-based loop length (from
`seq_loopLengthTicks()`) converted to the track's step domain via its DDA
interval.

```
/*
 * S080 C3: loop boundary wrap in seq_advanceTrackStep().
 *
 * What:       when seq_loopActive is set and the current track's
 *             bit is set in seq_loopTrackMask, the normal pattern-
 *             length wrap is replaced by a loop-length wrap.  The
 *             track's step index wraps back to seq_loopStartStep[track]
 *             and the DDA accumulator wraps to seq_loopStartAccum[track]
 *             plus any remainder.
 * Why:        this is the core loop mechanism.  The ISR must perform
 *             the wrap so that timing is sample-accurate.  The
 *             foreground cannot wrap fast enough for tight divisions
 *             (1/64, 1/128).
 * Inputs:     seq_loopActive, seq_loopTrackMask, seq_loopDivision,
 *             seq_loopModifier, seq_loopStartStep[track],
 *             seq_loopStartAccum[track], seq_stepIndex[track],
 *             seq_trackAccumulator[track].
 * Outputs:    seq_stepIndex[track] and seq_trackAccumulator[track]
 *             wrapped to the loop region.
 * Accessors:  seq_advanceTrackStep() (line 1688), called from
 *             seq_processSchedulerTick() DDA loop (line 2668).
 * Affiliates: seq_loopLengthTicks() (C2), seq_loopEngage() (C2),
 *             stepScale_ticksQ8() (StepScale.c:82).
 */
```

---

### C4. buttonHandler.c — SELECT button looper engage in PERF

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1374–1390 (handleSelectButton, SELECT_MODE_PERF case)
**Action:** MODIFY (replace current near-no-op with looper logic)

Replace the existing SELECT1-only realignment gesture with the looper interface.

On SELECT press in PERF:
1. Record the pressed SELECT button number (0–7) as the division.
2. Determine the track mask: if `perf_heldVoiceMask != 0`, use it; otherwise
   use 0x7F (all 7 tracks).
3. If no loop is active, call `seq_loopEngage(trackMask, division, 0)`.
4. If a loop is already active, call `seq_loopUpdateDivision(division, modifier)`.
5. Light the pressed SELECT LED; clear all other SELECT LEDs.
6. Store the pressed button as `bh_loopLastSelectButton` for last-pressed logic.

```
/*
 * S080 C4: SELECT button looper engage in PERF mode.
 *
 * What:       pressing a SELECT button (1–8) in PERF mode engages
 *             the looper at the corresponding musical division.
 *             SELECT1 = 1 bar, SELECT2 = 1/2, ..., SELECT8 = 1/128.
 *             When multiple SELECT buttons are held, the last pressed
 *             determines the active division.
 * Why:        SELECT buttons are unused in PERF mode (only SELECT1
 *             did a realignment gesture).  Repurposing them as a
 *             performance looper is the primary Phase C feature.
 * Inputs:     selectNr (0–7), perf_heldVoiceMask (track subset),
 *             seq_loopActive (current loop state).
 * Outputs:    seq_loopEngage() or seq_loopUpdateDivision() called.
 *             SELECT LEDs updated to show active division.
 *             If recording was active, it is disabled.
 * Accessors:  handleSelectButton() SELECT_MODE_PERF case (here).
 * Affiliates: seq_loopEngage() (C2), seq_loopDisengage() (C2),
 *             buttonHandler_perfVoiceReleased() (line 1000).
 */
```

---

### C5. buttonHandler.c — New looper state variables

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after line 265 (after dblclick state vars)
**Action:** ADD

```c
static uint8_t bh_loopSelectHeldMask   = 0u;
static uint8_t bh_loopLastSelectButton = 0u;
static uint8_t bh_loopTrackMask        = 0u;
static uint8_t bh_loopBarModifier      = 0u;
```

```
/*
 * S080 C5: foreground looper state in buttonHandler.
 *
 * What:       bh_loopSelectHeldMask tracks which SELECT buttons are
 *             currently held (8-bit, one per SELECT).
 *             bh_loopLastSelectButton is the most recently pressed
 *             SELECT button (determines active division).
 *             bh_loopTrackMask mirrors the track mask sent to the
 *             sequencer so the foreground can manage per-track
 *             transitions.
 *             bh_loopBarModifier tracks dot/triplet state from BAR
 *             button holds during looping.
 * Why:        the foreground needs to track button hold state to
 *             implement last-pressed-wins division selection and
 *             to manage loop engage/disengage transitions.
 * Inputs:     SELECT press/release events, BAR press/release events.
 * Outputs:    fed into seq_loopEngage()/seq_loopUpdateDivision()/
 *             seq_loopDisengage().
 * Accessors:  handleSelectButton() (C4), processRelease() SELECT
 *             handler (C6).
 * Affiliates: perf_heldVoiceMask (line 233), bh_fillArmed (line 714).
 */
```

---

### C6. buttonHandler.c — SELECT release: looper disengage

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2013–2018 (processRelease SELECT routing)
**Action:** MODIFY

Add PERF-mode SELECT release logic. When a SELECT button is released:
1. Clear its bit in `bh_loopSelectHeldMask`.
2. If other SELECT buttons are still held, update the active division to the
   last-pressed remaining button.
3. If no SELECT buttons remain held (`bh_loopSelectHeldMask == 0`), call
   `seq_loopDisengage()`, clear all looper state, restore BAR overlay LEDs.

```
/*
 * S080 C6: SELECT release looper disengage.
 *
 * What:       on SELECT release in PERF mode, the looper state is
 *             updated.  If this was the last held SELECT button,
 *             seq_loopDisengage() is called and all tracks resume
 *             at their master-clock positions.
 * Why:        the looper is a momentary performance gesture — tracks
 *             loop only while at least one SELECT button is held.
 *             Release-to-disengage ensures clean resumption.
 * Inputs:     released SELECT button number, bh_loopSelectHeldMask.
 * Outputs:    seq_loopDisengage() on last release.  SELECT LEDs
 *             cleared.  BAR overlay LEDs restored.
 * Accessors:  processRelease() SELECT routing (here).
 * Affiliates: seq_loopDisengage() (C2), BAR LED restore (E6).
 */
```

---

### C7. buttonHandler.c — Disable recording while looper engaged

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1781–1789 (BUT_REC without shift, recording toggle)
**Action:** MODIFY

Add a guard: if `seq_loopActive`, skip the recording toggle and return. The REC
button is consumed without side effects while the looper is engaged.

```
/*
 * S080 C7: block recording toggle while looper is engaged.
 *
 * What:       the REC button press (without shift) is ignored when
 *             seq_loopActive is nonzero.  The REC LED is not toggled.
 * Why:        looping and recording are mutually exclusive (plan §3,
 *             resolved ambiguity #2).  Engaging the looper already
 *             disables recording (C2); this guard prevents the user
 *             from re-enabling recording while looping.
 * Inputs:     seq_loopActive.
 * Outputs:    early return (no state change).
 * Accessors:  processPress() BUT_REC case (here).
 * Affiliates: seq_loopEngage() disables recording (C2),
 *             seq_setRecordingMode() (sequencer.c:3124).
 */
```

---

### C8. buttonHandler.c — Overflow reconciliation update

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2174–2175 (buttonHandler_processEvents overflow clear)
**Action:** ADD (after existing perf_heldVoiceMask clear)

```c
bh_loopSelectHeldMask = 0u;
bh_loopBarModifier = 0u;
if (seq_loopActive)
    seq_loopDisengage();
```

```
/*
 * S080 C8: clear looper state on event overflow.
 *
 * What:       the event ring overflow handler clears all held-button
 *             state.  The looper must also be disengaged to prevent
 *             stranded loops where a SELECT release event was lost.
 * Why:        event ring overflow can lose release events, leaving
 *             the looper permanently engaged with no held buttons.
 * Inputs:     event overflow detected.
 * Outputs:    bh_loopSelectHeldMask cleared, seq_loopDisengage()
 *             called if loop was active.
 * Accessors:  buttonHandler_processEvents() overflow block.
 * Affiliates: perf_heldVoiceMask overflow clear (line 2174).
 */
```

---

### C9. sequencer.c — Clear looper in seq_setStepIndexToStart()

**File:** `Core/Sequencer/sequencer.c`
**Line:** 3277 (inside seq_setStepIndexToStart(), after
`seq_clearSceneAutomationDirty()`)
**Action:** ADD

```c
seq_loopActive = 0u;
```

```
/*
 * S080 C9: clear looper on full sequencer reset.
 *
 * What:       seq_setStepIndexToStart() is called on transport start
 *             and full pattern reset.  The loop flag must be cleared
 *             to prevent stale loop state from a prior run.
 * Why:        transport start begins fresh playback; any prior loop
 *             configuration is invalid.
 * Inputs:     seq_setStepIndexToStart() call.
 * Outputs:    seq_loopActive = 0.
 * Accessors:  seq_setRunning(1) → seq_setStepIndexToStart().
 * Affiliates: seq_loopActive clear in seq_setRunning() (B5).
 */
```

---

## Phase D — Per-Track Looping + Scene Changes During Loop

### D1. buttonHandler.c — VOICE+SELECT per-track loop subsetting

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1502–1510 (handleVoiceButton PERF case)
**Action:** MODIFY

When a VOICE button is pressed during an active loop (`seq_loopActive &&
bh_loopSelectHeldMask != 0`), the looper track mask is updated:
1. Release all currently looping tracks from the loop (realign to master clock).
2. Set the new track mask to the currently held VOICE buttons.
3. Re-engage with the new mask at a fresh loop start point.

When VOICE is pressed with no active loop, existing behavior is preserved
(set `perf_heldVoiceMask` for held-voice scene changes).

```
/*
 * S080 D1: per-track loop subsetting via VOICE+SELECT.
 *
 * What:       pressing a VOICE button while the looper is active
 *             swaps which tracks are looping.  Previously looping
 *             tracks resume at their master-clock position; the
 *             newly pressed tracks begin looping at a new start
 *             point.  If VOICE is pressed before SELECT, the
 *             perf_heldVoiceMask is used as the loop track mask
 *             when SELECT is subsequently pressed.
 * Why:        allows real-time track-swapping during loop
 *             performance.  The plan specifies that pressing VOICE
 *             while SELECT is held replaces the loop set (§3.5).
 * Inputs:     voiceNr, bh_loopSelectHeldMask, seq_loopActive.
 * Outputs:    seq_loopDisengage() + seq_loopEngage() with new mask.
 * Accessors:  handleVoiceButton() PERF case (here).
 * Affiliates: seq_loopEngage() (C2), perf_heldVoiceMask (line 233).
 */
```

---

### D2. menu.c — Scene change during active loop: reset loop start

**File:** `Core/Menu/menu.c`
**Lines:** 8138–8168 (menu_perfModeSceneButtonPressed)
**Action:** MODIFY

After the scene change commits (after `seq_selectActivePattern()` at line 8164),
if `seq_loopActive`, re-snapshot the loop start points for all looping tracks.
This resets the loop region to the beginning of the new scene.

```
/*
 * S080 D2: reset loop start on scene change.
 *
 * What:       when a scene change occurs while the looper is active,
 *             the loop start points are re-captured from the new
 *             scene's step index positions (which seq_selectActivePattern
 *             has just reset).
 * Why:        the plan specifies that on scene change during loop,
 *             the loop start point resets to the beginning of the
 *             new scene (§3.6).
 * Inputs:     seq_loopActive, seq_loopTrackMask.
 * Outputs:    seq_loopStartStep[] and seq_loopStartAccum[] updated.
 * Accessors:  menu_perfModeSceneButtonPressed() (here).
 * Affiliates: seq_selectActivePattern() (sequencer.c:1122),
 *             seq_loopEngage() snapshot logic (C2).
 */
```

---

### D3. buttonHandler.c — Per-track scene change during loop

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 888–912 (buttonHandler_perfVoiceHeldSeqPressed)
**Action:** MODIFY

After per-track scene change (`seq_setTrackPlayedScene()` and
`seq_realignTrackToMasterClock()`), if the changed track is in the loop mask,
re-snapshot its loop start point.

```
/*
 * S080 D3: per-track scene change updates loop start for that track.
 *
 * What:       when a per-track scene change fires for a track that
 *             is currently looping, that track's loop start point
 *             is re-captured after the realignment.
 * Why:        per-track scene changes realign the track via
 *             seq_realignTrackToMasterClock(), which changes the
 *             DDA state.  The loop start must match the new state
 *             so the loop wraps correctly within the new scene.
 * Inputs:     track index, seq_loopTrackMask, seq_loopActive.
 * Outputs:    seq_loopStartStep[track] and seq_loopStartAccum[track]
 *             updated.
 * Accessors:  buttonHandler_perfVoiceHeldSeqPressed() (here).
 * Affiliates: seq_realignTrackToMasterClock() (sequencer.h:110),
 *             seq_loopStartStep/Accum snapshots (C1).
 */
```

---

## Phase E — BAR Button Overlay + SHIFT+REC Menu Rework

### E1. ParameterArray.h — New PAR_ enums for overlay settings

**File:** `Core/Bank/Scene/Preset/ParameterArray.h`
**Line:** after line 211 (after PAR_BUS_COMP_SIDECHAIN, before NUM_PARAMS)
**Action:** ADD

```c
PAR_PERF_BTN_LT,
PAR_PERF_NOTE_LT,
PAR_PERF_BTN_RT,
PAR_PERF_NOTE_RT,
```

```
/*
 * S080 E1: BAR overlay setting parameter IDs.
 *
 * What:       four new global parameters for the PERF BAR button
 *             overlay settings, stored in settings.cfg.
 *             PAR_PERF_BTN_LT/RT select the trigger type (vmx, v10,
 *             v64, v32, flm, fls, r/8, r16, r32, r64, r6t, 128).
 *             PAR_PERF_NOTE_LT/RT select the note (def or 0–127).
 * Why:        the overlay settings are user-configurable global
 *             parameters that persist across power cycles.  They
 *             require PAR_ IDs to participate in the parameter_values
 *             array and the settings.cfg write/read path.
 * Inputs:     compile-time enum assignment.
 * Outputs:    parameter_values[PAR_PERF_BTN_LT] etc.
 * Accessors:  menu.c SHIFT+REC page display, buttonHandler.c overlay
 *             trigger dispatch.
 * Affiliates: settings.cfg writer (E10), settings.cfg parser (E11),
 *             menuPages.h RECORDING_PAGE (E3).
 */
```

---

### E2. menu.h — New TEXT_ enums for overlay labels

**File:** `Core/Menu/menu.h`
**Line:** after line 154 (after TEXT_ROLL_FILL, before NUM_NAMES)
**Action:** ADD

```c
TEXT_PERF_BTN_LT,
TEXT_PERF_NOTE_LT,
TEXT_PERF_BTN_RT,
TEXT_PERF_NOTE_RT,
```

```
/*
 * S080 E2: menu text IDs for BAR overlay settings.
 *
 * What:       four new NamesEnum values for the SHIFT+REC menu page.
 *             Each maps to a {short, category, long} triplet in the
 *             valueNames table.
 * Why:        the static menuPages table references TEXT_ enums to
 *             look up display names.
 * Inputs:     compile-time enum assignment.
 * Outputs:    valueNames[TEXT_PERF_BTN_LT] etc.
 * Accessors:  menuPages.h RECORDING_PAGE row (E3),
 *             menu_repaintGeneric() (menu.c:11501).
 * Affiliates: shortNames/catNames/longNames (E4), valueNames (E5).
 */
```

---

### E3. menuPages.h — Repurpose RECORDING_PAGE

**File:** `Core/Menu/menuPages.h`
**Lines:** 126–137
**Action:** MODIFY

Replace the current single-cell RECORDING_PAGE with the new 2-subpage layout.

```
/* RECORDING_PAGE — repurposed as SHIFT+REC performance settings (S080). */
/* Subpage 0: BAR overlay trigger types and notes. */
{TEXT_PERF_BTN_LT,TEXT_PERF_NOTE_LT,TEXT_PERF_BTN_RT,TEXT_PERF_NOTE_RT,
 TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 PAR_PERF_BTN_LT,PAR_PERF_NOTE_LT,PAR_PERF_BTN_RT,PAR_PERF_NOTE_RT,
 PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
/* Subpage 1: Recording quantisation. */
{TEXT_QUANTISATION,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 PAR_QUANTISATION,PAR_NONE,PAR_NONE,PAR_NONE,
 PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
/* Subpages 2–7: reserved. */
```

```
/*
 * S080 E3: repurpose RECORDING_PAGE for SHIFT+REC menu.
 *
 * What:       subpage 0 exposes BAR overlay settings (pb<, pn<, pb>,
 *             pn>) under category "Prfrmnc".  Subpage 1 retains the
 *             existing quantisation parameter under category "Record".
 *             Subpages 2–7 are reserved for future live-record options.
 * Why:        the plan consolidates all performance settings accessible
 *             via SHIFT+REC into one page.  Quantisation moves from
 *             subpage 0 to subpage 1 so the overlay settings are
 *             immediately visible on entry.
 * Inputs:     static compile-time table.
 * Outputs:    menu_repaintGeneric() reads these cells for the
 *             RECORDING_PAGE.
 * Accessors:  menu_switchPage(RECORDING_PAGE), encoder traversal.
 * Affiliates: PAR_PERF_BTN_LT/RT (E1), TEXT_PERF_BTN_LT/RT (E2),
 *             SHIFT+REC entry handler (E7).
 */
```

---

### E4. MenuText.h — Add short/long names for overlay settings

**File:** `Core/Menu/MenuText.h`
**Lines:** 168 (end of shortNames), 187 (end of catNames), 217 (end of longNames)
**Action:** ADD (append entries to each array)

shortNames: `{"pb<"},{"pn<"},{"pb>"},{"pn>"}`
catNames: `{"Prfrmnc"}` (new category)
longNames: `{"BtnStat<"},{"BtnNote<"},{"BtnStat>"},{"BtnNote>"}`

Also add the overlay trigger type name table and note option name table as new
static const arrays.

```
/*
 * S080 E4: BAR overlay menu text.
 *
 * What:       short/cat/long name entries for the four overlay
 *             parameters, plus two new name tables for the trigger
 *             type selector (12 entries: vmx, v10, v64, v32, flm,
 *             fls, r/8, r16, r32, r64, r6t, 128) and the note
 *             selector (129 entries: def + 0..127).
 * Why:        the static-param special-case renderer needs display
 *             strings for the overlay parameter values.  The trigger
 *             type and note tables are not DTYPE_MENU tables (all 16
 *             MENU IDs are used) — they are rendered via the static-
 *             param path.
 * Inputs:     compile-time const arrays.
 * Outputs:    consumed by valueNames[] (E5) and the static-param
 *             renderer in menu.c (E6).
 * Accessors:  menu_repaintGeneric(), menu_encoderChangeParameter().
 * Affiliates: MENU_* defines (lines 25–40 — NOT modified; all 16
 *             are occupied), static-param precedent from S078 run
 *             mode (trackPlayModeNames, line 121).
 */
```

Also add a category entry to catNames:

**File:** `Core/Menu/MenuText.h`
**Line:** 187 (end of catNames, after `{"Track"}`)
**Action:** ADD

```c
{"Prfrmnc"},
```

And the corresponding enum in menu.h:

**File:** `Core/Menu/menu.h`
**Line:** after line 169 (after CAT_TRACK)
**Action:** ADD

```c
CAT_PERFORMANCE,
```

```
/*
 * S080 E4 (affiliate): CAT_PERFORMANCE category.
 *
 * What:       category label "Prfrmnc" for BAR overlay parameters on
 *             the SHIFT+REC menu page.
 * Why:        distinguishes overlay settings from other categories.
 * Inputs:     compile-time enum and parallel catNames[] array.
 * Outputs:    displayed in the category line of the full-view parameter
 *             editor.
 * Accessors:  valueNames[] entries for TEXT_PERF_BTN_LT/RT etc.
 * Affiliates: catNames[] (MenuText.h), valueNames[] (menu.c).
 */
```

---

### E5. menu.c — valueNames entries for overlay parameters

**File:** `Core/Menu/menu.c`
**Line:** 1312 (after the last valueNames entry, before the closing `}`)
**Action:** ADD

```c
{SHORT_PERF_BTN_LT,  CAT_PERFORMANCE, LONG_PERF_BTN_LT},
{SHORT_PERF_NOTE_LT, CAT_PERFORMANCE, LONG_PERF_NOTE_LT},
{SHORT_PERF_BTN_RT,  CAT_PERFORMANCE, LONG_PERF_BTN_RT},
{SHORT_PERF_NOTE_RT, CAT_PERFORMANCE, LONG_PERF_NOTE_RT},
```

```
/*
 * S080 E5: valueNames entries for overlay settings.
 *
 * What:       maps TEXT_PERF_BTN_LT..TEXT_PERF_NOTE_RT to their
 *             {short, category, long} display tuples.
 * Why:        menu_repaintGeneric() uses valueNames[] to render
 *             cell labels.
 * Inputs:     TEXT_ enum indices (E2).
 * Outputs:    display strings for SHIFT+REC menu cells.
 * Accessors:  menu_repaintGeneric() compact view (line 11751).
 * Affiliates: shortNames/catNames/longNames (E4).
 */
```

---

### E6. menu.c — Static-param special case for overlay parameters

**File:** `Core/Menu/menu.c`
**Lines:** in menu_parseGlobalParam() (after existing cases, before default)
**Action:** ADD

Add case blocks for `PAR_PERF_BTN_LT`, `PAR_PERF_NOTE_LT`, `PAR_PERF_BTN_RT`,
`PAR_PERF_NOTE_RT` that clamp values to their respective ranges and mark
settings dirty.

For the dtype table:
**File:** `Core/Menu/menu.c`
**Line:** after line 1110 (dtype table, after PAR_EFFECT_MORPH entry)
**Action:** ADD

```c
[PAR_PERF_BTN_LT]  = DTYPE_0B127,
[PAR_PERF_NOTE_LT] = DTYPE_0B127,
[PAR_PERF_BTN_RT]  = DTYPE_0B127,
[PAR_PERF_NOTE_RT] = DTYPE_0B127,
```

These use DTYPE_0B127 as the storage type, but the **display** is handled via
static-param special-case rendering (same pattern as PAR_TRACK_PLAY_MODE from
S078).

```
/*
 * S080 E6: static-param rendering and commit for overlay settings.
 *
 * What:       the four overlay parameters use a static-param special
 *             case for display formatting (looking up names from the
 *             overlay trigger type and note name tables), clamping
 *             values to their domain, and marking settings dirty on
 *             commit.
 * Why:        all 16 DTYPE_MENU IDs are occupied.  The static-param
 *             special case (precedent: PAR_TRACK_PLAY_MODE in S078)
 *             allows custom name-table rendering without consuming
 *             a DTYPE_MENU slot.
 * Inputs:     parameter_values[PAR_PERF_BTN_LT] etc., encoder delta.
 * Outputs:    clamped parameter value, filesystem_markSettingsDirty().
 * Accessors:  menu_parseGlobalParam() (line 15136),
 *             menu_encoderChangeParameter() (line 11829).
 * Affiliates: perfBtnTypeNames[] (E4), DTYPE_MENU saturation note
 *             (MenuText.h:25–40).
 */
```

---

### E7. buttonHandler.c — SHIFT+REC entry/exit protocol

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1778–1780 (BUT_REC with shift: `menu_switchPage(RECORDING_PAGE)`)
**Action:** MODIFY

Replace the simple page switch with the new entry protocol:
1. Save current page, subpage, and parameter cursor.
2. Switch to RECORDING_PAGE.
3. Switch to subpage 0.
4. Reset parameter cursor.
5. Set `bh_shiftRecMenuActive = 1u`.
6. Light both SHIFT and REC LEDs.

Also add SHIFT+REC menu state variable:

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after the looper state vars (C5)
**Action:** ADD

```c
static uint8_t bh_shiftRecMenuActive  = 0u;
static uint8_t bh_shiftRecSavedPage   = 0u;
static uint8_t bh_shiftRecSavedSubPg  = 0u;
```

Exit protocol (in SHIFT release handler at lines 2070–2127 and in BUT_REC
press handler at lines 1781–1789):

If `bh_shiftRecMenuActive`:
1. Restore saved page, subpage, parameter cursor.
2. Clear `bh_shiftRecMenuActive`.
3. Restore SHIFT and REC LEDs to normal state.
4. Suppress normal SHIFT-release or REC-toggle behavior.

```
/*
 * S080 E7: SHIFT+REC menu entry/exit protocol.
 *
 * What:       SHIFT+REC opens the RECORDING_PAGE (overlay settings +
 *             quantisation).  Both SHIFT and REC LEDs illuminate.
 *             Exit via either SHIFT release or REC press restores
 *             the prior page/subpage/cursor and returns LEDs to
 *             normal.
 * Why:        the SHIFT+REC menu is a transient overlay that must
 *             not interfere with normal SHIFT behavior (morph show,
 *             pattern settings page) or normal REC behavior
 *             (recording toggle).  The entry/exit protocol saves
 *             and restores state so the user returns exactly where
 *             they were.
 * Inputs:     SHIFT held + REC pressed (entry), SHIFT released or
 *             REC pressed again (exit).
 * Outputs:    page/subpage switch, LED state, bh_shiftRecMenuActive
 *             flag.
 * Accessors:  processPress() BUT_REC case (entry),
 *             processRelease() BUT_SHIFT case (exit),
 *             processPress() BUT_REC case (exit if menu active).
 * Affiliates: menu_switchPage() (menu.c:14682),
 *             menu_switchSubPage() (menu.c:14539),
 *             bh_shiftRecSavedPage/SubPg (state vars).
 */
```

---

### E8. buttonHandler.c — BAR button overlay toggle in PERF

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1819–1840 (BUT_BAR1 press), 1842–1860 (BUT_BAR2 press)
**Action:** MODIFY

Add a PERF-mode intercept before the normal bar navigation. When
`bh_state.selectButtonMode == SELECT_MODE_PERF`:
1. If the looper is active (`seq_loopActive`), handle as dot/triplet modifier
   (see E9) instead of overlay toggle.
2. Otherwise, toggle the BAR overlay:
   - BAR1: if overlay `<` is active, deactivate. If `>` is active, deactivate
     `>` and activate `<`. If neither, activate `<`.
   - BAR2: same logic for `>`.
3. Update BAR LEDs: active overlay's LED on, other off.

New state variable:

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after bh_shiftRecMenuActive
**Action:** ADD

```c
static uint8_t bh_overlayActive = 0u;  /* 0 = none, 1 = '<', 2 = '>' */
```

```
/*
 * S080 E8: BAR button overlay toggle in PERF mode.
 *
 * What:       in PERF mode (not during looping), BAR1 and BAR2
 *             become toggle switches for the trigger overlay.  Only
 *             one overlay can be active at a time.  The active
 *             overlay's BAR LED is illuminated; the other is dark.
 * Why:        the overlay changes what VOICE buttons do in PERF
 *             mode — from mute toggle to trigger dispatch.  BAR
 *             buttons are otherwise unused in PERF (no bar
 *             navigation applies).
 * Inputs:     BUT_BAR1/BUT_BAR2 press event,
 *             bh_state.selectButtonMode == SELECT_MODE_PERF.
 * Outputs:    bh_overlayActive set to 0/1/2.  BAR LEDs updated.
 * Accessors:  processPress() BUT_BAR1/BUT_BAR2 cases (here).
 * Affiliates: VOICE trigger dispatch (E12), BAR LED suppression
 *             during looping (E9), bh_overlayActive state.
 */
```

---

### E9. buttonHandler.c — BAR as dot/triplet modifier during loop

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1819–1840 (BUT_BAR1), 1842–1860 (BUT_BAR2)
**Action:** MODIFY (within the PERF intercept added in E8)

When `seq_loopActive && bh_loopSelectHeldMask != 0`:
- BAR1 press: set `bh_loopBarModifier = 2u` (triplet), call
  `seq_loopUpdateDivision()`, light BAR1 LED.
- BAR2 press: set `bh_loopBarModifier = 1u` (dot), call
  `seq_loopUpdateDivision()`, light BAR2 LED.
- BAR release: clear `bh_loopBarModifier = 0u`, call
  `seq_loopUpdateDivision()`, turn off BAR LED.

Both BAR LEDs are forced off when the looper is active but no BAR is held.

```
/*
 * S080 E9: BAR as dot/triplet modifier during looping.
 *
 * What:       while SELECT buttons are held (looper active), BAR
 *             buttons become momentary modifiers: BAR2 = dot (+50%),
 *             BAR1 = triplet (×2/3).  The modifier takes effect
 *             while held and reverts on release.  If both are held,
 *             last pressed wins.
 * Why:        the looper's musical divisions can be modified in
 *             real time.  BAR overlay state is preserved but LEDs
 *             are suppressed while looping.
 * Inputs:     BAR press/release, bh_loopSelectHeldMask, seq_loopActive.
 * Outputs:    bh_loopBarModifier, seq_loopUpdateDivision(),
 *             BAR LED state.
 * Accessors:  processPress() BAR cases (here), processRelease()
 *             BAR cases.
 * Affiliates: seq_loopUpdateDivision() (C2), bh_overlayActive (E8).
 */
```

---

### E10. filesystem.c — Settings writer: new overlay keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** 17431–17444 (settings writer cases 17u, the terminator)
**Action:** MODIFY

Insert 4 new cases before the terminator. Shift terminator to case 21u with
`lines=21`.

```c
case 17u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "perf_btn_lt", parameter_values[PAR_PERF_BTN_LT]);
case 18u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "perf_note_lt", parameter_values[PAR_PERF_NOTE_LT]);
case 19u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "perf_btn_rt", parameter_values[PAR_PERF_BTN_RT]);
case 20u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "perf_note_rt", parameter_values[PAR_PERF_NOTE_RT]);
case 21u:
    return filesystem_formatAssignmentU16Line(dst, cap, "lines", 21u);
```

```
/*
 * S080 E10: settings.cfg writer — overlay keys.
 *
 * What:       four new settings lines for BAR overlay configuration.
 *             Lines 17–20 write perf_btn_lt, perf_note_lt,
 *             perf_btn_rt, perf_note_rt.  The terminator moves from
 *             case 17u to case 21u with lines=21.
 * Why:        these settings persist across power cycles.  The
 *             settings.cfg writer is an explicit case-per-line
 *             allowlist — new keys require new cases and an
 *             incremented terminator count.
 * Inputs:     parameter_values[PAR_PERF_BTN_LT..PAR_PERF_NOTE_RT].
 * Outputs:    four new keyed lines in settings.cfg, updated lines=
 *             count.
 * Accessors:  filesystem_nextSettingsLine() (line 17355).
 * Affiliates: settings parser (E11), filesystem_markSettingsDirty()
 *             (line 23978).
 */
```

---

### E11. filesystem.c — Settings parser: new overlay keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** 2770–2771 (before the `else return 0u` in
`filesystem_settingsParamForKey()`)
**Action:** ADD

```c
else if (strcmp(key, "perf_btn_lt") == 0)  *param = PAR_PERF_BTN_LT;
else if (strcmp(key, "perf_note_lt") == 0) *param = PAR_PERF_NOTE_LT;
else if (strcmp(key, "perf_btn_rt") == 0)  *param = PAR_PERF_BTN_RT;
else if (strcmp(key, "perf_note_rt") == 0) *param = PAR_PERF_NOTE_RT;
```

Also add default values:

**File:** `Core/Hardware/SD/filesystem.c`
**Line:** after line 2697 (after PAR_FOLLOW default, in defaults block)
**Action:** ADD

```c
parameter_values[PAR_PERF_BTN_LT]  = 0u;  /* vmx */
parameter_values[PAR_PERF_NOTE_LT] = 0u;  /* def */
parameter_values[PAR_PERF_BTN_RT]  = 0u;  /* vmx */
parameter_values[PAR_PERF_NOTE_RT] = 0u;  /* def */
```

```
/*
 * S080 E11: settings.cfg parser — overlay keys and defaults.
 *
 * What:       maps the four new settings keys to their PAR_ enum
 *             values.  Boot defaults set all four to 0 (vmx trigger
 *             type, default note).
 * Why:        a settings.cfg written by older firmware will not
 *             contain these keys.  The parser returns 0 (unknown key)
 *             and the boot defaults provide safe initial values.
 * Inputs:     settings.cfg key string.
 * Outputs:    *param set to the matching PAR_ enum value.
 * Accessors:  filesystem_settingsParamForKey() (line 2746).
 * Affiliates: settings writer (E10), PAR_PERF_BTN_LT (E1).
 */
```

---

### E12. buttonHandler.c — VOICE trigger dispatch with overlay active

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1502–1510 (handleVoiceButton PERF case)
**Action:** MODIFY (additional intercept before existing perf_heldVoiceMask logic)

When `bh_overlayActive != 0` and `!buttonHandler_getShift()`:
1. Look up the trigger type and note from the active overlay's parameters.
2. Call `seq_triggerVoice()` or `seq_previewVoice()` with the configured velocity
   and note.
3. For roll types, start a DDA-based repeat timer.
4. On release, send note-off and stop any roll timer.

When `bh_overlayActive != 0` and `buttonHandler_getShift()`:
- Fall through to normal PERF behavior (mute toggle on release).

```
/*
 * S080 E12: VOICE trigger dispatch with BAR overlay active.
 *
 * What:       when an overlay is active and SHIFT is not held, VOICE
 *             button down-press fires a trigger based on the overlay's
 *             configured type and note.  Release sends note-off.
 *             For roll types (r/8..128), a DDA repeat timer fires
 *             at the configured rate while held.  SHIFT+VOICE reverts
 *             to normal PERF behavior (mute toggle).
 * Why:        the overlay converts VOICE buttons from mute toggles to
 *             trigger buttons for performance fills, flams, and rolls.
 * Inputs:     bh_overlayActive, parameter_values[PAR_PERF_BTN_LT/RT],
 *             parameter_values[PAR_PERF_NOTE_LT/RT], voiceNr.
 * Outputs:    seq_triggerVoice() / seq_previewVoice() called.
 *             For rolls: DDA repeat timer started/stopped.
 * Accessors:  handleVoiceButton() PERF case (here).
 * Affiliates: seq_triggerVoice() (sequencer.h:126),
 *             seq_previewVoice() (sequencer.h:136),
 *             stepScale_ticksQ8() (StepScale.c:82) for roll timing.
 */
```

---

## Phase F — Scene Queue / Change Division Globals

### F1. ParameterArray.h — New PAR_ enums for scene globals

**File:** `Core/Bank/Scene/Preset/ParameterArray.h`
**Line:** after PAR_PERF_NOTE_RT (added in E1)
**Action:** ADD

```c
PAR_SCENE_CHANGE_DIV,
PAR_SCENE_QUEUE_DIV,
PAR_SCENE_QUEUE_ACTION,
PAR_SCENE_PC_RESET,
```

```
/*
 * S080 F1: scene queue/change division global parameter IDs.
 *
 * What:       four global parameters controlling scene change timing.
 *             PAR_SCENE_CHANGE_DIV (scd): musical division at which
 *             direct scene changes take effect.
 *             PAR_SCENE_QUEUE_DIV (qcd): division for queued changes.
 *             PAR_SCENE_QUEUE_ACTION (qac): action on queue fire
 *             (normal, reset mutes, reset morphs, both, reload).
 *             PAR_SCENE_PC_RESET (pcr): boolean, whether scene
 *             changes reset tracks to beginning.
 * Why:        these controls add musical timing to scene changes,
 *             replacing the current instant-switch-only behavior.
 * Inputs:     compile-time enum assignment.
 * Outputs:    parameter_values[PAR_SCENE_CHANGE_DIV] etc.
 * Accessors:  sequencer ISR boundary detection, foreground scene
 *             change execution.
 * Affiliates: settings.cfg writer (F5), settings.cfg parser (F6),
 *             menuPages.h global page (F3).
 */
```

---

### F2. menu.h — New TEXT_ enums for scene globals

**File:** `Core/Menu/menu.h`
**Line:** after TEXT_PERF_NOTE_RT (added in E2)
**Action:** ADD

```c
TEXT_SCENE_CHANGE_DIV,
TEXT_SCENE_QUEUE_DIV,
TEXT_SCENE_QUEUE_ACTION,
TEXT_SCENE_PC_RESET,
```

```
/*
 * S080 F2: menu text IDs for scene queue/change division parameters.
 *
 * What:       four new NamesEnum values mapped to display name
 *             triplets in valueNames[].
 * Why:        the global settings page needs TEXT_ references.
 * Inputs:     compile-time enum assignment.
 * Outputs:    valueNames[TEXT_SCENE_CHANGE_DIV] etc.
 * Accessors:  menuPages.h global page (F3), menu_repaintGeneric().
 * Affiliates: shortNames/catNames/longNames (F4), valueNames (menu.c).
 */
```

---

### F3. menuPages.h — Add scene globals to MENU_MIDI_PAGE

**File:** `Core/Menu/menuPages.h`
**Lines:** 42–43 (MENU_MIDI_PAGE subpage 1, currently has PAR_FOLLOW at cell 3,
PAR_PAT_STORE_USE at cell 7)
**Action:** MODIFY

The four new scene globals are placed on a new subpage (subpage 3) of
MENU_MIDI_PAGE, keeping the existing subpages 0–2 unchanged. This avoids
disrupting the current global page layout.

**File:** `Core/Menu/menuPages.h`
**Line:** 50 (currently an empty subpage row for subpage 3)
**Action:** MODIFY (replace empty subpage with new content)

```
/* S080 subpage 3: Scene change/queue division globals + PCR. */
{TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,
 TEXT_SCENE_PC_RESET,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
 PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,
 PAR_SCENE_PC_RESET,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

```
/*
 * S080 F3: global settings page — scene change/queue parameters.
 *
 * What:       subpage 3 of MENU_MIDI_PAGE exposes scd, qcd, qac, pcr.
 *             The first three use static-param special-case rendering
 *             (same as BAR overlay settings).  pcr is DTYPE_ON_OFF.
 * Why:        keeping these on a dedicated subpage after the bus
 *             compressor page (subpage 2) avoids disrupting existing
 *             global page navigation.
 * Inputs:     static compile-time table.
 * Outputs:    menu_repaintGeneric() reads cells for display.
 * Accessors:  encoder traversal through global settings pages.
 * Affiliates: PAR_SCENE_CHANGE_DIV (F1), TEXT_SCENE_CHANGE_DIV (F2).
 */
```

---

### F4. MenuText.h — Add short/long names for scene globals

**File:** `Core/Menu/MenuText.h`
**Lines:** end of shortNames, catNames, longNames
**Action:** ADD (append)

shortNames: `{"scd"},{"qcd"},{"qac"},{"pcr"}`
longNames: `{"ChngDiv"},{"QueDiv"},{"QueActn"},{"PCReset"}`

Also add the scene change division name table and queue action name table as
new static const arrays:

```c
static const char sceneChangeDivNames[][4] = {
    {10},
    {"ins"},{"1/8"},{"1/4"},{"1/2"},{"1b"},
    {"2b"},{"3b"},{"4b"},{"ens"},{"enl"},
};

static const char sceneQueueActionNames[][4] = {
    {5},
    {"nrm"},{"mut"},{"mor"},{"m+m"},{"rld"},
};
```

```
/*
 * S080 F4: scene queue/change division menu text.
 *
 * What:       short/long name entries for scd, qcd, qac, pcr.
 *             sceneChangeDivNames (10 entries) is shared by both
 *             scd and qcd.  sceneQueueActionNames (5 entries) is
 *             for qac.  pcr uses the existing on/off renderer.
 * Why:        static-param special-case rendering needs display
 *             strings.  scd and qcd share the same division options.
 * Inputs:     compile-time const arrays.
 * Outputs:    consumed by the static-param renderer in menu.c.
 * Accessors:  menu_repaintGeneric(), menu_encoderChangeParameter().
 * Affiliates: sceneChangeDivNames also used by scene chain condition
 *             rendering (Phase G).
 */
```

---

### F5. filesystem.c — Settings writer: scene global keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** in filesystem_nextSettingsLine(), after overlay keys (E10)
**Action:** ADD (before terminator)

Insert 4 new cases. Terminator moves from case 21u to case 25u with
`lines=25`.

```c
case 21u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "scd", parameter_values[PAR_SCENE_CHANGE_DIV]);
case 22u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "qcd", parameter_values[PAR_SCENE_QUEUE_DIV]);
case 23u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "qac", parameter_values[PAR_SCENE_QUEUE_ACTION]);
case 24u:
    return filesystem_formatAssignmentU16Line(
        dst, cap, "pcr", parameter_values[PAR_SCENE_PC_RESET]);
case 25u:
    return filesystem_formatAssignmentU16Line(dst, cap, "lines", 25u);
```

```
/*
 * S080 F5: settings.cfg writer — scene global keys.
 *
 * What:       four new settings lines for scene change/queue division
 *             globals.  Total data lines: 25 (17 original + 4 overlay
 *             + 4 scene globals).  Terminator: lines=25.
 * Why:        these settings persist across power cycles.
 * Inputs:     parameter_values[PAR_SCENE_CHANGE_DIV..PAR_SCENE_PC_RESET].
 * Outputs:    four keyed lines in settings.cfg.
 * Accessors:  filesystem_nextSettingsLine().
 * Affiliates: settings parser (F6), overlay writer (E10).
 */
```

---

### F6. filesystem.c — Settings parser: scene global keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** 2770 (before `else return 0u`)
**Action:** ADD (after overlay key entries from E11)

```c
else if (strcmp(key, "scd") == 0) *param = PAR_SCENE_CHANGE_DIV;
else if (strcmp(key, "qcd") == 0) *param = PAR_SCENE_QUEUE_DIV;
else if (strcmp(key, "qac") == 0) *param = PAR_SCENE_QUEUE_ACTION;
else if (strcmp(key, "pcr") == 0) *param = PAR_SCENE_PC_RESET;
```

Also add defaults:

```c
parameter_values[PAR_SCENE_CHANGE_DIV]   = 0u;  /* ins */
parameter_values[PAR_SCENE_QUEUE_DIV]    = 0u;  /* ins */
parameter_values[PAR_SCENE_QUEUE_ACTION] = 0u;  /* nrm */
parameter_values[PAR_SCENE_PC_RESET]     = 0u;  /* off */
```

```
/*
 * S080 F6: settings.cfg parser — scene global keys and defaults.
 *
 * What:       maps scd/qcd/qac/pcr to PAR_ enums.  Defaults are
 *             instant division, normal action, PCR off.
 * Why:        older firmware settings.cfg files lack these keys;
 *             defaults preserve existing instant-switch behavior.
 * Inputs:     settings.cfg key string.
 * Outputs:    *param set to matching PAR_ enum.
 * Accessors:  filesystem_settingsParamForKey().
 * Affiliates: settings writer (F5), PAR_SCENE_CHANGE_DIV (F1).
 */
```

---

### F7. sequencer.c — Scene change division deferral

**File:** `Core/Sequencer/sequencer.c`
**Line:** new static state variables (after looper state defs from C2)
**Action:** ADD

```c
static uint8_t  seq_pendingSceneChange = 0u;
static uint8_t  seq_pendingSceneIndex  = 0u;
static uint8_t  seq_pendingSceneQueue  = 0u;
static uint8_t  seq_pendingQueueScene  = 0u;
static uint8_t  seq_previousScene      = 0u;
static uint16_t seq_sceneRepeatCount   = 0u;
static uint16_t seq_sceneBarCount      = 0u;
```

```
/*
 * S080 F7: scene change deferral state.
 *
 * What:       seq_pendingSceneChange is 1 when a direct scene change
 *             has been deferred to a musical boundary.
 *             seq_pendingSceneIndex is the target scene.
 *             seq_pendingSceneQueue and seq_pendingQueueScene are the
 *             queue equivalents (SHIFT+SEQ scene).
 *             seq_previousScene tracks the scene that was active
 *             before the last change (for `>NN` chain conditions).
 *             seq_sceneRepeatCount counts full-scene repetitions
 *             (longest track by scale×length completes).
 *             seq_sceneBarCount counts bars since the last scene
 *             change.
 * Why:        non-instant scene changes (scd != ins) require the
 *             ISR to detect the musical boundary and set a "fire
 *             now" flag.  The foreground executes the actual change.
 *             The repeat/bar counts support scene chain condition
 *             evaluation (Phase G).
 * Inputs:     ISR boundary detection, foreground scene change request.
 * Outputs:    seq_pendingSceneChange flag checked by foreground.
 * Accessors:  seq_handleMasterBoundary() (line 2283) for boundary
 *             detection.
 * Affiliates: menu_perfModeSceneButtonPressed() (menu.c:8138),
 *             Phase G chain evaluation.
 */
```

---

### F8. sequencer.c — Boundary detection for deferred scene changes

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 2283–2329 (seq_handleMasterBoundary)
**Action:** MODIFY

After the existing bar-boundary detection and pattern-switch logic, add:
1. Increment `seq_sceneBarCount` at each bar boundary.
2. Check for scene repetition completion (all tracks have wrapped at least once).
3. If a deferred direct scene change or queued change is pending, check whether
   the current boundary matches the configured division. If so, set a fire flag
   that the foreground checks on the next main-loop pass.

```
/*
 * S080 F8: boundary detection for deferred scene changes.
 *
 * What:       extends seq_handleMasterBoundary() to track bar count,
 *             detect scene repetition completion, and check pending
 *             deferred/queued scene changes against their configured
 *             divisions.
 * Why:        non-instant scene changes need the ISR to signal the
 *             correct musical boundary.  The ISR sets a "fire now"
 *             flag; the foreground executes the change (DSP apply,
 *             LED refresh).
 * Inputs:     seq_pendingSceneChange, seq_pendingSceneQueue,
 *             parameter_values[PAR_SCENE_CHANGE_DIV],
 *             parameter_values[PAR_SCENE_QUEUE_DIV],
 *             seq_barCounter (existing), seq_sceneBarCount.
 * Outputs:    fire flag set for foreground consumption.
 * Accessors:  seq_handleMasterBoundary() (line 2283).
 * Affiliates: foreground scene change executor (F9),
 *             seq_processSchedulerTick() (line 2604).
 */
```

---

### F9. menu.c / buttonHandler.c — Scene change with division gating

**File:** `Core/Menu/menu.c`
**Lines:** 8138–8168 (menu_perfModeSceneButtonPressed)
**Action:** MODIFY

When `parameter_values[PAR_SCENE_CHANGE_DIV] != 0` (not instant):
1. Instead of executing the scene change immediately, set
   `seq_pendingSceneChange = 1` and `seq_pendingSceneIndex = scene_index`.
2. The foreground main loop checks the fire flag set by the ISR (F8) and
   executes the change when signaled.
3. On change execution, if `PAR_SCENE_PC_RESET` is set, call
   `seq_setStepIndexToStart()` instead of `seq_realignActivePatternToMasterClock()`.

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 932–954 (buttonHandler_perfScenePressed)
**Action:** MODIFY

Same gating: if not instant, defer the scene change instead of calling
`menu_perfModeSceneButtonPressed()` directly.

**SHIFT+SEQ queue:**

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1329–1330 (SHIFT+SELECT in PERF — currently no-op)
**Action:** MODIFY

Repurpose SHIFT+SEQ in PERF to queue a scene change:
1. Set `seq_pendingSceneQueue = 1`, `seq_pendingQueueScene = seqButtonPressed`.
2. Start double-rate blink on the queued scene's SEQ LED.

```
/*
 * S080 F9: scene change with division gating.
 *
 * What:       when PAR_SCENE_CHANGE_DIV is not instant, direct scene
 *             changes are deferred: the foreground sets a pending
 *             flag and the ISR signals when the boundary is reached.
 *             SHIFT+SEQ queues a scene change at PAR_SCENE_QUEUE_DIV.
 *             Queue action (PAR_SCENE_QUEUE_ACTION) determines what
 *             resets occur on queue fire: mutes, morphs, both, or
 *             reload.
 * Why:        musical quantization of scene changes prevents audible
 *             jumps.  Queuing allows pre-programming the next scene
 *             during performance.
 * Inputs:     scene_index (from SEQ button press),
 *             PAR_SCENE_CHANGE_DIV, PAR_SCENE_QUEUE_DIV,
 *             PAR_SCENE_QUEUE_ACTION, PAR_SCENE_PC_RESET.
 * Outputs:    pending flags for ISR consumption (F8), queue LED blink.
 * Accessors:  buttonHandler_perfScenePressed() (line 932),
 *             menu_perfModeSceneButtonPressed() (menu.c:8138),
 *             SHIFT+SELECT PERF case (line 1329).
 * Affiliates: seq_handleMasterBoundary() (F8), scene change executor,
 *             led_setBlinkState() for queue indication.
 */
```

---

### F10. menu.c — Static-param rendering for scene globals

**File:** `Core/Menu/menu.c`
**Lines:** in menu_parseGlobalParam() and encoder/display paths
**Action:** ADD

Add static-param special cases for PAR_SCENE_CHANGE_DIV, PAR_SCENE_QUEUE_DIV
(both use sceneChangeDivNames), PAR_SCENE_QUEUE_ACTION (uses
sceneQueueActionNames), and mark settings dirty on commit. PAR_SCENE_PC_RESET
uses the existing DTYPE_ON_OFF path.

```
/*
 * S080 F10: static-param rendering for scene globals.
 *
 * What:       PAR_SCENE_CHANGE_DIV and PAR_SCENE_QUEUE_DIV display
 *             values from sceneChangeDivNames[] (10 entries).
 *             PAR_SCENE_QUEUE_ACTION displays from
 *             sceneQueueActionNames[] (5 entries).  All three mark
 *             settings dirty on commit.  PAR_SCENE_PC_RESET uses
 *             DTYPE_ON_OFF (on/off display, 0/1 value).
 * Why:        DTYPE_MENU space is full.  Static-param special cases
 *             avoid consuming a DTYPE_MENU slot.
 * Inputs:     parameter_values[], encoder delta.
 * Outputs:    clamped values, filesystem_markSettingsDirty().
 * Accessors:  menu_parseGlobalParam(), menu_encoderChangeParameter().
 * Affiliates: sceneChangeDivNames/sceneQueueActionNames (F4),
 *             PAR_SCENE_CHANGE_DIV (F1).
 */
```

---

## Phase G — Scene Chaining

### G1. SceneData.h — Add chain parameters to scene_settings_t

**File:** `Core/Bank/Scene/SceneData.h`
**Line:** 261 (before the closing `} scene_settings_t;` at line 262)
**Action:** ADD

```c
    uint8_t chain_condition1;
    uint8_t chain_next1;
    uint8_t chain_condition2;
    uint8_t chain_next2;
```

```
/*
 * S080 G1: per-scene chain parameters.
 *
 * What:       four bytes per scene controlling automatic scene
 *             transitions.  chain_condition1/2 select from 79
 *             condition values (0 = nul/never).  chain_next1/2
 *             select from 44 next-scene values (0 = slf/repeat).
 *             Total: 16 scenes × 4 bytes = 64 bytes.
 * Why:        scene chaining allows automatic, condition-based
 *             scene transitions during performance.  Conditions
 *             include repeat count, bar count, probability, bar
 *             position, from-scene, fill/loop state.
 * Inputs:     SHIFT+PLAY+SEQ chain menu, scene copy/clear.
 * Outputs:    ISR-evaluated at bar/repetition boundaries.
 * Accessors:  SceneData getter/setter (G2), sceneset writer (G5),
 *             sceneset parser (G6), chain evaluator (G7).
 * Affiliates: scene_commitSettings() (G3 — must include chain data),
 *             scene_settingsDefaults() (G4 — must default to 0).
 *             Chain data copies with scene but does NOT fan out
 *             through bank_sceneMaskVoiceEdit().
 */
```

---

### G2. SceneData.c — Chain getters/setters

**File:** `Core/Bank/Scene/SceneData.c`
**Line:** after scene_getTrackMorphShuffle() (line 1016)
**Action:** ADD

Four setter/getter pairs following the existing pattern:
- `scene_setChainCondition1(scene_index, value)`
- `scene_getChainCondition1(scene_index)`
- `scene_setChainNext1(scene_index, value)`
- `scene_getChainNext1(scene_index)`
- Same for condition2/next2.

Each setter uses `scene_storeParameterByte()` (the change-aware store helper)
and clamps to the valid range (0–78 for conditions, 0–43 for next-scene).

```
/*
 * S080 G2: scene chain parameter getters/setters.
 *
 * What:       named accessor functions for the four chain parameters
 *             per scene.  Setters clamp to valid ranges and use the
 *             shared change-aware store helper.  Getters return the
 *             clamped value or 0 for invalid indices.
 * Why:        the AutoSave extension rule requires named setters for
 *             every serialized byte.  Direct assignments are limited
 *             to initialization or validated whole-commit.
 * Inputs:     scene_index (0–15), value (0–78 or 0–43).
 * Outputs:    scene.settings.chain_condition1/next1/condition2/next2.
 * Accessors:  SHIFT+PLAY+SEQ chain menu (G8), scene_commitSettings()
 *             (G3), chain evaluator (G7).
 * Affiliates: scene_storeParameterByte() (SceneData.c:56),
 *             SCENE_COUNT (SceneData.h:24).
 */
```

---

### G3. SceneData.c — Update scene_commitSettings() for chain data

**File:** `Core/Bank/Scene/SceneData.c`
**Lines:** 1074–1107 (scene_commitSettings)
**Action:** MODIFY

Add chain parameter commits after the bus comp loop (line 1105):

```c
scene_setChainCondition1(scene_index, image.chain_condition1);
scene_setChainNext1(scene_index, image.chain_next1);
scene_setChainCondition2(scene_index, image.chain_condition2);
scene_setChainNext2(scene_index, image.chain_next2);
```

```
/*
 * S080 G3: include chain data in scene_commitSettings().
 *
 * What:       chain parameters are committed through their named
 *             setters as part of a whole-Scene settings commit.
 * Why:        scene_commitSettings() is the single validated entry
 *             point for writing all Scene settings (copy, load,
 *             clear).  Chain data must participate so scene copy/clear
 *             carries chain parameters.
 * Inputs:     src->chain_condition1/next1/condition2/next2.
 * Outputs:    target scene's chain parameters updated.
 * Accessors:  scene_commitSettings() (here).
 * Affiliates: scene_settingsDefaults() (G4), scene copy/clear paths,
 *             bank_selectActiveSceneForEditMask() — chain data is
 *             explicitly excluded from voice-edit-mask fan-out.
 */
```

---

### G4. SceneData.c — Default chain values in scene_settingsDefaults()

**File:** `Core/Bank/Scene/SceneData.c`
**Lines:** 1048–1072 (scene_settingsDefaults)
**Action:** no explicit change needed

The function starts with `memset(out, 0, sizeof(*out))` at line 1054. Since
the default for all chain parameters is 0 (nul condition, slf next-scene),
the memset already provides correct defaults. No additional code is required.

```
/*
 * S080 G4: chain defaults are zero (provided by memset).
 *
 * What:       chain_condition1/2 = 0 (nul/never), chain_next1/2 = 0
 *             (slf/repeat self).  Both defaults mean "no chaining."
 * Why:        a fresh Scene should have no automatic transitions.
 *             The memset at line 1054 already zeros these fields.
 * Inputs:     scene_settingsDefaults() memset.
 * Outputs:    all chain parameters = 0.
 * Accessors:  scene_settingsDefaults() (here).
 * Affiliates: filesystem_initSceneStage() (filesystem.c:16628) —
 *             also uses memset, so chain defaults are safe there too.
 */
```

---

### G5. filesystem.c — Sceneset writer: chain keys

**File:** `Core/Hardware/SD/filesystem.c`
**Lines:** 17321–17327 (filesystem_nextScenesetLine, after case 17u
`track_morph_shuffle`, before `default: return 0u`)
**Action:** ADD

```c
case 18u:
    return filesystem_formatAssignmentCsvU8Line(
        dst, cap, "chain_condition",
        (const uint8_t[]){scene->settings.chain_condition1,
                          scene->settings.chain_condition2}, 2u);
case 19u:
    return filesystem_formatAssignmentCsvU8Line(
        dst, cap, "chain_next",
        (const uint8_t[]){scene->settings.chain_next1,
                          scene->settings.chain_next2}, 2u);
```

```
/*
 * S080 G5: sceneset.scg writer — chain keys.
 *
 * What:       two new sceneset lines write the per-scene chain
 *             parameters as comma-separated pairs.  chain_condition
 *             writes condition1,condition2 (0–78).  chain_next
 *             writes next1,next2 (0–43).
 * Why:        chain data is Scene settings that must persist across
 *             save/load cycles.  Using CSV pairs keeps the key count
 *             low and mirrors the existing voice_morph_amount pattern.
 * Inputs:     scene->settings.chain_condition1/2, chain_next1/2.
 * Outputs:    two keyed lines in sceneset.scg.
 * Accessors:  filesystem_nextScenesetLine() (line 17229).
 * Affiliates: sceneset parser (G6), filesystem_initSceneStage() (G4).
 */
```

---

### G6. storageTypes.c — Sceneset parser: chain keys

**File:** `Core/Hardware/SD/storageTypes.c`
**Lines:** 795 (before the final `return STORAGE_STATUS_OK` in
`storage_scenesetParseLine()`)
**Action:** ADD

```c
} else if (storage_streq(key, "chain_condition")) {
    if (!target_settings)
        return STORAGE_STATUS_BAD_VALUE;
    uint8_t cc[2];
    st = storage_parseCsvU8(value, cc, 2u, 78u);
    if (st != STORAGE_STATUS_OK) return st;
    target_settings->chain_condition1 = cc[0];
    target_settings->chain_condition2 = cc[1];
} else if (storage_streq(key, "chain_next")) {
    if (!target_settings)
        return STORAGE_STATUS_BAD_VALUE;
    uint8_t cn[2];
    st = storage_parseCsvU8(value, cn, 2u, 43u);
    if (st != STORAGE_STATUS_OK) return st;
    target_settings->chain_next1 = cn[0];
    target_settings->chain_next2 = cn[1];
}
```

```
/*
 * S080 G6: sceneset.scg parser — chain keys.
 *
 * What:       parses chain_condition and chain_next CSV pairs into
 *             staged Scene settings.  Missing keys keep the stage
 *             default 0 (no chaining) — backward compatible.
 * Why:        sceneset.scg files from older firmware will not contain
 *             these keys; the 0 default produces no automatic
 *             transitions.
 * Inputs:     "chain_condition" or "chain_next" key with CSV value.
 * Outputs:    target_settings->chain_condition1/2, chain_next1/2.
 * Accessors:  storage_scenesetParseLine() (line 558).
 * Affiliates: sceneset writer (G5), filesystem_initSceneStage().
 */
```

---

### G7. sequencer.c — Chain condition evaluator

**File:** `Core/Sequencer/sequencer.c`
**Line:** new function, after seq_handleMasterBoundary() modifications (F8)
**Action:** ADD

```c
static uint8_t seq_evaluateChainCondition(uint8_t cond, uint8_t scene_index);
static void seq_evaluateSceneChains(uint8_t at_bar, uint8_t at_rep);
```

`seq_evaluateChainCondition()` takes a condition index (0–78) and the current
scene index, and returns 1 if the condition is met. The 79 conditions are:
- 0: nul (never)
- 1: xal (always, rep boundary)
- 2: bal (always, bar boundary)
- 3–13: x01..x08, x16, x32, x64 (repeat count)
- 14–24: b01..b08, b16, b32, b64 (bar count)
- 25–33: r1x..r9x (probability, rep)
- 34–42: r1b..r9b (probability, bar)
- 43–46: 1:4..4:4 (bar position mod 4)
- 47–54: 1:8..8:8 (bar position mod 8)
- 55–70: >01..>16 (from-scene)
- 71: fix (fill active, rep)
- 72: fib (fill active, bar)
- 73: !fx (fill inactive, rep)
- 74: !fb (fill inactive, bar)
- 75: lox (loop active, rep)
- 76: lob (loop active, bar)
- 77: !lx (loop inactive, rep)
- 78: !lb (loop inactive, bar)

`seq_evaluateSceneChains()` is called from seq_handleMasterBoundary() (F8) at
bar and repetition boundaries. It:
1. Reads the active scene's chain parameters.
2. Evaluates condition 1 first (priority).
3. If condition 1 fires, resolves next-scene 1.
4. If condition 1 does not fire, evaluates condition 2.
5. If condition 2 fires, resolves next-scene 2.
6. If both have probability and combined >= 100%, weights proportionally.
7. Resolves the next-scene value (slf/nxt/prv/s01–s16/r01–r16/r+1–r+4/
   r-1–r-4/stp) to an actual scene index.
8. If a change is needed, defers it through the scene change division path (F8).
9. Manual scene switches cancel all pending chain transitions.

```
/*
 * S080 G7: scene chain condition evaluator.
 *
 * What:       evaluates per-scene chain conditions at bar and
 *             repetition boundaries.  Called from the ISR via
 *             seq_handleMasterBoundary().  Evaluates both conditions,
 *             condition 1 has priority.  Probability conditions use
 *             GetRngValue() (ISR-safe).
 * Why:        automatic scene transitions are the core scene-chaining
 *             feature.  Evaluation must run in the ISR to detect
 *             boundaries with sample accuracy.  The actual scene
 *             change is deferred to the foreground via the pending-
 *             scene mechanism (F7/F8).
 * Inputs:     scene chain parameters from SceneData, seq_sceneBarCount,
 *             seq_sceneRepeatCount, seq_previousScene, seq_fillActive,
 *             seq_loopActive, seq_barCounter.
 * Outputs:    seq_pendingSceneChange flag and index set if a chain
 *             condition fires.
 * Accessors:  seq_handleMasterBoundary() (line 2283).
 * Affiliates: SceneData chain getters (G2), GetRngValue() (ISR-safe),
 *             scene change division (F8), `stp` next-scene handler.
 */
```

---

### G8. buttonHandler.c — SHIFT+PLAY+SEQ chain menu entry

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1767–1776 (BUT_START_STOP press)
**Action:** MODIFY

Add a SHIFT guard: if SHIFT is held when PLAY is pressed, do NOT toggle
transport. Instead, arm a `bh_chainMenuArmed` flag. The actual chain menu opens
when a SEQ button is pressed while this flag is armed.

New state variables:
```c
static uint8_t bh_chainMenuArmed  = 0u;
static uint8_t bh_chainMenuScene  = 0u;
static uint8_t bh_chainMenuActive = 0u;
```

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1074–1087 (buttonHandler_seqButtonPressed, SELECT_MODE_PERF)
**Action:** MODIFY

Add a check: if `bh_chainMenuArmed && buttonHandler_getShift()`:
1. Open the chain menu for the pressed SEQ button's scene.
2. Set `bh_chainMenuActive = 1u`, `bh_chainMenuScene = seqButtonPressed`.
3. Display the four chain parameters for that scene.
4. Both the active scene's and the viewed scene's SEQ LEDs blink.

The chain menu is a transient display overlay using the same static-param
rendering mechanism. The four chain parameters use dedicated name tables for
their 79 and 44 value options.

Exit: pressing SHIFT (release) while chain menu is active closes it. SEQ button
press while chain menu is active switches to that scene's chain parameters.

```
/*
 * S080 G8: SHIFT+PLAY+SEQ chain menu entry.
 *
 * What:       holding SHIFT, pressing PLAY (consumed without toggling
 *             transport), then pressing a SEQ button opens the chain
 *             menu for that scene.  The menu shows cn1, nx1, cn2, nx2.
 *             SHIFT release exits.  Pressing a different SEQ button
 *             switches which scene's chain is viewed.
 * Why:        the chain menu is a scene-specific editor that must not
 *             interfere with transport.  The SHIFT+PLAY chord is the
 *             entry gesture; PLAY is consumed so transport does not
 *             toggle.
 * Inputs:     SHIFT held + PLAY pressed + SEQ pressed.
 * Outputs:    chain menu displayed, chain parameters editable via
 *             encoder.  Scene LEDs blink for active and viewed scenes.
 * Accessors:  processPress() BUT_START_STOP (here),
 *             buttonHandler_seqButtonPressed() PERF case.
 * Affiliates: SceneData chain getters/setters (G2), chain condition
 *             name table (G9), next-scene name table (G9).
 */
```

---

### G9. MenuText.h — Chain condition and next-scene name tables

**File:** `Core/Menu/MenuText.h`
**Line:** after stepRollRateNames (line 240)
**Action:** ADD

Two new name tables:

```c
static const char chainConditionNames[][4] = {
    {79},
    {"nul"},{"xal"},{"bal"},
    {"x01"},{"x02"},{"x03"},{"x04"},{"x05"},{"x06"},{"x07"},{"x08"},
    {"x16"},{"x32"},{"x64"},
    {"b01"},{"b02"},{"b03"},{"b04"},{"b05"},{"b06"},{"b07"},{"b08"},
    {"b16"},{"b32"},{"b64"},
    {"r1x"},{"r2x"},{"r3x"},{"r4x"},{"r5x"},{"r6x"},{"r7x"},{"r8x"},{"r9x"},
    {"r1b"},{"r2b"},{"r3b"},{"r4b"},{"r5b"},{"r6b"},{"r7b"},{"r8b"},{"r9b"},
    {"1:4"},{"2:4"},{"3:4"},{"4:4"},
    {"1:8"},{"2:8"},{"3:8"},{"4:8"},{"5:8"},{"6:8"},{"7:8"},{"8:8"},
    {">01"},{">02"},{">03"},{">04"},{">05"},{">06"},{">07"},{">08"},
    {">09"},{">10"},{">11"},{">12"},{">13"},{">14"},{">15"},{">16"},
    {"fix"},{"fib"},{"!fx"},{"!fb"},
    {"lox"},{"lob"},{"!lx"},{"!lb"},
};

static const char chainNextNames[][4] = {
    {44},
    {"slf"},{"nxt"},{"prv"},
    {"s01"},{"s02"},{"s03"},{"s04"},{"s05"},{"s06"},{"s07"},{"s08"},
    {"s09"},{"s10"},{"s11"},{"s12"},{"s13"},{"s14"},{"s15"},{"s16"},
    {"r01"},{"r02"},{"r03"},{"r04"},{"r05"},{"r06"},{"r07"},{"r08"},
    {"r09"},{"r10"},{"r11"},{"r12"},{"r13"},{"r14"},{"r15"},{"r16"},
    {"r+1"},{"r+2"},{"r+3"},{"r+4"},
    {"r-1"},{"r-2"},{"r-3"},{"r-4"},
    {"stp"},
};
```

```
/*
 * S080 G9: chain condition and next-scene display name tables.
 *
 * What:       chainConditionNames has 79 entries for the chain
 *             condition selector.  chainNextNames has 44 entries
 *             for the next-scene selector.  Both follow the existing
 *             name-table convention: [0] = count byte, [1..N] =
 *             3+1 char display entries.
 * Why:        the chain menu's static-param renderer needs display
 *             strings for the condition and next-scene values.
 *             These are not DTYPE_MENU tables (all 16 IDs occupied).
 * Inputs:     compile-time const arrays in flash.
 * Outputs:    consumed by the chain menu renderer in menu.c.
 * Accessors:  chain menu display (G8), menu_encoderChangeParameter().
 * Affiliates: chain_condition1/2 (SceneData.h, G1), chain_next1/2.
 */
```

---

### G10. menu.h / menu.c — TEXT_ and valueNames for chain params

**File:** `Core/Menu/menu.h`
**Line:** after TEXT_SCENE_PC_RESET (added in F2)
**Action:** ADD

```c
TEXT_CHAIN_COND1,
TEXT_CHAIN_NEXT1,
TEXT_CHAIN_COND2,
TEXT_CHAIN_NEXT2,
```

And a new category:

**File:** `Core/Menu/MenuText.h`
**Line:** after `{"Prfrmnc"}`
**Action:** ADD

```c
{"Chain"},
```

**File:** `Core/Menu/menu.h`
**Line:** after CAT_PERFORMANCE
**Action:** ADD

```c
CAT_CHAIN,
```

Short names: `{"cn1"},{"nx1"},{"cn2"},{"nx2"}`
Long names: `{"Conditn1"},{"NxtScn1"},{"Conditn2"},{"NxtScn2"}`

valueNames entries (menu.c):
```c
{SHORT_CHAIN_COND1, CAT_CHAIN, LONG_CHAIN_COND1},
{SHORT_CHAIN_NEXT1, CAT_CHAIN, LONG_CHAIN_NEXT1},
{SHORT_CHAIN_COND2, CAT_CHAIN, LONG_CHAIN_COND2},
{SHORT_CHAIN_NEXT2, CAT_CHAIN, LONG_CHAIN_NEXT2},
```

```
/*
 * S080 G10: chain menu display names.
 *
 * What:       TEXT_ enums, valueNames entries, category "Chain" for
 *             the four chain parameters displayed in the SHIFT+PLAY+
 *             SEQ chain menu.
 * Why:        the chain menu uses the generic repaint path, which
 *             needs TEXT_ → display name resolution.
 * Inputs:     compile-time enums and tables.
 * Outputs:    display strings for chain menu cells.
 * Accessors:  chain menu renderer (G8), menu_repaintGeneric().
 * Affiliates: chainConditionNames/chainNextNames (G9).
 */
```

---

### G11. sequencer.c — Track stop behavior for `stp` next-scene

**File:** `Core/Sequencer/sequencer.c`
**Lines:** in seq_advanceTrackStep() (line 1688), near the once-mode early
return block (lines 1755–1757)
**Action:** ADD

Add a `SEQ_PLAY_STATE_CHAIN_STOPPED` flag. When the chain evaluator resolves
`stp` as the next-scene value:
1. Set `seq_trackPlayState[track] |= SEQ_PLAY_STATE_CHAIN_STOPPED` for all
   tracks.
2. In seq_advanceTrackStep(), check this flag and skip step processing
   (similar to once-mode stopped, but without clearing on transport restart
   until the next chain condition fires or transport stops and restarts).

When the next chain condition fires with a non-stp next-scene:
1. Clear the chain-stopped flag for all tracks.
2. Resume from the stopped position (tracks do not reset).

```
/*
 * S080 G11: track stop behavior for chain `stp` next-scene.
 *
 * What:       when a chain condition fires with next-scene = stp,
 *             all tracks stop advancing.  Transport continues;
 *             tracks freeze in place.  The next chain condition
 *             with a non-stp target resumes playback from the
 *             frozen position.
 * Why:        the plan specifies that stp is not a transport stop
 *             but a track freeze.  This enables musical patterns
 *             like "play 3 bars, silence for 1 bar, repeat."
 * Inputs:     chain evaluator resolves stp (index 43 in next-scene
 *             table).
 * Outputs:    SEQ_PLAY_STATE_CHAIN_STOPPED flag set/cleared.
 *             seq_advanceTrackStep() skips step processing when set.
 * Accessors:  seq_advanceTrackStep() (line 1688), chain evaluator
 *             (G7).
 * Affiliates: SEQ_PLAY_STATE_STOPPED (line 314), once-mode early
 *             return (line 1755).
 */
```

---

### G12. sequencer.c — Clear chain state on transport stop and manual switch

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 2877–2911 (seq_setRunning)
**Action:** MODIFY

In the stop branch, clear all chain-related state:
```c
seq_pendingSceneChange = 0u;
seq_pendingSceneQueue = 0u;
seq_sceneRepeatCount = 0u;
seq_sceneBarCount = 0u;
seq_previousScene = 0u;
```

Also, when a manual scene switch is executed (in menu_perfModeSceneButtonPressed
at menu.c:8138), clear pending chain transitions:
```c
seq_pendingSceneChange = 0u;
```

```
/*
 * S080 G12: clear chain state on transport stop and manual switch.
 *
 * What:       transport stop clears all deferred/queued scene changes,
 *             repeat/bar counts, and the previous-scene tracker.
 *             A manual scene switch cancels any pending chain-initiated
 *             transition.
 * Why:        the plan specifies that transport stop resets all scene
 *             chain state, and that manual switches cancel conditional
 *             changes.  This prevents stale chain state from affecting
 *             the next transport start.
 * Inputs:     seq_setRunning(0) or manual scene switch.
 * Outputs:    all pending/queue flags cleared, counters reset.
 * Accessors:  seq_setRunning() (line 2877),
 *             menu_perfModeSceneButtonPressed() (menu.c:8138).
 * Affiliates: seq_pendingSceneChange (F7), seq_sceneRepeatCount (F7).
 */
```

---

## Phase H — Integration and Verification

### H1. Autosave — Scene section growth for chain data

**File:** `Core/Bank/Scene/Autosave.h`
**Line:** 63 (`AUTOSAVE_SCENE_SECTION_BYTES 1920u`)
**Action:** VERIFY (may need MODIFY)

The chain data adds 4 bytes per scene to `scene_settings_t`. The Autosave scene
section is 1920 bytes, which includes all scene settings, effect, and kit data.
Verify that the section size still accommodates the expanded struct. If
`sizeof(scene_settings_t)` grows by 4 bytes, the existing 1920-byte section
must still have headroom. Check via `_Static_assert` that the autosave payload
layout remains valid.

The autosave byte-addressed payload maps SceneData fields to specific byte
offsets. The new chain fields must be added to `autosave_getLivePayloadByte()`
and `autosave_applyScenePayload()` in filesystem.c so they participate in
runtime autosave.

```
/*
 * S080 H1: Autosave integration for chain data.
 *
 * What:       the four chain bytes per scene must be included in the
 *             Autosave scene section.  Add them to
 *             autosave_getLivePayloadByte() (read path) and
 *             autosave_applyScenePayload() (write path).  Verify
 *             that AUTOSAVE_SCENE_SECTION_BYTES has headroom.
 * Why:        without Autosave integration, chain parameters would
 *             be lost on unexpected power loss.
 * Inputs:     scene_settings_t chain fields.
 * Outputs:    chain data included in autosave payload.
 * Accessors:  autosave_getLivePayloadByte() (filesystem.c),
 *             autosave_applyScenePayload() (filesystem.c).
 * Affiliates: Autosave.h section sizes, SceneData chain fields (G1).
 */
```

---

### H2. Build verification

Compile the full firmware after each phase and verify:
1. No warnings (treat as errors).
2. `sizeof(scene_settings_t)` matches expectation (old + 4 bytes).
3. `_Static_assert` on `PAR_BUS_COMP_SIDECHAIN < NUM_PARAMS` still passes
   (and extends to new PAR_ values).
4. settings.cfg `lines=` count matches the number of data lines.
5. conditionNames count byte matches the array length.

---

### H3. Hardware verification checklist

| Test | Feature | Expected |
|------|---------|----------|
| PERF entry | A2 | Display starts at mrp, subpage 0 |
| Encoder scroll | A1/A3 | mrp → fxm → 1VM → 2VM → [subpage] → 3VM → ... |
| Automation markers | A3/A4 | Underlines appear on correct cells across both subpages |
| SHIFT+REC | E7 | Opens overlay settings, SHIFT or REC exits, returns to prior page |
| BAR overlay | E8/E12 | BAR toggle lights LED, VOICE fires trigger, SHIFT+VOICE mutes |
| Roll overlay | E12 | Held VOICE repeats at configured rate |
| Looper engage | C4 | SELECT press loops all tracks, SELECT LED lights |
| Looper division | C4 | Different SELECT buttons change loop length |
| Looper disengage | C6 | All SELECT released, tracks resume at master clock |
| Per-track loop | D1 | VOICE+SELECT loops subset, VOICE swap during loop |
| Dot/triplet | E9 | BAR2 held = dot, BAR1 held = triplet during loop |
| Scene change during loop | D2 | Loop start resets to new scene beginning |
| lop/!lp conditions | B4 | Steps with lop fire only during loop, !lp fire only without |
| Scene chain menu | G8 | SHIFT+PLAY+SEQ opens chain params for that scene |
| Chain evaluation | G7 | Conditions fire at correct boundaries, condition 1 has priority |
| stp next-scene | G11 | Tracks freeze, resume on next chain |
| Scene change division | F8/F9 | Non-instant changes defer to boundary |
| Scene queue | F9 | SHIFT+SEQ queues, LED blinks, fires at division |
| Queue action | F9 | mut/mor/m+m/rld execute on fire |
| PCR | F9 | Tracks restart from beginning on scene change |
| Settings save/load | E10/E11/F5/F6 | All new keys persist across power cycle |
| Scene save/load | G5/G6 | Chain data persists in sceneset.scg |
| Recording+looper mutual exclusion | C7 | Engaging loop disables rec, rec blocked during loop |

---

## Summary of File Changes

| File | Phase | Changes |
|------|-------|---------|
| `Core/Menu/menuPages.h` | A1, E3, F3 | PERF split to 2 subpages; RECORDING_PAGE repurposed; globals subpage 3 |
| `Core/Menu/menu.h` | E2, F2, G10 | New TEXT_ and CAT_ enums |
| `Core/Menu/MenuText.h` | B1, E4, F4, G9 | conditionNames +2; overlay/chain/scene name tables; short/cat/long names |
| `Core/Menu/menu.c` | A3, A4, A5, D2, E5, E6, F9, F10, G10, G12 | Markers, scanner, valueNames, static-param rendering, scene change gating |
| `Core/Hardware/frontPanel/buttonHandler.h` | — | No changes |
| `Core/Hardware/frontPanel/buttonHandler.c` | A2, C4–C8, D1, D3, E7–E9, E12, F9, G8 | Bug fix; looper SELECT/release; overlay toggle; SHIFT+REC; chain menu; scene queue |
| `Core/Sequencer/sequencer.h` | B2, C1 | seq_loopActive extern; looper state/API decls |
| `Core/Sequencer/sequencer.c` | B3–B5, C2, C3, C9, F7, F8, G7, G11, G12 | loopActive def; condition dispatch +2; looper engine; scene change deferral; chain evaluator |
| `Core/Bank/Scene/SceneData.h` | G1 | chain fields in scene_settings_t |
| `Core/Bank/Scene/SceneData.c` | G2, G3, G4 | chain getters/setters; commitSettings update |
| `Core/Bank/Scene/Preset/ParameterArray.h` | E1, F1 | PAR_ enums for overlay + scene globals |
| `Core/Hardware/SD/filesystem.c` | E10, E11, F5, F6, G5 | settings.cfg writer +8 keys; sceneset writer +2 keys; parser updates |
| `Core/Hardware/SD/storageTypes.c` | G6 | sceneset parser — chain keys |
| `Core/Bank/Scene/Autosave.h` | H1 | Verify section size headroom |
| `Core/Hardware/frontPanel/ledHandler.h` | — | No changes (existing constants sufficient) |
| `Core/Hardware/frontPanel/ledHandler.c` | — | No changes (existing API sufficient) |
| `Core/Sequencer/StepScale.c` | — | No changes (existing API sufficient for DDA roll timing) |
| `config.h` | — | No changes |

---

*End of implementation schedule. No code changes in this turn.*
