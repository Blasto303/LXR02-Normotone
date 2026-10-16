# S081 P1C — Live Record Fix Implementation Schedule

Session 081, part 1C (2026-10-13), branch `dev-ph6-cleanup`.

This schedule fixes the findings of "Code review — P1B implementation" at the
end of `S081_P1B_IMPLEMENTATION.md`:

- must-fix items 1–5;
- minor items 6–14;
- two further defects found while this schedule was written (items 15 and
  16, below).

Item 29 of the P1 review (FX realign on Scene changes that do not reset it)
is still a pending user decision and is not scheduled here.

**Line numbers** are the P1B working tree as reviewed on 2026-10-13 (P1B code
uncommitted on top of `d21e245`). Each entry gives:

- `file:line`;
- the action (**ADD** / **REMOVE** / **MODIFY**);
- a code sketch where the shape matters;
- a comment block to place next to the change as documentation-in-place.

Where a function is declared in a header, the same comment block (or its
first paragraph) goes above the declaration. Where a step replaces an
existing comment block, the new block replaces the old one; it is not added
beside it.

**Authority:**

- `S081_P1_LIVE_RECORD.md` (revision 11), especially §7, §8.7, §8.8, §9.1,
  §10;
- `S081_P1B_IMPLEMENTATION.md` (schedule plus its code review);
- binding invariants in `MEMORY.md` (looper, automation priority, RAM
  policy).

**Build gate after every phase:** `make all` clean, then
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.

## Implementation log (P1C)

Executed against `dev-ph6-cleanup` on top of the uncommitted P1B work. Each
entry records what landed; every change carries the adjacent comment block from
its schedule step. `make all` is the per-phase gate.

### Phase C1 — done (2026-10-13)

- **SC-1** `seq_place_t.snapQ8`; `seq_placeFromSnapshot()` records the trk snap
  shift (snap-up `interval - off`, snap-down `-off`).
- **SC-2** `seq_setEraseMasks()` writes both masks in one PRIMASK section.
- **SC-3** DEV-only `seq_recRingDrops` + a `'W'` cause-5 trace on a full ring.
- **SC-4** the per-track Scene-epoch comment documents the single-caller
  condition (no code change).

Gate: `make all` clean.

### Phase C2 — done (2026-10-13)

- **LC-1/LC-2** `liveRec_eraseFxLanes` split into `liveRec_eraseKnobFxLanes`
  (16-bit, knob erase) and `liveRec_eraseFxSeqCount` (FX SEQ hold);
  `liveRec_pushEraseMasks()` ORs all lanes while the FX SEQ hold is down and no
  longer truncates to 8 bits.
- **LC-3/LC-4/LC-5** `liveRec_eraseParam()` writes the knob lanes;
  `liveRec_eraseFxSeqHeld()` counts; `liveRec_eraseReleaseHeld()` clears only the
  hold counts/specials/full (keeps the knob lanes).
- **LC-6** `liveRec_forceOff()` and `liveRec_eraseEnd()` reset every erase source
  through `liveRec_eraseReleaseHeld()`.
- **LC-7/LC-8/LC-9** roll start delay + span flag; the span writer only runs after
  the start step and never rewrites it; `liveRec_endRoll()` writes the start step
  at its delay with fill measured from the roll's own start (1..100), a later
  step at timing 0 / fill = release delay (1..99), and a quick release as a plain
  trigger at the start step.
- **LC-10/LC-11** the flam grace is re-placed from the main's capture
  (`deltaQ8 + snapQ8` quantised; stored tick + delta un-quantised); the stored
  `seq_place_t` grace is removed.
- **LC-12** header comments updated (`activePatternLocked`, `eraseFxSeqHeld`,
  `eraseReleaseHeld`).

Gate: `make all` clean.

### Phase C3 — done (2026-10-13)

- **BC-1/BC-2** REC does nothing during erase (test moved above SHIFT+REC); the
  SHIFT release ends erase before the latched-menu exits, with an `eraseEnded`
  LED rule.
- **BC-3..BC-6** per-button gesture masks (`bh_eraseSeqMask`,
  `bh_eraseFxSeqMask`, `bh_eraseVoiceMask`); `buttonHandler_liveEraseBegin()` is
  the single entry; the press router sets bits, the release router ends exactly
  that gesture (even after erase ends) and consumes it; a MODE press clears the
  masks.

Gate: `make all` clean.

### Phase C4 — done (2026-10-13)

- **MC-1** `menu_liveRecRecordCell(subPage, position)` tests `menu_activePage`
  (top-level PageNames) for the exclusions; the sub-page parameter is renamed so
  the P1B bug cannot recur.

Gate: `make all` clean. Flash headroom 185,388 B (185,996 B at P1B end).

### Phase C5/C6

Host test (C5) and the documentation updates (C6) are not run here; C6 is prose
work that does not affect the build.

---

## Items found while writing this schedule

**15. The roll span writer overwrites the roll's start step — and can write
a roll before a forward-quantised start (`LiveRecord.c:353-358`).**

- The span writer writes the rolled track's *previous* step whenever a new
  ring entry arrives while the roll is held. It does not test whether that
  step is the start step, or a step before it.
- **Roll started on the current step (`rel == 0`):** when the next step
  begins, `prev` is the start step. It is rewritten with timing 0, so the
  start's micro-timing is lost.
- **Roll quantised forward onto the next step (`rel > 0`):** when that step
  begins, `prev` is the step the hit sounded in. A roll is written there,
  one step before the recorded start.

**16. The SEQ-erase hold count survives erase end (`LiveRecord.c:684-695`,
`:661-671`).**

- `liveRec_eraseEnd()` and `liveRec_forceOff()` do not reset
  `liveRec_eraseSeqCount`.
- Suppose a SEQ button is still held when SHIFT and CLEAR are released. Its
  release then arrives outside ERASE, `liveRec_eraseSeqHeld()` returns early,
  and the count stays at 1.
- In the next erase session:
  - the first SEQ press takes the count to 2 without capturing the active
    track;
  - its release only brings the count back to 1, so the specials bit for
    the stale track stays set;
  - the SEQ erase keeps running until erase ends or a MODE press.

---

## 0. Item map

| Item | Content | Steps |
|---|---|---|
| 1 | FX erase lane mask cut to 8 bits | LC-1 |
| 2 | Knob-erase FX lanes wiped by an FX SEQ release or a MODE press | LC-1, LC-2, LC-3, LC-4, LC-5 |
| 3 | Recordable-page exclusion tests the sub-page index | MC-1 |
| 4 | Roll end written at the wrong step and timing | LC-6, LC-7, LC-9 |
| 5 | Live erase outlives both keys with the SHIFT+REC menu open | BC-1, BC-2 |
| 6 | `trk` flam grace placed from the unsnapped time | SC-1, LC-10 |
| 7 | Roll fill on the start step measured from the step start | LC-9 |
| 8 | Un-quantised flam grace committed with a stale placement | LC-10, LC-11 |
| 9 | Dead grace state (`graceDeltaQ8`, `graceTick`) | LC-10, LC-11 (now used) |
| 10 | SEQ/VOICE erase releases after a mode change; FX SEQ hold not counted | BC-3, BC-4, BC-5, BC-6, LC-4 |
| 11 | Erase masks published as two separate writes | SC-2 |
| 12 | Scene-epoch bump in realign: accepted, document the single-caller condition | SC-4 |
| 13 | Ring-full drops not counted | SC-3 |
| 14 | `liveRec_activePatternLocked()` header comment | LC-12 |
| 15 | Span writer overwrites the start step / writes before a forward start | LC-7, LC-8 |
| 16 | SEQ-erase count survives erase end | LC-5, LC-6, BC-3 |

---

## Phase C1 — Sequencer

**Files:** `Core/Sequencer/sequencer.c`, `Core/Sequencer/sequencer.h`

### SC-1 Placement reports the snap shift (item 6) — MODIFY `seq_place_t` at `sequencer.h:442-455`; MODIFY `seq_placeFromSnapshot()` at `sequencer.c:3954-4036`

**`sequencer.h:450-455`** — ADD a field:

```c
typedef struct {
    uint8_t  step;
    uint8_t  delay;
    int8_t   rel;
    uint16_t ahead;
    int16_t  snapQ8;   /* P1C: Q8 shift applied by the trk snap (0 otherwise) */
} seq_place_t;
```

Replace the comment above the typedef (`sequencer.h:442-449`):

```
/*
 * One placed live hit (S081 §8.3, P1B item 5, P1C item 6).
 *
 * step  landing step index. delay  micro-timing 0..99 from the played start.
 * rel  -1 behind the current step, 0 current, +1 ahead. ahead  whole ticks
 * from capture to the landed step start when rel > 0. snapQ8  the signed Q8
 * shift the trk snap added to deltaQ8 (0 without snap): deltaQ8 + snapQ8 is
 * the snapped hit's offset from the capture. Why snapQ8: a quantised flam
 * places its grace 1/32 before the SNAPPED main; without it the grace was
 * placed from the raw hit time under trk. Affiliates: liveRec_placeFlam().
 */
```

**`sequencer.c:3965`** — ADD the local `int32_t snapQ8 = 0;`.

**`sequencer.c:4000-4008`** (the `if (!clamp99 && snapToStep)` block) — record
the shift that the block applies:

```c
if (!clamp99 && snapToStep) {
    if ((uint32_t)off * 2u >= (uint32_t)interval) {
        snapQ8 = (int32_t)interval - (int32_t)off;
        steps += seq_shiftPositionQ8(track, &pos, snapQ8);
        off = pos.accum;
    } else {
        snapQ8 = -(int32_t)off;
        off = 0u;
    }
}
```

**`sequencer.c:4022-4025`** — ADD `out->snapQ8 = (int16_t)snapQ8;` next to
`out->step`. The value is bounded by half an interval (≤ 32767 for any
16-bit Q8 interval), so it fits `int16_t`.

Extend the function comment's "Outputs" line to read
`*out (step, delay, rel, ahead, snapQ8)`.

Stack only: `seq_place_t` grows from 6 B to 8 B. No static instance is left
after LC-11.

### SC-2 Publish both erase masks together (item 11) — MODIFY `seq_setEraseMasks()` at `sequencer.c:1029-1040`

```c
void seq_setEraseMasks(uint8_t specials, uint8_t full)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    seq_eraseSpecialsMask = specials;
    seq_eraseFullMask = full;
    __set_PRIMASK(primask);
}
```

Replace the comment at `:1030-1035`:

```
/*
 * Set the per-track live-erase masks (S081 §10, C-4; P1C item 11).
 *
 * What: stores the specials and full erase masks as one update. TIM3 reads
 * them to clear bit 15 + specials (specials) or everything (full). Why: the
 * two stores were separate, so TIM3 could act on one new mask and one old
 * mask for one tick (for example, a TRACK hold turning into a SEQ hold).
 * Context: foreground; a two-store PRIMASK section. Caller:
 * liveRec_pushEraseMasks().
 */
```

### SC-3 DEV-only count of ring-full drops (item 13) — ADD after `sequencer.c:860`; MODIFY the ring push at `sequencer.c:3028-3037`

**After `:860`:**

```c
#if DEV_MODE_LOGGING
static volatile uint16_t seq_recRingDrops;
#endif
```

```
/*
 * Step-boundary ring overflow count (S081 H-3, P1C item 13). DEV only.
 *
 * What: the number of ring entries dropped because the 16-entry ring was
 * full; every drop also records a PatternTrace 'W' event with cause 5
 * (ring full) and (track << 8) | step. Why: a dropped entry merges two
 * steps for LiveRecord's step-end table and loses a roll-span write; the
 * P1B log claimed this was counted, but the push dropped silently.
 * RAM: 2 B DEV-only (logging-allocation policy). Context: written in TIM3.
 * Affiliates: the ring push in the step-advance path, liveRec_drop()
 * causes 1–4, decode_devlogs.py.
 */
```

**`:3030-3037`** — ADD an `else` branch to `if (next != seq_recRingTail)`:

```c
} else {
#if DEV_MODE_LOGGING
    seq_recRingDrops++;
    patternTrace_record(PAT_TRACE_STAGE_LIVEREC_DROP, 5u,
                        (uint32_t)(((uint32_t)track << 8u) |
                                   (uint8_t)(seq_stepIndex[track] & 0x7F)));
#endif
}
```

Change the sentence "A full ring drops the entry." in the comment block
above (`:3015-3024`) to "A full ring drops the entry (DEV: counted, trace
cause 5, P1C item 13)." `patternTrace_record()` is documented as safe from
TIM3 (`PatternTrace.h`).

### SC-4 Scene-epoch bump site is accepted (item 12) — MODIFY the comment at `sequencer.c:3683`

There is no code change. Replace the one-line comment with:

```
/*
 * A per-track Scene change advances the Scene epoch (S081 P1B item 10,
 * P1C item 12).
 *
 * What: LiveRecord starts a new take when this epoch moves. Why here and
 * not in seq_setTrackPlayedScene(): today the only per-track Scene change
 * is the buttonHandler Scene path, which sets the played Scene and then
 * realigns, so both sites see the same events. If a second caller of
 * seq_setTrackPlayedScene() appears without a realign, move this bump into
 * seq_setTrackPlayedScene(). Affiliates: seq_getSceneEpoch(),
 * liveRec_service() step 1.
 */
```

Gate: `make all` clean.

---

## Phase C2 — LiveRecord

**Files:** `Core/Sequencer/LiveRecord.c`, `Core/Sequencer/LiveRecord.h`

### LC-1 FX erase lanes: full 16 bits, knob lanes and the FX SEQ hold combined (items 1, 2) — MODIFY `liveRec_pushEraseMasks()` at `LiveRecord.c:177-179`

```c
effects_setLiveEraseLanes(
    (uint16_t)((liveRec_target == scene_getActiveIndex())
               ? (uint16_t)(liveRec_eraseKnobFxLanes |
                            ((liveRec_eraseFxSeqCount != 0u) ? 0xFFFFu : 0u))
               : 0u));
```

