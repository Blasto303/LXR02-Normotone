# S080 P1 — PERF Mode Updates

Session 080 plan. Six features plus one bug fix, all in PERF mode or supporting
sequencer/global infrastructure. None change DSP, Pattern storage, AutoSave, or
the filesystem.

---

## 1. PERF Menu Re-order and Page-Entry Bug Fix

### 1.1 Menu Re-order

Current PERFORMANCE_PAGE subpage 0 (menuPages.h line 72):
`mrp, 1VM, 2VM, 3VM, 4VM, 5VM, 6VM, fxm` — one subpage, 8 cells.

New layout:

| Subpage | Cell 0 | Cell 1 | Cell 2 | Cell 3 | Cell 4 | Cell 5 | Cell 6 | Cell 7 |
|---------|--------|--------|--------|--------|--------|--------|--------|--------|
| 0       | mrp    | fxm    | 1VM    | 2VM    | —      | —      | —      | —      |
| 1       | 3VM    | 4VM    | 5VM    | 6VM    | —      | —      | —      | —      |

Cells 4–7 on both subpages are `TEXT_EMPTY`/`PAR_NONE` terminators.

Impact: the PERF morph automation-presence search (`menu_applyPerfMarkers`,
`va_searchRestart`) must be updated to match the new cell positions. The
`VA_PERF_MORPH_EFFECT_BIT` mask for `fxm` moves from the last cell to cell 1
on subpage 0. The six voice morph cells shift: voices 1–2 are subpage 0 cells
2–3; voices 3–6 are subpage 1 cells 0–3.

### 1.2 Page-Entry Bug Fix

**Symptom:** on PERF entry the display starts at the `4VM` cell instead of
`mrp`. **Root cause hypothesis:** `menu_switchPage(PERFORMANCE_PAGE)` in
`handleModeButtons()` (buttonHandler.c line 1219) followed by
`menu_switchSubPage(0)` (line 1220) does not reset the within-subpage parameter
index. If the previous page visit left `menuIndex` at cell 4+, the display
starts at that cell. The fix is to call `menu_resetActiveParameter()` (which
zeros the parameter cursor within the subpage) after `menu_switchSubPage(0)`, or
to ensure `menu_switchPage()` itself resets the cursor for PERFORMANCE_PAGE.
Note that `menu_resetActiveParameter()` is already called at line 15006
inside `menu_switchPage()`, after the LED update block — so the double-call
from buttonHandler may be resetting it BEFORE the page switch completes, then
it gets overwritten. Verify the exact sequencing before fixing.

---

## 2. BAR Button Overlay (SHIFT+REC Menu Rework)

### 2.1 Concept

In PERF mode, BAR1 (`<`) and BAR2 (`>`) become toggle buttons for an overlay
that changes what the TRACK (VOICE) buttons do. Only one overlay can be active
at a time. The selected overlay is indicated by an illuminated BAR button. When
no overlay is active, both BAR buttons are dark and TRACK buttons behave
normally (mute toggle on release, per-track scene change on hold+SEQ).

When an overlay IS active:

- **Track button down-press** immediately sends a trigger (note-on) based on
  the overlay's configured trigger type and note.
- **Track button release** sends note-off.
- **SHIFT+Track button** performs the normal PERF function (mute toggle).
- All other PERF functions (scene switch, morph, fill, etc.) work normally.
- Per-track scene changes via hold+SEQ still work, but the trigger fires on
  the initial down-press regardless.

### 2.2 Overlay Settings — stored in settings.cfg

Four new settings saved in `settings.cfg`:

| Key            | Short | Cat      | Long       | Range                 | Default |
|----------------|-------|----------|------------|-----------------------|---------|
| `perf_btn_lt`  | `pb<` | Prfrmnc  | BtnStat<   | see §2.3              | `vmx`   |
| `perf_note_lt` | `pn<` | Prfrmnc  | BtnNote<   | see §2.4              | `def`   |
| `perf_btn_rt`  | `pb>` | Prfrmnc  | BtnStat>   | see §2.3              | `vmx`   |
| `perf_note_rt` | `pn>` | Prfrmnc  | BtnNote>   | see §2.4              | `def`   |

