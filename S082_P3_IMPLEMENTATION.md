# S082 P3 — Implementation Schedule (work while a background Bank load runs)

*Written 2026-10-19 from a code deep dive of the working tree: `45846b7` plus
the uncommitted S082 P0, P1, P2 and scroll-fix changes. Planning only: no code
has changed. Implements `S082_P3_WORK_WHILE_BG_LOAD.md` (W1–W5, D40–D53).*

---

## 0. How to use this schedule

### 0.1 Line numbers

- Every location is `file:line` in the working tree described above. Each entry also names its **anchor function**, because line numbers move as soon as the first edit lands.
- **Commit the working tree first**, so these numbers are pinned.
- Within one file, apply edits bottom-up, or re-find each anchor by name.

### 0.2 Actions

| Tag | Meaning |
|-----|---------|
| **ADD** | New code (function, declaration, branch, constant, static). |
| **MOD** | Change existing code in place. |
| **REM** | Remove existing code. |
| **DOC** | Comment-only or documentation change. |

### 0.3 Comment blocks

- Every ADD and MOD carries the block to paste next to it, in the project format: What / Why / Inputs / Outputs / Accessors / Affiliates.
- `.h` declarations carry the shortened form: What / Inputs / Outputs / Accessors.
- Where a MOD only changes a condition, the block replaces or extends the comment directly above that condition.

### 0.4 Order and build

| Step | Area | Files |
|------|------|-------|
| S1 | Load availability in BankData | `BankData.c/.h` |
| S2 | Bank Load: progressive flag, early present bits, edit masks | `filesystem.c/.h` |
| S3 | Preset: unlocked flag, flag pass-through, availability end | `presetManager.c/.h` |
| S4 | Two-slot fast blink | `ledHandler.c/.h` |
| S5 | Menu: gates, frozen screen, LEDs, completion | `menu.c/.h` |
| S6 | ButtonHandler gates and Scene selection | `buttonHandler.c` |
| S7 | No chain evaluation during a load | `sequencer.c` |
| S8 | Copy/clear during a load | `copyClearSession.c`, `copyClearService.c`, `AutosaveTrace.h`, `decode_devlogs.py` |
| S9 | Effect fan-out | `EffectsManager.c` |
| S10 | Documentation | spec files, `STORAGE_SRAM_MANIFEST.md` |

- S1 must land first; every later step calls its API.
- Build after each step (`make all && make img`) and run the link budget (`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`).
- There must be no new warnings beyond the existing unused-function set in `filesystem.c`.

### 0.5 RAM (D47, approved 2026-10-19: up to 4 B)

| Item | Bytes | Region | Owner | Note |
|------|------:|--------|-------|------|
| `bank_load_unavailable_mask` | 2 | SRAM1 static | BankData | Replaces the spec's filesystem done mask (§0.6 a). Same bytes, different owner. |
| `led_fastBlinkLed[2]` (was 1 B) | +1 | SRAM1 static | LED | Second fast-blink slot. |
| `menu_bgLoadAvailCount` | 1 | SRAM1 static | Menu | SEQ LED repaint trigger. |
| `PM_BG_UNLOCKED` | 0 | spare bit of `pm_bg_phase` | Preset | — |
| **Total** | **4** | | | Within D47. |

### 0.6 What the deep dive changed against the spec

The spec stands; these are implementation decisions and corrections found in
the code.

**a. Availability lives in BankData, not in a filesystem done mask.**
- BankData sits below Preset, Menu, Sequencer, copy/clear and EffectsManager, and is the only module all of them include. BankData cannot include filesystem or Preset.
- The schedule therefore keeps one 2 B **unavailable mask** in BankData:
  - it is set to the request's load and empty sets when the filesystem accepts the request;
  - a child's bit clears when that child completes;
  - the child's present bit is set at that moment.

  "Available" is then `present & ~unavailable`.
- Outside a load the mask is 0, so every changed caller behaves exactly as today.
- No filesystem done mask is needed.

**b. Leaving Load/Save needs no deferral (spec §5.4, R6).**
- The page exit only invalidates the name scratch and clears the browser cache (`menu.c:17261-17281`).
- The HCNAMES flush is already a background write that waits for an idle card (`menu_triggerDeferredHcnamesFlush()`).
- Nothing changes there.

**c. D48 is already true.** `menu_loadSaveCommandInNoPlaybackScope()` returns 0 off Load/Save (`menu.c:1862-1879`), so leaving the page ends the codec suspension. There is no code change.

**d. Spec §7.6 audit item 1 is resolved.**
- Every Preset or filesystem request posted by Menu, ButtonHandler or MIDI comes from a Load/Save-family function, which is frozen during the load. The one exception is the background HCNAMES flush, which waits for an idle card.
- Only `preset_load*`/`preset_save*` set `pm_status` busy, so Instrument and Effect `typ` changes are runtime only.
- The SOM generator (`SomGenerator.c`) produces triggers and never writes PatternData, so it needs no gate.

**e. Edit fan-out had no presence filter.**
- `bank_editTargetMask()` returns the raw entry (`BankData.c:495-500`), so during a load it would fan out into pending Scenes. S1 filters it.
- Layout safety among loaded Scenes uses an **incremental revalidation over available Scenes** at each child completion, instead of a use-time check. A full revalidation would strip links to pending Scenes, which still hold old content.

**f. More Menu gates than the spec listed.** These also stop on `menu_storageBusy` and would leave the panel half dead:
- the knob repaint (`menu.c:15540`);
- the runtime widgets (`menu.c:15614`);
- the STEP track-automation overlay (`menu.c:4324`);
- the final repaint of `menu_switchPage()` (`menu.c:17311`).

**g. The end-of-load present commit must respect edits made during the load.** The existing formula re-derives the loaded bits. That would resurrect a loaded Scene the user cleared mid-load (`clearOps.c:393`). Progressive loads keep the live mask instead (F5).

**h. Copy/clear during a load is slow, and name jobs hold the queue.**
- While the facade is busy, the service already throttles engine calls to 0.1% CPU (`ccSvc_tick()`, `copyClearService.c:1609-1621`, unchanged).
- Jobs that remap names (Kit, Scene, Instrument copies) commit their content, then **hold their name phase** until the load and its command end (D53). While a job is queued, a new COPY press is refused (`copyClearSession.c:591`).

  This is the most permissive behaviour the shared 9 kB name buffer allows. The user should know it.

**i. A pending Scene change armed before a direct load** to a Scene that becomes unavailable is dropped when it fires, by the executor's availability check (§S5 M10).

**j. Frozen-screen name.** The Bank page name normally comes from the browser cache (`filesystem_bankSlotName()`, `menu.c:12159`), which the exit clears. The frozen screen reads the request's captured `op_bank_display_name` through a new accessor.

**k. `NN.` is shared with Bank Save,** so Bank Save also becomes 1-based (D51).

---

## S1 — Load availability in BankData

### B1.1 `Core/Bank/BankData.c:21` (after `bank_scene_mask_voice_edit[]`) — ADD static

```c
/*
 * Scenes a running background Bank load has not finished (S082 P3 D44).
 *
 * What: bit N set = resident Scene N belongs to the load's load or empty set
 * and is not yet complete: pending, in progress, emptied, or a requested child
 * that never loads. A completed child's bit clears; every remaining bit clears
 * at the end of the load. Zero outside a load.
 * Why: the present mask and the edit masks describe the old Bank until the
 * last child commits, but the panel is unlocked from the first child (W4). A
 * Scene being replaced must never be played, viewed for editing, fanned out
 * to or copied into; a loaded Scene must be usable at once. BankData is the
 * one module every reader (Menu, Sequencer, copy/clear, EffectsManager)
 * already includes.
 * Inputs: bank_beginLoadAvailability(), bank_noteLoadChildDone(),
 * bank_endLoadAvailability().
 * Outputs: bank_sceneAvailable(), bank_sceneAvailableMask(),
 * bank_loadUnavailableMask(), filters in bank_editTargetMask() and
 * bank_sceneFanoutMask().
 * SRAM: 2 B, SRAM1 static, owner BankData (approved 2026-10-19, S082 P3 D47).
 * Affiliates: filesystem_requestLoadBank(), Bank Load phase 20,
 * on_bank_load_complete(), preset_bgEnd().
 */
static uint16_t bank_load_unavailable_mask;
```

### B1.2 `Core/Bank/BankData.c` after `bank_scenePresent()` (ends 258) — ADD availability API

```c
/*
 * Start a load-availability session (S082 P3 D44, W4).
 *
 * What: marks every Scene of the accepted load's load and empty sets as
 * unavailable. Why: from the moment the filesystem accepts a progressive
 * Bank load the panel can act; a Scene about to be replaced must already be
 * excluded, before any child phase runs. Inputs: scope_mask = act | empty.
 * Outputs: bank_load_unavailable_mask. No AutoSave mark (not a Bank field).
 * Accessors: filesystem_requestLoadBank() for FS_BANK_FLAG_PROGRESSIVE.
 * Affiliates: bank_noteLoadChildDone(), bank_endLoadAvailability().
 */
void bank_beginLoadAvailability(uint16_t scope_mask)
{
    bank_load_unavailable_mask = bank_normalizeSceneMask(scope_mask);
}

/*
 * Publish one completed Bank child (S082 P3 D44, W4).
 *
 * What: for a Scene still unavailable in this session, sets its present bit,
 * clears its unavailable bit, then revalidates the edit masks among the
 * Scenes now available. Why: the child's settings, Kit, Pattern and Effect
 * are complete; the Scene must be selectable, lit in PERF and a fan-out
 * member immediately, and its new edit-mask entry (merged before the first
 * child, F3) must not link Scenes whose layouts differ. Revalidating only
 * among available Scenes keeps the links to pending Scenes, whose old content
 * would otherwise strip them. The present bit is set early on purpose: it is
 * exactly what the end-of-load commit sets, and a Bank load that fails later
 * still leaves the loaded content present.
 * Inputs: scene 0..15. Outputs: present mask (AutoSave field marked; the
 * drain is held by the load's yield), unavailable mask, edit-mask entries.
 * Accessors: filesystem Bank Load phase 20 (success, progressive loads only).
 * Affiliates: bank_revalidateVoiceEditMasksIn(), bank_setScenePresentMask().
 */
void bank_noteLoadChildDone(uint8_t scene)
{
    uint16_t bit = bank_sceneBit(scene);

    if (bit == 0u || (bank_load_unavailable_mask & bit) == 0u)
        return;
    (void)bank_setScenePresentMask((uint16_t)(bank_scene_mask_present | bit));
    bank_load_unavailable_mask =
        (uint16_t)(bank_load_unavailable_mask & (uint16_t)~bit);
    bank_revalidateVoiceEditMasksIn(bank_sceneAvailableMask());
}

/*
 * End the load-availability session (S082 P3).
 *
 * What: clears the unavailable mask; availability equals presence again.
 * Why: called once the filesystem has committed the final present mask, or
 * on any failure; idempotent so every exit can call it.
 * Inputs: none. Outputs: bank_load_unavailable_mask = 0.
 * Accessors: on_bank_load_complete(), preset_bgEnd().
 * Affiliates: bank_beginLoadAvailability().
 */
void bank_endLoadAvailability(void)
{
    bank_load_unavailable_mask = 0u;
}

/*
 * Read the load-availability state (S082 P3).
 *
 * What: bank_loadUnavailableMask() is the raw unavailable set (0 outside a
 * load); bank_sceneAvailableMask() is present & ~unavailable;
 * bank_sceneAvailable() tests one Scene (0 for index 16 and above).
 * Why: user-facing selection, fan-out, copy/clear and LEDs use availability;
 * persistence (AutoSave, Bank Save, the end-of-load commit) keeps using the
 * present mask. Outside a load both are identical.
 * Inputs: present and unavailable masks. Outputs: masks or boolean.
 * Accessors: Menu Scene LEDs and Scene changes, ButtonHandler PERF presses,
 * copy/clear job gate, EffectsManager fan-out, bank_editTargetMask(),
 * bank_sceneFanoutMask().
 * Affiliates: bank_scenePresent(), bank_scenePresentMask().
 */
uint16_t bank_loadUnavailableMask(void)
{
    return bank_load_unavailable_mask;
}

uint16_t bank_sceneAvailableMask(void)
{
    return (uint16_t)(bank_scene_mask_present &
                      (uint16_t)~bank_load_unavailable_mask);
}

uint8_t bank_sceneAvailable(uint8_t scene_index)
{
    return (uint8_t)((bank_sceneAvailableMask() &
                      bank_sceneBit(scene_index)) != 0u);
}
```

### B1.3 `Core/Bank/BankData.c:429-458` — `bank_revalidateVoiceEditMasks()` — MOD into a scoped form

Replace the function with the following. The body is the existing loop,
restricted to `scope`:

```c
/*
 * Revalidate directional VOICE edit-mask entries within a Scene scope
 * (F5 repair; S082 P3 scope).
 *
 * What: for every owner in scope, drops each member bit in scope whose
 * layout no longer matches the owner; members and owners outside scope are
 * untouched. Changed entries pass through the indexed setter (active-bit
 * invariant, Bank mask field marked on change).
 * Why: load completion and Effect type changes revalidate everything
 * (scope 0xFFFF). During a progressive Bank load only the available Scenes
 * hold final content; revalidating against pending Scenes would remove links
 * that are valid once those Scenes load (S082 P3 D44).
 * Inputs: scope mask; resident Effect/Instrument types.
 * Outputs: edit-mask entries. Foreground only; no storage.
 * Accessors: bank_revalidateVoiceEditMasks() (full), bank_noteLoadChildDone().
 * Affiliates: scene_editLayoutMatches(), bank_setSceneMaskVoiceEditForScene().
 */
void bank_revalidateVoiceEditMasksIn(uint16_t scope)
{
    uint8_t owner;

    for (owner = 0u; owner < BANK_SCENE_SLOT_COUNT; owner++) {
        uint16_t mask;
        uint16_t kept;
        uint8_t member;

        if ((scope & bank_sceneBit(owner)) == 0u)
            continue;
        mask = bank_scene_mask_voice_edit[owner];
        kept = mask;
        for (member = 0u; member < BANK_SCENE_SLOT_COUNT; member++) {
            uint16_t bit = bank_sceneBit(member);

            if (member == owner || (mask & bit) == 0u ||
                (scope & bit) == 0u)
                continue;
            if (!scene_editLayoutMatches(owner, member))
                kept = (uint16_t)(kept & (uint16_t)~bit);
        }
        if (kept != mask)
            bank_setSceneMaskVoiceEditForScene(owner, kept);
    }
}

void bank_revalidateVoiceEditMasks(void)
{
    bank_revalidateVoiceEditMasksIn(0xFFFFu);
}
```

Keep the existing comment block above `bank_revalidateVoiceEditMasks()` and
add one line: "Full-scope wrapper of bank_revalidateVoiceEditMasksIn()."

### B1.4 `Core/Bank/BankData.c:471-493` — `bank_sceneFanoutMask()` — MOD member filter

Replace `!bank_scenePresent(member) ||` with `!bank_sceneAvailable(member) ||`.
Then extend the "Edit-mask helpers" comment above it (`BankData.c:460-470`)
with:

```c
 * S082 P3 D44: members must be available (present and not pending in a
 * running background Bank load), so a paste, clear or edit never reaches a
 * Scene the load is about to replace. Outside a load this equals the former
 * present test.
```

### B1.5 `Core/Bank/BankData.c:495-500` — `bank_editTargetMask()` — MOD

```c
uint32_t bank_editTargetMask(void)
{
    /*
     * Edit targets, limited to available Scenes during a load
     * (S082 C4; S082 P3 D44).
     *
     * What: Scene 17 alone while it is active; otherwise the active Scene's
     * edit-mask entry without the Scenes a running background Bank load has
     * not finished. Why: the entry is applied raw by every Menu commit loop;
     * during a load it can name pending Scenes (old content about to be
     * replaced) or emptied ones. Outside a load the unavailable mask is 0 and
     * the result is the existing entry.
     * Inputs: active Scene, active entry, bank_load_unavailable_mask.
     * Outputs: 32-bit target mask.
     * Accessors: menu_cellCommitValue(), menu_commitBusCompParam(), PAR_MORPH
     * and voice Morph commits, other Menu fan-out loops.
     * Affiliates: bank_sceneMaskVoiceEdit(), bank_loadUnavailableMask().
     */
    if (scene_getActiveIndex() == SCENE_BACKGROUND_INDEX)
        return 1ul << SCENE_BACKGROUND_INDEX;
    return (uint32_t)(uint16_t)(bank_sceneMaskVoiceEdit() &
                                (uint16_t)~bank_load_unavailable_mask);
}
```

### B1.6 `Core/Bank/BankData.c` after `bank_mergeVoiceEditMasksAfterLoad()` (ends 583) — ADD

```c
/*
 * Remove failed Bank children from every edit-mask entry (S082 P3).
 *
 * What: every entry loses the failed Scenes' bits (keeping its own bit); a
 * failed Scene's own entry becomes itself only.
 * Why: a progressive Bank load merges the new Bank's edit masks before the
 * first child (so loaded children fan out correctly at once); the end of the
 * load must then only correct the children that failed. Re-running the full
 * merge would overwrite edit-mask changes the user made during the load.
 * Inputs: failed_mask (op_bank_scene_failed_mask).
 * Outputs: entries through bank_setSceneMaskVoiceEditForScene().
 * Accessors: filesystem Bank Load metadata commit (progressive loads).
 * Affiliates: bank_mergeVoiceEditMasksAfterLoad(), FS_BANK_FLAG_PROGRESSIVE.
 */
void bank_stripFailedFromVoiceEditMasks(uint16_t failed_mask)
{
    uint8_t n;

    failed_mask = bank_normalizeSceneMask(failed_mask);
    if (failed_mask == 0u)
        return;
    for (n = 0u; n < BANK_SCENE_SLOT_COUNT; n++) {
        uint16_t bit = bank_sceneBit(n);
        uint16_t entry = (failed_mask & bit)
            ? bit
            : (uint16_t)((bank_scene_mask_voice_edit[n] &
                          (uint16_t)~failed_mask) | bit);

        if (entry != bank_scene_mask_voice_edit[n])
            bank_setSceneMaskVoiceEditForScene(n, entry);
    }
}
```

`bank_normalizeSceneMask()` is an existing helper (used at `BankData.c:239`).
If it is declared after these insertion points, add a forward declaration at
the top of the file.

### B1.7 `Core/Bank/BankData.h` — ADD declarations

**After `bank_scenePresent()` (49):**

```c
/*
 * Background Bank load availability (S082 P3 D44, W4).
 * What: a session marks the load's Scenes unavailable until each completes;
 * available = present and not pending. Zero/equal to presence outside a load.
 * Inputs: begin (act | empty), child done (0..15), end.
 * Outputs: masks and tests for selection, fan-out, copy/clear and LEDs.
 * Accessors: filesystem Bank Load, Preset completion, Menu, ButtonHandler,
 * copy/clear, EffectsManager.
 */
void bank_beginLoadAvailability(uint16_t scope_mask);
void bank_noteLoadChildDone(uint8_t scene);
void bank_endLoadAvailability(void);
uint16_t bank_loadUnavailableMask(void);
uint16_t bank_sceneAvailableMask(void);
uint8_t bank_sceneAvailable(uint8_t scene_index);
```

**After `bank_revalidateVoiceEditMasks()` (70):**

```c
/*
 * Scoped revalidation (S082 P3): only owners and members inside scope.
 * Accessors: bank_noteLoadChildDone(); the full form uses scope 0xFFFF.
 */
void bank_revalidateVoiceEditMasksIn(uint16_t scope);
```

**After `bank_mergeVoiceEditMasksAfterLoad()` (138):**

```c
/*
 * Remove failed children from every entry; a failed child's entry becomes
 * itself (S082 P3). Accessors: progressive Bank Load metadata commit.
 */
void bank_stripFailedFromVoiceEditMasks(uint16_t failed_mask);
```

**DOC `BankData.h:87-102` (`bank_sceneFanoutMask()` contract):** replace
"members ... that are present" with "members ... that are available
(present, and not pending in a running background Bank load, S082 P3)".

**DOC `BankData.h:106-117` (`bank_editTargetMask()` contract):** append "During
a background Bank load the resident result excludes unavailable Scenes
(S082 P3 D44)."

---

## S2 — Bank Load: progressive flag, early present bits, edit masks

### F2.1 `Core/Hardware/SD/filesystem.c:1444` (after `FS_BANK_FLAG_PREVIEW_CANCEL`) — ADD flag

```c
/*
 * Progressive Bank Load (S082 P3 W4, D44).
 *
 * What: set by Preset for an unlocked background Bank load. The request
 * starts a BankData availability session; each completed child sets its
 * present bit at once; the new Bank's edit masks are merged before the first
 * child; the end commit keeps the live present mask and only removes failed
 * children from the edit masks.
 * Why: the user may play, edit and select loaded Scenes while later children
 * load; the old all-at-the-end commit would hide them until completion and
 * overwrite edits made during the load.
 * Inputs: filesystem_requestLoadBank() flags (PRESET_BANK_LOAD_PROGRESSIVE).
 * Outputs: the progressive branches of Bank Load phases 17, 98, 20 and the
 * metadata commit. Lifetime: op_bank_flags, cleared by filesystem_start().
 * Affiliates: bank_beginLoadAvailability(), bank_noteLoadChildDone(),
 * bank_stripFailedFromVoiceEditMasks(), preset_tickBackgroundBankLoad().
 */
#define FS_BANK_FLAG_PROGRESSIVE     0x10u
_Static_assert(FS_BANK_FLAG_PROGRESSIVE == PRESET_BANK_LOAD_PROGRESSIVE,
               "Preset passes Bank Load flags straight through");
_Static_assert(FS_BANK_FLAG_BOOT == PRESET_BANK_LOAD_BOOT &&
               FS_BANK_FLAG_REPLACE == PRESET_BANK_LOAD_REPLACE,
               "Preset passes Bank Load flags straight through");
```

`filesystem.c` already includes `presetManager.h` (`:74`).

### F2.2 `Core/Hardware/SD/filesystem.c:31661-31662` — `filesystem_requestLoadBank()` — MOD

After
`op_bank_flags = (uint8_t)(flags | (valid_empty ? FS_BANK_FLAG_REPLACE : 0u));`
insert:

```c
    /*
     * Start the load-availability session before any child phase
     * (S082 P3 D44).
     *
     * What: a progressive load marks its load and empty sets unavailable now.
     * Why: Preset unlocks the panel as soon as this request is accepted; a
     * Scene about to be replaced must be excluded from that first moment.
     * Inputs: FS_BANK_FLAG_PROGRESSIVE, validated masks.
     * Outputs: BankData unavailable mask.
     * Affiliates: bank_beginLoadAvailability(), on_bank_load_complete().
     */
    if (op_bank_flags & FS_BANK_FLAG_PROGRESSIVE)
        bank_beginLoadAvailability(
            (uint16_t)(op_bank_scene_load_mask | op_bank_scene_empty_mask));
```

Also extend the function's header comment (`filesystem.c:31606-31612`) with
"Inputs: flags may carry FS_BANK_FLAG_PROGRESSIVE (S082 P3)."

### F2.3 `Core/Hardware/SD/filesystem.c` before `filesystem_loadBankDirectory_tick()` — ADD helper

```c
/*
 * Merge the new Bank's edit masks before the first child (S082 P3 D44).
 *
 * What: for a progressive load, applies bank_mergeVoiceEditMasksAfterLoad()
 * with the request's load and empty sets as soon as bankset.bcg is parsed and
 * the empties are done, i.e. at the transition into the first child.
 * Why: a child that completes is usable at once; its fan-out must follow the
 * new Bank's entry, not the old one. Entries naming Scenes that are still
 * pending are harmless: fan-out and edit targets exclude unavailable Scenes,
 * and bank_noteLoadChildDone() revalidates layouts as each child lands.
 * Failed children are removed at the end (bank_stripFailedFromVoiceEditMasks()).
 * Inputs: op_bankset_state entries and seen mask, op_bank_scene_load_mask,
 * op_bank_scene_empty_mask, op_bank_flags.
 * Outputs: BankData edit-mask entries (AutoSave fields marked; drain held).
 * Accessors: Bank Load phases 17 and 98 at their "first child" exits.
 * Affiliates: the metadata commit (F2.5), FS_BANK_FLAG_PROGRESSIVE.
 */
static void filesystem_bankLoadMergeEditMasksEarly(void)
{
    if ((op_bank_flags & FS_BANK_FLAG_PROGRESSIVE) == 0u)
        return;
    bank_mergeVoiceEditMasksAfterLoad(
        op_bankset_state.scene_mask_voice_edit,
        op_bankset_state.seen_scene_mask_voice_edit,
        op_bank_scene_load_mask,
        op_bank_scene_empty_mask);
}
```

Call it in two places:
- **`filesystem.c:14846`** (phase 17, before `op_bank_child_cursor = child_slot;` / `op_phase = 27u;`) — ADD `filesystem_bankLoadMergeEditMasksEarly();`
- **`filesystem.c:14953`** (phase 98, same position) — ADD the same call.

The empty-Bank branches (`filesystem.c:14776-14828`, `14885-14935`) are
unchanged: no child loads progressively there.

### F2.4 `Core/Hardware/SD/filesystem.c:14986-14997` — Bank Load phase 20 — MOD

Turn the failure test into an if/else:

