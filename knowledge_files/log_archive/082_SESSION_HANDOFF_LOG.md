# Session 082 — Handoff Log

```
DATE: 2026-10-16 / 2026-10-20
SESSION GOAL: Load/Save cleanup ahead of S083 MIDI: Bank Load/Save Replace
              and Merge, Instrument Load `chg` row, fast name editing, and
              the background Bank load through a 17th Scene so a Bank change
              never interrupts playback (needed by MIDI CC0); then fix what
              the hardware tests found and let the user keep working while a
              Bank load runs.
COMPLETED: Four parts, each plan -> schedule -> implementation ->
           assessment (-> fixes):
           P0 Load/Save menu cleanup (S1-S25; phases A1, A2, B1-B3, C0-C10,
              D) plus the first-boot freeze review (F1-F9 applied, F5 open);
           P1 background Bank load / Scene 17 remaining work (D28-D39,
              E1-E9, F) plus assessment fixes A1-A8 and the Bank slot
              scrolling fix;
           P2 audio during a background load (fixes 1-3: Effect source
              rebind, whole-Scene AutoSave range marking, idle-time
              filesystem servicing);
           P3 work while a background load runs (W1-W5, D40-D53; S1-S10)
              plus its assessment.
           MIDI_SPEC.md and S083_P1_MIDI_IMPLEMENTATION_PLAN.md (rev 4) were
           written at the start of the session; S083 is the next session.
VERIFIED ON HARDWARE:
  - P0 first boot: Load/Save froze once while pot 1 scrolled during an
    AutoSave drain; not reproduced (§3.6). A later boot emptied Scene 2 as
    Case 3 after a rapid all-pot spin (F5, open).
  - P1 build: user reported Bank slot scrolling blocked (fixed, §4.7) and,
    on a running Bank load, a volume jump, a slow load and an end-of-load
    glitch (-> P2).
  - P2 build: "ok, that works fine" (user, 2026-10-19). The load is still
    slow, by the user's choice (no more speed-ups).
  - P0/P1 verification matrices were never run row by row. P3 is untested.

CHANGES THIS SESSION (detail in §9):
- Core/Bank/BankData.c/.h: edit-mask merge helpers, 32-bit edit/fan-out
  masks (Scene 17 = bit 16), load availability (unavailable mask), scoped
  revalidation, failed-child strip.
- Core/Bank/Scene/SceneData.c/.h: Scene 17 record and accessors, shared
  empty-Scene primitive.
- Core/Bank/Scene/Pattern/PatternData.c, PatternStackService.c: index 16 ->
  pat_background_region (pat_regionFor()).
- Core/Bank/Scene/Preset/presetManager.c/.h: two-mask Bank Load/Save,
  background Bank load state machine (phases 0-6), Scene 17 build/rebuild,
  result byte, unlocked/locked split, availability end.
- Core/Bank/Scene/Autosave.c/.h, AutosaveTrace.h: range marking; trace
  stages b/h, Q 0x08, stall site 10, CC drop reasons 9/10.
- Core/DSP/Effects/EffectsManager.c/.h: 32-bit fan-out, source rebind,
  availability filter.
- Core/Hardware/SD/filesystem.c/.h, storageTypes.c/.h: Bank outcome table,
  phase 98, Bank Save removal and manifest read, HCNAMES tokens, boot
  reader, content probe + cancel, name capture, saves from 17, AutoSave
  yield/abort allow-list, uncounted ticks, progressive Bank Load.
- Core/Hardware/frontPanel/buttonHandler.c/.h, ledHandler.c/.h: Bank COPY
  tap/hold, latched-surface helper, MIDI-origin entry, panel gates, two-slot
  fast blink.
- Core/Menu/menu.c/.h, CopyClear/*.c/.h: Bank page UI, chg row, pot
  coalescing, Load/Save gates, background-load UI, frozen screen, copy/clear
  gates.
- Core/Sequencer/sequencer.c/.h: Scene 17 switch/move/release, chain gates.
- Core/MIDI/MidiParser.c, MidiMessages.h: drop filter during a load.
- main.c, config.h: idle-time filesystem servicing.
- tools/decode_devlogs.py, tools/verify_bank_autosave.py.

KNOWN ISSUES INTRODUCED: P3 A1 (queued action parts run for a dropped
  target), P3 A2 (frozen-screen blink loss on a second Load/Save press),
  P2-4 switch self-check not built; see §13.
KNOWN ISSUES RESOLVED: runtime Bank Load switched the playing Scene
  (MEMORY.md known bug, closed by S9/D26 + Scene 17); Bank slot scrolling
  blocked by the preview scan; Load/Save pots ignored the storage gate
  (freeze class); refused HCNAMES requests left a reset register; identity
  block shared across Scenes could empty a Scene at boot (P1 A3); decoder
  op table mislabelled 0x2b.

NEXT SESSION RECOMMENDED GOAL: S083 MIDI (S083_P1_MIDI_IMPLEMENTATION_PLAN.md,
  updated with the S082 outcome notes in its §13). Before or alongside it:
  run the S082 hardware checks in §11 (including the S081 interaction rows,
  §11.2 item 7), decide P3 A1/A2, and run the owed S081 checks
  (`081_SESSION_HANDOFF_LOG.md` §13.2).
BLOCKERS: S082 P1/P3 hardware verification (S083 P4/CC0 depends on it).

CRITICAL REMINDERS FOR NEXT SESSION:
- Load/Save Slot Scrolling Policy (MEMORY.md) is binding: number -> name ->
  LEDs, each async; the next turn disposes them; nothing may hold
  menu_storageBusy while scrolling.
- Nothing loads into Scene 17; every load targets the home Scene
  (scene_backgroundHomeIndex()). Scene 17 is never AutoSaved and never in a
  16-bit mask.
- A cached Scene index is a Scene-17 hazard (EffectsManager was the case the
  audit missed). Every new per-block consumer must follow the active Scene.
- Selection, fan-out, copy/clear and LEDs use availability
  (bank_sceneAvailable*()); persistence uses presence. Outside a load they
  are identical.
- Never abort an AsyncFATFS-backed phase that has a callback outstanding
  (filesystem_autosaveDrainAbortSafe() is the allow-list pattern).
- Card preparation after S082: delete /.hcnames, .hcnamtmp, .hcprms1 and
  .hcprms2 (new Scene-row tokens, assignment present mask).
```

---

## 1. Session overview and timeline

| Date | Work | Commit |
|------|------|--------|
| 2026-10-16 | S083 MIDI plan written (rev 1). | `8206a73` |
| 2026-10-16/17 | MIDI plan rounds 2–3; background Bank load split into `S082_P1_LOAD_SAVE.md` (D1–D24). `MIDI_SPEC.md` reference written. | `8b1b647` |
| 2026-10-17/18 | `S082_P0_LOAD_SAVE_MENU_CLEANUP.md` revisions 1–4 (S1–S25), `S082_P0_IMPLEMENTATION.md` schedule. | `8b1b647`, `45846b7` |
| 2026-10-18 | P0 implemented (A1–D); §12 first-boot freeze review; §13 fixes F1–F4, F6–F9 + F5 instrumentation. `S082_P1_LOAD_SAVE.md` audit → D28–D35. | in `b25ffbe` |
| 2026-10-18/19 | `S082_P1_IMPLEMENTATION.md` (R1/R6/R7/R8 decided → D36–D39); E1–E9 implemented; assessment A1–A9; fixes A1–A8; Bank slot scrolling fix (§14). | in `b25ffbe` |
| 2026-10-19 | `S082_P2_LOAD_SAVE_FOLLOWUP.md` (hardware report on a running load), `S082_P2_IMPLEMENTATION.md` fixes 1–3, implemented, assessed. User: "works fine". | in `b25ffbe` |
| 2026-10-19 | `S082_P3_WORK_WHILE_BG_LOAD.md` (W1–W5, D40–D53), `S082_P3_IMPLEMENTATION.md` (S1–S10), implemented, assessed. | `8a83df0` |
| 2026-10-20 | Close-out: this log, index, specs, MEMORY.md, volatile notes, S083 plan notes. | uncommitted |

