# S083 P2 — MIDI track fix-ups: handoff

- **Session:** 083, part 2 (2026-10-22).
- **Branch:** `dev-ph6-midi` (uncommitted at the time of writing).
- **Status:** implemented, built, host-tested, and **hardware-checked by the
  user**.
- **Scope:** everything after the S083 P1 assessment and fixes
  (`S083_P1_IMPLEMENTATION.md` §13–§14), from the user's request "seems ok, a
  couple of minor corrections…" onward. It is recorded in
  `S083_P1_IMPLEMENTATION.md` §15, §16 and §17; this document is the
  standalone handoff.

| Part | Topic | Log section |
|------|-------|-------------|
| A | Track settings page 2: `ch` `off`, `nte` `off`/`on` + category, `asm` → `rmp` | §15 |
| B | MIDI Learn persistence: own file `midilrn.cfg`, safe-written after every Learn | §16 |
| C | Global Morph also sets FX Morph (not recorded) | §17 |
| D | SHIFT+REC `pv<`/`pv>` overlay roll velocity rhythm; page 2 = `pv< pv> qnt rtk` | §17 |
| E | CC2 on G sets the PERF BAR overlay (state + LEDs), one shared overlay state | §17 |
| F | Comment-block audit of all landed changes (this handoff) | — |

**Final gates:**

- `make all` / `make img` PASS. The only warnings are older ones: the 7
  `filesystem.c` unused-function warnings, and `PatternData.c:201` /
  `EuklidGenerator.c:54` when those units recompile.
- Link `text=608,744`, `data=424`, `bss=430,112`; flash 608,848 of 753,664 B.
- `make -C tools/midi_test run` → `PASS (0 failures)`.

---

## Part A — Track settings page 2 (`ch`, `nte`, `rmp`)

### A.1 Request

The second page of the STEP track settings is `ch nte lop asm`. The user
asked for:

- `ch`: add `off` below `glb`.
- `nte`: category Track (it showed Step); add `off` below the default;
  relabel `def` as `on`; the default stays `on`.
- `asm`: short label `rmp`, category `Automtn`, long name `Ramp`.

### A.2 What the new values mean

The request named the values but not their effect. The meanings below were
implemented and confirmed on hardware.

| Setting | Effect |
|---------|--------|
| `ch glb` (default) | Follow the global channel G (unchanged). |
| `ch off` | The track has **no MIDI channel**: no notes in or out, no voice-channel CC1/NRPN/Learn/PC, no per-track PC out. |
| `nte on` (default, was `def`) | No root: the track shares its channel's notes (stripe on G, split elsewhere). Unchanged behaviour. |
| `nte 1..127` | Root of the track's receive range (unchanged). |
| `nte off` | The track is **left out of the note map**: no notes received or sent, and it takes no stripe/split position. Its channel still carries CC1/NRPN/Learn/PC. |

### A.3 Storage versus menu order

The defaults stay stored as 0, so no stored default changed and no migration
is needed. The new `off` value is appended above the old range in storage,
but shown first in the menu.

| Setting | Stored (Scene settings, `sceneset.scg`, AutoSave) | Menu order |
|---------|--------------------------------------------------|------------|
| Track `ch` | 0 `glb`, 1..16, **17 `off`** (`MIDI_TRACK_CHANNEL_OFF`) | 0 `off`, 1 `glb`, 2..17 = ch 1..16 |
| Track `nte` | 0 `on`, 1..127, **128 `off`** (`MIDI_TRACK_NOTE_OFF`) | 0 `off`, 1 `on`, 2..128 = notes 1..127 |

**Why two orders:** the user wanted `off` listed below `glb`/`on`, but
moving `off` to stored 0 would change the stored default and every existing
`sceneset.scg`. `menu.c` converts at the edge
(`menu_trackMidiChanToMenu/FromMenu()`, `menu_trackMidiNoteToMenu/FromMenu()`):
menu = stored + 1, with `off` at 0.

### A.4 Changes

1. **`Core/MIDI/MidiDefs.h`**
   - ADD `MIDI_TRACK_CHANNEL_OFF` (17) and `MIDI_TRACK_NOTE_OFF` (128).
   - The contract block defines both byte domains, their effects and the
     menu order.
   - These are the single source for the two values.
