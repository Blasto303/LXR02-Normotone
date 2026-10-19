# S082 P3 — Work while a background Bank load runs

*Opened 2026-10-19 after the S082 P2 fixes were confirmed on hardware.
Specification only: no code has changed. Updated 2026-10-19 with the user's
answers (§2.2, D40–D53) and the RAM approval (§8). Every question is answered;
§10 lists only the four adopted recommendations, which stand unless the user
overrides them. The implementation schedule is next.*

Read first: `S082_P1_LOAD_SAVE.md` (D1–D39), `S082_P1_IMPLEMENTATION.md`
§12–§14, `S082_P2_IMPLEMENTATION.md`, `MEMORY.md` (RAM Allocation Approval
Policy, DSP CPU policy, Load/Save Slot Scrolling Policy).

---

## 1. Goal

A Bank load with audio playing still takes 10–20 s for 16 children, and the
user does not want to tune it any further. Instead, the panel stops being
locked to `Load:[Bank]` for that whole time. The user can leave Load/Save,
edit, perform and change Scene while the load runs in the background.

The approach is as permissive as possible:
- the menus are not locked out;
- an individual operation that would collide with the load is dropped silently when it runs.

---

## 2. Requirements and decisions — binding

### 2.1 Requirements (user, 2026-10-19)

| Id | Requirement |
|----|-------------|
| W1 | While a load runs and the panel is **not** in Load/Save mode, the Load/Save mode LED flashes rapidly (2× the normal blink rate). |
| W2 | In Load/Save mode during a load, only the Bank Load screen can be shown (`Load:[Bank   ]` / `NNN <name> NN.`), and nothing on it can be operated. |
| W3 | Parameters and Morph can be changed. Functions that would need the Scene 17 swap cannot run. |
| W4 | In PERF mode, the SEQ LEDs of Scenes not yet loaded are dark and fill in left to right as the children load. The user can switch from Scene 17 to another Scene before the load completes. |
| W5 | No other load speed-ups are part of this work. |

### 2.2 Decisions (user, 2026-10-19)

"Answered" rows are the user's answers. "Adopted" rows are recommendations
from the first draft that the user did not address; they stand unless the
user overrides them (§10).

| Id | Decision | Source |
|----|----------|--------|
| D40 | **Scope.** The panel unlocks for loads on Scene 17 and for direct loads with the transport running. A load requested with the transport stopped keeps the P1 lock for its whole duration. | Q1, adopted |
| D41 | **Copy/clear is permitted.** The copy/clear session is not locked out. A job that would need the Scene 17 swap (an overlapping paste while 17 exists) or a Scene that is not available (§4) is dropped silently when it runs. Clears are allowed. | Q2, answered |
| D42 | **Merge-kept Scenes** are lit and selectable from the start; they are not being loaded. | Q3, adopted |
| D43 | **A loaded Scene without Pattern content stays dark** (normal PERF rule). The fill may have gaps; the W1 LED tells the user the load is still running. | Q4, answered |
| D44 | **Fan-out during the load.** Edits fan out through the edit mask, limited to Scenes that are available (loaded or kept). The user can switch freely and repeatedly between any available Scenes. | Q5, answered |
| D45 | **Viewed Scene not available** (`PAR_FOLLOW` off, view left on a Scene being replaced): STEP, Pattern-settings and SOM edits there are dropped silently until it loads. | Q6, adopted |
| D46 | **Child-failure overlay** shows only if the user is on Load/Save when the load ends. | Q7, answered |
| D47 | **RAM approved:** up to 4 B (§8). | Q8, Q9, answered |
| D48 | **Codec suspension (S058)** during an unlocked load applies only while the panel is on Load/Save. Elsewhere audio stays up when the transport stops. | Q10, adopted |
| D49 | **Frozen Load/Save screen:** the SEQ LEDs show the same as PERF (the fill). There is no Load↔Save toggle; the Load/Save button only shows the single Bank Load screen. `NN.` progress stays and always shows the live count. | Q11, Q13, Q14, answered |
| D50 | **No chaining during the load.** Chain bar and repetition counters keep running, but no chain condition is evaluated and no conditional change fires until the load completes. | Q12, answered |
| D51 | **`NN.` is 1-based.** The progress shows the Scene number being processed (`01.`–`16.`), not the zero-based slot. This is user-facing information. The indicator is shared, so Bank Save shows the same numbering. | Q14a, answered |
| D52 | **Manual vs automated Scene changes.** Anything where the user selects "change to this Scene" is processed when the target is available. That covers: SEQ press, deferred (`scd`) change, SHIFT+SEQ queue, and per-track (VOICE+SEQ) moves. Automated changes, whose result could depend on what has loaded so far, are not processed until the load completes: chain conditions and their `nxt`/`prv`/random targets. MIDI PC stays dropped (D13). | Q15, answered |
| D53 | **Copy/clear names wait.** A copied Scene keeps its old name until the load ends; the copy's names are then published. | Q16, answered |

---

## 3. Baseline facts (checked 2026-10-19)

### 3.1 What locks the panel today

The R7/D38 lock is five explicit gates, plus the general storage-busy gates
that apply to **any** Preset operation:

| Site | Gate | Effect during a background load |
|------|------|----------------------------------|
| `buttonHandler.c:1903` `handleModeButtons()` | `preset_backgroundBankLoadRunning()` | every MODE press dropped |
| `buttonHandler.c:2340` `buttonHandler_shiftRecMenuEnter()` | same | SHIFT+REC dropped |
| `menu.c:9868` `menu_loadInstrumentVoicePressed()` | command active or load running | VOICE on Load/Save consumed |
| `menu.c:16913` `menu_switchPage()` | load running | every page switch dropped |
| `menu.c:16915` `menu_switchPage()` | `menu_storageBusy` or Preset not idle | a Load/Save exit is queued in `menu_pendingPageSwitch`, any other switch dropped |
| `menu.c:14800` `menu_parseEncoder()` | `menu_storageBusy` or Preset not idle | encoder dead on **every** page (Load-number scroll for Kit only) |
| `menu.c:15343` `menu_parseKnobDelta()` | `menu_storageBusy` | pots dead on every page |
| `menu.c:17337` `menu_processPendingPageSwitch()` | same | queued exit waits for idle |
| `copyClearSession.c:588` `copyClear_copyPressed()` | `menu_isStorageBusy()` | copy/clear session refused |

Removing the five R7 gates is therefore not enough. While the command is
accepted, `menu_storageBusy` is 1 (`menu.c:247`), and from phase 4 on
`pm_status` is `PRESET_LOAD_IN_PROGRESS`, so the encoder, pots, page changes
and COPY stay blocked everywhere.

### 3.2 The load's own state

- **Phases** (`presetManager.c:3787`): 1 decide, 2 quiesce, 3 build/switch to Scene 17, 4 post the load, 5 wait, 6 MIDI preview wait. Phase values are 0–6 only, so **bit 0x08 of `pm_bg_phase` is unused**, although `PM_BG_PHASE_MASK` is 0x0F (`presetManager.c:112`).
- **Bank file:** `bankset.bcg`, including the new Bank's per-Scene edit masks (`op_bankset_state`), is parsed and finalised before any child (`filesystem.c:14552-14623`).
- **Emptied Scenes:** Replace-emptied Scenes are emptied all at once (phase 98, `filesystem.c:14855`) **before the first child loads**.
- **Child order and completion:**
  - children load in ascending slot order (`filesystem.c:14999`);
  - each child commits its settings and Kit in `filesystem_commitSceneStage()` **before** its Pattern and Effect I/O, so it is complete only when the loop moves past it;
  - a failed child's bit is removed from `op_bank_scene_load_mask` and set in `op_bank_scene_failed_mask` (`filesystem.c:14990-14997`).
- **Present mask and edit masks:** both are committed only after the last child (`filesystem.c:15025-15081`). Until then `bank_scenePresent()` reports the **old** Bank. The edit-mask merge, `bank_mergeVoiceEditMasksAfterLoad()` (`BankData.c:559`), gives:
  - a loaded child: its new entry ∩ loaded set, plus itself;
  - an emptied Scene: itself;
  - a kept Scene: its old entry minus the replaced Scenes, plus itself.
- **Progress cursor:** `filesystem_bankChildCursor()` (`filesystem.c:27276`) returns 0 before the first child and stays on the last child after the loop. It cannot tell "child 15 in progress" from "all children done".
- **Name cache:** the Bank Load does not use the shared browser name cache; HCNAMES has its own mirror (`filesystem.c:1058-1075`). Clearing the name cache when leaving Load/Save mid-load is safe for the load.

### 3.3 Completion assumes the user is still on the load

`menu.c:16224` (`PRESET_OP_BANK_LOAD`):
- runs a full Scene activation (`menu_startSoundApply()`) when the **current** active Scene is in the replaced set (`menu.c:16255`);
- otherwise acks and runs `menu_requestLoadCommandFinalIndexRestore()`, then `menu_finishLoadSaveCommand()` → `menu_resetSaveParametersEx(1)` (`menu.c:17362`). That resets Load/Save state, may reset the Load Scene selection LEDs, and repaints;
- shows `menu_showFilesystemErrorOverlay()` on child failures.

`menu_backgroundBankLoadAbort()` (`menu.c:10280`) re-requests the Load page
selection only when the page is Load/Save, so it is already safe off-page.

### 3.4 Scene changes and chains

- **PERF Scene change.** `menu_perfSceneChangeExecute()` (`menu.c:9041`) refuses a Scene that is not `bank_scenePresent()`. It then runs:
  - `scene_selectActive()`;
  - `seq_selectActivePattern()`, which releases Scene 17 (`sequencer.c:2155`);
  - `preset_startDrumsetApply()`.

  The drumset worker is ticked by `menu_pollPresetStatus()` whatever `pm_status` is (`menu.c:15901`).
- **SHIFT+SEQ queue:** `buttonHandler.c:1700`, gated by `bank_scenePresent()`.
- **Chains** are evaluated in the TIM3 ISR. `seq_chainServiceTick()` (`sequencer.c:5696`) advances the counters, then calls `seq_chainEvaluate()` (`sequencer.c:5636`). Scene 17 has no chain (C9), but a Scene switched to mid-load has one.
- **Pending changes:** `seq_switchToBackgroundScene()` clears any pending direct or queued change (`sequencer.c:2311`).
- **Live record** targets the recording track's played Scene (`LiveRecord.c:346`), so it follows whatever the tracks play.

### 3.5 Edit fan-out

