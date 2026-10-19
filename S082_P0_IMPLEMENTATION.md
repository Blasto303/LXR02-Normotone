# S082 P0 — Implementation Schedule

*Written 2026-10-18 from a code deep-dive of commit `8b1b647` ("load save
cleanup pre-implementation plan"; working tree clean). Planning only: no code
has changed. It implements `S082_P0_LOAD_SAVE_MENU_CLEANUP.md` revision 4
(decisions S1–S25) and `S082_P1_LOAD_SAVE.md` (D1–D27).*

---

## 0. How to use this schedule

### 0.1 Line numbers

- Every location is `file:line` at `8b1b647`. Each entry also names its
  **anchor function**, because line numbers move as soon as the first edit
  lands.
- Within one file, apply edits **bottom-up**, or re-find each anchor by its
  function name.
- Paths are relative to the repository root. `Core/` is omitted in tables
  where the column is already a path.

### 0.2 Actions

| Tag | Meaning |
|-----|---------|
| **ADD** | New code (function, declaration, branch, field). |
| **MOD** | Change existing code in place. |
| **REM** | Delete existing code. |
| **DOC** | Comment-only change. |

### 0.3 Comment blocks

Every ADD and MOD carries the comment block to paste next to the code, in the
project's existing style:

```
/*
 * <one-line summary>.
 *
 * What: ...
 * Why: ...
 * Inputs: ...
 * Outputs: ...
 * Accessors: ...
 * Affiliates: ...
 */
```

The same block is used in the `.h` declaration (shortened to What / Inputs /
Outputs / Accessors where the `.c` already holds the full text, matching the
existing header style).

### 0.4 RAM (policy check)

All new static storage is listed in §9 with bytes, region, lifetime and
owner. It is inside the acknowledged totals except where §9 marks
**ACK NEEDED**.

### 0.5 Order

| Phase | Content | Depends on |
|-------|---------|-----------|
| A1 | Name editing: pot repaint coalescing, BAR2 | — |
| A2 | Instrument Load `chg` row | — |
| B1 | Bank child content preview (Load source, Save slot) | — |
| B2 | Bank page UI: selection, Merge, COPY tap/hold, LEDs, labels, retention | B1 |
| B3 | Bank Load/Save operation semantics (two masks, empty, removal, active/viewed rule, edit masks, HCNAMES tokens, AutoSave) | B2 |
| C | Scene 17 background load | B3 |
| D | Docs, tools, close | all |

Each phase ends with a build (`make all && make img`), the link budget
(`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`) and the
verification rows in §10.

---

## Phase A1 — Name editing display path and BAR2 (S14, Q32–Q34)

### A1.1 `Core/Menu/menu.c:9750-9752` — `menu_handleLoadSaveKnobDelta()` — MOD

**Change.** Replace the per-delta full repaint with the coalesced knob flag
for pots 2–4. Pot 1 (case 0) keeps a full repaint, because it changes
page/type (a layout change).

```c
    default:
        return;
    }

    if (knobNr == 0u) {
        if (!menu_storageBusy)
            menu_repaintAll();
    } else {
        menu_knobs_dirty = 1u;
    }
}
```

**Comment block** (above the new tail):

```c
/*
 * Coalesce Load/Save pot repaints through the shared knob service.
 *
 * What: pots 2-4 (slot number, cursor, character) now only raise
 * menu_knobs_dirty; menu_serviceKnobRepaint() repaints at most once per
 * LCD_LIMIT_TICKS_PER_REFRESH (20 ms) and sends changed characters only.
 * Pot 1 still repaints in full because it changes the page/type layout.
 * Why: the former menu_repaintAll() on every delta invalidated the LCD shadow,
 * so each pot step queued a whole 32-character frame (~70 queue ops, ~70 ms on
 * the TIM7 nibble machine). While one frame drained the next did not fit and
 * was deferred, which made the Save name editor lag far behind the same pots
 * on the VOICE pages (S082 P0 §5.1). Pot behaviour is unchanged (S14).
 * Inputs: knob index and the delta already applied to Menu state above.
 * Outputs: menu_knobs_dirty for pots 2-4; an immediate full repaint for pot 1.
 * Accessors: menu_parseKnobDelta() (sole caller).
 * Affiliates: menu_serviceKnobRepaint(), main.c service_knob_repaint(),
 * sendDisplayBuffer(), menu_repaintLoadSavePage().
 */
```

### A1.2 `Core/Menu/menu.c:14443-14454` — `menu_serviceKnobRepaint()` — MOD

**Change.** On Load/Save pages, dispatch to `menu_repaint()`, which renders
`menu_repaintLoadSavePage()` and calls `sendDisplayBuffer()` itself.

```c
    if (menu_knobs_dirty) {
        menu_knobs_dirty = 0;
        if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
            menu_repaint();
            return;
        }
        menu_repaintGeneric();
        sendDisplayBuffer();
    }
```

**Comment block:**

```c
/*
 * Route the coalesced knob repaint to the page that owns the LCD.
 *
 * What: Load/Save pages render through menu_repaint() (which dispatches to
 * menu_repaintLoadSavePage() and sends the frame); every other page keeps the
 * generic renderer.
 * Why: Load/Save pots now use this coalesced path (A1.1). The generic renderer
 * would paint a parameter page over the Load/Save frame.
 * Inputs: menu_knobs_dirty, menu_activePage, menu_storageBusy (guard above,
 * unchanged: no repaint while storage owns the panel).
 * Outputs: at most one incremental LCD frame per 20 ms service tick.
 * Accessors: main.c service_knob_repaint().
 * Affiliates: menu_handleLoadSaveKnobDelta(), menu_repaint(),
 * menu_repaintLoadSavePage() (clears its rows first, so a diff repaint is
 * safe), sendDisplayBuffer().
 */
```

### A1.3 `Core/Menu/menu.c:9771-9799` — `menu_loadSaveBarButtonPressed()` — MOD

**Change.**
- In the BAR2 branch, remove `name[ci + 1u] = ' ';` (REM).
- Replace both `menu_repaintAll()` calls with `menu_repaint()` (MOD).
- Replace the BAR2 comment (DOC).

```c
    if (advance) {
        if (ci < 7u) {
            menu_saveOptions.state++;
        } else {
            editModeActive = 0u;
            menu_repaint();
            return 1u;
        }
    } else {
```

**Comment block** (replaces "BAR2 is insert-space-forward"):

```c
/*
 * BAR2 is cursor-forward (S082 S14).
 *
 * What: moves the underline cursor one cell right and leaves every character
 * of the name exactly as it is; at the last cell the press is consumed and
 * changes nothing. Edit brackets close as before.
 * Why: the former insert-space-forward blanked the next cell, so stepping
 * right through an existing name destroyed it (user, S082).
 * Inputs: menu_saveOptions.state (NAME1..NAME8) and the active name buffer.
 * Outputs: the cursor state only; no name byte is written.
 * Accessors: buttonHandler.c BUT_BAR2 press.
 * Affiliates: BAR1 backspace (unchanged), menu_loadSaveActiveNameBuffer(),
 * menu_repaint() (incremental: a cursor move changes two cells).
 */
```

### A1.4 `Core/Hardware/frontPanel/buttonHandler.c:2919-2922` — BUT_BAR2 case — DOC

Change "BAR2 is insert-space-forward in Load/Save character entry" to
"BAR2 is cursor-forward (keeps characters) in Load/Save character entry".

### A1.5 Measurement (no code)

Time `filesystem_instrumentTargetExists()` per Instrument Save repaint with the
existing DEV trace (one temporary `autosaveTrace_record()` around the call,
removed before close). If it exceeds ~1 ms, cache its result per
`(type, name)` change (a separate follow-up, not scheduled here).

---

## Phase A2 — Instrument Load `chg` row (S082 P0 §6, Q17–Q20)

### A2.1 `Core/Menu/menu.c:1695-1698` — `menu_instrument_source_t` — MOD

```c
typedef enum {
    MENU_INSTRUMENT_SOURCE_KIT = 0,
    MENU_INSTRUMENT_SOURCE_POOL,
    MENU_INSTRUMENT_SOURCE_CHANGE
} menu_instrument_source_t;
```

**Comment block** (above the enum):

```c
/*
 * Lower-row source of nested Instrument Load.
 *
 * What: KIT is the reversible `kit` row (the session snapshot); POOL is a
 * numbered Instrument/ library row; CHANGE is the `chg` row shown while the
 * selected type differs from the destination slot's resident type and no
 * file of the new type has been loaded yet.
 * Why: a type change has no `kit` snapshot of the new type, so showing `kit`
 * was wrong (S082 item 2). CHANGE names row 000 of the new type and commits it
 * on a click or a positive turn.
 * Inputs/Outputs: written by menu_instrumentLoadStepType(), the lower-row
 * encoder handler and the apply completion; read by the renderer and the
 * deferred-selection dispatcher.
 * Accessors: menu_repaintLoadSavePage(), menu_handleLoadSaveMenu(),
 * menu_tickInstrumentApply(), menu_pollPresetStatus().
 * Affiliates: menu_instrumentLoadCommitChange() (A2.6),
 * menu_instrumentLoadKitRowAvailable() (A2.2). No storage change (enum value).
 */
```

### A2.2 `Core/Menu/menu.c` after `menu_invalidateInstrumentLoadTemp()` (ends 7449) — ADD `menu_instrumentLoadKitRowAvailable()`

```c
static uint8_t menu_instrumentLoadKitRowAvailable(void)
{
    return (uint8_t)(menu_instrumentTempValid ||
                     menu_instrumentTempOperationPending);
}
```

Add the forward declaration beside `menu.c:1930`.

**Comment block:**

```c
/*
 * Report whether the reversible `kit` row exists in this nested session.
 *
 * What: nonzero while the session snapshot is valid or still being written.
 * Why: after a `chg` commit the snapshot is invalidated and the `kit` row is
 * gone until the nested session ends (Q18); the encoder must then clamp at
 * row 000 instead of crossing back to a row with nothing to restore. Deriving
 * this from the existing snapshot state needs no new byte.
 * Inputs: menu_instrumentTempValid, menu_instrumentTempOperationPending.
 * Outputs: boolean.
 * Accessors: lower-row encoder handler (A2.5), menu_instrumentLoadStepType()
 * (A2.4).
 * Affiliates: menu_prepareInstrumentLoadTemp() (sets it at session entry),
 * menu_invalidateInstrumentLoadTemp() (clears it).
 */
```

### A2.3 `Core/Menu/menu.c` before `menu_instrumentLoadRefreshBaseType()` (8200) — ADD `menu_instrumentTypeSelectableForMask()`

```c
static uint8_t menu_instrumentTypeSelectableForMask(instrument_type_t type)
{
    uint8_t scene_index;

    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((menu_kitLoadSceneMask & (uint16_t)(1u << scene_index)) != 0u &&
            !instrumentManager_typeSelectableForSceneSlot(
                scene_index, menu_instrumentLoadSlot, type))
            return 0u;
    }
    return instrumentManager_typeSelectableForSceneSlot(
        menu_instrumentLoadScene, menu_instrumentLoadSlot, type);
}
```

**Comment block:**

```c
/*
 * Test a candidate Instrument type against every destination Scene.
 *
 * What: nonzero only when the type is selectable for the voice slot in the
 * browsed Scene and in every Scene of the destination mask.
 * Why: a `chg` commit loads into every selected Scene; the Advanced-type
 * limit (at most two) can differ per Scene, so a type legal in the browsed
 * Scene could silently mix types across the mask (Q20: refuse instead).
 * Skipping such types in the type list is the refusal.
 * Inputs: type, menu_kitLoadSceneMask, menu_instrumentLoadScene/Slot.
 * Outputs: boolean.
 * Accessors: menu_instrumentLoadStepType().
 * Affiliates: instrumentManager_typeSelectableForSceneSlot(),
 * preset_loadInstrumentForScenes().
 */
```

### A2.4 `Core/Menu/menu.c:8264-8360` — `menu_instrumentLoadStepType()` — MOD

**Change.**
1. In the registry loop (8326-8357), replace
   `instrumentManager_typeSelectableForSceneSlot(...)` with
   `menu_instrumentTypeSelectableForMask(entry->type)`.
2. REM the `menu_invalidateInstrumentLoadTemp();` at 8336. The snapshot now
   survives a type step (Q19). The S054 owed-restore flag survives with it.
3. After `menu_instrumentLoadType = entry->type;`, ADD the source selection:

```c
            if (!menu_instrumentLoadMorphMode &&
                entry->type != menu_instrumentLoadBaseType) {
                menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_CHANGE;
                menu_instrumentLoadShownType = entry->type;
                menu_instrumentLoadShownIndex = 0u;
            } else if (menu_instrumentLoadKitRowAvailable()) {
                menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
                menu_instrumentLoadShownType = entry->type;
            } else {
                menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_POOL;
                menu_instrumentLoadShownType = entry->type;
            }
```

4. Keep the Morph-row branches (8282-8310). They invalidate the snapshot by
   design, because the Morph `kit` row has different semantics.

**Comment block** (replacing the comment at 8333-8335):

```c
/*
 * A type step chooses the lower-row source; it no longer ends the preview.
 *
 * What: a normal type other than the slot's resident type shows `chg`
 * (row 000 of that type, loaded only on commit); the resident type shows the
 * `kit` row again when the session snapshot still exists, otherwise the pool
 * row the slot now holds.
 * Why: the old code invalidated the `kit` snapshot here while the renderer
 * still printed `kit <old name>`, and a negative turn then restored a snapshot
 * that no longer existed (S082 item 2). Stepping types is browsing; only a
 * committed type change ends the `kit` row (Q18, Q19).
 * Inputs: selected registry entry, menu_instrumentLoadBaseType, Morph mode,
 * snapshot state.
 * Outputs: menu_instrumentLoadType, menu_instrumentLoadSource,
 * menu_instrumentLoadShownType/Index, then the existing typed-index request.
 * Accessors: lower-row and type-row encoder handling in
 * menu_handleLoadSaveMenu().
 * Affiliates: menu_instrumentTypeSelectableForMask(),
 * menu_instrumentLoadKitRowAvailable(), menu_requestInstrumentIndexLoad(),
 * menu_restoreInstrumentLoadTemp() (type-matched restore guard),
 * menu_instrumentKitRestorePending (S054).
 */
```

### A2.5 `Core/Menu/menu.c:13160-13290` — nested Instrument Load lower-row encoder — MOD

**Change.**

(a) At the top of the `if (count > 0u && inc != 0)` block (13166), ADD the
CHANGE handling:

```c
                    if (menu_instrumentLoadSource ==
                        MENU_INSTRUMENT_SOURCE_CHANGE) {
                        if (inc > 0)
                            menu_instrumentLoadCommitChange();
                        if (!menu_storageBusy)
                            menu_repaintAll();
                        return;
                    }
```

(b) In the crossing test (13185, `if (next < 0)`), ADD before the existing
body:

```c
                    if (next < 0 && !menu_instrumentLoadKitRowAvailable()) {
                        /* No `kit` row after a committed type change. */
                        next = 0;
                    }
```

Then the existing `next < 0` branch runs only when a `kit` row exists.

**Comment block** for (a):

```c
/*
 * `chg` row turns (Q17).
 *
 * What: a positive turn commits the type change by loading row 000 of the new
 * type; a negative turn does nothing; the row stays `chg` until the commit.
 * Why: the type change must be explicit and permanent, and row 000 is the row
 * the `chg` label already names.
 * Inputs: encoder delta while the lower row is in edit mode.
 * Outputs: one Instrument Load request through menu_instrumentLoadCommitChange().
 * Accessors: menu_handleLoadSaveMenu() nested Instrument Load branch.
 * Affiliates: menu_instrumentLoadRequestSelection(), the deferred-selection
 * retry in menu_pollPresetStatus() (POOL source).
 */
```

**Comment block** for (b):

```c
/*
 * Clamp at row 000 when the session has no `kit` row (Q18).
 *
 * What: after a committed type change the snapshot is gone, so a negative
 * turn at 000 stays at 000 instead of entering the `kit` restore branch.
 * Why: restoring a snapshot that does not exist would post a refused request
 * and leave the row labelled `kit` over a pool Instrument.
 * Inputs: the computed next row, snapshot availability.
 * Outputs: next clamped to 0.
 * Accessors: the lower-row encoder handler.
 * Affiliates: menu_instrumentLoadKitRowAvailable(), menu_restoreInstrumentLoadTemp().
 */
```

### A2.6 `Core/Menu/menu.c` after `menu_instrumentLoadRequestSelection()` (ends ~8198) — ADD `menu_instrumentLoadCommitChange()`

```c
static void menu_instrumentLoadCommitChange(void)
{
    menu_selectionGeneration++;
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_POOL;
    menu_instrumentLoadShownType = menu_instrumentLoadType;
    menu_instrumentLoadShownIndex = 0u;
    menu_instrumentLoadIndex[menu_instrumentLoadType] = 0u;
    if (menu_storageBusy)
        menu_deferSelectionRequest = 1u;
    else
        menu_instrumentLoadRequestSelection();
}
```

Add the forward declaration beside `menu.c:1929`.

**Comment block:**

```c
/*
 * Commit a `chg` row: load row 000 of the newly selected type.
 *
 * What: converts the lower row to POOL index 0 and posts the ordinary
 * Instrument Load (or the coalesced retry while storage is busy). The slot's
 * type changes when the load commits; menu_tickInstrumentApply() then ends
 * the `kit` row for the session (A2.8).
 * Why: one commit path for the click and the positive turn (Q17), reusing the
 * proven pool-load transaction (clear modulation owners, image replace,
 * rebind) instead of a second loader.
 * Inputs: menu_instrumentLoadType, destination mask/slot, storage state.
 * Outputs: source/shown state; a Preset request or menu_deferSelectionRequest.
 * Accessors: A2.5(a), A2.7.
 * Affiliates: menu_instrumentLoadRequestSelection(),
 * preset_loadInstrumentForScenes(), menu_pollPresetStatus() deferred retry.
 */
```

### A2.7 `Core/Menu/menu.c:13158` — `menu_handleLoadSaveMenu()` nested Instrument Load — MOD

**Change.** Replace `(void)btnClicked;` with a click commit for `chg`:

```c
        if (btnClicked &&
            menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR &&
            menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_CHANGE) {
            editModeActive = 1u;
            menu_instrumentLoadCommitChange();
            return;
        }
```

**Comment block:**

```c
/*
 * `chg` row click (Q17).
 *
 * What: an encoder click on the lower row while it shows `chg` commits the type
 * change (row 000) and leaves the row in edit mode for further browsing.
 * Why: the user asked for "clicked or changed"; the click otherwise only
 * toggles edit mode on this row.
 * Inputs: btnClicked, cursor row, lower-row source.
 * Outputs: menu_instrumentLoadCommitChange().
 * Accessors: menu_parseEncoder() -> menu_handleLoadSaveMenu().
 * Affiliates: A2.5(a).
 */
```

### A2.8 `Core/Menu/menu.c:765-791` — `menu_tickInstrumentApply()` — MOD

**Change.** After `menu_instrumentApplyActive = 0u;` (780), ADD the
type-change finalisation:

```c
    if (menu_instrumentLoadActive && !menu_instrumentSaveMode &&
        !menu_instrumentLoadMorphMode) {
        const kit_instrument_slot_t *slot = scene_instrumentSlotConst(
            menu_instrumentLoadScene, menu_instrumentLoadSlot);
        if (slot && slot->type != menu_instrumentLoadBaseType) {
            menu_invalidateInstrumentLoadTemp();
            menu_instrumentLoadRefreshBaseType(1u);
            menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_POOL;
        }
    }
```

**Comment block:**

```c
/*
 * End the `kit` row after a committed type change (Q18).
 *
 * What: when the applied slot's resident type differs from the session base
 * type, the session snapshot (of the old type) is disposed and the base type
 * follows the slot, so the row stays numbered and stepping back to the old
 * type shows `chg`.
 * Why: the type change is permanent for the session; the old-type snapshot
 * can no longer be restored into a different-type slot.
 * Inputs: SceneData slot type after apply, menu_instrumentLoadBaseType.
 * Outputs: snapshot invalidated, base type refreshed, POOL source.
 * Accessors: menu_pollPresetStatus() via menu_tickInstrumentApply().
 * Affiliates: menu_invalidateInstrumentLoadTemp(),
 * menu_instrumentLoadRefreshBaseType(), menu_finishInstrumentApplySession()
 * (its `kit` retry now sees no snapshot and does nothing).
 */
```

### A2.9 `Core/Menu/menu.c:11026-11037` — `menu_repaintLoadSavePage()` nested Instrument Load — MOD

**Change.** ADD a CHANGE branch before the `MENU_INSTRUMENT_SOURCE_KIT` branch:

```c
        if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_CHANGE) {
            char first_name[MENU_INSTRUMENT_SAVE_NAME_LEN + 1u];

            memcpy(&editDisplayBuffer[1][1], "chg", 3u);
            filesystem_copyInstrumentDisplayName(first_name,
                                                  menu_instrumentLoadType, 0u);
            memcpy(&editDisplayBuffer[1][5], first_name, 8u);
        } else if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_KIT) {
```

**Comment block:**

```c
/*
 * Render the `chg` row.
 *
 * What: `chg` in the three selector cells and the name of row 000 of the
 * selected type, read from its typed `.hcindex` (blank during the index
 * handoff, filled by the index completion repaint).
 * Why: shows what a commit will load; there is no snapshot of the new type, so
 * `kit` would be wrong (S082 item 2).
 * Inputs: menu_instrumentLoadType, the resident typed index.
 * Outputs: LCD cells 1..12 of row 2.
 * Accessors: menu_repaint(), menu_repaintAll().
 * Affiliates: filesystem_copyInstrumentDisplayName(), menu_instrumentIndexLoadComplete().
 */
```

### A2.10 Comment updates — DOC

- `menu.c:1670-1677` (the Kit/Instrument Load mask comment): add "the lower
  row is `kit`, a numbered pool row, or `chg` after a type step".
- `menu.c:10890-10893` (renderer header): mention `chg`.

---

## Phase B1 — Bank child content preview (S082 P0 §4.7)

### B1.1 `Core/Hardware/SD/filesystem.c:1303` — ADD `op_bank_child_content_mask`

```c
static uint16_t op_bank_child_content_mask = 0u;
```

**Comment block:**

```c
/*
 * Bank child content mask from the last preview scan (+2 B SRAM1, S082 B1).
 *
 * What: bit N set when Bank-local child NN exists AND its PAT4 address array
 * holds at least one trigger bit - the same rule PERF uses to light a Scene
 * (pat_sceneHasActiveSteps()).
 * Why: Bank Load/Save LEDs show the source/slot "as PERF would", and Merge acts
 * only on Scenes with pattern data (S8, Q7); presence alone cannot tell.
 * Inputs: filesystem_scanBankScenes_tick() probe phases.
 * Outputs: filesystem_bankChildContentMask().
 * Lifetime: static; reset by filesystem_start() with the other op fields.
 * Owner: filesystem.c. Accessors: Menu Bank preview completion.
 * Affiliates: op_bank_child_present_mask, PATTERN_FILE_HEADER_BYTES,
 * PAT_ADDR_TRIGGER_BIT.
 */
```

Also ADD `op_bank_child_content_mask = 0u;` beside the existing
`op_bank_child_present_mask = 0u;` in the operation reset
(`filesystem.c:26239`).

### B1.2 `Core/Hardware/SD/filesystem.c:15727-15893` — `filesystem_scanBankScenes_tick()` — MOD

**Change.**
- Case 8 (15832-15861): when a child parses, also copy its display name into
  `op_bank_child_scratch.bank_child_display[child_slot]` (the same 144 B
  union Bank Load uses; the preview is a separate operation).
- Case 10 (15869-15876): instead of finishing, enter the probe at phase 11
  when `op_bank_child_present_mask != 0`.
- ADD phases 11–19 (one child per pass, cooperative):

| Phase | Action |
|------:|--------|
| 11 | pick the next present child at/after `op_bank_child_cursor`; none left → `chdir(NULL)`, finish DONE |
| 12 | `storage_formatBankSceneDir(name, child, display)`; `afatfs_opendir_lfn()` inside the Bank directory (CWD is still the Bank directory, set at phase 7, kept open until phase 9 closes the iterator handle; re-enter the Bank directory as Bank Load phases 21–26 do if needed) |
| 13 | wait open; `chdir` into the child; `afatfs_findFirstObject()` |
| 14 | iterate: the first `.pat` file (`filesystem_nameHasExtension(.., ".pat")`) → copy its open name into `op_scene_pattern_open_name`; end → phase 15 |
| 15 | close the child directory handle; no `.pat` → content bit stays 0, phase 19 |
| 16 | `afatfs_fopen_lfn(op_scene_pattern_open_name, "r")` |
| 17 | `afatfs_fseek(op_file, PATTERN_FILE_HEADER_BYTES, AFATFS_SEEK_SET)` (wait while IN_PROGRESS); set `op_item_offset = 0` |
| 18 | read up to 160 B into `op_line_buf`; for each odd byte test `0x80` (bit 15 of the little-endian entry); a hit sets the content bit and jumps to the close; stop at 1,792 B |
| 19 | close the file; `afatfs_chdirParent()` (poll until it returns `AFATFS_OPERATION_SUCCESS`; see `asyncfatfs.h:451`) back to the Bank directory; `op_bank_child_cursor++`; phase 11 |

**Comment block** (above the new phases):

```c
/*
 * Probe each present Bank child for pattern content (S082 B1).
 *
 * What: for every child found by the iterator, opens its one `.pat` file,
 * streams the 1,792-byte address array (offset PATTERN_FILE_HEADER_BYTES) in
 * 160-byte reads through op_line_buf and sets op_bank_child_content_mask bit N
 * at the first trigger bit. A child without a readable `.pat` has no content.
 * Why: the Bank page lights a Scene only when it has trigger steps (S082 P0
 * §4.3) and Merge acts only on such Scenes (S8). Reading the address array is
 * the PERF rule applied to the file; nothing is loaded or validated.
 * Inputs: op_slot (root Bank), op_bank_child_present_mask, the per-child
 * display names captured in case 8.
 * Outputs: op_bank_child_content_mask; CWD returned to root at finish.
 * Buffers: op_line_buf (existing, 160 B) and op_scene_pattern_open_name
 * (existing); fs_stage_workspace and pat_background_region are untouched (the
 * AutoSave writer and Scene 17 own them).
 * Accessors: filesystem_requestScanBankScenes().
 * Affiliates: filesystem_loadSceneDirectory_tick() phase 9 (the same `.pat`
 * discovery rule), storage_formatBankSceneDir(), Menu Bank preview.
 * Cost: one directory visit and one 1.8 kB read per present child; measured in
 * B1 (R1). Fallback if too slow: a stored mask in bankset.bcg (A1).
 */
```

### B1.3 `Core/Hardware/SD/filesystem.c:30527-30541` — ADD `filesystem_bankChildContentMask()`

```c
uint16_t filesystem_bankChildContentMask(void)
{
    return op_bank_child_content_mask;
}
```

**Comment block:** What: last preview scan's content mask (child present and
holding trigger steps). Why: Bank LEDs and the Merge act mask. Inputs: none.
Outputs: 16-bit mask. Accessors: `menu_bankLoadPreviewComplete()`.
Affiliates: `filesystem_bankChildSceneMask()`. Risk: operation-local, so Menu
must check that the slot it requested is still shown (as for the present
mask).