2. **`Core/Bank/Scene/SceneData.c/h`**
   - `scene_setTrackMidiChannel()` clamps to 0..17 (anything above becomes
     `off`).
   - `scene_setTrackMidiNote()` clamps to 0..128 (anything above becomes
     `off`).
   - The getters return the `off` values. The channel getter returns 0
     (unassigned) for stale bytes above 17; the pre-S083 fallback to "track
     + 1" stays retired.
   - Contract blocks updated.
3. **`Core/Hardware/SD/storageTypes.c`**
   - The `sceneset.scg` `midi_channel` parser accepts 0..17 and `midi_note`
     accepts 0..128, so `off` survives Save/Load.
   - Includes `MidiDefs.h`.
4. **`Core/MIDI/MidiNoteMap.c/h`** (stateless and host-testable):
   - `midiNoteMap_effectiveChannel()` returns 0 for `ch off`; it does not
     fall back to G.
   - New static `midiNoteMap_noteTracksOnChannel()` is
     `midiNoteMap_tracksOnChannel()` minus the `nte off` tracks. Both
     `midiNoteMap_resolve()` (RX) and `midiNoteMap_inverse()` (TX) use it,
     and `inverse()` refuses a `nte off` track.
   - `midiNoteMap_tracksOnChannel()` still includes `nte off` tracks, because
     CC1/NRPN/PC routing needs them.
   - The header keeps host-local copies of the two values
     (`MIDI_NOTEMAP_CHANNEL_OFF`/`_NOTE_OFF`) so the host test needs no
     firmware headers. A firmware `_Static_assert` ties them to `MidiDefs.h`.
5. **`tools/midi_test/notemap_test.c`**
   - New cases:
     - `ch off` leaves the G stripe and has no channel.
     - `nte off` leaves the stripe but keeps its channel, and sends nothing.
     - A track that is alone on its channel with `nte off` is unmapped.
   - The randomized settings now include both `off` values. A new invariant
     checks that a `nte off` track never resolves.
6. **`Core/Menu/menu.c`**
   - `menu_getParameterDisplayValue()` reads track `ch`/`nte` from the
     **viewed Scene** in menu order.
     - This also fixed a pre-existing stale display: the `parameter_values[]`
       mirror was never refreshed from the Scene, so the cells showed the
       last edit instead of the track and Scene in front of the user.
   - Clamps (`menu_clampCellValue()` static-param cases): G `ch` 0..16, track
     `ch` 0..17, track `nte` 0..128.
   - `menu_getMidiStaticName()` names the full lists: `off`/`glb`/channel
     and `off`/`on`/note name.
   - `menu_parseGlobalParam()` converts menu values to stored values, stores
     them in the viewed Scene, and calls `midiOut_trackNoteOff()` when `ch`
     **or** `nte` changes, so no note hangs.
   - Prototypes come before `menu_getParameterDisplayValue()` because the
     first build failed with an implicit declaration.
7. **`menu.h`, `menu.c` (`valueNames`), `menuPages.h`**
   - New `TEXT_TRACK_NOTE` (`nte`, category **Track**, long name Note) for
     the STEP track-settings page. The step-special note on the STEP step
     page keeps `TEXT_NOTE` (category Step).
   - New `CAT_AUTOMATION` (`Automtn`); `TEXT_TRACK_AUTO_SMOOTH` uses it.
8. **`Core/Menu/MenuText.h`**
   - Short name `asm` → `rmp`; long name `Automtn` → `Ramp`.
   - The category list gains `Automtn`.
   - The unused `menuText_def` was removed (`menuText_on` covers the
     default).
9. **Comments**
   - Every comment naming the cell `asm` now says `rmp`, in
     `PatternData.c/h`, `ParameterArray.h`, `menu.c`, `menuPages.h` and
     `sequencer.c/h`.
   - Code and storage identifiers keep their names: `auto_smooth`,
     `PAR_TRACK_AUTO_SMOOTH`, PAT4 track byte 5. No format change.

**RAM:** none.

---

## Part B — MIDI Learn persistence: `midilrn.cfg`

### B.1 Problem and root cause

Learn assignments did not survive a reboot.

**Root cause:** `midiLearn_formatSettingsLine()` returned `1` instead of the
line **length** in both of its returns.

- `filesystem_writeTextLine()` writes exactly the returned number of bytes,
  so every Learn row reached `settings.cfg` as the single character `l`.
- On load, the first `l` line (no `=`) was a parse error. The 24 fixed lines
  before it had already loaded, but reading stopped and no row was restored.
- The defect came from the S083 P1 schedule's F-3 text.

### B.2 Decision

