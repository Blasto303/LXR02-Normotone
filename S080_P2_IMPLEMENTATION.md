# S080 P2 — Implementation Schedule

Implementation schedule for `S080_P2_PERF_EXPANSION_DEFECTS.md` (Defects
1–15 and 8a, plus Phase G scene chaining).  Every change is cited by file,
line and action (ADD / REMOVE / MODIFY).  Every new or rewritten block
carries its documentation comment inside the code snippet, written to be
pasted as-is into the `.c` / `.h` file.  For pure removals the rationale is
given in a short note instead.

No code has been changed by this document.

---

## 0. Conventions

* **Line numbers** refer to the working tree at commit `e621884` (S080 P1
  code; all P2 code was rolled back).  Each citation also quotes anchor text.
  Insertions shift later lines, so locate each edit by its anchor text, or
  apply the edits in a file from the bottom up.
* **One implementation pass.**  Some steps call functions introduced by a
  later step (noted as "provided by Step N"), and Step 0/1 remove
  `PAR_SCENE_PC_RESET` before Step 4 replaces its last use, so intermediate
  states do not build.  The build is verified once, at the end (Step 13).
* **Comment style** matches the surrounding code: block comments with
  What / Why / Inputs / Outputs / Accessors / Affiliates, 4-space
  indentation in `buttonHandler.c`, `menu.c`, `ledHandler.c` and the S080
  blocks of `sequencer.c`, tabs in the older `sequencer.c` functions.

---

## 1. Decisions applied

These come from the defect plan, the review, and the user's answers.