Branch: `dev-ph6-midi`, from the S081 code commit `2a0fd86` ("s081 record
finished, needs doc and closeout"). Session 081 was closed out after this
session, on 2026-10-21 (`081_SESSION_HANDOFF_LOG.md`, index entry 081).
S082 did not change `LiveRecord.c/.h`; how its Scene 17 and background-load
work meets S081 live record is in §13 item 8.

---

## 2. Part P0 — Load/Save menu cleanup (`S082_P0_LOAD_SAVE_MENU_CLEANUP.md` rev 4)

Five items:
1. Bank Load/Save rework: all 16 Scenes by default, plus a Merge mode on the copy/clear button.
2. Instrument Load `chg` row.
3. The background Bank load (Scene 17), specified in P1.
4. Slow Load/Save endless pots (a display fix only).
5. BAR2 in name editing.

### 2.1 Terms

| Term | Meaning |
|------|---------|
| Present | `bank_scenePresent()` bit set: a resident Scene with state. Every load and copy/clear destination sets it; no edit path writes a non-present Scene. On card: child `NN <name>/` exists. |
| Content | Present **and** at least one active trigger step (`pat_sceneHasActiveSteps()`) — what PERF lights. On card: the same test on the child's PAT4 address array. |
| Selection | The 16-bit SEQ matrix choice on the Bank page; default all 16; deselected = unlit. |
| Replace | Default mode: every Scene is acted on; a deselected Scene loads/saves empty. |
| Merge | Toggled by a COPY tap: only selected Scenes with content are acted on; everything else stays. |
| Played Scene | `seq_activePattern` and every `seq_perTrackPattern[t]`. |
| Source / RAM / Slot view | SEQ LEDs showing the selected Load source Bank, the resident Bank as PERF shows it, or the selected Save slot. |

### 2.2 Decisions (binding)

Round 1 (2026-10-18):

| Id | Decision |
|----|----------|
| S1 | COPY tap toggles Merge; a hold shows the other view and does not toggle. Timing = `BUTTON_TIMEOUT` = `BUTTON_HOLD_DELAY_MS` 200 ms. |
| S2 | All 16 Scenes toggle, unlit ones included, with the `led_flashGroup()` flash. |
| S3 | "Saved empty" = child `NN` removed from the slot. A selected Scene is written exactly as it is (a Kit or partial Kit with no pattern is written). |
| S4 | The object being acted with takes priority: on Load the loading Bank's objects overrule, on Save the resident Bank's. `.hcnames` sources stay accurate. |
| S5 | Merge load while running: Scene 17 only when a played Scene is loaded or emptied; only replaced Scenes lose dirty state; whole-Bank AutoSave re-mark afterwards (P1 D25). |
| S6 | ~11 B new SRAM1 accepted. |

Round 2:

| Id | Decision |
|----|----------|
| S7 | Outside Merge, a deselected Scene loads empty or saves empty. |
| S8 | Replace uses exactly what is there. Merge writes only a selected Scene with content and leaves the destination unchanged otherwise. |
| S9 | **The active Scene never changes because of a load or save.** Stopped: stays. Running and a played Scene replaced: background load from Scene 17. Running and no played Scene replaced: load directly. |
| S10 | The viewed Scene never changes because of a load. |
| S11 | `bankset.bcg` `active_scene` is legacy: Bank Load never applies it; used only at boot without an AutoSave record; Bank Save still writes it. |
| S12 | Voice edit masks under Merge exclude each other (Load and Save): a kept destination Scene removes every replaced Scene from its mask; a source Scene deselected on the matrix is removed from every other Scene's mask. |
| S13 | HCNAMES sources: in a Merge Save deselected Scenes keep their source. The `.hcnames` format may change freely (the user wipes AutoSave/temp files). |
| S14 | Pots: fix the display path only; every pot function stays the same. Only BAR2 changes (move without overwriting). |
| S15 | The 2 B COPY press tick is approved. |
| S16 | Every unanswered recommendation of rounds 1–2 is accepted (below). |

Accepted recommendations (S16):

| Q | Rule |
|---|------|
| Q2 | While COPY is held: OK blocked; encoder slot moves allowed (held view follows); MODE exits normally. |
| Q3 | Merge separate for Load and Save, off at entry, same retention as that page's selection (off after a completed Save). |
| Q5 | No blinks on the Bank page in either view. |
| Q7 | "Pattern data" = active trigger steps only (the PERF rule). |
| Q11 | A Replace Load of an empty Bank, or with nothing selected, is allowed (empties all 16). A Merge with nothing to act on is refused. |
| Q12 | Live PAT4 probe for content; a stored mask only if the probe is too slow. |
| Q13 | Retention resets as in §2.4. |
| Q14 | After a Bank Load the cursor returns to the type row; selection and Merge are kept. |
| Q15 | MIDI CC0 is always Replace with all 16 selected, and the menu shows that state. |
| Q17 | `chg`: lower-row click loads 000; a positive turn loads 000; a negative turn does nothing. |
| Q18 | No `kit` row after a `chg` commit until the nested session ends; going back to the old type shows `chg`. |
| Q19 | Stepping to another type and back without committing brings `kit` back with its snapshot (the invalidation moves to the commit). |
| Q20 | With several destination Scenes, `chg` is refused unless every selected Scene can take the type. |
| Q21 | Labels exactly `mr` (Load Merge) and `MR` (Save Merge, occupied slot). |
| Q32 | BAR2 moves right keeping every character; at the last cell nothing; leaves edit mode as today. |
| Q33 | No pot step-size or acceleration change. |
| Q34 | The display fix covers every Load/Save pot (Load and Save pages, nested Instrument Load/Save). |
| Q30, Q31 | Not adopted (pot 3/4 behaviour unchanged). Q10, Q26 superseded by S9. |

Round 3:

| Id | Decision |
|----|----------|
| S17 | The active Scene is always present. A stopped Replace that empties it leaves it present with default content (editable, AutoSaved, written by a Replace Save as a default child). |
| S18 | View at the Scene 17 switch: with `PAR_FOLLOW` on the view follows playback onto 17 (edits and live record go to 17); off, the view stays. The load itself never moves the view. |
| S19 | Replace and edit masks: Load — loaded Scenes take their source entries; Save — the slot gets the RAM entries. Emptied/removed Scenes reset to self and are removed from every other entry. |
| S20 | Merge mask rule 2 also covers source Scenes skipped for having no content. |
| S21 | New HCNAMES Scene-row tokens `bNNN` (child of Bank NNN at the row's index) and `x` (intentionally empty). Boot treats `x` as empty with no notice. |
| S22 | After any Bank Save, row 0 and `active_bank` = target slot; written Scenes become `-` under it. |
| S23 | A present Scene with only default content is written by a Replace Save as a full default child, not removed. |
| S24 | Merge Save edit masks: written Scenes take their RAM entry minus every Scene not written; the slot's untouched children remove every written Scene (read from the slot's `bankset.bcg`); a missing or malformed manifest = every entry self. |
| S25 | Re-apply only on replaced content: the active Scene runs the activation worker only when its content was replaced. The user's active and viewed Scenes never change because of a load or save, except the automatic move to Scene 17. |

### 2.3 Outcome per Scene n

| Mode | Acted on when | Action | Otherwise |
|------|---------------|--------|-----------|
| Load Replace | selected(n) ∧ source child n present | load child n exactly | resident Scene n emptied |
| Load Merge | selected(n) ∧ source content(n) | load child n | unchanged |
| Save Replace | selected(n) ∧ resident present(n) | write child n exactly | slot child n removed |
| Save Merge | selected(n) ∧ resident content(n) | write child n | slot child n unchanged |

Menu turns this into `act_mask` and `empty_mask` at OK (Merge:
`empty_mask = 0`). The filesystem intersects `act_mask` with the children it
discovers; a child that vanished since the preview is emptied by Replace and
kept by Merge.

### 2.4 LEDs, selection, COPY, labels

- **LEDs:**

  | Page | Normal view | COPY held |
  |------|-------------|-----------|
  | Bank Load | selected ∧ source content | resident present ∧ trigger steps (PERF base) |
  | Bank Save | selected ∧ resident content | Save slot content |

  - There are no blinks; the old active-Scene blink on Bank Load is removed.
  - One shared helper with PERF (`menu_residentContentSceneMask()`).
  - An empty or missing slot shows no LEDs; the matrix is dark while a preview scan runs.
- **Selection:**
  - One retained 16-bit mask (`menu_kitLoadSceneMask`), default `0xFFFF`. Every SEQ press toggles its bit, and all 16 may be deselected.
  - Load resets it on entry, a type change, a page change, a mode change, nested Instrument entry, or a different source slot; **not** after a completed Load.
  - Save resets it on entry, a type, page or mode change, or a completed Save; **not** on a slot move.
  - `menu_resetSaveParameters()` was split into `menu_resetSaveParametersEx(after_command)` so a Bank Load keeps its selection.
- **COPY on the Bank page:** routed by ButtonHandler before copy/clear (`menu_bankCopyPressed/HoldReached/Released/ForceRelease/Armed()`, press tick `bh_bankCopyPressTick`).
  - A press consumes SEQ and OK.
  - A hold at 200 ms peeks at the other view; a tap toggles Merge.
  - MODE or an event overflow force-releases with no toggle. `LED_COPY` mirrors Merge.
  - COPY on other Load/Save types stays refused.
- **Labels:** Load `ok`/`mr`; Save `ok`/`OW`, and in Merge `ok`/`MR`. `...` and the `NN.` progress are unchanged (the progress became 1-based in P3).

### 2.5 Card content preview (B1)

`filesystem_requestScanBankScenes()` now returns a present mask and a
**content mask** (`filesystem_bankChildContentMask()`,
`op_bank_child_content_mask` 2 B). After the existing child iterator
(phase 8 also copies display names into
`op_bank_child_scratch.bank_child_display[]`), probe phases 11–20 visit each
present child one at a time:
1. open the child directory and find its first `.pat`;
2. seek past the header (`PATTERN_FILE_HEADER_BYTES`);
3. read the 1,792-byte address array in 160 B chunks through `op_line_buf`;
4. set the content bit at the first odd byte with `0x80` (bit 15 of the little-endian entry);
5. close, return to the Bank directory, and go to the next child.

No `fs_stage_workspace` or `pat_background_region` is used. It runs on Load
(the source) and on Save (the slot view). Trace stage `b` reports both masks,
and since F8 also the elapsed time in 64 ms units.

### 2.6 Bank-level data (B3)

**Requests.** `preset_loadBank(slot, act, empty, flags)` and
`preset_saveBank(slot, act, empty)`.
`filesystem_requestLoadBank(slot, act, empty, flags, cb)` has five
arguments. Flags:

| Flag | Value | Meaning |
|------|------:|---------|
| `FS_BANK_FLAG_BOOT` | 0x01 | Apply the manifest `active_scene` (boot only) |
| `FS_BANK_FLAG_REPLACE` | 0x02 | Missing requested children are emptied. `PRESET_BANK_LOAD_REPLACE` passes it explicitly because an all-16 Replace has `empty = 0`. |
| `FS_BANK_FLAG_AUTOSAVE_YIELD` | 0x04 | Reserved for the yield |
| `FS_BANK_FLAG_PREVIEW_CANCEL` | 0x08 | P1 §14 preview cancel |
| `FS_BANK_FLAG_PROGRESSIVE` | 0x10 | P3 progressive Bank Load |

The Preset values equal the FS values (`_Static_assert`s in `filesystem.c`).

**Bank Load:**
- **Phase 17:** Replace empties missing children; a BOOT-gated active-Scene re-pick; route to phase 98.
- **New phase 98:** `filesystem_bankEmptyScenes()` resets every empty-mask Scene through the shared primitive `scene_resetResidentToEmpty()`:
  - settings, Effect and Kit defaults;
  - Pattern region init;
  - present bit off, except the active Scene (S17);
  - HCNAMES rows staged as `x` + blank inheriting child rows, refreshed (`filesystem_stageEmptySceneRows()`).
  - It runs under `filesystem_patternServiceReady/Finish()`.

  `filesystem_bootReaderEmptyScene()` reuses the same helpers.
- **Metadata commit after the last child:**
  - the old row-0 source is captured first;
  - kept Scenes whose Scene row inherits get `b<old Bank>` (`filesystem_stageKeptSceneSources()`);
  - present mask **assigned**: `(old & ~(load|empty)) | (load & ~failed)`, plus the active Scene;
  - `bank_mergeVoiceEditMasksAfterLoad()`;
  - the active Scene, playback and view are realigned only for BOOT.

  The empty-Bank branches do the same.
- **Menu completion** (`PRESET_OP_BANK_LOAD`):
  - the runtime empty-Bank fallback ladder is removed;
  - `bank_revalidateVoiceEditMasks()` runs after every load;
  - a Merge triggers `autosave_markResidentBankDirty()`;
  - the active Scene is re-applied only if it was replaced (S25).

  `filesystem_lastBankLoadEmptyMask()` feeds it.

**Bank Save:**
- **Request:** `filesystem_requestSaveBank(..., save_mask, empty_mask, ...)`.
  - Written entries are `(ram & W) | self`, removed entries are self.
  - The manifest `active_scene` is the RAM active Scene (the home Scene on 17, D20), or the first written child if the active Scene is removed.
- **Phases 61–64** (a Merge with untouched children and an existing directory):
  - read the slot's old `bankset.bcg` with `storage_banksetParseLineMasked(state, line, protect = W|E)`, which skips protected entries and `active_scene`;
  - each untouched entry becomes `bank_mergeVoiceEditEntry(old_or_self, W|E, n)`;
  - a missing or malformed manifest gives self.
- **Per-child loop:** visits written | removed children. A removed child runs only the exact-object delete (phases 20–21).
- **Phase 45 rewritten:**
  - removed: the present-mask OR, the active-Scene set, and the per-Scene mask restore (Save never changes RAM);
  - added: row 0 / `active_bank` = target; written Scene rows `-`; resident Scenes not written keep explicit provenance through `stageKeptSceneSources`; non-present removed Scenes `x`; `bank_invalidateSdCleanScene()` per removed child.

**Edit-mask helpers (BankData):**
- `bank_mergeVoiceEditEntry(entry, keep, n)` = `(entry & keep) | bit(n)`.
- `bank_mergeVoiceEditMasksAfterLoad(src, seen, loaded, emptied)`:
  - loaded n: `(seen ? src[n] : bit) & loaded | bit`;
  - emptied n: bit;
  - kept n: `ram[n] & ~(loaded|emptied) | bit`.

**HCNAMES source tokens** (S21; no compatibility):

| Token | Register value | Meaning |
|-------|----------------|---------|
| `bNNN` | `FS_RESIDENT_SOURCE_BANK_CHILD_BASE` 0x1000 + NNN (to `…_END` 0x13E8) | This Scene came from child NN (the row's own index) of root Bank NNN, not the Bank in row 0. |
| `x` | `FS_RESIDENT_SOURCE_EMPTY` 0x1ffb | Intentionally emptied Scene; boot treats it as empty with no notice. |

- **Placement:**
  - valid only on Scene rows 1..16;
  - inside the 13-bit value field, above the direct slots 0..999 and below the specials 0x1ffc..0x1fff;
  - the REFRESHED 0x2000 and DIRTY 0x8000 flags are untouched.
- **Code:** `filesystem_residentSourceValid/parseResidentSourceToken/formatResidentNameLine/resolveResidentSource()`. The resolver treats both tokens as terminal.
- **Boot reader:**
  - EMPTY: reset without the Case-3 notice;
  - BANK_CHILD: `filesystem_bootReaderEnterSceneFolder()` enters `Bank/NNN <name>/NN <scene>/` through the boot Bank index (unresolvable → Case 3).
- **HCNAMES-authoritative boot loader:** extends the effective present mask from Scene rows (`b` sets the bit, `x` clears it except for the active Scene).

**Operation table:**

| Operation | Row 0 | Loaded/written | Kept / not written | Emptied |
|-----------|-------|----------------|--------------------|---------|
| Replace Load | source slot | `-` | — | `x`, child rows blank |
| Merge Load | source slot | `-` | `-` under the old row 0 → `b<old>`; direct/`@`/`?`/`bNNN` unchanged | — |
| Replace Save | target | `-` | removed Scenes that hold RAM content: explicit as above | non-present: `x` |
| Merge Save | target | `-` | unchanged; made explicit if `-` under the old row 0 | — |

### 2.7 Name editing (A1, items 4–5)

- **Diagnosis:** every Load/Save pot delta called `menu_repaintAll()`, a 32-character frame, about 70 ms to drain on the TIM7 nibble machine, so frames were skipped.
- **Fix:**
  - pots 2–4 set `menu_knobs_dirty`;
  - `menu_serviceKnobRepaint()` routes LOAD/SAVE pages through `menu_repaint()` (changed characters only, at most every 20 ms);
  - `menu_repaintAll()` stays only for layout changes (page/type entry, command start/finish).
- **BAR2** (`menu_loadSaveBarButtonPressed()`) moves right without blanking and is inert at the last cell; BAR1 is unchanged.

### 2.8 Instrument Load `chg` (A2)

- **State:** new source `MENU_INSTRUMENT_SOURCE_CHANGE`. `menu_instrumentLoadStepType()` enters it when the selected normal type differs from the resident type.
- **Row:**
  - shows `chg` in cells 1..3 and row 000's name (`filesystem_copyInstrumentDisplayName(type, 0)`) once the typed `.hcindex` is resident, blank during the handoff;
  - no `.hctmp` save is posted;
  - a click or a positive turn commits row 000 (`menu_instrumentLoadCommitChange()`); a negative turn does nothing.
- **After the commit:**
  - the source is POOL at 000;
  - no `kit` row until the nested session ends (`menu_instrumentLoadKitRowAvailable()`);
  - the old type shows `chg`;
  - the `kit` snapshot is invalidated at commit, not at the type step;
  - S054 owed-restore unchanged; the type-change finalisation lives in `menu_finishInstrumentApplySession()`.
- **Multi-Scene:** `menu_instrumentTypeSelectableForMask()` hides a type that some selected Scene cannot take.
- **InstrumentMrp** never shows `chg`.

### 2.9 Implementation build trail (P0)

| Phase | Flash used | Notes |
|-------|-----------:|-------|
| A1 | 573,688 | headroom 179,976 |
| A2 | 574,224 | |
| B1 | 575,248 | |
| B2 | 575,840 | |
| B3 | 578,464 | |
| C0–C4 | 577,392–577,992 | |
| C10 + post-C10 | 589,816 | headroom 163,848 |
| §13 fixes | 590,944 | `text=590,528 data=416 bss=430,816`; headroom 162,720 |

Post-C10 verification fixes:
- `PRESET_BANK_LOAD_REPLACE`;
- the HCNAMES-authoritative boot loader handles EMPTY/BANK_CHILD and extends the present mask;
- Bank Save phase 45 keeps present removed Scenes in the kept set;
- phase 98's empty-Bank path stages kept provenance;
- `verify_bank_autosave.py` accepts `x` rows and `bNNN`, and checks the S082 present rules.

Phase D docs updated `FILESYSTEM_SPEC.md`, `AUTOSAVE.md`,
`MODULE_INTERCHANGE_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`,
`COPYCLEAR_UTILITIES.md`, `PATTERN_DYNAMIC_STACK.md`,
`STORAGE_SRAM_MANIFEST.md`, `MEMORY.md` and both tools.

### 2.10 P0 risks and design choices worth keeping

| Id | Risk / choice | Status |
|----|---------------|--------|
| R1 | The content probe adds up to 16 child visits and PAT4 opens per slot move. | Accepted; the preview is cancellable (P1 §14) and timed in `b` (F8). Fallback, if ever needed: a stored `scene_mask_content` in `bankset.bcg`. |
| R2/R3 | Replace is destructive by default; a Replace Save writes 16 children plus removals and is non-atomic (a power-fail leaves a mixed Bank). | By request; `OW` remains the Save warning; S058 clean skip. |
| R4 | A Merge skips Kit-only Scenes (no trigger steps). | By request. |
| R5 | COPY is shared with copy/clear and live erase. | Routed before the copy/clear entry points; `bh_clearHeld` not set on the Bank path. |
| R8 | Present-mask assignment changes the AutoSave OR rule. | Load only; `verify_bank_autosave.py` updated. |
| R9 | New HCNAMES tokens reach every reader (boot authoritative path, winner reader, AutoSave source fields, both tools). | All updated in B3/post-C10. |
| R12 | Removing the active/viewed realign at the Bank Load commit must not bring back the S054 "Scene Load never loads the pattern" bug: `seq_activePattern`, `menu_shownPattern` and `scene_getActiveIndex()` must already agree before the load and stay agreed. | Rule kept; retest the S055 round trip (PERF switch, then Scene Load). |
| R13 | Merge Save with a missing or malformed slot manifest. | Every untouched entry = self. |
| R14 | Pot coalescing shares the knob repaint service with other repaint owners (child progress, browser callbacks, `...`). | `menu_repaint()` only; the existing guards stay. |
| A1 | Content source. | Live PAT4 probe (not a stored mask). |
| A2 | Who computes the masks. | Menu, from a coordinate-checked preview; the filesystem intersects with discovery. |
| A3 | Where Save removal runs. | Inside the per-child loop. |
| A4 | Owner of the empty-Scene primitive. | SceneData (`scene_resetResidentToEmpty()`) + filesystem staging (`filesystem_bankEmptyScenes()`); the boot reader reuses it. |
| A8 | Encoding of `bNNN`/`x`. | A dedicated range above 999, below the 0x1ffc specials, clear of the flag bits. |
| A10 | Where the edit-mask merge lives. | One pure BankData helper family used by Load and Save. |
| A11 | Is `active_scene` still needed? | Kept, boot-only. |

---

## 3. Part P0 Phase C — Scene 17 (first implementation) and the first-boot review

### 3.1 Scene 17 storage and mapping (C0–C2)

- **Storage:**
  - `SCENE_BACKGROUND_INDEX` 16, `SCENE_SLOT_TOTAL` 17;
  - `scene_background` (`scene_t`, 1,656 B), `scene_backgroundFlags` (exists) and `scene_backgroundHome` (the Scene active at the build), 2 B.
- **Accessors:**
  - `scene_get()`, `scene_getConst()` and `scene_selectActive()` accept 16 while it exists; `scene_indexValid()` stays 0..15 (storage, AutoSave, HCNAMES, masks).
  - `scene_backgroundExists()`, `scene_backgroundHomeIndex()` (active < 16 ? active : home), `scene_playableIndexValid()`, `scene_backgroundBegin()`, `scene_backgroundRelease()`.
- **PatternData:**
  - `pat_regionFor()` maps 16 → `pat_background_region` while 17 exists; every Scene-indexed `pat_*` goes through it;
  - `pat_snapshotScene()` and `pat_initScene()` stay resident-only; Scene 17 resets with `pat_rawRegionReset(16)` (`pat_initRegion()` factored out).
- **PatternStackService:** `pat_patternValid()` replaces `scene_indexValid()` at six sites. `patSvc_prepareSceneReplace()` stays resident-only: no load writes 17.

### 3.2 Scene-index audit (C3, `S082_P1` §5)

Appendix A of `S082_P0_IMPLEMENTATION.md` listed every line reading
`scene_getActiveIndex()`, `seq_activePattern`, `seq_perTrackPattern[`,
`seq_getTrackPlayedScene(`, `preset_getSlotPlayedScene(`,
`menu_getViewedPattern()`, `menu_shownPattern` or `liveRec_target`
(~200 rows). Rule legend:

| Rule | Meaning | Action |
|------|---------|--------|
| A | Reads/writes through `scene_get*`/`pat_*`/`scene_instrumentSlot*` | none after C0/C1 |
| P | Playback/sequencer/LiveRecord/mixer coordinate | none; confirm no 16-sized array |
| F | Edit fan-out over a 16-bit mask | C4 |
| R | Resident-only sink (HCNAMES row, AutoSave payload, Bank masks) | bounds-checked already (`filesystem_residentSceneRow()` sentinel, `autosave_scenePayloadBase()` refuses ≥16, `bank_sceneBit()` → 0) |
| B | `1u << index` into a 16-bit mask | per site: a no-op, or the home Scene |
| L | SEQ LED | none lit (D8) |
| V | View mirror | follows only via the switch with FOLLOW |
| M | MIDI PC/channel | no PC for 17 |

Decided B/R sites:
- Kit Load masks (`preset_loadDrumset()`, `filesystem_requestLoad()`) use the home Scene;
- `menu_residentPresentSceneMask()` fallback uses the home Scene;
- `menu_resetLoadSaveSceneSelection()` defaults to the home Scene (but `menu_loadSaveSourceScene` stays 16 so Save from 17 works);
- the `menu_switchPage()` Load default uses the home Scene;
- `ccCopy_runKit()` into 17 sets no present bit or AutoSave mark;
- Bank Save's manifest `active_scene` uses the home Scene;
- Scene Save from 17 publishes no resident HCNAMES row.

**Rule added after P2:** a module that **caches** a Scene index (EffectsManager)
is a separate audit class; see §5.

### 3.3 Fan-out (C4)

- `bank_editTargetMask()` (`uint32_t`) is `1ul << 16` while 17 is active, else the active entry.
- `bank_sceneFanoutMask32()`: 16 → `1ul << 16`.
- Every Menu commit loop (Kit cells, Instrument cells, voice mix, bus compressor, `PAR_MORPH`, voice Morph), `effects_fanoutMask()` and its consumers, and the copyOps/clearOps loops went to 32-bit masks over `SCENE_SLOT_TOTAL`.
- LEDs stay 16-bit (no Scene lit for 17).

### 3.4 Build, switch, request, release, gates (C5–C10)

- **Build:** `preset_buildBackgroundScene()` (rebuild flag; later + `replaced_mask`).
  - Fresh build: copy the active `scene_t` (settings, Effect record, FX sequence, bus compressor, Morph), clear the chain fields (D9b), `scene_backgroundBegin(active)`.
  - Fast path with no per-track overrides: whole-region copy.
  - Per-track path: reset region 16; for T1..T7, sum the step block bytes, and if they fit copy each block (`pat_rawReadBlock` → `pat_rawPlace`, trigger-only steps via `pat_rawPublishEmpty(ON)`), the track parameters (length, scale, shuffle, run mode, rotation, loop exclude, MIDI channel/note, track Morph endpoints) and the voice slot data (Kit instrument Normal/Morph/interp, voice Morph, audio out, FX send Normal/Morph, fader).
  - T6/T7 share slot 6, taken from T6's Scene when T6 fits. A non-fitting track is left empty.
  - Stack: one `PAT_RAW_BLOCK_MAX` (136 B) buffer.
- **Switch:** `seq_switchToBackgroundScene()`. Under PRIMASK it sets active/pending/all per-track to 16, `seq_perTrackActive = 0`, and re-snapshots the looper; then it clears pending Scene changes and refreshes the effective track cache. No realign, roll/condition/epoch reset, PC, note-off or LED notify. Then `scene_selectActive(16)`, `menu_setPlayedPattern(16)`, view only with FOLLOW, and the PERF LEDs refresh with none lit.
- **Release:** `seq_selectActivePattern(p < 16)` calls `scene_backgroundRelease()` after publishing. Per-track changes never release.
- **Gates (C9):**
  - AutoSave scalar and both Pattern scheduler calls gated on `!scene_backgroundExists()` (`settings.cfg` not gated, D24);
  - `seq_sendProgChg()` refuses ≥ 16;
  - `seq_chainResolveNext()` refuses cur ≥ 16;
  - an overlapping paste while 17 exists → `CC_RUN_DROP`, reason `SCENE17` (9);
  - no LCD screen prints the active Scene number, so D22 (`--`) needed no site.
- **Request (C7):** `preset_requestBackgroundBankLoad()` / `preset_tickBackgroundBankLoad()` (ticked first in `menu_pollPresetStatus()`), with `pm_bg_phase` (1 B) and `ccSvc_dropQueued()`. `fs_autosave_yield` (1 B) + `filesystem_setAutosaveYield()`. `menu_backgroundBankLoadAbort()`; the Menu Bank OK posts through the request.
- **UI (C10):** `menu_backgroundBankLoadUiBegin()`; `menu_loadSaveEnterTopAt(page, what, slot)`.

### 3.5 First-boot freeze report and review (`S082_P0_IMPLEMENTATION.md` §12)

**Report.** On the first boot of the S082 image the user turned pot 1 to
scroll the Load/Save type; the screen froze until a power cycle. It was not
reproduced.

**The trace** (`SD_CARD_082_P0/asavetrc.bin`) showed:
- the only session where Load/Save was entered while an AutoSave drain owned the facade;
- the drain finished DONE, then the Bank preview completed (#051480);
- then no further record (the freeze records were lost at reset).

**Assessed mechanism** (probable, not proven):
1. All Load/Save pots bypassed the storage gate the encoder applies, so pot 1 could change page, type or nested mode and clear the name cache while an index load, HCNAMES read, `.hctmp` save or preview owned the facade.
2. Refusal paths overwrote another owner's `menu_storageBusy`.
3. Deferred retries went to the top-level type even for nested Instrument entries.
4. The new probe had unbounded waits (EOF read, open refusal) and no stall observer.

Findings and fixes (§13):

| Id | Finding | Fix |
|----|---------|-----|
| F1 (High) | Phase 3 always built Scene 17, even stopped or for a Merge keeping every played Scene. | `PM_BG_NEEDED`: not needed → phase 4 directly; rebuild only when 17 exists. |
| F2 | Load/Save pots bypassed the storage gate. | `menu_handleLoadSaveKnobDelta()` obeys the encoder gate; pot 2 on a Kit/KitMrp/Instrument Load number allowed. DEV `h` 0x06 witness. |
| F3 | Refused requests forced `menu_storageBusy = 0`. | Restore the prior value (`menu_requestLibraryIndexLoad()`, `…KitEntryNames()`, `…ResidentNameScratch()`). |
| F4 | Preview probe unbounded waits. | Case 18 zero-byte read at EOF/not ready closes with no content bit; stall observer (site 10, `BkPv`, 20,000 polls) closes handles and finishes ERROR. |
| F5 | Scene 2 emptied as Case 3 at boot after a rapid all-pot spin (Kit row 18 "Barf", direct 000, failed although `Kit/000 Barf` exists). | **Open.** Instrumentation only (§3.6). |
| F6 | Nested Instrument deferrals re-posted as top-level. | Re-posted through `menu_requestInstrumentEntryNames()`. |
| F7 | Replace inferred from masks (an all-16 Merge became Replace). | `PRESET_BANK_REQUEST_MERGE` 0x80 ORed into `origin`. |
| F8 | Preview trace had no duration. | `b` flags = elapsed / 64 ms, saturated. |
| F9 | `filesystem_prepareResidentNamesCache()` (turns non-dirty sources UNKNOWN, keeps R) ran before `filesystem_start()` acceptance, so a refused start left a reset register. | Reset only after acceptance, in all seven HCNAMES request functions. **Binding ordering rule.** |

### 3.6 F5 evidence instrumentation (DEV only)

New stage **`h`** (`AUTOSAVE_TRACE_STAGE_HCNAMES_PROVENANCE`):

| Code | Producer | When |
|------|----------|------|
| 0x01 REFRESHED_RESET | `filesystem_prepareResidentNamesCache()` | refreshed non-dirty rows about to become UNKNOWN |
| 0x02 WRITE_UNKNOWN_REFRESHED | `filesystem_formatResidentNameLine()` | a present row written as `?` + R (Case-3 precursor) |
| 0x03 KIT_SOURCE_STAGED | `filesystem_setResidentSource()` | Kit row 17..32 source staged |
| 0x04 REFRESH_ON_UNKNOWN | `filesystem_setResidentRefreshed()` | R raised on an UNKNOWN row |
| 0x05 KIT_LOAD_CONTEXT | Menu Kit Load completion | completed after Menu left Load:[Kit] |
| 0x06 POT_DROPPED | `menu_handleLoadSaveKnobDelta()` | first dropped pot delta of a burst |

`Q` 0x08 Case-3 detail follows every Case-3 record:
- **value:** the raw register word | reason << 16 | sub << 24;
- **reasons (`FS_BOOT_FAIL()`):**
  - 1 unresolvable source;
  - 2 folder not entered;
  - 3 `sceneset.scg` not opened;
  - 4 sceneset parse;
  - 5 kitset parse;
  - 6 Instrument member (sub = slot);
  - 7 Scene storage missing;
  - 9 other loader (sub = row).

The old Case-3 "source" field was always zero.

**Probable F5 mechanism:**
- a register image **reset but not fully re-read** before a writer persisted it;
- route (a), a refused request after its reset, is fixed by F9;
- route (b), pot-1 type changes under in-flight reads, `.hctmp` saves and Kit Loads, is gated by F2/F3;
- an AutoSave post-drain publication from a partially restored image is not excluded.

**Capture protocol:**
1. spin the Kit page with Scene 2 active;
2. exit Load/Save and wait for the drain;
3. copy the card **before** reboot, then reboot and copy again;
4. look for `h` 0x02/0x01/0x04 on rows 17..32 and `Q` 0x08.

DEV storage: `menu_loadSavePotDropBurst` 1 B, `op_bank_preview_start_tick`
2 B, `fs_boot_fail_reason/_sub` 2 B, preview stall observer 5 B; no
production RAM.

---

## 4. Part P1 — background Bank load and Scene 17 (`S082_P1_LOAD_SAVE.md`)

### 4.1 Goal

A Bank load happens **immediately**, without stopping playback. What is
playing keeps sounding exactly the same while Scenes 1–16 are replaced. Every
other data operation on the outgoing Bank (AutoSave, Pattern AutoSave,
copy/clear) gives way. The same flow serves the Menu and MIDI CC0 (S083).

### 4.2 Decisions D1–D39 (binding; later amendments noted)

| Id | Decision |
|----|----------|
| D1 | Immediate. AutoSave (scalar and Pattern) is dropped; pending dirty state of the outgoing Bank is discarded, not flushed (Replace; Merge keeps it, D25). |
| D2 | Copy what is playing into Scene 17: active Scene + playing tracks, honouring per-track playback. |
| D3 | When it does not fit: whole tracks in T1→T7 order; T6 and T7 separate (T6 kept if it fits when T7 does not). |
| D4 | Sound identical when all tracks fit. |
| D5 | Switch instantly and seamlessly after the copy. |
| D6 | Then load (amended by D25: the P0 outcome table, not always all 16). |
| D7 | UI: MIDI takes the UI to Load:[Bank] at the slot as if OK; afterwards the UI unlocks on Load/Save and stays there. *Amended by P3 D40: after phase 5 the user may leave; "stays there" applies only when on Load/Save at completion.* |
| D8 | PERF shows no Scene selected while 17 is active. |
| D9 | 17 is a normal Scene except (a) once left it can never be selected again, (b) no chain (chain fields cleared at the copy). |
| D10 | Released only by a whole-Scene change (SEQ press, MIDI PC, queued change); per-track moves never release it. |
| D11 | While 17 exists: no AutoSave, no overlapping copy/paste (*P3: dropped silently at run time; menus not locked*). |
| D12 | Copy/clear in progress at the request: drop queued, let the running job complete. |
| D13 | MIDI PC and Bank Change during a load are dropped. |
| D14 | Stopped at the request: no Scene 17 (amended by D26: the manifest `active_scene` is not applied). |
| D15 | 17 is editable, never AutoSaved, Scene-Saveable, a copy source; its edit mask is itself only; LCD names = the identity captured at the copy (extended by D29). |
| D16 | Live record follows playback into 17 seamlessly (D35); looper anchors kept; pending direct/queued change cancelled. |
| D17 | Dirty marks for Scenes 1–16 keep accumulating while 17 exists; written after the release; 17 never marked. |
| D18 | 17 can be a copy/clear destination (non-overlapping). Nothing loads into 17 (D36). |
| D19 | Bank load while 17 is active: rebuild in place (scope narrowed by D31). |
| D20 | Bank Save on 17 allowed: Scenes 1–16 with the home Scene as `active_scene`; 17 is never saved as active. |
| D21 | Power-off on 17: boot loads the new Bank at its stored `active_scene`; the old Bank's AutoSave records are ignored (identity mismatch). |
| D22 | Where a Scene number is shown, 17 shows `--` (no current screen prints one). |
| D23 | An overlapping paste refused on 17 uses the copy/clear refusal display (*P3: silent drop*). |
| D24 | `settings.cfg` keeps writing on 17 (`active_bank`, globals, S083 Learn lines). |
| D25 | Merge load: P0 outcome table; Scene 17 only when a played Scene is loaded or emptied; only replaced Scenes lose dirty state; whole-Bank re-mark after a Merge. |
| D26 | A load or save never changes the active or viewed Scene. Stopped: active stays (manifest not applied). Running with a played Scene replaced: Scene 17. Running otherwise: direct. `active_scene` legacy (boot only). |
| D27 | View at the switch follows onto 17 only with `PAR_FOLLOW` on. The active Scene is re-applied only when its content was replaced; an emptied active Scene stays present. |
| D28 | No return to 17: a track moved off cannot be put back; the view cannot return. |
| D29 | Scene 17 keeps all ten captured names (Scene, Kit, 6 Instruments, Pattern, Effect; 90 B). Scene/Kit/KitMrp/Instrument/Pattern Save from 17 are allowed and prefill from the capture; name reads for 16 use the capture, never HCNAMES. |
| D30 | Rebuild: a track that does not fit is left **empty** in 17; its old 17 blocks are freed before the fit check. |
| D31 | Rebuild scope: copy into 17 only tracks whose played resident Scene the load replaces/empties; others stay; if none, load directly and 17 is unchanged. |
| D32 | A MIDI-origin load may abandon any Menu state, including an unsaved name edit. |
| D33 | While a load runs, MIDI other than realtime, note and CC is dropped; a Bank Change too. When something else is busy, a MIDI Bank request is **deferred** (one pending, latest wins, 2 B). |
| D34 | The load keeps a result (none/done/failed); S083 sends CC0 TX from it (1 B). |
| D35 | Live record carries on into 17 with no reset. |
| D36 | Nothing loads into 17: every load (Kit, KitMrp, Scene, Pattern, Instrument, InstrumentMrp, the `kit` row) goes to the **home Scene**, the SEQ the Load page shows. Saves from 17 and pastes into 17 stay allowed. |
| D37 | AutoSave dropped at once: an in-flight drain is aborted (torn, stale record); only an HCNAMES publication already under way finishes. |
| D38 | Panel input during a load is dropped, not queued (MODE, SHIFT+REC, VOICE entry, page switches). *Amended by P3: holds for phases 1–4 and 6 and a stopped load.* |
| D39 | A MIDI load closes everything (SHIFT+REC without restoring its page, chain editor, COPY/CLEAR, looper, nested Instrument, name editing) and shows the Bank Load screen as if entered by hand. |

Rules from the P1 deep dive:
- **R1:** = D36.
- **R2:** a successful normal save from 17 renames the matching captured name; KitMrp and InstrumentMrp saves are identity-neutral.
- **R3:** the capture copies from the in-RAM HCNAMES mirror (no card read); an invalid mirror gives `Empty`.
- **R4:** the drop filter does not affect MIDI thru.
- **R5:** a Menu Bank OK discards a pending MIDI request.
- **R6:** = D37.
- **R7:** = D38.
- **R8:** = D39.

### 4.3 State machine (as built, `presetManager.c`)

`pm_bg_phase` (1 B):
- low bits = phase (mask 0x07 since P3);
- `PM_BG_UNLOCKED` 0x08 (P3), `PM_BG_ORIGIN_MIDI` 0x10, `PM_BG_REPLACE` 0x20, `PM_BG_NEEDED` 0x40, `PM_BG_DISCARDED` 0x80.

The slot is in `pm_request_slot`, the act mask in `pm_kit_request_scene_mask`, and empty = Replace ? ~act : 0.

| Phase | Action |
|------:|--------|
| 0 | Idle; a pending MIDI slot (`pm_bg_pending_slot`, 0xFFFF none) starts when Preset, the facade and the Menu are idle (`menu_backgroundBankLoadUiIdle()`): `menu_backgroundBankLoadUiBegin(slot)` then phase 6. |
| 1 | Decide Scene 17 (`preset_backgroundScene17Needed(act|empty)`: transport running and the active Scene or a track's played resident Scene is replaced, via `preset_backgroundCandidateTracks()`) → phase 2. |
| 2 | First pass: `ccSvc_dropQueued()`, `filesystem_setAutosaveYield(1)`. Wait for `!ccSvc_busy()`, an idle facade and `!filesystem_patternSnapshotInUse()`. Then Replace → `autosave_discardDirtyMask()` (once), and go to 3 (needed) or 4. |
| 3 | `preset_buildBackgroundScene(rebuild = scene_backgroundExists(), replaced)`. Fresh build: `seq_switchToBackgroundScene()`, `scene_selectActive(16)`, `effects_rebindSourceScene(16)` (P2), played/view (FOLLOW). Rebuild: `seq_moveTracksToBackgroundScene(candidates)`, view never moved. Then `menu_backgroundNamesChanged()` and the PERF LEDs refresh. |
| 4 | `preset_loadBank(slot, act, empty, REPLACE? | PROGRESSIVE?)`. Refused while busy → retry; refused on an idle facade → FAILED. P3: on acceptance set `PM_BG_UNLOCKED` if progressive. |
| 5 | Wait for `pm_status == PRESET_IDLE` → `preset_bgEnd(0)`. |
| 6 | MIDI origin: `menu_backgroundBankLoadUiPoll()` WAIT / READY (Replace, all-16 masks) / REFUSED (missing slot, preview failed) → phase 1 or FAILED. |

- **A8 bounded waits:** phases 2, 4 and 6 end FAILED after `PM_BG_WAIT_LIMIT_MS` 20 s. The wait start is kept in `pm_bg_pending_slot`, which never holds a request while a load is in flight.
- **Helpers:**
  - `preset_bgSetPhase()` (stamps waits);
  - `preset_bgWaitExpired()`;
  - `preset_bgEnd(failed)`: yield off, phase 0, pending NONE, (P3) `bank_endLoadAvailability()`, and on failure the result plus `menu_backgroundBankLoadAbort()`.
- **Result:** `pm_bg_result` is set in `on_bank_load_complete()` when phase == 5:
  - `PRESET_BG_RESULT_DONE` 1 / `FAILED` 2, code mask 3, | `PRESET_BG_RESULT_MIDI` 0x80;
  - `preset_takeBackgroundBankLoadResult()` reads and clears it;
  - child failures inside a DONE load still count as DONE.
- **Request:** `preset_requestBackgroundBankLoad(slot, act, empty, origin)`.
  - `origin` = `PRESET_BANK_ORIGIN_MENU` 0 / `MIDI` 1, `| PRESET_BANK_REQUEST_MERGE`.
  - MIDI: slot < max → pending (latest wins).
  - Menu: refused if Preset is busy or the slot is missing (`filesystem_bankSlotExists()`), clears a pending MIDI request (R5).
  - Refused/dropped while any phase ≠ 0.
- **Queries:**
  - `preset_backgroundBankLoadRunning()` = phase ≠ 0;
  - P3: `preset_backgroundBankLoadUnlocked()` = bit 3; `preset_backgroundBankLoadLocked()` = running && !unlocked.

### 4.4 P1 implementation (E1–E9)

- **E1 name capture.** Rows `FS_BACKGROUND_NAME_SCENE`, `_KIT`, `_INSTRUMENT_0..5`, `_PATTERN`, `_EFFECT` (10 × 9 B, `fs_background_name`, `_Static_assert` 90 B).
  - Writers: `filesystem_backgroundNamesCaptureScene(src)` (fresh build), `filesystem_backgroundNameCaptureInstrument(dst_slot, src_scene, src_slot)` (per copied slot), `filesystem_backgroundNameSet(row, name)` (after a save from 17).
  - `filesystem_residentInstrument/Kit/Scene/PatternName()` and `filesystem_sceneComponentNameRaw()` read the capture for index 16.
  - The Pattern child and the Scene Save writer name children from it.
- **E2 Menu name paths for 16:**
  - `menu_requestResidentNameScratch()`, `menu_requestKitEntryNames()`, `menu_requestInstrumentEntryNames()`, `menu_requestSceneEntryName()` (+ factored `menu_requestSceneIndexAfterName()`);
  - `menu_backgroundNamesChanged()` drops scratch keyed to 16 after every build or rebuild.
- **E3 saves from 17:** Kit Save (case 21), Instrument Save, Scene Save (no HCNAMES rows; the capture is renamed), Pattern Save. The capture is renamed after a success (R2).
- **E4 rebuild scope and fit:**
  - `preset_backgroundCandidateTracks()`;
  - `preset_backgroundScene17Needed()` rewritten (it no longer always rebuilds while 17 is active);
  - `preset_clearBackgroundTrack()` (publish-empty, frees blocks before the fit, D30);
  - `preset_copyBackgroundSlot()` captures the slot name;
  - `seq_moveTracksToBackgroundScene(mask)` (PRIMASK, `seq_recomputePerTrackActive()`, looper re-snapshot, clear pending, refresh).
- **E5 request/tick:** phases 0 and 6; slot refusal; the phase 4 retry; the uniform Replace discard; the result byte.
- **E6 MIDI-origin entry:**
  - `buttonHandler_closeLatchedSurfaces(restore_page, leaving_perf)` is shared with MODE;
  - `buttonHandler_enterLoadSaveForBackgroundLoad()`: close everything without restoring SHIFT+REC, end the copy/clear session, `dblclick_cancel()`, leave Morph voice mode, Load/Save mode + LEDs, BAR LEDs off;
  - `menu_backgroundBankLoadUiBegin()` rewritten; `menu_backgroundBankLoadUiIdle()`, `menu_backgroundBankLoadUiPoll()`;
  - R7 drops at MODE, SHIFT+REC, VOICE and `menu_switchPage()`; the Bank OK slot check.
- **E7 MIDI drop filter:** `midiParser_acceptedDuringBankLoad()`, after thru routing in `midiParser_parseMidiMessage()`.
  - Accepts realtime ≥ 0xF8, note on/off, and CC except CC0 and `CC_BANK_SELECT_LSB` (0x20).
  - Drops PC, pitch bend, pressure, MTC quarter frame and song position/select.
- **E8 nothing loads into 17:** `menu_instrumentEntryScene()` (Load → home, Save → 16); the four Preset Instrument loaders refuse 16.
- **E9 AutoSave dropped at once:** the yield also aborts an in-flight drain at its next tick through its error close-down; HCNAMES publication phases 70–77 and the Pattern follow-on are never cut.

### 4.5 P1 assessment (A1–A9) and fixes

| Id | Severity | Finding | Fix |
|----|----------|---------|-----|
| A1 | High (schedule defect) | E9 aborted the scalar drain in phases waiting for an AsyncFATFS callback (open/remove: the callback then writes `op_file`/`op_file_ready`/`op_remove_done` **inside the next Bank load**, handle leak; close: a second `fclose()` on a closing handle → phase 40/42 retry forever). | `filesystem_autosaveDrainAbortSafe(op_phase)` allow-list: 0, 1, 3, 5, 10, 13, 16, 17, 19, 21, 30, 32, 34, 36, 38, 39, 44, 50, 52, 54, 56, 62, 64, 65, 66, 67. Every wait phase, 40–46 and 70–77 run on; the abort fires at the next allowed phase. |
| A2 | Medium | Pattern drain phase 7 (seek IN_PROGRESS) aborted mid-seek. | Exclude phase 7; abort at 8. |
| A3 | High (pre-existing, normal case on 17) | One identity block (`fs_identity_name`) served the name session and every load target. On 17, Kit Load writes home while the Kit-page session Scene is 16: the prefill showed loaded names, and a Kit Save from 17 set the identity Kit row, which the session-exit flush wrote into **home's** HCNAMES Kit row (source = loaded slot) → boot builds a non-existent folder → home emptied as Case 3 (F5 pattern). | Load-page session Scene = home; `menu_requestResidentNameScratch()` flushes dirty rows before reseeding; new `menu_flushForeignNameRows(source)` at Save-context entry (Kit, Instrument, Scene) writes rows owed for any other Scene first. Covers resident multi-destination cases too. |
| A4 | Low | Scene Save from 17 skipped `filesystem_prepareResidentNamesCache()`. | Back outside the branch (F9 rule). |
| A5 | Low | A refused MIDI request ended on the resident Bank slot with a stale preview. | `menu_backgroundBankLoadAbort()` re-requests the shown slot's selection. |
| A6 | Low (docs) | Deleted/short comment blocks. | Restored. |
| A7 | Low (docs) | Wrong decision refs; manifest byte split. | 91 B BSS + 2 B `.data`. |
| A8 | Info | Unbounded waits with every exit dropped. | 20 s bounded waits (§4.3). |
| A9 | — | Hardware verification. | Open. |

### 4.6 RAM (P1)

`fs_background_name` 90 B (BSS), `pm_bg_result` 1 B (BSS),
`pm_bg_pending_slot` 2 B (`.data`, initialiser 0xFFFF, +2 B flash). The
second use of `pm_bg_pending_slot` as the wait-start tick adds nothing.

Builds:
- P1 final: `text=594,344`.
- After the A-fixes: `text=594,872`, `data=420`, `bss=430,920`, flash 595,292 (headroom 158,372).

### 4.7 Bank slot scrolling fix (`S082_P1_IMPLEMENTATION.md` §14)

**Report.** Bank Load/Save slot scrolling blocked and lagged again (Scene
scrolling was fine). The user restated the rule, which is now **binding in
`MEMORY.md`** ("Load/Save Slot Scrolling Policy"):
- number → name → LED array, each asynchronous;
- each disposed in that order by the next turn.

An older MEMORY.md note had required the opposite (hold input until the
mask is valid). It was corrected.

**Cause.** Every slot change started the child preview, a scan of up to 16
children plus PAT4 reads, which held `menu_storageBusy`. The encoder and pot
gates dropped every turn until the scan finished.

**Fix:**
- `FS_BANK_FLAG_PREVIEW_CANCEL`, `filesystem_cancelScanBankScenes()` and `filesystem_scanBankScenesRunning()`. The scan acts on cancel only at callback-free phases (closes through its own close phases, returns to root, finishes ERROR, so a partial mask is never taken as valid).
- The Bank folder name is captured at request (`op_bank_display_name`).
- The preview never takes or clears `menu_storageBusy`. A request while a scan runs cancels it and defers; the deferred retry serves the latest slot.
- Every new selection (slot, type, page) and the Load/Save exit cancel a running preview.
- Bank Save OK during a preview cancels it and is consumed (a second OK posts). Bank Load OK stays gated by a valid preview.
- The MIDI UI poll treats a running scan as WAIT.

Build `text=595,136`, flash 595,556 (headroom 158,108).

---

## 5. Part P2 — audio during a background Bank load

### 5.1 Report and card (`S082_P2_LOAD_SAVE_FOLLOWUP.md`)

On a running Bank load that replaced the playing Scene:
1. the playing Scene got noticeably louder during the load;
2. each child loaded extremely slowly;
3. there was a glitch when the load finished.

`SD_CARD_082_P1/`:
- **image:** 595,572 B (the scroll-fix build);
- **result:** `active_bank=0`, row 0 `FluxTime 000`; the load completed;
- **trace coverage:** none of the load itself, because the trace flush is suppressed while an accepted command runs and the user powered off before the next flush;
- **scroll fix:** the cancelled previews show as `E` op 0x2b at their close phases. The decoder mislabelled 0x2b; it is fixed in P3.

### 5.2 Findings

- **P2-1 (High, confirmed in code):** EffectsManager caches its source Scene (`effects_state.scene_index`), set only by `effects_activateScene()`.
  - `effects_service()` (every render block, `mixer.c:893`) applies `scene_effectConst(scene_index)`: Morph, FX sequence, FX return level, route, pan and all type parameters.
  - The Scene 17 switch runs no activation, so the index kept naming home; when the load committed home's new Effect record (`filesystem_commitSceneStage()`), the sounding Effect took the new Bank's values.
  - **Missed by the §5 audit because it is a cached index.**
  - The other modules checked are clean: mixer via `preset_getSlotPlayedScene()`, the bus compressor via the active index, the Morph engine guarded, the Scene apply worker, the single-voice worker, and the modulation bindings.
- **P2-2 (High, probable):** `on_bank_load_complete()` marked every loaded child per byte (`autosave_markSceneWithPatternDirty()`).
  - That is about 1,300–1,500 marks per Scene, 20,000+ for 16, each with an IRQ save/restore and a DEV `D` record, in one foreground call.
  - The render queue holds two 96-frame slots (2.18 ms each, `config.h:795`), about 2–4 ms of headroom, so the burst underran. A stopped load hid it (codec suspended).
- **P2-3 (Medium, structural):** one `filesystem_tick()` per main-loop pass gives each `sdcard_poll()` one 16 B burst (`SDCARD_BURST_SIZE`), 32 passes per sector. With audio running, a child took about a second.
- **P2-4 (process):** verify D4 against every Scene-level consumer; a DEV self-check comparing the cached indices with the active Scene at the switch and at the end. **Not built.**
- **P2-5 (tooling):** no runtime Bank Load trace records (request, switch, child commits, completion, underrun count); the trace flush is suppressed during commands; decoder op table. The decoder was fixed in P3; **the rest is open.**

### 5.3 Fixes (`S082_P2_IMPLEMENTATION.md`)

- **Fix 1 — `effects_rebindSourceScene(scene)`.**
  - It sets only `effects_state.scene_index`: no `switchRuntime`, handoff, `force_all`, sequence clock, automation or LFO reset.
  - It refuses a missing record or a type different from the running type (the caller then falls back to `effects_activateScene()`).
  - Called in phase 3 of a fresh build after `scene_selectActive(16)`; a rebuild makes no call.
  - Scene 17's record is a byte copy, so `effects_service()` computes values equal to `last_applied[]` and writes nothing.
  - Side effect: Effect edits on 17 now reach the live Effect.
- **Fix 2 — `autosave_maskPayloadRangeDirty(first, count)`.**
  - It ORs whole mask bytes (edge bytes masked) and adds fresh bits to `autosave_dirty_count`, inside one PRIMASK section (about 5–10 µs).
  - `autosave_markSceneWithoutPatternDirty()` marks `[base + 8, base + 1,920)` (239 mask bytes; the Scene name 0..7 and Pattern are excluded) and emits one `L` LOAD_MARK, with no per-byte `D` or nested `I` records.
  - It is exact because drain phase 56 keeps a patch only where `autosave_getLivePayloadByte()` returns a value, so reserved and name cells are taken and dropped.
  - The source offsets are checked inside the range: Scene 8–9, Effect type 128–130, Effect source 558–559, Kit source 648–649, Instrument sources 779/971/1163/1355/1547/1739.
  - `autosave_markKitDirty()` keeps its per-byte form (Kit Load).
  - It benefits Bank/Scene Load completion, whole-Scene paste, the Merge re-mark and the filesystem ensure paths.
- **Fix 3 — idle-time servicing.**
  - `config.h` `BG_LOAD_IDLE_FS_BUDGET_US` = 200.
  - `main_serviceBackgroundLoadIdle()` runs after the regular `filesystem_tick()` + render pair: while `preset_backgroundBankLoadRunning()`, the codec is not suspended, the facade is BUSY and `audioCodec_queueFreeSlots() == 0`, it calls `filesystem_tickUncounted()` until the budget expires. The queue is re-tested before each step.
  - `filesystem_tickUncounted()` sets `fs_stall_poll_uncounted` (1 B, DEV_STALL_DETECTION only) so `filesystem_pollPhaseStall()` and `filesystem_autosaveDrainStalled()` neither count nor reset.
  - SD driver timeouts are time-based (`sdcard_waitTimedOut()`), so extra polls cannot shorten them.
  - Optional later tuning: run `FS_FAST_DRAIN_POLL_PASSES` inside the window.

Build: `text=595,968`, `data=420`, `bss=430,920`; flash 596,388 (headroom
157,276). The P2 assessment found no defects. **Hardware: the user confirmed
"works fine"** (2026-10-19).

---

## 6. Part P3 — work while a background load runs

### 6.1 Requirements (W1–W5) and decisions (D40–D53)

| Id | Requirement / decision |
|----|------------------------|
| W1 | While a load runs and the panel is not in Load/Save mode, the Load/Save mode LED flashes 2× the normal rate. |
| W2 | In Load/Save mode only the Bank Load screen (`Load:[Bank   ]` / `NNN <name> NN.`) shows, with no interaction. |
| W3 | Parameters and Morph can change; functions needing the Scene 17 swap cannot run. |
| W4 | PERF: Scenes not yet loaded are dark and fill left to right; the user can switch from 17 to a loaded Scene. |
| W5 | No other load speed-ups. |
| D40 | Unlock for Scene 17 loads and running direct loads; a load requested stopped keeps the P1 lock (adopted). |
| D41 | Copy/clear permitted; a job needing the 17 swap (overlap while 17 exists) or an unavailable Scene drops silently when it runs; clears allowed. |
| D42 | Merge-kept Scenes lit and selectable from the start (adopted). |
| D43 | A loaded Scene with no pattern content stays dark (normal PERF rule). |
| D44 | Fan-out through the edit mask limited to available Scenes; the user can switch freely between available Scenes. |
| D45 | Viewed Scene unavailable (FOLLOW off): STEP/Pattern-settings edits dropped silently (adopted; SOM needs nothing). |
| D46 | Child-failure overlay only on Load/Save at completion. |
| D47 | RAM up to 4 B approved. |
| D48 | Codec suspension only while on Load/Save (adopted; already true in code). |
| D49 | Frozen screen: SEQ LEDs = PERF fill; no Load↔Save toggle; live `NN.`. |
| D50 | No chain evaluation during a load; counters keep running. |
| D51 | `NN.` is 1-based (`01.`–`16.`); shared with Bank Save. |
| D52 | Manual "change to this Scene" (SEQ, `scd`, SHIFT+SEQ queue with its qac parts, VOICE+SEQ) proceeds when the target is available; automated changes (chain conditions and `nxt`/`prv`/random targets) wait for completion; MIDI PC stays dropped. |
| D53 | A copied Scene keeps its old name until the load ends; then the copy's names publish. |

### 6.2 Baseline facts (from the deep dive)

- **The lock was wider than R7.** The command keeps `menu_storageBusy` set and Preset reports LOAD_IN_PROGRESS from phase 4, so these all blocked every page:
  - the encoder (`menu_parseEncoder()`), pots (`menu_parseKnobDelta()`), page switch (busy queue), `menu_processPendingPageSwitch()`;
  - the knob repaint, runtime widgets, STEP overlay, the final repaint of `menu_switchPage()`;
  - COPY (`copyClear_copyPressed()`).
- **During a load the present mask and edit masks are the old Bank's** until the last child commits; `bankset.bcg` is parsed before any child.
- **Load order:** emptied Scenes are emptied in phase 98 before the first child; children load in ascending order. Each child commits settings and Kit before its Pattern/Effect I/O and is complete only when phase 20 is reached with DONE (from Scene Load phase 72).
- **`filesystem_bankChildCursor()`** is 0 before the first child and stays on the last child after the loop.
- **Clearing the browser cache is safe for the load:** Bank Load HCNAMES uses the dedicated mirror (`case 85`).
- **The completion re-applies an active Scene that is in the replaced set:** a second, audible activation if the user had switched to a loaded child.
- **Copy/clear names** use a borrowed 9 kB buffer that needs an idle facade, and would give up after `CC_NAME_RETRY_TICKS` (1,000).
- **One fast-blink LED existed;** `LED_MODE4` is Load/Save (steady) and MENU (normal blink).
- **The S058 scope** (`menu_loadSaveCommandInNoPlaybackScope()`) already returns 0 off Load/Save, so D48 needed no code.
- **Leaving Load/Save posts no card work** (the names flush is a background write); no deferral was needed.
- **No non-Load/Save page posts a Preset or filesystem request.** Instrument and Effect `typ` changes are runtime only; SOM never writes PatternData.

### 6.3 Design as built

- **Unlocked flag:** `PM_BG_PHASE_MASK` 0x07, `PM_BG_UNLOCKED` 0x08.
  - Phase 4: `progressive = NEEDED || (seq_isRunning() && !preset_backgroundScene17Needed(act|empty))`, which also covers the D31 direct load while 17 exists.
  - Passes `PRESET_BANK_LOAD_PROGRESSIVE`; on acceptance, phase 5 + UNLOCKED.
- **Availability (BankData, 2 B `bank_load_unavailable_mask`):**
  - `bank_beginLoadAvailability(act|empty)` runs in `filesystem_requestLoadBank()` (progressive), synchronously with the acceptance, so there is no window.
  - `bank_noteLoadChildDone(scene)` from phase 20 on success: sets the present bit early, clears the unavailable bit, `bank_revalidateVoiceEditMasksIn(bank_sceneAvailableMask())`.
  - `bank_endLoadAvailability()` in `on_bank_load_complete()` (before Menu) and `preset_bgEnd()`.
  - Queries: `bank_sceneAvailable()`, `bank_sceneAvailableMask()` (present & ~unavailable), `bank_loadUnavailableMask()`.
  - Users:
    - `bank_editTargetMask()` (raw entry & ~unavailable);
    - `bank_sceneFanoutMask()` (member test = available);
    - `effects_fanoutMask()` (`&= ~unavailable`);
    - `menu_residentPresentSceneMask()` (availability, no home fallback while any bit is unavailable);
    - PERF/queue/per-track/chain-editor/edit-mask-toggle selection;
    - the copy/clear job gate.
  - Persistence (AutoSave, Bank Save, the end commit) keeps presence. Outside a load all are identical.
- **Edit masks:**
  - `filesystem_bankLoadMergeEditMasksEarly()` at both first-child exits (phases 17, 98; progressive only);
  - at the metadata commit, `bank_stripFailedFromVoiceEditMasks(failed)` instead of the full merge;
  - scoped revalidation per child keeps links to pending Scenes; the full revalidation still runs at Menu completion.
- **Present commit (progressive):** `present_now & ~empty` (+ active), so a mid-load `clear scene` stays cleared; failed children keep their old bit, as before.
- **Panel:**
  - `menu_backgroundLoadReleasesPanel()` (unlocked && not Load/Save) relaxes the encoder, pots, knob repaint, runtime widgets, STEP overlay, the COPY session gate, the `menu_switchPage()` busy branch (no queuing) and its final repaint;
  - the R7 gates use `preset_backgroundBankLoadLocked()`.
- **Frozen screen:**
  - `menu_enterFrozenBankLoadPage()`: LOAD_PAGE, Bank, `EDIT_TYPE`, editMode 1, slot = `preset_getRequestSlot()`, no fresh-entry work;
  - a second Load/Save press returns early;
  - the name comes from `filesystem_bankLoadDisplayName()`;
  - SEQ LEDs through `menu_refreshPerfSceneLeds()` (`menu_bankLoadFrozenPageActive()`, also in the per-beat refresh);
  - `NN.` = cursor + 1 in the paint and in `menu_refreshBankChildProgress()`.
- **Fill observer:** `menu_refreshBackgroundLoadSceneLeds()` with `menu_bgLoadAvailCount` (1 B). The popcount of the available mask triggers a repaint (PERF or frozen page), plus one final repaint when the unlock ends.
- **W1:** `menu_syncQueuedSceneLed()` owns both fast-blink slots (`led_setFastBlinkSlot(LED_FAST_SLOT_QUEUED_SCENE / LED_FAST_SLOT_BANK_LOAD, led)`); `LED_MODE4` while unlocked and mode ≠ LOAD_SAVE. `led_fastBlinkLed[2]` shares one phase; the old single-LED API was removed.
- **Completion (`PRESET_OP_BANK_LOAD`):**
  - `active_replaced` only when not unlocked;
  - off page: `menu_finishBackgroundLoadCommandOffPage()` (S058 end, command and busy cleared, no Load/Save reset or repaint);
  - overlay only `!unlocked || on_page`;
  - the failed-operation branch is also quiet off-page;
  - `menu_backgroundBankLoadAbort()` is defensive off-page.
- **Scene changes:** `bank_sceneAvailable()` in `menu_perfSceneChangeExecute()`, the PERF press, the VOICE+SEQ move, the SHIFT+SEQ queue, `menu_chainMenuOpen/View()` and the VOICE-held mask toggle.
- **Chains:** `seq_chainServiceTick()` returns before `seq_chainEvaluate()` while `preset_backgroundBankLoadRunning()` (one byte, ISR-safe); counters keep running.
- **D45:** `menu_viewedSceneLoadPending()` joins the H-2 refusals (encoder and pots on SEQ_PAGE, STEP toggle).
- **Copy/clear:**
  - `copyClear_copyPressed()` ignores storage busy when the panel is released;
  - `ccSvc_jobLoadPending()` drops with `AUTOSAVE_TRACE_CC_DROP_LOAD_PENDING` (10) when the destination or paste source is unavailable;
  - `ccSvc_ensureScratch()` refuses while `menu_isLoadSaveCommandActive()` (covers the load and the frozen-screen final index restore, which reuses the same 9 kB array);
  - the name retry counter is frozen while the command is active;
  - the trickle governor (0.1 % CPU while the facade is busy) is unchanged, so jobs are slow during a load;
  - a job in its name phase holds the queue, and COPY is refused while a job is queued.
- **Ordering of copy names vs the Bank HCNAMES write:**
  - loaded child identity rows are set at their own commit (`filesystem.c:13151`), before the child is available;
  - the end of the load stages only row 0 and kept sources;
  - a copy records a row remap and clears the destination's R; it publishes after the command.

  So the copied name lands last.

### 6.4 Schedule corrections (`S082_P3_IMPLEMENTATION.md` §0.6)

| Item | Correction |
|------|------------|
| a | Availability is in BankData (an unavailable mask + early present bits), not a filesystem done mask; same 2 B, owner changed. |
| b | No exit deferral (leaving Load/Save posts no card work). |
| c | D48 already true. |
| d | Audit item 1 resolved (no non-Load page posts storage requests; `typ` runtime only; SOM no writes). |
| e | `bank_editTargetMask()` had no presence filter; per-child scoped revalidation instead of a use-time layout check. |
| f | Four more gates found (knob repaint, widgets, STEP overlay, switchPage repaint). |
| g | The end commit must respect mid-load edits. |
| h | Copy/clear slow during a load; name jobs hold the queue. |
| i | A pending change armed before a direct load is dropped by the executor. |
| j | The frozen name from the captured `op_bank_display_name`. |
| k | Bank Save `NN.` is also 1-based. |

### 6.5 Implementation log and assessment

- **Steps:** S1–S10 all DONE (implementer's log, 2026-10-19).
- **Deviations:**
  - (1) the BankData mask, as scheduled;
  - (2) linked BSS +8 B through alignment (the objects total 4 B);
  - (3) two misplaced edits, corrected before the final build (confirmed by the assessment).
- **Docs:** the spec "Implemented as" notes, `S082_P1` D7/D38 amendments, the manifest and the reference specs were updated.
- **Builds:**
  - S2 `text=596,168`;
  - S3 `596,416`;
  - S5 `597,464`, `bss=430,928`;
  - final `text=598,120`, `data=420`, `bss=430,928`; flash 598,540 (headroom 155,124); DTCM 4,472; ITCM 4,168; no new warnings.
- **Decoder:** the `fs_internal_op_t` table gained `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`, restoring 0x2b = `FS_INTERNAL_OP_SCAN_BANK_SCENES`.

**Assessment (§14): conforms; no blocking defect.** Interactions checked:
- the preview cancel only acts while a scan runs;
- the name-cache clear is safe;
- the copy-name ordering holds;
- the Pattern service replace waits only on the service Scene.

| Id | Severity | Finding | Recommendation |
|----|----------|---------|----------------|
| A1 | Low | `menu_serviceDeferredSceneChange()` applies `menu_applyQueuedSceneAction()` (qac mute/morph) before `menu_perfSceneChangeExecute()` drops an unavailable target: a queue armed before a direct (Merge, running) load whose target the load replaces still clears mutes (audible) and writes Morph 0 into the pending Scene. | Skip the whole action when `!bank_sceneAvailable(scene)`. **Open.** |
| A2 | Low (cosmetic) | A second Load/Save press on the frozen screen runs `led_clearAllBlinkLeds()` (`buttonHandler.c:1975`) before `menu_switchPage()` returns early; the PERF-style blinks are lost until the next beat/child. | Repaint (`menu_refreshLoadSceneLeds()`) in the re-press guard. **Open.** |
| A3 | Observation | SHIFT+REC opened from Load/Save mode shows no W1 blink (the mode stays LOAD_SAVE). | Matches W1 literally; leave unless asked. |

---

## 7. Resources (RAM policy record)

| Owner | Object | Bytes | Region | Part | Approval |
|-------|--------|------:|--------|------|----------|
| SceneData | `scene_background` | 1,656 | SRAM1 static | P0 C0 | 2026-10-17 |
| SceneData | `scene_backgroundFlags` + `scene_backgroundHome` | 2 | SRAM1 | P0 C0 | 2026-10-17 |
| Menu | `menu_bankPreviewContentMask` | 2 | SRAM1 | P0 B2 | S6 |
| Menu | `menu_bankUiFlags` | 1 | SRAM1 | P0 B2 | S6 |
| ButtonHandler | `bh_bankCopyPressTick` | 2 | SRAM1 | P0 B2 | S15 |
| filesystem | `op_bank_child_content_mask` | 2 | op scratch | P0 B1 | S6 |
| filesystem | `op_bank_scene_empty_mask` | 2 | op scratch | P0 B3 | 2026-10-18 |
| filesystem | `op_bank_save_written_mask` | 2 | op scratch | P0 B3 | 2026-10-18 |
| filesystem | `op_bank_flags` | 1 | op scratch | P0 B3 | 2026-10-18 |
| filesystem | `fs_autosave_yield` | 1 | SRAM1 | P0 C7 | 2026-10-18 |
| Preset | `pm_bg_phase` | 1 | SRAM1 | P0 C7 | 2026-10-18 |
| filesystem | `fs_background_name` | 90 | SRAM1 | P1 E1 | D29 |
| Preset | `pm_bg_pending_slot` | 2 | `.data` | P1 E5 | D33 |
| Preset | `pm_bg_result` | 1 | SRAM1 | P1 E5 | D34 |
| BankData | `bank_load_unavailable_mask` | 2 | SRAM1 | P3 | D47 |
| LED | `led_fastBlinkLed[2]` | +1 | SRAM1 | P3 | D47 |
| Menu | `menu_bgLoadAvailCount` | 1 | SRAM1 | P3 | D47 |
| PatternData | Scene 17 Pattern | 0 | reuses `pat_background_region` | — | — |

DEV-only:
- P0 §13: 1 + 2 + 2 + 5 B;
- P2: `fs_stall_poll_uncounted` 1 B under DEV_STALL_DETECTION.

Stack: `preset_buildBackgroundScene()` and `preset_copyBackgroundTrack()` use
one 136 B block buffer.

---

## 8. Storage and format changes

- **HCNAMES:** Scene-row tokens `bNNN` and `x` (§2.6). No compatibility: delete `/.hcnames`, `.hcnamtmp` and `.hcprms1/2` before testing.
- **AutoSave:**
  - the present mask is assigned on Bank Load;
  - a Merge re-marks the whole Bank;
  - AutoSave is suspended while Scene 17 exists, and the drain is aborted by the yield (allow-list);
  - whole-Scene marks are one range.
- **`bankset.bcg`:** unchanged format; `active_scene` is boot-only; Merge Save reads the old manifest (`storage_banksetParseLineMasked()`).
- **Bank Save:** deletes empty-mask children.
- **`settings.cfg`:** unchanged.
- **Trace (DEV):**
  - stage `b` (Bank preview: masks + elapsed);
  - stage `h` (HCNAMES provenance 0x01–0x06);
  - `Q` 0x08 Case-3 detail;
  - stall site 10 `BkPv`;
  - CC drop reasons 9 `SCENE17` and 10 `LOAD_PENDING`;
  - `L` per whole-Scene mark replaces the `D` floods.

---

## 9. Affected files

| File | Parts |
|------|-------|
| `Core/Bank/BankData.c/.h` | P0 B3/C4, P3 S1 |
| `Core/Bank/Scene/SceneData.c/.h` | P0 B3.6/C0 |
| `Core/Bank/Scene/Pattern/PatternData.c`, `PatternStackService.c` | P0 C1/C2/C5 |
| `Core/Bank/Scene/Preset/presetManager.c/.h` | P0 B3/C5/C7, P1 E4/E5/E8/A8, P2 F1, P3 S3 |
| `Core/Bank/Scene/Autosave.c/.h`, `AutosaveTrace.h` | P0 B1.6/§13, P2 F2, P3 S8 |
| `Core/DSP/Effects/EffectsManager.c/.h` | P0 C4, P2 F1, P3 S9 |
| `Core/Hardware/SD/filesystem.c/.h` | P0 B1/B3/C7/C9/§13, P1 E1/E3/E9/A1/A2/A4/§14, P2 F3, P3 S2 |
| `Core/Hardware/SD/storageTypes.c/.h` | P0 B3.14 |
| `Core/Hardware/frontPanel/buttonHandler.c/.h` | P0 A1.4/B2.13, P1 E6/A6, P3 S6 |
| `Core/Hardware/frontPanel/ledHandler.c/.h` | P3 S4 |
| `Core/Menu/menu.c/.h` | P0 A1/A2/B2/B3.12/C3/C10/§13, P1 E2/E6/E8/A3/A5/§14, P3 S5 |
| `Core/Menu/CopyClear/copyClearService.c/.h`, `copyClearSession.c`, `copyOps.c`, `clearOps.c` | P0 C4/C7/C9, P3 S8 |
| `Core/Sequencer/sequencer.c/.h` | P0 C6/C8/C9, P1 E4.6, P3 S7 |
| `Core/MIDI/MidiParser.c`, `MidiMessages.h` | P1 E7 |
| `main.c`, `config.h` | P2 F3 |
| `tools/decode_devlogs.py`, `tools/verify_bank_autosave.py` | P0 D/§13, P1, P3 |

---

## 10. API changes (summary)

New or changed public APIs:

- **BankData:**
  - `bank_editTargetMask()`, `bank_sceneFanoutMask32()`;
  - `bank_mergeVoiceEditEntry()`, `bank_mergeVoiceEditMasksAfterLoad()`;
  - `bank_beginLoadAvailability()`, `bank_noteLoadChildDone()`, `bank_endLoadAvailability()`;
  - `bank_loadUnavailableMask()`, `bank_sceneAvailableMask()`, `bank_sceneAvailable()`;
  - `bank_revalidateVoiceEditMasksIn()`, `bank_stripFailedFromVoiceEditMasks()`.
- **SceneData:** `scene_backgroundExists/HomeIndex/Begin/Release()`, `scene_playableIndexValid()`, `scene_resetResidentToEmpty()`; `scene_get/getConst/selectActive()` accept 16 while it exists.
- **Preset:**
  - `preset_loadBank(slot, act, empty, flags)`, `preset_saveBank(slot, act, empty)`;
  - `preset_requestBackgroundBankLoad()`, `preset_tickBackgroundBankLoad()`;
  - `preset_backgroundBankLoadRunning/Unlocked/Locked()`, `preset_takeBackgroundBankLoadResult()`;
  - `preset_buildBackgroundScene(rebuild, replaced)`, `preset_backgroundCandidateTracks()` (static);
  - flags `PRESET_BANK_LOAD_BOOT/REPLACE/PROGRESSIVE`, `PRESET_BANK_ORIGIN_MENU/MIDI`, `PRESET_BANK_REQUEST_MERGE`, `PRESET_BG_RESULT_*`.
- **Sequencer:** `seq_switchToBackgroundScene()`, `seq_moveTracksToBackgroundScene()`; release in `seq_selectActivePattern()`; chain suspended during a load.
- **EffectsManager:** `effects_rebindSourceScene()`; `effects_fanoutMask()` 32-bit, availability-filtered.
- **Autosave:** `autosave_maskPayloadRangeDirty()` (static); `autosave_markSceneWithoutPatternDirty()` range form.
- **filesystem:**
  - Bank requests: `filesystem_requestLoadBank(slot, act, empty, flags, cb)`; `filesystem_requestSaveBank(..., empty_mask, ...)`.
  - Bank results and preview: `filesystem_lastBankLoadEmptyMask()`, `filesystem_bankChildContentMask()`, `filesystem_scanBankScenesRunning()`, `filesystem_cancelScanBankScenes()`, `filesystem_bankSlotExists()`, `filesystem_bankLoadDisplayName()`.
  - AutoSave and ticks: `filesystem_setAutosaveYield()`, `filesystem_autosaveDrainAbortSafe()` (static), `filesystem_tickUncounted()`.
  - Scene 17 names: `filesystem_backgroundNamesCaptureScene()`, `filesystem_backgroundNameCaptureInstrument()`, `filesystem_backgroundNameSet()`.
  - Constants: `FS_RESIDENT_SOURCE_BANK_CHILD_BASE/END`, `FS_RESIDENT_SOURCE_EMPTY`.
- **storageTypes:** `storage_banksetParseLineMasked()`.
- **ButtonHandler:** `buttonHandler_enterLoadSaveForBackgroundLoad()`; `buttonHandler_closeLatchedSurfaces()` (static).
- **ledHandler:** `led_setFastBlinkSlot()`, `led_getFastBlinkSlot()`, `LED_FAST_SLOT_*` (replace `led_setFastBlinkLed/getFastBlinkLed()`).
- **Menu:**
  - Bank COPY: `menu_bankCopyPressed/HoldReached/Released/ForceRelease/Armed()`.
  - Background-load UI: `menu_backgroundBankLoadUiBegin/UiIdle/UiPoll/Abort()`, `menu_backgroundNamesChanged()`, `MENU_BG_UI_*`.
  - P3 panel queries: `menu_backgroundLoadReleasesPanel()`, `menu_bankLoadFrozenPageActive()`, `menu_viewedSceneLoadPending()`.
  - Statics: `menu_flushForeignNameRows()`, `menu_finishBackgroundLoadCommandOffPage()`, `menu_enterFrozenBankLoadPage()`, `menu_refreshBackgroundLoadSceneLeds()`, `menu_resetSaveParametersEx()`, `menu_instrumentEntryScene()`, `menu_instrumentLoadCommitChange()`, `menu_instrumentLoadKitRowAvailable()`, `menu_instrumentTypeSelectableForMask()`.
- **Copy/clear:** `ccSvc_dropQueued()`, `ccSvc_jobLoadPending()` (static); drop reasons 9 and 10.
- **MIDI:** `midiParser_acceptedDuringBankLoad()` (static), `CC_BANK_SELECT_LSB`.
- **main:** `main_serviceBackgroundLoadIdle()` (static), `BG_LOAD_IDLE_FS_BUDGET_US`.

---

## 11. Build results and hardware verification

### 11.1 Build trail (DEV link)

| Point | text | data | bss | Flash used / headroom |
|-------|-----:|-----:|----:|------------------------|
| S081 baseline (`2a0fd86`) | 573,296 | 416 | 429,112 | 573,712 / 179,952 |
| P0 after §13 fixes | 590,528 | 416 | 430,816 | 590,944 / 162,720 |
| P1 final | 594,344 | 420 | 430,920 | 594,764 / 158,900 |
| P1 A-fixes | 594,872 | 420 | 430,920 | 595,292 / 158,372 |
| P1 scroll fix | 595,136 | 420 | 430,920 | 595,556 / 158,108 |
| P2 | 595,968 | 420 | 430,920 | 596,388 / 157,276 |
| **P3 final (S082 close)** | **598,120** | **420** | **430,928** | **598,540 / 155,124** |

DTCM statics 4,472 B, ITCM 4,168 B, FXBUF 126,592 B (margin 3,712 B). There
are no new warnings beyond the existing unused-function set in `filesystem.c`,
the newlib stubs and the LTO note. The production build was not re-measured.

### 11.2 Hardware status and open test lists

| Part | Status |
|------|--------|
| P0 | First-boot freeze once (not reproduced); F5 Scene 2 invalidation open. Matrix rows (`S082_P0_IMPLEMENTATION.md` §10) not run. |
| P1 | Scroll fix and the running-load audio problems found by the user; P2 fixed the latter. §10 rows E1–E9, A1–A8 checks not run. |
| P2 | Confirmed working by the user (2026-10-19). |
| P3 | Untested. |

**Test list to carry** (union of the open rows):

1. **P0 Bank round trips:**
   - 4 modes × {all, partial, none} × {empty, Kit-only, full} children;
   - the active and viewed Scene never change;
   - an emptied active Scene stays present;
   - `.hcnames` rows `-`/`bNNN`/`x` after each, then reboot (HCNAMES-authoritative and AutoSave paths);
   - `verify_bank_autosave.py`; the S058 clean skip still skips.
2. **P0 UI:**
   - LED matrix vs PERF; tap < 200 ms vs hold;
   - SEQ/OK blocked while COPY is held; MODE and event overflow while held;
   - labels; retention; `chg` rows; pot coalescing and BAR2.
3. **P0 F-fixes:**
   - stopped / Merge-keeping loads build no Scene 17;
   - the 30 s all-pot spin during the preview and an AutoSave drain;
   - a truncated `.pat` in a Bank child ends the preview (or `BkPv`);
   - F5 capture protocol (§3.6).
4. **P1:**
   - name prefills and saves from 17;
   - rebuild scope and fit (T3 on Scene 5, forced non-fit, T6/T7);
   - MIDI request deferral, drop and missing slot;
   - the MIDI-origin entry from every surface;
   - the drop filter;
   - loads on 17 go to home;
   - a load during a scalar drain in a wait phase and an action phase, during a Pattern drain in phases 7 and 4, and during an HCNAMES publication;
   - the A3 sequence (Kit Load into home, Kit Save from 17, exit, reboot with no AutoSave record);
   - A8 (stall phase 2: FAILED after 20 s).
5. **P2:** the `audioCodec_underrunCount` record across a 16-Scene Replace and a Merge (only listening was confirmed).
6. **P3** (`S082_P3_IMPLEMENTATION.md` §14.4):
   - leave Load/Save at once (MODE4 blinks, edits sound on 17, no change at commits);
   - PERF fill; switching 17 → A → B → A (no second activation, AutoSave idle);
   - fan-out links;
   - the frozen screen (1-based `NN.`, D7 on page, quiet off page);
   - queued/chain behaviour incl. A1;
   - FOLLOW-off STEP drop;
   - copy/clear mid-load (`LOAD_PENDING`, names after the load);
   - stopped load locked;
   - MIDI CC0 unlock at phase 5;
   - card pull off page;
   - underrun count.
7. **S081 live record across S082** (§13 item 8):
   - LATCH recording running through a background load that builds Scene 17: the take continues across the switch, recorded steps land on 17, and an R12 menu override and an `asm` ramp survive the switch;
   - a Scene change off 17 starts a new take, and the take recorded on 17 is gone after the release unless 17 was Scene-Saved first;
   - COPY during an unlocked load is refused while REC is armed;
   - a MIDI note during a load records on the played Scene.

---

## 12. Specification reference updates (S082 close)

| File | Update |
|------|--------|
| `FILESYSTEM_SPEC.md` | Bank Load/Save rewritten as a full reference: outcome table, phases (17, 98, 20, metadata commit, Save 61–64/removal/45), HCNAMES tokens with register values, boot reader, content probe and cancel, background and progressive Bank Load, Scene 17 filesystem rules (name capture, saves from 17, AutoSave yield/abort allow-list), idle servicing, F9 ordering rule; stale "present mask is the OR" sentence corrected. |
| `MODULE_INTERCHANGE_SPEC.md` | S082 APIs per module (BankData, SceneData, Preset, Sequencer, EffectsManager, filesystem, Menu, buttonHandler, ledHandler, copy/clear, MIDI, main); stale entries corrected (fast-blink API, panel gates, name-capture function names); the closing S082 section expanded into a "Background Bank load" walkthrough. |
| `AUTOSAVE.md` | Whole-Scene range marking (P2); abort allow-list; Scene 17 and P3 notes checked. |
| `DEV_MODES.md` | Stages `b`/`h`, `Q` 0x08 and boot-fail codes, stall site 10 `BkPv`, uncounted idle ticks, CC drop reasons 9/10, `L` per whole Scene. |
| `PATTERN_DYNAMIC_STACK.md` | Chain suspension during a Bank load; Scene 17 region use. |
| `BANK_PRESET_ARCHITECTURE.md`, `COPYCLEAR_UTILITIES.md`, `MIDI_SPEC.md`, `STORAGE_SRAM_MANIFEST.md` | References to the deleted S082 root documents repointed to this log; content checked. |

---

## 13. Carry-forward

1. **Hardware:** the §11.2 list. P1 and P3 matter most before S083 Phase D (MIDI PC/CC0).
2. **P3 A1** (queued action on a dropped target) and **A2** (frozen-screen blink) are one-line fixes, awaiting the user's decision. A3 is an observation.
3. **F5 Scene 2 Case-3 invalidation:** root cause open; capture protocol and instrumentation in place.
4. **First-boot freeze:** not reproduced; F2/F3/F4 address its probable routes. Use `DEV_LOGGING_IWDG 1` for a capture.
5. **P2-4 switch self-check** (DEV comparison of cached Scene indices) not built. **P2-5:** runtime Bank Load trace records (request, switch, per-child commit, completion, underrun count) not built; the trace flush is suppressed during a command (a power-off before the next flush loses it).
6. **Optional tuning:** `FS_FAST_DRAIN_POLL_PASSES` inside the idle window (P2 §11.4) only if the user asks; the user declined further speed-ups (W5).
7. **Production build** (`DEV_MODE_LOGGING 0`) not re-measured since S075.
8. **S081 live record across S082.** S081 was closed out on 2026-10-21 (`081_SESSION_HANDOFF_LOG.md`). S082 did not change `LiveRecord.c/.h`. The following was checked in code against that log; none of it is hardware-tested (§11.2 item 7):
   - **Scene 17 switch.** `seq_switchToBackgroundScene()` and `seq_moveTracksToBackgroundScene()` change only the read address. They keep the Scene and playback epochs, so the live-record take continues (D35). They also keep the R12 menu overrides (`seq_menuOverride`) and the `asm` ramps.
   - **Whole-Scene change.** `seq_selectActivePattern()` and `seq_alignActivePatternToScene()` clear the overrides, break the ramps and bump `seq_sceneEpoch`, which starts a new take. A whole-Scene change includes the release of 17 and a switch between loaded Scenes after the P3 unlock. A panel per-track move (`seq_setTrackPlayedScene()`, then `seq_realignTrackToMasterClock()`) clears that slot's override and starts a new take. The epoch bump is in the realign, not in `seq_setTrackPlayedScene()`.
   - **A take on 17 is temporary.** With the transport on 17, T = A = 16, so recorded steps write into `pat_background_region`. Scene 17 is never AutoSaved (D15) and is freed at its release, so the take is lost unless the user saves the Scene from 17 (D29) or copies it out before a Scene change. This follows from D15 and D35 and was not called out in P1.
   - **COPY refused while armed.** `copyClear_copyPressed()` refuses while `liveRec_armed()` (S081 F-N, `copyClearSession.c:587`). The P3 copy/clear-during-load path (D41) therefore needs REC off; clears are not affected.
   - **Pattern replacement suspends writing.** While `patSvc_replacePending()`, LiveRecord releases its target, sets writing to 0, turns the automation filters off and skips the ring drain (`LiveRecord.c:482`, `:525`). With T ≠ A during a load (a D31 partial rebuild or a per-track move), a loading child that is the service Scene suspends recording until its commit. Low risk.
   - **MIDI notes and CCs during a load.** They pass the D33 filter, so MIDI notes keep recording through `liveRec_triggerIn()`. CC recording is still the `0xFFFF` stub.
9. **Disposable:**
   - the eight root S082 documents (`S082_P0_LOAD_SAVE_MENU_CLEANUP.md`, `S082_P0_IMPLEMENTATION.md`, `S082_P1_LOAD_SAVE.md`, `S082_P1_IMPLEMENTATION.md`, `S082_P2_LOAD_SAVE_FOLLOWUP.md`, `S082_P2_IMPLEMENTATION.md`, `S082_P3_WORK_WHILE_BG_LOAD.md`, `S082_P3_IMPLEMENTATION.md`) are superseded by this log and the specs;
   - the card copies `SD_CARD_082_P0/` and `SD_CARD_082_P1/` (committed in `b25ffbe`) are evidence only;
   - the pre-implementation versions of the documents are in `45846b7` and `b25ffbe`.
10. **Next session:** S083 MIDI (`S083_P1_MIDI_IMPLEMENTATION_PLAN.md`, §13 "S082 outcome notes").