Add to the comment inside the function (after "the FX-sequence erase lanes
apply only when T == A."):

```
 * The FX lane mask is 16 bits (lane 0 = Morph, 1..15 = parameter lanes):
 * the knob-erase lanes OR all lanes while an FX-mode SEQ hold is down
 * (P1C items 1/2). P1B cast it to 8 bits, dropping lanes 8..15.
```

### LC-2 Separate knob-erase lanes from the FX SEQ hold (item 2) — MODIFY `LiveRecord.c:118-121`

Rename `liveRec_eraseFxLanes` to `liveRec_eraseKnobFxLanes`, and ADD
`static uint8_t liveRec_eraseFxSeqCount;`.

```c
/* Live-erase masks owned by LiveRecord, pushed to the Sequencer/Effects (G). */
static uint8_t  liveRec_eraseSpecials;
static uint8_t  liveRec_eraseFull;
static uint16_t liveRec_eraseKnobFxLanes;
static uint8_t  liveRec_eraseFxSeqCount;
```

```
/*
 * FX-sequence erase sources (S081 §10.2, P1C item 2).
 *
 * What: liveRec_eraseKnobFxLanes holds the lanes chosen by knob erase; they
 * stay until erase ends. liveRec_eraseFxSeqCount counts the SEQ buttons held
 * in FX mode; while nonzero every lane is erased. liveRec_pushEraseMasks()
 * combines the two. Why: P1B kept both in one mask, so releasing an FX SEQ
 * hold, or any MODE press, also dropped the knob-erase lanes, which §10.2
 * keeps "until erase ends". A count (not a flag) keeps the hold on until the
 * last of several held SEQ buttons is released (item 10). RAM: +1 B.
 * Affiliates: liveRec_eraseParam(), liveRec_eraseFxSeqHeld(),
 * liveRec_eraseReleaseHeld(), liveRec_pushEraseMasks().
 */
```

### LC-3 Knob erase writes the knob lanes (item 2) — MODIFY `liveRec_eraseParam()` at `LiveRecord.c:1115`

`liveRec_eraseFxLanes |=` becomes `liveRec_eraseKnobFxLanes |=`. Replace
the one-line comment at `:1109` (move it above the function):

```
/*
 * Knob erase target mark (S081 §10.2, G-2; P1C item 2).
 *
 * What: adds the target to the erase bitmap (removed from rtk step by step)
 * and, for an Effect cell, its FX lane to the knob-erase lane mask. Both
 * stay until erase ends; SEQ/TRACK releases and MODE presses do not clear
 * them. Affiliates: menu_parseKnobDelta() (erase branch),
 * liveRec_pushEraseMasks().
 */
```

### LC-4 FX SEQ hold is counted (items 2, 10) — MODIFY `liveRec_eraseFxSeqHeld()` at `LiveRecord.c:1151-1158`

```c
void liveRec_eraseFxSeqHeld(uint8_t held)
{
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    if (held)
        liveRec_eraseFxSeqCount++;
    else if (liveRec_eraseFxSeqCount > 0u)
        liveRec_eraseFxSeqCount--;
    liveRec_pushEraseMasks();
}
```

```
/*
 * FX-mode SEQ hold erase (S081 §10.2, P1C items 2/10).
 *
 * What: while any SEQ button is held in FX mode, every lock lane of each FX
 * step the FX playhead reaches is erased. Counted per held button; the
 * knob-erase lanes are separate and untouched. Why: P1B wrote 0 on any
 * release, ending the hold while another SEQ button was still down and
 * wiping knob-erase lanes. Accessors: buttonHandler erase router (BC-4,
 * BC-5). Affiliates: liveRec_pushEraseMasks().
 */
```

Use the first paragraph as the header comment at `LiveRecord.h:114` (above the declaration at `:115`).

### LC-5 Release-held clears only the hold gestures (items 2, 16) — MODIFY `liveRec_eraseReleaseHeld()` at `LiveRecord.c:1182-1189`

```c
void liveRec_eraseReleaseHeld(void)
{
    liveRec_eraseSeqCount = 0u;
    liveRec_eraseFxSeqCount = 0u;
    liveRec_eraseSpecials = 0u;
    liveRec_eraseFull = 0u;
    liveRec_pushEraseMasks();
}
```

Replace the comment at `:1171-1181`:

```
/*
 * Release every held erase gesture (S081 §10.1, P1B item 8, P1C 2/16).
 *
 * What: drops the SEQ-hold (count, specials), TRACK-hold (full) and FX
 * SEQ-hold (count) gestures at once. Erase mode, the knob-erase bitmap and
 * the knob-erase FX lanes are kept. Why: a mode change re-maps SEQ and
 * VOICE buttons, so their held gestures must stop. It is also the gesture
 * reset at erase end and force-off, so a button still held then cannot
 * leave a count that stretches the next erase session's SEQ hold.
 * Accessors: handleModeButtons(), liveRec_eraseEnd(), liveRec_forceOff().
 * Affiliates: liveRec_pushEraseMasks().
 */
```

Update the header comment at `LiveRecord.h:117-118` (above `:119`) to the first paragraph.

### LC-6 Erase end and force-off reset every erase source (item 16) — MODIFY `liveRec_forceOff()` at `LiveRecord.c:661-671` and `liveRec_eraseEnd()` at `:684-695`

In both functions, replace the three lines
`liveRec_eraseSpecials = 0u; liveRec_eraseFull = 0u; liveRec_eraseFxLanes = 0u; liveRec_pushEraseMasks();`
with:

```c
liveRec_eraseKnobFxLanes = 0u;
liveRec_eraseReleaseHeld();          /* counts, specials, full; pushes */
```

The `memset(liveRec_erase, …)` stays. Comment, above `liveRec_eraseEnd()`:

```
/*
 * End live erase (S081 §10.1, P1C item 16).
 *
 * What: clears every erase source (knob bitmap, knob FX lanes, SEQ/FX-SEQ
 * hold counts, TRACK holds), pushes the empty masks, restores the prior
 * LATCH/OFF mode and starts a new take. Why: P1B left the SEQ-hold count
 * set when a SEQ button was still held at erase end; its release then
 * arrived outside erase and was ignored, so the next session's SEQ hold
 * captured no track and never ended. Accessors:
 * buttonHandler_liveEraseEnd(). Affiliates: liveRec_eraseReleaseHeld().
 */
```

`liveRec_forceOff()` gets the same block, with its first line changed to
"Force record and erase off (S081 P1 looper interim; P1C item 16)" and the
accessor changed to `handleSelectButton()` (BH-7).

### LC-7 Roll start state: start delay and span flag (items 4, 15) — MODIFY `LiveRecord.c:65-74`; MODIFY the fast-roll start at `:998-1012`

**`:72-74`** — ADD after `liveRec_rollIndex`:

```c
static uint8_t  liveRec_rollStartDelay = 0u;
static uint8_t  liveRec_rollSpan = 0u;
```

Replace the comment at `:65-71`:

```
/*
 * Open fast-roll capture (S081 §8.8, E-4; P1C items 4/15).
 *
 * What: while a fast roll is held, liveRec_rollHeld is 1 and:
 *  - liveRec_rollTrack/rollStep/rollStartDelay give the start step and
 *    its micro-timing as written at the start;
 *  - liveRec_rollIndex is the current rate;
 *  - liveRec_rollSpan is 1 once the playhead has reached the start step
 *    (at once when the start is the current step or behind it; at the
 *    start step's ring entry when the hit was quantised forward).
 * The span writer only writes steps after the start, and only while
 * liveRec_rollSpan is set. Why: P1B rewrote the start step with timing 0
 * when it ended, and wrote a roll onto the step before a forward-quantised
 * start; the end writer had no start delay to keep. RAM: +2 B.
 * Affiliates: liveRec_triggerIn() (start), liveRec_service() (span),
 * liveRec_endRoll().
 */
```

**`:1004`** (after `liveRec_rollStep = pl.step;`) — ADD:

```c
liveRec_rollStartDelay = pl.delay;
liveRec_rollSpan = (uint8_t)(pl.rel <= 0);
```

### LC-8 Span writer starts after the start step (item 15) — MODIFY `liveRec_service()` at `LiveRecord.c:353-358` and `:361-364`

```c
if (liveRec_rollHeld && b.track == liveRec_rollTrack &&
    liveRec_rollSpan && prev != liveRec_rollStep) {
    liveRec_writeStep(liveRec_rollTrack, prev, liveRec_rollNote,
                      liveRec_rollNoteDefault, liveRec_rollVel,
                      0u, liveRec_rollIndex, 100u, seq_fillActive);
}
```

After the `liveRec_prevStep[b.track] = b.step;` block (`:361-364`), ADD:

```c
if (liveRec_rollHeld && b.track == liveRec_rollTrack &&
    b.step == liveRec_rollStep)
    liveRec_rollSpan = 1u;        /* a forward-quantised start is reached */
```

Replace the comment block at `:341-352`:

```
/*
 * Extend a held fast roll over the step that just ended
 * (S081 §8.8 E-4, P1B items 3/16, P1C item 15).
 *
 * What: when the rolled track's next ring entry arrives, its previous step
 * has ended under the held roll. That step is written as an active step
 * with the roll rate, timing 0 and fill 100 (carry-over), but only if the
 * roll's start step has been reached (liveRec_rollSpan) and the ended step
 * is not the start step itself. The start step keeps the delay and fill it
 * was written with at the start; the end step is written by
 * liveRec_endRoll(). fil applies while PERF is held. Why: the steps the
 * roll spans must replay it. P1B rewrote the start step with timing 0 and,
 * for a forward-quantised start, wrote a roll on the step before it.
 * Affiliates: seq_takeRecBoundary(), liveRec_writeStep(), LC-7 state.
 */
```

The ring entry for the start step can be dropped when the ring is full
(SC-3). In that case `liveRec_rollSpan` stays 0 and the release records a
plain trigger (LC-9). This is accepted and is visible as trace cause 5.

### LC-9 Roll end: start step and start delay; fill from the roll's own start (items 4, 7) — MODIFY `liveRec_endRoll()` at `LiveRecord.c:850-890`

```c
static void liveRec_endRoll(uint8_t writeEnd)
{
    if (!liveRec_rollHeld) {
        liveRec_rollPendingIdx = 0u;
        return;
    }
    if (writeEnd && seq_isRunning()) {
        seq_track_time_t snap;
        seq_place_t pl;

        seq_captureTrackTime(liveRec_rollTrack, &snap);
        if (seq_placeFromSnapshot(liveRec_rollTrack, &snap,
                                  liveRec_frac8(&snap), 0u, &pl)) {
            if (liveRec_rollHits == 0u || !liveRec_rollSpan) {
                /* Released before the second hit, or before a forward-
                 * quantised start: the start step becomes a plain trigger. */
                liveRec_writeStep(liveRec_rollTrack, liveRec_rollStep,
                                  liveRec_rollNote, liveRec_rollNoteDefault,
                                  liveRec_rollVel, liveRec_rollStartDelay,
                                  0u, 0u, seq_fillActive);
            } else if (pl.step == liveRec_rollStep) {
                /* Released on the start step: keep its delay; the fill is
                 * the part of the step from the roll's start to the release. */
                int16_t f = (int16_t)pl.delay - (int16_t)liveRec_rollStartDelay;

                liveRec_writeStep(liveRec_rollTrack, liveRec_rollStep,
                                  liveRec_rollNote, liveRec_rollNoteDefault,
                                  liveRec_rollVel, liveRec_rollStartDelay,
                                  liveRec_rollIndex,
                                  (uint8_t)((f < 1) ? 1 : ((f > 100) ? 100 : f)),
                                  seq_fillActive);
            } else {
                /* Released on a later step: it starts at its boundary. */
                liveRec_writeStep(liveRec_rollTrack, pl.step,
                                  liveRec_rollNote, liveRec_rollNoteDefault,
                                  liveRec_rollVel, 0u, liveRec_rollIndex,
                                  (uint8_t)((pl.delay < 1u) ? 1u : pl.delay),
                                  seq_fillActive);
            }
        }
    }
    seq_setLiveHold(liveRec_rollTrack, 0u);
    liveRec_rollHeld = 0u;
    liveRec_rollSpan = 0u;
    liveRec_rollPendingIdx = 0u;
}
```

`pl.delay` is 0..99, so the later-step fill is 1..99. The start-step fill
is clamped to 1..100.

Replace the comment at `:836-849`:

```
/*
 * End a recorded fast roll (S081 §8.8, P1B item 16, P1C items 4/7).
 *
 * What: places the release on the rolled track and writes one step:
 *  - released before the second hit, or before a forward-quantised start
 *    step was reached: the START step is rewritten as a plain trigger at
 *    its start delay (roll and fill removed);
 *  - released on the start step: the start step keeps its delay and gets
 *    fill = release delay - start delay (1..100);
 *  - released on a later step: that step gets timing 0 (a spanned step
 *    starts at its boundary) and fill = release delay (1..99).
 * The live hold on the rolled track is lifted. With writeEnd = 0 (stop,
 * Scene change, realign: the playhead has jumped) nothing is written.
 * Why: the Sequencer runs a roll's fill window from its delayed first hit
 * (seq_rollBegin()), so on the start step the fill must be measured from
 * the start delay. P1B wrote the quick-release trigger at the release
 * point, and micro-timed the end step by the release offset. Inputs:
 * liveRec_roll* state (LC-7), the live capture. Outputs: one write, live
 * hold off. Accessors: liveRec_rollRelease(), liveRec_rollRateChange(),
 * liveRec_service(). Affiliates: seq_placeFromSnapshot(), seq_setLiveHold(),
 * pat_recordStep() (fill 100 stored as absent, PD-1).
 */
```

### LC-10 Flam grace placed from the main's capture (items 6, 8, 9) — MODIFY `liveRec_placeFlam()` at `LiveRecord.c:813-835`

```c
static void liveRec_placeFlam(uint8_t track, const seq_track_time_t *snap,
                              const seq_place_t *mainPl, int32_t mainDeltaQ8,
                              const liveRec_trigger_t *t, uint8_t setFil)
{
    seq_place_t gracePl;
    uint8_t haveGrace = 0u;

    if (seq_getQuantisation() != NO_QUANTISATION) {
        /* Quantised: 12 master ticks (1/32) before the snapped main. */
        haveGrace = seq_placeFromSnapshot(
            track, snap, mainDeltaQ8 + mainPl->snapQ8 - (12 * 256), 0u,
            &gracePl);
    } else if (liveRec_graceValid) {
        /* Un-quantised: the grace's own time, re-placed now. */
        int32_t graceQ8 = liveRec_graceDeltaQ8 +
            (int32_t)(liveRec_graceTick - snap->elapsedTick) * 256;

        haveGrace = seq_placeFromSnapshot(track, snap, graceQ8, 0u, &gracePl);
    }
    if (haveGrace && gracePl.step != mainPl->step)
        (void)liveRec_commitHit(track, &gracePl, t->note, t->note_is_default,
                                liveRec_graceVel, 0u, setFil);
    liveRec_graceValid = 0u;
    (void)liveRec_commitHit(track, mainPl, t->note, t->note_is_default,
                            t->velocity, 0u, setFil);
}
```

`liveRec_graceTick - snap->elapsedTick` is an unsigned difference cast to
`int32_t`. It is a small negative number, because the grace precedes the
main by 1/32 of the overlay timer. A position break between the two clears
`liveRec_graceValid` (`liveRec_service()` step 2), so the subtraction never
spans a jump.

Replace the comment at `:799-812`:

```
/*
 * Record a flam as two steps (S081 R9/Q9, P1B item 5, P1C items 6/8).
 *
 * What: both hits are placed from the MAIN's capture, so the echo rules
 * see the playhead as it is when they are written:
 *  - quantised (1/8..1/64 or trk): the grace is 12 master ticks (1/32)
 *    before the main's quantised or snapped position (deltaQ8 + snapQ8);
 *  - un-quantised: the grace's stored capture (tick + sub-tick) is
 *    re-placed relative to the main's capture.
 * If both land in one step, only the main is written. Why: under trk the
 * grace was placed from the raw hit time, not the snapped main; un-
 * quantised, P1B committed the grace with the placement taken at grace
 * time, so its rel/ahead (echo deadline, cancel) were up to 1/32 stale.
 * Inputs: main snapshot and placement, liveRec_grace* (LC-11). Outputs:
 * one or two writes. Accessors: liveRec_triggerIn() (FLAM_MAIN).
 * Affiliates: seq_placeFromSnapshot(), liveRec_commitHit().
 */
```

### LC-11 Grace state: capture only (items 8, 9) — MODIFY `LiveRecord.c:76-82`; MODIFY the FLAM_GRACE branch at `:1014-1020`

- **REMOVE** `static seq_place_t liveRec_gracePl;` (`:78`).
- Keep `liveRec_graceDeltaQ8`, `liveRec_graceTick`, `liveRec_graceVel` and
  `liveRec_graceValid`; LC-10 now reads the first two.
- In the FLAM_GRACE branch, REMOVE `liveRec_gracePl = pl;`.

The grace is still placed at capture time (the earlier
`seq_placeFromSnapshot()` call in `liveRec_triggerIn()`). That call is only
the "track has started" gate; its result is not kept.

Replace the comment at `:76-77`:

```
/*
 * Pending flam grace (S081 §8.7; P1B item 5, P1C items 8/9).
 *
 * What: the grace hit's capture, as the master tick and the Q8 delta from
 * it (quantised or sub-tick), plus its velocity. The main hit re-places it
 * from its own capture (liveRec_placeFlam()). Cleared at every position
 * break and after each main. Why: a placement stored at grace time had a
 * stale rel/ahead by the time it was committed. RAM: 10 B
 * (the 6 B stored placement is removed).
 */
```

### LC-12 Header comments (item 14) — MODIFY `LiveRecord.h:99`, `:114`, `:117-118`

**ADD above `uint8_t liveRec_activePatternLocked(void);` (`:99`):**

```
/* Is the active Scene's Pattern closed for editing? True only while live
** record or erase is writing (armed and running) AND the recording target
** T = P(rtk) differs from the active Scene A (S081 Q13, P1B item 1). */
```

`:114` (`liveRec_eraseFxSeqHeld`) and `:117-118`
(`liveRec_eraseReleaseHeld`) take the first paragraphs of LC-4 and LC-5.

Also update the P1B state comment block at `LiveRecord.c:92-101`:

- add the LC-2 and LC-7 state to its list;
- change "RAM about 10 B" to "RAM about 14 B (P1B 10 B + P1C 4 B)".

Gate: `make all` clean.

---

## Phase C3 — buttonHandler

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

### BC-1 REC does nothing while erase is enabled, SHIFT or not (item 5) — MODIFY `processPress()` `case BUT_REC:` at `buttonHandler.c:2705-2723`

Move the ERASE test above the SHIFT+REC test. The menu-exit test stays
first, so a SHIFT+REC menu already open can still be closed with REC.

```c
case BUT_REC:
    if (bh_shiftRecMenuActive) {
        /* S080 E7: REC exits the SHIFT+REC settings menu. */
        buttonHandler_shiftRecMenuExit();
        break;
    }
    if (liveRec_mode() == LIVEREC_MODE_ERASE) {
        /* Live erase is enabled: REC does nothing (S081 §10.3). */
        break;
    }
    if (buttonHandler_getShift()) {
        /* S080 E7: SHIFT+REC opens the settings menu (overlay + quant). */
        buttonHandler_shiftRecMenuEnter();
    } else if (seq_loopActive) {
        ...
```

```
/*
 * REC while live erase is enabled (S081 §10.3, P1C item 5).
 *
 * What: REC does nothing while erase is on, including with SHIFT held
 * (which it almost always is during erase). An already-open SHIFT+REC menu
 * still closes. Why: SHIFT+REC was tested first, so REC during erase opened
 * the settings menu; the menu then swallowed the SHIFT release and erase
 * stayed on with both keys up. Affiliates: processRelease() BUT_SHIFT
 * (BC-2), buttonHandler_shiftRecMenuEnter().
 */
```

### BC-2 SHIFT release ends erase before the latched-menu exits (item 5) — MODIFY `processRelease()` `case BUT_SHIFT:` at `buttonHandler.c:3197-3241`

New order:

```c
case BUT_SHIFT: {
    uint8_t eraseEnded = 0u;

    bh_shiftHeld = 0u;
    if (liveRec_mode() == LIVEREC_MODE_ERASE && !bh_clearHeld) {
        buttonHandler_liveEraseEnd();             /* SHIFT/CLEAR LEDs off */
        eraseEnded = 1u;
    }
    if (bh_shiftReleaseConsume) {
        bh_shiftReleaseConsume = 0u;
        led_setValue(0u, LED_SHIFT);
        break;
    }
    if (bh_shiftRecMenuActive || menu_chainMenuIsOpen()) {
        if (eraseEnded)
            led_setValue(1u, LED_SHIFT);          /* latched menu keeps it lit */
        break;
    }
    if (liveRec_mode() == LIVEREC_MODE_ERASE)
        led_setValue(1u, LED_SHIFT);              /* CLEAR still held */
    else
        led_setValue(0, LED_SHIFT);

    switch (bh_state.selectButtonMode) {           /* unchanged */
    ...
    }
    break; }
```

The block braces are needed for the local `eraseEnded`. Keep the existing S080
latched-menu comment block above the consume test. Replace the two P1B
comment blocks ("SHIFT hold flag tracks the physical key" and "SHIFT
release during live erase") with one block above `bh_shiftHeld = 0u;`:

```
/*
 * SHIFT release and live erase (S081 §10.1, P1B item 12, P1C item 5).
 *
 * What: the hold flag is cleared first; then, if erase is on and CLEAR is
 * also free, erase ends here, before the latched-menu exits. The menu exits
 * keep their LED rules (consume: off; open menu: stays lit). If erase is on
 * and CLEAR is still held, the SHIFT LED stays lit. Why: the consume and
 * open-menu breaks ran before the erase test, so with the SHIFT+REC menu
 * open (BC-1) or a CLEAR press made inside it, releasing both keys left
 * erase on. Affiliates: BC-1, buttonHandler_liveEraseEnd(),
 * processRelease() BUT_COPY.
 */
```

### BC-3 Erase begin helper and per-button gesture masks (items 10, 16) — ADD after `buttonHandler.c:388`; ADD `buttonHandler_liveEraseBegin()` before `:1133`; MODIFY the COPY press at `:2750-2758` and the SHIFT press at `:2880-2883`

**After `:388`:**

```c
static uint16_t bh_eraseSeqMask;
static uint16_t bh_eraseFxSeqMask;
static uint8_t  bh_eraseVoiceMask;
```

```
/*
 * Buttons holding a live-erase gesture (S081 §10.2, P1C items 10/16).
 *
 * What: one bit per SEQ button (bh_eraseSeqMask: an active-track SEQ hold
 * in VOICE/STEP; bh_eraseFxSeqMask: an FX-sequence hold in FX) and per
 * TRACK button (bh_eraseVoiceMask) whose press started an erase gesture.
 * A release ends a gesture only if its bit is set, whatever the mode or
 * erase state is by then. Cleared at a MODE press during erase (the
 * gestures were released by liveRec_eraseReleaseHeld()) and when erase
 * starts from off. Why: releases were routed by the mode at release time,
 * so after a mode change an old button's release ended a gesture started
 * by a new button, and a release after erase end was lost. RAM: 5 B.
 * Affiliates: processPress()/processRelease() erase routers,
 * handleModeButtons(), buttonHandler_liveEraseBegin().
 */
```

**Before `:1133`:**

```c
static void buttonHandler_liveEraseBegin(void)
{
    if (liveRec_mode() != LIVEREC_MODE_ERASE) {
        bh_eraseSeqMask = 0u;
        bh_eraseFxSeqMask = 0u;
        bh_eraseVoiceMask = 0u;
    }
    liveRec_eraseBegin();
    led_setValue(1u, LED_SHIFT);
    led_setValue(1u, LED_COPY);
}
```

```
/*
 * Start live erase from the key-press paths (S081 §10.1, P1C item 16).
 *
 * What: enables erase (LiveRecord ends an overdub and keeps the prior
 * LATCH/OFF) and lights SHIFT and CLEAR. Entering from off also clears the
 * gesture masks, so a button still held from an earlier session cannot end
 * a gesture of this one. A repeat call while erase is already on (SHIFT
 * pressed again with CLEAR held) keeps the masks. Why: one entry point for
 * both key orders, mirroring buttonHandler_liveEraseEnd(). Affiliates:
 * liveRec_eraseBegin(), BC-3 masks.
 */
```

**`:2750-2758`** (COPY press, armed branch): replace
`liveRec_eraseBegin(); led_setValue(1u, LED_SHIFT); led_setValue(1u, LED_COPY);`
with `buttonHandler_liveEraseBegin();`. Keep the comment.

**`:2880-2883`** (SHIFT press): replace the braced
`liveRec_eraseBegin(); led_setValue(1u, LED_COPY);` with
`buttonHandler_liveEraseBegin();`. The following `led_setValue(1, LED_SHIFT)`
stays.

### BC-4 Press router sets the gesture bit (item 10) — MODIFY `buttonHandler.c:2598-2611`

```c
if (eraseSeq >= 0) {
    uint16_t bit = (uint16_t)(1u << eraseSeq);

    if (eraseMode == SELECT_MODE_VOICE || eraseMode == SELECT_MODE_STEP) {
        bh_eraseSeqMask |= bit;
        liveRec_eraseSeqHeld(menu_getActiveVoice(), 1u);
    } else if (eraseMode == SELECT_MODE_FX) {
        bh_eraseFxSeqMask |= bit;
        liveRec_eraseFxSeqHeld(1u);
    }
    /* PERF, LOAD_SAVE, MENU, SOM_GEN: consumed */
    return;
}
if (eraseVoice >= 0) {
    if (eraseMode == SELECT_MODE_VOICE || eraseMode == SELECT_MODE_STEP ||
        eraseMode == SELECT_MODE_PERF  || eraseMode == SELECT_MODE_FX) {
        bh_eraseVoiceMask |= (uint8_t)(1u << eraseVoice);
        liveRec_eraseTrackHeld((uint8_t)eraseVoice, 1u);
    }
    /* other modes: consumed */
    return;
}
```

Add to the router comment (`:2577-2592`) after the table:

```
 * Each press that starts a gesture sets its button's bit in
 * bh_eraseSeqMask / bh_eraseFxSeqMask / bh_eraseVoiceMask; the release
 * edge ends exactly that gesture (P1C item 10).
```

### BC-5 Release router ends the gesture by bit (items 10, 16) — MODIFY `buttonHandler.c:3006-3032`

The router no longer requires ERASE to end a gesture; it still consumes every
SEQ/VOICE release while ERASE.

```c
{
    int8_t eraseSeq = btn_to_seq(buttonNr);
    int8_t eraseVoice = btn_to_voice(buttonNr);

    if (eraseSeq >= 0) {
        uint16_t bit = (uint16_t)(1u << eraseSeq);

        if ((bh_eraseSeqMask & bit) != 0u) {
            bh_eraseSeqMask &= (uint16_t)~bit;
            liveRec_eraseSeqHeld(menu_getActiveVoice(), 0u);
            return;
        }
        if ((bh_eraseFxSeqMask & bit) != 0u) {
            bh_eraseFxSeqMask &= (uint16_t)~bit;
            liveRec_eraseFxSeqHeld(0u);
            return;
        }
    }
    if (eraseVoice >= 0) {
        uint8_t bit = (uint8_t)(1u << eraseVoice);

        if ((bh_eraseVoiceMask & bit) != 0u) {
            bh_eraseVoiceMask &= (uint8_t)~bit;
            liveRec_eraseTrackHeld((uint8_t)eraseVoice, 0u);
            return;
        }
    }
    if (liveRec_mode() == LIVEREC_MODE_ERASE &&
        (eraseSeq >= 0 || eraseVoice >= 0))
        return;                                   /* consumed */
}
```

After erase has ended, LiveRecord ignores the `…Held(…, 0u)` calls. The
release is still consumed, so a SEQ button that started an erase gesture
never reaches the ordinary SEQ release path.

Replace the comment at `:3006-3013`:

```
/*
 * Live erase button router, release edge (S081 §10.2, P1B items 8/9,
 * P1C items 10/16).
 *
 * What: a SEQ or TRACK release ends the gesture its own press started
 * (BC-3 masks), whatever the mode is now, and is consumed, even after
 * erase has ended. Any other SEQ/TRACK release while erase is on is
 * consumed. Why: P1B routed by the mode at release time, so after a mode
 * change an old button's release could end a new button's gesture, and a
 * release after erase end was lost (leaving LiveRecord's hold count set,
 * item 16). Affiliates: BC-4 (press edge), liveRec_eraseSeqHeld(),
 * liveRec_eraseFxSeqHeld(), liveRec_eraseTrackHeld().
 */
```

### BC-6 MODE press clears the gesture masks (item 10) — MODIFY `handleModeButtons()` at `buttonHandler.c:1808-1809`

```c
if (liveRec_mode() == LIVEREC_MODE_ERASE) {
    liveRec_eraseReleaseHeld();
    bh_eraseSeqMask = 0u;
    bh_eraseFxSeqMask = 0u;
    bh_eraseVoiceMask = 0u;
}
```

Extend the comment at `:1801-1807` with: "The BC-3 masks are cleared with
them, so the old buttons' later releases are consumed as no-ops (P1C item
10)."

Gate: `make all` clean.

---

## Phase C4 — Menu

**File:** `Core/Menu/menu.c`

### MC-1 Page exclusion tests the top-level page (item 3) — MODIFY `menu_liveRecRecordCell()` at `menu.c:14132-14134`

```c
if (menu_activePage == PERFORMANCE_PAGE || menu_activePage == RECORDING_PAGE ||
    menu_activePage == MENU_MIDI_PAGE   || menu_activePage == LOAD_PAGE ||
    menu_activePage == SAVE_PAGE)
    return;
```

`page` stays only as the `menu_cellClearTarget(page, position, &target)`
argument. In the function comment (`:14103-14123`), change "Inputs: page,
cell position." to:

```
 * Inputs: page = the sub-page index ((menuIndex & MASK_PAGE) >> PAGE_SHIFT)
 * and the cell position, both for the cell resolver only; the page
 * exclusions test menu_activePage (the top-level PageNames value). P1B
 * compared the sub-page with PageNames, so PERF edits were recorded and
 * some sub-pages were never recorded (P1C item 3).
```

Rename the parameter from `page` to `subPage`, both in the definition and in
the forward declaration at `menu.c:2050-2051`, so that the mistake cannot
recur.

Gate: `make all` clean.

---

## Phase C5 — RAM, build and verification

### RAM

All normal SRAM1, static.

| Item | Bytes | Owner |
|---|---|---|
| `liveRec_eraseFxSeqCount` | +1 | LiveRecord |
| `liveRec_rollStartDelay`, `liveRec_rollSpan` | +2 | LiveRecord |
| `liveRec_gracePl` removed (−6); `graceDeltaQ8`, `graceTick` now kept by the linker (+8) | +2 | LiveRecord |
| `bh_eraseSeqMask`, `bh_eraseFxSeqMask`, `bh_eraseVoiceMask` | +5 | buttonHandler |
| **Every build** | **+10** | |
| `seq_recRingDrops`, DEV_MODE_LOGGING only | +2 | Sequencer |

The stack grows by 2 B per `seq_place_t` local (SC-1). Together with P1B,
every build is about +71 B over P1, against about +51 B in the original P1B
table. This is within the user's approval, which covers follow-ons. Phase C6
records the corrected totals.

### Verification

**Host test.** Extend the P1B `seq_placeFromSnapshot()` test:

- `snapQ8` equals the applied shift for snap-up, snap-down and an odd
  shuffled step;
- `snapQ8` is 0 without snap.

**Hardware: the P1B F6 matrix, plus these rows:**

1. **FX lanes:**
   - FX-mode SEQ hold erases locks on lanes 8–15 (item 1);
   - an FX knob erase keeps working after an FX SEQ release and after a
     MODE press, until erase ends (item 2).
2. **Menu page exclusion:**
   - PERF `Nvm` edits during LATCH are not recorded;
   - VOICE sub-pages 7 and above are recorded (item 3).
3. **Roll endings:**
   - a quick roll release leaves a plain trigger on the start step at its
     delay;
   - a roll released on its start step keeps the start delay, and fill is
     about release − start;
   - a roll held over several steps: start step unchanged, spanned steps
     timing 0 / fill 100, end step timing 0 / fill = release offset
     (items 4, 7, 15).
4. **Forward-quantised roll (qnt 1/16, hit late in a step):** no roll on the
   step before the start (item 15).
5. **SHIFT+CLEAR, then REC:**
   - no menu opens;
   - releasing the keys in either order ends erase;
   - repeat with the SHIFT+REC menu opened first, then CLEAR with SHIFT
     held (item 5).
6. **Flams:**
   - a `trk` flam's grace lands 1/32 before the snapped main;
   - an un-quantised flam across a step boundary sounds on the next pass
     without a doubled grace (items 6, 8).
7. **SEQ erase across sessions:**
   - hold SEQ, release SHIFT and CLEAR, then release SEQ;
   - start erase again and hold and release another SEQ;
   - the erase stops at its release, on the active track (item 16).
8. **Two SEQ buttons in FX erase:** releasing one keeps the erase on
   (item 10).
9. **Mode change during a SEQ hold:** the old button's release does not end
   a SEQ hold started in the new mode (item 10).
10. **DEV build ring overflow:** force a ring overflow (all seven tracks
    live-held at a fast scale with a blocked foreground). Trace `'W'` cause
    5 appears and `seq_recRingDrops` counts it (item 13).

### Phase C6 — documentation at close (with P1B F7 and P1 Phase I)

- **`MODULE_INTERCHANGE_SPEC.md`:**
  - `seq_place_t.snapQ8`;
  - the `liveRec_eraseFxSeqHeld()` count semantics;
  - `liveRec_eraseReleaseHeld()` as the erase-gesture reset.
- **`PATTERN_DYNAMIC_STACK.md`:**
  - live-record roll recording: start step and delay kept; span only after
    the start; end-step rules;
  - stage `W` cause 5.
- **`STORAGE_SRAM_MANIFEST.md`:** the P1B table corrected as in the
  P1B review, plus the table above.
- **`tools/decode_devlogs.py`:** stage `W` causes 1–5 (1 handover, 2
  ownership, 3/4 refused write, 5 ring full).
- **`S081_P1_LIVE_RECORD.md` §8.8:** the roll-end rules of LC-9, and the
  "released before a forward-quantised start" case.

---

## Order of work

| Phase | Content | Build gate |
|---|---|---|
| C1 | Sequencer SC-1..SC-4 | clean build; LiveRecord compiles unchanged (new field unused) |
| C2 | LiveRecord LC-1..LC-12 | clean build |
| C3 | buttonHandler BC-1..BC-6 | clean build |
| C4 | Menu MC-1 | clean build |
| C5 | Host test, then the hardware matrix (P1B F6 plus the rows above) | — |
| C6 | Documentation, with P1B F7 and P1 Phase I | — |

C2 and C3 meet at the erase-gesture API. `liveRec_eraseFxSeqHeld()` and
`liveRec_eraseReleaseHeld()` keep their signatures, so C2 builds without C3.
C3 then only adds the buttonHandler masks.

---

## Code review — P1C implementation (2026-10-13)

**Scope:** every SC, LC, BC and MC step above, checked at its site in the
current working tree. P1B and P1C are both uncommitted on top of `d21e245`,
so there is no P1C-only diff. Each step was read in place against its sketch
and comment block, then the build and the link budget were run.

**Verdict:** all 16 items landed as scheduled and the build is clean. The P1B
must-fix items 1–5 and the new items 15–16 are fixed for the cases the
schedule described. **One must-fix remains:** the item 15 fix assumes
LiveRecord's view of the playhead is current, but it can lag by up to one
service period (C-1 below). That gap is in my LC-7/LC-8 design, not in the
implementation. Three minor items follow; none blocks C5.

### Must-fix

**C-1. Roll state is decided on a ring view that lags the playhead —
`LiveRecord.c:1096-1100`, `:930-975`, `:396-415` (items 4, 15).**

**What lags.**

- TIM3 pushes a ring entry at each step start.
- `liveRec_service()` drains the ring only in the 500 Hz pass
  (`timebase.c:212`).
- `liveRec_triggerIn()` (roll start) and `liveRec_endRoll(1)` (release,
  rate switch) run from button processing. Their live capture is exact.
- `liveRec_prevStep[]` and `liveRec_rollSpan`, however, still reflect the last
  drain, which can be up to 2 ms old.

LC-7/LC-8 assumed that "the step current at the capture" and "the last
drained step" are the same. Within 2 ms after a step boundary they are not.

**Effects, in order of damage:**

1. **A roll starting in the first ≤ 2 ms of step C, with the start on C or
   behind it.**
   - At the start, `prevStep` is still C−1 and `rollSpan` is 1.
   - The next drain delivers C's entry, so `prev` = C−1, and C−1 ≠ the start
     step.
   - C−1 is written as a roll span (timing 0, fill 100). A step before the
     roll is overwritten.
   - Item 15 removed this for drains that keep up, but not inside the lag
     window.
2. **A release in the first ≤ 2 ms of a later step L.**
   - `endRoll` writes L with the end fill and clears `rollHeld`.
   - The drain then delivers L's entry with `prev` = L−1, but the roll is no
     longer held, so L−1 never gets its span write.
   - L−1 was fully covered by the roll but stays as it was, leaving a gap in
     the recorded roll.
3. **A release in the first ≤ 2 ms of a forward-quantised start step S.**
   - S's entry is not yet drained, so `rollSpan` is still 0.
   - The roll is recorded as a plain trigger. Benign, but wrong.

The window is one service period per step boundary: about 1.6 % of roll
starts and ends at 1/16 and 120 BPM, and more on faster scales. Effect 1
silently destroys stored step content.

**Fix: drain after the capture, at both decision points.** Any entry for a
step that began before a capture is already in the ring when the capture
is taken, so draining right after the capture makes the LiveRecord view
current for that capture. Entries pushed later belong to later steps and
are handled normally.

1. Factor `liveRec_service()` step 2 (position epoch) and step 5 (ring drain)
   into `static void liveRec_syncRing(void)`.
   - Keep step 3's guard: no drain while `patSvc_replacePending()`.
   - `liveRec_service()` calls it in place of those two steps.
2. In `liveRec_triggerIn()`, call `liveRec_syncRing()` immediately before the
   fast-roll start sets `liveRec_rollHeld`.
   - It runs after the capture and placement.
   - No roll is held yet, so the drain writes no span.
   - `prevStep` becomes C, and the existing `rollSpan = (rel <= 0)` is then
     correct.
3. In `liveRec_endRoll()`, call `liveRec_syncRing()` immediately after
   `seq_captureTrackTime()`, before the three-way decision.
   - The roll is still held, so every step that ended before the release gets
     its span write.
   - S's entry sets `rollSpan` if S has begun.
   - The `writeEnd = 0` path (called from the service itself) does not sync.
4. Position-epoch handling moves with the drain. A jump between the last
   service pass and the capture is therefore seen before any span write.

The comment above `liveRec_syncRing()` should say:

- **What:** it is the service's position-epoch check and ring drain, also
  run at roll start and roll end.
- **Why:** roll decisions need a playhead view no older than the capture,
  and the 500 Hz drain lags by up to 2 ms.
- **Context:** foreground only (button processing and the 500 Hz pass share
  it).
- **Accessors:** `liveRec_service()`, `liveRec_triggerIn()`,
  `liveRec_endRoll()`.

Update the LC-7 state comment and the LC-8 span comment to name the sync.
No RAM.

**Add a hardware check:**

- record rolls started and released right on step boundaries at a fast
  scale (for example 1/32 at 160 BPM);
- the step before each start must be unchanged;
- there must be no gap before each end step.

### Minor

**C-2. `ahead` ignores the `trk` snap — `sequencer.c:4071-4075`.** This
predates P1C (from SQ-3).

- `aheadQ8 = deltaQ8 - off` leaves out `snapQ8`, so a hit snapped forward
  reports `ahead` = 0.
- It is harmless today: the echo deadline adds a full interval, which covers
  the half-interval snap.
- One-line fix: `aheadQ8 = deltaQ8 + snapQ8 - (int32_t)off`. Do it with C-1
  so the echo deadline is exact for the snap.

**C-3. Comment placement and wording.**

- **Comment position in `LiveRecord.c`:** the LC-6 blocks sit inside the
  bodies of `liveRec_forceOff()` (`:716-724`) and `liveRec_eraseEnd()`
  (`:746-756`), after the `memset`. The house style puts them above the
  function, and the schedule did too. Move them.
- **`menu.c:14119`:** the comment still says "Inputs: page = the sub-page
  index", but the parameter is `subPage` (MC-1). Change it to `subPage`.
- **P1B/P1C state comment (`LiveRecord.c:114`):** it lists
  `lastSceneEpochValid`, but the variable is `liveRec_sceneEpochValid`.
  Correct the name.

**C-4. Mask declaration site.** `bh_eraseSeqMask`, `bh_eraseFxSeqMask` and
`bh_eraseVoiceMask` are declared at `buttonHandler.c:174-189` rather than
after `:388`. They are declared before every use, so this is accepted.

### Verified correct

- **Sequencer:**
  - SC-1: `snapQ8` is recorded in both snap branches and is 0 otherwise;
    the typedef comment was replaced.
  - SC-2: one PRIMASK section.
  - SC-3: DEV counter and `'W'` cause-5 trace in the full-ring branch;
    the push comment was updated.
  - SC-4: comment only.
- **LiveRecord:**
  - LC-1: the 16-bit mask ORs the knob lanes with all lanes while the FX
    SEQ count is nonzero.
  - LC-2, LC-3, LC-4: counted FX SEQ hold.
  - LC-5: releasing held gestures keeps the knob lanes and the knob bitmap.
  - LC-6: erase end and force-off reset every source through
    `liveRec_eraseReleaseHeld()`; item 16 is closed.
  - LC-7.
  - LC-8: the span is skipped before the start and on the start step, and
    `rollSpan` is set after the span test for the same entry.
  - LC-9: all three end cases, with the fill measured from the start delay
    on the start step.
  - LC-10: quantised grace from `deltaQ8 + snapQ8`; un-quantised grace
    re-placed from the main's capture.
  - LC-11: `liveRec_gracePl` removed.
  - LC-12: header comments.
- **buttonHandler:**
  - BC-1: the ERASE test is above SHIFT+REC, and the menu exit stays first.
  - BC-2: erase ends before the consume and menu breaks; the `eraseEnded`
    LED rule is in place; both key orders, with and without the SHIFT+REC
    menu, now end erase.
  - BC-3: `buttonHandler_liveEraseBegin()` clears the masks only when
    entering from off, and both press sites use it.
  - BC-4, BC-5: bit-routed release, consumed even after erase ends.
  - BC-6.
- **Menu:** MC-1 (exclusions test `menu_activePage`; parameter renamed in
  both the declaration and the definition).

### Implementer log

The log is accurate. The Flash headroom it reports (185,388 B) matches this
rebuild exactly. C5 and C6 are correctly reported as not run.

### Build

`make all` is clean with no warnings. `tools/link_budget.py`:

| Region | Use |
|---|---|
| Flash | 568,276 / 753,664 B, headroom **185,388 B** (−608 B against P1B end) |
| ITCM | 4,168 / 16,384 B |
| DTCM statics | 4,472 B |
| FXBUF | 126,592 B, margin 3,712 B |

**RAM:**

- `.bss` grew by 16 B against P1B. That is the planned +10 B every build and
  +2 B DEV-only (`DEV_MODE_LOGGING` is on in this build), plus alignment
  padding.
- `nm` confirms every new symbol at its planned size: the three
  buttonHandler masks (5 B), `liveRec_eraseKnobFxLanes` (2 B),
  `liveRec_eraseFxSeqCount`, `liveRec_rollStartDelay` and `liveRec_rollSpan`
  (1 B each), the grace delta and tick (4 B each, now kept), and
  `seq_recRingDrops` (2 B).
- `liveRec_gracePl` is gone.
- C-1 adds no RAM.

### Remaining

1. Fix C-1, and with it C-2. Address C-3 in the same pass. Clean build after.
2. **C5:** the host test, then the hardware matrix:
   - the P1B F6 rows;
   - the P1C C5 rows;
   - the C-1 boundary roll check.
3. **C6**, together with P1B F7 and P1 Phase I. Add `liveRec_syncRing()` to
   the roll-recording description in `PATTERN_DYNAMIC_STACK.md`.
4. **Item 29** (FX realign on Scene changes that do not reset it) still needs
   your decision.

---

## Implementation log — review fixes C-1, C-2, C-3 (2026-10-13)

Only these three items were changed. C-4 was accepted and needs no change.

**C-1. Ring drain at roll start and roll end (`LiveRecord.c`).**

- **New helpers:** the service's step 2 (position epoch) and step 5 (ring
  drain) were moved unchanged into two new static helpers:
  - `liveRec_syncPosition()`;
  - `liveRec_drainRing()`.
