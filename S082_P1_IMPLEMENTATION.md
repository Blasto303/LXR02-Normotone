# S082 P1 — Implementation Schedule (Scene 17 remaining work)

*Written 2026-10-18 from a code deep dive of the working tree on
`dev-ph6-midi`: commit `45846b7` plus the uncommitted S082 P0, Phase C and
§13 review-fix changes. Planning only: no code has changed. It implements the
remaining work of `S082_P1_LOAD_SAVE.md` §10 (P1-1 … P1-10) under decisions
D1–D35.*

*Revision 2 (2026-10-18): the user's answers to the derived rules.
- **R1:** nothing loads into Scene 17; every load goes to the home Scene, which is the SEQ shown as the Load destination.
- **R6:** AutoSave is dropped at once. An in-flight drain is aborted, not waited for.
- **R7:** page exits during a background load are dropped, not queued.
- **R8:** confirmed.

Phase E8 is rewritten, E9 is new, and E5.4, E6.5, §2, §10 and Phase F are updated.*

---

## 0. How to use this schedule

### 0.1 Line numbers

- Every location is `file:line` in the working tree described above. Each
  entry also names its **anchor function**, because line numbers move as soon
  as the first edit lands.
- **Commit the current working tree before starting**, so these line numbers
  are pinned to a known revision.
- Within one file, apply edits **bottom-up**, or re-find each anchor by name.
- Paths are relative to the repository root.

### 0.2 Actions

| Tag | Meaning |
|-----|---------|
| **ADD** | New code (function, declaration, branch, field). |
| **MOD** | Change existing code in place. |
| **REM** | Delete existing code. |
| **DOC** | Comment-only change. |

### 0.3 Comment blocks

Every ADD and MOD carries the comment block to paste next to the code, in the
project style:

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

The `.h` declaration carries the same block, shortened to What / Inputs /
Outputs / Accessors where the `.c` already holds the full text (the existing
header style). Where a step replaces an existing comment block, the new block
replaces it entirely.

### 0.4 RAM (policy check)

New static storage: **93 B**, all accepted 2026-10-18 (`S082_P1` §6): 90 B
Scene 17 name capture, 2 B pending MIDI request, 1 B result. §9 has the
table. No other step adds static storage or a large stack frame.

### 0.5 Phases

| Phase | Content | P1 task | Depends on |
|-------|---------|---------|-----------|
| E1 | Scene 17 name capture: storage, fill and read API, filesystem readers for index 16 | P1-1 | — |
| E2 | Menu name paths for index 16 (Kit, Instrument, Scene, Pattern entry and prefill) | P1-1 | E1 |
| E3 | Saving from 17: explicit index-16 guards; the capture follows the saved name | P1-2 | E1 |
| E4 | Rebuild scope and fit (D30/D31); per-track move to 17 | P1-3 | E1 |
| E5 | Request and tick: slot refusal, direct path waits for the card, pending MIDI request, MIDI UI phase, result | P1-4, P1-5, P1-6, P1-8 | E4, E6 |
| E6 | MIDI-origin UI entry: ButtonHandler latch close, Menu entry/poll/idle, page-exit gate, Menu OK slot check | P1-4, P1-6 | — |
| E7 | MIDI drop filter while a Bank load is in flight | P1-7 | — |
| E8 | Nothing loads into Scene 17: every load targets the home Scene (R1) | P1-1, P1-2 | E1 |
| E9 | AutoSave dropped at once: abort an in-flight drain (R6) | P1-5 | — |
| F | Docs, close | P1-9 | all |

Each phase ends with a build (`make all && make img`), the link budget
(`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`) and its
verification rows in §10.

---

## 1. Traceability: decisions → steps

| Decision / task | Steps |
|-----------------|-------|
| D29, P1-1 (names, read paths) | E1.1–E1.7, E2.1–E2.6, E4.4 (capture at build) |
| D29, P1-2 (saving from 17) | E3.1–E3.5, E8 |
| D30, D31, P1-3 | E4.1–E4.8 |
| §4.1 refusal, P1-4 | E5.3, E6.4, E6.7 |
| D1/D12, P1-5, R6 | E5.4 phases 1, 2, 4; E9 |
| R1 | E8 |
| R7 | E6.5 |
| D7, D32, D33, P1-6 | E5.1, E5.3, E5.4 phases 0/6, E6.1–E6.6 |
| D13, D33, P1-7 | E7.1–E7.3 |
| D34, P1-8 | E5.1, E5.2, E5.5, E5.6 |
| D35 | No code: `seq_switchToBackgroundScene()` already keeps the take (it does not bump `seq_sceneEpoch` or reset live-record marks). E4.6 keeps the same rule for the partial move. F updates the D16 wording in the docs. |
| D28 | No code: no gesture can target 17 (SEQ indices are 0..15). E4.5 phase 3 rebuild does **not** move the view (D28), and F records it in the docs. |

---

## 2. Rules derived during the deep dive

R1, R6, R7 and R8 were decided by the user on 2026-10-18. The others were
accepted with the recommendations.

| Id | Rule | Why | Where |
|----|------|-----|-------|
| R1 | **Decided (user):** nothing can load into Scene 17. Every load while on 17 (Kit, KitMrp, Scene, Pattern, Instrument, InstrumentMrp, the `kit` row restore) goes to the **home Scene**, the Scene that was active when 17 was built (`scene_backgroundHomeIndex()`). That is the SEQ the Load page shows as its destination. Saves from 17 stay allowed (D29), and so do copy/clear pastes into 17 (D18: copy/clear is not a load). | Kit, Scene and Pattern Load already default to the home Scene (P0 C3). Nested Instrument entry used the active index (16), whose `1u << 16` mask truncates to 0. The per-Scene Preset Instrument loaders accept a Scene index and would write into 17 if one were passed. | E8 |
| R2 | **A successful normal save from 17 renames the matching captured name** (Kit, Instrument, Scene, Pattern). KitMrp and InstrumentMrp saves are identity-neutral, as for resident Scenes. | A resident save makes the saved name the Scene's identity; the capture is Scene 17's identity (D29), so it behaves the same way. | E3 |
| R3 | **The capture copies from the in-RAM HCNAMES mirror**; there is no card read. If the mirror is not valid, the names read `Empty`. | The mirror is the card authority for the mounted session (Option 1C) and is stable while phase 2 holds the facade idle. A card read would delay the switch (D1, D5). | E1.4 |
| R4 | **The MIDI drop filter does not affect MIDI thru routing.** | Routing runs before interpretation in `midiParser_parseMidiMessage()`; "dropped" means not acted on by Helicase. | E7.2 |
| R5 | **A Menu Bank OK discards a pending (not yet started) MIDI request.** | The user's explicit, later action wins (latest wins, D33). | E5.3 |
| R6 | **Decided (user): drop AutoSave, no need to wait for it to finish.** Every Bank load (with or without Scene 17) aborts an in-flight scalar or Pattern AutoSave drain at its next tick, through the drain's existing error close-down. The aborted record is torn and stale, and the other A/B or ping-pong file keeps the last good one (D1). Only the close of the open handle is waited for, a few ms. The one exception is the HCNAMES register publication (parameter-drain phases 70–77, and the Pattern drain's follow-on HCNAMES update): it cannot be cut, because an abort between the live remove and the temp rename would lose `/.hcnames`, so it finishes. Pending dirty state follows D1/D25 on every path: Replace discards it, Merge keeps it (and re-marks the whole Bank). Queued copy/clear jobs are dropped and the running job completes (D12). | D1, D12, D25. P1-5 needs the card idle before `preset_loadBank()`. This reverses the §13 F1 note "no copy/clear drop, no AutoSave yield or discard" for the direct path. | E5.4, E9 |
| R7 | **Decided (user): drop, don't queue.** While a background Bank load runs, a MODE press, SHIFT+REC, a VOICE press that would enter nested Instrument mode, and any page switch are dropped. They are not queued, so nothing happens after the load. The encoder, pots, SEQ, BAR and COPY are already locked by the accepted command (`menu_loadSaveCommandActive`). | The UI stays on Load:[Bank] (D7). The MIDI preview phase and phases 1–3 have windows with `menu_storageBusy` clear. A queued exit would fire right after the load. | E6.5 |
| R8 | **Confirmed (user):** a MIDI Bank load closes everything (SHIFT+REC without restoring its page, the chain editor, COPY/CLEAR, the looper, nested Instrument, name editing) and shows the Bank Load screen exactly as if it had been entered by hand. | Restoring the SHIFT+REC page could enter Load/Save and post an index request just before the Bank entry clears the shared name cache (the S054 hazard). | E6.1, E6.2 |

---

## Phase E1 — Scene 17 name capture (D29, P1-1)

### E1.1 `Core/Hardware/SD/filesystem.h:1060-1070` (after `filesystem_residentPatternName()`) — ADD row enum and API

```c
/*
 * Scene 17 captured-name rows (S082 P1 D29).
 *
 * What: indices into the 10-row Scene 17 name capture: Scene, Kit, six
 * Instruments, Pattern, Effect.
 * Inputs/Outputs: row ids for filesystem_backgroundName*().
 * Accessors: Preset build (E4), filesystem save paths (E3), resident-name
 * readers for index 16 (E1.5).
 */
enum {
    FS_BACKGROUND_NAME_SCENE = 0u,
    FS_BACKGROUND_NAME_KIT,
    FS_BACKGROUND_NAME_INSTRUMENT_0,
    FS_BACKGROUND_NAME_PATTERN = FS_BACKGROUND_NAME_INSTRUMENT_0 + 6u,
    FS_BACKGROUND_NAME_EFFECT,
    FS_BACKGROUND_NAME_ROW_COUNT
};

/*
 * Capture every Scene-level name of one resident Scene into Scene 17.
 *
 * What: copies the Scene, Kit, six Instrument, Pattern and Effect rows of
 * src_scene from the HCNAMES mirror into the Scene 17 capture.
 * Inputs: resident Scene 0..15. Outputs: capture rows 0..9.
 * Accessors: preset_buildBackgroundScene() (fresh build).
 */
void filesystem_backgroundNamesCaptureScene(uint8_t src_scene);

/*
 * Capture one Instrument name into a Scene 17 slot.
 *
 * What: copies the Instrument row (src_scene, src_slot) into capture row
 * FS_BACKGROUND_NAME_INSTRUMENT_0 + dst_slot.
 * Inputs: Scene 17 slot 0..5, resident source Scene and slot.
 * Outputs: one capture row.
 * Accessors: preset_copyBackgroundSlot() (per-track build and rebuild).
 */
void filesystem_backgroundNameCaptureInstrument(uint8_t dst_slot,
                                                uint8_t src_scene,
                                                uint8_t src_slot);

/*
 * Replace one Scene 17 captured name.
 *
 * What: writes eight display cells into one capture row.
 * Inputs: row (FS_BACKGROUND_NAME_*), eight-cell name (NULL blanks it).
 * Outputs: one capture row.
 * Accessors: Kit, Instrument, Scene and Pattern Save from Scene 17 (E3).
 */
void filesystem_backgroundNameSet(uint8_t row, const char *name);
```

### E1.2 `Core/Hardware/SD/filesystem.c:1105-1107` (after `fs_identity_valid_mask`) — ADD storage

```c
/*
 * Scene 17 captured names (S082 P1 D29).
 *
 * What: ten eight-cell names (plus NUL) for Scene 17: Scene, Kit, six
 * Instruments, Pattern, Effect, captured from the HCNAMES mirror when Scene 17
 * is built (and per copied slot when it is rebuilt), and renamed by a
 * successful normal save from Scene 17.
 * Why: HCNAMES rows 1..16 describe resident Scenes only and are replaced by the
 * Bank load that created Scene 17. fs_identity_name cannot hold them: it has
 * eight rows and is cleared at every Menu session exit. Without this capture,
 * Scene Save, Kit/KitMrp Save, Instrument Save and Pattern Save from 17 would
 * prefill and write `Empty` names, and the Kit/Scene Load/Save entry on 17
 * would fail (D29).
 * Inputs: filesystem_backgroundNamesCaptureScene(),
 * filesystem_backgroundNameCaptureInstrument(), filesystem_backgroundNameSet().
 * Outputs: filesystem_resident*Name(16[, slot]) and
 * filesystem_sceneComponentNameRaw(16, ...).
 * SRAM: 90 B, SRAM1 static, owner filesystem (accepted 2026-10-18,
 * S082 P1 §6). Lifetime: valid while scene_backgroundExists(); stale and
 * unread after the release; overwritten by the next build.
 * Accessors: the functions above. Affiliates: hcnames_name_mirror (source),
 * preset_buildBackgroundScene(), menu_backgroundNamesChanged().
 */
static char fs_background_name[FS_BACKGROUND_NAME_ROW_COUNT]
                              [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
```

**ADD** beside the existing `_Static_assert` group (`filesystem.c:1113-1125`):

```c
_Static_assert(sizeof(fs_background_name) == 90u,
               "S082 P1 D29: Scene 17 name capture is 10 x 9 bytes");
```

### E1.3 `Core/Hardware/SD/filesystem.c:5892-5907` (after `filesystem_cachedResidentName()`) — ADD readers

```c
/*
 * Read one raw Scene 17 captured name.
 *
 * What: returns the capture row as stored (eight cells, NUL-terminated), or
 * eight spaces for an out-of-range row.
 * Why: the Scene Save child-name builders test blankness themselves
 * (filesystem_residentNameIsBlank()) and must see the raw cells, exactly as
 * they see filesystem_cachedResidentName() for resident Scenes.
 * Inputs: FS_BACKGROUND_NAME_* row. Outputs: const pointer into
 * fs_background_name.
 * Accessors: filesystem_sceneComponentNameRaw(), the resident-name readers.
 * Affiliates: fs_background_name.
 */
static const char *filesystem_backgroundNameRaw(uint8_t row)
{
    if (row >= FS_BACKGROUND_NAME_ROW_COUNT)
        return "        ";
    return fs_background_name[row];
}

/*
 * Read one Scene component name for a resident Scene or Scene 17.
 *
 * What: index 16 while Scene 17 exists returns the capture row bg_row;
 * every other index returns filesystem_cachedResidentName(row).
 * Why: Scene Save derives the Pattern child filename and the Effect stem from
 * the source Scene's rows; for a Scene Save from 17 (D15, D29) those rows are
 * the capture, because HCNAMES has no row 17.
 * Inputs: scene index, capture row, resident HCNAMES row for the same
 * component (the sentinel FS_RESIDENT_NAMES_ROW_COUNT for index 16).
 * Outputs: raw eight-cell name.
 * Accessors: filesystem_makePatternChildFilename(),
 * filesystem_saveSceneDirectory_tick() cases 33 and 82.
 * Affiliates: filesystem_backgroundNameRaw(), scene_backgroundExists().
 */
static const char *filesystem_sceneComponentNameRaw(uint8_t scene_index,
                                                    uint8_t bg_row,
                                                    uint16_t row)
{
    if (scene_index == SCENE_BACKGROUND_INDEX && scene_backgroundExists())
        return filesystem_backgroundNameRaw(bg_row);
    return filesystem_cachedResidentName(row);
}
```

### E1.4 `Core/Hardware/SD/filesystem.c` after `filesystem_residentPatternName()` (ends 32976) — ADD capture writers

