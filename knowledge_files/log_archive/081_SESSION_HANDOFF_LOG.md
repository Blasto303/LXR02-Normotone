# Session 081 — Handoff Log

```
DATE: 2026-10-12 / 2026-10-16 (logged 2026-10-21, after Session 082 landed)
SESSION GOAL: Live recording on the performance surface: overlay triggers
              and knob automation recorded onto a recording track, live
              erase, overdub; the playback-rule changes recording needs
              (step reset rule, menu-vs-automation priority, Effect lock
              rules, 96-PPQ roll table); then linear automation ramps
              between steps, looping while recording, and a VOICE-mode bar
              LED pulse.
COMPLETED: Four parts, each plan -> schedule -> implementation -> review/
           assessment (-> fixes):
           P1 live record (plan rev 1-11, schedule rev 4, phases A1-H)
              plus fix rounds P1B (review items 1-28) and P1C (P1B review
              items 1-14 plus 15/16, review fix C-1..C-3, shuffle removed
              from placement, hardware fix 1);
           P2 linear automation ramps `asm` (D1-D10) plus follow-ups D11
              (write only on change, +128 B) and D12 (live record writes the
              first change, `LIVEREC_AUTOMATION_FIRST_CHANGE`);
           P3 loop while record (L-2, Q1-Q6, FQ1-FQ7) plus the global Morph
              fix G-1 (+7 B);
           P4 VOICE-mode bar-change LED pulse plus the pulse/flash clock-wrap
              fix.
VERIFIED ON HARDWARE: Mostly not.
  - P1 recording was used once on the device: "recorded automation is not
    underlined" -> Hardware fix 1 (§5.6).
  - Not run: P1B F6 matrix, P1C C5 matrix (incl. the C-1 boundary-roll
    check), P2 Phase E matrix incl. the 64-ramp CPU measurement, P3-C rows
    1-15, P4 rows 1-10, G-1 checks 1-4, the first-change checks 1-6. The
    host tests that were run all passed (§6.6, §7.5, §9.3).

CHANGES THIS SESSION (detail in §12):
- Core/Sequencer/LiveRecord.c/.h (NEW): live record and live erase engine.
- Core/Sequencer/sequencer.c/.h: step reset rule, deferred automation,
  R12 menu override, FX realign, roll table, QUANT_TRACK, live-record
  services (ring, capture, placement, echo, erase masks, epochs, filter),
  automation ramp engine, looper jump entries and loop-aware placement.
- Core/MIDI/MidiVoiceControl.c/.h: per-voice pending-reset mask.
- Core/MIDI/MidiParser.c: note and CC record hooks.
- Core/DSP/Effects/EffectsManager.c/.h: R13 (menuHold, one-step Morph
  lane, owner/marker removal), live FX-sequence erase, WIDE8 7-bit inverse.
- Core/Bank/Scene/Pattern/PatternData.c/.h: trigger policy, record/erase
  primitives, `track_auto_smooth`, single-target lookup.
- Core/Bank/Scene/Pattern/PatternStackService.c/.h: RECORD_STEP and
  ERASE_SPECIALS ops, staging table, target override.
- Core/Bank/Scene/Pattern/PatternTrace.h: stage 'W'.
- Core/Bank/Scene/Preset/presetManager.c/.h, presetMorphEngine.c/.h:
  per-target Scene override clears. ParameterArray.h: PAR_TRACK_AUTO_SMOOTH.
- Core/Hardware/frontPanel/buttonHandler.c: REC tap/hold, erase chord and
  router, overlay reports and R19 fixes, looper rules while recording.
- Core/Hardware/frontPanel/ledHandler.c/.h: recorded-step LED by absolute
  step, wrap-safe pulse/flash expiry, VOICE bar pulse.
- Core/Menu/menu.c/.h, menuPages.h, MenuText.h: R12 call sites, record and
  erase hooks, Q13 refusal, `rtk` cell, roll/qnt lists, `asm` cell,
  PAR_ROLL retired, recorded-automation underline.
- Core/Menu/CopyClear/copyClearSession.c, copyClearService.c; Core/Hardware/
  SD/filesystem.c; Core/Hardware/timebase.c; main.c; config.h; Makefile;
  Core/Bank/Scene/Pattern/SomGenerator.c (call signature).
- tools/convert_pattern_autosmooth.py (NEW).

KNOWN ISSUES INTRODUCED: LiveRecord now includes menu.h (deviation from
  the "LiveRecord never calls Menu" invariant, §5.6); P3 A-1 (tapping the
  VOICE of an already-looped track drops it from the loop on release); P4
  B-1 (2 B of `.data` cost 4 B), B-2 (stop+start inside one LED drain);
  documented ramp limits (§7.6) and first-change limits (§7.8).
KNOWN ISSUES RESOLVED: step rolls played 4x fast; a delayed (shuffled or
  micro-timed) trigger undid its own step's automation; every roll
  retrigger restored the base values; roll steps ignored shuffle and
  micro-timing; an empty step cut a running roll; the BAR overlay LED hung
  in other modes; LED pulses/flashes started just before the 16-bit
  millisecond wrap were lost; LATCH re-stamp/clear used the next step's
  fired bit (P1 code, FQ4); the global Morph could leak into a pending
  voice/FX Morph write (G-1).

NEXT SESSION RECOMMENDED GOAL: S083 MIDI (already planned in S082). Before
  or alongside it: run the S081 hardware lists (§13.2), measure the 64-ramp
  CPU worst case, confirm P3 A-1, add stage 'W' to
  tools/decode_devlogs.py, and re-measure the production build.
BLOCKERS: none for S083 planning; the S081 hardware checks are owed before
  live record is called done.

CRITICAL REMINDERS FOR NEXT SESSION:
- Automation priority is now R12 (menu edit beats Instrument automation
  until the voice's next trigger) and R13 for Effects; S075 F3 is amended.
- Instrument automation resets once per step at its FIRST trigger; keep the
  per-voice pending-reset mask (never a flag in the queue entry).
- Ramp samples are automation writes (seq_writeInstrumentAutomation());
  keep seq_rampNextStep() in step with the play-mode switch in
  seq_advanceTrackStep(), and the side-effect-free condition path in step
  with seq_evaluateStepCondition().
- The step-boundary ring has one producer function, seq_recRingPush()
  (TIM3, or foreground only inside the looper's PRIMASK sections).
- The master morph `mrp` is never recorded or automatable.
- The ten root S081_*.md files are superseded by this log and the specs;
  the user will delete them.
```

---

## 1. Session overview and timeline

Session 081 ran on `dev-ph6-cleanup` from the S080 close commit `02553df`
(text 556,008, data 412, bss 427,976; flash payload 556,420 B, headroom
197,244 B). It was merged to `prime` with PR #7 (`42ffde5`). Session 082
then branched `dev-ph6-midi` from `2a0fd86` and ran its own Load/Save and
background-Bank-load work. S082's handoff log says "S081 itself was never
closed out": this log is that close-out, written on 2026-10-21. Line
numbers in the root plan files refer to the S081 trees and are not valid
on the S082 tree.

S082 did not touch `LiveRecord.c/.h` (`git diff 2a0fd86 HEAD` is empty for
both). Its `sequencer.c`, `EffectsManager.c`, `PatternData.c` and
`ledHandler.c` changes are Scene 17 and background-load work layered on top
of the S081 code.

| Date | Commit | Content |
|---|---|---|
| 2026-10-12 | `96a403b` | `S081_P1_LIVE_RECORD.md` rev 1–8 (requirements, decisions Q1–Q22, F1–F14, F-A..F-S, module pass) |
| 2026-10-12 | `0a35d8d` | `S081_P1_IMPLEMENTATION.md` (schedule rev 1) |
| 2026-10-13 | `a6c96a0` | plan rev 9–11 (schedule review Appendix A, looper decisions L-2/L1–L4); schedule rev 2–4; loop-while-record split into its own plan |
| 2026-10-13 | `d21e245` | P1 code, phases A1–H; P1 code review (must-fix 1–13, should-fix 14–29) |
| 2026-10-13 | (in `b6c7b86`) | P1B schedule, implementation, review (must-fix 1–5, minor 6–14) |
| 2026-10-13 | (in `b6c7b86`) | P1C schedule, implementation, review (C-1..C-4); C-1..C-3 fixed; shuffle removed from placement; hardware fix 1 |
| 2026-10-13/14 | `b6c7b86` | `S081_P2_LINEAR_AUTOMATION.md` rev 2 (D1–D10), schedule, implementation; D11 writes-on-change |
| 2026-10-14 | `4d5c2c5` | D12 first-change follow-up (`LIVEREC_AUTOMATION_FIRST_CHANGE`) |
| 2026-10-14/16 | (in `2a0fd86`) | `S081_P3_LOOP_WHILE_RECORD.md` rev 2–5, schedule, implementation, assessment, FQ4 explanation, global Morph examination, G-1 |
| 2026-10-16 | (in `2a0fd86`) | `S081_P4_VOICE_MODE_BAR_LED.md` rev 1–3, schedule, implementation, assessment |
| 2026-10-16 | `2a0fd86` | final S081 code: text 573,296, data 416, bss 429,112 |

**Documents produced** (all in the repository root, all superseded by this
log and the specs, §15):

| File | Role |
|---|---|
| `S081_P1_LIVE_RECORD.md` | P1 plan, rev 1–11, Appendix A schedule review |
| `S081_P1_IMPLEMENTATION.md` | P1 schedule rev 4, implementation log, RAM summary, P1 code review |
| `S081_P1B_IMPLEMENTATION.md` | P1B fix schedule, log, review |
| `S081_P1C_IMPLEMENTATION.md` | P1C fix schedule, log, review, C-1 fix, shuffle removal, hardware fix 1 |
| `S081_P2_LINEAR_AUTOMATION.md` | P2 plan rev 2 plus §13 (D11, D12) |
| `S081_P2_IMPLEMENTATION.md` | P2 schedule, work notes, first-change follow-up |
| `S081_P3_LOOP_WHILE_RECORD.md` | P3 plan rev 1–5 |
| `S081_P3_IMPLEMENTATION.md` | P3 schedule, progress log, assessment, FQ4 in practice, Global Morph examination, G-1 |
| `S081_P4_VOICE_MODE_BAR_LED.md` | P4 plan rev 1–3 |
| `S081_P4_IMPLEMENTATION.md` | P4 schedule, progress log, assessment |

**LED words used throughout (user's meaning, the reverse of the code
names):** *flash* = continuous (`led_setBlinkLed()`); *blink* = one
transient pulse that returns to the previous state (`led_pulseLed()`).

---

## 2. Part P1 — live record: requirements and decisions

### 2.1 Requirements (final)

| # | Requirement |
|---|---|
| R1 | Overlay triggers (VOICE with BAR `<`/`>` in PERF) record an active step (note, velocity, roll rate, roll length) at the track's position, quantised or un-quantised (micro-timing delay). |
| R2 | MIDI notes record the same way (later; API unchanged). |
| R3 | Knob edits of Instrument, Effect and Scene parameters record automation onto the recording track `rtk` under the LATCH rules. |
| R4 | MIDI CC automation, same rules (later; API compatible). |
| R5 | PERF-page actions are not recorded, except the Morph cells `1VM`..`6VM` and `fxm`, which record like the same targets on the VOICE mix and Effect pages (P3 FQ5). `mrp` is never recorded. |
| R6 | One recording-target Scene: the played Scene of `rtk`. Anything belonging to another Scene is not recorded. Best effort; playback always has priority. |
| R7 | `rtk` follows every active-track change in any mode. SHIFT+REC can override it until the next track selection. At boot it is the active track. |
| R8 | A recorded trigger overwrites trigger, note, velocity, micro-timing and roll; never automation; never the condition except R10. |
| R9 | A quantised flam records the main on the quantise division and the grace on the earlier step with a delay; main only if both cannot be placed. |
| R10 | Any step that receives anything while PERF is held (fill active) gets `fil`, replacing its condition. |
| R11 | MIDI is separable: two thin call sites only. |
| R12 | Outside record: a menu edit of an automated Instrument parameter sounds at once and suppresses that parameter's automation until the voice's next trigger (any source), a transport change or a Scene change. For all other parameters the menu value applies until the next automation of that parameter. A menu value is never reverted by anything. |
| R13 | Track automation of Effect and Scene parameters is set once and forgotten. An FX-sequence lock (including the FX Morph lane) applies at its step start and returns to the menu value at its step end. Lock beats track automation; a menu edit beats both until the next automation. |
| R14 | Live erase (§3.8). |
| R15 | Overdub: holding REC records automation only on steps where the parameter was adjusted. No latching. Never toggles the record state. Every overdub entry and exit is a new take. Works with fill. Mutually exclusive with live erase. |
| R16 | `qnt` gains `trk` (track step-grid quantise); the looper uses it for per-track loop starts. |
| R17 | Step roll table corrected to 96 PPQ, plus `64T` and `128`. |
| R18 | *(Withdrawn.)* The SHIFT Morph view is unchanged; its edits are not recordable. Selecting a parameter there (copy, clear, live erase) works. |
| R19 | Overlay fixes: the BAR overlay LED shows only in PERF (state kept for re-entry); switching overlays while a VOICE trigger is held updates the held trigger/roll. |

### 2.2 Decision log (user, 2026-10-12/13)

| Item | Decision |
|---|---|
| Q1 | Non-target-Scene items are dropped. |
| Q2 | Master-clock quantise plus `trk`; `rnd` run mode snaps backwards only. |
| Q3 | Per-PPQ-tick µs stamp. MIDI timestamps and realtime-MIDI queue-skip priority deferred to the MIDI rework. |
| Q4 | Superseded by R12 and LATCH. |
| Q5 | Dedicated step-boundary ring. |
| Q6 | Per-entry `patSvc` writes first; a composite op only if drops are measured. |
| Q7 | Record track scale and shuffle; never length. |
| Q8 | `fil` on every step that receives anything while PERF is held. |
| Q9 | Un-quantised flam in one step: keep the main. |
| Q10 | Type mismatch across Scenes: drop. |
| Q11/F1 | Step rolls are 4× fast on hardware: fix the table, add 64T/128. |
| Q12 | `def` note: no note special. |
| Q13 | Silent refusal; the shown value does not change. |
| Q14 | `PAR_ROLL` retired in place. |
| Q15 | Erase follows the target rule. |
| Q16/F10 | Morph view unchanged; its edits are not recorded; parameter selection there works. |
| Q17/F2/F3 | R13. |
| Q18 | `rtk` only, runtime (not saved). |
| Q19/F6 | The menu value is never reverted, including at the end of recording or overdub. |
| Q20 | Muted tracks may be recorded. |
| Q21 | Arming while stopped is allowed; recording starts with playback. Same for live erase. |
| Q22 | Tracks 6/7 are ordinary `rtk` values. |
| F5 | Overdub records triggers; latches start fresh; entry/exit = a new take. |
| F7 | SEQ- and TRACK-button erase are held gestures; SEQ erase targets the active track. |
| F8 | Erase and overdub are mutually exclusive. |
| F9 | Looper uses `trk` for per-track loop starts. |
| F11 | Any Scene change resets latches. |
| F12 | Any trigger of the voice ends the R12 override. |
| F13 | REC LED blinks on overdub entry and exit, then shows the record state. |
| F14 | Latches cover the whole target ID space as bitmaps. |
| F-A | Keep `32T`; drop `48` from the roll list. |
| F-B | Overlay switch while held: single → roll starts repeating at the new rate; roll → single/flam stops repeating, no extra hit; single → single does nothing. |
| F-C | A recorded roll changes rate at the step containing the switch. |
| F-D | Own-Instrument re-stamps go only on steps of `rtk` that fire this pass, or would have fired if the track were not muted. |
| F-E/F-F | While erase is enabled: PLAY is a plain transport toggle (never the chain menu); SEQ never opens the chain menu; SEQ in PERF does nothing. Live erase does nothing in Load/Save and global settings. In FX mode it erases the FX sequence. |
| F-G | The recorded value is the menu value, as observed. Raw playback with Morph ≠ 0 is accepted behaviour. |
| F-H | No migration: stored roll indices simply shift. Backward compatibility is never a constraint. |
| F-I | Linked Scenes (VOICE edit mask) stay up to date during the take: menu edits fan out as today; FX-sequence erase fans out (deferred reconcile). Pattern data never fans out. |
| F-J | FX knob erase removes both the track automation on `rtk` and that FX lane's locks as the playheads pass. |
| F-K | SOM_GEN is incomplete future work: live erase and live record ignore it. |
| F-L | Instrument automation resets at each step's **first** trigger only (§3.1). Flams and rolls slower than the step rate are recorded as individual steps. |
| F-M | Pattern AutoSave is gated on `liveRec_patternWriting()` (armed and running), not on armed. |
| F-N | COPY without SHIFT stays refused while armed. |
| F-O | Toggling the held overlay's own BAR off stops the roll repeating (no extra hit) and ends the recorded roll at that step. |
| F-P | Live-hold suppression of stored triggers while a recorded roll is held, only while recording. |
| F-Q | Effect/Scene edits in the SHIFT Morph view count as menu edits for R12/R13 (overlay cleared, `menuHold` set); still not recordable. Try on hardware. |
| F-R | A roll runs its own length (step interval × fill, from its delayed first hit). Only the next step's trigger cuts it: a plain hit ends it, a matching roll carries it, a different roll replaces it. An empty or automation-only step does not. |
| F-S | Roll steps take the track's shuffle as well as micro-timing. |
| L-2 | (2026-10-13) The looper is not locked out by record or live erase. Implemented in P3; P1 kept the S080 lockout as an interim. |
| L1 | (2026-10-13) Kept: with `qnt trk`, each looped track's loop starts at its own last step start (per-track anchors in `seq_loopSnapshotTracks()`). `off` keeps the fastest-track anchor; 1/8..1/64 keep the grid boundary. Anchors choose only which window a track replays; step timing under the loop is unchanged. |
| L3 | (2026-10-13) Adopted in P1: a step-end automation write is queued only when the step holds no entry for the target or a different value; a clear only when an entry exists. |
| Shuffle | (2026-10-13, P1C) Shuffle applies only at playback; recording never uses it. |
| RAM | Approved, including follow-ons. |

