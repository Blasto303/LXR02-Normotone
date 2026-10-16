# S081 P3 — Loop While Record

Session 081, part 3, branch `dev-ph6-cleanup`. Plan only.

- **Revision 1** (2026-10-13) was written during P1 planning as "S081 P2"; its
  labels P2-A..D, P2-Q1..Q6 and R2-x are renamed P3-A..D, Q1..Q6 and R3-x
  here, because P2 is now Linear Automation Ramps
  (`S081_P2_LINEAR_AUTOMATION.md`).
- **Revision 2** (2026-10-14):
  - checks every claim against the code as it stands after P1, P1B, P1C,
    P2 and the P2 follow-ups (line numbers are that tree);
  - folds in the user's answers to Q1–Q6 (§8);
  - corrects the placement, echo and step-boundary design (§3.3, §3.4, §3.9);
  - lists new follow-up questions FQ1–FQ6 (§9).
- **Revision 3** (2026-10-14): folds in the answers to FQ1–FQ6 (§8). New:
  - a VOICE press while looping adds its track (FQ1);
  - a held overlay roll ends at loop engage and its VOICE becomes a
    looper TRACK hold (FQ2);
  - jump entries (FQ3) and the P1 fired-bit fix (FQ4, P3-0) are in scope;
  - PERF-page Morph edits (`fxm`, `1VM`..`6VM`) become recordable, like
    the same targets on the VOICE and Effect pages (FQ5);
  - loop-aware placement is built now (FQ6).

  One follow-up remains (§9, FQ7).
- **Revision 4** (2026-10-16): FQ7 answered: the master morph `mrp` is not
  recordable or automatable (user). Two §3.2 bullets corrected: with FQ2
  (a held roll ends at engage) and Q4 (no overlay while looping), no roll
  and no live hold can be active during a loop. Implementation schedule:
  `S081_P3_IMPLEMENTATION.md`.
- **Revision 5** (2026-10-16, after implementation): the global Morph rule
  is made binding and its leak closed (§3.10, fix G-1, user-approved 7 B
  SRAM1). Details in `S081_P3_IMPLEMENTATION.md`, "Global Morph
  examination" and "G-1 implemented".

**Scope.** Let the PERF looper run while live record (LATCH or OVERDUB) or
live erase is on, so the user can loop the playhead while recording or
clearing.

**Depends on** (all implemented):

- P1 live record (`S081_P1_LIVE_RECORD.md`, schedules P1/P1B/P1C):
  - L3: step-end writes and clears only on real changes (P1 §9.5);
  - L1: with `qnt trk` each looped track gets its own loop start
    (`seq_loopSnapshotTracks()`, P1 Appendix A-15);
  - the step-start ring semantics (P1 M-9);
  - live-cursor placement (`seq_placeFromSnapshot()`, P1B item 5);
  - echo marks with a deadline (P1B items 6/25).
- P2 automation ramps: the loop wrap and loop release end ramps.
- P2 follow-up D12: live record writes a parameter's **first change** in a
  step (`LIVEREC_AUTOMATION_FIRST_CHANGE`).

| Decision | State |
|---|---|
| L-2: no looper lockout during record or erase | decided (P1 §2); P1 ships the S080 lockout as an interim, this plan lifts it |
| L1: `trk` per-track loop starts | implemented in P1 |
| L3: write only real changes | implemented in P1 |
| L2: PERF buttons while looping | decided (Q1, Q4, FQ1, FQ2), §3.7 |
| L4: loop-aware placement and echo | decided (Q2, Q6, FQ6), §3.3, §3.4 |
| Jump entries at mid-step wraps and resumes | decided (FQ3), §3.9 |
| P1 fired-bit fix | decided (FQ4), §7, P3-0 |
| PERF Morph edits recordable | decided (FQ5), §3.10 |

**Authority read:**

