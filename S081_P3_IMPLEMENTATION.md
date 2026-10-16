# S081 P3 — Loop While Record: Implementation Schedule

Session 081, part 3 (2026-10-16), branch `dev-ph6-cleanup`.

This schedule implements `S081_P3_LOOP_WHILE_RECORD.md` revision 4
(answers Q1–Q6, FQ1–FQ7). Implementation is in progress: see the progress
log at the end of this file for the state of each phase and step.

**Line numbers** are the working tree on 2026-10-16: P1/P1B/P1C and P2 with
its follow-ups (ramp writes on change, `LIVEREC_AUTOMATION_FIRST_CHANGE`),
uncommitted on top of `d21e245`. Each entry gives:

- `file:line`;
- the action (**ADD** / **REMOVE** / **MODIFY**);
- the code;
- the comment block that goes beside the change as documentation in place.

Where a function or type is declared in a header, the header gets its own
comment block (given with the step). Where a step replaces an existing
comment block, the new block replaces the old one; it is not added beside
it. Line numbers shift as earlier steps land; each step names a unique
anchor (function name or quoted line) to find its place.

**Authority:**

- `S081_P3_LOOP_WHILE_RECORD.md` revision 4;
- `MEMORY.md` binding rules: looper invariants (S080), RAM approval policy,
  automation priority;
- `PATTERN_DYNAMIC_STACK.md` §6.5;
- `S081_P1_LIVE_RECORD.md` §9 (M-9 ring semantics);
- `S081_P2_IMPLEMENTATION.md` (first-change rule).

**Build gate after every phase:** `make all` clean (no new warnings), then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

---

## 0. Summary

### RAM and cost

- **Static RAM:** none. The stored fired bit and the jump flag pack into the
  existing `liveRec_prevValid[7]` bytes, renamed `liveRec_prevFlags[7]`.
- **Stack:** `seq_track_time_t` grows by 12 B (loop fields, SQ-6). It is a
  local in `liveRec_triggerIn()`, `liveRec_endRoll()` and
  `liveRec_paramIn()`; those frames do not nest, so the deepest chain grows
  by 12 B. This is not a material increase; listed for the policy.
- **Flash:** about +0.8 to +1.2 KB (placement maths with 64-bit divisions,
  ring helper, button logic).
- **TIM3:** at a loop wrap, one comparison and at most one ring push per
  wrapped recording track. No change elsewhere.

### Decision map

| Plan item | Steps |
|---|---|
| FQ4 fired bit of the ended step (§7) | SQ-1, SQ-2, HD-1, LR-1..LR-5 (P3-0) |
| FQ3 jump entries (§3.9) | SQ-3, SQ-4, LR-4 (P3-A) |
| §3.4 echo: clear on resume, none at the wrap | SQ-4 (P3-A) |
| FQ6/Q6 loop-aware placement (§3.3) | SQ-5, SQ-6, HD-2 (P3-A) |
| L-2 lockout removed (§3.1) | BH-6, BH-8, LR-6, LR-7 (P3-B) |
| Q1 hold set: any SELECT/VOICE/BAR (§3.7) | BH-1, BH-2, BH-3, BH-4, BH-10 (P3-B) |
| Q4 TRACK keeps looper functions (§3.7) | BH-4, BH-5, BH-7, BH-9 (P3-B) |
| FQ1 VOICE without SELECT adds its track | BH-5 (P3-B) |
| FQ2 held overlay ends at engage, becomes a TRACK hold | BH-6 (P3-B) |
| FQ5 PERF Morph edits recorded (§3.10) | MN-1 (P3-B) |

### Placement mapping, verified before scheduling

The loop-aware placement rule (SQ-6) was checked against a host simulation
of the TIM3 timeline: the wrap runs before the DDA, the snapshot is the
state after tick `anchor − 1`, and the step advances when the accumulator
reaches the interval. The check compared the predicted image for every
current tick in four passes and every whole-tick delta from about −2L to
+3L against the simulated state, in fwd mode, for:

- intervals 3, 5, 6, 7.3, 7.8 and 24 ticks;
- loop lengths 2..384 ticks;
- anchors 1..100.

**Result: 0 mismatches in 86,593,860 cases**, once the pre-anchor rule
below was added. The first version mapped a backward delta from a later
pass to before the anchor into a pass; 30,696 such cases failed. That
history was played unlooped, so it is now the snapshot shifted by the full
offset (plan §3.3, revision 4).

---

## Phase P3-0 — Ring format and the ended step's own fired bit (FQ4)

This phase is behaviour-neutral except for the FQ4 fix: no jump entry is
pushed yet, so every `jump` is 0.

### SQ-1 Ring jump bit and push helper — MODIFY `sequencer.c:979-983`; ADD `seq_recRingPush()` after `seq_takeRecBoundary()` (`:1010`)

Replace the packing comment and add the jump bit:

```c
/*
 * Ring entry packing (S081 §19.1 item 1; S081 P3 adds the jump bit).
 *
 * step 7 bits, track 3, fired 1, jump 1 (bits 0..11 of 16). fired: the step
 * holds a trigger and its condition passed, known at its start. jump: the
 * entry was pushed by a looper position jump (wrap mid-step, resume), not by
 * a step advance; the step it begins is a partial replay whose trigger did
 * not fire on this pass (plan §3.9). Affiliates: seq_recRingPush(),
 * seq_takeRecBoundary(), liveRec_drainRing().
 */
#define SEQ_REC_RING_STEP_MASK   0x007Fu
#define SEQ_REC_RING_TRACK_MASK  0x0380u
#define SEQ_REC_RING_TRACK_SHIFT 7u
#define SEQ_REC_RING_FIRED_BIT   0x0400u
#define SEQ_REC_RING_JUMP_BIT    0x0800u
```

ADD after `seq_takeRecBoundary()`:

```c
static void seq_recRingPush(uint8_t track, int16_t step, uint8_t fired,
                            uint8_t jump)
{
    uint8_t next;

    if (!seq_recWriting || step < 0 ||
        (track != seq_recTrack &&
         (seq_liveHoldMask & (uint8_t)(1u << track)) == 0u))
        return;
    next = (uint8_t)((seq_recRingHead + 1u) & 0x0Fu);
    if (next != seq_recRingTail) {
        seq_recRing[seq_recRingHead] =
            (uint16_t)(((uint16_t)(step & 0x7F)) |
                       ((uint16_t)(track & 0x7u) << SEQ_REC_RING_TRACK_SHIFT) |
                       (fired ? SEQ_REC_RING_FIRED_BIT : 0u) |
                       (jump ? SEQ_REC_RING_JUMP_BIT : 0u));
        seq_recRingHead = next;
    } else {
#if DEV_MODE_LOGGING
        seq_recRingDrops++;
        patternTrace_record(PAT_TRACE_STAGE_LIVEREC_DROP, 5u,
                            (uint32_t)(((uint32_t)track << 8u) |
                                       (uint8_t)(step & 0x7F)));
#endif
    }
}
```

```
/*
 * Push one step-boundary entry for the recording track (S081 C-9a; S081 P3).
 *
 * What: when recording is writing and the track is rtk or live-held, writes
 * {step, track, fired, jump} at the ring head and publishes the head; a full
 * ring drops the entry (DEV: counted, trace cause 5). A negative step (not
 * started) pushes nothing. Why: the step advance (C-9a) and, from P3, the
 * looper's position jumps (wrap mid-step, resume) must report the step that
 * begins in one format; one producer function keeps the head/tail protocol
 * in one place. Context: TIM3 (step advance, wrap) or foreground with
 * PRIMASK held (resume). The ring stays single-producer because the
 * foreground pushes only inside the looper's PRIMASK sections, and
 * single-consumer (liveRec_drainRing()). Inputs: track, step, fired, jump.
 * Output: one ring entry. Accessors: seq_advanceTrackStep(),
 * seq_loopProcessTick(), seq_loopResumeTrack(). Affiliates:
 * seq_takeRecBoundary(), seq_recRingDrops, liveRec_drainRing().
 */
```

### SQ-1b Step advance uses the helper — MODIFY `sequencer.c:3149-3168` (the `if (seq_recWriting && …)` push block in `seq_advanceTrackStep()`)

Replace the block (the gate, the head/tail code and the DEV drop) with:

```c
		seq_recRingPush(track, seq_stepIndex[track], rec_fired, 0u);
```

The comment block above it (`:3135-3148`) stays. Append one sentence to it:
"The push itself is seq_recRingPush() (S081 P3), shared with the looper's
jump entries."

### SQ-2 Decode the jump bit — MODIFY `seq_takeRecBoundary()` at `sequencer.c:1008`

After `out->fired = …;` ADD:

```c
    /* S081 P3: a looper position jump began this step (plan §3.9). */
    out->jump = (uint8_t)((e & SEQ_REC_RING_JUMP_BIT) ? 1u : 0u);
```

Extend the function comment (`:985-991`): "S081 P3: also reports whether a
looper position jump (not a step advance) pushed the entry."