```c
        if (op_close_status != FS_STATUS_DONE) {
            ... existing failure body unchanged ...
        } else if (op_bank_flags & FS_BANK_FLAG_PROGRESSIVE) {
            /*
             * Publish the completed child at once (S082 P3 W4, D44).
             *
             * What: the child's settings, Kit, Pattern and Effect are
             * committed and its directory is closed; BankData sets its
             * present bit, makes it available and revalidates layouts among
             * available Scenes. Why: the user may select it, fan out to it
             * and see it fill in PERF before the last child (W4).
             * Inputs: op_bank_child_cursor. Outputs: BankData state.
             * Affiliates: bank_noteLoadChildDone(), Menu PERF fill observer.
             */
            bank_noteLoadChildDone(op_bank_child_cursor);
        }
```

### F2.5 `Core/Hardware/SD/filesystem.c:15046-15081` — metadata commit after the last child — MOD

Replace the present-mask block and the merge call with:

```c
        /*
         * Resident availability after the load (S082 B3; S082 P3 progressive).
         *
         * What: non-progressive loads keep the existing outcome formula.
         * A progressive load already set each loaded child's present bit as
         * it completed; it now only clears the emptied Scenes, keeping
         * whatever the user did to loaded or kept Scenes during the load
         * (a mid-load `clear scene` removed a bit that must stay removed).
         * Failed children keep their old bit, exactly as before. The active
         * Scene always stays present (S17).
         * Inputs: load/empty/failed masks, live present mask, active Scene,
         * FS_BANK_FLAG_PROGRESSIVE. Outputs: BankData present mask, with an
         * explicit AutoSave mark.
         * Affiliates: bank_setScenePresentMask(), bank_noteLoadChildDone(),
         * autosave_markBankFieldDirty().
         */
        {
            uint16_t present;
            uint8_t active = scene_getActiveIndex();

            if (op_bank_flags & FS_BANK_FLAG_PROGRESSIVE)
                present = (uint16_t)(bank_scenePresentMask() &
                                     (uint16_t)~op_bank_scene_empty_mask);
            else
                present = (uint16_t)(
                    (bank_scenePresentMask() &
                     (uint16_t)~(op_bank_scene_load_mask |
                                 op_bank_scene_empty_mask)) |
                    (op_bank_scene_load_mask &
                     (uint16_t)~op_bank_scene_failed_mask));
            if (active < 16u)
                present = (uint16_t)(present | (uint16_t)(1u << active));
            if (!bank_setScenePresentMask(present))
                autosave_markBankFieldDirty(
                    AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);
        }
```

The trace record and `bank_selectActiveSceneForEditMask()` are unchanged.
Then replace the `bank_mergeVoiceEditMasksAfterLoad(...)` call with:

```c
        /*
         * Edit masks at the end of the load (S082 B3; S082 P3).
         *
         * What: a progressive load merged the new Bank's entries before its
         * first child; it now only removes failed children. Other loads run
         * the full merge here as before.
         * Why: re-merging would overwrite edit-mask changes made while the
         * panel was unlocked. Inputs: op_bankset_state, outcome masks, flags.
         * Outputs: BankData entries. Affiliates: filesystem_bankLoadMergeEditMasksEarly(),
         * bank_stripFailedFromVoiceEditMasks().
         */
        if (op_bank_flags & FS_BANK_FLAG_PROGRESSIVE)
            bank_stripFailedFromVoiceEditMasks(op_bank_scene_failed_mask);
        else
            bank_mergeVoiceEditMasksAfterLoad(
                op_bankset_state.scene_mask_voice_edit,
                op_bankset_state.seen_scene_mask_voice_edit,
                op_bank_scene_load_mask,
                op_bank_scene_empty_mask);
```

### F2.6 `Core/Hardware/SD/filesystem.c` after `filesystem_bankChildCursor()` (ends 27294) — ADD

```c
/*
 * Display name of the Bank being loaded (S082 P3 W2, D49).
 *
 * What: the eight-character name captured by filesystem_requestLoadBank()
 * (op_bank_display_name). Why: the frozen Load:[Bank] screen is re-entered
 * after the user left Load/Save, which cleared the browser cache that
 * normally supplies the name; the captured copy is stable until the next
 * Bank operation, and no other operation can start during the load.
 * Inputs: none. Outputs: pointer to 8 characters (NUL at [8]).
 * Accessors: menu_repaintLoadSavePage() for an unlocked background load.
 * Affiliates: filesystem_bankSlotName(), filesystem_requestLoadBank().
 */
const char *filesystem_bankLoadDisplayName(void)
{
    return op_bank_display_name;
}
```

### F2.7 `Core/Hardware/SD/filesystem.h:652` (after `filesystem_bankChildCursor()`) — ADD

```c
/*
 * Name of the Bank being loaded, captured at request (S082 P3 W2).
 * Accessors: the frozen Load:[Bank] screen.
 */
const char *filesystem_bankLoadDisplayName(void);
```

---

## S3 — Preset

### P3.1 `Core/Bank/Scene/Preset/presetManager.c:112` — MOD mask, ADD flag

Replace `#define PM_BG_PHASE_MASK      0x0Fu` with:

```c
#define PM_BG_PHASE_MASK      0x07u
/*
 * The load is unlocked for the panel (S082 P3 W3, W4, D40).
 *
 * What: set on entry to phase 5 when the Bank Load was posted progressively
 * (Scene 17 load, or a direct load with the transport running); cleared with
 * every other flag by preset_bgEnd(). Phase values are 0-6, so bit 3 was
 * unused under the former 0x0F mask.
 * Why: Menu, ButtonHandler and the LED owner need one test for "the user may
 * work while this load finishes"; phases 1-4 and 6, and stopped loads, keep
 * the S082 P1 lock (D38).
 * Inputs: phase 4 acceptance. Outputs: preset_backgroundBankLoadUnlocked().
 * Affiliates: PRESET_BANK_LOAD_PROGRESSIVE, preset_backgroundBankLoadLocked().
 */
#define PM_BG_UNLOCKED        0x08u
```

**DOC `presetManager.c:95-99`:** in the `pm_bg_phase` comment, change "flags =
MIDI origin, Replace, Scene 17 needed, dirty mask discarded" to "flags =
unlocked (bit 3), MIDI origin, Replace, Scene 17 needed, dirty mask
discarded".

### P3.2 `Core/Bank/Scene/Preset/presetManager.h:188-189` — ADD flag

After `#define PRESET_BANK_LOAD_REPLACE  0x02u`:

```c
/*
 * Progressive runtime Bank load (S082 P3): loaded children become available
 * one by one; equals FS_BANK_FLAG_PROGRESSIVE. Set only by the background
 * load for an unlocked load.
 */
#define PRESET_BANK_LOAD_PROGRESSIVE 0x10u
```

### P3.3 `Core/Bank/Scene/Preset/presetManager.c:3908-3928` — phase 4 — MOD

```c
    case 4u: {
        /*
         * Post the ordinary two-mask Bank Load; wait while the card is busy
         * (P1-5). Decide the unlock (S082 P3 D40).
         *
         * What: a refusal while the facade is BUSY is retried next pass, for
         * at most PM_BG_WAIT_LIMIT_MS (A8); a refusal on an idle facade is a
         * real failure. The load is progressive, and the panel unlocks at
         * phase 5, when Scene 17 carries playback, or when the transport runs
         * and no played Scene is replaced (a direct D25/D31 load). A load
         * requested stopped keeps the P1 lock: it replaces the active Scene in
         * place and runs fast with the codec suspended.
         * Why: the unlock must start exactly when the filesystem has accepted
         * the request and BankData has marked the affected Scenes unavailable,
         * never earlier.
         * Inputs: act/empty, PM_BG_NEEDED, seq_isRunning(),
         * preset_backgroundScene17Needed().
         * Outputs: phase 5 (+ PM_BG_UNLOCKED), or failure.
         * Affiliates: preset_loadBank(), FS_BANK_FLAG_PROGRESSIVE.
         */
        uint8_t progressive = (uint8_t)(
            (pm_bg_phase & PM_BG_NEEDED) != 0u ||
            (seq_isRunning() &&
             !preset_backgroundScene17Needed((uint16_t)(act | empty))));
        uint8_t flags = (uint8_t)(
            ((pm_bg_phase & PM_BG_REPLACE) ? PRESET_BANK_LOAD_REPLACE : 0u) |
            (progressive ? PRESET_BANK_LOAD_PROGRESSIVE : 0u));

        if (preset_loadBank(pm_request_slot, act, empty, flags)) {
            preset_bgSetPhase(5u);
            if (progressive)
                pm_bg_phase |= PM_BG_UNLOCKED;
        } else if (filesystem_status() != FS_STATUS_BUSY ||
                   preset_bgWaitExpired()) {
            preset_bgEnd(1u);
        }
        return; }
```

`preset_backgroundScene17Needed()` is defined above the tick (`:3618`).

### P3.4 `Core/Bank/Scene/Preset/presetManager.c` after `preset_backgroundBankLoadRunning()` (ends 3683) — ADD

```c
/*
 * Panel state of a background Bank load (S082 P3 D38, D40).
 *
 * What: Unlocked = phase 5 of a progressive load (the user may leave
 * Load/Save and work). Locked = any other running phase, or a stopped load
 * (the S082 P1 R7 panel drop still applies).
 * Why: the P1 gates dropped every panel action while a load ran; P3 keeps
 * that only for the locked part.
 * Inputs: pm_bg_phase. Outputs: boolean; single-byte read, ISR-safe.
 * Accessors: ButtonHandler MODE/SHIFT+REC, menu_switchPage(), Menu storage
 * gates, frozen Load:[Bank] screen, completion, the LED owner.
 * Affiliates: PM_BG_UNLOCKED, preset_backgroundBankLoadRunning().
 */
uint8_t preset_backgroundBankLoadUnlocked(void)
{
    return (uint8_t)((pm_bg_phase & PM_BG_UNLOCKED) != 0u);
}

uint8_t preset_backgroundBankLoadLocked(void)
{
    return (uint8_t)(preset_backgroundBankLoadRunning() &&
                     !preset_backgroundBankLoadUnlocked());
}
```

### P3.5 `Core/Bank/Scene/Preset/presetManager.h:236` (after `preset_backgroundBankLoadRunning()`) — ADD

```c
/*
 * Unlocked: phase 5 of a progressive load, the panel is free (S082 P3).
 * Locked: running and not unlocked (S082 P1 R7 drop applies).
 * Accessors: ButtonHandler, Menu, LED owner.
 */
uint8_t preset_backgroundBankLoadUnlocked(void);
uint8_t preset_backgroundBankLoadLocked(void);
```

### P3.6 `Core/Bank/Scene/Preset/presetManager.c:3752-3765` — `preset_bgEnd()` — MOD

After `filesystem_setAutosaveYield(0u);` add:

```c
    /*
     * Close any availability session (S082 P3). What: idempotent backstop
     * for every exit, including failures before the filesystem accepted the
     * request. Affiliates: bank_endLoadAvailability(), on_bank_load_complete().
     */
    bank_endLoadAvailability();
```

Add `bank_endLoadAvailability()` to the function's Affiliates line.

### P3.7 `Core/Bank/Scene/Preset/presetManager.c:649` — `on_bank_load_complete()` — MOD

Insert as the first statement:

```c
    /*
     * End the availability session before Menu sees the completion
     * (S082 P3 D44).
     *
     * What: availability returns to the present mask, which the filesystem
     * has just committed (success) or left as the children set it (failure).
     * Why: Menu's completion repaints PERF and finishes the command from the
     * final state; a stale unavailable bit would keep a Scene dark.
     * Inputs: none. Outputs: BankData session closed.
     * Affiliates: bank_endLoadAvailability(), filesystem metadata commit.
     */
    bank_endLoadAvailability();
```

`presetManager.c` already includes `BankData.h`.

---

## S4 — Two-slot fast blink (W1)

### L4.1 `Core/Hardware/frontPanel/ledHandler.c:194-209` — MOD state

Replace the comment and `led_fastBlinkLed` with:

```c
/*
 * Fast blink state, two slots (S080 P2 Defect 10; S082 P3 W1).
 *
 * What: led_fastBlinkLed[slot] is the logical LED each slot drives with
 * LED_LAYER_FAST (0xFF = slot free); both slots share led_fastBlinkPhase and
 * the led_fastBlinkNext toggle deadline. Slot 0 is the queued Scene's SEQ LED;
 * slot 1 is the Load/Save mode LED while a background Bank load runs.
 * Why: both indications can be due at once in PERF (a queued change during a
 * load); one shared phase keeps them in step and costs one byte more than the
 * former single slot.
 * Inputs: led_setFastBlinkSlot(). Outputs: LED phase via
 * led_renderFromStack() and led_tickHandler().
 * RAM: 5 B SRAM1 (was 4 B; +1 B approved 2026-10-19, S082 P3 D47).
 * Affiliates: menu_syncQueuedSceneLed() (owner of both slots),
 * LED_FAST_BLINK_TIME_MS.
 */
static uint8_t  led_fastBlinkLed[LED_FAST_SLOT_COUNT] = { 0xFFu, 0xFFu };
```

