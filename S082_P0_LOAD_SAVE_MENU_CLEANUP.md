# S082 P0 — Load/Save Menu Cleanup (session plan, revision 4)

*Opened 2026-10-17. Revision 2 (2026-10-18) added the first-round answers and
items 4–5. Revision 3 (2026-10-18) added the second-round answers. Revision 4
(2026-10-18) adds the third-round answers (§2.4): **no questions remain
open** and the plan is ready for implementation. Planning only: no code has
changed. This is the umbrella plan for Session 082. It covers five items:*

1. *Bank Load and Bank Save rework: all 16 Scenes by default, and a Merge
   mode on the copy/clear button.*
2. *Instrument Load: a `chg` row when the instrument type changes.*
3. *The background Bank load with Scene 17. Its design is decided in
   `S082_P1_LOAD_SAVE.md` (D1–D27). It is preparatory work for
   `S083_P1_MIDI_IMPLEMENTATION_PLAN.md` and is implemented this session. §7
   lists only the changes that items 1 and 3 force on each other.*
4. *Endless pots in Load/Save name editing are slow and laggy (a display
   fix only; every pot function stays the same).*
5. *BAR2 (`>`) in name editing moves the cursor without overwriting the
   character.*

Read first: `MEMORY.md` (RAM policy, the `filesystem_ack()` rule, Bank
Load/Save facts, edit-mask intent, Instrument Load `kit` row), `SCOPING_TARGETS.md`
Phase 6, `S082_P1_LOAD_SAVE.md`, `FILESYSTEM_SPEC.md` (Bank Load/Save,
`bankset.bcg` v2, HCNAMES source tokens, the Session 058 clean-Scene skip),
`AUTOSAVE.md` (Bank `scene_present_mask`), `COPYCLEAR_UTILITIES.md`
(`clear scene`), `042_SESSION_HANDOFF_LOG.md` and `044_SESSION_HANDOFF_LOG.md`
(Bank preview, OK/OW lifecycle, `kit` row).

§10 lists ambiguities, risks and open architectural questions. §11 lists the
questions still open before the first full code implementation.

---

## 1. Terms

| Term | Meaning |
|------|---------|
| **Present** | A resident Scene whose `bank_scenePresent()` bit is set. Every load sets it (`presetManager.c:92`, `:2631`), and so does a copy/clear destination. No edit path can write a non-present Scene: PERF selection, per-track and queued changes refuse it (`menu.c` `menu_perfSceneChangeExecute()`, `buttonHandler.c:1421/1467/1686`), and edit-mask fan-out skips it (`bank_sceneFanoutMask()`, `BankData.c:471`). Present therefore means "has state". On the card: the child folder `NN <name>/` exists. |
| **Content** | What PERF lights: present **and** at least one active (trigger) step (`pat_sceneHasActiveSteps()`, `PatternData.c:1216`). On the card: the same test on the child's PAT4 address array. The user's Merge wording "present (has kit and pattern)" means this. |
| **Selection** | The 16-bit SEQ matrix choice on the Bank page. Default: all 16 selected. A deselected Scene is shown unlit. |
| **Replace** | The default Bank Load/Save mode. It uses exactly what is there: every Scene is acted on, and a deselected Scene loads or saves empty. |
| **Merge** | The mode toggled by a tap on COPY. Only selected Scenes with content are acted on; everything else stays as it is. |
| **Played Scene** | `seq_activePattern` and every `seq_perTrackPattern[t]`. |
| **Source view / RAM view / Slot view** | What the SEQ LEDs show: the selected Load source Bank, the resident Bank as PERF shows it, or the selected Save slot. |

---

## 2. Decisions — binding

### 2.1 Round 1 (user, 2026-10-18)

| Id | Decision |
|----|----------|
| S1 | **COPY tap/hold.** A tap toggles Merge; a hold shows the other view and does not toggle. Same timing as the other buttons (e.g. PERF in PERF mode): `BUTTON_TIMEOUT` = `BUTTON_HOLD_DELAY_MS`, 200 ms (`config.h:458`). |
| S2 | **All 16 Scenes toggle**, unlit ones included, with the `led_flashGroup()` flash on each press. |
| S3 | **"Saved empty" = child `NN` removed from the slot.** A selected Scene is written exactly as it is: a Kit or partial Kit with no pattern data is written, not emptied. |
| S4 | **The object being acted with takes priority.** On a Load the loading Bank's objects overrule; on a Save the saving (resident) Bank's objects overrule. `.hcnames` sources stay accurate. |
| S5 | **Merge load while running:** Scene 17 only when a played Scene is loaded or emptied; only replaced Scenes lose their dirty state; whole-Bank AutoSave re-mark afterwards. `S082_P1` D25. |
| S6 | **RAM:** ~11 B new static SRAM1 accepted (§8). |

### 2.2 Round 2 (user, 2026-10-18)