### 2.3 Trigger Type Options (`pb<` / `pb>`)

| Display | Description |
|---------|-------------|
| `vmx`   | Single trigger at velocity 127 |
| `v10`   | Single trigger at velocity 100 |
| `v64`   | Single trigger at velocity 64 |
| `v32`   | Single trigger at velocity 32 |
| `flm`   | Flam: 1/32 note duration note at vel 64, then normal trigger at vel 100 |
| `fls`   | Flam variable step: flam timing = ½ the track's current step value |
| Roll rates (held): |
| `r/8`   | Roll at 1/8 rate while held |
| `r16`   | Roll at 1/16 rate while held |
| `r32`   | Roll at 1/32 rate while held |
| `r64`   | Roll at 1/64 rate while held |
| `r6t`   | Roll at 1/64 triplet rate while held |
| `128`   | Roll at 1/128 rate while held |

Roll triggers are repeated at the given rate as long as the track button is
held. The trigger fires immediately on press and the repeat timer starts from
that moment.

### 2.4 Note Options (`pn<` / `pn>`)

Same options as the track note setting in MODE STEP (PAR_TRACK_MIDI_NOTE),
except `off` is replaced by `def`. When `def` is selected, the trigger uses
the track's default MIDI note. When any specific note is selected, the
trigger uses that note instead.

### 2.5 BAR Button Behavior in PERF Mode

- **BAR1 press:** toggle overlay `<`. If already active, deactivate (dark). If
  `>` is active, deactivate `>` and activate `<`.
- **BAR2 press:** same, for overlay `>`.
- **LED:** the active BAR button is illuminated; the other is dark. When no
  overlay is active, both are dark.
- **While looper is engaged:** both BAR LEDs are forced off (see §3). BAR
  buttons serve the looper's dot/triplet function instead.
- Overlay state is runtime only (not saved). On PERF entry, no overlay is
  active. Overlay persists across Scene changes within a PERF session.

### 2.6 SHIFT+REC Menu Rework

This replaces `RECORDING_PAGE`. The existing quantisation parameter remains on
the page; the overlay settings are added alongside it. The static
`RECORDING_PAGE` entry in `menuPages.h` is repurposed with the new layout.

**New entry/exit protocol:**

- **Entry:** SHIFT+REC. Both SHIFT and REC LEDs illuminate. The menu opens on
  the first sub-page. The previous page/subpage/screen state is saved for
  restoration on exit.
- **Exit:** press SHIFT or REC (either one). Both LEDs return to normal state.
  The menu returns to the saved page/subpage/screen. Exception: if live record
  is enabled (future, next session), the REC LED flashes instead of being
  steady, live record is NOT interrupted by menu entry, and the REC button
  press to exit does not toggle live record state.
- **Navigation:** encoder scrolls through parameters. When the last parameter
  on the current sub-page is passed, the display advances to the next sub-page.
  When scrolling back past the first parameter, the display returns to the
  previous sub-page. Sub-pages are not cyclic (stop at first/last).

**Sub-page 0 — Performance button overlay + quantisation:**

| Cell | Short | Long      | Options |
|------|-------|-----------|---------|
| 0    | `pb<` | BtnStat<  | trigger types per §2.3 |
| 1    | `pn<` | BtnNote<  | note options per §2.4 |
| 2    | `pb>` | BtnStat>  | trigger types per §2.3 |
| 3    | `pn>` | BtnNote>  | note options per §2.4 |

Category line: `Prfrmnc`

**Sub-page 1 — Recording:**

| Cell | Short | Long        | Options |
|------|-------|-------------|---------|
| 0    | `qnt` | Quantise    | existing PAR_QUANTISATION options |