`led_fastBlinkPhase` and `led_fastBlinkNext` stay.

### L4.2 `Core/Hardware/frontPanel/ledHandler.c:572-573` — `led_clearAll()` — MOD

Replace `led_fastBlinkLed = 0xFFu;` with:

```c
    led_fastBlinkLed[LED_FAST_SLOT_QUEUED_SCENE] = 0xFFu;
    led_fastBlinkLed[LED_FAST_SLOT_BANK_LOAD] = 0xFFu;
```

### L4.3 `Core/Hardware/frontPanel/ledHandler.c:896-943` — REM `led_setFastBlinkLed()` / `led_getFastBlinkLed()`, ADD slot API

```c
/*
 * Drive one fast-blink slot (S080 P2 Defect 10; S082 P3 W1).
 *
 * What: moves slot `slot` to logical LED ledNr (0xFF frees it). The old LED
 * loses LED_LAYER_FAST and re-renders; the new LED gains it. When no slot was
 * blinking, the shared phase restarts lit with a fresh deadline; otherwise
 * the new LED joins the running phase.
 * Why: the queued-Scene SEQ LED and the Load/Save mode LED flash at double
 * rate, independent of the eight blink slots, which the PERF refresh clears
 * every beat, and of led_setMode2(), which resets the mode LEDs on every mode
 * change.
 * Inputs: slot < LED_FAST_SLOT_COUNT, logical LED id or 0xFF.
 * Outputs: layer bits, fast-blink state, LED output.
 * Accessors: menu_syncQueuedSceneLed().
 * Affiliates: led_renderFromStack(), led_tickHandler(), led_clearAll().
 */
void led_setFastBlinkSlot(uint8_t slot, uint8_t ledNr)
{
    uint8_t old;
    uint8_t physLed;
    uint8_t index;

    if (slot >= LED_FAST_SLOT_COUNT)
        return;
    old = led_fastBlinkLed[slot];
    if (ledNr != 0xFFu) {
        physLed = led_toPhysicalNumber(ledNr);
        if (physLed >= NUM_OUTS && physLed != LED_BAR1)
            ledNr = 0xFFu;
    }
    if (ledNr == old)
        return;
    led_fastBlinkLed[slot] = 0xFFu;
    if (old != 0xFFu) {
        physLed = led_toPhysicalNumber(old);
        index = (physLed == LED_BAR1) ? 40u : physLed;
        if (index < LED_LAYER_COUNT)
            led_activeLayers[index] &= (uint8_t)~LED_LAYER_FAST;
        led_renderFromStack(old);
    }
    if (ledNr != 0xFFu) {
        uint8_t other = led_fastBlinkLed[slot ^ 1u];

        physLed = led_toPhysicalNumber(ledNr);
        index = (physLed == LED_BAR1) ? 40u : physLed;
        if (index < LED_LAYER_COUNT)
            led_activeLayers[index] |= LED_LAYER_FAST;
        if (other == 0xFFu) {
            led_fastBlinkPhase = 1u;
            led_fastBlinkNext =
                (uint16_t)(time_sysTick + LED_FAST_BLINK_TIME_MS);
        }
        led_fastBlinkLed[slot] = ledNr;
        led_renderFromStack(ledNr);
    }
}

/* Current LED of one fast-blink slot, or 0xFF (S082 P3). */
uint8_t led_getFastBlinkSlot(uint8_t slot)
{
    return (slot < LED_FAST_SLOT_COUNT) ? led_fastBlinkLed[slot] : 0xFFu;
}
```

`slot ^ 1u` relies on exactly two slots. A `_Static_assert(LED_FAST_SLOT_COUNT == 2u, ...)`
goes next to it.

### L4.4 `Core/Hardware/frontPanel/ledHandler.c:1047-1059` — `led_tickHandler()` — MOD

```c
    /*
     * Fast blink, both slots (S080 P2 Defect 10; S082 P3 W1).
     *
     * What: while any slot is in use, toggles the shared phase every
     * LED_FAST_BLINK_TIME_MS and re-renders each slot's LED. Why: double-rate
     * flashing of the queued Scene and of the Load/Save mode LED during a
     * background load. The signed-distance test is wrap-safe on the 16-bit
     * clock. Affiliates: led_setFastBlinkSlot(), led_renderFromStack().
     */
    if ((led_fastBlinkLed[0] != 0xFFu || led_fastBlinkLed[1] != 0xFFu) &&
        (uint16_t)(time_sysTick - led_fastBlinkNext) < 0x8000u) {
        uint8_t s;

        led_fastBlinkNext = (uint16_t)(time_sysTick + LED_FAST_BLINK_TIME_MS);
        led_fastBlinkPhase ^= 1u;
        for (s = 0u; s < LED_FAST_SLOT_COUNT; s++)
            if (led_fastBlinkLed[s] != 0xFFu)
                led_renderFromStack(led_fastBlinkLed[s]);
    }
```

**DOC `ledHandler.c:76-79` (`LED_FAST_BLINK_TIME_MS` comment):** change "used
only by the queued-scene indication" to "used by the queued-Scene indication
and the background Bank load mode LED (S082 P3 W1)".

### L4.5 `Core/Hardware/frontPanel/ledHandler.c:1786-1787` — per-beat PERF refresh — MOD

```c
        /*
         * ... existing S077 P2 §5.1 text ...
         * S082 P3 D49: the frozen Load:[Bank] screen shows the PERF Scene
         * row too, so it gets the same per-beat refresh.
         */
        if (buttonHandler_getMode() == SELECT_MODE_PERF ||
            menu_bankLoadFrozenPageActive())
            menu_refreshPerfSceneLeds();
```

### L4.6 `Core/Hardware/frontPanel/ledHandler.h:143-148` — MOD

Replace the two declarations and their comment with:

```c
/*
 * Fast blink slots (S080 P2 Defect 10; S082 P3 W1): double-rate LEDs on their
 * own layer, sharing one phase. Slot QUEUED_SCENE = queued Scene's SEQ LED;
 * slot BANK_LOAD = Load/Save mode LED during a background Bank load.
 * ledNr 0xFF frees a slot. Blink and base writes never disturb them.
 * Accessors: menu_syncQueuedSceneLed().
 */
#define LED_FAST_SLOT_QUEUED_SCENE 0u
#define LED_FAST_SLOT_BANK_LOAD    1u
#define LED_FAST_SLOT_COUNT        2u
void    led_setFastBlinkSlot(uint8_t slot, uint8_t ledNr);
uint8_t led_getFastBlinkSlot(uint8_t slot);
```

The only users of the removed functions are `menu.c:9211-9212` (M5.13).

---

## S5 — Menu

### M5.1 `Core/Menu/menu.c` after `menu_isStorageBusy()` (ends 2959) — ADD panel predicates

```c
/*
 * Panel free during a background Bank load (S082 P3 W2, W3).
 *
 * What: nonzero when the running background Bank load is unlocked and the
 * panel is not on Load/Save. Why: the accepted command keeps
 * menu_storageBusy set and Preset reports LOAD_IN_PROGRESS for the whole
 * load; every storage gate that guards ordinary pages (encoder, pots, page
 * switch, knob repaint, widgets, STEP overlay, copy/clear) must ignore them
 * there, while Load/Save itself stays frozen (W2).
 * Inputs: preset_backgroundBankLoadUnlocked(), menu_activePage.
 * Outputs: boolean.
 * Accessors: Menu storage gates, copyClear_copyPressed().
 * Affiliates: menu_storageBusy, preset_getStatus(),
 * menu_bankLoadFrozenPageActive().
 */
uint8_t menu_backgroundLoadReleasesPanel(void)
{
    return (uint8_t)(preset_backgroundBankLoadUnlocked() &&
                     menu_activePage != LOAD_PAGE &&
                     menu_activePage != SAVE_PAGE);
}

/*
 * Frozen Load:[Bank] screen shown (S082 P3 W2, D49).
 *
 * What: nonzero while the load is unlocked and the panel is on Load/Save,
 * which then shows only the Bank Load screen. Why: its SEQ LEDs follow PERF,
 * including the per-beat refresh. Inputs: as above. Outputs: boolean.
 * Accessors: led per-beat refresh, menu_refreshLoadSceneLeds().
 * Affiliates: menu_enterFrozenBankLoadPage().
 */
uint8_t menu_bankLoadFrozenPageActive(void)
{
    return (uint8_t)(preset_backgroundBankLoadUnlocked() &&
                     menu_activePage == LOAD_PAGE);
}

/*
 * Viewed Scene not yet available during a Bank load (S082 P3 D45).
 *
 * What: nonzero when the viewed Pattern is a resident Scene the running load
 * has not finished. Why: with PAR_FOLLOW off the view can stay on the home
 * Scene the load is replacing; a STEP or track-setting edit there would be
 * overwritten or race the Pattern replacement, so it is dropped silently,
 * like the S081 H-2 refusal. Zero outside a load.
 * Inputs: menu_getViewedPattern(), bank_loadUnavailableMask().
 * Outputs: boolean.
 * Accessors: menu_parseEncoder(), menu_parseKnobDelta() (SEQ_PAGE),
 * ButtonHandler STEP toggle.
 * Affiliates: liveRec_activePatternLocked().
 */
uint8_t menu_viewedSceneLoadPending(void)
{
    uint8_t viewed = menu_getViewedPattern();

    return (uint8_t)(viewed < 16u &&
                     (bank_loadUnavailableMask() &
                      (uint16_t)(1u << viewed)) != 0u);
}
```

### M5.2 `Core/Menu/menu.h:651` (after `menu_isStorageBusy()`) — ADD

```c
/*
 * Background Bank load panel state (S082 P3).
 * menu_backgroundLoadReleasesPanel(): unlocked load and not on Load/Save.
 * menu_bankLoadFrozenPageActive(): unlocked load on the frozen Load:[Bank].
 * menu_viewedSceneLoadPending(): the viewed Scene is not yet loaded.
 * Accessors: copyClearSession.c, ledHandler.c, buttonHandler.c.
 */
uint8_t menu_backgroundLoadReleasesPanel(void);
uint8_t menu_bankLoadFrozenPageActive(void);
uint8_t menu_viewedSceneLoadPending(void);
```

### M5.3 `Core/Menu/menu.c` after `menu_finishLoadSaveCommand()` (ends 290) — ADD off-page finish

```c
/*
 * End an unlocked background Bank load's command off Load/Save
 * (S082 P3 §5.10).
 *
 * What: releases no-playback storage (if S058 owned it), clears
 * menu_loadSaveCommandActive and menu_storageBusy, and nothing else: no
 * Load/Save cursor reset, no editModeActive change, no Load LED reset, no
 * repaint of the Load/Save page.
 * Why: menu_finishLoadSaveCommand() resets Load/Save state through
 * menu_resetSaveParametersEx(), which sets editModeActive and repaints; on
 * another page that would corrupt the page the user is working on. The next
 * Load/Save entry is a fresh entry and resets everything itself.
 * Inputs: menu_loadSaveCommandActive. Outputs: command and busy flags.
 * Accessors: PRESET_OP_BANK_LOAD completion and its failure branch,
 * menu_backgroundBankLoadAbort().
 * Affiliates: menu_finishLoadSaveCommand(), menu_endNoPlaybackStorage().
 */
static void menu_finishBackgroundLoadCommandOffPage(void)
{
    if (!menu_loadSaveCommandActive)
        return;
    menu_endNoPlaybackStorage();
    menu_loadSaveCommandActive = 0u;
    menu_storageBusy = 0u;
}
```

### M5.4 `Core/Menu/menu.c:304-327` — `menu_paintLoadSaveConfirmation()` — MOD (D51)

Replace the digit computation with the 1-based Scene number:

```c
        if (child != 0xFFu && child < 99u) {
            /*
             * Show Bank child progress as the 1-based Scene number
             * (S082 P3 D51).
             *
             * What: `NN.` shows child + 1 (01..16), the Scene number the user
             * sees on the SEQ row; it is read live, so a return to the frozen
             * Load:[Bank] screen shows the current child. Why: user-facing
             * numbers are 1-based; the former zero-based slot read one behind
             * the filling LED. Bank Load and Bank Save share the indicator.
             * Inputs: filesystem_bankChildCursor(). Outputs: cells 13-15.
             * Affiliates: menu_refreshBankChildProgress().
             */
            uint8_t shown = (uint8_t)(child + 1u);

            editDisplayBuffer[1][13] = (char)('0' + (shown / 10u));
            editDisplayBuffer[1][14] = (char)('0' + (shown % 10u));
            editDisplayBuffer[1][15] = '.';
        }
```

Keep the existing explanatory block above it. Change "zero-based Bank-local
Scene slot" to "zero-based cursor, shown 1-based".

### M5.5 `Core/Menu/menu.c:11786-11796` — `menu_refreshBankChildProgress()` — MOD

