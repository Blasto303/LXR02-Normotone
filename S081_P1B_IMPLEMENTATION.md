# S081 P1B — Live Record Fix Implementation Schedule

Session 081, part 1B (2026-10-13), branch `dev-ph6-cleanup`.

This schedule fixes review items 1–28 of "Code review — P1 implementation" in
`S081_P1_IMPLEMENTATION.md`. Item 29 (FX realign on non-resetting Scene
changes) is a pending user decision and is not scheduled here.

**Line numbers** are the P1 working tree as reviewed on 2026-10-13 (P1 code
uncommitted on top of `a6c96a0`). Each entry gives `file:line`, the action
(**ADD** / **REMOVE** / **MODIFY**), a code sketch where the shape matters,
and a comment block to place next to the change as documentation-in-place.
Where a function is declared in a header, the same comment block (or its
first paragraph) goes above the declaration.

---

## Implementation log (P1B)

Executed against `dev-ph6-cleanup`, on top of the committed P1 work
(`d21e245`). Each entry records what landed; every change carries the adjacent
comment block from its schedule step. `make all` is the per-phase gate. The
final build reports the Flash headroom from `tools/link_budget.py`.

### Phase F1 — done (2026-10-13)

- **SQ-1** muted-rtk fired bit now also requires bit 15.
- **SQ-2** `seq_loopShiftPosition()` body moved to `seq_shiftPositionQ8()`
  (returns the step-advance count); `seq_loopShiftPosition()` is a whole-tick
  wrapper.
- **SQ-3** `seq_projectStep()` replaced by `seq_placeFromSnapshot()` +
  `seq_place_t`.
- **SQ-4** `seq_track_time_t.nowUs`, captured inside the PRIMASK section.
- **SQ-5** `seq_quantiseTick(tick, frac8)` master-grid only; `trk`/`off`
  return unchanged.
- **SQ-6/SQ-7** `seq_echoDeadline[7]`; `seq_suppressStepOnce(track, step,
  aheadTicks)` sets it under PRIMASK; the echo block expires a stale mark before
  testing it.
- **SQ-8/SQ-9** `seq_trackShuffleMuteMask`; `seq_cancelPendingTrigger(track,
  step)` mutes only the matching deferral; `seq_fireDeferred()` queues the
  automation then returns if muted; a fresh deferral clears the mute bit.
- **SQ-10** `seq_sceneEpoch` + `seq_getSceneEpoch()`, bumped at the three Scene
  sites only.
- **SQ-11** the early deferral flush runs only when the step queues automation.
- **SQ-12** TIM3-shared live-record state is `volatile`; `seq_takeRecBoundary()`
  reads the head once.

LiveRecord was adapted minimally (a shim in `liveRec_place()` plus updated call
signatures) so F1 builds; F3 replaces the shim.

Gate: `make all` clean.

### Phase F2 — done (2026-10-13)

- **PD-1** `pat_recordStep()` stores velocity 100 and fill 100 as "absent".
- **PT-1** `PAT_TRACE_STAGE_LIVEREC_DROP = 'W'`.
- **FX-1** `effects_storeValue7()` (WIDE8 inverse).
- **FX-2** `effects_erasedLanes[16]` replaces the two combined accumulators.
- **FX-3** `effects_takeErasedLocks(uint8_t *step, uint16_t *laneMask)` returns
  one step at a time.

Gate: `make all` clean.

---

**Authority:**

- `S081_P1_LIVE_RECORD.md` (revision 11), especially §5.8, §7, §8, §9, §10,
  §19;
- `S081_P1_IMPLEMENTATION.md` (revision 4 plus review);
- binding invariants in `MEMORY.md` (looper, automation priority, RAM
  policy).

**Build gate after every phase:** `make all` clean, then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

---

### Phase F3 — done (2026-10-13)

- **LR-1** `liveRec_activePatternLocked()` is true only while writing and T != A.
- **LR-2** the drain erase filter now gets `liveRec_erase` (was `liveRec_dirty`).
- **LR-3/4/5** roll state keyed to `liveRec_rollTrack`; span writer uses it and
  carries `fil`; position/Scene breaks end the roll through `liveRec_endRoll()`.
- **LR-6** `liveRec_commitHit(track, pl, note, noteIsDefault, vel, roll, setFil)`
  (ahead → `seq_suppressStepOnce`, current → `seq_cancelPendingTrigger`).
- **LR-7** `liveRec_triggerIn()` rewritten: slot-type check, live capture with
  `liveRec_frac8()`, master-grid/trk/off delta, `seq_placeFromSnapshot()`,
  `liveRec_placeFlam()` for quantized/un-quantized flams, roll classify.
- **LR-8** `liveRec_eraseSeqHeld(track, held)` with a held-count + captured track.
- **LR-9** `liveRec_service()` split into a Scene epoch (new take) and a position
  epoch (capture state only); override requested only while writing; erase masks
  re-pushed each pass.
- **LR-10/11** `liveRec_targetOwned()` + the `liveRec_paramIn()` ownership gate.
- **LR-12** `liveRec_writeStep()` carries `noteIsDefault` and returns success.
- **LR-13/14/15** `liveRec_endRoll()` (release fill, plain-trigger fallback) and
  `liveRec_rollRateChange()` re-classification (fast/slow, single->roll promotion).
- **LR-16** Effect values stored through `effects_storeValue7()`.
- **LR-17** erase masks gated by ownership (T-track bits; FX lanes only when T==A).
- **LR-18** DEV-only `liveRec_drop()` at record/ownership/handover/step-end drops.
- **LR-19** FX erase drained one step at a time.
- **LR-20** header: `liveRec_eraseSeqHeld(track, held)` and
  `liveRec_eraseReleaseHeld()`.

Gate: `make all` clean.

### Phase F4 — done (2026-10-13)

- **BH-1/BH-2** the erase router implements the per-mode press/release table
  (SEQ: active track in VOICE/STEP, FX in FX, consumed elsewhere; VOICE: track in
  the four modes; PLAY: plain toggle).
- **BH-3..BH-6** SHIFT/CLEAR hold flags set/cleared before early exits; SHIFT/CLEAR
  LEDs lit during erase; `buttonHandler_liveEraseEnd()` is the single exit
  (restores the REC LED, restarts the automation search); `handleModeButtons()`
  calls `liveRec_eraseReleaseHeld()`.
- **BH-7** the looper record-off runs after `seq_loopEngage()` and only when the
  loop engaged.
- **BH-8** `bh_overlayNoteDefault` threaded through the overlay reports.

Gate: `make all` clean.

### Phase F5 — done (2026-10-13)

- **MN-1** `menu_cellClearTarget(page, position)` split out; `menu_knobClearTarget()`
  is the pot wrapper.
- **MN-2** `menu_encoderChangeParameter()` records via `menu_liveRecRecordCell`.
- **MN-3** `menu_liveRecRecordCell(page, position)` with the Effect-Morph-view and
  STEP-view-differs-from-A exclusions.
- **MN-4** the rtk display/edit seed reads `liveRec_recordTrack()`.
- **MN-5** knob erase no longer drops the Pattern-wide underline.

Gate: `make all` clean. Flash headroom 186,172 B (190,532 B at P1 end).

### Notes / deviations

- LR-18 ring-full drops are counted in the Sequencer ring push, not LiveRecord,
  so they are absent from `liveRec_drops`.
- LR-13 release fill uses the placement delay (0..99, clamped to 1..100).
- LR-20 partial: the `note_is_default` header comment already read as honoured;
  the `liveRec_activePatternLocked()` header comment was left as P1 text.
- F6 host tests and F7 documentation are not run here; F7 is prose only.

---

## 0. Item map

| Item | Short title | Changes |
|---|---|---|
| 1 | STEP lock only while writing into T ≠ A | LR-1 |
| 2 | Erase filter gets the erase bitmap | LR-2 |
| 3 | Roll state keyed to the rolled track | LR-3, LR-4, LR-5 |
| 4 | Muted rtk fired bit includes bit 15 | SQ-1 |
| 5 | Placement from live state; shuffle; run-mode next step; quantized flam | SQ-2, SQ-3, SQ-4, SQ-5, LR-6, LR-7 |
| 6 / 25 | Echo marks only ahead; deadline expiry | SQ-6, SQ-7, LR-6 |
| 7 | Cancel keeps the deferred step's automation | SQ-8, SQ-9, LR-6 |
| 8 | Erase router per-mode table | BH-1, BH-2 |
| 9 | SEQ erase uses the active track | LR-8, BH-1, BH-2 |
| 10 | Transport start/stop is not a take boundary | SQ-10, LR-9 |
| 11 | Ownership in the automation and trigger paths | LR-10, LR-11, LR-7 |
| 12 | SHIFT/CLEAR hold flags and LEDs | BH-3, BH-4, BH-5, BH-6 |
| 13 | Looper record-off only after a real engage | BH-7 |
| 14 | Encoder edits recorded | MN-1, MN-2, MN-3 |
| 15 | Overlay `def` note; velocity/fill "absent" | BH-8, BH-9, LR-12, PD-1 |
| 16 | Roll release fill, plain-trigger fallback, rate switches, span `fil` | LR-13, LR-14, LR-15, LR-4 |
| 17 | Sub-tick placement | SQ-4, LR-7 |
| 18 | WIDE8 Effect values stored as 7-bit | FX-1, LR-16 |
| 19 | `scl`/`shf` viewed == A; Effect Morph view excluded | MN-3 |
| 20 | rtk edit seed | MN-4 |
| 21 | Erase masks gated by ownership | LR-17 |
| 22 | Target override only while writing | LR-9 |
| 23 | Deferred flush only when the step queues automation | SQ-11 |
| 24 | ISR-shared state `volatile` | SQ-12 |
| 26 | DEV-only drop counters and trace stage | PT-1, LR-18 |
| 27 | Knob-erase underline restarted at erase end | MN-5, BH-6 |
| 28 | FX erase per-step lane masks | FX-2, FX-3, LR-19 |

Review correction: the review's item 18 said `fxm` stores `m / 2` against an
`effect_expand7Linear()` expansion with a rounding mismatch. That is wrong.
`effect_expand7Linear()` is `v < 127 ? v * 2 : 255` (`EffectsManager.c:237`),
so `m / 2` (255 → 127) is its exact inverse. Only WIDE8 descriptor rows with
their own `expand7` need the new inverse (FX-1).

---

## Phase F1 — Sequencer

**File:** `Core/Sequencer/sequencer.c` (header: `Core/Sequencer/sequencer.h`)

### SQ-1 Muted rtk fired bit requires a stored trigger (item 4) — MODIFY `sequencer.c:2633-2637`

```c
if (!process_step && seq_recWriting && track == seq_recTrack) {
    pat_step_specials_t rsp = pat_readStepSpecials(
        seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);
    rec_fired = (uint8_t)(pat_isStepActive(track,
                              (uint8_t)seq_stepIndex[track],
                              seq_perTrackPattern[track]) &&
                          seq_conditionPassesPure(&rsp, track));
}
```

```
/*
 * Would-have-fired for a muted recording track (S081 F-D, P1B item 4).
 *
 * What: on a muted rtk, the ring's fired bit is set only when the step holds
 * a trigger (bit 15) AND its condition would pass, evaluated without side
 * effects (seq_conditionPassesPure). Why: F-D counts a muted step as fired
 * only if it would have fired. Without the bit-15 test every empty step of a
 * muted rtk counted as fired, and LATCH re-stamped own-Instrument values onto
 * every step of the track. Inputs: the step's address entry and specials,
 * seq_recWriting, seq_recTrack. Output: rec_fired for the ring push (C-9a).
 * Context: TIM3. Affiliates: seq_conditionEval(), the ring push below,
 * liveRec_stepEnd().
 */
```

### SQ-2 Signed-Q8 position shift with an advance count (item 5) — ADD `seq_shiftPositionQ8()`; MODIFY `seq_loopShiftPosition()` at `sequencer.c:3589`

The body of `seq_loopShiftPosition()` moves into `seq_shiftPositionQ8()`.
That function takes a signed Q8 delta, returns the signed number of step
advances it applied (the `steps` value it already computes), and mutates the
image exactly as today. `seq_loopShiftPosition()` becomes a one-line wrapper
`(void)seq_shiftPositionQ8(track, pos, ticks * 256)`, which is numerically
identical for the looper (binding S080 invariant: no looper behaviour
change).