| Id | Decision |
|----|----------|
| S7 | **Deselection (Q23).** Outside Merge, a deselected Scene loads empty or saves empty. |
| S8 | **Replace uses exactly what is there (Q24).** Replace Load and Replace Save copy every Scene exactly as it is. **Merge** writes only a selected Scene with content; it leaves the destination as it is when the source Scene has no content or is deselected. |
| S9 | **The active Scene never changes because of a load or a save (Q25/Q26).** <br>• Stopped: the active Scene stays the same. <br>• Running, and a played Scene is replaced: background load from Scene 17. <br>• Running, and no played Scene is replaced: load directly; the active Scene stays as it is. |
| S10 | **The viewed Scene never changes because of a load.** |
| S11 | **`bankset.bcg` `active_scene` is legacy.** Bank Load never applies it. It is used only at boot when no AutoSave record is available. Bank Save still writes it. |
| S12 | **Voice edit masks under Merge exclude each other (Load and Save):** <br>• each destination Scene that is kept removes from its mask every Scene the operation replaced; <br>• a source Scene deselected on the matrix is removed from the mask of every other Scene that included it. |
| S13 | **HCNAMES sources (Q27/Q28).** In a Merge Save, Scenes deselected on the matrix keep their source unchanged. Otherwise choose rows that make sense. **The `.hcnames` format may change freely**; the user deletes AutoSave and temp files from the card after the change. |
| S14 | **Pots (Q29–Q34).** Fix the display path only. Every pot function stays the same. The only control change is BAR2 (`>`): move the cursor without overwriting the character. |
| S15 | **RAM (Q35):** the 2 B COPY press tick is approved. |
| S16 | **Every recommendation not answered in rounds 1 and 2 is accepted** (§2.3). |

### 2.3 Accepted recommendations (S16)

| Q | Accepted rule |
|---|---------------|
| Q2 | While COPY is held: OK blocked; encoder slot moves allowed (the held view follows); MODE exits normally. |
| Q3 | Merge is separate for Load and Save, off at entry, and has the same retention as that page's selection (off after a completed Save). |
| Q5 | No blinks on the Bank page in either view. |
| Q7 | "Pattern data" = active (trigger) steps only, the PERF rule. |
| Q11 | A Replace Load of an empty Bank, or with nothing selected, is allowed (it empties all 16 Scenes). A Merge with nothing to act on is refused. |
| Q12 | Live PAT4 probe for content; a stored mask only if the B1 measurement is too slow. |
| Q13 | Retention resets as listed in §4.4. |
| Q14 | After a Bank Load the cursor returns to the type row; the selection and Merge are kept. |
| Q15 | MIDI CC0 is always Replace with all 16 selected, and the menu shows that state. |
| Q17 | `chg`: the lower-row click loads 000; a positive turn loads 000; a negative turn does nothing. |
| Q18 | No `kit` row after a `chg` commit until the nested session ends; going back to the old type shows `chg`. |
| Q19 | Stepping to another type and back without committing brings `kit` back with its snapshot (the invalidation moves to the commit), with the S054 cases retested. |
| Q20 | With several destination Scenes, `chg` is refused unless every selected Scene can take the type. |
| Q21 | Labels exactly `mr` (Load Merge) and `MR` (Save Merge, occupied slot). |
| Q32 | BAR2 moves right keeping every character; at the last cell it does nothing; it leaves edit mode as today. |
| Q33 | No pot step-size or acceleration change. |
| Q34 | The display fix covers every Load/Save pot (Load and Save pages, nested Instrument Load/Save). |
| Q10, Q26 | Superseded by S9. |
| Q30, Q31 | Not adopted (S14): pot 3 and pot 4 behaviour is unchanged. |

### 2.4 Round 3 (user, 2026-10-18)

| Id | From | Decision |
|----|------|----------|
| S17 | Q36 | **The active Scene is always present.** A stopped Replace Load that empties it leaves it present with default content: editable, AutoSaved, and written by a Replace Save as a default child (the `clear scene` precedent). |
| S18 | Q37 | **The view at the Scene 17 switch:** with `PAR_FOLLOW` on, the view follows playback onto 17 (edits and live record go to 17, D15/D16); with `PAR_FOLLOW` off, the viewed Scene stays where it was. The load itself never moves the view. |
| S19 | Q38 | **Replace and edit masks:** Load: loaded Scenes take their source entries; Save: the slot gets the RAM entries. In both, every emptied/removed Scene is reset to self and removed from every other entry. |
| S20 | Q39 | **Merge mask rule 2** also covers source Scenes skipped for having no content: every Scene that was not loaded/written is removed from the other entries. |
| S21 | Q40 | **New HCNAMES Scene-row tokens** `bNNN` (child of Bank NNN at the row's index) and `x` (intentionally empty). The boot reader treats `x` as empty with no notice. |
| S22 | Q41 | **After any Bank Save**, row 0 and `active_bank` = the target slot; written Scenes become `-` under it. Confirm today's Save row-0 behaviour in code during B3. |
| S23 | Q42 | **A present Scene with only default content** is written by a Replace Save as a full default child, not removed. |
| S24 | Q43 | **Merge Save edit masks:** written Scenes take their RAM entry minus every Scene not written; the slot's untouched children remove every written Scene from their entry (read from the slot's `bankset.bcg`); a missing or malformed manifest counts as "every entry = self". |
| S25 | Q44 | **Re-apply only on replaced content.** The active Scene runs the Scene activation worker only when its content was replaced. **This concerns only the Bank's stored `active_scene` parameter and the DSP re-apply. The user's actual active and viewed Scenes never change because of a load or save.** The only exception is the automatic move to Scene 17 for a background load (S9, S18). Apart from that, only the user changes Scenes. |