Category line: `Record`

Sub-pages 2–3 are reserved for next session (live record options). The menu
framework must support scrolling to them but they may be empty initially.

---

## 3. Looper

### 3.1 Concept

The SELECT buttons (1–8) in PERF mode become a performance looper. While at
least one SELECT button is held, the sequencer loops all (or selected) tracks
over a musical division. The master step clock continues running underneath,
and when all SELECT buttons are released, tracks resume playback at exactly
the point they would have reached without looping.

### 3.2 SELECT Button Divisions

| SELECT Button | Division |
|---------------|----------|
| 1             | 1 bar (16 × 1/16 notes) |
| 2             | 1/2 bar |
| 3             | 1/4 bar |
| 4             | 1/8 bar |
| 5             | 1/16 |
| 6             | 1/32 |
| 7             | 1/64 |
| 8             | 1/128 |

When multiple SELECT buttons are held, the **last pressed** button determines
the active division. That button's LED is always illuminated.

### 3.3 Dot and Triplet Modifiers

While any SELECT button is held (looper active):

- Both BAR LEDs are forced off, even if a BAR overlay is engaged (the overlay
  state is preserved but the LEDs are suppressed).
- **Hold BAR2 (`>`):** adds a dot to the loop division (+50% duration). BAR2
  LED illuminates while held.
- **Hold BAR1 (`<`):** makes the division a triplet (2/3 duration). BAR1 LED
  illuminates while held.
- BAR1 and BAR2 are momentary modifiers — they take effect while held and
  revert when released. If both are held simultaneously, the last pressed
  takes precedence.

### 3.4 Loop Start Point

**Without quantize:** the loop start is set to the beginning of the step on
the looping track with the smallest step subdivision (fastest step scale).
Because different tracks may have different step scales, the start point may
not fall on an even step boundary for all tracks. Use fractional timing (the
existing DDA accumulator positions) to set the same absolute start point for
all looping tracks.

**With quantize:** use the beginning of the quantize division against the
master step clock to set the loop start. Note: if the quantize division is
large and the loop division is small, tracks may appear to jump backwards in
time. Tracks may wrap back based on their current settings but cannot change
across scenes to quantize.

### 3.5 Per-Track Looping

**All-track looping (default):** press a SELECT button with no TRACK buttons
held. All 7 tracks loop at the selected division.

**Per-track subsetting (TRACK then SELECT):** if the user holds one or more
TRACK buttons and then presses a SELECT button, only those tracks are looped.
- Held track LEDs use the "selected" display mode (inverse: normal
  illumination when not triggering, dark when triggering).
- The looped tracks remain looped and displayed this way even if the user
  releases the TRACK buttons, as long as at least one SELECT button is still
  held.
- Non-looped tracks continue normal playback.

**Track selection change (SELECT held, TRACK pressed):** if the user holds
SELECT and then presses one or more TRACK buttons:
- All previously looped tracks are released from the loop (resume position
  from master clock).
- The newly pressed/held tracks begin looping at a new loop point set at the
  moment of the last TRACK button press.
- This allows real-time "swapping" of which tracks are looping.

### 3.6 Scene Changes During Loop

While looping with SELECT buttons held:

- SEQ button scene changes work normally based on the still-running master
  step clock and global scene change options (see §5).
- On scene change, each looped track's loop start point keeps the same
  position relative to the master clock that it had in the outgoing scene.
  The incoming scene's track is positioned at that master-clock point using
  its own step scale and length. Example: outgoing track at 1/16 with the
  loop start at the beginning of step 5; incoming track at 1/8 → its loop
  start is the beginning of step 3. *(Corrected in S080 P2; the earlier text
  "resets to the very beginning of the new scene" was a misstatement. See
  S080_P2_PERF_EXPANSION_DEFECTS.md Defect 14.)*
