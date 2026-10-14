# S081 P2 — Linear Automation Ramps: Implementation Schedule

Session 081, part 2 (2026-10-13), branch `dev-ph6-cleanup`.

This schedule implements `S081_P2_LINEAR_AUTOMATION.md` (revision 2,
decisions D1–D10, follow-ups F1–F3 answered).

**Line numbers** are the working tree on 2026-10-13: P1/P1B/P1C uncommitted
on top of `d21e245`, including the P1C review fixes and hardware fix 1. Each
entry gives:

- `file:line`;
- the action (**ADD** / **REMOVE** / **MODIFY**);
- a code sketch where the shape matters;
- the comment block to place beside the change as documentation-in-place.

Where a function is declared in a header, the first paragraph of its comment
block also goes above the declaration. Where a step replaces an existing
comment block, the new block replaces the old one; it is not added beside it.

**Authority:**

- `S081_P2_LINEAR_AUTOMATION.md` revision 2;
- `MEMORY.md` binding rules: RAM approval, DSP CPU policy, automation
  priority, the parameter-mirror rule, the full menu dtype space;
- `PATTERN_DYNAMIC_STACK.md` §6;
- `S081_P1_LIVE_RECORD.md` §5.8 and R12.

**User answers to the plan's follow-ups:**

- **F1:** keep PAT4 version 2. Byte 5 is reserved and zero on every card
  file.
- **F2:** leave the legacy Bank text files alone.
- **F3:** RAM approved, 565 B plus 4 B DEV-only. This schedule uses
  **558 B plus 2 B DEV-only** (§Phase E).

**Build gate after every phase:** `make all` clean, then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

---

## 0. Design summary and decision map

### Data

- **Pattern setting:** `pat_scene_region_t.track_auto_smooth`, one bit per
  track, stored in PAT4 track-header byte 5.
- **Ramp table:** `seq_ramps[64]`, 8 B each.
  - `id` 0 means free. Otherwise `id = 1 + (track << 9 | target)`.
  - The rest of the slot is `from`, `to`, `startQ4` and `endQ4`. Times are in
    1/16 master ticks, low 16 bits, compared wrap-safe.
- **Per-track context:** `seq_rampCtx[7]`, 4 B each, written by TIM3.
  - `a`: bits 0..6 hold the A step; bit 7 is the "stepped" flag, set when a
    step's automation applies.
  - `b`: B step + 1; 0 means no ramp target.
  - `endQ4`: B's predicted apply time.
- **Block divider:** `seq_rampBlockDiv`, 1 B. The ramp service writes on
  every third 0.73 ms DSP block, about every 2.2 ms (D6).

### Flow

1. **TIM3, at every step of an `on` track (`seq_rampStepEvent()`):**
   - **Automation applied now:** write a context with the stepped flag set
     and a prediction of B.
   - **Automation deferred:** write the prediction without the flag;
     `seq_fireDeferred()` sets the flag when the step's automation queues.
   - **Nothing applied** (condition failed, muted, erased): write a "stepped,
     no B" context.
2. **Position breaks** (`seq_rampBreakTrack()` / `seq_rampBreakAll()`) write
   "stepped, no B" for the affected tracks.