The user asked for Learn rows to move to their own root file, `midilrn.cfg`,
**safely rewritten every time a Learn is made**, instead of rewriting
`settings.cfg`. Seven follow-up decisions were all accepted:

1. **Format:** keyed text, editable by hand:
   ```
   format=helicase.midilearn
   version=1
   learn=020,g,v1,drm,filter_freq
   learn=023,g,scn,1vm
   lines=4
   ```
   - Rows keep the existing format:
     `learn=CCC,<g|v>,<v1..v6|fx|scn>,<type>,<key>`. Scene rows have no type.
   - `lines=N` counts every line before it (2 + rows).
   - An unresolvable row is written as `# learn row unresolved` so the count
     stays exact.
2. **`settings.cfg`** returns to its fixed lines (24 at the time; 26 after
   Part D). A `learn=` line left by the first S083 build is an unknown key:
   it is ignored and dropped by the next settings write. There is no
   migration.
3. **Safe write:** the `settings.cfg` method. Write `midilrn.tmp`, close,
   `afatfs_sync()`, remove the old `midilrn.cfg`, rename. At boot, a leftover
   tmp is validated by its terminator, then promoted or discarded. Both files
   are in the card root.
4. **Timing:**
   - Immediate, with no debounce, whenever a row is stored or replaced. A
     full-table (`FUL`) refusal writes nothing.
   - The write waits for an idle facade, so AutoSave, a Load/Save command and
     a background Bank load are all waited out.
   - A Learn made during a write queues **exactly one** more write.
   - Learns made during an unlocked background Bank load are accepted.
5. **Boot:**
   - Load right after `settings.cfg`, before audio.
   - A missing file is an empty table: a card has none until its first
     Learn.
   - Malformed rows are skipped.
   - A settings load no longer clears the Learn table.
6. **RAM:** 1 B request flag, SRAM1, static, owned by the filesystem.
7. **Trace:** a DEV_MODE_LOGGING record, in logging builds only.

### B.3 Changes

**`Core/MIDI/MidiLearn.c/h`**

- **Fix:** both returns of the formatter (real row and `#` comment fallback)
  return `(uint8_t)pos`, the line length.
- **Renamed** for the new owner file:
  - `midiLearn_formatSettingsLine()` → `midiLearn_formatFileLine()`
  - `midiLearn_parseSettingsValue()` → `midiLearn_parseFileValue()`
- `midiLearn_captureCc()` calls `filesystem_requestMidiLearnWrite()` after it
  stores or replaces a row. It no longer calls
  `filesystem_markSettingsDirty()`. The `FUL` path writes nothing.
- The header block and the format/parse contract describe `midilrn.cfg`:
  the return is the length, and the callers are named.

**`Core/Hardware/SD/storageTypes.h`**

- ADD `STORAGE_MIDILEARN_FILENAME` (`"midilrn.cfg"`) and
  `STORAGE_MIDILEARN_TEMP_FILENAME` (`"midilrn.tmp"`), with a contract block.

**`Core/Hardware/SD/filesystem.h`**

- ADD `filesystem_requestMidiLearnWrite()` and
  `filesystem_requestLoadMidiLearn(cb)`.
- One contract block covers the gates, callers, outputs and the
  "always DONE" load rule.

**`Core/Hardware/SD/filesystem.c`: `settings.cfg` revert**

- REMOVE `FS_SETTINGS_FIXED_LINES`.
- REMOVE the learn block in `filesystem_nextSettingsLine()`.
- REMOVE the `learn` branch in `filesystem_parseSettingsLine()`. A comment
  now says the key is ignored as unknown.
- REMOVE `midiLearn_clearAll()` from `filesystem_resetSettingsToDefaults()`.
- RESTORE the fixed terminator case.

**`Core/Hardware/SD/filesystem.c`: new ops**

- `FS_INTERNAL_OP_LOAD_MIDILEARN` and `FS_INTERNAL_OP_SAVE_MIDILEARN` are
  **appended at the end** of `fs_internal_op_t`, so the numeric op codes in
  trace records and the decoder stay stable.
- Boot-log code `MLRNLOAD`; error-code names `MlrL`/`MlrS`; dispatch cases
  in the op switch.

**`Core/Hardware/SD/filesystem.c`: state**

- `fs_midilearn_write`, 1 B: bit 0 `FS_MIDILEARN_WRITE_PENDING`, bit 1
  `FS_MIDILEARN_WRITE_RETRIED`.