- Per-track scene changes (TRACK+SEQ) are not available while the looper is
  engaged; SEQ is always a full scene change (S080 P2 Defect 9).

### 3.7 Effects and FX Sequence

Effects and the FX sequence are **never** looped. They continue running
normally based on the master step clock regardless of looper state.

### 3.8 Loop Release

When all SELECT buttons are released:
- All tracks resume playback at the position they would have reached based on
  the master step clock (as if the loop had never happened).
- BAR overlay LEDs are restored to their pre-loop state.

---

## 4. Scene Chaining

### 4.1 Entry

**SHIFT+PLAY+SEQ(scene) button** in PERF mode opens the scene chaining menu
for the pressed scene.

- **SHIFT+PLAY must never toggle transport.** The PLAY button is consumed as
  part of the chord.
- SHIFT LED remains illuminated while the menu is open.
- Both the current active/playing scene's SEQ LED and the selected scene's SEQ
  LED flash while the menu is open.
- The menu shows the chaining options for the pressed scene. Even if the
  active/playing scene changes during this menu (from a chain firing or
  other trigger), the viewed scene's chain options do NOT change — only a
  user button press (pressing a different SEQ button while in the menu)
  changes which scene's chain is viewed.
- **Exit:** press SHIFT again. SHIFT LED returns to normal state.

### 4.2 Parameters

Single-page menu with four parameters:

| Cell | Short | Cat   | Long      | Default |
|------|-------|-------|-----------|---------|
| 0    | `cn1` | Chain | Conditn1  | `nul`   |
| 1    | `nx1` | Chain | NxtScn1   | `slf`   |
| 2    | `cn2` | Chain | Conditn2  | `nul`   |
| 3    | `nx2` | Chain | NxtScn2   | `slf`   |

Each scene has its own independent set of these four parameters (16 scenes ×
4 parameters = 64 chain parameter slots). They are stored in the Scene data
structure and saved/loaded with the Scene.

### 4.3 Condition Options (`cn1` / `cn2`)

| Display      | Type          | Description |
|-------------|---------------|-------------|
| `nul`       | Null          | Never fires. If neither condition fires, scene repeats itself. |
| `xal`       | Always (rep)  | Always fires at end of scene (longest track by scale×length). |
| `bal`       | Always (bar)  | Always fires at end of a bar (16 sixteenth notes on master clock). |
| `x01`..`x08`, `x16`, `x32`, `x64` | Repeat count | Fires if scene has repeated N times (longest track by scale×length). |
| `b01`..`b08`, `b16`, `b32`, `b64` | Bar count | Fires if scene has played for N bars (16 sixteenth notes on master clock). |
| `r1x`..`r9x` | Probability (rep) | 10%–90% chance, evaluated at end of scene repetition. |
| `r1b`..`r9b` | Probability (bar) | 10%–90% chance, evaluated at end of bar. |
| `1:4`..`4:4` | Bar position (4) | Fires if the master clock just finished playing bar X of 4 bars. |
| `1:8`..`8:8` | Bar position (8) | Fires if the master clock just finished playing bar X of 8 bars. |
| `>01`..`>16` | From scene   | Fires at end of scene if this scene was changed to from scene NN. |
| `fix`       | Fill active (rep) | Fires at end of repetition if fill is active. |
| `fib`       | Fill active (bar) | Fires at end of bar if fill is active. |
| `!fx`       | Fill inactive (rep) | Fires at end of repetition if fill is NOT active. |
| `!fb`       | Fill inactive (bar) | Fires at end of bar if fill is NOT active. |
| `lox`       | Loop active (rep) | Fires at end of repetition if the looper is engaged. |
| `lob`       | Loop active (bar) | Fires at end of bar if the looper is engaged. |
| `!lx`       | Loop inactive (rep) | Fires at end of repetition if the looper is NOT engaged. |
| `!lb`       | Loop inactive (bar) | Fires at end of bar if the looper is NOT engaged. |