| # | Decision | Source |
|---|----------|--------|
| D1 | Releasing a looped track's VOICE button drops that track from the loop at once.  The loop stays engaged while any SELECT, any VOICE of a still-looped track, or any BAR button is held. | Defect 8 follow-up |
| D2 | A track added mid-loop gets its own loop start, quantised like an engage, and keeps it; existing tracks keep theirs. | Defect 7 |
| D3 | **Looper engine is anchored to the master clock.**  Each looped track stores a start tick and its state just before that tick.  Wraps fire when `(elapsed − anchor) % loopTicks == 0`, before the DDA advance.  A dropped or released track resumes at its snapshot projected forward to now ("where it would have been"), not via `seq_realignTrackToMasterClock()`.  This replaces Defect 7 step 4's realign, which only matches when every other track sits on its canonical master-clock position. | Review |
| D4 | Loop start (§3.4): qnt off → the tick at which the fastest track of the engage/add event last stepped; qnt on → the last quantise boundary on the master clock.  The snapshot is obtained by rewinding the live state to that tick. | Plan §3.4 |
| D5 | Scene change during a loop keeps each track's anchor tick; the snapshot is recomputed for the incoming scene at that tick (Defect 14).  A resetting change moves every anchor to tick 0 (decided by user). | Defect 14 |
| D6 | No SELECT LED in PERF unless the looper is held or engaged; the A5 sub-page indication on SELECT is removed. | Defect 4 |
| D7 | SHIFT+PLAY is consumed (no transport toggle) in PERF mode only. | Plan §4.1 context |
| D8 | Latched menus (SHIFT+REC and the chain editor): every SHIFT release while open is ignored (SHIFT LED stays lit); a SHIFT press closes the menu and its own release is consumed; a MODE button closes it first. | Defect 3, user |
| D9 | Chain-fired changes execute at the boundary where the condition was evaluated; they are not re-quantised by `scd`.  A chain fire is suppressed while a direct or queued change is pending. | Review |
| D10 | "End of repetition" = master-clock multiples of the longest track's length × scale.  Repetition and bar counts start at zero when a scene becomes active. | Review |
| D11 | `xNN` / `bNN` fire when the count **equals** NN (once per scene activation). | Review |
| D12 | Chain target not present in the Bank → no change.  `rNN` picks among scenes 1..NN; `r+N` / `r-N` exclude the current scene. | Plan §4.4 |
| D13 | A chain fire whose target is the playing scene (`slf`, or a resolved target equal to it) is a *repeat*: chain counters reset, `stp` clears, tracks realign (or restart when pcr is on), no DSP re-apply. | Plan §4.5 example |
| D14 | Loopable flag stored as an *exclude* bit: one mask byte per Pattern region; PAT4 per-track header byte 4 (0 = loopable).  Old files read as loopable; no PAT4 version change.  (The plan said storageTypes.c; Pattern storage lives in filesystem.c's PAT4 reader/writer.) | Defect 8a |
| D15 | Queue actions per Defect 15.  Direct and chain changes use pcr; queued changes use the `qac` reset bit only.  The file reload in `rld` / `r+r` is deferred (degrades to mute + morph reset). | Defect 15, user |
| D16 | The chain editor uses the retired, empty `PATTERN_SETTINGS_PAGE`, sub-page 0.  The four chain cells are `parameter_values[]` display mirrors of the viewed scene; edits commit only to that scene (no edit-mask fan-out). | Review |
| D17 | The looper engages only while the transport runs.  A SELECT press while stopped lights its LED but does not engage. | Review |

---

## 2. RAM to acknowledge (allocation policy)

All entries are SRAM1 `.bss` unless noted.  Nothing goes into DTCM.

| Owner | Item | Bytes | Lifetime |
|-------|------|-------|----------|
| sequencer.c | `seq_loopAnchor[7]` (uint32) | +28 | runtime |
| sequencer.c | `seq_loopStartPlayState[7]` | +7 | runtime |
| sequencer.c | remove `seq_loopTickCount` (uint16) | −2 | — |
| sequencer.c | chain runtime: `seq_chainBarCount`, `seq_chainRepCount`, `seq_previousScene`, `seq_chainStopped`, `seq_pendingChainFire` | +5 | runtime |
| ledHandler.c | fast blink: `led_fastBlinkLed`, `led_fastBlinkPhase`, `led_fastBlinkNext` (uint16) | +4 | runtime |
| buttonHandler.c | `bh_chainPlayArmed`, `bh_shiftReleaseConsume`, `bh_loopBarHeld` | +3 | runtime |
| menu.c | `menu_chainMenuScene` | +1 | runtime |
| SceneData (scenes[16]) | `chain_condition[2]`, `chain_next[2]` per Scene | +64 | retained (sceneset, AutoSave) |
| PatternData (`pat_regions[16]` + `pat_background_region`) | `track_loop_exclude` per region | +17 | retained (Pattern data) |
| **Total** | | **+127** | |

`parameter_values[]` mirrors (`PAR_CHAIN_*`, `PAR_TRACK_LOOPABLE`) use the
fixed 384-entry allocation and add no RAM.  The AutoSave chain cells use
previously reserved cells of the 118-byte Scene allocation; the record
layout and format are unchanged.

---

## Step 0 — Shared enum and name-table edits

All parameter / text enums and their parallel name tables change together,
so they are edited once here.  Later steps use these names.

### 0.1 ParameterArray.h — remove `PAR_SCENE_PC_RESET`, add chain mirrors and loopable

**File:** `Core/Bank/Scene/Preset/ParameterArray.h`
**Lines:** 230–246 (the "S080 scene queue / change division globals" block
through `PAR_SCENE_PC_RESET,`)
**Action:** MODIFY (rewrite block), REMOVE `PAR_SCENE_PC_RESET`, ADD five ids

New (replaces lines 230–246):
```c
	/*
	 * S080 scene queue / change division globals (scd, qcd, qac).
	 *
	 * What: PAR_SCENE_CHANGE_DIV (scd) is the musical division at which direct
	 * (SEQ) scene changes take effect; PAR_SCENE_QUEUE_DIV (qcd) is the same for
	 * queued (SHIFT+SEQ) changes; PAR_SCENE_QUEUE_ACTION (qac) selects what a
	 * queued change does when it fires: nrm, mut, mrp, res, u+r, o+r, mmr, rld,
	 * r+r (values 0..8, S080 P2 Defect 15). Why: musical timing and console
	 * normalisation for scene changes. The PC reset for direct and chain-fired
	 * changes is the pre-existing PAR_BAR_RESET_MODE (pcr); queued changes carry
	 * their own reset inside qac and ignore pcr. Inputs: global settings page
	 * edits and settings load/default. Outputs: sequencer scene-change deferral
	 * and the foreground executor (menu_serviceDeferredSceneChange()).
	 * Affiliates: filesystem settings writer/parser (keys scd/qcd/qac),
	 * MENU_MIDI_PAGE sub-page 2, sceneQueueActionNames (MenuText.h).
	 */
	PAR_SCENE_CHANGE_DIV,
	PAR_SCENE_QUEUE_DIV,
	PAR_SCENE_QUEUE_ACTION,

	/*
	 * S080 Phase G chain editor mirrors (cn1, nx1, cn2, nx2).
	 *
	 * What: display/edit mirrors of the four chain bytes of the Scene shown in
	 * the SHIFT+PLAY+SEQ chain editor: condition 1 (0..78), next scene 1
	 * (0..43), condition 2, next scene 2. Why: the generic static-cell renderer
	 * reads parameter_values[]; the retained values live per Scene in
	 * scene_settings_t.chain_condition[]/chain_next[]. Inputs: loaded from the
	 * viewed Scene by menu_chainMenuOpen()/menu_chainMenuView(). Outputs:
	 * edits commit through menu_commitChainParam() to that Scene only (no edit
	 * mask fan-out). Never serialized as Globals; menu_parseGlobalParam()
	 * treats them as no-ops so a settings load cannot write SceneData.
	 * Affiliates: PATTERN_SETTINGS_PAGE (menuPages.h), chainConditionNames/
	 * chainNextNames (MenuText.h), scene_setChainCondition()/Next().
	 */
	PAR_CHAIN_COND1,
	PAR_CHAIN_NEXT1,
	PAR_CHAIN_COND2,
	PAR_CHAIN_NEXT2,

	/*
	 * S080 P2 Defect 8a: per-track loopable mirror (lop).
	 *
	 * What: on/off display/edit mirror of the active track's loopable flag in
	 * the viewed Scene's Pattern region (1 = the looper may capture the track,
	 * 0 = always excluded). Why: lets the performer keep utility tracks
	 * running free while looping. Appended here, not beside
	 * PAR_TRACK_PLAY_MODE, so no established Global id is renumbered. Inputs:
	 * pat_applyTrackSettingsToMenu(). Outputs: edits commit through
	 * menu_cellCommitValue() to pat_setTrackLoopable(); bulk Global apply is a
	 * no-op for this id. Affiliates: pat_scene_region_t.track_loop_exclude,
	 * seq_loopableTrackMask(), SEQ_PAGE sub-page 0 cell 6.
	 */
	PAR_TRACK_LOOPABLE,
```

**Line:** after 254 (after the `PAR_BUS_COMP_SIDECHAIN < NUM_PARAMS`
static assert)
**Action:** ADD
```c
/*
 * S080 P2: the chain mirrors and the loopable mirror consume existing
 * parameter_values[] capacity; this keeps the fixed flat allocation honest.
 */
_Static_assert(PAR_TRACK_LOOPABLE < NUM_PARAMS,
	"S080 chain/loopable mirrors must fit the fixed flat parameter allocation");
```

### 0.2 menu.h — text, category, long and short enums

**File:** `Core/Menu/menu.h`

**Lines:** 160–164 (`/* S080 F2: scene change/queue division, queue action,
PCR globals. */` … `TEXT_CHAIN_COND1, …`)
**Action:** MODIFY (remove `TEXT_SCENE_PC_RESET`), ADD `TEXT_TRACK_LOOPABLE`

New:
```c
    /* S080 F2: scene change/queue division and queue action globals. The
     * S080 F2 duplicate pcr label was removed in S080 P2 (Defect 5); the one
     * PC reset is TEXT_BAR_RESET_MODE. */
    TEXT_SCENE_CHANGE_DIV, TEXT_SCENE_QUEUE_DIV, TEXT_SCENE_QUEUE_ACTION,
    /* S080 G10: per-Scene chain parameter labels (cn1/nx1/cn2/nx2). */
    TEXT_CHAIN_COND1, TEXT_CHAIN_NEXT1, TEXT_CHAIN_COND2, TEXT_CHAIN_NEXT2,
    /* S080 P2 Defect 8a: per-track loopable label (lop) on the STEP page. */
    TEXT_TRACK_LOOPABLE,
```

**Lines:** 223–226 (`LONG_SCENE_CHANGE_DIV, …`, `LONG_SCENE_PC_RESET,`,
`LONG_CHAIN_COND1, …`)
**Action:** MODIFY

New:
```c
    LONG_SCENE_CHANGE_DIV, LONG_SCENE_QUEUE_DIV, LONG_SCENE_QUEUE_ACTION,
    LONG_CHAIN_COND1, LONG_CHAIN_NEXT1, LONG_CHAIN_COND2, LONG_CHAIN_NEXT2,
    /* S080 P2 Defect 8a: "Loopable". */
    LONG_TRACK_LOOPABLE,
```

**Lines:** 264–267 (`SHORT_SCENE_CHANGE_DIV, …`, `SHORT_SCENE_PC_RESET,`,
`SHORT_CHAIN_COND1, … SHORT_CHAIN_NEXT2`)
**Action:** MODIFY

New:
```c
    SHORT_SCENE_CHANGE_DIV, SHORT_SCENE_QUEUE_DIV, SHORT_SCENE_QUEUE_ACTION,
    SHORT_CHAIN_COND1, SHORT_CHAIN_NEXT1, SHORT_CHAIN_COND2, SHORT_CHAIN_NEXT2,
    /* S080 P2 Defect 8a: "lop". */
    SHORT_TRACK_LOOPABLE
```

Removal note (`*_SCENE_PC_RESET`): there is one PC reset parameter,
`PAR_BAR_RESET_MODE` ("pcr", Sequencr, PCReset).  The S080 F2 duplicate is
removed from all three parallel enums so the tables below stay aligned.

### 0.3 MenuText.h — short names, long names, queue action names

**File:** `Core/Menu/MenuText.h`

**Lines:** 170–172 (`/* S080 E4/G10: … */`, `{"scd"},{"qcd"},{"qac"},{"pcr"},`,
`{"cn1"},…`)
**Action:** MODIFY
```c
    /* S080 E4/G10: BAR overlay, scene globals, and chain labels. The S080 F2
     * duplicate "pcr" was removed in S080 P2 (Defect 5); index 63 above is the
     * only pcr label. */
    {"pb<"},{"pn<"},{"pb>"},{"pn>"},
    {"scd"},{"qcd"},{"qac"},
    {"cn1"},{"nx1"},{"cn2"},{"nx2"},
    /* S080 P2 Defect 8a: per-track loopable. */
    {"lop"},
```

**Lines:** 226–229 (`/* S080 E4/F4/G10: … */` … `{"ChngDiv"},{"QueDiv"},
{"QueActn"},{"PCReset"},` …)
**Action:** MODIFY
```c
    /* S080 E4/F4/G10: overlay, scene-global, and chain long names. */
    {"BtnStat<"},{"BtnNote<"},{"BtnStat>"},{"BtnNote>"},
    {"ChngDiv"},{"QueDiv"},{"QueActn"},
    {"Conditn1"},{"NxtScn1"},{"Conditn2"},{"NxtScn2"},
    /* S080 P2 Defect 8a. */
    {"Loopable"},
```

**Lines:** 289–297 (the `S080 F4: queued scene-change action names` comment
and `sceneQueueActionNames[]`)
**Action:** MODIFY
```c
/*
 * S080 F4 / P2 Defect 15: queued scene-change action names.
 *
 * What: [0] = count; [1..9] = qac values 0..8: nrm (plain change), mut (all
 * mutes off), mrp (incoming Scene morphs to 0), res (queued PC reset: tracks
 * and master clock restart), u+r (mute + reset), o+r (morph + reset), mmr
 * (mute + morph + reset), rld (reload + mute + morph), r+r (reload + mute +
 * morph + reset). Why: a queued change has its own PC reset, separate from
 * pcr which governs direct and chain-fired changes. Rendered by the S080
 * static-param resolver (all DTYPE_MENU ids are taken). Affiliates:
 * PAR_SCENE_QUEUE_ACTION, menu_queueActionFlags() (menu.c),
 * menu_clampCellValue() (clamp 0..8).
 */
static const char sceneQueueActionNames[][4] = {
    {9},
    {"nrm"},{"mut"},{"mrp"},{"res"},{"u+r"},
    {"o+r"},{"mmr"},{"rld"},{"r+r"},
};
```

### 0.4 menu.c — dtype table and valueNames

**File:** `Core/Menu/menu.c`

**Lines:** 1196–1213 (the `S080 E6/F10: BAR overlay and scene-global
dtypes.` comment through `[PAR_SCENE_PC_RESET] = DTYPE_ON_OFF,`)
**Action:** MODIFY
```c
    /*
     * S080 E6/F10/G and P2: BAR overlay, scene-global, chain and loopable
     * dtypes.
     *
     * Why DTYPE_0B127 and not DTYPE_MENU: all 16 DTYPE_MENU table ids and all
     * 16 DTYPE_* nibbles are already used. The overlay trigger-type names,
     * scene division/action names, chain condition/next names and their clamp
     * domains are applied by static-param special cases keyed on the PAR_ id
     * (the S078 run-mode precedent). PAR_PERF_NOTE_* render def or a note name.
     * PAR_TRACK_LOOPABLE is an ordinary on/off cell.
     */
    [PAR_PERF_BTN_LT] = DTYPE_0B127,
    [PAR_PERF_NOTE_LT] = DTYPE_0B127,
    [PAR_PERF_BTN_RT] = DTYPE_0B127,
    [PAR_PERF_NOTE_RT] = DTYPE_0B127,
    [PAR_SCENE_CHANGE_DIV] = DTYPE_0B127,
    [PAR_SCENE_QUEUE_DIV] = DTYPE_0B127,
    [PAR_SCENE_QUEUE_ACTION] = DTYPE_0B127,
    [PAR_CHAIN_COND1] = DTYPE_0B127,
    [PAR_CHAIN_NEXT1] = DTYPE_0B127,
    [PAR_CHAIN_COND2] = DTYPE_0B127,
    [PAR_CHAIN_NEXT2] = DTYPE_0B127,
    [PAR_TRACK_LOOPABLE] = DTYPE_ON_OFF,
```

**Lines:** 1336–1344 (the S080 valueNames group, including
`{SHORT_SCENE_PC_RESET,CAT_SCENE,LONG_SCENE_PC_RESET},`)
**Action:** MODIFY (remove the pcr row, append loopable)
```c
    {SHORT_SCENE_CHANGE_DIV,CAT_SCENE,LONG_SCENE_CHANGE_DIV},
    {SHORT_SCENE_QUEUE_DIV,CAT_SCENE,LONG_SCENE_QUEUE_DIV},
    {SHORT_SCENE_QUEUE_ACTION,CAT_SCENE,LONG_SCENE_QUEUE_ACTION},
    {SHORT_CHAIN_COND1,CAT_CHAIN,LONG_CHAIN_COND1},
    {SHORT_CHAIN_NEXT1,CAT_CHAIN,LONG_CHAIN_NEXT1},
    {SHORT_CHAIN_COND2,CAT_CHAIN,LONG_CHAIN_COND2},
    {SHORT_CHAIN_NEXT2,CAT_CHAIN,LONG_CHAIN_NEXT2},
    /* S080 P2 Defect 8a: lop / Track / Loopable on the STEP track page. */
    {SHORT_TRACK_LOOPABLE,CAT_TRACK,LONG_TRACK_LOOPABLE},
```

---

## Step 1 — Defects 5 + 6: remove the duplicate pcr, reorder the Scene settings page

### 1.1 filesystem.c — settings default, parser and writer

**File:** `Core/Hardware/SD/filesystem.c`

**Lines:** 2590–2606 (comment ending "…and PCR off (pcr = 0), which
preserves…" and the default assignments)
**Action:** MODIFY comment, REMOVE line 2606
`parameter_values[PAR_SCENE_PC_RESET] = 0u;`

Replace the comment's sentence "…normal queue action (qac = 0), and PCR off
(pcr = 0), which preserves…" with "…and normal queue action (qac = 0, nrm),
which preserves…".  Rationale: the scene globals are scd/qcd/qac only; the
single PC reset (`bar_reset_mode`) keeps its pre-S080 default at line 2684.

**Line:** 2795 `else if (strcmp(key, "pcr") == 0) *param = PAR_SCENE_PC_RESET;`
**Action:** REMOVE

Removal note: an old settings.cfg with a `pcr=` line now hits the
`else return 0u;` unknown-key path and is skipped; `bar_reset_mode` still
loads.

**Lines:** 17470–17515 (the `S080 F5: scene queue / change division
globals.` comment, `case 21u:` … `case 25u:` terminator)
**Action:** MODIFY
```c
    /*
     * S080 F5: scene queue / change division globals.
     *
     * What: scd (direct scene-change division), qcd (queued change division),
     * qac (queued change action, 0..8 incl. the queued PC reset, S080 P2
     * Defect 15). Why: these controls add musical timing and reset options to
     * scene changes and must persist across power cycles. The one PC reset for
     * direct/chain changes is bar_reset_mode above (S080 P2 Defect 5 removed
     * the duplicate pcr key). Together with the four overlay keys above the
     * settings data lines grow from 17 to 24; the terminator count moves with
     * them. Affiliates: filesystem_settingsParamForKey(), the global settings
     * page.
     */
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
        /*
         * Self-checking terminator for format expansion.
         *
         * What: emits "lines=24\n" as the final line, where 24 is the count
         * of preceding data lines (cases 0u..23u: 17 original + 4 overlay
         * (S080 E10) + 3 scene globals (S080 F5, P2 Defect 5)). Why: gives
         * consumers a cheap completeness signal that distinguishes a genuine
         * end-of-file from a clean-but-early EOF after a crash. Adding a
         * settings key requires incrementing both its own case and this
         * declared count. The boot recovery prelude validates this count; the
         * normal load path ignores it as an unknown key. Affiliates: §6 of
         * S057_SETTINGS_WRITE_SAFE.md.
         */
        return filesystem_formatAssignmentU16Line(dst, cap, "lines", 24u);
```
Note: a settings.cfg written by P1 firmware ends `lines=25`.  The recovery
prelude reads N from the file and compares it with the line count of that
same file, so a P1 file still validates.

### 1.2 menuPages.h — Scene settings sub-page layout

**File:** `Core/Menu/menuPages.h`
**Lines:** 46–62 (the `S074 Scene-owned bus compressor page.` and
`S080 F3:` comments plus the sub-page 2 row)
**Action:** MODIFY
```c
  /*
   * Scene settings sub-page (MENU_GLOBAL_SCENE_SUBPAGE, S074 / S080 P2).
   *
   * What: the scene change/queue globals (scd, qcd, qac) occupy columns 0-2
   * and the S074 Scene-owned bus compressor (cmp, cam, ctm, csc) columns 3-6;
   * column 7 is the TEXT_EMPTY/PAR_NONE traversal terminator. Why: the
   * compressor must stay the last group of settings in the global menu
   * (S080 P2 Defect 6), and the duplicate pcr cell is gone (Defect 5; the one
   * pcr is PAR_BAR_RESET_MODE on sub-page 1). This stays the last populated
   * settings sub-page. Accessors: global encoder traversal, menu cue logic.
   * Affiliates: menu_commitBusCompParam() (compressor cells are active-Scene
   * mirrors), menu_devCheckGlobalSceneSubPage() (diagnostic layout check).
   */
  {TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,TEXT_BUS_COMP_MODE,
    TEXT_BUS_COMP_AMOUNT,TEXT_BUS_COMP_TIME,TEXT_BUS_COMP_SIDECHAIN,TEXT_EMPTY,
    PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,PAR_BUS_COMP_MODE,
    PAR_BUS_COMP_AMOUNT,PAR_BUS_COMP_TIME,PAR_BUS_COMP_SIDECHAIN,PAR_NONE},
```

### 1.3 menu.c — diagnostic layout check

**File:** `Core/Menu/menu.c`
**Lines:** 16385–16399 (`BC18 diagnostic check` comment and the `bad`
expression)
**Action:** MODIFY
```c
/*
 * BC18 diagnostic check: the Scene-owned compressor block stays last.
 *
 * What: verifies the named page holds the scene globals in columns 0-2, cmp
 * at column 3 (top4), the terminator at column 7 (top8), and that no
 * populated settings page follows it (S080 P2 Defect 6 layout). Why: menu
 * cues and SELECT traversal rely on the append-only placement rule. Output:
 * silent success; on failure, a 1.5 s LCD notice before boot continues.
 * Production compiles the check out. Affiliate: menuPages.h and
 * MENU_GLOBAL_SCENE_SUBPAGE.
 */
static void menu_devCheckGlobalSceneSubPage(void)
{
    const Page *scene_page =
        &menuPages[MENU_MIDI_PAGE][MENU_GLOBAL_SCENE_SUBPAGE];
    uint8_t bad = (uint8_t)(scene_page->top4 != TEXT_BUS_COMP_MODE ||
                            scene_page->top8 != TEXT_EMPTY);
```
(The rest of the function is unchanged.)

### 1.4 menu.c — `pcr` commit comment

**File:** `Core/Menu/menu.c`
**Lines:** 15972–15979 (`case PAR_BAR_RESET_MODE:`)
**Action:** MODIFY (comment only)
```c
    case PAR_BAR_RESET_MODE:
        /*
         * pcr - the one PC reset (S080 P2 Defect 5).
         *
         * What: sets the Sequencer's legacy boundary-commit bar reset and is
         * read directly by the PERF scene-change executor: with pcr on, a
         * direct (SEQ) or chain-fired scene change restarts every track and
         * the master clock (seq_resetToPatternStart()). Queued (SHIFT+SEQ)
         * changes ignore pcr and use the reset bit of qac (Defect 15). Why:
         * one parameter, as before S080. Inputs: settings load / Global edit.
         * Outputs: seq_resetBarOnPatternChange. Affiliates:
         * menu_perfModeSceneButtonPressed(), menu_serviceDeferredSceneChange().
         */
        seq_resetBarOnPatternChange = value;
        break;
```

### 1.5 menu.c — clamp comment

**File:** `Core/Menu/menu.c`
**Lines:** 5687–5694 (comment `S080 E6/F10: BAR overlay and scene-global
domains.`)
**Action:** MODIFY (comment; code changes in Steps 4 and 11)

Replace "…scd/qcd to 0..9, and qac to 0..4. … pcr is DTYPE_ON_OFF and
clamps through the normal dtype path." with "…scd/qcd to 0..9, qac to 0..8
(Defect 15), chain conditions to 0..78 and chain next-scene values to 0..43
(Phase G)."

---

## Step 2 — Defect 4: no SELECT LED in PERF unless looping

### 2.1 ledHandler.c / ledHandler.h — remove `led_initPerformanceLeds()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 1145–1162 (comment `…SELECT LEDs are cleared and SELECT1 is lit
as the only playable pattern slot…` and the function body ending
`led_setValue(1, LED_PART_SELECT1);`)
**Action:** REMOVE

**File:** `Core/Hardware/frontPanel/ledHandler.h`
**Line:** 164 (`void led_initPerformanceLeds(void);`)
**Action:** REMOVE

Removal note: the function forced SELECT1 on (Defect 4).  Its only caller,
the PERF SHIFT-release path (buttonHandler.c:2605), is rewritten in 2.5 to
rely on `menu_switchPage(PERFORMANCE_PAGE)`, which repaints the SELECT row
through `menu_syncPerfSelectLed()`.  Nothing else calls it.

### 2.2 buttonHandler.c — PERF entry

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1468–1469 (`/* S080 A5: PERF lands on sub-page 0 (mrp) -> SELECT1 lit. */`
`led_setActiveSelectButton(0u);`)
**Action:** REMOVE

Removal note: `menu_switchPage(PERFORMANCE_PAGE)` two lines above already
runs `menu_syncPerfSelectLed()` (menu.c:15364), which now leaves the row dark
unless the looper is held or engaged.

### 2.3 buttonHandler.c — looper SELECT LED accessor

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** before 1205 (`static void buttonHandler_perfVoiceReleased`), in the
looper helper block added by Step 9.3
**Action:** ADD (shown here; it lives with the Step 9.3 helpers)
```c
/*
 * Report the SELECT LED the PERF looper owns (S080 P2 Defect 4).
 *
 * What: returns the zero-based SELECT index of the active loop division while
 * any looper SELECT is held or the loop is engaged (sustained by VOICE/BAR),
 * otherwise 0xFF. Why: in PERF the SELECT row is the looper's division
 * display and must be dark when the looper is idle; Menu needs this to repaint
 * the row on page changes without owning looper state. Inputs:
 * bh_loopSelectHeldMask, seq_loopActive, bh_loopLastSelectButton. Output:
 * 0..7 or 0xFF. Accessors: menu_syncPerfSelectLed(). Affiliates:
 * led_setActiveSelectButton(), led_clearSelectLeds().
 */
uint8_t buttonHandler_loopSelectLed(void)
{
    if (bh_loopSelectHeldMask != 0u || seq_loopActive)
        return bh_loopLastSelectButton;
    return 0xFFu;
}
```

**File:** `Core/Hardware/frontPanel/buttonHandler.h`
**Line:** after 115 (`void buttonHandler_muteVoice(...)`)
**Action:** ADD
```c
/*
 * S080 P2 Defect 4: SELECT index (0..7) of the PERF looper division while the
 * looper is held or engaged, else 0xFF. Client: menu_syncPerfSelectLed().
 */
uint8_t buttonHandler_loopSelectLed(void);
```

### 2.4 menu.c — `menu_syncPerfSelectLed()`

**File:** `Core/Menu/menu.c`
**Lines:** 9922–9946 (the `S080 A5: reflect the active PERF sub-page` comment
and function)
**Action:** MODIFY
```c
/*
 * Repaint the PERF SELECT row from looper state (S080 A5, P2 Defect 4).
 *
 * What:  lights the SELECT LED of the looper division while the looper is
 *        held or engaged, otherwise clears the SELECT row.
 * Why:   in PERF the SELECT row belongs to the performance looper; no SELECT
 *        LED is lit unless the looper is active (Defect 4). The S080 A5
 *        sub-page indication on SELECT1/SELECT2 is removed with it.
 * Inputs: buttonHandler_loopSelectLed().
 * Outputs: one SELECT LED lit, or the SELECT row dark.
 * Accessors: menu_switchPage() PERF LED block, the PERF cross-sub-page step in
 *        the encoder traversal, buttonHandler's looper release.
 * Affiliates: led_setActiveSelectButton(), led_clearSelectLeds().
 */
void menu_syncPerfSelectLed(void)
{
    uint8_t sel = buttonHandler_loopSelectLed();

    if (sel < 8u)
        led_setActiveSelectButton(sel);
    else
        led_clearSelectLeds();
}
```

**File:** `Core/Menu/menu.h`
**Lines:** 655–662 (comment above `void menu_syncPerfSelectLed(void);`)
**Action:** MODIFY (comment)
```c
/*
 * Repaint the PERF SELECT row from looper state (S080 A5, P2 Defect 4).
 *
 * Input: none. Output: the looper division's SELECT LED while the looper is
 * held or engaged, otherwise a dark SELECT row. Clients: menu_switchPage()
 * PERF block, the PERF sub-page step, buttonHandler looper release.
 * Affiliates: buttonHandler_loopSelectLed().
 */
```

**File:** `Core/Menu/menu.c`
**Lines:** 15362–15364 (`/* S080 A5: show the active PERFORMANCE_PAGE
sub-page on the SELECT row … */ menu_syncPerfSelectLed();`)
**Action:** MODIFY (comment only)
```c
        /* S080 P2 Defect 4: the SELECT row shows only the looper division. */
        menu_syncPerfSelectLed();
```

### 2.5 buttonHandler.c — SHIFT release in PERF must not wipe the looper LED

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2601–2605 (`case SELECT_MODE_PERF:` `led_clearAllBlinkLeds();`
`led_clearSelectLeds();` `menu_switchPage(PERFORMANCE_PAGE);`
`led_initPerformanceLeds();`)
**Action:** MODIFY
```c
        case SELECT_MODE_PERF:
            /*
             * Return to the PERF page after a SHIFT gesture.
             *
             * What: clears stale blinks and re-enters PERFORMANCE_PAGE; the
             * page switch repaints the SEQ Scene row and the SELECT row from
             * looper state. Why: the old led_clearSelectLeds() +
             * led_initPerformanceLeds() pair wiped the looper's division LED
             * whenever SHIFT was released during a loop (S080 P2 Defect 4).
             * Affiliates: menu_switchPage() PERF LED block,
             * menu_syncPerfSelectLed().
             */
            led_clearAllBlinkLeds();
            menu_switchPage(PERFORMANCE_PAGE);
```
(The S080 A2 comment and `menu_resetActiveParameter(); return;` that follow
are unchanged.)

---

## Step 3 — Defect 9: SEQ is always a full scene change during a loop

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1301–1313 (non-SHIFT `case SELECT_MODE_PERF:` routing)
**Action:** MODIFY
```c
        case SELECT_MODE_PERF:
            /*
             * PERF SEQ press routing (S077 P2 §4.1, §4.2; S080 P2 Defect 9,
             * Phase G).
             *
             * What: while the chain editor is open, a SEQ press selects which
             * Scene's chain is viewed (no scene change). Otherwise a held
             * VOICE routes to per-track assignment, unless the looper is
             * engaged: then SEQ is always a full scene change. Without a held
             * VOICE the press is a scene-level switch with double-click
             * detection. Why: during a loop the VOICE buttons choose looped
             * tracks, so per-track assignment would hijack every scene change
             * (Defect 9). Inputs: seqButtonPressed, perf_heldVoiceMask,
             * seq_loopActive, menu_chainMenuIsOpen(). Outputs: chain view
             * switch, per-track assignment or scene change. Affiliates:
             * menu_chainMenuView() (Step 11), buttonHandler_perfVoiceHeldSeqPressed(),
             * buttonHandler_perfScenePressed().
             */
            if (menu_chainMenuIsOpen()) {
                menu_chainMenuView(seqButtonPressed);
                break;
            }
            if (perf_heldVoiceMask != 0u && !seq_loopActive)
                buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
            else
                buttonHandler_perfScenePressed(seqButtonPressed);
            break;
```
(`menu_chainMenuIsOpen()` / `menu_chainMenuView()` are provided by Step 11.)

---

## Step 4 — Defects 11 + 15: queue actions, separate queued reset, mute LED sync

### 4.1 buttonHandler.c — `buttonHandler_clearAllMutes()`

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after 436 (end of `buttonHandler_showMuteLEDs()`)
**Action:** ADD
```c
/*
 * Clear every VOICE mute mirror (S080 P2 Defect 11).
 *
 * What: zeroes buttonHandler_mutedVoices and, when the VOICE row is currently
 * showing mute state, repaints it. Why: a queued scene change with a mute
 * reset (qac mut/u+r/mmr/rld/r+r) unmutes the Sequencer through
 * seq_setMute(7, 0), but the VOICE LEDs read this mirror; without the clear
 * they keep showing the old mutes, and the next VOICE release would toggle
 * from the stale state. The repaint is gated on menu_muteModeActive so a
 * queued change firing while another page owns the VOICE row does not
 * overwrite that page's LEDs. Inputs: none. Outputs: mirror cleared, VOICE
 * row repainted when it shows mutes. Accessors: menu_applyQueuedSceneAction().
 * Affiliates: seq_setMute(), buttonHandler_showMuteLEDs(),
 * buttonHandler_muteVoice().
 */
void buttonHandler_clearAllMutes(void)
{
    buttonHandler_mutedVoices = 0u;
    if (menu_muteModeActive)
        buttonHandler_showMuteLEDs();
}
```

**File:** `Core/Hardware/frontPanel/buttonHandler.h`
**Line:** after 115
**Action:** ADD
```c
/* S080 P2 Defect 11: clear the VOICE mute mirror after a bulk unmute. */
void    buttonHandler_clearAllMutes(void);
```

### 4.2 menu.c — executor prototype

**File:** `Core/Menu/menu.c`
**Lines:** 2008–2009 (`/* S080 F9: ungated PERF scene-change executor … */`
`static void menu_perfSceneChangeExecute(uint8_t scene_index);`)
**Action:** MODIFY
```c
/* S080 F9 / P2 Defect 15: ungated PERF scene-change executor; `restart`
 * selects a PC reset (pcr for direct/chain changes, qac for queued ones). */
static void menu_perfSceneChangeExecute(uint8_t scene_index, uint8_t restart);
/* S080 P2 Defect 10: owner of the queued-scene fast blink (Step 8). */
static void menu_syncQueuedSceneLed(void);
```

### 4.3 menu.c — direct scene press

**File:** `Core/Menu/menu.c`
**Lines:** 8229–8253 (`menu_perfModeSceneButtonPressed()`)
**Action:** MODIFY (last statement and comment)

Replace the final `menu_perfSceneChangeExecute(scene_index);` with:
```c
    /*
     * S080 P2 Defect 5/15: a direct change resets only when pcr
     * (PAR_BAR_RESET_MODE) is on.
     */
    menu_perfSceneChangeExecute(scene_index,
                                (uint8_t)(parameter_values[PAR_BAR_RESET_MODE] != 0u));
```

### 4.4 menu.c — executor, queue action decode, service

**File:** `Core/Menu/menu.c`
**Lines:** 8255–8330 (`menu_perfSceneChangeExecute()`,
`menu_applyQueuedSceneAction()`, `menu_serviceDeferredSceneChange()`)
**Action:** MODIFY (replace all three functions)
```c
static void menu_perfSceneChangeExecute(uint8_t scene_index, uint8_t restart)
{
    if (scene_index >= SCENE_COUNT || scene_index >= 16u ||
        !bank_scenePresent(scene_index)) {
        return;
    }
    scene_selectActive(scene_index);
    bank_selectActiveSceneForEditMask(scene_index);
    /*
     * PERF Scene selection maps one-to-one onto PatternData's Scene slot.
     *
     * Inputs: validated Scene button index. Outputs: Menu edits now target that
     * Scene's Pattern, and Sequencer playback reads the same Pattern immediately.
     * This keeps step edits single-Scene only; parameter fan-out continues to use
     * scene_mask_voice_edit and is not consulted by PatternData.
     */
    menu_setShownPattern(scene_index);
    seq_selectActivePattern(scene_index);
    /*
     * S080 F9/G12, P2: an executed switch cancels any pending direct change
     * and any chain fire, but never a user-queued change - only a transport
     * stop cancels a queued change (plan §5.3).
     */
    seq_cancelPendingDirectChange();
    /*
     * S080 P2 Defects 5/12/15: PC reset.
     *
     * What: when `restart` is set, every track and the master clock restart
     * from the beginning of the new Scene, and an engaged looper moves every
     * loop start to that beginning (Defect 14). Why: pcr governs direct and
     * chain changes; queued changes pass the qac reset bit instead. With
     * `restart` clear, seq_selectActivePattern() has already realigned every
     * track to the running master clock. Inputs: restart. Outputs:
     * seq_resetToPatternStart(). Affiliates: menu_perfModeSceneButtonPressed(),
     * menu_serviceDeferredSceneChange(), seq_loopRebaseToStart().
     */
    if (restart)
        seq_resetToPatternStart();
    preset_startDrumsetApply();
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

/*
 * Queue action flags (S080 P2 Defect 15).
 *
 * What: decodes PAR_SCENE_QUEUE_ACTION (0..8: nrm, mut, mrp, res, u+r, o+r,
 * mmr, rld, r+r) into independent mute / morph / reset / reload bits. Why:
 * the executor applies each part in a fixed order, and a table keeps the
 * option list and its meaning in one place. Inputs: qac value. Output: flag
 * byte (0 for out-of-range values = normal change). Accessors:
 * menu_applyQueuedSceneAction(), menu_serviceDeferredSceneChange().
 * Affiliates: sceneQueueActionNames (MenuText.h).
 */
#define MENU_QAC_MUTE    0x01u
#define MENU_QAC_MORPH   0x02u
#define MENU_QAC_RESET   0x04u
#define MENU_QAC_RELOAD  0x08u

static uint8_t menu_queueActionFlags(uint8_t qac)
{
    static const uint8_t flags[9] = {
        0u,                                              /* nrm */
        MENU_QAC_MUTE,                                   /* mut */
        MENU_QAC_MORPH,                                  /* mrp */
        MENU_QAC_RESET,                                  /* res */
        MENU_QAC_MUTE | MENU_QAC_RESET,                  /* u+r */
        MENU_QAC_MORPH | MENU_QAC_RESET,                 /* o+r */
        MENU_QAC_MUTE | MENU_QAC_MORPH | MENU_QAC_RESET, /* mmr */
        MENU_QAC_RELOAD | MENU_QAC_MUTE | MENU_QAC_MORPH, /* rld */
        MENU_QAC_RELOAD | MENU_QAC_MUTE | MENU_QAC_MORPH |
            MENU_QAC_RESET                               /* r+r */
    };

    return (qac < 9u) ? flags[qac] : 0u;
}

/*
 * Apply the pre-change part of a queued scene action (S080 F9, P2 Defects
 * 11 and 15).
 *
 * What: for the incoming Scene `target`, applies the mute reset (Sequencer
 * mutes and the VOICE LED mirror) and the morph reset (overall, all six voice
 * and the Effect Morph of the target set to 0 through preset_morphScene(),
 * no edit-mask fan-out), then returns the decoded flags so the caller can
 * pass the reset bit to the executor. Why: the morph reset runs before the
 * change so the incoming Scene starts at Morph 0; the mute mirror must follow
 * the Sequencer (Defect 11). Reload (rld/r+r) is deferred: it needs a
 * performance-safe Scene Load path from this executor, so those options
 * currently degrade to their mute + morph (+ reset) parts (plan §9.1-8).
 * Inputs: target Scene, PAR_SCENE_QUEUE_ACTION. Outputs: mute/morph side
 * effects; flag byte. Accessors: menu_serviceDeferredSceneChange().
 * Affiliates: seq_setMute(), buttonHandler_clearAllMutes(),
 * preset_morphScene(), menu_queueActionFlags().
 */
static uint8_t menu_applyQueuedSceneAction(uint8_t target)
{
    uint8_t flags =
        menu_queueActionFlags(parameter_values[PAR_SCENE_QUEUE_ACTION]);

    if ((flags & MENU_QAC_MUTE) != 0u) {
        seq_setMute(7u, 0u);
        buttonHandler_clearAllMutes();
    }
    if ((flags & MENU_QAC_MORPH) != 0u)
        preset_morphScene(target, 0u);
    return flags;
}

/*
 * Run a fired deferred / queued / chain scene change (S080 F8/F9, P2).
 *
 * What: called on every buttonHandler_tick(). When the TIM3 ISR (or an
 * instant request) has raised the fire flag it reads the target and the
 * change kind, clears the pending state, then:
 *   - queued (SHIFT+SEQ): applies the qac mute/morph parts and executes the
 *     change with the qac reset bit (pcr is ignored, Defect 15);
 *   - chain-fired to the playing Scene: a repeat (seq_chainRepeatSelf(),
 *     no DSP re-apply), restarting when pcr is on;
 *   - direct or chain-fired to another Scene: executes with pcr.
 * Finally it re-derives the queued-scene fast blink (Defect 10). Why: the
 * ISR detects boundaries; DSP apply, LEDs and Menu state are foreground work.
 * A queued change fires regardless of the current menu (plan §5.3). Inputs:
 * seq_sceneChangeFirePending(), seq_pendingSceneTarget/IsQueue/IsChain(),
 * PAR_BAR_RESET_MODE. Outputs: the scene change. Accessors:
 * buttonHandler_tick(). Affiliates: menu_perfSceneChangeExecute(),
 * menu_applyQueuedSceneAction(), seq_chainRepeatSelf() (Step 11),
 * menu_syncQueuedSceneLed() (Step 8).
 */
void menu_serviceDeferredSceneChange(void)
{
    if (seq_sceneChangeFirePending()) {
        uint8_t scene = seq_pendingSceneTarget();
        uint8_t isQueue = seq_pendingSceneIsQueue();
        uint8_t isChain = seq_pendingSceneIsChain();
        uint8_t pcr = (uint8_t)(parameter_values[PAR_BAR_RESET_MODE] != 0u);

        seq_clearPendingSceneChange();
        if (isQueue) {
            uint8_t flags = menu_applyQueuedSceneAction(scene);

            menu_perfSceneChangeExecute(
                scene, (uint8_t)((flags & MENU_QAC_RESET) != 0u));
        } else if (isChain && scene == scene_getActiveIndex()) {
            seq_chainRepeatSelf(pcr);
        } else {
            menu_perfSceneChangeExecute(scene, pcr);
        }
    }
    menu_syncQueuedSceneLed();
}
```

### 4.5 menu.c — qac clamp and name count

**File:** `Core/Menu/menu.c`
**Lines:** 5711–5713 (`case PAR_SCENE_QUEUE_ACTION:` `if (*value > 4u) *value = 4u;`)
**Action:** MODIFY
```c
        case PAR_SCENE_QUEUE_ACTION:
            /* S080 P2 Defect 15: nine queue actions, 0..8. */
            if (*value > 8u) *value = 8u;
            return;
```

**Lines:** 9685–9687 (`case PAR_SCENE_QUEUE_ACTION:`
`menu_nameTable3(sceneQueueActionNames, 5u, value, buf);`)
**Action:** MODIFY
```c
    case PAR_SCENE_QUEUE_ACTION:
        /* S080 P2 Defect 15: nrm mut mrp res u+r o+r mmr rld r+r. */
        menu_nameTable3(sceneQueueActionNames, 9u, value, buf);
        return 1u;
```

### 4.6 filesystem.c — sanitize qac on load

**File:** `Core/Hardware/SD/filesystem.c`
**Line:** inside `filesystem_sanitizeLoadedGlobals()` (begins line 2609),
after the `PAR_MIDI_CHAN_GLOBAL` normalisation
**Action:** ADD
```c
    /*
     * S080 P2 Defect 15: qac has nine values (0..8). A hand-edited or corrupt
     * settings.cfg must not leave an out-of-range action that the menu cannot
     * display; out-of-range values fall back to nrm (0). P1 files storing 3
     * (old m+m) or 4 (old rld) now read as res / u+r (backward compatibility
     * not required, user).
     */
    if (parameter_values[PAR_SCENE_QUEUE_ACTION] > 8u)
        parameter_values[PAR_SCENE_QUEUE_ACTION] = 0u;
```

---

## Step 5 — Defect 3 and the latched-menu rules (SHIFT+REC, chain editor)

### 5.1 buttonHandler.c — release-consume flag

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 289–300 (SHIFT+REC state block)
**Action:** MODIFY (comment) and ADD one variable after line 300
```c
/*
 * SHIFT+REC settings menu state (S080 E7, P2 Defect 3).
 *
 * What: bh_shiftRecMenuActive is set while the SHIFT+REC overlay (repurposed
 * RECORDING_PAGE) is latched open. bh_shiftRecSavedPage/SubPg remember where
 * to return on exit. bh_shiftReleaseConsume swallows the SHIFT release that
 * pairs with a SHIFT press used to close a latched menu (SHIFT+REC or the
 * chain editor). Why: latched menus open on SHIFT+REC / SHIFT+PLAY+SEQ, stay
 * open after every button is released, and close on the next SHIFT press (or
 * REC for SHIFT+REC). The closing press's release must not run the
 * mode-specific SHIFT release path (which, in PERF, would re-enter the page
 * and clear looper LEDs). Inputs: SHIFT/REC/PLAY/SEQ events. Outputs:
 * page/sub-page switch and LED state. RAM: +4 bytes SRAM1 (S080 P2 adds
 * bh_shiftReleaseConsume). Affiliates: processPress()/processRelease()
 * BUT_SHIFT, BUT_REC, menu_chainMenuClose().
 */
static uint8_t bh_shiftRecMenuActive  = 0u;
static uint8_t bh_shiftRecSavedPage   = 0u;
static uint8_t bh_shiftRecSavedSubPg  = 0u;
static uint8_t bh_shiftReleaseConsume = 0u;
```

### 5.2 buttonHandler.c — SHIFT press closes a latched menu

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2336–2338 (`case BUT_SHIFT:` /
`/* _SEQUENCER_ADD_SPIKE_: restore SHIFT-press mode behavior parity with AVR. */`)
**Action:** ADD directly after `case BUT_SHIFT:`
```c
    case BUT_SHIFT:
        /*
         * S080 P2 Defect 3 / Phase G: SHIFT closes a latched menu.
         *
         * What: if the SHIFT+REC settings menu or the chain editor is open,
         * this press closes it (restoring the previous page) and is
         * consumed; its paired release is swallowed through
         * bh_shiftReleaseConsume. Why: both menus latch open on entry and
         * close on the next SHIFT press (plan §2.6, §4.1). Inputs:
         * bh_shiftRecMenuActive, menu_chainMenuIsOpen(). Outputs: menu closed,
         * release-consume armed. Affiliates: buttonHandler_shiftRecMenuExit(),
         * menu_chainMenuClose() (Step 11), processRelease() BUT_SHIFT.
         */
        if (bh_shiftRecMenuActive || menu_chainMenuIsOpen()) {
            if (bh_shiftRecMenuActive)
                buttonHandler_shiftRecMenuExit();
            else
                menu_chainMenuClose();
            bh_shiftReleaseConsume = 1u;
            return;
        }
```

### 5.3 buttonHandler.c — SHIFT release while latched

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2573–2578 (`case BUT_SHIFT:` `if (bh_shiftRecMenuActive) {`
`/* S080 E7: SHIFT release exits … */` `buttonHandler_shiftRecMenuExit();`
`break; }`)
**Action:** MODIFY
```c
    case BUT_SHIFT:
        /*
         * S080 P2 Defect 3 / Phase G: latched-menu SHIFT release.
         *
         * What: (1) the release paired with a closing SHIFT press is consumed
         * and only turns the SHIFT LED off; (2) while SHIFT+REC or the chain
         * editor is open, every SHIFT release is ignored, so the menu stays
         * open and the SHIFT LED stays lit. Why: the old code exited SHIFT+REC
         * on the entry gesture's own release (Defect 3), and the PERF branch
         * below re-enters PERFORMANCE_PAGE, which snapped the chain editor
         * back to PERF. Inputs: bh_shiftReleaseConsume, bh_shiftRecMenuActive,
         * menu_chainMenuIsOpen(). Outputs: SHIFT LED; no page change.
         * Affiliates: processPress() BUT_SHIFT (5.2).
         */
        if (bh_shiftReleaseConsume) {
            bh_shiftReleaseConsume = 0u;
            led_setValue(0u, LED_SHIFT);
            break;
        }
        if (bh_shiftRecMenuActive || menu_chainMenuIsOpen())
            break;
```
(The `_SEQUENCER_ADD_SPIKE_` unwind and mode switch that follow are
unchanged.)

### 5.4 buttonHandler.c — REC exit stays as is

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2192–2197 (`case BUT_REC: if (bh_shiftRecMenuActive) { …
buttonHandler_shiftRecMenuExit(); break; }`)
**Action:** none (verified).  REC exits without arming
`bh_shiftReleaseConsume`, because SHIFT is not involved.  The SHIFT LED is
set from `buttonHandler_getShift()` by the exit function.

### 5.5 buttonHandler.c — `buttonHandler_shiftRecMenuEnter()` comment

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1745–1760 (the `SHIFT+REC settings menu entry/exit (S080 E7).`
comment)
**Action:** MODIFY (comment only).  Replace "Accessors: processPress()
BUT_REC (entry/exit), processRelease() BUT_SHIFT (exit)." with "Accessors:
processPress() BUT_REC (entry/exit) and BUT_SHIFT (exit; S080 P2 Defect 3:
the menu latches open and SHIFT release never exits), handleModeButtons()
(exit before a mode switch)."

### 5.6 buttonHandler.c — MODE buttons close latched menus and leave the looper

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after 1401 (end of the `menu_loadInstrumentTransactionBusy()` gate in
`handleModeButtons()`, before `if (!buttonHandler_getShift() && mode ==
SELECT_MODE_LOAD_SAVE …`)
**Action:** ADD
```c
    /*
     * S080 P2: a MODE press ends latched PERF surfaces first.
     *
     * What: closes the SHIFT+REC settings menu and the chain editor, and, when
     * the mode is about to leave PERF, releases the looper (tracks resume where
     * they would have been) and clears the looper hold state. Why: both menus
     * keep page state that a mode switch would otherwise strand (a later REC
     * press would "restore" a stale page), and the SELECT/BAR rows change
     * meaning outside PERF, so their release edges would never reach the
     * looper. Inputs: bh_shiftRecMenuActive, menu_chainMenuIsOpen(), current
     * and requested mode. Outputs: menus closed; looper released.
     * Affiliates: buttonHandler_shiftRecMenuExit(), menu_chainMenuClose(),
     * buttonHandler_loopForceRelease() (Step 9).
     */
    if (bh_shiftRecMenuActive)
        buttonHandler_shiftRecMenuExit();
    if (menu_chainMenuIsOpen())
        menu_chainMenuClose();
    {
        /* Same mode arithmetic as below; SHIFT+MODE1 stays in VOICE. */
        uint8_t next;

        if (buttonHandler_getShift() && mode == SELECT_MODE_VOICE)
            next = SELECT_MODE_VOICE;
        else if (buttonHandler_getShift())
            next = (uint8_t)((mode + 4u) & 0x07u);
        else
            next = (uint8_t)(mode & 0x07u);
        if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
            next != SELECT_MODE_PERF)
            buttonHandler_loopForceRelease();
    }
```

### 5.7 buttonHandler.c — event-overflow reconcile

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2700–2721 (the `S080 C8: a lost SELECT release must not strand the
looper.` block through `bh_shiftRecMenuActive = 0u;`)
**Action:** MODIFY
```c
        /*
         * S080 C8 / P2: a lost release must not strand the looper or a latch.
         *
         * What: releases the looper (tracks resume where they would have
         * been), clears every looper hold bit, drops the BAR overlay, and
         * closes the SHIFT+REC menu and the chain editor, disarming the
         * SHIFT+PLAY chord and the SHIFT release-consume. Why: a ring overflow
         * can drop the release edge, which would otherwise leave the looper
         * engaged with no held button or a menu latched with no exit edge.
         * Inputs: event overflow detected. Outputs: looper/overlay/latch
         * state cleared. Affiliates: buttonHandler_loopForceRelease(),
         * menu_chainMenuClose(), buttonHandler_shiftRecMenuExit().
         */
        buttonHandler_loopForceRelease();
        bh_overlayActive = 0u;
        if (bh_shiftRecMenuActive)
            buttonHandler_shiftRecMenuExit();
        if (menu_chainMenuIsOpen())
            menu_chainMenuClose();
        bh_shiftReleaseConsume = 0u;
        bh_chainPlayArmed = 0u;
```

---

## Step 6 — Defect 1: queued change at instant division (and while stopped)

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 469–477 (`void seq_queueSceneChange(uint8_t scene_index,
uint8_t division)`)
**Action:** MODIFY
```c
/*
 * Arm a queued (SHIFT+SEQ) scene change (S080 F7, P2 Defect 1).
 *
 * What: records the target and qcd division and arms the queue. A queued
 * change at the instant division (qcd = ins, the power-on default) or while
 * the transport is stopped raises the fire flag at once instead of waiting for
 * an ISR boundary. Why: seq_sceneChangeDivUnits(0) is 0, and the ISR boundary
 * test requires units != 0, so an instant queue never fired (Defect 1); a
 * stopped transport has no boundaries at all. Queuing again replaces the
 * pending target; queuing the playing Scene still fires the queue action.
 * Inputs: scene_index, division (qcd value). Outputs: seq_pendingQueue*,
 * seq_pendingSceneQueue, possibly seq_sceneFireFlag. Context: foreground.
 * Accessors: buttonHandler SHIFT+SEQ (PERF). Affiliates:
 * seq_handleMasterBoundary() (deferred fire), menu_serviceDeferredSceneChange().
 */
void seq_queueSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingQueueScene = scene_index;
    seq_pendingQueueDiv = division;
    seq_pendingSceneQueue = 1u;
    if (seq_sceneChangeDivUnits(division) == 0u || !seq_running)
        seq_sceneFireFlag = 1u;
}
```

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 462–467 (`void seq_requestSceneChange(uint8_t scene_index,
uint8_t division)`)
**Action:** MODIFY
```c
/*
 * Arm a direct (SEQ) scene change at a musical division (S080 F7, P2).
 *
 * What: records the target and scd division and arms the change; the TIM3
 * ISR raises the fire flag at the boundary. A user request replaces any chain
 * fire that has not run yet, so the pending change is no longer marked as
 * chain-raised. Why: a manual switch cancels conditional changes (plan §4.5);
 * a stale chain marker would make the executor treat this user change as a
 * chain repeat. Inputs: scene_index, division (scd 1..9; 0 is executed
 * directly by the caller). Outputs: seq_pendingScene*, seq_pendingChainFire
 * cleared. Accessors: menu_perfModeSceneButtonPressed().
 */
void seq_requestSceneChange(uint8_t scene_index, uint8_t division)
{
    seq_pendingSceneIndex = scene_index;
    seq_pendingSceneDiv = division;
    seq_pendingChainFire = 0u;
    seq_pendingSceneChange = 1u;
}
```

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 491–496 (`void seq_clearPendingSceneChange(void)`)
**Action:** MODIFY, and ADD `seq_cancelPendingDirectChange()` and
`seq_pendingSceneIsChain()` after it
```c
/*
 * Clear every pending scene change (S080 F7, P2).
 *
 * What: drops the direct, queued and chain-fired pending changes and the fire
 * flag. Why: called by the foreground executor once it has consumed a fired
 * change, and by the transport-stop path. Inputs: none. Outputs: all pending
 * scene-change state zero. Accessors: menu_serviceDeferredSceneChange().
 * Affiliates: seq_cancelPendingDirectChange().
 */
void seq_clearPendingSceneChange(void)
{
    seq_pendingSceneChange = 0u;
    seq_pendingSceneQueue = 0u;
    seq_pendingChainFire = 0u;
    seq_sceneFireFlag = 0u;
}

/*
 * Cancel a pending direct or chain-fired change, keeping a queued one
 * (S080 P2).
 *
 * What: clears the deferred direct (scd) change and any chain fire. A
 * user-queued change survives; if it was armed, the fire flag is cleared and
 * the ISR re-raises it at the next qcd boundary (or at once for an instant
 * queue). Why: a manual scene switch cancels conditional changes (plan §4.5)
 * but only a transport stop cancels a queued change (plan §5.3). Inputs:
 * none. Outputs: seq_pendingSceneChange/seq_pendingChainFire/seq_sceneFireFlag
 * cleared; an instant queue re-arms its fire flag. Accessors:
 * menu_perfSceneChangeExecute(). Affiliates: seq_queueSceneChange().
 */
void seq_cancelPendingDirectChange(void)
{
    seq_pendingSceneChange = 0u;
    seq_pendingChainFire = 0u;
    seq_sceneFireFlag = 0u;
    if (seq_pendingSceneQueue &&
        (seq_sceneChangeDivUnits(seq_pendingQueueDiv) == 0u || !seq_running))
        seq_sceneFireFlag = 1u;
}

/*
 * Report whether the pending change was raised by a scene chain (S080 G7).
 *
 * Output: 1 when the fired direct change came from seq_chainFire(), else 0.
 * Accessor: menu_serviceDeferredSceneChange() (repeat vs. full change).
 */
uint8_t seq_pendingSceneIsChain(void)
{
    return (uint8_t)(seq_pendingChainFire && !seq_pendingSceneQueue);
}
```

**File:** `Core/Sequencer/sequencer.h`
**Lines:** 118–123 (scene-change API prototypes)
**Action:** ADD after `void seq_clearPendingSceneChange(void);`
```c
/* S080 P2: cancel a pending direct/chain change but keep a queued one. */
void seq_cancelPendingDirectChange(void);
/* S080 G7: 1 when the fired pending change came from a scene chain. */
uint8_t seq_pendingSceneIsChain(void);
/* S080 P2 Defect 10: queued target Scene (0..15) or 0xFF when none. */
uint8_t seq_queuedSceneTarget(void);
```
(`seq_pendingChainFire` is declared in Step 11.1; `seq_queuedSceneTarget()`
is defined in Step 8.6.)

---

## Step 7 — Defect 12: scene change restarting from bar 0 (audit result)

**Root cause (confirmed by reading the code):**
`seq_selectActivePattern()` (sequencer.c:1361–1438) never resets; it calls
`seq_realignActivePatternToMasterClock()`.  The instant fast path in
`menu_perfModeSceneButtonPressed()` (menu.c:8246–8252) calls the same
executor.  The restart came from P2's Defect 5 step 1.5, which pointed the
executor's reset check (menu.c:8276) at `PAR_BAR_RESET_MODE`.  Before S080
that parameter only drove `seq_resetBarOnPatternChange` (reset the bar
counter at a boundary pattern commit, sequencer.c:2551).  With
`bar_reset_mode=1` in settings.cfg every scene change then ran
`seq_resetToPatternStart()`, which zeroes `seq_elapsedPpqTicks`, the master
step clock and every track cursor.

**Changes:** none beyond those already scheduled:

* Step 4.3/4.4 — direct and chain changes reset only when pcr is on; queued
  changes use their own `qac` reset.  With pcr off, every change realigns.
* Step 9.7 — `seq_setStepIndexToStart()` no longer disengages the looper; a
  resetting change rebases the loop to the start of the incoming scene.

**Verification:** see Step 13 rows T12.x.

---

## Step 8 — Defect 10: queued-scene fast blink on its own LED layer

Design: the fast blink is a separate LED layer (`LED_LAYER_FAST`) for one
LED, not a blink slot.  `led_renderFromStack()` re-applies its phase, so
`led_setValue()`, `led_setBlinkLed(…, 0)` and `led_clearAllBlinkLeds()`
(all used by the per-beat PERF Scene refresh and by scene changes) can no
longer disturb it.  Menu re-derives which LED should fast-blink from the
sequencer's queue state on every `buttonHandler_tick()`.  That covers queuing,
replacing a queued scene, the change firing, transport stop and leaving PERF
without any extra call sites.

### 8.1 ledHandler.c — timing constant

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after 75 (`#define LED_BLINK_TIME_MS      250`)
**Action:** ADD
```c
/*
 * Fast blink half-period (S080 P2 Defect 10): double the standard blink rate,
 * used only by the queued-scene indication (LED_LAYER_FAST).
 */
#define LED_FAST_BLINK_TIME_MS 125
```

### 8.2 ledHandler.c — layer bit

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after 148 (`#define LED_LAYER_PULSE  (1u << 3)`)
**Action:** ADD
```c
/*
 * S080 P2 Defect 10: fast-blink layer. Priority: pulse > fast > flash >
 * blink/chase > base. It is owned by led_setFastBlinkLed() alone; blink
 * start/stop, led_clearAllBlinkLeds() and led_setValue() never clear it.
 */
#define LED_LAYER_FAST   (1u << 4)
```

### 8.3 ledHandler.c — fast-blink state

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after 172 (`static volatile uint8_t led_blinkingLeds;`)
**Action:** ADD
```c
/*
 * Queued-scene fast blink state (S080 P2 Defect 10).
 *
 * What: led_fastBlinkLed is the one logical LED carrying LED_LAYER_FAST
 * (0xFF = none); led_fastBlinkPhase is its current on/off phase;
 * led_fastBlinkNext is the time_sysTick deadline of the next toggle. Why: a
 * queued scene change flashes its SEQ LED at double the normal rate until the
 * change fires (plan §5.3). Only one Scene can be queued, so one LED is
 * enough. A dedicated layer keeps the indication independent of the eight
 * shared blink slots, which the PERF Scene refresh clears on every beat.
 * Inputs: led_setFastBlinkLed(). Outputs: LED phase via led_renderFromStack()
 * and led_tickHandler(). RAM: 4 bytes SRAM1. Affiliates:
 * menu_syncQueuedSceneLed() (the owner), LED_FAST_BLINK_TIME_MS.
 */
static uint8_t  led_fastBlinkLed   = 0xFFu;
static uint8_t  led_fastBlinkPhase = 0u;
static uint16_t led_fastBlinkNext  = 0u;
```

### 8.4 ledHandler.c — render priority

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 308–309 (`if (layers & LED_LAYER_PULSE)` `return;` in
`led_renderFromStack()`)
**Action:** ADD after them
```c
    /*
     * S080 P2 Defect 10: the queued-scene fast blink owns its LED below pulse.
     * Re-applying the phase here is what makes it survive base writes
     * (led_setValue) and blink cancellation on the same LED.
     */
    if (layers & LED_LAYER_FAST) {
        led_setValueTemp(led_fastBlinkPhase, ledNr);
        return;
    }
```
Also update the function comment's priority sentence (line 288) to
"(pulse > fast > flash > blink/chase > base)".

### 8.5 ledHandler.c — `led_clearAll()`

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after 523 (`memset(led_activeLayers, 0, sizeof(led_activeLayers));`)
**Action:** ADD
```c
    /* S080 P2 Defect 10: the fast-blink owner follows the cleared layers. */
    led_fastBlinkLed = 0xFFu;
```

### 8.6 ledHandler.c — blink must not toggle a fast-blink LED

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Lines:** 795–797 (in `led_setBlinkLed()` start path:
`!(led_activeLayers[index] & (LED_LAYER_PULSE | LED_LAYER_FLASH)))`) and
891–893 (same test in `led_tickHandler()`'s blink loop)
**Action:** MODIFY both masks to
`(LED_LAYER_PULSE | LED_LAYER_FLASH | LED_LAYER_FAST)` with the comment
```c
                    /* S080 P2: a fast-blink LED is never toggled by blink. */
```

### 8.7 ledHandler.c — fast toggle in the tick handler

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** before the closing brace of `led_tickHandler()` (after line 897)
**Action:** ADD
```c
    /*
     * S080 P2 Defect 10: queued-scene fast blink.
     *
     * What: toggles the fast-blink phase every LED_FAST_BLINK_TIME_MS and
     * re-renders its LED. Why: double-rate flashing until the queued change
     * fires. The signed-distance test is wrap-safe on the 16-bit clock.
     */
    if (led_fastBlinkLed != 0xFFu &&
        (uint16_t)(time_sysTick - led_fastBlinkNext) < 0x8000u) {
        led_fastBlinkNext = (uint16_t)(time_sysTick + LED_FAST_BLINK_TIME_MS);
        led_fastBlinkPhase ^= 1u;
        led_renderFromStack(led_fastBlinkLed);
    }
```

### 8.8 ledHandler.c — public API

**File:** `Core/Hardware/frontPanel/ledHandler.c`
**Line:** after 843 (end of `led_clearAllBlinkLeds()`)
**Action:** ADD
```c
/*
 * Set or clear the fast-blink LED (S080 P2 Defect 10).
 *
 * What: moves LED_LAYER_FAST to ledNr (0xFF = none). The previous LED drops
 * the layer and re-renders from its remaining layers/base; the new LED starts
 * in the "on" phase. Why: the queued Scene's SEQ LED flashes at double rate
 * until the change fires, independent of the per-beat Scene refresh. Inputs:
 * logical LED id or 0xFF. Outputs: layer bits, fast-blink state, LED output.
 * Accessors: menu_syncQueuedSceneLed(). Affiliates: led_renderFromStack(),
 * led_tickHandler(), led_clearAll().
 */
void led_setFastBlinkLed(uint8_t ledNr)
{
    uint8_t old = led_fastBlinkLed;
    uint8_t physLed;
    uint8_t index;

    if (ledNr != 0xFFu) {
        physLed = led_toPhysicalNumber(ledNr);
        if (physLed >= NUM_OUTS && physLed != LED_BAR1)
            ledNr = 0xFFu;
    }
    if (ledNr == old)
        return;
    led_fastBlinkLed = 0xFFu;
    if (old != 0xFFu) {
        physLed = led_toPhysicalNumber(old);
        index = (physLed == LED_BAR1) ? 40u : physLed;
        if (index < LED_LAYER_COUNT)
            led_activeLayers[index] &= (uint8_t)~LED_LAYER_FAST;
        led_renderFromStack(old);
    }
    if (ledNr != 0xFFu) {
        physLed = led_toPhysicalNumber(ledNr);
        index = (physLed == LED_BAR1) ? 40u : physLed;
        if (index < LED_LAYER_COUNT)
            led_activeLayers[index] |= LED_LAYER_FAST;
        led_fastBlinkPhase = 1u;
        led_fastBlinkNext = (uint16_t)(time_sysTick + LED_FAST_BLINK_TIME_MS);
        led_fastBlinkLed = ledNr;
        led_renderFromStack(ledNr);
    }
}

/* S080 P2 Defect 10: current fast-blink LED, or 0xFF when none. */
uint8_t led_getFastBlinkLed(void)
{
    return led_fastBlinkLed;
}

/*
 * Report whether a logical LED holds a persistent blink slot (S080 P2).
 *
 * What: returns 1 when ledNr is in the blink set. Why: the PERF Scene refresh
 * re-asserts blinks on every beat; restarting an already blinking LED (clear
 * then set) resets its phase and makes the blink stutter, so the refresh asks
 * first and leaves running blinks alone. Accessors: menu_refreshPerfSceneLeds().
 */
uint8_t led_isBlinkLed(uint8_t ledNr)
{
    int i;

    for (i = 0; i < NUM_OF_BLINKABLE_LEDS; i++) {
        if ((led_blinkingLeds & (1 << i)) && led_blinkLedNumber[i] == ledNr)
            return 1u;
    }
    return 0u;
}
```

**File:** `Core/Hardware/frontPanel/ledHandler.h`
**Line:** after 140 (`void led_clearAllBlinkLeds(void);`)
**Action:** ADD
```c
/*
 * S080 P2 Defect 10: one double-rate blinking LED on its own layer (queued
 * Scene). ledNr 0xFF clears it. Blink and base writes never disturb it.
 */
void    led_setFastBlinkLed(uint8_t ledNr);
uint8_t led_getFastBlinkLed(void);
/* S080 P2: 1 when ledNr holds a persistent blink slot. */
uint8_t led_isBlinkLed(uint8_t ledNr);
```

### 8.9 sequencer.c — queued target accessor

**File:** `Core/Sequencer/sequencer.c`
**Line:** after 489 (end of `seq_pendingSceneTarget()`)
**Action:** ADD
```c
/*
 * Queued (SHIFT+SEQ) target, independent of any direct change (S080 P2
 * Defect 10).
 *
 * Output: the queued Scene 0..15 while a queued change is armed, else 0xFF.
 * Why: the fast-blink owner must show only the queued Scene. Single-byte
 * reads of ISR-shared state. Accessor: menu_syncQueuedSceneLed().
 */
uint8_t seq_queuedSceneTarget(void)
{
    return seq_pendingSceneQueue ? seq_pendingQueueScene : 0xFFu;
}
```

### 8.10 menu.c — fast-blink owner

**File:** `Core/Menu/menu.c`
**Line:** after the new `menu_serviceDeferredSceneChange()` (Step 4.4)
**Action:** ADD
```c
/*
 * Owner of the queued-scene fast blink (S080 P2 Defect 10).
 *
 * What: in PERF mode, fast-blinks the SEQ LED of the queued Scene; otherwise
 * (no queue, queue fired, transport stopped, or another mode owns the SEQ
 * row) clears it. Why: re-deriving from sequencer state on every tick covers
 * every way a queue can start, change or end without call sites in each path.
 * Inputs: seq_queuedSceneTarget(), buttonHandler_getMode(). Outputs:
 * led_setFastBlinkLed() on change only. Accessors:
 * menu_serviceDeferredSceneChange(). Affiliates: LED_LAYER_FAST.
 */
static void menu_syncQueuedSceneLed(void)
{
    uint8_t target = seq_queuedSceneTarget();
    uint8_t want = 0xFFu;

    if (buttonHandler_getMode() == SELECT_MODE_PERF && target < 16u)
        want = (uint8_t)(LED_SEQ1 + target);
    if (led_getFastBlinkLed() != want)
        led_setFastBlinkLed(want);
}
```

### 8.11 buttonHandler.c — SHIFT+SEQ in PERF

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1240–1261 (SHIFT branch `case SELECT_MODE_PERF:`, comment ending
"…left for a later LED-layer change)." and the `led_setBlinkLed(…)` call)
**Action:** MODIFY
```c
        case SELECT_MODE_PERF:
            /*
             * S080 F9 / Phase G / P2 Defect 10: SHIFT+SEQ in PERF.
             *
             * What: with PLAY held in the SHIFT+PLAY chord, opens the chain
             * editor for this Scene (Phase G). Otherwise queues a change to
             * this Scene at the qcd division. The queued Scene's SEQ LED
             * flashes at double rate until the change fires; that LED is
             * owned by menu_syncQueuedSceneLed() on the next tick, so no blink
             * is started here. Why: queuing lets the user pre-program the
             * next Scene during performance; the chord shares the SHIFT+SEQ
             * gesture space. Inputs: seqButtonPressed, bh_chainPlayArmed,
             * PAR_SCENE_QUEUE_DIV. Outputs: menu_chainMenuOpen() or
             * seq_queueSceneChange(). Affiliates: seq_queueSceneChange()
             * (Step 6), menu_serviceDeferredSceneChange(), menu_chainMenuOpen()
             * (Step 11).
             */
            if (bh_chainPlayArmed) {
                (void)menu_chainMenuOpen(seqButtonPressed);
                break;
            }
            if (seqButtonPressed < SCENE_COUNT &&
                bank_scenePresent(seqButtonPressed)) {
                seq_queueSceneChange(seqButtonPressed,
                                     parameter_values[PAR_SCENE_QUEUE_DIV]);
            }
            break;
```

---

## Step 9 — Defects 7, 8, 14: master-clock-anchored looper

### 9.1 sequencer.h — looper declarations

**File:** `Core/Sequencer/sequencer.h`
**Lines:** 52–103 (`Loop-active flag (S080 B2/C1).` through
`void seq_loopUpdateDivision(…);`)
**Action:** MODIFY (the config arrays become private to sequencer.c; the API
gains mask results, a mask update and the loopable query)
```c
/*
 * Loop-active flag (S080 B2/C1, P2).
 *
 * What: single-byte ISR gate, 1 while the PERF looper is engaged: a looper
 * SELECT, the VOICE of a looped track, or a BAR button is held (S080 P2
 * Defect 8). It may be 1 with no looped tracks (all looped VOICEs released
 * while SELECT is held). Written by the sequencer looper API under PRIMASK;
 * read by the TIM3 ISR in seq_evaluateStepCondition() (lop/!lp), the scene
 * chain conditions (lox/lob/!lx/!lb) and the loop wrap. Single-byte reads are
 * atomic on Cortex-M7, like seq_fillActive. Inputs: seq_loopEngage()/
 * seq_loopDisengage()/transport stop. Affiliates: buttonHandler looper,
 * seq_fillActive.
 */
extern uint8_t seq_loopActive;

/*
 * Looper control API (S080 C1, rewritten S080 P2 Defects 7/8/14).
 *
 * What: seq_loopEngage() starts a loop on trackMask (filtered by the loopable
 * flags), anchoring each track's loop start on the master clock, and returns
 * the mask actually looping (0 and no engage while the transport is stopped).
 * seq_loopUpdateMask() changes the looped set while engaged: added tracks get
 * a fresh anchor, kept tracks keep theirs, dropped tracks resume where they
 * would have been; it returns the new mask. seq_loopDisengage() ends the loop
 * and resumes every looped track. seq_loopUpdateDivision() changes the loop
 * length without moving any loop start. seq_loopableTrackMask() reports the
 * tracks whose played Scene allows looping (Defect 8a). Why: the foreground
 * owns gestures and the TIM3 ISR owns step timing; all shared state is
 * published under PRIMASK. Inputs: 7-bit masks, division 0..7 (1 bar..1/128),
 * modifier 0 none / 1 dot / 2 triplet. Callers: buttonHandler.c PERF
 * SELECT/VOICE/BAR handlers.
 */
uint8_t seq_loopEngage(uint8_t trackMask, uint8_t division, uint8_t modifier);
uint8_t seq_loopUpdateMask(uint8_t newMask);
void    seq_loopDisengage(void);
void    seq_loopUpdateDivision(uint8_t division, uint8_t modifier);
uint8_t seq_loopableTrackMask(void);
```

### 9.2 sequencer.c — looper state

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 259–281 (`Loop-active flag and looper state (S080 B3/C2).`
through `static uint16_t seq_loopTickCount = 0u;`)
**Action:** MODIFY
```c
/*
 * Looper state (S080 B3/C2, rewritten S080 P2 Defects 7/8/14).
 *
 * What: seq_loopActive is the ISR gate (see sequencer.h). seq_loopTrackMask
 * selects the looping tracks; seq_loopDivision/seq_loopModifier set the loop
 * length. For every looped track, seq_loopAnchor[] is the master-clock tick
 * (seq_elapsedPpqTicks units) at which its loop window starts, and
 * seq_loopStartStep/Accum/PlayState[] are the track's step cursor, Q8.8 DDA
 * accumulator and play-state bits just before that tick's DDA advance (the
 * snapshot). The ISR restores the snapshot whenever (elapsed - anchor) is a
 * positive multiple of the loop length, before the DDA advance.
 * Why anchors, not a shared tick counter: wraps always land on the master-
 * clock grid of the anchor, so a division/modifier change cannot shift a
 * track off the grid (P1 wrapped at once when the counter already exceeded a
 * shorter length), a track added mid-loop wraps on its own window instead of
 * another track's, and a scene change can keep the anchor while recomputing
 * the snapshot for the incoming scene (Defect 14). Resuming projects the
 * snapshot forward from the anchor, so a released track lands where it would
 * have been even when its position was never the canonical master-clock
 * position (Morph/automation scale changes, boundary commits, once modes).
 * Inputs: seq_loopEngage()/UpdateMask()/Disengage()/UpdateDivision(), scene
 * change refresh, transport reset rebase. Outputs: seq_loopProcessTick() wraps.
 * Concurrency: every foreground writer holds PRIMASK while it publishes; the
 * ISR reads only while seq_loopActive is set. RAM: 4 + 7x2 + 7x2 + 7 + 7x4 =
 * 67 B SRAM1 static, runtime-only (P1: 34 B). Affiliates:
 * seq_evaluateStepCondition() (lop/!lp), seq_processSchedulerTick().
 */
uint8_t seq_loopActive = 0u;
static uint8_t  seq_loopTrackMask = 0u;
static uint8_t  seq_loopDivision  = 0u;
static uint8_t  seq_loopModifier  = 0u;
static int16_t  seq_loopStartStep[NUM_TRACKS];
static uint16_t seq_loopStartAccum[NUM_TRACKS];
static uint8_t  seq_loopStartPlayState[NUM_TRACKS];
static uint32_t seq_loopAnchor[NUM_TRACKS];
```

### 9.3 sequencer.c — remove the P1 looper functions

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 283–414 (`seq_loopLengthTicks()`, `seq_loopEngage()`,
`seq_loopDisengage()`, `seq_loopUpdateDivision()`, `seq_loopProcessTick()`
with their comments)
**Action:** REMOVE

Removal note: the engine moves below `seq_realignTrackToMasterClock()`
(9.5), because it needs `seq_trackPlayState[]`, the effective-timing cache
and `seq_stepIndexForMode()`, which are declared later in the file.

### 9.4 sequencer.c — forward declarations

**File:** `Core/Sequencer/sequencer.c`
**Line:** after 917 (`static void seq_queueStepAutomations(uint8_t track, uint8_t step);`)
**Action:** ADD
```c
/* S080 P2 looper engine (defined after seq_realignTrackToMasterClock()). */
static void seq_loopRefreshSceneSnapshots(void);
static void seq_loopRebaseToStart(void);
static void seq_loopProcessTick(void);
```

### 9.5 sequencer.c — looper engine

**File:** `Core/Sequencer/sequencer.c`
**Line:** after the closing brace of `seq_realignTrackToMasterClock()`
(≈ line 2790, before the `Deferred trigger tick-down (S078 §3.2, S079 §7.4).`
comment)
**Action:** ADD
```c
/* =====================================================================
 * S080 P2 looper engine (Defects 7, 8, 14)
 * ===================================================================== */

/*
 * Convert a looper division + modifier to a master-clock tick count (S080 C2).
 *
 * What: division 0..7 maps to 1 bar (16 x 24 = 384 PPQ ticks at 96 PPQ) down
 * to 1/128 (3 ticks); modifier 1 (dot) scales x3/2 and modifier 2 (triplet)
 * scales x2/3. Why: the loop period is measured on the master step clock, the
 * same grid used by bar-based conditions. Inputs: division 0..7 (clamped),
 * modifier 0..2. Output: tick count >= 1. Affiliates: seq_loopProcessTick().
 */
static uint16_t seq_loopLengthTicks(uint8_t division, uint8_t modifier)
{
	static const uint16_t base[8] = {
		384u, 192u, 96u, 48u, 24u, 12u, 6u, 3u
	};
	uint16_t ticks;

	if (division > 7u)
		division = 0u;
	ticks = base[division];
	if (modifier == 1u)             /* dot: +50% */
		ticks = (uint16_t)((ticks * 3u) / 2u);
	else if (modifier == 2u)        /* triplet: x2/3 */
		ticks = (uint16_t)((ticks * 2u) / 3u);
	return (ticks != 0u) ? ticks : 1u;
}

/*
 * One track's position image (S080 P2).
 *
 * What: step cursor, Q8.8 DDA accumulator and play-state bits (once-stopped,
 * pip direction) - everything seq_advanceTrackStep() needs to continue from a
 * point in time. Used for the loop snapshot and the rewind/projection maths.
 */
typedef struct {
	int16_t  step;
	uint16_t accum;
	uint8_t  play_state;
} seq_loop_pos_t;

/*
 * Move a track position by a signed number of master ticks (S080 P2).
 *
 * What: applies `ticks` PPQ ticks (negative = rewind) to the DDA accumulator
 * and walks the step cursor by the resulting number of step advances under
 * the track's play mode: fwd/rev wrap modulo the effective length (from the
 * pre-start cursor -1 the first forward step is 0 and the first reverse step
 * is len-1); pip walks its 2 x len cycle and sets the direction bit; rnd draws
 * a new random step; onc/1fr clamp at the ends and set or clear the stopped
 * bit. Why: the looper must find a track's state at a past tick (loop start,
 * D4) and where a looped track would be now (resume, D3) without keeping a
 * shadow DDA running for every track. Uses the current effective scale and
 * length for the whole span (a Morph change during the span is approximated).
 * Inputs: track 0..6, position image, signed ticks. Outputs: position image
 * updated. Context: foreground (64-bit arithmetic; never called from TIM3).
 * Accessors: seq_loopSnapshotTracks(), seq_loopResumeTrack(). Affiliates:
 * seq_advanceTrackStep() (the per-step semantics mirrored here),
 * stepScale_ticksQ8().
 */
static void seq_loopShiftPosition(uint8_t track, seq_loop_pos_t *pos,
                                  int32_t ticks)
{
	const pat_scene_region_t *region =
	    pat_sceneRegion(seq_perTrackPattern[track]);
	uint8_t len = seq_effectiveTrackLength[track];
	uint8_t mode = region ? region->track_play_mode[track] : 0u;
	int64_t interval =
	    (int64_t)stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	int64_t total;
	int64_t steps;
	int64_t rem;

	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	if (mode >= 6u)
		mode = 0u;
	total = (int64_t)pos->accum + (int64_t)ticks * 256;
	steps = total / interval;
	rem = total % interval;
	if (rem < 0) {
		rem += interval;
		steps--;
	}
	pos->accum = (uint16_t)rem;
	if (steps == 0)
		return;

	switch (mode) {
	case 1u: { /* rev */
		int64_t base = (pos->step < 0) ? 0 : pos->step;
		int64_t idx = (base - steps) % (int64_t)len;

		if (idx < 0)
			idx += len;
		pos->step = (int16_t)idx;
		break;
	}
	case 2u: { /* pip: positions 0..len-1 forward, len..2len-1 reverse */
		int64_t cycle = (int64_t)len * 2;
		int64_t p;

		if (pos->step < 0)
			p = -1;
		else if (pos->play_state & SEQ_PLAY_STATE_PIP_REV)
			p = cycle - 1 - pos->step;
		else
			p = pos->step;
		p = (p + steps) % cycle;
		if (p < 0)
			p += cycle;
		if (p < (int64_t)len) {
			pos->step = (int16_t)p;
			pos->play_state &= (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
		} else {
			pos->step = (int16_t)(cycle - 1 - p);
			pos->play_state |= SEQ_PLAY_STATE_PIP_REV;
		}
		break;
	}
	case 3u: /* rnd */
		pos->step = (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
		break;
	case 4u: /* onc */
	case 5u: { /* 1fr: virtual position len = stopped after the last step */
		int64_t v = (pos->play_state & SEQ_PLAY_STATE_STOPPED)
		    ? (int64_t)len : (int64_t)pos->step;

		v += steps;
		if (v >= (int64_t)len) {
			pos->step = (int16_t)(len - 1u);
			pos->play_state |= SEQ_PLAY_STATE_STOPPED;
		} else {
			if (v < -1)
				v = -1;
			pos->step = (int16_t)v;
			pos->play_state &= (uint8_t)~SEQ_PLAY_STATE_STOPPED;
		}
		break;
	}
	case 0u: /* fwd */
	default: {
		int64_t idx = ((int64_t)pos->step + steps) % (int64_t)len;

		if (idx < 0)
			idx += len;
		pos->step = (int16_t)idx;
		break;
	}
	}
}

/*
 * Canonical position just before a master tick's advance (S080 P2 Defect 14).
 *
 * What: the master-clock position mapping of seq_realignTrackToMasterClock()
 * (position = (tick << 8) / interval, accumulator = remainder, play mode
 * applied to the index, pip direction from the 2 x len cycle) evaluated at
 * `anchor - 1`, i.e. the state the DDA would hold just before tick `anchor`
 * advances. Anchor 0 is the transport-start seed (cursor -1, accumulator
 * interval - 256). Why: after a scene change the looped track's loop start
 * stays at the same master-clock tick; this computes the incoming Scene's
 * content at that tick with the incoming Scene's own scale and length (plan
 * §3.6 corrected). Inputs: track, anchor tick. Output: position image.
 * Accessors: seq_loopRefreshSceneSnapshots(). Affiliates:
 * seq_stepIndexForMode(), seq_realignTrackToMasterClock().
 */
static void seq_loopCanonicalPreAdvance(uint8_t track, uint32_t anchor,
                                        seq_loop_pos_t *pos)
{
	const pat_scene_region_t *region =
	    pat_sceneRegion(seq_perTrackPattern[track]);
	uint8_t len = seq_effectiveTrackLength[track];
	uint8_t mode = region ? region->track_play_mode[track] : 0u;
	uint16_t interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	uint64_t master_q8;
	uint64_t position;

	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	if (mode >= 6u)
		mode = 0u;
	pos->play_state = 0u;
	if (anchor == 0u) {
		pos->step = -1;
		pos->accum = (uint16_t)(interval - 256u);
		return;
	}
	master_q8 = (uint64_t)(anchor - 1u) << 8u;
	position = master_q8 / interval;
	pos->step = seq_stepIndexForMode(mode, (uint32_t)position, len);
	pos->accum = (uint16_t)(master_q8 % interval);
	if (mode == 2u && (position % ((uint64_t)len * 2u)) >= len)
		pos->play_state = SEQ_PLAY_STATE_PIP_REV;
}

/*
 * Loop start tick for an engage / add event (S080 plan §3.4, P2 D4).
 *
 * What: with recording quantisation on (qnt 8/16/32/64), the last quantise
 * boundary on the master clock (it may lie more than one loop length back:
 * the track then jumps back at the next wrap, plan §3.4 note). With qnt off,
 * the tick at which the fastest track of `mask` last advanced, i.e.
 * now - floor(accumulator / 256). Before the first scheduler tick the anchor
 * is 0. Why: the loop must start at the beginning of a step, not wherever the
 * button landed, so the first repeat plays the step that was sounding.
 * Inputs: mask (non-zero), seq_elapsedPpqTicks, seq_quantisation, the DDA
 * accumulators. Output: anchor tick <= now. Context: foreground under
 * PRIMASK. Accessors: seq_loopSnapshotTracks().
 */
static uint32_t seq_loopEventAnchor(uint8_t mask)
{
	uint32_t now = seq_elapsedPpqTicks;
	uint16_t q;
	uint16_t best = 0xFFFFu;
	uint8_t fastest = 0u;
	uint8_t track;
	uint32_t back;

	if (seq_initialSchedulerTick)
		return 0u;
	switch (seq_quantisation) {
	case QUANT_8:  q = 48u; break;
	case QUANT_16: q = 24u; break;
	case QUANT_32: q = 12u; break;
	case QUANT_64: q = 6u;  break;
	default:       q = 0u;  break;
	}
	if (q != 0u)
		return now - (now % q);
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint16_t interval;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
		if (interval < best) {
			best = interval;
			fastest = track;
		}
	}
	back = (uint32_t)(seq_trackAccumulator[fastest] >> 8u);
	return (back > now) ? 0u : (now - back);
}

/*
 * Snapshot the tracks of one engage / add event (S080 P2 Defect 7, D4).
 *
 * What: computes one anchor for the event and, for every track in `mask`,
 * rewinds its live state to just before that anchor's advance and stores it as
 * the loop snapshot. Why: tracks added in the same gesture share one loop
 * start; tracks already looping are untouched. Inputs: mask, live DDA state.
 * Outputs: seq_loopAnchor/StartStep/StartAccum/StartPlayState for `mask`.
 * Context: foreground under PRIMASK. Accessors: seq_loopEngage(),
 * seq_loopUpdateMask().
 */
static void seq_loopSnapshotTracks(uint8_t mask)
{
	uint32_t anchor;
	int32_t back;
	uint8_t track;

	if (mask == 0u)
		return;
	anchor = seq_loopEventAnchor(mask);
	back = seq_initialSchedulerTick
	    ? 0 : -(int32_t)(seq_elapsedPpqTicks - anchor + 1u);
	for (track = 0u; track < NUM_TRACKS; track++) {
		seq_loop_pos_t pos;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		pos.step = seq_stepIndex[track];
		pos.accum = seq_trackAccumulator[track];
		pos.play_state = seq_trackPlayState[track];
		seq_loopShiftPosition(track, &pos, back);
		seq_loopAnchor[track] = anchor;
		seq_loopStartStep[track] = pos.step;
		seq_loopStartAccum[track] = pos.accum;
		seq_loopStartPlayState[track] = pos.play_state;
	}
}

/*
 * Resume one track where it would have been without the loop (S080 P2 D3).
 *
 * What: projects the track's loop snapshot (state just before its anchor
 * tick) forward to the current tick and writes it to the live cursor,
 * accumulator and play state, then refreshes the chase LED for the selected
 * voice. Why: dropped or released tracks must stay in time with the tracks
 * that never looped; projecting from the snapshot is exact even when the
 * track was not on its canonical master-clock position (the P2 realign was
 * not). After a scene change the snapshot is the incoming Scene's canonical
 * position, so the projection equals the incoming Scene's master-clock
 * position. Inputs: track, snapshot, anchor, seq_elapsedPpqTicks. Outputs:
 * live track state. Context: foreground under PRIMASK. Accessors:
 * seq_loopUpdateMask(), seq_loopDisengage().
 */
static void seq_loopResumeTrack(uint8_t track)
{
	seq_loop_pos_t pos;
	uint32_t ahead;

	pos.step = seq_loopStartStep[track];
	pos.accum = seq_loopStartAccum[track];
	pos.play_state = seq_loopStartPlayState[track];
	ahead = seq_initialSchedulerTick
	    ? 0u : (seq_elapsedPpqTicks - seq_loopAnchor[track] + 1u);
	if (ahead > 0x7FFFFFFFu)
		ahead = 0x7FFFFFFFu;
	seq_loopShiftPosition(track, &pos, (int32_t)ahead);
	seq_stepIndex[track] = pos.step;
	seq_trackAccumulator[track] = pos.accum;
	seq_trackPlayState[track] = pos.play_state;
	seq_lastMasterStep[track] = (uint8_t)((pos.step < 0) ? 0 : pos.step);
	if (track == menu_getActiveVoice()) {
		seq_ledState.chaseStep = seq_stepIndex[track];
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	}
}

uint8_t seq_loopEngage(uint8_t trackMask, uint8_t division, uint8_t modifier)
{
	uint32_t primask;
	uint8_t mask;

	/*
	 * Start a loop (S080 C2, P2 D3/D4/D17).
	 *
	 * What: filters trackMask by the loopable flags, snapshots the tracks at
	 * one anchor, publishes division/modifier/mask and sets seq_loopActive
	 * last, all under PRIMASK so the TIM3 ISR never sees a partial loop.
	 * Recording is turned off first (looping and recording are mutually
	 * exclusive, plan §9.2-2). Engages only while the transport runs; returns
	 * the mask now looping (0 when stopped). Inputs: 7-bit mask, division
	 * 0..7, modifier 0..2. Outputs: looper state. Accessors: buttonHandler
	 * SELECT press (PERF). Affiliates: seq_loopableTrackMask(),
	 * seq_loopSnapshotTracks().
	 */
	if (!seq_running)
		return 0u;
	if (division > 7u)
		division = 0u;
	if (modifier > 2u)
		modifier = 0u;
	if (seq_recordActive)
		seq_setRecordingMode(0u);
	mask = (uint8_t)(trackMask & seq_loopableTrackMask() & 0x7Fu);
	primask = __get_PRIMASK();
	__disable_irq();
	seq_loopActive = 0u;
	seq_loopDivision = division;
	seq_loopModifier = modifier;
	seq_loopSnapshotTracks(mask);
	seq_loopTrackMask = mask;
	seq_loopActive = 1u;            /* final gate */
	__set_PRIMASK(primask);
	return mask;
}

uint8_t seq_loopUpdateMask(uint8_t newMask)
{
	uint32_t primask;
	uint8_t old;
	uint8_t track;

	/*
	 * Change the looped track set while engaged (S080 P2 Defect 7, D1/D2).
	 *
	 * What: tracks leaving the set resume where they would have been (inside
	 * this call, before the mask is committed - the P2 ordering bug); tracks
	 * staying keep their anchors and snapshots; tracks joining get one fresh
	 * anchor for this event. Division and modifier are untouched. Inputs:
	 * desired 7-bit mask (filtered by the loopable flags). Outputs: looper
	 * state; returns the committed mask (0 when not engaged). Context:
	 * foreground; PRIMASK held for the whole update. Accessors: buttonHandler
	 * VOICE press/release (PERF). Affiliates: seq_loopResumeTrack(),
	 * seq_loopSnapshotTracks().
	 */
	newMask = (uint8_t)(newMask & seq_loopableTrackMask() & 0x7Fu);
	primask = __get_PRIMASK();
	__disable_irq();
	if (!seq_loopActive) {
		__set_PRIMASK(primask);
		return 0u;
	}
	old = seq_loopTrackMask;
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t bit = (uint8_t)(1u << track);

		if ((old & bit) != 0u && (newMask & bit) == 0u)
			seq_loopResumeTrack(track);
	}
	seq_loopSnapshotTracks((uint8_t)(newMask & (uint8_t)~old));
	seq_loopTrackMask = newMask;
	__set_PRIMASK(primask);
	return newMask;
}

void seq_loopDisengage(void)
{
	uint32_t primask;
	uint8_t mask;
	uint8_t track;

	/*
	 * End the loop (S080 C2, P2 D3).
	 *
	 * What: clears the gate and the mask, then resumes every looped track
	 * where it would have been, under PRIMASK. With the transport stopped
	 * there is nothing to resume (the stop already reset every cursor).
	 * Accessors: buttonHandler_loopCheckDisengage()/loopForceRelease().
	 */
	primask = __get_PRIMASK();
	__disable_irq();
	mask = seq_loopTrackMask;
	seq_loopActive = 0u;
	seq_loopTrackMask = 0u;
	if (seq_running) {
		for (track = 0u; track < NUM_TRACKS; track++) {
			if ((mask & (uint8_t)(1u << track)) != 0u)
				seq_loopResumeTrack(track);
		}
	}
	__set_PRIMASK(primask);
}

void seq_loopUpdateDivision(uint8_t division, uint8_t modifier)
{
	/*
	 * Change the loop length (S080 C2; P2 Defect 7: never moves a loop
	 * start). Single-byte stores; the ISR picks up the new length on its next
	 * tick and keeps wrapping on each track's anchor grid. Accessors:
	 * buttonHandler SELECT press/release and BAR press/release (PERF).
	 */
	if (division > 7u)
		division = 0u;
	if (modifier > 2u)
		modifier = 0u;
	seq_loopDivision = division;
	seq_loopModifier = modifier;
}

/*
 * Recompute loop snapshots for a newly selected Scene (S080 P2 Defect 14).
 *
 * What: keeps every looped track's anchor tick and replaces its snapshot with
 * the incoming Scene's canonical position just before that tick (its own
 * scale, length and play mode). Example: outgoing 1/16 loop start at step 5
 * (tick 96), incoming 1/8 -> step 3. Why: the loop start stays at the same
 * master-clock position across scene changes (plan §3.6 corrected). The caller
 * has just refreshed the effective-timing cache and realigned every track.
 * Inputs: seq_loopAnchor[], effective cache. Outputs: snapshots. Context:
 * foreground; caller holds PRIMASK. Accessors: seq_selectActivePattern(),
 * seq_alignActivePatternToScene(), seq_chainRepeatSelf().
 */
static void seq_loopRefreshSceneSnapshots(void)
{
	uint8_t track;

	if (!seq_loopActive)
		return;
	for (track = 0u; track < NUM_TRACKS; track++) {
		seq_loop_pos_t pos;

		if ((seq_loopTrackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_loopCanonicalPreAdvance(track, seq_loopAnchor[track], &pos);
		seq_loopStartStep[track] = pos.step;
		seq_loopStartAccum[track] = pos.accum;
		seq_loopStartPlayState[track] = pos.play_state;
	}
}

/*
 * Rebase an engaged loop to the start (S080 P2 Defect 14, decided).
 *
 * What: every looped track's anchor becomes tick 0 and its snapshot the
 * freshly seeded start state (cursor -1, accumulator interval - 256). Why: a
 * resetting scene change (pcr on for direct/chain changes, a qac reset for
 * queued ones) or an external/boundary reset restarts the master clock, so
 * the old anchor ticks no longer exist; the loop start becomes the beginning
 * of the incoming Scene. Inputs: seeded live state. Outputs: anchors and
 * snapshots. Context: called at the end of seq_setStepIndexToStart() (TIM3 or
 * foreground). The transport-stop path clears seq_loopActive first, so this is
 * a no-op there.
 */
static void seq_loopRebaseToStart(void)
{
	uint8_t track;

	if (!seq_loopActive)
		return;
	for (track = 0u; track < NUM_TRACKS; track++) {
		if ((seq_loopTrackMask & (uint8_t)(1u << track)) == 0u)
			continue;
		seq_loopAnchor[track] = 0u;
		seq_loopStartStep[track] = seq_stepIndex[track];
		seq_loopStartAccum[track] = seq_trackAccumulator[track];
		seq_loopStartPlayState[track] = seq_trackPlayState[track];
	}
}

/*
 * Looper wrap (S080 C3, rewritten S080 P2 D3).
 *
 * What: for every looped track whose (elapsed - anchor) is a positive
 * multiple of the loop length, restores the snapshot. Called once per master
 * tick before the DDA advance, so the step that fires at the anchor tick is
 * part of the loop and replays on every wrap. Why: in the TIM3 ISR so tight
 * divisions (1/64, 1/128) loop sample-accurately; per-track anchors keep
 * every wrap on the anchor's master-clock grid. Inputs: seq_loopActive,
 * division/modifier, mask, anchors, snapshots, seq_elapsedPpqTicks. Outputs:
 * seq_stepIndex[]/seq_trackAccumulator[]/seq_trackPlayState[] of wrapped
 * tracks. Context: TIM3 ISR. Affiliates: seq_processSchedulerTick().
 */
static void seq_loopProcessTick(void)
{
	uint16_t loop_ticks;
	uint8_t mask;
	uint8_t track;

	if (!seq_loopActive)
		return;
	mask = seq_loopTrackMask;
	if (mask == 0u)
		return;
	loop_ticks = seq_loopLengthTicks(seq_loopDivision, seq_loopModifier);
	for (track = 0u; track < NUM_TRACKS; track++) {
		uint32_t since;

		if ((mask & (uint8_t)(1u << track)) == 0u)
			continue;
		if (seq_loopAnchor[track] >= seq_elapsedPpqTicks)
			continue;
		since = seq_elapsedPpqTicks - seq_loopAnchor[track];
		if ((since % loop_ticks) != 0u)
			continue;
		seq_stepIndex[track] = seq_loopStartStep[track];
		seq_trackAccumulator[track] = seq_loopStartAccum[track];
		seq_trackPlayState[track] = seq_loopStartPlayState[track];
	}
}
```

### 9.6 sequencer.c — scheduler: wrap before the DDA advance

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 2914–2958 (from `/* Shuffle deferral tick-down runs before this
tick's DDA advance. */` through the P1 `seq_loopProcessTick();` call after
the DDA loop)
**Action:** MODIFY
```c
	/*
	 * S080 G7: scene chain boundaries (bar / repetition) are evaluated on the
	 * master clock before any track moves (Step 11).
	 */
	seq_chainServiceTick();

	/* Shuffle deferral tick-down runs before this tick's DDA advance. */
	seq_processDeferredTriggers();

	/*
	 * S080 G11: a chain `stp` freezes every track in place (transport keeps
	 * running, deferred triggers and rolls finish). Neither the loop wrap nor
	 * the DDA advance runs while frozen.
	 */
	if (!seq_chainStopped) {
		/*
		 * S080 P2 D3: looper wrap BEFORE the DDA advance, so a wrap at tick T
		 * restores the pre-advance snapshot and this tick's advance replays
		 * the anchor tick's step (P1 wrapped after the advance and replayed
		 * the step after the one that was sounding).
		 */
		seq_loopProcessTick();

		/* (existing S078 §2.4.1 per-track DDA comment and loop unchanged) */
		if (seq_initialSchedulerTick == 0u) {
			for (track = 0u; track < NUM_TRACKS; track++) {
				uint8_t cc = seq_effectiveTrackScale[track];
				uint16_t interval = stepScale_ticksQ8(cc);

				seq_trackAccumulator[track] += 256u;
				while (seq_trackAccumulator[track] >= interval) {
					seq_trackAccumulator[track] -= interval;
					seq_advanceTrackStep(track);
					anyAdvanced = 1u;
				}
			}
		}
	}
```
and REMOVE the P1 block after the DDA loop:
```c
    /*
     * S080 C3: master-clock loop wrap. Runs after the per-track DDA advance so
     * ...
     */
    seq_loopProcessTick();
```
(`seq_chainServiceTick()` and `seq_chainStopped` are provided by Step 11.)
Keep the existing comments inside the DDA loop verbatim; only the
surrounding `if (!seq_chainStopped) { … }` and the moved wrap are new.

### 9.7 sequencer.c — transport reset rebases instead of disengaging

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 3567–3569 (`/* S080 C9: a full sequencer reset invalidates any
prior loop config. */` `seq_loopActive = 0u;`)
**Action:** REMOVE

**Line:** before the closing brace of `seq_setStepIndexToStart()` (after the
FX DDA seed block, line ≈ 3622)
**Action:** ADD
```c
	/*
	 * S080 P2 Defect 14: an engaged loop survives a cursor reset.
	 *
	 * What: rebase every looped track to the seeded start (anchor 0). Why: a
	 * resetting scene change, an external reset or a boundary pattern commit
	 * restarts the master clock; the loop start becomes the beginning of the
	 * Scene instead of the loop dying with stale buttonHandler state (P1
	 * cleared seq_loopActive here). Transport stop clears seq_loopActive before
	 * calling this, so a stop still ends the loop. Affiliates:
	 * seq_loopRebaseToStart(), seq_setRunning().
	 */
	seq_loopRebaseToStart();
```

**Lines:** 3170–3171 in `seq_setRunning()` (`/* S080 B5: the looper is
runtime-only; drop it on transport stop. */` `seq_loopActive = 0u;`)
**Action:** MODIFY
```c
		/*
		 * S080 B5 / P2: the looper is runtime-only; a transport stop ends it
		 * (before seq_setStepIndexToStart(), so no rebase happens). The
		 * buttonHandler hold state is reconciled on the next looper button
		 * edge (seq_loopActive reads 0).
		 */
		seq_loopActive = 0u;
		seq_loopTrackMask = 0u;
```

### 9.8 sequencer.c — scene changes keep loop anchors

**File:** `Core/Sequencer/sequencer.c`
**Lines:** 1419–1421 in `seq_selectActivePattern()` (`/* S078 §5.4: the
played Scenes changed, so rebuild the effective cache. */`
`seq_refreshTrackEffectiveParams();` `seq_realignActivePatternToMasterClock();`)
**Action:** MODIFY
```c
	/* S078 §5.4: the played Scenes changed, so rebuild the effective cache. */
	seq_refreshTrackEffectiveParams();
	{
		/*
		 * S080 P2 Defect 14: realign and re-snapshot the looper atomically.
		 *
		 * What: every track moves to its master-clock position in the new
		 * Scene, and every looped track keeps its loop-start tick while its
		 * snapshot is recomputed for the new Scene. Why: the TIM3 ISR must
		 * never wrap a track to the outgoing Scene's snapshot after the
		 * realign. Affiliates: seq_loopRefreshSceneSnapshots().
		 */
		uint32_t primask = __get_PRIMASK();

		__disable_irq();
		seq_realignActivePatternToMasterClock();
		seq_loopRefreshSceneSnapshots();
		__set_PRIMASK(primask);
	}
```

**Lines:** 1485–1487 in `seq_alignActivePatternToScene()`
(`seq_refreshTrackEffectiveParams();` `seq_realignActivePatternToMasterClock();`)
**Action:** MODIFY the same way (same block and comment, with "Bank Load
alignment" in place of "new Scene").

### 9.9 buttonHandler.c — looper hold state

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 270–287 (`Looper foreground state (S080 C5).` block)
**Action:** MODIFY
```c
/*
 * Looper foreground state (S080 C5, P2 Defects 7/8).
 *
 * What: bh_loopSelectHeldMask - SELECT buttons (1..8) held as looper
 * divisions. bh_loopLastSelectButton - the most recently pressed SELECT (owns
 * the active division, last-pressed-wins). bh_loopTrackMask - the track set
 * the sequencer actually loops (returned by seq_loopEngage()/UpdateMask()).
 * bh_loopBarModifier - momentary BAR modifier (0 none, 1 dot, 2 triplet).
 * bh_loopBarHeld - BAR1 (bit 0) / BAR2 (bit 1) physically held in PERF,
 * tracked in event order. Why: the loop stays engaged while any SELECT, any
 * VOICE of a looped track, or any BAR is held (Defect 8); event-order BAR
 * state replaces btn_held[], which the ISR updates ahead of the queued edges.
 * Inputs: SELECT/VOICE/BAR press/release in PERF. Outputs: seq_loop*() calls,
 * SELECT/BAR LEDs. RAM: +5 bytes SRAM1 (S080 P2 adds bh_loopBarHeld).
 * Affiliates: buttonHandler_loopCheckDisengage(), buttonHandler_loopForceRelease(),
 * buttonHandler_loopSelectLed().
 */
static uint8_t bh_loopSelectHeldMask   = 0u;
static uint8_t bh_loopLastSelectButton = 0u;
static uint8_t bh_loopTrackMask        = 0u;
static uint8_t bh_loopBarModifier      = 0u;
static uint8_t bh_loopBarHeld          = 0u;
```

### 9.10 buttonHandler.c — looper helpers

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** before 1193 (the comment above `buttonHandler_perfVoiceReleased()`)
**Action:** ADD `buttonHandler_loopSelectLed()` (Step 2.3) and:
```c
/*
 * End the loop when nothing sustains it any more (S080 P2 Defect 8).
 *
 * What: returns while any looper SELECT, any VOICE of a still-looped track,
 * or any BAR button is held. Otherwise disengages the loop (every looped track
 * resumes where it would have been), clears the modifier and track mirror, and
 * restores the SELECT row (dark) and the BAR overlay LEDs. Why: the loop must
 * survive a SELECT release while a looped VOICE or a BAR is still held (plan
 * Defect 8 example). Inputs: bh_loopSelectHeldMask, perf_heldVoiceMask,
 * bh_loopTrackMask, bh_loopBarHeld. Outputs: seq_loopDisengage(), LEDs.
 * Accessors: SELECT, VOICE and BAR release paths. Affiliates:
 * menu_syncPerfSelectLed(), bh_overlayActive.
 */
static void buttonHandler_loopCheckDisengage(void)
{
    if (bh_loopSelectHeldMask != 0u)
        return;
    if ((perf_heldVoiceMask & bh_loopTrackMask) != 0u)
        return;
    if (bh_loopBarHeld != 0u)
        return;
    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
    if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
        menu_syncPerfSelectLed();
        led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
        led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
    }
}

/*
 * Force the looper off and forget every hold (S080 P2).
 *
 * What: clears the SELECT and BAR hold state, disengages the loop (tracks
 * resume where they would have been) and clears the mirrors. Why: leaving
 * PERF and an event-ring overflow both lose the release edges that would end
 * the loop normally. Accessors: handleModeButtons(),
 * buttonHandler_processEvents() overflow path.
 */
static void buttonHandler_loopForceRelease(void)
{
    bh_loopSelectHeldMask = 0u;
    bh_loopBarHeld = 0u;
    if (seq_loopActive)
        seq_loopDisengage();
    bh_loopTrackMask = 0u;
    bh_loopBarModifier = 0u;
}
```

### 9.11 buttonHandler.c — VOICE release drops a looped track

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1205–1211 (`buttonHandler_perfVoiceReleased()` up to
`perf_heldVoiceMask = (uint8_t)(perf_heldVoiceMask & ~bit);`)
**Action:** ADD after that line
```c
    /*
     * S080 P2 Defect 8 (D1): releasing a looped track's VOICE drops it.
     *
     * What: the track leaves the loop at once and resumes where it would have
     * been (inside seq_loopUpdateMask(), before the mask is committed); the
     * other looped tracks keep their loop starts. Then the loop ends if
     * nothing sustains it any more. Why: real-time control of which tracks
     * loop (Defect 8 follow-up). The mute toggle below is already suppressed
     * because the loop gesture set perf_voiceActionOccurred. Inputs: bit,
     * bh_loopTrackMask, seq_loopActive. Outputs: seq_loopUpdateMask(),
     * possibly seq_loopDisengage(). Affiliates:
     * buttonHandler_loopCheckDisengage().
     */
    if (seq_loopActive && (bh_loopTrackMask & bit) != 0u) {
        bh_loopTrackMask =
            seq_loopUpdateMask((uint8_t)(bh_loopTrackMask & (uint8_t)~bit));
        buttonHandler_loopCheckDisengage();
    }
```

### 9.12 buttonHandler.c — SELECT press (PERF)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1623–1675 (`case SELECT_MODE_PERF:` block in
`handleSelectButton()`, ending `led_setValue(0u, LED_BAR2); break; }`)
**Action:** MODIFY
```c
    case SELECT_MODE_PERF:
    {
        /*
         * S080 C4 / P2: SELECT buttons are the PERF performance looper.
         *
         * What:       SELECT1..8 select the loop division (0 = 1 bar ..
         *             7 = 1/128). The first press engages the loop: on the
         *             held VOICE tracks (VOICE then SELECT) or on all seven
         *             tracks, filtered by the loopable flags. Later presses
         *             only change the division (last pressed wins) - loop
         *             starts never move (Defect 7). While the transport is
         *             stopped the press only lights its LED (D17).
         * Why:        SELECT is otherwise unused in PERF.
         * Inputs:     selectNr 0..7, perf_heldVoiceMask, seq_loopActive.
         * Outputs:    seq_loopEngage()/seq_loopUpdateDivision(); the pressed
         *             SELECT LED lit; REC forced off; BAR LEDs off.
         * Accessors:  handleSelectButton() PERF case (here); the release half
         *             is buttonHandler_partButtonReleased().
         * Affiliates: seq_loopEngage(), buttonHandler_loopCheckDisengage().
         */
        uint8_t division = selectNr;

        bh_loopSelectHeldMask |= (uint8_t)(1u << selectNr);
        bh_loopLastSelectButton = selectNr;
        if (!seq_loopActive) {
            uint8_t trackMask = (perf_heldVoiceMask != 0u)
                ? perf_heldVoiceMask : 0x7Fu;

            bh_loopBarModifier = 0u;
            bh_loopTrackMask = seq_loopEngage(trackMask, division,
                                              bh_loopBarModifier);
        } else {
            seq_loopUpdateDivision(division, bh_loopBarModifier);
        }
        /* Looping and recording are mutually exclusive (S080 §3, C7). */
        if (seq_loopActive && bh_state.seqRecording) {
            bh_state.seqRecording = 0u;
            led_setValue(0u, LED_REC);
        }
        led_setActiveSelectButton(selectNr);
        /* S080 §3.3: both BAR LEDs are forced off while the looper runs. */
        led_setValue(0u, LED_BAR1);
        led_setValue(0u, LED_BAR2);
        break;
    }
```

### 9.13 buttonHandler.c — SELECT release (PERF)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1688–1737 (the `S080 C6: PERF looper release.` comment and its
`if (bh_state.selectButtonMode == SELECT_MODE_PERF && …) { … }` block)
**Action:** MODIFY
```c
    /*
     * S080 C6 / P2 Defect 8: PERF looper SELECT release.
     *
     * What:      clears this SELECT's hold bit. If other SELECTs remain, the
     *            division moves to the highest remaining one when the
     *            released button owned it. When the last SELECT is released
     *            the loop ends only if no looped VOICE and no BAR is held;
     *            otherwise it keeps running and the SELECT row keeps showing
     *            its division.
     * Why:       the loop is sustained by SELECT, looped VOICE or BAR holds
     *            (Defect 8). SHIFT+SELECT never sets a hold bit, so its
     *            release skips this block.
     * Inputs:    partNr 0..7, bh_loopSelectHeldMask.
     * Outputs:   seq_loopUpdateDivision(), possibly seq_loopDisengage(),
     *            SELECT LED.
     * Accessors: processRelease() SELECT routing.
     * Affiliates: buttonHandler_loopCheckDisengage(), menu_syncPerfSelectLed().
     */
    if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
        (bh_loopSelectHeldMask & (uint8_t)(1u << partNr)) != 0u) {
        uint8_t bit = (uint8_t)(1u << partNr);

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
            if (seq_loopActive)
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
            led_setActiveSelectButton(bh_loopLastSelectButton);
        } else {
            buttonHandler_loopCheckDisengage();
            if (seq_loopActive)
                led_setActiveSelectButton(bh_loopLastSelectButton);
        }
    }
```
(The `buttonHandler_TimerActionOccured()` tail that follows is unchanged.)

### 9.14 buttonHandler.c — VOICE press while looping

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 1872–1897 (the `S080 D1: VOICE pressed while the looper is
engaged swaps the loop track subset.` comment and its `if` block)
**Action:** MODIFY
```c
        /*
         * S080 D1 / P2 Defect 7: VOICE pressed while a looper SELECT is held.
         *
         * What:      the loop moves to the set of currently held VOICE tracks
         *            (including this one): tracks leaving it resume where
         *            they would have been, tracks already looping keep their
         *            loop starts, new tracks get a fresh loop start at this
         *            press. Why: real-time track swapping without disturbing
         *            the tracks that stay (Defect 7). Inputs: voiceNr,
         *            seq_loopActive, bh_loopSelectHeldMask, perf_heldVoiceMask.
         *            Outputs: seq_loopUpdateMask(); the press never toggles a
         *            mute. Affiliates: buttonHandler_perfVoiceReleased().
         */
        if (seq_loopActive && bh_loopSelectHeldMask != 0u) {
            perf_heldVoiceMask |= (uint8_t)(1u << voiceNr);
            bh_loopTrackMask = seq_loopUpdateMask(perf_heldVoiceMask);
            perf_voiceActionOccurred = 1u;
            dblclick_cancel();
            return;
        }
```

### 9.15 buttonHandler.c — BAR press (PERF)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2285–2290 (BAR1: `if (seq_loopActive && bh_loopSelectHeldMask
!= 0u) { bh_loopBarModifier = 2u; … break; }`) and 2318–2323 (BAR2, dot)
**Action:** MODIFY both.  BAR1:
```c
            /*
             * S080 E9 / P2 Defect 8: BAR1 hold is tracked in event order (it
             * can sustain the loop) and, while the loop is engaged, is the
             * momentary triplet modifier (x2/3). Loop starts never move.
             */
            bh_loopBarHeld |= 0x01u;
            if (seq_loopActive) {
                bh_loopBarModifier = 2u;            /* triplet */
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                break;
            }
```
BAR2 is the same with `bh_loopBarHeld |= 0x02u;` and `bh_loopBarModifier
= 1u; /* dot */`.  (The engaged test is `seq_loopActive` alone, so the
modifiers also work while the loop is sustained by VOICE or BAR after the
SELECT release.)

### 9.16 buttonHandler.c — BAR release (PERF)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Lines:** 2543–2571 (`case BUT_BAR1:` and `case BUT_BAR2:` in
`processRelease()`)
**Action:** MODIFY
```c
    case BUT_BAR1:
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /*
             * S080 E8/E9, P2 Defect 8: BAR1 release in PERF.
             *
             * What: clears the BAR1 hold. While looping, the modifier falls
             * back to the other BAR (dot) or none, the BAR1 LED goes dark,
             * and the loop ends if nothing sustains it any more. Otherwise
             * the BAR LED shows the overlay toggle. Why: BAR holds sustain
             * the loop (Defect 8); P1 left the BAR LED lit after a modifier
             * release. Inputs: bh_loopBarHeld, seq_loopActive. Outputs:
             * seq_loopUpdateDivision(), LED, possibly seq_loopDisengage().
             * Affiliates: buttonHandler_loopCheckDisengage().
             */
            bh_loopBarHeld &= (uint8_t)~0x01u;
            if (seq_loopActive) {
                bh_loopBarModifier = (bh_loopBarHeld & 0x02u) ? 1u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                led_setValue(0u, LED_BAR1);
                buttonHandler_loopCheckDisengage();
                break;
            }
            led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1);
            break;
        }
        led_setValue(0, LED_BAR1);
        break;

    case BUT_BAR2:
        if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
            /* S080 P2 Defect 8: BAR2 release, mirror of BAR1 above. */
            bh_loopBarHeld &= (uint8_t)~0x02u;
            if (seq_loopActive) {
                bh_loopBarModifier = (bh_loopBarHeld & 0x01u) ? 2u : 0u;
                seq_loopUpdateDivision(bh_loopLastSelectButton,
                                       bh_loopBarModifier);
                led_setValue(0u, LED_BAR2);
                buttonHandler_loopCheckDisengage();
                break;
            }
            led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2);
            break;
        }
        led_setValue(0, LED_BAR2);
        break;