- Cleared on facade reset and in `filesystem_initAfterCardReady()`.
- The boot load reuses the four `op_settings_recovery_*` bytes (the two ops
  never overlap); their comment records this.

**`filesystem_parseMidiLearnLine()`**

- Returns DONE (accepted or skipped) for:
  - blank lines;
  - `#` comments;
  - lines without `=`;
  - `lines=`;
  - unknown keys;
  - malformed or unresolvable `learn=` rows.
- Returns ERROR only for a wrong `format`/`version` value.

**`filesystem_nextMidiLearnLine()`**

- Emits the header, the rows, then `lines=2+rows`: at most 131 lines of
  about 60 characters or fewer.
- Rows are only appended or replaced at runtime, so a row learned mid-stream
  is either written now (the live count keeps `lines=` exact) or by the
  queued follow-up write.

**`filesystem_loadMidiLearn_tick()`**

- **Pass A (phases 0–8)** mirrors the `settings.tmp` prelude:
  - Validate the tmp: no format/version error, and the last line is
    `lines=N` with N == lines − 1.
  - Then promote it (remove `midilrn.cfg`, rename) or discard it.
- **Pass B (phases 10–14)** clears the table and reads `midilrn.cfg`:
  - A missing file means an empty table.
  - A format/version error empties the table and stops reading.
  - A read error, such as an over-long hand-edited line, keeps the rows read
    so far.
- The op **always finishes DONE**: a Learn-file problem never fails the boot
  ladder.

**`filesystem_saveMidiLearn_tick()`**

- The `saveGlobals` safe-write phases 0–10 on the Learn files.
- It shares the settings writer's DEV_STALL_DETECTION observer: code `MlWr`,
  site 8. The two writers never overlap.

**Scheduler: `filesystem_midiLearnWriterSchedule_tick()` and
`filesystem_midiLearnWriterCompleted()`**

- **Start gates:**
  - PENDING is set;
  - `!menu_isLoadSaveCommandActive()`;
  - `fs_settings_runtime_ready` (the boot gate opened by
    `filesystem_enableRuntimeSettingsWrites()`);
  - the volume is READY;
  - the caller runs it only with status IDLE and no copy/clear suspension.
- PENDING is cleared as the write starts, so a Learn during the write
  re-arms it. A refused start restores PENDING.
- **Retry policy:** the first ERROR requeues once (PENDING|RETRIED); a second
  ERROR gives up until the next Learn re-arms it. A failing card therefore
  cannot hold the facade in a retry loop.
- Completion acknowledges privately (`filesystem_ack()`), like the settings
  writer.
- `filesystem_tick()` runs this scheduler **before** the debounced settings
  writer, under the same idle and copy/clear guard.

**`main.c`**

- After the settings load and `filesystem_setAutosaveEnabled()`:
  `filesystem_requestLoadMidiLearn(NULL)`.
- Then poll while BUSY under the boot-logging timeout, `goto
  boot_filesystem_timeout` on timeout, then `filesystem_ack()`.

**`Core/Bank/Scene/AutosaveTrace.h`**

- ADD stage `m` (`AUTOSAVE_TRACE_STAGE_MIDI_LEARN_FILE`) with three events:

| Event | `value32` |
|-------|-----------|
| 0x01 WRITE | rows \| failed << 8 \| request flags after << 16 |
| 0x02 RECOVERY | outcome \| lines << 8 \| N << 16 \| terminator seen << 24. Outcomes: 1 promoted, 2 discarded, 3 promote remove failed, 4 promote rename failed |
| 0x03 LOAD | rows \| present << 8 \| result << 16 \| lines read << 24. Results: 0 ok, 1 bad format/version, 2 read error |

**`tools/decode_devlogs.py`**

- Stage `m` name, producer and decoder branch; boot code `MLRNLOAD`; the two
  new op names.
- **Also fixed:** a pre-existing drift. `FS_INTERNAL_OP_PATTERN_TRACE_FLUSH`
  was missing from the op-name list, so every op from
  `AUTOSAVE_PATTERN_DRAIN` onward decoded as its neighbour. The list now
  matches the enum: 54 entries, checked by script.

**RAM:** +1 B (`fs_midilearn_write`), which fits in alignment padding (bss
unchanged).

---

## Part C — Global Morph sets FX Morph

### C.1 Request and cause

FX Morph did not respond to the global Morph. The rule is "global Morph sets
all Morph", and the FX Morph it sets is **not recorded**, like the voice
Morphs.