### 2.3 Pre-existing defects found in the module pass (plan §3.3)

These were found while auditing the code each change touched; most were
fixed as P1 prerequisites.

1. **A delayed trigger undid its own step's Instrument automation.** TIM3
   queued a step's automation at the step start; a shuffled or micro-timed
   trigger fired later; every trigger went through
   `voiceControl_triggerNow()` → `seq_restoreAutomatedParameters()`, which
   restored the Morph base for every held parameter, including the ones the
   same step had just set. The foreground order (`voiceControl_processPending()`
   then `seq_drainPendingAutomation()`, `main.c`) is right for an on-time
   trigger only. Fixed by §3.1.
2. **Every roll retrigger restored the base.** A step's automation lasted
   only until the second roll hit. Fixed by §3.1.
3. **Dead Effect overlay owner state** under R13 (`owner[64]`,
   `pending_end`, `pending_track`, `owner_tracks`). Removed (§3.3).
4. **Realign did not reset Effect overlays.** Fixed (§3.3).
5. **The PAT4 reader has no roll-range check** and
   `convert_pattern_specials.py` never reads roll. Nothing needed.
6. **Two roll name tables:** `stepRollRateNames` (STEP list) and
   `rollRateNames` (legacy `PAR_ROLL`, retired with it).
7. **The Pattern Stack Service queue format** is 6 bits operation / 10 bits
   track+step / 16 bits payload; `PATTERN_DYNAMIC_STACK.md` §12.3 had
   documented a different 3/4/3/7/9/7 layout. Spec corrected at this close.
   The payload cannot hold a full record spec, so a staging table is used.
8. **The service target was `seq_activePattern` in eight places**; the
   target override replaced them with one desired-Scene helper.
9. **Dependency direction:** `seq_loopEngage()` turned recording off inside
   the Sequencer; that moved to buttonHandler (P1) and was removed (P3).
10. **LiveRecord must not depend on Menu:** the value resolver reads owner
    getters directly (SceneData, Pattern region, EffectsManager).
11. **STEP toggles bypass the Pattern Stack Service**
    (`buttonHandler_setRemoveStep()` → `pat_toggleStep()`), so the Q13
    refusal is an explicit UI check.
12. **Copy/clear refuses while armed**, also while stopped (kept, F-N).
13. **Pattern AutoSave was suspended while `seq_recordActive`**, also while
    stopped (changed, F-M).
14. **`led_pulseLed()`** inverts for 50 ms then re-renders: the overdub
    "blink".
15. **Roll steps ignored micro-timing and shuffle** (against S079 §2.1).
    Fixed (§3.1).
16. **Any step without a roll ended a running roll at its start**, even an
    empty step. Fixed (F-R).

### 2.4 Removed or replaced legacy code

| Item | Action |
|---|---|
| `seq_recordTrigger()` (folded every recording into bar 1), `seq_quantize()` (retired 128-sub-step bar) | removed; replaced by LiveRecord and `seq_quantiseTick()` |
| `seq_setRecordingMode()` per-track refusal | removed; LiveRecord arm path |
| legacy live roll (`seq_setRoll()`, `seq_rollState`, `seq_rollRate`, `seq_setRollRate()`, its TIM3 block) | removed (dead) |
| `PAR_ROLL`, `rollRateNames`, `MENU_ROLL_RATES` | retired (Euklid cell, parse, default, dtype) |
| old erase (`seq_eraseActive`, `seq_setErasingMode()`, SHIFT+COPY handlers, TIM3 block) | replaced by live erase |
| `SEQ_LED_DIRTY_REC_SUB`, `recordSubStep`, `led_updateRecordedSubStep()` | removed (no writer) |
| `bh_state.seqRecording`, `bh_state.seqErasing` | removed |
| Effect step markers (identity bit 11, `SEQ_PENDING_TYPE_FX_STEP_BIT`, `seq_queueEffectStepMarker()`, `seq_effectAutomationTracks`, `seq_setEffectAutomationTracks()`, `effects_automationStepBegin/Flush()`, `effects_automationPublishOwners()`) | removed (R13) |
| `held_morph`/`held_morph_valid`, `effects_seqLatchMorph()` | removed (one-step Morph lane) |
| `seq_recordActive` | kept, written only by LiveRecord as the published "writing" flag; its old readers now use LiveRecord queries |
| `preset_applySoundParameter(…recordAutomation)`, STEP armed-automation remnant, `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()` | left for the MIDI rework |

---

## 3. Part P1 — behaviour changes outside record (Phase A)

These change how existing patterns sound (intended; plan risk R-4/R-5a).

### 3.1 Step reset rule (§5.8, F-L, F-R, F-S)

**Rule.** Instrument automation (the held bits and the R12 override) resets
at a step's **first** trigger only:

| Step | Reset at |
|---|---|
| Plain trigger | its hit |
| Micro-timed / shuffled trigger | its delayed hit |
| Roll step | its first hit; later hits of that roll never reset |
| Roll carried across steps | each step's renewal of the roll, though the renewal fires no new first hit |

- No reset where a roll's hits run into the next step's time; the next
  step's own trigger resets.
- Live triggers (overlay presses, MIDI notes, preview) reset at each new
  press (F12). Each flam hit counts as a press. Overlay roll repeats from
  the 1 ms timer do not reset.

**Mechanism.**

- **Per-voice pending-reset mask** `voiceControl_pendingReset` (`u8`, bits
  0..6, `MidiVoiceControl.c`), never a flag inside the queue entry. Reason:
  `voiceControl_noteOff(voice)` deletes every queued entry for the voice,
  and `seq_triggerVoice()` calls it before every `noteOn()`, so a flag in
  the entry is lost when the entry is dropped. A stored note/velocity byte
  ≥ 128 is also not range-checked on PAT4 load.
  - Enqueue: a resetting trigger or `voiceControl_resetOnly()` sets the bit;
    `voiceControl_noteOnNoReset()` (roll retriggers 2+) does not.
  - `voiceControl_popPending()` tests and clears the voice's bit inside its
    existing PRIMASK section and returns it as `reset`.
  - `voiceControl_triggerNow(voice, note, vel, reset)` keeps the order:
    `preset_applyDeferredSceneSlotForTrigger()` → restore (only if `reset`)
    → `instrumentManager_triggerTrack()`, sidechain, LED. The restore must
    not move ahead of the deferred Scene-slot apply (schedule review A-13).
  - After the ring is empty, `voiceControl_processPending()` takes the
    remaining mask under PRIMASK and restores for each bit (reset-only
    renewals and resets whose trigger entry was dropped). All of this runs
    before `seq_drainPendingAutomation()`, so a step's own automation lands
    after its reset.
  - Accepted edge: if the foreground falls behind, a reset bit set by step
    N+1 can be consumed by a still-queued roll retrigger of step N; that
    retrigger then sounds with the base values.
- **`seq_triggerVoice(voice, vol, note, reset)`** gained `reset`: on-time
  step hits, deferred fires, `seq_rollBegin()`'s first hit, overlay and
  preview pass 1; `seq_processRollTicks()` retriggers pass 0; the overlay
  1 ms timer passes `bh_overlayCount != 0xFF` (1 for the flam main, 0 for
  roll repeats); SOM passes 1. A roll renewal (carry path in
  `seq_rollBegin()`) calls `voiceControl_resetOnly()`.
- **Deferred automation.** A step whose first trigger is deferred (shuffle
  or micro-timing) queues its automation entries at the deferred fire time,
  in TIM3, with the resetting trigger: one routine `seq_fireDeferred(track)`
  serves the expiry path and every early flush. It queues the step's
  automation, then starts a roll (`seq_rollBegin()`) or fires a plain hit
  with reset = 1. The deferral state gained `seq_trackShuffleStep[7]` and
  `seq_trackShuffleRoll[7]`.
  - Automation-only steps still queue at the step start (schedule review
    A-1: the first schedule would have broken them).
  - The early flush (a still-pending deferral from an earlier step) runs
    only when the new step itself queues automation (P1B SQ-11), so a long
    delay is not cut by an empty next step.
- **Roll steps honour micro-timing and shuffle** through the same deferral.
- **Roll end (F-R):** a running roll ends only when the new step has a
  trigger that fires. The roll engine ends a roll by itself after its fill
  window (`seq_rollRemaining`).

### 3.2 R12: menu edit beats Instrument automation until the next trigger

- A menu edit of Instrument parameter `(slot, local)` writes the sounding
  value at once (endpoint stored, interpolated at the Morph amount, as
  before), sets an **override bit** in `seq_menuOverride[slot]` (6 × `u64`)
  and clears the held bit (`seq_menuOverrideSet(slot, local)`, foreground).
- One call per edited cell, for `cell->slot`, before the edit-mask Scene
  loop (schedule review A-14: the override belongs to the runtime slot, not
  to each Scene).
- While the bit is set, `seq_drainPendingAutomation()` skips that target.
- The override ends at any trigger of the voice (cleared in
  `seq_restoreAutomatedParameters()` **before** its "held mask is zero"
  early return, schedule review A-2), at transport start/stop/reset
  (`seq_setStepIndexToStart()`), at a Scene change
  (`seq_selectActivePattern()`, `seq_alignActivePatternToScene()`), at a
  per-track Scene change (`seq_setTrackPlayedScene()`, that slot; tracks
  5/6 share slot 6) and at realign.
- The Morph sweep guard is unchanged; MIDI CC keeps its old path until the
  MIDI rework.
- This amends the binding S075 F3 rule (automation always wins until the
  next trigger): a menu edit now wins until the next trigger.

**Scene targets ("set once and forget; the menu applies until the next
automation"):**

| Target | Before | After |
|---|---|---|
| Voice Morph `Nvm` | menu edit cleared the override | unchanged |
| FX send `Nfx`, audio out `Nou` | override persisted until transport restart | a menu edit clears that slot's override (`preset_clearFxSendStepOverride()`, `preset_clearAudioOutStepOverride()`, which also re-applies the retained route) |
| Track length/scale/shuffle (405..425) | override persisted | a menu edit clears that track's override and refreshes the effective cache (`presetMorph_clearTrackParamStepOverride()` + `seq_refreshTrackEffectiveParams()`; `menu_clearTrackTimingOverride()` in menu.c, Normal and Morph-endpoint commits). Length added by schedule review A-21 |
| `7dc` | — | its clear on the KIT_SETTING commit |
| `fxm` | held until the Scene-rule reset | a menu edit clears it; an FX Morph-lane lock beats it for one step |

Each clear pairs with `seq_clearSceneAutomationBit(index)`.

### 3.3 R13: Effect parameters

Per Effect parameter `i` (the Morph lane works the same way, with `fxm` as
its track overlay):

```
lock(i)     FX-sequence step locks lane i: applies at that FX step's start,
            released at that step's end
overlay(i)  last Pattern-track automation of i: set once, held
menuHold(i) a menu edit of i since the last automation of i

eff(i) = menuHold(i) ? menu(i)
       : lock(i)     ? lock value
       : overlay(i)  ? overlay value
       : menu(i)

Morph base = menuHoldMorph ? menu : lockMorph(current step) ? lock
           : morph_override (fxm) ? override : menu
```

- A track overlay is replaced by another track's automation of `i` (last
  writer wins). It is cleared by an FX lock on `i` at that lock step's start
  (so after the lock step `i` returns to the **menu** value), by a menu edit
  of `i`, and by a Scene change, reload or realign.
- An FX lock is per step only; the former held Morph lane (holding through
  later unlocked steps) is one-step like every lane.
- A menu edit (`effects_setParameter()`, `effects_setMorphAmount()`, when
  the origin is the active Scene and the row is in the runtime type) clears
  the overlay and sets `menuHold(i)` (`u64` + `menuHoldMorph`). The next
  automation of `i` (an FX lock step start, or a track entry:
  `effects_applyAutomation()`, `effects_setMorphAutomation()`) clears it and
  applies. A lock already running when the menu edit happens stays
  overridden for the rest of its step.
- In `sel` run mode, lock step start = the step being selected; end =
  another step being selected.
- New `effects_lockClearLayers()` runs at FX lock step start
  (`effects_seqConsume()` STEP and `effects_seqSelect()`).
- Removed: step markers, `pending_end`, owner publication, the
  "failed-condition/muted steps keep overlays" rule, held Morph lane.
  `effects_state_t` 84 → 80 B, `effects_automation_t` 184 → 120 B.
- `effects_automationReset()` also clears `menuHold` and the live-erase
  state.

**Realign and the FX sequence** (schedule review A-5, plan error fixed):
`seq_realignActivePatternToMasterClock()` and
`seq_realignTrackToMasterClock()` call two new functions:

- `seq_fxRealignToMasterClock()`: under PRIMASK, the same mapping as the
  track realign (`position = (elapsed << 8) / interval`, accumulator =
  remainder, counter = position + 1); publishes the step for `position` as
  a STEP event; `sel` untouched. Needed because the FX position drifts
  after an FX step-scale change.
- `seq_requestEffectOverlayReset()`: sets only the overlay reset latch.

Not `seq_fxPublishReset()`: its RESET event invalidates the current FX
step and drops the running locks.

**Item 29 (from the P1 review), decided 2026-10-21:** a Scene change that
does not reset (`pcr` off) runs the full Pattern realign, so it now also
realigns the FX sequence to the master clock. The review flagged this
against the documented "menu values until the next FX boundary" rule. The
user's decision: **correct as implemented** — with `pcr` off, a Scene change
keeps every track and the FX sequence aligned to the master clock, the same
behaviour as the Pattern tracks. In detail: the realign publishes the
aligned FX step at once; on a whole-Scene change the following Effect
activation (`effects_activateScene()`, `seq_step_valid = 0`) clears that
step, so the new Scene's locks start at the next FX boundary from the
aligned position; on a chain repeat to the same Scene
(`seq_chainRepeatSelf()`, no re-apply) the aligned step's locks apply at
once.

### 3.4 Step roll rates (R17)

`seq_processRollTicks()` runs on the 96-PPQ tick; the table was in 24-PPQ
values (4× fast, confirmed on hardware).

| Index | Label | Ticks |
|---|---|---|
| 0 | off | 0 |
| 1 | 1/8 | 48 |
| 2 | 8tr | 32 |
| 3 | 16 | 24 |
| 4 | 16t | 16 |
| 5 | 32 | 12 |
| 6 | 32t | 8 |
| 7 | 64 | 6 |
| 8 | 64t | 4 |
| 9 | 128 | 3 |

