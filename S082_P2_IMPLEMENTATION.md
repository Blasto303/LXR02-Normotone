# S082 P2 — Implementation Schedule (audio during a background Bank load)

*Written 2026-10-19 from a code deep dive of the working tree: `45846b7` plus
the uncommitted S082 P0, P1, assessment fixes and Bank slot scrolling fix.
Planning only: no code has changed. It implements fixes 1–3 of
`S082_P2_LOAD_SAVE_FOLLOWUP.md`:*
- *P2-1, the Effect source rebind;*
- *P2-2, the end-of-load marking burst (its diagnosis is taken as correct);*
- *P2-3, idle-time filesystem servicing.*

---

## 0. How to use this schedule

### 0.1 Line numbers

- Every location is `file:line` in the working tree described above. Each
  entry also names its **anchor function**, because line numbers move as soon
  as the first edit lands.
- **Commit the working tree first**, so these numbers are pinned.
- Within one file, apply edits bottom-up, or re-find each anchor by name.

### 0.2 Actions

| Tag | Meaning |
|-----|---------|
| **ADD** | New code (function, declaration, branch, constant). |
| **MOD** | Change existing code in place. |
| **DOC** | Comment-only change. |

### 0.3 Comment blocks

Every ADD and MOD carries the block to paste next to it, in the project
format (What / Why / Inputs / Outputs / Accessors / Affiliates). The `.h`
declarations carry the shortened form (What / Inputs / Outputs / Accessors).

### 0.4 Order and build

| Step | Fix | Files |
|------|-----|-------|
| F1 | P2-1: Effect source rebind at the Scene 17 switch | `EffectsManager.c/.h`, `presetManager.c` |
| F2 | P2-2: whole-Scene AutoSave marking as one range | `Autosave.c/.h` |
| F3 | P2-3: idle-time filesystem servicing during a background Bank load | `config.h`, `filesystem.c/.h`, `main.c` |

Build after each step (`make all && make img`) and run the link budget
(`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`). There
must be no new warnings beyond the existing unused-function set in
`filesystem.c`.

### 0.5 RAM

| Item | Bytes | Region | Build | Status |
|------|------:|--------|-------|--------|
| F1 | 0 | — | — | — |
| F2 | 0 | — | — | — |
| F3 `fs_stall_poll_uncounted` | 1 | SRAM1 static | `DEV_STALL_DETECTION` only | Same class as the existing stall-observer statics (S082 §13.4); **please acknowledge**. Production builds add nothing. |

### 0.6 Correction to the P2 assessment

- The render queue holds two 96-frame slots (`config.h:795`, 2.18 ms each;
  `AudioCodecManager.c:330-333`), not 32-frame blocks.
- So the headroom is roughly 2–4 ms after a full queue, and less when a render
  is due. The diagnosis of P2-2 is unchanged.
- `S082_P2_LOAD_SAVE_FOLLOWUP.md` §3 P2-2 has been corrected.

---

## Fix 1 — Effect source rebind at the Scene 17 switch (P2-1)

**Cause, recap:**
- `effects_service()` (`EffectsManager.c:1807`) runs in the mixer every render
  block (`mixer.c:893`). It applies the record of `effects_state.scene_index`:
  level, route, pan, all type parameters, Morph and the FX sequence.
- Only `effects_activateScene()` (`EffectsManager.c:1713-1743`) sets that
  index. The Scene 17 switch performs no activation, so the index keeps naming
  the home Scene.
- The Bank load then overwrites the home Scene's Effect record
  (`filesystem_commitSceneStage()`, `filesystem.c:17619`).

**Fix:** retarget the index only. Scene 17's record is a byte-for-byte copy of
the home record taken at the build, so `effects_service()` computes the same
values, `last_applied[]` already holds them, and nothing is written to the
runtime.

### F1.1 `Core/DSP/Effects/EffectsManager.c` after `effects_activateScene()` (ends 1743) — ADD `effects_rebindSourceScene()`

```c
/*
 * Retarget the running Effect to an identical copy of its source Scene
 * (S082 P2 fix 1).
 *
 * What: changes only effects_state.scene_index, the Scene whose Effect
 * record effects_service() reads every render block. The runtime type and
 * member, its arena and tails, last_applied[], the FX sequence step and clock
 * state, the selected step, Pattern overlays, menu holds, LFO contributions
 * and the force flag are all left exactly as they are. Refused when the new
 * Scene has no record or its record's type differs from the running type.
 * Why: the Scene 17 switch must make no audible change (S082_P1 D4, D5). A
 * full effects_activateScene() resets the sequence step, clears automation
 * overlays and forces every parameter write, which is audible. Without any
 * rebind, the index keeps naming the home Scene, and the Bank load then
 * replaces that Scene's Effect record under the sounding Effect (level, type
 * parameters, Morph, FX sequence). Scene 17's record is a copy of the home
 * record, so after the rebind every value effects_service() computes equals
 * last_applied[] and no runtime write happens.
 * Inputs: scene_index (Scene 17 while it exists). Outputs:
 * effects_state.scene_index; returns 1 when retargeted, 0 when refused.
 * Context: foreground only, the same context as effects_service() (mixer
 * render in the main loop), so the single-byte store is never seen half-done.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (fresh Scene 17 build).
 * Affiliates: effects_activateScene() (the full activation used for real
 * Scene changes, including the release from Scene 17), effects_service(),
 * preset_buildBackgroundScene() (copies the Effect record).
 */
uint8_t effects_rebindSourceScene(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    effect_type_id_t type;

    if (!record)
        return 0u;
    type = effects_registryEntry(record->type) ? record->type : EFFECT_TYPE_OFF;
    if (type != effects_state.runtime_type)
        return 0u;
    effects_state.scene_index = scene_index;
    return 1u;
}
```