```c
static int32_t seq_shiftPositionQ8(uint8_t track, seq_loop_pos_t *pos,
                                   int32_t deltaQ8);
```

```
/*
 * Move a track position image by a signed Q8.8 tick delta (S080 P2, S081 P1B).
 *
 * What: adds deltaQ8 (1/256 tick units; negative = rewind) to the image's
 * DDA accumulator and walks the step cursor by the resulting number of step
 * advances under the track's play mode (fwd/rev wrap, pip 2 x len cycle with
 * direction bit, rnd fresh draw, onc/1fr clamp with the stopped bit). Returns
 * the signed number of step advances applied (0 = same step). Why: the
 * looper needs whole-tick shifts (seq_loopShiftPosition wraps this with
 * ticks * 256, numerically unchanged). Live-record placement needs sub-tick
 * shifts and to know whether a target step is behind, current or ahead of
 * the playhead. Uses the current effective scale and length for the whole
 * span (accepted S080 approximation). Inputs: track 0..6, position image,
 * signed Q8 delta. Outputs: image updated; return = step advances.
 * Context: foreground only (64-bit maths). Accessors: seq_loopShiftPosition()
 * (looper), seq_placeFromSnapshot() (live record). Affiliates:
 * seq_advanceTrackStep() (the per-step semantics mirrored here),
 * stepScale_ticksQ8().
 */
```

`seq_loop_pos_t` (`sequencer.c:3564`) moves above `seq_captureTrackTime()`
(or to the top of the looper section), so the live-record helpers can use
it. It stays file-static.

### SQ-3 Live-state placement — ADD `seq_placeFromSnapshot()`; REMOVE `seq_projectStep()` (`sequencer.c:3765`, `sequencer.h:451`) (item 5)

```c
typedef struct {
    uint8_t  step;      /* step index the hit lands on */
    uint8_t  delay;     /* micro-timing 0..99 */
    int8_t   rel;       /* -1 behind the current step, 0 current, +1 ahead */
    uint16_t ahead;     /* ticks from capture to the target step start (rel > 0) */
} seq_place_t;

uint8_t seq_placeFromSnapshot(uint8_t track, const seq_track_time_t *snap,
                              int32_t deltaQ8, uint8_t snapToStep,
                              seq_place_t *out);
```

Algorithm:

1. Build `pos = {snap->stepIndex, snap->accumulator, snap->playState}`.
   Return 0 if `stepIndex < 0` (pre-start).
2. If the play mode is `rnd` (3) and `deltaQ8 > 0`, set `deltaQ8 = 0`
   (backward only, plan §8.3).
3. Call `steps = seq_shiftPositionQ8(track, &pos, deltaQ8)`, then
   `off = pos.accum` (Q8 progress into the landed step).
4. **Shuffle.** With `sh = (snap->shuffle * 24 / 256) * 256` and an odd
   `pos.step` and `sh > 0`: if `off < sh`, the hit precedes this odd step's
   played start. Shift by `-(off + 1)` to land at the end of the previous
   step (`steps -= 1`); its delay becomes 99 (clamped: the true distance
   exceeds one interval). Otherwise `off -= sh`.
5. **`trk` snap** (`snapToStep`): if `2 * off >= interval`, advance to the
   next step with `seq_shiftPositionQ8(track, &pos, interval - off)`
   (`steps += 1`, `off = 0`); else `off = 0`.
6. `delay = (off * 100 + interval / 2) / interval`. If `delay >= 100`,
   advance to the next step (same shift, `steps += 1`) with `delay = 0`. The
   next step is the run mode's next, not `+1`.
7. `rel = sign(steps)`. `ahead` = whole ticks from capture to the landed
   step's start (`(deltaQ8 - off_into_step) >> 8`, clamped at 0) when
   `rel > 0`.

```
/*
 * Place a captured hit on a track's live step grid (S081 §8.3, P1B item 5).
 *
 * What: starting from the PRIMASK snapshot of the track's live cursor,
 * accumulator and play state taken when the hit sounded, shifts the image by
 * deltaQ8 (the quantised or sub-tick offset from the capture tick) and
 * reports the step the hit lands on, the micro-timing delay (0..99) measured
 * from that step's PLAYED start (shuffle deferral removed on odd steps), and
 * whether the step is behind, current or ahead of the playhead. snapToStep
 * rounds to the nearest step start of the track's own grid (qnt trk). Why:
 * the P1 code mapped ticks through the canonical master-clock position
 * (tick*256/current interval), which differs from the playing step as soon
 * as the track's scale or length changed during playback (menu edit, Morph,
 * scale/length automation). Hits then landed on the wrong step. Placement
 * must follow the track that is actually playing. The delay also has to be
 * relative to the played start, because playback re-adds the shuffle
 * deferral to a micro-timed odd step. Inputs: track, snapshot, signed Q8
 * delta, snap flag. Outputs: *out (step, delay, rel, ahead); returns 0 when
 * the track has not started (cursor -1). Context: foreground. Accessors:
 * liveRec_triggerIn(), liveRec_placeFlam(), liveRec_rollRelease().
 * Affiliates: seq_shiftPositionQ8(), seq_captureTrackTime(), the shuffle
 * deferral in seq_advanceTrackStep().
 */
```

Note for P2: `S081_P2_LOOP_WHILE_RECORD.md` §3.3/§4 name `seq_projectStep()`.
The loop branch belongs in `seq_placeFromSnapshot()` instead. When a track is
looped, the shift must wrap through its loop window.

### SQ-4 Capture records the µs time of the capture — MODIFY `seq_captureTrackTime()` at `sequencer.c:3733`; MODIFY `seq_track_time_t` at `sequencer.h:~420-433` (items 5, 17)

Add `uint32_t nowUs;` to `seq_track_time_t`, and inside the existing PRIMASK
section `out->nowUs = timebase_tim2Now();`.

```
/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11, P1B 17).
 *
 * What: captures, in one PRIMASK section: elapsed master tick, the last PPQ
 * tick's µs stamp and period, the µs time of this capture (nowUs), the
 * track's cursor, Q8.8 accumulator, effective interval/length/shuffle,
 * play mode/state and played Scene. Why: placement needs a consistent
 * image of TIM3-owned state, and the sub-tick fraction
 * (nowUs - stampUs) / periodUs must be measured against the same tick the
 * cursor belongs to. Reading the µs clock inside the section guarantees
 * that. Inputs: track 0..6. Output: *out. Context: foreground.
 * Accessors: liveRec_triggerIn(), liveRec_rollRelease(). Affiliates:
 * seq_placeFromSnapshot(), timebase_tim2Now(), seq_processSchedulerTick()
 * (writes seq_ppqStampUs/PeriodUs).
 */
```

### SQ-5 Master-grid quantiser only — MODIFY `seq_quantiseTick()` at `sequencer.c:3808`; MODIFY `sequencer.h:453` (item 5)

REMOVE the `QUANT_TRACK` branch (`trk` now snaps through
`seq_placeFromSnapshot(..., snapToStep = 1)`). The `track` parameter is
dropped: `uint32_t seq_quantiseTick(uint32_t tick, uint8_t frac8);`. Rounding
uses the sub-tick fraction: the half test is
`rem * 256 + frac8 >= q * 128`. `QUANT_TRACK` and `NO_QUANTISATION` return
`tick` unchanged.

```
/*
 * Quantise a capture to the master-clock grid (S081 §8.3, P1B item 5).
 *
 * What: for qnt 1/8, 1/16, 1/32, 1/64 (48/24/12/6 ticks) returns the
 * nearest grid tick to (tick + frac8/256); for off and trk returns tick
 * unchanged. Why: master-grid quantise is the same for every track; the
 * track-grid snap (trk) must follow the track's live step starts, so it
 * lives in seq_placeFromSnapshot(). The sub-tick fraction makes the
 * half-way decision exact. Inputs: capture tick, sub-tick fraction 0..255.
 * Output: grid tick. Context: any. Accessors: liveRec_triggerIn().
 * Affiliates: seq_quantisation, seq_loopEventAnchor() (same grid).
 */
```

### SQ-6 Echo deadline state — ADD `seq_echoDeadline[NUM_TRACKS]` after `sequencer.c:833` (items 6, 25)

```c
static volatile uint16_t seq_echoDeadline[NUM_TRACKS];
```

```
/*
 * Echo-mark deadline per track (S081 §8.6, P1B items 6/25).
 *
 * What: low 16 bits of the master tick after which a pending echo mark
 * (seq_echoStep[track]) is void. Set with the mark by
 * seq_suppressStepOnce(); TIM3 clears an expired mark before testing it.
 * Why: a mark must only silence the one replay it was set for. If the
 * playhead never reaches the marked step in that pass (run-mode jump,
 * once-mode park, play-mode or length change, rnd), a stale mark would
 * otherwise silence a later, legitimately recorded hit. A deadline covers
 * every such case without wiring each event (it replaces the unwired C-10
 * play-mode expiry). Inputs: written by seq_suppressStepOnce (foreground,
 * PRIMASK). Output: read in seq_advanceTrackStep (TIM3). RAM: 14 B SRAM1.
 * Affiliates: seq_echoStep[], seq_elapsedPpqTicks.
 */
```

### SQ-7 Mark with a deadline; expire before consuming — MODIFY `seq_suppressStepOnce()` at `sequencer.c:894` and `sequencer.h:446`; MODIFY the echo block at `sequencer.c:2689-2700` (items 6, 25)

New signature: `void seq_suppressStepOnce(uint8_t track, uint8_t step, uint16_t aheadTicks);`

```c
void seq_suppressStepOnce(uint8_t track, uint8_t step, uint16_t aheadTicks)
{
    uint16_t interval;
    uint32_t primask;

    if (track >= NUM_TRACKS)
        return;
    interval = (uint16_t)(stepScale_ticksQ8(seq_effectiveTrackScale[track]) >> 8u);
    primask = __get_PRIMASK();
    __disable_irq();
    seq_echoStep[track] = (int8_t)step;
    seq_echoDeadline[track] =
        (uint16_t)(seq_elapsedPpqTicks + aheadTicks + interval + 1u);
    __set_PRIMASK(primask);
}
```

```
/*
 * Mark one stored trigger to be skipped on its next replay (S081 §8.6).
 *
 * What: records the step and a deadline (capture tick + ticks until the
 * step + one step interval + 1) under PRIMASK, so TIM3 never sees a step
 * without its deadline. Why: a hit written onto a step the playhead has
 * not reached in this pass would otherwise sound twice: live, then stored.
 * Only steps AHEAD of the playhead are marked (LR-6); a step behind it is
 * not replayed in this pass, and marking it silenced the new hit on its
 * next pass. Inputs: track, step, aheadTicks from seq_placeFromSnapshot().
 * Outputs: seq_echoStep, seq_echoDeadline. Accessors: liveRec_commitHit().
 * Affiliates: the echo block in seq_advanceTrackStep().
 */
```

Echo block (`:2689-2700`), before the equality test:

```c
if (seq_echoStep[track] >= 0 &&
    (int16_t)((uint16_t)seq_elapsedPpqTicks - seq_echoDeadline[track]) > 0)
    seq_echoStep[track] = -1;          /* expired: never silences a later hit */
```

```
/*
 * Expire a stale echo mark before testing it (S081 §8.6, P1B items 6/25).
 *
 * What: a mark whose deadline has passed is dropped without effect. Then,
 * as before, a mark equal to this step is consumed and the stored trigger
 * skipped (fired still 1). Why: covers every path where the playhead never
 * reaches the mark (run-mode jumps, length/play-mode changes, once park).
 * Context: TIM3. Affiliates: seq_suppressStepOnce(), seq_echoDeadline[].
 */
```

### SQ-8 Mute flag for a cancelled deferral — ADD after `sequencer.c:253` (item 7)

```c
static volatile uint8_t seq_trackShuffleMuteMask;
```