`48` was dropped (same rate as `32T`); later indices moved down by one with
no migration (F-H). `seq_rollPpq()` guards `rate > 9`; the STEP roll clamp
and `menu_getRollRateName()` go 8 → 9. Byte format unchanged.

### 3.5 `qnt trk` (R16)

`quantisationNames` = `off 1/8 1/16 1/32 1/64 trk`; `QUANT_TRACK` = 5 after
`QUANT_64`; `seq_setQuantisation()` clamps 0..5. Recording snaps to the
nearest step start of the recorded track's own grid. Looper: each looped
track's loop start is its own last step start, computed with the `back`
offset inside the per-track loop of `seq_loopSnapshotTracks()`;
`seq_loopEventAnchor()` is unchanged for `off` and the grid (schedule
review A-15). Edge kept from S080: a track whose step is longer than the
loop is anchored more than one loop length back and replays that step's
start every loop. Filesystem: `quantisation` = 5 needs no parser change.

### 3.6 Overlay fixes (R19)

- BAR overlay LEDs are shown only in PERF. `handleModeButtons()` sets the
  BAR LEDs per mode (leaving PERF restores the target mode's state;
  entering PERF restores them from `bh_overlayActive`).
- **Switch while held** (`buttonHandler_overlaySwitchHeld()`, new
  `bh_overlayHeldType`, +1 B), called from both PERF BAR presses:
  roll → roll changes the repeat interval from the next hit; single → roll
  starts repeating at the new rate one interval after the switch (no hit at
  the switch); roll → single/flam stops repeating, no extra hit; single →
  single does nothing. Toggling the held overlay's own BAR off stops the
  repeat (F-O).

---

## 4. Part P1 — live record design (as built)

### 4.1 Module ownership and dependency direction

`Core/Sequencer/LiveRecord.c/.h` (prefix `liveRec_`) owns: the mode (OFF /
LATCH / OVERDUB / ERASE), arm-while-stopped, `rtk` follow/override, target
Scene negotiation, trigger → (step, specials) resolution, the latch, dirty
and erase bitmaps, step-end write/clear/re-stamp, the "is this target's
playback suppressed" filter registration, and the drop counters. It does
not own transport, DDA or projection (Sequencer), block encoding
(PatternData), admission/handover (PatternStackService), sound (the overlay
and MIDI fire their own triggers), commit, fan-out, Morph or DSP.

```
buttonHandler ──► LiveRecord ──► Sequencer (capture, placement, quantise,
Menu ──────────► LiveRecord        ring, echo/live-hold/erase masks, filter)
MidiParser ────► LiveRecord ──► PatternStackService (record/erase ops, target)
                 LiveRecord ──► EffectsManager (FX-sequence erase), SceneData getters
Menu ──► Preset / EffectsManager / Sequencer (R12/R13 menu-edit rules)
```

The Sequencer never calls LiveRecord (the drain filter reads bitmaps that
LiveRecord registers with `seq_setAutomationFilter()`). LiveRecord never
calls buttonHandler or MidiParser. **Deviation:** Hardware fix 1 made
LiveRecord include `menu.h` and call `menu_automationTargetRecorded()`
(§5.6); the Load suspend uses `patSvc_replacePending()`, not
`menu_isStorageBusy()` (schedule review A-11).

Gesture timing (REC hold, SHIFT/CLEAR chord) stays in buttonHandler; LiveRecord
receives decided events. `liveRec_service()` runs in the foreground 500 Hz
pass immediately before `patSvc_tick()` (`timebase.c`), so idle-direct
writes execute in the same pass. `liveRec_init(menu_getActiveVoice())` runs
in `main.c` after `copyClear_init()`.

### 4.2 Public API (`LiveRecord.h`, final)

```c
void    liveRec_init(uint8_t activeTrack);
void    liveRec_service(void);                 /* 500 Hz, before patSvc_tick() */
/* Mode (buttonHandler) */
void    liveRec_setLatch(uint8_t on);          /* REC tap */
void    liveRec_overdubBegin(void);  void liveRec_overdubEnd(void);
void    liveRec_eraseBegin(void);    void liveRec_eraseEnd(void);
uint8_t liveRec_mode(void);                    /* LIVEREC_MODE_OFF/LATCH/OVERDUB/ERASE */
uint8_t liveRec_armed(void);                   /* mode != OFF */
uint8_t liveRec_patternWriting(void);          /* armed && seq_isRunning() */
/* Recording track (Menu) */
void    liveRec_onActiveTrackChanged(uint8_t track);
void    liveRec_setRecordTrack(uint8_t track);
uint8_t liveRec_recordTrack(void);
uint8_t liveRec_activePatternLocked(void);     /* Q13: writing and T != A */
/* Triggers (buttonHandler overlay; MidiParser) */
void    liveRec_triggerIn(const liveRec_trigger_t *t);
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex);
void    liveRec_rollRelease(uint8_t track);
/* Automation (Menu; MidiParser stub) */
void    liveRec_paramIn(uint16_t target9, uint8_t source);
/* Erase gestures */
void    liveRec_eraseParam(uint16_t target9, uint8_t fxLane);
void    liveRec_eraseSeqHeld(uint8_t track, uint8_t held);  /* active track */
void    liveRec_eraseFxSeqHeld(uint8_t held);               /* counted */
void    liveRec_eraseTrackHeld(uint8_t track, uint8_t held);
void    liveRec_eraseReleaseHeld(void);        /* MODE press: drop hold gestures */
```

`liveRec_trigger_t` = {track, note, velocity, kind, roll, source,
note_is_default}; kinds `LIVEREC_HIT_SINGLE`, `FLAM_GRACE` (fires in
`buttonHandler_overlayTrigger()`), `FLAM_MAIN` (fires 1/32 later in
`buttonHandler_overlayTick()`), `ROLL_START`, `ROLL_HIT`; sources
`LIVEREC_SRC_OVERLAY`, `LIVEREC_SRC_MIDI`. `liveRec_paramIn()` carries no
value: the write reads the current retained value (§4.7). `liveRec_forceOff()`
existed in P1 for the looper lockout and was removed in P3.

### 4.3 Modes and the REC button

| Gesture | Result |
|---|---|
| REC tap (released before `DOUBLE_CLICK_TIMEOUT`, 300 ms) | toggles LATCH on/off **on release** (risk R-11: a tap is delayed up to 300 ms) |
| REC held past 300 ms | OVERDUB while held (`buttonHandler_tick()` hosts the timer, `bh_recPressTick` `u16` wrap-safe, `bh_recArmed`); release restores the prior LATCH/OFF state; entry and exit each start a new take |
| SHIFT+REC | latched settings menu (unchanged); REC in the menu closes it without toggling |
| REC while looping | P1: ignored, and engage turned record off; **P3: works normally** |
| SHIFT+CLEAR while LATCH or OVERDUB | live erase; ends overdub |
| REC while erase is enabled | nothing, SHIFT held or not (P1C BC-1) |

- Armed while stopped: nothing happens until playback starts.
- REC LED: on in LATCH, off otherwise; **blinks** (`led_pulseLed(LED_REC)`)
  on overdub entry and exit; **flashes** while the SHIFT+REC menu is open
  and armed.
- Fill (PERF hold) and REC hold may be held together: overdub writes get
  `fil`.

### 4.4 Recording track and target Scene (R6, R7)

- **`rtk`** = `PAR_AUTOM_TRACK` (runtime only), RECORDING_PAGE sub-page 2
  cell 0, short `rtk`, long `RecTrk` (reusing `TEXT_AUTOMATION_TRACK`),
  values 1..7 (stored 0..6, `DTYPE_0B127` clamped 0..6 with a name special
  case). Commit via `menu_parseGlobalParam()` → `liveRec_setRecordTrack()`;
  excluded from the RECORDING_PAGE settings-dirty mark. Display and edit
  seed read `liveRec_recordTrack()` (P1B MN-4). `menu_setActiveVoice()`
  calls `liveRec_onActiveTrackChanged(v)` only when the track changes, so
  re-selecting the same track keeps an override. Each change is a new take.
- **Target:** P(t) = track t's played Scene; A = active Scene; T = P(rtk).
  While writing (armed **and** running, P1B item 22) and T ≠ A, LiveRecord
  calls `patSvc_requestTarget(T)`; otherwise `patSvc_releaseTarget()`.
  While `!patSvc_targetOpen(T)` (handover), every write is dropped and
  counted (cause 1).
- **Ownership** (`liveRec_targetOwned()`):

  | Item | Recorded only if |
  |---|---|
  | Trigger on track t | P(t) == T and the slot type in T matches A |
  | Instrument parameter of slot s | the track playing s plays T (slot 6: `rtk` when `rtk` is track 6 or 7, else track 6), and the type in A equals the type in T |
  | Effect or Scene parameter | T == A |
  | Erase on track t | P(t) == T (masks gated, P1B LR-17); FX lanes only when T == A |

- **Scene epoch vs playback epoch** (P1B SQ-10/LR-9). `seq_sceneEpoch`
  bumps only when a track's Pattern changes (direct/queued/chain change,
  per-track reassignment, Scene/Bank load alignment). `seq_playbackEpoch`
  bumps at every playhead discontinuity (those plus transport start/stop,
  PC reset, realign). A Scene epoch starts a **new take** (latches and dirty
  cleared, origin re-captured, roll/grace discarded, FX-erase pending mask
  flushed). A playback epoch only ends per-step capture state (previous
  ring step, open roll, pending grace); latches and held erase gestures
  survive, so arming or erasing while stopped carries into playback.
- A filesystem Pattern replacement (`patSvc_replacePending()`) releases the
  target, sets writing 0, turns the filters off and suspends the drain.
- **Q13 refusal:** while `liveRec_activePatternLocked()` (writing and T ≠ A,
  P1B LR-1), edits that write the viewed Pattern are refused silently
  before `parameter_values[]` or the display change: `PAR_STEP_*`, track
  length/scale/shuffle/play mode/loopable, the step-automation editor,
  `va_writeAutomationFromKnob()`, the `sa_*` writers,
  `menu_broadcastStepSpecial()` (in `menu_parseKnobDelta()` and
  `menu_parseEncoder()`), and STEP toggles in
  `buttonHandler_setRemoveStep()`.

### 4.5 Sequencer services for live record

All in `sequencer.c/.h`; every TIM3-visible field is a byte or a
PRIMASK-published pair, and the TIM3-shared state is `volatile` (P1B
SQ-12, following the `seq_pending_automation` precedent).

| Service | Detail |
|---|---|
| Published target | `seq_setRecordTarget(track, writing)` → `seq_recTrack`, `seq_recWriting` |
| Step-boundary ring | `seq_recRing[16]` (`u16`) + head/tail; packing: step bits 0..6 (`0x007F`), track bits 7..9 (`0x0380`), fired `0x0400`, jump `0x0800` (P3). One producer function `seq_recRingPush(track, step, fired, jump)` (P3); it pushes only while writing and for `rtk` or a live-held track. Consumer `seq_takeRecBoundary(seq_rec_boundary_t *)` reads the head once. A full ring drops; DEV builds count `seq_recRingDrops` and trace `'W'` cause 5 (P1C SC-3) |
| Fired bit | known at the step start: bit 15 set and the condition passed. A muted `rtk` evaluates the condition without side effects (`seq_conditionPassesPure()`, sharing `seq_conditionEval()`; no `seq_trackLastConditionFired` update, a fresh RNG draw for probability) and also needs bit 15 (P1B SQ-1) |
| Echo suppression | `seq_suppressStepOnce(track, step, aheadTicks)` sets `seq_echoStep[track]` and `seq_echoDeadline[track]` = capture tick + aheadTicks + one interval + 1 under PRIMASK. TIM3 drops an expired mark before testing it; a matching mark is consumed and the stored trigger skipped (fired still 1; automation still plays). Marks are also cleared at the epoch/transport sites, on an effective length/scale change, and on a loop resume (P3) |
| Live hold | `seq_setLiveHold(track, on)` → `seq_liveHoldMask`: the track's stored triggers are skipped while a recorded roll is held (F-P) |
| Deferral cancel | `seq_cancelPendingTrigger(track, step)` mutes only the matching pending deferral (`seq_trackShuffleMuteMask`): `seq_fireDeferred()` still queues that step's automation, then returns without a trigger (P1B SQ-8/9) |
| Erase masks | `seq_setEraseMasks(specials, full)` (one PRIMASK section, P1C SC-2). Full: clear bit 15, `patSvc_enqueueErase()`, skip the step's automation. Specials: clear bit 15, `patSvc_enqueueEraseSpecials()`; automation still queues |
| µs stamp | `seq_ppqStampUs`, `seq_ppqPeriodUs` written at the head of `seq_processSchedulerTick()` from `timebase_tim2Now()` |
| Epochs | `seq_getPlaybackEpoch()`, `seq_getSceneEpoch()` (the per-track Scene bump sits in `seq_realignTrackToMasterClock()`, equivalent while the buttonHandler Scene path is the only caller of `seq_setTrackPlayedScene()`; P1B minor 12, accepted) |
| Drain filter | `seq_setAutomationFilter(latch, erase, eraseTrack)`: the drain skips an entry whose target bit is set in the latch bitmap (any owner track) or in the erase bitmap with owner track == `rtk`. (P1 passed the dirty bitmap as the erase filter; P1B LR-2) |
| Capture | `seq_captureTrackTime(track, seq_track_time_t *)`: one PRIMASK snapshot of elapsed tick, µs stamp and period, `nowUs` (the capture's own µs time, P1B SQ-4), step index, accumulator, effective interval/length/shuffle, play mode/state, played Scene, and (P3) loop fields |
| Placement | `seq_placeFromSnapshot(track, snap, deltaQ8, snapToStep, seq_place_t *)` (§4.6) |
| Quantiser | `seq_quantiseTick(tick, frac8)`: master grid only (1/8..1/64 = 48/24/12/6 ticks), half test `rem*256 + frac8 >= q*128`; `off` and `trk` return the tick unchanged |
| Recorded-step LED | `seq_markRecordedStep(track, scene, step)`; `led_updateRecordedMainStep()` draws by absolute step only in `menu_currentBar` on the visible track |

### 4.6 Trigger recording

**Gates:** LATCH or OVERDUB; transport running; ownership (§4.4). (P1 also
had a looper gate, removed in P3.)

**Capture and placement** (P1B SQ-3, P1C SC-1, C-2, shuffle removal):

1. `seq_captureTrackTime()` at the moment the hit sounds; the sub-tick
   fraction `liveRec_frac8()` = (nowUs − stampUs) / periodUs.
2. The delta from the capture: un-quantised = the sub-tick fraction;
   1/8..1/64 = `seq_quantiseTick()` minus the capture; `trk` = snap in step
   5 below.
3. `pos` = the **live** cursor, accumulator and play state from the
   snapshot (return 0 before the start). P1 had mapped ticks through the
   canonical master-clock position (`tick·256/current interval`), which is
   wrong as soon as the scale or length changed during playback.
4. `rnd`: a forward delta is clamped to 0 (backward only).
5. `steps = seq_shiftPositionQ8(track, &pos, deltaQ8)` (signed Q8 shift
   with the current effective scale and length, returns the advance count;
   `seq_loopShiftPosition()` is its whole-tick wrapper). P3: a shift across
   a loop wrap goes through `seq_placeLoopImage()` instead (§8.4).
6. `trk` snap: if `2·off >= interval` advance to the next step (snapQ8 =
   `interval − off`), else `off = 0` (snapQ8 = `−off`).
7. `delay = (off·100 + interval/2) / interval`, measured from the step's
   **nominal** start. A delay that rounds to 100 goes to the next step at
   delay 0 (the run mode's next, not +1). Shuffle is never used: playback
   adds the step's shuffle as for any step (user decision; the P1B rule
   that measured odd steps from the shuffled start lost grid hits on odd
   steps and mis-snapped `trk`).
8. `rel` = sign of the step advance (behind / current / ahead), or by time
   when the shift crossed a wrap (P3). `ahead` = whole ticks from the
   capture to the landed step start when `rel > 0`, with
   `aheadQ8 = deltaQ8 + snapQ8 − off` (P1C C-2).

**Commit** (`liveRec_commitHit()`): writes through `liveRec_writeStep()` →
`patSvc_recordStep()`; a landed step **ahead** of the playhead gets an echo
mark (`seq_suppressStepOnce(…, ahead)`); a write onto the **current** step
gets `seq_cancelPendingTrigger(track, step)`; steps behind get nothing
(P1B LR-6; P1 marked every non-current step, silencing new hits on their
next pass). On acceptance the recorded-step LED is marked.

**Written fields** (`pat_recordStep()`, `pat_record_spec_t` = {note,
velocity, timing, roll, fill, setFil, noteIsDefault}):

| Field | Value |
|---|---|
| bit 15 | on (trigger ON in the same PRIMASK publish) |
| note | played note; `def` → no note special (Q12, `bh_overlayNoteDefault`) |
| velocity | played velocity; 100 is stored as "absent" (P1B PD-1) |
| timing | delay or cleared |
| roll / fill | roll rules below, else cleared; fill 100 stored as absent; rhythm cleared |
| condition | kept; `fil` (21) when fill is active (R10) |
| automation | kept |

Cheap path: no specials and no automation → only bit 15
(`pat_setStepActive()`). Last write wins within a pass; a grace never
replaces its own main (LiveRecord rule).

**Flam** (grace velocity 64, main velocity 100, 12 ticks apart). The grace
is re-placed from the **main's** capture (P1C LC-10/LC-11): quantised, from
`deltaQ8 + snapQ8 − 12·256` (1/32 before the snapped main); un-quantised,
from the stored grace tick and delta. Quantised: if grace and main map to
different steps, write both; otherwise the main only. Un-quantised: each
hit at its own time; if both land in one step, keep the main.

**Roll** (overlay `r/8 r16 r32 r64 r6t 128` = step indices 1 3 5 7 8 9):

- **Slow rolls** (interval ≥ the track's live step interval, e.g. `r/8` on
  a 1/16 track) are recorded hit by hit (`ROLL_HIT` captures with their own
  placement, quantise and `fil`). Flams are always two individual steps.
- **Fast rolls:** the start is placed like a single hit (`liveRec_rollStep`,
  `liveRec_rollStartDelay`, `liveRec_rollSpan = (rel <= 0)`), the rolled
  track (`liveRec_rollTrack`, which may differ from `rtk`, P1B LR-3) is
  live-held, and `liveRec_syncRing()` runs first so the view of the
  playhead is current (P1C C-1).
- **Span steps:** while held, each ring entry of the rolled track writes its
  previous step as trigger + rate + fill 100 + `fil` — only after the start
  step, never the start step itself and never a step before a
  forward-quantised start (P1C LC-8, item 15).
- **End** (`liveRec_endRoll(writeEnd)`, P1C LC-9): `liveRec_syncRing()`
  right after the capture so every step that ended before the release gets
  its span write; then:
  - released before the second hit (`rollHits == 0`): the start step is
    rewritten as a plain trigger at its start delay (roll 0, fill 0);
  - released on the start step: the start keeps its delay, fill = release −
    start (1..100);
  - released on a later step: that step at timing 0, fill = release delay
    (1..99).
  - `writeEnd = 0` (position break, called from the service) writes nothing.
  - Always: live hold off.
- **Rate switches** (`liveRec_rollRateChange()`, P1B LR-14): index 0 ends
  the roll; fast → slow ends it (later hits record singly); slow → fast or
  single → roll promotes the **next** hit to a `ROLL_START`
  (`liveRec_rollPendingIdx`); fast → fast takes the new rate from the step
  containing the switch (F-C). The slow/fast test uses the live interval.
- Ended early by a Scene change, stop, REC off or `rtk` change.
- Risk R-9: recorded rolls end with the fill duck; live overlay rolls do
  not.

**Why `liveRec_syncRing()`** (P1C review C-1): TIM3 pushes ring entries at
step starts, but the drain runs only in the 500 Hz pass, so LiveRecord's
`prevStep` can lag the playhead by up to 2 ms. Roll starts in the first
2 ms of a step overwrote the previous step with a span; releases in the
first 2 ms of a step left a gap; releases in the first 2 ms of a
forward-quantised start recorded a plain trigger. Draining right after the
capture makes the view current; entries pushed later belong to later
steps. `liveRec_syncRing()` = `liveRec_syncPosition()` (position epoch) +
`liveRec_drainRing()`, skipping the drain while `patSvc_replacePending()`.

**MIDI:** `midiParser_noteOn()` builds a `liveRec_trigger_t` (SINGLE, MIDI)
and calls `liveRec_triggerIn()`; MIDI timestamps are processing time until
the MIDI rework.

### 4.7 Automation recording

**Recordable sources** (resolved by `menu_cellClearTarget(page, position)`,
the pot-clear resolver split out of `menu_knobClearTarget()` in P1B MN-1):
VOICE-page Instrument cells; `Nvm`, `Nou`, `Nfx`, `7dc`; EFFECT-page
AUTOMATABLE rows and the Effect page's Morph cell (labelled `mrp` there but
it is the FX Morph `fxm`); SEQ-page `scl`, `shf`; PERF Morph cells
`1VM`..`6VM` and `fxm` (P3 FQ5). Later: external CC.