`preset_morphScene()` wrote only the stored byte
(`scene_setEffectMorphAmount()`). Three things then kept the old amount:

- **Pattern `fxm` override:** it was never cleared (S076 Rule A). It
  outranks the stored amount in `effects_service()`'s Morph-base
  resolution.
- **Menu hold not set:** `menuHoldMorph` was never set, so an FX Morph lock
  step still won.
- **Display not refreshed:** the `PAR_EFFECT_MORPH` mirror and the `fxm`
  cell stayed stale.

Both the panel PERF `mrp` edit (`menu_parseGlobalParam(PAR_MORPH)`) and MIDI
CC1 on G (`menu_commitGlobalMorphFromMidi()`) go through
`preset_morphScene()`.

### C.2 Changes

**`Core/DSP/Effects/EffectsManager.c/h`**

- ADD `effects_setMorphAmountFromGlobal(scene, amount)`. It sets **one**
  Scene with no fan-out, because `preset_morphScene()` already runs once per
  edit-mask Scene.
- It applies the same rules as an `fxm` menu edit:
  - `effects_setMorphAmountScene()` stores the amount and clears the Pattern
    `fxm` override for the active Scene;
  - on the active Scene it sets `menuHoldMorph`, so the new amount holds over
    an FX Morph lock until the next Morph automation (S081 §5.3).
- It never records.
- The caller list on `effects_setMorphAmountScene()` is updated.

**`Core/Bank/Scene/Preset/presetManager.c`**

- `preset_morphScene()` calls the new function instead of the bare stored
  write.
- For the active Scene it also calls `preset_syncEffectMorphMirror()`.
- The `preset_morph()` and `preset_morphScene()` comment blocks now include
  the Effect Morph.

**`Core/Menu/menu.c`**

- `menu_commitGlobalMorphFromMidi()` also repaints `fxm`
  (`menu_notifyTargetChanged(sceneModTarget_effectMorphId())`).

### C.3 Safety check

A settings load never reaches this path. Bulk Global apply
(`menu_sendAllGlobals()` / `menu_tickGlobalApply()`) starts at
`PAR_BEGINNING_OF_GLOBALS` (135), and `PAR_MORPH` is 78. So a load cannot
set the menu hold without a user edit.

**Recording:** unchanged. The global Morph was already never recorded (G-1),
and the new path calls no LiveRecord function.

**RAM:** none.

---

## Part D — SHIFT+REC `pv<` / `pv>` (overlay roll velocity rhythm)

### D.1 Request

- Two new SHIFT+REC parameters, `pv<` and `pv>`. They set the velocity
  rhythm of the roll when the BAR overlay `<`/`>` trigger type is a roll,
  using the same values as the step special (`1-2`, `1-3`, …).
- The second SHIFT+REC page becomes `pv< pv> qnt rtk` on one page, instead
  of `qnt` and `rtk` on separate pages.

### D.2 Design

- **Value:** the packed roll-rhythm byte of the step special `rtm`
  (`PAR_STEP_RHYTHM`). Bits 7..4 are the cycle, bit 3 is 0 halve / 1 double,
  bits 2..0 are the full count.
- **List:** the same ordered list `stepRollRhythmBytes[]`: `off`, then
  `1-2 1-3 2-3 1-4 2-4 3-4 1-5 2-5 3-5 4-5`, then the same ten with `+`.
  The same labels (`menu_getRollRhythmName()`) and stepping
  (`menu_rollRhythmStep()`) are used, so the new cells cannot drift from the
  step cell.
- **One rule for both rolls:** the step-roll velocity decode became the
  public, pure `seq_rollRhythmVelocity(rhythm, vel, pos)`. Step rolls
  (`seq_rollComputeVelocity()`) and overlay rolls both call it.
- **Velocity:**
  - Overlay roll hit 0 is 100 (`BH_OVERLAY_VEL`); it is always inside the
    full window.
  - Each repeat plays the rhythm velocity at its position: halve = 50
    (floor 1), double = 127 (clamped from 200).
  - Examples: `1-2` gives 100, 50, 100, 50…; `1+3` gives 100, 127, 127, 100…
- **Scope:**
  - Only roll trigger types use it; single and flam ignore it. The flam main
    hit stays at 100.
  - Panel-held and MIDI-held rolls both use it.
  - The rhythm is read live at each repeat from the **active** overlay
    (`bh_overlayActive`), so a `pv` edit or an overlay change applies from
    the next hit.