```c
/*
 * Copy one eight-cell name into a Scene 17 capture row.
 *
 * What: stores up to eight printable cells, pads with spaces, NUL-terminates.
 * Why: one formatter keeps every capture writer consistent with
 * filesystem_setIdentityName().
 * Inputs: row, source cells (NULL = blank). Outputs: one capture row.
 * Accessors: the three public capture functions below.
 * Affiliates: storage_copyDisplayName() (the same eight-cell rule).
 */
static void filesystem_backgroundNameStore(uint8_t row, const char *name)
{
    uint8_t i;

    if (row >= FS_BACKGROUND_NAME_ROW_COUNT)
        return;
    for (i = 0u; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++) {
        char c = name ? name[i] : ' ';
        if (c == '\0')
            name = NULL, c = ' ';
        fs_background_name[row][i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
    fs_background_name[row][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
}

/*
 * Capture all names of the Scene that Scene 17 is built from (D29).
 *
 * What: copies the Scene, Kit, six Instrument, Pattern and Effect rows of
 * src_scene from the HCNAMES mirror into the capture.
 * Why: the Bank load that follows replaces those HCNAMES rows; Scene 17 must
 * keep the identity that was playing (D15, D22, D29). R3: the mirror is the
 * card authority for the mounted session; no card I/O runs, so the switch is
 * not delayed. An invalid mirror yields blank rows, which read as `Empty`.
 * Inputs: resident Scene 0..15; hcnames_name_mirror via
 * filesystem_cachedResidentName(). Outputs: capture rows 0..9.
 * Accessors: preset_buildBackgroundScene() (fresh build, phase 3: the facade
 * is idle, so no HCNAMES writer is mid-rewrite).
 * Affiliates: filesystem_resident*Row(), filesystem_backgroundNameStore().
 */
void filesystem_backgroundNamesCaptureScene(uint8_t src_scene)
{
    uint8_t slot;

    if (src_scene >= SCENE_COUNT)
        return;
    filesystem_backgroundNameStore(FS_BACKGROUND_NAME_SCENE,
        filesystem_cachedResidentName(filesystem_residentSceneRow(src_scene)));
    filesystem_backgroundNameStore(FS_BACKGROUND_NAME_KIT,
        filesystem_cachedResidentName(filesystem_residentKitRow(src_scene)));
    for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++)
        filesystem_backgroundNameCaptureInstrument(slot, src_scene, slot);
    filesystem_backgroundNameStore(FS_BACKGROUND_NAME_PATTERN,
        filesystem_cachedResidentName(filesystem_residentPatternRow(src_scene)));
    filesystem_backgroundNameStore(FS_BACKGROUND_NAME_EFFECT,
        filesystem_cachedResidentName(filesystem_residentEffectRow(src_scene)));
}

/*
 * Capture one Instrument name into a Scene 17 slot (D29).
 *
 * What: copies HCNAMES Instrument row (src_scene, src_slot) into capture row
 * FS_BACKGROUND_NAME_INSTRUMENT_0 + dst_slot.
 * Why: with per-track playback each Scene 17 slot comes from that track's
 * played Scene (D2), so its name must come from the same Scene.
 * Inputs: dst_slot 0..5, resident src_scene, src_slot 0..5.
 * Outputs: one capture row.
 * Accessors: filesystem_backgroundNamesCaptureScene(),
 * preset_copyBackgroundSlot().
 * Affiliates: filesystem_residentInstrumentRow().
 */
void filesystem_backgroundNameCaptureInstrument(uint8_t dst_slot,
                                                uint8_t src_scene,
                                                uint8_t src_slot)
{
    if (dst_slot >= STORAGE_KIT_SLOT_COUNT || src_scene >= SCENE_COUNT)
        return;
    filesystem_backgroundNameStore(
        (uint8_t)(FS_BACKGROUND_NAME_INSTRUMENT_0 + dst_slot),
        filesystem_cachedResidentName(
            filesystem_residentInstrumentRow(src_scene, src_slot)));
}

/*
 * Rename one Scene 17 captured name (R2).
 *
 * What: stores a new eight-cell name in one capture row.
 * Why: a successful normal save from Scene 17 makes the saved name Scene 17's
 * identity, as a resident save does for its HCNAMES row.
 * Inputs: FS_BACKGROUND_NAME_* row, name. Outputs: one capture row.
 * Accessors: Kit Save case 21, Instrument Save completion, Scene Save
 * staging, Pattern Save case 13 (E3).
 * Affiliates: filesystem_backgroundNameStore().
 */
void filesystem_backgroundNameSet(uint8_t row, const char *name)
{
    filesystem_backgroundNameStore(row, name);
}
```

### E1.5 `Core/Hardware/SD/filesystem.c:32904-32976` — `filesystem_residentInstrumentName()`, `filesystem_residentKitName()`, `filesystem_residentSceneName()`, `filesystem_residentPatternName()` — MOD

At the top of each of the four functions, before the mirror test, **ADD**:

```c
    /*
     * Scene 17 reads its captured name (S082 P1 D29).
     *
     * What: index 16 while Scene 17 exists returns the capture row (blank ->
     * the normal `Empty   ` fallback) instead of an HCNAMES mirror cell.
     * Why: HCNAMES has no row for Scene 17; without this the Kit, Scene,
     * Instrument and Pattern Save editors on 17 would prefill `Empty`, and
     * Scene Save from 17 would write `Empty` member names.
     * Inputs: scene_index (and slot). Outputs: eight-cell name pointer.
     * Accessors: Menu entry/prefill (E2), filesystem_requestSaveSceneDirectory().
     * Affiliates: filesystem_backgroundNameRaw(), scene_backgroundExists().
     */
    if (scene_index == SCENE_BACKGROUND_INDEX && scene_backgroundExists()) {
        const char *name = filesystem_backgroundNameRaw(<ROW>);
        return filesystem_residentNameIsBlank(name) ? "Empty   " : name;
    }
```

with `<ROW>` = `FS_BACKGROUND_NAME_INSTRUMENT_0 + instrument_slot` (bounds-checked
`instrument_slot < STORAGE_KIT_SLOT_COUNT`, else `"Empty   "`), `FS_BACKGROUND_NAME_KIT`,
`FS_BACKGROUND_NAME_SCENE`, `FS_BACKGROUND_NAME_PATTERN` respectively.

**DOC** in `filesystem.h:984`, `1014`, `1068`, `1070`: append to each
declaration comment: "Index 16 (Scene 17, while it exists) returns the S082
P1 D29 captured name."

### E1.6 `Core/Hardware/SD/filesystem.c:17566-17571` — `filesystem_makePatternChildFilename()` — MOD

Replace the two `filesystem_cachedResidentName(...)` reads:

```c
    /*
     * Pattern child stem for a resident Scene or Scene 17 (S082 P1 D29).
     *
     * What: the Pattern name row, else the Scene name row, else "pattern";
     * index 16 reads both from the Scene 17 capture.
     * Why: Scene Save from 17 must name its Pattern child after 17's identity,
     * not the blank sentinel row.
     * Inputs: scene_index. Outputs: name for the child filename.
     * Accessors: filesystem_saveSceneDirectory_tick() case 29.
     * Affiliates: filesystem_sceneComponentNameRaw().
     */
    name = filesystem_sceneComponentNameRaw(
        scene_index, FS_BACKGROUND_NAME_PATTERN,
        filesystem_residentPatternRow(scene_index));
    if (filesystem_residentNameIsBlank(name)) {
        name = filesystem_sceneComponentNameRaw(
            scene_index, FS_BACKGROUND_NAME_SCENE,
            filesystem_residentSceneRow(scene_index));
    }
```

### E1.7 `Core/Hardware/SD/filesystem.c:20873-20875` (case 33) and `21104-21106` (case 82) — `filesystem_saveSceneDirectory_tick()` — MOD

Replace in both places:

```c
            /*
             * Effect stem for a resident Scene or Scene 17 (S082 P1 D29).
             *
             * What: the Effect name row of the source Scene; index 16 reads
             * the Scene 17 capture. Why: Scene Save from 17 must name its
             * `.fx` child after 17's Effect, not the blank sentinel row.
             * Inputs: op_kit_save_source_scene. Outputs: fx_stem.
             * Accessors: Scene Save Effect child open.
             * Affiliates: filesystem_sceneComponentNameRaw().
             */
            const char *fx_stem = filesystem_sceneComponentNameRaw(
                op_kit_save_source_scene, FS_BACKGROUND_NAME_EFFECT,
                filesystem_residentEffectRow(op_kit_save_source_scene));
```

---

## Phase E2 — Menu name paths for index 16 (D29, P1-1)

Today, Load/Save entry on Scene 17 fails:
- `menu_requestResidentNameScratch(16)` returns 0 at `menu.c:7397`, so the Kit page never loads its index;
- `menu_requestSceneEntryName()` posts an HCNAMES read for 16, and its completion raises the error overlay (`menu.c:7940-7944`).

### E2.1 `Core/Menu/menu.c:7384-7400` — `menu_requestResidentNameScratch()` — MOD

Replace `if (scene >= 16u) return 0u;` with:

```c
    /*
     * Scene 17 fills the seven-name scratch from its capture (S082 P1 D29).
     *
     * What: for index 16 while Scene 17 exists, the Kit and six Instrument
     * identity rows are set from the capture synchronously and the scratch
     * becomes valid for Scene 16; no HCNAMES read is posted. A previous
     * Scene's dirty rows are still flushed first (that completion re-enters
     * this context). Any other index >= 16 is refused as before.
     * Why: HCNAMES has no row for Scene 17, so the normal entry read cannot
     * supply its names; without them Kit/KitMrp and Instrument Save on 17
     * cannot prefill (D29) and the Kit page stalls.
     * Inputs: scene, scratch state, scene_backgroundExists().
     * Outputs: fs_identity_name Kit/Instrument rows, menu_residentNameScratch*;
     * returns 1 (ready, or flush started).
     * Accessors: menu_requestKitEntryNames(), menu_requestInstrumentEntryNames().
     * Affiliates: filesystem_residentKitName(16),
     * filesystem_residentInstrumentName(16, slot),
     * menu_endResidentNameScratchSession(), menu_backgroundNamesChanged().
     */
    if (scene == SCENE_BACKGROUND_INDEX && scene_backgroundExists()) {
        uint8_t slot;

        if (menu_residentNameScratchValid &&
            menu_residentNameScratchScene == scene)
            return 1u;
        if (menu_residentNameScratchValid &&
            menu_endResidentNameScratchSession())
            return 1u;
        menu_residentNameScratchScene = scene;
        filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                   filesystem_residentKitName(scene));
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
            filesystem_setIdentityName(
                (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 + slot),
                filesystem_residentInstrumentName(scene, slot));
        menu_residentNameScratchValid = 1u;
        return 1u;
    }
    if (scene >= 16u)
        return 0u;
```

**DOC** the existing function comment: add "Index 16 is served from the
Scene 17 capture (D29)".

### E2.2 `Core/Menu/menu.c:7776-7797` — `menu_requestKitEntryNames()` branch A — MOD

Replace the final `(void)menu_requestResidentNameScratch(menu_loadSaveSourceScene); return;` with:

```c
        (void)menu_requestResidentNameScratch(menu_loadSaveSourceScene);
        /*
         * Continue at once when the scratch became valid synchronously
         * (S082 P1 D29: Scene 17's capture). A resident Scene still returns
         * here and re-enters from menu_residentNameScratchLoaded().
         *
         * What: falls through to the Save prefill and Kit index load only
         * when the scratch now belongs to the source Scene.
         * Why: the capture path has no completion callback to re-enter this
         * function.
         * Inputs: scratch state after the request. Outputs: branch B/C below.
         * Accessors: Kit/KitMrp entry and Save prefill.
         * Affiliates: menu_requestResidentNameScratch().
         */
        if (!menu_residentNameScratchValid ||
            menu_residentNameScratchScene != menu_loadSaveSourceScene)
            return;
```

### E2.3 `Core/Menu/menu.c:7709-7713` — `menu_requestInstrumentEntryNames()` — MOD

Same change as E2.2: after `(void)menu_requestResidentNameScratch(menu_instrumentLoadScene);`
continue only when `menu_residentNameScratchValid && menu_residentNameScratchScene == menu_instrumentLoadScene`;
otherwise `return`. Use the same comment block, naming
`menu_instrumentLoadScene` and "nested Instrument entry and Save prefill".

### E2.4 `Core/Menu/menu.c:7926-7961` — `menu_sceneResidentNameLoaded()` — MOD (factor the tail)

Move the tail (from `filesystem_clearNameCache();` through the index request)
into a new static helper and call it from here:

```c
/*
 * Load the Scene browser index after the Scene identity row is set.
 *
 * What: clears the shared name cache, takes the Menu storage gate and posts
 * the `/Scene/.hcindex` load; a refusal releases the gate and defers.
 * Why: one tail shared by the HCNAMES entry read (resident Scenes) and the
 * Scene 17 capture path (D29), so both enter the Scene browser identically.
 * Inputs: none (FS_IDENTITY_SCENE_ROW already set).
 * Outputs: menu_storageBusy, index request or deferral.
 * Accessors: menu_sceneResidentNameLoaded(), menu_requestSceneEntryName().
 * Affiliates: filesystem_requestLoadSceneIndex(),
 * menu_libraryIndexLoadComplete().
 */
static void menu_requestSceneIndexAfterName(void)
{
    filesystem_clearNameCache();
    menu_storageBusy = 1u;
    menu_selectionGenerationSnapshot = menu_selectionGeneration;
    if (!filesystem_requestLoadSceneIndex(menu_libraryIndexLoadComplete)) {
        menu_storageBusy = 0u;
        menu_deferSelectionRequest = 1u;
    }
}
```

### E2.5 `Core/Menu/menu.c:7963-8017` — `menu_requestSceneEntryName()` — MOD

After the facade-busy guard (`menu.c:8003-8006`), **ADD**:

```c
    /*
     * Scene 17 takes its Scene name from the capture (S082 P1 D29).
     *
     * What: for source Scene 16 while Scene 17 exists, the Scene identity row
     * is set from the capture and the Scene index is loaded directly; no
     * HCNAMES read is posted.
     * Why: HCNAMES has no Scene 17 row; the read's completion refuses index
     * 16 with the error overlay, which blocked Scene Save from 17 (D15).
     * Inputs: menu_loadSaveSourceScene, scene_backgroundExists().
     * Outputs: FS_IDENTITY_SCENE_ROW, Scene index request.
     * Accessors: Scene Load/Save entry.
     * Affiliates: filesystem_residentSceneName(16),
     * menu_requestSceneIndexAfterName().
     */
    if (menu_loadSaveSourceScene == SCENE_BACKGROUND_INDEX &&
        scene_backgroundExists()) {
        menu_sceneResidentNameScratchScene = SCENE_BACKGROUND_INDEX;
        filesystem_setIdentityName(
            FS_IDENTITY_SCENE_ROW,
            filesystem_residentSceneName(SCENE_BACKGROUND_INDEX));
        menu_requestSceneIndexAfterName();
        return;
    }
```

### E2.6 `Core/Menu/menu.c` after `menu_backgroundBankLoadAbort()` (ends 9975) + `Core/Menu/menu.h` after 742 — ADD `menu_backgroundNamesChanged()`

```c
/*
 * Drop Menu name scratch that belongs to an older Scene 17 (S082 P1 D29).
 *
 * What: if the seven-name scratch or the Scene-name scratch is keyed to
 * index 16, it is invalidated so the next entry re-reads the capture.
 * Why: index 16 is reused by every Scene 17 lifetime; a scratch left from a
 * previous Scene 17 would prefill the old names. Resident scratch is
 * untouched (no HCNAMES flush is owed for 16; its dirty bit never exists).
 * Inputs: menu_residentNameScratchScene, menu_sceneResidentNameScratchScene.
 * Outputs: scratch validity.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 after every build or
 * rebuild.
 * Affiliates: filesystem_backgroundNamesCaptureScene(),
 * menu_requestResidentNameScratch().
 */
void menu_backgroundNamesChanged(void)
{
    if (menu_residentNameScratchScene == SCENE_BACKGROUND_INDEX) {
        menu_residentNameScratchValid = 0u;
        menu_residentNameScratchScene =
            MENU_RESIDENT_NAME_SCRATCH_INVALID_SCENE;
    }
    if (menu_sceneResidentNameScratchScene == SCENE_BACKGROUND_INDEX)
        menu_sceneResidentNameScratchScene =
            MENU_RESIDENT_NAME_SCRATCH_INVALID_SCENE;
}
```