The type test is a guard, not an expected path. Scene 17 was copied from the
active Scene, whose type is the running type. If it ever fails, the caller
falls back to a full activation (F1.3), which is the only correct way to
change the type.

### F1.2 `Core/DSP/Effects/EffectsManager.h:472` (after `effects_activateScene()`) — ADD declaration

```c
/*
 * Retarget the running Effect to an identical copy of its source Scene
 * (S082 P2 fix 1).
 * What: changes only the Scene whose Effect record the per-block service
 * reads; no runtime, sequence, automation or LFO state changes.
 * Inputs: scene_index. Outputs: 1 retargeted, 0 refused (no record or a
 * different type).
 * Accessors: preset_tickBackgroundBankLoad() phase 3.
 */
uint8_t effects_rebindSourceScene(uint8_t scene_index);
```

### F1.3 `Core/Bank/Scene/Preset/presetManager.c:3880-3886` — `preset_tickBackgroundBankLoad()` phase 3, fresh-build branch — MOD

Replace:

```c
        } else {
            seq_switchToBackgroundScene();
            (void)scene_selectActive(SCENE_BACKGROUND_INDEX);
            menu_setPlayedPattern(SCENE_BACKGROUND_INDEX);
            if (parameter_values[PAR_FOLLOW])
                menu_setShownPattern(SCENE_BACKGROUND_INDEX);
        }
```

with:

```c
        } else {
            seq_switchToBackgroundScene();
            (void)scene_selectActive(SCENE_BACKGROUND_INDEX);
            /*
             * The sounding Effect follows playback onto Scene 17
             * (S082 P2 fix 1).
             *
             * What: retargets EffectsManager's per-block source from the home
             * Scene to Scene 17 without touching the running Effect; a type
             * mismatch (never expected: 17 is a copy of the active Scene)
             * falls back to the full activation.
             * Why: the Bank load is about to overwrite the home Scene's Effect
             * record; the Effect must keep reading the identical copy, or it
             * changes level and parameters mid-load (S082_P1 D4).
             * Inputs: Scene 17 built and active. Outputs: effects_state source.
             * Affiliates: effects_rebindSourceScene(), effects_activateScene(),
             * preset_buildBackgroundScene().
             */
            if (!effects_rebindSourceScene(SCENE_BACKGROUND_INDEX))
                effects_activateScene(SCENE_BACKGROUND_INDEX);
            menu_setPlayedPattern(SCENE_BACKGROUND_INDEX);
            if (parameter_values[PAR_FOLLOW])
                menu_setShownPattern(SCENE_BACKGROUND_INDEX);
        }
```

**Notes:**
- `presetManager.c` already includes `EffectsManager.h` (`:51`).
- The rebind must follow `scene_selectActive(16)`. Effect edits on Scene 17 then
  write through to the runtime: EffectsManager's edit paths only write live
  when the edited Scene equals `effects_state.scene_index`
  (`EffectsManager.c:652`, `719`, `781`, `1773`, `1778`). Before this fix, an
  Effect edit made on Scene 17 changed the record but not the sound; F1 fixes
  that as a side effect.
- **Rebuild in place:** no call is needed. The index is already 16, and D19
  keeps Scene 17's Scene-level state.
- **Release from 17:** a whole-Scene change already re-activates through
  `preset_startDrumsetApply()` → `effects_activateScene()`
  (`presetManager.c:1985`, `2043`).

### F1.4 `Core/DSP/Effects/EffectsManager.c:134-142` (`effects_state_t` comment) — DOC

Append:

```c
 * scene_index is the Scene whose Effect record effects_service() reads every
 * render block. effects_activateScene() sets it for a real Scene change;
 * effects_rebindSourceScene() moves it to Scene 17 at the background-load
 * switch without disturbing the runtime (S082 P2 fix 1). It must always name
 * the active Scene while audio runs.
```

---

## Fix 2 — Whole-Scene AutoSave marking as one range (P2-2)

**Cause, recap:**
- `on_bank_load_complete()` (`presetManager.c:676-684`) calls
  `autosave_markSceneWithPatternDirty()` for every loaded child, all in one
  `filesystem_tick()` callback.
- That calls `autosave_markSceneWithoutPatternDirty()` (`Autosave.c:2272-2308`),
  which marks each implemented payload byte through
  `autosave_markPayloadOffsetDirty()` (`Autosave.c:410-434`). Each byte costs
  one IRQ save/restore in `autosave_maskByteOr()` (`:386-398`), plus one `D`
  trace record in DEV builds.
- That is about 1,300–1,500 calls per Scene, about 20,000+ for 16 Scenes, which
  is several ms of foreground time with audio rendering in the same loop.
- The same marker serves the Merge whole-Bank re-mark (`menu.c:16261`,
  `autosave_markResidentBankDirty()`) and Scene Load (`presetManager.c:628`).