```

---

## Step 10 — Defect 8a: per-track loopable parameter

### 10.1 PatternData.h — region field and size assert

**File:** `Core/Bank/Scene/Pattern/PatternData.h`
**Line:** after 110 (`uint8_t track_play_mode[NUM_TRACKS];`)
**Action:** ADD
```c
    /*
     * Per-track looper exclusion (S080 P2 Defect 8a).
     *
     * What: bit N set = track N is never captured by the PERF looper ("lop"
     * off); 0 = loopable (the default). Stored inverted so a zero-filled
     * region or an older PAT4 file reads as "all loopable". Why: a persistent
     * opt-out for utility tracks. Inputs: STEP track page edits, Scene/Pattern
     * loads, copy/clear. Outputs: seq_loopableTrackMask(). Storage: byte 4 of
     * each track's 16-byte PAT4 track header (0/1 per track; the bytes were
     * reserved and zero, so the file version is unchanged). RAM: 1 byte per
     * region (17 B across pat_regions[16] and pat_background_region).
     * Affiliates: pat_setTrackLoopable()/pat_trackLoopable(), filesystem.c
     * PAT4 writer/readers.
     */
    uint8_t track_loop_exclude;
```

**Lines:** 115–118 (`_Static_assert(sizeof(pat_scene_region_t) == … 512u + 30u, …`)
**Action:** MODIFY `512u + 30u` → `512u + 31u`, and the message to
`"pat_scene_region_t size must match the Pattern budget (S080 P2: +1 B loop exclude)"`.

**Line:** after 435 (`void pat_setTrackPlayMode(…);`)
**Action:** ADD
```c
/*
 * Per-track looper opt-out (S080 P2 Defect 8a).
 *
 * pat_setTrackLoopable() stores loopable = 1 (default) or 0 for one track of a
 * resident Scene and marks the Pattern dirty; pat_trackLoopable() returns 1
 * when the track may be looped (also for invalid coordinates). Clients: Menu
 * STEP track page, copy/clear, seq_loopableTrackMask().
 */
void pat_setTrackLoopable(uint8_t scene_index, uint8_t track, uint8_t loopable);
uint8_t pat_trackLoopable(uint8_t scene_index, uint8_t track);
```

### 10.2 PatternData.c — assert, defaults, menu mirror, accessors, region copy

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

**Lines:** 44–47 (`_Static_assert(sizeof(pat_scene_region_t) == … 512u + 30u,`)
**Action:** MODIFY `30u` → `31u` (same message change as 10.1).

**Lines:** 866–873 (track default loop in `pat_initScene()`, ending
`region->pattern_next = 0u;`)
**Action:** ADD after `region->pattern_next = 0u;`
```c
    /* S080 P2 Defect 8a: every track starts loopable. */
    region->track_loop_exclude = 0u;
```

**Lines:** 1166–1175 (`pat_applyTrackSettingsToMenu()`)
**Action:** ADD after the `PAR_TRACK_PLAY_MODE` mirror line
```c
    /* S080 P2 Defect 8a: lop mirror (1 = loopable). */
    parameter_values[PAR_TRACK_LOOPABLE] =
        (uint8_t)((region->track_loop_exclude & (uint8_t)(1u << track)) == 0u);
```

**Line:** after 1226 (end of `pat_setTrackPlayMode()`)
**Action:** ADD
```c
/*
 * Per-track loopable setter/getter (S080 P2 Defect 8a).
 *
 * What: pat_setTrackLoopable() sets or clears the track's exclude bit in the
 * resident region (loopable = 0 sets it) and marks the Scene's Pattern dirty
 * only on change; pat_trackLoopable() reports 1 when the bit is clear. Why:
 * the flag is Pattern data persisted in the PAT4 track header and travels
 * with Pattern copy/clear. Inputs: Scene, track, flag. Outputs: region byte,
 * Pattern AutoSave dirty. Accessors: menu_cellCommitValue() (lop cell),
 * copyClearService.c, seq_loopableTrackMask() reads the region directly.
 * Affiliates: pat_scene_region_t.track_loop_exclude.
 */
void pat_setTrackLoopable(uint8_t scene_index, uint8_t track, uint8_t loopable)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);
    uint8_t bit;
    uint8_t next;

    if (!region || !pat_trackValid(track))
        return;
    bit = (uint8_t)(1u << track);
    next = loopable ? (uint8_t)(region->track_loop_exclude & (uint8_t)~bit)
                    : (uint8_t)(region->track_loop_exclude | bit);
    if (next == region->track_loop_exclude)
        return;
    region->track_loop_exclude = next;
    pat_markSceneDirty(scene_index);
}