Pattern Save prefill (`menu.c:14324-14328`) needs no change: it reads
`filesystem_residentPatternName(menu_loadSaveSourceScene)`, which E1.5 serves.

---

## Phase E3 — Saving from 17 (D29, P1-2, R2)

**Audit result:**
- **Source Scene 16 is accepted** by every save. Scene Save (`filesystem.c:30883`), Kit/KitMrp Save (`30830`) and Instrument Save (`32297`) validate with `scene_getConst(16)`. Pattern Save (`31079`) uses `scene_getActiveIndex()`.
- **Their resident HCNAMES provenance writes are already no-ops for 16.** The row helpers return the sentinel, `filesystem_setResidentSource()` and `filesystem_setResidentRefreshed()` bounds-check it, `autosave_markSourceDirty()` range-checks it, and the Menu completions guard `< SCENE_COUNT` (`menu.c:16187-16191`, `16226-16232`).
- **Two masks truncate `1u << 16` to 0 by accident:** `filesystem.c:20973` and `31080`.

E3 makes each 16 case explicit and adds the capture rename (R2).

### E3.1 `Core/Hardware/SD/filesystem.c:19008-19058` — `filesystem_saveKitDirectory_tick()` case 21, normal mode — MOD

Wrap the provenance staging (from `(void)filesystem_setResidentSource(filesystem_residentKitRow(...` to the last `filesystem_setResidentRefreshed(...)`) in `if (op_kit_save_source_scene < SCENE_COUNT) { ... }` and **ADD** the else branch:

```c
            /*
             * Kit Save from Scene 17 renames its captured Kit (S082 P1 R2).
             *
             * What: resident sources write HCNAMES provenance as before; the
             * Scene 17 source has no HCNAMES rows, so its captured Kit name
             * takes the saved name instead.
             * Why: the saved Kit becomes Scene 17's Kit identity, matching a
             * resident Kit Save (D29).
             * Inputs: op_kit_save_source_scene, preset_currentName.
             * Outputs: resident provenance cells, or capture row KIT.
             * Accessors: Kit Save completion.
             * Affiliates: filesystem_backgroundNameSet(),
             * filesystem_setIdentityName().
             */
            } else if (op_kit_save_source_scene == SCENE_BACKGROUND_INDEX &&
                       scene_backgroundExists()) {
                filesystem_backgroundNameSet(FS_BACKGROUND_NAME_KIT,
                                             preset_currentName);
            }
```

### E3.2 `Core/Hardware/SD/filesystem.c:17104-17160` — `filesystem_saveInstrument_tick()` normal-save completion — MOD

After `filesystem_setIdentityName(FS_IDENTITY_INSTRUMENT_ROW_0 + slot, display);`
wrap the provenance block (`filesystem_setResidentSource(...)` through
`op_kit_load_scene_mask = ...`) in `if (op_instrument_save_source_scene < SCENE_COUNT)`,
and **ADD** the else branch with:

```c
            /*
             * Instrument Save from Scene 17 renames its captured slot
             * (S082 P1 R2).
             *
             * What: the resident path stages HCNAMES provenance as before;
             * Scene 17 has no HCNAMES row, so the captured Instrument name of
             * the saved slot takes the saved stem. op_kit_load_scene_mask is 0
             * (explicitly, no longer a truncated shift).
             * Why: the saved Instrument becomes Scene 17's identity for that
             * slot (D29), and Kit/Scene Save from 17 then name the member
             * file after it.
             * Inputs: op_instrument_save_source_scene/slot, display.
             * Outputs: capture row INSTRUMENT_0 + slot.
             * Accessors: nested Instrument Save on 17.
             * Affiliates: filesystem_backgroundNameSet(), the typed index
             * rebuild that follows (unchanged).
             */
            } else {
                if (op_instrument_save_source_scene == SCENE_BACKGROUND_INDEX &&
                    scene_backgroundExists())
                    filesystem_backgroundNameSet(
                        (uint8_t)(FS_BACKGROUND_NAME_INSTRUMENT_0 +
                                  op_instrument_save_source_slot),
                        display);
                op_kit_load_scene_mask = 0u;
            }
```

`op_slot`, `op_library_index_rebuild_kind` and `op_library_index_rebuild_pending`
stay outside the guard (the typed index rebuild is still needed).

### E3.3 `Core/Hardware/SD/filesystem.c:20973-21030` — `filesystem_saveSceneDirectory_tick()` publication staging — MOD

Replace `op_scene_load_scene_mask = (uint16_t)(1u << op_kit_save_source_scene);` with:

```c
            /*
             * Resident Scene rows only (S082 P1 D29): Scene 17 publishes no
             * HCNAMES row; an explicit 0 replaces the truncated 1u << 16.
             */
            op_scene_load_scene_mask = (op_kit_save_source_scene < SCENE_COUNT)
                ? (uint16_t)(1u << op_kit_save_source_scene) : 0u;
```

Wrap the provenance staging block (Scene, Kit, Instrument, Effect, Pattern
sources and `filesystem_setResidentSceneRefreshed()`) in
`if (op_kit_save_source_scene < SCENE_COUNT) { ... }` and **ADD**:

```c
            /*
             * Scene Save from Scene 17 renames its captured Scene (S082 P1 R2).
             *
             * What: Scene 17 has no HCNAMES rows; its captured Scene name takes
             * the saved name. Kit, Instrument, Pattern and Effect names are
             * written from the capture and keep their values.
             * Why: the saved Scene becomes Scene 17's identity (D15, D29).
             * Inputs: op_kit_save_source_scene, op_scene_display_name.
             * Outputs: capture row SCENE.
             * Accessors: Scene Save from 17.
             * Affiliates: filesystem_backgroundNameSet().
             */
            else if (op_kit_save_source_scene == SCENE_BACKGROUND_INDEX &&
                     scene_backgroundExists())
                filesystem_backgroundNameSet(FS_BACKGROUND_NAME_SCENE,
                                             op_scene_display_name);
```

### E3.4 `Core/Hardware/SD/filesystem.c:31079-31080` — `filesystem_requestSave()` `FS_FILE_PATTERN` — MOD

```c
        op_pattern_scene = scene_getActiveIndex();
        /*
         * Pattern Save from Scene 17 (S082 P1 D29): the HCNAMES Pattern
         * publication is a deliberate no-op for index 16 (explicit 0 instead
         * of a truncated 1u << 16); case 13 renames the capture instead.
         */
        op_scene_load_scene_mask = (op_pattern_scene < SCENE_COUNT)
            ? (uint16_t)(1u << op_pattern_scene) : 0u;
```

### E3.5 `Core/Hardware/SD/filesystem.c:15741-15746` — `filesystem_savePattern_tick()` case 13 — MOD

Before `filesystem_startPatternHcnamesUpdate();` **ADD**:

```c
        /*
         * Pattern Save from Scene 17 renames its captured Pattern
         * (S082 P1 R2).
         *
         * What: after the `.pat` file is durable, the Scene 17 capture takes
         * the saved Pattern name; the HCNAMES update that follows has a zero
         * Scene mask and only rebuilds `/Pattern/.hcindex`.
         * Why: the saved Pattern becomes Scene 17's Pattern identity (D29).
         * Inputs: op_pattern_scene, op_pattern_display_name.
         * Outputs: capture row PATTERN.
         * Accessors: Pattern Save on 17.
         * Affiliates: filesystem_backgroundNameSet(),
         * filesystem_startPatternHcnamesUpdate().
         */
        if (op_pattern_scene == SCENE_BACKGROUND_INDEX &&
            scene_backgroundExists())
            filesystem_backgroundNameSet(FS_BACKGROUND_NAME_PATTERN,
                                         op_pattern_display_name);
```

---

## Phase E4 — Rebuild scope and fit (D30, D31, P1-3)

**Defects found:**
- `preset_backgroundScene17Needed()` returns 1 whenever Scene 17 is active (`presetManager.c:3452-3453`, `3459-3460`).
- The rebuild copies every track on a resident Scene, then `seq_switchToBackgroundScene()` moves **all** tracks to 17.
- A non-fitting rebuild track keeps its stale Scene 17 blocks and is then played.
- The fit check counts those stale blocks as used.

### E4.1 `Core/Bank/Scene/Preset/presetManager.c` before `preset_backgroundScene17Needed()` (3431) — ADD `preset_backgroundCandidateTracks()`

```c
/*
 * Tracks whose played resident Scene a Bank load replaces (S082 P1 D31).
 *
 * What: bit t is set when track t plays a resident Scene (0..15) that is in
 * the load's replaced mask (act | empty).
 * Why: one rule for "a played Scene is replaced" (D25) and for which tracks a
 * rebuild in place copies into Scene 17 (D31). Tracks on Scene 17 and tracks
 * on unaffected resident Scenes are never candidates.
 * Inputs: replaced mask, seq_perTrackPattern[]. Outputs: 7-bit track mask.
 * Accessors: preset_backgroundScene17Needed(), preset_buildBackgroundScene(),
 * preset_tickBackgroundBankLoad() phase 3.
 * Affiliates: seq_perTrackPattern[], SCENE_COUNT.
 */
static uint8_t preset_backgroundCandidateTracks(uint16_t replaced)
{
    uint8_t track;
    uint8_t mask = 0u;

    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t played = seq_perTrackPattern[track];

        if (played < SCENE_COUNT &&
            (replaced & (uint16_t)(1u << played)) != 0u)
            mask = (uint8_t)(mask | (uint8_t)(1u << track));
    }
    return mask;
}
```

### E4.2 `Core/Bank/Scene/Preset/presetManager.c:3431-3465` — `preset_backgroundScene17Needed()` — MOD

Replace the body and comment block:

```c
/*
 * Decide whether a Bank load needs Scene 17 (S082 P1 D2-D5, D14, D25, D31).
 *
 * What: Scene 17 is needed when the transport runs and the load replaces or
 * empties the active resident Scene or any track's played resident Scene.
 * While Scene 17 is already active it is needed only when a track still on a
 * resident Scene would lose that Scene (rebuild in place); otherwise the load
 * runs directly and Scene 17 is unchanged (D31).
 * Why: a played Scene's content must not disappear under the transport; a
 * load that touches only unplayed Scenes runs directly. The former
 * "Scene 17 is active -> always rebuild" pulled every track onto 17 (D31).
 * Inputs: replaced = act | empty, seq_isRunning(), active Scene,
 * seq_perTrackPattern[]. Output: 1 = build or rebuild, 0 = direct load.
 * Accessors: preset_tickBackgroundBankLoad() phase 1.
 * Affiliates: preset_backgroundCandidateTracks().
 */
static uint8_t preset_backgroundScene17Needed(uint16_t replaced)
{
    uint8_t active;

    if (!seq_isRunning())
        return 0u;
    active = scene_getActiveIndex();
    if (active < SCENE_COUNT &&
        (replaced & (uint16_t)(1u << active)) != 0u)
        return 1u;
    return (uint8_t)(preset_backgroundCandidateTracks(replaced) != 0u);
}
```

### E4.3 `Core/Bank/Scene/Preset/presetManager.c` before `preset_copyBackgroundTrack()` (3214) — ADD `preset_clearBackgroundTrack()`

```c
/*
 * Empty one Scene 17 track before a rebuild copies into it (S082 P1 D30).
 *
 * What: publishes every step of the track empty (trigger off), which frees
 * its old blocks in the background region.
 * Why: a rebuild candidate's old Scene 17 blocks must neither count against
 * the fit check nor be played when the track does not fit (D30: it is left
 * empty).
 * Inputs: track 0..6. Outputs: Scene 17 track empty, pool chunks freed.
 * Context: foreground; the track plays a resident Scene, not 16, so the ISR
 * does not read these entries.
 * Accessors: preset_buildBackgroundScene() (rebuild only).
 * Affiliates: pat_rawPublishEmpty(), pat_rawFreeChunks().
 */
static void preset_clearBackgroundTrack(uint8_t track)
{
    uint8_t step;

    for (step = 0u; step < NUM_STEPS; step++)
        pat_rawPublishEmpty(SCENE_BACKGROUND_INDEX, track, step,
                            PAT_RAW_TRIGGER_OFF);
}
```

### E4.4 `Core/Bank/Scene/Preset/presetManager.c:3301-3326` — `preset_copyBackgroundSlot()` — MOD

At the end of the function **ADD**:

```c
    /*
     * The slot's name follows its data (S082 P1 D29).
     *
     * What: the captured Instrument name of dst_slot is copied from the same
     * resident Scene and slot as the image. Why: per-track playback mixes
     * slots from several Scenes (D2); the name must match the voice.
     * Inputs: dst_slot, src_scene, src_slot. Outputs: one capture row.
     * Affiliates: filesystem_backgroundNameCaptureInstrument().
     */
    filesystem_backgroundNameCaptureInstrument(dst_slot, src_scene, src_slot);
```

**DOC** the function comment: add "and its captured Instrument name (D29)".

### E4.5 `Core/Bank/Scene/Preset/presetManager.c:3328-3429` — `preset_buildBackgroundScene()` — MOD

New signature: `uint8_t preset_buildBackgroundScene(uint8_t rebuild_in_place, uint16_t replaced_mask)`.

Changes:
1. Fresh build, after `*dst = *src;` and the chain clear: **ADD**
   `filesystem_backgroundNamesCaptureScene(active);` (before the fast-path return).
2. Before the track loop: `uint8_t candidates = rebuild_in_place ? preset_backgroundCandidateTracks(replaced_mask) : 0x7Fu;`
3. In the loop, replace the `src_scene >= SCENE_COUNT` block with:

```c
        if ((candidates & bit) == 0u)
            continue;                   /* D31: not affected by this load */
        if (src_scene >= SCENE_COUNT)
            src_scene = active;         /* fresh build only (no track on 16) */
        if (rebuild_in_place)
            preset_clearBackgroundTrack(track);  /* D30: free, then fit */
```

4. The T6/T7 slot rule is unchanged: tracks 5 and 6 always share a played
   Scene (`seq_setTrackPlayedScene()`), so they are candidates together.

Replacement comment block:

```c
/*
 * Build, or rebuild in place, Scene 17 from what is playing
 * (S082 P1 D2-D4, D9, D19, D29-D31).
 *
 * What: fresh build: copies the active Scene's Scene-level state (settings
 * without chain, Effect, FX sequence, bus compressor, Morph) and all its
 * captured names, then either the whole Pattern region (no per-track
 * overrides) or, per track in T1..T7 order, the track and its voice slot from
 * the track's played Scene. Rebuild in place: only the tracks whose played
 * resident Scene the load replaces are emptied and then copied; Scene 17's
 * Scene-level state and every other track stay. A track that does not fit is
 * left empty and later tracks are still tried (D3, D30). T6/T7's shared slot
 * follows T6 when T6 fits.
 * Why: Scene 17 must sound exactly like what is playing (D4), so the switch
 * changes only the read address (C6); a rebuild must not disturb tracks the
 * load does not touch (D31).
 * Inputs: rebuild_in_place, replaced_mask (act | empty), active Scene,
 * seq_perTrackPattern[], resident Scenes, pool capacity, HCNAMES mirror.
 * Outputs: scene_background, pat_background_region, the name capture;
 * returns the mask of tracks copied.
 * Context: foreground, before the switch or the per-track move; no written
 * track is read by the ISR yet.
 * Accessors: preset_tickBackgroundBankLoad() phase 3.
 * Affiliates: preset_backgroundCandidateTracks(), preset_clearBackgroundTrack(),
 * preset_copyBackgroundTrack(), preset_copyBackgroundSlot(),
 * pat_snapshotScene(), filesystem_backgroundNamesCaptureScene(),
 * scene_backgroundBegin().
 */
```

### E4.6 `Core/Sequencer/sequencer.c` after `seq_switchToBackgroundScene()` (ends 2316) — ADD `seq_moveTracksToBackgroundScene()`