```
/*
 * Deferred-trigger mute mask (S081 §8.6, P1B item 7).
 *
 * What: bit N set means track N's pending deferred trigger fires WITHOUT
 * sounding: seq_fireDeferred() still queues that step's automation (and the
 * step reset is skipped, because the live hit already reset the voice), but
 * no voice trigger and no roll start. Why: since A1-3, a deferred step's
 * automation is queued only when its trigger fires. Dropping the deferral
 * (the P1 cancel) lost the step's automation for the pass. Inputs: set by
 * seq_cancelPendingTrigger() under PRIMASK; cleared by seq_fireDeferred()
 * and whenever a new deferral is stored. Outputs: read by seq_fireDeferred
 * (TIM3). RAM: 1 B. Affiliates: seq_trackShufflePending[],
 * seq_trackShuffleStep[].
 */
```

### SQ-9 Cancel only the matching deferral, keep its automation — MODIFY `seq_cancelPendingTrigger()` at `sequencer.c:924`, `sequencer.h:448`; MODIFY `seq_fireDeferred()` at `sequencer.c:4518`; MODIFY the deferral store at `sequencer.c:~2790-2805` (item 7)

New signature: `void seq_cancelPendingTrigger(uint8_t track, uint8_t step);`

```c
void seq_cancelPendingTrigger(uint8_t track, uint8_t step)
{
    uint32_t primask;

    if (track >= NUM_TRACKS)
        return;
    primask = __get_PRIMASK();
    __disable_irq();
    if (seq_trackShufflePending[track] && seq_trackShuffleStep[track] == step)
        seq_trackShuffleMuteMask |= (uint8_t)(1u << track);
    __set_PRIMASK(primask);
}
```

`seq_fireDeferred()`:

- read and clear the mute bit first;
- always `seq_queueStepAutomations(track, step)`;
- if muted, return (no `seq_rollBegin()`, no `seq_triggerVoice()`).

The deferral store (`total_delay > 0`) clears the track's mute bit.

```
/*
 * Silence a pending deferred trigger that a live hit replaced
 * (S081 §8.6, P1B item 7).
 *
 * What: if the track's pending deferral belongs to `step`, marks it muted.
 * The deferral still fires on time, so the step's automation is queued in
 * order, but it does not trigger the voice. A deferral that belongs to a
 * different (earlier) step is left alone. Why: the P1 version cleared the
 * pending flag. That dropped the replaced step's automation (now queued at
 * fire time, A1-3), and it also cancelled an earlier step's still-pending
 * hit. Inputs: track, the step the live hit was written to. Outputs:
 * seq_trackShuffleMuteMask. Context: foreground (PRIMASK).
 * Accessors: liveRec_commitHit() (rel == 0). Affiliates: seq_fireDeferred(),
 * seq_trackShuffleStep[].
 */
```

```
/*
 * Fire one track's deferred trigger with its automation (S081 §5.8, P1B 7).
 *
 * What: queues the deferred step's automation entries first; if the
 * deferral was muted by seq_cancelPendingTrigger(), stops there (the live
 * hit already sounded and reset the voice); otherwise starts the roll or
 * fires the plain hit with reset = 1. Context: TIM3. Accessors:
 * seq_processDeferredTriggers() (expiry), seq_advanceTrackStep() (flush).
 * Affiliates: seq_queueStepAutomations(), seq_rollBegin(),
 * seq_trackShuffleMuteMask.
 */
```

### SQ-10 Scene epoch separate from the position epoch — ADD `seq_sceneEpoch` after `sequencer.c:839`; ADD `seq_getSceneEpoch()` near `:944` and in `sequencer.h` after `:454`; MODIFY the bump sites (item 10)

```c
static volatile uint8_t seq_sceneEpoch;
uint8_t seq_getSceneEpoch(void);
```

`seq_sceneEpoch++` beside the existing `seq_playbackEpoch++` at:

- `:1584` (`seq_setTrackPlayedScene()`);
- `:1851` (`seq_selectActivePattern()`);
- `:1951` (`seq_alignActivePatternToScene()`).

The playback epoch keeps every site, including `seq_setRunning()` (`:4946`),
`seq_setStepIndexToStart()` (`:5206`) and the realign sites (`:3444`,
`:3520`).

```
/*
 * Scene epoch (S081 §9.2 / F11, P1B item 10).
 *
 * What: incremented only when the Pattern a track plays from changes: a
 * direct/queued/chain Scene change, a per-track Scene reassignment, a
 * Scene/Bank load alignment. seq_playbackEpoch is still incremented at
 * every playhead discontinuity (those plus transport start/stop, PC reset
 * and realign). Why: LiveRecord starts a new take (latches reset, target
 * re-evaluated) only on a Scene change (F11). A transport start or stop is
 * only a position break: it must not wipe latches set while stopped, nor
 * end a held live erase ("playback starts with live erase enabled"). Two
 * counters let LiveRecord tell the cases apart. Inputs: the three Scene
 * sites. Output: seq_getSceneEpoch(). RAM: 1 B.
 * Accessors: liveRec_service(). Affiliates: seq_playbackEpoch.
 */
```

### SQ-11 Early deferral flush only when the step queues automation — MODIFY `sequencer.c:2839-2840` (item 23)

```c
if (seq_trackShufflePending[track] && !deferred_this_step &&
    pat_stepAutomationCount(seq_perTrackPattern[track], track,
                            (uint8_t)seq_stepIndex[track]) != 0u)
    seq_fireDeferred(track);
```

```
/*
 * Keep entry order without cutting delays short (S081 A1-3c, P1B item 23).
 *
 * What: a deferral still pending from an earlier step is fired early only
 * when this step is about to queue automation entries. Otherwise it keeps
 * its own timing and fires at expiry. Why: the early fire exists to keep the
 * earlier step's entries ahead of this step's entries in the pending queue.
 * Firing it at every step start shortened long delays (shuffle + up to 99 %
 * micro-timing) to the step boundary even when the next step was empty.
 * pat_stepAutomationCount() only reads the address entry and block header,
 * so it is TIM3-safe like seq_queueStepAutomations(). Context: TIM3.
 * Affiliates: seq_fireDeferred(), seq_queueStepAutomations().
 */
```

The flush inside the trigger branch (`:2773-2778`) stays: when this step's
own trigger fires, the earlier deferral must fire first in any case.

### SQ-12 ISR-shared live-record state `volatile` — MODIFY `sequencer.c:828-839` and `:252-253` (item 24)

Declare as `volatile`:

- `seq_recRing[16]`, `seq_recRingHead`, `seq_recRingTail`;
- `seq_recTrack`, `seq_recWriting`;
- `seq_echoStep[]`;
- `seq_eraseSpecialsMask`, `seq_eraseFullMask`, `seq_liveHoldMask`;
- `seq_ppqStampUs`, `seq_ppqPeriodUs`;
- `seq_playbackEpoch`;
- `seq_trackShuffleStep[]`, `seq_trackShuffleRoll[]`.

In `seq_takeRecBoundary()` (`:857`), read `seq_recRingHead` once into a
local before the empty test.

```
/*
 * TIM3 <-> foreground live-record state (S081 C-1..C-7, P1B item 24).
 *
 * What: every byte or word written by one context and read by the other is
 * volatile. The boundary ring is single-producer (TIM3 writes the entry,
 * then the head) and single-consumer (foreground reads the head once, then
 * the entry, then advances the tail). Why: with LTO the foreground drain can
 * be inlined into a loop; without volatile the compiler may keep
 * seq_recRingHead in a register across iterations, or load the entry before
 * the head check. Cortex-M7 byte and halfword accesses are single-copy
 * atomic, and volatile ordering is sufficient on this single core (the
 * seq_pending_automation precedent). Affiliates: seq_takeRecBoundary(), the
 * ring push in seq_advanceTrackStep(), liveRec_service().
 */
```

---

## Phase F2 — PatternData, PatternTrace, EffectsManager

### PD-1 Velocity 100 and fill 100 stored as "absent" — MODIFY `PatternData.c:861-862` and `:875-876` (item 15)

```c
if (spec->velocity == 100u) {
    flags &= (uint8_t)~PAT_SPECIAL_VEL_BIT;
} else {
    sp.velocity = spec->velocity;
    flags |= PAT_SPECIAL_VEL_BIT;
}
...
sp.roll = spec->roll;
flags |= PAT_SPECIAL_ROLL_BIT;
if (spec->fill >= 100u) {
    flags &= (uint8_t)~PAT_SPECIAL_FILL_BIT;
} else {
    sp.fill = spec->fill;
    flags |= PAT_SPECIAL_FILL_BIT;
}
```

```
/*
 * Defaults are stored as "absent" (S081 §8.4, P1B item 15).
 *
 * What: velocity 100 and fill 100 are the specials' defaults
 * (pat_readStepSpecials). They clear their flag bits instead of storing a
 * byte, exactly as the STEP-page setters do (pat_setStepFill clears at
 * 100). Why: keeps recorded blocks as small as hand-edited ones, and lets a
 * plain 100-velocity hit with the default note reach the cheap path
 * (bit 15 only, no pool block). Roll carry-over compares resolved values,
 * so an absent fill (100) still matches a stored fill of 100. Affiliates:
 * pat_readStepSpecials(), seq_rollCarries().
 */
```

### PT-1 Live-record drop trace stage — MODIFY `PatternTrace.h:52` (item 26)

Add `PAT_TRACE_STAGE_LIVEREC_DROP = 'W'` after `PAT_TRACE_STAGE_DIRECT_RETAIN`
(fix the trailing comma), and extend the enum comment.

```
/*
 * W (S081 P1B): a live-record write, clear or capture was dropped. flags =
 * cause (1 handover/target closed, 2 ownership, 3 staging full, 4 service
 * rejected, 5 ring full, 6 capacity); value = (track << 8) | step. DEV
 * builds only, like every PatternTrace record.
 */
```

`tools/decode_devlogs.py` needs the new stage letter at close (Phase F7).

### FX-1 Inverse of a WIDE8 row's expand7 — ADD `effects_storeValue7()` near `effects_getParameter()` in `EffectsManager.c`; declare in `EffectsManager.h` (item 18)

```c
uint8_t effects_storeValue7(uint8_t scene_index, uint8_t local, uint8_t value);
```

- Non-WIDE8 row: return `min(value, 127)`.
- WIDE8 row: return the smallest `v` in 0..127 with
  `descriptor->expand7(v) >= value`, or 127. That is a 128-iteration search,
  foreground only, run at step end for latched Effect targets.

```
/*
 * Convert a retained Effect value to its 7-bit Pattern form (S081 §9.2,
 * P1B item 18).
 *
 * What: for a WIDE8 row (0..255 retained, expand7 on playback) returns the
 * 7-bit value whose expand7() first reaches `value`. For any other row it
 * returns the value clamped to 127. Why: Pattern automation stores 7 bits
 * (packed with & 0x7F). A WIDE8 value above 127 written raw would wrap and
 * play back as a different value. Inputs: Scene (for its Effect type),
 * local parameter, retained value. Output: 0..127. Context: foreground.
 * Accessors: liveRec_targetValue7(). Affiliates: effects_getParameter(),
 * effect_param_descriptor_t.expand7, EFFECT_PARAM_FLAG_WIDE8.
 */
```

### FX-2 Per-step erased-lane masks — MODIFY `EffectsManager.c:202-208` (item 28)

REMOVE `effects_erasedSteps` and `effects_erasedLanesAcc`. ADD
`static uint16_t effects_erasedLanes[EFFECT_SEQ_STEP_COUNT];` (16 × 16 bits,
32 B; net +28 B).

```
/*
 * Locks excluded by live erase, per FX step (S081 §10.4, P1B item 28).
 *
 * What: effects_erasedLanes[s] is the set of lanes whose locks were kept
 * from sounding on FX step s since the last drain. Why: one combined
 * (stepMask, laneMask) pair cleared the union of lanes on every step seen in
 * a drain window, erasing locks that were never passed. Per-step masks make
 * the retained clear exact. Inputs: effects_lockClearLayers(). Outputs:
 * effects_takeErasedLocks(). RAM: 32 B. Affiliates: liveRec_service(),
 * effects_clearSeqLanes().
 */
```

MODIFY `effects_lockClearLayers()` (`:790-791`, `:804-805`) to
`effects_erasedLanes[step & 15u] |= lane bit`. Clear the array in the reset
at `:1312-1313`.

### FX-3 Drain one step at a time — MODIFY `effects_takeErasedLocks()` at `EffectsManager.c:1337-1352` and `EffectsManager.h:444` (item 28)

New signature: `uint8_t effects_takeErasedLocks(uint8_t *step, uint16_t *laneMask);`
It returns the lowest step with a non-zero mask, clears that entry, and
returns 1; it returns 0 when none remain.