Total: 1 + 2 + 11 + 11 + 9 + 9 + 4 + 8 + 16 + 4 + 4 = **79 condition values.**

### 4.4 Next Scene Options (`nx1` / `nx2`)

| Display      | Description |
|-------------|-------------|
| `slf`       | The scene itself (default — repeat). |
| `nxt`       | Next scene (wraps 16→1). |
| `prv`       | Previous scene (wraps 1→16). |
| `s01`..`s16` | Specific scene 01–16. |
| `r01`..`r16` | Random scene chosen between 01 and NN (inclusive). |
| `r+1`..`r+4` | Random scene between the next 1..4 scenes (wraps), exclusive of current. |
| `r-1`..`r-4` | Random scene between the previous 1..4 scenes (wraps), exclusive of current. |
| `stp`       | Tracks stop playback (not transport stop — tracks freeze in place). |

Total: 1 + 1 + 1 + 16 + 16 + 4 + 4 + 1 = **44 next-scene values.**

### 4.5 Evaluation Rules

- Conditions are evaluated at the boundary they specify (end of repetition or
  end of bar). A condition that specifies "end of repetition" is evaluated at
  the end of the longest track by scale×length. A condition that specifies
  "end of bar" is evaluated at the end of each bar (16 sixteenth notes on
  the master step clock).
- If **both** conditions are true simultaneously, **condition 1 always takes
  priority.**
- If the combined probability of both conditions is less than 100%, the scene
  repeats/continues when neither fires.
- If the combined probability equals or exceeds 100%, one will always fire.
  Weight them against each other proportionally.
- A manual scene switch or a user-queued scene switch (SHIFT+SEQ) **cancels
  all conditional changes.** Only whole scenes are chained — no per-track
  chaining.
- **`stp` behavior:** tracks stop playback where they are. Example: condition
  1 = `b03`, next = `stp`; condition 2 = `b04`, next = `slf`. This causes
  the scene to play 3 bars, stop for 1 bar, then repeat. This is not a
  transport stop — transport continues, the tracks simply cease advancing.

---

## 5. Scene Queue and Change Division (Global Options)

### 5.1 New Global Settings

Three new global parameters, ordered after `PAR_FOLLOW` (`SequncrFollow`) in
the global menu page:

| Order | Key   | Short | Cat   | Long          | Default |
|-------|-------|-------|-------|---------------|---------|
| 1     | `scd` | `scd` | Scene | ChngDiv       | `ins`   |
| 2     | `qcd` | `qcd` | Scene | QueDiv        | `ins`   |
| 3     | `qac` | `qac` | Scene | QueActn       | `nrm`   |

These are added to `settings.cfg` and auto-saved.

### 5.2 Scene Change Division (`scd`)

The division at which scene changes take place:

| Display | Description |
|---------|-------------|
| `ins`   | Instant (current behavior — default). |
| `1/8`   | Next 1/8 note boundary. |
| `1/4`   | Next 1/4 note boundary. |
| `1/2`   | Next 1/2 note boundary. |
| `1b`    | Next bar boundary (16 sixteenth notes). |
| `2b`    | Next 2-bar boundary. |
| `3b`    | Next 3-bar boundary. |
| `4b`    | Next 4-bar boundary. |
| `ens`   | End of shortest track (by scale×length). |
| `enl`   | End of longest track (by scale×length). |

On change, all tracks realign to where they should be based on the master
clock (current behavior), unless global option `PCR` (`SequencrPCReset`) is
enabled, in which case all tracks play from the beginning.

### 5.3 Queue Scene (SHIFT+SEQ in PERF)

SHIFT+SEQ(scene) in PERF mode queues a scene change instead of performing an
immediate switch. The queued scene's SEQ LED flashes at double the normal rate
until the change fires.

- The division for queued changes is controlled by `qcd` (same options as
  `scd`).