**Not recorded:** RECORDING, global, MIDI and Load/Save pages; step editors
and held-step overlays (`va_writeAutomationFromKnob()`, `sa_*`); FX lane
locks; LFO/velocity target cells, `typ`; Morph-view edits
(`voiceModeShowMorph`, `menu_patternTrackMorphViewActive()`, the Effect
Morph view); `len`; non-automatable targets; `scl`/`shf` while the viewed
Pattern is not A (with FOLLOW off a SEQ click changes the view without
changing playback); and the master morph `mrp` (`PAR_MORPH` resolves to
no target).

**Hook** `menu_liveRecRecordCell(subPage, position)`, called from
`menu_parseKnobDelta()` and `menu_encoderChangeParameter()` (encoder edits
recorded since P1B MN-2) **after** the commit, whether or not the value
changed (a detent clamped at min/max counts as adjusted). The page
exclusion tests the top-level `menu_activePage` (P1B passed the sub-page
index by mistake; P1C MC-1 renamed the parameter `subPage` so it cannot
recur).

**State:** three bitmaps over the 9-bit target ID space (64 B each, indexed
directly; 480 usable IDs would pack into 60 B):

| Bitmap | Set by | Cleared by | Meaning |
|---|---|---|---|
| `latch` | first adjustment in LATCH | new take (REC off, overdub entry/exit, track or `rtk` change, any Scene change) | the parameter owns its automation on `rtk` for this take |
| `dirty` | every adjustment (LATCH and OVERDUB) | the write (switch 0) / the step end (switch 1) | adjusted since its last write; switch 1: "this step has its value" |
| `erase` | knob turn during live erase | erase end | automation of this target is being erased from `rtk` |

A `dirty` bit is needed beside `latch`: on a step that is not active the
rule is "write if adjusted since the last write, else clear an existing
entry", which `latch` alone cannot tell apart.

**Own vs foreign:** an own Instrument parameter belongs to the slot that
`rtk` plays (tracks 6 and 7 both map to slot 6); everything else is
foreign. **Fired step** (F-D, F-L): a step of `rtk` whose first trigger
fired this pass (on-time, delayed, roll first hit, or carried-roll
renewal); every step a multi-step roll spans counts; an echo-suppressed
step recorded this pass counts; a muted `rtk` counts steps that would have
fired; steps whose condition fails do not.