- **While 17 is active:** `bank_editTargetMask()` (`BankData.c:495`) is Scene 17 only (C4).
- **After a switch to a resident Scene:** it uses that Scene's edit-mask entry and `bank_sceneFanoutMask()` (`BankData.c:471`). That filters members with `bank_scenePresent()` and `scene_editLayoutMatches()`. Mid-load, the entries and the present mask are the **old** Bank's.

### 3.6 Copy/clear

- An overlapping paste while Scene 17 exists already drops at run time (`CC_RUN_DROP`, `copyClearService.c:682`). Its snapshot needs the background region, which is Scene 17's Pattern.
- After 17 is released, the region is free. The Pattern AutoSave snapshot cannot run while the load holds the AutoSave yield, so the paste is not blocked once 17 is gone.
- **Name publication:**
  - jobs that change names publish them at the end through `filesystem_requestCopyResidentNames()` (`copyClearService.c:1680`);
  - this needs the borrowed name buffer, which can only be borrowed while the facade is idle;
  - it retries for `CC_NAME_RETRY_TICKS` (1,000) and then **gives up**, dropping the names (`copyClearService.c:1688-1696`).

  A 10–20 s load would exhaust the retries.
- Content changes never borrow the name buffer.

### 3.7 LEDs

- **Fast-blink layer:** `LED_LAYER_FAST` holds **one LED** (`led_fastBlinkLed`, `ledHandler.c:207`). Its half-period is 125 ms, exactly 2× the 250 ms normal blink (`ledHandler.c:75-80`). Its only owner is `menu_syncQueuedSceneLed()` (`menu.c:9204`), which drives the queued Scene's SEQ LED in PERF.
- **Mode LED:** the Load/Save mode LED is `LED_MODE4`, lit steady in Load/Save (`led_setMode2()`, `ledHandler.c:1169`). MENU mode (7) also uses `LED_MODE4`, blinking at the normal rate. The fast layer has priority over blink and survives `led_setMode2()`.
- **PERF Scene LEDs:** `menu_refreshPerfSceneLeds()` (`menu.c:8948`) lights Scenes with Pattern content (`menu_residentContentSceneMask()`), filtered by the old present mask. It is refreshed per beat and on Scene changes.

### 3.8 Transport stop during a load

`menu_updateNoPlaybackStorage()` (`menu.c:1934`) suspends the codec (S058)
whenever the command is active, in scope and the transport is stopped.

---

## 4. Definitions

| Term | Meaning |
|------|---------|
| **Unlocked load** | A background Bank load in scope (D40) that has reached phase 5, i.e. the filesystem has accepted the Bank Load. Phases 1–4 and 6 keep the P1 lock. |
| **Loaded** (child) | A child in the request's load set that the children loop has passed without failure. Monotonic: once loaded, it stays loaded for the rest of the load. |
| **In progress** | The child at the cursor. |
| **Pending** | A load-set child not yet reached. |
| **Emptied** | A Replace Scene outside the load set; empty from the start of the children. |
| **Kept** | A Merge Scene outside both sets; untouched by the load. |
| **Failed** | A child that failed; not present after the load. |
| **Done mask** | The 16-bit loaded set, kept by the filesystem (§7.2). |
| **Available** | `done ∪ (kept ∩ bank_scenePresentMask())`, plus Scene 17 while it exists, for playing only (D28 still forbids a return). Emptied, failed, pending and in-progress Scenes are not available. The present mask is not changed until the metadata commit, which does not touch kept bits, so the term is stable for the whole load. |

---

## 5. Behaviour

### 5.1 When the panel unlocks (D40)

1. **Scope:**
   - a load on **Scene 17** (`PM_BG_NEEDED`);
   - a **direct load with the transport running** (D25/D31: a Merge that does not touch a played Scene). Its active Scene is kept by definition.

   A load requested with the transport stopped keeps the P1 lock throughout.
2. **Phases 1–4:** P1 behaviour unchanged. Input is dropped and `Load:[Bank]` is shown. These phases are short, and a Scene change before phase 3 has switched to 17 would race the build.
3. **Phase 5:** on entry, Preset sets the unlocked flag (§7.1). §5.2–§5.9 then apply until the load ends.
4. **MIDI-origin load:** D39 still closes everything at the start. The load unlocks when its phase 6 → 1 → … path reaches phase 5.

### 5.2 Load/Save mode LED (W1)