- Queuing the currently playing scene also fires the queue action.
- A queued change always fires when due, regardless of what menu the user is
  on or what action they are performing.
- On change, editing is locked until the full changeover and menu refresh
  completes.
- The only thing that stops a queued change is a **transport stop**.
- Queuing a different scene replaces the pending queued scene.

### 5.4 Queue Action (`qac`)

What happens when a queued scene change fires:

| Display | Description |
|---------|-------------|
| `nrm`   | Normal change (default). |
| `mut`   | All mutes are reset to off. |
| `mrp`   | All morphs are reset to 0. |
| `res`   | Reset: tracks restart from the beginning (queued PC reset). |
| `u+r`   | Mute reset + reset. |
| `o+r`   | Morph reset + reset. |
| `mmr`   | Mute reset + morph reset + reset. |
| `rld`   | Scene is reloaded from source (if source exists). Also resets mutes and morphs. |
| `r+r`   | Reload + reset. |

*(Changed in S080 P2, Defect 15: queued changes have their own PC reset,
selected here; they ignore the global `pcr`.  `mor` is renamed `mrp`; `m+m`
is removed.)*

### 5.5 Global `PCR` — Pattern Change Reset

A new boolean global parameter, `SequencrPCReset`, stored as `pcr` in
settings.cfg:
- Off (default): scene changes realign tracks to master clock position
  (existing behavior).
- On: scene changes reset all tracks to play from the beginning.
- *(S080 P2: this is the existing `PAR_BAR_RESET_MODE` / `pcr`, not a new
  parameter (Defect 5).  It governs direct and chain-fired changes only;
  queued changes use the reset options in `qac` (§5.4, Defect 15).)*

Place this parameter adjacent to `scd`/`qcd`/`qac` in the global menu.

---

## 6. New Trigger Conditions: Loop Active / Inactive

### 6.1 Additions to the Condition Table

Insert two new conditions after `!fl` (index 22, "not fill") in the
conditionNames table (MenuText.h):

| New Index | Display | Description |
|-----------|---------|-------------|
| 23        | `lop`   | Trigger plays only if looping is engaged (any SELECT held in PERF). |
| 24        | `!lp`   | Trigger does NOT play if looping is engaged. |

The existing conditions at index 23+ (`lst`, `!ls`, `1mu`..`7mu`, `!1m`..`!7m`)
shift up by 2. The total count goes from 39 to 41.

**Backward compatibility is not a concern.** Insert in place. Existing
patterns that use condition indices 23+ will have their conditions shifted,
which is acceptable — no PAT4 migration is performed.

### 6.2 Impact

- `seq_evaluateStepCondition()` in sequencer.c must add dispatch for the two
  new condition values.
- The ISR reads a single byte `seq_loopActive` flag (atomic on Cortex-M7, same
  pattern as `seq_fillActive`) set by the looper in the foreground.
- `conditionNames` array size increases from 39+1 to 41+1.
- PAT4 condition field: the 6-bit condition index in the step specials block
  has a maximum of 63 values (0–63). Current count 39 + 2 = 41 is within
  range. No format change needed.

---

## 7. Implementation Order

Recommended build sequence:

| Phase | Feature | Dependencies |
|-------|---------|-------------|
| A     | §1 Menu re-order + page-entry bug fix | None |
| B     | §6 New trigger conditions (lop/!lp) | None (can parallel A) |
| C     | §3 Looper (core loop engine, SELECT buttons, start point, release) | §6 for the seq_loopActive flag |
| D     | §3.5 Per-track looping, §3.6 Scene changes during loop | §C |
| E     | §2 BAR overlay + SHIFT+REC menu rework | §C (for BAR suppression during loop) |
| F     | §5 Scene queue/change division globals | None (can parallel C/D/E) |
| G     | §4 Scene chaining | §C (for loop conditions), §F (for scene change division) |
| H     | Integration test, hardware verification | All |

