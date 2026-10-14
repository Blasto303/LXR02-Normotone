# S081 P2 — Linear Automation Ramps (`asm` / "Automtn")

Session 081, part 2 (2026-10-13), branch `dev-ph6-cleanup`. General plan:
behaviour, design, touched modules, RAM, decisions. The implementation
schedule with file/line steps follows once the RAM is acknowledged (§8) and
the follow-ups (§11) are answered.

Revision 2 folds in the user's answers to the first draft's Q1–Q9.

**Authority:**

- the user's specification and decisions (§1, §10);
- `MEMORY.md` binding rules:
  - RAM approval policy;
  - DSP CPU policy;
  - automation priority;
  - the parameter-mirror rule;
  - the full menu dtype space;
- `PATTERN_DYNAMIC_STACK.md` §6 (automation timing);
- `S081_P1_LIVE_RECORD.md` §5.8 (step reset rule) and R12 (menu override).

---

## 1. Specification

- **Automation type** is a per-track **Pattern** setting, saved with the
  Scene's Pattern. It is neither automatable nor morphable.
- **Menu:**
  - STEP mode, track parameters, right after Track Loopable (`lop`);
  - short name `asm`, long name `Automtn`;
  - values `off` (default) and `on`.
- **`off`:** automation works exactly as today. Every step's value is
  applied as a jump.
- **`on`:** a parameter ramps linearly from the current step's value to the
  next played step's value. This happens only if both steps hold an
  automation value for that parameter.
  - From the menu value to an automated step: no ramp. The parameter jumps,
    as today.
  - From an automated step back to the menu value: no ramp. The parameter
    jumps, as today.
  - The ramp needs two consecutively played steps that both hold the
    parameter.
- **Scope:** Instrument parameters on that track only (§5.3).
- **Cap:** at most **64 ramps** run at once. A pair that finds no free slot
  jumps, as today.

---

## 2. Definitions

- **Step A, step B:** A is the step whose automation is being applied. B is
  the step that the track plays next in normal (unlooped) play (§5.2).
- **Ramp pair:** a target that holds an entry on both A and B. For that
  target the ramp runs from A's value to B's value.
- **Apply time:** when a step's automation is applied.
  - Normally this is the step start, when the condition passes.
  - When the step's own trigger is deferred (shuffle or micro-timing), it is
    the deferred fire time (`PATTERN_DYNAMIC_STACK.md` §6.1, P1 §5.8).
  - A ramp runs from A's apply time to B's apply time, so it ends where B's
    value lands today.
- **Played:** the playhead reaches the step and its condition passes.
  Automation follows the condition gate, not the trigger.

---

## 3. Storage

### 3.1 Pattern region and file

- **New field `uint8_t track_auto_smooth`** in `pat_scene_region_t`, beside
  `track_loop_exclude`.
  - Bit N set means track N is `on`; 0 means `off`. A zero-filled region
    reads as `off`.
  - The region static assert grows from +31 to +32 bytes.
- **PAT4 file:** byte 5 of each track's 16-byte track header (0/1).
  - The writer (`filesystem.c:~3085`) writes it, and the three readers
    (`:13218`, `:15254`, `:28559`) read it, exactly like byte 4 (loopable).
- **PatternData:**
  - `pat_setTrackAutoSmooth()` and `pat_trackAutoSmooth()`, following the
    loopable pair;
  - region init clears it;
  - `pat_applyTrackSettingsToMenu()` sets the mirror;
  - the region copy at `PatternData.c:2205` copies it.
- **Copy/clear** (`copyClearService.c:855`, `:1033`):
  - track-settings paste copies it;
  - track clear resets it to `off`.
- **Not a target:** no SceneModTarget ID and no Morph endpoint, so it never
  appears in automation lists, the live-record resolver or pot clear.

### 3.2 Card update script