```
/*
 * Take one FX step's erased lanes (S081 §10.4, P1B item 28).
 *
 * What: returns the lowest FX step with pending erased lanes and its lane
 * mask, and clears that entry. Returns 0 when nothing is pending. Why: the
 * caller clears the retained locks step by step with
 * effects_clearSeqLanes(scene, 1 << step, lanes), including the linked-Scene
 * fan-out, without touching lanes that were never passed. Context:
 * foreground. Accessors: liveRec_service() (loops until 0). Affiliates:
 * effects_erasedLanes[], effects_clearSeqLanes().
 */
```

---

## Phase F3 — LiveRecord

**Files:** `Core/Sequencer/LiveRecord.c`, `Core/Sequencer/LiveRecord.h`

New state, after `LiveRecord.c:94`. RAM about 10 B, plus DEV-only counters:

```c
static uint8_t liveRec_lastSceneEpoch;      /* LR-9 */
static uint8_t liveRec_rollTrack;           /* LR-3 */
static uint8_t liveRec_rollHits;            /* LR-13: hits since ROLL_START */
static uint8_t liveRec_rollPendingIdx;      /* LR-14: fast roll to start at next hit */
static uint8_t liveRec_eraseSeqCount;       /* LR-8: held SEQ buttons */
static uint8_t liveRec_eraseSeqTrack;       /* LR-8: track of the SEQ erase */
#if DEV_MODE_LOGGING
static uint16_t liveRec_drops[7];           /* LR-18, indexed by trace cause */
#endif
```

### LR-1 STEP lock only while writing into T ≠ A — MODIFY `liveRec_activePatternLocked()` at `LiveRecord.c:573-578` (item 1)

```c
return (uint8_t)(liveRec_patternWriting() &&
                 liveRec_target != scene_getActiveIndex());
```

```
/*
 * Is the active Scene's Pattern closed for editing? (S081 Q13, P1B item 1)
 *
 * What: true only while live record or erase is writing (armed and the
 * transport running) AND the recording target T = P(rtk) differs from the
 * active Scene A. Only then does the Pattern Stack Service hold the target
 * override, which refuses edits of A. Why: the P1 version returned true
 * whenever P(rtk) != A, including with REC off. Selecting a track that plays
 * another Scene (S077 per-track playback) then silently locked STEP editing
 * and step toggles. Inputs: mode, transport, liveRec_target, the active
 * Scene. Output: lock flag. Accessors: menu_parseKnobDelta(),
 * menu_parseEncoder(), buttonHandler_setRemoveStep(). Affiliates:
 * patSvc_requestTarget() (LR-9).
 */
```

### LR-2 Erase filter gets the erase bitmap — MODIFY `LiveRecord.c:236` (item 2)

```c
seq_setAutomationFilter(liveRec_latch, liveRec_erase, liveRec_rtk);
```

```
/*
 * Register the drain filters (S081 §9.3, §10.2, P1B item 2).
 *
 * What: the latch bitmap silences every track's playback automation of a
 * latched target (its menu value sounds). The erase bitmap silences rtk's
 * entries of knob-erased targets before they play. Why: P1 passed the dirty
 * bitmap as the erase filter, so erased automation kept playing until it
 * was removed, and OVERDUB muted rtk's automation of every adjusted target.
 * Affiliates: seq_drainPendingAutomation(), liveRec_eraseParam(),
 * liveRec_paramIn().
 */
```

### LR-3 Roll track state — MODIFY `liveRec_triggerIn()` fast-roll branch at `LiveRecord.c:654-662` (item 3)

Set `liveRec_rollTrack = track; liveRec_rollHits = 0u;` alongside
`liveRec_rollHeld = 1u`.

```
/*
 * The rolled track owns the roll state (S081 §8.8, P1B item 3).
 *
 * What: a fast roll records its track in liveRec_rollTrack. Span writes,
 * the live hold and the release use that track, not rtk. Why: overlay rolls
 * fire on any VOICE, independent of the recording track (rtk is the
 * automation target). Keyed to rtk, a roll on another track wrote no span
 * steps, and its live hold was never cleared, so its stored triggers stayed
 * silent. Affiliates: seq_setLiveHold(), liveRec_service() span writer,
 * liveRec_rollRelease().
 */
```

### LR-4 Span writer uses the roll track and carries `fil` — MODIFY `LiveRecord.c:256-260` (items 3, 16)

```c
if (liveRec_rollHeld && b.track == liveRec_rollTrack) {
    liveRec_writeStep(liveRec_rollTrack, prev, liveRec_rollNote, 0u,
                      liveRec_rollVel, 0u, liveRec_rollIndex, 100u,
                      seq_fillActive);
}
```

(`liveRec_writeStep()` gains `noteIsDefault`; see LR-12.)

```
/*
 * Extend a held fast roll over the step that just ended (S081 §8.8 E-4).
 *
 * What: when the rolled track's next ring entry arrives, its previous step
 * has ended under the held roll and is written as an active step with the
 * roll rate and fill 100 (carry-over). fil applies while PERF is held.
 * Why: the steps the roll spans must replay it. Keyed to liveRec_rollTrack
 * (P1B item 3); R10 fil on every step that receives anything (item 16).
 * Affiliates: seq_takeRecBoundary(), liveRec_writeStep().
 */
```

### LR-5 Take and position boundaries clear the roll on its own track — MODIFY `LiveRecord.c:212-215` (item 3; also LR-9)

Replace `seq_setLiveHold(liveRec_rtk, 0u)` with a call to new
`liveRec_endRoll(0u)` (LR-13), which ends the roll on `liveRec_rollTrack`.

### LR-6 Commit by placement result — MODIFY `liveRec_commitHit()` at `LiveRecord.c:590-600` (items 5, 6, 7)

New signature:

```c
static uint8_t liveRec_commitHit(uint8_t track, const seq_place_t *pl,
                                 uint8_t note, uint8_t noteIsDefault,
                                 uint8_t vel, uint8_t roll, uint8_t setFil);
```

Body:

1. `liveRec_writeStep(...)` with `pl->delay`; return 0 if it was refused.
2. If `pl->rel > 0`: `seq_suppressStepOnce(track, pl->step, pl->ahead)`.
3. If `pl->rel == 0`: `seq_cancelPendingTrigger(track, pl->step)`.
4. If `pl->rel < 0`: nothing.

```
/*
 * Write one placed hit and apply the echo rules (S081 §8.4/§8.6, P1B 5/6/7).
 *
 * What: writes the step (trigger ON). Then: a step AHEAD of the playhead
 * is marked to skip its stored trigger once (it would otherwise replay the
 * hit moments later in this pass); the CURRENT step mutes its own pending
 * deferred trigger, if any (the live hit replaced it; its automation is
 * kept); a step BEHIND the playhead needs nothing (not replayed this pass).
 * Why: P1 marked every non-current step, including steps already passed,
 * which silenced the new hit on its next pass; and its cancel dropped the
 * replaced step's automation. Inputs: track, placement (step, delay, rel,
 * ahead), note, velocity, roll, fil. Output: nonzero when the write was
 * accepted. Accessors: liveRec_triggerIn(), liveRec_placeFlam().
 * Affiliates: seq_suppressStepOnce(), seq_cancelPendingTrigger(),
 * patSvc_recordStep().
 */
```

### LR-7 Trigger capture: placement, sub-tick, quantized flam, type check — MODIFY `liveRec_triggerIn()` at `LiveRecord.c:602-691`; REMOVE `liveRec_place()` at `:118-142` (items 5, 11, 17)

Flow:

1. **Gates** (unchanged), plus the slot-type check (item 11).
   `slot = (track >= INSTRUMENT_SLOT_COUNT) ? INSTRUMENT_SLOT_COUNT - 1 : track`.
   If `liveRec_target != scene_getActiveIndex()` and
   `scene_instrumentSlotConst(A, slot)->type !=
   scene_instrumentSlotConst(T, slot)->type`: drop (LR-18 cause 2).
2. **Capture:** `seq_captureTrackTime(track, &snap)`.
   `frac8 = periodUs ? min(255, (timebase_tim2Delta(nowUs, stampUs) * 256) /
   periodUs) : 0`.
3. **Delta:**

   | qnt | `deltaQ8` | `snapToStep` |
   |---|---|---|
   | off | `frac8` | 0 |
   | `trk` | `frac8` | 1 |
   | 1/8..1/64 | `((int32_t)seq_quantiseTick(snap.elapsedTick, frac8) − (int32_t)snap.elapsedTick) * 256` | 0 |

4. **Place:** `seq_placeFromSnapshot(track, &snap, deltaQ8, snapToStep, &pl)`;
   drop if it returns 0.
5. **By kind:**
   - SINGLE: `liveRec_commitHit(..., &pl, ...)`.
   - FLAM_GRACE: store `pl` (step/delay/rel/ahead), the velocity, the
     un-quantized grace `deltaQ8` and the capture tick; `graceValid = 1`.
   - FLAM_MAIN: `liveRec_placeFlam()` (below).
   - ROLL_START / ROLL_HIT: LR-13, LR-14.

ADD `static void liveRec_placeFlam(uint8_t track, const seq_track_time_t *snap, const seq_place_t *mainPl, int32_t mainDeltaQ8, const liveRec_trigger_t *t)`:

- **Quantized** (qnt != off): the grace is placed at `mainDeltaQ8 − 12 × 256`
  from the main's capture snapshot. Write both if the grace step differs
  from the main step; otherwise write the main only (R9).
- **Unquantized:** use the stored grace placement. Write it only if its
  step differs from the main step (Q9).

```
/*
 * Capture one live hit and record it (S081 §8, P1B items 5/11/17).
 *
 * What: gates (LATCH/OVERDUB, running, P1 looper lockout, P(track) == T,
 * same instrument type in A and T); captures the track's live state with
 * the µs time; derives the sub-tick fraction; quantises on the master grid
 * (1/8..1/64) or snaps to the track's own steps (trk); places the hit with
 * seq_placeFromSnapshot() (live cursor, played start after shuffle, run-mode
 * next step); then records it by kind (single, flam, roll). Why: placement
 * must follow the track that is playing, not its canonical master-clock
 * position; the type check keeps an Instrument trigger from being recorded
 * into a Scene whose slot holds a different instrument (plan §7). Inputs:
 * liveRec_trigger_t. Outputs: Pattern writes, echo marks, live hold, LED.
 * Accessors: buttonHandler overlay (E-2), midiParser_noteOn().
 * Affiliates: seq_captureTrackTime(), seq_quantiseTick(),
 * seq_placeFromSnapshot(), liveRec_commitHit(), liveRec_placeFlam().
 */
```

```
/*
 * Record a flam as two steps (S081 R9/Q9, P1B item 5).
 *
 * What: quantised, the main sits on the quantise division and the grace is
 * placed 12 master ticks (1/32) earlier from the same capture, so it lands
 * on the step before with the delay that puts it ahead of the main. If both
 * fall in one step, only the main is written. Un-quantised, each hit keeps
 * its own captured position; again, main only if they share a step. Why:
 * P1 quantised the grace and the main independently; on a quantised grid
 * both snapped to the same point and the grace was always lost. Inputs: main
 * capture snapshot and placement, the stored grace capture. Outputs: one or
 * two writes. Accessors: liveRec_triggerIn() (FLAM_MAIN). Affiliates:
 * seq_placeFromSnapshot(), liveRec_commitHit().
 */
```

### LR-8 SEQ-hold erase uses the active track — MODIFY `liveRec_eraseSeqHeld()` at `LiveRecord.c:749-759` and `LiveRecord.h:111` (item 9)

New signature: `void liveRec_eraseSeqHeld(uint8_t track, uint8_t held);`

- **Press:** `liveRec_eraseSeqCount++`. On the first press, set
  `liveRec_eraseSeqTrack = track` and the specials bit.
- **Release:** `liveRec_eraseSeqCount--`. At 0, clear the bit for
  `liveRec_eraseSeqTrack`.
- Then `liveRec_pushEraseMasks()`.