```c
    child = filesystem_bankChildCursor();
    if (child == 0xFFu || child >= 99u)
        return;
    /* S082 P3 D51: compare against the 1-based digits the paint writes. */
    child = (uint8_t)(child + 1u);
    tens = (char)('0' + (child / 10u));
    ones = (char)('0' + (child % 10u));
```

The rest is unchanged. The existing `menu_activePage` test keeps it on
Load/Save, which includes the frozen screen.

### M5.6 `Core/Menu/menu.c:12158-12160` — `menu_repaintLoadSavePage()` Bank name — MOD

```c
            if (menu_saveOptions.what == SAVE_TYPE_BANK) {
                /*
                 * S082 P3 W2: the frozen Load:[Bank] screen shows the name
                 * captured with the request; the browser cache was cleared
                 * when the user left Load/Save mid-load.
                 */
                displayName = menu_bankLoadFrozenPageActive()
                    ? filesystem_bankLoadDisplayName()
                    : filesystem_bankSlotName(
                          menu_currentPresetNr[SAVE_TYPE_BANK]);
            }
```

### M5.7 `Core/Menu/menu.c` before `menu_switchPage()` — ADD frozen entry

```c
/*
 * Enter the frozen Load:[Bank] screen during an unlocked load
 * (S082 P3 W2, D49).
 *
 * What: shows the Bank Load page exactly as the background load presents it:
 * top-level Bank type with the bracketed type field (`Load:[Bank    ]`), the
 * loading slot, the captured Bank name and the live `NN.` progress; SEQ LEDs
 * as PERF. Nothing is requested, reset or reloaded.
 * Why: the normal Load entry resets the Load/Save state and posts a browser
 * request, which would change the screen and fight the busy card. The
 * command is still active, so every Load/Save input stays dropped by the
 * existing gates (no cursor, no encoder, no pots, no SEQ, no COPY).
 * Inputs: preset_getRequestSlot() (the loading Bank slot).
 * Outputs: menu_activePage = LOAD_PAGE and the Bank Load state; Scene LEDs.
 * Accessors: menu_switchPage(LOAD_PAGE) while
 * preset_backgroundBankLoadUnlocked().
 * Affiliates: menu_loadSaveEnterTopAt() (the state it mirrors),
 * filesystem_bankLoadDisplayName(), menu_refreshLoadSceneLeds().
 */
static void menu_enterFrozenBankLoadPage(void)
{
    menu_activePage = LOAD_PAGE;
    menu_instrumentLoadActive = 0u;
    menu_instrumentSaveMode = 0u;
    menu_saveOptions.what = SAVE_TYPE_BANK;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    menu_currentPresetNr[SAVE_TYPE_BANK] = preset_getRequestSlot();
    menu_bankUiFlags = 0u;
    led_setValue(0u, LED_COPY);
    lockPotentiometerFetch();
    menu_refreshLoadSceneLeds();
}
```

The completion's selection check (`menu_isLoadSaveSelectionCurrent()`,
`menu.c:6233`) compares the request type and slot with
`menu_saveOptions.what` and `menu_currentPresetNr[]`. The entry sets both to
the request, so completion on the frozen screen takes the normal path
(spec §7.6 item 4).

### M5.8 `Core/Menu/menu.c:16904-16935` — `menu_switchPage()` gates — MOD

Replace the R7 backstop and the busy branch head with:

```c
    /*
     * Background Bank load gates (S082 P1 R7; S082 P3 W2, D38).
     *
     * What: while the load is locked (phases 1-4, 6, stopped loads) every
     * page request is dropped, as in P1. While it is unlocked, page switches
     * run immediately and are never queued; a Load/Save request while the
     * frozen Load:[Bank] screen is already shown does nothing (no Load/Save
     * toggle, D49).
     * Why: the P3 panel is free except for Load/Save itself; the queued-exit
     * mechanism below exists for short Load/Save transactions and would hold
     * every page change until the end of the load.
     * Inputs: preset_backgroundBankLoadLocked()/Unlocked(), pageNr,
     * menu_activePage. Outputs: none (drop) or fall through.
     * Affiliates: handleModeButtons(), menu_enterFrozenBankLoadPage().
     */
    if (preset_backgroundBankLoadLocked())
        return;
    if (preset_backgroundBankLoadUnlocked() && pageNr == LOAD_PAGE &&
        menu_activePage == LOAD_PAGE)
        return;
    if ((menu_storageBusy || preset_getStatus() != PRESET_IDLE) &&
        !preset_backgroundBankLoadUnlocked()) {
        ... existing busy body unchanged ...
    }
```

### M5.9 `Core/Menu/menu.c:17168-17170` — `menu_switchPage()` `case LOAD_PAGE:` — MOD

```c
    case LOAD_PAGE:
        menu_setVoiceModeShowMorph(0u);
        /* S082 P3 W2: during an unlocked load only the frozen Bank screen. */
        if (preset_backgroundBankLoadUnlocked()) {
            menu_enterFrozenBankLoadPage();
            break;
        }
        if (menu_activePage == LOAD_PAGE) {
        ... existing body unchanged ...
```

### M5.10 `Core/Menu/menu.c:17311-17312` — `menu_switchPage()` final repaint — MOD

```c
    /*
     * ... existing S054 text ...
     * S082 P3: an unlocked background load keeps menu_storageBusy set for its
     * whole run; the destination page (or the frozen screen) must still paint.
     */
    if (!menu_storageBusy || preset_backgroundBankLoadUnlocked())
        menu_repaintAll();
```

### M5.11 `Core/Menu/menu.c:14800` — `menu_parseEncoder()` — MOD

Replace `if (menu_storageBusy || preset_getStatus() != PRESET_IDLE) {` with:

```c
    if ((menu_storageBusy || preset_getStatus() != PRESET_IDLE) &&
        !menu_backgroundLoadReleasesPanel()) {
```

Append to the "Storage exclusivity" comment: "S082 P3: an unlocked background
Bank load blocks only Load/Save; other pages take the encoder
(menu_backgroundLoadReleasesPanel())."

Then **`menu.c:14832`** (H-2a refusal) — MOD:

```c
    /* S081 H-2a (Q13): silent refusal of STEP edits of A while T != A.
     * S082 P3 D45: also while the viewed Scene is still being loaded. */
    if ((liveRec_activePatternLocked() || menu_viewedSceneLoadPending()) &&
        menu_activePage == SEQ_PAGE) {
```

### M5.12 `Core/Menu/menu.c:15343` and `:15352` — `menu_parseKnobDelta()` — MOD

```c
    /* S082 P3: pots stay locked only on Load/Save during an unlocked load. */
    if (menu_storageBusy && !menu_backgroundLoadReleasesPanel()) return;
```

and

```c
    if ((liveRec_activePatternLocked() || menu_viewedSceneLoadPending()) &&
        menu_activePage == SEQ_PAGE)
        return;
```

Extend the Q13 comment with "S082 P3 D45: also while the viewed Scene is
being loaded."

### M5.13 `Core/Menu/menu.c:9192-9213` — `menu_syncQueuedSceneLed()` — MOD (W1)

```c
/*
 * Owner of both fast-blink slots (S080 P2 Defect 10; S082 P3 W1).
 *
 * What: slot QUEUED_SCENE fast-blinks the queued Scene's SEQ LED in PERF;
 * slot BANK_LOAD fast-blinks the Load/Save mode LED (LED_MODE4) while an
 * unlocked background Bank load runs and the panel is not in Load/Save mode.
 * Both clear when their condition ends.
 * Why: re-deriving from state on every tick covers every way a queue, a mode
 * change or a load can start or end without call sites in each path; in
 * Load/Save mode the frozen screen shows the load, so the mode LED stays as
 * led_setMode2() set it.
 * Inputs: seq_queuedSceneTarget(), buttonHandler_getMode(),
 * preset_backgroundBankLoadUnlocked().
 * Outputs: led_setFastBlinkSlot() on change only.
 * Accessors: menu_serviceDeferredSceneChange() (every buttonHandler_tick()).
 * Affiliates: LED_LAYER_FAST, LED_FAST_SLOT_*.
 */
static void menu_syncQueuedSceneLed(void)
{
    uint8_t target = seq_queuedSceneTarget();
    uint8_t mode = buttonHandler_getMode();
    uint8_t want = 0xFFu;
    uint8_t want_load = 0xFFu;

    if (mode == SELECT_MODE_PERF && target < 16u)
        want = (uint8_t)(LED_SEQ1 + target);
    if (led_getFastBlinkSlot(LED_FAST_SLOT_QUEUED_SCENE) != want)
        led_setFastBlinkSlot(LED_FAST_SLOT_QUEUED_SCENE, want);
    if (preset_backgroundBankLoadUnlocked() && mode != SELECT_MODE_LOAD_SAVE)
        want_load = LED_MODE4;
    if (led_getFastBlinkSlot(LED_FAST_SLOT_BANK_LOAD) != want_load)
        led_setFastBlinkSlot(LED_FAST_SLOT_BANK_LOAD, want_load);
}
```

### M5.14 `Core/Menu/menu.c:9270-9291` — `menu_residentPresentSceneMask()` — MOD

```c
static uint16_t menu_residentPresentSceneMask(void)
{
    uint16_t mask = bank_sceneAvailableMask();
    uint8_t home_scene;

    /*
     * ... existing text ...
     * S082 P3 W4, D42: the mask is BankData's availability: during a
     * background Bank load, Scenes not yet loaded (and emptied ones) drop
     * out, loaded children appear as each completes and kept Scenes stay.
     * PERF, the frozen Load:[Bank] screen and the VOICE-held LEDs therefore
     * fill left to right. No home-Scene fallback while a load is pending: an
     * all-pending Replace must show nothing. Outside a load availability is
     * presence, so nothing changes.
     */
    home_scene = scene_backgroundHomeIndex();
    if (mask == 0u && bank_loadUnavailableMask() == 0u &&
        home_scene < SCENE_COUNT)
        mask = (uint16_t)(1u << home_scene);
    return mask;
}
```

### M5.15 `Core/Menu/menu.c:9520` — `menu_refreshLoadSceneLeds()` — MOD

After the `if (menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) return;`
line, insert:

```c
    /*
     * Frozen Load:[Bank] screen: SEQ LEDs as PERF (S082 P3 D49).
     *
     * What: during an unlocked background load the page shows the PERF Scene
     * row (loaded and kept Scenes with content lit, pending ones dark, the
     * fill growing left to right). Why: the user's choice; the selection view
     * is meaningless while the command runs. Inputs: unlocked state.
     * Outputs: SEQ LEDs via menu_refreshPerfSceneLeds().
     * Affiliates: menu_bankLoadFrozenPageActive(), menu_residentPresentSceneMask().
     */
    if (menu_bankLoadFrozenPageActive()) {
        menu_refreshPerfSceneLeds();
        return;
    }
```

### M5.16 `Core/Menu/menu.c` Scene selection sites — MOD `bank_scenePresent` → `bank_sceneAvailable`

| Site | Function | Comment line to add |
|------|----------|---------------------|
| `menu.c:9043-9046` | `menu_perfSceneChangeExecute()` | "S082 P3 D52: only an available Scene; a direct, deferred or queued change to a Scene the load has not finished is dropped (including one armed before a direct load)." |
| `menu.c:8877-8879` | `menu_chainMenuOpen()` | "S082 P3: chain editor only for available Scenes." |
| `menu.c:8892-8894` | `menu_chainMenuView()` | same |
| `menu.c:9258` | VOICE-held SEQ edit-mask toggle | "S082 P3 D44: a Scene not yet loaded cannot join the edit mask (its layout is the old content's)." |

Outside a load each test is identical to today.

### M5.17 `Core/Menu/menu.c` after `menu_refreshBankChildProgress()` (ends 11799) — ADD fill observer and static

```c
/*
 * SEQ LED repaint trigger for the background load fill (S082 P3 W4, D49).
 *
 * What: menu_bgLoadAvailCount holds the number of available Scenes last
 * painted during an unlocked load (0xFF = none). When the count changes, the
 * Scene row is repainted: PERF mode through menu_refreshPerfSceneLeds(), the
 * frozen Load:[Bank] screen through menu_refreshLoadSceneLeds(). When the
 * load stops being unlocked, one final repaint restores the normal rules.
 * Why: children complete inside filesystem_tick(), independent of beats and
 * of input; with the transport stopped there is no per-beat refresh at all.
 * During a load availability only grows, so a count is a sufficient change
 * test.
 * Inputs: preset_backgroundBankLoadUnlocked(), bank_sceneAvailableMask(),
 * buttonHandler_getMode(), menu_activePage.
 * Outputs: SEQ LEDs; menu_bgLoadAvailCount.
 * SRAM: 1 B, SRAM1 static, owner Menu (approved 2026-10-19, S082 P3 D47).
 * Accessors: menu_pollPresetStatus() (every pass, before early returns).
 * Affiliates: bank_noteLoadChildDone(), menu_residentPresentSceneMask().
 */
static uint8_t menu_bgLoadAvailCount = 0xFFu;

static void menu_refreshBackgroundLoadSceneLeds(void)
{
    uint8_t count = 0xFFu;

    if (preset_backgroundBankLoadUnlocked()) {
        uint16_t avail = bank_sceneAvailableMask();

        count = 0u;
        while (avail != 0u) {
            avail = (uint16_t)(avail & (uint16_t)(avail - 1u));
            count++;
        }
    }
    if (count == menu_bgLoadAvailCount)
        return;
    menu_bgLoadAvailCount = count;
    if (buttonHandler_getMode() == SELECT_MODE_PERF)
        menu_refreshPerfSceneLeds();
    else if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        menu_refreshLoadSceneLeds();
}
```