---

## 3. Baseline (checked 2026-10-17/18)

| Area | Today | Where |
|------|-------|-------|
| Bank Load selectable Scenes | Only child folders found by the preview scan (`menu_bankLoadPreviewMask`). The default selection is that whole mask. | `menu_loadSaveSelectableSceneMask()` `menu.c:8894`, `menu_bankLoadPreviewComplete()` `menu.c:8021` |
| Bank Load semantics | Always mask-selective: unselected and absent children leave the resident Scene, its HCNAMES rows and its present bit unchanged. The present mask is OR-ed. In effect this is already a merge without the content rule. | `FILESYSTEM_SPEC.md` "Current behavior", `AUTOSAVE.md` `scene_present_mask` |
| Bank Load active/viewed Scene | Applies `bankset.bcg` `active_scene`; phase 20 commit calls `seq_alignActivePatternToScene()` and `menu_setShownPattern()`. This is the MEMORY.md known bug "runtime Bank Load can switch the playing Scene". | `FILESYSTEM_SPEC.md`, MEMORY.md (S054 note) |
| Bank Save selectable Scenes | Only resident present Scenes; default = the present mask. | `menu_resetLoadSaveSceneSelection()` `menu.c:8929` |
| Bank Save semantics | Writes `bankset.bcg` and the selected children; unselected children in the slot are never touched (S057). Clean children skipped (S058 Option 2). Phase 45 OR-s the present mask and restores the per-Scene masks (`filesystem.c:19310`). | `FILESYSTEM_SPEC.md` |
| Bank LEDs | Lit = selected ∩ selectable. Bank Load also blinks the active Scene. No content test. | `menu_refreshLoadSceneLeds()` `menu.c:8956` |
| PERF LEDs | Lit = present ∩ trigger steps; active Scene blinks while stopped, beat pulse while running. | `menu_refreshPerfSceneLeds()` `menu.c:8533` |
| SEQ press | Toggles a bit. The last selected bit cannot be cleared (`next != 0`). | `menu_loadSceneButtonPressed()` `menu.c:9016` |
| Save slot preview | None. Only the root slot's existence (`OW`). | `menu_currentSaveWouldOverwrite()` `menu.c:6198` |
| OK/OW label | `ok ` / `OW`; `...` or child progress `NN.` while a command runs. | `menu_paintLoadSaveConfirmation()` `menu.c:287` |
| After a command | `menu_resetSaveParameters()` returns to the type row, resets the Bank slot to `bank_restoreBankSlot()` and **resets the Scene selection**. It also runs straight after a Save request is posted. | `menu.c:16138`, `menu.c:259` |
| COPY in Load/Save | Refused silently: `cc_modeAllowed()` allows only VOICE/STEP/PERF/FX. `LED_COPY` exists. | `copyClearSession.c:192`, `buttonHandler.c:2852` |
| Empty-Scene primitives | Two variants: boot `filesystem_bootReaderEmptyScene()` (`filesystem.c:29022`, sources `?`+R) and copy/clear `ccClear_runScene()` (`clearOps.c:368`; settings and Effect defaults, Kit defaults, present bit off, mask reset to self, rows lose `R`). | — |
| Voice edit masks | One 16-bit entry per Scene (`scene_mask_voice_edit[16]` in `bankset.bcg`); fan-out keeps only present members with a matching layout. | `BankData.c:471` |
| HCNAMES source tokens | `-` inherit (Instrument → Kit → Scene → Bank row 0), `?` unknown, `000..999` direct root slot, `@` direct root Instrument stem. There is no token for "child of Bank NNN" other than `-` under row 0. | `FILESYSTEM_SPEC.md` §HCNAMES |
| Instrument type change | `menu_instrumentLoadStepType()` (`menu.c:8264`) invalidates the `kit` snapshot and loads the new type's `.hcindex`, while the row still prints `kit <old name>` (`menu.c:11026`). A negative turn posts a restore of the snapshot that was just invalidated. | `menu.c:13160–13290` |
| Load/Save pots | Every pot delta ends in `menu_repaintAll()` (`menu_handleLoadSaveKnobDelta()`, `menu.c:9701`). No coalescing. | §5.1 |
| BAR2 in name edit | Moves right and **blanks the next cell**. | `menu_loadSaveBarButtonPressed()` |

---

## 4. Item 1 — Bank Load and Bank Save

### 4.1 Requirements (user, 2026-10-17)