3. **Foreground consumes the stepped flag** (`seq_rampConsumeSteps()` /
   `seq_rampConsumeTrack()`) and ends that track's ramps. This runs:
   - at the top of the trigger restore (before B's restore);
   - at the top of the drain (before B's entries);
   - in the ramp service.
4. **The drain pairs each written Instrument entry of A**
   (`seq_rampStartIfPaired()`) with the same target on B
   (`pat_findStepAutomation()`) and takes a slot.
5. **The ramp service** (`seq_rampService()`, after the drain in `main.c`)
   writes the interpolated values through the drain's Instrument write path
   (`seq_writeInstrumentAutomation()`).
6. **The trigger restore skips ramped parameters** while their ramp runs (D3).

### Decision map

| Decision | Steps |
|---|---|
| D1 predictable B condition | SQ-4 (`seq_rampConditionPredict`), SQ-2 |
| D2 Instrument + dtype filter | SQ-6 (`seq_rampDtypeOk`), SQ-7 |
| D3 no restore of ramped parameters | SQ-9 |
| D4 `asm` / `Automtn` / `off`/`on` | MN-1..MN-6 |
| D5 Pattern setting + card script | PD-1..PD-6, FS-1..FS-4, CC-1, CC-2, TL-1 |
| D6 update rate | SQ-8 (`SEQ_RAMP_BLOCK_DIV`), MA-1 |
| D7 rev/onc/1fr ramp, rnd jumps | SQ-3 (`seq_rampNextStep`) |
| D8 looper: no ramp across the wrap; unlooped prediction | SQ-3, SQ-11 |
| D9 7-bit resolution | SQ-8 |
| D10 64-ramp cap | SQ-1, SQ-7 |

---

## Phase A — Pattern setting storage

### PD-1 Region field — ADD after `PatternData.h:125` (`uint8_t track_loop_exclude;`)

```c
    uint8_t track_auto_smooth;
```

```
    /*
     * Per-track automation ramp mode (S081 P2, "asm" / Automtn).
     *
     * What: bit N set = track N is "on": an Instrument parameter automated on
     * two consecutively played steps ramps linearly from the first step's
     * value to the second's; 0 = "off" (the default), every value is applied
     * as a jump. Why: per-track smooth automation; a Pattern setting, not
     * automatable or morphable. A zero-filled region and every existing PAT4
     * file read as "off". Inputs: STEP track page edits, Scene/Pattern loads,
     * copy/clear. Outputs: seq_rampStepEvent() (TIM3), the ramp service.
     * Storage: byte 5 of each track's 16-byte PAT4 track header (0/1; the
     * byte was reserved and zero, so the file version stays 2). RAM: 1 byte
     * per region (17 B across pat_regions[16] and pat_background_region).
     * Affiliates: pat_setTrackAutoSmooth()/pat_trackAutoSmooth(),
     * filesystem.c PAT4 writer/readers.
     */
```

### PD-2 Region size asserts — MODIFY `PatternData.h:130-133` and `PatternData.c:44-47`

In both static asserts, change `512u + 31u` to `512u + 32u`. Change the
message to:

```
"pat_scene_region_t size must match the Pattern budget (S081 P2: +1 B auto smooth)"
```

### PD-3 Init — MODIFY `PatternData.c:1020-1022` (`pat_initScene`)

After `region->track_loop_exclude = 0u;` ADD:

```c
    /* S081 P2: every track starts with automation ramps off. */
    region->track_auto_smooth = 0u;
```

### PD-4 Menu mirror refresh — MODIFY `pat_applyTrackSettingsToMenu()` at `PatternData.c:1314-1327`

After the `PAR_TRACK_LOOPABLE` assignment (`:1325-1326`) ADD:

```c
    /* S081 P2: asm mirror (1 = automation ramps on). */
    parameter_values[PAR_TRACK_AUTO_SMOOTH] =
        (uint8_t)((region->track_auto_smooth >> track) & 1u);
```

Extend the function comment (`:1308-1313`) to read: "length, scale,
shuffle, play mode, loopable and asm cells mirror the resident region".

### PD-5 Setter/getter — ADD after `pat_trackLoopable()` (`PatternData.c:1416`); declare after `PatternData.h:504`

```c
void pat_setTrackAutoSmooth(uint8_t scene_index, uint8_t track, uint8_t on)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);
    uint8_t bit;
    uint8_t next;

    if (!region || !pat_trackValid(track))
        return;
    bit = (uint8_t)(1u << track);
    next = on ? (uint8_t)(region->track_auto_smooth | bit)
              : (uint8_t)(region->track_auto_smooth & (uint8_t)~bit);
    if (next == region->track_auto_smooth)
        return;
    region->track_auto_smooth = next;
    pat_markSceneDirty(scene_index);
}

uint8_t pat_trackAutoSmooth(uint8_t scene_index, uint8_t track)
{
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);

    if (!region || !pat_trackValid(track))
        return 0u;
    return (uint8_t)((region->track_auto_smooth >> track) & 1u);
}
```

```
/*
 * Per-track automation ramp setter/getter (S081 P2, asm).
 *
 * What: pat_setTrackAutoSmooth() sets (on = 1) or clears the track's bit in
 * the resident region and marks the Scene's Pattern dirty only on change;
 * pat_trackAutoSmooth() reports the bit (0 for invalid coordinates). Why:
 * the flag is Pattern data persisted in the PAT4 track header and travels
 * with Pattern copy/clear, exactly like the loopable flag. Inputs: Scene,
 * track, flag. Outputs: region byte, Pattern AutoSave dirty. Accessors:
 * menu_cellCommitValue() (asm cell), copyClearService.c. The sequencer reads
 * the region directly (TIM3). Affiliates:
 * pat_scene_region_t.track_auto_smooth, seq_rampStepEvent().
 */
```

Header (`PatternData.h`, after `:504`):

```c
/*
 * Per-track automation ramp mode (S081 P2): set/clear the asm bit of one
 * track (marks the Pattern dirty on change); read it (0 when invalid).
 */
void pat_setTrackAutoSmooth(uint8_t scene_index, uint8_t track, uint8_t on);
uint8_t pat_trackAutoSmooth(uint8_t scene_index, uint8_t track);
```

### PD-6 Region copy — MODIFY `PatternData.c:2204-2205`

After `dst->track_loop_exclude = src->track_loop_exclude;` ADD:

```c
    /* S081 P2: the automation ramp mode travels with a Scene copy. */
    dst->track_auto_smooth = src->track_auto_smooth;
```

### PD-7 Single-target lookup — ADD after `pat_readStepAutomations()` (`PatternData.c:1458-1474`); declare after `PatternData.h:524`

```c
uint8_t pat_findStepAutomation(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint16_t target,
                               uint8_t *value_out)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    const uint8_t *p;
    uint16_t offset;
    uint8_t flags;
    uint8_t value_count = 0u;
    uint8_t auto_count;
    uint8_t idx;
    uint8_t i;

    if (!r || !entry || !value_out ||
        ((*entry & PAT_ADDR_SPECIALS_BIT) == 0u))
        return 0u;
    offset = (uint16_t)(*entry & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;
    p = &r->pool[offset];
    flags = (uint8_t)(p[2] & PAT_SPECIAL_FLAGS_MASK);
    /* value_count: one per set special flag, as pat_blockReadAutomations() */
    ...
    auto_count = (uint8_t)(p[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if ((uint32_t)offset +
            ((uint32_t)pat_blockChunks(flags, auto_count) * 4u) >
        (PAT_STACK_SIZE * 32u))
        return 0u;
    idx = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    for (i = 0u; i < auto_count; i++) {
        uint16_t packed = (uint16_t)(p[idx] | ((uint16_t)p[idx + 1u] << 8u));

        if ((packed & 0x01FFu) == target) {
            *value_out = (uint8_t)((packed >> 9u) & 0x7Fu);
            return 1u;
        }
        idx = (uint8_t)(idx + 2u);
    }
    return 0u;
}
```

```
/*
 * Find one target's value on one step (S081 P2).
 *
 * What: scans one step's automation tail for `target` and returns its 7-bit
 * value. Same block validation as pat_blockReadAutomations(), without
 * copying the list. Why: the automation drain pairs each Instrument entry of
 * step A with the same target on step B (ramp start); copying B's full list
 * would add a 252-byte stack buffer to the audio-loop drain. Inputs:
 * resident Scene/track/step, canonical target. Output: nonzero and
 * *value_out when found; 0 for trigger-only, invalid, malformed, or absent.
 * Context: foreground. Accessors: seq_rampStartIfPaired(). Affiliates:
 * pat_readStepAutomations(), pat_blockReadAutomations().
 */
```

Header (`PatternData.h`, after `:524`):

```c
/* Find one target's 7-bit value on one step without copying the list
** (S081 P2 ramp pairing); 0 when absent or invalid. */
uint8_t pat_findStepAutomation(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint16_t target,
                               uint8_t *value_out);
```

### FS-1 PAT4 writer — MODIFY `filesystem.c:3090-3097`

After the byte-4 assignment ADD:

```c
        /* S081 P2: byte 5 stores the automation ramp mode (1 = on). */
        header[base + 5u] =
            (uint8_t)((region->track_auto_smooth >> track) & 1u);
```

Extend the existing byte-4 comment's last sentence: "Byte 5 (S081 P2,
automation ramp mode) follows the same rule."

### FS-2 Scene/Bank Pattern reader — MODIFY `filesystem.c:13218-13230`

- After `region->track_loop_exclude = 0u;` (`:13218`) ADD
  `region->track_auto_smooth = 0u;`.
- In the track loop, after the byte-4 test ADD:

```c
                /* S081 P2: byte 5 = automation ramp mode (files read 0). */
                if (staging_buf[base + 5u] != 0u)
                    region->track_auto_smooth |= (uint8_t)(1u << track);
```

### FS-3 Root Pattern Load reader — MODIFY `filesystem.c:15254-15266`

Same as FS-2: the clear goes after `:15254`, the byte-5 test after `:15265`.

### FS-4 AutoSave boot reader — MODIFY `filesystem.c:28559-28571`

Same as FS-2: the clear goes after `:28559`, the byte-5 test after `:28570`.

### CC-1 Track paste copies the mode — MODIFY `copyClearService.c:854-856`

After the loopable copy ADD:

```c
                /* S081 P2: the automation ramp mode is part of a track copy. */
                pat_setTrackAutoSmooth(job->scene, job->track,
                                       pat_trackAutoSmooth(src->scene,
                                                           src->track));
```

### CC-2 Track clear resets the mode — MODIFY `copyClearService.c:1032-1033`

After the loopable reset ADD:

```c
            /* S081 P2: a cleared track's automation ramps are off again. */
            pat_setTrackAutoSmooth(job->scene, job->track, 0u);
```

### TL-1 Card script — ADD `tools/convert_pattern_autosmooth.py`

The structure follows `tools/convert_scene_scale.py`: same PAT4 constants,
`crc32c()`, `pat4_length()`, `iter_paths()` and `argparse` shape.

- **For each file:**
  - Read it. If it is not `PAT4` and its name ends in `.pat`, print
    `skip <path>: not PAT4` and continue. Other files are ignored silently.
  - **Validate:**
    - size == `pat4_length(header_size, stack_size)`;
    - version byte (offset 4) == 2;
    - stored CRC32C (offset 14, little-endian) == CRC32C over the file with
      bytes 14..17 zeroed.

    On a mismatch, report it, count a failure and leave the file untouched.
  - **Normalise:** for track 0..6, offset `48 + track*16 + 5`; any value
    other than 0 becomes 0 (`off`). Count the changes.
  - If nothing changed, print `ok <path>`. Otherwise, unless `--dry-run`,
    recompute and store the CRC, write the file, and print
    `done <path>: N track(s)`.
- **Exit status** is nonzero if any file failed.
- **Module docstring:**

```
"""Pattern automation-ramp field normaliser (S081 P2).

S081 P2 stores each track's automation ramp mode ("asm", off/on) in byte 5
of the PAT4 16-byte track header (offset 48 + track*16 + 5). The byte was
reserved and written as zero, so the PAT4 version stays 2 and existing files
read as "off". This script validates every PAT4 file (size, version, CRC32C)
and forces byte 5 to 0 (off) on any track where it is not 0, recomputing the
CRC. Non-PAT4 .pat files (the legacy text format under Bank/) are skipped
and listed; the firmware does not read them.

Usage:
    python3 tools/convert_pattern_autosmooth.py [--dry-run] PATH [PATH ...]
"""
```

Run on `SD_CARD/` after the firmware lands (user deletes AutoSaves first).
On the 2026-10-13 card all 194 PAT4 files report `ok`.

Gate: `make all` clean (Phase A compiles with Phase B's
`PAR_TRACK_AUTO_SMOOTH`; land PD-4 together with MN-1).

---

## Phase B — Menu

### MN-1 Parameter mirror — ADD after `ParameterArray.h:285` (`PAR_TRACK_LOOPABLE,`); ADD a static assert after `:299-300`

```c
	PAR_TRACK_AUTO_SMOOTH,
```

```
	/*
	 * S081 P2: per-track automation ramp mirror (asm).
	 *
	 * What: off/on display/edit mirror of the active track's automation ramp
	 * mode in the viewed Scene's Pattern region (1 = automation on two
	 * consecutively played steps ramps linearly). Why: the STEP track page
	 * edits it beside lop. Appended after PAR_TRACK_LOOPABLE so no
	 * established Global id is renumbered. Inputs:
	 * pat_applyTrackSettingsToMenu(). Outputs: edits commit through
	 * menu_cellCommitValue() to pat_setTrackAutoSmooth(); bulk Global apply
	 * is a no-op for this id. Affiliates: pat_scene_region_t.track_auto_smooth,
	 * SEQ_PAGE sub-page 0 cell 7.
	 */
```

```c
_Static_assert(PAR_TRACK_AUTO_SMOOTH < NUM_PARAMS,
	"S081 P2 asm mirror must fit the fixed flat parameter allocation");
```

### MN-2 Text enums — MODIFY `menu.h:169-170`, `:232-233`, `:274-275`

- After `TEXT_TRACK_LOOPABLE,` (`:170`) ADD:
  `/* S081 P2: per-track automation ramp label (asm). */ TEXT_TRACK_AUTO_SMOOTH,`
- After `LONG_TRACK_LOOPABLE,` (`:233`) ADD:
  `/* S081 P2: "Automtn". */ LONG_TRACK_AUTO_SMOOTH,`
- `:275`: change `SHORT_TRACK_LOOPABLE` to `SHORT_TRACK_LOOPABLE,`, then ADD:
  `/* S081 P2: "asm". */ SHORT_TRACK_AUTO_SMOOTH`

### MN-3 Text tables — MODIFY `MenuText.h:179-180` and `:238-239`

- Short table, after `{"lop"},` (`:180`) ADD:
  `/* S081 P2: per-track automation ramp. */ {"asm"},`
- Long table, after `{"Loopable"},` (`:239`) ADD:
  `/* S081 P2. */ {"Automtn"},`

The tables are indexed by `shortNamesEnum`/`longNamesEnum`, so the new
entries must stay last and in the same order as MN-2.

### MN-4 dtype and name row — MODIFY `menu.c:1220` and `menu.c:1352`

- dtype table, after `[PAR_TRACK_LOOPABLE] = DTYPE_ON_OFF,` ADD:
  `[PAR_TRACK_AUTO_SMOOTH] = DTYPE_ON_OFF,`
- `valueNames[]`, after the loopable row ADD:

```c
    /* S081 P2: asm / Track / Automtn on the STEP track page. */
    {SHORT_TRACK_AUTO_SMOOTH,CAT_TRACK,LONG_TRACK_AUTO_SMOOTH},
```

`valueNames[]` is indexed by `NamesEnum`, so this row pairs with
`TEXT_TRACK_AUTO_SMOOTH`.

### MN-5 STEP page cell 7 — MODIFY `menuPages.h:143`

Replace the eighth top entry `TEXT_EMPTY` with `TEXT_TRACK_AUTO_SMOOTH`, and
the eighth bottom entry `PAR_NONE` with `PAR_TRACK_AUTO_SMOOTH`. ADD above
the row:

```c
  /* S081 P2: cell 7 = asm (automation ramp mode), after lop. */
```

### MN-6 Commit and bulk no-op — MODIFY `menu.c:5121-5127` and `menu.c:16302`

After the lop commit block (`:5127`) ADD:

```c
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_TRACK_AUTO_SMOOTH) {
        pat_setTrackAutoSmooth(menu_getViewedPattern(), menu_getActiveVoice(),
                               (uint8_t)(value != 0u));
        parameter_values[PAR_TRACK_AUTO_SMOOTH] = (uint8_t)(value != 0u);
        return 1u;
    }
```

```
    /*
     * S081 P2: STEP-page asm cell.
     *
     * What: commits off/on to the viewed Scene's active track via PatternData
     * and refreshes the mirror. Why: PAR_TRACK_AUTO_SMOOTH sits in the
     * appended id range that menu_sendAllGlobals() replays on every settings
     * load, so it must not commit through menu_parseGlobalParam() (loopable
     * precedent). Inputs: clamped 0/1. Outputs: pat_setTrackAutoSmooth().
     * Affiliates: pat_applyTrackSettingsToMenu(), seq_rampStepEvent().
     */
```

At `:16302` ADD `case PAR_TRACK_AUTO_SMOOTH:` to the mirror case list. Its
comment already covers it; add "asm" to its first sentence.

The cell resolves to no Pattern target, so `menu_cellClearTarget()` returns
0 for it. Live record, pot clear and knob erase ignore it with no change.

Gate: `make all` clean. Hardware: `asm` shows after `lop`, edits per track,
and is saved, loaded, copied and cleared with the Pattern.

---

## Phase C — Sequencer ramp engine

**Files:** `Core/Sequencer/sequencer.c`, `Core/Sequencer/sequencer.h`.

### SQ-1 Ramp state — ADD after `sequencer.c:810` (`static uint64_t seq_menuOverride[...]`)

```c
typedef struct {
    uint16_t id;        /* 0 = free; 1 + ((track << 9) | target) */
    uint8_t  from;
    uint8_t  to;
    uint16_t startQ4;   /* 1/16 master ticks, low 16 bits */
    uint16_t endQ4;
} seq_ramp_t;

_Static_assert(sizeof(seq_ramp_t) == 8u, "ramp slot must stay 8 bytes");

typedef struct {
    uint8_t  a;         /* bits 0..6 A step, bit 7 = stepped (unconsumed) */
    uint8_t  b;         /* B step + 1; 0 = no ramp target */
    uint16_t endQ4;     /* B's predicted apply time */
} seq_rampCtx_t;

#define SEQ_RAMP_SLOTS          64u
#define SEQ_RAMP_STEPPED        0x80u
#define SEQ_RAMP_BLOCK_DIV      3u     /* 3 x 0.73 ms DSP blocks = ~2.2 ms */

static seq_ramp_t seq_ramps[SEQ_RAMP_SLOTS];
static volatile seq_rampCtx_t seq_rampCtx[NUM_TRACKS];
static uint8_t seq_rampBlockDiv;
#if DEV_MODE_LOGGING
static uint16_t seq_rampNoSlotDrops;
#endif
```

```
/*
 * Automation ramp state (S081 P2, asm; +541 B SRAM1, +2 B DEV-only).
 *
 * What: seq_ramps[64] are the running linear ramps. Each slot holds the
 * track and Instrument target, the from/to 7-bit values, and the start/end
 * times in 1/16 master ticks (low 16 bits, wrap-safe differences; a ramp
 * spans at most one step plus its deferral, far below 4096 ticks).
 * seq_rampCtx[7] is TIM3's per-track hand-off:
 *  - a: the step whose automation applies (A) and the "stepped" flag; the
 *    foreground ends the track's ramps when it consumes the flag;
 *  - b: the predicted next played step (B) + 1, 0 when no ramp may start;
 *  - endQ4: B's predicted apply time.
 * seq_rampBlockDiv paces the writes to every third DSP block.
 * Why: plan §5. Free slots are id 0, so zero-initialised storage starts
 * empty; a zero context has b = 0 (no B). Lifetime: static. Writers: TIM3
 * (seq_rampStepEvent, seq_fireDeferred, break hooks) for the contexts;
 * foreground (drain, ramp service, restore) for the slots. The foreground
 * reads a context under PRIMASK. RAM: 512 + 28 + 1 B every build; 2 B
 * DEV_MODE_LOGGING (slot exhaustion count). Affiliates:
 * seq_rampStepEvent(), seq_rampStartIfPaired(), seq_rampService(),
 * seq_restoreAutomatedParameters().
 */
```

### SQ-2 Repeat-condition helper — MODIFY `seq_conditionEval()` at `sequencer.c:2306-2320`; ADD `seq_conditionRepeatPasses()` above `seq_conditionEval()` (`:2287`)

```c
static uint8_t seq_conditionRepeatPasses(uint8_t cond, uint8_t repeatCount)
{
    static const uint8_t xy_table[9][2] = {
        {1u,2u},{2u,2u},{1u,3u},{2u,3u},{3u,3u},
        {1u,4u},{2u,4u},{3u,4u},{4u,4u}
    };

    if (cond == 20u)
        return (uint8_t)(repeatCount == 0u);
    return (uint8_t)((repeatCount % xy_table[cond - 11u][1]) ==
                     (uint8_t)(xy_table[cond - 11u][0] - 1u));
}
```

The repeat branch of `seq_conditionEval()` becomes
`result = seq_conditionRepeatPasses(cond, seq_trackRepeatCount[track]);`.

```
/*
 * Repeat-count condition test (S079 §3; factored for S081 P2).
 *
 * What: conditions 11..19 ("X:Y") pass when repeatCount % Y == X - 1;
 * 20 ("1:-") passes only on the first play (repeatCount 0). Why: playback
 * and the ramp predictor must apply the identical rule; the predictor passes
 * the repeat count B will see. Inputs: condition 11..20, repeat count.
 * Output: pass flag. Context: TIM3. Accessors: seq_conditionEval(),
 * seq_rampConditionPredict().
 */
```

### SQ-3 Next-step predictor — ADD near the ramp section (after SQ-9's function, see SQ-12 for order)

```c
static int16_t seq_rampNextStep(uint8_t track,
                                const pat_scene_region_t *region,
                                uint8_t *repeatInc)
{
    uint8_t len = seq_effectiveTrackLength[track];
    uint8_t mode = region->track_play_mode[track];
    int16_t idx = seq_stepIndex[track];

    if (len < 1u)
        len = NUM_STEPS_PER_BAR;
    if (mode >= 6u)
        mode = 0u;
    if (idx >= (int16_t)len)
        idx = (int16_t)(len - 1u);
    *repeatInc = 0u;
    switch (mode) {
    case 1u: /* rev */
        if (--idx < 0) {
            idx = (int16_t)(len - 1u);
            *repeatInc = 1u;
        }
        return idx;
    case 2u: /* pip: the boundary plays twice */
        if (seq_trackPlayState[track] & SEQ_PLAY_STATE_PIP_REV) {
            if (idx <= 0) {
                *repeatInc = 1u;
                return idx;
            }
            return (int16_t)(idx - 1);
        }
        if (idx >= (int16_t)(len - 1u)) {
            *repeatInc = 1u;
            return idx;
        }
        return (int16_t)(idx + 1);
    case 3u: /* rnd: never predicted (D7) */
        return -1;
    case 4u: /* onc */
    case 5u: /* 1fr */
        return (idx + 1 >= (int16_t)len) ? -1 : (int16_t)(idx + 1);
    default: /* fwd */
        if (++idx >= (int16_t)len) {
            idx = 0;
            *repeatInc = 1u;
        }
        return idx;
    }
}
```

```
/*
 * Predict the next played step for an automation ramp (S081 P2 §5.2).
 *
 * What: mirrors seq_advanceTrackStep()'s play-mode switch without side
 * effects: fwd/rev wrap at the effective length, pip repeats its boundary
 * step and turns, onc/1fr have no step after the last, rnd is never
 * predicted (D7). *repeatInc is 1 when the advance to B increments the
 * track's repeat count (wrap or pip turn), so B's repeat conditions are
 * evaluated with the count B will see. Why: a ramp needs B before B is
 * reached. The looper is ignored on purpose (D8): ramps follow the unlooped
 * segment; at a loop wrap the restored step's apply ends the ramps
 * (SQ-11). Inputs: track, its played Scene region, live index/play state,
 * effective length. Output: B index or -1. Context: TIM3. Keep in step with
 * the switch in seq_advanceTrackStep(). Affiliates: seq_rampPredict().
 */
```

At the head of the play-mode comment in `seq_advanceTrackStep()`
(`sequencer.c:2637-2652`) ADD one line: "Mirrored side-effect-free by
seq_rampNextStep() (S081 P2); change both together."

### SQ-4 Condition and deferral predictor — ADD `seq_rampConditionPredict()` and `seq_rampPredict()` after SQ-3

```c
static uint8_t seq_rampConditionPredict(const pat_step_specials_t *sp,
                                        uint8_t track, uint8_t repeatInc)
{
    uint8_t cond = sp->condition;

    if (cond == 0u || cond > 40u)
        return 1u;
    if (cond >= 11u && cond <= 20u)
        return seq_conditionRepeatPasses(
            cond, (uint8_t)(seq_trackRepeatCount[track] + repeatInc));
    if (cond == 25u)
        return seq_trackLastConditionFired[track];
    if (cond == 26u)
        return (uint8_t)(!seq_trackLastConditionFired[track]);
    return 0u;              /* rNN, fil/!fl, lop/!lp, mute: not predictable */
}
```

```
/*
 * Will B play, as far as can be known now? (S081 P2 D1)
 *
 * What: 1 when B's condition is predictable at A's apply time AND passes:
 * no condition, the repeat counters (with the count B will see), or
 * lst/!ls (seq_trackLastConditionFired already holds A's result; no other
 * condition on this track is evaluated before B). Probability, fill, loop
 * and mute conditions return 0, so B is never ramped to and jumps as today.
 * Why: the ramp rule requires both steps to play; a step that might not
 * play must not be ramped toward. Condition evaluation itself is unchanged.
 * Context: TIM3. Affiliates: seq_conditionEval(), seq_rampPredict().
 */
```

```c
static void seq_rampPredict(uint8_t track, const pat_scene_region_t *region,
                            seq_rampCtx_t *ctx)
{
    uint8_t scene = seq_perTrackPattern[track];
    uint8_t bit = (uint8_t)(1u << track);
    uint8_t repeatInc;
    int16_t b;
    pat_step_specials_t spb;
    uint16_t interval;
    uint16_t r;
    uint16_t ticksToB;
    uint16_t dB = 0u;

    ctx->b = 0u;
    ctx->endQ4 = 0u;
    if (pat_stepAutomationCount(scene, track,
                                (uint8_t)seq_stepIndex[track]) == 0u)
        return;
    if ((seq_mutedTracks & bit) || (seq_eraseFullMask & bit))
        return;
    b = seq_rampNextStep(track, region, &repeatInc);
    if (b < 0 || pat_stepAutomationCount(scene, track, (uint8_t)b) == 0u)
        return;
    spb = pat_readStepSpecials(scene, track, (uint8_t)b);
    if (!seq_rampConditionPredict(&spb, track, repeatInc))
        return;
    interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
    r = seq_trackAccumulator[track];
    ticksToB = (interval > r) ? (uint16_t)((interval - r + 255u) >> 8u) : 1u;
    /* B's automation is deferred with B's trigger (shuffle + micro-timing). */
    if (pat_isStepActive(track, (uint8_t)b, scene) &&
        (seq_eraseSpecialsMask & bit) == 0u) {
        uint8_t shuffle = seq_effectiveTrackShuffle[track];

        if (shuffle > 0u && (b & 1))
            dB = (uint16_t)(((uint16_t)shuffle * 24u) / 256u);
        if (spb.timing > 0u)
            dB = (uint16_t)(dB + (uint16_t)(((uint32_t)(interval >> 8u) *
                                             spb.timing) / 100u));
    }
    ctx->endQ4 = (uint16_t)((seq_elapsedPpqTicks + ticksToB + dB) << 4u);
    ctx->b = (uint8_t)(b + 1);
}
```

```
/*
 * Predict B and its apply time for step A (S081 P2 §5.2).
 *
 * What: at A's step start on an asm track, fills the context's B (+1) and
 * B's apply time, or leaves b = 0 when no ramp may start because:
 *  - A has no automation, or the track is muted / full-erased;
 *  - there is no predictable B (rnd, once end);
 *  - B has no automation, or B's condition is not predictably passing.
 * B's start is the next DDA advance (ceil((interval - remainder) / 256)
 * ticks after this tick). B's automation is deferred like B's trigger when
 * B holds a trigger: shuffle on odd steps plus micro-timing, the same
 * arithmetic as the deferral in seq_advanceTrackStep(). Why: a ramp must end
 * where B's value lands today. Inputs: live index, accumulator remainder,
 * effective scale/shuffle, B's specials, erase/mute masks. Output: *ctx b
 * and endQ4 (a is set by the caller). Context: TIM3, called from
 * seq_rampStepEvent() at A's step start (also for a deferred A, whose
 * prediction is taken before the playhead moves on). Affiliates:
 * seq_rampNextStep(), seq_rampConditionPredict(), pat_stepAutomationCount().
 */
```

### SQ-5 TIM3 step event and break hooks — ADD `seq_rampStepEvent()`, `seq_rampBreakTrack()`, `seq_rampBreakAll()` after SQ-4

```c
static void seq_rampStepEvent(uint8_t track, uint8_t applied, uint8_t deferred)
{
    const pat_scene_region_t *region =
        pat_sceneRegion(seq_perTrackPattern[track]);
    seq_rampCtx_t ctx;

    if (!region || ((region->track_auto_smooth >> track) & 1u) == 0u)
        return;
    ctx.a = (uint8_t)(seq_stepIndex[track] & 0x7F);
    if (!applied && !deferred) {
        ctx.a |= SEQ_RAMP_STEPPED;
        ctx.b = 0u;
        ctx.endQ4 = 0u;
    } else {
        if (applied)
            ctx.a |= SEQ_RAMP_STEPPED;
        seq_rampPredict(track, region, &ctx);
    }
    seq_rampCtx[track] = ctx;
}

static void seq_rampBreakTrack(uint8_t track)   /* TIM3 or foreground */
{
    uint32_t primask = __get_PRIMASK();

    if (track >= NUM_TRACKS)
        return;
    __disable_irq();
    seq_rampCtx[track].a = (uint8_t)(SEQ_RAMP_STEPPED | 0x7Fu);
    seq_rampCtx[track].b = 0u;
    __set_PRIMASK(primask);
}

static void seq_rampBreakAll(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++)
        seq_rampBreakTrack(track);
}
```

```
/*
 * TIM3 ramp hand-off at a step (S081 P2 §5.1/§5.5).
 *
 * What: on an asm track only, records what happened at this step for the
 * foreground:
 *  - applied (automation queued now): stepped flag + prediction of B;
 *  - deferred (automation queues at the deferred fire): prediction only;
 *    seq_fireDeferred() sets the stepped flag when the entries queue, so
 *    the ramps into this step run until its value actually lands;
 *  - neither (condition failed, muted, full-erased, no automation block):
 *    stepped flag with no B, so ramps toward this step end at its start.
 * Tracks with asm off write nothing; the ramp service cancels any ramp left
 * on them. Why: ramps end at the next played step's apply time, and that
 * step's own value then takes over. Context: TIM3. Accessors:
 * seq_advanceTrackStep(). Affiliates: seq_rampPredict(), seq_fireDeferred(),
 * seq_rampConsumeTrack().
 */
```

```
/*
 * Position break for automation ramps (S081 P2 §5.5).
 *
 * What: marks the track's context "stepped, no B", so the foreground ends
 * its ramps at the next consume and no ramp starts until the next step's
 * prediction. seq_rampBreakAll() does it for every track. Why: after a
 * transport change, realign, Scene change, PC reset, loop wrap/resume or a
 * chain stop, the predicted B will not follow. A ramp left running would
 * drift toward a value that never plays. Context: TIM3 or foreground
 * (PRIMASK section). Accessors: every seq_playbackEpoch bump site,
 * seq_loopProcessTick() wrap, seq_loopResumeTrack(), the chain stop.
 */
```

### SQ-6 Foreground helpers — ADD after SQ-5

```c
static uint16_t seq_rampNowQ4(void);
static void seq_rampEndTrack(uint8_t track);
static void seq_rampConsumeTrack(uint8_t track);
static void seq_rampConsumeSteps(void);
static uint8_t seq_rampDtypeOk(const ParamDescriptor *descriptor);
static uint8_t seq_rampTrackOn(uint8_t track);
```

- **`seq_rampNowQ4()`**
  - Under PRIMASK, copy `seq_elapsedPpqTicks`, `seq_ppqStampUs`,
    `seq_ppqPeriodUs` and `timebase_tim2Now()`.
  - `frac = period ? ((now - stamp) * 16) / period : 0`, clamped to 15.
  - Return `(uint16_t)((ticks << 4) + frac)`.
- **`seq_rampEndTrack(t)`:** set `id = 0` for every slot whose
  `((id - 1) >> 9) == t`.
- **`seq_rampConsumeTrack(t)`:** under PRIMASK, if
  `seq_rampCtx[t].a & SEQ_RAMP_STEPPED`, clear the bit and remember it.
  After the section, if it was set, call `seq_rampEndTrack(t)`.
- **`seq_rampConsumeSteps()`:** call `seq_rampConsumeTrack()` for tracks
  0..6.
- **`seq_rampDtypeOk(d)`:** `(d->dtype & 0x0F)` is one of `DTYPE_0B127`,
  `DTYPE_PM63`, `DTYPE_0B255`, `DTYPE_0B15`, `DTYPE_1B16` or `DTYPE_1B128`,
  and `d->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE`.
- **`seq_rampTrackOn(t)`:** region of `seq_perTrackPattern[t]` exists and
  its `track_auto_smooth` bit for `t` is set.

```
/*
 * Ramp time base (S081 P2 §5.5).
 *
 * What: the current master position in 1/16 ticks (low 16 bits): the 96-PPQ
 * tick count plus the sub-tick fraction from the tick's microsecond stamp.
 * Why: ramps measured in master ticks stretch with a tempo change; the
 * sub-tick fraction keeps the ~2 ms updates smooth between 5 ms ticks.
 * Context: foreground (PRIMASK copy). Affiliates: seq_ppqStampUs,
 * seq_captureTrackTime() (the same fields), seq_rampPredict() (same units).
 */
```

```
/*
 * End a track's ramps when its next step applies (S081 P2 §5.5).
 *
 * What: consumes the TIM3 stepped flag of one track (or all) and frees that
 * track's ramp slots. The values stay where they are; the new step's own
 * entries (drain) or today's restore rules take over. Why: a ramp lives
 * exactly from A's apply to the next played step's apply. Ending at B's
 * arrival (not at the computed time) keeps a late ramp sample from
 * overwriting B's value. At a loop wrap the restored step's apply ends the
 * ramps, so nothing ramps across the wrap (D8). Order: the trigger restore
 * consumes its own track first, the drain consumes all tracks before
 * applying entries, the ramp service consumes all tracks each block.
 * Context: foreground. Affiliates: seq_rampStepEvent(), seq_fireDeferred(),
 * seq_restoreAutomatedParameters(), seq_drainPendingAutomation().
 */
```

```
/*
 * Which Instrument parameters ramp (S081 P2 D2).
 *
 * What: only automatable descriptors with a numeric-range dtype (0B127,
 * PM63, and the numeric 0B255/0B15/1B16/1B128 should a descriptor use
 * them). Menu tables (waveforms, filter types), FM mode, on/off, LFO
 * polarity and target/voice selections never ramp: their in-between values
 * are other choices, not intermediate settings. Why: user decision D2.
 * Inputs: the descriptor of the target's slot in the played Scene. Output:
 * flag. Affiliates: seq_rampStartIfPaired().
 */
```

### SQ-7 Instrument write helper and pairing — MODIFY the drain at `sequencer.c:3297-3309`; ADD `seq_writeInstrumentAutomation()` and `seq_rampStartIfPaired()`

**`seq_writeInstrumentAutomation()`** takes over the body at `:3297-3309`:

```c
static const ParamDescriptor *seq_writeInstrumentAutomation(
    uint8_t owner_scene, uint16_t target, uint8_t value)
{
    uint8_t slot = instrumentParam_slot(target);
    uint8_t local = instrumentParam_local(target);
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(owner_scene, slot);
    const ParamDescriptor *descriptor = instrument
        ? instrumentManager_descriptor(instrument->type, local) : 0;

    if (!descriptor || !instrumentManager_writeRuntime(slot, descriptor, value))
        return 0;
    seq_automation_dirty[slot] |= (1ULL << local);
    return descriptor;
}
```

```
/*
 * Write one Instrument automation value (drain and ramps, S081 P2).
 *
 * What: resolves the descriptor in the owning track's played Scene, writes
 * the runtime value, and marks the parameter held for the next trigger's
 * restore. Returns the descriptor on success. Why: ramp samples are
 * automation values under the binding priority rule (MEMORY.md): the same
 * write path keeps the Morph sweep and menu apply from overwriting them.
 * The caller has already validated the target and the R12 override.
 * Context: foreground. Accessors: seq_drainPendingAutomation(),
 * seq_rampService(). Affiliates: instrumentManager_writeRuntime(),
 * seq_automation_dirty[].
 */
```

**Drain change.** `:3297-3309` becomes:

```c
                    {
                        const ParamDescriptor *descriptor =
                            seq_writeInstrumentAutomation(owner_scene, target,
                                                          value);

                        if (descriptor)
                            seq_rampStartIfPaired(owner_track, owner_scene,
                                (uint8_t)((identity & SEQ_PENDING_STEP_ID_MASK) %
                                          NUM_STEPS),
                                target, value, descriptor);
                    }
```

**`seq_rampStartIfPaired()`:**

```c
static void seq_rampStartIfPaired(uint8_t track, uint8_t scene, uint8_t step,
                                  uint16_t target, uint8_t from,
                                  const ParamDescriptor *descriptor)
{
    seq_rampCtx_t ctx;
    uint32_t primask;
    uint16_t now;
    uint16_t id;
    uint8_t to;
    uint8_t i;
    uint8_t freeSlot = SEQ_RAMP_SLOTS;

    if (!seq_rampTrackOn(track) || !seq_rampDtypeOk(descriptor))
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    ctx = seq_rampCtx[track];
    __set_PRIMASK(primask);
    if ((uint8_t)(ctx.a & 0x7Fu) != step || ctx.b == 0u)
        return;
    if (!pat_findStepAutomation(scene, track, (uint8_t)(ctx.b - 1u), target, &to))
        return;
    now = seq_rampNowQ4();
    if ((int16_t)(ctx.endQ4 - now) <= 0)
        return;
    id = (uint16_t)(1u + (((uint16_t)track << 9u) | target));
    for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
        if (seq_ramps[i].id == id) { freeSlot = i; break; }
        if (seq_ramps[i].id == 0u && freeSlot == SEQ_RAMP_SLOTS)
            freeSlot = i;
    }
    if (freeSlot == SEQ_RAMP_SLOTS) {
#if DEV_MODE_LOGGING
        seq_rampNoSlotDrops++;
#endif
        return;                         /* cap reached: jump as today (D10) */
    }
    seq_ramps[freeSlot].id = id;
    seq_ramps[freeSlot].from = from;
    seq_ramps[freeSlot].to = to;
    seq_ramps[freeSlot].startQ4 = now;
    seq_ramps[freeSlot].endQ4 = ctx.endQ4;
}
```

```
/*
 * Start a ramp for a just-written entry of step A (S081 P2 §5.4).
 *
 * What: when A's entry was written on an asm track for a rampable target,
 * and TIM3's context for this track still describes this A with a
 * predicted B that holds the same target, takes a slot that ramps from A's
 * value (just written) to B's value, from now to B's predicted apply time.
 * An existing slot for the same track/target is reused. With all 64 slots
 * busy the pair jumps as today (D10; DEV count). Why: plan §5.4; the
 * context check drops the pair (jump) if TIM3 has already moved past A,
 * which can only happen when the foreground lags a whole step. Inputs:
 * owning track/Scene, A's step (from the pending identity), target,
 * written value, descriptor. Output: one slot. Context: foreground (drain).
 * Affiliates: pat_findStepAutomation(), seq_rampDtypeOk(), seq_rampNowQ4().
 */
```

Also ADD at the top of `seq_drainPendingAutomation()`, after the Effect
reset block (`:3212`) and before `if (!seq_pending_automation_drain)`
(`:3214`):

```c
    /* S081 P2: ramps of tracks whose next step applied end first. */
    seq_rampConsumeSteps();
```

Extend the drain's function comment (`:3189-3199`) with: "S081 P2: before
applying entries it ends the ramps of tracks whose next step has applied;
each written Instrument entry of an asm track may start a ramp toward the
same target on the predicted next step (seq_rampStartIfPaired())."

### SQ-8 Ramp service — ADD `seq_rampService()` after SQ-7; declare in `sequencer.h` after `:307`

```c
void seq_rampService(void)
{
    uint16_t now;
    uint8_t i;

    seq_rampConsumeSteps();
    if (++seq_rampBlockDiv < SEQ_RAMP_BLOCK_DIV)
        return;
    seq_rampBlockDiv = 0u;
    now = seq_rampNowQ4();
    for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
        seq_ramp_t *r = &seq_ramps[i];
        uint16_t key;
        uint8_t track;
        uint16_t target;
        uint8_t scene;
        uint8_t slot;
        int16_t num;
        int16_t den;
        uint8_t value;

        if (r->id == 0u)
            continue;
        key = (uint16_t)(r->id - 1u);
        track = (uint8_t)(key >> 9u);
        target = (uint16_t)(key & 0x01FFu);
        scene = seq_perTrackPattern[track];
        slot = instrumentParam_slot(target);
        if (!seq_rampTrackOn(track) ||
            (seq_filterLatch != 0 &&
             (seq_filterLatch[target >> 3u] & (uint8_t)(1u << (target & 7u)))) ||
            (seq_filterErase != 0 && track == seq_filterEraseTrack &&
             (seq_filterErase[target >> 3u] & (uint8_t)(1u << (target & 7u)))) ||
            (slot < INSTRUMENT_SLOT_COUNT &&
             (seq_menuOverride[slot] & (1ULL << instrumentParam_local(target)))) ||
            !instrumentManager_targetValid(scene, target,
                                           INSTRUMENT_TARGET_AUTOMATION)) {
            r->id = 0u;                 /* cancel: value stays */
            continue;
        }
        num = (int16_t)(now - r->startQ4);
        den = (int16_t)(r->endQ4 - r->startQ4);
        if (den <= 0 || num >= den) {
            value = r->to;
        } else if (num <= 0) {
            value = r->from;
        } else {
            int32_t delta = ((int32_t)r->to - (int32_t)r->from) * num;

            value = (uint8_t)((int32_t)r->from +
                ((delta >= 0) ? (delta + den / 2) / den
                              : -((-delta + den / 2) / den)));
        }
        (void)seq_writeInstrumentAutomation(scene, target, value);
    }
}
```

```
/*
 * Automation ramp service (S081 P2 §5.5, D6/D9).
 *
 * What: every DSP block it ends the ramps of tracks whose next step has
 * applied. Every third block (~2.2 ms) it writes each running ramp's value
 * from + (to - from) x elapsed / duration, rounded to the 7-bit descriptor
 * value, holding `to` after the predicted end until the next step applies.
 * A ramp is cancelled (its value stays) when:
 *  - the track's asm turned off;
 *  - live record latches or erases the target (the drain's filters);
 *  - an R12 menu override is set;
 *  - the target is no longer valid in the track's played Scene.
 * Why: plan §5.5. Writes go through seq_writeInstrumentAutomation(), so ramp
 * values are held automation values (binding priority rule). Cost: at most
 * 64 runtime writes per pass, the worst case measured on the stress Scene
 * (DSP CPU policy; plan §7). Context: foreground, audio render loop, right
 * after seq_drainPendingAutomation(). Accessors: main.c
 * audio_check_and_render(). Affiliates: seq_rampConsumeSteps(),
 * seq_rampStartIfPaired().
 */
```

Header (`sequencer.h`, after `:307`):

```c
/* Automation ramp service (S081 P2): ends ramps of tracks whose next step
** applied and writes running ramps every ~2.2 ms. Call after the drain. */
void seq_rampService(void);
```

### SQ-9 Restore skips ramped parameters — MODIFY `seq_restoreAutomatedParameters()` at `sequencer.c:3364-3410`

After the slot computation (`:3372-3373`) and before the R12 override clear,
ADD:

```c
    /* S081 P2: this track's ramps end at its own next step first. */
    seq_rampConsumeTrack(trigger_track);
```

After `mask = seq_automation_dirty[slot]; if (!mask) return;`, ADD:

```c
    {
        uint64_t ramped = 0u;
        uint8_t i;

        for (i = 0u; i < SEQ_RAMP_SLOTS; i++) {
            uint16_t key;

            if (seq_ramps[i].id == 0u)
                continue;
            key = (uint16_t)(seq_ramps[i].id - 1u);
            if (instrumentParam_slot((uint16_t)(key & 0x01FFu)) == slot)
                ramped |= (1ULL << instrumentParam_local(
                                        (uint16_t)(key & 0x01FFu)));
        }
        mask &= ~ramped;
        rampHeld = ramped;
    }
```

(Declare `uint64_t rampHeld = 0u;` with the locals.) The final
`seq_automation_dirty[slot] = 0u;` (`:3409`) becomes
`seq_automation_dirty[slot] = rampHeld & seq_automation_dirty[slot];`.

ADD to the function comment (`:3350-3363`):

```
 * S081 P2 (D3): the trigger track's own ramps end first
 * (seq_rampConsumeTrack); parameters still held by a running ramp of this
 * slot (a live press, MIDI note, flam main or roll renewal mid-step) are
 * neither restored nor un-marked, so the ramp continues without a dip to the
 * menu value. Ramps of the other track sharing slot 5 (tracks 5/6) keep
 * running.
```

### SQ-10 Step event call sites — MODIFY `seq_advanceTrackStep()` at `sequencer.c:2718-2722`, `:3027-3031`, `:3033-3034`; MODIFY `seq_fireDeferred()` at `:4764-4776`

- `:2722` (after `uint8_t rec_fired = 0u;`) ADD:

```c
		/* S081 P2: what this step did with its automation (ramp hand-off). */
		uint8_t ramp_applied = 0u;
		uint8_t ramp_deferred = 0u;
```

- `:3027-3031` becomes:

```c
            if (!deferred_this_step) {
                /* S081 §5.3: the Effect step marker is removed (R13); the
                 * step's own automation entries are queued directly. */
                seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
                ramp_applied = 1u;
            } else {
                ramp_deferred = 1u;
            }
```

- After `:3033` (the closing brace of `if (process_step)`) ADD:

```c
		/* S081 P2: hand the step to the automation ramp engine. */
		seq_rampStepEvent(track, ramp_applied, ramp_deferred);
```

- `seq_fireDeferred()`, after `seq_queueStepAutomations(track, step);`
  (`:4773`) ADD:

```c
	/* S081 P2: the deferred step's automation lands now; end the ramps
	 * into it (the context still describes this step unless the next step
	 * has already started, in which case that step's event ended them). */
	if ((uint8_t)(seq_rampCtx[track].a & 0x7Fu) == step)
		seq_rampCtx[track].a |= SEQ_RAMP_STEPPED;
```

ADD one sentence to the `seq_fireDeferred()` comment: "S081 P2: sets the
ramp context's stepped flag, so ramps into this step end when its value
lands."

### SQ-11 Position-break hooks — MODIFY the epoch, loop and chain sites

| Site | Line | Call |
|---|---|---|
| `seq_setTrackPlayedScene()` | after `:1719` | `seq_rampBreakTrack(track);` |
| `seq_selectActivePattern()` | after `:1986` | `seq_rampBreakAll();` |
| `seq_alignActivePatternToScene()` | after `:2088` | `seq_rampBreakAll();` |
| `seq_realignActivePatternToMasterClock()` | after `:3637` | `seq_rampBreakAll();` |
| `seq_realignTrackToMasterClock()` | after `:3713` | `seq_rampBreakTrack(track);` |
| `seq_setRunning()` | after `:5197` | `seq_rampBreakAll();` |
| `seq_setStepIndexToStart()` | after `:5457` | `seq_rampBreakAll();` |
| `seq_loopProcessTick()` wrap | after `:4461` | `seq_rampBreakTrack(track);` |
| `seq_loopResumeTrack()` | after `:4235` | `seq_rampBreakTrack(track);` |
| chain `stp` | before `:4667` return | `seq_rampBreakAll();` |

At each epoch site ADD this one-line comment:

```c
	/* S081 P2: a playhead break ends automation ramps (no B will follow). */
```

At the loop wrap ADD:

```c
		/* S081 P2 (D8): no ramp across the loop wrap; the restored step's
		 * apply starts fresh. */
```

At the chain stop ADD:

```c
		/* S081 P2: frozen tracks reach no B; end their ramps. */
```

A wrap on a step boundary is followed in the same tick by the DDA advance,
whose step event overwrites the break with the restored step's context.
Both carry the stepped flag, so one consume ends the old ramps and the new
prediction stands.

### SQ-12 Forward declarations and placement — MODIFY `sequencer.c:1341-1348`

After `static void seq_fireDeferred(uint8_t track);` ADD:

```c
/* S081 P2 automation ramps (defined after seq_restoreAutomatedParameters()). */
static void seq_rampStepEvent(uint8_t track, uint8_t applied, uint8_t deferred);
static void seq_rampBreakTrack(uint8_t track);
static void seq_rampBreakAll(void);
static void seq_rampConsumeTrack(uint8_t track);
static void seq_rampConsumeSteps(void);
static const ParamDescriptor *seq_writeInstrumentAutomation(
    uint8_t owner_scene, uint16_t target, uint8_t value);
static void seq_rampStartIfPaired(uint8_t track, uint8_t scene, uint8_t step,
                                  uint16_t target, uint8_t from,
                                  const ParamDescriptor *descriptor);
```

**Placement:** SQ-3..SQ-8 form one section,
`/* ===== S081 P2 automation ramps ===== */`, directly after
`seq_restoreAutomatedParameters()` (`:3410`), in this order:

1. `seq_rampNextStep`;
2. `seq_rampConditionPredict`;
3. `seq_rampPredict`;
4. `seq_rampStepEvent`;
5. the break hooks;
6. the SQ-6 helpers;
7. `seq_writeInstrumentAutomation`;
8. `seq_rampStartIfPaired`;
9. `seq_rampService`.

`seq_conditionRepeatPasses()` (SQ-2) sits above `seq_conditionEval()`.

Gate: `make all` clean.

---

## Phase D — Main loop

### MA-1 Ramp service call — MODIFY `main.c:213-214`

After `seq_drainPendingAutomation();` ADD:

```c
            /* S081 P2: automation ramps, after the step values land. */
            seq_rampService();
```

The call sits inside the per-`OUTPUT_DMA_SIZE` block loop, so ramps share
the DSP control cadence. The service paces its writes to every third block.

Gate: `make all` clean, then `link_budget.py`.

---

## Phase E — RAM, build and verification

### RAM

All static, normal SRAM1.

| Item | Bytes | Owner | Lifetime |
|---|---|---|---|
| `seq_ramps[64]` (8 B) | 512 | Sequencer | static |
| `seq_rampCtx[7]` (4 B) | 28 | Sequencer | static |
| `seq_rampBlockDiv` | 1 | Sequencer | static |
| `track_auto_smooth` × 17 regions | 17 | PatternData | static |
| `PAR_TRACK_AUTO_SMOOTH` mirror | 0 (existing slot) | Menu | — |
| **Every build** | **558** | | |
| `seq_rampNoSlotDrops`, DEV_MODE_LOGGING only | 2 | Sequencer | static |

- This is within the approved 565 B (+4 B DEV). The planned 8 B active mask
  is replaced by `id == 0` free slots.
- **Stack:**
  - `seq_rampStartIfPaired()` and `seq_rampService()` use a few locals;
  - `pat_findStepAutomation()` copies nothing;
  - no list buffer is added to the drain.

### Verification

**Build:** `make all` clean, `link_budget.py`, and `nm` sizes for the
symbols above.

**Host test** (optional; same harness style as the P1B placement test):
`seq_rampNextStep()` against the advance switch for every mode, length 1..16
and both pip directions.

**Hardware:**

1. `asm` after `lop`. On/off survives save, load, AutoSave, track
   copy/paste and track clear. Run the card script with `--dry-run` and
   without it: all `ok`.
2. **fwd** with automation on consecutive steps: audible linear ramp.
   - Menu value → automated step: jump.
   - Automated step → step without the parameter: jump.
3. **Other modes:**
   - rev, pip (the boundary plays twice: no audible ramp there), onc, 1fr:
     ramp;
   - rnd: always jumps.
4. **Shuffle** on odd B, and **micro-timing on B:** the ramp ends where B's
   value lands, with no step at the end.
5. **Tempo change mid-ramp:** the ramp stretches.
6. **B conditions:**
   - `1:2`, `lst`: ramp when B plays, jump otherwise;
   - `r50`, `fil`, `lop`, mute: always jump.
7. **Menu knob turned mid-ramp:** the menu wins until the next trigger, and
   the ramp is cancelled.
8. **Overlay press, MIDI note, flam main or roll renewal mid-ramp:** no dip
   (D3).
9. **Live record LATCH on an `asm` track:**
   - latched targets play the menu value, with no ramp;
   - knob erase on rtk: no ramp for the erased target.
10. **Stop, start, Scene change, PC reset, chain `stp` mid-ramp:** the ramp
    stops and no value drifts.
11. **Looper:**
    - inside the loop, ramps as in normal play;
    - the last loop step ramps toward the step after the loop end when that
      step holds the parameter;
    - at the wrap, the loop start's value or today's rule applies, with no
      ramp back (D8).
12. **Per-track Scene playback:** the flag and B come from the track's
    played Scene.
13. **Discrete parameters** (waveform, filter type, FM mode, LFO
    polarity/targets): always jump (D2).
14. **CPU:** stress Scene with 64 ramps running (for example 7 tracks of
    dense automation on 1/32 steps). Read the `cpu` widget. If the worst
    case is too high, lower the ramp count per pass (16-per-pass
    round-robin, plan §7) and re-measure.

---

## Phase F — Documentation at close

- **`PATTERN_DYNAMIC_STACK.md` §6:** automation ramps.
  - TIM3 context and the stepped flag;
  - predictor rules (modes, conditions, deferral);
  - end and cancel rules;
  - the restore skip;
  - PAT4 track-header byte 5.
- **`STORAGE_SRAM_MANIFEST.md`:** the RAM table above; region size +1 B
  (background region 10,527 B).
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - `pat_setTrackAutoSmooth()`, `pat_trackAutoSmooth()`,
    `pat_findStepAutomation()`;
  - `seq_rampService()`;
  - `PAR_TRACK_AUTO_SMOOTH`.
- **PAT4 format notes:** track-header byte 5 = asm (0/1), version 2.
- **`MEMORY.md`:** one binding line: ramp samples are automation writes
  (`seq_writeInstrumentAutomation`); keep `seq_rampNextStep()` in step with
  the play-mode switch.

---

## Order of work

| Phase | Content | Gate |
|---|---|---|
| A | PD-1..PD-7, FS-1..FS-4, CC-1, CC-2, TL-1 | build with MN-1 (PD-4 uses the mirror id) |
| B | MN-1..MN-6 | clean build; hardware item 1 |
| C | SQ-1..SQ-12 | clean build |
| D | MA-1 | clean build; link budget |
| E | build/RAM check, hardware matrix, CPU measurement | — |
| F | documentation | — |

---

## Work notes (implementation, 2026-10-14)

Baseline before any change (`make all` + `link_budget.py`): flash 568,604 /
753,664 B (headroom 185,060 B), ITCM 4,168 B, DTCM statics 4,472 B.

### Phase A + B — done

- **PD-1..PD-7** applied as written. PD-7's value-count block carries a
  one-line comment ("one special value byte per set flag precedes the
  automation tail") in place of the schedule's `...`.
- **PD-5 header:** the declaration comment also names the clients (STEP
  asm cell, copy/clear) and notes that the sequencer reads the region byte
  directly from TIM3.
- **FS-1..FS-4** applied at the anchors (`filesystem.c:3099`, `:13223`/
  `:13235`, `:15263`/`:15275`, `:28572`/`:28584` after the edit).
- **CC-1, CC-2** applied.
- **MN-1..MN-6** applied. Additional comment updates (no behaviour):
  - the dtype-table comment in `menu.c` now names `PAR_TRACK_AUTO_SMOOTH`
    as an ordinary on/off cell beside the loopable one;
  - the mirror no-op case comment in `menu_parseGlobalParam()` names lop,
    chain and asm;
  - the `menuPages.h` SEQ_PAGE comment said "The last cell is the traversal
    terminator". Checked `menu_moveToMenuItem()`: sub-page 0 uses the generic
    `checkvalid` path, which stops at column 8 on non-MIDI pages, so cell 7
    is reachable and nothing relies on a terminator there. The comment now
    says all eight cells are populated.
- **TL-1** `tools/convert_pattern_autosmooth.py` written. Deviation: the
  version check reads the 16-bit little-endian field at offsets 4..5 (the
  firmware's `filesystem_patternParseHeader()` form), not only byte 4.
  - `--dry-run SD_CARD/`: **194 `ok`, 229 `skip … not PAT4`**, exit 0 (the
    plan's prediction).
  - Scratch-copy self-test: a PAT4 copy with byte 5 set on two tracks
    reported `plan … 2 track(s)`, then `done … 2 track(s)`, then `ok`, and the
    result was byte-identical to the original; the same file with one byte
    appended reported `fail … size 10657 != expected 10656`, exit 1.
- **Gate:** `make all` clean (no new warnings; the remaining warnings are the
  pre-existing unused `filesystem.c` functions, `PatternData.c:200` packed
  member, `EuklidGenerator.c:54`, newlib stubs).

### Phase C + D — done

All anchors matched the schedule's line numbers before editing. SQ-1..SQ-12
and MA-1 applied; the engine section sits directly after
`seq_restoreAutomatedParameters()` under
`/* ===== S081 P2 automation ramps ===== */` in the SQ-12 order.

**Deviations from the schedule (all in `sequencer.c` unless noted):**

1. **Race fix in `seq_rampStartIfPaired()` (correctness).** The drain
   follows the live producer count, so TIM3 can queue step A's entries and
   set A's stepped flag *after* the drain's top-of-pass
   `seq_rampConsumeSteps()`. The drain would then start the A→B ramps, and
   the next consume (ramp service) would see A's flag and end them at once:
   the pair would jump. `seq_rampStartIfPaired()` now, in its PRIMASK
   section, consumes the flag when it still belongs to this A, then ends the
   track's older ramps before starting the new one. Its comment block and
   the end/consume comment describe this.
2. **Held ramps stay exact (robustness).** When `elapsed ≥ duration` the
   service writes `to` and sets `from = to`. Without this, a ramp held for
   more than 2,048 ticks (≈10 s at 120 BPM; possible only when the track
   never reaches its next step, e.g. SOM mode switched on mid-ramp) would
   see the 16-bit time difference wrap negative and write `from`.
3. **`seq_setTrackPlayedScene()`** breaks both tracks 5 and 6 when either
   changes Scene (the choke pair switches together there).
4. **`seq_rampConsumeSteps()`** collects all seven flags in one PRIMASK
   section, then ends ramps outside it (it runs every DSP block).
5. **`seq_rampNowQ4()`** computes the fraction with a 64-bit multiply and
   returns 15/16 when a full period has passed since the stamp (a stalled
   external clock holds the ramp just before the next tick).
6. **SQ-6 prototype block not added:** every helper is defined before its
   first use in the section; the SQ-12 forward declarations cover the uses
   earlier in the file. `seq_rampTrackOn()` also guards the track range.
7. **Extra static assert:** `sizeof(seq_rampCtx_t) == 4`.
8. **`sequencer.h` comments:** the `seq_drainPendingAutomation()` and
   `seq_restoreAutomatedParameters()` declaration comments gained an S081 P2
   paragraph; `seq_rampService()` has a full contract block.
9. **SQ-2:** `seq_conditionRepeatPasses()` sits above the
   `seq_conditionEval()` comment pair (not between the comment and the
   function).

**Writes only on change (user decision, 2026-10-14, +128 B approved):**
plan §7 says "writes happen only when the rounded value changes"; the
schedule's SQ-8 wrote every running ramp each pass because an 8-byte slot
had no room for a last-written value. The user chose to spend the RAM:

- `seq_ramp_t` gains `uint8_t last` after `to` (9 B of fields + 1 B
  halfword padding = 10 B; static assert updated). `seq_ramps[64]` is now
  640 B.
- `seq_rampStartIfPaired()` sets `last = from` (the drain has just written
  A's value).
- `seq_rampService()` computes the value as before and skips the write when
  it equals `last`; otherwise it stores `last` and writes.
- Safe because the held bit keeps the Morph sweep, menu apply and external
  MIDI off a ramp-held parameter, and an R12 menu override cancels the ramp.
  The only remaining direct runtime writers (internal legacy MIDI and the
  supplemental-parameter path) already bypass the priority rule today; with
  per-pass writes a ramp would have overwritten such a write within ~2 ms,
  now it holds until the ramp's value next changes.
- Comment blocks updated: the ramp-state block (RAM 640 + 28 + 1 B), the
  service block (`.c`) and the `seq_rampService()` declaration (`.h`).
- Worst case is unchanged (64 writes per pass when every ramp changes value
  each pass); the CPU measurement still counts that case.

**Known limitations (documented, not changed):**

- A transport reset that runs in TIM3 while a ramp-service pass is in
  progress can let that pass write one more sample after
  `seq_restoreAllAutomation()`; the value then holds until the voice's next
  trigger. The break hook ends the ramp at the next consume.
- B's predicted apply time assumes B's trigger is deferred when it is
  active. If a live hold or echo mark suppresses B's trigger, B's entries
  land at B's nominal start; the ramp then ends there (stepped flag), a
  little before its computed end.

**Host check:** a Python transcription of the `seq_advanceTrackStep()`
switch and `seq_rampNextStep()` agree on next index and repeat increment in
all 49,536 cases (fwd/rev/pip both directions/onc/1fr, length 1..128).

**Gate (`make all` + `link_budget.py`):** clean, no new warnings.

| | Baseline | After P2 (with writes-on-change) | Δ |
|---|---|---|---|
| Flash used | 568,604 | 571,804 | +3,200 |
| Flash headroom | 185,060 | 181,860 | −3,200 |
| ITCM | 4,168 | 4,168 | 0 |
| DTCM statics | 4,472 | 4,472 | 0 |

`arm-none-eabi-size`: text 571,392, data 412, bss 429,104 (DEV config).
The writes-on-change step alone: flash +96 B, bss +128 B (428,976 →
429,104).

**RAM by symbol (`nm -S`), all SRAM1:**

| Symbol | Bytes | Approved |
|---|---|---|
| `seq_ramps` | 640 | 512 + 128 (2026-10-14) |
| `seq_rampCtx` | 28 | 28 |
| `seq_rampBlockDiv` | 1 | 1 |
| `seq_rampNoSlotDrops` (DEV) | 2 | 2 |
| `pat_regions` | 168,448 (16 × 10,528) | +16 |
| `pat_background_region` | 10,528 | +1 |

Total **686 B every build + 2 B DEV** (558 B scheduled + 128 B approved
for writes-on-change). Phase F's `STORAGE_SRAM_MANIFEST.md` table must use
`seq_ramps[64]` (10 B) = 640 B. Correction for Phase F:
the background region is **10,528 B** (10,527 after S080 + 1), not 10,527.

**Remaining:** Phase E hardware matrix and CPU measurement (user), Phase F
documentation.

**Image:** `make img` → `build/LXRV2_lxr02.img` (571,804 B payload,
including writes-on-change), built 2026-10-14 from this tree, ready for the
Phase E matrix.
