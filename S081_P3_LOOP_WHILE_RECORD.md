# S081 P2 — Loop While Record

Session 081 plan, part 2 (2026-10-13), branch `dev-ph6-cleanup`, base
`02553df`. Plan only; no code has changed.

**Scope.** Let the PERF looper run while live record (LATCH or OVERDUB) or
live erase is on, so the user can loop the playhead while recording or
clearing.

**Depends on P1** (`S081_P1_LIVE_RECORD.md`, `S081_P1_IMPLEMENTATION.md`)
being implemented. In particular:

- L3: step-end writes and clears only on real changes (P1 §9.5);
- L1: `trk` per-track loop anchors (P1 schedule A3-3c);
- the step-start ring semantics (P1 schedule C-9a).

| Decision | State |
|---|---|
| L-2: no looper lockout during record or erase | decided (P1 §2). P1 ships the S080 lockout as an interim; this plan lifts it |
| L1: `trk` gives each looped track its own anchor | decided, in P1 |
| L3: write only real changes | decided, in P1 |
| L2: PERF button sharing while looping and recording | proposed here (§3.7) |
| L4: loop-aware placement and echo | proposed here (§3.3, §3.4) |

Authority read:

- `PATTERN_DYNAMIC_STACK.md` §6.5 (looper engine);
- `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface";
- `080_SESSION_HANDOFF_LOG.md` §2.3;
- `MEMORY.md` "Looper invariants (S080) — binding".

---

## 1. Why this is safe in principle

- Looper actions are not recorded. Looping only moves track playheads.
- The looper never touches Pattern data (`PATTERN_DYNAMIC_STACK.md` §6.5).
- Live record and live erase already work from wherever the playhead is.
  TIM3 reports the steps a track actually plays through the step-boundary
  ring (P1 C-9a), so a wrap is just a different next step.
- The changes are therefore about **placement and timing**: where a
  captured hit lands while the track is looped, and how often the same
  steps are passed.

## 2. The looper engine as it stands (S080 + P1)

| Function | Line | Role |
|---|---|---|
| `seq_loopProcessTick()` | `sequencer.c:3385` | TIM3, every tick, **before** the DDA. For each looped track with `(elapsed − anchor) % L == 0`, it restores the snapshot (cursor, accumulator, play state just before the anchor tick's advance). The DDA then replays the anchor's step |
| `seq_loopSnapshotTracks()` | `:3125` | Takes the anchor(s) and snapshot at engage (`:3222`) and for tracks added by a swap (`:3262`). P1 L1 adds per-track anchors for `trk` |
| `seq_loopShiftPosition()` | `:2929` | Foreground position maths: moves a position image by a signed tick count (current effective scale and length) |
| `seq_loopResumeTrack()` | `:3167` | Projects the snapshot to now: the track resumes where it would have been |
| `seq_loopDisengage()` | `:3268` | Under PRIMASK: ends the loop, resumes every looped track |
| `seq_loopUpdateMask()` | `:3229` | Swap: leaving tracks resume, staying tracks keep their start, new tracks are snapshotted |
| `seq_loopRefreshSceneSnapshots()` | — | Scene change: anchors kept, snapshots recomputed for the incoming Scene; a resetting change moves anchors to 0 |

P1 interim lockout:

- `liveRec_forceOff()` runs before `seq_loopEngage()` (P1 schedule B-4);
- REC is ignored while looping (B-2a);
- LiveRecord's trigger gate includes "looper not engaged" (E-1).

---

## 3. Behaviour with P2

### 3.1 Lockout lifted (L-2)

- REC short press and REC hold (overdub) work while looping.
- Engaging, swapping, changing division and releasing the loop leave the
  record mode as it is.
- SHIFT+CLEAR live erase works while looping.
- Transport stop still ends the loop (S080) and the take (P1 epoch).

### 3.2 Recording follows the looped playhead

- **Each wrap is a new pass.** The ring reports every step a looped track
  plays, wraps included. After a wrap, the next entry is the replayed
  anchor step, which ends the step that was playing (P1 C-9a semantics).
- **LATCH.** On every pass over the window:
  - an adjusted parameter is written to the step it was adjusted in;
  - an own-Instrument parameter is re-stamped on fired steps;
  - foreign entries are cleared.

  With L3, a repeated pass over unchanged steps queues nothing.
- **OVERDUB.** Writes only where an adjustment happened; looping makes it a
  classic loop overdub.
- **Fast-roll spans** (P1 E-4) follow the actual played steps, so a roll
  held across a wrap writes the steps on both sides of it.
- **Live hold** (P1 F-P) skips the stored triggers of a looped track while a
  recorded roll is held, on every pass.

### 3.3 Placement inside a loop (L4a)

The capture snapshot (`seq_captureTrackTime()`) already holds the looped
track's live cursor and accumulator. So un-quantized placement onto the
**current** step is correct without change.

Anything that projects to another tick uses `seq_projectStep()`, which today
maps through the un-looped master-clock position. That covers forward snaps
(quantize ahead, a delay that rounds to the next step, the flam main) and a
flam grace before the current step. While a track is looped, that is wrong.
P2 adds a loop branch:

- **Mapping:** for a looped track, tick `t` maps to the snapshot shifted by
  `((t − anchor) mod L)` ticks (`seq_loopShiftPosition()`), which is exactly
  what the wrap will play.
- **Forward across a wrap:** a forward snap that crosses the wrap lands on
  the replayed step after it.
- **Backward across a wrap:** a flam grace that falls before the last wrap
  maps to the end of the previous pass, the last part of the window.
- **Quantize grids:** the master-clock grid (`qnt` 1/8..1/64) is unchanged;
  `trk` snaps to the track's step grid as replayed.
- **`rnd` run mode:** forward snaps on a looped `rnd` track are not allowed,
  as in P1. The looper replays a snapshot, but `rnd` draws fresh steps on
  each pass.

### 3.4 Echo suppression inside a loop (L4b, L4c)

- **Expiry at the wrap:** an echo mark also expires at the track's loop
  wrap, cleared in `seq_loopProcessTick()` when it restores the snapshot.
- **Expiry on any loop position jump:** marks also expire in
  `seq_loopResumeTrack()`, `seq_loopDisengage()` and `seq_loopUpdateMask()`.
- **Replay (L4c):** a hit recorded onto a step the loop will replay sounds
  again on the next pass. That is the stored step playing back, like a
  looping overdub. Only a write **ahead of the playhead in the current
  pass** is echo-suppressed.

### 3.5 Live erase while looping

- SEQ hold, TRACK hold and knob erase act on each pass over the looped
  steps. A repeated pass finds nothing left to remove (L3 existence checks).
- FX-sequence erase follows the FX playhead, which is never looped (S080
  invariant).

### 3.6 Takes and epochs

- Loop engage, swap, division change, release and resume are **not** take
  boundaries. They are playhead moves, so latches persist (P2-Q3).
- A Scene change while looping is a take boundary (P1 epoch), as for any
  Scene change. Anchors are kept or reset by the existing S080 rules, and
  the target Scene is re-evaluated.

### 3.7 PERF button sharing (L2)

These are the S080 priorities, kept unchanged:

| Situation | VOICE press | BAR1 / BAR2 |
|---|---|---|
| Loop engaged, a looper SELECT held | swaps the looped tracks (`buttonHandler.c` ~`:2060`); no overlay trigger | triplet / dot modifier (`:2453`, `:2490`) |
| Loop engaged, no SELECT held, overlay active | fires and records the overlay trigger (`:2088`) | modifiers; the overlay cannot be toggled or switched, so R19 switch-while-held is unavailable mid-loop |
| Loop engaged, live erase on | erase router takes VOICE (TRACK-hold erase), P1 G-1a; it cannot sustain or swap the loop (P2-Q4) | modifiers |
| Loop not engaged | P1 behaviour | overlay toggles |

A VOICE pressed for an overlay trigger never joins the looper hold set
(S080 accepted limit), so it neither sustains nor drops a loop.

### 3.8 Conditions

- `lop`/`!lp` steps keep their meaning; a recorded step is played back
  under its own condition.
- Steps recorded while looping are **not** given `lop` automatically
  (P2-Q5). Fill (`fil`) still applies as in P1.

---

## 4. Module changes

| Module | Change |
|---|---|
| buttonHandler | Remove the P1 interim lockout: the `liveRec_forceOff()` call and REC LED clear before `seq_loopEngage()` (P1 B-4a), and the "ignore REC while looping" branch (P1 B-2a). Nothing else in the looper gestures changes. |
| LiveRecord | Remove the looper gate from `liveRec_triggerIn()` (P1 E-1). Remove `liveRec_forceOff()` from the API. |
| Sequencer: `seq_projectStep()` | Loop branch (§3.3). Foreground; reads `seq_loopTrackMask`, `seq_loopAnchor[]`, `seq_loopStart*[]`, `seq_loopLengthTicks()` under the existing capture PRIMASK and uses `seq_loopShiftPosition()`. Read-only on looper state. |
| Sequencer: echo expiry | `seq_loopProcessTick()`: on a wrap, `seq_echoStep[track] = -1` (one byte store in TIM3). `seq_loopResumeTrack()`, `seq_loopDisengage()`, `seq_loopUpdateMask()`: clear the affected tracks' marks inside their existing PRIMASK sections. |
| PatternData / Service / EffectsManager | None. |
| Docs at close | `PATTERN_DYNAMIC_STACK.md` §6.5: the `seq_loopEngage()` row ("turns recording off") and a looping-while-recording note. `MODULE_INTERCHANGE_SPEC.md` PERF surface: "Recording is turned off on engage and REC is ignored while looping". The live-record section: §3 of this plan. `MEMORY.md`: the looper/record exclusivity statement. |

**Binding looper invariants** (`MEMORY.md`), kept:

- master-clock anchors per track, with the snapshot before the anchor
  tick's advance;
- no shared loop tick counter;
- the wrap runs before the DDA;
- every looper publish is under PRIMASK;
- `seq_setStepIndexToStart()` rebases, and transport stop ends the loop.

P2 adds only a byte clear in the wrap and read-only uses of the anchor and
snapshot.

**RAM:** none expected.

---

## 5. Phases

| Phase | Content | Verify (hardware) |
|---|---|---|
| P2-A | Loop-aware `seq_projectStep()`; echo expiry at wrap and position jumps | Host test: projection matches what `seq_loopProcessTick()` plays, across wraps, for every run mode and loop division incl. dot/triplet |
| P2-B | Lift the lockout (buttonHandler, LiveRecord gate, API) | REC/hold/erase while looping; engage/swap/release keep the record mode |
| P2-C | End-to-end matrix | LATCH/OVERDUB/erase × loop division (1 bar … 1/128) × qnt (off, 1/16, `trk`) × run mode × per-track Scenes; Pattern Stack Service drop counters at 1/128 loops |
| P2-D | Docs (§4) | — |

---

## 6. Risks

- **R2-1 Write rate on short loops.** L3 removes repeat writes. A user who
  keeps turning a knob during a 1/128 loop still dirties a parameter on
  almost every step, which can exceed the busy drain (one event per 2 ms).
  Drops are counted (P1 H-3).
- **R2-2 Projection approximation.** `seq_loopShiftPosition()` uses the
  current effective scale and length for the whole span (accepted S080
  limit). A Morph or automation change to scale or length during a loop
  can move a forward snap by part of a step.
- **R2-3 Ring rate.** At 1/128 loops with fast tracks, entries for `rtk`
  arrive every few ticks. The 16-entry ring is drained each 2 ms pass;
  overflow is counted.
- **R2-4 `rnd` and once modes under a loop.** `rnd` replays a snapshot but
  draws fresh steps, so a looped `rnd` track records onto different steps
  each pass. Once modes can park past the end inside a loop window. Both
  are existing looper behaviour; recording simply follows them.
- **R2-5 Chain `stp` freeze.** While frozen, the wrap and the DDA are both
  skipped (S080), so no ring entries arrive and nothing is written. That is
  consistent, because nothing plays.

---

## 7. Open questions

1. **P2-Q1 (L2):** accept the S080 PERF priorities in §3.7 unchanged while
   recording: a SELECT-held VOICE press swaps the loop, BAR are modifiers,
   overlay triggers fire with no SELECT held. Recommend accept.
2. **P2-Q2 (L4c):** a hit recorded onto a step the loop replays sounds
   again on the next pass (looping overdub). Recommend yes.
3. **P2-Q3:** loop engage, swap, division change and release are not take
   boundaries; latches persist across them. Recommend yes.
4. **P2-Q4:** while live erase is on in PERF, VOICE presses go to the erase
   router (TRACK-hold erase), so they cannot swap or sustain the loop. A
   loop already sustained by a held VOICE stays sustained until that VOICE
   is released. SELECT and BAR still sustain. Recommend accept.
5. **P2-Q5:** should steps recorded while looping get the `lop` condition
   automatically (as fill gives `fil`)? Recommend no: the loop is a
   performance view of the pattern, not a pattern variant.
6. **P2-Q6 (L4a):** a forward snap that crosses the wrap lands on the
   replayed step after the wrap (§3.3), rather than being clamped to the
   end of the window. Recommend yes, because that is when the hit will next
   sound.