| Id | Requirement |
|----|-------------|
| B1 | **Bank Load, Replace (default):** all 16 resident Scenes are replaced by the source Bank, exactly as it is. A source Scene that is empty or has no pattern data still replaces the resident Scene. A Scene deselected on the matrix is loaded **empty** (S7). |
| B2 | **Bank Load LEDs:** show the selected source as PERF would show it once loaded: no content or deselected → unlit. |
| B3 | **Bank Save, Replace (default):** all 16 resident Scenes are saved to the slot exactly as they are (S3, S8). A deselected Scene is saved **empty**: its child is removed (S3, S7). |
| B4 | **Bank Save LEDs:** show the resident Bank exactly as PERF shows it now; deselected → unlit. |
| B5 | **A COPY tap toggles Merge** on the Bank page (S1). `LED_COPY` is lit while Merge is on. |
| B6 | **A COPY hold shows the other view:** Bank Load → RAM view; Bank Save → Slot view. The matrix selection cannot change while COPY is held. |
| B7 | **Selection retention, Save:** kept until the Save runs, Load/Save is left, or another Load/Save type is selected. |
| B8 | **Selection retention, Load:** kept until another menu/mode is selected or another source slot is selected. |
| B9 | **Labels:** Load Merge `ok` → `mr`. Save Merge: blank slot `ok`; occupied slot `MR` instead of `OW`. |
| B10 | **Load Merge:** a source Scene with no content, or deselected, leaves the resident Scene unchanged (S8). |
| B11 | **Save Merge:** a resident Scene with no content, or deselected, leaves the slot's child unchanged (S8). |
| B12 | **No load or save changes the active or the viewed Scene** (S9, S10). |

### 4.2 Outcome per Scene n

| Mode | Acted on when | Action | Otherwise |
|------|---------------|--------|-----------|
| Load Replace | selected(n) ∧ source child n present | load child n exactly (with or without triggers) | resident Scene n **emptied** |
| Load Merge | selected(n) ∧ source content(n) | load child n | resident Scene n unchanged |
| Save Replace | selected(n) ∧ resident present(n) | write child n exactly (Kit, partial Kit, Effect, settings, with or without triggers) | slot child n **removed** |
| Save Merge | selected(n) ∧ resident content(n) | write child n | slot child n unchanged |

- Menu turns this into two masks at OK: `act_mask` (load/write) and
  `empty_mask` (Replace only: empty/remove). Merge always has
  `empty_mask = 0`.
- The filesystem still intersects `act_mask` with the children it discovers
  at execution (existing rule). A child that has vanished since the preview
  is treated as Replace → empty, Merge → unchanged.
- In Replace Save, a non-present resident Scene has default content and no
  state (Q24 check, §1), so removing its child is "exactly what is there".
  The emptied active Scene stays present (S17).

### 4.3 LEDs

| Page | Normal view (lit when) | COPY held (lit when) |
|------|------------------------|----------------------|
| Bank Load | selected(n) ∧ source content(n) | resident present(n) ∧ trigger steps(n) (PERF base) |
| Bank Save | selected(n) ∧ resident content(n) | Save slot content(n) |

- No blinks on either page (Q5). Today's active-Scene blink in Bank Load is
  removed.
- The PERF lit rule is reused through one helper shared with
  `menu_refreshPerfSceneLeds()`, so "same as PERF" cannot drift.
- An empty or missing source/slot shows no LEDs.
- While a preview scan runs, the matrix is dark and input is gated, as today.
- While Scene 17 is active the RAM view shows Scenes 1–16 only (D8).

### 4.4 Matrix selection

- One retained 16-bit selection for the Bank page (today's
  `menu_kitLoadSceneMask` takes the role). Its default is `0xFFFF`.
- Every SEQ press toggles its bit, unlit Scenes included, with the
  `led_flashGroup()` flash (S2). The "cannot clear the last bit" rule is
  removed: all 16 deselected is legal (Q11).
- Reset to `0xFFFF`:
  - Load: entering Load/Save, a type change, a page change (Load ↔ Save), a
    mode change, nested Instrument entry, a different source slot. A
    completed Load does **not** reset it.
  - Save: entering Load/Save, a type change, a page change, a mode change, a
    completed Save. Moving the Save slot does **not** reset it.
- `menu_resetSaveParameters()` must stop resetting the Bank selection after a
  Load. Today it resets it after every command.

### 4.5 Merge and the COPY button (S1)

- In Load/Save mode on the Bank type, ButtonHandler routes COPY to a new Menu
  handler before `copyClear_buttonPressed()`/`copyClear_copyPressed()`. On
  every other Load/Save type COPY stays refused, as today.
- **Press:** record the press tick (its own 2 B, S15). SEQ presses are
  consumed from this point on, and OK is blocked.
- **Hold:** at `BUTTON_TIMEOUT` (200 ms) the other view appears; Merge does
  not change. Encoder slot moves stay allowed and refresh the held view (Q2).
- **Release before the threshold (tap):** Merge toggles and `LED_COPY`
  follows.
- **Release after the threshold:** the normal view returns.
- `LED_COPY` shows the Merge state: set on Bank entry and on every view
  refresh, cleared when the type, page or mode changes.
- Merge is separate for Load and Save and follows that page's selection
  retention (Q3).
- **SHIFT+COPY** on the Bank page: same as COPY. Check at implementation: if
  live record can stay armed into Load/Save, the SHIFT+COPY live-erase branch
  (`buttonHandler.c:2862`) must not fire on the Bank page.