- **Recording:** LiveRecord receives the actual played velocities. The
  rhythm itself is **not** written into recorded steps, following the
  existing S081 rule "rhythm is never recorded" (`pat_record_spec_t`).
- **Persistence:** `pv<`/`pv>` are persisted Globals like `pb<`/`pn<`, as
  `settings.cfg` `perf_vel_lt`/`perf_vel_rt` (default 0 = off). A
  RECORDING_PAGE edit already marks settings dirty in `menu_cellCommitValueEx()`.

### D.3 Changes

**`Core/Sequencer/sequencer.c/h`**

- ADD `seq_rollRhythmVelocity()`, with the contract in `sequencer.h`. It is
  the former body of `seq_rollComputeVelocity()`, which is now a wrapper
  supplying the step roll's per-track state. Step-roll behaviour is
  unchanged.

**`Core/Bank/Scene/Preset/ParameterArray.h`**

- ADD `PAR_PERF_VEL_LT` and `PAR_PERF_VEL_RT`, **appended after
  `PAR_TRACK_AUTO_SMOOTH`** so no established id is renumbered. They use
  spare `parameter_values[]` capacity, with a `_Static_assert`.

**`Core/Menu/menu.h`, `MenuText.h`, `menu.c`**

- Labels: `TEXT_PERF_VEL_LT/RT` (appended before `NUM_NAMES`),
  `SHORT_PERF_VEL_LT/RT` (`pv<`/`pv>`), `LONG_PERF_VEL_LT/RT`
  (`Rhythm<`/`Rhythm>`), category `CAT_PERFORMANCE` (Prfrmnc), and their
  `valueNames` rows.
- Dtype `DTYPE_0B255`, as for `PAR_STEP_RHYTHM`.

**`menu_paramIsRollRhythm()`**

- New predicate, true for `PAR_STEP_RHYTHM`, `PAR_PERF_VEL_LT` and
  `PAR_PERF_VEL_RT`.
- It replaces the six `static_param == PAR_STEP_RHYTHM` cell tests:
  - two value-name paths (`menu_formatCellValue3()`, the compact renderer);
  - the clamp, which snaps an unlisted byte;
  - the full-view `DTYPE_0B255` label;
  - the encoder list step;
  - the pot list step.
- The `menu_parseGlobalParam()` `case PAR_STEP_RHYTHM` (step broadcast) is
  deliberately untouched: `pv<`/`pv>` need no runtime apply and fall to the
  default case.

**`Core/Menu/menuPages.h`**

- RECORDING_PAGE sub-page 1 is now `pv< pv> qnt rtk`. Sub-page 2 (formerly
  `rtk`) is reserved.
- The generic S080 cross-sub-page encoder step and
  `menu_staticFirst/LastPopulatedColumn()` needed no change.

**`Core/Hardware/frontPanel/buttonHandler.c`**

- The overlay entry field `vel` (always 100) became `pos`, the roll hit
  position, so the 8 B entry size is unchanged.
- ADD `BH_OVERLAY_VEL` (100).
- `buttonHandler_overlayStart()`: a roll's hit 0 plays 100 and sets
  `pos = 1`; a flam sets `pos = 0`.
- `buttonHandler_overlayReconfigure()`: a change from single/flam to roll
  continues at `pos = 1` (the hit already played was hit 0); roll to roll
  keeps `pos`.
- `buttonHandler_overlayTick()`: a roll repeat plays
  `seq_rollRhythmVelocity(pv of the active overlay, 100, pos)` and then
  advances `pos`.
  - At 255 it wraps to 16 (256 mod 60), which keeps the phase for every
    cycle length 2..5.
  - The flam main plays 100.

**`Core/Hardware/SD/filesystem.c`**

- Defaults: `PAR_PERF_VEL_LT/RT` = 0.
- Parser keys `perf_vel_lt`/`perf_vel_rt`.
- Writer cases 24/25; the terminator moves to case 26, `lines=26`.
- Old files with `lines=24` load normally, with the new keys at 0. The
  normal load ignores the terminator; only the tmp recovery checks it, and
  it validates `N == lines − 1` for whatever file it reads.

**RAM:** none.

---

## Part E — CC2 sets the PERF BAR overlay

### E.1 Request and finding

The user expected a MIDI assignment for the PERF roll/trigger overlay that
also updates the LEDs, and asked whether it was CC2. **It is CC2**
(MIDI_SPEC §5.3), and the user confirmed: **G only, for both the trigger and
the overlay change.**