### B1.4 `Core/Hardware/SD/filesystem.h:785-786` — ADD declaration

```c
uint16_t filesystem_bankChildContentMask(void);
```

Header comment: "Content mask (present ∩ trigger steps) of the last Bank child
preview; same staleness rule as `filesystem_bankChildSceneMask()`."

### B1.5 `Core/Hardware/SD/filesystem.c:30505-30525` — `filesystem_requestScanBankScenes()` — DOC

Update the comment: the scan returns present **and** content masks, and runs
on Load (source) and Save (slot).

### B1.6 Timing trace — ADD (logging only)

- `Core/Bank/Scene/AutosaveTrace.h:144` (stage enum): ADD
  `AUTOSAVE_TRACE_STAGE_BANK_PREVIEW = 'Q'`. Pick an unused letter after
  checking the enum.
- At scan finish: record `value32 = present_mask | (content_mask << 16)` and
  the elapsed ms in `detail`. `#if DEV_MODE_LOGGING` only.
- `tools/decode_devlogs.py`: decode the new stage.

**Comment block:** What: one trace record per completed preview with both masks
and elapsed time. Why: R1 measurement on the slow and fast reference cards.
Inputs: masks, start tick. Outputs: one 8-byte trace record. Logging-only (no
production RAM). Affiliates: `decode_devlogs.py`.

---

## Phase B2 — Bank page UI (S1–S3, S7, S16; P0 §4.3–§4.6)

### B2.1 `Core/Menu/menu.c:1692-1694` — Bank preview state — MOD + ADD

The existing triple (`menu_bankLoadPreviewMask/Slot/Valid`) becomes the
**Bank child preview** for whichever Bank page is shown: the Load source, or
the Save slot (Save view = held view). The Bank page is one page at a time, so
one triple serves both.

ADD:

```c
static uint16_t menu_bankPreviewContentMask = 0u;
static uint8_t menu_bankUiFlags = 0u;
#define MENU_BANK_MERGE_LOAD   0x01u
#define MENU_BANK_MERGE_SAVE   0x02u
#define MENU_BANK_COPY_HELD    0x04u
#define MENU_BANK_COPY_PEEK    0x08u
```

**Comment block:**

```c
/*
 * Bank page state (+3 B SRAM1, S082 B2).
 *
 * What: the content mask (present and trigger steps) of the previewed Bank
 * slot, beside the existing present mask/slot/valid triple; and one flag byte:
 * Merge for Load, Merge for Save, COPY held, COPY hold threshold reached
 * (other view shown).
 * Why: the Bank LEDs show content, not presence (S082 P0 §4.3); Merge is a
 * per-page toggle on COPY (S1, Q3); the held view needs to know whether the
 * press became a hold.
 * Inputs: menu_bankLoadPreviewComplete(), menu_bankCopyPressed/Released/
 * HoldReached().
 * Outputs: menu_refreshLoadSceneLeds(), menu_bankOperationMasks(), labels.
 * Lifetime: static. Owner: menu.c.
 * Accessors: Bank page helpers in B2.2-B2.12.
 * Affiliates: filesystem_bankChildContentMask(), LED_COPY.
 */
```

### B2.2 `Core/Menu/menu.c` near `menu_residentPresentSceneMask()` (8855) — ADD helpers

```c
static uint8_t menu_bankPageActive(void);
static uint8_t menu_bankMergeOn(void);
static uint16_t menu_residentContentSceneMask(void);
static void menu_bankOperationMasks(uint16_t *act_mask, uint16_t *empty_mask);
```

- `menu_bankPageActive()`: `(LOAD_PAGE || SAVE_PAGE) && !menu_instrumentLoadActive && what == SAVE_TYPE_BANK`.
- `menu_bankMergeOn()`: the flag for the current page.
- `menu_residentContentSceneMask()`: bit N = `menu_residentPresentSceneMask()`
  bit ∧ `pat_sceneHasActiveSteps(N)`. **Factor out** of
  `menu_refreshPerfSceneLeds()` (8565-8566), which then calls it, so PERF and
  Bank cannot drift.
- `menu_bankOperationMasks()` implements the P0 §4.2 table:

```c
    uint16_t sel = menu_kitLoadSceneMask;
    if (menu_activePage == LOAD_PAGE) {
        if (menu_bankMergeOn()) {
            *act_mask = (uint16_t)(sel & menu_bankPreviewContentMask);
            *empty_mask = 0u;
        } else {
            *act_mask = (uint16_t)(sel & menu_bankLoadPreviewMask);
            *empty_mask = (uint16_t)~*act_mask;
        }
    } else {
        uint16_t present = bank_scenePresentMask();
        if (menu_bankMergeOn()) {
            *act_mask = (uint16_t)(sel & menu_residentContentSceneMask());
            *empty_mask = 0u;
        } else {
            *act_mask = (uint16_t)(sel & present);
            *empty_mask = (uint16_t)~*act_mask;
        }
    }
```

**Comment blocks:**

`menu_residentContentSceneMask()`:

```c
/*
 * Resident Scenes PERF would light (present and holding trigger steps).
 *
 * What: one shared rule for the PERF SEQ base state and the Bank page.
 * Why: "same as PERF" (S082 P0 §4.3, B2/B4) must not drift between two copies.
 * Inputs: BankData present mask (with the active-Scene fallback),
 * pat_sceneHasActiveSteps() per Scene.
 * Outputs: 16-bit mask.
 * Accessors: menu_refreshPerfSceneLeds(), menu_refreshLoadSceneLeds(),
 * menu_bankOperationMasks().
 * Affiliates: menu_residentPresentSceneMask(), PatternData.
 */
```

`menu_bankOperationMasks()`:

```c
/*
 * Turn the Bank page state into the operation's two masks (S082 P0 §4.2).
 *
 * What: act_mask = Scenes loaded/written; empty_mask = Scenes emptied (Load) or
 * removed from the slot (Save). Replace acts on all 16: a selected Scene with
 * a source child (Load) or present resident state (Save) is acted on, every
 * other Scene is emptied/removed (S7, S8, S3). Merge acts only on selected
 * Scenes with content and empties nothing (S8).
 * Why: one place computes the outcome table; filesystem receives explicit
 * masks and never infers the mode from the UI.
 * Inputs: menu_kitLoadSceneMask (selection), the preview present/content
 * masks (Load), BankData present mask and resident content (Save), Merge flag.
 * Outputs: *act_mask, *empty_mask.
 * Accessors: the OK dispatch (B2.10).
 * Affiliates: preset_loadBank(), preset_saveBank(), S17 (the active Scene stays
 * present when emptied - enforced in filesystem, B3.6).
 */
```

### B2.3 `Core/Menu/menu.c:8894-8927` — `menu_loadSaveSelectableSceneMask()` — MOD

**Change.** Both Bank pages return `menu_allPhysicalSceneMask()`. REM the
Load-Bank preview-mask branch (8913-8921) and the Save-Bank use of the present
mask.

```c
    if (menu_bankPageActive())
        return menu_allPhysicalSceneMask();
```

placed before `if (menu_activePage == SAVE_PAGE)`.

**Comment block:**

```c
/*
 * Every Scene button toggles on the Bank page (S2).
 *
 * What: all 16 SEQ buttons are selectable on Load:[Bank] and Save:[Bank],
 * including Scenes that are unlit (no content) or absent from the source.
 * Why: Replace acts on all 16 and a deselected Scene loads/saves empty (S7), so
 * deselecting an unlit Scene still changes the result.
 * Inputs: page/type. Outputs: 0xFFFF on the Bank page.
 * Accessors: menu_loadSceneButtonPressed(), menu_refreshLoadSceneLeds().
 * Affiliates: menu_bankOperationMasks().
 */
```

### B2.4 `Core/Menu/menu.c:8929-8954` — `menu_resetLoadSaveSceneSelection()` — MOD

**Change.** Both Bank pages reset to `0xFFFF` and clear their page's Merge
flag and the COPY flags. REM the `present_mask` default for Save Bank
(8944-8945) and the `0` default for Load Bank (8946-8948).

```c
    if (menu_bankPageActive()) {
        menu_kitLoadSceneMask = 0xFFFFu;
        menu_bankUiFlags = (uint8_t)(menu_bankUiFlags &
            (uint8_t)~(MENU_BANK_COPY_HELD | MENU_BANK_COPY_PEEK |
                       (menu_activePage == LOAD_PAGE ? MENU_BANK_MERGE_LOAD
                                                     : MENU_BANK_MERGE_SAVE)));
        led_setValue(0u, LED_COPY);
        return;
    }
```

**Comment block:**

```c
/*
 * Bank page selection default: all 16 Scenes, Merge off (S082 P0 §4.4, Q3).
 *
 * What: resets the Bank selection to 0xFFFF and turns this page's Merge off.
 * Why: Replace on all 16 is the default for both Load and Save; Merge starts
 * off at every reset boundary (entry, type change, Load<->Save, mode change,
 * a new Load source, a completed Save).
 * Inputs: page/type. Outputs: menu_kitLoadSceneMask, menu_bankUiFlags, LED_COPY.
 * Accessors: menu_loadSaveEnterTop(), the type-row handler,
 * menu_resetSaveParametersEx(), menu_requestBankChildPreview() (new Load source).
 * Affiliates: menu_bankOperationMasks(), menu_refreshLoadSceneLeds().
 */
```

### B2.5 `Core/Menu/menu.c:8956-9014` — `menu_refreshLoadSceneLeds()` — MOD

**Change.** ADD a Bank branch before the generic loop (after the
blink/value clear at 8979-8982). It returns, so the generic loop no longer
runs for Bank, and the Bank Load active-Scene blink (9010-9013) is removed
(Q5).

```c
    if (menu_bankPageActive()) {
        uint16_t lit;
        uint8_t held = (uint8_t)((menu_bankUiFlags & MENU_BANK_COPY_PEEK) != 0u);
        uint8_t preview_ok = (uint8_t)(menu_bankLoadPreviewValid &&
            menu_bankLoadPreviewSlot ==
                menu_currentPresetNr[SAVE_TYPE_BANK]);

        if (menu_activePage == LOAD_PAGE)
            lit = held ? menu_residentContentSceneMask()
                       : (preview_ok ? (uint16_t)(menu_kitLoadSceneMask &
                                       menu_bankPreviewContentMask) : 0u);
        else
            lit = held ? (preview_ok ? menu_bankPreviewContentMask : 0u)
                       : (uint16_t)(menu_kitLoadSceneMask &
                                    menu_residentContentSceneMask());
        for (scene_index = 0u; scene_index < 16u; scene_index++)
            led_setValue((uint8_t)((lit >> scene_index) & 1u),
                         (uint8_t)(LED_SEQ1 + scene_index));
        led_setValue((uint8_t)menu_bankMergeOn(), LED_COPY);
        return;
    }
```

**Comment block:**

```c
/*
 * Bank page SEQ LEDs (S082 P0 §4.3).
 *
 * What: steady LEDs, no blinks. Load: the selected source Scenes with content
 * (COPY held: the resident Bank as PERF lights it). Save: the selected
 * resident Scenes with content (COPY held: the selected Save slot's children
 * with content). LED_COPY mirrors this page's Merge flag.
 * Why: the matrix shows the result of the operation, i.e. what PERF will show
 * afterwards (B2, B4), and the held view shows the other side (B6).
 * Inputs: selection, preview masks (valid only for the shown slot), resident
 * content mask, COPY peek flag, Merge flag.
 * Outputs: LED_SEQ1..16 base values, LED_COPY.
 * Accessors: every Bank page state change (selection, preview completion,
 * COPY edges, Merge toggle, slot move).
 * Affiliates: menu_residentContentSceneMask(), menu_bankLoadPreviewComplete(),
 * led_flashGroup() press feedback (unchanged, in menu_loadSceneButtonPressed()).
 */
```

### B2.6 `Core/Menu/menu.c:9016-9122` — `menu_loadSceneButtonPressed()` — MOD

**Change.**
- After the `scene_index` bound checks (9029-9036), ADD
  `if (menu_bankPageActive() && (menu_bankUiFlags & MENU_BANK_COPY_HELD)) return 1u;`
- Bank Save branch (9087-9091) and the final else (9113-9117): REM the
  `if (next != 0u)` guard on the Bank page, so all-deselected is legal. Keep
  the guard for Kit/Scene/Pattern Load (unchanged).

```c
        if (menu_saveOptions.what == SAVE_TYPE_BANK) {
            menu_kitLoadSceneMask = (uint16_t)(menu_kitLoadSceneMask ^ bit);
        } else {
```

**Comment blocks:**

COPY-held guard: What: SEQ presses are consumed with no change while COPY is
held. Why: B6/Q2, the held view is read-only. Inputs: Bank page, COPY held
flag. Outputs: handled, no mutation. Accessors: ButtonHandler SEQ press.
Affiliates: `menu_bankCopyPressed()`.

Free toggle: What: on the Bank page any Scene toggles, including the last
selected one. Why: all 16 deselected is a valid Replace (empties the Bank /
slot, Q11); unlit Scenes toggle with flash feedback (S2). Inputs: bit.
Outputs: `menu_kitLoadSceneMask`. Affiliates: `menu_bankOperationMasks()`.

### B2.7 `Core/Menu/menu.c:8070-8115` — `menu_requestBankLoadPreview()` → rename `menu_requestBankChildPreview()` — MOD

**Change.**
- Rename. Update callers at 7817 and 6863, and the forward declaration at 1925.
- REM `menu_kitLoadSceneMask = 0u;` (8089).
- A Load-page call with a slot different from the previous preview slot
  resets the selection and Load Merge (B8) via
  `menu_resetLoadSaveSceneSelection()`.
- Clear `menu_bankPreviewContentMask` with the present mask.

**Comment block** (replaces the existing one):

```c
/*
 * Request the child preview for the highlighted Bank slot (Load source or Save
 * slot).
 *
 * What: clears preview validity so stale LEDs disappear, then posts the
 * filesystem child scan (presence + content). A missing root slot publishes
 * empty masks at once. On Load, moving to a different source slot resets the
 * selection and Merge (B8); on Save the selection is kept across slot moves (B7).
 * Why: both Bank pages need the slot's content (Load normal view, Save held
 * view), and Bank OK must be gated until the source masks are valid.
 * Inputs: zero-based Bank slot, page.
 * Outputs: preview triple + content mask invalidated; menu_storageBusy while
 * the scan runs (no `...`).
 * Accessors: menu_requestCurrentLoadSaveSelection(), menu_libraryIndexLoadComplete().
 * Affiliates: filesystem_requestScanBankScenes(), menu_bankLoadPreviewComplete().
 */
```

### B2.8 `Core/Menu/menu.c:8021-8066` — `menu_bankLoadPreviewComplete()` — MOD

**Change.**
- Accept `SAVE_PAGE` as well as `LOAD_PAGE` (8056).
- Store `menu_bankPreviewContentMask = filesystem_bankChildContentMask();`.
- REM `menu_kitLoadSceneMask = menu_bankLoadPreviewMask;` (8063). The
  selection is no longer derived from the source.
- On a stale coordinate (8047-8053) keep the selection; only invalidate the
  preview.

**Comment block:** What: publishes the completed preview's present and content
masks for the still-shown Bank slot on either Bank page, then repaints LEDs.
Why: the selection is now independent of the source (S7); the preview only
feeds LEDs and the operation masks. Inputs: filesystem result, requested
slot, page. Outputs: preview triple + content mask, LEDs. Accessors:
filesystem completion callback. Affiliates: `menu_refreshLoadSceneLeds()`,
`menu_bankOperationMasks()`.

### B2.9 Preview on the Save page — MOD

- `menu.c:6862-6863` (`menu_requestCurrentLoadSaveSelection()`): change the
  condition to `if (what == SAVE_TYPE_BANK) menu_requestBankChildPreview(slot);`
  (both pages).
- `menu.c:7789-7794` (`menu_libraryIndexLoadComplete()`): drop the
  `menu_activePage == LOAD_PAGE` term from `continue_bank_preview` (keep
  `LOAD_PAGE || SAVE_PAGE`).

Comment: "Save:[Bank] previews the selected slot for the COPY-held view (B6)."

### B2.10 `Core/Menu/menu.c:13388-13436` — OK dispatch, Bank cases — MOD

**Change.**
- Before the `switch`, ADD: if `menu_bankPageActive() && (menu_bankUiFlags & MENU_BANK_COPY_HELD)`,
  ignore the click (Q2).
- Save Bank (13388-13402):

```c
                case SAVE_TYPE_BANK: {
                    uint16_t act, empty;
                    menu_bankOperationMasks(&act, &empty);
                    if (menu_bankMergeOn() && act == 0u)
                        break;                              /* Q11 */
                    ...trace unchanged...
                    if (preset_saveBank(menu_currentPresetNr[SAVE_TYPE_BANK],
                                        act, empty, 0u))
                        commandAccepted = 1u;
                    break; }
```

- Load Bank (13430-13435): the same, with
  `preset_requestBackgroundBankLoad(slot, act, empty, PRESET_BANK_ORIGIN_MENU)`
  (Phase C). Until Phase C lands, use `preset_loadBank(slot, act, empty, 0u)`.
  Also require `menu_bankLoadPreviewValid` for the shown slot; otherwise the
  click is ignored, as the current input gate does.

**Comment block:**

```c
/*
 * Bank OK: compute the outcome masks and post the operation (S082 P0 §4.2).
 *
 * What: Replace posts act/empty for all 16 Scenes; Merge posts only act and is
 * refused when nothing qualifies (Q11). A click while COPY is held is ignored
 * (Q2). Load uses the background request so a running transport keeps playing
 * (Phase C); Save uses the two-mask Bank Save.
 * Why: the UI owns the selection and mode; filesystem executes exactly the
 * masks it is given.
 * Inputs: Bank page state, preview validity, menu_currentPresetNr[SAVE_TYPE_BANK].
 * Outputs: an accepted Preset request -> menu_beginLoadSaveCommand().
 * Accessors: menu_handleLoadSaveMenu() click path.
 * Affiliates: menu_bankOperationMasks(), preset_saveBank(),
 * preset_requestBackgroundBankLoad(), preset_loadBank().
 */
```

### B2.11 Labels — `Core/Menu/menu.c:11225-11228` (Save) and `11246` (Load) — MOD

```c
        if (menu_bankPageActive() && menu_bankMergeOn() &&
            menu_currentSaveWouldOverwrite())
            memcpy(&editDisplayBuffer[1][14], "MR", 2);
        else if (menu_currentSaveWouldOverwrite())
            memcpy(&editDisplayBuffer[1][14], "OW", 2);
        else
            memcpy(&editDisplayBuffer[1][14], menuText_ok, 2);
```

Load:

```c
            memcpy(&editDisplayBuffer[1][14],
                   (menu_bankPageActive() && menu_bankMergeOn()) ? "mr"
                                                                 : menuText_ok,
                   2);
```