```c
/*
 * Move selected tracks onto Scene 17 without a sound change (S082 P1 D31).
 *
 * What: under PRIMASK, points seq_perTrackPattern[t] at index 16 for each
 * track in track_mask, recomputes seq_perTrackActive and re-snapshots the
 * looper; then cancels pending Scene changes and refreshes the effective
 * track cache. Active Scene, step positions, DDA accumulators, rolls,
 * conditions, epochs, the live-record take (D35) and looper anchors are kept.
 * Why: a rebuild in place copies only the tracks whose resident Scene the
 * load replaces; only those may move, because tracks on unaffected Scenes
 * stay where they are (D31). Each moved track's data and slot were just
 * copied from the Scene it was playing, so the move changes the read address
 * only (D4).
 * Inputs: track_mask (bit t = track t), Scene 17 exists.
 * Outputs: seq_perTrackPattern[], seq_perTrackActive.
 * Context: foreground.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (rebuild).
 * Affiliates: seq_switchToBackgroundScene() (fresh build),
 * seq_recomputePerTrackActive(), seq_loopRefreshSceneSnapshots(),
 * seq_clearPendingSceneChange(), seq_refreshTrackEffectiveParams().
 */
void seq_moveTracksToBackgroundScene(uint8_t track_mask)
{
	uint32_t primask;
	uint8_t track;

	if (!scene_backgroundExists() || !pat_patternValid(SCENE_BACKGROUND_INDEX))
		return;
	primask = __get_PRIMASK();
	__disable_irq();
	for (track = 0u; track < NUM_TRACKS; track++)
		if (track_mask & (uint8_t)(1u << track))
			seq_perTrackPattern[track] = SCENE_BACKGROUND_INDEX;
	seq_recomputePerTrackActive();
	seq_loopRefreshSceneSnapshots();
	__set_PRIMASK(primask);
	seq_clearPendingSceneChange();
	seq_refreshTrackEffectiveParams();
}
```

`Core/Sequencer/sequencer.h:308` — **ADD** after `seq_switchToBackgroundScene()`:

```c
/*
 * Move selected tracks onto Scene 17 without a sound change (S082 P1 D31).
 * Inputs: track_mask (bit t = track t). Outputs: those tracks play index 16;
 * seq_perTrackActive recomputed; pending Scene changes cancelled.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (rebuild in place).
 */
void seq_moveTracksToBackgroundScene(uint8_t track_mask);
```

### E4.7 `Core/Bank/Scene/Preset/presetManager.h:199-205` — `preset_buildBackgroundScene()` declaration — MOD

Signature `uint8_t preset_buildBackgroundScene(uint8_t rebuild_in_place, uint16_t replaced_mask);`.
Shortened comment: "Inputs: rebuild flag; replaced mask (act | empty) selects rebuild
candidates (D31). Outputs: Scene 17 state and name capture; copied-track mask.
Accessors: preset_tickBackgroundBankLoad() phase 3."

### E4.8 Phase 3 of the tick

See E5.4 phase 3. The rebuild calls `seq_moveTracksToBackgroundScene(candidates)` and **does not** call `scene_selectActive()`, `menu_setPlayedPattern()` or `menu_setShownPattern()`. The active Scene is already 16, and D28 forbids moving the view back to 17.

---

## Phase E5 — Request and tick (P1-4, P1-5, P1-6, P1-8)

### E5.1 `Core/Bank/Scene/Preset/presetManager.c:92-124` — background state — MOD + ADD

**MOD** the `pm_bg_phase` comment block, phase list:

```c
 * What: low nibble = phase (0 idle / start a pending MIDI request, 1 decide,
 * 2 quiesce, 3 build or rebuild and switch, 4 post the load, 5 wait for
 * completion, 6 MIDI origin: wait for the Load:[Bank] preview); flags = MIDI
 * origin, Replace, Scene 17 needed, dirty mask discarded.
```

**ADD** after `#define PM_BG_DISCARDED 0x80u`:

```c
#define PM_BG_PHASE_MIDI_UI   6u

/*
 * Pending MIDI Bank load request (S082 P1 D33).
 *
 * What: the Bank slot of the latest MIDI request not yet started, or
 * PM_BG_PENDING_NONE. Why: a MIDI request that arrives while Preset, the
 * filesystem facade or a Menu command is busy is deferred, latest wins, and
 * started by phase 0 once everything is idle; a request while a Bank load is
 * in flight is dropped instead (D13). A Menu Bank OK discards it (R5).
 * Inputs: preset_requestBackgroundBankLoad() (MIDI origin).
 * Outputs: preset_tickBackgroundBankLoad() phase 0.
 * SRAM: 2 B, SRAM1 static, owner Preset (accepted 2026-10-18, S082 P1 §6).
 * Affiliates: menu_backgroundBankLoadUiIdle(), menu_backgroundBankLoadUiBegin().
 */
#define PM_BG_PENDING_NONE    0xFFFFu
static uint16_t                  pm_bg_pending_slot = PM_BG_PENDING_NONE;

/*
 * Result of the last background Bank load (S082 P1 D34).
 *
 * What: PRESET_BG_RESULT_NONE / _DONE / _FAILED in the low bits, plus
 * PRESET_BG_RESULT_MIDI when the load had MIDI origin. Why: the origin must
 * learn completion or failure (S083 sends CC0 TX from it); pm_bg_phase has
 * no free bit. Inputs: on_bank_load_complete() (phase 5), every failure
 * path of the tick, phase 6 refusal. Outputs:
 * preset_takeBackgroundBankLoadResult() (read and clear).
 * SRAM: 1 B, SRAM1 static, owner Preset (accepted 2026-10-18, S082 P1 §6).
 */
static uint8_t                   pm_bg_result = PRESET_BG_RESULT_NONE;
```

### E5.2 `Core/Bank/Scene/Preset/presetManager.h:219-235` — ADD result API

```c
/*
 * Background Bank load result codes (S082 P1 D34).
 * What: NONE (nothing new), DONE, FAILED; PRESET_BG_RESULT_MIDI is ORed in
 * when the load had MIDI origin.
 * Accessors: preset_takeBackgroundBankLoadResult(); S083 CC0 TX.
 */
#define PRESET_BG_RESULT_NONE    0x00u
#define PRESET_BG_RESULT_DONE    0x01u
#define PRESET_BG_RESULT_FAILED  0x02u
#define PRESET_BG_RESULT_CODE    0x03u
#define PRESET_BG_RESULT_MIDI    0x80u

/*
 * Read and clear the last background Bank load result (S082 P1 D34).
 * Inputs: none. Outputs: PRESET_BG_RESULT_* | optional _MIDI; NONE when no
 * load finished since the last read.
 * Accessors: S083 MIDI service (CC0 TX).
 */
uint8_t preset_takeBackgroundBankLoadResult(void);
```

**DOC** `preset_requestBackgroundBankLoad()` declaration (`232`): "MIDI origin:
the slot is held as one pending request (latest wins) and started when the
system is idle; it returns 0 (dropped) while a Bank load is in flight (D13,
D33). Menu origin: refused when the slot does not exist (§4.1)."

**DOC** `preset_backgroundBankLoadRunning()` (`234`): "Nonzero from the start
of a request (including the MIDI UI phase) until completion. A pending,
unstarted MIDI request does not count. MIDI drops non-RT/note/CC input while
it is set (D33)."

### E5.3 `Core/Bank/Scene/Preset/presetManager.c:3467-3520` — `preset_requestBackgroundBankLoad()` — MOD

```c
uint8_t preset_requestBackgroundBankLoad(uint16_t slot, uint16_t act_mask,
                                         uint16_t empty_mask, uint8_t origin)
{
    uint8_t merge;

    merge = (uint8_t)((origin & PRESET_BANK_REQUEST_MERGE) != 0u);
    origin = (uint8_t)(origin & (uint8_t)~PRESET_BANK_REQUEST_MERGE);
    if ((pm_bg_phase & PM_BG_PHASE_MASK) != 0u)
        return 0u;                  /* D13/D33: drop while a load is in flight */
    if (origin == PRESET_BANK_ORIGIN_MIDI) {
        if (slot >= STORAGE_BANK_MAX_SLOTS)
            return 0u;
        pm_bg_pending_slot = slot;  /* D33: latest wins; phase 0 starts it */
        return 1u;
    }
    if (pm_status != PRESET_IDLE)
        return 0u;
    if (!filesystem_bankSlotExists(slot))
        return 0u;                  /* §4.1: never build Scene 17 for nothing */
    pm_bg_pending_slot = PM_BG_PENDING_NONE;     /* R5 */
    pm_request_slot = slot;
    pm_kit_request_scene_mask = act_mask;
    (void)empty_mask;
    pm_bg_result = PRESET_BG_RESULT_NONE;
    pm_bg_phase = (uint8_t)(1u | (merge ? 0u : PM_BG_REPLACE));
    return 1u;
}
```

Replacement comment block:

```c
/*
 * Request a Bank load that keeps playing (S082 P1 §4, D1-D35).
 *
 * What: Menu origin: refused when the slot does not exist in the resident
 * Bank index (§4.1) or Preset is busy; otherwise accepted at once and advanced
 * by the tick. MIDI origin: dropped while a Bank load is in flight (D13);
 * otherwise held as the one pending request (latest wins, D33), which phase 0
 * starts when Preset, the facade and the Menu are idle, by entering
 * Load:[Bank] exactly as an OK would (D7, D32). A Menu request discards a
 * pending MIDI one (R5). The mode is stated by Menu (Merge flag); MIDI is
 * always Replace with all 16 (Q15).
 * Why: a Bank change must not interrupt what is playing (D1-D5); MIDI CC0
 * (S083) must reach the same path; a missing slot must be refused before any
 * Scene 17 work.
 * Inputs: slot, act/empty masks (Menu), origin | PRESET_BANK_REQUEST_MERGE.
 * Outputs: accepted (1) / refused or dropped (0); pm_bg_phase or
 * pm_bg_pending_slot.
 * Accessors: Menu Bank OK (`menu.c:14090`), S083 MIDI CC0.
 * Affiliates: preset_tickBackgroundBankLoad(), filesystem_bankSlotExists(),
 * menu_backgroundBankLoadUiBegin(), preset_backgroundBankLoadRunning().
 */
```

### E5.4 `Core/Bank/Scene/Preset/presetManager.c:3527-3629` — `preset_tickBackgroundBankLoad()` — MOD

Phase changes. Unlisted lines stay.

**Phase 0 (ADD, before the `switch`):**

```c
    /*
     * Start a deferred MIDI request once everything is idle (D33, D7, D32).
     *
     * What: with no load in flight, a pending MIDI slot is started when
     * Preset, the facade and the Menu are idle: the UI is taken to
     * Load:[Bank] at that slot (abandoning any Menu state) and phase 6 waits
     * for its preview, exactly as a user would before OK.
     * Inputs: pm_bg_pending_slot, menu_backgroundBankLoadUiIdle().
     * Outputs: phase 6 with MIDI origin and Replace; pending cleared.
     */
    if (phase == 0u) {
        if (pm_bg_pending_slot == PM_BG_PENDING_NONE ||
            pm_status != PRESET_IDLE || !menu_backgroundBankLoadUiIdle())
            return;
        pm_request_slot = pm_bg_pending_slot;
        pm_bg_pending_slot = PM_BG_PENDING_NONE;
        pm_bg_result = PRESET_BG_RESULT_NONE;
        menu_backgroundBankLoadUiBegin(pm_request_slot);
        pm_bg_phase = (uint8_t)(PM_BG_PHASE_MIDI_UI | PM_BG_ORIGIN_MIDI |
                                PM_BG_REPLACE);
        return;
    }
```

**Phase 6 (ADD case):**

```c
    case PM_BG_PHASE_MIDI_UI: {
        /*
         * MIDI origin: wait for the Load:[Bank] preview, then act as OK
         * (D7, §4.1, Q15).
         *
         * What: the Menu reports WAIT while the Bank index or child preview
         * runs; READY with the Replace/all-16 masks it would compute on OK;
         * REFUSED when the slot is missing (the page shows it as Menu would)
         * or the preview failed. READY continues at phase 1; REFUSED ends
         * the command with a FAILED result.
         */
        uint16_t a = 0u, e = 0u;
        uint8_t ui = menu_backgroundBankLoadUiPoll(pm_request_slot, &a, &e);

        if (ui == MENU_BG_UI_WAIT)
            return;
        if (ui == MENU_BG_UI_REFUSED) {
            pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                                     PRESET_BG_RESULT_MIDI);
            pm_bg_phase = 0u;
            menu_backgroundBankLoadAbort();
            return;
        }
        pm_kit_request_scene_mask = a;
        pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) | 1u);
        return; }
```

**Phase 1 (MOD):** both branches go to phase 2; only `PM_BG_NEEDED` differs:

```c
        if (preset_backgroundScene17Needed(replaced))
            pm_bg_phase |= PM_BG_NEEDED;
        pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) | 2u);
```

Comment: "Decide Scene 17 once (D2-D5, D14, D25, D31). Every request then drops
background work (phase 2: D12 copy/clear, D1/R6 AutoSave aborted, card idle);
only a Scene 17 request builds."

**Phase 2 (MOD):** the first pass is unchanged: `ccSvc_dropQueued()` and
`filesystem_setAutosaveYield(1u)`. With E9 the yield now also **aborts** an
in-flight drain at its next tick, so the existing wait
(`ccSvc_busy() || filesystem_status() == FS_STATUS_BUSY || filesystem_patternSnapshotInUse()`)
lasts:
- for AutoSave, only the close of its open handle;
- for an HCNAMES publication already under way (R6 exception), until it completes;
- for copy/clear, until the running job completes (D12).

Replace the tail:

```c
        /*
         * Drop the outgoing Bank's pending AutoSave state (D1, D25, R6).
         *
         * What: the facade is idle (any drain aborted and closed), so a
         * Replace discards the dirty mask on every path, with or without
         * Scene 17; a Merge keeps it and Menu re-marks the whole Bank after
         * the load. Then build (Scene 17 needed) or load directly.
         */
        if ((pm_bg_phase & PM_BG_DISCARDED) == 0u) {
            if ((pm_bg_phase & PM_BG_REPLACE) != 0u)
                autosave_discardDirtyMask();
            pm_bg_phase |= PM_BG_DISCARDED;
        }
        pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) |
                                ((pm_bg_phase & PM_BG_NEEDED) ? 3u : 4u));
```

The Replace discard runs after an aborted drain's
`autosave_maskRestoreCaptured()`, which puts its captured bits back as part of
the error close-down. The discard therefore clears every bit.

**Phase 3 (MOD):**

```c
    case 3u: {
        /*
         * Build and switch, or rebuild and move only the affected tracks
         * (D2-D5, D19, D27-D31).
         *
         * What: fresh build: build Scene 17 and switch every track to it;
         * the view follows only with PAR_FOLLOW on (D27). Rebuild in place:
         * copy the candidate tracks and move only them (D31); the active
         * Scene is already 17 and the view is never moved (D28). Either way
         * the Menu drops any stale Scene 17 name scratch (D29).
         */
        uint16_t replaced = (uint16_t)(act | empty);
        uint8_t rebuild = scene_backgroundExists();

        if (!preset_buildBackgroundScene(rebuild, replaced) &&
            !scene_backgroundExists()) {
            filesystem_setAutosaveYield(0u);
            pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                ((pm_bg_phase & PM_BG_ORIGIN_MIDI) ? PRESET_BG_RESULT_MIDI : 0u));
            pm_bg_phase = 0u;
            menu_backgroundBankLoadAbort();
            return;
        }
        if (rebuild) {
            seq_moveTracksToBackgroundScene(
                preset_backgroundCandidateTracks(replaced));
        } else {
            seq_switchToBackgroundScene();
            (void)scene_selectActive(SCENE_BACKGROUND_INDEX);
            menu_setPlayedPattern(SCENE_BACKGROUND_INDEX);
            if (parameter_values[PAR_FOLLOW])
                menu_setShownPattern(SCENE_BACKGROUND_INDEX);
        }
        menu_backgroundNamesChanged();
        menu_refreshPerfSceneLeds();
        pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) | 4u);
        return; }
```