As built in P1, CC2 kept its own state in `MidiNotes` (`midiNotes_action`,
1 B) that only shaped received MIDI notes:

- it never changed `bh_overlayActive`, so the panel VOICE overlay and the
  BAR LEDs did not follow;
- a BAR overlay active in PERF overrode it (C-9).

From the panel it looked as if nothing happened.

### E.2 Design: one overlay state

- `bh_overlayActive` (0 off, 1 `<`, 2 `>`) is the only overlay state.
- Both the BAR presses in PERF and CC2 on G (any mode) set it. **The last
  one wins.**
- Received notes use it in any mode; the panel VOICE buttons use it in PERF.
- **LEDs:** CC2 updates the BAR LEDs only in PERF, and only while the looper
  does not own them. The existing paths restore the LEDs from
  `bh_overlayActive` on PERF mode entry and on looper disengage.
- **Retired:** the C-9 "BAR wins over CC2" priority and the F-3
  panel-versus-MIDI re-configure split.
- **Consequence (accepted with the hardware check):** an overlay turned on
  with BAR in PERF also shapes received MIDI notes after leaving PERF, just
  as a CC2 overlay always did in any mode. Restricting MIDI-note overlays to
  PERF would be a small change if ever wanted.

### E.3 Changes

**`Core/Hardware/frontPanel/buttonHandler.c/h`**

- ADD `buttonHandler_overlayActive()`, which returns the state in any mode.
- ADD `buttonHandler_setOverlay(which)`, which makes the same change as a
  BAR press:
  - sets the state;
  - updates the BAR LEDs in PERF when `!seq_loopActive`;
  - re-configures held entries through `buttonHandler_overlaySwitchHeld()`.

  A repeated value does nothing.
- REMOVE `buttonHandler_overlayHeldBar()` and
  `buttonHandler_overlayMidiActionChanged()`.
- REMOVE the `BH_OVERLAY_SEL_ALL/MIDI/PANEL` selector:
  `buttonHandler_overlayReconfigure(which)` now re-configures every held
  entry, panel or MIDI.
- `buttonHandler_overlaySwitchHeld()` lost its CC2 branch.
- The overflow recovery calls `buttonHandler_overlayReconfigure(0u)`.
- Comment blocks updated: the state variable, re-configure, switch-held,
  `setOverlay`, and the header contract.

**`Core/MIDI/MidiNotes.c/h`**

- REMOVE `midiNotes_action` (−1 B) and `midiNotes_noteAction()`.
- `midiNotes_setNoteAction()` validates 0..2 and calls
  `buttonHandler_setOverlay()`.
- `midiNotes_handle()` uses `buttonHandler_overlayActive()`.
- Module and contract blocks updated (no own RAM).

**`Core/MIDI/MidiControl.c`**

- The G-only check is unchanged:
  `case CC_NOTE_ACTION: return onG ? midiNotes_setNoteAction(value) : 0u;`
- A comment block now states the rule. On any other channel CC2 is not
  consumed and is forwarded.

**RAM:** −1 B.

---

## Part F — Comment-block audit (done for this handoff)

Every change in Parts A–E was checked for a code-adjacent block covering
what, why, inputs/outputs, callers and affiliates. Most already had one;
these were added or refreshed:

| Location | Fix |
|----------|-----|
| `menu.c` `menu_formatCellValue3()` and the full-view renderer | Two call-site comments still said `off / glb / def`; they now name the menu-order lists (`off`/`glb`/channel, `off`/`on`/note). |
| `MidiNoteMap.c` `midiNoteMap_noteTracksOnChannel()` | Expanded to a full block: why `nte off` leaves only the note map, why its channel still counts for CC/NRPN/Learn/PC, accessors, host-test coverage. |
| `sequencer.c` `seq_rollComputeVelocity()` | Notes the move of the decode into `seq_rollRhythmVelocity()` and why. |
| `buttonHandler.c` overlay start / re-configure / tick | Roll position 0/1 rules, `BH_OVERLAY_VEL`, live `pv` read, flam main velocity, reported velocity. |
| `presetManager.c` `preset_morph()` / `preset_morphScene()` | Outputs include the Effect Morph and the `fxm` mirror; nothing is recorded. |
| `EffectsManager.c` `effects_setMorphAmountScene()` | Caller list includes `effects_setMorphAmountFromGlobal()`. |
| `MidiControl.c` CC2 case | New block: G only, sets the BAR overlay, forwarded elsewhere. |