**LATCH** (REC toggled on). A latched parameter's menu value always sounds
(the drain skips every track's automation of it). At the end of each step N
of `rtk`, for every set bit in `latch`:

| End of step N | Own Instrument parameter | Foreign (other slot, Effect, Scene) |
|---|---|---|
| dirty | write menu value, clear dirty (switch 0) / clear dirty only (switch 1) | same |
| not dirty, N **fired** | write menu value (re-stamp) | clear an existing entry |
| not dirty, N not fired, entry exists | clear | clear |
| not dirty, N begun by a looper jump (P3) | nothing | nothing |
| otherwise | nothing | nothing |

New take: a pending dirty write for the current step is flushed first. The
menu keeps its value; afterwards R12 applies as after any menu edit.

**OVERDUB** (REC held). Every dirty target is written to step N (switch 0)
or at its first change (switch 1); nothing latches, nothing is cleared,
nothing is re-stamped. Live sound follows R12 (an Instrument parameter's
automation is suppressed until the voice's next trigger; others sound at
the menu value until their next automation).

**Write volume (L3):** one foreground `pat_readStepAutomations()` read of
the ended step answers all latched targets; a write is queued only when the
step has no entry for the target or a different value, a clear only when an
entry exists. Without this LATCH would rewrite every fired step's re-stamps
on every pass. Writes go through `patSvc_writeStepAutomation()`, clears
through `patSvc_removeStepAutomation()`; `fil` (R10) on any written step via
`patSvc_setStepCondition(…, 21)`. Risk R-2: many latches on a fast `rtk`
can exceed the busy drain (one event per 2 ms, 64-entry queue); drops are
counted; a composite op is built only if measured (Q6). Risk R-3: re-stamps
add an entry per fired step per latched parameter.

**Value source** (`liveRec_targetValue7()`, schedule review A-6): every
value comes from the take's origin Scene A, which the VOICE, EFFECT and
PERF pages display and edit — never from T. Under per-track playback the
edit writes A's image and the active-Scene runtime write puts A's value on
the sounding slot, so A's menu value is what the user hears.

| Target | Value read (Scene A) |
|---|---|
| Instrument | `instrument_parameters[local]` (Normal endpoint, not `morph_interpolation[]`) |
| `Nvm` | Morph amount store-7 (127 ↔ 255); since G-1 the captured individual edit (`liveRec_morphValue[]`) |
| `Nou`, `Nfx` | `scene_getVoiceAudioOut()`, `scene_getVoiceFxSendAmount()` |
| `7dc` | `scene_getSlot6Track7AmpEnvelopeDecay()` |
| `scl`, `shf` | Pattern region Normal (`track_scale`, `track_shuffle`) |
| `fxm` | `scene_getEffectMorphAmount()` store-7 (`m/2` is the exact inverse of `effect_expand7Linear()`, `v < 127 ? v·2 : 255`); since G-1 the captured edit |
| Effect | retained Normal, WIDE8 rows through `effects_storeValue7()` (inverse of the row's `expand7`, P1B FX-1) |

Risk R-6 (accepted, F-G): with voice Morph ≠ 0 the raw playback differs from
the interpolated sound heard during the take. Risk R-7: with `rtk` on slot
6, a trigger from the other slot-6 track, or a live trigger, ends the
voice's automation hold with no stamped step behind it.

**MIDI CC:** `midiParser_enterTaggedParameter()` calls `liveRec_paramIn()`
with the invalid target `0xFFFF` (stub; the CC → target mapping belongs to
the MIDI rework).

### 4.8 Live erase (R14)

**Entry and exit.** SHIFT+CLEAR (the COPY/CLEAR button) in either order
while LATCH or OVERDUB (`buttonHandler_liveEraseBegin()`, the single entry;
`copyClear_copyPressed()` is not called). Overdub ends; latches reset. SHIFT
and CLEAR LEDs stay lit while either is held; erase stays enabled until
**both** are released (`buttonHandler_liveEraseEnd()`, the single exit:
restores the REC LED and restarts the automation-underline search). The
SHIFT release runs the erase-end test before its consume and latched-menu
breaks (P1C BC-2). Outside record, SHIFT+CLEAR keeps its S075 clear
meaning. While erase is enabled: PLAY is a plain transport toggle
(`buttonHandler_setRunStopState()` + `seq_setRunning()`, bypassing
`bh_chainPlayArmed`), SEQ never opens the chain menu. While stopped nothing
passes; starting playback starts with erase enabled. A MODE change keeps
erase enabled but drops held SEQ/TRACK gestures
(`liveRec_eraseReleaseHeld()`, and the per-button gesture masks
`bh_eraseSeqMask`, `bh_eraseFxSeqMask`, `bh_eraseVoiceMask` are cleared);
Load/Save and global settings do nothing until left.

**Gestures:**

| Gesture | Duration | Effect as the playhead passes |
|---|---|---|
| Knob over an automatable target (selection, not adjustment; works in the Morph view and on PERF Morph cells) | until erase ends | the parameter is not adjusted; its target joins `erase`; the drain skips it on `rtk`; each passed step of `rtk` has its entry removed |
| SEQ hold (VOICE, STEP) | while held | on the **active track** (passed from buttonHandler, P1B LR-8): triggers and specials removed, not played; the condition is kept on steps with automation; automation still plays |
| SEQ hold (FX) | while held, counted per button | every lock (all lanes, incl. Morph) of each FX step the FX playhead reaches is removed before it applies; linked Scenes follow |
| TRACK hold | while held | that track: everything removed (trigger, specials, condition, automation), not played |

| Mode | Knob | SEQ hold | TRACK hold |
|---|---|---|---|
| VOICE | Instrument cells, `Nvm` `Nou` `Nfx` `7dc` | active track | whole track |
| STEP | `len`, `scl`, `shf` (`len` erasable though not recordable) | active track | whole track |
| PERF | `1VM`..`6VM`; `fxm` (track automation + FX Morph lane); `mrp` nothing | consumed (SEQ = Scenes) | whole track; **not while looping** (P3 Q4) |
| FX | Effect parameters and the Effect Morph: track automation on `rtk` **and** that FX lane's locks (F-J) | FX sequence locks | whole track |
| SOM_GEN | nothing (F-K) | consumed | consumed |
| Load/Save, MENU | nothing | consumed | consumed |

Presses are consumed (no step toggle, voice select, mute, overlay trigger,
Scene change or FX hook); the release router ends exactly the gesture its
press started, by bit, even after erase ended (P1C BC-5). Releasing a SEQ
or TRACK button stops its erase; another press restarts it while SHIFT or
CLEAR is still held.

**Exclusivity with overdub:** erase enabled, then REC → nothing (LEDs stay);
REC held (overdub), then SHIFT, then CLEAR → overdub disabled, erase
enabled; releasing REC later restores the prior LATCH/OFF state without a
toggle.

**Knob erase in the menu:** its own branch before the copy/clear pot branch
(`menu_knobClearTarget()` → `liveRec_eraseParam(target, fx_lane)`, no
value change). It no longer drops the Pattern-wide underline
(`menu_automationTargetCleared()` is for whole-Scene removals only); the
search restarts at erase end (P1B MN-5).

**FX-sequence erase and linked Scenes (F-I):**

- LiveRecord pushes `effects_setLiveEraseLanes(laneMask)` (16 bits: the
  knob lanes `liveRec_eraseKnobFxLanes` OR all lanes while the FX SEQ hold
  count `liveRec_eraseFxSeqCount` is nonzero; P1C LC-1/LC-2; P1B had cut
  the mask to 8 bits and shared one variable between knob and SEQ).
- At each FX step start EffectsManager excludes those lanes' locks from
  resolution for that step (never sound, not even for one block) and records
  them in `effects_erasedLanes[16]` (per-step lane masks, P1B FX-2).
- `liveRec_service()` drains one step at a time
  (`effects_takeErasedLocks(&step, &lanes)`) and calls
  `effects_clearSeqLanes(origin, 1 << step, lanes)`, which clears the
  retained locks in the origin Scene and every linked same-type Scene
  (`effects_fanoutMask()`, which uses the inactive Scene's own edit-mask
  entry, so a flush after a Scene change still fans out). The service also
  flushes when erase or recording ends. Risk R-6b: every Scene-change entry
  point must reach the flush.
- Pattern data (recorded triggers, automation writes/clears, Pattern erase)
  never fans out.
- `liveRec_eraseReleaseHeld()` clears only the hold gestures (keeps the knob
  lanes and the knob bitmap); `liveRec_eraseEnd()` resets every erase
  source through it.

### 4.9 Service pass order (`liveRec_service()`, final)

1. Scene epoch changed → new take.
2. Playback epoch changed → `liveRec_syncPosition()`: clear the previous
   ring steps and flags, `liveRec_endRoll(0)`, drop the pending grace.
3. Replacement pending → release the target, writing 0, filters off,
   return.
4. Target: T = `seq_getTrackPlayedScene(rtk)`; request/release the
   override; `seq_setRecordTarget(rtk, patternWriting)`; filters while
   armed; `liveRec_pushEraseMasks()` every pass while ERASE (ownership can
   change with the Scene).
5. Ring drain (`liveRec_drainRing()`): each entry ends the track's previous
   step (span write for the rolled track; `liveRec_stepEnd()` for `rtk`
   with the ended step's own flags; switch 1 writes waiting dirty values
   into the first step after a start/break) and stores its own flags.
6. FX-erase drain, one step at a time.

### 4.10 DEV-only drop counting

`liveRec_drop(cause, track, step)`: `liveRec_drops[7]` (`u16`, 14 B) and a
PatternTrace stage `'W'` event (`PAT_TRACE_STAGE_LIVEREC_DROP`; cause in
flags, `track << 8 | step` in value), compiled out without
`DEV_MODE_LOGGING`. Causes: 1 handover (target not open), 2 ownership,
3/4 refused write (the code uses 4 for every refused record/automation
write), 5 ring full (counted in the Sequencer push, `seq_recRingDrops`).
`tools/decode_devlogs.py` does not decode stage `'W'` yet (open).

### 4.11 Pattern Stack Service and PatternData changes

- `pat_writeDynamic()` gained a `trigger_policy` (KEEP / ON / OFF, the
  `PAT_RAW_TRIGGER_*` values) applied inside both PRIMASK publishes
  (attached and detach paths); every older caller passes KEEP.
- `pat_recordStep(scene, track, step, spec)` (returns success): reads the
  existing specials and automation, applies the spec, writes with trigger
  ON; cheap path sets bit 15 only.
- `pat_eraseSpecials(scene, track, step)`: with automation, keeps the
  condition and drops the other specials; otherwise releases the block
  (trigger KEEP; TIM3 already cleared it).
- `PATSVC_OP_RECORD_STEP` (payload = a staging-slot index into a 4-slot
  service-owned table; `patSvc_recordStep()` copies the spec in, a full
  table rejects and counts, execution or a count-drop frees the slot) and
  `PATSVC_OP_ERASE_SPECIALS` (TIM3-safe `patSvc_enqueueEraseSpecials()`,
  modelled on `patSvc_enqueueErase()`); `patSvc_requiredChunks()` sizes
  record ≤ current block + new specials, erase-specials ≤ current block.
- **Target override:** `service_target_request` (`0xFF` = none) and
  `patSvc_desiredScene()` = the request, else `seq_activePattern`; it
  replaced the eight `seq_activePattern` comparisons (submit, the three
  submit variants, `patSvc_tick()` ×2, `patSvc_idle()`,
  `patSvc_finishSceneReplace()`, `patSvc_endExclusive()`).
  `patSvc_requestTarget()` returns 0 when refused (exclusive claim,
  filesystem replacement, non-resident Scene, busy queue);
  `patSvc_releaseTarget()`, `patSvc_targetOpen(scene)`,
  `patSvc_replacePending()`. `patSvc_idle()` is false while an override is
  held, so Pattern AutoSave never snapshots the overridden Scene mid-take.
  Invariants: publish-before-free, one target at a time, handover drains
  first. Risk R-1: each move of T drains the queue and rebuilds slack;
  actions in that window are dropped.

### 4.12 Gates elsewhere

- `copyClear_copyPressed()`: refuses while `liveRec_armed()` (F-N).
- `filesystem.c` Pattern AutoSave gates: `liveRec_patternWriting()` (F-M).
- `quantisation` 5 loads without a parser change.

### 4.13 Deferred out of S081

- Automation `hold` reconciliation (SCOPING §4.3a): R12/R13 likely settle
  it; not confirmed.
- Step-record while stopped; recording FX-sequence locks.
- MIDI rework: accurate note timestamps, queue-skip priority for realtime
  MIDI, whether CC adopts R12 menu priority (today: Normal endpoint, lowest
  priority), the CC → target mapping, the dead helpers
  `midiParser_playVoiceMidiNote()` / `midiParser_triggerVoice()`.
- Risk R-6a: the side-effect-free condition path must stay in step with
  `seq_evaluateStepCondition()` when conditions are added.

---

## 5. Part P1 — implementation, reviews and fix rounds

### 5.1 Schedule review corrections (plan Appendix A, rev 9–11)

The first schedule was reviewed against the plan and the code at
`02553df` before any code was written:

| # | Correction |
|---|---|
| A-1 | Automation-only steps would have stopped queueing automation: the call stays in the step-allowed block and is skipped only when this step's own trigger was deferred. |
| A-2 | The R12 override clear moved before the restore's early return. |
| A-3 | Complete `seq_triggerVoice()` caller table (SOM, overlay timer `count != 0xFF`). |
| A-4 | One `seq_fireDeferred()` for expiry and flush (a flush had lost automation and rolls). |
| A-5 | Realign realigns the FX sequence instead of resetting it (plan error). |
| A-6 | Values from the origin Scene A, Normal endpoint. |
| A-7 | Phase order fixed so every phase builds (MIDI call site in B, erase work all in G-5). |
| A-8 | New C-9/C-10 for the TIM3 changes and echo expiry. |
| A-9 | Step-end logic placed in F-5, E-4, G-6. |
| A-10 | Q13 refusal also on STEP toggles. |
| A-11 | `patSvc_replacePending()` instead of `menu_isStorageBusy()` (plan error). |
| A-12 | Unscheduled items placed (recorded-step LED, `rtk` cell, service cases, FX lock exclusion, live-hold ownership, slow-roll rule, drop counters). |
| A-13 | Reset ordering and atomic mask. |
| A-14 | Override per slot, not per Scene. |
| A-15 | `trk` loop anchors (L1). |
| A-16 | Overlay names corrected (`LED_BAR1/2`, `led_setValue()`, new `bh_overlayHeldType`). |
| A-17 | REC timer `u16` wrap-safe; REC LED flash in the menu; `seqRecording` uses listed. |
| A-18 | Return values (`pat_recordStep()`, `patSvc_requestTarget()`). |
| A-19 | `LIVEREC_HIT_ROLL_HIT`; flam main reported where it fires. |
| A-20 | Erase routing for Load/Save, MENU, SOM_GEN. |
| A-21 | Track length override clear (plan error). |
| A-22 | L-2 moved to P3. |
| M-1..M-9 | line refs, comments, RAM table (no double count; drop counters DEV-only), dependency order, ring semantics (TIM3 pushes at step start; the next entry ends the previous step). |

### 5.2 P1 implementation log (`d21e245`)

Phases, each gated by a clean `make all`:

| Phase | Content | Flash headroom after |
|---|---|---|
| A1 | pending-reset mask, `reset` argument, deferral state + `seq_fireDeferred()`, F-R, `seq_menuOverride`, per-target clears, R12 menu call sites | 195,476 B |
| A2 | owner/marker removal, `menuHold`, `effects_lockClearLayers()`, resolution order, FX realign | 195,580 B (bss −72 B) |
| A3 | roll table, names, `QUANT_TRACK`, per-track `trk` anchors, overlay fixes, legacy roll/`PAR_ROLL` retirement | 195,188 B |
| B | LiveRecord skeleton, REC tap/hold, `rtk` cell, gate moves, removals, LED cleanup, init/service wiring | 194,780 B (bss +192) |
| C | ring, target, echo/live hold, erase masks, µs stamp, epoch, filter, capture/projection/quantiser, TIM3 changes, echo expiry | 193,844 B |
| D | trigger policy, `pat_recordStep()`, `pat_eraseSpecials()`, service ops, target override | 192,916 B |
| E | `liveRec_triggerIn()`, overlay reports, fast-roll spans | 190,532 B |
| F | menu record hook, value resolver, CC stub, step-end tables (L3) | — |
| G | FX erase, erase chord and router, knob erase, TIM3 erase branch, old erase removed | — |
| H | target negotiation, Q13 refusal | 188,236 B |
| I | documentation: deferred to this close-out | — |

P1 build: text 565,016, data 412, bss 428,316 (+340 B since S080).
Implementer deviations: the A3-4b roll notifications landed in E-2; the C-10
play-mode echo expiry was not wired (later replaced by deadlines); F-1b
(encoder) was not hooked; H-3 drop counters not added; micro-timing,
sub-tick and roll release fill were simplified (all fixed in P1B).

### 5.3 P1 code review (2026-10-13)

Verdict: phases A1–A3 sound; the recording paths had defects.

**Must-fix (all fixed in P1B):**

| # | Problem |
|---|---|
| 1 | `liveRec_activePatternLocked()` was true whenever P(rtk) ≠ A, even with REC off: per-track playback silently locked STEP editing. |
| 2 | The drain's erase filter was given the dirty bitmap. |
| 3 | Roll spans, live-hold release and the take-boundary clear used `rtk`, not the rolled track; a non-`rtk` roll stayed live-held. |
| 4 | Muted `rtk` fired bit ignored bit 15: every step counted as fired. |
| 5 | Placement used the canonical master-clock position, not the live state; shuffle ignored; "delay 100 → +1" wrong for rev/pip. |
| 6 | Echo marks set on steps behind the playhead and never expired at the pass wrap. |
| 7 | `seq_cancelPendingTrigger()` dropped the deferred step's automation and could cancel an earlier step's deferral. |
| 8 | The erase router ignored the per-mode table. |
| 9 | SEQ-hold erase used `rtk`, not the active track. |
| 10 | Transport start/stop was a take boundary: held erase dropped, stale local masks. |
| 11 | Ownership missing in the automation path; no slot-type check for triggers. |
| 12 | SHIFT/CLEAR hold flags set/cleared after early breaks; LEDs wrong. |
| 13 | `liveRec_forceOff()` ran even when the engage did nothing (stopped). |

**Should-fix (14–28, fixed in P1B):** 14 encoder edits not recorded; 15
`def` note and absent velocity/fill; 16 roll release fill, quick release,
single → roll, fast ↔ slow, span `fil`; 17 µs sub-tick unused; 18 WIDE8
Effect values (the review's `fxm` rounding claim was wrong: `m/2` is the
exact inverse); 19 `scl`/`shf` only while viewed == A, Effect Morph view;
20 `rtk` edit seed; 21 erase masks ungated by ownership; 22 override held
while stopped (blocked AutoSave); 23 early deferral flush at every step;
24 TIM3-shared state not `volatile`; 25 play-mode echo expiry; 26 DEV drop
counters; 27 knob erase dropped the underline; 28 one accumulated FX erase
mask. **29** (FX realign on non-resetting Scene changes) was decided at the close-out: correct as implemented (§3.3).

### 5.4 P1B (fix round 1)

Phases F1 Sequencer (SQ-1..SQ-12), F2 PatternData/trace/Effects (PD-1,
PT-1, FX-1..FX-3), F3 LiveRecord (LR-1..LR-20), F4 buttonHandler
(BH-1..BH-9), F5 Menu (MN-1..MN-5). Flash headroom 186,172 B at the end
(rebuild 185,996 B). New: `seq_shiftPositionQ8()`, `seq_placeFromSnapshot()`
(replacing `seq_projectStep()`), `seq_echoDeadline[7]`,
`seq_trackShuffleMuteMask`, `seq_sceneEpoch`, `effects_storeValue7()`,
`effects_erasedLanes[16]`, `liveRec_targetOwned()`, `liveRec_endRoll()`,
`liveRec_drop()`, `liveRec_eraseReleaseHeld()`, `bh_overlayNoteDefault`,
`menu_cellClearTarget()`, stage `'W'`. RAM ≈ +61 B every build (planned
≈ +51; the linker kept the grace state, `liveRec_rollNoteDefault`,
`liveRec_sceneEpochValid`) + 14 B DEV.

**P1B review must-fix (fixed in P1C):** 1 FX erase lanes cut to 8 bits by a
`(uint8_t)` cast; 2 knob-erase FX lanes wiped by an FX SEQ release or a
MODE press; 3 page exclusion tested the sub-page index (PERF `Nvm`
recorded; sub-pages 7–10/14 silently not); 4 roll end written at the
release step/delay instead of the start step, and spanned end steps got
the release delay as timing; 5 live erase could stay on with both keys
released when REC opened the SHIFT+REC menu during erase. **Minor:** 6
`trk` flam grace from the unsnapped time; 7 start-step fill measured from
the step start; 8 un-quantised grace committed with a stale placement; 9
dead grace state; 10 erase SEQ count across a mode change and the
uncounted FX SEQ hold; 11 erase masks published as two writes; 12
Scene-epoch bump site (accepted); 13 ring-full drops not counted; 14 header
comment.

### 5.5 P1C (fix round 2)

Phases C1 Sequencer (SC-1 `seq_place_t.snapQ8`, SC-2 one PRIMASK section
for both erase masks, SC-3 DEV ring-drop counter, SC-4 comment), C2
LiveRecord (LC-1..LC-12), C3 buttonHandler (BC-1..BC-6: REC does nothing
during erase, SHIFT release ends erase first, `buttonHandler_liveEraseBegin()`,
per-button gesture masks, MODE clears them), C4 Menu (MC-1). Two more
defects found while writing the schedule: **15** the span writer overwrote
the roll's start step and could write a roll before a forward-quantised
start; **16** the SEQ-erase hold count survived erase end. Flash headroom
185,388 B. RAM +10 B every build + 2 B DEV.

**P1C review:** C-1 must-fix (the 2 ms ring lag, §4.6) → fixed with
`liveRec_syncRing()`; C-2 `ahead` ignored the snap → fixed; C-3 comment
placement/wording → fixed; C-4 mask declaration site → accepted. Flash
headroom after the fixes 185,084 B.

**Shuffle removed from placement** (user decision, §4.6 step 7): flash
headroom 185,196 B.

### 5.6 Hardware fix 1 — recorded automation gets its underline

Symptom on hardware: a recorded parameter automation was not underlined.
Cause: the underline comes from Menu's bounded presence search
(`va_scanService()`), whose completed result is never re-validated; plan §12
required recorded targets to update the search bits, but no step did it.
Change: the scan's per-entry classification moved unchanged into
`va_searchRecordTarget(target)`; new `menu_automationTargetRecorded(scene,
track, target)` sets the same bit the scan would set when the write is on
the shown Pattern (and the searched track on VOICE/STEP pages) and repaints
if a completed result changed, without restarting the search (other
underlines do not blink during a take). `liveRec_stepEnd()` calls it for
both accepted writes (dirty and re-stamp), and from P2 D12
`liveRec_writeCurrentValue()` too; LiveRecord now includes `menu.h`. Not
changed: erase still restarts the search at erase end; a take that removes
the last entry of a target keeps its underline until the next restart.
Flash headroom 185,060 B, no RAM.

---

## 6. Part P2 — linear automation ramps (`asm`)

### 6.1 Specification (user)

- **Automation type** is a per-track **Pattern** setting saved with the
  Scene's Pattern, neither automatable nor morphable. STEP mode, track
  parameters, after `lop`: short `asm`, long `Automtn`, values `off`
  (default) / `on`.
- `off`: every step's value is applied as a jump, as before.
- `on`: a parameter ramps linearly from the current step's value to the
  next played step's value, only if both steps hold an automation value for
  that parameter. Menu → automated step and automated step → menu still
  jump.
- Scope: Instrument parameters of that track. Cap: 64 simultaneous ramps; a
  pair without a free slot jumps.

### 6.2 Decisions

| # | Decision |
|---|---|
| D1 | B's condition must be predictable at A's apply time, otherwise B jumps. Predictable: none, the repeat counters `1:2`..`4:4` and `1:-` (with the repeat count B will see), `lst`/`!ls` (from A's result). Not predictable: `rNN`, `fil`/`!fl`, `lop`/`!lp`, mute conditions. |
| D2 | Instrument descriptor parameters only, automatable, numeric dtype: `DTYPE_0B127`, `DTYPE_PM63` (and `0B255`, `0B15`, `1B16`, `1B128` should a descriptor use them). Never: `DTYPE_MENU`, `DTYPE_MIX_FM`, `DTYPE_ON_OFF`, `DTYPE_0b1`, `DTYPE_LFO_POLARITY`, LFO/velocity targets, all Scene targets, Effect parameters. |
| D3 | A resetting trigger during A (overlay, MIDI, flam main, roll renewal) does not restore a parameter an active ramp holds. |
| D4 | `asm` / `Automtn` / `off`/`on`. |
| D5 | Pattern setting in the PAT4 track header; a card script; AutoSaves deleted by the user. |
| D6 | Every active ramp written each ~2 ms pass, measured; 16-per-pass round-robin only if the measurement requires it. |
| D7 | fwd, rev, pip, onc, 1fr ramp; rnd always jumps. |
| D8 | Looper: ramps follow the unlooped segment; no ramp across the wrap back to the loop start; the last loop step ramps toward the step after the loop end when that step holds the parameter. |
| D9 | 7-bit ramp resolution accepted. |
| D10 | Cap of 64 ramps. |
| F1 | PAT4 version stays 2 (byte 5 was reserved and zero in every card file). |
| F2 | Legacy text-format `SD_CARD/Bank/*/*/pattern.pat` (229 files) left alone. |
| F3 | RAM approved (565 B + 4 B DEV planned). |
| D11 | (2026-10-14) Write a ramp's value only when the rounded value changes; slot grows to 10 B: **+128 B approved**. |
| D12 | (2026-10-14) Live record writes a parameter's **first change** in a step (§7). |

### 6.3 Storage and menu

- `pat_scene_region_t.track_auto_smooth` (bit N = track N `on`), beside
  `track_loop_exclude`; region static assert +1 B. Zero-filled = `off`.
- PAT4 file: byte 5 of each track's 16-byte track header (0/1), written by
  the PAT4 writer and read by the three readers (Scene/Bank Pattern, root
  Pattern Load, AutoSave boot), exactly like byte 4 (loopable).
- `pat_setTrackAutoSmooth()`, `pat_trackAutoSmooth()`; init clears; region
  copy copies; `pat_applyTrackSettingsToMenu()` sets the mirror.
- Copy/clear: track-settings paste copies it; track clear resets it.
- Not a SceneModTarget: never in automation lists, the live-record
  resolver or pot clear.
- Menu: SEQ_PAGE sub-page 0 cell 7 (was `TEXT_EMPTY`; all eight cells are
  reachable, the "traversal terminator" comment was wrong), new mirror
  `PAR_TRACK_AUTO_SMOOTH` after the Globals (consumes an existing
  `parameter_values[]` slot, `NUM_PARAMS` stays 384, static assert), plain
  `DTYPE_ON_OFF`, texts `asm`/`Automtn`, name row `{SHORT_TRACK_AUTO_SMOOTH,
  CAT_TRACK, LONG_TRACK_AUTO_SMOOTH}`; no-op in `menu_parseGlobalParam()`,
  commits in `menu_cellCommitValue()` for the viewed Pattern and active
  track.
- `pat_findStepAutomation(scene, track, step, target, *value)`: a
  single-target lookup that copies nothing (used by ramp pairing and the
  D12 write).
- **`tools/convert_pattern_autosmooth.py`** (modelled on
  `convert_scene_scale.py`): for each PAT4 file (magic `PAT4`, 16-bit LE
  version 2 at offsets 4..5, 160-byte header) validates the header and the
  CRC32C, sets byte 5 of the 7 track headers to 0, recomputes the CRC32C
  with the field zeroed and writes it at offset 14; non-PAT4 `.pat` files
  are skipped and listed; `--dry-run`; nonzero exit on any failure. Run on
  `SD_CARD/` (dry run): 194 `ok`, 229 `skip … not PAT4`. Self-test on a
  scratch copy: set bytes reported and restored byte-identical; an appended
  byte fails with a size error.

### 6.4 Engine design (as built, `sequencer.c` section "S081 P2 automation ramps")

**Data:**

```c
typedef struct {            /* seq_ramps[64], 10 B each (D11) */
    uint16_t id;            /* 0 = free; 1 + ((track << 9) | target) */
    uint8_t  from, to, last;
    uint16_t startQ4, endQ4;/* 1/16 master ticks, low 16 bits, wrap-safe */
} seq_ramp_t;
typedef struct {            /* seq_rampCtx[7], 4 B, written by TIM3 */
    uint8_t  a;             /* bits 0..6 A step, bit 7 = stepped (unconsumed) */
    uint8_t  b;             /* B step + 1; 0 = no ramp target */
    uint16_t endQ4;         /* B's predicted apply time */
} seq_rampCtx_t;
SEQ_RAMP_SLOTS 64, SEQ_RAMP_STEPPED 0x80, SEQ_RAMP_BLOCK_DIV 3
```

**Flow:**

1. **TIM3, every step of an `on` track** (`seq_rampStepEvent(track,
   applied, deferred)`, called from `seq_advanceTrackStep()`): automation
   applied now → context with the stepped flag and a prediction of B;
   deferred → the prediction only (`seq_fireDeferred()` sets the flag when
   the entries queue, so ramps into the step run until its value lands);
   neither (condition failed, muted, full-erased) → "stepped, no B".
2. **Prediction** (`seq_rampPredict()`): no B when A has no automation, the
   track is muted or full-erased, there is no B (`seq_rampNextStep()`: fwd
   and rev wrap at the effective length, pip repeats its boundary step and
   turns, onc/1fr have none after the last, rnd none), B has no automation,
   or B's condition is not predictably passing
   (`seq_rampConditionPredict()`, sharing `seq_conditionRepeatPasses()`
   with playback). B's start = the next DDA advance (`ceil((interval −
   remainder)/256)` ticks); B's automation is deferred like B's trigger when
   B holds one (shuffle on odd B + micro-timing, the same arithmetic as the
   deferral). `*repeatInc` gives B's repeat conditions the count B will see.
   The looper is ignored on purpose (D8).
3. **Position breaks** (`seq_rampBreakTrack()` / `seq_rampBreakAll()`)
   write "stepped, no B" at: `seq_setTrackPlayedScene()` (both tracks 5 and
   6, which switch together), `seq_selectActivePattern()`,
   `seq_alignActivePatternToScene()`, both realigns, `seq_setRunning()`,
   `seq_setStepIndexToStart()`, the loop wrap, `seq_loopResumeTrack()`, the
   chain `stp` freeze.
4. **Consume** (`seq_rampConsumeTrack()` / `seq_rampConsumeSteps()`, all
   seven flags in one PRIMASK section, ends outside it): ends a track's
   ramps when its stepped flag is seen — at the top of the trigger restore
   (its own track), at the top of the drain (all tracks, before B's
   entries), and in the ramp service.
5. **Pairing** (`seq_rampStartIfPaired()`, from the drain's Instrument
   branch after `seq_writeInstrumentAutomation()`): track `on`, dtype OK,
   context still describes this A with a B, B holds the target
   (`pat_findStepAutomation()`), the predicted end is in the future → take
   or reuse a slot {from = A's value just written, to = B's value, start =
   now, end = B's apply time, last = from}. No free slot → jump (DEV
   `seq_rampNoSlotDrops`). **Race fix (deviation):** the drain follows the
   live producer count, so TIM3 can queue A's entries and set A's stepped
   flag after the drain's top-of-pass consume; pairing therefore consumes
   the flag inside its PRIMASK section when it still belongs to this A and
   ends the track's older ramps before starting the new one.
6. **Service** (`seq_rampService()`, `main.c` audio render loop right after
   `seq_drainPendingAutomation()`): consumes every DSP block; every third
   0.73 ms block (~2.2 ms) computes `from + (to − from)·elapsed/duration`
   (rounded), holds `to` after the end (and sets `from = to`, so a ramp held
   > 2,048 ticks cannot wrap negative), and writes only when the value
   differs from `last` (D11). Cancel (value stays): track's `asm` off, the
   target latched or erased by live record, an R12 override, or the target
   no longer valid in the played Scene.
7. **Time base** `seq_rampNowQ4()`: master ticks ×16 plus the sub-tick
   fraction from the µs stamp (64-bit multiply; 15/16 when a full period
   has passed, so a stalled external clock holds the ramp just before the
   next tick). Ramps stretch with tempo changes.
8. **Restore skip (D3):** `seq_restoreAutomatedParameters()` first consumes
   its own track's flag, then neither restores nor un-marks parameters of
   the slot held by a running ramp; ramps of the other slot-6 track keep
   running.
9. **Write path:** `seq_writeInstrumentAutomation(owner_scene, target,
   value)` (the drain's old Instrument body: descriptor in the played
   Scene, `instrumentManager_writeRuntime()`, held bit), so ramp samples are
   automation values under the binding priority rule.

**Interactions:** per-track playback reads the flag and B from
`seq_perTrackPattern[track]`; latched and erased targets never ramp; a
recorded `asm` track writes per-step values as normal; the loop wrap and
loop release end ramps.

### 6.5 Implementation (`b6c7b86`)

Applied as scheduled (PD-1..PD-7, FS-1..FS-4, CC-1, CC-2, TL-1, MN-1..MN-6,
SQ-1..SQ-12, MA-1). Deviations: the pairing race fix, held ramps exact,
both slot-6 tracks broken together, one-section consume, 64-bit `NowQ4`,
no SQ-6 prototype block, `sizeof(seq_rampCtx_t) == 4` assert, extra header
comments, `seq_conditionRepeatPasses()` placement; D11 writes-on-change.

### 6.6 Results

- Host check: a Python transcription of the `seq_advanceTrackStep()` switch
  and `seq_rampNextStep()` agree on next index and repeat increment in all
  49,536 cases (fwd/rev/pip both directions/onc/1fr, length 1..128).
- Build: flash used 568,604 → 571,804 (+3,200 B); text 571,392, data 412,
  bss 429,104; writes-on-change alone +96 B flash, +128 B bss.
- RAM by symbol: `seq_ramps` 640, `seq_rampCtx` 28, `seq_rampBlockDiv` 1,
  `seq_rampNoSlotDrops` 2 (DEV), `pat_regions` 16 × 10,528,
  `pat_background_region` 10,528 → **686 B every build + 2 B DEV**. The
  background region is 10,528 B (10,527 after S080, +1).

### 6.7 Known ramp limits (documented, not changed)

- A transport reset in TIM3 during a ramp-service pass can let that pass
  write one more sample after `seq_restoreAllAutomation()`; the value holds
  until the voice's next trigger.
- B's predicted apply time assumes B's trigger is deferred when B is
  active; if a live hold or echo mark suppresses B's trigger, B's entries
  land at its nominal start and the ramp ends there (stepped flag), a little
  early.
- With writes-on-change, an internal legacy MIDI write or a
  supplemental-parameter write (the two paths that already bypass the
  priority rule) now holds until the ramp's value next changes instead of
  being overwritten within ~2 ms.

### 6.8 P2 hardware matrix (not run)

1. `asm` after `lop`; survives save, load, AutoSave, track copy/paste and
   clear; card script `--dry-run` and real run all `ok`.
2. fwd consecutive automated steps: audible ramp; menu → automated and
   automated → no-entry steps jump.
3. rev, pip (boundary plays twice: no ramp there), onc, 1fr ramp; rnd jumps.
4. Shuffle on odd B and micro-timing on B: the ramp ends where B lands.
5. Tempo change mid-ramp: the ramp stretches.
6. B conditions: `1:2`, `lst` ramp when B plays; `r50`, `fil`, `lop`, mute
   always jump.
7. Menu knob mid-ramp: the menu wins until the next trigger; ramp
   cancelled.
8. Overlay press, MIDI note, flam main, roll renewal mid-ramp: no dip.
9. LATCH on an `asm` track: latched targets play the menu value; erased
   targets do not ramp.
10. Stop, start, Scene change, PC reset, chain `stp` mid-ramp: ramp stops,
    no drift.
11. Looper: ramps inside the loop; last loop step toward the step after the
    loop end; no ramp back at the wrap.
12. Per-track Scene playback: flag and B from the played Scene.
13. Discrete parameters always jump.
14. **CPU:** a stress Scene with 64 ramps (e.g. 7 tracks of dense
    automation on 1/32 steps) on the `cpu` widget; if too high, the
    16-per-pass round-robin and re-measure.

---

## 7. Part P2 follow-up D12 — live record writes the first change

### 7.1 Why

A step's automation plays at its start (apply time). The P1 rule wrote, at
the end of step N, the value the parameter held then, so every recorded
move played back up to one step early (with `asm`, the whole ramped curve
one step early). The first change in step N is the start of that step's
motion, so it belongs at the step's start.

Example: a knob moves 20 → 80 across steps 4 and 5 (first change in 4 =
21, value at the end of 4 = 50, first change in 5 = 51, rest 80): P1 writes
50 and 80; first-change writes 21 and 51.

### 7.2 Rule (`LIVEREC_AUTOMATION_FIRST_CHANGE` 1, the default)

- **First change in a step:** written at once into the step `rtk` is
  playing (the live step from `seq_captureTrackTime()`, after the ring is
  drained with `liveRec_syncRing()` so the previous step's end has run), as
  the current menu value just after the edit. `dirty` then means "this step
  has its value"; later changes of that target in the step are not written.
- **Step end:** a dirty target only has its bit cleared. The LATCH rules for
  untouched targets are unchanged.
- **No step known** (stopped, Pattern replacement pending, or no ring entry
  for `rtk` since a start or break): the change waits as a dirty bit; its
  current value is written into the next step that starts
  (`liveRec_writePendingFirst()` at the first ring entry after a
  start/break), which then counts as having it.
- Unchanged: one value per step and target, L3, `fil`, ownership, erase. The
  resting knob value still plays where nothing is recorded (OVERDUB and the
  cleared LATCH steps play the menu value; LATCH re-stamps own Instrument
  parameters on later fired steps).
- Switch 0 = the exact P1 code path (same image size as before the
  follow-up).

### 7.3 Changes

`config.h`: `#define LIVEREC_AUTOMATION_FIRST_CHANGE 1` with its contract
block. `LiveRecord.c`: `#include "config.h"` plus an `#error` when the switch
is undefined (an undefined name reads as 0 in `#if`); new
`liveRec_writeCurrentValue(step, target)` (L3 compare via
`pat_findStepAutomation()`, `menu_automationTargetRecorded()` on success,
drop cause 4 on refusal, `fil` while PERF is held) and
`liveRec_writePendingFirst(step)` (LATCH/OVERDUB only; bits stay set);
`liveRec_drainRing()`, `liveRec_stepEnd()` and `liveRec_paramIn()` follow
the rule. The callers commit before `liveRec_paramIn()`, so the immediate
write reads the new value. No RAM. Flash 571,804 → 572,204 (+400 B) with the
switch at 1.

### 7.4 Limits (documented, not changed)

- If TIM3 advances between the drain and the step read in
  `liveRec_paramIn()` (microseconds), the value goes to the new step and
  that step's end can clear its bit early; a later change in the step is
  then written once more.
- A change while a Pattern replacement is pending, with a step known, sets
  the bit without a write; that step's end clears it (a Pattern Load during
  recording loses it).