- **Outside Load/Save mode:** while the load is unlocked, `LED_MODE4` fast-blinks at 125 ms (2×).
- **In Load/Save mode:** steady as usual.
- **When the load ends** (success or failure): the blink stops, and whatever `led_setMode2()` set comes back (off, or MENU's normal blink).
- **Two fast LEDs:** the fast layer carries two LEDs (D47). The queued Scene's SEQ LED and `LED_MODE4` can be due together in PERF, and they share one phase and one deadline.
- **Owner:** a single owner (`menu_syncQueuedSceneLed()`, extended) derives both LEDs from state every tick, so no call site starts or stops the blink.

### 5.3 Load/Save mode during the load (W2, D49)

- **On entry** (MODE), the Bank Load screen shows exactly as at OK:
  - line 1 `Load:[Bank   ]`;
  - line 2 the loading slot `NNN`, the loading Bank's name, and `NN.` progress.
- **Progress:** `NN.` always reads the live cursor (`menu_paintLoadSaveConfirmation()`, `menu.c:301-330`), so a return mid-load shows the current child, never a stale value. It shows the 1-based Scene number, `cursor + 1` (`01.`–`16.`, D51). This matches the SEQ LED that is filling.
- **SEQ LEDs:** the same as PERF (§5.6 fill).
- **No interaction:**
  - a further Load/Save MODE press does nothing (no Load↔Save toggle);
  - encoder, pots, VOICE (Instrument entry), SEQ/BAR, COPY and Merge do nothing.
- **Leaving:** another MODE button works normally (§5.4).
- **Fresh-entry work is skipped:** no `menu_resetSaveParameters()`, no `menu_requestCurrentLoadSaveSelection()`, no Kit mask reset and no browser index request.
- **Slot and name come from the request:**
  - the slot is `pm_request_slot`;
  - the name is `op_bank_display_name`, captured at request time.

  Neither comes from the browser cache, which was cleared on exit.
- **Load ends while the user is on this screen:** D7 applies unchanged. The page unlocks exactly as after a normal Bank Load and stays there, with the child-failure overlay if any (D46).

### 5.4 Leaving and returning

- **Leaving works:** MODE (any other mode), SHIFT+REC and SHIFT+mode changes switch pages immediately; nothing is queued in `menu_pendingPageSwitch`.
- **Exit work done now** (safe with a busy facade): selection generation bump, preview cancel (none runs), name cache clear, COPY/Merge release.
- **Exit work deferred:** the resident-name session end (`end_resident_name_session`, `menu.c:16999`) posts an HCNAMES flush that needs the facade. It is **deferred to the load's completion**, not dropped.
- **Returning:** shows §5.3. Leaving and returning any number of times is allowed.

**Implemented as:** no deferral is needed. The page exit only invalidates the
name scratch and clears the browser cache, and the HCNAMES flush is already a
background write that waits for an idle card (schedule §0.6 b).

### 5.5 What runs during an unlocked load (W3, D41)

Nothing is locked out at the menu level except Load/Save itself (W2).
Operations that collide with the load are dropped silently when they run.
Nothing is queued to run after the load.

| Function | During an unlocked load |
|----------|-------------------------|
| Voice, Effect, PERF, Pattern-settings, Global and SHIFT+REC parameter edits (pots, encoder, MIDI CC) | **Run.** Targets per §5.7. |
| Morph (overall, voice, Effect; pots, SHIFT view, MIDI CC) | **Run.** Targets per §5.7. |
| Instrument `typ` / Effect `typ` changes | **Run** if they use only the runtime workers (§7.6 audit). Any path that posts a Preset/filesystem request is dropped. |
| Transport start/stop, mutes, solo, fill, rolls, looper | **Run** (already did in P1). |
| Live record, overdub, erase | **Run** (already did in P1). The target is a played Scene, which is always available. |
| STEP/SEQ step edits, Pattern settings, Pattern generator (SOM) | **Run** on an available viewed Scene; dropped silently otherwise (D45). |
| PERF Scene change (SEQ), deferred (`scd`) change, SHIFT+SEQ queue, VOICE+SEQ per-track move | **Run** to available Scenes; dropped silently otherwise (§5.6). |
| Chain editor (SHIFT+PLAY+SEQ) | **Runs** for available Scenes (edits settings only). Chains do not fire until the load ends (D50). |
| Copy/clear session (COPY, SHIFT+COPY, menus) | **Runs.** Each job is checked when it runs (§5.8). |
| Any Load or Save (Kit, KitMrp, Scene, Pattern, Instrument, InstrumentMrp, Bank, Globals, Samples, the `kit` row) | **Not reachable**: Load/Save is frozen (W2). |
| Global page items that start a card or Preset operation | **Dropped** (§7.6 audit). Plain Global parameter edits run; `settings.cfg` writes wait for the facade (D24). |
| MIDI PC, MIDI Bank Change, other non-RT/note/CC MIDI | **Dropped** (D13, D33 unchanged). |

**Implemented as:** D48 was already satisfied:
`menu_loadSaveCommandInNoPlaybackScope()` returns 0 off Load/Save, so leaving
the page ends the codec suspension with no code change (schedule §0.6 c).

### 5.6 PERF Scene LEDs and Scene changes (W4, D42, D43)

**LEDs.** While the load is unlocked, PERF, and the frozen Load/Save screen
(D49), paint:

| Scene | SEQ LED |
|-------|---------|
| Loaded | Normal PERF rule: lit if it has Pattern content; active/viewed blink; beat pulse. |
| Kept (Merge) | Normal PERF rule from the start. |
| Pending, in progress, emptied, failed | Dark. |
| Scene 17 | No Scene lit (D8). |

- **Fill:** it moves left to right because children load in ascending order. Loaded Scenes without content stay dark (D43).
- **Repaint timing:** the LEDs repaint when the done mask changes, not only per beat (§7.3).
- **At completion:** the normal PERF refresh runs with the committed present mask.

**Scene changes.**
- **Accepted only for available Scenes:** a SEQ press, a deferred (`scd`) change, a SHIFT+SEQ queue and a VOICE+SEQ per-track move are accepted only for an **available** Scene, and dropped silently at the press otherwise. Availability is monotonic, so a deferred or queued target stays valid until it fires.
- **Switching freely (D44):** the user can switch repeatedly between any available Scenes. Each switch is an ordinary activation (`preset_startDrumsetApply()`), which reads committed, complete data.
- **Leaving 17:** the first whole-Scene change from 17 releases it (D10). There is no return (D28).
- **After the release**, AutoSave stays held until the load ends. The yield is released only by `preset_bgEnd()`.
- **Per-track moves** do not release 17 (D10).
- **Manual vs automated (D52).** The rule is decided by who chose the target, not by the gesture:
  - **Manual, processed when the target is available:** SEQ press (direct or deferred by `scd`), SHIFT+SEQ queue (including its `qac` mute/morph/reset parts), and VOICE+SEQ per-track moves (to a Scene or back).
  - **Automated, not processed until the load completes:** chain conditions and every chain target (`sNN`, `nxt`, `prv`, random ranges) (D50, §5.9). They could pick a different Scene depending on what has loaded so far.

### 5.7 Edit targets and fan-out (D44, D45)

- **Active Scene 17:** edits go to 17 only (C4, unchanged).
- **Active resident Scene** (after leaving 17, or on a direct load):
  - edits go to the active Scene;
  - they also fan out to the members of its edit-mask entry that are **available** and pass `scene_editLayoutMatches()`.

  Pending, in-progress, emptied and failed Scenes never receive fan-out.
- **Edit-mask source during the load:** the entries must describe the **new** Bank for loaded children. The merge therefore moves to the start of the children (§7.2):
  - before the first child, `bank_mergeVoiceEditMasksAfterLoad()` runs with the request's load and empty masks, after the `bankset.bcg` parse;
  - at the end, instead of the full merge, only the failed children's bits are removed from every entry.

  User edits to edit masks during the load therefore survive. A failed child's entry becomes itself only.
- **Fan-out filter:** `bank_sceneFanoutMask()` uses availability instead of `bank_scenePresent()` during the load.
- **Viewed Scene** (STEP, Pattern settings, SOM): edits are dropped silently while the viewed Scene is not available (D45). The display shows whatever is resident until the Scene loads.
- **Per-track overrides:** a voice whose track plays another Scene edits that Scene under the existing per-track rules. That Scene is always available.

### 5.8 Copy/clear during the load (D41)

Copy/clear is not locked out (`copyClearSession.c:588` ignores an unlocked
load). Each job is checked when it runs:

| Case | Behaviour |
|------|-----------|
| Source or destination Scene not available (pending, in progress, emptied, failed) | Dropped silently (`CC_RUN_DROP`, new drop reason). |
| Overlapping paste while Scene 17 exists (the "Scene 17 swap") | Dropped silently (existing `CC_RUN_DROP`, `copyClearService.c:682`). |
| Overlapping paste after 17 was released mid-load | Runs (the region is free; the Pattern AutoSave snapshot is held by the yield). |
| Non-overlapping paste, clear, Scene 17 as source or destination | Runs. |
| Name publication of a finished job | **Waits** while the load runs: the retry counter does not advance while `preset_backgroundBankLoadRunning()`, and the job publishes after the load ends. It is never dropped because of the load. |

The Bank's own HCNAMES write happens at the end of the load. A copy job's name
publication must therefore come **after** it, and its rows must still be the
copied names (R14, §7.6 audit item 6).

### 5.9 Chains during the load (D50, D52)

- `seq_chainServiceTick()` keeps advancing `seq_chainBarCount` and `seq_chainRepCount`, but skips `seq_chainEvaluate()` while a background Bank load runs.
- A `stp` freeze already set stays set.
- **After the load ends:** evaluation resumes at the next bar or repetition boundary with the counts as they stand. A condition already satisfied by then can fire at the first boundary after completion.
- This applies to every background load, locked or unlocked.

### 5.10 Completion off the Load/Save page

When the load ends while the panel is not on Load/Save:

1. **No completion Scene activation.** In an unlocked load the active Scene is one of:
   - 17, which is never re-applied;
   - a kept Scene, which is not replaced;
   - a Scene the user switched to after it loaded, whose activation already applied the loaded data.

   So the `active_replaced` apply (`menu.c:16255`) is skipped for every unlocked load, on or off the page. A second activation would be audible.
2. `bank_revalidateVoiceEditMasks()` and the Merge `autosave_markResidentBankDirty()` run as today.
3. **Silent command end:** no final Bank index restore, no Load/Save reset, no Load LED reset and no Load page repaint. `menu_loadSaveCommandActive` and `menu_storageBusy` are cleared. The next Load/Save entry is a normal fresh entry.
4. The deferred resident-name session end (§5.4) runs.
5. **No child-failure overlay** (D46).
6. The W1 blink stops. The PERF LEDs repaint with the committed present mask. Copy/clear name publications waiting under §5.8 proceed.

When the user is on Load/Save at completion, the existing path runs (D7), with
step 1 applied.

### 5.11 Failure

- **Before phase 5:** P1 behaviour.
- **In phase 5:** the command ends as in §5.10, with the overlay only on Load/Save (D46). Playback stays where it is.
- **If the user had left 17:** the load ends without it, and nothing switches back.
- **Edit masks after a failed load:** entries of children that never loaded hold the new Bank's values over old content. Fan-out still requires a layout match, and present-mask rules apply after the load. Accepted (R15).

---

## 6. Amended decisions

| Id | Amendment |
|----|-----------|
| D7 | "Afterwards the UI unlocks on Load/Save and stays there" applies only when the user is on Load/Save at completion. Before completion the user may leave once the load is unlocked. |
| D11, D23 | During a load, an overlapping paste while 17 exists is dropped silently at run time; the copy/clear menu is not locked (D41). |
| D38 | The panel lock holds for phases 1–4 and 6, and for the whole of a stopped load. From phase 5 of an unlocked load, §5.5 replaces it. |
| C4 / fan-out | After leaving 17 during a load, fan-out uses the new edit-mask entries limited to available Scenes (D44, §5.7). |
| Chains (S080 G7) | No chain evaluation while a Bank load runs; counters continue (D50). |
| S058 | During an unlocked load the codec is suspended only while the panel is on Load/Save (D48). |

D8, D9, D10, D13, D27, D28 and D33 are unchanged.

---

## 7. Code impact (planning level)

The implementation schedule will give the full comment blocks; this section
fixes the sites.

### 7.1 Unlocked flag — Preset

| Site | Change |
|------|--------|
| `presetManager.c:112` | MOD `PM_BG_PHASE_MASK` 0x0F → 0x07; ADD `PM_BG_UNLOCKED` 0x08. No new RAM. |
| `presetManager.c:3908` (phase 4 → 5) | MOD: on entry to phase 5, set `PM_BG_UNLOCKED` when `PM_BG_NEEDED` is set, or when the transport runs and the active Scene is not in `act | empty`. |
| `presetManager.c:3752` `preset_bgEnd()` | Clears it with the rest (already zeroes `pm_bg_phase`). |
| `presetManager.h` | ADD `preset_backgroundBankLoadUnlocked()` and `preset_backgroundBankLoadAvailableMask()` (§4, from the done mask, `act`/`empty` and `bank_scenePresentMask()`). |

### 7.2 Done mask and edit masks — filesystem, BankData

**Implemented as:** the availability state lives in BankData as a 2 B
*unavailable* mask (`bank_load_unavailable_mask`), not as a filesystem done
mask; a completed child sets its present bit immediately and clears its
unavailable bit. This is the schedule's §0.6 a decision. `filesystem.c` keeps
no new per-operation mask.

| Site | Change |
|------|--------|
| `filesystem.c` op scratch (beside `op_bank_child_cursor`, 1410) | ADD `op_bank_done_mask` (2 B, D47). Cleared at Bank Load start; the child's bit is set when the loop leaves a child without failure (`filesystem.c:14990-15000`). |
| `filesystem.c` (beside `filesystem_bankChildCursor()`, 27276) | ADD `filesystem_bankLoadDoneMask()`: the mask while `FS_INTERNAL_OP_LOAD_BANK` runs, 0 otherwise. |
| `filesystem.c:14855` phase 98, before the first child | MOD: call `bank_mergeVoiceEditMasksAfterLoad()` with the request's load and empty masks (moved from the end). |
| `filesystem.c:15077` metadata commit | MOD: replace the full merge with removal of `op_bank_scene_failed_mask` bits from every entry (each entry keeps its own bit). The empty-Bank branches (`filesystem.c:14809`, `14918`) are unchanged. |
| `BankData.c:471` `bank_sceneFanoutMask()` | MOD: the member filter uses availability during a load. |
| `BankData.c:495` `bank_editTargetMask()` / `bank_sceneFanoutMask32()` | Unchanged (17 only while 17 is active). |

### 7.3 Menu

| Site | Change |
|------|--------|
| `menu.c:14800`, `15343`, `16915`, `17337` | MOD: the storage-busy gates ignore an unlocked load when the page is not Load/Save. On Load/Save they keep blocking (W2). |
| `menu.c:16913` | MOD: the R7 backstop applies only while the load is not unlocked. |
| `menu.c:9868` | Unchanged (Load/Save stays frozen). |
| `menu.c:16999-17030` | MOD: leaving Load/Save in an unlocked load defers `end_resident_name_session` to completion. |
| `menu.c:17168` (LOAD_PAGE entry) | MOD: in an unlocked load, enter the frozen Bank screen (§5.3) and skip the fresh-entry work. A press while already on it changes nothing. |
| `menu.c:16224-16278` | MOD: skip the `active_replaced` apply for unlocked loads. Off-page: skip the final index restore, the Load/Save reset and the failure overlay (§5.10). Run the deferred name-session end. |
| `menu.c:8948` `menu_refreshPerfSceneLeds()` | MOD: during an unlocked load, use availability for presence. Also paints the frozen Load/Save screen's SEQ LEDs (D49) in place of `menu_refreshLoadSceneLeds()`. |
| `menu.c:15804` `menu_pollPresetStatus()` | ADD: beside `menu_refreshBankChildProgress()`, repaint the SEQ LEDs when the done-mask popcount differs from the remembered one (1 B, D47; the mask is monotonic, so the count is enough). |
| `menu.c:9041` `menu_perfSceneChangeExecute()` | MOD: availability instead of `bank_scenePresent()` during an unlocked load. |
| `menu.c:9204` `menu_syncQueuedSceneLed()` | MOD: also owns the `LED_MODE4` fast blink (W1). |
| `menu.c:320-326` `menu_paintLoadSaveConfirmation()` | MOD: paint `child + 1` (D51). Bank Load and Bank Save share the indicator. |
| `menu.c:11786-11796` `menu_refreshBankChildProgress()` | MOD: compare against the same `child + 1` digits, or the change test repaints every pass. |
| STEP / SOM / Pattern-settings edit entry points | MOD: drop silently when the viewed Scene is not available (D45). |
| `menu.c:1934` `menu_updateNoPlaybackStorage()` | MOD: during an unlocked load, in scope only while the page is Load/Save (D48). |

### 7.4 ButtonHandler, Sequencer, Copy/clear, LED

| Site | Change |
|------|--------|
| `buttonHandler.c:1903`, `2340` | MOD: drop only while the load is not unlocked. |
| `buttonHandler.c:1700` | MOD: queue only an available Scene (D52, manual). |
| VOICE+SEQ per-track move (`buttonHandler.c` ~2630) | MOD: target must be available (D52, manual). It does not release 17 (D10). |
| `sequencer.c:5696` `seq_chainServiceTick()` | MOD: after the counter updates, return without `seq_chainEvaluate()` while `preset_backgroundBankLoadRunning()` (a one-byte read, ISR-safe). |
| `copyClearSession.c:588` | MOD: `menu_isStorageBusy()` refusal ignores an unlocked load. |
| `copyClearService.c` job run phases | MOD: drop (new trace reason) when a source or destination Scene is not available. |
| `copyClearService.c:1688` | MOD: do not advance `ccSvc_nameRetry` while a background Bank load runs. |
| `ledHandler.c:196-209`, `906` | MOD: a second fast-blink LED (1 B, D47) sharing phase and deadline; API per LED. |

### 7.5 Unchanged

The loader's children loop, phase 98 emptying, the commit, P2 range marking
and idle servicing, the AutoSave yield and gates, the Scene 17 build and
release, and the MIDI drop/defer rules.

### 7.6 Audit before implementing

1. **Preset/filesystem requests.** Every Preset/filesystem request reachable from a non-Load/Save page while unlocked must drop cleanly when the facade is BUSY or `pm_status` is not idle: no half-changed state and no retry that fires after the load. Sites to check:
   - Global actions;
   - Instrument and Effect `typ`;
   - the chain editor;
   - the PERF `rld` queue action;
   - SHIFT+REC items.
2. **Present-mask readers.** Every reader of `bank_scenePresent()` / `bank_scenePresentMask()` reachable mid-load must use availability or be shown harmless:
   - Scene LEDs;
   - per-track moves;
   - fan-out (including Morph fan-out);
   - `menu_residentContentSceneMask()`;
   - copy/clear Scene validity;
   - PERF names.
3. **Frozen-screen inputs.** Readers of `preset_currentName` and `menu_saveOptions` on non-Load pages, to confirm §5.3 can repaint the frozen screen after visiting them.
4. **Selection retry.** `menu_isLoadSaveSelectionCurrent()` after leaving and returning: the completion's retry branch must not fire for a frozen screen.
5. **Name lag.** HCNAMES rows are written at the end of the load, so a loaded child's name on the LCD may be the old Bank's until then. Confirm, or accept (R9).
6. **Copy/clear names vs the Bank's HCNAMES write.**
   - Does copy/clear change the in-RAM resident name register when the job runs, or only when it publishes?
   - Does the Bank load's end-of-load HCNAMES write overwrite a copied row?

   The ordering in §5.8 must leave the copied names on the card.
7. **Early edit-mask merge on boot.** Moving the merge before the first child also changes boot loads. Confirm the boot path does not read the old entries between phase 98 and the end.

**Resolved by the implementation dive (schedule §0.6):** item 1 — every Preset
or filesystem request reachable while unlocked comes from a Load/Save-family
path, which stays frozen; Instrument/Effect `typ` and the SOM generator are
runtime-only. Item 3 — the frozen screen reads the request's captured name and
slot, not the browser cache. Item 4 — the frozen entry sets
`menu_saveOptions.what`/`menu_currentPresetNr[]` from the request, so the
completion's selection check takes the normal path. The boot load keeps its
non-progressive merge at the end (the early merge is progressive-only).

---

## 8. Resources (D47, approved 2026-10-19)

| Item | Bytes | Region | Status |
|------|------:|--------|--------|
| `PM_BG_UNLOCKED` flag | 0 | spare bit 3 of `pm_bg_phase` | No RAM |
| Second fast-blink LED id | 1 | SRAM1, static, owner LED | **Approved 2026-10-19** |
| `op_bank_done_mask` | 2 | SRAM1, filesystem op scratch | **Approved 2026-10-19** |
| Last painted done-mask count (SEQ LED repaint trigger) | 1 | SRAM1, static, owner Menu | **Approved 2026-10-19** |
| Loading Bank name for §5.3 | 0 | reuse `op_bank_display_name` through an accessor | No RAM |

Total 4 B. Record the measured sizes in `STORAGE_SRAM_MANIFEST.md` at
implementation. Anything larger needs a new acknowledgement.

**Implemented as (2026-10-19):** the 2 B mask is BankData's
`bank_load_unavailable_mask`, not `op_bank_done_mask` (schedule §0.6 a).
Measured symbols: mask 2 B, `led_fastBlinkLed[2]` 2 B (+1 B), Menu
`menu_bgLoadAvailCount` 1 B; linked DEV BSS grew 430,920 → 430,928 B
(alignment). `PM_BG_UNLOCKED` uses spare bit 3 of `pm_bg_phase`.

---

## 9. Risks

| Id | Risk | Mitigation |
|----|------|------------|
| R1 | **Gates wider than R7.** Unlocking misses a storage-busy gate, or opens one whose action needs the facade. | §3.1 table; §7.6 audit item 1. |
| R2 | **Double activation at completion** after the user switched to a loaded child. | §5.10 step 1. |
| R3 | **Stale present and edit masks mid-load.** | Done mask, availability, early edit-mask merge (§5.7). |
| R4 | **Viewed Scene being replaced** (`PAR_FOLLOW` off). | D45 silent drop. |
| R5 | **Completion code paints Load/Save state off-page.** | §5.10 step 3. |
| R6 | **Name-session flush with a busy facade** on leaving Load/Save. | Defer to completion (§5.4). |
| R7 | **Fast-blink slot conflict** (queued Scene vs W1). | Second LED (§5.2). |
| R8 | **Silent audio when the transport stops on another page.** | D48. |
| R9 | **Names lag the data** for loaded children until the Bank's HCNAMES write. | Accept; §7.6 item 5. |
| R10 | **Load slows while the user works.** User activity shares main-loop time with the filesystem. No DSP work is skipped; P2 idle servicing still uses only spare time. | Accept; measure in §11. |
| R11 | **Chain fires after the load** at the first boundary, from counts accumulated during it (D50). | Intended; note in the manual. |
| R12 | **Release mid-load and AutoSave.** | The yield holds until `preset_bgEnd()`; verify on hardware. |
| R13 | **Copy/clear on a Scene that becomes in progress** between the press and the job run. | Availability checked at run time, not only at the press (§5.8). |
| R14 | **Copy/clear names overwritten** by the Bank's end-of-load HCNAMES write, or lost to the retry limit. | Retry freeze (§5.8); §7.6 item 6. |
| R15 | **Edit masks after a failed load** describe the new Bank for children that never loaded. | Accepted; layout match and present rules limit the effect. |

---

## 10. Open questions

All questions are answered (§2.2: Q2, Q4, Q5, Q7, Q11–Q16 → D41, D43, D44,
D46, D49–D53; RAM → D47). Four recommendations were adopted without an
explicit answer. They stand unless the user overrides them:

| Id | Adopted as |
|----|------------|
| Q1 | D40: a load requested with the transport stopped keeps the P1 lock. |
| Q3 | D42: Merge-kept Scenes are lit and selectable from the start. |
| Q6 | D45: edits on a viewed Scene that is not available are dropped silently. |
| Q10 | D48: during an unlocked load the codec is suspended only while the panel is on Load/Save. |

---

## 11. Verification outline

1. **Replace load on 17 with audio running; leave Load/Save at once.** Check:
   - MODE4 fast-blinks;
   - pots, encoder and Morph work on Voice, Effect and PERF;
   - edits sound on 17;
   - no audible change at any child commit (D4).
2. **PERF during the load.** Check:
   - SEQ LEDs dark, filling left to right;
   - kept Scenes lit from the start;
   - presses on pending or in-progress Scenes do nothing.
3. **Switch to a loaded child, then between loaded children, repeatedly.** Check:
   - 17 is released on the first switch;
   - each Scene sounds as loaded;
   - edits fan out only to loaded or kept members of the new edit mask;
   - no second activation at completion (listen; trace `menu_startSoundApply`);
   - AutoSave does not run until the load ends.
4. **Queued SHIFT+SEQ to a loaded Scene while W1 blinks:** both LEDs fast-blink and the queued change fires at its boundary. A `scd`-deferred SEQ press to a loaded Scene also fires.
5. **Per-track moves:** VOICE+SEQ moves a track from 17 to a loaded Scene (17 stays active) and back to another loaded Scene. A move to a pending Scene does nothing.
6. **Chain conditions on a loaded Scene:** nothing fires during the load (including `nxt`, `prv` and random targets); counts continue; evaluation resumes after completion.
7. **Return to Load/Save mid-load.** Check:
   - the frozen Bank screen with the live `NN.`, 1-based (`01.` while Scene 1 loads, matching the filling SEQ LED);
   - PERF-style SEQ LEDs;
   - every control dead, no toggle;
   - leaving again works.
8. **Load ends on another page.** Check:
   - no Load/Save repaint, no LED clobber, no overlay;
   - the blink stops;
   - the next Load/Save entry is fresh;
   - the HCNAMES flush ran.
9. **Load ends on Load/Save:** D7 behaviour, with the overlay on failure.
10. **Copy/clear mid-load.** Check:
   - clear and non-overlapping paste on available Scenes run;
   - an overlapping paste while 17 exists is dropped;
   - an overlapping paste after the release runs;
   - jobs on pending Scenes are dropped;
   - names publish after the load and stay correct.
11. **`PAR_FOLLOW` off:** STEP edits on the home Scene are dropped until it loads.
12. **Stopped load:** P1 lock unchanged (D40). Stop the transport mid-load on another page: audio stays up (D48).
13. **MIDI-origin load:** D39 UI take-over, then unlock at phase 5.
14. **Failure injection** (pull the card mid-load) on another page: the command ends, no overlay, playback unchanged.
15. **Underruns:** `audioCodec_underrunCount` unchanged across 1–14.