**Comment block:** What: Merge labels: Load `mr`, Save `MR` on an occupied slot
(`ok` on a blank one). Why: B9/Q21; Merge never overwrites the whole slot.
Inputs: Bank page, Merge flag, slot occupancy. Outputs: LCD cells 14-15.
Accessors: `menu_repaintLoadSavePage()`. Affiliates:
`menu_currentSaveWouldOverwrite()`, `menu_paintLoadSaveConfirmation()` (`...`
and `NN.` progress are unchanged).

### B2.12 COPY handling — Menu side — ADD (near `menu_loadSceneButtonPressed()`)

```c
uint8_t menu_bankCopyPressed(void);
void menu_bankCopyHoldReached(void);
void menu_bankCopyReleased(void);
void menu_bankCopyForceRelease(void);
uint8_t menu_bankCopyArmed(void);
```

- `menu_bankCopyPressed()`: returns 0 unless `menu_bankPageActive()` and no
  command is active; sets COPY_HELD and returns 1.
- `menu_bankCopyHoldReached()`: sets COPY_PEEK and refreshes LEDs (the other
  view).
- `menu_bankCopyReleased()`: if PEEK is clear (a tap), toggles this page's
  Merge flag and repaints the label. Then clears HELD/PEEK and refreshes LEDs.
- `menu_bankCopyForceRelease()`: clears HELD/PEEK with no toggle (MODE exit,
  event overflow).
- `menu_bankCopyArmed()`: HELD and not PEEK (used by the ButtonHandler tick).

Declare all five in `Core/Menu/menu.h` (near line 710).

**Comment block** (shared, above the group in `menu.c`; header: What /
Inputs / Outputs / Accessors):

```c
/*
 * COPY on the Bank page: tap toggles Merge, hold shows the other view (S1).
 *
 * What: a press marks COPY held (SEQ and OK are then consumed); reaching the
 * common hold threshold shows the other view (Load: resident RAM; Save: the
 * selected slot); a release before the threshold toggles this page's Merge
 * flag; a release after it only returns the normal view. A forced release
 * (MODE exit, event overflow) never toggles.
 * Why: the user asked for a toggle and a momentary view on one button with
 * the same tap/hold timing as other buttons (BUTTON_TIMEOUT = 200 ms). COPY is
 * otherwise refused in Load/Save (cc_modeAllowed()), so no copy/clear gesture
 * is displaced.
 * Inputs: ButtonHandler edges and its hold timer; Bank page state.
 * Outputs: menu_bankUiFlags, LEDs, label repaint.
 * Accessors: buttonHandler.c BUT_COPY press/release, buttonHandler_tick(),
 * buttonHandler_processEvents() overflow path, menu_switchPage() exit.
 * Affiliates: menu_refreshLoadSceneLeds(), menu_bankMergeOn(),
 * copyClearSession.c (unaware; never called on the Bank page).
 */
```

### B2.13 COPY handling — ButtonHandler — MOD/ADD `Core/Hardware/frontPanel/buttonHandler.c`

**(a) Storage — ADD near `bh_clearHeld` (170):**

```c
static uint16_t bh_bankCopyPressTick;
```

Comment: What: press tick of a Bank-page COPY (+2 B SRAM1, S15). Why: tap/hold
discrimination at `BUTTON_TIMEOUT` without reusing the step long-press timer
(`buttonHandler_buttonTimer`), which belongs to SEQ steps. Owner:
ButtonHandler. Accessors: BUT_COPY press, `buttonHandler_tick()`.

**(b) Press — MOD `case BUT_COPY:` (2852-2875).** First statement:

```c
        if (bh_state.selectButtonMode == SELECT_MODE_LOAD_SAVE &&
            menu_bankCopyPressed()) {
            bh_bankCopyPressTick = time_sysTick;
            break;
        }
```

Comment: What: on the Bank page COPY is the Merge/peek button and never
reaches copy/clear or live erase (SHIFT+COPY included). Why: S1; R5
(isolation from `copyClear_copyPressed()` and the live-erase chord). Inputs:
mode, Bank page. Outputs: Menu COPY state, press tick. Affiliates:
`menu_bankCopyPressed()`.

`bh_clearHeld` is **not** set on this path, so the live-erase release logic at
3356 is unaffected.

**(c) Release — MOD `processRelease()` (3068-3084).** First statement of the
`BUT_COPY` branch:

```c
        if (menu_bankCopyArmed() || menu_bankCopyPeekActive()) {
            menu_bankCopyReleased();
            return;
        }
```

(`menu_bankCopyPeekActive()` is a sixth trivial accessor; or have
`menu_bankCopyReleased()` return 0 when not held, and branch on that.)

Comment: What: a Bank-page COPY release ends the gesture in Menu (tap →
toggle, hold → view back). Why: the copy/clear release must not run for a
press it never armed. Affiliates: `menu_bankCopyReleased()`.

**(d) Hold — ADD in `buttonHandler_tick()` after the REC-hold block (1253):**

```c
    if (menu_bankCopyArmed() &&
        (uint16_t)(time_sysTick - bh_bankCopyPressTick) >= BUTTON_TIMEOUT)
        menu_bankCopyHoldReached();
```

Comment: What: promotes a held Bank-page COPY to the other view at the common
hold threshold. Why: S1. Inputs: press tick, Menu armed state. Outputs: the
Menu peek. Affiliates: `BUTTON_TIMEOUT`, `menu_bankCopyHoldReached()`.

**(e) Overflow — MOD `buttonHandler_processEvents()` (3486):** after
`copyClear_eventOverflow()`, ADD `menu_bankCopyForceRelease();`.
Comment: "a lost COPY release must not leave the Bank page held".

### B2.14 `Core/Menu/menu.c:15816-15827` — `menu_switchPage()` Load/Save exit — MOD

ADD `menu_bankCopyForceRelease(); menu_bankUiFlags = 0u; led_setValue(0u, LED_COPY);`
inside the exit block. Comment: What: leaving Load/Save ends COPY and both
Merge flags. Why: the Q3/Q13 retention boundaries. Affiliates:
`menu_resetLoadSaveSceneSelection()`.

`menu.c:15973-15974` (LOAD_PAGE entry) sets
`menu_kitLoadSceneMask = 1u << active`. It is followed by
`menu_resetSaveParameters()`, which now resets the Bank page to `0xFFFF`
(B2.4). No change is needed, but add a DOC note that the Bank page overrides
this default.

### B2.15 `Core/Menu/menu.c:16138-16167` and `:259-285` — retention after a command — MOD

**Change.**
- Rename the body of `menu_resetSaveParameters()` to
  `static void menu_resetSaveParametersEx(uint8_t after_command)`.
- `menu_resetSaveParameters()` (public, `menu.h`) becomes a wrapper with
  `after_command = 0`.
- `menu_finishLoadSaveCommand()` (284) calls
  `menu_resetSaveParametersEx(1u)`.
- In the Ex body, replace the unconditional
  `menu_resetLoadSaveSceneSelection();` (16165) with:

```c
    if (!(after_command && menu_activePage == LOAD_PAGE &&
          menu_saveOptions.what == SAVE_TYPE_BANK))
        menu_resetLoadSaveSceneSelection();
```

**Comment block:**

```c
/*
 * Keep the Bank Load selection and Merge after a completed Load (Q14, B8).
 *
 * What: a command completion on Load:[Bank] returns the cursor to the type row
 * but keeps the matrix selection and Load Merge; every other completion
 * (including Save:[Bank], B7) and every non-command reset behaves as before.
 * Why: the user keeps a Bank Load selection until a different source or
 * menu/mode is chosen; after a load the source slot is unchanged.
 * Inputs: after_command, page, type.
 * Outputs: cursor reset; selection reset unless retained.
 * Accessors: menu_finishLoadSaveCommand() (after_command = 1);
 * menu_resetSaveParameters() wrapper (0).
 * Affiliates: menu_resetLoadSaveSceneSelection(), menu_requestBankChildPreview().
 */
```

### B2.16 Type-row resets — `Core/Menu/menu.c:13481, 13517, 13526` — no change

These already call `menu_resetLoadSaveSceneSelection()`. With B2.4 a type
change resets the Bank page (Q13). Note in the commit only.

---

## Phase B3 — Bank Load/Save operation semantics (S3–S13, S17–S25)

### B3.1 Request API — `Core/Bank/Scene/Preset/presetManager.h:186-191` and `.c:3069-3135` — MOD

```c
#define PRESET_BANK_LOAD_BOOT  0x01u
uint8_t preset_loadBank(uint16_t presetNr, uint16_t act_mask,
                        uint16_t empty_mask, uint8_t flags);
uint8_t preset_saveBank(uint16_t presetNr, uint16_t act_mask,
                        uint16_t empty_mask, uint8_t force_save);
```

- `preset_loadBank()` stores `pm_kit_request_scene_mask = act_mask` and
  passes the masks and flags to `filesystem_requestLoadBank()`.
- `preset_saveBank()` passes `empty_mask` to `filesystem_requestSaveBank()`.

Callers:
- `main.c:1046`: `preset_loadBank(boot_bank_slot, 0xffffu, 0u, PRESET_BANK_LOAD_BOOT)`.
- `menu.c` B2.10.
- Search for every other `preset_loadBank(` (only the boot loader and Menu at
  `8b1b647`).

**Header comment block (replaces 170-185):**

```c
/*
 * Load and save root Bank folders with explicit outcome masks (S082 B3).
 *
 * preset_loadBank(): act_mask = Scenes loaded from their Bank-local child;
 * empty_mask = Scenes reset to the empty Scene (Replace). Scenes in neither
 * mask are kept (Merge). A requested child that is missing is emptied when the
 * request is a Replace (empty_mask != 0), kept otherwise. PRESET_BANK_LOAD_BOOT
 * (boot only) also applies the manifest's active_scene and realigns playback;
 * a runtime load never changes the active or viewed Scene (S9-S11).
 * preset_saveBank(): act_mask = resident Scenes written to their child;
 * empty_mask = children removed from the slot; others untouched.
 * Inputs: root slot, masks, flags/force. Outputs: accepted/refused; completion
 * through PRESET_OP_BANK_LOAD / PRESET_OP_BANK_SAVE.
 * Accessors: Menu Bank OK, main.c boot, preset_requestBackgroundBankLoad().
 * Affiliates: filesystem_requestLoadBank(), filesystem_requestSaveBank().
 */
```

REM `preset_loadFirstAvailableSceneOrKit()`'s use after a runtime empty Bank
(see B3.12). The function itself stays for boot (`main.c:1087, 1104`).

### B3.2 `Core/Hardware/SD/filesystem.c:1343-1349` — ADD op fields

```c
static uint16_t op_bank_scene_empty_mask = 0u;
static uint16_t op_bank_save_written_mask = 0u;
static uint8_t op_bank_flags = 0u;
#define FS_BANK_FLAG_BOOT            0x01u
#define FS_BANK_FLAG_REPLACE         0x02u
#define FS_BANK_FLAG_AUTOSAVE_YIELD  0x04u
```

**Comment block:**

```c
/*
 * Bank operation outcome masks and flags (+5 B SRAM1, S082 B3; see §9).
 *
 * What: op_bank_scene_empty_mask = Scenes emptied by a Bank Load or children
 * removed by a Bank Save; op_bank_save_written_mask = every child a Bank Save
 * treats as written (including S058 clean-skipped children), used by the
 * Merge edit-mask rule; op_bank_flags: BOOT (apply the manifest active_scene),
 * REPLACE (missing requested children are emptied), AUTOSAVE_YIELD (Scene 17
 * request: the AutoSave drain closes at its next safe point).
 * Why: the S082 outcome table needs explicit masks, and boot keeps its own
 * active-Scene rule (S11).
 * Lifetime: static, reset by filesystem_start(). Owner: filesystem.c.
 * Accessors: filesystem_requestLoadBank/SaveBank(), Bank Load/Save ticks.
 * Affiliates: preset_loadBank(), preset_saveBank(),
 * bank_mergeVoiceEditEntry().
 */
```

### B3.3 `Core/Hardware/SD/filesystem.c:30210-30279` + `filesystem.h` — `filesystem_requestLoadBank()` — MOD

```c
bool filesystem_requestLoadBank(uint16_t slot, uint16_t act_mask,
                                uint16_t empty_mask, uint8_t flags,
                                fs_completion_cb_t cb);
```

- Refuse only when `(act_mask | empty_mask) == 0` (MOD 30232): a Replace with
  nothing selected is legal (Q11).
- Store `op_bank_scene_empty_mask = empty_mask & ~act_mask`.
- `op_bank_flags = flags | (empty_mask ? FS_BANK_FLAG_REPLACE : 0)`.
  Assign these **after** `filesystem_startRepairBankNames()` succeeds,
  because `filesystem_start()` resets op state. Check that the repair hand-off
  does not call `filesystem_start()` again; if it does, carry the fields
  through it as `op_bank_scene_load_mask` is carried.

**Comment block** (replaces 217-224): What: validates and starts a Bank Load
with explicit act/empty masks and flags. Why: S082 outcome table; an all-empty
Replace is a valid request. Inputs: slot, masks, flags, callback. Outputs:
accepted/refused; op masks. Accessors: `preset_loadBank()`. Affiliates:
`filesystem_loadBankDirectory_tick()` phases 17/20/98.

### B3.4 `Core/Hardware/SD/filesystem.c:14286-14287` — Bank Load phase 17 — MOD

**Change.**

```c
        if (op_bank_flags & FS_BANK_FLAG_REPLACE)
            op_bank_scene_empty_mask = (uint16_t)(op_bank_scene_empty_mask |
                (op_bank_scene_load_mask & (uint16_t)~op_bank_child_present_mask));
        op_bank_scene_load_mask =
            (uint16_t)(op_bank_scene_load_mask & op_bank_child_present_mask);
```

- Runtime (no BOOT flag): REM the active-Scene re-pick (14298-14311). Keep it
  under `if (op_bank_flags & FS_BANK_FLAG_BOOT)`.
- Before both the empty-Bank branch (14312) and the child loop (14394): if
  `op_bank_scene_empty_mask != 0`, go to the new phase 98 and return to the
  same decision afterwards.

**Comment block:**

```c
/*
 * Resolve the effective load/empty sets after child discovery (S082 B3).
 *
 * What: a Replace request empties every requested Scene whose child is absent;
 * a Merge request keeps it. The load set is intersected with the discovered
 * children as before. Emptying runs first (phase 98) so the per-child loop and
 * the metadata commit see final resident state.
 * Why: Replace "uses exactly what is there" (S8); the source Bank's missing
 * child is an empty Scene. The manifest active_scene is applied only at boot
 * (S11), so the runtime path no longer re-picks it.
 * Inputs: op_bank_scene_load_mask, op_bank_child_present_mask, op_bank_flags.
 * Outputs: op_bank_scene_load_mask, op_bank_scene_empty_mask, next phase.
 * Accessors: filesystem_loadBankDirectory_tick().
 * Affiliates: phase 98, phase 20 commit, the empty-Bank branch.
 */
```

### B3.5 Bank Load phase 98 (new) — ADD in `filesystem_loadBankDirectory_tick()`

```c
    case 98: /* EMPTY THE REPLACE-ONLY SCENES BEFORE CHILD LOADS */
        if (!filesystem_patternServiceReady(op_bank_scene_empty_mask))
            return;
        filesystem_bankEmptyScenes(op_bank_scene_empty_mask);
        filesystem_patternServiceFinish(op_bank_scene_empty_mask);
        op_phase = op_bank_scene_load_mask ? 99u : 100u;
        return;
```

Phases 99 and 100 re-enter the existing "first child" (14394-14416) and
empty-Bank (14312-14392) code, which is factored into two static helpers so
it can be reached from both places.

**Comment block:**

```c
/*
 * Empty every Replace-only Scene (S082 B3, S7, S17).
 *
 * What: waits until the Pattern service released the selected Scenes, resets
 * each to the empty Scene (settings, Effect and Kit defaults, Pattern region
 * init), then reopens the service. HCNAMES rows are staged in the same helper.
 * Why: Replace loads deselected or missing Scenes empty. The Pattern region is
 * replaced directly, so it uses the same quiesce/finish bracket as a Pattern
 * load (filesystem_patternServiceReady()).
 * Inputs: op_bank_scene_empty_mask.
 * Outputs: resident Scenes emptied; register rows staged.
 * Accessors: Bank Load phase 17.
 * Affiliates: filesystem_bankEmptyScenes(), scene_resetResidentToEmpty(),
 * pat_initScene(), filesystem_patternServiceReady()/Finish().
 */
```

### B3.6 Shared empty primitive — ADD `scene_resetResidentToEmpty()` (SceneData) and `filesystem_bankEmptyScenes()` (filesystem); MOD `filesystem_bootReaderEmptyScene()`

**SceneData** — `Core/Bank/Scene/SceneData.c` (after `scene_resetKitToDefaults()`)
and `.h:648`:

```c
void scene_resetResidentToEmpty(uint8_t scene_index);
```

Body: the data half of `filesystem_bootReaderEmptyScene()`
(`filesystem.c:29034-29051`): memset, `scene_busCompDefaults()`, MIDI channels,
per-slot audio/send/fader defaults, `instrumentManager_resetSlot()` with the
initial types, and `scene_effectRecordDefaults()`. It does **not** touch
Pattern, names, present bit or masks.

**Comment block:**

```c
/*
 * Reset one resident Scene's retained data to the empty Scene.
 *
 * What: settings (bus compressor, MIDI, per-voice routing/send/fader), the six
 * Kit slots (default types) and the Effect record are reset to the values a
 * fresh boot uses. Pattern, HCNAMES, present bit and edit masks belong to
 * their owners and are handled by the caller.
 * Why: one data definition of "empty Scene" for the boot reader (Case 3) and
 * the S082 Replace Bank Load (R7); before S082 the boot reader owned a private
 * copy.
 * Inputs: resident Scene index 0..15 (Scene 17 is never emptied).
 * Outputs: scenes[index] rewritten; no AutoSave mark (callers mark).
 * Accessors: filesystem_bootReaderEmptyScene(), filesystem_bankEmptyScenes().
 * Affiliates: scene_busCompDefaults(), instrumentManager_resetSlot(),
 * scene_effectRecordDefaults(), pat_initScene() (caller), copy/clear
 * `clear scene` (keeps its own commit-funnel path: it must mark AutoSave per
 * field and apply runtime; see COPYCLEAR_UTILITIES.md).
 */
```

**filesystem** — MOD `filesystem_bootReaderEmptyScene()` (29022-29070) to call
`scene_resetResidentToEmpty()` + `pat_initScene()`, then keep its own
HCNAMES `?`+R rows (boot Case 3 semantics unchanged).

**filesystem** — ADD `static void filesystem_bankEmptyScenes(uint16_t mask)`:

```c
    for (n = 0; n < 16; n++) if (mask & bit(n)) {
        scene_resetResidentToEmpty(n);
        pat_initScene(n);
        filesystem_stageEmptySceneRows(n);       /* B3.9 */
        bank_invalidateSdCleanScene(n);
        filesystem_patternAutosaveOnLoad(n);
        autosave_markSceneWithPatternDirty(n);   /* no-op if non-present */
    }
```

**Comment block:** What: empties every Scene in the mask (data, Pattern,
register rows, clean bit, AutoSave scope). Why: Replace Bank Load (S7).
Inputs: mask. Outputs: emptied Scenes. Accessors: Bank Load phase 98.
Affiliates: `scene_resetResidentToEmpty()`, `filesystem_stageEmptySceneRows()`,
`bank_invalidateSdCleanScene()`, `autosave_markSceneWithPatternDirty()`.

### B3.7 `Core/Hardware/SD/filesystem.c:14459-14640` — Bank Load phase 20 commit — MOD

**Change (in order inside the commit block, 14491-14640):**

1. Capture `old_bank_source = filesystem_residentSource(FS_IDENTITY_BANK_ROW);`
   **before** `filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)` (14509).
2. ADD `filesystem_stageKeptSceneSources(kept_mask, old_bank_source)` where
   `kept_mask = ~(op_bank_scene_load_mask | op_bank_scene_empty_mask)` (B3.9).
3. MOD the present-mask merge (14523-14540):

```c
        {
            uint16_t present = (uint16_t)(
                (bank_scenePresentMask() &
                 (uint16_t)~(op_bank_scene_load_mask | op_bank_scene_empty_mask)) |
                (op_bank_scene_load_mask & (uint16_t)~op_bank_scene_failed_mask));
            uint8_t active = scene_backgroundHomeIndex();   /* Phase C; = scene_getActiveIndex() before C */
            if (active < 16u)
                present = (uint16_t)(present | (uint16_t)(1u << active));  /* S17 */
            if (!bank_setScenePresentMask(present))
                autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);
        }
```

4. MOD the edit-mask restore loop (14556-14565): call
   `bank_mergeVoiceEditMasksAfterLoad(op_bankset_state.scene_mask_voice_edit,
   op_bankset_state.seen_scene_mask_voice_edit, op_bank_scene_load_mask,
   op_bank_scene_empty_mask)` (B3.13).
5. Gate `bank_selectActiveSceneForEditMask(op_bank_active_scene)` (14555),
   `scene_selectActive()` (14579), `seq_alignActivePatternToScene()` (14618),
   `menu_setShownPattern()` and `menu_setPlayedPattern()` (14619-14620) under
   `if (op_bank_flags & FS_BANK_FLAG_BOOT)`.
6. ADD for the runtime path (else branch): assert agreement
   `seq_activePattern == scene_getActiveIndex() || scene_backgroundExists()`
   and `menu_getViewedPattern()` unchanged. On mismatch, record a DEV trace
   only (R12); never realign.
7. Replace the comment at 14581-14617 (DOC).

**Comment block** (replaces 14581-14617):

```c
/*
 * Runtime Bank Load never moves the active or viewed Scene (S9-S11, D26).
 *
 * What: only a boot load (FS_BANK_FLAG_BOOT) applies the manifest active_scene
 * and realigns SceneData, playback (seq_alignActivePatternToScene()) and the
 * STEP/PERF mirrors. A runtime load leaves scene_getActiveIndex(),
 * seq_activePattern, the per-track map and menu_shownPattern exactly as they
 * were (or on Scene 17 during a background load); Menu re-applies the active
 * Scene only when its content was replaced (S25).
 * Why: the user's Scene is never changed by a load. The S054 reason for this
 * realign (playback reading Scene 0 after a boot manifest chose another Scene)
 * applies only at boot, where these mirrors are still BSS-zero; at runtime
 * the three already agree because only a user Scene change moves them.
 * Inputs: op_bank_flags, op_bank_active_scene (boot only).
 * Outputs: boot: the manifest Scene becomes active; runtime: nothing.
 * Accessors: Bank Load phase 20.
 * Affiliates: seq_alignActivePatternToScene(), menu_setShownPattern(),
 * menu_setPlayedPattern(), SCENE_LOAD_PAT_RESTORE.md, MEMORY.md known bug
 * "runtime Bank Load can switch the playing Scene" (closed by this change).
 */
```