- Both settings (pre-existing P1): the first ring entry after arming REC
  while running ends the stale step left from an earlier take. With switch
  1 this only clears dirty bits; with 0 a dirty value can be written into
  that stale step. Open.

### 7.5 Hardware checks (not run)

1. LATCH slow sweep across steps: each step's value matches where the sweep
   was early in that step; with `asm`, the ramp follows the gesture in time.
2. Several moves in one step: only the first is written.
3. Knob moved while stopped and armed, then start: lands on the first step.
4. OVERDUB: same as 1–2; untouched targets keep their entries.
5. `fil` with PERF held.
6. Switch 0 rebuild: P1 behaviour returns.

---

## 8. Part P3 — loop while record

### 8.1 Scope and decisions

Let the PERF looper run while live record (LATCH, OVERDUB) or live erase
is on. Looper actions are not recorded; the looper never touches Pattern
data; recording follows the playhead wherever it is.

| # | Decision (user, 2026-10-14/16) |
|---|---|
| L-2 | No looper lockout during record or erase. |
| Q1 | Once a SELECT is held in PERF the loop engages (transport running) and stays engaged until **every SELECT, TRACK (VOICE) and BAR button is released**, or the MODE changes, recording or not. |
| Q2 | A hit recorded onto a replayed step sounds again on the next pass (today the loop pre-empts the trigger overlays, so this only applies to a roll end at engage and to future MIDI input). |
| Q3 | Engage, swap, division change and release are not take boundaries. |
| Q4 | While the loop is engaged, SELECT, BAR and TRACK always keep their looper functions. |
| Q5 | Recorded steps are not given `lop`. |
| Q6 | A forward snap across the wrap lands on the replayed step after it. |
| FQ1 | A VOICE press while looping with no SELECT held adds its track to the loop. |
| FQ2 | A held overlay roll ends when the loop starts; the track hold then acts as a normal looper TRACK hold. |
| FQ3 | Jump entries at mid-step wraps and resumes. |
| FQ4 | Fix the P1 fired-bit off-by-one in P3 (RAM allowed if needed; none used). |
| FQ5 | Morphs are recorded the same way from every page: PERF `1VM`..`6VM` and `fxm` record. Apart from the overlay + TRACK triggers/rolls, nothing else in PERF is recorded. |
| FQ6 | Build loop-aware placement now. |
| FQ7 | The master morph `mrp` is not recordable or automatable. |
| G-1 | (2026-10-16) The global Morph must never be recorded or trigger a record, from the menu or MIDI; the voice/FX Morph values a global change writes are never recorded through it (+7 B approved). |