- **The service:** it calls them at the same two points as before, so its
  behaviour is unchanged.
- **`liveRec_syncRing()`:** it runs `liveRec_syncPosition()` and then
  `liveRec_drainRing()`, skipping the drain while
  `patSvc_replacePending()`, as the service does. Its comment block covers
  the 2 ms lag it closes.
- **`liveRec_triggerIn()`:** the fast-roll start calls `liveRec_syncRing()`
  before it sets the live hold and the roll state. `prevStep` is then the
  capture's step, so `rollSpan = (rel <= 0)` holds.
- **`liveRec_endRoll()`:** it calls `liveRec_syncRing()` right after
  `seq_captureTrackTime()`.
  - The roll is still held, so every step that ended before the release gets
    its span write.
  - If the sync finds a position break, the roll has already been ended
    without a write (`endRoll(0)`), and the function returns.
- **Comments:** the LC-7 state comment and the span-writer comment now name
  the sync.

**C-2. `ahead` includes the snap (`sequencer.c`, `seq_placeFromSnapshot()`).**
The one-line change is
`aheadQ8 = deltaQ8 + snapQ8 - (int32_t)off`.

**C-3. Comments.**

- The `liveRec_forceOff()` and `liveRec_eraseEnd()` comment blocks were moved
  above their functions.