uint8_t pat_trackLoopable(uint8_t scene_index, uint8_t track)
{
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);

    if (!region || !pat_trackValid(track))
        return 1u;
    return (uint8_t)((region->track_loop_exclude & (uint8_t)(1u << track)) == 0u);
}
```

**Lines:** 2012–2015 (`memcpy(dst->track_play_mode, …);`
`dst->pattern_change_bar = …;` in `pat_rawRegionCopyBody()`)
**Action:** ADD after the play-mode `memcpy`
```c
    /* S080 P2 Defect 8a: the loop exclusion travels with a Scene copy. */
    dst->track_loop_exclude = src->track_loop_exclude;
```

### 10.3 filesystem.c — PAT4 writer and the three readers

**File:** `Core/Hardware/SD/filesystem.c`

**Lines:** 3066–3068 (`/* S078 §4.5: byte 3 of the 16-byte track header
stores play mode. */` `header[base + 3u] = region->track_play_mode[track];`)
**Action:** ADD after them
```c
        /*
         * S080 P2 Defect 8a: byte 4 stores the looper exclusion (1 = track
         * not loopable). The byte was reserved and written as zero, so older
         * files read as loopable and the PAT4 version stays 2.
         */
        header[base + 4u] =
            (uint8_t)((region->track_loop_exclude >> track) & 1u);