**New file:** `tools/convert_pattern_autosmooth.py`, modelled on
`tools/convert_scene_scale.py` (same CRC32C and PAT4 header handling).

- **Input:** a card directory (recursive) or single files. `--dry-run`
  reports and writes nothing.
- **For each PAT4 file** (magic `PAT4`, header version 2, 160-byte header):
  1. Validate the header and the stored CRC32C. A mismatch is reported and
     the file is skipped.
  2. Set byte 5 of each of the 7 track headers to `0` (`off`).
  3. Recompute the CRC32C over the file with the CRC field zeroed, and write
     it at offset 14.
- **Non-PAT4 `.pat` files** are skipped and listed.
- **Exit status** is nonzero if any file fails.
- **Run on `SD_CARD/`** once the firmware lands. You delete AutoSaves
  beforehand.

**State of the card today (checked 2026-10-13):**

- All 194 PAT4 files (`SD_CARD/Pattern/` and the `Scene/*/pattern.pat`
  files) already hold zero in track-header bytes 5..15. They already read
  as `off`, and the script leaves their content as it is.
- `SD_CARD/Bank/*/*/pattern.pat` (229 files) are the old text format
  (`format=helicase.pattern`, written by `tools/populate_scene_directory.py`)
  and not PAT4. The firmware does not read that format. The script skips
  them (follow-up F2).

Whether the file version should change is follow-up F1.

---

## 4. Menu

- **Cell:** SEQ_PAGE sub-page 0, **cell 7** (`menuPages.h:143`, today
  `TEXT_EMPTY`/`PAR_NONE`), after `lop`.
- **Parameter:** new mirror `PAR_TRACK_AUTO_SMOOTH`, placed after the
  Globals, following the binding mirror rule (`MEMORY.md`, Track Loopable
  precedent).
  - It is a no-op in `menu_parseGlobalParam()`.
  - It commits in `menu_cellCommitValue()` through `pat_setTrackAutoSmooth()`
    for the viewed Pattern and the active track.
  - It consumes an existing `parameter_values[]` slot. `NUM_PARAMS = 384`
    stays, with a static assert like the loopable one.
- **Display:**
  - plain `DTYPE_ON_OFF` (`off`/`on`), so no dtype or special case is
    needed;
  - new texts: short `asm` (`TEXT_`/`SHORT_` tables in `MenuText.h`) and
    long `Automtn` (`LONG_` table);
  - name-table row `{SHORT_TRACK_AUTO_SMOOTH, CAT_TRACK,
    LONG_TRACK_AUTO_SMOOTH}`.
- **Not recordable:** the cell resolves to no Pattern target, so
  `menu_liveRecRecordCell()` already ignores it.

---

## 5. Playback design

### 5.1 Overview

- **TIM3, at A's apply time, on an `on` track:**
  - predicts B and B's apply time;
  - stores them in a small per-track context.
- **Foreground, when `seq_drainPendingAutomation()` applies A's entries for
  that track:**
  - reads B's entries once;
  - starts one ramp per ramp pair;
  - writes the interpolated values until the track's next played step
    applies.

TIM3 does no matching. Its work is one prediction per step on `on` tracks
only.

### 5.2 Predicting B (TIM3)

| Play mode | B |
|---|---|
| fwd | next index, wrapping at the effective length |
| rev | previous index, wrapping at 0 |
| pip | next index in the current direction, with the boundary repeat |
| onc, 1fr | next index; no B after the last step (no ramp) |
| rnd | none (always jumps) |

- The effective length is `seq_effectiveTrackLength[]`. Prediction uses the
  same arithmetic as `seq_advanceTrackStep()`. `seq_shiftPositionQ8()` by
  one interval already implements it.
- **Looper (user decision):** B is predicted **as if the loop were not
  engaged**.
  - The track ramps exactly as the same segment would in normal play.
  - The last loop step ramps toward the step after the loop end, if that
    step holds the parameter.
  - At the wrap, the loop's first step takes over (§5.5). There is no ramp
    from the loop end back to the loop start.