### 4.6 Labels

| Page | Mode | Slot | Label |
|------|------|------|-------|
| Load | Replace | — | `ok` |
| Load | Merge | — | `mr` |
| Save | Replace | blank / occupied | `ok` / `OW` |
| Save | Merge | blank / occupied | `ok` / `MR` |

`menu_paintLoadSaveConfirmation()` gains the Merge input. `...` and the `NN.`
child progress are unchanged.

### 4.7 Card content preview

The LEDs need content per child, not only presence:

- **Source view (Load):** extend the existing Bank child scan
  (`filesystem_requestScanBankScenes()`, `filesystem.c:30505`). Scan each
  found child's PAT4 address array (7 × 128 × 2 = 1,792 B) for a trigger bit.
  It returns two masks: present and content.
- **Slot view (Save):** the same scan, run on the Save page when the Save slot
  moves. Today no Save-page child scan exists.
- **Buffer:** stream the address array in small reads through an existing
  line or sector buffer. `fs_stage_workspace` (shared with the AutoSave
  writer) and `pat_background_region` (Scene 17) are not used.
- **Cost:** up to 16 child directory visits plus 16 PAT4 opens per slot move,
  gated as today. Measure it in B1 (R1). The fallback is a stored
  `scene_mask_content` in `bankset.bcg` (A1).

### 4.8 Bank-level data

**Active and viewed Scene (S9–S11).**
- No load or save changes `scene_getActiveIndex()`, `seq_activePattern` (except
  the Scene 17 switch itself) or the viewed Scene (`menu_shownPattern`).
- The Bank Load commit stops calling `seq_alignActivePatternToScene()` with
  the manifest's Scene, `menu_setShownPattern()` and
  `bank_selectActiveSceneForEditMask()` from `bankset.bcg`.
- `bankset.bcg` `active_scene` is read only at boot (no AutoSave record).
  Bank Save writes the resident active Scene; on Scene 17 it writes the
  Scene 17 original-index record (`S082_P1` §6, D20).
- When the active Scene's content was replaced (stopped, or loaded while
  playback runs from Scene 17), the active Scene is re-applied through the
  normal Scene activation worker. When it was not replaced, nothing is
  re-applied, which avoids an audible re-apply (S25).

**Voice edit masks** (`scene_mask_voice_edit[16]`).

| Mode | Load (source overrules, S4) | Save (resident overrules, S4) |
|------|-----------------------------|-------------------------------|
| Replace | Every loaded Scene takes its source entry. Emptied Scenes are reset to self and removed from every other entry (S19). | The slot's entries are written from RAM. Removed children are reset to self and removed from every other entry (S19). |
| Merge | Loaded Scenes take their source entry minus every Scene that was not loaded. Kept Scenes remove every loaded Scene from their entry (S12, S20). | Written Scenes take their RAM entry minus every Scene that was not written. The slot's untouched children remove every written Scene from their entry (S12, S24). This needs the slot's existing `bankset.bcg`. |

The active-bit invariant and the layout check in `bank_sceneFanoutMask()` stay
as they are.

**HCNAMES sources (S4, S13).** Two new source tokens, with no compatibility
constraint (S13, S21):

| Token | Meaning | Use |
|-------|---------|-----|
| `bNNN` | Child of Bank `NNN` at this row's own Scene index | A Scene row whose content came from a Bank other than the one in row 0 |
| `x` | Intentionally empty Scene (defaults) | A Scene row emptied by Replace (Load) |

Kit, Instrument, Pattern and Effect rows keep inheriting (`-`) through their
Scene row. So only the Scene row (1..16) changes, plus the child rows that an
empty Scene clears.

| Operation | Row 0 | Loaded/written Scenes | Kept / not-written Scenes | Emptied Scenes |
|-----------|-------|-----------------------|---------------------------|----------------|
| Replace Load | source slot | `-` (inherit row 0) | — | `x`, child rows blank |
| Merge Load | source slot | `-` | made explicit: `-` under the old row 0 → `b<old>`; direct `NNN`, `@`, `?`, `bNNN` unchanged | — |
| Replace Save | target slot (S22) | `-` | removed-from-slot Scenes that hold RAM content: made explicit as above | non-present: `x` |
| Merge Save | target slot (S22) | `-` | **unchanged** (S13), made explicit if they were `-` under the old row 0 | — |

The boot reader learns both tokens: `x` is an empty, non-present Scene with
no notice; `bNNN` resolves through `Bank/NNN/<two-digit index>`. The AutoSave
source fields carry the new values as ordinary `uint16_t` register values.
The encoding is checked against the register's flag bits at implementation.

**Other Bank data.**