### HD-1 Boundary struct — MODIFY `sequencer.h:458-463`

```c
/*
 * One step-boundary ring entry (S081 §19.1 item 1; S081 P3).
 *
 * step: the step that begins. track: 0..6. fired: that step holds a trigger
 * and its condition passed (known at its start). jump: the entry comes from a
 * looper position jump (a wrap that lands mid-step, or a resume), not from a
 * step advance; the step is a partial replay and its trigger did not fire on
 * this pass. Producer: seq_recRingPush(). Consumer: liveRec_drainRing(),
 * which applies a step's own fired and jump flags when that step ends.
 */
typedef struct {
    uint8_t step;
    uint8_t track;
    uint8_t fired;
    uint8_t jump;
} seq_rec_boundary_t;
```

### LR-1 Per-track flags replace the valid byte — MODIFY `LiveRecord.c:121-123`

```c
/*
 * Last boundary-ring entry per track (S081 C-9a; S081 P3 FQ3/FQ4).
 *
 * What: liveRec_prevStep[t] is the step that began with track t's last ring
 * entry; liveRec_prevFlags[t] holds that entry's flags:
 *  - LIVEREC_PREV_VALID: an entry has been seen since the last playhead
 *    break (the step is known);
 *  - LIVEREC_PREV_FIRED: that step's trigger fired at its start;
 *  - LIVEREC_PREV_JUMP: that step began with a looper jump (partial
 *    replay, plan §3.9).
 * The track's next entry ends this step, and the step-end table runs with
 * THESE flags. Why: P1 M-9 says the ended step uses its own entry's fired
 * bit; the drain passed the next entry's bit (plan §7). Storing the flags
 * per track fixes that and carries the jump flag with no new RAM (the
 * former 0/1 valid byte). Writers: liveRec_drainRing(); cleared at every
 * playhead break (liveRec_syncPosition()). Readers: liveRec_drainRing(),
 * liveRec_paramIn(). RAM: unchanged (7 + 7 B).
 */
#define LIVEREC_PREV_VALID  0x01u
#define LIVEREC_PREV_FIRED  0x02u
#define LIVEREC_PREV_JUMP   0x04u
static uint8_t liveRec_prevStep[NUM_TRACKS];
static uint8_t liveRec_prevFlags[NUM_TRACKS];
```

### LR-2 Break clears the flags — MODIFY `LiveRecord.c:330` (`liveRec_syncPosition()`)

`memset(liveRec_prevValid, 0, sizeof(liveRec_prevValid));` becomes
`memset(liveRec_prevFlags, 0, sizeof(liveRec_prevFlags));`. No comment
change.

### LR-3 Drain uses the ended step's own flags — MODIFY `liveRec_drainRing()` at `LiveRecord.c:340-396`

- `:342` `if (b.track < NUM_TRACKS && liveRec_prevValid[b.track]) {` becomes:

```c
        uint8_t flags = (b.track < NUM_TRACKS) ? liveRec_prevFlags[b.track]
                                               : 0u;

        if ((flags & LIVEREC_PREV_VALID) != 0u) {
```

- `:371` `liveRec_stepEnd(b.track, prev, b.fired);` becomes:

```c
            /*
             * The ended step's own flags (S081 P3 FQ4/FQ3).
             *
             * What: the step-end table runs with the fired and jump flags
             * stored when this step BEGAN, not with the new entry's. Why:
             * P1 M-9; the P1 code used b.fired, so LATCH re-stamped or
             * cleared step N by whether step N+1 fired. Affiliates:
             * liveRec_prevFlags[], liveRec_stepEnd().
             */
            liveRec_stepEnd(b.track, prev,
                            (uint8_t)((flags & LIVEREC_PREV_FIRED) != 0u),
                            (uint8_t)((flags & LIVEREC_PREV_JUMP) != 0u));
```

- `:386-388` (first-change block) `!liveRec_prevValid[b.track]` becomes
  `(flags & LIVEREC_PREV_VALID) == 0u`.
- `:391-392` the store becomes:

```c
            liveRec_prevStep[b.track] = b.step;
            liveRec_prevFlags[b.track] = (uint8_t)(LIVEREC_PREV_VALID |
                (b.fired ? LIVEREC_PREV_FIRED : 0u) |
                (b.jump ? LIVEREC_PREV_JUMP : 0u));
```

Extend the function's leading comment (`/* Boundary-ring drain: span writes
and the step-end tables. */`) to: "Boundary-ring drain: span writes and the
step-end tables. Each entry ends the track's previous step with that step's
own fired/jump flags (S081 P3) and stores its own flags for its end."

### LR-4 Step end: jump rule — MODIFY `liveRec_stepEnd()` at `LiveRecord.c:149` (declaration) and `:723` (definition)

Forward declaration (`:149`):

```c
static void liveRec_stepEnd(uint8_t track, uint8_t step, uint8_t fired,
                            uint8_t jump);
```

Definition signature likewise. In the LATCH, not-dirty part, right after
`/* LATCH, not dirty (and not overdub). */` ADD:

```c
        /*
         * Partial replay after a looper jump (S081 P3 FQ3, plan §3.9).
         *
         * What: a step begun by a jump entry gets no re-stamp and no clear;
         * only its dirty targets were handled above. Why: its trigger did not
         * fire on this pass because the wrap or resume entered it mid-step,
         * so "not fired" would wipe the own-Instrument entries of a step that
         * does play when it is entered from its start. Inputs: jump (the
         * step's own flag, liveRec_prevFlags). Affiliates:
         * seq_loopProcessTick(), seq_loopResumeTrack().
         */
        if (jump)
            continue;
```

Extend the function comment (the block ending "The not-dirty LATCH rules
(re-stamp / clear) are unchanged.") with:

```
 * S081 P3: `fired` and `jump` are the ended step's OWN flags (stored when it
 * began; P1 M-9, plan §7). A step begun by a looper jump (`jump`) is a
 * partial replay: dirty targets and erase run as usual, the LATCH re-stamp
 * and clear do not.
```

### LR-5 First-change step test — MODIFY `liveRec_paramIn()` at `LiveRecord.c:1377`

`stepKnown = liveRec_prevValid[liveRec_rtk];` becomes:

```c
            stepKnown = (uint8_t)((liveRec_prevFlags[liveRec_rtk] &
                                   LIVEREC_PREV_VALID) != 0u);
```

No comment change (the paramIn block already says "when a ring entry for
rtk has been seen").

**Gate P3-0:** `make all` clean; `grep -n liveRec_prevValid Core/` returns
nothing. Hardware (LATCH, no looper): on a pattern with a fired step
followed by an empty step, a latched own-Instrument parameter is re-stamped
on the fired step and cleared from the empty one (before the fix, the
other way round).

---

## Phase P3-A — Jump entries, echo on resume, loop-aware placement

### SQ-3 Wrap pushes a jump entry when no advance follows — MODIFY `seq_loopProcessTick()` at `sequencer.c:5179-5183`

After `seq_rampBreakTrack(track);` (inside the restore branch) ADD:

```c
		/*
		 * Report a mid-step wrap to live record (S081 P3 FQ3, plan §3.9).
		 *
		 * What: when this tick's DDA will not advance the restored track
		 * (accumulator + 256 < interval), no step-advance entry follows, so
		 * a jump entry reports the restored step as beginning now (fired 0).
		 * When the DDA does advance, its own entry reports the replayed step
		 * and nothing is pushed here. Why: with qnt off or a 1/8..1/64 grid,
		 * a track whose step does not start at the anchor wraps mid-step;
		 * without an entry LiveRecord keeps treating the pre-wrap step as
		 * playing (wrong-step writes; the first-change dirty bit blocks the
		 * next pass). Every wrap must be a new pass for live record, also
		 * for loops shorter than a step. Inputs: restored cursor and
		 * accumulator, effective scale. Output: at most one ring entry.
		 * Context: TIM3, before the DDA. Affiliates: seq_recRingPush(),
		 * liveRec_drainRing(), liveRec_stepEnd() (jump rule).
		 */
		if (!seq_SomModeActive &&
		    (uint32_t)seq_trackAccumulator[track] + 256u <
		        (uint32_t)stepScale_ticksQ8(seq_effectiveTrackScale[track]))
			seq_recRingPush(track, seq_stepIndex[track], 0u, 1u);