- `PATTERN_DYNAMIC_STACK.md` §6.5 (looper engine);
- `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface";
- `080_SESSION_HANDOFF_LOG.md` §2.3;
- `MEMORY.md` "Looper invariants (S080) — binding".

---

## 1. Why this is safe in principle

- Looper actions are not recorded. Looping only moves track playheads.
- The looper never touches Pattern data (`PATTERN_DYNAMIC_STACK.md` §6.5).
- Live record and live erase already work from wherever the playhead is.
- The work is therefore about:
  - **placement and timing**: where a captured hit lands while the track is
    looped;
  - **step boundaries**: telling LiveRecord when a looped track jumps to
    another step (§3.9);
  - **buttons**: which button does what while looping (§3.7).

---

## 2. The looper engine as it stands

| Function | Where | Role |
|---|---|---|
| `seq_loopProcessTick()` | `sequencer.c:5155` | TIM3, every tick, **before** the DDA. For each looped track with `(elapsed − anchor) % L == 0`, it restores the snapshot (cursor, accumulator, play state just before the anchor tick's advance) and ends the track's automation ramps (P2). The DDA then advances only if that track advanced at the anchor tick |
| `seq_loopSnapshotTracks()` | `:4866` | Anchor(s) and snapshot at engage and for tracks added by a swap. `off`: the fastest track's last step start; `qnt 1/8..1/64`: the last grid boundary; `trk`: each track's own last step start (L1) |
| `seq_shiftPositionQ8()` | `:4511` | Foreground position maths: moves a position image by a signed Q8 tick delta (current effective scale and length) |
| `seq_loopShiftPosition()` | `:4605` | Whole-tick wrapper of the above |
| `seq_loopResumeTrack()` | `:4936` | Projects the snapshot to now: the track resumes where it would have been; ends its ramps (P2) |
| `seq_loopEngage()` | `:4961` | Engage under PRIMASK; returns 0 and does nothing while the transport is stopped |
| `seq_loopUpdateMask()` | `:4999` | Swap: leaving tracks resume, staying tracks keep their start, new tracks are snapshotted |
| `seq_loopDisengage()` | `:5038` | Under PRIMASK: ends the loop, resumes every looped track |
| `seq_loopUpdateDivision()` | `:5066` | BAR modifiers and SELECT division changes |
| `seq_loopRefreshSceneSnapshots()` | `:5095` | Scene change: anchors kept, snapshots recomputed for the incoming Scene; a resetting change moves anchors to 0 |

None of these bumps the playback epoch. To LiveRecord, a loop wrap is only a
different next step, if a ring entry reports it (§3.9).

**Live record side:**

| Function | Where | Role |
|---|---|---|
| `seq_captureTrackTime()` | `sequencer.c:4663` | PRIMASK snapshot of the live cursor, accumulator, scale, length, play state |
| `seq_placeFromSnapshot()` | `:4713` | Places a hit by shifting the **live cursor** by the capture delta (P1B). Callers: `liveRec_triggerIn()`, `liveRec_placeFlam()`, `liveRec_endRoll()` |
| `seq_suppressStepOnce()` | `:1040` | Echo mark with a deadline: capture tick + ticks to the step + one interval + 1 |
| `liveRec_drainRing()` | `LiveRecord.c:337` | Ring entry = a step start; the next entry of a track ends its previous step (`liveRec_stepEnd()`) |

**P1 interim lockout (to be removed):**

- `buttonHandler.c:2166-2169`: after `seq_loopEngage()`, if the loop engaged
  and record or erase is armed, `liveRec_forceOff()` and the REC LED off
  (P1B item 13 moved this after the engage);
- `buttonHandler.c:2785`: REC press ignored while looping;
- `LiveRecord.c:1170`: `liveRec_triggerIn()` returns while looping.

Knob recording (`liveRec_paramIn()`) has no loop gate of its own. Live erase
starts only while REC is armed (SHIFT+CLEAR, `buttonHandler.c:2822`), so it
is locked out through REC.

---

## 3. Behaviour with P3

### 3.1 Lockout lifted (L-2)

- REC short press (LATCH) and REC hold (OVERDUB) work while looping.
- Engaging, swapping, changing division and releasing the loop leave the
  record mode as it is.
- SHIFT+CLEAR live erase works while looping (REC armed, as always).
- Transport stop still ends the loop (S080). It is a playhead break for live
  record, not a take boundary: latches survive (P1B; only a Scene change
  starts a new take).

### 3.2 Recording follows the looped playhead

- **Each wrap is a new pass.** Once §3.9 is in place, live record sees every
  step a looped track plays, wraps included.
- **What records while looping** (the loop exists only in PERF mode, whose
  only page is PERFORMANCE_PAGE):
  - PERF-page Morph edits `fxm` and `1VM`..`6VM` (FQ5, §3.10);
  - LATCH passes over latched targets (re-stamp own Instrument parameters
    on fired steps, clear foreign entries);
  - pending first-change values;
  - the end of an overlay roll held when the loop engaged (FQ2, §3.7);
  - knob erase of PERF-page Morph targets.
- **What does not record while looping:**
  - overlay triggers and rolls: they do not fire while looping (Q2, Q4);
  - `mrp`, the master morph: not recordable or automatable (FQ7, user);
  - MIDI notes and CCs: not yet recordable (`MidiParser.c:151` is a stub).
- **LATCH.** On every pass over the window:
  - an adjusted parameter's first change is written to the step it was made
    in (D12);
  - an own-Instrument latched parameter is re-stamped on fired steps;
  - foreign latched entries are cleared.

  With L3, a repeated pass over unchanged steps queues nothing.
- **OVERDUB.** Writes only where an adjustment happened.
- **Loops shorter than a step.** Every wrap reopens the same step, so a
  continuously adjusted parameter is written to that step once per pass; the
  latest pass's first change wins.
- **Fast-roll spans and live hold** (P1 E-4, F-P) cannot occur while
  looping (revision 4): a roll held at engage ends there (FQ2), and no
  overlay trigger fires while the loop is engaged (Q4). The span writer and
  the live-hold mask are unchanged and still work once the loop ends.
- **Automation ramps (P2).** Ramps never cross a wrap or a loop release;
  `asm` tracks ramp inside the window as in normal play (P2 D8).

### 3.3 Placement inside a loop (L4a, Q6)

Since P1B, `seq_placeFromSnapshot()` shifts the track's **live** cursor by
the capture delta. Placement is therefore already correct for any target
inside the current loop window. Only a shift that **crosses a wrap** is
wrong, because the live-cursor walk continues past the window as if
unlooped:

- **Forward across the wrap** (quantize ahead, a delay rounding into the
  next step, a roll end placed ahead): the hit lands on the step after the
  window end. **P3:** it lands on the replayed step after the wrap, where
  the hit will next sound (Q6).
- **Backward across the wrap** (a flam grace before the last wrap): the
  walk lands before the window start. **P3:** it maps to the end of the
  previous pass, the last part of the window.

**Change:** a loop branch in `seq_placeFromSnapshot()` for a looped track.

- The capture adds the track's loop state to the PRIMASK snapshot
  (`seq_captureTrackTime()`): looped flag, anchor, snapshot position, loop
  length L. This is stack only.
- Ticks to the next wrap: `L − ((elapsed − anchor) mod L)`, with the
  sub-tick fraction. Ticks since the last wrap: `(elapsed − anchor) mod L`.
- A delta inside that range shifts the live cursor as today.
- A delta beyond it restarts from the snapshot (`seq_shiftPositionQ8()`)
  shifted by the remainder modulo L; backward, from the snapshot shifted to
  the end of the previous pass.
- A backward delta that reaches before the loop's first pass (before the
  anchor) lands in the unlooped history: the snapshot shifted by the full
  offset (revision 4; found by the mapping simulation in the schedule).
- `rel` (behind / current / ahead) is decided by time, not by step count.
  A forward snap across the wrap is ahead even when its step index is lower.
- `ahead` stays the time from the capture to the landed step. It feeds the
  echo deadline.
- **Unchanged:**
  - the master-clock grid (`qnt` 1/8..1/64);
  - `trk` snaps to the track's step grid as replayed;
  - `rnd`: forward shifts are not allowed (P1; the shift is clamped to 0).

### 3.4 Echo suppression inside a loop (L4b, L4c, Q2)

- **No clear at the wrap** (changed from revision 1). Revision 1 cleared
  echo marks at every wrap. That contradicts Q6: a forward snap across the
  wrap lands on the replayed step right after it, and that replay must be
  suppressed because the live hit just sounded.
- **The P1B deadline does the expiry:** capture + `ahead` + one step
  interval + 1. With `ahead` measured through the wrap (§3.3), the mark
  covers exactly the next play of the landed step. A mark the playhead does
  not reach in time is dropped.
- **Clear on a resume:** `seq_loopResumeTrack()` clears the track's mark
  inside the caller's PRIMASK section. This covers release, a track leaving
  in a swap, and disengage, where the cursor jumps and the step index loses
  its meaning.
- **Replay (L4c, Q2: yes):** a hit written onto a step the loop replays
  sounds again on the next pass. That is the stored step playing back, a
  looping overdub. Only the one play right after the capture is suppressed.
  - No overlay hit is captured while looping (§3.7), so this applies to a
    roll end at engage (FQ2) and to future MIDI note input.

### 3.5 Live erase while looping

- The loop exists only in PERF.
  - SEQ (Scene) buttons are consumed by the erase router in PERF, as in P1.
  - TRACK (VOICE) buttons keep their looper functions while the loop is
    engaged (Q4), so TRACK-hold erase is not available while looping.
- What erases while looping:
  - knob erase of PERF-page Morph targets (`fxm`, `1VM`..`6VM`) on rtk;
  - SEQ/TRACK erase gestures started before the loop engaged, until
    released;
  - the TIM3 erase masks of those gestures.

  Each pass finds less to remove; a pass with nothing left queues nothing
  (L3).
- FX-sequence erase follows the FX playhead, which is never looped (S080).

### 3.6 Takes and epochs (Q3: yes)

- Loop engage, swap, division change, release and resume are **not** take
  boundaries and bump no epoch. Latches persist.
- A Scene change while looping starts a new take through the **Scene
  epoch** (P1B), as for any Scene change. Anchors are kept or reset by the
  S080 rules, and the target Scene is re-evaluated.
- Transport stop/start, realign and PC reset are playhead breaks (playback
  epoch): per-step capture state ends, latches survive.

### 3.7 PERF buttons while looping (L2; Q1, Q2, Q4)

**Rule (Q1, user):** once a SELECT button is held in PERF mode, the loop
engages (transport running). It stays engaged until **every SELECT, TRACK
(VOICE) and BAR button is released**, or the MODE changes. This applies
whether or not record or erase is on.

**Rule (Q4, user):** while the loop is engaged, SELECT, BAR and TRACK
buttons always keep their looper functions. Record and erase never take
them over.

| Button | Loop not engaged | Loop engaged (any record/erase state) |
|---|---|---|
| SELECT | press engages (transport running); division | division; holds the loop |
| BAR1 / BAR2 | overlay toggle | triplet / dot modifier; holds the loop |
| TRACK (VOICE) | mute on release; overlay trigger when an overlay is on; TRACK-hold erase in erase | **looper only**: with a SELECT held, swaps the looped tracks (S080 D1); without a SELECT held, adds its track to the loop with a fresh loop start (FQ1); holds the loop; release of a looped track drops it from the loop. Never an overlay trigger, a mute toggle or an erase gesture (Q2, Q4) |
| MODE leaving PERF | — | ends the loop (`buttonHandler_loopForceRelease()`), record mode kept |
| REC, SHIFT+CLEAR | P1 | P1 (lockout removed, §3.1) |

**Changes from today's code:**

- **Hold set:** `buttonHandler_loopCheckDisengage()` (`buttonHandler.c:1547`)
  keeps the loop while any SELECT, any VOICE **of a looped track** or any
  BAR is held. Q1 makes it **any** held VOICE. A held VOICE of a track that
  is not looped (`lop` off, or left by a swap) also holds the loop.
- **Overlay pre-empted:** the PERF VOICE press (`buttonHandler.c:2420`)
  fires the overlay trigger while looping when no SELECT is held. Q2/Q4:
  while looping the overlay never fires. The overlay setting is kept and
  works again after the loop ends.
- **No mute while looping:** a VOICE release while looping never toggles a
  mute. Today only a looped track's release suppresses it.
- **Erase router:** in erase, a PERF VOICE press goes to TRACK-hold erase
  (`buttonHandler.c:2645-2668`). While looping it goes to the looper
  instead (Q4). SEQ in PERF stays consumed.
- **VOICE press without SELECT (FQ1):** today it only joins
  `perf_heldVoiceMask` and changes nothing in the loop. P3: the track joins
  the loop with a fresh loop start (`seq_loopUpdateMask()` with the old
  loop mask plus this track: staying tracks keep their starts, the new one
  is snapshotted), exactly the new-track rule of a swap.
  - A track the looper may not capture (`lop` off) does not join but still
    holds the loop (Q1).
  - Its release drops it from the loop (S080 D1).
- **Overlay held at engage (FQ2):** a VOICE firing an overlay trigger is
  in `bh_overlayVoiceMask`, not `perf_heldVoiceMask`, so today it neither
  joins the engage set nor holds the loop, and its release still runs the
  overlay release. P3, at a successful engage (`seq_loopActive` after
  `seq_loopEngage()` at `buttonHandler.c:2151`), for every VOICE in
  `bh_overlayVoiceMask`:
  1. **The overlay gesture ends**, as its release does today
     (`buttonHandler_overlayRelease()`): repeats stop, note-off, and
     `liveRec_rollRelease()` writes the recorded roll's end. A flam
     still waiting for its main hit loses it, as on an early release
     today.
  2. **The VOICE becomes a normal looper TRACK hold:** its bit moves to
     `perf_heldVoiceMask` and `perf_voiceActionOccurred` is set. It holds
     the loop, its track joins the loop like any held VOICE (the engage
     mask is rebuilt from the held set), and its release drops the track
     (S080 D1) with no mute toggle and no second overlay release.
  - **Order:** the engage mask (`buttonHandler.c:2147`, today the held
    VOICE set or all tracks) is built from `perf_heldVoiceMask |
    bh_overlayVoiceMask`, so the held overlay track loops. Steps 1 and 2
    run only if `seq_loopEngage()` engaged. With the transport stopped the
    engage does nothing (SELECT only lights its LED), and the overlay
    gesture continues unchanged.
- **Unchanged:** engage requires a running transport (SELECT while stopped
  only lights its LED); ring overflow and leaving PERF force the loop off.

### 3.8 Conditions (Q5: no)

- `lop`/`!lp` steps keep their meaning; a recorded step plays back under its
  own condition.
- Steps recorded while looping are **not** given `lop`. Fill (`fil`) still
  applies as in P1.

### 3.9 Step boundaries at wraps and resumes (new in revision 2)

- **Problem:** a ring entry is pushed only by a step advance
  (`seq_advanceTrackStep()`). At a wrap the snapshot is restored, and the
  DDA advances only if that track advanced at the anchor tick.
  - `trk` anchors every track at its own step start, so every wrap is
    reported.
  - With `qnt off` (the fastest track's step start) or a 1/8..1/64 grid,
    any track whose step does not start at the anchor wraps **mid-step**
    with no ring entry.
  - `seq_loopResumeTrack()` (release, swap, disengage) also jumps without
    an entry.
- **Effect on live record:** LiveRecord still believes the pre-jump step is
  playing.
  - Step-end rule (switch 0): a change after the jump is written into the
    old step.
  - First-change rule (switch 1): the value goes to the right step (the
    live cursor), but the old step's dirty bit blocks the first change after
    the jump.
- **Fix (FQ3, decided):** TIM3 pushes a **jump entry** for rtk (and
  live-held tracks) when a wrap or resume moves the track without a step
  advance.
  - A spare bit of the 16-bit ring entry carries the jump flag.
  - LiveRecord ends the previous step as usual.
  - The step begun by a jump entry is a partial replay: its trigger did not
    fire on this pass. At its end only dirty writes run. No LATCH re-stamp
    and no clear, because "not fired" would otherwise wipe the
    own-Instrument entries of a step that does play.
  - **RAM:** none planned. The flag and the stored fired bit (FQ4) pack
    into the existing per-track `liveRec_prevValid[]` byte (bit 0 valid,
    bit 1 fired, bit 2 begun by a jump). FQ4 allows a little RAM if the
    schedule finds packing unsafe; any such bytes will be listed with
    region, owner and lifetime before implementation.
  - The first-change rule (D12) then works across jumps: the jump entry
    ends the old step and clears its dirty bits, so the first change after
    the jump is written to the replayed step.

### 3.10 PERF-page Morph edits are recorded (FQ5, new in revision 3)

- **User rule:** Morphs are recorded the same way from every page; there is
  no reason for two interactions. Apart from the overlay + TRACK
  triggers and rolls, nothing else in PERF mode is recorded.
- **Today:** `menu_liveRecRecordCell()` (`menu.c:14212`) returns early on
  PERFORMANCE_PAGE (P1 §9.1, enforced by P1C item 3).
- **Same targets already:** `menu_cellClearTarget()` (`menu.c:14069`)
  resolves the PERF cells to the same targets the other pages use:
  - `1VM`..`6VM` → `sceneModTarget_voiceMorphId(slot)`, the `Nvm` target
    of the VOICE mix page;
  - `fxm` → `sceneModTarget_effectMorphId()`, the Effect page's Morph
    target.

  The commits match what live record reads back
  (`liveRec_targetValue7()`): `preset_morphVoiceScene()` stores the
  retained voice Morph amount; `menu_commitEffectMorphParam()` the Effect
  Morph amount.
- **Change:** remove `PERFORMANCE_PAGE` from the page exclusion in
  `menu_liveRecRecordCell()`. The rest of the gate is unchanged:
  ownership (a Scene target records only when T == A), latch/dirty, the
  first-change rule.
- **`mrp`** (`PAR_MORPH`, the master morph) resolves to no target
  (`menu_cellClearTarget()` returns 0). It is not recordable or automatable
  (FQ7, user).
- **Applies always,** looping or not: PERF Morph edits record in LATCH and
  OVERDUB like the same edits on the VOICE and Effect pages.
- **Global Morph rule (user, binding, revision 5):** the global Morph
  (`mrp`) is never recorded and never triggers a record, from the menu or
  from MIDI. The voice and FX Morph values a global change writes are never
  recorded individually. Only an individual edit of `1VM`..`6VM`/`Nvm` or
  `fxm` records, to the recording track.
  - **Checked:** no global-Morph path reaches `liveRec_paramIn()`.
  - **Leak closed (G-1):** live record wrote the stored Morph value at
    write time, which a global change overwrites (step-end rule, or a
    pending first change). LiveRecord now captures each individual Morph
    edit (`liveRec_morphValue[7]`, 7 B SRAM1) and writes that value.

---

## 4. Module changes

| Module | Change |
|---|---|
| buttonHandler | Remove the interim lockout (`:2166-2169`, `:2785`). Hold set: any held VOICE holds the loop (`buttonHandler_loopCheckDisengage()`). VOICE while looping: looper only (no overlay trigger, no mute toggle, not the erase router); a press without SELECT adds its track (FQ1). At engage: held overlay VOICEs end their gesture and become TRACK holds (FQ2). |
| Menu | `menu_liveRecRecordCell()`: PERFORMANCE_PAGE no longer excluded (FQ5, §3.10). |
| LiveRecord | Remove the looper gate from `liveRec_triggerIn()` (`:1170`). Remove `liveRec_forceOff()` (`:871`, `LiveRecord.h:88`); its only caller is the lockout. Ring drain: the ended step uses its own fired bit (FQ4); jump entries with the partial-replay rule (FQ3). |
| Sequencer: placement | Loop branch in `seq_placeFromSnapshot()`; loop fields in `seq_captureTrackTime()` (§3.3). Read-only on looper state, under the existing capture PRIMASK. |
| Sequencer: echo | Clear the mark in `seq_loopResumeTrack()`; no clear at the wrap (§3.4). |
| Sequencer: ring | Jump entry from `seq_loopProcessTick()` and `seq_loopResumeTrack()` for rtk and live-held tracks when no step advance follows (§3.9). |
| PatternData / Service / EffectsManager | None. |
| Docs at close | `PATTERN_DYNAMIC_STACK.md:748` (`seq_loopEngage()` "turns recording off") and a looping-while-recording note; `MODULE_INTERCHANGE_SPEC.md:306` ("Recording is turned off on engage and REC is ignored while looping") and the PERF hold rules (§3.7); the live-record section: §3 of this plan; `MEMORY.md`: the looper/record exclusivity statement; `S081_P1_LIVE_RECORD.md` references to `S081_P2_LOOP_WHILE_RECORD.md` (lines ~160, 258, 561, 608) → this file; P1 §9.1 "Not recorded: PERF" → PERF Morph cells recorded (FQ5). |

**Binding looper invariants** (`MEMORY.md`), kept:

- master-clock anchors per track, with the snapshot before the anchor
  tick's advance;
- no shared loop tick counter;
- the wrap runs before the DDA;
- every looper publish is under PRIMASK;
- `seq_setStepIndexToStart()` rebases, and transport stop ends the loop;
- P2: the wrap and `seq_loopResumeTrack()` call `seq_rampBreakTrack()`.

P3 adds to the wrap and the resume only a ring push (§3.9) and an echo clear
in the resume; placement is read-only on looper state.

**RAM:** none expected (stack only in the capture snapshot).

---

## 5. Phases

| Phase | Content | Verify |
|---|---|---|
| P3-0 | FQ4 fired-bit fix | LATCH re-stamp and clear follow the ended step's own fired state (a fired step followed by a non-fired step, and the reverse) |
| P3-A | Loop-aware placement; echo clear on resume; jump entries | Host test: placement matches what `seq_loopProcessTick()` plays, across wraps, for every run mode and division incl. dot/triplet; jump entries at mid-step wraps (`off`, grid) |
| P3-B | Lift the lockout; PERF button rules (§3.7, FQ1, FQ2); PERF Morph recording (§3.10) | REC/hold/erase while looping; engage/swap/release keep the record mode; loop held by any SELECT/VOICE/BAR; no overlay trigger, mute toggle or TRACK erase while looping; VOICE without SELECT adds its track; a held roll ends at engage, its end is recorded, its VOICE holds the loop and its release drops the track; `fxm`/`1VM`..`6VM` edits record in LATCH/OVERDUB, looping or not |
| P3-C | End-to-end matrix | LATCH/OVERDUB/erase × loop division (1 bar … 1/128) × qnt (off, 1/16, `trk`) × run mode × per-track Scenes; Pattern Stack Service drop counters at 1/128 loops |
| P3-D | Docs (§4) | — |

---

## 6. Risks

- **R3-1 Write rate on short loops.** L3 removes repeat writes. A parameter
  adjusted continuously during a 1/128 loop is still written about once per
  pass (first change per step), which can exceed the busy drain (one event
  per 2 ms). Drops are counted (P1 H-3).
- **R3-2 Projection approximation.** `seq_shiftPositionQ8()` uses the
  current effective scale and length for the whole span (accepted S080
  limit). A Morph or automation change to scale or length during a loop can
  move a forward snap by part of a step.
- **R3-3 Ring rate.** At 1/128 loops with fast tracks, entries for `rtk`
  arrive every few ticks, plus jump entries. The 16-entry ring is drained
  each 2 ms pass; overflow is counted.
- **R3-4 `rnd` and once modes under a loop.** `rnd` replays a snapshot but
  draws fresh steps, so a looped `rnd` track records onto different steps
  each pass. Once modes can park past the end inside a loop window. Both are
  existing looper behaviour; recording follows them.
- **R3-5 Chain `stp` freeze.** While frozen, the wrap and the DDA are both
  skipped (S080), so no ring entries arrive and nothing is written. That is
  consistent, because nothing plays.

---

## 7. Findings outside P3 (recorded, not changed)

- **P1 fired bit of the ended step.** `liveRec_drainRing()` calls
  `liveRec_stepEnd(b.track, prev, b.fired)` (`LiveRecord.c:371`): it passes
  the **new** entry's fired bit for the step that ended. The P1 plan
  (revision 11, M-9) says the ended step uses its **own** entry's fired bit.
  - Effect: LATCH re-stamps or clears own-Instrument entries on step N
    according to whether step N+1 fired.
  - **Decided (FQ4):** fixed in P3-0. P3's jump entries need the stored
    fired bit anyway.
- **P1 stale previous step.** Arming REC while running leaves the ring's
  last step for rtk from an earlier take; the first entry then ends that
  stale step (noted in `S081_P2_IMPLEMENTATION.md`).

---

## 8. Answers (user, 2026-10-14)

| # | Question | Answer |
|---|---|---|
| Q1 | PERF priorities while looping and recording | The loop engages when a SELECT is held in PERF and stays engaged until every SELECT, TRACK and BAR button is released or the MODE changes, with or without record (§3.7) |
| Q2 | A hit recorded onto a replayed step sounds again on the next pass | Yes. Today there is no way to do this, because the loop pre-empts the trigger overlays (§3.4, §3.7) |
| Q3 | Loop engage/swap/division/release are not take boundaries | Yes (§3.6) |
| Q4 | Erase takes VOICE while looping | **No.** While the loop is engaged, SELECT, BAR and TRACK always keep their looper functions (§3.5, §3.7) |
| Q5 | Give recorded steps `lop` | No (§3.8) |
| Q6 | A forward snap across the wrap lands on the replayed step after it | Yes (§3.3); echo handling adjusted to match (§3.4) |
| FQ1 | VOICE press while looping with no SELECT held | Yes: adds its track to the loop (§3.7) |
| FQ2 | Overlay VOICE held when the loop engages | The roll ends when the loop starts; the track hold then acts as normal for loop operation (§3.7) |
| FQ3 | Jump entries at mid-step wraps and resumes | Yes (§3.9) |
| FQ4 | Fix the P1 fired bit in P3 | Yes; some RAM may be used if necessary (§3.9, §7) |
| FQ5 | What records while looping | Morphs are recorded, as they already are from the VOICE pages; no reason for two interactions. Apart from overlay + TRACK triggers/rolls, nothing else in PERF is recorded (§3.10) |
| FQ6 | Build loop-aware placement now | Yes (§3.3) |
| FQ7 | Master morph `mrp` | Not recordable or automatable (user, 2026-10-16) |

---

## 9. Follow-up questions

All answered (§8). None open.