| Topic | Load | Save |
|-------|------|------|
| Request | `preset_loadBank(slot, act_mask, empty_mask)` | `preset_saveBank(slot, act_mask, empty_mask, force)` |
| Empty | One shared empty-Scene primitive (A4): settings, Effect and Kit defaults, Pattern region init, present bit off except for the active Scene, which stays present (S17); edit-mask rule above, HCNAMES `x` | Exact-object delete of child `NN` (existing primitive), for every `empty_mask` bit whose child exists |
| Present mask | Assigned, not OR-ed: `(old & ~(act|empty)) | loaded` (changes the `AUTOSAVE.md` rule) | Unchanged in RAM |
| Bank identity | `active_bank` = source slot (as today) | `active_bank` = target slot (as today) |
| AutoSave | Mark loaded and emptied Scenes, the present-mask bytes and the changed mask entries. Merge: whole-Bank re-mark (S5) | As today |
| Clean skip (S058) | Loaded children become clean; emptied children are not | Unchanged; removed children lose their clean bit |

---

## 5. Items 4 and 5 — name editing (S14)

### 5.1 Item 4: diagnosis

The pot hardware and the delta rate are the same in every mode
(`endless_pot_check()`, `main.c:250`). The difference is the LCD path:

| Path | Repaint per event | LCD traffic |
|------|-------------------|-------------|
| VOICE and other pages, pots | `menu_knobs_dirty`, consumed by `menu_serviceKnobRepaint()` at most every 20 ms (`LCD_LIMIT_TICKS_PER_REFRESH`, `main.c:91`) | changed characters only |
| Load/Save, main encoder | `menu_repaint()`; `menu_repaintAll()` only on a click (`menu.c:13841`, whose comment states this rule) | changed characters only |
| **Load/Save, pots** | **`menu_repaintAll()` on every delta, for every pot, with no coalescing** | **the whole 32-character frame** |

How a full frame plays out:
- `menu_repaintAll()` sets the shadow `currentDisplayBuffer` to 127, so
  `sendDisplayBuffer()` queues all 32 characters: 64 ops plus about 4
  hardware-cursor ops.
- Each op takes about 1 ms on the TIM7 nibble state machine (5–6 ticks of
  200 µs, `lcd.c:502`), so one frame takes about 70 ms to drain.
- Meanwhile the next frame does not fit: the queue holds 128 ops and a frame
  needs about 70. It is skipped (`menu_lcdRefreshPending`) and retried only
  when at least 72 ops are free (`menu.c:14824`).
- The result: a new frame every 70–80 ms at best, with each further delta
  queuing another full frame. A VOICE edit changes about 3 characters
  (≈ 6 ms).

Instrument Save also runs `filesystem_instrumentTargetExists()` on every
repaint for its `OW` label. Measure it; it is not the main cause.

### 5.2 Item 4: design (display only)

- Load/Save pot deltas set `menu_knobs_dirty` instead of repainting.
- On the Load/Save pages, `menu_serviceKnobRepaint()` calls `menu_repaint()`
  (which dispatches to `menu_repaintLoadSavePage()`) instead of
  `menu_repaintGeneric()`. The 20 ms coalescing and the `menu_storageBusy`
  guard stay.
- `menu_repaintAll()` stays only for real layout changes: entering a page or
  type, and a command's start and finish.
- The BAR handler's `menu_repaintAll()` becomes `menu_repaint()`.
- **No pot behaviour changes** (S14): pot 3's first-detent edit-mode exit and
  pot 4's name-cell rule stay as they are.

### 5.3 Item 5: BAR2

- BAR2 moves the cursor one cell right and leaves every character as it is.
- At the last cell it is consumed and does nothing.
- It leaves edit mode as today.
- BAR1 (backspace: blank the current cell, move left) is unchanged.
- It applies to every buffer `menu_loadSaveActiveNameBuffer()` returns.

---

## 6. Item 2 — Instrument Load `chg` row

### 6.1 Requirement (user, 2026-10-17)

When the selected type differs from the destination slot's resident type,
the lower row must not show `kit`. It shows `chg` and the name of the first
instrument (row 000) of the new type in the Library. There is no `kit`
snapshot step. Clicking `chg` with the encoder, or turning off it, loads
the new type and the row becomes a slot number for good.

### 6.2 Design (Q17–Q20 accepted)

- **New source state** `MENU_INSTRUMENT_SOURCE_CHANGE` (an enum value; no
  storage). It is entered when `menu_instrumentLoadStepType()` selects a
  normal type ≠ `menu_instrumentLoadBaseType`. It is left when the selected
  type equals the resident type again (back to `kit` with its snapshot), or
  when the change commits.
- **The `kit` snapshot** is invalidated only when a different type commits,
  not at the type step (Q19). The S054 owed-restore rule
  (`menu_instrumentKitRestorePending`) stays.
- **Render:** `chg` in cells 1..3. Cells 5..12 show
  `filesystem_copyInstrumentDisplayName(type, 0)` once the new typed
  `.hcindex` is resident; they are blank during the index handoff. No
  `.hctmp` save is posted.
- **Commit:** a click on the lower row while it shows `chg`, or a positive
  turn, calls `menu_instrumentLoadRequestSelection()` for index 0. A negative
  turn does nothing.
- **After the commit:** the source becomes `POOL` at index 0. There is no
  `kit` row until the nested session ends (voice, Scene or exit). Stepping
  back to the old type shows `chg`. A negative turn at 000 clamps at 000.