**Comment block** for the present-mask change (replaces 14510-14522):

```c
/*
 * Resident availability after the load is exactly what the outcome left.
 *
 * What: Scenes outside the load and empty sets keep their bit; loaded children
 * set theirs (failed children excepted); emptied Scenes clear theirs, except
 * the active Scene, which always stays present (S17).
 * Why: Replace "uses exactly what is there" (S8); the former OR kept stale
 * bits for Scenes the Replace emptied. The active Scene stays editable and
 * AutoSaved even when emptied.
 * Inputs: load/empty/failed masks, active (home) Scene.
 * Outputs: BankData present mask, with an explicit AutoSave mark on a no-op.
 * Accessors: Bank Load phase 20 and the empty-Bank branch.
 * Affiliates: bank_setScenePresentMask(), AUTOSAVE.md scene_present_mask (rule
 * changed from OR to assignment, Phase D).
 */
```

### B3.8 Empty-Bank branch — `Core/Hardware/SD/filesystem.c:14312-14392` — MOD

Apply the same items 1–6 as B3.7 to this branch:
- present mask (REM "preserve", 14329-14353);
- kept-Scene sources;
- the mask merge (14361-14370);
- the boot-gated active select (14360).

REM the comment "Preserve the existing resident Scene availability", because
a Replace of an empty Bank empties all 16 (Q11).

### B3.9 HCNAMES source tokens — `filesystem.h:856-873`, `filesystem.c:182, 5836, 5889, 5978, 22202` — ADD/MOD

**(a) `filesystem.h` — ADD after 870:**

```c
#define FS_RESIDENT_SOURCE_BANK_CHILD_BASE 0x1000u   /* + Bank slot 0..999 */
#define FS_RESIDENT_SOURCE_BANK_CHILD_END  0x13E8u
#define FS_RESIDENT_SOURCE_EMPTY           0x1ffbu
```

**Comment block:**

```c
/*
 * Scene-row provenance tokens added in S082 (no compatibility, S13).
 *
 * BANK_CHILD_BASE + NNN (`bNNN` on card): this Scene's content came from child
 * NN (the row's own Scene index) of root Bank NNN, which is not the Bank in
 * row 0 - written for Scenes a Merge Load kept, and for resident Scenes a Bank
 * Save did not write.
 * EMPTY (`x` on card): the Scene was intentionally emptied by a Replace Bank
 * Load (or is non-present at a Replace Bank Save); boot treats it as an empty
 * Scene with no notice.
 * Both are valid only on Scene rows 1..16. The values sit inside the 13-bit
 * value field, above the direct-slot range (0..999) and below the existing
 * specials (0x1ffc..0x1fff), so the REFRESHED (0x2000) and DIRTY (0x8000)
 * flags are untouched.
 * Affiliates: filesystem_residentSourceValid(), _parseResidentSourceToken(),
 * _formatResidentNameLine(), _resolveResidentSource(), the boot reader,
 * AutoSave source fields (carried as plain uint16_t values),
 * tools/verify_bank_autosave.py.
 */
```

**(b) `filesystem.c:5836-5848` `filesystem_residentSourceValid()` — MOD:**
accept BANK_CHILD and EMPTY only when `row >= 1 && row <= 16`.

**(c) `filesystem.c:5978-6010` `filesystem_parseResidentSourceToken()` — MOD:**
parse `x` → EMPTY; `bNNN` → BASE+NNN. Strict: exactly `b` + 3 digits.

**(d) `filesystem.c:22256-22263` `filesystem_formatResidentNameLine()` — MOD:**
EMPTY → `x`; BANK_CHILD → `b` + three digits. Neither carries an Instrument
type field: they are Scene rows only.

**(e) `filesystem.c:5889-5935` `filesystem_resolveResidentSource()` — MOD:**
treat BANK_CHILD and EMPTY as terminal sources (like a direct slot) and return
them with `*resolved_row = row`.

**(f) ADD `static void filesystem_stageEmptySceneRows(uint8_t scene)`:**
- Scene row ← EMPTY, name blank.
- Kit, six Instrument, Pattern and Effect rows ← INHERIT, names blank.
- Set the refreshed flag on all nine rows (`filesystem_setResidentRefreshed()`).
- Instrument rows keep the type token of the reset slot (default types), as
  the formatter derives it.

**(g) ADD `static void filesystem_stageKeptSceneSources(uint16_t kept, uint16_t old_bank_source)`:**
for each kept Scene whose Scene-row source is INHERIT, set
`BANK_CHILD_BASE + old_bank_source` when `old_bank_source < 1000`, else
UNKNOWN. Other sources are unchanged.

**Comment blocks:**

`filesystem_stageEmptySceneRows()`: What: stages the register image of an
emptied Scene (`x` Scene row, inheriting blank child rows, refreshed). Why: S13
accurate provenance; boot must not resolve an emptied Scene to the source
Bank's child (which may exist when it was deselected). Inputs: Scene index.
Outputs: staged rows in `fs_resident_source[]` and `hcnames_name_mirror[]`,
written by the existing phase-85 stream. Accessors:
`filesystem_bankEmptyScenes()`, Bank Save phase 45 (non-present). Affiliates:
`filesystem_setResidentSource()`, `filesystem_cacheResidentName()`.

`filesystem_stageKeptSceneSources()`: What: makes a kept Scene's inherited
provenance explicit (`b<old Bank>`) before row 0 moves to the new Bank. Why: a
`-` Scene row would otherwise claim the new Bank's same-numbered child (S4,
S13, R14). Inputs: kept mask, old row-0 source. Outputs: staged Scene-row
sources. Accessors: Bank Load phase 20 / empty branch, Bank Save phase 45.
Affiliates: `FS_RESIDENT_SOURCE_BANK_CHILD_BASE`.

### B3.10 Boot reader — `filesystem.c:27906-27938` `filesystem_bootReaderEnterSceneFolder()` and `29133-29300` `filesystem_bootReaderEvaluateScene()` — MOD

**(a) `filesystem_bootReaderEvaluateScene()`** at the Case-2 dispatch (29235):
- `resolved == FS_RESIDENT_SOURCE_EMPTY` → `filesystem_bootReaderEmptyScene()`
  data reset **without** the `case3_scene_mask` notice, keep the `x` rows, and
  return 1.
- `resolved` in the BANK_CHILD range is accepted like a direct slot for index
  0..8 and passed to the narrow loaders with `resolved_row` = the Scene row.

**(b) `filesystem_bootReaderEnterSceneFolder()`** ADD a BANK_CHILD branch:
- enter `Bank/`;
- find `NNN <name>` for the decoded slot through the Bank `.hcindex` cache
  (`filesystem_cachedLibraryName(FS_NAME_CACHE_BANK, slot)`) when
  `fs_list_cache_kind == FS_NAME_CACHE_BANK`; boot reloads `/Bank/.hcindex`
  before Bank selection (MEMORY.md). If the cache is not Bank, the row is
  unresolvable (Case 3, as today);
- then the child `NN <scene name>`.

**(c) `filesystem_bootReaderEnterKitFolder()`** (27951-27978): a BANK_CHILD
Scene-row source resolves through (b).

**Comment block (b):**

```c
/*
 * Resolve a `bNNN` Scene source to its physical Bank child (S082 B3).
 *
 * What: enters Bank/NNN <name>/NN <scene name>/ for a Scene whose content came
 * from a Bank other than row 0's, using the boot-resident Bank index for the
 * root folder name and the register's Scene name for the child.
 * Why: Merge Load keeps Scenes from the previous Bank; their provenance stays
 * resolvable for the HCNAMES-authoritative boot path (S13).
 * Inputs: scene_index, decoded Bank slot, Bank index cache.
 * Outputs: CWD in the child on success; 0 -> Case 3 (empty + notice) as for
 * any unresolvable source.
 * Accessors: filesystem_bootReaderNarrowLoadScene/Kit/Instrument/Effect().
 * Affiliates: FS_RESIDENT_SOURCE_BANK_CHILD_BASE, storage_formatBankSceneDir().
 */
```

### B3.11 Bank Save — request, loop, removal, phase 45 — `Core/Hardware/SD/filesystem.c`

**(a) `filesystem_requestSaveBank()` (30281-30447) — MOD**

- Signature: add `uint16_t empty_mask` after `bank_scene_save_mask`
  (header 1384-area).
- Keep the present filter (30338-30341) for `bank_scene_save_mask`. Menu
  already intersected it.
- `op_bank_save_written_mask = full_save_mask;`
- `op_bank_scene_empty_mask = empty_mask & ~full_save_mask & 0xFFFF;`
- The active-scene fallback (30395-30417) runs only when the active Scene is
  in `empty_mask` (Replace removed it): first written child. Otherwise
  `op_bank_active_scene` = the RAM active Scene (S4, S11); on Scene 17 this is
  `scene_backgroundHomeIndex()` (D20, Phase C).
- Edit-mask capture (30428-30433): for written Scenes,
  `(ram[n] & W) | bit(n)`; for removed Scenes, `bit(n)`. Untouched entries are
  filled by B3.11(d) after reading the old manifest. Set
  `seen = W | empty | (Merge-untouched read later)`.

**Comment block** for the mask capture:

```c
/*
 * Stage the slot's edit masks with the resident Bank overruling (S4, S19, S24).
 *
 * What: a written Scene's entry is its RAM entry restricted to the written set
 * plus itself; a removed child's entry is itself only. Entries of untouched
 * children (Merge) are completed after the slot's existing bankset.bcg is read
 * (phase 61), removing every written Scene from them.
 * Why: Scenes in one mask must match; after a partial save, a written Scene can
 * only match written Scenes, and untouched children no longer match the
 * written ones.
 * Inputs: BankData per-Scene entries, op_bank_save_written_mask,
 * op_bank_scene_empty_mask.
 * Outputs: op_bankset_state.scene_mask_voice_edit[] / seen bits.
 * Accessors: filesystem_requestSaveBank().
 * Affiliates: bank_mergeVoiceEditEntry() (B3.13), phases 61-64.
 */
```

**(b) Phases 6–7 (19017-19037) — MOD + ADD phases 61–64:**
- If there are untouched children
  (`~(op_bank_save_written_mask | op_bank_scene_empty_mask) & 0xFFFF != 0`)
  and the Bank directory existed (`op_bank_existing_dir_found`), go from phase
  6 to phase 61 before opening `bankset.bcg` for writing:

| Phase | Action |
|------:|--------|
| 61 | open `bankset.bcg` "r" (missing → phase 64 with every untouched entry = self, R13) |
| 62 | read lines (`filesystem_readTextLine()`, `filesystem.c:11480`) and parse with `storage_banksetParseLineMasked(&op_bankset_state, line, protect = W | E)`, which stores only the untouched entries |
| 63 | close |
| 64 | for each untouched n: `entry = bank_mergeVoiceEditEntry(old_or_self, W | E, n)`; restore `active_scene`/seen fields; phase 7 |

**Comment block:**

```c
/*
 * Read the slot's existing manifest for a partial (Merge) Bank Save (S24).
 *
 * What: parses the existing bankset.bcg of the target Bank, keeps only the
 * entries of children this save leaves untouched, and removes every written
 * or removed Scene from them. A missing or malformed manifest counts as
 * "every untouched entry = self".
 * Why: the slot keeps those children as they are, but their masks must not
 * point at children the save replaced (mutual exclusion, S12/S24).
 * Inputs: op_bank_existing_dir_found, written/removed masks.
 * Outputs: completed op_bankset_state; then the normal writer (phase 7).
 * Buffers: op_line_buf and op_bankset_state (existing); no new storage (§0.4).
 * Accessors: filesystem_saveBankDirectory_tick().
 * Affiliates: storage_banksetParseLineMasked(), bank_mergeVoiceEditEntry().
 */
```

**(c) Per-child loop — phases 11, 12, 20, 21 (19064-19283) — MOD**

```c
#define FS_BANK_SAVE_VISIT_MASK (op_bank_scene_save_mask | op_bank_scene_empty_mask)
```

- Phase 11 (19069) and phase 12 (19100): iterate `FS_BANK_SAVE_VISIT_MASK`
  instead of `op_bank_scene_save_mask`. Phase 11's "mask == 0 → 45" uses the
  visit mask.
- Phase 20 (19202): if the cursor is a removal bit, skip
  `filesystem_prepareBankSceneSaveSource()` and only
  `filesystem_deleteBankChildSlotDirectoryStart()`.
- Phase 21 (19258): after the delete, a removal bit goes to phase 12; a write
  goes to phase 22 (unchanged).

**Comment block:**

```c
/*
 * Remove the children a Replace Bank Save saves empty (S3, S7).
 *
 * What: the per-child loop visits written and removed children in slot order;
 * a removed child runs only the existing exact-object delete (phases 20-21) and
 * advances, a written child continues to the Scene writer as before.
 * Why: "saved empty" means the child folder no longer exists in the slot (S3);
 * reusing the S057 per-child delete keeps one delete path and the handle
 * census.
 * Inputs: op_bank_scene_save_mask, op_bank_scene_empty_mask, op_bank_child_cursor.
 * Outputs: deleted or rewritten child; phase 12 cursor advance.
 * Accessors: filesystem_saveBankDirectory_tick().
 * Affiliates: filesystem_deleteBankChildSlotDirectoryStart(),
 * filesystem_deleteSlotDirectory_tick(), afatfs_countOpenHandles() census.
 */
```

**(d) Phase 45 (19310-19382) — MOD**

- REM `bank_setScenePresentMask(... | op_bank_scene_save_mask)` (19323-19324).
  Save never changes resident presence.
- REM `bank_selectActiveSceneForEditMask(op_bank_active_scene)` (19325). Save
  never changes the active Scene (S25).
- REM the per-Scene mask restore loop (19327-19336). RAM masks stay as they
  are; the staged entries describe the **slot**.
- Keep the display name, restore slot, settings mark, row-0 source/refreshed.
- ADD before 19367: `old_bank_source` captured **before** row 0 is staged
  (19367); `filesystem_stageKeptSceneSources(~W & present, old_bank_source)`
  for resident Scenes not written; written Scenes' Scene rows ←
  `FS_RESIDENT_SOURCE_INHERIT` (S22); non-present resident Scenes in the
  removal set ← `filesystem_stageEmptySceneRows()` source only (rows already
  blank).
- ADD: `bank_invalidateSdCleanScene(n)` for each removed n.

**Comment block** (replacing 19356-19361):

```c
/*
 * Bank Save publishes the slot, never changes the resident Bank (S4, S22, S25).
 *
 * What: the saved slot becomes the boot Bank (row 0, active_bank, display
 * name); written Scenes inherit from it; resident Scenes not written keep
 * explicit provenance (`b<old Bank>`, a direct slot, `@`, `?`); presence, the
 * active Scene and the RAM edit masks are untouched.
 * Why: the former phase applied the staged slot manifest back into RAM (active
 * slot and masks) and OR-ed presence - harmless for a full save, wrong for a
 * Merge whose slot masks describe untouched card children.
 * Inputs: op_slot, written/removed masks, old row-0 source.
 * Outputs: BankData identity/restore slot, staged HCNAMES sources.
 * Accessors: filesystem_saveBankDirectory_tick().
 * Affiliates: filesystem_stageKeptSceneSources(), filesystem_setResidentSource(),
 * bank_invalidateSdCleanScene().
 */
```

### B3.12 Menu Bank Load completion — `Core/Menu/menu.c:15055-15103` — MOD

**Change.**
- REM the empty-Bank fallback ladder (15075-15094) for runtime loads. An
  empty source is now a valid Replace (all Scenes emptied) or a refused
  Merge.
- Re-apply rule (S25): compute
  `replaced = act_mask | empty_mask` (from Preset, ADD
  `preset_bankLoadRequestMasks()`), and
  `active_replaced = (replaced >> scene_getActiveIndex()) & 1` when the active
  Scene is 0..15.
  - If `active_replaced` → `menu_startSoundApply(...)` as today.
  - Else (Scene 17 active, or a Merge that kept the active Scene) → no apply.
    Call `preset_ackStatus()` and then the final index restore
    (`menu_requestLoadCommandFinalIndexRestore()`) directly.
- `bank_revalidateVoiceEditMasks()` runs after every Bank Load (layout may
  have changed).
- After a Merge (kept Scenes exist), `autosave_markResidentBankDirty()` (S5,
  whole-Bank re-mark).

**Comment block:**

```c
/*
 * Bank Load completion: re-apply only a replaced active Scene (S25, S5).
 *
 * What: the shared Scene activation worker runs only when the active Scene's
 * content was loaded or emptied; otherwise the command goes straight to its
 * read-only index restore. A Merge that kept Scenes re-marks the whole Bank so
 * the AutoSave record describes the mixed resident Bank under its new
 * identity. The runtime empty-Bank fallback ladder is gone: a Replace of an
 * empty Bank empties the Bank (Q11); the ladder remains for boot only.
 * Why: re-applying an unchanged active Scene would be audible for nothing, and
 * the user's Scene is never changed by a load.
 * Inputs: Preset request masks, active Scene, Scene 17 state.
 * Outputs: sound apply or final index restore; AutoSave marks.
 * Accessors: menu_pollPresetStatus().
 * Affiliates: menu_startSoundApply(), menu_requestLoadCommandFinalIndexRestore(),
 * autosave_markResidentBankDirty(), bank_revalidateVoiceEditMasks().
 */
```

### B3.13 BankData — merge helpers — `Core/Bank/BankData.h` (after 105) and `.c` (after 520) — ADD

```c
uint16_t bank_mergeVoiceEditEntry(uint16_t entry, uint16_t keep_mask,
                                  uint8_t scene);
void bank_mergeVoiceEditMasksAfterLoad(const uint16_t *source_entries,
                                       uint16_t source_seen,
                                       uint16_t loaded_mask,
                                       uint16_t emptied_mask);
```

- `bank_mergeVoiceEditEntry(entry, keep, n)` = `(entry & keep) | bit(n)`.
- After a load:
  - loaded n: `(src[n] if seen else bit(n)) & loaded | bit(n)`;
  - emptied n: `bit(n)`;
  - kept n: `ram[n] & ~(loaded | emptied) | bit(n)`.

  All writes go through `bank_setSceneMaskVoiceEditForScene()` (normalise,
  active-bit invariant, AutoSave mark).

**Comment block (`.c`):**

```c
/*
 * Mutually exclusive VOICE edit masks after a partial Bank operation (S12,
 * S19, S20, S24).
 *
 * What: bank_mergeVoiceEditEntry() restricts one entry to a set of Scenes that
 * still match it, plus the Scene itself. After a Bank Load, loaded Scenes take
 * their source entry restricted to the loaded set, emptied Scenes are reset to
 * self, and kept Scenes drop every loaded or emptied Scene. The same rule
 * serves Replace (no kept Scenes) and Merge.
 * Why: Scenes in one edit mask must stay equal in parameters and FX (MEMORY.md
 * edit-mask intent); a partial load or save breaks that equality between the
 * two sides.
 * Inputs: source entries + seen bits (Load), loaded/emptied masks.
 * Outputs: per-Scene entries via bank_setSceneMaskVoiceEditForScene().
 * Accessors: filesystem Bank Load phase 20 / empty branch; Bank Save request
 * and phase 64 (entry helper only).
 * Affiliates: bank_revalidateVoiceEditMasks() (layout check, run by Menu after
 * the load), bank_sceneFanoutMask().
 */
```

### B3.14 storageTypes — `Core/Hardware/SD/storageTypes.c:~1800-1840` and `.h:595-602` — ADD `storage_banksetParseLineMasked()`

A wrapper around `storage_banksetParseLine()` that ignores
`scene_mask_voice_edit_NN` keys whose bit is in `protect` and ignores
`active_scene`.

**Comment block:** What: parses one bankset line without overwriting protected
per-Scene entries or the staged active Scene. Why: Merge Bank Save reads the
slot's old manifest into the same staging that already holds the written
Scenes' entries (no second buffer). Inputs: state, line, protect mask.
Outputs: parse status; untouched entries + seen bits. Accessors: Bank Save
phase 62. Affiliates: `storage_banksetParseLine()`.

### B3.15 Preset — `presetManager.c:556-610` `on_bank_load_complete()` — MOD

- Mark loaded children as today.
- Emptied children were already marked in filesystem (B3.6). Do not mark them
  here twice.
- ADD the stored request masks for Menu: `pm_bank_request_empty_mask` (2 B).
  **Alternative with no RAM:** `filesystem_lastBankLoadEmptyMask()`, reading
  `op_bank_scene_empty_mask` before the completion helper resets op scratch,
  as `filesystem_lastBankLoadSceneMask()` already does. Choose the
  filesystem getter (0 B). ADD it at `filesystem.c:31768-31790` with its
  header declaration.

---

## Phase C — Scene 17 background Bank load (`S082_P1` D1–D27)

### C0 Constants and Scene 17 storage — `Core/Bank/Scene/SceneData.h:24`, `SceneData.c:25-26` — ADD

```c
#define SCENE_BACKGROUND_INDEX 16u
#define SCENE_SLOT_TOTAL       17u
```

```c
static scene_t scene_background;           /* 1,656 B, accepted S082 P1 §6 */
static uint8_t scene_backgroundFlags;      /* bit0 exists */
static uint8_t scene_backgroundHome;       /* original Scene index 0..15 */
```

Accessors:

```c
uint8_t scene_backgroundExists(void);
uint8_t scene_backgroundHomeIndex(void);  /* active < 16 ? active : home */
uint8_t scene_playableIndexValid(uint8_t scene_index);
void scene_backgroundBegin(uint8_t home_index);
void scene_backgroundRelease(void);
```

- MOD `scene_get()` (139-150) and `scene_getConst()` (152-162): index 16 →
  `&scene_background` while it exists, else NULL.
- MOD `scene_selectActive()` (176-190): accept 16 only while it exists.
- `scene_indexValid()` stays **resident 0..15** (storage, AutoSave, HCNAMES,
  masks).

**Comment block** (above the storage):

```c
/*
 * Scene 17: the background Scene of a running Bank load (S082 P1).
 *
 * What: one scene_t outside scenes[16] (index SCENE_BACKGROUND_INDEX), plus an
 * exists flag and the index of the Scene that was active when it was built.
 * scene_get()/scene_getConst()/scene_selectActive() address it as index 16
 * while it exists; scene_indexValid() stays 0..15, so storage, AutoSave,
 * HCNAMES and the 16-bit Bank masks never see it.
 * Why: playback continues unchanged from Scene 17 while Scenes 1-16 are
 * replaced (D1-D5); Scene 17 is otherwise a normal, editable Scene (D9, D15).
 * SRAM: 1,656 B + 2 B, SRAM1 static, owner SceneData (accepted 2026-10-17).
 * Lifetime: exists from the switch (C6) until a whole-Scene change (C8).
 * Accessors: scene_background*(), scene_get(), scene_getConst().
 * Affiliates: pat_background_region (its Pattern), seq_switchToBackgroundScene(),
 * preset_buildBackgroundScene(), the S082 P1 §5 audit (C3).
 */
```

### C1 PatternData mapping — `Core/Bank/Scene/Pattern/PatternData.c`