- The state comment now names `sceneEpochValid`.
- `menu_liveRecRecordCell()`'s comment now says `subPage =`.

**Build:**

- `make all` is clean; the compile has no warnings. The link prints the usual
  newlib `_close`/`_lseek`/`_read`/`_write` stub notices and the LTO
  serial-compilation note. Neither comes from these sources.
- Flash headroom is **185,084 B** (−304 B against the P1C build).
- No new variables. `.bss` moved by 16 B of linker padding; every LiveRecord
  and ring symbol is at its earlier size.

**Still open:**

- C5: the host test and the hardware matrix, including the C-1 boundary
  roll check;
- C6: the documentation pass;
- item 29.

---

## Implementation log — shuffle removed from placement (2026-10-13)

**Decision (user):** shuffle applies only at playback. Recording never uses
it.

**Defect.** `seq_placeFromSnapshot()` (SQ-3) measured the delay from an odd
step's *shuffled* start, which caused two faults:

- **Hits before that start.** These went to the previous step at delay 99,
  including a quantised hit exactly on the grid. A grid hit on an odd step
  was therefore never written to that step.
- **`trk` snap on odd steps.** The snap distance mixed the shuffled offset
  with the raw grid position, so a forward-snapped hit landed on the next
  step at a delay of about shuffle/step instead of 0.