```

**Lines:** 13186–13197 (scene-load reader: `region->pattern_change_bar =
staging_buf[32];` … loop ending `region->track_play_mode[track] = staging_buf[base + 3u];`),
15219–15229 (root Pattern Load reader, same shape) and 28505–28515 (AutoSave
boot reader, same shape)
**Action:** MODIFY all three the same way: before the `for (track …)`
loop add `region->track_loop_exclude = 0u;`, and inside the loop after the
play-mode line add
```c
                /* S080 P2 Defect 8a: byte 4 = looper exclusion (old files 0). */
                if (staging_buf[base + 4u] != 0u)
                    region->track_loop_exclude |= (uint8_t)(1u << track);
```

### 10.4 copyClearService.c — track copy and clear

**File:** `Core/Menu/CopyClear/copyClearService.c`
**Lines:** 851–853 (`/* S078 §4.1: play mode is part of a track copy. */`
`pat_setTrackPlayMode(job->scene, job->track, s->track_play_mode[src->track]);`)
**Action:** ADD after them
```c
                /* S080 P2 Defect 8a: loopable is part of a track copy. */
                pat_setTrackLoopable(job->scene, job->track,
                                     pat_trackLoopable(src->scene, src->track));
```

**Line:** 1028 (`pat_setTrackPlayMode(job->scene, job->track, 0u);` in the
track clear)
**Action:** ADD after it
```c
            /* S080 P2 Defect 8a: a cleared track is loopable again. */
            pat_setTrackLoopable(job->scene, job->track, 1u);