The candidate mask is computed **before** any track moves, so
`preset_backgroundCandidateTracks()` returns the same mask in
`preset_buildBackgroundScene()` and here (nothing changes
`seq_perTrackPattern[]` in between).

**Phase 4 (MOD):**

```c
    case 4u:
        /*
         * Post the ordinary two-mask Bank Load; wait while the card is busy
         * (P1-5).
         *
         * What: a refusal while the facade is BUSY (for example a
         * settings.cfg write, D24) is retried next pass; a refusal on an idle
         * facade is a real failure: release the yield, record FAILED, end the
         * command. On failure while on Scene 17 playback stays on 17 (§4.5).
         */
        if (preset_loadBank(pm_request_slot, act, empty,
                            (uint8_t)((pm_bg_phase & PM_BG_REPLACE)
                                          ? PRESET_BANK_LOAD_REPLACE : 0u))) {
            pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) | 5u);
        } else if (filesystem_status() != FS_STATUS_BUSY) {
            filesystem_setAutosaveYield(0u);
            pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                ((pm_bg_phase & PM_BG_ORIGIN_MIDI) ? PRESET_BG_RESULT_MIDI : 0u));
            pm_bg_phase = 0u;
            menu_backgroundBankLoadAbort();
        }
        return;
```

`preset_loadBank()` calls `filesystem_ack()` and sets `pm_status` before the
filesystem request, and restores `PRESET_IDLE` on refusal
(`presetManager.c:3131-3141`). A retry is therefore safe.

**Phase 5:** unchanged (the result is set in E5.6).

**MOD** the function comment block (What/Why/Inputs/Outputs/Accessors/Affiliates) to list phases 0–6 as in E5.1, and to name
`menu_backgroundBankLoadUiIdle()`, `menu_backgroundBankLoadUiPoll()`,
`menu_backgroundNamesChanged()`, `seq_moveTracksToBackgroundScene()` and
`preset_backgroundCandidateTracks()` as affiliates.

### E5.5 `Core/Bank/Scene/Preset/presetManager.c` after `preset_backgroundBankLoadRunning()` (3525) — ADD `preset_takeBackgroundBankLoadResult()`

```c
/*
 * Read and clear the last background Bank load result (S082 P1 D34).
 *
 * What: returns pm_bg_result and resets it to NONE.
 * Why: the origin of a load needs one completion signal; a read-and-clear
 * byte lets S083 send exactly one CC0 reply per load.
 * Inputs: pm_bg_result. Outputs: PRESET_BG_RESULT_* (| _MIDI).
 * Accessors: S083 MIDI service.
 * Affiliates: on_bank_load_complete(), preset_tickBackgroundBankLoad().
 */
uint8_t preset_takeBackgroundBankLoadResult(void)
{
    uint8_t result = pm_bg_result;

    pm_bg_result = PRESET_BG_RESULT_NONE;
    return result;
}
```

### E5.6 `Core/Bank/Scene/Preset/presetManager.c:597-650` — `on_bank_load_complete()` — MOD

Before `preset_completeFilesystemOp(PRESET_OP_BANK_LOAD);` **ADD**:

```c
    /*
     * Record the background load's result for its origin (S082 P1 D34).
     *
     * What: when this completion belongs to a background request (phase 5),
     * DONE or FAILED from the terminal filesystem status, with the MIDI
     * flag when the request had MIDI origin. Child failures inside a DONE
     * load still show the normal overlay and count as DONE. The boot load
     * (no background phase) records nothing.
     * Inputs: pm_bg_phase, filesystem_status(). Outputs: pm_bg_result.
     * Affiliates: preset_takeBackgroundBankLoadResult().
     */
    if ((pm_bg_phase & PM_BG_PHASE_MASK) == 5u)
        pm_bg_result = (uint8_t)(
            ((filesystem_status() == FS_STATUS_DONE)
                 ? PRESET_BG_RESULT_DONE : PRESET_BG_RESULT_FAILED) |
            ((pm_bg_phase & PM_BG_ORIGIN_MIDI) ? PRESET_BG_RESULT_MIDI : 0u));
```

---

## Phase E6 — MIDI-origin UI entry (D7, D32, D33, P1-4, P1-6)

**Defects found:**
- `menu_backgroundBankLoadUiBegin()` (`menu.c:9946-9956`) returns silently when the Menu is busy, but the load still runs.
- It does not close SHIFT+REC, end a copy/clear session or release the looper.
- It does not update the ButtonHandler mode or the mode LEDs.
- From a non-Load page it skips `menu_switchPage()`'s page-leave cleanup.

### E6.1 `Core/Hardware/frontPanel/buttonHandler.c:1861-1879` + after `buttonHandler_shiftRecMenuExit()` (ends ~2341) — MOD + ADD

**ADD** a shared static helper above `handleModeButtons()` (`1832`):

```c
/*
 * Close every latched front-panel surface before a mode change (S080 P2,
 * S081, S082 P1 D7/D32).
 *
 * What: closes the SHIFT+REC overlay (restoring its saved page only when
 * restore_page is set), closes the chain editor, ends held live-erase
 * gestures, and releases the looper when the mode leaves PERF.
 * Why: one implementation for the MODE press and the MIDI-origin Bank load,
 * which must leave the panel exactly as a MODE press would (D7); the MIDI
 * path does not restore the SHIFT+REC page (R8) because the Bank entry
 * follows at once.
 * Inputs: restore_page, leaving_perf. Outputs: latches closed, looper and
 * erase holds released.
 * Accessors: handleModeButtons(), buttonHandler_enterLoadSaveForBackgroundLoad().
 * Affiliates: buttonHandler_shiftRecMenuExit(), menu_chainMenuClose(),
 * liveRec_eraseReleaseHeld(), buttonHandler_loopForceRelease().
 */
static void buttonHandler_closeLatchedSurfaces(uint8_t restore_page,
                                               uint8_t leaving_perf)
{
    if (bh_shiftRecMenuActive) {
        if (restore_page) {
            buttonHandler_shiftRecMenuExit();
        } else {
            bh_shiftRecMenuActive = 0u;
            led_setBlinkLed(LED_REC, 0u);
            led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH),
                         LED_REC);
            led_setValue(buttonHandler_getShift(), LED_SHIFT);
        }
    }
    if (menu_chainMenuIsOpen())
        menu_chainMenuClose();
    if (liveRec_mode() == LIVEREC_MODE_ERASE) {
        liveRec_eraseReleaseHeld();
        bh_eraseSeqMask = 0u;
        bh_eraseFxSeqMask = 0u;
        bh_eraseVoiceMask = 0u;
    }
    if (leaving_perf)
        buttonHandler_loopForceRelease();
}
```

The helper needs forward declarations for `buttonHandler_shiftRecMenuExit()`
(already at `1831`) and `buttonHandler_loopForceRelease()` (defined at ~1585,
before use).

**MOD** `handleModeButtons()`: replace `1861-1879` and the `leaving PERF`
block (`1880-1892`) with one call. Compute `next` first, as the code does
today, then call
`buttonHandler_closeLatchedSurfaces(1u, bh_state.selectButtonMode == SELECT_MODE_PERF && next != SELECT_MODE_PERF);`.
The existing comment blocks move onto the helper (merged above).

**ADD** after `buttonHandler_shiftRecMenuExit()`:

```c
/*
 * Put the front panel into Load/Save for a MIDI-origin Bank load
 * (S082 P1 D7, D32).
 *
 * What: closes every latched surface without restoring the SHIFT+REC page
 * (R8), ends an open COPY/CLEAR session (nothing is applied), cancels the
 * double-click window, leaves Morph voice mode, and sets the Load/Save mode
 * with its mode LEDs; BAR LEDs go dark (non-PERF rule). The Menu page change
 * is Menu's (menu_backgroundBankLoadUiBegin()).
 * Why: a MIDI Bank load takes the UI exactly as if the user had pressed MODE
 * to Load/Save and OK (D7), abandoning any Menu state (D32).
 * Inputs: bh_state, latch flags, copy/clear session.
 * Outputs: bh_state.selectButtonMode = SELECT_MODE_LOAD_SAVE; LEDs.
 * Accessors: menu_backgroundBankLoadUiBegin().
 * Affiliates: buttonHandler_closeLatchedSurfaces(), copyClear_copyReleased(),
 * dblclick_cancel(), buttonHandler_setMorphVoiceMode().
 */
void buttonHandler_enterLoadSaveForBackgroundLoad(void)
{
    buttonHandler_closeLatchedSurfaces(
        0u, (uint8_t)(bh_state.selectButtonMode == SELECT_MODE_PERF));
    copyClear_copyReleased();
    dblclick_cancel();
    buttonHandler_setMorphVoiceMode(0u);
    bh_state.selectButtonMode = SELECT_MODE_LOAD_SAVE;
    led_clearAllBlinkLeds();
    led_setMode2(SELECT_MODE_LOAD_SAVE);
    led_setValue(0u, LED_BAR1);
    led_setValue(0u, LED_BAR2);
}
```

`copyClear_copyReleased()` (`copyClearSession.c:614`) is the existing clean
end of a session: it returns when no session is open. A COPY still held
physically is later released into that no-op. A queued copy/clear job that
already started is handled by phase 2 (`ccSvc_dropQueued()`, wait for the
running job, D12).

`Core/Hardware/frontPanel/buttonHandler.h` after `113` — **ADD**:

```c
/*
 * Put the front panel into Load/Save for a MIDI-origin Bank load
 * (S082 P1 D7, D32).
 * Inputs: none. Outputs: latches closed, copy/clear session ended,
 * Load/Save mode and LEDs. Menu performs the page change.
 * Accessors: menu_backgroundBankLoadUiBegin().
 */
void buttonHandler_enterLoadSaveForBackgroundLoad(void);
```

### E6.2 `Core/Menu/menu.c:9932-9956` — `menu_backgroundBankLoadUiBegin()` — MOD

```c
void menu_backgroundBankLoadUiBegin(uint16_t slot)
{
    buttonHandler_enterLoadSaveForBackgroundLoad();
    menu_pendingPageSwitch = MENU_PENDING_PAGE_NONE;
    menu_bankCopyForceRelease();
    menu_bankUiFlags = 0u;
    led_setValue(0u, LED_COPY);
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
        menu_loadSaveEnterTopAt(LOAD_PAGE, SAVE_TYPE_BANK, slot);
    } else {
        menu_saveOptions.what = SAVE_TYPE_BANK;
        menu_switchPage(LOAD_PAGE);
        menu_currentPresetNr[SAVE_TYPE_BANK] = slot;
    }
    menu_kitLoadSceneMask = 0xFFFFu;
    menu_beginLoadSaveCommand();
    menu_refreshLoadSceneLeds();
}
```

Replacement comment block:

```c
/*
 * Enter Load:[Bank] for a MIDI-origin background Bank load
 * (S082 P1 D7, D32, Q15).
 *
 * What: closes every latched surface and ends any copy/clear session
 * (ButtonHandler), discards a queued page exit and any Bank-page COPY state,
 * then enters the top-level Bank Load page at the requested slot: inside
 * Load/Save through menu_loadSaveEnterTopAt() (which also ends a nested
 * Instrument session, a name edit and flushes owed HCNAMES rows); from any
 * other page through menu_switchPage(LOAD_PAGE), whose page-leave cleanup
 * runs and whose single Bank index request then previews the slot installed
 * right after it (the slot is read only at the asynchronous completion).
 * Replace with all 16 Scenes selected; the accepted-command state (`...`)
 * locks input from here to the end of the load.
 * Why: a MIDI CC0 load must look exactly like a front-panel OK and must take
 * the UI whatever it was doing (D32).
 * Inputs: Bank slot. Precondition: menu_backgroundBankLoadUiIdle() (the
 * facade is idle, so clearing the shared name cache is safe).
 * Outputs: Load:[Bank] page, command active, Bank index/preview requested.
 * Accessors: preset_tickBackgroundBankLoad() phase 0.
 * Affiliates: buttonHandler_enterLoadSaveForBackgroundLoad(),
 * menu_loadSaveEnterTopAt(), menu_switchPage(), menu_beginLoadSaveCommand(),
 * menu_backgroundBankLoadUiPoll().
 */
```

**Check while applying:** `menu_loadSaveTypeIsRestored(SAVE_TYPE_BANK)` must
be true on `LOAD_PAGE` (`menu.c:6332`), so that `menu_resetSaveParameters()`
inside `menu_switchPage()` keeps the Bank type.

### E6.3 `Core/Menu/menu.c` after `menu_backgroundBankLoadUiBegin()` — ADD `menu_backgroundBankLoadUiIdle()`

```c
/*
 * Report whether a deferred MIDI Bank load may take the UI now (S082 P1 D33).
 *
 * What: nonzero when no Menu storage request, Load/Save command, queued page
 * exit, Instrument transaction or Preset operation is active and the
 * filesystem facade is idle.
 * Why: D33 defers a MIDI request while anything else is busy; starting the
 * Bank entry under an in-flight request would clear the shared name cache
 * beneath it (S054) or strand that request's completion.
 * Inputs: menu_storageBusy, menu_loadSaveCommandActive,
 * menu_pendingPageSwitch, menu_loadInstrumentTransactionBusy(),
 * preset_getStatus(), filesystem_status().
 * Outputs: boolean.
 * Accessors: preset_tickBackgroundBankLoad() phase 0.
 * Affiliates: menu_backgroundBankLoadUiBegin().
 */
uint8_t menu_backgroundBankLoadUiIdle(void)
{
    return (uint8_t)(!menu_storageBusy && !menu_loadSaveCommandActive &&
                     menu_pendingPageSwitch == MENU_PENDING_PAGE_NONE &&
                     !menu_loadInstrumentTransactionBusy() &&
                     preset_getStatus() == PRESET_IDLE &&
                     filesystem_status() != FS_STATUS_BUSY);
}
```

### E6.4 `Core/Menu/menu.c` after E6.3 — ADD `menu_backgroundBankLoadUiPoll()`

```c
/*
 * Poll the MIDI-origin Load:[Bank] preview (S082 P1 D7, §4.1, Q15).
 *
 * What: WAIT while the Bank index or child preview for the slot is running
 * or deferred; REFUSED when the page is no longer top-level Load:[Bank], the
 * slot does not exist in the Bank index (the page already shows it as Menu
 * would), or the preview finished without valid masks; READY with the masks
 * the OK handler would post (Replace, all 16 selected).
 * Why: the MIDI load must use the same preview and outcome table as the
 * front-panel OK, and must refuse a missing slot before any Scene 17 work.
 * Inputs: slot, menu_storageBusy, menu_deferSelectionRequest, preview
 * triple, filesystem_bankSlotExists().
 * Outputs: MENU_BG_UI_*; *act_mask / *empty_mask on READY.
 * Accessors: preset_tickBackgroundBankLoad() phase 6.
 * Affiliates: menu_requestBankChildPreview(), menu_bankLoadPreviewComplete(),
 * menu_bankOperationMasks().
 */
uint8_t menu_backgroundBankLoadUiPoll(uint16_t slot, uint16_t *act_mask,
                                      uint16_t *empty_mask)
{
    if (menu_activePage != LOAD_PAGE || menu_instrumentLoadActive ||
        menu_saveOptions.what != SAVE_TYPE_BANK ||
        menu_currentPresetNr[SAVE_TYPE_BANK] != slot)
        return MENU_BG_UI_REFUSED;
    if (menu_storageBusy || menu_deferSelectionRequest)
        return MENU_BG_UI_WAIT;
    if (!menu_bankLoadPreviewValid || menu_bankLoadPreviewSlot != slot ||
        !filesystem_bankSlotExists(slot))
        return MENU_BG_UI_REFUSED;
    menu_bankOperationMasks(act_mask, empty_mask);
    return MENU_BG_UI_READY;
}
```