- **B's apply time:**
  - B's nominal start;
  - plus the shuffle deferral if B is odd;
  - plus B's micro-timing if B has a trigger with a timing special.

  This is the same rule that defers B's automation today. It is held in
  master ticks (Q8) from A's apply time.
- **B's condition must be predictable at A's apply time (decision):**
  - **Predictable** (ramps): no condition, the repeat counters (`1:2`..`4:4`,
    `1:-`, evaluated with the repeat count B will see), and `lst`/`!ls`
    (from A's result).
  - **Not predictable** (B jumps, as today): `rNN`, `fil`/`!fl`,
    `lop`/`!lp` and the mute conditions `1mu`..`!7m`.
  - Condition evaluation itself is unchanged.
- **Ramp context per track:** B's step, a valid flag and the duration (4 B ×
  7 tracks).

### 5.3 Which targets ramp (decision)

A target ramps only if all of these hold:

- it is an **Instrument descriptor parameter** of the track's slot
  (`instrumentParam_isVoiceParameter()`, automatable);
- its descriptor dtype (low nibble) is a **numeric range**: `DTYPE_0B127`
  or `DTYPE_PM63`. These are the only numeric dtypes Instrument descriptors
  use today; `DTYPE_0B255`, `DTYPE_0B15`, `DTYPE_1B16` and `DTYPE_1B128` are
  admitted if a descriptor ever uses them;
- it is valid for the track's played Scene
  (`instrumentManager_targetValid()`).

**Everything else jumps, as today:**

- `DTYPE_MENU` (waveforms and other tables), `DTYPE_MIX_FM` (FM mode),
  `DTYPE_ON_OFF`, `DTYPE_0b1`, `DTYPE_LFO_POLARITY`;
- the LFO/velocity target and voice selections;
- all Scene targets: `Nvm` Voice Morph, Audio Out, FX Send, `7dc`,
  `len`/`scl`/`shf`, `fxm`;
- Effect parameters.

### 5.4 Starting ramps (foreground drain)

For each Instrument entry of A on an `on` track with a valid ramp context:

1. **Rampable?** Apply the §5.3 test.
2. **Does B hold the same target?** B's entry list is read once per track
   per step with `pat_readStepAutomations()` (foreground), and targets are
   matched.