```
/*
 * SEQ-hold live erase on the active track (S081 §10.2, F7, P1B item 9).
 *
 * What: while any SEQ button is held in VOICE/STEP mode during live erase,
 * the active track's triggers and specials are removed as the playhead
 * passes (TIM3 specials-erase mask); the step's condition is kept where
 * automation exists. The track is captured on the first press and released
 * when the last SEQ button is released, so a track change mid-hold cannot
 * strand a mask bit. Why: F7 names the active track; P1 used rtk, which
 * differs after an rtk override. Inputs: active track (from buttonHandler),
 * held flag. Outputs: liveRec_eraseSpecials, pushed masks. Accessors:
 * buttonHandler erase router (BH-1/BH-2). Affiliates:
 * liveRec_pushEraseMasks(), seq_setEraseMasks().
 */
```

### LR-9 Service: Scene vs position epochs; override only while writing; re-push erase — MODIFY `liveRec_service()` at `LiveRecord.c:193-241` (items 10, 22, 21)

New order:

1. **Scene epoch:** if `seq_getSceneEpoch() != liveRec_lastSceneEpoch`
   (seeded on the first pass): `liveRec_newTake()`.
2. **Position epoch:** if `seq_getPlaybackEpoch()` changed (seeded on the
   first pass), on any epoch change:
   - `memset(liveRec_prevValid, 0)`;
   - `liveRec_endRoll(0u)`;
   - `liveRec_graceValid = 0`.

   Erase gesture state and latches are kept.
3. **Replacement pending** (unchanged): release target, writing 0, filters
   off, return.
4. **Target:** `liveRec_target = seq_getTrackPlayedScene(liveRec_rtk)`.
   - If `liveRec_patternWriting() && liveRec_target != scene_getActiveIndex()`:
     `patSvc_requestTarget()`; else `patSvc_releaseTarget()`.
   - `seq_setRecordTarget(liveRec_rtk, liveRec_patternWriting())`.
   - Filters (LR-2) while armed, else off.
   - `liveRec_pushEraseMasks()` every pass while ERASE (LR-17: ownership
     can change with the Scene).
5. Ring drain (unchanged except LR-4).
6. FX erase drain (LR-19).

```
/*
 * LiveRecord service, one 500 Hz foreground pass (S081 §19.2, P1B 10/21/22).
 *
 * What: (1) a Scene change (seq_getSceneEpoch) starts a new take: latches
 * and dirty bits cleared, origin re-captured. (2) Any playhead break
 * (seq_getPlaybackEpoch: also transport start/stop, PC reset, realign)
 * only ends per-step capture state: the previous ring step, an open roll, a
 * pending flam grace. Latches and held erase gestures survive, so arming or
 * erasing while stopped carries into playback. (3) A filesystem replacement
 * suspends writing. (4) The target override is requested only while
 * writing (armed and running) and T != A, so Pattern AutoSave is not
 * blocked while armed and stopped (F-M). Erase masks are re-pushed each pass
 * through the ownership gate. (5) The boundary ring drives span writes and
 * the step-end tables. (6) FX-sequence erase is cleared step by step. Why:
 * P1 treated every epoch as a take boundary: transport start wiped latches,
 * zeroed held erase masks in the Sequencer while LiveRecord kept stale
 * copies, and the override was held while stopped. Context: foreground,
 * before patSvc_tick(). Affiliates: seq_getSceneEpoch(),
 * seq_getPlaybackEpoch(), patSvc_requestTarget/releaseTarget(),
 * liveRec_pushEraseMasks(), effects_takeErasedLocks().
 */
```

### LR-10 Ownership helper — ADD `liveRec_targetOwned()` after `liveRec_isOwnInstrument()` (`LiveRecord.c:~378`) (item 11)

```c
static uint8_t liveRec_targetOwned(uint16_t target);
```

- **Instrument target** with slot `s`: the track is `s`, except that slot 5
  uses `rtk` when `rtk` is 5 or 6, else 5. Requires
  `seq_getTrackPlayedScene(track) == liveRec_target`, and the slot type in
  A equals the type in T.
- **Scene or Effect target:** requires `liveRec_target == scene_getActiveIndex()`.

```
/*
 * Does a recordable target belong to the recording target Scene T?
 * (S081 §7, P1B item 11)
 *
 * What: an Instrument parameter is owned when the track that plays its slot
 * plays T and the slot holds the same instrument type in the active Scene A
 * (whose image the menu edits) and in T (whose Pattern stores the entry).
 * An Effect or Scene parameter is owned only when T == A, because the
 * edited object is the active Scene's. Why: plan §7 says anything belonging
 * to another Scene is not recorded; P1 recorded Effect and Scene edits into
 * a per-track Scene T and never compared instrument types, so a descriptor
 * index could mean a different parameter on playback. Inputs: target ID.
 * Output: 1 owned / 0 drop. Accessors: liveRec_paramIn(),
 * liveRec_eraseParam(). Affiliates: seq_getTrackPlayedScene(),
 * scene_instrumentSlotConst().
 */
```

### LR-11 `liveRec_paramIn()` ownership gate — MODIFY `LiveRecord.c:717-734` (item 11)

After the mode test: if `!liveRec_targetOwned(target9)`, count a drop
(LR-18 cause 2) and return.

```
/*
 * Mark an adjusted parameter for recording (S081 §9, P1B item 11).
 *
 * What: every recordable gesture (pots, encoder; MIDI CC later) marks its
 * target dirty, and in LATCH also latched, provided the target belongs to
 * the recording target Scene. Why: see liveRec_targetOwned(). The value is
 * not passed: the step-end write reads the current menu value. Inputs:
 * target ID, source. Outputs: liveRec_dirty/latch bits. Accessors:
 * menu_liveRecRecordCell(), midiParser_enterTaggedParameter().
 * Affiliates: liveRec_stepEnd(), liveRec_targetValue7().
 */
```

### LR-12 Write path carries `noteIsDefault` — MODIFY `liveRec_writeStep()` at `LiveRecord.c:145-161`; MODIFY `liveRec_trigger_t` users (item 15)

Add a `noteIsDefault` parameter and set `spec.noteIsDefault = noteIsDefault`.
`liveRec_triggerIn()` passes `t->note_is_default`; span writes pass the
roll's stored flag.

```
/*
 * Submit one recorded step (S081 §8.4, P1B item 15).
 *
 * What: builds the record spec (note or "default note", velocity, timing,
 * roll, fill, fil) and submits it through patSvc_recordStep() to the track's
 * played Scene; on acceptance, marks the recorded-step LED. Why: Q12, an
 * overlay hit with note `def` stores no note special (playback then uses the
 * default note); P1 always stored the resolved MIDI note. Inputs: step
 * fields. Output: nonzero when accepted (else a counted drop, LR-18).
 * Affiliates: pat_recordStep(), seq_markRecordedStep().
 */
```

### LR-13 Roll end with release fill and the plain-trigger fallback — ADD `liveRec_endRoll()`; MODIFY `liveRec_rollRelease()` at `LiveRecord.c:706-714` and the ROLL_HIT branch at `:666-672` (item 16)

`static void liveRec_endRoll(uint8_t writeEnd)`:

- If `writeEnd` and the transport is running:
  - place `now` on `liveRec_rollTrack` (`seq_captureTrackTime()` +
    `seq_placeFromSnapshot()` with `deltaQ8 = frac8`, no snap);
  - `fill = clamp(1..100, delay)`;
  - if `liveRec_rollHits == 0`, rewrite the start step as a plain trigger
    (roll 0);
  - otherwise write the landed step with roll and that fill.
- Always: `seq_setLiveHold(liveRec_rollTrack, 0)`, `liveRec_rollHeld = 0`,
  `liveRec_rollPendingIdx = 0`.

The ROLL_HIT branch increments `liveRec_rollHits` for a fast roll (still not
written per hit). `liveRec_rollRelease()` calls `liveRec_endRoll(1u)`.

```
/*
 * End a recorded fast roll (S081 §8.8, P1B item 16).
 *
 * What: the step containing the release gets the roll with
 * fill = elapsed part of that step (1..100). A roll released before its
 * second hit is rewritten as a plain trigger. The live hold on the rolled
 * track is lifted. Called with writeEnd = 0 at a position break (stop,
 * Scene change, realign), where the playhead has jumped and no end fill can
 * be placed. Why: P1 left every recorded roll at fill 100 and never applied
 * the "released before the second hit" rule. Inputs: liveRec_roll* state,
 * the live capture. Outputs: one write, live hold off. Accessors:
 * liveRec_rollRelease(), liveRec_rollRateChange(), liveRec_service().
 * Affiliates: seq_placeFromSnapshot(), seq_setLiveHold().
 */
```

### LR-14 Rate switches re-classify; slow→fast and single→roll start at the next hit — MODIFY `liveRec_rollRateChange()` at `LiveRecord.c:693-704` and the ROLL_HIT branch (item 16)

| Situation | Action |
|---|---|
| `rollIndex == 0` | `liveRec_endRoll(1u)` |
| roll held (fast), new rate slow (`ticks >= step interval`) | `liveRec_endRoll(1u)`; later ROLL_HITs record as singles |
| roll held, new rate fast | `liveRec_rollIndex = rollIndex` (the step containing the switch takes the new rate at its end, F-C) |
| no roll held, new rate fast | `liveRec_rollPendingIdx = rollIndex`; the next ROLL_HIT is handled as a ROLL_START with that index |
| no roll held, new rate slow | nothing (hits record as singles) |

```
/*
 * Overlay rate switch while a roll is held (S081 §8.8 F-C, R19, P1B 16).
 *
 * What: re-classifies the roll at every switch: a fast roll that becomes
 * slow ends (its later hits record as singles); a roll that becomes fast
 * (from slow, or single -> roll with no hit at the switch) starts as a
 * recorded fast roll at its next hit; fast -> fast changes the rate from
 * the step containing the switch. Why: P1 only updated the index, so a
 * single -> roll switch recorded a fast roll as many single hits, and a
 * fast -> slow switch kept writing roll steps. Inputs: track, new step-roll
 * index (0 = roll ended). Outputs: liveRec_roll* state. Accessors:
 * buttonHandler_overlaySwitchHeld(). Affiliates: liveRec_endRoll(), the
 * ROLL_HIT branch of liveRec_triggerIn().
 */
```

### LR-15 Fast/slow test uses the live interval — MODIFY `LiveRecord.c:643-664` (item 16)

`stepTicks = snap.interval >> 8` stays. A fast-roll start now sets
`liveRec_rollTrack` (LR-3), `liveRec_rollHits = 0` and the note-default flag.
The start step is written through `liveRec_commitHit()` with fill 100.

### LR-16 Effect values stored as 7-bit — MODIFY `liveRec_targetValue7()` at `LiveRecord.c:345-347` (item 18)

```c
if (effectTarget_isEffectId(target)) {
    uint8_t local = effectTarget_local(target);

    return effects_storeValue7(a, local,
        effects_getParameter(a, local, EFFECT_IMAGE_NORMAL));
}
```

(Comment: see FX-1.)

### LR-17 Erase masks gated by ownership — MODIFY `liveRec_pushEraseMasks()` at `LiveRecord.c:97-101` (item 21)

```c
uint8_t owned = 0u;

for (t = 0u; t < NUM_TRACKS; t++)
    if (seq_getTrackPlayedScene(t) == liveRec_target)
        owned |= (uint8_t)(1u << t);
seq_setEraseMasks((uint8_t)(liveRec_eraseSpecials & owned),
                  (uint8_t)(liveRec_eraseFull & owned));
effects_setLiveEraseLanes(
    (liveRec_target == scene_getActiveIndex()) ? liveRec_eraseFxLanes : 0u);
```

```
/*
 * Push the live-erase masks through the ownership gate (S081 §7, §10.2,
 * P1B item 21).
 *
 * What: only tracks that play the target Scene T receive their
 * specials/full erase bits; the FX-sequence erase lanes apply only when
 * T == A. Called on every gesture change and on every service pass, so a
 * Scene change re-gates held erases at once. Why: TIM3 clears bit 15 in the
 * track's played Scene directly. Ungated, a held TRACK erase on a track
 * playing another Scene wrote into a non-target Pattern (and its pool
 * reclaim was then refused). Inputs: gesture masks, seq_getTrackPlayedScene,
 * T, A. Outputs: seq_setEraseMasks(), effects_setLiveEraseLanes().
 * Accessors: liveRec_erase*Held(), liveRec_eraseParam(), liveRec_service().
 */
```