The rule "not busy, not deferred, not valid ⇒ failed" holds because:
- every step of the entry chain either sets `menu_storageBusy` (index, preview), arms `menu_deferSelectionRequest` (facade busy), or publishes the preview synchronously (missing slot, `menu.c:8200-8204`);
- a failed preview returns without setting valid (`menu.c:8162-8168`).

`Core/Menu/menu.h` after `742` — **ADD**:

```c
/*
 * MIDI-origin Bank load UI hooks (S082 P1 D7, D29, D33).
 * menu_backgroundBankLoadUiIdle(): nonzero when a deferred MIDI request may
 * take the UI. menu_backgroundBankLoadUiPoll(): WAIT / READY (masks filled) /
 * REFUSED for the Load:[Bank] preview of slot.
 * menu_backgroundNamesChanged(): drop Menu name scratch keyed to Scene 17.
 * Accessors: preset_tickBackgroundBankLoad().
 */
#define MENU_BG_UI_WAIT     0u
#define MENU_BG_UI_READY    1u
#define MENU_BG_UI_REFUSED  2u
uint8_t menu_backgroundBankLoadUiIdle(void);
uint8_t menu_backgroundBankLoadUiPoll(uint16_t slot, uint16_t *act_mask,
                                      uint16_t *empty_mask);
void    menu_backgroundNamesChanged(void);
```