---

## 8. Storage Summary

### New RAM

| Item | Bytes | Region | Lifetime |
|------|-------|--------|----------|
| Scene chain data: 16 scenes × 4 params × 1 byte | 64 | SRAM1 (SceneData) | Resident |
| BAR overlay runtime state | ~4 | SRAM1 (menu/buttonHandler) | Runtime |
| Looper state (mask, division, start points per track, modifier) | ~40 | SRAM1 (sequencer) | Runtime |
| Scene queue state (queued scene, division, pending flag) | ~4 | SRAM1 (sequencer) | Runtime |
| Global scene change params (scd, qcd, qac, pcr) | 4 | SRAM1 (globals) | Retained |
| SHIFT+REC menu page/state | ~4 | SRAM1 (menu) | Runtime |
| Looper active flag for ISR | 1 | SRAM1 (sequencer) | Runtime |
| Previous scene tracker (from-scene condition) | 1 | SRAM1 (sequencer) | Runtime |
| Condition table growth (2 entries) | 8 | Flash (const) | Static |
| **Total persistent SRAM1** | **~72** | | |
| **Total runtime-only SRAM1** | **~50** | | |

The 64-byte scene chain data is the only persistent growth. It lives in
SceneData and is saved/loaded with the Scene. The 4 global bytes go into the
existing settings allocation.

### settings.cfg Growth

7 new keys: `perf_btn_lt`, `perf_note_lt`, `perf_btn_rt`, `perf_note_rt`,
`scd`, `qcd`, `qac`, `pcr`. The `lines=` terminator count increases from 17
to 25.

### PAT4 / Pattern Data

No format change. The condition index field (6 bits, max 63) has room for the
two new conditions (total 41).

### SceneData

64 new bytes for scene chain parameters. Must be added to the `scene_t`
structure, serialized in `sceneset.scg`, and included in Scene save/load.

---

## 9. Risks, Ambiguities, and Open Questions

### 9.1 Risks

1. **ISR/foreground race on looper state.** The looper start-point calculation
   involves reading DDA accumulators, which are written by the TIM3 ISR at
   4 kHz. The looper engage/disengage must be atomic with respect to the ISR.
   The `seq_loopActive` flag is a single byte (atomic read/write on Cortex-M7),
   but the per-track start-point array and division must be published
   coherently. A brief priority-2 mask or a generation counter may be needed.

2. **Fractional loop start across different step scales.** Tracks at different
   step scales (e.g., track 1 at 1/8, track 2 at 1/32) share the same master
   clock but have different DDA intervals. Setting the same absolute start
   point for all tracks requires computing each track's DDA accumulator value
   at the loop start. This is the most technically complex part of the looper.

3. **Scene chain evaluation in ISR.** The condition evaluation at bar/repetition
   boundaries runs in the TIM3 ISR context. The 79-value condition table
   includes probability checks (RNG calls), scene-source checks, and fill/loop
   state reads. Each must be ISR-safe. `GetRngValue()` is already ISR-safe.

4. **Scene change division timing.** Non-instant scene changes (`scd` != `ins`)
   require the sequencer to defer the actual scene switch until the correct
   boundary. This is a new deferred-event mechanism in the ISR. The visual
   queue feedback (double-rate blink) runs in the foreground LED handler.

5. **`stp` (stop playback) in scene chaining.** This is a new concept: tracks
   stop advancing but transport continues. Need to define what happens to
   mute state, LED state, and whether stopped tracks resume from their
   stopped position or from the beginning when the next chain fires.

6. **Menu dtype space is full (S078).** All 16 `DTYPE_MENU` table IDs and all
   16 `DTYPE_*` nibbles are used. The new scene chain parameters, BAR overlay
   settings, and global scene options need custom menu rendering using static
   param special cases (the run-mode precedent), not new dtype slots.