### LR-18 DEV-only drop counting — ADD `liveRec_drop(cause, track, step)` (item 26)

Under `#if DEV_MODE_LOGGING`: `liveRec_drops[cause]++` and
`patternTrace_record(PAT_TRACE_STAGE_LIVEREC_DROP, cause, (track << 8) | step)`.
Without logging it is an empty inline. Call sites:

- a refused `patSvc_recordStep()` (cause 3/4);
- refused `patSvc_writeStepAutomation()`/`patSvc_removeStepAutomation()`
  results in `liveRec_stepEnd()` (cause 4);
- the ownership drops (cause 2);
- writes skipped while `!patSvc_targetOpen(T)` (cause 1).

```
/*
 * Count a dropped live-record action (S081 H-3, P1B item 26).
 *
 * What: in DEV builds, increments liveRec_drops[cause] and records one
 * PatternTrace 'W' event (cause in flags, track/step in value). Compiles to
 * nothing when DEV_MODE_LOGGING is 0. Why: best-effort recording drops by
 * design (handover, ownership, full queues); without a witness, a missing
 * recorded step cannot be told apart from a placement defect.
 * RAM: 14 B DEV-only (logging-allocation policy). Affiliates:
 * patternTrace_record(), decode_devlogs.py.
 */
```

### LR-19 FX erase drained per step — MODIFY `LiveRecord.c:270-277` (item 28)

```c
uint8_t fxStep;
uint16_t lanes;

while (effects_takeErasedLocks(&fxStep, &lanes))
    effects_clearSeqLanes(liveRec_origin, (uint16_t)(1u << fxStep), lanes);
```

(Comment: see FX-3.)

### LR-20 Header — MODIFY `LiveRecord.h`

- `liveRec_eraseSeqHeld(uint8_t track, uint8_t held)` (LR-8).
- Comment updates for `liveRec_activePatternLocked()` (LR-1).
- `liveRec_trigger_t` (`:67-78`) comment: `note_is_default` is now honoured
  (LR-12).
- ADD `void liveRec_eraseReleaseHeld(void);`: clears the SEQ-hold count and
  track, `liveRec_eraseSpecials`, `liveRec_eraseFull` and the FX-SEQ lanes
  (not the knob-erase lanes or the erase bitmap), then
  `liveRec_pushEraseMasks()`. Comment block:

```
/*
 * Release every held erase gesture (S081 §10.1, P1B item 8).
 *
 * What: drops the SEQ-hold (specials), TRACK-hold (full) and FX-SEQ-hold
 * erase gestures at once, keeping erase mode and the knob-erase targets.
 * Why: a mode change re-maps SEQ and VOICE buttons, so their held gestures
 * must stop and must not be left set waiting for releases that no longer
 * route to erase. Accessors: handleModeButtons(). Affiliates:
 * liveRec_pushEraseMasks().
 */
```

---

## Phase F4 — buttonHandler

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

### BH-1 Erase router press, per-mode table — MODIFY `buttonHandler.c:2520-2547` (items 8, 9)

```c
if (liveRec_mode() == LIVEREC_MODE_ERASE) {
    int8_t eraseSeq = btn_to_seq(buttonNr);
    int8_t eraseVoice = btn_to_voice(buttonNr);
    uint8_t mode = bh_state.selectButtonMode;

    if (eraseSeq >= 0) {
        if (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP)
            liveRec_eraseSeqHeld(menu_getActiveVoice(), 1u);
        else if (mode == SELECT_MODE_FX)
            liveRec_eraseFxSeqHeld(1u);
        return;                                   /* PERF, LOAD_SAVE, MENU, SOM_GEN: consumed */
    }
    if (eraseVoice >= 0) {
        if (mode == SELECT_MODE_VOICE || mode == SELECT_MODE_STEP ||
            mode == SELECT_MODE_PERF  || mode == SELECT_MODE_FX)
            liveRec_eraseTrackHeld((uint8_t)eraseVoice, 1u);
        return;                                   /* other modes: consumed */
    }
    if (buttonNr == BUT_START_STOP) { /* unchanged: plain toggle */ }
}
```

```
/*
 * Live erase button router, press edge (S081 §10.2, P1B items 8/9).
 *
 * What: while live erase is enabled, implements the per-mode table:
 *  - SEQ in VOICE/STEP erases the ACTIVE track's triggers/specials while
 *    held; SEQ in FX erases the FX sequence; SEQ in PERF (Scene buttons),
 *    Load/Save, global settings and SOM_GEN is consumed with no action;
 *  - VOICE in VOICE/STEP/PERF/FX erases that whole track while held, and is
 *    consumed elsewhere;
 *  - PLAY is a plain transport toggle (never the SHIFT+PLAY chain chord).
 * Why: P1 erased in every mode, so PERF Scene buttons removed triggers, and
 * it used rtk instead of the active track. Inputs: button, mode, active
 * voice. Outputs: LiveRecord erase gestures or nothing. Accessors:
 * processPress(). Affiliates: liveRec_eraseSeqHeld(),
 * liveRec_eraseFxSeqHeld(), liveRec_eraseTrackHeld(), BH-2 (release edge).
 */
```

### BH-2 Erase router release, same table — MODIFY `buttonHandler.c:2918-2940` (items 8, 9)

Mirror BH-1 for release edges:

- `liveRec_eraseSeqHeld(menu_getActiveVoice(), 0u)` in VOICE/STEP (LiveRecord
  ignores the track on release, LR-8);
- `liveRec_eraseFxSeqHeld(0u)` in FX;
- `liveRec_eraseTrackHeld(track, 0u)` in the four modes;
- every other SEQ/VOICE release while ERASE is consumed.

**A mode change while a SEQ/VOICE erase is held:** on any MODE press while
ERASE, `handleModeButtons()` (top of the function, `:1531` region, after the
latched-menu closes) calls new `liveRec_eraseReleaseHeld()` (LR-20). Plan
§10.1: "any held SEQ/TRACK erase stops". Their later physical releases then
find nothing to clear.

```
/*
 * A mode change ends held erase gestures (S081 §10.1, P1B item 8).
 *
 * What: SEQ and TRACK buttons change meaning with the mode, so their held
 * erase gestures stop at a MODE press; erase itself (SHIFT/CLEAR) and the
 * knob-erase targets stay enabled. Affiliates: liveRec_eraseReleaseHeld().
 */
```

### BH-3 SHIFT press sets the hold flag first — MODIFY `buttonHandler.c:2777-2802` (item 12)

Move `bh_shiftHeld = 1u;` to the first statement of `case BUT_SHIFT:` (before
the latched-menu close at `:2790`). Keep the `liveRec_eraseBegin()` test where
it is. When erase begins here, also `led_setValue(1u, LED_COPY)`.

```
/*
 * SHIFT hold flag tracks the physical key (S081 §10.1, P1B item 12).
 *
 * What: bh_shiftHeld is set on every SHIFT press, including the press that
 * closes a latched menu (whose release is consumed). Why: the live-erase
 * chord ends only when BOTH SHIFT and CLEAR are released; a flag skipped on
 * an early return left erase unable to end, or able to end with SHIFT still
 * held. Affiliates: BH-4, BH-5, liveRec_eraseBegin/End().
 */
```

### BH-4 SHIFT release clears the hold flag first and keeps the LEDs during erase — MODIFY `buttonHandler.c:3104-3130` (item 12)

- Move `bh_shiftHeld = 0u;` to the first statement of `case BUT_SHIFT:` in
  `processRelease()` (before the `bh_shiftReleaseConsume` test at `:3118`).
- Then, if `liveRec_mode() == LIVEREC_MODE_ERASE`:
  - if `bh_clearHeld`, keep the SHIFT LED lit (skip `led_setValue(0, LED_SHIFT)`);
  - otherwise run the erase end (BH-6).

The rest of the SHIFT-release mode logic is unchanged.

```
/*
 * SHIFT release during live erase (S081 §10.1, P1B item 12).
 *
 * What: the hold flag is cleared before any early exit. While erase is on
 * and CLEAR is still held, the SHIFT LED stays lit (spec: SHIFT and CLEAR
 * lit while either key is held); when CLEAR is also free, erase ends.
 * Affiliates: BH-3, BH-5, BH-6.
 */
```

### BH-5 COPY/CLEAR press and release LEDs during erase — MODIFY `buttonHandler.c:2677-2690` and `:2884-2893` (item 12)

- **Press:** when `liveRec_eraseBegin()` runs, light both `LED_SHIFT` and
  `LED_COPY`.
- **Release** while ERASE:
  - if `bh_shiftHeld`, keep `LED_COPY` lit;
  - otherwise run the erase end (BH-6).

```
/*
 * CLEAR key during live erase (S081 §10.1, P1B item 12).
 *
 * What: entering erase lights SHIFT and CLEAR; releasing CLEAR keeps erase
 * (and both LEDs) while SHIFT is held; releasing the last of the two ends
 * erase. Why: the spec requires both LEDs lit while either key is held; P1
 * never lit CLEAR and turned SHIFT off on its release. Affiliates: BH-3,
 * BH-4, BH-6, copyClear_copyPressed() (bypassed while armed).
 */
```

### BH-6 Erase end helper — ADD `buttonHandler_liveEraseEnd()` near the overlay helpers (items 12, 27)

```c
static void buttonHandler_liveEraseEnd(void)
{
    liveRec_eraseEnd();
    led_setValue(0u, LED_SHIFT);
    led_setValue(0u, LED_COPY);
    led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC);
    menu_patternContentChanged();
}
```

It replaces the direct `liveRec_eraseEnd()` calls at `:2890` and `:3128`.

```
/*
 * End live erase from the key-release paths (S081 §10.1, P1B items 12/27).
 *
 * What: ends erase (LiveRecord restores the prior LATCH/OFF state), turns
 * SHIFT and CLEAR off, restores the REC LED to the record state, and asks
 * Menu to restart the automation search. Why: one exit point for both key
 * orders. Knob erase removes automation progressively and only from rtk,
 * so the underline must be recomputed by the search at the end rather than
 * dropped at the first turn (MN-5). Affiliates: liveRec_eraseEnd(),
 * menu_patternContentChanged().
 */
```

### BH-7 Looper record-off only after a real engage — MODIFY `buttonHandler.c:2051-2064` (item 13)

Move the block after the `seq_loopEngage()` call:

```c
bh_loopBarModifier = 0u;
bh_loopTrackMask = seq_loopEngage(trackMask, division, bh_loopBarModifier);
if (seq_loopActive && liveRec_armed()) {
    liveRec_forceOff();
    led_setValue(0u, LED_REC);
}
```

```
/*
 * Looper engage turns record off only if the loop engaged (S081 P1 interim,
 * P1B item 13).
 *
 * What: after seq_loopEngage(), if the loop is now active and live record
 * or erase is armed, record is forced off and the REC LED cleared. Why:
 * seq_loopEngage() does nothing while the transport is stopped (a SELECT
 * press then only lights its LED); P1 disarmed record before the call, so a
 * SELECT press while stopped lost the armed state. This restores the S080
 * order (engage, then test seq_loopActive). P2 removes the lockout (L-2).
 * Affiliates: seq_loopEngage(), liveRec_forceOff().
 */
```

### BH-8 Overlay `def` note flag — ADD `static uint8_t bh_overlayNoteDefault;` after `buttonHandler.c:377`; MODIFY `buttonHandler_overlayTrigger()` at `:929-950` and `buttonHandler_overlaySwitchHeld()` at `:1060-1070` (item 15)

- Set `bh_overlayNoteDefault = (param note == 0)` before the note is
  resolved.
- `buttonHandler_overlayReport()` gains a `noteIsDefault` argument and
  copies it to `t.note_is_default` (`:925`).
- The overlayTick reports pass `bh_overlayNoteDefault`.

```
/*
 * Overlay note is "default" (S081 Q12, P1B item 15).
 *
 * What: 1 when the active overlay's note parameter is 0 (`def`): the hit
 * plays the track's MIDI note, and a recording stores no note special.
 * Why: Q12. Without the flag LiveRecord could not tell `def` from an
 * explicit note. RAM: 1 B. Affiliates: buttonHandler_overlayReport(),
 * liveRec_triggerIn().
 */
```

### BH-9 single→roll and roll→roll report the index, not a start — MODIFY `buttonHandler.c:1098-1110` (item 16)