- ADD `static pat_scene_region_t *pat_regionFor(uint8_t scene_index)`: 0..15 →
  `&pat_regions[i]`; 16 → `&pat_background_region` while
  `scene_backgroundExists()`; else NULL.
- MOD every direct `pat_regions[scene_index]` use to `pat_regionFor()`: lines
  118, 200, 661, 987, 1036, 1048, 1129, 1167, 1213, 1231, 1258 (keep 57 and
  62, the declaration and comment).
- MOD every `scene_indexValid(scene_index)` guard in PatternData (63, 116, 197,
  961, 985, 1036, 1048, 1229, 1256, 1288, 1679) to `pat_regionFor(i) != NULL`.
  Exceptions:
  - `pat_snapshotScene()` (116): source 0..15 only, and refused while Scene 17
    exists;
  - `pat_initScene()` (985): 0..15 only. Scene 17 is reset through
    `pat_rawRegionReset(16)` in C5.
- MOD `pat_patternValid()` (959-962): `return pat_regionFor(scene_index) != NULL;`.
- MOD `pat_backgroundPoolMut()` (150) and `pat_autosaveSnapshot()` (129): add
  a comment that callers are gated off while Scene 17 exists (C9).
- DOC the background-region comment (60-85): add use 4, Scene 17, and its
  exclusivity (no AutoSave Pattern drain, no overlap paste while it exists).

**Comment block** (`pat_regionFor()`):

```c
/*
 * Resolve a playable Scene index to its Pattern region.
 *
 * What: resident Scenes 0..15 map to pat_regions[]; index 16 maps to
 * pat_background_region while Scene 17 exists; anything else is NULL.
 * Why: one choke point lets playback, edits, live record and copy/clear
 * address Scene 17 without knowing it is the background region (S082 P1 §5).
 * Inputs: scene index. Outputs: region pointer or NULL.
 * Accessors: every Scene-indexed pat_* function in this file.
 * Affiliates: scene_backgroundExists(), pat_patternValid(),
 * filesystem_patternSnapshotInUse() (the snapshot user is gated off).
 */
```

### C2 PatternStackService — `Core/Bank/Scene/Pattern/PatternStackService.c:1347, 1402, 1474, 1837, 1869, 1899, 2018` — MOD

Replace `scene_indexValid(scene)` with `pat_patternValid(scene)` in:
- `patSvc_validStepRequest()`;
- `patSvc_beginExclusive()`;
- `patSvc_clearTrack()`;
- `patSvc_clearPattern()`;
- `patSvc_removeTrackAutomationByTarget()`;
- `patSvc_requestTarget()`.

`patSvc_prepareSceneReplace()` (1402) stays resident-only: no load writes
Scene 17.

Comment: "Scene 17 is a playable service target (live record, edits,
compaction)."

### C3 Scene-index audit (`S082_P1` §5) — apply rules per site

Appendix A lists every site that can receive index 16 (the active Scene,
played Scene, viewed Scene or LiveRecord target), with its enclosing function
and a rule. Rules:

| Rule | Meaning | Action |
|------|---------|--------|
| **A** | Reads/writes through `scene_get*` / `pat_*` / `scene_instrumentSlot*` | None after C0/C1. Confirm while applying. |
| **P** | Playback / sequencer / LiveRecord / mixer uses the index as a Scene coordinate | None after C0/C1 (`pat_patternValid(16)` is true while Scene 17 exists). Confirm no 16-sized array is indexed. |
| **F** | Edit fan-out over a 16-bit mask | C4. |
| **R** | Resident-only sink (HCNAMES row, AutoSave payload, Bank present/clean masks) | Already bounds-checked (`filesystem_residentSceneRow()` returns the sentinel for ≥16; `autosave_scenePayloadBase()` refuses ≥16; `bank_sceneBit()` returns 0). Confirm each call is a no-op for 16 and not an error path. |
| **B** | `1u << index` into a 16-bit mask | 16 → 0. Decide per site: a no-op (acceptable) or substitute `scene_backgroundHomeIndex()` (Load/Save defaults). |
| **L** | SEQ LED for the Scene | No Scene lit for 16 (D8); `menu_refreshPerfSceneLeds()` guards `< 16` already. |
| **V** | Viewed-Scene mirror (`menu_shownPattern`, follow) | S18: follows to 16 only via the Scene 17 switch with `PAR_FOLLOW` on. |
| **M** | MIDI PC/channel | No PC for 17 (C9). |

**Decisions on the B/R rows found:**

| Site | Decision |
|------|----------|
| `presetManager.c:2910-2913` `preset_loadDrumset()` | Kit Load mask from the active Scene: on 17 use the home Scene. Kit Load never targets 17 (D18). |
| `filesystem.c:29764-29775` `filesystem_requestLoad()` | Same as above. |
| `menu.c:8867-8868` `menu_residentPresentSceneMask()` | The fallback bit uses `scene_backgroundHomeIndex()`. |
| `menu.c:8931-8946` `menu_resetLoadSaveSceneSelection()` | The default single Scene is the home Scene while on 17. |
| `menu.c:15866, 15975` `menu_switchPage()` | Use the home Scene for Load masks. |
| `EffectsManager.c:1066-1161` (paste/reset/resetMorph) | Fan-out → C4. |
| `LiveRecord.c:1483, 1501` `liveRec_paramIn()` | The mask bit for 16 is 0. Recording on 17 is driven by `liveRec_target == 16` through `pat_*`; confirm the bit is used only for AutoSave/marks (R). |
| `sequencer.c:4129` `seq_rampService()` | Inspect: if it is a per-Scene dirty bit, 16 → skip (Scene 17 is never AutoSaved). |
| `presetManager.c:2603-2703` `preset_startInstrumentApplyImage()` | R/B: AutoSave marks are no-ops for 16. A mask bit is only feedback. |
| `copyOps.c:784` `ccCopy_runKit()` | R: present-mask set for a destination 17 → skip (17 is not Bank-present). |
| `menu.c:8846` `menu_voiceHeldSceneButtonPressed()` | VOICE + SEQ per-track assignment: the SEQ indices are 0..15; the "active" comparison with 16 is false (fine). |

### C4 Edit fan-out targets — BankData + every fan-out loop

**ADD** `Core/Bank/BankData.h/.c`:

```c
uint32_t bank_editTargetMask(void);
uint32_t bank_sceneFanoutMask32(uint8_t scene);
```

- `bank_editTargetMask()` = `scene_getActiveIndex() == 16 ? (1ul << 16) : bank_sceneMaskVoiceEdit()`.
- `bank_sceneFanoutMask32(16)` = `1ul << 16`; otherwise
  `bank_sceneFanoutMask(scene)`.

**MOD** each loop to `uint32_t` mask and `scene_index < SCENE_SLOT_TOTAL`,
testing `(mask >> scene_index) & 1u`:

| File:line | Function |
|-----------|----------|
| `menu.c:4936, 4990, 5031` | `menu_cellCommitValue()` (kit cells, instrument cells, voice mix) |
| `menu.c:13978` | `menu_commitBusCompParam()` |
| `menu.c:16384, 16420` | `menu_parseGlobalParam()` (overall Morph, voice Morph) |
| `menu.c:8805` | `menu_refreshVoiceHeldSceneLeds()` (LEDs: keep 16-bit; with 17 active show none, D8) |
| `EffectsManager.c:557` | `effects_fanoutMask()` → returns `uint32_t`; 16 → `1ul<<16` |
| `EffectsManager.c:627, 729, 742, 755, 769, 1025, 1059, 1088, 1136, 1231, 1284, 1799` | its consumers |
| `copyOps.c:703, 769, 821, 994, 1057, 1162` | `bank_sceneFanoutMask()` → `bank_sceneFanoutMask32()` |
| `clearOps.c:242-244, 293-295, 324, 425, 445-447, 488, 575, 651, 659` | the same |

**Comment block** (BankData):

```c
/*
 * Edit fan-out targets including Scene 17 (bit 16).
 *
 * What: 32-bit Scene masks for edit and copy/clear fan-out. While the active
 * (viewed/edited) Scene is Scene 17 the target set is Scene 17 alone; for
 * resident Scenes it is the existing 16-bit entry, unchanged.
 * Why: Scene 17's edit mask is itself only (D15) and a 16-bit mask cannot
 * name it; without this, edits on 17 would fan out to the home Scene's group
 * or write nothing.
 * Inputs: active Scene, per-Scene entries. Outputs: uint32_t masks.
 * Accessors: Menu commit loops, EffectsManager edit API, copy/clear jobs.
 * Affiliates: bank_sceneMaskVoiceEdit(), bank_sceneFanoutMask(),
 * scene_backgroundExists(). Foreground only; no SRAM.
 */
```

### C5 Build Scene 17 — `Core/Bank/Scene/Preset/presetManager.c` — ADD `preset_buildBackgroundScene()`

```c
uint8_t preset_buildBackgroundScene(uint8_t rebuild_in_place);
```

Steps (D2–D4, D9, D19):
1. `rebuild_in_place == 0`:
   - `scene_background = *scene_getConst(active)` (settings, Effect record, FX
     sequence, bus compressor, global Morph);
   - clear the chain (`settings.chain_*` = none, D9b);
   - `scene_backgroundBegin(active)` (exists flag, home index);
   - fast path when `!seq_perTrackActive`: `pat_snapshotScene(active)`
     (whole region copy) and done.
2. Per-track path (`seq_perTrackActive`, or `rebuild_in_place`):
   - `pat_rawRegionReset(16)` (fresh only);
   - for t = T1..T7 in order with `src = seq_perTrackPattern[t]`:
     - skip if `src == 16` (rebuild in place: the track stays);
     - sum `pat_rawBlockBytes()` over the track's steps → chunks;
     - if it fits in `pat_rawFreeChunks(16)`: copy each step block with
       `pat_rawReadBlock(src, t, s)` + `pat_rawPlace(16, t, s, block, trigger)`,
       and block-less trigger steps with `pat_rawPublishEmpty(16, t, s, ON)`;
     - copy the track parameters (length, scale, shuffle, run mode, rotation,
       loop exclude, automatable flags, MIDI channel/note, track Morph
       endpoints);
     - copy the slot data (Kit instrument image Normal/Morph/interp, voice
       Morph amount, audio out, FX send Normal/Morph, fader). T6/T7 share slot
       6: take it from T6's Scene if T6 fits, else from T7's (§4.2);
     - a non-fitting track is left empty in 17 (D3).
3. Names: keep the 90 B runtime identity as Scene 17's captured identity (D15,
   §6 "0 new").

**Comment block:**

```c
/*
 * Build (or rebuild in place) Scene 17 from what is playing (D2-D4, D9, D19).
 *
 * What: copies the active Scene's Scene-level state (settings without chain,
 * Effect, FX sequence, bus compressor, Morph) and, per track in T1..T7 order,
 * the Pattern track and its voice slot from the track's played Scene into
 * Scene 17. With no per-track overrides the whole Pattern region is copied in
 * one step (fits by construction). A track whose blocks do not fit is left
 * empty and later tracks are still tried; T6/T7's shared slot follows T6 when
 * T6 fits. A rebuild in place keeps tracks already on 17 and Scene 17's
 * Scene-level state.
 * Why: Scene 17 must sound exactly like what is playing (D4), so the switch
 * can be seamless (no activation worker, C6).
 * Inputs: active Scene, seq_perTrackPattern[], resident Scenes, pool capacity.
 * Outputs: scene_background, pat_background_region; returns a mask of tracks
 * copied (bit set) for the trace.
 * Context: foreground, before the switch; Scene 17 is not played yet (fresh)
 * or the copied tracks are not on 17 yet (rebuild), so no ISR reads the
 * written bytes.
 * Accessors: preset_requestBackgroundBankLoad() tick.
 * Affiliates: pat_snapshotScene(), pat_rawReadBlock(), pat_rawPlace(),
 * pat_rawFreeChunks(), pat_rawRegionReset(), scene_backgroundBegin().
 */
```

### C6 Switch — `Core/Sequencer/sequencer.c` after `seq_alignActivePatternToScene()` (2160) + `sequencer.h` — ADD `seq_switchToBackgroundScene()`

```c
void seq_switchToBackgroundScene(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    seq_previousScene = seq_activePattern;
    seq_activePattern = SCENE_BACKGROUND_INDEX;
    seq_pendingPattern = SCENE_BACKGROUND_INDEX;
    for (t = 0; t < NUM_TRACKS; t++) seq_perTrackPattern[t] = SCENE_BACKGROUND_INDEX;
    seq_perTrackActive = 0u;
    seq_loadPendigFlag = 0u;
    seq_newPatternAvailable = 0u;
    seq_loopRefreshSceneSnapshots();
    __set_PRIMASK(primask);
    seq_cancelPendingDirectChange();
    seq_pendingSceneQueue = 0u;          /* via a small cancel helper */
    seq_chainBarCount = 0u; seq_chainRepCount = 0u; seq_chainStopped = 0u;
    seq_refreshTrackEffectiveParams();   /* identical values (copied) */
}
```

No realign, no roll/condition/epoch resets, no `seq_sendProgChg()`, no
note-off, no `led_notifyPatternChanged()`.

**Comment block:**

```c
/*
 * Move playback to Scene 17 without a sound change (D5, D16).
 *
 * What: points the active, pending and every per-track Scene at index 16 in
 * one PRIMASK section and re-snapshots the looper for the new Scene, keeping
 * step positions, DDA accumulators, rolls, conditions, epochs and looper
 * anchors; cancels pending direct/queued Scene changes. No program change,
 * note-off or LED notify is sent (Scene 17 has no number).
 * Why: Scene 17 is an exact copy of what played, so the switch must change the
 * read address only; seq_selectActivePattern() would reset rolls/conditions,
 * realign and send PC (audible), and seq_alignActivePatternToScene() also
 * realigns.
 * Inputs: Scene 17 built (scene_backgroundExists()).
 * Outputs: seq_activePattern = seq_perTrackPattern[] = 16.
 * Context: foreground.
 * Accessors: preset_requestBackgroundBankLoad() tick.
 * Affiliates: seq_loopRefreshSceneSnapshots() (S080 looper invariants: under
 * PRIMASK, anchors kept), seq_cancelPendingDirectChange(),
 * seq_refreshTrackEffectiveParams(), LiveRecord (follows the played Scene).
 */
```

Then (still C6), in Menu/Scene:
- `scene_selectActive(16)`;
- `menu_setPlayedPattern(16)`;
- if `parameter_values[PAR_FOLLOW]`, `menu_setShownPattern(16)` (S18);
- `menu_refreshPerfSceneLeds()` shows none (D8).

### C7 Request and orchestration — `presetManager.c/.h` — ADD

```c
#define PRESET_BANK_ORIGIN_MENU 0u
#define PRESET_BANK_ORIGIN_MIDI 1u
uint8_t preset_requestBackgroundBankLoad(uint16_t slot, uint16_t act_mask,
                                         uint16_t empty_mask, uint8_t origin);
uint8_t preset_backgroundBankLoadRunning(void);
void preset_tickBackgroundBankLoad(void);     /* called from menu_pollPresetStatus() */
```

State: one byte `pm_bg_phase` (ACK NEEDED, §9). The slot and masks reuse
`pm_request_slot`/`pm_kit_request_scene_mask` plus the filesystem op masks;
the origin fits in `pm_bg_phase` bits.

Phases:

| Phase | Action |
|------:|--------|
| 0 | Refuse if a Bank load runs (D13) or the slot is missing. MIDI origin: `menu_backgroundBankLoadUiBegin(slot)` (switch to Load:[Bank], set slot, Replace, all 16, `...`, close latched menus as MODE does, D7, Q15). |
| 1 | Decide Scene 17: needed iff `seq_isRunning()` and (`act|empty`) contains the active Scene or any `seq_perTrackPattern[t]` (≤15), or a played Scene is already 17 (D19 rebuild) (D25). Not needed → phase 4 (direct load). |
| 2 | Drop background work: `ccSvc_dropQueued()` (new, below); wait `!ccSvc_busy()` (D12, completes the running job); Replace: `autosave_discardDirtyMask()` (D1), Merge: keep (D25); set `FS_BANK_FLAG_AUTOSAVE_YIELD`; wait until the facade is idle and `!filesystem_patternSnapshotInUse()`. |
| 3 | `preset_buildBackgroundScene(rebuild)`; `seq_switchToBackgroundScene()` + C6 Menu/Scene steps. |
| 4 | `preset_loadBank(slot, act, empty, 0)`; normal completion (B3.12). Scene 17 active → no apply. |
| 5 | Done: signal the origin (MIDI CC0 TX in S083); the UI unlocks on Load/Save (D7). |

**ADD** `copyClearService.c` `ccSvc_dropQueued()`: drop jobs that have not
started (queue head > 0), keep the running job; trace each drop.

**ADD** `filesystem.c` AutoSave drain yield: in the parameter and Pattern drain
ticks, at each phase boundary that has closed or not yet opened its file, if
`op_bank_flags & FS_BANK_FLAG_AUTOSAVE_YIELD`, finish early with the record
left stale (D1: a torn record is acceptable). The flag lives in the shared op
flags (B3.2) **only if** the drain does not reset op state on start;
otherwise use a separate 1 B `fs_autosave_yield` (§9).

**Comment block** (`preset_requestBackgroundBankLoad()`):

```c
/*
 * Bank load that keeps playing (S082 P1 §4, D1-D27).
 *
 * What: accepts a Bank load from the Menu or MIDI at once. If the transport
 * runs and a played Scene is about to be replaced, it drops queued copy/clear
 * work, lets the running job finish, discards (Replace) or keeps (Merge)
 * pending AutoSave state, makes the AutoSave drain yield, builds Scene 17,
 * switches playback to it seamlessly, then runs the ordinary two-mask Bank
 * Load into Scenes 1-16. Otherwise it runs the Bank Load directly. MIDI origin
 * also switches the UI to Load:[Bank] exactly as an OK would.
 * Why: a Bank change must not interrupt what is playing (D1-D5) and must be
 * reachable from MIDI CC0 (S083 P4).
 * Inputs: slot, outcome masks, origin. Outputs: accepted/refused; background
 * phases ticked from menu_pollPresetStatus(); completion through the normal
 * PRESET_OP_BANK_LOAD path.
 * Accessors: Menu Bank OK (B2.10), S083 MIDI CC0.
 * Affiliates: preset_buildBackgroundScene(), seq_switchToBackgroundScene(),
 * preset_loadBank(), ccSvc_dropQueued(), autosave_discardDirtyMask(),
 * menu_backgroundBankLoadUiBegin(), preset_backgroundBankLoadRunning() (S083
 * drops PC/CC0 while it is set, D13).
 */
```

### C8 Release — `sequencer.c:2034` `seq_selectActivePattern()` and `:1771` `seq_setTrackPlayedScene()` — MOD

- `seq_selectActivePattern(p)`: when `p < 16` and `scene_backgroundExists()`,
  call `scene_backgroundRelease()` **after** the new Scene is published (after
  the PRIMASK section at 2130-2142). Release clears the exists flag, so
  `pat_patternValid(16)` and `scene_get(16)` become invalid. Nothing reads 16
  any more, because every track now points at p.
- `seq_setTrackPlayedScene()`: a per-track change never releases (D10).
  Assigning a track to 16 is allowed only while 17 exists (double-click
  realign / VOICE+SEQ on the active Scene, §4.6).
- Menu: after release, `scene_selectActive(p)` is already done by the PERF
  path. Refresh the PERF LEDs.
- AutoSave resumes automatically (C9 gate off).

**Comment block:**

```c
/*
 * Release Scene 17 on the first whole-Scene change (D9a, D10).
 *
 * What: once every track points at a resident Scene, Scene 17's storage
 * becomes invalid and can never be selected again; AutoSave and overlap paste
 * return.
 * Why: Scene 17 owns the sounding Effect and Scene-level state until a
 * whole-Scene change replaces them; per-track moves never release it.
 * Inputs: new Scene p < 16, scene_backgroundExists().
 * Outputs: scene_backgroundRelease().
 * Accessors: seq_selectActivePattern() (SEQ press, MIDI PC, queued/chain fire).
 * Affiliates: scene_backgroundRelease(), pat_regionFor(), C9 gates.
 */
```

### C9 Gates and labels

| Gate | File:line | Change |
|------|-----------|--------|
| AutoSave scalar + Pattern drains | `filesystem.c:25805, 25808, 25813` | Add `&& !scene_backgroundExists()`. `settings.cfg` (25744) is **not** gated (D24). Comment: "No AutoSave while Scene 17 exists (D11); dirty marks of Scenes 1–16 keep accumulating (D17) and are written after the release." |
| Overlap paste | `copyClearService.c:674-680` `ccSvc_runPatternPaste()` | If `ccSvc_pasteOverlaps()` and `scene_backgroundExists()` → `ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_SCENE17, ...)`, return `CC_RUN_DROP`; the existing refusal display shows (D23). New drop-reason constant in `copyClearService.h` and `decode_devlogs.py`. |
| Scene Load into 17 | n/a | Load/Save destinations are SEQ 0..15; nothing to change (D18). |
| Scene Save from 17 | `filesystem_requestSaveSceneDirectory()` (`filesystem.c:29900`) source check (`scene_getConst(source)` is valid for 16 after C0) | Allowed (D15). Its HCNAMES publication must skip row updates for 16 (rows are resident-only; `filesystem_residentSceneRow(16)` = sentinel). Confirm. |
| PC TX | `sequencer.c:6314` `seq_sendProgChg()` | `if (ptn >= 16) return;` (D8, no number). The switch never calls it (C6); this guards other paths. |
| Chain | `sequencer.c:5487` `seq_chainResolveNext()` | Chain fields are cleared in 17 (C5). `cur = 16` → `(cur + 1) & 0x0F` would wrap; with no condition it never evaluates. Add `if (cur >= 16) return 0xFFu;` defensively. |
| PERF LEDs | `menu.c:8533` `menu_refreshPerfSceneLeds()` | Already `active < 16` guarded → none lit (D8). Confirm the queued-Scene owner `menu_syncQueuedSceneLed()`. |
| Scene number labels | Menu screens that print the active Scene number | Print `--` for 16 (D22). Find with `grep -n "scene_getActiveIndex() + 1\|active + 1" Core/Menu/menu.c` while applying C3. |
| Bank Save on 17 | B3.11(a) | `active_scene` = home index (D20). |
| Bank Load on 17 | C7 phase 1/3 | Rebuild in place (D19). |
| Copy/clear destination 17 | `copyClearSession.c` SEQ coordinates are 0..15; the "current Scene" default is the active Scene | Pattern-region ops on 17 work through `pat_*`; Scene-child pastes fan out with `bank_sceneFanoutMask32()` (C4). Non-overlapping only (gate above, D18). |

### C10 Menu UI hooks — `menu.c` — ADD

- `void menu_backgroundBankLoadUiBegin(uint16_t slot)`: performs
  `menu_switchPage(LOAD_PAGE)` with type Bank; sets
  `menu_currentPresetNr[SAVE_TYPE_BANK] = slot`, Replace, `0xFFFF`; calls
  `menu_beginLoadSaveCommand()` (`...`); closes latched menus
  (`menu_chainMenuClose()`, SHIFT+REC) as MODE does (D7, Q15).