### 8.2 PERF buttons while looping (as built)

| Button | Loop not engaged | Loop engaged (any record/erase state) |
|---|---|---|
| SELECT | press engages (transport running); division | division; holds the loop |
| BAR1 / BAR2 | overlay toggle | triplet / dot modifier; holds the loop |
| TRACK (VOICE) | mute on release; overlay trigger when an overlay is on; TRACK-hold erase in erase | **looper only:** with a SELECT held, swaps the looped tracks (S080 D1); without, adds its track with a fresh loop start (`seq_loopUpdateMask()`, FQ1); a `lop`-off track does not join but holds the loop; holds the loop; release of a looped track drops it. Never an overlay trigger, mute toggle or erase gesture |
| MODE leaving PERF | — | ends the loop (`buttonHandler_loopForceRelease()`, which also clears `perf_heldVoiceMask` and `perf_voiceActionOccurred`); record mode kept |
| REC, SHIFT+CLEAR | P1 rules | P1 rules (lockout removed) |

At a successful engage (`seq_loopActive` after `seq_loopEngage()`), the
engage mask is `perf_heldVoiceMask | bh_overlayVoiceMask`; every held
overlay VOICE ends its gesture as its release would
(`buttonHandler_overlayRelease()`: repeats stop, note-off,
`liveRec_rollRelease()` writes the recorded end; a flam still waiting for
its main loses it) and becomes a TRACK hold (bit moves to
`perf_heldVoiceMask`, `perf_voiceActionOccurred` set; its release drops the
track with no mute toggle and no second overlay release). With the
transport stopped the engage does nothing and the overlay continues. The
erase routers pass PERF VOICE presses and hold releases to the looper while
looping. `buttonHandler_loopCheckDisengage()` tests `perf_heldVoiceMask !=
0` (any held VOICE, looped or not). Transport stop still ends the loop.

### 8.3 Recording while looping

- Every wrap is a new pass. Recordable while looping: PERF Morph edits,
  LATCH passes over latched targets, pending first-change values, the end
  of an overlay roll held at engage, knob erase of PERF Morph targets, SEQ
  /TRACK erase gestures started before the engage (until released). Not:
  overlay triggers and rolls (they do not fire while looping), `mrp`, MIDI
  (not yet recordable). With L3 an unchanged pass queues nothing.
- Loops shorter than a step: each wrap reopens the same step; a
  continuously adjusted parameter is written once per pass; the latest
  pass's first change wins.
- Fast-roll spans and live hold cannot occur while looping (a held roll
  ends at engage; no overlay fires); both still work after the loop ends.
- Ramps never cross a wrap or a loop release.
- Takes: loop actions bump no epoch; a Scene change while looping starts a
  new take through the Scene epoch; stop/start, realign and PC reset are
  playhead breaks.
- FX-sequence erase follows the FX playhead, which is never looped.

### 8.4 Mechanisms

**FQ4 — each step ends with its own fired bit (P3-0).** The P1 drain called
`liveRec_stepEnd(track, prev, b.fired)` with the **new** entry's fired bit,
so LATCH re-stamped or cleared step N by whether step N+1 fired. Now
`liveRec_prevFlags[7]` (replacing `liveRec_prevValid[7]`, no new RAM) holds
`LIVEREC_PREV_VALID` / `FIRED` / `JUMP` of the entry that began the step,
and the step end uses them. Example (triggers on 1 and 3, `dcy` latched):
old code cleared 1 and 3 and wrote 2 and 4; fixed code writes 1 and 3 and
clears 2 and 4. Stored patterns do not change by themselves; a pattern
recorded with the old code holds latched values on the empty steps, and the
next LATCH pass over it (same parameter latched) moves them onto the
triggered steps. Any LATCH hardware test done before P3-0 showed the
shifted behaviour.