Add a forward declaration beside the other static prototypes
(`menu.c:1983`).

### M5.18 `Core/Menu/menu.c:15841` — `menu_pollPresetStatus()` — ADD call

After `menu_refreshBankChildProgress();`:

```c
    /*
     * Repaint the Scene row as background load children land (S082 P3 W4).
     * Before any worker below can return early; cheap when nothing changed.
     * Affiliates: menu_refreshBackgroundLoadSceneLeds().
     */
    menu_refreshBackgroundLoadSceneLeds();
```

### M5.19 `Core/Menu/menu.c:16224-16278` — `PRESET_OP_BANK_LOAD` completion — MOD

```c
    case PRESET_OP_BANK_LOAD:
    {
        /*
         * Bank Load completion: re-apply only a replaced active Scene (S25, S5).
         *
         * ... existing text ...
         *
         * S082 P3 §5.10: an unlocked load never re-applies: the active Scene
         * is Scene 17, a kept Scene, or a loaded child the user switched to
         * after it completed (its activation already applied the loaded data);
         * a second activation would be audible. Off Load/Save the command
         * ends quietly (no Load/Save reset or repaint, no failure overlay,
         * D46); on the frozen screen it ends exactly as before (D7).
         * Inputs: + preset_backgroundBankLoadUnlocked(), menu_activePage.
         * Affiliates: + menu_finishBackgroundLoadCommandOffPage().
         */
        uint16_t replaced;
        uint8_t active;
        uint8_t active_replaced;
        uint8_t unlocked = preset_backgroundBankLoadUnlocked();
        uint8_t on_page = (uint8_t)(menu_activePage == LOAD_PAGE ||
                                    menu_activePage == SAVE_PAGE);

        if (on_page && !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 0;
            break;
        }

        replaced = (uint16_t)(filesystem_lastBankLoadSceneMask() |
                              filesystem_lastBankLoadEmptyMask());
        active = scene_getActiveIndex();
        active_replaced = (uint8_t)(!unlocked && active < 16u &&
                                    (replaced & (uint16_t)(1u << active)));

        bank_revalidateVoiceEditMasks();

        if ((uint16_t)~replaced & 0xffffu)
            autosave_markResidentBankDirty();

        if (active_replaced) {
            uint8_t reset_save = on_page;
            menu_startSoundApply(1u, reset_save, 1u, 0u, 1u, 0u, 1u, 0u,
                                 FS_STALE_WARNING_NONE);
        } else {
            preset_ackStatus();
            if (!menu_requestLoadCommandFinalIndexRestore()) {
                if (menu_loadSaveCommandActive) {
                    if (unlocked && !on_page)
                        menu_finishBackgroundLoadCommandOffPage();
                    else
                        menu_finishLoadSaveCommand();
                } else {
                    menu_storageBusy = 0u;
                    menu_resetSaveParameters();
                    menu_repaintAll();
                }
            }
        }
        if (preset_bankLoadFailedSceneMask() != 0u && (!unlocked || on_page))
            menu_showFilesystemErrorOverlay();
        break;
    }
```

`menu_requestLoadCommandFinalIndexRestore()` already returns 0 off
`LOAD_PAGE` (`menu.c:6690`). The PERF repaint comes from M5.17 when the
unlocked flag clears on the next poll.

### M5.20 `Core/Menu/menu.c:16021-16036` — failed-operation branch of `menu_pollPresetStatus()` — MOD

```c
        if (menu_loadSaveCommandActive) {
            /*
             * S082 P3 D46: a failed unlocked background Bank load that ends
             * off Load/Save finishes quietly: no Load/Save reset, no repaint
             * of another page, no error overlay. Playback stays where it is.
             * Affiliates: menu_finishBackgroundLoadCommandOffPage().
             */
            if (preset_getCompletedOp() == PRESET_OP_BANK_LOAD &&
                preset_backgroundBankLoadUnlocked() &&
                menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) {
                menu_finishBackgroundLoadCommandOffPage();
                preset_ackStatus();
                return;
            }
            filesystem_clearNameCache();
            menu_finishLoadSaveCommand();
        } else if (...existing...)
```

### M5.21 `Core/Menu/menu.c:10280-10287` — `menu_backgroundBankLoadAbort()` — MOD (defensive)

```c
void menu_backgroundBankLoadAbort(void)
{
    if (!menu_loadSaveCommandActive)
        return;
    /*
     * S082 P3: aborts come from phases 2-4 and 6, all locked, so the panel is
     * on Load/Save; should one arrive elsewhere, end the command quietly.
     */
    if (menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) {
        menu_finishBackgroundLoadCommandOffPage();
        return;
    }
    menu_finishLoadSaveCommand();
    ... existing tail unchanged ...
}
```

### M5.22 Other `menu_storageBusy` gates on ordinary pages — MOD

| Site | Function | New condition |
|------|----------|---------------|
| `menu.c:15540` | `menu_serviceKnobRepaint()` | `if (menu_storageBusy && !menu_backgroundLoadReleasesPanel()) return;` |
| `menu.c:15614` | `menu_serviceRuntimeWidgets()` | same |
| `menu.c:4324` | `menu_enterStepTrackAutomationOverlay()` | `if (voiceNr >= NUM_TRACKS || (menu_storageBusy && !menu_backgroundLoadReleasesPanel())) return;` |

Comment for each: "S082 P3: an unlocked background Bank load keeps
menu_storageBusy set but frees every page except Load/Save
(menu_backgroundLoadReleasesPanel())."

### M5.23 Unchanged Menu sites (verified)

- `menu.c:9868` `menu_loadInstrumentVoicePressed()`: Load/Save stays frozen (W2).
- `menu.c:1934` `menu_updateNoPlaybackStorage()`: D48 already holds (§0.6 c).
- `menu.c:17337` `menu_processPendingPageSwitch()`: nothing is queued in an unlocked load. A locked load drops page requests before the queue.
- `menu.c:16999-17030` Load/Save exit work: safe with a busy card (§0.6 b).
- `menu.c:9365` Bank OK masks: Load/Save only.

---

## S6 — ButtonHandler

### H6.1 `Core/Hardware/frontPanel/buttonHandler.c:1891-1904` — `handleModeButtons()` — MOD

```c
    /*
     * A locked background Bank load owns the panel (S082 P1 R7, D7;
     * S082 P3 D38, D40).
     *
     * What: every MODE press is dropped while the load is locked (phases 1-4
     * and 6, or a load requested stopped). Once it is unlocked MODE works
     * normally; Menu shows the frozen Load:[Bank] screen for Load/Save.
     * Why: ... existing text ...
     * Inputs: preset_backgroundBankLoadLocked(). Outputs: none.
     * Accessors: MODE button presses.
     * Affiliates: menu_switchPage() backstop, preset_tickBackgroundBankLoad().
     */
    if (preset_backgroundBankLoadLocked())
        return;
```

### H6.2 `Core/Hardware/frontPanel/buttonHandler.c:2338-2341` — `buttonHandler_shiftRecMenuEnter()` — MOD

```c
    /* SHIFT+REC is dropped while a locked background Bank load owns the
     * panel (S082 P1 R7); an unlocked load allows it (S082 P3 W3). */
    if (preset_backgroundBankLoadLocked())
        return;
```

### H6.3 `Core/Hardware/frontPanel/buttonHandler.c:1364-1366` — STEP toggle refusal — MOD

```c
    /* S081 H-2b (Q13): silently refuse STEP toggles while T != A.
     * S082 P3 D45: also while the viewed Scene is still being loaded. */
    if (liveRec_activePatternLocked() || menu_viewedSceneLoadPending())
        return;
```

### H6.4 Scene selection sites — MOD `bank_scenePresent` → `bank_sceneAvailable`

| Site | Function | Comment line to add |
|------|----------|---------------------|
| `buttonHandler.c:1434-1436` | `buttonHandler_perfVoiceHeldSeqPressed()` | "S082 P3 D52: per-track moves only to available Scenes (manual change)." |
| `buttonHandler.c:1480-1482` | `buttonHandler_perfScenePressed()` | "S082 P3 D52: Scene and view changes only to available Scenes." |
| `buttonHandler.c:1699-1700` | SHIFT+SEQ queue | "S082 P3 D52: queue only an available Scene; availability only grows during a load, so the target is still valid when the change fires." |

Also update the comment at `buttonHandler.c:1426`: "Validation: the Scene
must be resident" becomes "Validation: the Scene must be available (resident,
and not pending in a background Bank load, S082 P3)".

---

## S7 — Sequencer: no chain evaluation during a load (D50, D52)

### Q7.1 `Core/Sequencer/sequencer.c:5686-5714` — `seq_chainServiceTick()` — MOD

```c
/*
 * Per-tick chain boundary service (S080 G7; S082 P3 D50, D52).
 *
 * What: on each master tick after the first, detects bar and repetition
 * boundaries and advances the counters; evaluates the chain unless a direct,
 * queued or chain change is already pending (a manual or queued switch
 * cancels conditional changes, plan §4.5), or a background Bank load runs.
 * Why: counters keep running during a load (D50); evaluation is suspended
 * because a chain target (`sNN`, `nxt`, `prv`, random range) could resolve
 * differently depending on which children have loaded (D52). After the load
 * the next boundary evaluates with the counts as they stand.
 * Inputs: seq_elapsedPpqTicks, pending flags,
 * preset_backgroundBankLoadRunning() (one-byte read, ISR-safe).
 * Outputs: counters, seq_chainEvaluate(). Context: TIM3 ISR via
 * seq_processSchedulerTick().
 */
static void seq_chainServiceTick(void)
{
	... existing body up to the counter updates ...
	if (seq_pendingSceneChange || seq_pendingSceneQueue || seq_sceneFireFlag)
		return;
	if (preset_backgroundBankLoadRunning())
		return;
	seq_chainEvaluate(at_bar, at_rep);
}
```

`sequencer.c` already includes `presetManager.h` (`:65`).
`seq_chainResolveNext()` (`:5620`) is unchanged: it is never reached during a
load.

---

## S8 — Copy/clear during a load (D41, D53)

### C8.1 `Core/Menu/CopyClear/copyClearSession.c:588` — `copyClear_copyPressed()` — MOD

```c
    /* S082 P3 D41: an unlocked background Bank load does not lock copy/clear
     * out (menu_backgroundLoadReleasesPanel()); jobs that would collide are
     * dropped when they run (ccSvc_tick()). */
    if (menu_isStorageBusy() && !menu_backgroundLoadReleasesPanel())
        refused |= 1u << 2u;
```

### C8.2 `Core/Menu/CopyClear/copyClearService.c` before `ccSvc_tick()` (1596) — ADD job gate

```c
/*
 * Does a job touch a Scene the background load has not finished?
 * (S082 P3 D41)
 *
 * What: nonzero when the job's destination Scene, or for a paste the copy
 * source Scene, is a resident Scene still unavailable in BankData. Scene 17
 * (index 16) is never pending; its own rules stay (D11, CC_DROP_SCENE17).
 * Fan-out members are filtered separately by bank_sceneFanoutMask32().
 * Why: a pending or in-progress Scene is about to be replaced (content and
 * Pattern region); an emptied one is empty. Copy/clear stays open during the
 * load (D41), so the collision is resolved by dropping that job silently.
 * Availability only grows during a load, so a job that passes once stays
 * valid.
 * Inputs: job, copyClear_source(), bank_loadUnavailableMask().
 * Outputs: boolean.
 * Accessors: ccSvc_tick() before each engine call.
 * Affiliates: AUTOSAVE_TRACE_CC_DROP_LOAD_PENDING, bank_sceneFanoutMask().
 */
static uint8_t ccSvc_jobLoadPending(const cc_job_t *job)
{
    uint16_t pending = bank_loadUnavailableMask();
    const cc_source_t *src;

    if (pending == 0u)
        return 0u;
    if (job->scene < 16u && (pending & (uint16_t)(1u << job->scene)) != 0u)
        return 1u;
    if ((job->op & CC_JOB_CLASS_MASK) == CC_JOB_PASTE) {
        src = copyClear_source();
        if (src && src->scene < 16u &&
            (pending & (uint16_t)(1u << src->scene)) != 0u)
            return 1u;
    }
    return 0u;
}
```