- `menu_pollPresetStatus()`: call `preset_tickBackgroundBankLoad()` each pass
  before the Preset status dispatch.

Comment blocks follow the same template; Affiliates: C7, D7.

---

## Phase D — Docs, tools, close

| File | Change |
|------|--------|
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | Bank Load/Save "Current behavior": two masks, Replace/Merge, removal, present-mask assignment, runtime active rule, bankset `active_scene` boot-only, Merge Save manifest read; HCNAMES tokens `bNNN`/`x`; Bank preview probe. |
| `AUTOSAVE.md` | `scene_present_mask` rule (assignment on Load); Merge whole-Bank re-mark; AutoSave suspended while Scene 17 exists; new source values. |
| `MODULE_INTERCHANGE_SPEC.md` | Load/Save UI (Bank page, COPY tap/hold, labels, retention), name-editing repaint rule, Instrument `chg`, Scene 17 index rules. |
| `BANK_PRESET_ARCHITECTURE.md` | `active_scene` legacy; edit-mask mutual exclusion. |
| `COPYCLEAR_UTILITIES.md` | Scene 17 as destination, overlap refusal, fan-out bit 16; `clear scene` unchanged. |
| `PATTERN_DYNAMIC_STACK.md` | Background region use 4 (Scene 17), `pat_regionFor()`. |
| `STORAGE_SRAM_MANIFEST.md` | §9 allocations, measured. |
| `MEMORY.md` | Close the known bug "runtime Bank Load can switch the playing Scene"; Load/Save pot repaint rule; Scene 17 invariants; card preparation (delete `.hcnames`, `.hcnamtmp`, `.hcprms1/2`). |
| `tools/verify_bank_autosave.py:255-277` | Accept missing children for `x` rows; accept `bNNN` Scene sources; stop requiring all 16 children. |
| `tools/decode_devlogs.py` | New trace stage (B1.6) and drop reason (C9). |
| `S082_P1_LOAD_SAVE.md` §7 | Record implementation questions found while applying C3. |

---

## 9. RAM summary (policy)

| Item | Bytes | Region | Lifetime | Owner | Status |
|------|------:|--------|----------|-------|--------|
| `menu_bankPreviewContentMask` | 2 | SRAM1 static | permanent | Menu | in S6 total |
| `menu_bankUiFlags` | 1 | SRAM1 static | permanent | Menu | in S6 total |
| `bh_bankCopyPressTick` | 2 | SRAM1 static | permanent | ButtonHandler | S15 |
| `op_bank_child_content_mask` | 2 | SRAM1 static | op | filesystem | in S6 total |
| `op_bank_scene_empty_mask` | 2 | SRAM1 static | op | filesystem | **ACK NEEDED** |
| `op_bank_save_written_mask` | 2 | SRAM1 static | op | filesystem | **ACK NEEDED** |
| `op_bank_flags` | 1 | SRAM1 static | op | filesystem | **ACK NEEDED** |
| `pm_bg_phase` | 1 | SRAM1 static | permanent | Preset | **ACK NEEDED** |
| `fs_autosave_yield` (only if the op flag cannot be used) | 0–1 | SRAM1 static | permanent | filesystem | **ACK NEEDED if used** |
| Bank slot-view triple | 0 | — | — | — | reuses the existing preview triple (§B2.1) |
| Scene 17 `scene_background` + 2 B flags/home | 1,658 | SRAM1 static | permanent | SceneData | accepted 2026-10-17 (`S082_P1` §6) |
| Scene 17 Pattern | 0 | `pat_background_region` | — | PatternData | reuse |
| **New total outside Scene 17** | **15–16** | | | | S6/S15 accepted ~13 B; **+2–3 B to acknowledge** |

Stack: the B1 probe and B3 helpers use existing buffers; there is no new
large stack frame. `preset_buildBackgroundScene()` uses one
`PAT_RAW_BLOCK_MAX` (136 B) stack buffer, the same as the copy/clear paste
engine.

---

## 10. Verification per phase

| Phase | Rows |
|-------|------|
| A1 | Fast pot 3/pot 4 turns on Kit, Scene, Bank, Pattern and Instrument Save; all four pots at once; no corrupted characters; pot behaviour unchanged; BAR2 keeps characters at cells 1–7 and is inert at 8; BAR1 unchanged; Load-page pot 2 scroll responsiveness. |
| A2 | Each type step shows `chg` + row 000's name; click and positive turn load 000; negative turn stays; after the commit no `kit` row (000 clamps); old type shows `chg`; back to the resident type before committing restores `kit` and its restore works (S054 cases: busy crossing, repeat turn, idle retry); Mrp unaffected; multi-Scene mask with an Advanced-limit conflict hides the type. |
| B1 | Preview masks equal a host scan of the card for Banks with empty, Kit-only and full children; trace timing on the slow and fast cards; report before B2. |
| B2 | LEDs per mode/view against PERF; tap < 200 ms toggles, hold ≥ 200 ms peeks; SEQ/OK blocked while held; encoder moves while held refresh the Save slot view; MODE while held; event overflow while held; COPY on Kit/Scene/Pattern still refused; retention matrix (§4.4); labels `ok`/`mr`/`OW`/`MR`. |
| B3 | Card round trips: 4 modes × {all, partial, none selected} × {empty, Kit-only, full} children; the active and viewed Scene never change (stopped and running Merge without Scene 17); emptied active Scene stays present and editable; `.hcnames` rows (`-`, `bNNN`, `x`) after each; reboot after each (HCNAMES-authoritative path and AutoSave path); `verify_bank_autosave.py`; S058 clean skip still skips; removed children deleted. |
| C | `S082_P1` §8 rows; Merge running with and without a played Scene replaced; edits on 17 do not fan out; Scene Save from 17; overlap paste refused on 17; PC not sent at the switch; release by SEQ press, by queued change and by chain; per-track moves do not release. |

---

## 11. Implementation log

### Phase A1 — COMPLETE
- A1.1: replaced per-delta `menu_repaintAll()` with `menu_knobs_dirty = 1u` for pots 2-4 in `menu_handleLoadSaveKnobDelta()`.
- A1.2: added Load/Save routing in `menu_serviceKnobRepaint()` through `menu_repaint()`.
- A1.3: removed `name[ci + 1u] = ' '` from BAR2 in `menu_loadSaveBarButtonPressed()`; both `menu_repaintAll()` → `menu_repaint()`.
- A1.4: updated BAR2 comment in `buttonHandler.c` to "cursor-forward (keeps characters)".
- Build: 573,688 / 753,664 B flash, headroom 179,976 B.

### Phase A2 — COMPLETE
- A2.1: added `MENU_INSTRUMENT_SOURCE_CHANGE` enum with comment block.
- A2.2: added `menu_instrumentLoadKitRowAvailable()`.
- A2.3: added `menu_instrumentTypeSelectableForMask()` and `menu_instrumentLoadCommitChange()`.
- A2.4: replaced type check with mask check in `menu_instrumentLoadStepType()`; added CHANGE/KIT/POOL source selection.
- A2.5a: added CHANGE handling in lower-row encoder block.
- A2.5b: added `kit` row clamp when unavailable.
- A2.7: replaced `(void)btnClicked;` with `chg` click commit logic.
- A2.8: type-change finalization in `menu_finishInstrumentApplySession()` (moved from `menu_tickInstrumentApply()` due to declaration ordering).
- A2.9: added CHANGE renderer branch before KIT branch.
- A2.10: updated renderer header comment to mention `chg`.
- Forward declarations added for `menu_instrumentLoadCommitChange`, `menu_instrumentLoadKitRowAvailable`, `menu_instrumentLoadRefreshBaseType`.
- Build: 574,224 / 753,664 B flash, headroom 179,440 B (+536 B over A1).

### Phase B1 — COMPLETE
- B1.1: added `op_bank_child_content_mask` with comment block in filesystem.c; reset in `filesystem_start()`.
- B1.2: modified `filesystem_scanBankScenes_tick()`: case 8 now copies display names into `op_bank_child_scratch.bank_child_display[]` with lexical-winner rule; case 10 enters probe phase 11 when present children exist; added phases 11-20 for cooperative content probe (directory iterate for `.pat`, seek past header, read address array in 160 B chunks, test trigger bits).
- B1.3: added `filesystem_bankChildContentMask()` accessor.
- B1.4: added header declaration in filesystem.h.
- B1.5: updated `filesystem_requestScanBankScenes()` comment to mention both masks and Load/Save usage.
- B1.6: added `AUTOSAVE_TRACE_STAGE_BANK_PREVIEW = 'b'` to AutosaveTrace.h; trace emission at probe completion (`#if DEV_MODE_LOGGING`); decode_devlogs.py updated with 'b' stage enum, producer, and decode branch.
- Build: 575,248 / 753,664 B flash, headroom 178,416 B (+1,024 B over A2).

### Phase B2 — COMPLETE
- B2.13(a-e): COPY button handling for Bank pages in buttonHandler.c — press/release/hold/overflow intercepts with `bh_bankCopyPressTick` storage.
- B2.14: Load/Save exit cleanup in `menu_switchPage()` — `menu_bankCopyForceRelease()`, `menu_bankUiFlags = 0`, COPY LED off; DOC note at LOAD_PAGE entry about Bank pages overriding Kit scene mask.
- B2.15: Refactored `menu_resetSaveParameters()` into `menu_resetSaveParametersEx(uint8_t after_command)` so Bank page scene selection is retained after a Bank Save command completes.
- B2.16: Type-row reset DOC note.
- Build: 575,840 / 753,664 B flash, headroom 177,824 B (+592 B over B1).

### Phase B3 — COMPLETE
- B3.1: `preset_bankLoadRequestMasks()` and `preset_bankLoadEmptyMask()` in presetManager.h/.c.
- B3.2: Added op fields `op_bank_scene_empty_mask`, `op_bank_save_written_mask`, `op_bank_flags` with flag defines in filesystem.c; reset in `filesystem_start()`.
- B3.3: Changed `filesystem_requestLoadBank()` to 5-arg signature `(slot, act_mask, empty_mask, flags, cb)`; updated filesystem.h declaration, presetManager.c caller, and generic `filesystem_requestLoad()` FS_FILE_BANK case.
- B3.4: Rewrote Bank Load phase 17 — Replace-empties-missing rule, BOOT-gated active-Scene re-pick, phase 98 routing.
- B3.5: Added Bank Load phase 98 — empties Scenes via `filesystem_bankEmptyScenes()`, routes to empty-Bank or child-loop.
- B3.6: Added three static helpers before `filesystem_loadBankDirectory_tick()`: `filesystem_stageEmptySceneRows()`, `filesystem_stageKeptSceneSources()`, `filesystem_bankEmptyScenes()`. Added `scene_resetResidentToEmpty()` in SceneData.c/.h. Refactored `filesystem_bootReaderEmptyScene()` to use the new helpers.
- B3.7: Updated Bank Load phase 20 commit — `old_bank_source` capture, `filesystem_stageKeptSceneSources()`, new present-mask merge formula, BOOT-gated active/playback, `bank_mergeVoiceEditMasksAfterLoad()`.
- B3.8: Updated empty-Bank branch (phase 17) and phase 98 empty-Bank sub-path with same changes as B3.7.
- B3.9(a-e): New HCNAMES source tokens (`FS_RESIDENT_SOURCE_BANK_CHILD_BASE`, `FS_RESIDENT_SOURCE_EMPTY`) in filesystem.h; updated `filesystem_residentSourceValid()`, `filesystem_parseResidentSourceToken()`, `filesystem_formatResidentNameLine()`, `filesystem_resolveResidentSource()`.
- B3.10(a): Boot reader EMPTY dispatch — `filesystem_bootReaderEvaluateScene()` handles EMPTY as intentional empty without Case-3 notice; BANK_CHILD sets resolved_row to Scene row and is accepted in the Case-2 dispatch.
- B3.10(b): `filesystem_bootReaderEnterSceneFolder()` BANK_CHILD branch — enters Bank/NNN/NN/ using cached Bank index for the decoded slot.
- B3.10(c): Kit folder routing through Scene folder handles BANK_CHILD via existing resolved_row dispatch.
- B3.11(a): Changed `filesystem_requestSaveBank()` to accept `uint16_t empty_mask`; stages written entries with `bank_mergeVoiceEditEntry()`, empty entries as self-only, seen = written | empty.
- B3.11(b): Added Bank Save phases 61-64 — reads old bankset.bcg for partial Merge Save, parses untouched entries with `storage_banksetParseLineMasked()`, completes them with `bank_mergeVoiceEditEntry()`.
- B3.11(c): Per-child loop (phases 11, 12, 20, 21) uses visit mask (written | empty); removal bit at phase 20 skips `prepareBankSceneSaveSource()` and routes to phase 12 after delete at phase 21.
- B3.11(d): Phase 45 rewritten — REM present-mask OR, active-Scene set, mask restore loop; ADD old_bank_source capture, written Scenes → INHERIT, kept Scenes → `filesystem_stageKeptSceneSources()`, empty Scenes → `filesystem_stageEmptySceneRows()` + `bank_invalidateSdCleanScene()`.
- B3.12: Rewrote Menu `PRESET_OP_BANK_LOAD` completion — removed empty-Bank fallback ladder for runtime loads; added re-apply rule (S25); `bank_revalidateVoiceEditMasks()` after every Bank Load; `autosave_markResidentBankDirty()` when Merge.
- B3.13: Added `bank_mergeVoiceEditEntry()` and `bank_mergeVoiceEditMasksAfterLoad()` in BankData.c/.h.
- B3.14: Added `storage_banksetParseLineMasked()` in storageTypes.c/.h — wrapper that protects per-Scene entries and active_scene from overwrite.
- B3.15: Added `filesystem_lastBankLoadEmptyMask()` getter in filesystem.c/.h.
- Build: 578,464 / 753,664 B flash, headroom 175,200 B (+3,216 B over B1).

### Phase C0 — COMPLETE
- Added `SCENE_BACKGROUND_INDEX 16u` and `SCENE_SLOT_TOTAL 17u` in SceneData.h.
- Added `scene_background` (scene_t), `scene_backgroundFlags`, `scene_backgroundHome` static storage in SceneData.c.
- Modified `scene_get()`, `scene_getConst()`, `scene_selectActive()` to accept index 16 when Scene 17 exists.
- Added 5 accessors: `scene_backgroundExists()`, `scene_backgroundHomeIndex()`, `scene_playableIndexValid()`, `scene_backgroundBegin()`, `scene_backgroundRelease()`.
- Build: 577,992 / 753,664 B flash.

### Phase C1 — COMPLETE
- Added `pat_regionFor()` static helper with forward declaration in PatternData.c.
- Changed `pat_sceneRegion()`, `pat_sceneRegionMut()`, `pat_patternValid()`, `pat_addrPtr()` to delegate to `pat_regionFor()`.
- Updated 6 direct `pat_regions[scene_index]` accesses to use `pat_regionFor()`.
- Left `pat_snapshotScene()` and `pat_initScene()` as resident-only.
- Build: 577,392 / 753,664 B flash.

### Phase C2 — COMPLETE
- Replaced `scene_indexValid(scene)` with `pat_patternValid(scene)` at 6 sites in PatternStackService.c.
- Left `patSvc_prepareSceneReplace()` as `scene_indexValid()` (resident-only).
- Build: 577,424 / 753,664 B flash.

### Phase C4 — COMPLETE
- BankData.c/.h: Added `bank_editTargetMask()` (returns uint32_t; Scene 17 → bit 16 only) and `bank_sceneFanoutMask32()`.
- EffectsManager.c: `effects_fanoutMask()` return type widened to uint32_t with Scene 17 body logic; all `uint16_t mask` → `uint32_t mask`; all bit-test patterns → `(1ul << x)`; 12 fan-out loop bounds → `SCENE_SLOT_TOTAL`; type-match loop at line 576 stays `SCENE_COUNT`.
- menu.c: 7 fan-out sites (`MENU_CELL_KIT_SETTING`, `MENU_CELL_INSTRUMENT`, `MENU_CELL_SCENE_SETTING`, `menu_commitBusCompParam`, `PAR_MORPH`, `PAR_VOICEn_MORPH`) changed from `uint16_t bank_sceneMaskVoiceEdit()` to `uint32_t bank_editTargetMask()` with `SCENE_SLOT_TOTAL` bounds and `(1ul <<)` bit tests. LED refresh at line 8992 kept 16-bit (D8).
- copyOps.c: `ccCopy_bit()` widened to uint32_t; `ccCopy_activeIn()` param to uint32_t; 5 mask locals → uint32_t; 6 `bank_sceneFanoutMask()` → `bank_sceneFanoutMask32()`; 8 loops → `SCENE_SLOT_TOTAL`; cleaned `(uint16_t)~ccCopy_bit()` casts.
- clearOps.c: `ccClear_bit()` widened to uint32_t; `ccClear_effectRowsChanged()` param to uint32_t; 3 mask locals → uint32_t; all `bank_sceneFanoutMask()` → `bank_sceneFanoutMask32()`; 3 loops → `SCENE_SLOT_TOTAL`; cleaned `(uint16_t)~ccClear_bit()` cast.
- Build: 577,544 / 753,664 B flash, headroom 176,120 B (+120 B over C2).

### Phase C3 — COMPLETE
- preset_loadDrumset(): Kit Load destination mask now uses
  scene_backgroundHomeIndex() while Scene 17 is active (never bit 16).
- filesystem_requestLoad(): generic Kit/Scene/Pattern loads use the home
  Scene for their 16-bit destination masks while Scene 17 is active.
- menu_residentPresentSceneMask(): the one-Scene fallback uses the home
  Scene.
- menu_resetLoadSaveSceneSelection(): the default single Scene bit uses the
  home Scene; menu_loadSaveSourceScene keeps the active index so Scene Save
  from 17 stays allowed (D15).
- menu_switchPage() LOAD_PAGE entry: the Kit/Scene/Pattern default bit uses
  the home Scene (Bank pages still override to 0xFFFF).
- ccCopy_runKit(): a Kit copy into Scene 17 commits the image but neither
  sets a Bank present bit nor marks AutoSave (17 is never Bank-present).
- preset_saveBank() and filesystem_requestSaveBank(): the manifest
  active_scene uses the home Scene while 17 is active.
- filesystem_requestSaveSceneDirectory(): Scene Save from 17 publishes no
  resident HCNAMES row (mask stays zero) while still writing the directory.
- Verified no change needed: preset_startInstrumentApplyImage() (mask bit is
  feedback only; resident destinations), liveRec_paramIn() (no Scene bit
  mask), seq_rampService() (Scene coordinate through pat_*),
  menu_voiceHeldSceneButtonPressed() (comparison with 16 is false).

### Phase C5 — COMPLETE
- PatternData.c: factored the empty-region initializer into
  pat_initRegion(); pat_initScene() stays resident-only and
  pat_rawRegionReset() now resets either a resident region or Scene 17.
- pat_clearPattern() and pat_removeTrackAutomationByTarget() now accept
  Scene 17 through pat_patternValid().
- presetManager.c: added preset_backgroundTrackChunks(),
  preset_copyBackgroundTrack(), preset_copyBackgroundSlot() and
  preset_buildBackgroundScene(). The fast path copies the whole active
  region when no per-track overrides exist; the per-track path resets Scene
  17, copies each track that fits (leaving a non-fitting track empty),
  copies track parameters/MIDI/Morph endpoints and the T1..T7 voice slots,
  with slot 6 following T6 when T6 fits. Chain fields are cleared; fresh
  builds copy Pattern globals from the active Scene.
- Header declaration with the C5 contract added to presetManager.h.

### Phase C6 — COMPLETE
- Added seq_switchToBackgroundScene() in sequencer.c/.h: under PRIMASK it
  points active/pending/all per-track Scenes at 16, re-snapshots the looper,
  then clears every pending Scene change and refreshes the effective track
  cache. No realign, roll/condition/epoch reset, PC, note-off or LED notify.

### Phase C7 — COMPLETE
- presetManager.c/.h: added preset_requestBackgroundBankLoad(),
  preset_backgroundBankLoadRunning(), preset_tickBackgroundBankLoad() and
  the one-byte pm_bg_phase state (phase + MIDI/Replace/rebuild/discard
  bits). The slot and act mask reuse pm_request_slot /
  pm_kit_request_scene_mask; the empty mask is derived from Replace.
- Phase 1 decides Scene 17; phase 2 drops queued copy/clear jobs, sets the
  AutoSave yield and waits for the facade/Pattern snapshot to become idle
  before discarding the Replace dirty mask; phase 3 builds and switches;
  phase 4 posts the ordinary two-mask Bank Load; phase 5 waits for Menu to
  acknowledge and releases the yield.
- copyClearService.c/.h: added ccSvc_dropQueued() (keeps the running job,
  restores early-trigger bits, traces each drop).
- filesystem.c/.h: added the one-byte persistent fs_autosave_yield and
  filesystem_setAutosaveYield(); both AutoSave schedulers decline new
  drains while it is set.
- Menu failure paths call the new menu_backgroundBankLoadAbort() so a
  refused phase 4 cannot strand the accepted command busy state.

### Phase C8 — COMPLETE
- seq_selectActivePattern() releases Scene 17 after a resident Scene has
  been published and the looper re-snapshotted. Per-track changes do not
  release.

### Phase C9 — COMPLETE
- AutoSave service tick gates the scalar and both Pattern scheduler calls on
  !scene_backgroundExists(); settings.cfg is unchanged (D24).
- seq_sendProgChg() refuses indices >= 16; seq_chainResolveNext() returns
  no target when the current Scene is 17.
- ccSvc_runPatternPaste() refuses an overlapping paste while Scene 17
  exists with new trace reason AUTOSAVE_TRACE_CC_DROP_SCENE17 (decoder
  updated).
- Bank Save on 17 writes the home Scene as active_scene; Scene Save from 17
  writes its directory with a zero HCNAMES publication mask.
- Scene number labels: no current LCD screen prints the active Scene number
  (the only "Sc NN" text is the boot empty-Scene notice, which is resident
  0..15), so D22 needs no code site today.

### Phase C10 — COMPLETE
- menu.c/.h: added menu_backgroundBankLoadUiBegin() (MIDI origin: enter
  Load:[Bank], explicit slot, Replace, all 16, accepted-command busy state,
  latched menu closed) and menu_backgroundBankLoadAbort().
- menu_loadSaveEnterTop() now delegates to
  menu_loadSaveEnterTopAt(page, what, bank_slot) so the MIDI path can install
  its slot before the entry index request.
- menu_pollPresetStatus() calls preset_tickBackgroundBankLoad() before the
  Preset completion dispatch.
- Menu Bank Load OK now posts through preset_requestBackgroundBankLoad()
  (Phase C), replacing the direct preset_loadBank() call.
 - Build: 589,816 / 753,664 B flash, headroom 163,848 B.

---

### Post-C10 verification fixes
- Preset/filesystem Bank Load: added PRESET_BANK_LOAD_REPLACE (0x02). A
  Replace with all 16 selected has empty_mask = 0, so the filesystem could
  not infer Replace from a nonempty empty mask; missing source children would
  have been kept instead of emptied. The background request now passes the
  flag, and boot still passes BOOT only.