**DOC** `menu.h:731` (`menu_backgroundBankLoadUiBegin()`): update to the E6.2
summary ("closes latched surfaces, ends copy/clear, enters Load:[Bank] at the
slot, command active; precondition menu_backgroundBankLoadUiIdle()").

### E6.5 Drop page and mode changes during a background load (R7) — MOD

The drop must happen where the gesture starts, so that ButtonHandler's mode
state and LEDs never change for a page that will not change. `menu_switchPage()`
gets the same rule as a backstop for any other caller.

**(a) `Core/Hardware/frontPanel/buttonHandler.c:1833-1845` — `handleModeButtons()`**:
after the existing `menu_loadInstrumentTransactionBusy()` gate, **ADD**:

```c
    /*
     * A running background Bank load owns the panel (S082 P1 R7, D7).
     *
     * What: every MODE press is dropped while
     * preset_backgroundBankLoadRunning(): no latch closes, no looper
     * release, no mode/LED change, no page switch, and nothing is queued.
     * Why: the UI stays on Load:[Bank] until the load ends and then stays
     * there (D7); a queued exit would fire right after the load, and an
     * applied one would leave the page under a running load.
     * Inputs: preset_backgroundBankLoadRunning(). Outputs: none.
     * Accessors: MODE button presses.
     * Affiliates: menu_switchPage() backstop (c), preset_tickBackgroundBankLoad().
     */
    if (preset_backgroundBankLoadRunning())
        return;
```

**(b) `Core/Hardware/frontPanel/buttonHandler.c` — `buttonHandler_shiftRecMenuEnter()` (~2312)**:
first statement, **ADD** `if (preset_backgroundBankLoadRunning()) return;`
with the comment "SHIFT+REC is dropped while a background Bank load owns the
panel (S082 P1 R7); nothing is saved or switched."

**(c) `Core/Menu/menu.c:16465` — `menu_switchPage()`**: before the existing
busy branch, **ADD**:

```c
    /*
     * Backstop: no page switch while a background Bank load runs
     * (S082 P1 R7).
     *
     * What: a page request is dropped (not queued in
     * menu_pendingPageSwitch) while preset_backgroundBankLoadRunning().
     * Why: ButtonHandler drops MODE and SHIFT+REC at the source (a, b); this
     * catches any other caller. The load's own switches
     * (menu_chainMenuClose() and menu_switchPage(LOAD_PAGE) in
     * menu_backgroundBankLoadUiBegin()) run in phase 0 before the phase is
     * set, so they are not affected.
     * Inputs: preset_backgroundBankLoadRunning(). Outputs: none.
     * Affiliates: handleModeButtons(), menu_backgroundBankLoadUiBegin().
     */
    if (preset_backgroundBankLoadRunning())
        return;
```

**(d) `Core/Menu/menu.c:9589-9592` — `menu_loadInstrumentVoicePressed()`**:
after the page/voice test, **ADD**:

```c
    /*
     * VOICE is consumed and ignored during an accepted command or a
     * background Bank load (S082 P1 R7).
     *
     * What: returns handled without entering nested Instrument mode.
     * Why: entering it would leave Load:[Bank] under the running load.
     * Inputs: menu_loadSaveCommandActive, preset_backgroundBankLoadRunning().
     */
    if (menu_loadSaveCommandActive || preset_backgroundBankLoadRunning())
        return 1u;
```

`menu_processPendingPageSwitch()` is unchanged: nothing is queued during a
background load. The MIDI entry already clears any queued exit (E6.2).

### E6.6 `Core/Menu/menu.c:14075-14097` — Menu OK, Load `SAVE_TYPE_BANK` — MOD

```c
                    if (menu_bankLoadPreviewValid &&
                        menu_bankLoadPreviewSlot == bslot &&
                        filesystem_bankSlotExists(bslot)) {
```

Comment:

```c
                /*
                 * Bank Load OK needs a valid preview of an existing slot
                 * (S082 B2.10, S082 P1 §4.1).
                 *
                 * What: a missing slot (shown `Empty`) is refused here, as it is
                 * in preset_requestBackgroundBankLoad(); its preview is valid
                 * with empty masks, so without this check OK would request a
                 * Replace that the filesystem refuses only after Scene 17 work.
                 */
```

### E6.7 Refusal display

There is no new display. A missing slot shows as the Bank browser shows it
(`filesystem_bankSlotName()` → `Empty   `). The MIDI command ends through
`menu_backgroundBankLoadAbort()`, so the page unlocks on Load:[Bank] at that
slot (§4.1).

---

## Phase E7 — MIDI drop filter while a Bank load is in flight (D13, D33, P1-7)

### E7.1 `Core/MIDI/MidiMessages.h:93-94` — ADD

```c
/*
 * Bank Select LSB (CC32). With CC_BANK_CHANGE (CC0) it forms a MIDI Bank
 * Change; both are dropped while a Bank load is in flight (S082 P1 D13).
 */
#define CC_BANK_SELECT_LSB  0x20u
```

### E7.2 `Core/MIDI/MidiParser.c` before `midiParser_parseMidiMessage()` (1787) — ADD

```c
/*
 * MIDI that may act while a Bank load is in flight (S082 P1 D13, D33).
 *
 * What: accepts system realtime (0xF8-0xFF), note on/off, and control change
 * except a Bank Change (CC0 / CC32). Everything else is rejected: program
 * change, pitch bend, polyphonic and channel pressure, MTC quarter frame,
 * song position/select and the other system common messages.
 * Why: D33 drops non-realtime/note/CC input during a load, and D13 drops a
 * Bank Change and program change, which would otherwise start a second load
 * or a Scene change into Scenes that are being written. Clock and transport
 * keep playback in sync; notes and CCs keep the performance playing.
 * Inputs: msg (status, data1). Outputs: 1 = act on it, 0 = drop.
 * Accessors: midiParser_parseMidiMessage().
 * Affiliates: preset_backgroundBankLoadRunning(), MidiRealtime (the realtime
 * queue path is not filtered).
 */
static uint8_t midiParser_acceptedDuringBankLoad(MidiMsg msg)
{
    uint8_t type = (uint8_t)(msg.status & 0xF0u);

    if (msg.status >= 0xF8u)
        return 1u;
    if (type == NOTE_ON || type == NOTE_OFF)
        return 1u;
    if (type == MIDI_CC)
        return (uint8_t)(msg.data1 != CC_BANK_CHANGE &&
                         msg.data1 != CC_BANK_SELECT_LSB);
    return 0u;
}
```

### E7.3 `Core/MIDI/MidiParser.c:1808-1809` — `midiParser_parseMidiMessage()` — MOD

After `if(msg.bits.sysxbyte) return;` **ADD**:

```c
	/*
	 * Drop non-realtime/note/CC input while a Bank load is in flight
	 * (S082 P1 D13, D33). Thru routing above is unaffected (R4); only
	 * Helicase's own interpretation is suppressed. A pending, unstarted MIDI
	 * Bank request does not count as in flight.
	 */
	if (preset_backgroundBankLoadRunning() &&
	    !midiParser_acceptedDuringBankLoad(msg))
		return;
```

`presetManager.h` is already included (`MidiParser.c:53`).

---

## Phase E8 — Nothing loads into Scene 17 (R1)

**Rule (user, 2026-10-18):** nothing can load into Scene 17. While 17 is
active, every load goes to the **home Scene** (`scene_backgroundHomeIndex()`,
the Scene that was active when 17 was built). That is the SEQ the Load page
shows as its destination. Saves from 17 stay allowed (D29), and so do
copy/clear pastes into 17 (D18: copy/clear is not a load).

**Load paths and their destination while on 17:**

| Load | Destination today | After E8 |
|------|-------------------|----------|
| Kit, KitMrp | home (P0 C3: `preset_loadDrumset()` `presetManager.c:2910`, `filesystem_requestLoad()` `filesystem.c:30727`) | unchanged |
| Scene, Pattern | home (C3: `menu_switchPage()` LOAD_PAGE default `menu.c:16731`, `menu_resetLoadSaveSceneSelection()` `menu.c:9215`) | unchanged |
| Bank | resident 0..15 only | unchanged |
| Instrument, InstrumentMrp, `kit` row restore | **refused**: the destination is the active index 16, whose mask `1u << 16` truncates to 0; the per-Scene loaders would write into 17 if given 16 | home (E8.1, E8.2); Preset refuses 16 (E8.3) |

The Load-page SEQ indicator (`menu_refreshLoadSceneLeds()`, `menu.c:9321-9348`)
lights `menu_kitLoadSceneMask`. That is the home bit for Kit/Scene/Pattern
(C3) and, after E8.2, for nested Instrument Load. The SEQ shown as the Load
destination is therefore always the home Scene. The code lights Load
destinations steady (only Save sources blink, `menu.c:9346`). The verification
row in §10 checks on hardware that the indicated SEQ is the home Scene.

### E8.1 `Core/Menu/menu.c` before `menu_loadInstrumentVoicePressed()` (9576) — ADD `menu_instrumentEntryScene()`

```c
/*
 * Destination/source Scene of a nested Instrument session (S082 P1 R1, D29).
 *
 * What: the active Scene, except while Scene 17 is active: a Load session
 * targets the home Scene (the SEQ the Load page shows as its destination) and
 * a Save session reads Scene 17.
 * Why: nothing loads into Scene 17 (R1). Every load goes to the home Scene,
 * as Kit, Scene and Pattern Load already do (P0 C3). Instrument Save from
 * 17 is allowed and reads 17's slot and captured name (D29). Without this,
 * Load entry used index 16 and its 1u << 16 mask truncated to 0.
 * Inputs: save_mode, scene_getActiveIndex(), scene_backgroundHomeIndex().
 * Outputs: Scene index 0..15 (Load) or 0..16 (Save).
 * Accessors: menu_loadInstrumentVoicePressed(),
 * menu_loadSaveEnterInstrumentLoad(), menu_loadSaveEnterInstrumentSave().
 * Affiliates: menu_kitLoadSceneMask, menu_refreshLoadSceneLeds(),
 * preset_loadInstrumentForScenes(), the E8.3 Preset guards.
 */
static uint8_t menu_instrumentEntryScene(uint8_t save_mode)
{
    uint8_t scene = scene_getActiveIndex();

    if (scene >= SCENE_COUNT && !save_mode)
        scene = scene_backgroundHomeIndex();
    return scene;
}
```

### E8.2 `Core/Menu/menu.c:9638-9640`, `10019-10021`, `10074-10076` — MOD

At each of the three sites replace:

```c
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
```

with:

```c
    /*
     * S082 P1 R1: a Load session on Scene 17 targets the home Scene (the SEQ
     * shown as the Load destination); a Save session reads Scene 17 (D29).
     * The mask is explicit for index 16 instead of a truncated shift.
     */
    menu_instrumentLoadScene = menu_instrumentEntryScene(menu_instrumentSaveMode);
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (menu_instrumentLoadScene < SCENE_COUNT)
        ? (uint16_t)(1u << menu_instrumentLoadScene) : 0u;
```

The save-mode argument at each site:
- `9638`: `menu_instrumentSaveMode` (set two lines above);
- `10019`: `0u` (Load entry);
- `10074`: `1u` (Save entry).

The `kit` row snapshot (`menu_prepareInstrumentLoadTemp()`) and its restore
then use the home Scene's slot, which is the slot that Load writes.

### E8.3 `Core/Bank/Scene/Preset/presetManager.c:2835`, `2862`, `3726`, `3848` — `preset_loadInstrumentTemp()`, `preset_loadInstrumentMorphTemp()`, `preset_loadInstrument()`, `preset_loadInstrumentMorph()` — MOD

First statement of each, **ADD**:

```c
    /*
     * Nothing loads into Scene 17 (S082 P1 R1).
     *
     * What: a destination Scene outside the resident range (index 16) is
     * refused before any Preset or filesystem state changes.
     * Why: these loaders take a Scene index, and scene_get(16) is valid while
     * Scene 17 exists, so a stray caller could otherwise write into 17. The
     * Menu passes the home Scene (E8.1); this is the backstop.
     * Inputs: destination_scene. Outputs: 0 (refused) for index >= SCENE_COUNT.
     * Affiliates: menu_instrumentEntryScene(), scene_backgroundHomeIndex().
     */
    if (destination_scene >= SCENE_COUNT)
        return 0u;
```

`preset_loadInstrumentForScenes()` (`3737`) and every Kit/Scene/Pattern/Bank
load take a 16-bit mask, which cannot name Scene 17, so they need no guard.

**DOC** `presetManager.h`, the four declarations: append "Refuses Scene 17
(index 16): nothing loads into it (S082 P1 R1)."

---

## Phase E9 — AutoSave dropped at once (R6, D1)

**Today:** `fs_autosave_yield` only stops the schedulers from admitting a new
drain (`filesystem.c:25627`, `25852`). Phase 2 then waits for an in-flight
drain to finish, which can take seconds for a full A/B record.

**After E9:** the yield also aborts an in-flight drain at its next tick, using
the error close-downs both drains already have. Closing the open handle is the
only wait. The one exception is the HCNAMES register publication, which runs to
completion (R6).

### E9.1 `Core/Hardware/SD/filesystem.c:1405-1423` (`fs_autosave_yield` comment) and `25001-25021` (`filesystem_setAutosaveYield()`) — DOC

Replace the "What" lines in both:

```c
 * What: one persistent flag. While set, both AutoSave schedulers decline new
 * drains, and an in-flight scalar or Pattern drain aborts at its next tick
 * through its error close-down (open handles closed, record left torn and
 * stale, captured dirty bits restored). The parameter drain's HCNAMES
 * publication (phases 70-77) and the Pattern drain's follow-on HCNAMES update
 * are never cut: an abort between the live remove and the temp rename would
 * lose /.hcnames (S082 P1 R6).
```

**Why** (add): "D1/R6: a Bank load drops AutoSave at once instead of waiting
for a record that the load makes stale anyway."

**Affiliates** (add): `filesystem_autosaveParameterDrain_tick()`,
`filesystem_autosavePatternDrain_tick()`, `filesystem_autosaveWriterFinishError()`.

### E9.2 `Core/Hardware/SD/filesystem.c:8392` — `filesystem_autosaveParameterDrain_tick()` — ADD (after the `DEV_STALL_DETECTION` observer, before `switch (op_phase)`)

```c
    /*
     * Abort the scalar drain for a Bank load (S082 P1 R6, D1).
     *
     * What: while fs_autosave_yield is set, every phase except the HCNAMES
     * publication (70-77) and the error close-down itself (40-46) routes
     * through filesystem_autosaveWriterFinishError(): open source/target
     * handles are closed, the transaction's captured dirty bits are restored,
     * and the operation ends ERROR. The writer's completion re-arms its
     * five-second retry, which the yield and the Scene 17 gate keep from
     * running until the load is over.
     * Why: the Bank is about to be replaced; the record being written is
     * stale, and the load must not wait for it (D1). The A/B scheme keeps the
     * other file as the last valid record. Recovery writes (30-39, 44) may
     * leave both files invalid. That is accepted, because the old Bank's
     * records are ignored after the load anyway (identity mismatch, D21).
     * Inputs: fs_autosave_yield, op_phase.
     * Outputs: close-down phases 40-43 or ERROR; the facade becomes idle.
     * Accessors: preset_tickBackgroundBankLoad() phase 2 (sets the yield and
     * waits for the facade).
     * Affiliates: filesystem_autosaveWriterFinishError(),
     * filesystem_autosaveWriterCompleted(), autosave_maskRestoreCaptured().
     */
    if (fs_autosave_yield &&
        !(op_phase >= 70u && op_phase <= 77u) &&
        !(op_phase >= 40u && op_phase <= 46u)) {
        filesystem_autosaveWriterFinishError();
        return;
    }
```

`filesystem_autosaveWriterFinishError()` either moves to close-down phase 40
or 42 (excluded from the test, so the close-down runs on the following ticks)
or finishes ERROR at once.

No new trace record is added. The existing TERMINAL record (flags bit 0 = 0)
marks the aborted drain, and no named error code is set, so no error overlay
appears.

### E9.3 `Core/Hardware/SD/filesystem.c:15777-15781` — `filesystem_autosavePatternDrain_tick()` — ADD (before `switch (op_phase)`)

```c
    /*
     * Abort the Pattern drain for a Bank load (S082 P1 R6, D1).
     *
     * What: while fs_autosave_yield is set, phases 0-1 (no handle open)
     * finish ERROR at once; phases 3-8 (file open, streaming) jump to the
     * close at phase 9 with ERROR status. Phase 2 (open pending) waits for its
     * callback and is aborted from phase 3 on the next tick. Phases 9-10
     * (closing) and the follow-on HCNAMES Pattern update are not cut. The
     * ERROR completion keeps the Scene's dirty bit, and the snapshot is
     * released (filesystem_patternSnapshotInUse() covers phases 2-6 only).
     * Why: the record being written is stale once the Bank changes (D1), and
     * the load must not wait for a full PAT4 stream; the ping-pong partner
     * keeps the last valid file.
     * Inputs: fs_autosave_yield, op_phase, op_file.
     * Outputs: ERROR completion after the handle closes.
     * Accessors: preset_tickBackgroundBankLoad() phase 2.
     * Affiliates: filesystem_autosavePatternDrainSchedule_tick(),
     * filesystem_autosaveNonSemanticPatternDrainSchedule_tick() (same op),
     * filesystem_patternSnapshotInUse().
     */
    if (fs_autosave_yield) {
        if (op_phase <= 1u) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (op_phase >= 3u && op_phase <= 8u) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9u;
        }
    }
```

### E9.4 What phase 2 still waits for

With E9 in place, phase 2 of the background tick waits on the facade only for:
- an aborted drain's handle close (a few ms);
- an HCNAMES publication already under way (R6 exception);
- a `settings.cfg` or trace-flush write (short; D24);
- the running copy/clear job (D12; there is no abort API).

The Replace discard follows (E5.4).

---

## Phase F — Docs and close (P1-9)

| File | Change |
|------|--------|
| `S082_P1_LOAD_SAVE.md` | §10: mark P1-1 … P1-8 done with the build numbers. P1-1 text: names come from the in-RAM HCNAMES mirror, not a card read (R3). D16: change the wording per D35. §4.1 step 2: an in-flight AutoSave drain is aborted (R6), except an HCNAMES publication. §4.2 "Names": the capture (E1). §4.6: the release leaves the capture stale and unread. §7: record R1–R8 with the user's answers. Add R1 to D18 ("nothing loads into 17; loads go to the home Scene"). |
| `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | Scene 17: no return (D28); names and saves from 17 (D29, R2); nothing loads into 17, every load goes to the home Scene shown as the Load destination (R1). MIDI: request/defer/drop rules, `preset_takeBackgroundBankLoadResult()`, the drop filter (D33, R4). Panel: MODE, SHIFT+REC, VOICE entry and page switches are dropped during a background load (R7). |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | Scene 17 name capture; Scene/Kit/Instrument/Pattern Save from 17 (no HCNAMES publication, capture rename). |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | +93 B: `fs_background_name` 90, `pm_bg_pending_slot` 2, `pm_bg_result` 1 (measured from `arm-none-eabi-nm`). |
| `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md` | A MIDI Bank load ends an open COPY/CLEAR session (nothing applied); queued jobs are dropped by every Bank load (R6). |
| `knowledge_files/specification_reference/AUTOSAVE.md` | Every Bank load aborts an in-flight scalar/Pattern drain through its error close-down (torn record; the HCNAMES publication phases are never cut). A Replace discards the dirty mask on every path; a Merge keeps it (R6, D1, D25). |
| `MEMORY.md` | Scene 17 invariants: the capture is the identity of index 16; rebuild moves only affected tracks; MIDI defer/drop. |
| `tools/decode_devlogs.py` | No change (no new trace records). |

---

## 9. RAM summary (policy)

| Item | Bytes | Region | Lifetime | Owner | Status |
|------|------:|--------|----------|-------|--------|
| `fs_background_name` | 90 | SRAM1 static | permanent | filesystem | accepted 2026-10-18 (D29) |
| `pm_bg_pending_slot` | 2 | SRAM1 static | permanent | Preset | accepted 2026-10-18 (D33) |
| `pm_bg_result` | 1 | SRAM1 static | permanent | Preset | accepted 2026-10-18 (D34) |
| **Total new** | **93** | | | | within the accepted §6 totals |

Stack: no new buffer. `preset_clearBackgroundTrack()` uses none, and
`preset_copyBackgroundTrack()` keeps its existing `PAT_RAW_BLOCK_MAX` (136 B)
buffer.

---

## 10. Verification per phase

| Phase | Rows |
|-------|------|
| E1/E2 | On 17 (built from Scene N with per-track overrides from M): Kit Save and KitMrp Save prefill N's Kit name; Instrument Save prefills each slot's name from the Scene that slot came from; Scene Save prefills N's Scene name; Pattern Save prefills N's Pattern name. The Kit page loads its index (no stall). The Scene page has no error overlay. Then release 17, build a new 17 from another Scene: every prefill shows the new names (no stale scratch). |
| E3 | Scene Save from 17: the Pattern child and the `.fx` child are named after 17's Pattern and Effect names; member files after the captured Instrument names. After a Kit, Instrument, Scene or Pattern Save from 17, the next prefill on 17 shows the saved name. `/.hcnames` is unchanged by these saves (rows 1..16 untouched). KitMrp and InstrumentMrp saves do not rename. |
| E4 | On 17, move T3 to Scene 5 per track. Bank load Merge that does not touch Scene 5: no rebuild, T3 stays on 5. Replace (Scene 5 replaced): T3 copied into 17, sounds the same, T3 now on 17; other tracks untouched. Force T3 not to fit: T3 silent on 17 (not its old 17 content). T6/T7 move together. The view stays where it was in every case (D28). |
| E5 | Menu OK on a missing slot: refused, nothing changes. MIDI request while a Kit Load runs: deferred, starts after it (latest of two requests wins). MIDI request during a load: dropped. A stopped Replace load: dirty mask discarded; a Merge load: kept and the whole Bank re-marked. `preset_takeBackgroundBankLoadResult()` gives DONE / FAILED once per load, with the MIDI flag for MIDI origin. |
| E6 | MIDI request from PERF with the looper engaged, from SHIFT+REC, from the chain editor, from a COPY session, from Save name editing, from nested Instrument Load, from Load:[Kit]: each ends on the Load:[Bank] screen at the slot, the same as entering it by hand, with `...`, then unlocks on Load:[Bank] after the load. During the load MODE, SHIFT+REC and VOICE do nothing (mode LEDs unchanged) and nothing happens after the load. Missing slot by MIDI: the page shows `Empty` and unlocks, result FAILED. |
| E7 | During a load: clock/start/stop keep sync; notes play; CCs (other than 0/32) act; PC, pitch bend and pressure do nothing; MIDI thru still forwards everything. After the load all messages act again. |
| E8 | On 17, on each Load page (Kit, KitMrp, Scene, Pattern, nested Instrument and InstrumentMrp), the SEQ shown as the destination is the home Scene. Each load writes the home Scene, and 17's sound does not change. The `kit` row restores the home Scene's slot. Instrument Save saves 17's slot. A copy/paste into 17 still works (D18). |
| E9 | Start a Bank load (Menu and MIDI, running and stopped) during a scalar drain (trace phases 3, 13, 21), a recovery write, and a Pattern drain (phases 3–8): each drain ends ERROR within a few ticks (TERMINAL flags 0, no error overlay), and the load starts without waiting for the record. A load during an HCNAMES publication (phases 70–77): it waits for the publication, and `/.hcnames` stays intact. After release, AutoSave writes a fresh record. Reboot after an aborted drain: the boot picks the other A/B file, or no record (D21), with no crash. |
| Regression | `S082_P1` §8 rows; `S082_P0_IMPLEMENTATION.md` §13.5; Kit/Scene/Instrument/Pattern Save from resident Scenes publish HCNAMES as before; Bank Load/Save round trips (§10 B3 rows). |

---

## 11. Implementation log

**2026-10-19 implementation (two sessions, context-continued).**

| Phase | Steps | Build text | Deviations |
|-------|-------|-----------|------------|
| E1 | E1.1–E1.5 | 591,912 B | None. |
| E2 | E2.1–E2.4 | 591,912 B | None (dead-code elimination absorbed the new code). |
| E3 | E3.1–E3.5 | 591,912 B | None. |
| E4 | E4.1–E4.8 | 593,248 B | E5.1/E5.2 pulled forward: `pm_bg_result` and `PRESET_BG_RESULT_*` referenced in E4.8 tick rewrite. Forward decl added for `preset_backgroundCandidateTracks()` before `preset_buildBackgroundScene()`. |
| E5 | E5.1–E5.6 | — | Built together with E6. |
| E6 | E6.1–E6.6 | 594,620 B | None. |
| E7 | E7.1–E7.3 | 594,620 B | None (LTO folded the filter). |
| E8 | E8.1–E8.3 | 594,264 B | None. |
| E9 | E9.1–E9.3 | 594,344 B | None. |
| F | Spec docs, MEMORY.md, S082_P1_LOAD_SAVE.md | 594,344 B | None. |

Final build: `text=594,344`, `data=420`, `bss=430,920`; flash payload
594,764 B of 753,664 B (**headroom 158,900 B**). DTCM/FXBUF unchanged.
+93 B BSS: `fs_background_name` 90, `pm_bg_pending_slot` 2, `pm_bg_result` 1.

---

## 12. Post-implementation assessment (2026-10-19)

*Written for whoever applies the follow-up fixes and runs the hardware tests.*

**Method.** Every step of E1–E9 was read in the working tree, against the
schedule and against the code around it, including the callers and callees
each change depends on. `build/lxr02.elf` is newer than every changed source
and was inspected without rebuilding (`arm-none-eabi-size`,
`arm-none-eabi-nm`). Nothing was tested on hardware, and no code was changed.

### 12.1 Verdict

- **E1–E8 are implemented as scheduled.** Every scheduled symbol, call site and
  guard is present, and the D-rules are honoured: rebuild scope and fit, the
  MIDI pending/defer/drop rules, the R7 panel drops, R1 "nothing loads into 17",
  and the result register.
- **E9 is implemented exactly as scheduled, but the schedule's abort rule is
  wrong (A1).** It aborts the scalar AutoSave drain in phases that are waiting
  for an asynchronous card callback. That can corrupt the Bank load that
  follows, or hang it. This is a defect in the schedule, not in the
  implementation.
- **One latent name-register defect is now reachable on Scene 17 (A3).** It
  existed for resident multi-destination loads. E2 (the Kit page now works on
  17) combined with R1 (loads go to the home Scene) makes it the normal case.
  It can empty the home Scene at boot.
- The rest are low-severity code and documentation points (A4–A9).

**Build and RAM:**
- The final ELF matches the log: `text=594,344`, `data=420`, `bss=430,920`.
- The new storage is `fs_background_name` 90 B (BSS), `pm_bg_result` 1 B
  (BSS) and `pm_bg_pending_slot` 2 B. The last one is in **`.data`**, because it
  starts at `0xFFFF`. The total is 93 B as approved.
- The log's identical text size for E1, E2 and E3 (591,912 B) cannot be
  checked from the final ELF; only the final figure is confirmed.

### 12.2 Conformance by phase

| Phase | Result | Notes |
|-------|--------|-------|
| E1 | Conforms | Capture storage, writers and readers as scheduled. `filesystem_residentKit/Scene/PatternName()` carry two-line comments instead of the full block (A6). |
| E2 | Conforms | The Kit and Instrument entries continue when the scratch fills synchronously; the Scene entry uses the capture; `menu_requestSceneIndexAfterName()` is factored out. |
| E3 | Conforms, one deviation | Kit, Instrument, Scene and Pattern Save have explicit 16 guards and capture renames. Scene Save moved `filesystem_prepareResidentNamesCache()` inside the resident-only branch (A4). |
| E4 | Conforms | Candidates, clear-then-fit, and the partial move to 17. The candidate mask is computed twice from an unchanged `seq_perTrackPattern[]`, as scheduled. The view is not moved on a rebuild (D28). |
| E5 | Conforms | Phases 0–6, the slot refusal, the phase 4 retry while the card is busy (`filesystem_ack()` does nothing while BUSY, so the retry is safe), the uniform Replace discard, and the result recorded in `on_bank_load_complete()`. |
| E6 | Conforms | The shared latch helper; the MIDI entry closes everything (R8); the idle and poll functions; the R7 drops at MODE, SHIFT+REC, VOICE and `menu_switchPage()`; the slot check on Bank OK. `filesystem_bankSlotExists()` is safe on the Bank page: the child preview scan does not touch the shared name cache. No internal `menu_switchPage()` caller runs during a load. The comment explaining the Instrument-transaction gate in `handleModeButtons()` was deleted (A6). |
| E7 | Conforms | Filter after thru routing (R4); the realtime queue is untouched. |
| E8 | Conforms | Entry helper and three sites; four Preset guards (one-line comments, A6). |
| E9 | Implemented as scheduled; the schedule is defective | A1, A2. |
| F | Done, with errors | A7. |

### 12.3 Findings

| Id | Severity | Location | Finding | Fix |
|----|----------|----------|---------|-----|
| A1 | **High** | `filesystem.c:8495-8499` `filesystem_autosaveParameterDrain_tick()` (E9.2; schedule defect) | The abort runs in **every** phase except 40–46 and 70–77, including phases that wait for an asynchronous card callback. There are two cases. **(a) Open or remove pending** (phases 2, 11, 12, 18, 24, 25, 31, 35, 51): `filesystem_autosaveWriterFinishError()` finishes the operation at once, but the AsyncFATFS callback is still due. `on_file_opened()` / `on_remove_complete()` then write `op_file`/`op_file_ready`/`op_remove_done` **inside the Bank load that starts next**. The load can read the AutoSave file as its own handle, or continue on a remove it never issued, and a handle leaks. **(b) Close pending** (phases 4, 14, 15, 22, 33, 37, 55): the handle pointer is cleared only when its close completes (for example `14`, `15`, `22`), so the close-down issues a second `fclose()` on a closing handle at phase 40/42. If AsyncFATFS refuses it, phase 40/42 retries forever and the facade stays BUSY. With R7 the user cannot leave Load:[Bank]. Phase 2 of the background tick sets the yield at the moment the drain is most likely to be in one of these waits, so this is likely in practice. | Abort only at phases with no callback outstanding. Replace the test with an allow-list helper, `filesystem_autosaveDrainAbortSafe(op_phase)`: true for 0, 1, 3, 5, 10, 13, 16, 17, 19, 21, 30, 32, 34, 36, 38, 39, 44, 50, 52, 54, 56, 62, 64, 65, 66, 67. In every other phase the drain runs on, and the abort fires at the next allowed phase, a few ms later at most. The cursor-polled seek waits (20, 53, 63) also stay excluded. Re-run the E9 rows of §10 with trace-confirmed aborts in a wait phase and in an action phase. |
| A2 | Medium | `filesystem.c:15927-15937` `filesystem_autosavePatternDrain_tick()` (E9.3) | Phase 7 polls `afatfs_fseek()`, which can be IN_PROGRESS across ticks. The abort jumps from phase 7 straight to the close at phase 9 with a seek still in progress. Phases 0–1, 2 and 9–10 are handled correctly. | Exclude phase 7 (`op_phase >= 3u && op_phase <= 8u && op_phase != 7u`); the abort then fires at phase 8. |
| A3 | **High** (pre-existing, now the normal case on 17) | `menu.c` Kit/Instrument name session; `filesystem.c:19189` (Kit Save), `17277` (Instrument Save), `filesystem_cacheCurrentResidentKitNames()` `6582` | One identity block (`fs_identity_name`) serves the name-session Scene and every load target. On 17 the Kit page's session Scene is 16 (the Save source, C3), while Kit Load writes the home Scene (R1). The Kit Load rewrites the identity rows with the loaded names and marks home dirty, so **(1)** Kit/Instrument Save prefill on 17 shows the loaded home names, not 17's capture (against D29). **(2)** A Kit Save from 17 in the same session then sets the identity Kit row to the saved name (unconditionally, `19189`). At session exit, the flush writes **that** name into the **home** Scene's HCNAMES Kit row, whose source is the loaded Kit slot. The boot reader builds the Kit folder as `NNN <name>` from those two values (`filesystem.c:29014`), so the folder is not found, and the home Scene is emptied as Case 3 (the F5 pattern). Instrument Save from 17 after an Instrument/Kit Load does the same to an Instrument row. Resident Scenes have had this whenever a Kit Load's destination did not include the session Scene. | Keep the identity block owned by one Scene at a time. When a Kit or Instrument load commits to a mask that **excludes** the name-session Scene, end the session at that completion: flush the dirty rows now (`menu_endResidentNameScratchSession()`, using the existing deferred-retry path) and invalidate the scratch, so the next prefill re-seeds from the session Scene (the capture for 16). Add the same check before accepting Kit, Instrument and Scene Save: if `menu_residentNameDirtySceneMask` has a bit other than the source Scene's, flush first. Test: on 17, Kit Load into home, then Kit Save from 17 under a new name, exit, reboot without an AutoSave record. Home must keep its Kit, and `.hcnames` home Kit row = the loaded Kit name. |
| A4 | Low | `filesystem.c:21216-21226` Scene Save staging (E3.3) | `filesystem_prepareResidentNamesCache()` now runs only for resident sources, so a Scene Save from 17 enters the HCNAMES update without clearing or invalidating the mirror first. This differs from the request-function contract (F9 ordering rule). The effect is probably harmless (mask 0; the read refills the rows), but it is untested. | Move the call back after the `if/else`, as before E3, so both paths prepare the cache. |
| A5 | Low | `menu.c:286` `menu_finishLoadSaveCommand()` → `menu_resetSaveParametersEx(1u)` | A refused MIDI request (missing slot) ends with the page on `bank_restoreBankSlot()` (the current Bank), not on the requested slot as E6.7 states. The preview then belongs to the other slot, so the LEDs stay dark and OK is refused until the slot moves. | Accept and correct E6.7, or re-request the preview of the shown slot after the abort (`menu_requestBankChildPreview(menu_currentPresetNr[SAVE_TYPE_BANK])` in `menu_backgroundBankLoadAbort()` when on Load:[Bank]). |
| A6 | Low (documentation) | `buttonHandler.c:1872-1875`; `presetManager.c:2888`, `2917`, `3941`, `4065`; `filesystem.c` resident-name readers | The "Freeze mode ownership through the complete Instrument transaction" block was deleted from `handleModeButtons()`. The E8.3 guards and three E1.5 readers carry one- or two-line comments instead of the scheduled blocks. | Restore the deleted block; paste the scheduled blocks. |
| A7 | Low (documentation) | `S082_P1_LOAD_SAVE.md` §10 rows P1-3, P1-8; `STORAGE_SRAM_MANIFEST.md:10-13` | P1-3 cites "R8" (should be D30) and P1-8 cites "R7" (should be D34). The manifest says "+93 B BSS"; it is 91 B BSS + 2 B `.data` (plus 2 B of flash initialiser). §10's per-phase sizes repeat the unverifiable intermediate figures. | Correct the references and the section split; keep only the final figures. |
| A8 | Info | `presetManager.c:3719-3801` phases 2 and 4 | Phases 2 and 4 wait without a time limit (a copy/clear job, a stalled facade). Before R7 a MODE exit was queued, but it could not run until the load finished either, so this is not a new hang. Now every exit is dropped, so a stuck load can only be left by a power cycle. | Optional: a bounded wait (for example 10 s of polls) that records FAILED, releases the yield and ends the command. |
| A9 | Info | — | No hardware verification yet; §10 rows E1–E9 and the regression rows are open. | — |

### 12.4 Recommended order

1. **A1 and A2** before any hardware test of a Bank load during AutoSave.
2. **A3** before testing Kit/Instrument Save on Scene 17. Its resident form also
   affects multi-destination Kit Loads.
3. A4, A5.
4. Hardware: §10 rows E1–E9, then the regression rows, with `DEV_STALL_DETECTION`
   on so that any facade stall is recorded.
5. A6, A7.

### 12.5 Status

- E1–E8 conform to the schedule.
- E9 needs the A1/A2 correction.
- A3 is open, and is a pre-existing defect that is now the normal case on 17.
- Nothing has been tested on hardware.

*Update 2026-10-19: A1–A8 are fixed. A9 (hardware verification) stays open.
See §13.*

---

## 13. Assessment fixes (2026-10-19)

All A1–A8 fixes are in the working tree and built. A9 is the open hardware
test. Nothing has been tested on hardware.

### 13.1 Fixes applied

| Id | Result | Change (file : anchor) |
|----|--------|------------------------|
| A1 | **Fixed** | `filesystem.c` : new `filesystem_autosaveDrainAbortSafe()` (before `filesystem_autosaveParameterDrain_tick()`). It is an allow-list of the action phases that have no AsyncFATFS callback outstanding: 0, 1, 3, 5, 10, 13, 16, 17, 19, 21, 30, 32, 34, 36, 38, 39, 44, 50, 52, 54, 56, 62, 64, 65, 66, 67. The yield test in the drain tick now uses it. Every open/close/remove/seek wait phase, the error close-down (40–46) and the HCNAMES publication (70–77) run on, so the abort fires at the next action phase. Each listed phase was checked: no callback outstanding, handles accurate; 16 and 38 are polled `afatfs_sync()`; 19, 52 and 62 start a seek and then move to its cursor-polled wait phase (20, 53, 63). |
| A2 | **Fixed** | `filesystem.c` : `filesystem_autosavePatternDrain_tick()` yield block. Phase 7 (seek possibly IN_PROGRESS) is excluded; the abort fires at phase 8. |
| A3 | **Fixed** (resident form included) | `menu.c` : three parts. **(1)** `menu_resetLoadSaveSceneSelection()`: on `LOAD_PAGE` the source/name-session Scene is the home Scene (the Scene that Load writes, R1); `SAVE_PAGE` keeps the active index (saves from 17, D29). Off Scene 17 both are the active Scene, so nothing changes there. **(2)** `menu_requestResidentNameScratch()` (Scene 17 and resident paths): dirty rows are flushed before the identity is reseeded even when the scratch is already invalid; the Scene 17 path defers while the facade is busy. **(3)** New `menu_flushForeignNameRows(source)`, called at Save-context entry in `menu_requestKitEntryNames()` (SAVE_PAGE), `menu_requestInstrumentEntryNames()` (Save mode) and `menu_requestSceneEntryName()` (SAVE_PAGE). Rows owed for any Scene other than the Save source (any Scene at all for 17) are written before the prefill, so typed names are never lost; the flush completion re-enters the Save page. This closes both the Scene 17 case (Kit Load into home, then Kit Save from 17) and the resident cases (multi-destination Kit Load, or Scene Load then Scene Save, followed by a save from another Scene). |
| A4 | **Fixed** | `filesystem.c` : Scene Save staging. `filesystem_prepareResidentNamesCache()` is back after the resident/Scene 17 `if/else`, so both paths prepare the register before the HCNAMES update (F9 ordering rule). |
| A5 | **Fixed** | `menu.c` : `menu_backgroundBankLoadAbort()`. After finishing the command on top-level Load:[Bank] it calls `menu_requestCurrentLoadSaveSelection(0u)`, so the preview, LEDs and OK gate follow the slot on screen (the resident Bank's slot after the reset). It covers refusal, failure and timeout. §E6.7's claim that "the page unlocks at that slot" is superseded: the page shows the resident Bank slot, previewed. |
| A6 | **Fixed** | `buttonHandler.c` : `handleModeButtons()`, the "Freeze mode ownership through the complete Instrument transaction" block restored. `presetManager.c` : the four Instrument loader guards now carry the full comment block (two-line statement). `filesystem.c` : `filesystem_residentKit/Scene/PatternName()` carry the full block. |
| A7 | **Fixed** | `S082_P1_LOAD_SAVE.md` §10: P1-3 cites D30 and P1-8 cites D34; intermediate sizes removed. §7 R-table: the R8 row (it described the rebuild, which is D31) now states the MIDI close-all rule (D39); R6 and R7 note the A1/A2 and A8 changes. `STORAGE_SRAM_MANIFEST.md`: 91 B BSS + 2 B `.data` (plus 2 B flash initialiser); the A8 second use of `pm_bg_pending_slot`; the current build line. |
| A8 | **Fixed, no new RAM** | `presetManager.c` : new `PM_BG_WAIT_LIMIT_MS` (20 s), `preset_bgSetPhase()`, `preset_bgWaitExpired()`, `preset_bgEnd()`; the tick rewritten on top of them, with the phase logic unchanged. The waits in phases 2, 4 and 6 end FAILED (yield released, command ended, A5 re-preview) after 20 s. The wait start is held in `pm_bg_pending_slot`, which can never hold a request while a load is in flight (a MIDI request is dropped then, D13; a Menu request is refused). Every exit restores `PM_BG_PENDING_NONE`. Phase 5 (the Bank Load itself) keeps the filesystem's own stall observers. `#include "timebase.h"` added. |
| A9 | **Open** | Hardware verification, below. |

### 13.2 Build

- `make all && make img`: no new warnings (only the newlib syscall stubs, the LTO
  serial note and the existing unused-function warnings in `filesystem.c`).
- `text=594,872`, `data=420`, `bss=430,920`.
- Flash 595,292 / 753,664 B, **headroom 158,372 B** (+528 B text over the
  assessed build).
- DTCM statics 4,472 B; FXBUF margin 3,712 B.
- No RAM change.
- `build/LXRV2_lxr02.img` written.

### 13.3 Hardware tests to run (A9)

1. **§10 rows E1–E9** and the regression rows.
2. **A1/A2:**
   - start a Bank load while the trace shows the scalar drain in a wait phase (2, 12, 14, 22) and in an action phase (3, 13);
   - do the same while the Pattern drain is in phase 7 and phase 4;
   - each must end ERROR (TERMINAL flags 0) within a few ticks of reaching an allowed phase;
   - the Bank load must then open its own files: no wrong-handle read, no hang in phase 40/42.
3. **A3:**
   - on 17: Kit Load (it lands in home), toggle to Save, check the Kit prefill shows 17's captured name, Kit Save from 17 under a new name, exit, reboot with no usable AutoSave record;
   - home keeps its Kit, `.hcnames` home Kit row = the loaded Kit name, no Case 3;
   - resident: Kit Load into Scenes 2+3 while editing Scene 2, then Kit Save from Scene 2: Scene 3's row keeps the loaded name.
4. **A5:** MIDI request for a missing slot; the page ends on the resident Bank's slot with correct LEDs and a working OK.
5. **A8:** force a stalled facade or copy/clear job during phase 2: after 20 s the load ends FAILED, `...` clears, MODE works again.

---

## 14. Bank slot scrolling fix (2026-10-19, user report)

**Report:**
- Scrolling Bank Load/Save slots blocked and lagged again; Scene scrolling was fine.
- The user's rule (now binding in `MEMORY.md`, "Load/Save Slot Scrolling Policy"):
  number → name → LED array, each asynchronous, each disposed in that order by
  the next turn.

**Cause:**
- Every Bank slot change started the child preview, a card scan of up to 16
  children plus PAT4 reads, and the preview held `menu_storageBusy`.
- The encoder gate (`menu_parseEncoder()`) and the pot gate
  (`menu_handleLoadSaveKnobDelta()`) drop every turn while that flag is set, so
  each turn waited for the previous slot's whole scan.
- An older `MEMORY.md` note required this gating ("hold input until its
  destination mask is valid"); it is now corrected.

**Fix:**

| File : anchor | Change |
|---------------|--------|
| `filesystem.c` : `FS_BANK_FLAG_PREVIEW_CANCEL`, `filesystem_cancelScanBankScenes()`, `filesystem_scanBankScenesRunning()` (+ `.h`) | A running preview can be disposed. `filesystem_scanBankScenes_tick()` acts on the request only in phases with no callback outstanding: it closes the open directory or `.pat` through its own close phases, returns to root, and finishes ERROR, so a partial mask is never taken as valid. |
| `filesystem.c` : `filesystem_requestScanBankScenes()`, scan phases 0/5 | The Bank folder name is captured at request time (`op_bank_display_name`), because the shared name cache can now be cleared while the scan runs. |
| `menu.c` : `menu_requestBankChildPreview()`, `menu_bankLoadPreviewComplete()`, `menu_libraryIndexLoadComplete()` | The preview no longer takes or clears `menu_storageBusy`. A request while a scan runs cancels it and defers; the deferred retry serves the latest slot once the facade is free. The LEDs are dark until the preview completes, and the number and name (from the resident index) are immediate. |
| `menu.c` : `menu_requestCurrentLoadSaveSelection()` | Every new selection (slot, type, page) cancels a running preview. |
| `menu.c` : `menu_switchPage()` (leaving Load/Save) | Cancels a running preview. |
| `menu.c` : Bank Save OK | If a preview is running, OK cancels it (no restart) and is consumed; the next OK posts. Bank Load OK stays gated by a valid preview of the shown slot. |
| `menu.c` : `menu_backgroundBankLoadUiPoll()` | A running scan is WAIT (the MIDI path no longer sees `menu_storageBusy` during the preview). |
| `MEMORY.md` | New binding policy section; the two contradicting Load:Bank notes corrected. |

**Build:**
- No new warnings.
- `text=595,136`, `data=420`, `bss=430,920`; flash 595,556 B, headroom 158,108 B.
- No RAM change. Image written.

**Not tested on hardware.**

**Test:**
- Spin the encoder and pot 2 quickly across Bank slots on Load and Save:
  - the number and name follow every detent;
  - the LEDs stay dark while moving, then light for the slot you stop on;
  - no detent is lost.
- Stop on a slot; OK on Load works once the LEDs are lit.
- On Save, OK during a preview needs a second press.
- MODE out of Load/Save mid-scroll.
- Type change mid-scroll.