**`copyClearService.c:19`** — ADD `#include "BankData.h"` after
`#include "SceneData.h"`. Neither `SceneData.h` nor `copyOps.h` includes it,
and the service calls no `bank_` function today.

### C8.3 `Core/Menu/CopyClear/copyClearService.c:1628-1629` — `ccSvc_tick()` job dispatch — MOD

```c
        /*
         * S082 P3 D41: drop a job whose Scene the background load has not
         * finished; the ordinary DROP tail restores early triggers and frees
         * the slot. Affiliates: ccSvc_jobLoadPending().
         */
        if (ccSvc_jobLoadPending(job)) {
            ccSvc_traceDropReason(AUTOSAVE_TRACE_CC_DROP_LOAD_PENDING,
                                  job->kind);
            r = CC_RUN_DROP;
        } else {
            r = ((job->op & CC_JOB_CLASS_MASK) == CC_JOB_PASTE)
                    ? ccCopy_runJob(job) : ccClear_runJob(job);
        }
```

The register path (`ccSvc_runRegister()`) needs no gate. Its Scene comes from
a job that already passed this gate, and availability only grows.

### C8.4 `Core/Menu/CopyClear/copyClearService.c:192-208` — `ccSvc_ensureScratch()` — MOD (D53)

Insert before `ccSvc_buf = filesystem_borrowNameCacheScratch();`:

```c
    /*
     * Hold the name loan while a Load/Save command runs (S082 P3 D53).
     *
     * What: no borrow while menu_isLoadSaveCommandActive(), which covers a
     * background Bank load from OK (or the MIDI UI take-over) to its final
     * index restore. Why: the facade is busy for the whole load, and right
     * after it the frozen screen's final Bank index restore reuses the same
     * 9 kB array; a borrow in between would corrupt one or the other. A job
     * in its name phase waits; its content is already committed.
     * Inputs: menu_isLoadSaveCommandActive(). Outputs: 0 (not ready).
     * Affiliates: ccSvc_namesReady(), menu_requestLoadCommandFinalIndexRestore().
     */
    if (menu_isLoadSaveCommandActive())
        return 0u;
```

### C8.5 `Core/Menu/CopyClear/copyClearService.c:1688` — name-publication retry — MOD (D53)

```c
        /*
         * S082 P3 D53: never give up on the names because of a Load/Save
         * command (a background Bank load lasts far longer than
         * CC_NAME_RETRY_TICKS); the retry budget counts only while no
         * command holds the card.
         */
        if (menu_isLoadSaveCommandActive())
            return;
        if (++ccSvc_nameRetry < CC_NAME_RETRY_TICKS)
            return;
```

### C8.6 `Core/Bank/Scene/AutosaveTrace.h:592` — ADD drop reason

```c
/* S082 P3 D41: the job's Scene is still being loaded by a background Bank load. */
#define AUTOSAVE_TRACE_CC_DROP_LOAD_PENDING    10u
```

The reason field is 6 bits (`decode_devlogs.py:612`), so 10 fits.

### C8.7 `tools/decode_devlogs.py:556-559` — `CC_DROP` — MOD

Add `10: "LOAD_PENDING"`. While here, update the op table (`0x2b` is
`FS_INTERNAL_OP_SCAN_BANK_SCENES`, still open from P2-5).

---

## S9 — EffectsManager fan-out

### E9.1 `Core/DSP/Effects/EffectsManager.c:562-590` — `effects_fanoutMask()` — MOD

After the entry is ORed in (line 579), and before the type filter, insert:

```c
    /*
     * S082 P3 D44: no Effect fan-out into a Scene a running background Bank
     * load has not finished (pending, in progress or emptied); zero mask
     * outside a load. Affiliates: bank_loadUnavailableMask(),
     * bank_editTargetMask().
     */
    mask &= ~(uint32_t)bank_loadUnavailableMask();
```

The originating Scene is always available: it is active or edited. Extend
the function's comment Affiliates with `bank_loadUnavailableMask()`.

---

## S10 — Documentation

| File | Change |
|------|--------|
| `S082_P3_WORK_WHILE_BG_LOAD.md` | **§7.2:** the done mask became BankData's unavailable mask, with early present bits (this schedule §0.6 a). **§5.4:** no deferral is needed (§0.6 b). **D48:** already satisfied (§0.6 c). **§7.6:** items 1, 3, 4 resolved. **§8:** the 2 B owner is BankData. |
| `S082_P1_LOAD_SAVE.md` | D7 and D38: one line each, "amended by S082 P3 (D40, §5)". |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | **Header:** an S082 P3 paragraph (+4 B: BankData 2, LED +1, Menu 1). **S082 table:** rows `bank_load_unavailable_mask` (2, permanent), `led_fastBlinkLed[2]` (+1), `menu_bgLoadAvailCount` (1). **Line 32:** "fast blink 4 B" becomes 5 B. Refresh the link numbers after the build. |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | **Bank Load:** `FS_BANK_FLAG_PROGRESSIVE`; availability session; early present bits at phase 20; edit-mask merge before the first child and failed-only strip at the end; progressive present commit. **Progress:** `NN.` is 1-based (Load and Save). |
| `knowledge_files/specification_reference/BANK_PRESET_ARCHITECTURE.md` | The availability API and its rule: selection, fan-out and copy/clear use availability; persistence uses presence. |
| `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md` | Jobs on pending Scenes drop (`LOAD_PENDING`). The name phase waits for the command to end. The trickle rate applies during the load (§0.6 h). |
| `knowledge_files/specification_reference/AUTOSAVE.md` | Present-mask and edit-mask marks during a progressive load accumulate under the load's yield. |

---

## 11. What stays bounded

- **`bank_noteLoadChildDone()`:** one present-mask store and at most 16 × 15 layout comparisons, once per child inside one `filesystem_tick()` call.
- **Early merge:** 16 entries, once per load.
- **`menu_refreshBackgroundLoadSceneLeds()`:** one 16-bit popcount per Menu poll; a repaint only on change.
- **`ccSvc_jobLoadPending()`:** two mask tests per service tick.
- **Chain gate:** one byte read in the ISR.
- **No DSP work is added or skipped.**

## 12. Verification

The spec §11 list (1–15) is the hardware script. In addition:

1. **Build:** no new warnings; the link budget shows +4 B BSS against S082 P2 (`bss=430,920`), allowing for alignment.
2. **Outside a load nothing changes.** A Kit, Scene, Pattern or stopped Bank load, plus copy/clear, fan-out edits and PERF switching, all behave as on the P2 build. The unavailable mask is 0 and every changed test reduces to the old one.
3. **Edit masks:**
   - set a VOICE edit link between two loaded children mid-load, and confirm it survives completion;
   - load a Bank with a mismatched-layout link, and confirm it is stripped when the second Scene lands.
4. **Clear a loaded Scene mid-load** (`clear scene`): it stays empty and not present after completion (F2.5).
5. **Copy a Kit mid-load:**
   - the content applies at once;
   - the names publish after the command ends;
   - a further COPY is refused until then.
6. **DEV trace:** a copy into a pending Scene shows `LOAD_PENDING`.
7. **Bank Save:** `NN.` reads `01.`–`16.`.

## 13. Implementation log

*(To be filled in by the implementer: per step, the date, the build numbers
and any deviation from this schedule.)*

### 2026-10-19 — S1 BankData availability — DONE

- Baseline `make all` before any edit: `text=595,968`, `data=420`,
  `bss=430,920`; flash 596,388 / 753,664 B (headroom 157,276 B).
- Added the static 2 B `bank_load_unavailable_mask` and the availability API
  (`bank_beginLoadAvailability`, `bank_noteLoadChildDone`,
  `bank_endLoadAvailability`, `bank_loadUnavailableMask`,
  `bank_sceneAvailableMask`, `bank_sceneAvailable`).
- Split `bank_revalidateVoiceEditMasks()` into the scoped
  `bank_revalidateVoiceEditMasksIn()` plus the full-scope wrapper; the loop
  now skips owners and members outside the scope.
- `bank_sceneFanoutMask()` filters on availability;
  `bank_editTargetMask()` masks out unavailable Scenes; added
  `bank_stripFailedFromVoiceEditMasks()`.
- Header declarations and contract text updated.
- Build after S1: unchanged linked sizes (the new code has no caller yet, so
  LTO/gc dropped both text and the 2 B mask). No new warnings.

### 2026-10-19 — S2 filesystem progressive Bank Load — DONE

- Added `FS_BANK_FLAG_PROGRESSIVE` (0x10) plus compile-time equality checks
  against `PRESET_BANK_LOAD_BOOT/REPLACE/PROGRESSIVE` (the Preset progressive
  define was added in this step so the checks exist from the start).
- `filesystem_requestLoadBank()` starts the availability session with
  `act | empty`; phases 17 and 98 call the new
  `filesystem_bankLoadMergeEditMasksEarly()` before the first child; phase 20
  publishes each successful child through `bank_noteLoadChildDone()`.
- The post-children commit keeps the live present mask for a progressive load
  (clearing only emptied Scenes) and strips failed children from the edit masks
  instead of re-running the full merge. Non-progressive paths are unchanged.
- Added `filesystem_bankLoadDisplayName()` and its header declaration.
- Build after S2: `text=596,168`, `data=420`, `bss=430,920` (mask still
  optimised out until callers reached it; no new warnings).

### 2026-10-19 — S3 Preset unlocked flag — DONE

- `PM_BG_PHASE_MASK` is now 0x07 with `PM_BG_UNLOCKED` = bit 3;
  `PRESET_BANK_LOAD_PROGRESSIVE` added to the header.
- Phase 4 posts `PRESET_BANK_LOAD_PROGRESSIVE` for a Scene 17 load or a
  running-transport direct load, then sets `PM_BG_UNLOCKED` on phase 5.
- Added `preset_backgroundBankLoadUnlocked()`/`Locked()`;
  `preset_bgEnd()` clears the BankData session as a backstop and
  `on_bank_load_complete()` ends it before Menu sees completion.
- Build after S3: `text=596,416`, `data=420`, `bss=430,920`.

### 2026-10-19 — S4 two-slot fast blink — DONE

- `led_fastBlinkLed[2]` with `LED_FAST_SLOT_QUEUED_SCENE` and
  `LED_FAST_SLOT_BANK_LOAD`, one shared phase/deadline. Added
  `led_setFastBlinkSlot()`/`led_getFastBlinkSlot()`; `led_clearAll()` clears
  both slots; the tick renders both.
- The per-beat PERF refresh now also runs when
  `menu_bankLoadFrozenPageActive()`.
- Removed the old single-LED API; its only caller was Menu.
- Build: included in the S5 result (ledHandler alone had a missing Menu
  declaration until S5 landed).

### 2026-10-19 — S5 Menu — DONE

- Added `menu_backgroundLoadReleasesPanel()`, `menu_bankLoadFrozenPageActive()`,
  `menu_viewedSceneLoadPending()` and their header declarations.
- Added `menu_finishBackgroundLoadCommandOffPage()`; the Bank Load completion
  suppresses re-activation for unlocked loads and finishes silently off-page;
  the failure branch does the same (no overlay off-page); abort is defensive.
- Frozen Load:[Bank] entry (`menu_enterFrozenBankLoadPage()`), the
  `menu_switchPage()` gates, and the final repaint guard.
- `NN.` progress is 1-based in both paint and change detection (D51); the
  frozen page reads the captured Bank name.
- Storage gates: encoder, pots, knob repaint, runtime widgets, STEP track
  overlay. SEQ/Scene changes and chain editor use availability; PERF/Load
  Scene LEDs use availability.
- Fill observer `menu_refreshBackgroundLoadSceneLeds()` (+1 B
  `menu_bgLoadAvailCount`) runs before any Menu worker early return.
- `menu_syncQueuedSceneLed()` owns both fast-blink slots, including the
  Load/Save mode LED (W1).
- Build after S5: `text=597,464`, `data=420`, `bss=430,928`.

### 2026-10-19 — S6 ButtonHandler — DONE

- MODE and SHIFT+REC drop only while `preset_backgroundBankLoadLocked()`.
- PERF Scene/view presses, VOICE+SEQ per-track moves and SHIFT+SEQ queue use
  `bank_sceneAvailable()`; the STEP toggle also refuses while
  `menu_viewedSceneLoadPending()`.

### 2026-10-19 — S7 Sequencer chains — DONE

- `seq_chainServiceTick()` still advances bar/repetition counters but returns
  before `seq_chainEvaluate()` while `preset_backgroundBankLoadRunning()`
  (one-byte ISR-safe read).

### 2026-10-19 — S8 Copy/clear — DONE

- COPY no longer refuses on `menu_isStorageBusy()` when
  `menu_backgroundLoadReleasesPanel()` is set.