```

Extend the function comment (`:5142-5154`) with: "S081 P3: a wrap that
lands mid-step pushes a jump entry for the recording track (no step advance
follows on this tick); echo marks are not cleared here (plan §3.4: the P1B
deadline expires them)."

### SQ-4 Resume clears the echo mark and reports the jump — MODIFY `seq_loopResumeTrack()` at `sequencer.c:4936-4958`

At the head of the function ADD a local `int16_t before = seq_stepIndex[track];`.
After the P2 line `seq_rampBreakTrack(track);` ADD:

```c
	/*
	 * Live record across a resume (S081 P3, plan §3.4/§3.9).
	 *
	 * What: (1) the track's echo mark is dropped: the cursor jumps, so a
	 * marked step index no longer means the play it was set for. (2) If the
	 * resumed cursor is on another step, a jump entry reports that step as
	 * beginning now (fired 0); the next DDA advance reports the following
	 * step as usual. Why: release, a track leaving in a swap and disengage
	 * move the playhead without a step advance, so LiveRecord would keep the
	 * pre-resume step. Same step index: no entry (the playhead continues the
	 * step it was in). Context: foreground; every caller
	 * (seq_loopUpdateMask(), seq_loopDisengage()) holds PRIMASK, so the push
	 * cannot interleave with the TIM3 producer. Affiliates:
	 * seq_recRingPush(), seq_suppressStepOnce(), liveRec_drainRing().
	 */
	seq_echoStep[track] = -1;
	if (pos.step >= 0 && pos.step != before)
		seq_recRingPush(track, pos.step, 0u, 1u);
```

Extend the function comment (`:4920-4935`) with: "S081 P3: also drops the
track's echo mark and pushes a jump entry when the step changes."

### HD-2 Capture snapshot carries the loop state — MODIFY `sequencer.h:437-456` (`seq_track_time_t` and its comment)

Add after `uint8_t  playedScene;`:

```c
    /* S081 P3: loop state at the capture (looped = 0: the rest is unused). */
    uint8_t  looped;
    uint8_t  loopStartPlayState;
    int16_t  loopStartStep;
    uint16_t loopStartAccum;
    uint16_t loopTicks;
    uint32_t loopAnchor;
```

Replace the struct comment with:

```c
/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11, C-8a;
 * S081 P3 loop fields).
 *
 * Inputs: track 0..6. Outputs: a consistent snapshot of TIM3-mutated state
 * for LiveRecord's placement math. S081 P3: when the track is looped,
 * looped = 1 and loopAnchor/loopStart*/loopTicks copy its loop anchor tick,
 * snapshot (cursor, accumulator, play state before the anchor tick's
 * advance) and loop length, so seq_placeFromSnapshot() can map a shift that
 * crosses a wrap (plan §3.3). Stack only (+12 B per local).
 */
```

### SQ-5 Capture fills the loop fields — MODIFY `seq_captureTrackTime()` at `sequencer.c:4685`

After `out->playedScene = seq_perTrackPattern[track];` (inside the PRIMASK
section) ADD:

```c
    /*
     * S081 P3: the track's loop state under the same PRIMASK, so placement
     * sees the anchor and snapshot that the next wraps will use (plan §3.3).
     * Not looped (or before the first scheduler tick): looped = 0.
     */
    out->looped = (uint8_t)(seq_loopActive && !seq_initialSchedulerTick &&
                            (seq_loopTrackMask & (uint8_t)(1u << track)) != 0u);
    out->loopAnchor = seq_loopAnchor[track];
    out->loopStartStep = seq_loopStartStep[track];
    out->loopStartAccum = seq_loopStartAccum[track];
    out->loopStartPlayState = seq_loopStartPlayState[track];
    out->loopTicks = seq_loopLengthTicks(seq_loopDivision, seq_loopModifier);
```

Extend the function comment (`:4655-4662`) with: "S081 P3: also copies the
track's loop anchor, snapshot and loop length when it is looped."

### SQ-6 Loop-aware placement — ADD `seq_placeLoopImage()` above `seq_placeFromSnapshot()` (`sequencer.c:4689`); MODIFY `seq_placeFromSnapshot()` at `:4713-4775`

ADD:

```c
static uint8_t seq_placeLoopImage(uint8_t track, const seq_track_time_t *snap,
                                  int32_t deltaQ8, seq_loop_pos_t *pos)
{
    int64_t span;
    int64_t nowRel;
    int64_t tgtRel;
    int64_t kNow;
    int64_t kTgt;
    int64_t x;

    if (!snap->looped || snap->loopTicks == 0u ||
        snap->elapsedTick < snap->loopAnchor)
        return 0u;
    span = (int64_t)snap->loopTicks * 256;
    nowRel = ((int64_t)snap->elapsedTick - (int64_t)snap->loopAnchor + 1) * 256;
    tgtRel = nowRel + (int64_t)deltaQ8;
    kNow = (nowRel - 256) / span;
    x = tgtRel - 256;
    kTgt = (x >= 0) ? (x / span) : -((-x + span - 1) / span);
    if (kTgt == kNow || (kNow == 0 && kTgt < 0))
        return 0u;                      /* same pass: live cursor is exact */
    pos->step = snap->loopStartStep;
    pos->accum = snap->loopStartAccum;
    pos->play_state = snap->loopStartPlayState;
    (void)seq_shiftPositionQ8(track, pos,
        (int32_t)((kTgt < 0) ? tgtRel : (x - kTgt * span + 256)));
    return 1u;
}
```

```
/*
 * Map a placement shift that crosses a loop wrap (S081 P3, plan §3.3, Q6).
 *
 * What: for a looped track, finds which loop pass the target time falls in.
 * Times are Q8 ticks relative to the snapshot (the state after tick
 * anchor - 1): the capture is nowRel = (elapsed - anchor + 1) * 256; pass k
 * covers relative times 256 + k*L*256 .. 255 + (k+1)*L*256 (the wrap at tick
 * anchor + k*L restores the snapshot and the same tick's DDA adds 256).
 *  - Same pass as the capture, or from the first pass back into the
 *    unlooped history: returns 0, the caller walks the live cursor (exact).
 *  - A later or earlier pass: the image is the snapshot shifted by the
 *    target's phase in its pass (x - k*L*256 + 256), i.e. exactly what that
 *    pass plays. A forward snap across the wrap lands on the replayed step
 *    after it (Q6).
 *  - Before the loop's first pass (kTgt < 0) from a later pass: the
 *    unlooped history, the snapshot shifted by the full relative offset.
 * Why: the live-cursor walk (P1B) continues past the window end as if
 * unlooped. Verified against a TIM3 timeline simulation: 0 mismatches in
 * 86,593,860 cases (S081_P3_IMPLEMENTATION.md §0). Inputs: track, capture
 * snapshot with loop fields, signed Q8 delta. Output: *pos and 1 when the
 * snapshot branch applies, else 0 (pos untouched). Context: foreground
 * (64-bit maths). Accessors: seq_placeFromSnapshot(). Affiliates:
 * seq_shiftPositionQ8(), seq_loopProcessTick(), seq_captureTrackTime().
 */
```

In `seq_placeFromSnapshot()`:

- Locals: ADD `uint8_t crossed;`.
- `:4738` `steps = seq_shiftPositionQ8(track, &pos, deltaQ8);` becomes:

```c
    /* S081 P3: a shift across a loop wrap maps onto the replayed pass. */
    crossed = seq_placeLoopImage(track, snap, deltaQ8, &pos);
    steps = crossed ? 0 : seq_shiftPositionQ8(track, &pos, deltaQ8);
    if (pos.step < 0)
        return 0u;
```

- `:4765` `out->rel = …;` becomes:

```c
    /*
     * S081 P3: behind / current / ahead by TIME when the shift crossed a
     * wrap (the landed step is in another pass; its index may be lower even
     * though it plays later). Otherwise by step advances, as before.
     */
    if (crossed)
        out->rel = (int8_t)((deltaQ8 > 0) ? 1 : -1);
    else
        out->rel = (int8_t)((steps > 0) ? 1 : ((steps < 0) ? -1 : 0));