- HCNAMES-authoritative boot loader: the effective present mask is now
  extended from Scene rows (bNNN sets, x clears except the active Scene) and
  its per-Scene loop handles EMPTY and BANK_CHILD sources. Without this, a
  Merge-saved mixed Bank lost its kept Scenes and marked them Case 3 on the
  boot path that runs when no AutoSave record is usable.
- Bank Save phase 45 keeps present Scenes whose slot child was removed in the
  kept-source set (they still hold RAM content); only non-present removed
  Scenes get the explicit x row.
- Bank Load phase 98 empty-Bank path now captures the old row-0 source and
  stages kept Scene provenance like the phase-20 commit.
- tools/verify_bank_autosave.py accepts missing children whose Scene row is
  x, accepts bNNN sources, and checks the S082 present-mask rules instead of
  requiring all 16 children and an exact mask.
- Final build and image: 589,816 B flash, 163,848 B headroom;
  build/LXRV2_lxr02.img written.

## Appendix A — Scene-index audit (`S082_P1` §5), generated at `8b1b647`

Every non-comment line in `Core/**/*.c` and `main.c` that reads
`scene_getActiveIndex()`, `seq_activePattern`, `seq_perTrackPattern[`,
`seq_getTrackPlayedScene(`, `preset_getSlotPlayedScene(`,
`menu_getViewedPattern()`, `menu_shownPattern` or `liveRec_target`. These are
the sources of index 16 while Scene 17 is active.

- **Rule** comes from the surrounding ±4 lines (§C3 legend). The implementer
  confirms each row while applying C3; multi-rule rows list every rule seen.
- The edit fan-out loops that do not read the active index directly are
  listed separately in C4.
- **Lines** are the hit lines.