The rebuild after the audit had identical sizes (comments only), and the
host test passed.

---

## Specifications and memory updated

- **`MIDI_SPEC.md`:**
  - §2.2: track `ch`/`nte` lists, stored values, effects.
  - §4.1: `off` tracks. §4.3: overlay state and roll rhythm.
  - §5.2: global Morph sets FX Morph, never recorded.
  - §5.3: rewritten (CC2 sets the BAR overlay; C-9 retired).
  - §6.1/§6.3/§6.4: `midilrn.cfg`.
  - §10 RAM, §11 storage, the as-built table.
- **`FILESYSTEM_SPEC.md`:**
  - root file lists (`midilrn.cfg`);
  - `settings.cfg` schema: 26 lines + `lines=26`, `perf_vel_*`, `learn=`
    ignored;
  - new section "`midilrn.cfg` — MIDI Learn table (S083)";
  - the copy/clear suspension list;
  - `sceneset.scg` rows 4/5 (`midi_channel` 0..17, `midi_note` 0..128).
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - SEQ_PAGE row (`ch nte lop rmp`), RECORDING_PAGE row (`pv< pv> qnt rtk`);
  - overlay entry points and table fields;
  - MidiNotes and MidiLearn rows;
  - S080 settings keys note.
- **`PATTERN_DYNAMIC_STACK.md`:** §6.2b heading and label `rmp`.
- **`DEV_MODES.md`:** stage `m` table; `MlWr` stall row (site 8, shared with
  Settings).
- **`STORAGE_SRAM_MANIFEST.md`:** +1 B (`fs_midilearn_write`), −1 B (CC2
  state), the `vel` → `pos` reuse.
- **`MEMORY.md`:**
  - the MIDI / Clock reminders: track `ch`/`nte` binding, Learn in
    `midilrn.cfg`, CC2 = BAR overlay, global Morph sets FX Morph, SHIFT+REC
    page 2;
  - the module tree line.

---

## Hardware verification (user: "hardware checks out")

The checks listed in `S083_P1_IMPLEMENTATION.md` §15.5, §16.3 and §17.4:

- **Part A:**
  - The page reads `ch nte lop rmp`, with the full names "Voice Channel",
    "Track Note" and "Automtn Ramp".
  - `ch`/`nte` follow the viewed track and Scene.
  - `ch off` and `nte off` behave as in A.2.
  - The values survive reboot (`sceneset.scg` 17/128).
- **Part B:**
  - The first Learn creates `midilrn.cfg` and leaves no tmp.
  - Learns survive reboot; re-learn and macros rewrite the file.
  - A Learn while the card is busy is written afterwards; `FUL` writes
    nothing.
  - `settings.cfg` has no learn rows.
  - tmp promote/discard works, and hand edits load.
- **Part C:** `mrp` and CC1 on G move FX Morph, including over `fxm`
  automation until its next step. No `fxm`/`Nvm` lane is recorded.
- **Part D:** page 2 is `pv< pv> qnt rtk`; roll velocities follow the
  rhythm, for panel and MIDI rolls; the values survive reboot.
- **Part E:** CC2 0/1/2 on G sets the BAR overlay and LEDs, and the last
  action wins. CC2 on other channels is ignored and forwarded.

---

## Notes for the next session

- **Uncommitted:** all of Parts A–F sit on `dev-ph6-midi` on top of commit
  `d007027` ("s083 midi implemented, untested").
- **Card files:**
  - `midilrn.cfg` is new on cards.
  - `settings.cfg` is now 26 lines. Older 24-line files load with
    `pv<`/`pv>` = off.
  - Files from the first S083 build carry broken `l` lines; they are ignored
    and rewritten on the next settings write.
- **Rhythm not recorded:** `pv` rhythms are not recorded into steps (S081
  rule). If recorded overlay rolls should replay with their rhythm, that is
  a LiveRecord / `pat_record_spec_t` change.
- **Shared overlay state:** a BAR overlay set in PERF also applies to MIDI
  notes in other modes (Part E.2).
- **Learn write retries:** one automatic retry, then the next Learn re-arms
  it. A card that keeps failing leaves `midilrn.cfg` stale until a later
  Learn succeeds. The `m` WRITE record and the `E` record show it.
- **Pre-existing, untouched:** the 7 `filesystem.c` unused-function warnings
  (§14.1).