7. **SHIFT+REC menu overlaps with SHIFT's existing role.** In the new protocol,
   SHIFT is held when entering the menu, then either SHIFT or REC exits.
   Current SHIFT behavior on release (mode revert, etc.) must be suppressed
   while the SHIFT+REC menu is active.

8. **Queue action `rld` (reload from source).** This requires a filesystem
   read during a scene change, which is an async operation. The edit lock
   during changeover must handle this gracefully without blocking the ISR.
   If the source file is missing, the reload degrades to a normal change.

### 9.2 Resolved Ambiguities

1. **BAR overlay triggers do NOT record this session.** Overlay triggers are
   performance-only. Live recording of overlay triggers is next session's
   work.

2. **Looper and recording are mutually exclusive.** Engaging the looper
   disables recording if it is currently enabled. Recording cannot be enabled
   while the looper is engaged. The REC LED is cleared when the looper
   engages; the REC button is ignored (for record toggle) while the looper
   is active.

3. **Scene chain data copies with the Scene but does NOT fan out through the
   edit mask.** Chain parameters are Scene settings for copy/clear purposes
   (they copy and clear with the Scene), but they are excluded from the
   voice-edit-mask fan-out path. Each Scene's chain is independent.

4. **SHIFT+PLAY chord detection.** `BUT_START_STOP` processing is gated
   when SHIFT is held and a SEQ press follows within the chord window.
   PLAY is consumed without side effects. The `handleModeButtons()` path
   must not fire during the chord.

5. **Looper divisions are master-clock-based.** "1 bar" is always 16
   sixteenth notes on the master step clock, consistent with bar-based
   conditions throughout the system.

6. **Tracks 5+6 (HiHat pair) loop independently.** Unlike per-track scene
   changes where they switch together, the looper treats them as separate
   tracks. Each can be independently included or excluded from a loop.

7. **`>NN` (from-scene) condition.** A new `seq_previousScene` byte tracks
   the scene that was active before the last change. Reset on transport
   stop. One byte of runtime-only SRAM1.

### 9.3 Architectural Decisions (Resolved)

1. **Looper engine: ISR with foreground config.** The foreground publishes
   loop config (division, track mask, start point); the TIM3 ISR executes
   the wrap in `seq_advanceTrackStep()`. All ISR-touched state must be
   coherent — the foreground publishes a complete config snapshot and then
   sets `seq_loopActive` as the final atomic gate. The ISR ignores loop
   config while the flag is 0.

2. **Scene chain storage: `scene_t`.** Chain parameters are Scene settings,
   stored in `scene_t` and serialized in `sceneset.scg`. They participate
   in Scene save/load and copy/clear, but do NOT fan out through the
   voice-edit mask. Each Scene's chain is independent.

3. **Scene queue/change division: ISR flag → foreground execution.** The
   ISR detects the musical boundary and sets a "fire now" flag. The
   foreground executes the actual scene change (DSP apply, LED refresh,
   potentially `rld` filesystem access) on the next main-loop pass.

4. **SHIFT+REC menu: replaces RECORDING_PAGE.** The existing static
   `RECORDING_PAGE` in `menuPages.h` is repurposed. Quantisation moves to
   sub-page 1; overlay settings are sub-page 0. The dtype-full constraint
   is handled with static-param special cases (the run-mode precedent)
   for the new overlay parameters.

5. **Roll-rate timer: DDA.** The BAR overlay's held-roll uses the existing
   step-scale DDA infrastructure to schedule trigger repeats. This is the
   proven timing mechanism for musically-quantized events.

6. **PCR applies to direct and chain-initiated scene changes.** *(S080 P2,
   Defect 15: queued changes have their own reset, selected by `qac`.)*

7. **Condition table: insert, no migration.** `lop` and `!lp` are inserted
   after `!fl` at indices 23–24. Existing conditions shift by 2. No PAT4
   migration — backward compatibility is not a concern.

---

*End of plan. Implementation begins after review.*