```

- The `if (steps > 0) {` that computes `out->ahead` becomes
  `if (out->rel > 0) {`. `ahead` stays the time from the capture to the
  landed step start, so the echo deadline covers the play after the wrap
  (plan §3.4).

Extend the `seq_placeFromSnapshot()` comment block (`:4689-4712`) with:

```
 * S081 P3: for a looped track, a shift that crosses a loop wrap is mapped
 * onto the pass the target falls in (seq_placeLoopImage()): a forward snap
 * across the wrap lands on the replayed step after it, a backward one at the
 * end of the previous pass (or in the unlooped history before the loop's
 * first pass). rel is then decided by time; ahead is the time to the landed
 * step, so the echo mark covers the play after the wrap.
```

Header (`sequencer.h:493`, above the declaration) ADD:

```c
/*
 * Place a captured hit on a track's step grid (S081 §8.3, P1B item 5;
 * S081 P3 loop-aware): shifts the live cursor by deltaQ8, or, for a looped
 * track when the shift crosses a wrap, the loop snapshot by the target's
 * phase. Returns 0 when the track has not started.
 */
```

**Gate P3-A:** `make all` clean; `link_budget.py`.

**Host test (scratch, not committed):** the mapping check in §0, extended
with sub-tick deltas, rev and pip modes, and `seq_placeLoopImage()` maths
copied 1:1. Expected: 0 mismatches.

---

## Phase P3-B — Lockout lifted, PERF button rules, PERF Morph recording

### LR-6 Trigger gate removed — MODIFY `liveRec_triggerIn()` at `LiveRecord.c:1169-1170`

REMOVE:

```c
    if (seq_loopActive)
        return;                       /* P1 interim looper lockout (P2 removes) */
```

No replacement comment (the plan's L-2 is the rule; the function comment
lists no looper gate).

### LR-7 `liveRec_forceOff()` removed — REMOVE `LiveRecord.c:862-881` and `LiveRecord.h:88`; MODIFY `LiveRecord.c:1491`

- REMOVE the function and its comment block ("Force record and erase off
  (S081 P1 looper interim; P1C item 16)").
- REMOVE `void    liveRec_forceOff(void);` from `LiveRecord.h`.
- In the `liveRec_eraseReleaseHeld()` comment: "It is also the gesture reset
  at erase end and force-off" becomes "It is also the gesture reset at
  erase end", and "Accessors: handleModeButtons(), liveRec_eraseEnd(),
  liveRec_forceOff()." becomes "Accessors: handleModeButtons(),
  liveRec_eraseEnd()."

Gate: `grep -rn liveRec_forceOff Core/` returns nothing.

### MN-1 PERF Morph edits are recorded — MODIFY `menu_liveRecRecordCell()` at `menu.c:14220`; MODIFY its comment (`:14188-14211`)

```c
    if (menu_activePage == RECORDING_PAGE ||
        menu_activePage == MENU_MIDI_PAGE   || menu_activePage == LOAD_PAGE ||
        menu_activePage == SAVE_PAGE)
        return;
```

In the comment block, the bullet "the page is PERF, RECORDING, global
settings, Load or Save;" becomes "the page is RECORDING, global settings,
Load or Save;". ADD at the end of the block:

```
 * S081 P3 (FQ5): PERF edits are recorded. The PERF Morph cells resolve to
 * the same targets as the VOICE mix page (1VM..6VM -> Nvm) and the Effect
 * page (fxm), and commit the values liveRec_targetValue7() reads, so a
 * Morph is recorded the same way from every page. mrp (master morph) has no
 * target and is never recorded (FQ7).
```

### BH-1 Looper state comment — MODIFY `buttonHandler.c:299-314`

In the "Looper foreground state" comment, "the loop stays engaged while any
SELECT, any VOICE of a looped track, or any BAR is held (Defect 8)" becomes:

```
 * the loop stays engaged while any SELECT, any VOICE or any BAR is held
 * (S081 P3 Q1; S080 counted only VOICEs of looped tracks), until all are
 * released or the mode leaves PERF.
```

### BH-2 Any held VOICE holds the loop — MODIFY `buttonHandler_loopCheckDisengage()` at `buttonHandler.c:1535-1563`

`if ((perf_heldVoiceMask & bh_loopTrackMask) != 0u)` becomes
`if (perf_heldVoiceMask != 0u)`.

Replace the function comment:

```c
/*
 * End the loop when nothing holds it any more (S080 P2 Defect 8; S081 P3 Q1).
 *
 * What: returns while any looper SELECT, any VOICE or any BAR is held.
 * Otherwise disengages the loop (every looped track resumes where it would
 * have been), clears the modifier and track mirror, and restores the SELECT
 * row (dark) and the BAR overlay LEDs. Why: user rule Q1: the loop runs from
 * the first SELECT press until every SELECT, TRACK and BAR button is
 * released (or the mode leaves PERF), with or without record. A held VOICE
 * of a track that is not looped (lop off, or dropped) also holds it.
 * Inputs: bh_loopSelectHeldMask, perf_heldVoiceMask, bh_loopBarHeld.
 * Outputs: seq_loopDisengage(), LEDs. Accessors: SELECT, VOICE and BAR
 * release paths. Affiliates: menu_syncPerfSelectLed(), bh_overlayActive,
 * buttonHandler_perfVoiceReleased().
 */
```

### BH-3 Leaving PERF clears the VOICE holds — MODIFY `buttonHandler_loopForceRelease()` at `buttonHandler.c:1575-1584`

After `bh_loopBarModifier = 0u;` ADD:

```c
    /*
     * S081 P3 (Q1): the PERF VOICE holds end with PERF. Their releases arrive
     * in the new mode, where the PERF release path does not run, so a bit
     * left set would hold the next loop for ever.
     */
    perf_heldVoiceMask = 0u;
    perf_voiceActionOccurred = 0u;
```

Extend the function comment: "S081 P3: also clears the PERF VOICE holds
(`perf_heldVoiceMask`), which now hold the loop."

### BH-4 VOICE release while looping — MODIFY `buttonHandler_perfVoiceReleased()` at `buttonHandler.c:1605-1625`

Replace the block `if (seq_loopActive && (bh_loopTrackMask & bit) != 0u) { … }`
and its comment with:

```c
    /*
     * VOICE release while looping (S080 P2 Defect 8 D1; S081 P3 Q1/Q4).
     *
     * What: a looped track's release drops it from the loop at once (it
     * resumes where it would have been; the others keep their loop starts).
     * Every release while looping is a looper gesture: it never toggles a
     * mute, and the loop ends if nothing holds it any more (any SELECT,
     * VOICE or BAR). Why: Q4, TRACK buttons keep their looper functions
     * while the loop is engaged; Q1, any held TRACK holds the loop, so the
     * release of a non-looped VOICE can be the last hold. Inputs: bit,
     * bh_loopTrackMask, seq_loopActive. Outputs: seq_loopUpdateMask(),
     * possibly seq_loopDisengage(). Affiliates:
     * buttonHandler_loopCheckDisengage().
     */
    if (seq_loopActive) {
        if ((bh_loopTrackMask & bit) != 0u)
            bh_loopTrackMask = seq_loopUpdateMask(
                (uint8_t)(bh_loopTrackMask & (uint8_t)~bit));
        perf_voiceActionOccurred = 1u;
        buttonHandler_loopCheckDisengage();
    }
```

### BH-5 VOICE press while looping: looper only — MODIFY `handleVoiceButton()` PERF branch at `buttonHandler.c:2386-2410`

Replace the block `if (seq_loopActive && bh_loopSelectHeldMask != 0u) { … }`
and its comment with:

```c
        /*
         * VOICE press while the loop is engaged (S080 D1/P2 Defect 7;
         * S081 P3 Q4/FQ1).
         *
         * What: the press is a looper gesture, before the overlay trigger,
         * the mute arm and (via BH-7) the erase router:
         *  - a SELECT held: the loop moves to the held VOICE set (swap,
         *    S080 D1): leaving tracks resume, staying ones keep their loop
         *    starts, new ones get a fresh start;
         *  - no SELECT held: this track joins the loop with a fresh loop
         *    start (FQ1); tracks already looping keep theirs. A track the
         *    looper may not capture (lop off) does not join.
         * Either way the VOICE holds the loop (Q1), its release drops the
         * track (S080 D1), and it never fires an overlay trigger, toggles a
         * mute or starts an erase gesture (Q2/Q4). SHIFT+VOICE behaves the
         * same. Inputs: voiceNr, seq_loopActive, bh_loopSelectHeldMask,
         * perf_heldVoiceMask, bh_loopTrackMask. Outputs:
         * seq_loopUpdateMask(), perf_heldVoiceMask, perf_voiceActionOccurred.
         * Affiliates: buttonHandler_perfVoiceReleased(),
         * seq_loopableTrackMask().
         */
        if (seq_loopActive) {
            uint8_t bit = (uint8_t)(1u << voiceNr);

            perf_heldVoiceMask |= bit;
            perf_voiceActionOccurred = 1u;
            dblclick_cancel();
            if (bh_loopSelectHeldMask != 0u)
                bh_loopTrackMask = seq_loopUpdateMask(perf_heldVoiceMask);
            else
                bh_loopTrackMask = seq_loopUpdateMask(
                    (uint8_t)(bh_loopTrackMask | bit));
            return;
        }
```

The overlay branch below (`:2420`) is unchanged; it is now reached only
while the loop is not engaged. Add one sentence to its comment: "Not reached
while the loop is engaged (S081 P3 Q2/Q4: the loop pre-empts the overlay)."

### BH-6 Engage: lockout removed, held overlay VOICEs become TRACK holds — MODIFY `handleSelectButton()` PERF case at `buttonHandler.c:2122-2175`

- In the case comment (`:2124-2141`), "the pressed SELECT LED lit; REC forced
  off; BAR LEDs off." becomes "the pressed SELECT LED lit; BAR LEDs off; the
  record mode is kept (S081 P3, L-2)."
- `:2147-2148` the mask becomes:

```c
            uint8_t held = (uint8_t)(perf_heldVoiceMask | bh_overlayVoiceMask);
            uint8_t trackMask = (held != 0u) ? held : 0x7Fu;
```

- REMOVE `:2153-2169` (the "Looper engage turns record off only if the loop
  engaged" comment and the `liveRec_forceOff()` / REC LED block). ADD in its
  place:

```c
            /*
             * Held VOICEs at a successful engage (S081 P3 FQ2, Q4).
             *
             * What: only if the loop engaged (transport running):
             *  1. every VOICE still firing an overlay trigger ends that
             *     gesture as its release would (buttonHandler_overlayRelease():
             *     repeats stop, note-off, liveRec_rollRelease() writes the
             *     recorded roll's end; a flam still waiting for its main hit
             *     loses it, as on an early release);
             *  2. those VOICEs become ordinary looper TRACK holds
             *     (perf_heldVoiceMask): their tracks are already in the
             *     engage mask above, they hold the loop, and their release
             *     drops the track with no mute toggle and no second overlay
             *     release;
             *  3. every held VOICE is marked as a looper gesture
             *     (perf_voiceActionOccurred), so no release toggles a mute
             *     while looping (Q4).
             * The record mode is left as it is (L-2; the P1 interim
             * liveRec_forceOff() is removed). With the transport stopped the
             * engage does nothing and the overlay gesture continues. Why: user
             * answer FQ2 ("roll ends when loop starts, track hold acts as
             * normal for loop operation"). Inputs: bh_overlayVoiceMask,
             * perf_heldVoiceMask, seq_loopActive. Outputs: overlay ended,
             * hold masks. Affiliates: buttonHandler_overlayRelease(),
             * liveRec_rollRelease(), buttonHandler_perfVoiceReleased().
             */
            if (seq_loopActive) {
                uint8_t overlay = bh_overlayVoiceMask;
                uint8_t v;

                for (v = 0u; v < NUM_TRACKS; v++) {
                    if ((overlay & (uint8_t)(1u << v)) != 0u)
                        buttonHandler_overlayRelease(v);
                }
                perf_heldVoiceMask |= overlay;
                if (perf_heldVoiceMask != 0u)
                    perf_voiceActionOccurred = 1u;
            }
```

### BH-7 Erase press router leaves looper VOICEs alone — MODIFY `processPress()` at `buttonHandler.c:2663`

`if (eraseVoice >= 0) {` becomes:

```c
        if (eraseVoice >= 0 &&
            !(eraseMode == SELECT_MODE_PERF && seq_loopActive)) {
```

Add to the router's comment block (`:2625-2644`), after the VOICE bullet:

```
 *  - except VOICE in PERF while the loop is engaged: it keeps its looper
 *    function (S081 P3 Q4) and falls through to handleVoiceButton();
```

### BH-8 REC works while looping — MODIFY `processPress()` BUT_REC at `buttonHandler.c:2785-2792`

REMOVE:

```c
        } else if (seq_loopActive) {
            /*
             * P1 interim lockout: REC is ignored while the looper is engaged
             * (S080 rule; P2 removes it, L-2). Engaged loop already forced
             * recording off in the caller, so the user cannot re-arm mid-loop.
             */
            break;
```

The `} else {` hold-timer branch follows the SHIFT+REC branch directly.
No new comment: the remaining REC press comment (B-2a) describes the
behaviour.

### BH-9 Erase release router passes looper VOICE releases — MODIFY `processRelease()` at `buttonHandler.c:3110-3112`

```c
        if (liveRec_mode() == LIVEREC_MODE_ERASE &&
            (eraseSeq >= 0 ||
             (eraseVoice >= 0 &&
              (perf_heldVoiceMask & (uint8_t)(1u << eraseVoice)) == 0u)))
            return;                                   /* consumed */
```

Add to the release-router comment (`:3070-3083`):

```
 * S081 P3: a VOICE release whose press was a PERF hold (perf_heldVoiceMask,
 * looper or per-track gesture) is not consumed: it continues to the PERF
 * release path, so a looper TRACK hold ends normally during erase (Q4) and
 * the hold bit cannot be stranded.
```

### BH-10 SELECT release comment — MODIFY `buttonHandler_partButtonReleased()` comment at `buttonHandler.c:2197-2212`

"the loop ends only if no looped VOICE and no BAR is held" becomes "the
loop ends only if no VOICE and no BAR is held (S081 P3 Q1)"; "the loop is
sustained by SELECT, looped VOICE or BAR holds (Defect 8)" becomes "the loop
is held by SELECT, VOICE or BAR holds (Defect 8; S081 P3 Q1: any VOICE)".
No code change.

**Gate P3-B:** `make all` clean; `link_budget.py`;
`grep -rn liveRec_forceOff Core/` empty; image `make img`.

---

## Phase P3-C — Verification (hardware, user)

**Buttons (Q1, Q4, FQ1, FQ2):**

1. PERF, transport running, REC off: SELECT engages; the loop runs until
   every SELECT, VOICE and BAR is released, in any order.
2. Same with LATCH on, with OVERDUB held, and with live erase on: identical
   hold behaviour. The record mode survives engage, swap, division change
   and release.
3. While looping, no SELECT held: a VOICE press adds its track (fresh loop
   start). Its release drops it. A `lop`-off track's VOICE does not join but
   holds the loop.
4. While looping, with a SELECT held: VOICE swaps (S080 behaviour).
5. While looping: VOICE never fires the overlay trigger (overlay on), never
   toggles a mute, and in erase never erases its track.
6. Overlay roll held, then SELECT: the roll stops at engage, its recorded
   end is written (LATCH), the VOICE holds the loop, and its release drops
   the track with no mute toggle.
7. Leave PERF with a VOICE held: the loop ends; back in PERF, a new loop
   ends normally when its buttons are released (no stuck hold).
8. Transport stopped: SELECT only lights its LED; a held overlay roll
   continues.

**Recording (FQ5, §3.2):**

9. PERF `1VM`..`6VM` and `fxm` edits record in LATCH and OVERDUB, looping
   or not, the same as from the VOICE mix and Effect pages. `mrp` is never
   recorded.
10. LATCH while looping: latched own-Instrument parameters are written again
    on fired steps each pass; no repeat writes when unchanged (L3); the
    Pattern Stack Service drop counters stay at 0 for 1/16 loops.

**Step boundaries (FQ3, FQ4):**

11. FQ4: a fired step followed by an empty step: re-stamp on the fired one,
    clear on the empty one (LATCH).
12. FQ3, qnt off, a slow track (1/4) looped by a fast track's 1/16 window:
    a knob moved after each wrap is written to the replayed step (first
    change, once per pass), and the step's own-Instrument entries are not
    cleared by the partial replay.
13. Loop shorter than a step (1/128 on a 1/16 track): one write per pass at
    most; the latest pass's first change wins.
14. Release (resume) mid-step: the next knob change is written to the step
    the track resumed on.

**Placement and echo (§3.3, §3.4):** only a roll end placed at engage
exercises these today.

15. A roll released at engage with qnt 1/16 near a loop end: the end lands
    on the replayed step after the wrap, and that one replay is silent
    (echo); later passes play it.

**Regression:** S080 looper matrix (`080_SESSION_HANDOFF_LOG.md` §12) rows
for engage/swap/release/division/scene change; P2 ramps across a wrap
(no ramp across, D8).

---

## Phase P3-D — Documentation at close

- **`PATTERN_DYNAMIC_STACK.md` §6.5:**
  - `:748` `seq_loopEngage()` row: remove "turns recording off";
  - a "Looping while recording" note: jump entries (§3.9), loop-aware
    placement (§3.3), echo expiry by deadline and on resume (§3.4).
- **`MODULE_INTERCHANGE_SPEC.md` PERF performance surface:**
  - `:306` "Recording is turned off on engage and REC is ignored while
    looping" → the Q1/Q4 hold rules (§3.7);
  - `seq_rec_boundary_t.jump`, `seq_track_time_t` loop fields,
    `seq_placeFromSnapshot()` loop behaviour; `liveRec_forceOff()` removed.
- **`S081_P1_LIVE_RECORD.md`:**
  - references to `S081_P2_LOOP_WHILE_RECORD.md` (around lines 160, 258,
    561, 608) → `S081_P3_LOOP_WHILE_RECORD.md`;
  - §9.1 "Not recorded: PERF" → PERF Morph cells recorded (FQ5);
  - M-9: implemented as specified since P3-0.
- **`MEMORY.md`:** the looper/record exclusivity statement → the Q1/Q4
  rules; one binding line: the ring has one producer function
  (`seq_recRingPush()`), and foreground pushes only under the looper's
  PRIMASK.

---

## Order of work

| Phase | Steps | Gate |
|---|---|---|
| P3-0 | SQ-1, SQ-1b, SQ-2, HD-1, LR-1..LR-5 | clean build; FQ4 hardware row 11 |
| P3-A | SQ-3, SQ-4, HD-2, SQ-5, SQ-6 | clean build; host mapping test |
| P3-B | LR-6, LR-7, MN-1, BH-1..BH-10 | clean build; `liveRec_forceOff` gone; image |
| P3-C | hardware matrix | — |
| P3-D | docs | — |

---

## Progress log

Notes recorded as each step lands. Baseline before any change (2026-10-16,
`make all` up to date): `text=571,792`, `data=412`, `bss=429,104`; flash
payload 572,204 / 753,664 B (headroom 181,460 B).

### P3-0 — Ring format and the ended step's own fired bit (FQ4) — DONE

- **SQ-1** `sequencer.c`: packing comment replaced; `SEQ_REC_RING_JUMP_BIT
  (0x0800)` added; `seq_recRingPush()` added after `seq_takeRecBoundary()`
  with its comment block. Matches the schedule exactly.
- **SQ-1b** `seq_advanceTrackStep()`: the inline push block replaced by the
  single `seq_recRingPush(track, seq_stepIndex[track], rec_fired, 0u)` call;
  the comment above gained the shared-helper sentence.
- **SQ-2** `seq_takeRecBoundary()`: decodes `SEQ_REC_RING_JUMP_BIT` into
  `out->jump`; the function comment notes it.
- **HD-1** `sequencer.h`: `seq_rec_boundary_t` gained `uint8_t jump;` with
  the new struct comment block.
- **LR-1** `LiveRecord.c`: `liveRec_prevValid[]` replaced by
  `liveRec_prevFlags[]` with `LIVEREC_PREV_VALID/FIRED/JUMP` and the new
  comment block. RAM unchanged (7 + 7 B).
- **LR-2** `liveRec_syncPosition()`: clears `liveRec_prevFlags[]`.
- **LR-3** `liveRec_drainRing()`: reads the entry's own `flags` once; the
  step-end table runs with the ended step's own fired/jump flags; the
  first-change test and the flag store use the new bits.
- **LR-4** `liveRec_stepEnd()`: `jump` parameter added (prototype and
  definition); the jump rule (`if (jump) continue;` in the LATCH, not-dirty
  arm) added with its comment; the function comment extended.
- **LR-5** `liveRec_paramIn()`: `stepKnown` reads `LIVEREC_PREV_VALID` from
  `liveRec_prevFlags[]`.

**Gate P3-0 PASS:** `make all` clean (only the pre-existing libc_nano
`_close`/`_lseek_r`/`_read_r`/`_write_r` linker warnings); `text=571,872`
(+80 B), `data=412`, `bss=429,104` (**unchanged — no new RAM**); flash
payload 572,284 / 753,664 B (headroom 181,380 B).
`grep -n liveRec_prevValid Core/` returns nothing. FQ4 hardware row 11 is
for the user (P3-C).

### P3-A — Jump entries, echo on resume, loop-aware placement — DONE

- **SQ-3** `seq_loopProcessTick()`: after `seq_rampBreakTrack(track)` in the
  restore branch, a jump entry is pushed when this tick's DDA will not
  advance the restored track (`accumulator + 256 < interval`, and not in SOM
  mode). Comment block added; the function comment extended.
- **SQ-4** `seq_loopResumeTrack()`: local `int16_t before =
  seq_stepIndex[track];` added; after the P2 `seq_rampBreakTrack(track)` the
  echo mark is dropped (`seq_echoStep[track] = -1`) and a jump entry is
  pushed when the resumed step differs. Comment block added; the function
  comment extended.
- **HD-2** `sequencer.h`: `seq_track_time_t` gained `looped`,
  `loopStartPlayState`, `loopStartStep`, `loopStartAccum`, `loopTicks`,
  `loopAnchor` (+12 B stack). The struct comment was rewritten; the
  schedule's `loopStart*/loopTicks` spelling was reworded (it closed the C
  comment — caught by the first build attempt).
- **SQ-5** `seq_captureTrackTime()`: fills the loop fields under the same
  PRIMASK (`looped` requires `seq_loopActive && !seq_initialSchedulerTick`
  and the track bit in `seq_loopTrackMask`).
- **SQ-6** `seq_placeLoopImage()` added above `seq_placeFromSnapshot()`
  (comment block included); `seq_placeFromSnapshot()` gained `uint8_t
  crossed;`, the `crossed ? 0 : shift` call, the `if (pos.step < 0) return
  0u;` guard, the time-based `rel`, and `if (out->rel > 0)` for `ahead`. The
  function comment was extended; a header comment was added in
  `sequencer.h` above the declaration.

**Gate P3-A PASS:** `make all` clean (no warnings from `sequencer.c`,
`LiveRecord.c` or `sequencer.h`); `text=572,400` (+528 B over P3-0),
`data=412`, `bss=429,104` (**unchanged**); flash payload 572,812 / 753,664 B
(headroom 180,852 B). Stack: `seq_track_time_t` +12 B per local; the locals
do not nest (see §0), so the deepest frame grows by 12 B.

**Host mapping test (scratch, `/private/tmp/s081_p3_loopmap.py`, not
committed): PASS.** `seq_shiftPositionQ8()`, `seq_placeLoopImage()` and the
shift part of `seq_placeFromSnapshot()` were replicated 1:1 in Python and
compared against a simulated TIM3 looped timeline (wrap before the DDA,
snapshot = state after tick `anchor-1`, DDA at `accumulator >= interval`),
in fwd, rev and pip modes, for intervals 3, 6, 7.3 and 24 ticks; lengths
2..24; anchors 1..55; loop lengths 2..48; captures in four passes; whole-tick
deltas `-2L`..`+3L` and sub-tick deltas (fraction 0/255).
**Result: 0 mismatches in 13,210,776 cases**, including the pre-anchor
unlooped-history rule (plan §3.3 revision 4).

### P3-B — Lockout lifted, PERF button rules, PERF Morph recording — DONE

- **LR-6** `liveRec_triggerIn()`: the `seq_loopActive` gate removed; the gate
  list in the function comment no longer names the lockout.
- **LR-7** `liveRec_forceOff()` removed (function + comment block), its
  declaration removed from `LiveRecord.h`; the `liveRec_eraseReleaseHeld()`
  comments in both files no longer name it.
- **MN-1** `menu_liveRecRecordCell()`: `PERFORMANCE_PAGE` removed from the
  page exclusion; the comment bullet and a new FQ5 paragraph added.
- **BH-1** looper-foreground-state comment: any held VOICE holds the loop.
- **BH-2** `buttonHandler_loopCheckDisengage()`: test is now
  `perf_heldVoiceMask != 0u`; comment rewritten for Q1.
- **BH-3** `buttonHandler_loopForceRelease()`: clears `perf_heldVoiceMask`
  and `perf_voiceActionOccurred`; comment extended. Only called when leaving
  PERF or on event-ring overflow, so holds never survive a mode change.
- **BH-4** `buttonHandler_perfVoiceReleased()`: while looping, every release
  is a looper gesture (drop a looped track, set `perf_voiceActionOccurred`,
  run the disengage check) and never toggles a mute.
- **BH-5** `handleVoiceButton()` PERF branch: with the loop engaged the press
  is looper-only — a SELECT held swaps the set, no SELECT joins the track
  with a fresh start (FQ1); the overlay-trigger branch is not reached.
- **BH-6** `handleSelectButton()` PERF case: the engage mask is
  `perf_heldVoiceMask | bh_overlayVoiceMask`; on a successful engage every
  held overlay VOICE ends its gesture (`buttonHandler_overlayRelease()`) and
  becomes a TRACK hold. The `liveRec_forceOff()` / REC-LED block is gone, so
  the record mode is kept (L-2).
- **BH-7** `processPress()` erase router: a PERF VOICE press while the loop
  is engaged is not consumed and falls through to `handleVoiceButton()`; the
  router comment notes the exception.
- **BH-8** `processPress()` `BUT_REC`: the `seq_loopActive` lockout branch
  removed.
- **BH-9** `processRelease()` erase release router: a VOICE release that was
  a PERF hold is not consumed, so a looper TRACK hold ends normally during
  erase; comment extended.
- **BH-10** `buttonHandler_partButtonReleased()` comment: the hold set is
  SELECT, any VOICE or BAR.

**Gate P3-B PASS:** `make all` clean (no warnings from `buttonHandler.c`,
`menu.c`, `LiveRecord.c`); `text=573,072` (+672 B over P3-A), `data=412`,
`bss=429,104` (**unchanged**); `make img` → `build/LXRV2_lxr02.img`
(573,484 B payload). `grep -rn liveRec_forceOff Core/` returns nothing (the
last mention in a BH-6 comment was reworded to satisfy the gate). Flash
headroom 180,180 B.

**Deviation (flagged):** the schedule's BH-6 comment block names
`liveRec_forceOff()`; to keep the gate grep clean the comment says "the P1
interim looper force-off call is removed" instead. No behaviour difference.

### P3-C — Verification matrix — PENDING (hardware, user)

Rows 1–15 of the phase above need the device and the user. Nothing in this
phase is done here.

### P3-D — Documentation at close — DONE

- **`PATTERN_DYNAMIC_STACK.md` §6.5:** the `seq_loopEngage()` row no longer
  says "turns recording off" (it now notes recording is not touched, S081 P3
  L-2); a **Looping while recording (S081 P3)** note was added after the
  invariants: jump entries, loop-aware placement, echo expiry and the single
  ring producer.
- **`MODULE_INTERCHANGE_SPEC.md` "PERF performance surface":** the prose
  "Recording is turned off on engage and REC is ignored while looping" was
  replaced by the Q1/Q4 hold rules, the FQ2 overlay-at-engage rule and L-2;
  the button map gained "VOICE press, looping, no SELECT held" and "VOICE
  (looping)" rows and the overlay row now excludes the engaged loop. The
  `Core/Sequencer/sequencer` table gained a **Live record service** row
  covering `seq_rec_boundary_t.jump`, the `seq_track_time_t` loop fields,
  `seq_placeFromSnapshot()` loop behaviour and the removal of
  `liveRec_forceOff()`.
- **`S081_P1_LIVE_RECORD.md`:** every reference to
  `S081_P2_LOOP_WHILE_RECORD.md` now points to `S081_P3_LOOP_WHILE_RECORD.md`
  (and the bare "P2" labels in that file to "P3"); §9.1 R5 and the
  "Not recorded" list now record the PERF Morph cells (`1VM`..`6VM`, `fxm`)
  and keep `mrp` unrecordable (FQ5/FQ7); M-9 notes the ring now carries each
  step's own fired bit and the jump flag (implemented since P3-0).
- **`MEMORY.md`:** the S080 looper summary line now says the loop is
  sustained by any SELECT/VOICE/BAR until all are released and that live
  record/erase run while looping; a new binding bullet **Looper while
  recording (S081 P3)** states L-2, Q1, Q4/FQ1/FQ2, the single ring producer
  `seq_recRingPush()` (TIM3 for a step advance or a mid-step wrap; foreground
  only under the looper's PRIMASK for a resume) and the FQ5/FQ7 Morph rules.

**Remaining stale cross-references (not in the scheduled list, left for the
user):** `S081_P1_IMPLEMENTATION.md` (lines 21, 1814) and
`S081_P1B_IMPLEMENTATION.md` (lines 323, 1730, 2033) still name
`S081_P2_LOOP_WHILE_RECORD.md`; those files are historical implementation
schedules.

---

## Status

| Phase | State |
|---|---|
| P3-0 | Done; clean build; BSS unchanged; FQ4 row 11 for the user |
| P3-A | Done; clean build; host mapping test 0/13,210,776 |
| P3-B | Done; clean build; `liveRec_forceOff` gone; image built |
| P3-C | Pending (hardware, user) |
| P3-D | Done |
| G-1 (global Morph fix, post-assessment) | Done 2026-10-16; clean build; logs updated; hardware checks 1–4 for the user |

Final build at the P3-B close: `text=573,072`, `data=412`, `bss=429,104`;
flash payload 573,484 / 753,664 B (headroom 180,180 B). At that point static
RAM was unchanged from the pre-P3 baseline (+0 bytes); the only size increase
was the approved +12 B of stack in `seq_track_time_t` and about 1.3 KB of
flash.

**Superseded by G-1 (2026-10-16):** P3 now adds **+7 B static SRAM1**
(`liveRec_morphValue[7]`, approved; `.bss` +8 B with 1 B alignment padding).
Build after G-1, with P4 in the tree: `text=573,296`, `data=416`,
`bss=429,112`; flash payload 573,712 / 753,664 B (headroom 179,952 B);
`build/LXRV2_lxr02.img` written.

---

## Assessment (2026-10-16)

**Verdict: implementation conforms to the schedule. No code defect found;
no code change needed before the hardware matrix (P3-C).** One behaviour
should be confirmed on hardware (A-1).

### What was checked

- **Build:** `sequencer.c`, `LiveRecord.c`, `buttonHandler.c`, `menu.c` and
  `ledHandler.c` forced to rebuild; `make all` clean (no warnings beyond the
  pre-existing libc_nano and `filesystem.c` notices).
  - Sizes: `text=573,160`, `data=412+4` (the +4 is P4), `bss=429,104`.
  - Flash: 573,576 / 753,664 B (headroom 180,088 B).
  - P3 static RAM is +0 B, as scheduled.
- **Gates:** `grep -rn liveRec_forceOff Core/` and
  `grep -rn liveRec_prevValid Core/` are both empty.
- **Step by step against the code:**

| Step | Code | Result |
|---|---|---|
| SQ-1/1b/2, HD-1 | `seq_recRingPush()` (`sequencer.c:1043`), jump bit, decode, `seq_rec_boundary_t.jump` | as scheduled; the advance path's push is the single helper call (`:3209`) |
| LR-1..LR-5 | `liveRec_prevFlags[]` + `LIVEREC_PREV_*`; drain passes the ended step's own fired/jump; `stepEnd(…, jump)`; jump rule in the LATCH not-dirty arm; `paramIn` VALID test | as scheduled; FQ4 fix confirmed (`LiveRecord.c:408-410`) |
| SQ-3 | wrap pushes a jump entry only when `accumulator + 256 < interval` and not SOM (`sequencer.c:5350-5353`) | as scheduled |
| SQ-4 | resume drops the echo mark, pushes a jump entry on a step change (`:5099-5101`) | as scheduled |
| HD-2, SQ-5 | loop fields filled under the capture PRIMASK (`:4733-4739`) | as scheduled |
| SQ-6 | `seq_placeLoopImage()` identical to the verified mapping; `crossed`, `pos.step < 0` guard, time-based `rel`, `ahead` when `rel > 0` | as scheduled |
| LR-6, LR-7 | trigger gate and `liveRec_forceOff()` removed | as scheduled |
| MN-1 | PERFORMANCE_PAGE no longer excluded (`menu.c:14225`) | as scheduled |
| BH-1..BH-10 | hold set, force release, release, press, engage, erase routers, REC | as scheduled |

- **Concurrency:**
  - Foreground ring pushes happen only in `seq_loopResumeTrack()`. Its
    only callers are `seq_loopUpdateMask()` and `seq_loopDisengage()`, and
    both hold PRIMASK, so the ring stays single-producer.
  - `seq_rampBreakTrack()` saves and restores PRIMASK, so nesting inside
    those sections is safe.
- **FQ5 reachability:** `menu_liveRecRecordCell()` still returns early
  while `voiceModeShowMorph` is set. That latch is cleared by every MODE
  press except SHIFT+MODE1 (`buttonHandler.c:1921`), so it is never on in
  PERF and does not block PERF Morph recording.
- **Placement:** the code matches the verified mapping 1:1. The
  implementer's host test covered fwd, rev and pip with sub-tick deltas: 0
  mismatches in 13,210,776 cases. The schedule's fwd-only check was 0 in
  86,593,860.

### Findings

- **A-1 (confirm on hardware): tapping the VOICE of a track that is
  already looping removes it from the loop on release.**
  - FQ1's "add" is a no-op for a track already in the loop, but the VOICE
    still becomes a hold. Its release then drops the track (S080 D1, kept
    by BH-4).
  - After an engage with no VOICE held (all tracks loop), a quick tap of
    VOICE 3 therefore takes track 3 out of the loop.
  - This follows the existing D1 rule, but the plan does not state it.
    Add it to P3-C row 3 and confirm it is wanted.
- **A-2 (by design, plan §3.9):** with `qnt off` or a grid, a track that
  always wraps mid-step gets no LATCH re-stamp or clear on that step while
  the loop runs; only its dirty (adjusted) targets are written. The step is
  handled normally again once it is entered from its start.
- **A-3 (pre-existing, unchanged):** arming REC while running leaves
  `liveRec_prevStep[rtk]` from an earlier take, so the first ring entry ends
  that stale step. With `LIVEREC_AUTOMATION_FIRST_CHANGE` 1 this only
  clears dirty bits. With 0, a pending dirty value could be written into
  the stale step.
- **A-4 (documentation):** the BH-6 comment wording deviation (no
  `liveRec_forceOff` name, to keep the grep gate clean) is harmless.
  - The stale cross-references in `S081_P1_IMPLEMENTATION.md` and
    `S081_P1B_IMPLEMENTATION.md` are historical schedules and can stay.

### Hardware focus for P3-C

- Rows 1–8: the Q1 hold rule and Q4, including SHIFT+VOICE while looping,
  which is a looper gesture.
- Row 6: FQ2 roll end at engage, with the recorded end visible in the step.
- Row 11: FQ4. Stored patterns do not change by themselves; what changes
  is what a LATCH pass writes (see "FQ4 in practice" below).
- Rows 12–14: jump entries with `qnt off`.
- A-1 above.

---

## FQ4 in practice (2026-10-16)

**What the fix changes.** In LATCH, a parameter that is latched and is an
own-Instrument parameter of the recording track is handled at the end of
every step it is not being adjusted on:

- step fired → write the current value again (re-stamp);
- step did not fire → remove its entry (clear).

Before P3-0 the "fired" bit used was the **next** step's, so the decision
for each step was shifted by one.

**Example**, triggers on steps 1 and 3, steps 2 and 4 empty, LATCH on with
`dcy` latched:

| Step | Trigger | Old code (next step's bit) | Fixed code (own bit) |
|---|---|---|---|
| 1 | yes | step 2 empty → **clear** step 1 | **write** step 1 |
| 2 | no | step 3 fired → **write** step 2 | **clear** step 2 |
| 3 | yes | step 4 empty → **clear** step 3 | **write** step 3 |
| 4 | no | step 1 fired → **write** step 4 | **clear** step 4 |

**Why it matters for sound.** An entry on an empty step applies at that
step and is restored away by the next trigger (its restore runs first), so
the old code's entries mostly missed the hits they were meant for. The
fixed code puts the value on the triggered steps, where it shapes the hit.

**What "expect a change" means.**

- Nothing changes in a stored pattern until a LATCH pass runs over it.
- A pattern recorded with the old code holds latched values on the empty
  steps. The next LATCH pass over it (same parameter latched) moves them:
  it writes the triggered steps and clears the empty ones.
- Any hardware test of LATCH done before P3-0 showed the shifted
  behaviour; re-check it against the table above.

---

## Global Morph examination (2026-10-16)

**Rule (user, binding):** the global Morph (`mrp`, `PAR_MORPH`, the PERF
master morph) is never recorded. It must not trigger a record from the menu
or from MIDI. The per-voice Morphs and the FX Morph that a global change
sets are never recorded individually because of it. Changing an individual
voice Morph (`1VM`..`6VM`, VOICE mix `Nvm`) or the FX Morph (`fxm`) records
it to the recording track.

### How a global Morph change propagates

`preset_morphScene()` (`presetManager.c:3618`) writes:

- the Scene's global amount;
- all six per-voice Morph amounts (`scene_setAllVoiceMorphAmounts()`);
- the Effect Morph amount (`scene_setEffectMorphAmount()`).

These are the same stored values that live record reads for the `Nvm` and
`fxm` targets (`liveRec_targetValue7()`).

### Entry points into live record

There is exactly one real entry, `liveRec_paramIn()` from
`menu_liveRecRecordCell()` (`menu.c:14243`). The MIDI call
(`MidiParser.c:151`) passes the invalid target `0xFFFF` and returns at
once.

| Global Morph source | Path | Triggers a record? |
|---|---|---|
| PERF `mrp` knob/encoder | `menu_liveRecRecordCell()` → `menu_cellClearTarget()` returns 0 for `PAR_MORPH` (`menu.c:14069`) | **No** |
| Knob erase / pot clear on `mrp` | same resolver, no target | **No** |
| MIDI CC1 on the global channel | `midiParser_setMorphFromModWheel()` → `preset_morph()` (`MidiParser.c:373-392`); no LiveRecord call | **No** |
| Settings load replay | `menu_sendAllGlobals()` → `menu_parseGlobalParam()` `PAR_MORPH` → `preset_morphScene()` | **No** |
| Scene clear/reset | `menu.c:8731` `preset_morphScene(target, 0)` | **No** |
| Euklid page `PAR_MORPH` cell | page compiled out (`ENABLE_EUKLID_PAGE 0`) | **No** |

| Individual Morph source | Target | Recorded? |
|---|---|---|
| PERF `1VM`..`6VM` (P3 MN-1) | `sceneModTarget_voiceMorphId(slot)` | **Yes** (rtk, T == A) |
| VOICE mix `Nvm` | same target | **Yes** |
| PERF `fxm` (P3 MN-1) | `sceneModTarget_effectMorphId()` | **Yes** |
| Effect page Morph cell (labelled `mrp` on the Effect page, but it is the FX Morph) | `sceneModTarget_effectMorphId()` | **Yes** |
| MIDI CC1 on a voice channel (`preset_morphVoice()`) | — | **No**: MIDI CC recording is not implemented (stub) |

**Result:** a global Morph change never **triggers** a record, from the menu
or from MIDI.

### Leak found: a global change can be written through a pending individual change

Live record does not store the value of an adjustment; it reads the
target's stored value when it writes. For `Nvm`/`fxm` that stored value is
overwritten by a global Morph change. So when an individual Morph change is
**pending** (dirty, not yet written) and the global Morph moves before the
write, the written value is the global Morph's:

- **G-L1, `LIVEREC_AUTOMATION_FIRST_CHANGE` 0 (step-end rule):** `1VM` moved
  in step N, then `mrp` moved in step N → at the end of step N the `mrp`
  value is written into `Nvm`.
- **G-L2, switch 1 (the default), change made with no step known**
  (transport stopped, or just after a playhead break): `1VM` moved, then
  `mrp` moved, then the next step starts → `liveRec_writePendingFirst()`
  writes the `mrp` value.
- **Not leaks:**
  - switch 1 with a step known writes at once, right after the individual
    commit (callers commit first);
  - the LATCH re-stamp never applies to Morph targets: they are Scene
    targets, which the re-stamp rule treats as foreign (clear only).

### Proposed fix G-1 (needs RAM acknowledgement)

Capture the value at the individual edit for the seven Morph targets and
write the captured value, never a re-read:

- **State:** `static uint8_t liveRec_morphValue[7]` (6 voice Morphs + FX
  Morph). **7 B SRAM1, owner LiveRecord, static, runtime-only.**
- **`liveRec_paramIn()`:** for a voice/FX Morph target (descriptor kind
  `SCENE_MOD_TARGET_KIND_VOICE_MORPH` / `…_EFFECT_MORPH`), store
  `liveRec_targetValue7(target)` in the table on every individual edit.
  This is the value the user just set: the menu commits before it calls.
- **Every dirty write** reads through a new
  `liveRec_recordValue7(target)`: for a Morph target it returns the
  captured value, otherwise `liveRec_targetValue7()`. The places are:
  - `liveRec_stepEnd()` (switch 0);
  - `liveRec_writeCurrentValue()` (switch 1, immediate and pending).
- **Result:** a written Morph value is always the last individual edit,
  never the global Morph's.
  - The capture is only read while the target is dirty, and dirty is only
    set by an individual edit, so no stale value can be written.
  - No global-Morph code path needs to know about LiveRecord (Preset stays
    independent of LiveRecord).
- **Alternative without RAM (not recommended):** discard pending Morph
  changes when the global Morph moves. That loses an individual edit made
  just before a global move in the same step, or while stopped, which
  breaks "changing an individual Morph records it".


### G-1 implemented (2026-10-16, RAM approved by the user)

- **`LiveRecord.c`:**
  - `liveRec_morphValue[7]` with its contract block (after the
    latch/dirty/erase bitmaps);
  - `liveRec_morphIndex()`: voice Morph slot 0..5, FX Morph 6, else 0xFF;
  - `liveRec_recordValue7()`: the captured value for a Morph target, else
    `liveRec_targetValue7()`.
  - **Capture:** `liveRec_paramIn()` stores the value on every individual
    Morph edit, after the ownership check and before the first-change early
    return, so a waiting change carries the latest individual value.
  - **Writes:** both dirty writes now use `liveRec_recordValue7()`: the
    step-end write in `liveRec_stepEnd()` (switch 0) and
    `liveRec_writeCurrentValue()` (switch 1, immediate and pending). Their
    comments say so.
  - **Unchanged:** the LATCH re-stamp keeps `liveRec_targetValue7()`. It
    covers only own-Instrument parameters, never a Morph.
- **`LiveRecord.h`:** the `liveRec_paramIn()` declaration comment states the
  rule.
- **Result:**
  - G-L1 and G-L2 are closed. A recorded voice/FX Morph value is always
    the last individual edit.
  - The global Morph still has no recording target and no path into
    LiveRecord.
  - No Preset or MIDI code was touched.
- **Build:** `make all` clean.
  - Sizes: `text=573,296` (+136 B), `data=416`, `bss=429,112`.
  - **BSS +8 B:** the 7 B table plus 1 B alignment padding (`nm -S`:
    `liveRec_morphValue` 7 B, SRAM1). The approval was for 7 B; the 1 B of
    padding is reported here.
  - Flash payload: 573,712 / 753,664 B (headroom 179,952 B).
  - `make img`: `build/LXRV2_lxr02.img` written.
- **Hardware checks (user):**
  1. LATCH or OVERDUB, playing: move `1VM` (PERF) or `Nvm` (VOICE mix):
     recorded to the recording track. Move `mrp`: nothing recorded, and no
     step of any track gains a voice/FX Morph entry.
  2. Transport stopped, REC armed: move `2VM` to a value, then move `mrp`,
     then start. The first step gets the `2VM` value, not the `mrp` value.
  3. With `LIVEREC_AUTOMATION_FIRST_CHANGE 0` (rebuild): in one step move
     `fxm`, then `mrp`. The step gets the `fxm` value.
  4. MIDI CC1 on the global channel during LATCH: nothing recorded.