No change in shape: `liveRec_rollRateChange(bh_overlayTrack, rollIdx[idx])` is
already called for both branches. LiveRecord now handles "no roll held"
(LR-14). Update the comment to say the next timer hit starts the recorded
roll.

---

## Phase F5 — Menu

**File:** `Core/Menu/menu.c`

### MN-1 Position-based recordable-target resolver — MODIFY `menu_knobClearTarget()` at `menu.c:13946-13959` (item 14)

Split it:

```c
static uint8_t menu_cellClearTarget(uint8_t active_page, uint8_t position,
                                    cc_pot_target_t *out);
static uint8_t menu_knobClearTarget(uint8_t knobNr, cc_pot_target_t *out)
{
    /* position = knobNr + second_page, exactly as before */
    return menu_cellClearTarget(active_page, (uint8_t)(knobNr + second_page), out);
}
```

The body of today's function moves into `menu_cellClearTarget()`. It uses
`position` where it used `knobNr + second_page`: the `menu_resolveCell()` call
at `:13959` and the SEQ-page `menu_seqCellToTrackTarget()` call.

```
/*
 * Resolve a menu cell position to its Pattern target and FX lane
 * (S075 pot clear; S081 P1B item 14).
 *
 * What: for the cell at (page, position) returns the Pattern automation
 * target (Instrument descriptor, Scene target incl. 7dc, STEP len/scl/shf,
 * PERF Morph, Effect row, Effect Morph) and the FX lane, or 0 when the cell
 * is not automatable. menu_knobClearTarget() is the pot wrapper (position =
 * knob + visible half). Why: pot clear, live-record recording and knob
 * erase need the same mapping, and the encoder edits a cell by position
 * rather than by knob. Inputs: page, cell position. Output: *out.
 * Accessors: menu_knobClearTarget(), menu_liveRecRecordCell(). Affiliates:
 * menu_resolveCell(), menu_sceneSettingAutomationTarget(),
 * menu_seqCellToTrackTarget().
 */
```

### MN-2 Encoder edits are recorded — MODIFY `menu_encoderChangeParameter()` at `menu.c:12684` (item 14)

After `(void)menu_cellCommitValue(&cell, value);`:
`menu_liveRecRecordCell(activePage, activeParameter);`.

```
/*
 * Record an encoder edit (S081 §9.1 F-1b, P1B item 14).
 *
 * What: after the ordinary commit, a recordable encoder edit marks its
 * target in LiveRecord, exactly like a pot edit. Clamped edits count. Why:
 * F-1b was not hooked in P1, so encoder edits were never recorded.
 * Affiliates: menu_liveRecRecordCell(), menu_cellCommitValue().
 */
```

### MN-3 Recordable test by cell; `scl`/`shf` only while the viewed Pattern is A; Effect Morph view excluded — MODIFY `menu_liveRecRecordKnob()` at `menu.c:14056` → `menu_liveRecRecordCell(page, position)`; MODIFY the caller at `:14273` (items 14, 19)

The caller at `:14273` passes `(activePage, knobNr + second_page)` (the same
`second_page` rule as `menu_knobClearTarget()`). Add to the recordable test:

```c
if (menu_activePage == EFFECT_PAGE && menuEffects_showMorph())
    return;
if (menu_activePage == SEQ_PAGE &&
    menu_getViewedPattern() != scene_getActiveIndex())
    return;
```

and resolve through `menu_cellClearTarget(page, position, &target)`.

```
/*
 * Mark a recordable edit for live record (S081 §9.1, F-1a/b/c,
 * P1B items 14/19).
 *
 * What: in LATCH or OVERDUB, an edit of a cell that resolves to a Pattern
 * target is passed to liveRec_paramIn(), unless:
 *  - the page is PERF, RECORDING, global settings, Load or Save;
 *  - a Morph view is active (VOICE SHIFT view, STEP Morph view, Effect-page
 *    Morph view);
 *  - the target is a track length;
 *  - it is a STEP-page cell while the viewed Pattern is not the active
 *    Scene.
 * Why: Morph-view edits change endpoints, not the sounding value (Q16), so
 * the Effect view must be excluded like the others. The STEP page shows the
 * viewed Pattern, which with FOLLOW off can differ from the playing active
 * Scene; recording its scl/shf would store a value the user does not hear
 * (F-1c). Inputs: page, cell position. Output: liveRec_paramIn() call.
 * Accessors: menu_parseKnobDelta(), menu_encoderChangeParameter().
 * Affiliates: menu_cellClearTarget(), menuEffects_showMorph(),
 * menu_patternTrackMorphViewActive().
 */
```

### MN-4 rtk edit seed follows LiveRecord — MODIFY `menu_cellDisplayValue()` at `menu.c:4841-4842` (item 20)

```c
if (cell->kind == MENU_CELL_STATIC) {
    if (cell->static_param == PAR_AUTOM_TRACK)
        return liveRec_recordTrack();
    return menu_getParameterDisplayValue(cell->static_param);
}
```

```
/*
 * rtk value comes from LiveRecord (S081 §11, P1B item 20).
 *
 * What: the rtk cell's value (edit seed and display) is LiveRecord's
 * recording track. parameter_values[PAR_AUTOM_TRACK] is only the commit
 * scratch byte. Why: rtk follows every active-track change inside
 * LiveRecord; seeding edits from the stale parameter byte made the first
 * detent jump. Affiliates: liveRec_recordTrack(), menu_getS080StaticName()
 * (PAR_AUTOM_TRACK display), menu_parseGlobalParam() (commit).
 */
```

### MN-5 Knob erase does not drop the Pattern-wide underline — MODIFY `menu.c:14159` (item 27)

REMOVE `menu_automationTargetCleared(eraseTarget.pattern_target);`. The
search restarts at erase end (BH-6). Update the comment block above the
branch (`:14153`) to say so.

```
/*
 * Knob erase selects a target, it does not prove removal (S081 §10.2,
 * P1B item 27).
 *
 * What: the turn marks the target for progressive removal from rtk only;
 * the automation underline is left as is and recomputed when erase ends.
 * Why: menu_automationTargetCleared() is for whole-Scene removals
 * (MEMORY.md); live erase is per-track and progressive, so dropping the
 * underline at the first turn showed "no automation" while entries
 * remained. Affiliates: liveRec_eraseParam(), buttonHandler_liveEraseEnd().
 */
```

---

## Phase F6 — RAM, build and verification

### RAM

All normal SRAM1, static.

| Item | Bytes | Owner |
|---|---|---|
| `seq_echoDeadline[7]` (u16) | +14 | Sequencer |
| `seq_trackShuffleMuteMask` | +1 | Sequencer |
| `seq_sceneEpoch` | +1 | Sequencer |
| `effects_erasedLanes[16]` (u16) replacing two u16 | +28 net | EffectsManager |
| LiveRecord: scene epoch, roll track, roll hits, pending index, SEQ-erase count and track | +6 | LiveRecord |
| `bh_overlayNoteDefault` | +1 | buttonHandler |
| **Every build** | **≈ +51** | |
| `liveRec_drops[7]` (u16), DEV_MODE_LOGGING only | +14 | LiveRecord |

This is within the user's approval, which includes follow-ons. Stack: the
new `seq_place_t` (5 B) and the extra `nowUs` in `seq_track_time_t` are
locals.

### Verification

- **Host test:** `seq_placeFromSnapshot()` against a simulated DDA for
  fwd/rev/pip/onc and every 96-PPQ scale position. Cover:
  - shuffle on odd steps;
  - qnt off/1/16/`trk`;
  - a scale change mid-run (actual ≠ canonical);
  - a delay that rounds to 100;
  - placements behind, on and ahead of the current step.
- **Hardware, recording:**
  1. per-track playback with REC off: STEP edits and toggles work (item 1);
  2. knob erase: erased automation never sounds (2);
  3. an overlay roll on a non-rtk track: spans written, the track's stored
     triggers return after release (3);
  4. muted rtk with LATCH: no entries on empty steps (4);
  5. scale changed during playback, then record: hits on the playing steps
     (5);
  6. backward quantize and flam grace: the hit sounds on the next pass (6);
  7. a hit on a micro-timed current step: that step's automation still
     plays (7);
  8. PERF erase: Scene buttons do nothing (8);
  9. SEQ erase after an rtk override: the active track is erased (9);
  10. SHIFT+CLEAR+TRACK held, then PLAY: erase runs from the start (10);
  11. T ≠ A: Effect edits not recorded (11);
  12. SHIFT/CLEAR either order and either release order: LEDs and end (12);
  13. SELECT while stopped and armed: record stays armed (13).
- **Hardware, should-fix:**
  - encoder edits recorded (14);
  - `def` overlay stores no note (15);
  - roll release fill, quick-release plain trigger, single→roll, fast↔slow
    (16);
  - sub-tick un-quantized placement at slow tempo (17);
  - FX-sequence erase across steps with different lanes (28).

### Phase F7 — documentation at close (with P1 Phase I)

- `PATTERN_DYNAMIC_STACK.md`: live-record placement (live-state shift,
  shuffle, echo deadline, deferred mute); §12.16 stage `W`.
- `MODULE_INTERCHANGE_SPEC.md`:
  - `seq_placeFromSnapshot()`, `seq_shiftPositionQ8()`,
    `seq_getSceneEpoch()`;
  - the new signatures of `seq_suppressStepOnce()`,
    `seq_cancelPendingTrigger()`, `seq_quantiseTick()`,
    `liveRec_eraseSeqHeld()` and `effects_takeErasedLocks()`;
  - `effects_storeValue7()`.
- `STORAGE_SRAM_MANIFEST.md`: the RAM table above.
- `tools/decode_devlogs.py`: stage `W`.
- `S081_P2_LOOP_WHILE_RECORD.md`: replace `seq_projectStep()` with
  `seq_placeFromSnapshot()` in §3.3 and §4.

---

## Order of work

| Phase | Content | Build gate |
|---|---|---|
| F1 | Sequencer SQ-1..SQ-12 | callers updated for the new signatures; LiveRecord adapted in the same build to `seq_placeFromSnapshot()` (a minimal LR-7 so it compiles) |
| F2 | PD-1, PT-1, FX-1..FX-3 | clean build |
| F3 | LiveRecord LR-1..LR-20 | clean build |
| F4 | buttonHandler BH-1..BH-9 | clean build |
| F5 | Menu MN-1..MN-5 | clean build |
| F6 | Host test, then hardware matrix | — |
| F7 | Documentation, with P1 Phase I | — |

F1 and F3 meet in `liveRec_triggerIn()`. If F1 builds alone, keep the P1
`liveRec_place()` call compiling through a temporary shim, and remove the
shim in F3.

---

## Code review — P1B implementation (2026-10-13)

**Scope:** the uncommitted P1B working tree on top of `d21e245`, covering
`sequencer.c/.h`, `LiveRecord.c/.h`, `buttonHandler.c`, `menu.c`,
`EffectsManager.c/.h`, `PatternData.c` and `PatternTrace.h`. Each step was
checked against SQ-1..SQ-12, PD-1, PT-1, FX-1..FX-3, LR-1..LR-20, BH-1..BH-9
and MN-1..MN-5 above, and against plan §5.8, §7, §8, §9 and §10.

**Verdict:** most of the schedule landed as written, and the P1 must-fix
items 1–13 are resolved in code. **Five defects block the F6 hardware
matrix:**

- two are in live erase (FX lanes);
- one is in menu recording;
- one is in roll recording;
- one is in the SHIFT/CLEAR chord.

All five are small and local. Nine minor items follow them; none blocks F6.

### Must-fix

**1. FX erase lanes 8–15 are lost — `LiveRecord.c:177-179` (LR-17, item 28).**
`liveRec_pushEraseMasks()` passes the lane mask to
`effects_setLiveEraseLanes()` through a `(uint8_t)` cast. The 16-bit mask is
cut to 8 bits:

- an FX-mode SEQ hold (`0xFFFF`) erases only lanes 0–7;
- a knob erase on a lane at 8 or above does nothing.

Fix: change the cast to `(uint16_t)`.

**2. Knob-erase FX lanes are wiped by an FX SEQ release or a mode change —
`LiveRecord.c:1156`, `:1187` (LR-20, §10.2).**
The knob erase (`:1115`, `|=`) and the FX-mode SEQ hold (`:1156`, `= held ?
0xFFFF : 0`) share `liveRec_eraseFxLanes`. As a result:

- releasing an FX SEQ hold sets the mask to 0;
- `liveRec_eraseReleaseHeld()` (`:1187`, called on every MODE press) also sets
  it to 0.

Both drop knob-erase lanes that §10.2 keeps "until erase ends". LR-20 said
explicitly "not the knob-erase lanes".

Fix:

- add `static uint16_t liveRec_eraseKnobFxLanes` (+2 B);
- `eraseParam` sets bits in it;
- the FX SEQ hold keeps its own `0xFFFF`/0 term;
- push `knob | hold`;
- `eraseReleaseHeld()` and `eraseFxSeqHeld(0)` clear only the hold term;
- `eraseEnd()` and `forceOff()` clear both.

**3. The recordable-page exclusion tests the wrong variable —
`menu.c:14132` (MN-3, items 14/19).**
`menu_liveRecRecordCell(page, position)` compares `page` with
`PERFORMANCE_PAGE`, `RECORDING_PAGE`, `MENU_MIDI_PAGE`, `LOAD_PAGE` and
`SAVE_PAGE`. Both callers pass the sub-page index
(`(menuIndex & MASK_PAGE) >> PAGE_SHIFT`), not the top-level page, so:

- on PERF the exclusion no longer fires, and `Nvm` Morph edits that resolve
  through `menu_cellClearTarget()` are recorded (spec: PERF is never
  recorded);
- on any page whose sub-page index happens to equal 7, 8, 9, 10 or 14, edits
  are silently not recorded.

P1 tested `menu_activePage`. Fix: test `menu_activePage` again; `page` stays
only as the `menu_cellClearTarget()` argument.

**4. A roll end is written at the wrong place and timing —
`LiveRecord.c:850-890` (LR-13, item 16).**

- **Quick release (`liveRec_rollHits == 0`):** the plain trigger is written at
  the *release* step with the *release* delay. LR-13 says "rewrite the start
  step". The start step keeps its roll (fill 100), and a stray trigger lands
  where the button was released.
- **Normal release:** the end step is written with `timing = pl.delay`, which
  micro-times the step by the release offset.
  - A step the roll spanned into starts at its boundary, so its timing must
    be 0.
  - When the release lands on the start step, the start's own delay must be
    kept.
  - Today it overwrites a correctly placed start with the release offset.

The start step is already kept in `liveRec_rollStep` (`:1004`), but the start
delay is not stored. This is a gap in my LR-13 sketch, not only in the
implementation.

Fix:

- add `liveRec_rollStartDelay` (+1 B), set alongside `liveRec_rollStep`;
- `rollHits == 0`: write `liveRec_rollStep` with `liveRec_rollStartDelay`,
  roll 0, fill 0;
- otherwise: timing is `(pl.step == liveRec_rollStep) ? liveRec_rollStartDelay
  : 0`, with roll and fill as now.

**5. Live erase can stay on with both keys released —
`buttonHandler.c:2711-2721`, `:3217-3231` (BH-4, item 12, §10.3).**

1. In `BUT_REC` press, the SHIFT+REC menu test (`:2711`) comes before the
   ERASE test (`:2721`). While erase is on, SHIFT is normally held, so REC
   opens the SHIFT+REC menu. §10.3 says "Erase enabled, then REC held →
   Nothing".
2. In `BUT_SHIFT` release, the consume break (`:3220`) and the latched-menu
   break (`:3223`) both run *before* the ERASE block (`:3231`).

Confirmed sequence:

1. SHIFT+CLEAR turns erase on.
2. REC opens the menu.
3. CLEAR is released; SHIFT is still held, so erase is kept.
4. SHIFT is released; the menu is open, so the handler breaks.

Erase is now on with no key held and both LEDs lit. Only another CLEAR press
and release gets out.

The same happens if SHIFT+REC opened the menu first and CLEAR was then
pressed with SHIFT still held, because the COPY press calls
`liveRec_eraseBegin()` without testing for the menu.

Fix:

- move the ERASE test above the SHIFT+REC test in `BUT_REC` press;
- in `BUT_SHIFT` release, run the erase-end test (`!bh_clearHeld` →
  `buttonHandler_liveEraseEnd()`) before the consume and menu breaks;
- the breaks keep their LED handling otherwise.

### Minor

6. **`trk` flam grace is placed from the unsnapped time —
   `LiveRecord.c:820-824`.** The main hit is snapped to the track's step
   (`snapToStep = 1`), but the grace uses `mainDeltaQ8 - 12*256` with the
   raw delta, so it lands 1/32 before the *hit*, not before the snapped
   main. Fix: derive the grace from the main's snapped position (step start
   + delay) or pass the snapped delta.
7. **Roll fill is measured from the step start.** When the release lands on
   the start step of a roll that began late in it, the fill includes the part
   before the roll started. This disappears once item 4 is fixed if the fill
   on the start step is computed as `delay − startDelay`. Otherwise accept
   it and note it in §8.8.
8. **Un-quantized flam grace uses a stale placement.** `liveRec_gracePl`
   (`rel`, `ahead`) is from the grace's own capture, but it is committed at
   main time. A grace that was ahead gets an echo deadline up to 1/32 too
   late, and one that was current gets a cancel that no longer matches. Both
   are harmless today, because the deadline overshoots and the cancel is
   step-matched. Re-place it at commit or document it.
9. **Dead grace state.** `liveRec_graceDeltaQ8` and `liveRec_graceTick`
   (`:79-80`, written at `:1016-1017`) are never read; LTO removes them.
   Delete them.
10. **Erase SEQ count across a mode change.** `liveRec_eraseReleaseHeld()`
    zeroes the count, but the old buttons' later releases still route to
    erase in the new mode:
    - a VOICE/STEP release decrements a count that a new SEQ press started;
    - an FX-mode release clears an FX SEQ hold started after the change.

    The FX SEQ hold is also not counted, so releasing one of two held SEQ
    buttons in FX ends it. Low impact; a per-button held mask would fix both.
11. **The erase masks are published as two separate writes.**
    `seq_setEraseMasks()` stores specials and full one after the other.
    TIM3 can see one updated and not the other for one tick. Benign, as
    noted before.
12. **Scene-epoch bump site.** The bump sits in
    `seq_realignTrackToMasterClock()` (`sequencer.c:3684`) instead of
    `seq_setTrackPlayedScene()`. It is equivalent while the only caller is
    the buttonHandler Scene path. Accepted; if a second caller of
    `seq_setTrackPlayedScene()` appears, move the bump there.
13. **Ring-full drops are not counted anywhere.** The implementation log says
    they are counted in the Sequencer ring push, but the push
    (`sequencer.c:3028-3037`) drops silently. Either add a DEV-only counter
    there (or bump `liveRec_drops` cause 5 from the push), or correct the
    log.
14. **LR-20 header comment.** The `liveRec_activePatternLocked()` comment in
    `LiveRecord.h:99` still describes P1. Update it to "writing and T ≠ A".

### Verified correct

- **Sequencer:**
  - SQ-1: the muted-rtk fired bit needs `pat_isStepActive`.
  - SQ-2: `seq_shiftPositionQ8()` returns the advance count; the loop
    wrapper uses ×256.
  - SQ-3: `seq_placeFromSnapshot()` handles:
    - the shuffle rule (odd step with `off < sh` → previous step, delay 99);
    - the `trk` snap;
    - delay ≥ 100 → next step;
    - `rel`/`ahead`.
  - SQ-4: `nowUs` is captured inside PRIMASK.
  - SQ-5: master-grid quantiser.
  - SQ-6/SQ-7: the echo deadline is set under PRIMASK and a stale mark
    expires before it is tested.
  - SQ-8/SQ-9:
    - only a matching deferral is muted;
    - `seq_fireDeferred()` queues the automation before the mute return;
    - a fresh deferral clears the mute bit.
  - SQ-10: separate Scene and position epochs.
  - SQ-11: the early flush is gated by the automation count.
  - SQ-12: `volatile` on the TIM3-shared state; the ring head is read once.
- **PatternData / trace / Effects:**
  - PD-1: velocity 100 and fill 100 are stored as absent.
  - PT-1: stage `'W'`.
  - FX-1: `effects_storeValue7()` is the WIDE8 inverse.
  - FX-2/FX-3: per-step lane array and a one-step drain. The writer
    (`effects_service()` → `effects_seqConsume()`/`effects_seqSelect()`) and
    the drain (`liveRec_service()`) are both foreground, so there is no race.
- **LiveRecord:**
  - LR-1, LR-2, LR-3/4/5 (roll keyed to `liveRec_rollTrack`; spans carry
    `fil`);
  - LR-6, LR-7 (apart from minor items 6 and 8), LR-8, LR-9;
  - LR-10/11 (slot-5 rtk rule, type match, Effect/Scene only when T == A);
  - LR-12, LR-14, LR-15, LR-16;
  - LR-17 (apart from must-fix item 1), LR-18 (apart from minor item 13),
    LR-19.
- **buttonHandler:**
  - BH-1/BH-2: per-mode table.
  - BH-3/BH-4: the hold flags are set and cleared before every early exit.
  - BH-5, BH-6.
  - BH-7: record-off only after a real engage.
  - BH-8: `def` note flag on every report, including the overlay switch.
  - BH-9: unchanged shape.
  - Must-fix item 5 is a remaining path, not a regression of BH-3..BH-6.
- **Menu:**
  - MN-1: `menu_cellClearTarget()` split.
  - MN-2: encoder hook after the commit.
  - MN-3: apart from must-fix item 3, the Effect Morph-view and viewed ≠ A
    exclusions are correct.
  - MN-4, MN-5.

### Implementer deviations

- **LR-18 ring-full drops:** accepted outside `liveRec_drops`, but the log
  entry is inaccurate (minor item 13).
- **LR-13 fill from the placement delay:** this is the schedule's rule.
  The real defect is the write position and timing (must-fix item 4).
- **LR-20 partial:** finish the header comment (minor item 14).
- **F6/F7 not run:** still pending; see below.

### Build

`make all` is clean with no warnings. `tools/link_budget.py` on the current
tree:

| Region | Use |
|---|---|
| Flash | 567,668 / 753,664 B, headroom **185,996 B** (the log says 186,172 B; this rebuild of the current tree is 176 B larger) |
| ITCM | 4,168 / 16,384 B |
| DTCM statics | 4,472 B |
| FXBUF | 126,592 B, margin 3,712 B |

**RAM, checked with `nm`:**

- The F6 table entries are all present.
- Not in the table:
  - the flam grace state kept by the linker (`liveRec_gracePl` 6 B,
    `liveRec_graceVel` 1 B, `liveRec_graceValid` 1 B);
  - `liveRec_rollNoteDefault` (1 B);
  - `liveRec_sceneEpochValid` (1 B).
- Every-build total: about +61 B against the planned ≈ +51 B.
- This build has `DEV_MODE_LOGGING` on, so `liveRec_drops` (14 B) is
  included.
- The must-fix items add 3 B (`liveRec_eraseKnobFxLanes`,
  `liveRec_rollStartDelay`).
- F7 must update the RAM table in `STORAGE_SRAM_MANIFEST.md` with these
  figures.

### Remaining

1. **Fix must-fix items 1–5.** Items 1 and 3 are one-line fixes; items 2, 4
   and 5 are local to the cited functions. Clean build after.
2. **Run F6.**
   - Run the host test of `seq_placeFromSnapshot()` as scheduled.
   - Add these rows to the hardware matrix:
     - FX SEQ hold erases lanes 8–15 (item 1);
     - an FX knob erase survives an FX SEQ release and a MODE press
       (item 2);
     - PERF Morph edits are not recorded (item 3);
     - a quick roll release leaves a plain trigger on the start step;
     - a roll spanning steps leaves timing 0 on the end step (item 4);
     - SHIFT+CLEAR, then REC, then release both keys in either order ends
       erase (item 5).
3. **Do F7** as scheduled, including:
   - `decode_devlogs.py` stage `'W'`;
   - the `seq_projectStep()` → `seq_placeFromSnapshot()` edit in
     `S081_P2_LOOP_WHILE_RECORD.md` §3.3 and §4;
   - the corrected RAM table.
4. **Item 29** (FX realign on non-resetting Scene changes) still needs your
   decision.