3. **If both hold:** take a ramp slot {track, target, from = A's value, to =
   B's value, start, duration}. If no slot is free, the parameter jumps.
4. **A's value is written as today.** The ramp starts from it.

### 5.5 Running and ending ramps (foreground)

- **Placement:** a ramp service runs right after the drain in the main
  foreground sequence (`main.c:214`), at most once per 2 ms (500 Hz).
- **Value:** `from + (to − from) × elapsed / duration`.
  - `elapsed` is master ticks with the sub-tick fraction, so a tempo change
    stretches the ramp with the music.
  - The value is rounded to the 7-bit descriptor value. 7-bit resolution is
    accepted (decision).
  - A ramp holds `to` once `elapsed ≥ duration`.
- **Write path:** the same as the drain's Instrument branch:
  `instrumentManager_writeRuntime()` plus the `seq_automation_dirty` bit.
  Ramp values are therefore automation values under the binding priority
  rule: the Morph sweep and the menu apply never overwrite them before the
  next trigger.
- **End:** all of a track's ramps end at that track's next played step's
  apply time. That is B normally, or the loop's first step at a wrap.
  - B's own entries then apply its exact values. Any of them that pair with
    the step after B start the next ramps at once.
  - Parameters without an entry on that step follow today's rules.
  - The end is processed **before** that step's trigger restore and drain,
    in the same foreground pass.
- **Cancel** (the value stays where it is, and today's rules apply from
  there):
  - a position break on the track: stop/start, realign, Scene change, PC
    reset, loop engage/release/division change;
  - the target is latched, or erased by live record (the drain's filters);
  - an R12 menu override on the parameter (the menu edit wins until the
    next trigger, as today);
  - the track's played Scene changes;
  - the track's `asm` turns `off`.

### 5.6 Triggers during a ramp (decision)

- A resetting trigger during step A (live overlay press, MIDI note, flam
  main, roll renewal) does **not** restore parameters that an active ramp
  holds. The ramp continues.
- `seq_restoreAutomatedParameters()` skips those parameters' dirty bits
  while their ramp runs. It restores every other dirty parameter as today.
- At the next played step, the ramps have already ended (§5.5), so that
  step's trigger restores normally.

---

## 6. Live record and per-track playback

- **Live record (P1):**
  - recording on an `on` track writes per-step values exactly as today;
  - latched targets play the menu value, so they never ramp;
  - erased targets on rtk are filtered, so they never ramp;
  - no recording rule changes.
- **Per-track Scene playback:** the `asm` flag and B's entries are read from
  the track's played Scene (`seq_perTrackPattern[track]`), like all
  per-track data.
- **Loop-while-record (`S081_P3_LOOP_WHILE_RECORD.md`):** no interaction
  beyond §5.2's looper rule.

---

## 7. CPU (decision: baseline first, measured)

- **TIM3:** one step prediction per `on` track per step. This is a few
  integer operations and one block-header read for B's condition and
  micro-timing. It is small and fixed.
- **Foreground drain:** one `pat_readStepAutomations()` for B per `on` track
  per step, plus target matching (≤ 63 × 63 comparisons for two full
  steps; typically a handful).
- **Ramp service baseline:** every active ramp is written each 2 ms pass.
  - The worst case is **64 ramps × 500 Hz = 32,000** `writeRuntime` calls/s.
  - Filter and pitch setters recompute coefficients, so this is measured
    on the stress Scene with 64 ramps on the `cpu` widget, under the DSP CPU
    policy (worst case counts).
  - If the measurement is too high, the service switches to a fixed slice
    of 16 ramps per pass, round-robin. Each ramp is then updated every
    8 ms with 64 active, at a constant cost.
- **Writes happen only when the rounded value changes.** This saves calls
  on slow ramps. The budget is still the worst case.

---

## 8. RAM (acknowledgement needed before implementation)

All static, normal SRAM1.

| Item | Bytes | Owner | Lifetime |
|---|---|---|---|
| Ramp table, 64 slots × 8 B (track+target 2, from 1, to 1, start tick 2, duration 2) | 512 | Sequencer | static |
| Ramp active mask (64 bits) | 8 | Sequencer | static |
| Per-track ramp context (B step, valid, duration), 7 × 4 B | 28 | Sequencer | static |
| `track_auto_smooth` in `pat_scene_region_t`, × 17 regions (16 + background) | 17 | PatternData | static |
| `PAR_TRACK_AUTO_SMOOTH` mirror | 0 (existing `parameter_values[]` slot) | Menu | — |
| **Total** | **565** | | |

- DEV_MODE_LOGGING-only counters (no free slot, B not played as
  predicted): 4 B, logging builds only.
- SRAM1 free space is reserved for Pattern data under the policy, so the
  565 B needs your explicit acknowledgement.

---

## 9. Modules touched

| Module | Change |
|---|---|
| `PatternData.c/.h` | region field, set/get, init, region copy, `pat_applyTrackSettingsToMenu()` |
| `filesystem.c` | PAT4 writer byte 5; three readers |
| `copyClearService.c` | track-settings paste and clear |
| `ParameterArray.h` | `PAR_TRACK_AUTO_SMOOTH` mirror + static assert |
| `MenuText.h`, `menu.h` | `asm`, `Automtn` texts and enum entries |
| `menuPages.h` | SEQ sub-page 0 cell 7 |
| `menu.c` | name-table row; commit in `menu_cellCommitValue()`; no-op in `menu_parseGlobalParam()` |
| `sequencer.c/.h` | TIM3 B prediction + context; drain pairing; ramp service; end/cancel hooks; restore skip (§5.6) |
| `main.c` | ramp service call after the drain |
| `tools/convert_pattern_autosmooth.py` | new card update script (§3.2) |
| Docs (at close) | `PATTERN_DYNAMIC_STACK.md` (automation timing + ramps), PAT4 format notes, `STORAGE_SRAM_MANIFEST.md`, `MODULE_INTERCHANGE_SPEC.md`, `MEMORY.md` (binding rule summary) |

---

## 10. Decisions (user, 2026-10-13)

| # | Decision |
|---|---|
| D1 | B's condition must be predictable at A's apply time; otherwise B jumps (§5.2). |
| D2 | Instrument parameters only, filtered further by dtype: numeric-range dtypes ramp; `DTYPE_MENU` and the other selection dtypes never do (§5.3). |
| D3 | Ramped parameters are not restored by a resetting trigger while their ramp runs (§5.6). |
| D4 | Short name `asm`, long name `Automtn`, values `off`/`on` (§4). |
| D5 | Pattern setting in the PAT4 track header; a script updates the `.pat` files in `SD_CARD/`; AutoSaves are deleted by the user (§3). |
| D6 | Update rate: every active ramp each 2 ms, measured; 16-per-pass round-robin only if the measurement requires it (§7). |
| D7 | rev, onc and 1fr ramp like fwd and pip; rnd always jumps (§5.2). |
| D8 | Looper: ramps follow the unlooped segment; no ramp across the wrap back to the loop start; the last loop step ramps toward the step after the loop end when that step holds the parameter (§5.2, §5.5). |
| D9 | 7-bit ramp resolution is accepted (§5.5). |
| D10 | Cap of 64 simultaneous ramps; a pair without a free slot jumps (§1). |

---

## 11. Follow-ups

- **F1. PAT4 version.** Track-header byte 5 was reserved and is zero in
  every PAT4 file on the card, so no version change is needed. This is the
  Track Loopable precedent (byte 4, version stayed 2). The script then only
  normalises byte 5 and verifies the CRC. **Recommended.** Alternative: bump
  `PATTERN_FILE_VERSION` 2 → 3, so the field is formally part of the
  format. The script then rewrites the version byte in every file, and the
  firmware rejects any file that is not converted.
- **F2. Legacy Bank files.** `SD_CARD/Bank/*/*/pattern.pat` (229 files) are
  the old text format and are not loaded by the firmware. The script skips
  them. Do you want them left alone, deleted, or converted? Converting them
  is outside this plan.
- **F3. RAM.** Please acknowledge the 565 B in §8 (plus 4 B DEV-only).

---

## 12. Phasing

1. **Setting:**
   - storage, file I/O, copy/clear, menu cell;
   - the card script.

   Hardware check: save, load, copy and clear keep the flag, and `asm` shows
   after `lop`.
2. **Engine:**
   - TIM3 prediction, including the condition rule and the looper rule;
   - drain pairing;
   - ramp service, with the RAM tables after acknowledgement;
   - end and cancel hooks;
   - restore skip.
3. **Measurement and hardware matrix:**
   - stress Scene with 64 ramps (CPU);
   - fwd/rev/pip/onc/1fr, and rnd jumping;
   - shuffle and micro-timing on A and B;
   - tempo change mid-ramp;
   - predictable vs `rNN`/`fil` conditions on B;
   - menu override mid-ramp;
   - live hit and MIDI note mid-ramp (no dip);
   - live record latch on an `on` track;
   - Scene change and stop mid-ramp;
   - looper: last loop step ramps toward the step after the loop end, and
     there is no ramp across the wrap;
   - per-track Scene playback;
   - menu-to-automation and automation-to-menu still jump.
4. **Docs.**