```

### 10.5 menu.c — commit the lop cell

**File:** `Core/Menu/menu.c`
**Line:** before 4920 (the `S074 bus compressor cells are Scene-owned`
comment in `menu_cellCommitValue()`)
**Action:** ADD
```c
    /*
     * S080 P2 Defect 8a: STEP-page lop cell.
     *
     * What: commits the on/off value to the viewed Scene's active track via
     * PatternData and refreshes the mirror. Why: PAR_TRACK_LOOPABLE sits in
     * the appended id range that menu_sendAllGlobals() replays on every
     * settings load, so it must not commit through menu_parseGlobalParam().
     * Inputs: clamped 0/1. Outputs: pat_setTrackLoopable(). Affiliates:
     * pat_applyTrackSettingsToMenu(), seq_loopableTrackMask().
     */
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_TRACK_LOOPABLE) {
        pat_setTrackLoopable(menu_getViewedPattern(), menu_getActiveVoice(),
                             (uint8_t)(value != 0u));
        parameter_values[PAR_TRACK_LOOPABLE] = (uint8_t)(value != 0u);
        return 1u;
    }
```

**Line:** after the `case PAR_TRACK_PLAY_MODE:` block in
`menu_parseGlobalParam()` (ends ≈ 15647)
**Action:** ADD
```c
    case PAR_TRACK_LOOPABLE:
    case PAR_CHAIN_COND1:
    case PAR_CHAIN_NEXT1:
    case PAR_CHAIN_COND2:
    case PAR_CHAIN_NEXT2:
        /*
         * S080 P2: display mirrors only. menu_sendAllGlobals() replays every
         * id from PAR_BEGINNING_OF_GLOBALS on settings load; these mirrors
         * must never write PatternData or SceneData from that bulk path.
         * Page edits commit in menu_cellCommitValue().
         */
        break;
```

### 10.6 menuPages.h — STEP track page cell

**File:** `Core/Menu/menuPages.h`
**Lines:** 120–128 (`SEQ_PAGE` comment and sub-page 0 row)
**Action:** MODIFY
```c
/* SEQ_PAGE
 * Subpage 0 is the track-settings front page. S078 §7 groups the three
 * morphable parameters (len, scl, shf) on the left, followed by the discrete
 * play mode (run), MIDI channel, MIDI note and the S080 P2 looper opt-out
 * (lop, Defect 8a). The last cell is the traversal terminator. Clients:
 * STEP-mode VOICE re-press toggles menuIndex through Menu helpers, and encoder
 * scrolling reaches every cell through normal traversal.
 */
/* SEQ_PAGE */ {
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_SHUFFLE,TEXT_TRACK_PLAY_MODE,TEXT_MIDI_CHANNEL,TEXT_NOTE,TEXT_TRACK_LOOPABLE,TEXT_EMPTY, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_SHUFFLE,PAR_TRACK_PLAY_MODE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_TRACK_LOOPABLE,PAR_NONE},
```

### 10.7 sequencer.c — loopable query

**File:** `Core/Sequencer/sequencer.c`
**Line:** end of the Step 9.5 looper engine block
**Action:** ADD
```c
/*
 * Tracks the looper may capture (S080 P2 Defect 8a).
 *
 * What: bit N set when track N's played Scene does not exclude it from
 * looping (pat_scene_region_t.track_loop_exclude). Why: excluded tracks are
 * silently left out of every engage/add, with no LED or error. Read at
 * gesture time; a later scene change does not drop a track already looping.
 * Inputs: seq_perTrackPattern[], resident regions. Output: 7-bit mask.
 * Accessors: seq_loopEngage(), seq_loopUpdateMask().
 */
uint8_t seq_loopableTrackMask(void)
{
	uint8_t mask = 0u;
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		const pat_scene_region_t *region =
		    pat_sceneRegion(seq_perTrackPattern[track]);

		if (!region ||
		    (region->track_loop_exclude & (uint8_t)(1u << track)) == 0u)
			mask |= (uint8_t)(1u << track);
	}
	return mask;
}
```

---

## Step 11 — Defect 2 (Phase G scene chaining) + Defect 13

### 11.1 SceneData.h — chain fields, constants, API

**File:** `Core/Bank/Scene/SceneData.h`
**Line:** after 251 (`uint8_t track_morph_shuffle[NUM_TRACKS];`, before the
`Autosave extension rule for Scene settings.` comment)
**Action:** ADD
```c
    /*
     * Scene chain settings (S080 G1, P2).
     *
     * What: chain_condition[0..1] = cn1/cn2 (0..78: nul, xal, bal, x01..x64,
     * b01..b64, r1x..r9x, r1b..r9b, 1:4..4:4, 1:8..8:8, >01..>16, fix, fib,
     * !fx, !fb, lox, lob, !lx, !lb); chain_next[0..1] = nx1/nx2 (0..43: slf,
     * nxt, prv, s01..s16, r01..r16, r+1..r+4, r-1..r-4, stp). Zero = nul / slf
     * = no chaining. Why: per-Scene automatic transitions (plan §4). They copy
     * and clear with the Scene but never fan out through the VOICE edit mask.
     * Inputs: chain editor, sceneset.scg, AutoSave restore, copy/clear.
     * Outputs: seq_chainEvaluate() (TIM3, single-byte reads). Storage:
     * sceneset keys chain_condition/chain_next; AutoSave Scene cells 72..75.
     * RAM: 4 B per Scene, 64 B across scenes[16]. Affiliates:
     * scene_setChainCondition()/scene_setChainNext() (the only writers).
     */
    uint8_t chain_condition[SCENE_CHAIN_SLOT_COUNT];
    uint8_t chain_next[SCENE_CHAIN_SLOT_COUNT];
```

**Line:** after 145 (`#define SCENE_BUS_COMP_SIDECHAIN_OFF   0u`)
**Action:** ADD
```c
/*
 * S080 Phase G chain domains. Two (condition, next) pairs per Scene; the
 * condition and next-scene tables are chainConditionNames/chainNextNames
 * (MenuText.h) and seq_chainConditionWeight()/seq_chainResolveNext()
 * (sequencer.c), which must stay in the same order.
 */
#define SCENE_CHAIN_SLOT_COUNT      2u
#define SCENE_CHAIN_CONDITION_MAX  78u
#define SCENE_CHAIN_NEXT_MAX       43u
#define SCENE_CHAIN_NEXT_STOP      43u
```

**Line:** after 575 (`uint8_t scene_getTrackMorphShuffle(…);`)
**Action:** ADD
```c
/*
 * Scene chain accessors (S080 G2).
 *
 * Setters clamp (condition 0..78, next 0..43), commit through the change-aware
 * Scene store (AutoSave cell + card-clean bit) and ignore invalid Scene/slot.
 * Getters return 0 for invalid coordinates and are single-byte SRAM reads, so
 * the TIM3 chain evaluator may call them. Clients: chain editor (menu.c),
 * scene_commitSettings(), AutoSave restore, seq_chainEvaluate().
 */
void scene_setChainCondition(uint8_t scene_index, uint8_t slot, uint8_t value);
uint8_t scene_getChainCondition(uint8_t scene_index, uint8_t slot);
void scene_setChainNext(uint8_t scene_index, uint8_t slot, uint8_t value);
uint8_t scene_getChainNext(uint8_t scene_index, uint8_t slot);
```

### 11.2 SceneData.c — accessors and whole-settings commit

**File:** `Core/Bank/Scene/SceneData.c`
**Line:** after 1024 (end of `scene_getTrackMorphShuffle()`)
**Action:** ADD
```c
/*
 * Scene chain accessors (S080 G2; contract in SceneData.h).
 *
 * What: named setters/getters for the two chain pairs, clamped to the table
 * sizes and committed through scene_storeParameterByte() with AutoSave cells
 * AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE/NEXT_BASE + slot. Why: the
 * AutoSave extension rule requires a named index, getter and change-aware
 * setter for every retained Scene byte. Inputs: Scene 0..15, slot 0..1,
 * value. Outputs: scene.settings.chain_condition[]/chain_next[].
 * Affiliates: autosave_getSceneParameter()/autosave_applyScenePayload().
 */
void scene_setChainCondition(uint8_t scene_index, uint8_t slot, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT)
        return;
    if (value > SCENE_CHAIN_CONDITION_MAX)
        value = SCENE_CHAIN_CONDITION_MAX;
    scene_storeParameterByte(
        scene_index, &scene->settings.chain_condition[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE + slot), value);
}

uint8_t scene_getChainCondition(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT ||
        scene->settings.chain_condition[slot] > SCENE_CHAIN_CONDITION_MAX)
        return 0u;
    return scene->settings.chain_condition[slot];
}

void scene_setChainNext(uint8_t scene_index, uint8_t slot, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT)
        return;
    if (value > SCENE_CHAIN_NEXT_MAX)
        value = SCENE_CHAIN_NEXT_MAX;
    scene_storeParameterByte(
        scene_index, &scene->settings.chain_next[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE + slot), value);
}

uint8_t scene_getChainNext(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT ||
        scene->settings.chain_next[slot] > SCENE_CHAIN_NEXT_MAX)
        return 0u;
    return scene->settings.chain_next[slot];
}
```

**Lines:** 1104–1106 in `scene_commitSettings()` (after the bus comp loop,
before `return (uint8_t)(memcmp(…));`)
**Action:** ADD
```c
    /*
     * S080 G3: copy/clear carry the Scene chain (defaults 0 = no chaining via
     * scene_settingsDefaults()' memset). Not part of the VOICE edit-mask
     * fan-out: only this Scene changes.
     */
    for (i = 0u; i < SCENE_CHAIN_SLOT_COUNT; i++) {
        scene_setChainCondition(scene_index, i, image.chain_condition[i]);
        scene_setChainNext(scene_index, i, image.chain_next[i]);
    }
```
(`scene_settingsDefaults()` and `filesystem_initSceneStage()` both start
with `memset(…, 0, …)`, so the chain defaults need no code: G4.)

Also update the stale size note in the comment at 1045: "(sizeof(scene_settings_t),
under 64 B)" → "(sizeof(scene_settings_t), about 80 B)".

### 11.3 Autosave.h / Autosave.c — Scene cells 72..75

**File:** `Core/Bank/Scene/Autosave.h`
**Lines:** 262–266 (`AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE = 65,`
`AUTOSAVE_SCENE_PARAM_COUNT = 72`)
**Action:** MODIFY
```c
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE = 65,
    /*
     * S080 G: Scene chain bytes, condition 1/2 (72..73) and next 1/2
     * (74..75). They occupy previously reserved cells of the 118-byte Scene
     * allocation, so the AutoSave record layout, masks and format version
     * stay unchanged. Owner: scene_setChainCondition()/scene_setChainNext().
     */
    AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE = 72,
    AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE = 74,
    AUTOSAVE_SCENE_PARAM_COUNT = 76
```

**File:** `Core/Bank/Scene/Autosave.c`
**Lines:** 113–115 (`_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE == NUM_TRACKS, …);`)
**Action:** MODIFY
```c
_Static_assert(AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE -
                   AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE == NUM_TRACKS,
               "Scene track Morph shuffle group must cover every track");
/* S080 G: the two chain groups close the live cells. */
_Static_assert(AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE -
                   AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE ==
                   SCENE_CHAIN_SLOT_COUNT,
               "Scene chain condition group must cover both slots");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT -
                   AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE ==
                   SCENE_CHAIN_SLOT_COUNT,
               "Scene chain next group must cover both slots");
_Static_assert(AUTOSAVE_SCENE_PARAM_COUNT <=
                   AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES,
               "Scene live cells must fit the reserved Scene allocation");
```

**Lines:** 965–969 (final `else` in `autosave_getSceneParameter()`:
`/* Indices 65..71 are the S078 track Morph shuffle endpoints. */`)
**Action:** MODIFY
```c
    } else if (parameter_index <
               AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE) {
        /* Indices 65..71 are the S078 track Morph shuffle endpoints. */
        *value = scene->settings.track_morph_shuffle[
            parameter_index - AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE];
    } else if (parameter_index < AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE) {
        /* Indices 72..73 are the S080 Scene chain conditions. */
        *value = scene->settings.chain_condition[
            parameter_index - AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE];
    } else {
        /* Indices 74..75 are the S080 Scene chain next-scene values. */
        *value = scene->settings.chain_next[
            parameter_index - AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE];
    }
```

**Lines:** 1470–1478 (`if (value == 0u) { … return; }` in the track Morph
length branch of `autosave_applyScenePayload()`)
**Action:** MODIFY `return;` → `continue;`, with the comment line
```c
                /* S080 P2: continue, not return - later cells (track Morph
                 * scale/shuffle, Scene chain) must still restore. */
```
Rationale: the old `return` aborted the restore loop at the first
zero-length cell, so every cell after 51 (and now the chain cells) was
skipped for a pre-S078 record.

**Lines:** 1495–1501 (final `else` restoring the shuffle endpoints)
**Action:** MODIFY
```c
        } else if (parameter_index <
                   AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE) {
            /* Indices 65..71 restore the S078 track Morph shuffle endpoints. */
            scene_setTrackMorphShuffle(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE),
                value);
        } else if (parameter_index < AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE) {
            /*
             * Indices 72..73 restore the S080 chain conditions through the
             * clamping setter; pre-S080 records hold zero (nul).
             */
            scene_setChainCondition(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE),
                value);
        } else {
            /* Indices 74..75 restore the S080 chain next-scene values. */
            scene_setChainNext(
                scene_index,
                (uint8_t)(parameter_index -
                          AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE),
                value);
        }
```

### 11.4 filesystem.c / storageTypes.c — sceneset keys

**File:** `Core/Hardware/SD/filesystem.c`
**Line:** after 17349 (end of `case 17u:` in `filesystem_nextScenesetLine()`)
**Action:** ADD
```c
    case 18u:
        /*
         * S080 G5: Scene chain lines, appended after the track Morph lines so
         * no earlier writer line moves. chain_condition = cn1,cn2 (0..78);
         * chain_next = nx1,nx2 (0..43). Parser keys: storageTypes.c
         * storage_scenesetParseLine(). Affiliates: scene_settings_t.chain_*.
         */
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "chain_condition", scene->settings.chain_condition,
            SCENE_CHAIN_SLOT_COUNT);
    case 19u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "chain_next", scene->settings.chain_next,
            SCENE_CHAIN_SLOT_COUNT);
```

**File:** `Core/Hardware/SD/storageTypes.c`
**Line:** after the `"track_morph_shuffle"` branch (ends ≈ 724) in
`storage_scenesetParseLine()`
**Action:** ADD
```c
    } else if (storage_streq(key, "chain_condition")) {
        /*
         * Parse the Scene chain conditions (S080 G6).
         *
         * Inputs: two comma-separated values 0..78. Output: staged settings
         * chain_condition[]. A missing key keeps the stage default 0 (nul = no
         * chaining), so older sceneset.scg files load unchanged. Writer:
         * filesystem_nextScenesetLine() line 18.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value, target_settings->chain_condition,
                                  SCENE_CHAIN_SLOT_COUNT,
                                  SCENE_CHAIN_CONDITION_MAX);
    } else if (storage_streq(key, "chain_next")) {
        /* S080 G6: next-scene values 0..43 (slf..stp); writer line 19. */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value, target_settings->chain_next,
                                  SCENE_CHAIN_SLOT_COUNT,
                                  SCENE_CHAIN_NEXT_MAX);
```

### 11.5 sequencer.c — chain runtime state

**File:** `Core/Sequencer/sequencer.c`
**Line:** after 437 (`static uint8_t seq_sceneFireFlag      = 0u;`)
**Action:** ADD
```c
/*
 * Scene chain runtime state (S080 G7, P2).
 *
 * What: seq_chainBarCount / seq_chainRepCount count master-clock bar and
 * repetition boundaries since the active Scene was selected (saturating at
 * 255). seq_previousScene is the Scene active before the last real change
 * (0xFF = none; the >NN conditions). seq_chainStopped freezes every track
 * after a `stp` chain fire. seq_pendingChainFire marks the pending direct
 * change as chain-raised. Why: the TIM3 evaluator needs per-activation
 * counts; the foreground executor needs to tell a chain repeat from a real
 * change. Inputs: seq_chainServiceTick(), seq_selectActivePattern(),
 * seq_setRunning(), seq_chainRepeatSelf(). RAM: 5 B SRAM1, runtime-only.
 * Affiliates: seq_chainEvaluate(), menu_serviceDeferredSceneChange().
 */
static uint8_t seq_chainBarCount    = 0u;
static uint8_t seq_chainRepCount    = 0u;
static uint8_t seq_previousScene    = 0xFFu;
static uint8_t seq_chainStopped     = 0u;
static uint8_t seq_pendingChainFire = 0u;
```

**File:** `Core/Sequencer/sequencer.c`
**Line:** with the other includes (after 51 `#include "SceneData.h"`)
**Action:** ADD `#include "BankData.h"` with the comment
`/* S080 G7: bank_scenePresent() for chain targets (single SRAM read). */`

### 11.6 sequencer.c — chain engine