- **Multi-Scene destinations:** `chg` is refused unless every selected Scene
  can take the type (`instrumentManager_typeSelectableForSceneSlot()`).
- **Busy path:** the existing deferred-selection rules apply; the commit
  counts as a pool row.
- **InstrumentMrp** never shows `chg`.

---

## 7. Item 3 — Background Bank load (Scene 17): interactions with item 1

`S082_P1_LOAD_SAVE.md` stays the authority for the Scene 17 mechanism. S5 is
recorded there as D25, S9–S11 as D26, and S18/S25 as D27.

| P1 ref | Interaction | Status |
|--------|-------------|--------|
| D6, §4.4 | Apply the §4.2 outcome instead of always replacing 1–16. | Decided (D25) |
| D1, D17 | Only replaced Scenes lose dirty state; whole-Bank re-mark after a Merge. | Decided (D25) |
| D2–D5 | No Scene 17 when no played Scene is loaded or emptied. Replace acts on all 16, so a running Replace load always uses Scene 17; only Merge can skip it. | Decided (D25) |
| D14, §4.4 | Stopped: the active Scene stays (the manifest's `active_scene` is not applied). | Decided (D26) |
| D15, D16 | The load never moves the view (S10). At the Scene 17 switch the view follows playback onto 17 only with `PAR_FOLLOW` on. | Decided (S18, `S082_P1` D27) |
| D20 | Bank Save on 17 writes the Scene 17 original index as `active_scene`. | §4.8 |
| D7, §4.1 | MIDI CC0 = Replace, all 16 selected. | Accepted (Q15) |
| §4.4 | The content probe uses neither `pat_background_region` nor `fs_stage_workspace`. | §4.7 |
| — | MEMORY.md known bug "runtime Bank Load can switch the playing Scene". | Closed by D26 + Scene 17; record at close |

**Order:** item 1's load semantics (masks, empty primitive, active-Scene
rule) come first. Scene 17 is built on that one load path.

---

## 8. Resources (RAM policy)

| Item | Bytes | Region | Owner | Status |
|------|------:|--------|-------|--------|
| Bank page Merge flags (Load, Save) + COPY-held flag | 1–2 | SRAM1 static | Menu | Accepted (S6) |
| Slot-view content mask + its slot + valid | ~5 | SRAM1 static | Menu | Accepted (S6) |
| Source content mask | 2 | SRAM1 static | Menu | Accepted (S6) |
| Filesystem content mask result | 2 | SRAM1 static | filesystem | Accepted (S6) |
| COPY press tick (tap/hold) | 2 | SRAM1 static | ButtonHandler | Accepted (S15) |
| Probe read buffer, `chg` state, pot coalescing, HCNAMES tokens | 0 | existing | — | Reuse |
| **Total new** | **~13** | SRAM1 | | Scene 17 allocations: `S082_P1` §6 |

Any extra buffer that the Merge Save mask merge needs to read the slot's
`bankset.bcg` must fit an existing parser state (`op_bankset_state`).
Otherwise it needs a new acknowledgement.

---

## 9. Phases and verification

| Phase | Content | Verify |
|-------|---------|--------|
| A1 | **Name editing** (§5): pot coalescing, diff repaint, BAR2. | Fast pot 3 and pot 4 turns on Kit, Scene, Bank, Pattern and Instrument Save feel like VOICE; no corrupted characters with all four pots moving; pot behaviour unchanged; BAR2 keeps characters; BAR1 unchanged; `filesystem_instrumentTargetExists()` time per repaint (trace). |
| A2 | **Instrument `chg`** (§6). | Each type step shows `chg` and row 000's name; click and positive turn load 000; negative turn stays put; no `kit` after the commit; back to the resident type before committing restores `kit`; Mrp unchanged; rapid turns while busy; S054 cases; multi-Scene refusal. |
| B1 | **Content preview** (§4.7). Measure on the slow and fast reference cards. | Masks match a host check of the card copy; time per slot move logged. Report before B2. |
| B2 | **Bank page UI** (§4.3–4.6). OK still sends today's request. | LED matrix against PERF per mode and view; tap vs hold at 200 ms; retention matrix; COPY with other buttons and MODE; no leak into copy/clear. |
| B3 | **Operation semantics** (§4.2, §4.8): two-mask requests, shared empty primitive, Save child removal, present-mask assignment, active/viewed rule, edit-mask rules, HCNAMES tokens + boot reader, AutoSave marks. | Card round trips for 4 modes × {all, partial, none} × {empty, Kit-only, full} children; active and viewed Scene unchanged in every case; `.hcnames` sources; boot after each; `tools/verify_bank_autosave.py`; clean skip. |
| C | **Scene 17** per `S082_P1` §5 audit, §4.1–§4.6, D25, D26. | `S082_P1` §8, plus running Merge loads that do and do not touch a played Scene. |
| D | **Docs and close:** `FILESYSTEM_SPEC.md` (Bank Load/Save, HCNAMES tokens), `AUTOSAVE.md` (present mask, Merge marks, source values), `MODULE_INTERCHANGE_SPEC.md` (Load/Save UI, COPY, name editing), `STORAGE_SRAM_MANIFEST.md`, `COPYCLEAR_UTILITIES.md` (shared empty primitive), `BANK_PRESET_ARCHITECTURE.md` (`active_scene` legacy, mask rules), MEMORY.md (closed known bug, Load/Save pot repaint rule, card preparation: delete `.hcnames`, `.hcnamtmp`, `.hcprms1/2`). | — |