**FQ3 — jump entries.** A ring entry was pushed only by a step advance.
With `qnt off` (fastest-track anchor) or a grid, a track whose step does not
start at the anchor wraps **mid-step** with no entry, and
`seq_loopResumeTrack()` jumps without one. LiveRecord then kept treating the
pre-jump step as playing (switch 0 writes went to the old step; with switch
1 the old step's dirty bit blocked the first change after the jump).

- `seq_loopProcessTick()` wrap: after `seq_rampBreakTrack()`, if this tick's
  DDA will not advance the restored track (`accumulator + 256 < interval`,
  and not SOM mode), push `{restored step, fired 0, jump 1}`. When the DDA
  advances, its own entry reports the replayed step.
- `seq_loopResumeTrack()`: drops the echo mark (`seq_echoStep[track] = -1`)
  and pushes a jump entry when the resumed step differs. All callers
  (`seq_loopUpdateMask()`, `seq_loopDisengage()`) hold PRIMASK, so the
  foreground push cannot interleave with TIM3: the ring stays
  single-producer through `seq_recRingPush()`.
- A step begun by a jump entry is a **partial replay**: at its end only
  dirty writes and erase run; no LATCH re-stamp and no clear (A-2 by design:
  a track that always wraps mid-step never gets re-stamps there).

**Loop-aware placement (FQ6, Q6).** `seq_track_time_t` gained `looped`,
`loopStartPlayState`, `loopStartStep`, `loopStartAccum`, `loopTicks`,
`loopAnchor` (+12 B stack), filled under the capture PRIMASK (`looped`
requires `seq_loopActive && !seq_initialSchedulerTick` and the track bit).
`seq_placeLoopImage(track, snap, deltaQ8, pos)`:

```
span   = L*256;  nowRel = (elapsed - anchor + 1)*256;  tgtRel = nowRel + delta
kNow   = (nowRel - 256) / span;   x = tgtRel - 256
kTgt   = floor(x / span)
same pass (kTgt == kNow), or first pass back into the unlooped history
(kNow == 0 && kTgt < 0)                -> 0: walk the live cursor (exact)
otherwise pos = loop snapshot shifted by
   (kTgt < 0) ? tgtRel                  (unlooped history before pass 0)
              : x - kTgt*span + 256     (the target's phase in its pass)
```

Pass k covers relative times `256 + k·L·256 .. 255 + (k+1)·L·256` (the wrap
at tick `anchor + k·L` restores the snapshot and the same tick's DDA adds
256). With `crossed`, `rel` is decided by time (a forward snap across the
wrap is ahead even if its step index is lower) and `ahead` is the time to
the landed step, so the echo mark covers exactly the play after the wrap.
Echo marks are **not** cleared at the wrap (that would contradict Q6); the
P1B deadline expires them. Unchanged: the master grid, `trk` snaps on the
replayed grid, `rnd` forward shifts clamped to 0.

Verification: before scheduling, a host simulation (fwd; intervals 3, 5, 6,
7.3, 7.8, 24 ticks; loop lengths 2..384; anchors 1..100; four passes;
deltas −2L..+3L): **0 mismatches in 86,593,860 cases** once the pre-anchor
rule was added (the first version failed 30,696 cases mapping a backward
delta from a later pass to before the anchor). After implementation, the
code replicated 1:1 in Python (fwd, rev, pip; intervals 3, 6, 7.3, 24;
lengths 2..24; anchors 1..55; loops 2..48; whole and sub-tick deltas): **0
mismatches in 13,210,776 cases**.

### 8.5 Implementation (in `2a0fd86`)

| Phase | Steps | Result |
|---|---|---|
| P3-0 | SQ-1 `SEQ_REC_RING_JUMP_BIT` + `seq_recRingPush()`, SQ-1b, SQ-2 decode, HD-1 `seq_rec_boundary_t.jump`, LR-1..LR-5 (`liveRec_prevFlags`, jump rule) | text +80 B, bss unchanged |
| P3-A | SQ-3 wrap jump entry, SQ-4 resume, HD-2 loop fields, SQ-5 capture, SQ-6 `seq_placeLoopImage()` | text +528 B, bss unchanged |
| P3-B | LR-6 trigger gate removed, LR-7 `liveRec_forceOff()` removed, MN-1 PERF Morph recording, BH-1..BH-10 | text +672 B, bss unchanged; flash headroom 180,180 B |
| P3-C | hardware matrix | pending |
| P3-D | docs (PATTERN_DYNAMIC_STACK §6.5, MODULE_INTERCHANGE_SPEC PERF surface, MEMORY.md) | done |

Deviations: the HD-2 comment text `loopStart*/loopTicks` closed the C
comment (reworded); the BH-6 comment avoids the name `liveRec_forceOff` so
the grep gate stays clean.

**Assessment (2026-10-16):** conforms; no code defect. Concurrency checked
(foreground pushes only under the looper's PRIMASK; `seq_rampBreakTrack()`
saves/restores PRIMASK, so nesting is safe). FQ5 reachability:
`voiceModeShowMorph` is cleared by every MODE press except SHIFT+MODE1, so
it is never set in PERF. Findings:

- **A-1 (confirm on hardware):** tapping the VOICE of a track already in
  the loop removes it on release (FQ1's "add" is a no-op, but the press is
  still a hold, and its release drops the track per S080 D1). After an
  engage with no VOICE held (all tracks loop), a quick tap of VOICE 3 takes
  track 3 out. Open.
- A-2: partial-replay steps get no re-stamp/clear (by design).
- A-3: the stale previous step when REC is armed while running (§7.4).
- A-4: documentation-only notes.

### 8.6 G-1 — the global Morph is never recorded

**Propagation:** `preset_morphScene()` writes the Scene's global amount, all
six per-voice Morph amounts (`scene_setAllVoiceMorphAmounts()`) and the
Effect Morph amount — the same stored values live record reads for `Nvm`
and `fxm`.

**Entry points checked:** the only real entry is `liveRec_paramIn()` from
`menu_liveRecRecordCell()`; the MIDI call passes `0xFFFF`.

| Global Morph source | Records? |
|---|---|
| PERF `mrp` knob/encoder (`menu_cellClearTarget()` returns 0 for `PAR_MORPH`) | no |
| knob erase / pot clear on `mrp` | no |
| MIDI CC1 on the global channel (`midiParser_setMorphFromModWheel()` → `preset_morph()`) | no |
| settings load replay (`menu_sendAllGlobals()` → `preset_morphScene()`) | no |
| Scene clear/reset (`preset_morphScene(target, 0)`) | no |
| Euklid `PAR_MORPH` cell (page compiled out) | no |

| Individual Morph source | Records? |
|---|---|
| PERF `1VM`..`6VM`, VOICE mix `Nvm` (`sceneModTarget_voiceMorphId(slot)`) | yes (rtk, T == A) |
| PERF `fxm`, Effect page Morph cell (`sceneModTarget_effectMorphId()`) | yes |
| MIDI CC1 on a voice channel | no (CC recording not implemented) |

**Leak found:** live record read the stored value at write time, which a
global change overwrites: G-L1 (switch 0) `1VM` then `mrp` in step N → the
`mrp` value written at the end of N; G-L2 (switch 1, no step known)
`1VM`, `mrp`, start → `liveRec_writePendingFirst()` wrote the `mrp` value.
Not leaks: switch 1 with a step known writes at once; the LATCH re-stamp
never applies to Morph targets (Scene targets are foreign).

**Fix (+7 B SRAM1, approved):** `liveRec_morphValue[7]` (6 voice Morphs + FX
Morph), `liveRec_morphIndex()` (voice slot 0..5, FX 6, else 0xFF),
`liveRec_recordValue7()` (captured value for a Morph target, else
`liveRec_targetValue7()`). `liveRec_paramIn()` captures on every
individual Morph edit after the ownership check and before the
first-change early return; both dirty writes (`liveRec_stepEnd()` switch 0,
`liveRec_writeCurrentValue()` switch 1) use `liveRec_recordValue7()`; the
re-stamp keeps `liveRec_targetValue7()` (own Instrument only). No Preset or
MIDI code changed. Build: text 573,296 (+136 B), data 416, bss 429,112
(+8 B: 7 B + 1 B alignment).

**G-1 hardware checks (not run):** 1. LATCH/OVERDUB playing: `1VM` or
`Nvm` recorded; `mrp` records nothing anywhere. 2. Stopped and armed: `2VM`,
then `mrp`, then start → the first step gets the `2VM` value. 3. Switch 0:
`fxm` then `mrp` in one step → the `fxm` value. 4. MIDI CC1 on the global
channel during LATCH: nothing recorded.

### 8.7 P3-C hardware matrix (not run)

1. PERF, running, REC off: SELECT engages; the loop runs until every
   SELECT, VOICE and BAR is released, in any order.
2. Same with LATCH, OVERDUB held, and live erase: identical; the record
   mode survives engage, swap, division change and release.
3. Looping, no SELECT held: VOICE adds its track; release drops it; a
   `lop`-off VOICE holds without joining. (Add A-1.)
4. Looping with SELECT held: VOICE swaps.
5. Looping: VOICE never fires the overlay, toggles a mute, or erases.
6. Overlay roll held, then SELECT: roll stops, recorded end written, VOICE
   holds the loop, release drops the track, no mute toggle.
7. Leave PERF with a VOICE held: the loop ends; a new loop later ends
   normally.
8. Stopped: SELECT only lights; a held overlay roll continues.
9. PERF `1VM`..`6VM`/`fxm` record in LATCH/OVERDUB, looping or not; `mrp`
   never.
10. LATCH while looping: re-stamp each pass, no repeat writes, service drop
    counters 0 for 1/16 loops.
11. FQ4: fired step then empty step → re-stamp on the fired, clear on the
    empty.
12. FQ3 with `qnt off`, a 1/4 track looped by a 1/16 window: a knob moved
    after each wrap is written to the replayed step once per pass; its
    own-Instrument entries are not cleared by the partial replay.
13. Loop shorter than a step: at most one write per pass.
14. Release mid-step: the next change goes to the resumed step.
15. A roll released at engage with qnt 1/16 near a loop end: lands on the
    replayed step after the wrap; that one replay is silent; later passes
    play it.
Regression: S080 looper rows; P2 ramps across a wrap.

### 8.8 P3 risks

R3-1 write rate on short loops (one first change per pass per step can
exceed the busy drain; counted); R3-2 `seq_shiftPositionQ8()` uses the
current scale/length for the whole span (a scale/length change during a
loop can move a forward snap by part of a step); R3-3 ring rate at 1/128
loops (16 entries per 2 ms drain; overflow counted); R3-4 `rnd` and once
modes under a loop (existing looper behaviour, recording follows); R3-5 a
chain `stp` freeze pushes no entries and nothing is written (consistent).

---

## 9. Part P4 — VOICE-mode bar-change LED pulse

### 9.1 Specification (user) and answers

1. In VOICE mode the SELECT LED of the bar the active track is playing
   pulses once when that bar changes.
2. The pulse must not interrupt the VOICE-mode SELECT row (sub-page
   indicator).
3. No pulse when the track has a single bar (effective length ≤ 16), when
   the new bar is the bar being exited (incl. a loop back to it), or in
   `rnd`.
4. VOICE mode only (STEP has its bar chaselight; PERF never shows the bar).

Answers: Q1 the shared 50 ms pulse; Q2 no pulse on the first step after a
start; Q3 a jump (looper wrap/resume, realign, per-track Scene change,
length change) that lands in another bar pulses once at the next drain; Q4
2 B acknowledged; fix the pulse lost at the clock wrap; Q5 fix the flash
expiry the same way.

### 9.2 Design (as built, `ledHandler.c`)

- `led_updateVoiceBarPulse(running)`, called in the `SEQ_LED_DIRTY_CHASE`
  branch of `led_processSeqLedState()` after the STEP chaselight, in every
  mode. It reads the active track live with `seq_captureTrackTime()` (not
  `seq_ledState.chaseStep`, which carries no track id, so an active-voice
  change between TIM3's write and the drain would pulse falsely). Bar =
  `stepIndex / 16`. It pulses `LED_PART_SELECT1 + bar` only when running,
  VOICE mode, same track as last time, a bar was remembered and differs,
  effective length > 16, play mode ≠ 3 (`rnd`, a literal: there is no named
  constant), bar < 8. It remembers the track and bar in every mode (so
  entering VOICE mid-play compares against a current baseline) and forgets
  the bar when stopped. State `led_barPulseTrack`, `led_barPulseBar`
  (0xFF = none, 2 B, `.data`).
- The pulse uses `led_pulseLed()` (a 50 ms inversion on the top
  `LED_LAYER_PULSE`), never base state, so the sub-page row is untouched; a
  sub-page change during the pulse is shown when it expires. The pulse
  shows over a SELECT flash/blink, which resumes after. 8 shared pulse
  slots; with all busy the pulse is dropped (existing rule).
- What counts: the bar of the step now playing differs from the last seen.
  The pip boundary repeat and a wrap within one bar do not pulse; a wrap
  from the last bar to bar 1 does; a length cut to ≤ 16 stops pulses at
  once.
- **Clock-wrap fix:** `time_sysTick` is a 16-bit ms counter (wraps every
  65.536 s). `led_tickHandler()` ended a pulse when `time_sysTick >
  end`; a pulse started in the last 50 ms before the wrap had a small
  `end` and ended on the next tick, never seen (about 1 in 1,300 pulses,
  every client). The flash end and phase-step tests had the same compare
  (a 400 ms flash could end at once or its phase step every tick). New
  `led_timeReached(deadline)` = `(int16_t)(uint16_t)(time_sysTick −
  deadline) > 0`, the form the fast blink already used; the pulse, flash end
  and flash phase tests use it; blink and fast blink are unchanged.
  `led_tickHandler()` and `led_processSeqLedState()` both run in the main
  loop, so starting and expiring a pulse never overlap.

### 9.3 Implementation and assessment

LH-1..LH-4 (wrap-safe expiry), LH-5..LH-8 (bar pulse) in `2a0fd86`. Build:
text 573,160 (+88 B), data 416 (+4: `.data` is `ALIGN(4)`-rounded),
bss unchanged; `nm -S` shows both bytes in SRAM1. Host check of
`led_timeReached()` (starts 65,000..65,535 and 0..1,000): every pulse lasts
exactly 51 ticks and every flash 401 ticks with 4 phase steps, 0 bad; the
old compare ended the pulse early for 50 start values (65,486..65,535).

Assessment (2026-10-16): conforms, no functional defect.
- **B-1:** the 2 approved bytes cost 4 B of RAM (`.data` padding; also in
  the flash init image). Option 2 (0 = none, store track + 1 / bar + 1, move
  to `.bss`) was recommended; the user accepted the P4 follow-ups as they
  are, so no change was made.
- **B-2 (low):** a transport stop and start inside one LED drain (1–2 ms,
  only reachable by an external MIDI Stop immediately followed by Start) is
  not seen; the first step after the start can pulse once.
- B-3: the snapshot local is 40 B with the P3 loop fields.

### 9.4 P4 hardware list (not run)

1. VOICE, 64-step fwd track: one pulse per bar change 1→2→3→4→1 (the lit
   sub-page LED blinks off when it is the bar LED, others blink on).
2. Sub-page changes during playback: the row stays right.
3. Length ≤ 16 (incl. Morph/length automation down to 16): no pulse.
4. pip at a bar boundary: none for the repeat; one when it crosses.
5. `rnd`: none.
6. STEP: chaselight unchanged, no pulse; PERF: none.
7. Active voice change during playback: no pulse from the change.
8. Transport start: no pulse on the first step.
9. A realign into another bar: one pulse.
10. Minutes of playback across 65.5 s wraps: no pulse missing; copy/step/REC
    pulses and SELECT/SEQ flashes look the same near a wrap.

---

## 10. Resources (RAM policy record)

All static, normal SRAM1, approved by the user (P1 "including follow-ons",
P2 F3 + D11, P3 G-1, P4 Q4).

| Part | Item | Bytes |
|---|---|---|
| P1 A1 | `voiceControl_pendingReset` | +1 |
| P1 A1 | `seq_trackShuffleStep[7]` + `seq_trackShuffleRoll[7]` | +14 |
| P1 A1 | `seq_menuOverride[6]` (`u64`) | +48 |
| P1 A2 | `menuHold` (`u64`) + `menuHoldMorph` | +9 |
| P1 A2 | owner/pending/held fields removed | −77 |
| P1 A3 | `bh_overlayHeldType` | +1 |
| P1 B | LiveRecord (bitmaps 192, mode/rtk/T/origin/epoch ~16, roll ~8, grace ~4) | +220 |
| P1 B | REC hold timer | +3 |
| P1 C | ring 34, echo 7, hold 1, erase masks 2, µs stamps 8, epoch 1, filter pointers 9 | +62 |
| P1 D | staging table 34, target request 2 | +36 |
| P1 G | FX-erase pending mask + lane mask | +34 |
| P1 total | every build ~+428 (~80 released: owner/pending 75, held_morph 2, legacy roll 2, `recordSubStep` 1; net ~+348) | |
| P1 DEV | `liveRec_drops` | +16 (14 used) |
| P1B | `seq_echoDeadline[7]` 14, shuffle mute mask 1, `seq_sceneEpoch` 1, `effects_erasedLanes[16]` +28 net, LiveRecord +6 (+ linker-kept grace/roll/epoch bytes), `bh_overlayNoteDefault` 1 | ≈ +61 |
| P1C | `liveRec_eraseFxSeqCount` 1, roll start delay + span 2, grace state +2 net, three `bh_erase*Mask` 5 | +10 (+2 DEV `seq_recRingDrops`) |
| P2 | `seq_ramps[64]` (10 B) 640, `seq_rampCtx[7]` 28, `seq_rampBlockDiv` 1, `track_auto_smooth` × 17 regions 17 | +686 (+2 DEV `seq_rampNoSlotDrops`) |
| P3 | none static; +12 B stack per `seq_track_time_t` local (frames do not nest) | 0 |
| P3 G-1 | `liveRec_morphValue[7]` | +7 (bss +8) |
| P4 | `led_barPulseTrack`, `led_barPulseBar` | +2 (`.data` +4) |
| **S081 net (DEV link)** | bss 427,976 → 429,112; data 412 → 416 | **bss +1,136, data +4** |

`pat_background_region` and each `pat_regions[]` entry are now 10,528 B.
`effects_automation_t` 184 → 120 B, `effects_state_t` 84 → 80 B. The
production (non-DEV) build was not re-measured this session.

---

## 11. Storage and format changes

| Item | Change |
|---|---|
| PAT4 track header byte 5 | `asm` (0/1). Version stays 2. Converter `tools/convert_pattern_autosmooth.py` (card already zero). |
| Stored step roll index | meaning shifted (index 1..9 = `1/8 8tr 16 16t 32 32t 64 64t 128`); no migration (F-H). |
| Settings `quantisation` | value 5 = `trk`; no parser change. |
| `rtk` | runtime only, never saved. |
| Pattern region | `track_auto_smooth` (+1 B per region). |

---

## 12. Affected files (code, `02553df..2a0fd86`)

| File | Change |
|---|---|
| `Core/Sequencer/LiveRecord.c/.h` | new module (§4) |
| `Core/Sequencer/sequencer.c/.h` | reset rule, deferral, R12 override, FX realign, roll table, `QUANT_TRACK`, live-record services, ramps, looper jump entries, loop-aware placement |
| `Core/MIDI/MidiVoiceControl.c/.h` | pending-reset mask, `noteOnNoReset()`, `resetOnly()`, `triggerNow(…, reset)` |
| `Core/MIDI/MidiParser.c` | `liveRec_triggerIn()` note hook, `liveRec_paramIn(0xFFFF)` CC stub |
| `Core/DSP/Effects/EffectsManager.c/.h` | R13, live FX erase, `effects_storeValue7()` |
| `Core/Bank/Scene/Pattern/PatternData.c/.h` | trigger policy, `pat_recordStep()`, `pat_eraseSpecials()`, `track_auto_smooth`, `pat_findStepAutomation()` |
| `Core/Bank/Scene/Pattern/PatternStackService.c/.h` | RECORD_STEP/ERASE_SPECIALS, staging, target override |
| `Core/Bank/Scene/Pattern/PatternTrace.h` | stage `'W'` |
| `Core/Bank/Scene/Pattern/SomGenerator.c` | `seq_triggerVoice(…, 1)` |
| `Core/Bank/Scene/Preset/presetManager.c/.h`, `presetMorphEngine.c/.h` | per-target Scene override clears |
| `Core/Bank/Scene/Preset/ParameterArray.h` | `PAR_TRACK_AUTO_SMOOTH` |
| `Core/Hardware/frontPanel/buttonHandler.c` | REC, erase chord/router, overlay reports and R19, looper rules |
| `Core/Hardware/frontPanel/ledHandler.c/.h` | recorded-step LED, `led_timeReached()`, bar pulse |
| `Core/Hardware/SD/filesystem.c` | AutoSave gates, PAT4 byte 5 writer/readers |
| `Core/Hardware/timebase.c` | `liveRec_service()` before `patSvc_tick()` |
| `Core/Menu/menu.c/.h`, `menuPages.h`, `MenuText.h` | R12 sites, record/erase hooks, Q13, `rtk`, lists, `asm`, `PAR_ROLL`, underline |
| `Core/Menu/CopyClear/copyClearSession.c`, `copyClearService.c` | armed refusal; `asm` copy/clear |
| `main.c`, `config.h`, `Makefile` | `liveRec_init()`, `seq_rampService()`; `LIVEREC_AUTOMATION_FIRST_CHANGE`; LiveRecord in SRCS |
| `tools/convert_pattern_autosmooth.py` | new |

---

## 13. Build results and hardware verification

### 13.1 Build trail (DEV link, `make all` clean at every step)

| Point | text | data | bss | Flash headroom |
|---|---|---|---|---|
| S080 close (`02553df`) | 556,008 | 412 | 427,976 | 197,244 |
| P1 (`d21e245`) | 565,016 | 412 | 428,316 | 188,236 |
| P1B end | — | — | — | 185,996 (rebuild) |
| P1C + C-1..C-3 | — | — | — | 185,084 |
| shuffle removal | — | — | — | 185,196 |
| hardware fix 1 | — | — | — | 185,060 |
| P2 + D11 | 571,392 | 412 | 429,104 | 181,860 |
| D12 (switch 1) | 571,792 | 412 | 429,104 | 181,460 |
| P3-B | 573,072 | 412 | 429,104 | 180,180 |
| P4 | 573,160 | 416 | 429,104 | 180,088 |
| G-1 (`2a0fd86`) | 573,296 | 416 | 429,112 | 179,952 |

Pre-existing warnings only (`PatternData.c:200` packed member, unused
`filesystem.c` functions, `EuklidGenerator.c:54`, newlib stub notes, LTO
serial note). ITCM 4,168 B, DTCM statics 4,472 B, FXBUF margin 3,712 B
unchanged. Image `build/LXRV2_lxr02.img` 573,712 B payload.

### 13.2 Hardware status and owed checks

- Used on hardware: P1 recording once (→ Hardware fix 1).
- Owed: P1B F6 rows (per-track playback with REC off: STEP edits work; knob
  erase never sounds; overlay roll on a non-`rtk` track; muted `rtk`;
  scale change then record; backward quantise and grace; micro-timed
  current step keeps automation; PERF erase Scene buttons inert; SEQ erase
  after an `rtk` override; SHIFT+CLEAR+TRACK then PLAY; T ≠ A Effect edits
  not recorded; SHIFT/CLEAR orders; SELECT while stopped and armed;
  encoder edits; `def` note; roll endings; sub-tick at slow tempo; FX erase
  across steps); P1C C5 rows 1–10 (FX lanes 8–15 and knob lanes surviving a
  SEQ release/MODE press; page exclusion; roll endings incl. forward
  quantise; SHIFT+CLEAR then REC; flams; SEQ erase across sessions; two SEQ
  buttons in FX; mode change during a hold; DEV ring overflow trace) and the
  **C-1 boundary roll check** (rolls started/released right on step
  boundaries at 1/32, 160 BPM: no overwritten step before the start, no gap
  before the end); host test of `seq_placeFromSnapshot()` (scheduled, not
  run); P2 §6.8 incl. CPU; D12 §7.5; P3 §8.7; G-1 §8.6; P4 §9.4.
- Phase A behaviour (reset rule, R12/R13, roll rates) should be signed off
  on hardware: existing patterns sound different by design.

### 13.3 Open items carried forward

| Item | State |
|---|---|
| P1 item 29: FX realign on non-resetting Scene changes | **closed** 2026-10-21: correct as implemented (user; same master-clock alignment as the tracks while `pcr` is off) |
| P3 A-1: tapping an already-looped VOICE drops it on release | confirm on hardware |
| Stale previous step when REC is armed while running (P2 §7.4, P3 A-3) | open, low with switch 1 |
| P4 B-1 (4 B for 2 B) | accepted as is |
| P4 B-2 (stop+start inside one drain) | open, low |
| LiveRecord includes `menu.h` (Hardware fix 1) | invariant deviation, recorded |
| `tools/decode_devlogs.py` stage `'W'` causes 1–5 | not added |
| Production build size | not re-measured |
| 64-ramp CPU worst case | not measured |
| Automation `hold` reconciliation (SCOPING §4.3a) | not confirmed |
| F-Q (SHIFT Morph-view Effect/Scene edits as menu edits) | try on hardware |

---

## 14. Specification reference updates (S081 close, 2026-10-21)

Some S081 notes were already in the specs from the P3-D/P4/G-1 documentation
steps (`PATTERN_DYNAMIC_STACK.md` §6.5 looping-while-recording note,
`MODULE_INTERCHANGE_SPEC.md` PERF surface and Live record service row and
ledHandler rows, `STORAGE_SRAM_MANIFEST.md` P3/P4/G-1 ledger). The close-out
added:

- `PATTERN_DYNAMIC_STACK.md`: step reset rule and deferred automation
  (§6.1), R12 amendment (§6.2a), automation ramps (§6.2b), live record
  (§6.7), roll table, `qnt trk`, queue format correction (§12.3), live-record
  service ops and target override (§12.12), trace stage `W` (§12.16).
- `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md`: R13 resolution,
  one-step Morph lane, overlay lifetime, live FX erase, FX realign.
- `MODULE_INTERCHANGE_SPEC.md`: LiveRecord module section; MidiVoiceControl,
  Sequencer, PatternData/PatternStackService, EffectsManager, Preset,
  Menu, buttonHandler rows.
- `STORAGE_SRAM_MANIFEST.md`: P1/P1B/P1C/P2 RAM, region size 10,528 B.
- `FILESYSTEM_SPEC.md`: PAT4 track byte 5, `quantisation` 5.
- `MEMORY.md` and `knowledge_files/volatile/`.

---

## 15. Disposable documents

The ten root files listed in §1 are superseded by this log and the specs.
They contain the step-by-step schedules (file:line anchors at the S081
trees, comment-block text that now lives beside the code) and may be
deleted. Their stale cross-references to the never-created
`S081_P2_LOOP_WHILE_RECORD.md` (in `S081_P1_IMPLEMENTATION.md` and
`S081_P1B_IMPLEMENTATION.md`) are historical and need no fix.

---

## 16. Carry-forward

1. S083 MIDI is next (planned in S082). MIDI note and CC recording plug into
   `liveRec_triggerIn()` and `liveRec_paramIn()`; the CC → target mapping,
   timestamps and R12 priority for CC are MIDI-rework decisions (§4.13).
   MIDI CC1 on the global channel must stay unrecorded (G-1).
2. Run the hardware lists in §13.2 and measure the 64-ramp CPU case.
3. Confirm P3 A-1 (P1 item 29 is decided: correct as implemented).
4. Add stage `'W'` to `tools/decode_devlogs.py`.
5. Re-measure the production build.