**File:** `Core/Sequencer/sequencer.c`
**Line:** after the Step 9.5 / 10.7 looper block (before the `Deferred
trigger tick-down` comment)
**Action:** ADD
```c
/* =====================================================================
 * S080 Phase G scene chain engine
 * ===================================================================== */

#define SEQ_CHAIN_BAR_TICKS 384u    /* 16 x 1/16 at 96 PPQ */

/*
 * True on the tick at which the longest track completes a repetition
 * (S080 G7, P2 D10).
 *
 * What: the repetition length is the largest effective length x Q8.8 step
 * interval over the seven tracks; the boundary is the first master tick
 * at or after each multiple of it. Why: "end of repetition = end of the
 * longest track by scale x length" (plan §4.5), on the master clock so
 * realigned and reset Scenes agree. Inputs: effective cache,
 * seq_elapsedPpqTicks. Output: 0/1. Context: TIM3 ISR.
 */
static uint8_t seq_chainAtRepetitionEnd(void)
{
	uint32_t longest = 0u;
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t len = seq_effectiveTrackLength[track];
		uint32_t cycle;

		if (len < 1u)
			len = NUM_STEPS_PER_BAR;
		cycle = (uint32_t)len *
		        stepScale_ticksQ8(seq_effectiveTrackScale[track]);
		if (cycle > longest)
			longest = cycle;
	}
	if (longest == 0u)
		return 0u;
	return (uint8_t)((((uint64_t)seq_elapsedPpqTicks << 8u) % longest) < 256u);
}

/*
 * Weight of one chain condition at this boundary (S080 G7).
 *
 * What: 0 = does not fire here, 100 = fires, 10..90 = probability (r1x..r9b).
 * Index order matches chainConditionNames[] (MenuText.h): 0 nul, 1 xal, 2 bal,
 * 3..13 xNN, 14..24 bNN, 25..33 rNx, 34..42 rNb, 43..46 X:4, 47..54 X:8,
 * 55..70 >NN, 71 fix, 72 fib, 73 !fx, 74 !fb, 75 lox, 76 lob, 77 !lx, 78 !lb.
 * Repetition-kind conditions only count at a repetition end, bar-kind only at
 * a bar end. Counts fire when equal (D11). X:4 / X:8 use the master bar just
 * completed. Inputs: condition, boundary flags, chain counters, fill/loop
 * flags, seq_previousScene. Output: weight. Context: TIM3 ISR.
 */
static uint8_t seq_chainConditionWeight(uint8_t cond, uint8_t at_bar,
                                        uint8_t at_rep)
{
	static const uint8_t counts[11] = {
		1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 16u, 32u, 64u
	};
	uint32_t bar_done = seq_elapsedPpqTicks / SEQ_CHAIN_BAR_TICKS;

	if (cond == 0u)
		return 0u;
	if (cond == 1u)
		return at_rep ? 100u : 0u;
	if (cond == 2u)
		return at_bar ? 100u : 0u;
	if (cond <= 13u)
		return (at_rep && seq_chainRepCount == counts[cond - 3u]) ? 100u : 0u;
	if (cond <= 24u)
		return (at_bar && seq_chainBarCount == counts[cond - 14u]) ? 100u : 0u;
	if (cond <= 33u)
		return at_rep ? (uint8_t)((cond - 24u) * 10u) : 0u;
	if (cond <= 42u)
		return at_bar ? (uint8_t)((cond - 33u) * 10u) : 0u;
	if (cond <= 46u)
		return (at_bar && bar_done != 0u &&
		        ((bar_done - 1u) % 4u) == (uint32_t)(cond - 43u)) ? 100u : 0u;
	if (cond <= 54u)
		return (at_bar && bar_done != 0u &&
		        ((bar_done - 1u) % 8u) == (uint32_t)(cond - 47u)) ? 100u : 0u;
	if (cond <= 70u)
		return (at_rep && seq_previousScene == (uint8_t)(cond - 55u))
		    ? 100u : 0u;
	switch (cond) {
	case 71u: return (at_rep && seq_fillActive) ? 100u : 0u;
	case 72u: return (at_bar && seq_fillActive) ? 100u : 0u;
	case 73u: return (at_rep && !seq_fillActive) ? 100u : 0u;
	case 74u: return (at_bar && !seq_fillActive) ? 100u : 0u;
	case 75u: return (at_rep && seq_loopActive) ? 100u : 0u;
	case 76u: return (at_bar && seq_loopActive) ? 100u : 0u;
	case 77u: return (at_rep && !seq_loopActive) ? 100u : 0u;
	case 78u: return (at_bar && !seq_loopActive) ? 100u : 0u;
	default:  return 0u;
	}
}

/*
 * Resolve a next-scene value to a Scene index (S080 G7, D12).
 *
 * What: 0 slf, 1 nxt (wraps 16->1), 2 prv (wraps 1->16), 3..18 s01..s16,
 * 19..34 rNN (random among Scenes 1..NN), 35..38 r+N (random among the next N,
 * excluding the current), 39..42 r-N (previous N). Returns 0xFF for stp or an
 * absent target (no change). Order matches chainNextNames[]. Inputs: next
 * value, current Scene. Output: Scene 0..15 or 0xFF. Context: TIM3 ISR
 * (GetRngValue() and bank_scenePresent() are ISR-safe reads).
 */
static uint8_t seq_chainResolveNext(uint8_t next, uint8_t cur)
{
	uint8_t target;
	uint16_t rnd = (uint16_t)GetRngValue() & 0x7FFFu;

	if (next == 0u)
		target = cur;
	else if (next == 1u)
		target = (uint8_t)((cur + 1u) & 0x0Fu);
	else if (next == 2u)
		target = (uint8_t)((cur + 15u) & 0x0Fu);
	else if (next <= 18u)
		target = (uint8_t)(next - 3u);
	else if (next <= 34u)
		target = (uint8_t)(rnd % (uint16_t)(next - 18u));
	else if (next <= 38u)
		target = (uint8_t)((cur + 1u + (rnd % (uint16_t)(next - 34u))) & 0x0Fu);
	else if (next <= 42u)
		target = (uint8_t)((cur + 15u - (rnd % (uint16_t)(next - 38u))) & 0x0Fu);
	else
		return 0xFFu;               /* stp is handled by the caller */
	return bank_scenePresent(target) ? target : 0xFFu;
}

/*
 * Evaluate the active Scene's chain at a boundary and fire (S080 G7).
 *
 * What: weights both conditions; condition 1 has priority. w1 = 100 fires 1.
 * Two probabilities summing over 100 are weighted proportionally (one always
 * fires). Otherwise one draw r in 0..99: r < w1 fires 1, r < w1 + w2 fires 2,
 * else nothing (the Scene repeats). A `stp` result freezes every track; any
 * other result raises a chain-marked direct change that the foreground runs at
 * once (not re-quantised by scd, D9). Why: plan §4.5 evaluation rules. Inputs:
 * scene_getChainCondition()/Next() of seq_activePattern, boundary flags.
 * Outputs: seq_chainStopped or the pending-change fields + fire flag (set
 * last). Context: TIM3 ISR. Affiliates: menu_serviceDeferredSceneChange().
 */
static void seq_chainEvaluate(uint8_t at_bar, uint8_t at_rep)
{
	uint8_t scene = seq_activePattern;
	uint8_t w1 = seq_chainConditionWeight(
	    scene_getChainCondition(scene, 0u), at_bar, at_rep);
	uint8_t w2 = seq_chainConditionWeight(
	    scene_getChainCondition(scene, 1u), at_bar, at_rep);
	uint8_t which = 0u;
	uint8_t next;
	uint8_t target;

	if (w1 == 0u && w2 == 0u)
		return;
	if (w1 >= 100u) {
		which = 1u;
	} else if (w1 != 0u && w2 != 0u && w2 < 100u &&
	           (uint16_t)w1 + w2 > 100u) {
		uint16_t r = (uint16_t)(((uint16_t)GetRngValue() & 0x7FFFu) %
		                        (uint16_t)(w1 + w2));
		which = (r < w1) ? 1u : 2u;
	} else {
		uint16_t r = (uint16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % 100u);
		uint16_t sum = (uint16_t)w1 + w2;

		if (sum > 100u)
			sum = 100u;
		if (r < w1)
			which = 1u;
		else if (r < sum)
			which = 2u;
	}
	if (which == 0u)
		return;
	next = scene_getChainNext(scene, (uint8_t)(which - 1u));
	if (next == SCENE_CHAIN_NEXT_STOP) {
		seq_chainStopped = 1u;
		return;
	}
	target = seq_chainResolveNext(next, scene);
	if (target == 0xFFu)
		return;
	seq_pendingSceneIndex = target;
	seq_pendingSceneDiv = 0u;
	seq_pendingSceneChange = 1u;
	seq_pendingChainFire = 1u;
	seq_sceneFireFlag = 1u;         /* published last */
}

/*
 * Per-tick chain boundary service (S080 G7).
 *
 * What: on each master tick after the first, detects bar and repetition
 * boundaries, advances the counters, and evaluates the chain unless a direct,
 * queued or chain change is already pending (a manual or queued switch
 * cancels conditional changes, plan §4.5). Inputs: seq_elapsedPpqTicks,
 * pending flags. Outputs: counters, seq_chainEvaluate(). Context: TIM3 ISR via
 * seq_processSchedulerTick().
 */
static void seq_chainServiceTick(void)
{
	uint8_t at_bar;
	uint8_t at_rep;

	if (seq_elapsedPpqTicks == 0u)
		return;
	at_bar = (uint8_t)((seq_elapsedPpqTicks % SEQ_CHAIN_BAR_TICKS) == 0u);
	at_rep = seq_chainAtRepetitionEnd();
	if (!at_bar && !at_rep)
		return;
	if (at_bar && seq_chainBarCount < 255u)
		seq_chainBarCount++;
	if (at_rep && seq_chainRepCount < 255u)
		seq_chainRepCount++;
	if (seq_pendingSceneChange || seq_pendingSceneQueue || seq_sceneFireFlag)
		return;
	seq_chainEvaluate(at_bar, at_rep);
}

/*
 * Chain repeat of the playing Scene (S080 G7, P2 D13).
 *
 * What: resets the chain counters and `stp` freeze, retriggers once-mode
 * tracks and the per-track condition counters, and realigns every track to the
 * master clock (keeping loop anchors), or restarts tracks and master clock
 * when `restart` (pcr) is set. Why: a chain fire to the playing Scene (slf, or
 * a resolved target equal to it) is a repeat - plan §4.5 "3 bars, stop 1 bar,
 * then repeat" - and must not re-run the bounded DSP Scene apply. Inputs:
 * restart. Outputs: sequencer state. Context: foreground. Accessors:
 * menu_serviceDeferredSceneChange(). Affiliates: seq_resetToPatternStart(),
 * seq_loopRefreshSceneSnapshots().
 */
void seq_chainRepeatSelf(uint8_t restart)
{
	uint32_t primask;

	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
	if (restart) {
		seq_resetToPatternStart();
		return;
	}
	primask = __get_PRIMASK();
	__disable_irq();
	memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
	memset(seq_trackRepeatCount, 0, sizeof(seq_trackRepeatCount));
	memset(seq_trackRndStepCount, 0, sizeof(seq_trackRndStepCount));
	memset(seq_trackLastConditionFired, 0,
	       sizeof(seq_trackLastConditionFired));
	seq_realignActivePatternToMasterClock();
	seq_loopRefreshSceneSnapshots();
	__set_PRIMASK(primask);
}
```

**File:** `Core/Sequencer/sequencer.h`
**Line:** after the Step 6 additions (scene-change API)
**Action:** ADD
```c
/*
 * S080 G7: chain repeat of the playing Scene (counters reset, stp cleared,
 * realign; restart when `restart` = pcr). Client: menu_serviceDeferredSceneChange().
 */
void seq_chainRepeatSelf(uint8_t restart);
```

### 11.7 sequencer.c — chain hooks in scene select and transport

**File:** `Core/Sequencer/sequencer.c`
**Line:** after 1376 (`if (!pat_patternValid(pattern)) return;` in
`seq_selectActivePattern()`)
**Action:** ADD
```c
	/*
	 * S080 G7/G12: a Scene selection starts a fresh chain.
	 *
	 * What: remembers the outgoing Scene for the >NN conditions (only on a
	 * real change), restarts the bar/repetition counts and releases a `stp`
	 * freeze. Why: counts are per Scene activation; any executed change -
	 * manual, queued or chain - cancels pending conditional state (plan
	 * §4.5). Affiliates: seq_chainServiceTick(), seq_chainEvaluate().
	 */
	if (pattern != seq_activePattern)
		seq_previousScene = seq_activePattern;
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
```

**Line:** after 1466 (`if (!pat_patternValid(scene_index)) return;` in
`seq_alignActivePatternToScene()`)
**Action:** ADD
```c
	/* S080 G7: a Bank Load alignment starts a fresh chain with no history. */
	seq_previousScene = 0xFFu;
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;
```

**Lines:** 3172–3175 in `seq_setRunning()` (`/* S080 G12: a transport stop
cancels pending/queued scene changes. */` and the three clears)
**Action:** MODIFY
```c
		/*
		 * S080 G12 / P2: a transport stop cancels pending, queued and
		 * chain-fired scene changes and clears all chain state (the only
		 * thing that stops a queued change, plan §5.3).
		 */
		seq_pendingSceneChange = 0u;
		seq_pendingSceneQueue = 0u;
		seq_pendingChainFire = 0u;
		seq_sceneFireFlag = 0u;
		seq_chainBarCount = 0u;
		seq_chainRepCount = 0u;
		seq_chainStopped = 0u;
		seq_previousScene = 0xFFu;
```
And in the start branch (after `trigger_reset(1);`, line 3192) ADD
```c
		/* S080 G7: counts start at zero with the transport. */
		seq_chainBarCount = 0u;
		seq_chainRepCount = 0u;
		seq_chainStopped = 0u;
```

### 11.8 menu.c — chain editor

**File:** `Core/Menu/menu.c`
**Line:** after 2076 (`static uint8_t menu_commitEffectMorphParam(uint8_t value);`)
**Action:** ADD
```c
/* S080 G8: chain editor cell helpers (defined beside the editor). */
static uint8_t menu_paramIsChain(uint16_t paramNr);
static uint8_t menu_commitChainParam(uint16_t paramNr, uint8_t value);
```

**Line:** before 4920 (with the Step 10.5 lop branch in `menu_cellCommitValue()`)
**Action:** ADD
```c
    /*
     * S080 G8: chain editor cells commit to the viewed Scene only (no VOICE
     * edit-mask fan-out); see menu_commitChainParam().
     */
    if (cell->kind == MENU_CELL_STATIC &&
        menu_paramIsChain(cell->static_param))
        return menu_commitChainParam(cell->static_param, (uint8_t)value);
```

**Lines:** 5698–5718 (S080 switch in `menu_clampCellValue()`)
**Action:** ADD cases
```c
        case PAR_CHAIN_COND1:
        case PAR_CHAIN_COND2:
            /* S080 G: 79 chain conditions, 0..78. */
            if (*value > SCENE_CHAIN_CONDITION_MAX)
                *value = SCENE_CHAIN_CONDITION_MAX;
            return;
        case PAR_CHAIN_NEXT1:
        case PAR_CHAIN_NEXT2:
            /* S080 G: 44 next-scene values, 0..43. */
            if (*value > SCENE_CHAIN_NEXT_MAX)
                *value = SCENE_CHAIN_NEXT_MAX;
            return;
```