- Added `ccSvc_jobLoadPending()`; a pending destination or paste source drops
  the job at run time with new reason `LOAD_PENDING` (10).
- The name loan waits while `menu_isLoadSaveCommandActive()` and the name
  retry counter does not advance in that window (D53).
- `AutosaveTrace.h` and `tools/decode_devlogs.py` updated; the decoder's
  `fs_internal_op_t` table also gained the missing
  `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`, which restores `0x2b =
  FS_INTERNAL_OP_SCAN_BANK_SCENES` (the P2-5 open item).

### 2026-10-19 — S9 EffectsManager — DONE

- `effects_fanoutMask()` masks out `bank_loadUnavailableMask()` after the
  edit entry is ORed in; zero outside a load.

### 2026-10-19 — S10 Documentation — DONE

- Updated this spec (§5.4, §5.5, §7.2, §7.6, §8), `S082_P1_LOAD_SAVE.md`
  (D7/D38), `STORAGE_SRAM_MANIFEST.md`, `FILESYSTEM_SPEC.md`,
  `BANK_PRESET_ARCHITECTURE.md`, `COPYCLEAR_UTILITIES.md` and `AUTOSAVE.md`.

### Final build and deviations

- `make all`: `text=598,120`, `data=420`, `bss=430,928`; flash 598,540 /
  753,664 B (headroom 155,124 B); DTCM statics 4,472 B; ITCM 4,168 B.
- `make img`: `build/LXRV2_lxr02.img` (598,540 B) written OK.
- No new compiler warnings; only the existing unused-function set in
  `filesystem.c` and the toolchain's `_close/_lseek/_read/_write` notices.
- A final `make clean && make all && make img` reproduced the same sizes
  (`text=598,120`, `data=420`, `bss=430,928`; image 598,540 B). The 20 build
  warnings are all pre-existing (EuklidGenerator, PatternData packed member,
  asyncfatfs, USB, SplashAnimation, filesystem unused functions, libc linker
  notices, LTO serial note).
- **Deviation 1:** the 2 B state is BankData's unavailable mask, per schedule
  §0.6 a (the spec's filesystem done mask was not added).
- **Deviation 2:** linked BSS grew 8 B rather than 4 B because the new bytes
  changed section alignment; the approved objects total 4 B (nm sizes: 2, 2,
  1 with `led_fastBlinkLed` +1 over its former 1 B).
- **Deviation 3:** one ambiguous patch context initially put the
  `menu_switchPage()` repaint guard in `menu_residentNameScratchFlushComplete()`
  and the encoder gate in `menu_instrumentLoadRequestSelection()`; both were
  corrected before the final build, so the landed diff matches the schedule.

---

## 14. Post-implementation assessment (2026-10-19)

*Code review of the working tree against this schedule and
`S082_P3_WORK_WHILE_BG_LOAD.md`. Read-only: no code was changed by this
assessment. Hardware has not been run.*

### 14.1 Verdict

**The implementation matches the schedule. No defect blocks a hardware test.**
- Every S1–S10 item is present at its anchor, with the scheduled logic and comment blocks.
- Outside a load, every changed test reduces to the old one, because the unavailable mask is 0.
- Two low-severity issues and one observation are listed in §14.3. Neither affects data, audio or the load itself.

### 14.2 What was checked

| Step | Result |
|------|--------|
| **Build** | `make all` is up to date at `text=598,120`, `data=420`, `bss=430,928`; flash 598,540 / 753,664 B (headroom 155,124 B), as logged. No new warnings. |
| **S1 BankData** | `bank_load_unavailable_mask` (`BankData.c:44`) and the availability API (`:294-373`) are as scheduled. `bank_normalizeSceneMask()` only strips bits above 15, so neither the session nor the failed strip can gain bits. `bank_revalidateVoiceEditMasksIn()` (`:562`) skips owners and members outside the scope; the full wrapper uses 0xFFFF. `bank_sceneFanoutMask()` (`:625`) uses availability, `bank_editTargetMask()` (`:650-652`) masks pending Scenes, and `bank_stripFailedFromVoiceEditMasks()` (`:752`) makes a failed child's entry itself only. |
| **S2 filesystem** | Flag 0x10 with the three `_Static_assert`s (`filesystem.c:1462-1467`). The session starts in `filesystem_requestLoadBank()` (`:31782`), synchronously with Preset's acceptance, so there is no window in which the panel is unlocked but a pending Scene still reads as available. The early merge is at both first-child exits (`:14897`, `:15005`); `bank_noteLoadChildDone()` runs only on the successful close of phase 20 (`:15051-15063`). The progressive present formula and failed-only strip are at `:15132`, `:15167`. |
| **S3 Preset** | `PM_BG_PHASE_MASK` is 0x07 and phase 6 still fits. `preset_bgSetPhase()` keeps bit 3. The unlock rule in phase 4 (`presetManager.c:3990`) is as scheduled. The session ends in `on_bank_load_complete()` (`:680`) before Menu sees the completion, and in `preset_bgEnd()` (`:3817`). `PM_BG_UNLOCKED` is still set when Menu dispatches `PRESET_OP_BANK_LOAD` (phase 5 waits for `PRESET_IDLE`, which the dispatch's ack produces), so the completion sees the correct state. |
| **S4 LED** | Two slots sharing a phase (`ledHandler.c:210`, `920-963`, `1076-1085`). A new slot joins a running phase rather than restarting it. `led_clearAll()` resets both slots and the owner re-asserts them next tick. The frozen screen gets the per-beat refresh (`:1815`). The old API is removed and no caller remains. |
| **S5 Menu** | All 22 items are at their anchors. Spot-checked in full: the quiet finish (`menu.c:305`); the 1-based paint and change test (`:304-330`, `:11923-11929`); frozen entry (`:17143`); `menu_switchPage()` gates (`:17182-17188`), `case LOAD_PAGE` (`:17444`) and final repaint (`:17598`); completion (`:16465-16525`); failure branch (`:16239-16253`); abort (`:10410-10418`); LED owner (`:9302-9316`). The fill observer runs before any early return (`:16050`). The S058 scope needs no change (schedule §0.6 c). |
| **S6 ButtonHandler** | MODE and SHIFT+REC drop only while locked (`buttonHandler.c:1911`, `2348`). The STEP toggle refuses a pending viewed Scene (`:1366`). Per-track moves, Scene/view presses and SHIFT+SEQ use availability (`:1438`, `1485`, `1707`). |
| **S7 Sequencer** | Counters advance; evaluation is skipped while a load runs (`sequencer.c:5719`). |
| **S8 copy/clear** | The COPY gate (`copyClearSession.c:591`), job gate (`copyClearService.c:1631`, `1685`), loan hold (`:210`) and retry freeze (`:1757`) are present. `ccSvc_ensureScratch()` is the only borrow path (callers `copyOps.c:684-961`, service `:1743`). Phase 2 of the load waits for `ccSvc_busy()`, so no loan can be outstanding when a load starts. Trace reason 10 and the decoder are updated, and the decoder op table now restores `0x2b` (closes P2-5's decoder item). |
| **S9 EffectsManager** | `effects_fanoutMask()` masks pending Scenes after the entry and before the type filter (`EffectsManager.c:586`); the Scene 17 early return comes first. |
| **S10 docs** | The spec carries "Implemented as" notes (§5.4, D48, §7.2, §7.6, §8). D7 and D38 in `S082_P1_LOAD_SAVE.md` point to P3. The manifest header, S082 table and §8.2 row are updated, and each reference spec has its P3 paragraph. |
| **Deviation 3** | Confirmed reverted: neither `menu_residentNameScratchFlushComplete()` (`menu.c:7188-7248`) nor `menu_instrumentLoadRequestSelection()` (`:8547-8608`) contains a P3 condition. |
| **Deviation 2** | Accepted. The approved objects total 4 B; the extra 4 B of linked BSS is section alignment. |

**Interactions checked by reading the code:**
- **Leaving Load/Save mid-load.**
  - `filesystem_cancelScanBankScenes()` acts only while a preview scan runs (`filesystem.c:32141`), so it cannot set a flag on the running Bank Load.
  - The name-cache clear cannot disturb the load: its HCNAMES rows stream from the dedicated mirror (`filesystem.c:15426-15433`, `case 85`).
- **Copy/clear names against the Bank's HCNAMES write** (schedule §7.6 item 6). The ordering holds:
  - a loaded child's identity rows are set at its own commit (`filesystem.c:13151-13166`), before the child becomes available to copy into;
  - the end of the load sets only the Bank row and the kept Scenes' sources (`:15108`, `14140-14162`);
  - a copy records only a row remap and clears the destination's refreshed flag; the names resolve when the copy publishes, after the command ends.

  The copied name therefore lands last. Confirm on hardware (§14.4 item 7).
- **Pattern service.** The loader's `patSvc_prepareSceneReplace()` waits only when the child being loaded is the service Scene. A switched-to Scene is always available, so it can never be that child. With `PAR_FOLLOW` off and the view on the home Scene, edits there are dropped (D45), so the service queue the loader waits for stays empty.

### 14.3 Findings

| Id | Severity | Finding | Recommendation |
|----|----------|---------|----------------|
| A1 | Low | **Queued action parts run for a target the executor then drops.** `menu_serviceDeferredSceneChange()` calls `menu_applyQueuedSceneAction(scene)` (`menu.c:9271`) before `menu_perfSceneChangeExecute()` applies the availability test (`:9133-9137`). A SHIFT+SEQ queue armed **before a direct (Merge, transport running) load**, whose target the load then replaces, still clears all mutes (`mut`) and writes Morph 0 into the pending Scene (`mrp`), although no change happens. The Morph write is overwritten when the child loads; the mute reset is audible. The Scene 17 path is unaffected (`seq_switchToBackgroundScene()` clears pending changes). New queues during the load are already gated at the press. | In the `isQueue` branch, skip the whole action when `!bank_sceneAvailable(scene)`: one test before `menu_applyQueuedSceneAction()`. |
| A2 | Low (cosmetic) | **A second Load/Save press on the frozen screen clears its blinks.** `handleModeButtons()` runs `led_clearAllBlinkLeds()` (`buttonHandler.c:1975`) before `menu_switchPage(LOAD_PAGE)`, which then returns at the frozen re-press guard (`menu.c:17184`) without repainting. The PERF-style blinks (active Scene while stopped, viewed Scene with `PAR_FOLLOW` off) are lost until the next beat refresh or child completion. With the transport stopped that can be several seconds. | In the re-press guard, call `menu_refreshLoadSceneLeds()` before returning. |
| A3 | Observation | **SHIFT+REC opened from Load/Save mode shows no W1 blink.** The mode stays `SELECT_MODE_LOAD_SAVE` while the recording settings page is shown, so the owner (`menu.c:9312`) keeps `LED_MODE4` steady even though the load screen is not visible. This matches W1 literally ("not in Load/Save mode"). | Leave as is, unless the user wants the blink keyed to the page (`menu_activePage != LOAD_PAGE`) rather than the mode. |

No defect was found in S1–S4, S6–S9 or the docs.

### 14.4 Hardware test priorities

Run the spec §11 script (1–15) and schedule §12. In particular:

1. **Replace on 17 with audio running; leave Load/Save at once.** Check:
   - MODE4 fast-blinks;
   - pots, encoder and Morph work on Voice, Effect and PERF;
   - no audible change at any child commit;
   - `audioCodec_underrunCount` is unchanged.
2. **PERF fill and switching.** Check:
   - pending Scenes are dark and fill in order;
   - switch 17 → loaded A → loaded B → back to A;
   - no second activation at completion (listen; trace `menu_startSoundApply`);
   - AutoSave stays idle until the end.
3. **Fan-out with links.** Check:
   - a link between two loaded children made mid-load survives completion;
   - a mismatched-layout link is stripped as the second Scene lands;
   - no edit reaches a pending Scene.
4. **Frozen screen.** Check:
   - leave and return several times, with `NN.` reading `01.`–`16.`;
   - completion while on it gives D7, the final index restore, and the overlay on a child failure;
   - completion elsewhere gives no overlay and no repaint of the current page.
5. **Queued and chain changes.** Check:
   - SHIFT+SEQ to a loaded Scene with both fast LEDs;
   - chains frozen during the load and resuming afterwards;
   - A1 (a queue armed before a direct load into a replaced Scene).
6. **`PAR_FOLLOW` off:** STEP edits on the home Scene are dropped until it loads.
7. **Copy/clear mid-load.** Check:
   - a clear on a loaded Scene;
   - an overlapping paste before and after leaving 17;
   - a Kit copy into a loaded Scene, with its names published after the load and correct after a reboot;
   - a job on a pending Scene shows `LOAD_PENDING` in the DEV trace.
8. **Stopped load:** the P1 lock is unchanged and chains do not fire.
9. **MIDI CC0 load:** the D39 take-over, then the unlock at phase 5.
10. **Card pulled mid-load on another page:** the command ends quietly, playback stays, and loaded Scenes stay present.