**Change: `sequencer.c`, `seq_placeFromSnapshot()`.**

- The shuffle block was removed, together with the `sh` and `clamp99`
  locals.
- The delay is measured from the step's nominal start, for every quantise
  setting.
- A quantised or `trk`-snapped hit is written on the step it snaps to at
  delay 0, and playback adds that step's shuffle as for any other step.
- The function comment was rewritten.
- The signature is unchanged (`snapToStep`).

**Comments:**

- `sequencer.h`: the `seq_place_t` comment says the delay is measured from
  the nominal start.
- `LiveRecord.c`: the `liveRec_triggerIn()` comment says shuffle applies
  only at playback.

**Plan:** `S081_P1_LIVE_RECORD.md` §8.3, the un-quantised rule now uses the
nominal step start and records the decision.

**Build:** `make all` is clean; there are no warnings in the touched files.
Flash headroom is 185,196 B.

---

## Hardware fix 1 — recorded automation gets its underline (2026-10-13)

**Symptom:** after a parameter automation was recorded, its name was not
underlined.

**Cause:**

- The underline comes from Menu's bounded presence search
  (`va_scanService()`).
- A completed result is never re-validated. Only an explicit
  `va_searchRestart()` or a direct bit update changes it.
- Plan §12 requires recorded targets to update the search bits, but no P1,
  P1B or P1C step implemented that. LiveRecord's writes left the result
  unchanged until the page, track or Pattern changed.

**Change:**

- **`menu.c`, `va_searchRecordTarget(target)`:** the scan's per-entry
  classification was moved here unchanged. `va_scanService()` now calls it.
- **`menu.c`/`menu.h`, new `menu_automationTargetRecorded(scene, track,
  target)`:**
  - when the write is on the shown Pattern (and, on VOICE/STEP pages, on
    the searched track), it sets the same bit the scan would set;
  - it repaints if a completed result changed;
  - the search is not restarted, so other underlines do not blink out
    during a take.
- **`LiveRecord.c`, `liveRec_stepEnd()`:** both accepted
  `patSvc_writeStepAutomation()` writes (dirty, and LATCH re-stamp) call the
  notification. `menu.h` is now included.

**Not changed:**

- Erase still restarts the search at erase end (BH-6).
- A take that removes the last entry of a target keeps that underline until
  the next restart.

**Build:** `make all` is clean; there are no warnings in the touched files.
Flash headroom is 185,060 B. No new RAM.