---

## 10. Ambiguities, risks and open architectural questions

### 10.1 Ambiguities

All resolved. AM1–AM15 by rounds 1–2 (§2.1–§2.3). AM16 (the emptied active
Scene) by S17, AM17 (the view at the Scene 17 switch) by S18, AM18 (Replace
and edit masks) by S19, AM19 (mask rule 2 and content) by S20. Ambiguities
found during implementation are added here.

### 10.2 Risks

| Id | Risk | Mitigation |
|----|------|-----------|
| R1 | The content probe adds up to 16 child visits and PAT4 opens to every slot move. | Measure in B1; fallback stored mask (A1). |
| R2 | Replace is destructive by default. | By request; `OW` remains the Save warning. Document it. |
| R3 | A Replace Save writes 16 children plus removals: longer; a power-fail leaves a mixed Bank. | S058 clean skip; non-atomic as today. |
| R4 | A Merge skips Kit-only Scenes. | By request; documented. |
| R5 | COPY is shared with copy/clear and live erase (`bh_clearHeld`, `copyClear_eventOverflow()`). | Route it before the copy/clear entry points; test MODE and an event-ring overflow while held. |
| R6 | `menu_resetSaveParameters()` runs after every command. | Bank-only retention branch. |
| R7 | A third empty-Scene variant. | One shared primitive (A4). |
| R8 | Present-mask assignment changes the AutoSave OR rule. | Load only; re-run `verify_bank_autosave.py`. |
| R9 | **New HCNAMES tokens reach every reader:** boot authoritative path, winner reader, AutoSave source fields, `tools/decode_devlogs.py`, `tools/verify_bank_autosave.py` and any card validator. | List every parser in B3; update the tools in the same phase. |
| R10 | Scene 17 skip conditions (D25) add branches to the riskiest path. | One load path; Scene 17 only in the request wrapper. |
| R11 | Moving the `kit` invalidation to the commit changes the S054 restore path. | Keep the owed-restore rule; retest S054. |
| R12 | **Removing the active/viewed realign** at the Bank Load commit (§4.8) undoes the S054 "Scene Load never loads the pattern" fix, which depended on that realign. It must not come back in another form: `seq_activePattern`, `menu_shownPattern` and `scene_getActiveIndex()` must already agree before the load and stay agreed. | Assert agreement at the commit; retest the S055 round trip (PERF switch, then Scene Load). |
| R13 | **Edit-mask merging** reads the slot's old `bankset.bcg` during a Merge Save. A malformed or missing manifest needs a rule. | Treat a missing manifest as "every entry = self". |
| R14 | Pot coalescing moves Load/Save repaints onto the shared knob service next to other repaint owners (child progress, browser callbacks, `...`). | `menu_repaint()` only; the existing guards stay; test pots during an index load and during `...`. |
| R15 | Not re-applying an active Scene that a Merge did not replace (§4.8) changes today's always-apply ending. | Apply exactly when the active Scene's content changed. |

### 10.3 Open architectural questions

| Id | Question | Leaning |
|----|----------|---------|
| A1 | Content from a live PAT4 probe or a stored mask in `bankset.bcg`? | Live probe (Q12 accepted). |
| A2 | Who computes `act_mask`/`empty_mask`? | Menu, from a coordinate-checked preview; filesystem intersects with discovery. |
| A3 | Save removal inside the per-child loop or as a separate pass? | Inside the loop. |
| A4 | Owner of the single empty-Scene primitive. | SceneData + Preset, called by Bank Load and `clear scene`; the boot reader keeps its own call site but uses it. |
| A7 | COPY router location. | ButtonHandler branch calling Menu; copy/clear unaware. |
| A8 | Encoding of `bNNN` and `x` in the `uint16_t` source register beside `FS_RESIDENT_SOURCE_REFRESHED_FLAG`/`DIRTY_FLAG` and `0x1ffc..0x1fff`. | A dedicated range above 999 checked against every flag mask. |
| A9 | Load/Save repaint ownership. | Pots and BAR share the coalesced request now. |
| A10 | **Where the edit-mask merge lives.** Load: BankData after commit, given `act_mask`/`empty_mask` and the parsed source entries. Save: the filesystem Bank Save, given the RAM entries and the slot's parsed entries. | One pure BankData helper `bank_mergeVoiceEditMasks(kept_entries, incoming_entries, replaced_mask, excluded_mask)` used by both. |
| A11 | **Is `bankset.bcg` `active_scene` still needed at all?** S11 keeps it for boot without AutoSave. | Keep it; document it as boot-only. |

---

## 11. Open questions

None at revision 4. Round 3 (Q36–Q44) is recorded as S17–S25 (§2.4).
Questions that come up during implementation go here, with the phase that
raised them.