| File | Function | Lines | Rule |
|------|----------|-------|------|
| `Bank/Scene/Pattern/PatternStackService.c` | `patSvc_desiredScene()` | 124 | P |
| `Bank/Scene/Preset/presetManager.c` | `preset_applyInstrumentRuntimeValueInternal()` | 842 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setInstrumentParameter()` | 973 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setSupplementalParameterInternal()` | 1031 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setInstrumentParameterFromMidi()` | 1070 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_getSlotPlayedScene()` | 1109, 1112, 1113 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_applyKitAudioRouting()` | 1136 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_clearAudioOutStepOverride()` | 1234 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setVoiceAudioOut()` | 1369 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setVoiceFxSendAmount()` | 1404 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setVoiceFxSendMorph()` | 1437 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setSlot6Track7AmpEnvelopeDecay()` | 1504 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_applySceneSettings()` | 1512 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_resetAndApplyKitVoiceImage()` | 1805 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_sendDrumsetParameters()` | 1877, 1879, 1881 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_startDrumsetApply()` | 1935, 1937, 1938 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_commitSingleVoiceSlot()` | 1996 | A? |
| `Bank/Scene/Preset/presetManager.c` | `preset_tickDrumsetApply()` | 2102 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_applyDeferredSceneSlotForTrigger()` | 2211, 2219 | A?/A |
| `Bank/Scene/Preset/presetManager.c` | `preset_commitStagedKitNormalToMorph()` | 2420, 2447 | R/A |
| `Bank/Scene/Preset/presetManager.c` | `preset_startKitMorphApply()` | 2470 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_startInstrumentMorphApply()` | 2515 | R |
| `Bank/Scene/Preset/presetManager.c` | `preset_startInstrumentApplyImage()` | 2603, 2691, 2699, 2701, 2703 | R/B/A |
| `Bank/Scene/Preset/presetManager.c` | `preset_loadDrumset()` | 2910, 2913 | B |
| `Bank/Scene/Preset/presetManager.c` | `preset_saveBank()` | 3096 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_morph()` | 3615 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_morphScene()` | 3652, 3656 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_morphVoice()` | 3674 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_morphVoiceScene()` | 3702, 3704 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_rebuildMorph()` | 3721 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_setBusCompSetting()` | 3871 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_syncBusCompMirrors()` | 3877 | A |
| `Bank/Scene/Preset/presetManager.c` | `preset_syncEffectMorphMirror()` | 3902 | A |
| `Bank/Scene/Preset/presetMorphEngine.c` | `presetMorph_writeRuntimeBaseEx()` | 498 | A |
| `Bank/Scene/Preset/presetMorphEngine.c` | `presetMorph_applyVoiceNowInternal()` | 669 | A |
| `Bank/Scene/Preset/presetMorphEngine.c` | `presetMorph_clearLfoSource()` | 795 | A |
| `Bank/Scene/Preset/presetMorphEngine.c` | `presetMorph_getResolvedVoiceAmount()` | 852 | A |
| `DSP/Effects/EffectsManager.c` | `effects_fanoutMask()` | 572 | F |
| `DSP/Effects/EffectsManager.c` | `effects_pasteRecord()` | 1066, 1067 | B |
| `DSP/Effects/EffectsManager.c` | `effects_resetRecord()` | 1100, 1101 | B |
| `DSP/Effects/EffectsManager.c` | `effects_resetMorphToNormal()` | 1160, 1161 | B |
| `DSP/Effects/EffectsManager.c` | `effects_resetMorphToNormalSingle()` | 1205 | A |
| `DSP/Effects/EffectsManager.c` | `effects_pasteSeqStep()` | 1249 | A |
| `DSP/Effects/EffectsManager.c` | `effects_clearSeqLanes()` | 1304 | A |
| `DSP/Effects/EffectsManager.c` | `effects_init()` | 1704 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_runtimeInit()` | 1360 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_resetRuntimeSlot()` | 1531 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_applySlot6AlternateDecay()` | 1777 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_noteRuntimeValueChanged()` | 2198 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_resolveModulationTarget()` | 2350 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_applySlotDecimationTarget()` | 2436 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_slotDecimationBase()` | 2464 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_descriptorImageBase()` | 2496 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_applyVelocitySceneTarget()` | 2543 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_updateLfoSceneDestination()` | 2614, 2638 | A |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_installLfoModulationTarget()` | 3010, 3018, 3033 | L |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_installVelocityModulationTarget()` | 3088, 3103 | L |
| `DSP/Instruments/InstrumentManager.c` | `instrumentManager_writeRuntimeInternal()` | 3563, 3589, 3604, 3627, 3640 | A |
| `DSPAudio/BusCompressor.c` | `busComp_sidechainTrigger()` | 183 | P |
| `DSPAudio/mixer.c` | `mixer_calcNextSampleBlock()` | 927, 978 | P |
| `Hardware/SD/filesystem.c` | `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()` | 24745 | A? |
| `Hardware/SD/filesystem.c` | `filesystem_requestLoad()` | 29764, 29767, 29770, 29775 | B |
| `Hardware/SD/filesystem.c` | `filesystem_requestSave()` | 30089 | A |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_selectBar()` | 671 | L |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_updateSubSteps()` | 708 | L |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_overlayTrigger()` | 980 | A |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_overlaySwitchHeld()` | 1107 | A |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_setRemoveStep()` | 1364 | V |
| `Hardware/frontPanel/buttonHandler.c` | `buttonHandler_perfRefreshHeldSceneLeds()` | 1508 | L |
| `Hardware/frontPanel/buttonHandler.c` | `handleSelectButton()` | 2053 | V |
| `Hardware/frontPanel/buttonHandler.c` | `handleVoiceButton()` | 2579, 2581 | V |
| `Hardware/frontPanel/buttonHandler.c` | `processPress()` | 3031, 3034, 3039 | L |
| `Hardware/frontPanel/buttonHandler.c` | `processRelease()` | 3257 | L |
| `Hardware/frontPanel/ledHandler.c` | `led_updateCurrentStep()` | 1347 | V |
| `Hardware/frontPanel/ledHandler.c` | `led_processSeqLedState()` | 1772 | L |
| `MIDI/MidiParser.c` | `midiParser_enterTaggedParameter()` | 133 | A |
| `MIDI/MidiParser.c` | `midiParser_getVoiceMidiNote()` | 437 | M |
| `MIDI/MidiParser.c` | `midiParser_getVoiceMidiChannel()` | 452 | M |
| `Menu/CopyClear/clearOps.c` | `ccClear_applyActiveSettings()` | 169 | A |
| `Menu/CopyClear/clearOps.c` | `ccClear_potTurned()` | 262 | V |
| `Menu/CopyClear/clearOps.c` | `ccClear_runSceneSettings()` | 345 | A |
| `Menu/CopyClear/clearOps.c` | `ccClear_runScene()` | 372 | A |
| `Menu/CopyClear/clearOps.c` | `ccClear_runFx()` | 426, 434 | F/A |
| `Menu/CopyClear/clearOps.c` | `ccClear_runResetMorphTrack()` | 489 | F |
| `Menu/CopyClear/clearOps.c` | `ccClear_runResetSceneMorph()` | 576 | F |
| `Menu/CopyClear/clearOps.c` | `ccClear_runResetFxMorph()` | 652, 661 | F |
| `Menu/CopyClear/copyClearService.c` | `ccSvc_patternChangedUi()` | 214 | L |
| `Menu/CopyClear/copyClearService.c` | `ccSvc_triggersChangedUi()` | 233 | L |
| `Menu/CopyClear/copyClearSession.c` | `cc_activeScene()` | 156 | V |
| `Menu/CopyClear/copyOps.c` | `ccCopy_activeIn()` | 634 | A |
| `Menu/CopyClear/copyOps.c` | `ccCopy_applyActiveSettings()` | 640 | A |
| `Menu/CopyClear/copyOps.c` | `ccCopy_runKit()` | 784 | R |
| `Menu/CopyClear/copyOps.c` | `ccCopy_runSceneSettings()` | 843 | A |
| `Menu/CopyClear/copyOps.c` | `ccCopy_runScene()` | 902, 911 | A |
| `Menu/CopyClear/copyOps.c` | `ccCopy_runMorphTrack()` | 1050 | A |
| `Menu/CopyClear/copyOps.c` | `ccCopy_runSceneMorph()` | 1156 | A |
| `Menu/menu.c` | `menu_startSoundApply()` | 559 | V |
| `Menu/menu.c` | `menu_finishSoundApply()` | 626 | V |
| `Menu/menu.c` | `menu_getParameterDisplayValue()` | 928 | V |
| `Menu/menu.c` | `menu_loadSamplesModal()` | 1610 | V |
| `Menu/menu.c` | `va_searchRestart()` | 2190 | V |
| `Menu/menu.c` | `va_scanService()` | 2469 | V |
| `Menu/menu.c` | `va_resolveHeldValue()` | 2528 | V |
| `Menu/menu.c` | `va_updateHeldState()` | 2604 | L |
| `Menu/menu.c` | `va_refreshAutomationLeds()` | 2934, 2943, 2946, 2950 | L |
| `Menu/menu.c` | `va_writeAutomationFromKnob()` | 3760 | V |
| `Menu/menu.c` | `sa_writeAutomationFromKnob()` | 3961 | V |
| `Menu/menu.c` | `sa_clearAutomationFromKnob()` | 4020 | V |
| `Menu/menu.c` | `sa_refreshAutomationLeds()` | 4072, 4075 | L |
| `Menu/menu.c` | `menu_enterStepTrackAutomationOverlay()` | 4284 | V |
| `Menu/menu.c` | `menu_resolveCellAbsolute()` | 4337 | A |
| `Menu/menu.c` | `menu_cellDisplayValue()` | 4862, 4863, 4869, 4893 | A |
| `Menu/menu.c` | `menu_cellCommitValue()` | 4962, 5127, 5144, 5183 | V/A |
| `Menu/menu.c` | `menu_lfoTargetContext()` | 5278 | A |
| `Menu/menu.c` | `menu_velocityTargetNormalize()` | 5548 | A |
| `Menu/menu.c` | `menu_velocityTargetEditParam()` | 5586 | A |
| `Menu/menu.c` | `menu_velocityTargetDisplayValue()` | 5613 | A |
| `Menu/menu.c` | `menu_formatInstrumentTargetShort()` | 5664, 5688 | A |
| `Menu/menu.c` | `menu_displayInstrumentTargetFull()` | 5710, 5738 | A |
| `Menu/menu.c` | `menu_clampInstrumentValue()` | 6162 | A |
| `Menu/menu.c` | `menu_refreshPerfSceneLeds()` | 8536, 8537 | V |
| `Menu/menu.c` | `menu_serviceDeferredSceneChange()` | 8769 | A |
| `Menu/menu.c` | `menu_refreshVoiceHeldSceneLeds()` | 8803 | F |
| `Menu/menu.c` | `menu_voiceHeldSceneButtonPressed()` | 8846 | R |
| `Menu/menu.c` | `menu_residentPresentSceneMask()` | 8867, 8868 | B |
| `Menu/menu.c` | `menu_resetLoadSaveSceneSelection()` | 8931, 8932, 8946 | B/A |
| `Menu/menu.c` | `menu_refreshLoadSceneLeds()` | 9011 | L |
| `Menu/menu.c` | `menu_loadInstrumentVoicePressed()` | 9238 | A |
| `Menu/menu.c` | `menu_instrumentLoadOptionCountForVoice()` | 9343, 9353 | A |
| `Menu/menu.c` | `menu_instrumentLoadOptionAt()` | 9367, 9377 | A |
| `Menu/menu.c` | `menu_loadSaveEnterInstrumentLoad()` | 9560 | A |
| `Menu/menu.c` | `menu_loadSaveEnterInstrumentSave()` | 9615 | A |
| `Menu/menu.c` | `menu_stepAutomationAddDefault()` | 11626 | V |
| `Menu/menu.c` | `menu_stepAutomationEnsurePage()` | 11643, 11650 | V |
| `Menu/menu.c` | `menu_stepAutomationEdit()` | 11779 | V |
| `Menu/menu.c` | `menu_stepAutomationExecuteItem0()` | 11978 | V |
| `Menu/menu.c` | `menu_repaintStepAutomation()` | 12031 | V |
| `Menu/menu.c` | `menu_encoderChangeParameter()` | 12650 | V |
| `Menu/menu.c` | `menu_moveToMenuItem()` | 12910 | V |
| `Menu/menu.c` | `menu_commitEffectMorphParam()` | 14007 | A |
| `Menu/menu.c` | `menu_liveRecRecordCell()` | 14234 | V |
| `Menu/menu.c` | `menu_pollPresetStatus()` | 15156, 15157 | V |
| `Menu/menu.c` | `menu_switchPage()` | 15866, 15905, 15975, 16010 | B/L/V/A |
| `Menu/menu.c` | `menu_parseGlobalParam()` | 16248, 16260, 16280, 16297, 16311, 16324, 16469, 16488, 16502, 16517, 16528, 16539, 16548, 16557, 16566, 16575, 16584, 16595, 16605 | L/V |
| `Menu/menu.c` | `menu_fxVoiceMixOverlayBegin()` | 16875 | V |
| `Menu/menu.c` | `menu_showStepEditPage()` | 16945 | V |
| `Menu/menu.c` | `menu_setShownPattern()` | 17007, 17009, 17013 | L/V |
| `Menu/menu.c` | `menu_setPlayedPattern()` | 17093 | V |
| `Menu/menu.c` | `menu_init()` | 17190 | L |
| `Menu/menuEffects.c` | `menuEffects_record()` | 93 | A |
| `Menu/menuEffects.c` | `menuEffects_enter()` | 224 | A |
| `Menu/menuEffects.c` | `menuEffects_cellValue()` | 389 | A |
| `Menu/menuEffects.c` | `menuEffects_cellCommit()` | 459 | A |
| `Menu/menuEffects.c` | `menuEffects_editModeChanged()` | 656 | A |
| `Menu/menuEffects.c` | `menuEffects_service()` | 748 | A |
| `Menu/menuEffects.c` | `menuEffects_holdEdit()` | 1049, 1054 | A |
| `Menu/menuEffects.c` | `menuEffects_shownParam()` | 1088 | A |
| `Menu/menuEffects.c` | `menuEffects_editParam()` | 1119 | A |
| `Sequencer/LiveRecord.c` | `?()` | 68 | P |
| `Sequencer/LiveRecord.c` | `liveRec_pushEraseMasks()` | 272, 278 | P |
| `Sequencer/LiveRecord.c` | `liveRec_writeStep()` | 325, 326 | P |
| `Sequencer/LiveRecord.c` | `liveRec_newTake()` | 345, 346 | P |
| `Sequencer/LiveRecord.c` | `liveRec_init()` | 354, 355 | P |
| `Sequencer/LiveRecord.c` | `liveRec_service()` | 533, 534, 535 | P |
| `Sequencer/LiveRecord.c` | `liveRec_targetValue7()` | 577 | P |
| `Sequencer/LiveRecord.c` | `liveRec_recordValue7()` | 687 | P |
| `Sequencer/LiveRecord.c` | `liveRec_targetOwned()` | 735, 737, 752, 755, 761 | P |
| `Sequencer/LiveRecord.c` | `liveRec_writeCurrentValue()` | 784 | P |
| `Sequencer/LiveRecord.c` | `liveRec_stepEnd()` | 856, 939 | P |
| `Sequencer/LiveRecord.c` | `liveRec_activePatternLocked()` | 1079 | P |
| `Sequencer/LiveRecord.c` | `liveRec_triggerIn()` | 1288, 1292, 1296, 1298 | P |
| `Sequencer/LiveRecord.c` | `liveRec_paramIn()` | 1483, 1501 | B/P |
| `Sequencer/sequencer.c` | `seq_pendingSceneIsChain()` | 676, 691 | P |
| `Sequencer/sequencer.c` | `seq_recomputePerTrackActive()` | 723 | P |
| `Sequencer/sequencer.c` | `seq_restoreAllAutomation()` | 1347 | P |
| `Sequencer/sequencer.c` | `seq_restoreAllSceneAutomation()` | 1378 | P |
| `Sequencer/sequencer.c` | `seq_fxClockTick()` | 1485 | P |
| `Sequencer/sequencer.c` | `seq_fxRealignToMasterClock()` | 1557 | P |
| `Sequencer/sequencer.c` | `seq_refreshTrackEffectiveParams()` | 1621 | P |
| `Sequencer/sequencer.c` | `seq_init()` | 1687 | P |
| `Sequencer/sequencer.c` | `seq_setTrackPlayedScene()` | 1776, 1779, 1780 | P |
| `Sequencer/sequencer.c` | `seq_clearPerTrackOverrides()` | 1875 | P |
| `Sequencer/sequencer.c` | `seq_getTrackPlayedScene()` | 1890, 1893, 1894 | P |
| `Sequencer/sequencer.c` | `seq_triggerVoice()` | 1957, 1964 | P |
| `Sequencer/sequencer.c` | `seq_previewVoice()` | 2000, 2014 | P |
| `Sequencer/sequencer.c` | `seq_selectActivePattern()` | 2059, 2060, 2064, 2068, 2142, 2143 | L/P |
| `Sequencer/sequencer.c` | `seq_alignActivePatternToScene()` | 2193, 2197 | P |
| `Sequencer/sequencer.c` | `seq_queueStepAutomations()` | 2286 | P |
| `Sequencer/sequencer.c` | `seq_trackEffectTypeMatchesActive()` | 2373, 2374 | P |
| `Sequencer/sequencer.c` | `seq_advanceTrackStep()` | 2755, 2891, 2898, 2939, 2942, 2953, 2968, 2987, 3023, 3038, 3040, 3044, 3046, 3176 | P |
| `Sequencer/sequencer.c` | `seq_applySceneAutomation()` | 3255 | P |
| `Sequencer/sequencer.c` | `seq_drainPendingAutomation()` | 3390 | P |
| `Sequencer/sequencer.c` | `seq_restoreAutomatedParameters()` | 3574 | P |
| `Sequencer/sequencer.c` | `seq_rampPredict()` | 3709 | P |
| `Sequencer/sequencer.c` | `seq_rampStepEvent()` | 3770 | P |
| `Sequencer/sequencer.c` | `seq_rampTrackOn()` | 3971 | P |
| `Sequencer/sequencer.c` | `seq_rampService()` | 4129 | B |
| `Sequencer/sequencer.c` | `seq_handleMasterBoundary()` | 4209, 4214, 4217, 4227, 4228 | L/P |
| `Sequencer/sequencer.c` | `seq_realignActivePatternToMasterClock()` | 4342 | P |
| `Sequencer/sequencer.c` | `seq_realignTrackToMasterClock()` | 4429 | P |
| `Sequencer/sequencer.c` | `seq_shiftPositionQ8()` | 4555 | P |
| `Sequencer/sequencer.c` | `seq_loopCanonicalPreAdvance()` | 4670 | P |
| `Sequencer/sequencer.c` | `seq_captureTrackTime()` | 4724, 4727 | P |
| `Sequencer/sequencer.c` | `seq_loopableTrackMask()` | 5374 | P |
| `Sequencer/sequencer.c` | `seq_chainEvaluate()` | 5526 | P |
| `Sequencer/sequencer.c` | `seq_fireDeferred()` | 5679 | P |
| `Sequencer/sequencer.c` | `seq_markRecordedStep()` | 6218 | L |
| `Sequencer/sequencer.c` | `seq_setStepIndexToStart()` | 6410 | P |
| `main.c` | `main()` | 1364 | A |
### Phase D — COMPLETE (docs and tools)
- FILESYSTEM_SPEC.md: S082 Bank Load/Save outcome, two masks, present-mask
  assignment, runtime active-Scene rule, boot-only active_scene, Merge Save
  manifest read, bNNN/x tokens, Bank preview probe.
- AUTOSAVE.md: present-mask assignment, Merge re-mark, Scene 17 suspension,
  new source tokens.
- MODULE_INTERCHANGE_SPEC.md: Bank page/COPY/labels/retention, name editing,
  Instrument chg, Scene 17 rules, and the new Preset APIs.
- BANK_PRESET_ARCHITECTURE.md: legacy active_scene, edit-mask mutual
  exclusion, Scene 17 mask.
- COPYCLEAR_UTILITIES.md: Scene 17 source/destination, overlap refusal, 32-bit
  fan-out, no present bit for a Kit copy into 17.
- PATTERN_DYNAMIC_STACK.md: background region use 4 (Scene 17) and
  pat_regionFor().
- STORAGE_SRAM_MANIFEST.md: S082 allocation table (16 B outside Scene 17 plus
  1,658 B for Scene 17).
- MEMORY.md: closed the runtime Bank Load active-Scene bug, Load/Save pot
  repaint rule, Scene 17 invariants, and card preparation (delete .hcnames,
  .hcnamtmp, .hcprms1/.hcprms2).
- tools/verify_bank_autosave.py and tools/decode_devlogs.py updated; both
  files compile with python3 -m py_compile.
- S082_P1_LOAD_SAVE.md §7 records the implementation notes.

---

## 12. Post-implementation review and first-boot freeze assessment (2026-10-18)

*Code review of the uncommitted working tree on `45846b7`, plus the card copy
`SD_CARD_082_P0/` (`asavetrc.bin` decoded with `tools/decode_devlogs.py`).
No code was changed for this assessment.*

### 12.1 The report

On the first boot of the S082 image the user opened Load/Save and turned
endless pot 1 to scroll the Load/Save type. The screen froze and did not
recover without a power cycle. Later boots did not reproduce it. The user
suspected AutoSave interference.

### 12.2 What the trace shows

The new-firmware sessions start at record #051462: the first
`AUTOSAVE_TRACE_STAGE_BANK_PREVIEW` (`b`) record is #051480.

| Record | Tick (ms) | Event |
|-------:|----------:|-------|
| #051462 | 1444 | **Session 1** (first boot): boot reader done, no Case 2/3 |
| #051463 | 3191 | AutoSave writer armed; deadline 8191 |
| #051467 | 8192 | **Parameter drain admitted** (owns the filesystem facade) |
| #051468 | 9394 | **Load/Save entered** (`menu_requestKitEntryNames()` branch 1); the HCNAMES request is refused because the drain owns the facade, and deferred |
| #051472–74 | 10693–10846 | Drain validates, merges, **finishes DONE** |
| #051475–76 | 10846–11061 | Deferred Kit entry retried; Kit index accepted |
| #051477 | 15440 | 5-second budget report |
| #051480 | 15627 | Bank child preview complete (16 present, 16 with content) |
| — | — | **No further record**, then **session 2** boots (#051481) |

How to read the end of session 1:
- The trace flush runs every 500 ms whenever the facade is idle and no OK/OW
  command is active (`filesystem_autosaveTraceFlushSchedule_tick()`;
  browsing does not block it).
- A budget report (`H`) is produced every 5 s by `filesystem_tick()`.
- The preview record at 15627 reached the card, so the facade was idle at
  least once after it. No `H` record after 15440 ever reached the card.
- So from shortly after 15627 until the power cycle, either the facade
  stayed **BUSY**, or the main loop stopped. The records describing the
  freeze itself were in RAM and were lost at reset.

Session 1 is also the **only** session in which Load/Save was entered while
an AutoSave drain owned the facade:
- in sessions 2 and 3, Load/Save was entered (5209 ms, and similar) before
  the writer's deadline;
- later arms were held by the Load/Save page guard (`W` record #053039).

### 12.3 Assessment

The freeze is **not proven** by the card: its own records were never written.
The evidence and the code support this account.

1. **AutoSave's role is indirect.** The drain behaved correctly: it finished
   DONE at 10846 and wrote nothing afterwards. What it did was delay the
   Load/Save entry requests, so the user's pot-1 scrolling overlapped
   in-flight index loads and the new, longer Bank child preview (16 child
   visits and PAT4 reads on a cold first-boot card). These overlapping
   windows exist only when Load/Save is entered during a drain, which
   happened only in session 1.
2. **All four Load/Save pots bypass the storage busy gate (pre-existing,
   widened by S082).**
   - `menu_parseEncoder()` (`menu.c:14274` ff.) drops input while
     `menu_storageBusy || preset_getStatus() != PRESET_IDLE`, except plain
     Load-number turns.
   - The pot path `menu_parseKnobDelta()` → `menu_handleLoadSaveKnobDelta()`
     (`menu.c:14911`, `10141`) checks only `menu_loadSaveCommandActive`.
   - So pot 1 can change page, type or nested Instrument mode
     (`menu_loadSaveEnterTopAt()`, `menu_loadSaveEnterInstrumentLoad()`) and
     call `filesystem_clearNameCache()` while an index load, HCNAMES read,
     `.hctmp` save, Instrument apply or Bank preview owns the facade.
   - S082 lengthened the Bank preview (B1 probe) and added a Save:[Bank]
     preview, so these windows are now seconds long instead of milliseconds.
3. **Refusal and completion paths overwrite the busy owner.**
   - `menu_requestLibraryIndexLoad()` (`menu.c:8012`) and
     `menu_requestKitEntryNames()` (`menu.c:7759`) set `menu_storageBusy = 1`
     and then `0` on refusal, even when another request (for example the
     Bank preview) owns the flag.
   - `menu_bankLoadPreviewComplete()` clears it unconditionally.
   - After a pot-1 sweep, Menu can therefore believe it is idle while the
     facade is busy, or the reverse, and completions run against a context
     that has already changed.
4. **Deferred retries can go to the wrong owner (pre-existing).** The
   dispatcher (`menu.c:15460-15478`) re-posts every deferral that is not a
   nested pool reload through `menu_requestCurrentLoadSaveSelection()`, which
   acts on the last **top-level** type.
   - A deferral raised by nested Instrument entry (HCNAMES / `.hctmp` /
     typed index) during a pot-1 sweep is re-posted as, for example, a Bank
     preview.
   - The Instrument session then never completes its entry. The trace's
     later sessions show such sweeps: bursts of refused Kit index requests
     (#052380–#052385) and Instrument entries every ~11 ms
     (#053374–#053382).
5. **The new probe has wait points that never time out.**
   `filesystem_scanBankScenes_tick()`:
   - case 18 (`filesystem.c:16263`) returns on `afatfs_fread() == 0` forever
     at end of file (a short or foreign `.pat`);
   - cases 12 and 16 (`16157`, `16223`) retry `afatfs_opendir_lfn()` /
     `afatfs_fopen_lfn()` forever if the handle pool refuses;
   - `FS_INTERNAL_OP_SCAN_BANK_SCENES` has no `filesystem_pollPhaseStall()`
     observer, so such a wait would hold the facade BUSY indefinitely and
     leave Menu gated with no trace.

   On this card the probe itself completed (content `0xffff`). This is a
   hazard, not the proven cause.

**Most probable mechanism:**
1. A fast pot-1 sweep during the first-boot overlap (item 1) left Menu and the
   facade out of step (items 2–4): the facade busy, or Menu's busy gate never
   released.
2. Every input path was then refused: encoder and MODE are gated on
   `menu_storageBusy` / Preset status, and the page switch queues behind them
   (`menu_pendingPageSwitch`).
3. The screen stopped repainting because both pot paths repaint only when
   `!menu_storageBusy`.

The absence of any later `H` record is consistent with the facade staying
BUSY. A main-loop hang cannot be excluded, because `DEV_LOGGING_IWDG` is 0.

### 12.4 Review findings (code check of the S082 implementation)

| Id | Severity | Location | Finding | Fix (not applied) |
|----|----------|----------|---------|-------------------|
| F1 | **High** | `presetManager.c:3523` ff. `preset_tickBackgroundBankLoad()` phases 1–3 | Phase 1 computes "Scene 17 needed" into `PM_BG_REBUILD`, but phase 3 **always** builds Scene 17 and calls `seq_switchToBackgroundScene()`. Every Menu Bank Load, including a stopped one (D14) and a Merge that keeps every played Scene (D25), ends on Scene 17: PERF shows no Scene, AutoSave is suspended until the next Scene change, and the user's Scene has changed (against S9/S25). Phase 2's copy/clear drop, AutoSave yield and Replace discard also run when not needed. | Use one bit `PM_BG_NEEDED`. Phase 1: not needed → phase 4 directly (no drop, yield, discard or build). Needed → phase 2. Keep a separate rebuild-in-place test (`scene_backgroundExists()`) for C5. |
| F2 | Medium | `menu.c:14911` → `menu_handleLoadSaveKnobDelta()` | All Load/Save pots bypass the busy gate that the encoder applies (12.3 item 2). The most probable freeze route. | Apply the encoder's gate at the top of `menu_handleLoadSaveKnobDelta()`: while `menu_storageBusy || preset_getStatus() != PRESET_IDLE`, drop the delta, except pot 2 on the Load-number field under the same conditions as `allow_load_number_scroll` in `menu_parseEncoder()`. Pot behaviour is otherwise unchanged (S14). DEV trace one record per dropped burst. |
| F3 | Medium | `menu.c:8012`, `7759`, `menu_bankLoadPreviewComplete()` | Requesters overwrite another owner's `menu_storageBusy` on refusal; the preview completion clears it unconditionally (12.3 item 3). | A refused request must not write `menu_storageBusy` unless it set it from 0 itself (capture `was_busy` first), or refuse early when `menu_storageBusy` is already set. Completions clear only the busy flag they own. |
| F4 | Medium | `filesystem.c:16157-16311` probe | Unbounded waits; no stall observer (12.3 item 5). | Case 18: `n == 0 && afatfs_feof(op_file)` → close, content unknown (bit 0). Cases 12/16: an open refusal counts as a failed child after a bounded retry count. Add the `filesystem_pollPhaseStall()` observer to the scan op, as other ops have, with a named error code so a stall ends DONE/ERROR instead of holding the facade. |
| F5 | Medium | Boot #053402–#053403 | The latest boot emptied **Scene 2** as Case 3: Kit row 18 ("Barf") was refreshed with direct source 000 and the narrow Kit load failed, although `Kit/000 Barf` exists. A Kit Load into Scene 2 had committed at tick 45929 in the previous session (#052553–#052947). No earlier session on this card has a Case 3. **Not yet explained**; possibly S082-related (boot reader changes B3.10). | Reproduce: Kit Load into a non-active Scene, wait for the AutoSave drain, copy `.hcnames` + `.hcprms*` **before** rebooting, reboot, then compare. Check `filesystem_bootReaderNarrowLoadKit()` against the new `filesystem_bootReaderEvaluateScene()` dispatch and the S082 changes in `filesystem_bootHcnamesAuthoritativeLoad()`. |
| F6 | Low | `menu.c:15460-15478` | Deferred retries are re-posted to the top-level request even when the deferral came from nested Instrument entry (12.3 item 4; pre-existing). | Dispatch nested-Instrument deferrals to `menu_requestInstrumentEntryNames()` when the entry is incomplete (no valid scratch / temp / typed index), and to the pool request otherwise. |
| F7 | Low | `presetManager.c:3476` `preset_requestBackgroundBankLoad()` | Replace is inferred as `empty_mask != 0 || act_mask == 0xFFFF`, so a Merge in which all 16 source Scenes have content and are selected becomes a Replace (AutoSave discard instead of a whole-Bank re-mark; a vanished child is emptied instead of kept). | Pass the mode explicitly from Menu (an origin/mode flags argument). |
| F8 | Low | B1.6 trace (`filesystem.c` case 11) | The preview record carries the masks but not the elapsed time, so R1 (preview duration on slow/fast cards) cannot be measured from the trace. | Record the start tick at phase 0 and put the elapsed ms in `flags` (saturated) or in a second record. |

The rest of the reviewed diff matches the schedule:
- A1: pots 2–4 coalesced; BAR2 no longer blanks.
- A2: `chg` state, commit, clamp, apply-time finalisation (moved to
  `menu_finishInstrumentApplySession()`, as the log says).
- B1 probe: handle use is balanced on every path; the scratch is cleared
  per start in `filesystem_start()`.
- C4 32-bit fan-out.
- C9 gates.

The Appendix A audit rows were not re-verified one by one in this review.

### 12.5 Recommended order

1. **F1**: functional bug on every Bank Load.
2. **F2 + F3**: the freeze class.
3. **F4**: removes the silent facade-hang route.
4. **F5**: investigate with the capture protocol below. It can lose a
   Scene's contents at boot.
5. F6–F8.

### 12.6 Reproduction and capture protocol for the freeze

1. Build with `DEV_LOGGING_IWDG 1` for this test only (`config.h:200`). A
   main-loop hang then resets and leaves the `.devwdg_noinit` capsule in
   `/bootlog.bin`; a facade stall does not reset (`filesystem_tick()` keeps
   feeding), so F4's stall observer is what records that case.
2. Make AutoSave dirty: edit any parameter (or use a first boot after
   flashing), leave Load/Save, and wait for the `S` arm.
3. Enter Load/Save **after** the writer deadline, while the drain runs (about
   5–8 s after the edit).
4. Turn pot 1 continuously, fast, from Kit through Scene, Bank, Pattern and
   Samples into the Instrument Load entries and back, especially while the
   Bank index and preview are loading.
5. On a freeze, wait 10 s, power off, and copy the card. Decode
   `asavetrc.bin` and `bootlog.bin`.
6. With F2 applied, the same sweep must show the pot deltas dropped (DEV
   trace) and no freeze. With F4 applied, a forced short `.pat` in a Bank
   child must end the preview with an error code instead of a hang.

**Status:** the freeze is **unresolved and not reproduced**; F1–F8 are
**open**; nothing in this section has been hardware-verified.

---

## 13. Review fixes and Scene 2 evidence instrumentation (2026-10-18)

*Applied to the working tree after §12. User context for F5: the Scene 2
invalidation followed a **rapid spin test**, turning all four endless pots and
the main encoder fast at the same time, on Scene 2. That means Kit Loads (Kit
Load live-loads on every slot change) interleaved with repeated Load/Save type
changes.*

### 13.1 Fixes applied

| Id | File : anchor | Change |
|----|---------------|--------|
| F1 | `presetManager.c` `preset_tickBackgroundBankLoad()` phases 1/3; `PM_BG_REBUILD` renamed `PM_BG_NEEDED` | Phase 1: Scene 17 needed → phase 2 (drop/yield/discard → build → switch); not needed → **phase 4 directly** (ordinary two-mask Bank Load; no copy/clear drop, no AutoSave yield or discard, no Scene 17). Phase 3 rebuilds in place only when Scene 17 already exists (`scene_backgroundExists()`). A stopped load (D14) and a Merge that keeps every played Scene (D25) no longer change the user's Scene. |
| F7 | `presetManager.h` `PRESET_BANK_REQUEST_MERGE` (0x80, ORed into `origin`); `preset_requestBackgroundBankLoad()`; `menu.c` Bank OK | The mode is stated by Menu (`menu_bankMergeOn()`) instead of inferred from the masks; MIDI origin is always Replace (Q15). An all-16 Merge stays a Merge. |
| F2 | `menu.c` `menu_handleLoadSaveKnobDelta()` (top) | The Load/Save pots obey the encoder's storage gate: while `menu_storageBusy` or Preset is not idle, every pot delta is dropped, except pot 2 on the Load-number field for Kit/KitMrp or nested Instrument Load (the same turn `menu_parseEncoder()` allows). Pot behaviour is otherwise unchanged (S14). |
| F3 | `menu.c` `menu_requestLibraryIndexLoad()`, `menu_requestKitEntryNames()`, `menu_requestResidentNameScratch()` | A refused request restores `menu_storageBusy` to its prior value instead of forcing 0, so it no longer releases the gate of another in-flight request or Instrument apply. |
| F4 | `filesystem.c` `filesystem_scanBankScenes_tick()` case 18; new stall observer at the top of the tick; `filesystem_requestScanBankScenes()` rearm; `AutosaveTrace.h` `AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_PREVIEW` = 10 | A zero-byte read at EOF or with the filesystem not ready closes the `.pat` with no content bit (no longer waits forever). With `DEV_STALL_DETECTION`, 20,000 polls in one phase record an `X` stall, close the preview's handles, return to root and finish with error `BkPv`. |
| F6 | `menu.c` deferred-selection dispatcher in `menu_pollPresetStatus()` | A deferral raised while nested Instrument Load/Save is open is re-posted through `menu_requestInstrumentEntryNames()` (except the existing pool-number retry), no longer through the top-level type's request. |
| F8 | `filesystem.c` case 11 trace; `AutosaveTrace.h` `b` comment | The `b` record's `flags` byte now carries the elapsed preview time in 64 ms units (saturated at 255). |
| F9 | `filesystem.c`: 7 HCNAMES request functions (`filesystem_requestLoadResidentInstrumentName`, `…UpdateResidentInstrumentNames`, `…LoadResidentKitName`, `…UpdateResidentKitNames`, `…LoadResidentSceneName`, `…CopyResidentNames`, `…UpdateResidentSceneNames`); rule documented in `filesystem_prepareResidentNamesCache()` | **New, from the Scene 2 analysis (13.3).** The register reset (`filesystem_prepareResidentNamesCache()`, which turns every non-dirty source into UNKNOWN while **keeping** the refreshed witness) now runs only **after** `filesystem_start()` accepts the operation. Before, a refused start left the reset applied with no read to restore it. |

Not changed: F5 (root cause still open; evidence below).

### 13.2 Evidence instrumentation added (DEV logging only)

A new trace stage **`h` = `AUTOSAVE_TRACE_STAGE_HCNAMES_PROVENANCE`**
(`AutosaveTrace.h`, `AUTOSAVE_TRACE_HCN_*` flags), plus one new boot-reader
record:

| Record | Producer | Fires when | value32 |
|--------|----------|------------|---------|
| `h` 0x01 REFRESHED_RESET | `filesystem_prepareResidentNamesCache()` | refreshed, non-dirty Scene/Kit/Instrument rows are about to become UNKNOWN | count, first row, current_op, op_phase |
| `h` 0x02 WRITE_UNKNOWN_REFRESHED | `filesystem_formatResidentNameLine()` | a present Scene/Kit/Instrument row is **written to the card** as `?` + R (the durable Case-3 precursor) | row, current_op, op_phase |
| `h` 0x03 KIT_SOURCE_STAGED | `filesystem_setResidentSource()` | a Kit row (17..32) source is staged | row, source |
| `h` 0x04 REFRESH_ON_UNKNOWN | `filesystem_setResidentRefreshed()` | R is raised on a Scene/Kit/Instrument row whose source is UNKNOWN | row, current_op, op_phase |
| `h` 0x05 KIT_LOAD_CONTEXT | Menu `PRESET_OP_KIT_LOAD` completion | a Kit Load completes after Menu left Load:[Kit]/[KitMrp] (or entered nested Instrument) | what, page, request mask |
| `h` 0x06 POT_DROPPED | `menu_handleLoadSaveKnobDelta()` | first pot delta of a burst dropped by the F2 gate | knob, what, page, busy |
| `Q` 0x08 Case-3 detail | `filesystem_bootReaderEvaluateScene()` | right after every Case-3 record | raw register word of the failing row (value, R bit 13, dirty bit 15) \| reason \<\<16 \| sub \<\<24 |

Case-3 reasons come from the new `FS_BOOT_FAIL()` codes in the narrow Scene
and Kit loaders:

| Code | Meaning |
|-----:|---------|
| 1 | unresolvable source |
| 2 | folder not entered |
| 3 | `sceneset.scg` not opened |
| 4 | `sceneset.scg` parse/finalize |
| 5 | `kitset.kcg` parse/finalize |
| 6 | Instrument member file (sub = slot) |
| 7 | Scene storage missing |
| 9 | other loader (sub = row index) |

Notes:
- The existing Case-3 record never carried the source: the decoder's
  "source=0x0000" was always zero. The new record replaces that guess.
- `tools/decode_devlogs.py` decodes all of the above, the `b` elapsed time
  and stall site 10. It compiles and re-decodes `SD_CARD_082_P0/asavetrc.bin`
  unchanged.

### 13.3 Analysis: Scene 2 invalidation (F5)

**Facts:**
- The last boot (#053402/03) emptied Scene 2 because Kit row 18 failed to
  resolve.
- `Kit/000 Barf` and its `kitset.kcg` and six members are intact on the card.
- In the previous session a Kit Load committed into Scene 2 at tick 45929.
- That session shows the spin: bursts of refused Kit index requests
  (#052380–#052385, six in 30 ms) and Instrument entries every ~11 ms.

**Mechanism this points to (consistent with the evidence, not yet proven):**
1. A `?` + R Kit row is exactly what makes boot empty a Scene (Case 3:
   refreshed, unresolvable).
2. A committed Kit Load stages the Kit row's source as **dirty**, which
   survives a register reset. So the row must have lost its staged source
   and kept R through a register image that was **reset but not fully
   re-read** before a writer persisted it.
3. Two routes produce such an image:
   - **(a)** a refused HCNAMES request after its reset (fixed by F9);
   - **(b)** pot-1 type changes during in-flight name reads, `.hctmp` saves
     and Kit Loads, with the seven-name session ended and restarted under a
     running Kit Load (made unreachable by F2's gate; the busy-owner loss
     fixed by F3).

   Rapid all-pot spinning on the Kit page is the condition that exercises
   both.
4. A third candidate cannot be excluded until the `h` records show the
   writer: an AutoSave post-drain register publication using a partially
   restored image.

**What the new records will show on the next occurrence:**
- `h` 0x02 names the operation (`current_op`/`op_phase`) that wrote row 18
  as `?` + R.
- `h` 0x01 or 0x04 just before it shows where R survived a reset.
- `h` 0x03 shows the last staged Kit source.
- `h` 0x05 shows whether the Kit Load completed out of context.
- At the next boot, `Q` 0x08 shows the register word and failure reason
  (reason 1 = unresolvable, i.e. the `?` + R route; reasons 2/5/6 = a real
  folder/kitset/member failure, which would point elsewhere).

### 13.4 Build results

- `make all && make img`: no new warnings (only the existing unused-function
  warnings in `filesystem.c`).
- `text=590,528`, `data=416`, `bss=430,816`; flash payload 590,944 /
  753,664 B (**162,720 B free**, +1,128 B over the previous image).
- New static storage is all logging/diagnostic:
  - `menu_loadSavePotDropBurst` 1 B (`DEV_MODE_LOGGING`);
  - `op_bank_preview_start_tick` 2 B (`DEV_MODE_LOGGING`);
  - `fs_boot_fail_reason`/`_sub` 2 B (`DEV_MODE_LOGGING`);
  - preview stall observer 5 B (`DEV_STALL_DETECTION`, the same class as
    the existing observers).

  There is no production RAM change. The fixes F1–F3, F6, F7 and F9 add no
  storage.
- **Not hardware-tested.** No host test exists for these paths.

### 13.5 Test protocol

1. **F1:**
   - stopped transport, Load:[Bank] OK → PERF still shows the same Scene; no
     Scene 17; AutoSave arms afterwards;
   - running, Merge that excludes the playing Scene → no Scene 17;
   - running Replace → Scene 17 (D2–D5).
2. **F2/F3:** spin all four pots and the encoder rapidly across every
   Load/Save type and nested Instrument Load for ≥ 30 s, including during the
   Bank preview and while an AutoSave drain runs at entry (§12.6).
   - Expect no freeze; `h` 0x06 bursts in the trace; pots inert only while
     storage is busy.
3. **F4:** put a truncated `.pat` (< 1,952 B) in one Bank child → the
   preview completes (that child unlit) or ends with `BkPv`; never a hang.
4. **F5 capture:**
   - repeat the Scene 2 spin (Kit page, all pots + encoder, active Scene 2);
   - exit Load/Save, wait for the AutoSave drain (`T` record);
   - **copy the card before rebooting** (`.hcnames`, `.hcprms1/2`,
     `asavetrc.bin`);
   - reboot, copy again;
   - look for `h` 0x02/0x01/0x04 on rows 17..32 and `Q` 0x08 at boot.
5. Regression: Kit Load/Save, Instrument Load (`kit` row, `chg`), Scene and
   Bank Load/Save round trips (§10 B3 rows).

**Status:**
- F1, F2, F3, F4, F6, F7, F8 and F9 are applied, built, and **not
  hardware-verified**.
- F5 is **open**, with instrumentation in place.
- The first-boot freeze is **not reproduced**; F2/F3/F4 address its most
  probable routes.