**Lines:** 9681–9690 (switch in `menu_getS080StaticName()`)
**Action:** ADD cases (and append "chain condition / next scene" to the
function comment's list)
```c
    case PAR_CHAIN_COND1:
    case PAR_CHAIN_COND2:
        menu_nameTable3(chainConditionNames, 79u, value, buf);
        return 1u;
    case PAR_CHAIN_NEXT1:
    case PAR_CHAIN_NEXT2:
        menu_nameTable3(chainNextNames, 44u, value, buf);
        return 1u;
```

**Line:** before 8163 (`void menu_refreshPerfSceneLeds(void)`), after the
three static prototypes at 8159–8161
**Action:** ADD
```c
/*
 * Scene chain editor (S080 G8, P2 Defects 2 and 13).
 *
 * What: menu_chainMenuScene is the Scene whose chain is shown (0xFF =
 * closed). menu_chainMenuOpen() shows the four chain cells of a present Scene
 * on PATTERN_SETTINGS_PAGE sub-page 0 and keeps SHIFT lit; menu_chainMenuView()
 * switches the viewed Scene (a SEQ press while open); menu_chainMenuClose()
 * returns to PERFORMANCE_PAGE sub-page 0. Only a user SEQ press changes the
 * viewed Scene; an active-Scene change (chain fire, queue) does not. Why: a
 * latched per-Scene editor entered with SHIFT+PLAY+SEQ in PERF (plan §4.1);
 * the retired, empty PATTERN_SETTINGS_PAGE hosts it without a new page table.
 * Inputs: Scene index from buttonHandler. Outputs: page, PAR_CHAIN_* mirrors,
 * SEQ/SHIFT LEDs. RAM: 1 byte SRAM1. Accessors: buttonHandler SHIFT+SEQ (open),
 * PERF SEQ press (view), SHIFT press / MODE / overflow (close). Affiliates:
 * menu_refreshPerfSceneLeds() (blinks active and viewed Scene),
 * menu_commitChainParam(), scene_getChain*()/scene_setChain*().
 */
static uint8_t menu_chainMenuScene = 0xFFu;

static void menu_chainLoadMirrors(uint8_t scene_index)
{
    parameter_values[PAR_CHAIN_COND1] = scene_getChainCondition(scene_index, 0u);
    parameter_values[PAR_CHAIN_NEXT1] = scene_getChainNext(scene_index, 0u);
    parameter_values[PAR_CHAIN_COND2] = scene_getChainCondition(scene_index, 1u);
    parameter_values[PAR_CHAIN_NEXT2] = scene_getChainNext(scene_index, 1u);
}

uint8_t menu_chainMenuOpen(uint8_t scene_index)
{
    if (scene_index >= SCENE_COUNT || scene_index >= 16u ||
        !bank_scenePresent(scene_index))
        return 0u;
    menu_chainMenuScene = scene_index;
    menu_chainLoadMirrors(scene_index);
    menu_switchPage(PATTERN_SETTINGS_PAGE);
    menu_switchSubPage(0u);
    menu_resetActiveParameter();
    led_setValue(1u, LED_SHIFT);
    menu_repaintAll();
    return 1u;
}

void menu_chainMenuView(uint8_t scene_index)
{
    if (menu_chainMenuScene >= 16u || scene_index >= SCENE_COUNT ||
        scene_index >= 16u || !bank_scenePresent(scene_index))
        return;
    menu_chainMenuScene = scene_index;
    menu_chainLoadMirrors(scene_index);
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

void menu_chainMenuClose(void)
{
    if (menu_chainMenuScene >= 16u)
        return;
    menu_chainMenuScene = 0xFFu;
    menu_switchPage(PERFORMANCE_PAGE);
    menu_switchSubPage(0u);
    menu_resetActiveParameter();
    led_setValue(buttonHandler_getShift(), LED_SHIFT);
    menu_repaintAll();
}

uint8_t menu_chainMenuIsOpen(void)
{
    return (uint8_t)(menu_chainMenuScene < 16u);
}

static uint8_t menu_paramIsChain(uint16_t paramNr)
{
    return (uint8_t)(paramNr >= PAR_CHAIN_COND1 && paramNr <= PAR_CHAIN_NEXT2);
}

/*
 * Commit one chain cell to the viewed Scene (S080 G8).
 *
 * What: writes the clamped value through the SceneData setter of the viewed
 * Scene's slot and refreshes the four mirrors from the stored bytes. Why: the
 * chain belongs to one Scene and never fans out (plan §9.2-3). Inputs:
 * PAR_CHAIN_* id, value. Output: 1 (repaint) or 0 when closed.
 */
static uint8_t menu_commitChainParam(uint16_t paramNr, uint8_t value)
{
    uint8_t scene_index = menu_chainMenuScene;

    if (scene_index >= 16u)
        return 0u;
    switch (paramNr) {
    case PAR_CHAIN_COND1: scene_setChainCondition(scene_index, 0u, value); break;
    case PAR_CHAIN_NEXT1: scene_setChainNext(scene_index, 0u, value);      break;
    case PAR_CHAIN_COND2: scene_setChainCondition(scene_index, 1u, value); break;
    case PAR_CHAIN_NEXT2: scene_setChainNext(scene_index, 1u, value);      break;
    default: return 0u;
    }
    menu_chainLoadMirrors(scene_index);
    return 1u;
}
```

**Lines:** 8163–8227 (`menu_refreshPerfSceneLeds()`)
**Action:** MODIFY (rewrite)
```c
void menu_refreshPerfSceneLeds(void)
{
    uint8_t scene_index;
    uint8_t active_scene = scene_getActiveIndex();
    uint8_t viewed = menu_getViewedPattern();
    uint16_t present_mask = menu_residentPresentSceneMask();
    uint16_t blink_mask = 0u;

    /*
     * Repaint PERF-mode Scene selection on the 16 SEQ LEDs (S077 P2 §5,
     * S080 P2 Defect 13).
     *
     * What: Scenes with retained Pattern steps are lit steady. These Scenes
     * blink at the standard rate: the active Scene while stopped (while
     * running it follows the beat pulse instead, S077 P2 §5.1); the viewed
     * Scene when PAR_FOLLOW is off and it differs from the active one
     * (§5.2); and, while the chain editor is open, both the active Scene and
     * the Scene whose chain is shown, regardless of transport (plan §4.1,
     * Defect 13). A blink already running is left alone so the per-beat
     * refresh does not restart its phase; only LEDs leaving the blink set are
     * cancelled. The queued Scene's double-rate flash is a separate LED layer
     * (LED_LAYER_FAST) that this repaint cannot disturb. Inputs: BankData
     * present mask, SceneData active index, PatternData step activity,
     * transport, beat pulse, follow, menu_chainMenuScene. Outputs: SEQ LED
     * base values and blink slots. Accessors: PERF entry, beat drain, scene
     * change, chain editor. Affiliates: led_isBlinkLed(), led_setBlinkLed(),
     * menu_syncQueuedSceneLed().
     */
    if (active_scene < 16u &&
        (present_mask & (uint16_t)(1u << active_scene)) != 0u &&
        (!seq_isRunning() || menu_chainMenuScene < 16u))
        blink_mask |= (uint16_t)(1u << active_scene);
    if (!parameter_values[PAR_FOLLOW] && viewed < 16u &&
        viewed != active_scene &&
        (present_mask & (uint16_t)(1u << viewed)) != 0u)
        blink_mask |= (uint16_t)(1u << viewed);
    if (menu_chainMenuScene < 16u)
        blink_mask |= (uint16_t)(1u << menu_chainMenuScene);

    for (scene_index = 0u; scene_index < 16u; scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        uint8_t led = (uint8_t)(LED_SEQ1 + scene_index);
        uint8_t base = (uint8_t)(((present_mask & bit) != 0u) &&
                                 pat_sceneHasActiveSteps(scene_index));

        if ((blink_mask & bit) != 0u) {
            if (!led_isBlinkLed(led)) {
                led_setValue(base, led);
                led_setBlinkLed(led, 1u);
            }
        } else {
            led_setBlinkLed(led, 0u);
            led_setValue(base, led);
        }
    }
    if (seq_isRunning() && active_scene < 16u &&
        (present_mask & (uint16_t)(1u << active_scene)) != 0u &&
        (blink_mask & (uint16_t)(1u << active_scene)) == 0u)
        led_setValue(seq_ledState.beatPulse,
                     (uint8_t)(LED_SEQ1 + active_scene));
}
```

**Line:** 15349 (`if (pageNr == PERFORMANCE_PAGE) {` in the LED block of
`menu_switchPage()`)
**Action:** MODIFY
```c
    /*
     * S080 G8: the chain editor page keeps the PERF LED rows (mutes on VOICE,
     * Scene row on SEQ, looper on SELECT).
     */
    if (pageNr == PERFORMANCE_PAGE ||
        (pageNr == PATTERN_SETTINGS_PAGE && menu_chainMenuScene < 16u)) {
```

**File:** `Core/Menu/menu.h`
**Line:** after 460 (`void    menu_refreshPerfSceneLeds(void);`)
**Action:** ADD
```c
/*
 * S080 G8 scene chain editor (SHIFT+PLAY+SEQ in PERF).
 *
 * menu_chainMenuOpen() shows a present Scene's chain (returns 1 when opened),
 * menu_chainMenuView() switches the viewed Scene while open,
 * menu_chainMenuClose() returns to PERF, menu_chainMenuIsOpen() reports the
 * latch. Clients: buttonHandler.c. Affiliates: PATTERN_SETTINGS_PAGE,
 * scene_getChain*()/scene_setChain*().
 */
uint8_t menu_chainMenuOpen(uint8_t scene_index);
void    menu_chainMenuView(uint8_t scene_index);
void    menu_chainMenuClose(void);
uint8_t menu_chainMenuIsOpen(void);
```

### 11.9 menuPages.h — chain editor page

**File:** `Core/Menu/menuPages.h`
**Lines:** 153–156 (`/* PATTERN_SETTINGS_PAGE */ {` comment and empty
sub-page 0)
**Action:** MODIFY
```c
/* PATTERN_SETTINGS_PAGE */ {
  /*
   * S080 G8: sub-page 0 hosts the SHIFT+PLAY+SEQ scene chain editor: cn1,
   * nx1, cn2, nx2 (category Chain) of the Scene in menu_chainMenuScene. The
   * cells are PAR_CHAIN_* mirrors committed by menu_commitChainParam(). Pattern
   * repeat/next switching stays retired (the legacy ENABLE_EUKLID_PAGE SHIFT
   * path would also land here; it is compiled out).
   */
  {TEXT_CHAIN_COND1,TEXT_CHAIN_NEXT1,TEXT_CHAIN_COND2,TEXT_CHAIN_NEXT2,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_CHAIN_COND1,PAR_CHAIN_NEXT1,PAR_CHAIN_COND2,PAR_CHAIN_NEXT2,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

### 11.10 buttonHandler.c — SHIFT+PLAY chord

**File:** `Core/Hardware/frontPanel/buttonHandler.c`
**Line:** after the Step 5.1 variables (after `bh_shiftReleaseConsume`)
**Action:** ADD
```c
/*
 * SHIFT+PLAY chord arm (S080 G8).
 *
 * What: 1 between a PLAY press made with SHIFT held in PERF and the PLAY
 * release. Why: SHIFT+PLAY+SEQ opens the chain editor; PLAY in the chord never
 * toggles the transport (plan §4.1). Inputs: PLAY press/release. Outputs:
 * the SHIFT+SEQ PERF branch opens the editor instead of queuing. RAM: +1 byte
 * SRAM1. Affiliates: menu_chainMenuOpen().
 */
static uint8_t bh_chainPlayArmed = 0u;
```

**Lines:** 2181–2190 (`case BUT_START_STOP:` in `processPress()`)
**Action:** ADD at the top of the case
```c
    case BUT_START_STOP:
        /*
         * S080 G8: SHIFT+PLAY in PERF arms the chain-editor chord and is
         * consumed - it never toggles the transport. Outside PERF, SHIFT+PLAY
         * keeps its old meaning.
         */
        if (bh_state.selectButtonMode == SELECT_MODE_PERF &&
            buttonHandler_getShift()) {
            bh_chainPlayArmed = 1u;
            break;
        }
```

**Line:** in `processRelease()`'s `switch (buttonNr)`, before
`case BUT_MODE2:` (line 2530)
**Action:** ADD
```c
    case BUT_START_STOP:
        /* S080 G8: releasing PLAY ends the SHIFT+PLAY chord. */
        bh_chainPlayArmed = 0u;
        break;
```

The chord's SEQ half is in Step 8.11 (SHIFT+SEQ PERF branch); viewing
another Scene is Step 3; closing is Step 5.2.

---

## Step 12 — Not changed in this pass (stated so nothing is assumed)

* **Reload from source (`rld`, `r+r`).**  Deferred (D15).  The executor
  applies the mute and morph (and reset) parts only.
* **`ens` / `enl` scene-change divisions** keep the P1 one-bar
  approximation in `seq_sceneChangeDivUnits()`.
* **Edit lock during changeover (plan §5.3)** keeps the P1 behaviour.
* **Documentation files** (`STORAGE_SRAM_MANIFEST.md` region size
  10,526 → 10,527 B; the PERF/looper sections of the spec references) are
  updated at session close, not in code.

---

## Step 13 — Build and verification

### 13.1 Build

1. `make all && make img`.  Expect no warnings.  Any remaining
   `PAR_SCENE_PC_RESET`, `*_SCENE_PC_RESET`, `seq_loopTickCount`,
   `led_initPerformanceLeds`, or `seq_loopStartStep` (outside sequencer.c)
   reference is a missed edit.
2. Static asserts to watch: `pat_scene_region_t` (+31u, both copies),
   AutoSave Scene groups (`COUNT = 76 ≤ 118`), `PAR_TRACK_LOOPABLE <
   NUM_PARAMS`, and the filesystem stage-cache assert at
   filesystem.c:1121–1127 (`scene_settings_t` grows by 4 B).  If the stage
   assert fails, stop and report; do not raise `FS_STAGE_CACHE_BYTES`
   without approval.
3. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`; report
   text/data/bss deltas.  Expected bss ≈ +127 B (Section 2).

### 13.2 Hardware test matrix

| Row | Setup | Action | Expected |
|-----|-------|--------|----------|
| T1.1 | PERF, qcd = ins | SHIFT+SEQ another Scene | Changes at once; its queue action applies (Defect 1). |
| T1.2 | Transport stopped | SHIFT+SEQ | Changes at once. |
| T2.1 | PERF | Hold SHIFT, press PLAY, press SEQ 3 | Chain editor for Scene 3 opens; transport unchanged. |
| T2.2 | Editor open | Release SHIFT, PLAY, SEQ | Editor stays open, SHIFT LED lit (user report 3). |
| T2.3 | Editor open | Press SEQ 5 | Editor shows Scene 5's chain; SEQ 5 and the active Scene blink. |
| T2.4 | Editor open, running, chain fires | — | Viewed Scene unchanged; the active-Scene blink moves. |
| T2.5 | Editor open | Press SHIFT, release | PERF page, SHIFT LED off after release, looper LED intact. |
| T2.6 | cn1 = b03 / nx1 = stp; cn2 = b04 / nx2 = slf | Run 8 bars | 3 bars play, 1 bar frozen, repeat. |
| T2.7 | cn1 = r5x / nx1 = nxt; cn2 = r6x / nx2 = prv | Run | One of the two fires at every repetition end (sum > 100, weighted). |
| T2.8 | cn1 = xal / nx1 = s04 | Manual SEQ change before the end | Chain cancelled, counts restart. |
| T2.9 | Chain set, save/load Bank | — | Chain survives sceneset and AutoSave restore. |
| T3.1 | Any page | SHIFT+REC, release both | Menu stays open; SHIFT and REC lit. |
| T3.2 | Menu open | SHIFT press / REC press | Closes to the previous page. |
| T4.1 | PERF idle | — | No SELECT LED lit; scrolling PERF sub-pages keeps the row dark. |
| T5.1 | Global menu | Scroll | One pcr (sub-page 1); sub-page 2 = scd qcd qac cmp cam ctm csc. |
| T7.1 | All 1/16, hold VOICE1, press SELECT5 (1/16) | Listen against the other tracks | Track 1 repeats the step that was sounding, on the grid. |
| T7.2 | Loop running on tracks 1 | Hold SELECT, press VOICE2 | Track 2 joins at its own start; track 1's loop unchanged. |
| T7.3 | Tracks 1+2 looping | Release VOICE1 | Track 1 resumes in time with the non-looped tracks. |
| T7.4 | Loop running at 1 bar | Press SELECT5, then BAR1, BAR2, release | Every wrap stays on the grid; loop starts never move. |
| T7.5 | Track scale 1/8 on track 2, Morph scale sweep | Loop, release | Released tracks in time with their unlooped neighbours. |
| T7.6 | qnt = 16 | Engage mid-step | Loop starts on the 1/16 grid. |
| T8.1 | Hold VOICE1, SELECT3; release SELECT3 | — | Loop continues; SELECT3 LED stays lit. |
| T8.2 | Then release VOICE1 | — | Loop ends; track 1 back in time; SELECT row dark. |
| T8.3 | Hold SELECT, hold BAR2, release SELECT | — | Loop continues (dotted) until BAR2 release. |
| T8a.1 | Track 7 lop = off | SELECT with no VOICE | Tracks 1–6 loop, track 7 runs free. |
| T8a.2 | lop off, save/load Pattern | — | Persists; old Patterns load as loopable. |
| T9.1 | Hold VOICE1 + SELECT (looping) | Press SEQ 4 | Full scene change, not per-track. |
| T10.1 | Running, qcd = 1b | SHIFT+SEQ | Queued SEQ LED flashes at double rate continuously until the change (user report 2). |
| T10.2 | Queue pending | Transport stop | Flash stops, queue cancelled. |
| T11.1 | Tracks muted, qac = mut | Queue fires | Sequencer and VOICE LEDs unmuted. |
| T12.1 | pcr off | Direct change mid-bar, every scd | Tracks continue from master-clock position. |
| T12.2 | pcr on | Direct change | Tracks restart from the beginning. |
| T14.1 | Loop on a 1/16 track at step 5 | Change to a Scene with that track at 1/8 | Loop start becomes step 3; wraps keep their timing. |
| T14.2 | Loop running, pcr on | Direct change | Loop restarts at the beginning of the new Scene; loop stays engaged. |
| T15.1 | qac = res / u+r / o+r / mmr | Queue fires, pcr off | Reset happens; mutes/morphs per option. |
| T15.2 | qac = nrm, pcr on | Queue fires | No reset (queued changes ignore pcr). |
| T15.3 | qac = mrp | Queue fires | Incoming Scene's mrp, 1VM..6VM and fxm read 0. |

---

## 14. Implementation log (executed pass)

Status: **implemented in one pass, builds clean** (2026-10-11, branch
`dev-ph6-cleanup`, working tree based on `e621884`).  All fifteen defects and
8a plus Phase G are in the source.  Hardware testing of the §13.2 matrix is
pending (the user runs it).

### 14.1 Steps applied

| Step | Defect(s) | Result |
|------|-----------|--------|
| 0 | shared enums/name tables | `PAR_SCENE_PC_RESET` + `*_SCENE_PC_RESET` removed; `PAR_CHAIN_COND1/NEXT1/COND2/NEXT2`, `PAR_TRACK_LOOPABLE`, their TEXT/SHORT/LONG entries and valueNames rows added; `sceneQueueActionNames` grown to 9 (nrm..r+r); dtype table extended. |
| 1 | 5 + 6 | `pcr` settings key/default/writer removed (`lines=24`), Scene sub-page 2 is now `scd qcd qac cmp cam ctm csc EMPTY`, BC18 check looks at top4/top8, `pcr` commit comment rewritten. |
| 2 | 4 | `led_initPerformanceLeds()` deleted; PERF SELECT row is looper-only via `buttonHandler_loopSelectLed()` + `menu_syncPerfSelectLed()`; SHIFT-release PERF branch no longer wipes the looper LED. |
| 3 | 9 | non-SHIFT SEQ router gains `&& !seq_loopActive` plus the chain-editor view branch. |
| 4 | 11 + 15 | `menu_queueActionFlags()` (0..8), `menu_applyQueuedSceneAction(target)` returns flags, executor takes `restart`, `menu_serviceDeferredSceneChange()` handles queue/chain-repeat/direct; new `buttonHandler_clearAllMutes()`; qac clamp 0..8 + name count 9; qac sanitized on load. |
| 5 | 3 | latched SHIFT+REC/chain editor: `bh_shiftReleaseConsume`, SHIFT-press close, SHIFT-release ignore, MODE-close, overflow reconcile. |
| 6 | 1 | `seq_queueSceneChange()` instant/stopped fast path; `seq_requestSceneChange()` clears the chain marker; `seq_clearPendingSceneChange` + new `seq_cancelPendingDirectChange()`/`seq_pendingSceneIsChain()`. |
| 7 | 12 | audit only (no code): root cause is the pcr merge; fixed by step 4 + 9.7. |
| 8 | 10 | `LED_LAYER_FAST` (bit 4) + `LED_FAST_BLINK_TIME_MS` (125), `led_setFastBlinkLed()/led_getFastBlinkLed()/led_isBlinkLed()`, render priority pulse>fast>flash>blink, blink guards include FAST, tick toggle; `seq_queuedSceneTarget()`; `menu_syncQueuedSceneLed()`; SHIFT+SEQ no longer starts its own blink. |
| 9 | 7 + 8 + 14 | master-clock-anchored looper (`seq_loopAnchor/StartStep/StartAccum/StartPlayState`), `seq_loopShiftPosition()`/`seq_loopCanonicalPreAdvance()`/`seq_loopEventAnchor()`/`seq_loopSnapshotTracks()`/`seq_loopResumeTrack()`; `seq_loopEngage` returns mask, `seq_loopUpdateMask()` realigns dropped tracks before commit, `seq_loopDisengage` projects snapshots; wrap before the DDA advance; `seq_setStepIndexToStart()` rebases; `seq_selectActivePattern()`/`seq_alignActivePatternToScene()` realign+re-snapshot under PRIMASK; `buttonHandler_loopCheckDisengage()`/`loopForceRelease()`/`loopSelectLed()`, SELECT/VOICE/BAR rework, BAR holds sustain the loop. |
| 10 | 8a | `pat_scene_region_t.track_loop_exclude` (+1 B, asserts 30→31), `pat_setTrackLoopable()/pat_trackLoopable()`, PAT4 byte 4 writer + three readers, copy/clear, lop cell commit + Global no-op, SEQ page cell 6, `seq_loopableTrackMask()`. |
| 11 | 2 + 13 | Phase G: SceneData chain fields/constants/accessors, AutoSave cells 72..75 (+LIVE_BYTES 72→76), sceneset lines 18/19 + parser, chain runtime state + BankData include, chain engine (repetition/bar service, condition weights, next resolver, repeat-self), hooks in scene select/transport, chain editor in menu.c (`PATTERN_SETTINGS_PAGE` sub-page 0), `menu_refreshPerfSceneLeds()` rewrite with blink-set logic, switchPage LED block covers the editor, SHIFT+PLAY chord. |

### 14.2 Deviations from the schedule (all mechanical)

1. **`AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES`** in `Autosave.h` was raised 72→76.
   The schedule omitted it; the `AUTOSAVE_SCENE_PARAM_COUNT == LIVE_BYTES`
   assert (Autosave.h:393) requires it.  No record-layout or format change.
2. **`extern uint8_t seq_running;`** added near the S080 F7 state block:
   `seq_queueSceneChange()`/`seq_cancelPendingDirectChange()` read it before
   its definition.  No new storage.
3. **`seq_loopStartStep` family are now `static`** in sequencer.c (the header
   externs were removed); no other file referenced them.
4. **Forward declarations** added for `buttonHandler_shiftRecMenuEnter/Exit`
   and for the new static looper functions (`seq_loopRefreshSceneSnapshots`,
   `seq_loopRebaseToStart`, `seq_loopProcessTick`).
5. **Chain-engine static functions** (`seq_chainServiceTick`, etc.) are defined
   before `seq_processSchedulerTick()` and need no prototypes.
6. **Cosmetic:** the per-track DDA block in `seq_processSchedulerTick()` was
   re-indented one level to sit inside the new `if (!seq_chainStopped)` guard.
   Comments preserved verbatim.
7. The `menu_syncQueuedSceneLed()` prototype was added with the Step 4.2
   prototypes (it is defined after `menu_serviceDeferredSceneChange()` but
   called from it).
8. **Defect 8 mute suppression:** the Step 9.11 drop block sets
   `perf_voiceActionOccurred = 1u`.  The schedule's comment says the mute
   toggle is "already suppressed", but that only holds when a track joins via
   a VOICE press made *while* the loop runs (Step 9.14).  For the VOICE-then-
   SELECT engage order the VOICE press predates the loop and never set the
   flag, so releasing the looped VOICE would have toggled the mute.  The one
   assignment makes the schedule's stated behaviour true for both orders.

### 14.3 Build and resource measurements

Baseline (pristine `HEAD`/S080 P1, built in a temp tree) vs. this pass:

| Metric | P1 baseline | This pass | Delta |
|--------|-------------|-----------|-------|
| text | 549,592 | 555,664 | +6,072 |
| data | 412 | 412 | 0 |
| bss | 427,856 | 427,976 | **+120** |
| flash payload | 550,004 | 556,076 | +6,072 |

The +120 B bss is within the approved §2 total of +127 B (small alignment
savings in the Scene/Pattern/sequencer groups).  Flash headroom after the pass:
197,588 B of the 753,664 B application window.

`make all` has **no new warnings** (warning set is identical to the P1
baseline).  `make img` produced `build/LXRV2_lxr02.img` (556,076 B) with the
per-sector CRC stamping.  Static asserts that pass include
`pat_scene_region_t` (+31 B, both copies), the AutoSave Scene-chain groups
(`COUNT = 76 <= 118`), `PAR_TRACK_LOOPABLE < NUM_PARAMS`, and the
`scene_settings_t` stage-cache assert (`... +4 B`, no `FS_STAGE_CACHE_BYTES`
change needed).

### 14.4 Not changed (per Step 12)

`rld`/`r+r` source reload remains deferred (degrades to mute + morph + reset);
`ens`/`enl` keep the one-bar approximation; edit lock during changeover is
unchanged.  Specification references (`STORAGE_SRAM_MANIFEST.md` region size,
PERF/looper/chain spec sections) are updated at session close, not here.

---

## Step 14 — Post-hardware follow-up (user, after the P2 implementation)

Two minor points from the first hardware pass, **implemented in code** (not
just scheduled).  Line numbers below are in the tree *after* these edits.
The comment blocks quoted here are the ones now in the source.

Build after the change: `make all && make img` clean (only the existing
warnings in untouched code).  Flash 556,420 / 753,664 B (headroom 197,244 B).
No RAM change: both items reuse existing state and `const` tables.

### 14.1 Compressor settings together on the last page, with cues

**Request:** every bus-compressor setting is a Scene setting, so all four
(cmp, cam, ctm, csc) sit together on the last settings page with the `+`
cue.  In the Global settings nothing follows `qac`, and that screen ends with
the `^` cue to say a Scene-settings page follows.

**Why the Step 1.2 layout was wrong:** it put scd/qcd/qac and cmp on one
screen and cam/ctm/csc on the next.  The compressor was split across two
screens and shown next to Global parameters, and the Scene-ownership cue
`+` landed on the screen that started with scd.

**Changes:**

| File | Line | Action | Change |
|------|------|--------|--------|
| `Core/Menu/menuPages.h` | 44–76 | MODIFY | MENU_MIDI_PAGE sub-page 2 = scd, qcd, qac, then TEXT_EMPTY/PAR_NONE in columns 3–7.  New sub-page 3 = cmp, cam, ctm, csc, then an empty second half.  One trailing empty sub-page row removed (8 rows kept). |
| `Core/Menu/menu.h` | 31–41 | MODIFY | `MENU_GLOBAL_SCENE_SUBPAGE` 2u → 3u; comment updated. |
| `Core/Menu/menu.c` | 10360–10377 | MODIFY | `checkScrollSign()` global cues: sub-page 3 first half `+` (unchanged rule, new page); the sub-page directly before it, when it has no second half, returns `^`. |
| `Core/Menu/menu.c` | 12594–12611 | MODIFY | `menu_moveToMenuItem()`: MENU_MIDI_PAGE joins the S080 static cross-sub-page step, so the encoder crosses from qac (column 2) to cmp and back.  The generic path stops on the empty column 3. |
| `Core/Menu/menu.c` | 15266–15283 | MODIFY | `menu_switchSubPage()` (MODE MENU re-press cycle): a global sub-page with no second half advances to the next populated sub-page instead of jumping to sub-page 0, so the cycle still reaches the compressor page. |
| `Core/Menu/menu.c` | 16717–16733 | MODIFY | BC18 diagnostic check restored to the S074 shape: top1 = cmp, top4 = csc, top5 empty, nothing populated after it. |

In-code comment for the page table (menuPages.h, sub-page 2):
```c
  /*
   * Scene change / queue globals (S080 F3, P2).
   *
   * What: scd, qcd, qac in columns 0-2; columns 3-7 are TEXT_EMPTY/PAR_NONE,
   * so no parameter follows qac on this screen and it is the last Global
   * screen. Its scroll cue is '^' (a Scene-settings page follows). Why: the
   * compressor cells are Scene settings and must sit together on their own
   * page, marked '+' (user, S080 P2 follow-up). Encoder traversal crosses
   * from qac to the compressor page through the static cross-sub-page step
   * (menu_moveToMenuItem(), extended to MENU_MIDI_PAGE in S080 P2); the
   * MODE MENU re-press cycle reaches it through menu_switchSubPage().
   * Affiliates: checkScrollSign() ('^' cue), MENU_GLOBAL_SCENE_SUBPAGE.
   */
```
(sub-page 3):
```c
  /*
   * Scene-owned bus compressor page (MENU_GLOBAL_SCENE_SUBPAGE, S074 /
   * S080 P2).
   *
   * What: cmp, cam, ctm, csc together in columns 0-3; the empty second half
   * terminates traversal. Why: all four are Scene settings (active-Scene
   * mirrors fanned out through the VOICE edit mask), so they are shown
   * together on the last settings page with the '+' Scene-ownership cue.
   * New global pages go before this one and move MENU_GLOBAL_SCENE_SUBPAGE.
   * Accessors: global encoder traversal, checkScrollSign() ('+'). Affiliates:
   * menu_commitBusCompParam(), menu_devCheckGlobalSceneSubPage() (diagnostic
   * layout check).
   */
```
Cue code (menu.c, `checkScrollSign()`):
```c
        /*
         * S074 / S080 P2 settings cues.
         *
         * What: the Scene-owned compressor page (MENU_GLOBAL_SCENE_SUBPAGE,
         * the last populated sub-page) shows '+' for Scene ownership. The
         * last Global screen directly before it - the scd/qcd/qac sub-page,
         * which has no second half - shows '^' to say a Scene-settings page
         * follows (user, S080 P2). Other screens keep the existing first-
         * screen '^', next-page '*' and last-screen '<' rules. Inputs:
         * activePage, is2ndPage, has2ndPage(). Output: cue character.
         * Affiliates: menuPages.h MENU_MIDI_PAGE layout, menu_moveToMenuItem().
         */
        if (activePage == MENU_GLOBAL_SCENE_SUBPAGE && !is2ndPage)
            return '+';
        if (activePage + 1 == MENU_GLOBAL_SCENE_SUBPAGE && !is2ndPage &&
            !has2ndPage(activePage))
            return '^';
```
Traversal (menu.c, `menu_moveToMenuItem()`):
```c
    /*
     * S080 P2: MENU_MIDI_PAGE joins the cross-sub-page step.
     *
     * What: the scd/qcd/qac sub-page ends after column 2, so the generic path
     * would stop on its empty column 3 and never reach the compressor page
     * that follows. With the step, scrolling past qac lands on cmp and
     * scrolling back from cmp lands on qac. Full 8-column global sub-pages
     * behave as before (the step fires at column 7 / column 0, where the
     * generic path also crossed); the only change is that scrolling no longer
     * wraps from the first to the last global sub-page, where it already
     * stopped on an empty cell. Affiliates: menuPages.h MENU_MIDI_PAGE,
     * MENU_GLOBAL_SCENE_SUBPAGE.
     */
    if (menu_activePage == PERFORMANCE_PAGE ||
        menu_activePage == RECORDING_PAGE ||
        menu_activePage == MENU_MIDI_PAGE) {
```
MODE MENU cycle (menu.c, `menu_switchSubPage()`):
```c
            else if (menu_activePage == MENU_MIDI_PAGE) {
                /*
                 * S080 P2: a global sub-page with no second half (scd/qcd/qac)
                 * advances to the next populated sub-page (the compressor
                 * page) instead of jumping back to sub-page 0, so the MODE
                 * MENU re-press cycle still reaches every settings screen.
                 * After the last populated sub-page it wraps to 0 as before.
                 */
                menu_cell_t nextCell =
                    menu_resolveCell((uint8_t)(activePage + 1u), 0u);
                if (activePage < NUM_SUB_PAGES-1 &&
                    !menu_cellIsEmpty(&nextCell))
                    activePage++;
                else
                    activePage = 0;
                activeParameter = 0;
            }
```

Screen sequence after the change: `bpm syn qnt ch` (`^`) → `txf rxf mrt ssv`
(`*`) → `pcr cki co1 flw` (`*`) → `oit cpu ats pts` (`*`) → `scd qcd qac`
(`^`) → `cmp cam ctm csc` (`+`).

**Supersedes:** Step 1.2 (sub-page 2 layout) and Step 1.3 (diagnostic check
on top4/top8).

### 14.2 Longer queued-change division list (qcd)

**Request:** add 5b, 6b, 7b, 8b, 9b, 12b, 15b and 16b to `qcd` (QueDiv).

**Design:** `qcd` gets its own option list; `scd` is unchanged.  Bar
options sit in order before `ens`/`enl`:
`ins 1/8 1/4 1/2 1b 2b 3b 4b 5b 6b 7b 8b 9b 12b 15b 16b ens enl` (0..17).
Units on the 16-bit master step clock: 2, 4, 8, 16, 32, 48, 64, 80, 96, 112,
128, 144, 192, 240, 256 sixteenths; `ens`/`enl` keep the one-bar
approximation.

| File | Line | Action | Change |
|------|------|--------|--------|
| `Core/Menu/MenuText.h` | 288–314 | MODIFY / ADD | `sceneChangeDivNames` comment now scd-only; new `sceneQueueDivNames[]` (18 entries). |
| `Core/Sequencer/sequencer.c` | 365–398 | ADD | `seq_queueChangeDivUnits()` table (same order as the names). |
| `Core/Sequencer/sequencer.c` | 439, 504, 2672–2676 | MODIFY | Queue paths (`seq_queueSceneChange()` instant test, `seq_cancelPendingDirectChange()` re-arm, ISR boundary test) use `seq_queueChangeDivUnits()`; the direct path keeps `seq_sceneChangeDivUnits()`. |
| `Core/Sequencer/sequencer.c` | 303–315, 420–430 | MODIFY | State and `seq_queueSceneChange()` comments describe the two lists. |
| `Core/Menu/menu.c` | 5744–5755 | MODIFY | Clamp: scd 0..9, qcd 0..17. |
| `Core/Menu/menu.c` | 9938–9946 | MODIFY | Name resolver: qcd renders from `sceneQueueDivNames` (18). |
| `Core/Hardware/SD/filesystem.c` | 2623–2635 | ADD | `filesystem_sanitizeLoadedGlobals()`: qcd > 17 → 0, scd > 9 → 0. |
| `Core/Bank/Scene/Preset/ParameterArray.h` | 233–237 | MODIFY | Parameter comment: scd 0..9, qcd 0..17. |

In-code comment for the units table (sequencer.c):
```c
/*
 * Convert a queued scene change division (qcd) to master 1/16-note units
 * (S080 P2).
 *
 * What: 0 = instant (returns 0); 1..3 = 1/8, 1/4, 1/2 note; 4..12 = 1..9 bars;
 * 13 = 12 bars; 14 = 15 bars; 15 = 16 bars; 16/17 = end of shortest/longest
 * track (approximated by one bar, like scd). Why: the queued division has its
 * own, longer option list (5b..9b, 12b, 15b, 16b, user request) so a queued
 * change can land on long phrase boundaries; the direct scd list is unchanged.
 * The order must match sceneQueueDivNames[] (MenuText.h). Inputs: qcd value.
 * Output: units of the 16-bit master step clock (largest 256). Context: TIM3
 * ISR and foreground (pure). Accessors: seq_handleMasterBoundary(),
 * seq_queueSceneChange(), seq_cancelPendingDirectChange().
 */
```
Names table comment (MenuText.h):
```c
/*
 * S080 P2: queued scene-change division names (qcd, QueDiv).
 *
 * What: [0]=count; [1..18] = stored qcd value 0..17: instant, 1/8, 1/4, 1/2,
 * then 1..9, 12, 15 and 16 bars, then end-of-shortest-track and
 * end-of-longest-track. Why: a queued change is pre-programmed during a
 * performance and often lands at a phrase boundary, so qcd offers the longer
 * bar counts (5b..9b, 12b, 15b, 16b, user request) that a direct change does
 * not need. Rendered by the S080 static-param resolver because every
 * DTYPE_MENU id is taken. Affiliates: PAR_SCENE_QUEUE_DIV,
 * seq_queueChangeDivUnits() (sequencer.c, must keep the same order),
 * menu_clampCellValue() (clamp 0..17), filesystem_sanitizeLoadedGlobals().
 */
```
Settings note: a settings.cfg written before this change that stored qcd 8
(`ens`) or 9 (`enl`) now reads as 5b / 6b (backward compatibility not
required).  The settings key (`qcd`) and the `lines=24` count are unchanged.

### 14.3 Hardware checks for this step

| Row | Action | Expected |
|-----|--------|----------|
| T14.1 | MENU mode, scroll the encoder to the end | After `qac` the cue is `^`; the next screen is cmp cam ctm csc with `+`; scrolling back from cmp returns to qac. |
| T14.2 | MENU mode, press MODE MENU repeatedly | Cycle passes through scd/qcd/qac and the compressor page, then returns to bpm. |
| T14.3 | Edit cmp..csc | Values apply to the Scene(s) as before. |
| T14.4 | qcd = 16b, SHIFT+SEQ while running | Queued scene flashes, fires on the next 16-bar boundary of the master clock. |
| T14.5 | Scroll qcd through its range | ins 1/8 1/4 1/2 1b…9b 12b 15b 16b ens enl; scd still stops at enl after 4b. |