**Why range marking is exact:**
1. The drain takes every dirty bit and keeps a patch only when
   `autosave_getLivePayloadByte()` returns a live value
   (`filesystem.c` drain phase 56: "a failed get proves the format cell has no
   current owner"). A dirty reserved or unowned byte is cleared and costs
   nothing else.
2. Within a Scene section, the getter
   (`Autosave.c:1130-1240`) returns values only for:
   - the Scene source;
   - the Scene parameters;
   - the Effect type, source and parameters;
   - the Kit source and parameters;
   - the Instrument cells.

   It returns nothing for the Scene name (bytes 0–7) or any reserved cell.
3. A whole-Scene replacement is exactly the case where every live byte of the
   Scene has changed.

So OR-ing the section from the Scene source to the section end, with the name
excluded as today, captures the same live set as the per-byte markers, and
possibly more: any gettable cell those markers did not name is correct to
capture after a full replacement.

**Geometry:**
- Scene base = `AUTOSAVE_BANK_SECTION_BYTES + scene × AUTOSAVE_SCENE_SECTION_BYTES`
  = 128 + 1,920·s, which is 8-aligned.
- The range [base + 8, base + 1,920) is therefore 239 whole mask bytes, with no
  partial edge bytes.
- The helper below still handles edges, so other callers can use it safely.

### F2.1 `Core/Bank/Scene/Autosave.c` after `autosave_markPayloadOffsetDirty()` (ends 434) — ADD `autosave_maskPayloadRangeDirty()`

```c
/*
 * Mark a contiguous payload range dirty in one short critical section
 * (S082 P2 fix 2).
 *
 * What: ORs every payload bit in [first, first + count) into the canonical
 * mask, whole mask bytes at a time (partial edge bytes masked), and adds each
 * newly set bit to autosave_dirty_count, all inside one IRQ save/restore. No
 * per-byte trace record is written; the caller emits one witness. Rejected
 * while tracking is disabled or when the range starts outside the payload;
 * an end past the payload is clamped.
 * Why: marking a whole replaced Scene byte-by-byte costs one IRQ round trip
 * and one DEV trace record per byte, about 20,000 of each for a 16-Scene Bank
 * load in one foreground call, which stalls audio rendering long enough to
 * underrun while Scene 17 plays. A whole Scene section is 239 mask bytes, so
 * one pass takes a few microseconds. The drain discards dirty bits that have
 * no live getter, so including reserved bytes is harmless.
 * Inputs: payload-relative first offset, byte count; tracking flag.
 * Outputs: autosave_dirty_mask and autosave_dirty_count; returns the number
 * of newly set bits (0 when rejected or already dirty).
 * Accessors: autosave_markSceneWithoutPatternDirty().
 * Affiliates: autosave_maskByteOr() (the single-byte form with the same
 * fresh-bit accounting), autosave_markPayloadOffsetDirty() (per-cell
 * producer), drain phase 56 / autosave_getLivePayloadByte().
 */
static uint16_t autosave_maskPayloadRangeDirty(uint16_t first, uint16_t count)
{
    uint16_t end;
    uint16_t offset;
    uint16_t fresh_total = 0u;
    uint32_t primask;

    if (!autosave_mutation_tracking_enabled || count == 0u ||
        first >= AUTOSAVE_PAYLOAD_BYTES)
        return 0u;
    end = (uint16_t)(first + count);
    if (end > AUTOSAVE_PAYLOAD_BYTES || end < first)
        end = AUTOSAVE_PAYLOAD_BYTES;
    primask = autosave_irqSave();
    offset = first;
    while (offset < end) {
        uint16_t mask_byte = (uint16_t)(offset >> 3u);
        uint8_t first_bit = (uint8_t)(offset & 7u);
        uint16_t span = (uint16_t)(8u - first_bit);
        uint8_t bits;
        uint8_t old;
        uint8_t fresh;

        if (span > (uint16_t)(end - offset))
            span = (uint16_t)(end - offset);
        bits = (uint8_t)(((1u << span) - 1u) << first_bit);
        old = autosave_dirty_mask[mask_byte];
        fresh = (uint8_t)(bits & (uint8_t)~old);
        autosave_dirty_mask[mask_byte] = (uint8_t)(old | bits);
        fresh_total = (uint16_t)(fresh_total + popcount8_lut[fresh]);
        offset = (uint16_t)(offset + span);
    }
    autosave_dirty_count = (uint16_t)(autosave_dirty_count + fresh_total);
    autosave_irqRestore(primask);
    return fresh_total;
}
```

**Notes:**
- `autosave_dirty_count` is updated once with the exact fresh total, still
  inside the same PRIMASK section, so the "exact population" invariant of
  `autosave_maskByteOr()` (`:376-385`) holds. The count cannot overflow: the
  mask has 30,848 bits.
- Interrupts stay off for about 239 iterations of a few instructions each, on
  the order of 5–10 µs, far below one audio slot.

### F2.2 `Core/Bank/Scene/Autosave.c:2272-2308` — `autosave_markSceneWithoutPatternDirty()` — MOD

Replace the body (the four per-scope markers) with:

```c
void autosave_markSceneWithoutPatternDirty(uint8_t scene_index)
{
    uint16_t scene_base;
    /* Pack the outer terminal LOAD_MARK in locals only; no persistent state is added. */
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    /*
     * Mark the whole live payload of one replaced Scene in one range
     * (S082 P2 fix 2).
     *
     * What: one contiguous range from the Scene source field to the end of
     * the Scene section (Scene source and settings, Effect type/source/
     * parameters, Kit source/parameters and all six Instruments) becomes
     * dirty; the eight Scene-name bytes and Pattern are excluded, exactly as
     * before. One LOAD_MARK witness is recorded; the former per-byte DIRTY
     * records and the nested Kit/Instrument witnesses are no longer emitted
     * for whole-Scene marks.
     * Why: the per-scope, per-byte markers cost one IRQ round trip and one DEV
     * trace record per byte; a 16-Scene Bank load completion stalled the
     * foreground long enough to underrun audio while Scene 17 plays. The drain
     * keeps a patch only for bytes with a live getter, so the range captures
     * the same live set; reserved bytes are taken and dropped.
     * Inputs: resident Scene index (present, tracking enabled).
     * Outputs: canonical mask range; one LOAD_MARK trace record.
     * Accessors: autosave_markSceneWithPatternDirty() (Bank and Scene Load
     * completion, copy/paste of a whole Scene), autosave_markResidentBankDirty(),
     * filesystem.c baseline/ensure paths (8756, 14145, 25960, 27646-27654).
     * Affiliates: autosave_maskPayloadRangeDirty(), autosave_scenePayloadBase(),
     * autosave_getLivePayloadByte(), drain phase 56.
     */
    if (autosave_scenePayloadBase(scene_index, &scene_base)) {
        (void)autosave_maskPayloadRangeDirty(
            (uint16_t)(scene_base + AUTOSAVE_SCENE_SOURCE_OFFSET),
            (uint16_t)(AUTOSAVE_SCENE_SECTION_BYTES -
                       AUTOSAVE_SCENE_SOURCE_OFFSET));
    }

    /* Emit the durable whole-Scene witness after the range is published. */
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED;
    trace_value = ((uint32_t)AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE <<
                   AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT) |
                  ((uint32_t)scene_index << AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_LOAD_MARK, trace_flags,
                         trace_value);
}
```

**Check while applying:**
1. `autosave_markSourceDirty()` maps the Scene, Effect, Kit and Instrument
   HCNAMES rows to offsets inside this Scene's section. The former body marked
   them explicitly; the range must contain each of them. Confirm the four
   source offsets fall in [base + 8, base + 1,920).
2. `autosave_scenePayloadBase()` (`:437-447`) applies the same present-Scene
   gate the per-scope markers used.
3. **`autosave_markKitDirty()` keeps its per-byte form.** It also serves normal
   Kit Load completion, which marks only the Kit scope. That is one Scene,
   about 900 marks; note it as a later candidate for the same treatment if
   Kit Load while playing shows a glitch.

### F2.3 `Core/Bank/Scene/Autosave.h:600-627` (marker family comment) — DOC

Append to the block above `autosave_markSceneWithoutPatternDirty()`:

```c
 * S082 P2 fix 2: the whole-Scene markers publish one contiguous mask range
 * (Scene source through the end of the Scene section) in one short critical
 * section with one LOAD_MARK witness, instead of per-byte marks; the drain
 * keeps only bytes that have a live getter, so the captured set is the
 * Scene's complete live image.
```

### F2.4 Tools — no change

- `tools/decode_devlogs.py` only names stages, and
  `tools/verify_bank_autosave.py` does not read mark records.
- Expect far fewer `D` records after a Bank load: one `L` per Scene instead
  of about 1,400 `D` records.

---

## Fix 3 — Idle-time filesystem servicing during a background Bank load (P2-3)

**Cause, recap:**
- The main loop calls `filesystem_tick()` once per pass (`main.c:1449`),
  between 16 `audio_check_and_render()` calls (`main.c:1386-1451`).
- While audio plays, most of each pass is rendering, so a Bank child of
  several hundred bounded steps takes about a second.
- Stopped loads are fast only because S058 suspends the codec and enables the
  fast drain (`FS_FAST_DRAIN_POLL_PASSES`, `filesystem.c:2139`, `26818-26823`).

**Fix:**
- While a background Bank load runs and the facade is BUSY, use the idle time
  in which both render slots are full to run more `filesystem_tick()` calls.
- Bound it by a µs budget and stop the moment a render slot frees.
- Audio is never delayed by more than one filesystem step, the same bound as
  today's loop.

**Stall-observer interaction (must be handled):**
- The DEV stall observers count *polls* in one phase, not time:
  - `filesystem_pollPhaseStall()`, `filesystem.c:18653-18665`: 20,000 polls
    for the Scene and Bank loaders (`:12614`, `:14184`);
  - `filesystem_autosaveDrainStalled()`, `:8431`: 30,000.
- `DEV_STALL_DETECTION` is 1 (`config.h:111`). Extra back-to-back polls would
  shrink the observers' wall-clock window several times over and could abort
  a healthy load on a slow card.
- The extra ticks are therefore **uncounted**. The observers keep counting
  only the one regular tick per main-loop pass, so their timing is unchanged.

### F3.1 `config.h` after `AUDIO_RENDER_IN_DMA_ISR` (802) — ADD

```c
/*
 * Idle-time filesystem budget during a background Bank load (S082 P2 fix 3).
 *
 * What: the longest stretch, in microseconds, that the main loop may spend
 * running extra filesystem_tick() calls in one pass while both audio render
 * slots are full and a background Bank load owns the busy facade.
 * Why: with audio playing, one filesystem step per main-loop pass makes a
 * 16-Scene Bank load take tens of seconds; the full-queue window (up to one
 * 2.18 ms slot) is otherwise spent spinning. The loop also stops the moment
 * a render slot frees, so audio is delayed by at most one filesystem step.
 * Tune only from hardware evidence (per-child load time, underrun count).
 * Accessors: main_serviceBackgroundLoadIdle() (main.c).
 */
#define BG_LOAD_IDLE_FS_BUDGET_US  200u
```

### F3.2 `Core/Hardware/SD/filesystem.c` beside the stall-observer prototypes (1608-1614) — ADD flag

```c
#if DEV_STALL_DETECTION
/*
 * Poll-count suppression for idle-time ticks (S082 P2 fix 3).
 *
 * What: nonzero only inside filesystem_tickUncounted(); while set, the stall
 * observers neither count nor reset, so an extra tick is invisible to them.
 * Why: the observers measure "N polls in one phase", calibrated to one
 * regular tick per main-loop pass. Extra idle-time ticks would reach N
 * several times sooner in wall time and could abort a healthy operation
 * waiting on a slow card.
 * SRAM: 1 B, DEV_STALL_DETECTION builds only (same class as the observer
 * statics). Lifetime: set and cleared within one call.
 * Accessors: filesystem_tickUncounted(), filesystem_pollPhaseStall(),
 * filesystem_autosaveDrainStalled().
 */
static uint8_t fs_stall_poll_uncounted = 0u;
#endif
```

### F3.3 `Core/Hardware/SD/filesystem.c:18653-18665` — `filesystem_pollPhaseStall()` — MOD

Make this the first statement:

```c
    /* Idle-time ticks are invisible to the observer (S082 P2 fix 3). */
    if (fs_stall_poll_uncounted)
        return 0u;
```

### F3.4 `Core/Hardware/SD/filesystem.c:8431` — `filesystem_autosaveDrainStalled()` — MOD

Make the same check the first statement:

```c
    /* Idle-time ticks are invisible to the observer (S082 P2 fix 3). */
    if (fs_stall_poll_uncounted)
        return 0u;
```

Both functions are compiled only under `DEV_STALL_DETECTION`, the same guard
as the flag.

### F3.5 `Core/Hardware/SD/filesystem.c` after `filesystem_tick()` (starts 26789) — ADD `filesystem_tickUncounted()`

```c
/*
 * Run one extra filesystem step that the stall observers do not count
 * (S082 P2 fix 3).
 *
 * What: exactly filesystem_tick(), with the DEV poll-count stall observers
 * suppressed for its duration. In builds without DEV_STALL_DETECTION it is a
 * plain filesystem_tick().
 * Why: the main loop may call this repeatedly in idle time during a
 * background Bank load; the operation advances faster, while the observers'
 * one-poll-per-pass calibration, and so their wall-clock window, stays as it
 * was. A genuine stall is still caught by the regular per-pass tick.
 * Inputs: none. Outputs: one operation step (callbacks may run, as in
 * filesystem_tick()).
 * Accessors: main_serviceBackgroundLoadIdle().
 * Affiliates: filesystem_tick(), filesystem_pollPhaseStall(),
 * filesystem_autosaveDrainStalled(), fs_stall_poll_uncounted.
 */
void filesystem_tickUncounted(void)
{
#if DEV_STALL_DETECTION
    fs_stall_poll_uncounted = 1u;
    filesystem_tick();
    fs_stall_poll_uncounted = 0u;
#else
    filesystem_tick();
#endif
}
```

### F3.6 `Core/Hardware/SD/filesystem.h:576` (after `filesystem_tick()`) — ADD declaration

```c
/*
 * One extra filesystem step that the DEV stall observers do not count
 * (S082 P2 fix 3).
 * Inputs: none. Outputs: one filesystem_tick() step.
 * Accessors: main.c idle-time servicing during a background Bank load.
 */
void        filesystem_tickUncounted(void);
```

### F3.7 `main.c` after `audio_check_and_render()` (ends 223) — ADD `main_serviceBackgroundLoadIdle()`

```c
/*
 * Give a background Bank load the idle time between audio renders
 * (S082 P2 fix 3).
 *
 * What: while a background Bank load runs and the filesystem facade is BUSY,
 * runs extra (stall-uncounted) filesystem steps as long as both render slots
 * are full, for at most BG_LOAD_IDLE_FS_BUDGET_US per call. It returns at once
 * when a render slot frees, the facade leaves BUSY, the load ends, or the
 * budget is spent.
 * Why: with audio playing (Scene 17), one filesystem step per main-loop pass
 * makes each Bank child take about a second; the full-queue window is
 * otherwise spent spinning through the loop. Rendering keeps priority: the
 * queue is tested before every step, so a render is delayed by at most one
 * bounded filesystem step, the same bound the main loop already relies on.
 * Limited to background Bank loads so no other workflow's timing changes. It
 * is foreground scheduling only; no DSP work is skipped (DSP CPU policy).
 * Inputs: preset_backgroundBankLoadRunning(), filesystem_status(),
 * audioCodec_isSuspended(), audioCodec_queueFreeSlots(), TIM2 microseconds.
 * Outputs: filesystem progress; completion callbacks may run, as in the
 * regular tick.
 * Accessors: the main loop, once per pass after the regular filesystem_tick().
 * Affiliates: filesystem_tickUncounted(), audio_check_and_render(),
 * BG_LOAD_IDLE_FS_BUDGET_US, preset_tickBackgroundBankLoad().
 */
static void main_serviceBackgroundLoadIdle(void)
{
    uint32_t start;

    if (!preset_backgroundBankLoadRunning() || audioCodec_isSuspended())
        return;
    start = timebase_tim2Now();
    while (filesystem_status() == FS_STATUS_BUSY &&
           audioCodec_queueFreeSlots() == 0u &&
           timebase_tim2Delta(timebase_tim2Now(), start) <
               BG_LOAD_IDLE_FS_BUDGET_US) {
        filesystem_tickUncounted();
    }
}
```

**Notes:**
- A suspended codec (a stopped load) returns at once. That path already uses
  the S058 fast drain, and the queue test would otherwise never see a free
  slot.
- `main.c` already includes `presetManager.h`, `filesystem.h`, `timebase.h`
  and `AudioCodecManager.h` (`main.c:39`, `49`, `71`, `78`).

### F3.8 `main.c:1448-1450` — main loop — MOD

Replace:

```c
        // tick the SD read async
        filesystem_tick();
        audio_check_and_render();
    }
```

with:

```c
        // tick the SD read async
        filesystem_tick();
        audio_check_and_render();
        /*
         * Background Bank load: use the full-queue idle window for extra
         * filesystem steps (S082 P2 fix 3); returns at once otherwise. The
         * loop's next statement is a render check, so a slot that freed
         * during the window is served first.
         */
        main_serviceBackgroundLoadIdle();
    }
```

### F3.9 What stays bounded

- Each `filesystem_tick()` step is already bounded by design: one line, one
  read or write window, one open/close request, or one budgeted
  classification slice.
- The F4 stall observers, the A1 abort allow-list and the bounded waits (A8)
  are unchanged.
- MIDI and front-panel services see at most `BG_LOAD_IDLE_FS_BUDGET_US` of
  extra latency per main-loop pass, and only during a background Bank load.

---

## 9. Verification

| Fix | Test |
|-----|------|
| F1 | Running transport, several Effect types (including one different from the new Bank's home Effect), FX level off default, Effect Morph off-centre, an FX sequence running: Bank load replacing the playing Scene. There must be no audible change at the switch, when the home child commits, or at completion. On Scene 17, an Effect edit changes the sound, which it did not before. Release by SEQ press: the next Scene's Effect activates normally. |
| F2 | Running transport, Replace load of a full 16-Scene Bank: no glitch at completion. `audioCodec_underrunCount` is unchanged across the load (DEV read-out, or the commented LCD diagnostic in `main.c`). After release, AutoSave writes a record; reboot without loss; `tools/verify_bank_autosave.py` passes. Trace: one `L` per loaded Scene, no `D` flood. Merge load: the whole-Bank re-mark is also glitch-free. Stopped Bank Load and root Scene Load: AutoSave content unchanged from before (compare `.hcprms*` after a drain). |
| F3 | Per-child load time while playing is measurably shorter than before F3 (stopwatch or the `NN.` progress). No underruns during the load. Front panel and MIDI stay responsive. No false `X` stall records on the slow reference card. A stopped load behaves as before (fast drain path). |
| All | `S082_P1` §8 rows; `S082_P1_IMPLEMENTATION.md` §10 and §13.3. |

---

## 10. Implementation log

**2026-10-19 implementation.** All three fixes are in the working tree and
built. Nothing has been tested on hardware. The working tree was not
committed first (commits belong to the user), so the plan's line numbers were
re-located by anchor function before every edit.

| Step | Result | Build | Deviations |
|------|--------|-------|------------|
| F1 | Done (F1.1–F1.4) | `text=595,184`, `bss=430,920` | None. `effects_rebindSourceScene()` sets only `effects_state.scene_index`; the phase 3 call follows `scene_selectActive(16)` and falls back to `effects_activateScene()` on refusal. |
| F2 | Done (F2.1–F2.3) | `text=595,152`, `bss=430,920` | None. Range helper ORs whole mask bytes inside one PRIMASK section and updates `autosave_dirty_count` once with the exact fresh total. `autosave_markKitDirty()` keeps its per-byte form (Kit Load path). |
| F3 | Done (F3.1–F3.8) | `text=595,968`, `bss=430,920` | None. `fs_stall_poll_uncounted` is 1 B under `DEV_STALL_DETECTION`; the 1 B did not change the reported BSS because it filled existing alignment padding. |

Final build: `text=595,968`, `data=420`, `bss=430,920`; flash payload
596,388 B of 753,664 B (**headroom 157,276 B**). DTCM statics 4,472 B;
FXBUF 126,592 B with 3,712 B margin. `build/LXRV2_lxr02.img` written.

No new compiler warnings beyond the existing unused-function set in
`filesystem.c`, the newlib syscall stubs, and the LTO serial note.

### 10.1 F1 — Effect source rebind

- `EffectsManager.c`: added `effects_rebindSourceScene()` after
  `effects_activateScene()`; extended the `effects_state_t` layout comment.
- `EffectsManager.h`: added the shortened declaration after
  `effects_activateScene()`.
- `presetManager.c`: fresh-build branch of `preset_tickBackgroundBankLoad()`
  phase 3 now rebinds after `scene_selectActive(SCENE_BACKGROUND_INDEX)`.
  Rebuild-in-place deliberately makes no call (index is already 16).
- Side effect noted while checking the edit paths: Effect edits made on Scene
  17 now write through to the live runtime, because
  `effects_state.scene_index` names 17.

### 10.2 F2 — whole-Scene AutoSave range

- `Autosave.c`: added `autosave_maskPayloadRangeDirty()` after
  `autosave_markPayloadOffsetDirty()`. It handles partial edge mask bytes,
  clamps an end past the payload, rejects when tracking is off, and keeps the
  exact-population invariant by adding only fresh bits to
  `autosave_dirty_count` inside the same PRIMASK section.
- `Autosave.c`: `autosave_markSceneWithoutPatternDirty()` now marks
  `[scene_base + 8, scene_base + 1,920)` (239 mask bytes) and emits one
  `LOAD_MARK` witness. The per-scope Scene/Effect/Kit markers and their
  nested `D`/`I` witnesses are no longer emitted for a whole Scene.
- `Autosave.h`: marker-family comment extended.
- Checked before applying: every source offset the former body marked falls in
  the range (Scene 8..9, Effect type 128..130, Effect source 558..559, Kit
  source 648..649, Instrument sources at 779/971/1163/1355/1547/1739); the
  Scene name 0..7 and the Pattern row stay excluded. `autosave_getLivePayloadByte()`
  returns 0 for every newly covered reserved cell (Effect name 131..138, Kit
  name 640..647, reserved parameter/Instrument cells), so drain phase 56 takes
  and drops those bits without a patch. The captured live set is the same as
  before.
- Cost on the completion path is now 16 short IRQ sections (about 239
  iterations each) instead of ~20,000 IRQ round trips and `D` records.
- Static check of the remaining completion work the follow-up named:
  `bank_mergeVoiceEditMasksAfterLoad()` and `bank_revalidateVoiceEditMasks()`
  are bounded 16×16 loops, and the HCNAMES/index rewrites are separate
  state-machine ticks, so the whole-Scene marking was the multi-millisecond
  foreground component. No code change was needed there.

### 10.3 F3 — idle-time filesystem servicing

- `config.h`: added `BG_LOAD_IDLE_FS_BUDGET_US` = 200 after
  `AUDIO_RENDER_IN_DMA_ISR`.
- `filesystem.c`: added `fs_stall_poll_uncounted` beside the DEV observer
  prototypes; `filesystem_pollPhaseStall()` and
  `filesystem_autosaveDrainStalled()` return 0 immediately while it is set
  (neither counts nor resets); added `filesystem_tickUncounted()` after
  `filesystem_tick()`, with the non-DEV build reducing to a plain tick.
- `filesystem.h`: added the public declaration.
- `main.c`: added `main_serviceBackgroundLoadIdle()` after
  `audio_check_and_render()` and called it at the end of the main loop, after
  the regular `filesystem_tick()` + render pair. It returns at once without a
  background Bank load or with a suspended codec, and otherwise runs
  uncounted ticks only while the facade is BUSY, both render slots are full,
  and the 200 µs TIM2 budget has not expired.
- Scheduling note: rendering is never delayed by more than one bounded
  filesystem step. The queue is re-tested before every extra tick; the budget
  is far below one 2.18 ms slot.

### 10.4 Verification status

| Item | Status |
|------|--------|
| `make all && make img` | PASS (final ELF above). |
| New warnings | None beyond the existing set. |
| Link budget | PASS: flash headroom 157,276 B, DTCM/FXBUF unchanged. |
| F1 listening test (switch / child commit / completion, Effect edits on 17) | Open, hardware. |
| F2 Replace/Merge load glitch and underrun count, AutoSave round trip | Open, hardware. |
| F3 per-child load time, responsiveness, no false `X` stalls | Open, hardware. |

---

## 11. Post-implementation assessment (2026-10-19)

*Written for whoever runs the hardware tests and closes S082 P2.*

**Method:**
- Read every F1–F3 change in the working tree against this schedule, and
  re-checked the assumptions each fix depends on in the surrounding code.
- Rebuilt the touched files (`main.c`, `Autosave.c`, `EffectsManager.c`,
  `presetManager.c`, `filesystem.c`).
- No code was changed and nothing was tested on hardware.

### 11.1 Verdict

**All three fixes are implemented exactly as scheduled. No defects were found.**

| Check | Result |
|-------|--------|
| Build | The rebuild reproduces the logged ELF: `text=595,968`, `data=420`, `bss=430,920`; flash headroom 157,276 B; DTCM 4,472 B; FXBUF margin 3,712 B. |
| Warnings | Only the existing set: seven unused functions in `filesystem.c`, the newlib syscall stubs, and the LTO serial note. |
| RAM | `fs_stall_poll_uncounted` is 1 B, present only under `DEV_STALL_DETECTION`, and fits existing BSS padding. No production growth, as `STORAGE_SRAM_MANIFEST.md` records. |

### 11.2 F1 — Effect source rebind: correct

- `effects_rebindSourceScene()` (`EffectsManager.c:1777`):
  - writes only `effects_state.scene_index`;
  - refuses a missing record or a type that differs from the running type.
- The phase 3 call (`presetManager.c:3898`) follows `scene_selectActive(16)`
  and falls back to `effects_activateScene()`. The rebuild-in-place branch
  makes no call, which is correct: the index is already 16.
- Scene 17's record is the whole-`scene_t` copy made in
  `preset_buildBackgroundScene()`. Every value `effects_service()` computes
  therefore equals `last_applied[]`, and no runtime write occurs at the switch.
- **Side effect (intended):** Effect edits on Scene 17 now reach the live
  Effect. EffectsManager's live-write guards compare against
  `effects_state.scene_index`.
- **Release from 17** relies on the existing whole-Scene change path reaching
  `effects_activateScene()`. That path is unchanged here; it is in the
  hardware rows below, including a queued/chain change.

### 11.3 F2 — whole-Scene range marking: correct

- **The helper** (`autosave_maskPayloadRangeDirty()`):
  - masks partial edge bytes;
  - clamps the end;
  - rejects when tracking is off;
  - adds only fresh bits to `autosave_dirty_count`, inside one PRIMASK
    section. The exact-population invariant holds.
- **The marker** (`autosave_markSceneWithoutPatternDirty()`) marks
  `[base + 8, base + 1,920)` and emits one `LOAD_MARK`.
- **Independently confirmed against `Autosave.h` and the getter:**
  - every source field the former body marked lies inside the range: Scene
    8–9, Effect source 558–559, Kit source 648–649, Instrument sources at
    640 + 128 + 192·slot + 11;
  - `autosave_getLivePayloadByte()` returns nothing for the newly covered name
    and reserved cells, or for Morph cells of non-morphable descriptors
    (`Autosave.c:1310-1365`).

  Drain phase 56 therefore captures exactly the live set the per-byte markers
  produced. The extra bits are taken and dropped, and cost only
  classification time.
- **Cost:** 16 short critical sections instead of about 20,000 IRQ round trips
  and `D` records. Every whole-Scene caller benefits:
  - Bank and Scene Load completion;
  - whole-Scene paste;
  - the Merge re-mark;
  - the filesystem ensure/baseline paths.

### 11.4 F3 — idle-time filesystem servicing: correct

- **Code:**
  - `main_serviceBackgroundLoadIdle()` (`main.c:240-262`) and its single call
    at the end of the main loop (`main.c:1496`);
  - `filesystem_tickUncounted()` (`filesystem.c:27155`);
  - the observer guards in `filesystem_pollPhaseStall()` (`:18678`) and
    `filesystem_autosaveDrainStalled()` (`:8451`).

  All match F3.2–F3.8, and the flag and both guards sit under the same
  `DEV_STALL_DETECTION` guard.
- **Assumptions confirmed during this review:**
  1. **SD driver timeouts are time-based.** `sdcard_waitTimedOut()` uses
     `time_sysTick` (`sdcard_lxr02.c:97-151`), and its only counter is a
     bounded loop inside one call (`:253`). Extra `afatfs_poll()` calls cannot
     shorten a driver timeout.
  2. **One step is tiny.** Each `sdcard_poll()` moves one
     `SDCARD_BURST_SIZE` = 16 B burst (`sdcard_lxr02.c:96`, `:360-405`). A
     filesystem step is microseconds long, so the worst-case render delay after
     a slot frees (one step) is negligible next to the 2.18 ms slot.
  3. **This also quantifies P2-3.** Without the fast drain, a playing load got
     one 16 B burst per main-loop pass: 32 passes per 512 B sector. That is why
     each child took about a second.
- **Residual effect, not a defect:** passes that spend up to 200 µs in the
  idle window make the main loop run fewer passes per second during a
  background load. The poll-counted stall observers therefore take slightly
  longer in wall time to declare a real stall. They never fire falsely.
- **Optional tuning, not a defect:** the idle loop does one `afatfs_poll()` per
  `filesystem_tick()`. Running the fast-drain poll count
  (`FS_FAST_DRAIN_POLL_PASSES`) inside the idle window would cut state-machine
  overhead per burst. Consider it only if the hardware timing shows the load
  is still slow.

### 11.5 Documentation gaps (Low)

This schedule had no docs step (planning omission). The implementation updated
`MEMORY.md` (line 213) and `STORAGE_SRAM_MANIFEST.md` (lines 9–11, 594).
Still to do:
- `knowledge_files/specification_reference/AUTOSAVE.md`: the marker table (row
  at line 1048) and the whole-Scene notes (lines 569–572) should say that
  whole-Scene marks are one range with one `LOAD_MARK`, and no longer produce
  per-byte `D` or nested `I` records.
- `S082_P1_LOAD_SAVE.md` §5 (Scene index audit): add the "cached Scene index"
  rule from the P2 follow-up (P2-4). EffectsManager was the case it missed.
- `S082_P2_LOAD_SAVE_FOLLOWUP.md`: mark P2-1 to P2-3 as implemented. P2-4
  (switch self-check) and P2-5 (runtime Bank Load trace records, decoder op
  table for `0x2b`) remain open; they were not part of this schedule.

### 11.6 Hardware tests to run

1. The §9 rows for F1, F2 and F3.
2. **F1:**
   - release from Scene 17 by SEQ press and by a queued/chain change: the next
     Scene's Effect activates;
   - an Effect edit on 17 is audible.
3. **F2:**
   - `audioCodec_underrunCount` is unchanged across a running 16-Scene Replace
     load and a Merge load;
   - after release, an AutoSave round trip and reboot pass, and
     `tools/verify_bank_autosave.py` passes.
4. **F3:**
   - per-child time is noticeably shorter while playing, on the slow reference
     card;
   - no `X` stall records;
   - the encoder, pots and MIDI stay responsive during the load.
5. **Regression:** a stopped Bank Load (fast-drain path, codec suspended) behaves
   as before; Scene Load and Kit Load AutoSave content is unchanged.

### 11.7 Status

- F1–F3 are implemented, build clean, and match the schedule.
- No code defects were found.
- The documentation follow-ups in §11.5 are open.
- Hardware verification (§11.6) is open.
