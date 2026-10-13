# S081 P1 — Live Record: Implementation Schedule

Session 081 implementation schedule, branch `dev-ph6-cleanup`, base `02553df`.
Cut from `S081_P1_LIVE_RECORD.md` §15 and §19.

Line numbers are at base `02553df` unless noted. Each change entry carries a
comment-block description to be placed adjacent to the code as
documentation-in-place.

**Revision 2 (2026-10-13):** review corrections applied. The record of each
correction is `S081_P1_LIVE_RECORD.md` Appendix A (items 1–14 and 16–21,
plus the minor fixes). Item 15 (looper) is unchanged here, pending a
decision.

**Revision 4 (2026-10-13):**

- **L1 kept:** A3-3c is rewritten for per-track `trk` anchors.
- **L3 adopted:** F-5 writes and clears only real changes.
- **Moved to P2:** the looper/record work (L-2 lockout removal, L2 button
  sharing, L4 loop-aware placement and echo) moved to
  `S081_P2_LOOP_WHILE_RECORD.md`. So B-2a, B-2e, B-4 and E-1 are back to
  the interim lockout (`liveRec_forceOff()` before `seq_loopEngage()`), and
  the L4 notes are removed from C-8b and C-10.

---

## Implementation log

Executed against `dev-ph6-cleanup`, base `02553df` (code unchanged through
`a6c96a0`). Each entry records what landed; every change carries the adjacent
comment block from its schedule step. Full firmware build (`make all`) is the
per-phase gate.

### Phase A1 — done (2026-10-13)

- **A1-1** `Core/MIDI/MidiVoiceControl.c/.h`: added `voiceControl_pendingReset`
  mask, `voiceControl_noteOnNoReset()`, `voiceControl_resetOnly()`;
  `voiceControl_popPending()` now returns the per-voice reset under PRIMASK;
  `voiceControl_triggerNow()` gained `reset` and only restores when set;
  `voiceControl_processPending()` drains leftover reset bits under PRIMASK.
- **A1-2** `Core/Sequencer/sequencer.c/.h`: `seq_triggerVoice()` gained `reset`;
  all callers updated (roll first hit / step hits / overlay / SOM / preview = 1,
  `seq_processRollTicks` retriggers = 0, overlay timer = `count != 0xFF`).
- **A1-3** `sequencer.c`: `seq_trackShuffleStep/Roll` deferral state,
  `seq_fireDeferred()` (queues automation then fires roll/plain), deferral path
  now covers roll steps, step-allowed block skips automation when the step's
  trigger was deferred, roll end rule (F-R) only cuts on a firing trigger,
  roll carry path calls `voiceControl_resetOnly()`.
- **A1-4** `sequencer.c/.h`: `seq_menuOverride[6]`, `seq_menuOverrideSet()`,
  drain skip filter, per-trigger clear in `seq_restoreAutomatedParameters()`,
  clears at all A1-4e transport/Scene/per-track/realign sites.
- **A1-5** `presetManager.c/.h`, `presetMorphEngine.c/.h`:
  `preset_clearFxSendStepOverride()`, `preset_clearAudioOutStepOverride()`,
  `presetMorph_clearTrackParamStepOverride()`.
- **A1-6** `sequencer.c/.h`: `seq_clearSceneAutomationBit()`.
- **A1-7** `Core/Menu/menu.c`: INSTRUMENT commit sets the override before the
  edit-mask loop; AUDIO_OUT/FX_SEND/7dc commits do the per-target clear;
  `menu_clearTrackTimingOverride()` helper applied to the STEP length/scale/
  shuffle Normal and Morph-endpoint commits.

Gate: `make all` clean (pre-existing filesystem unused-function warnings and
linker libc notes only). Flash headroom 195,476 B.

### Phase A2 — done (2026-10-13)

- **A2-1** `EffectsManager.c/.h`: removed `owner[]`, `pending_end`,
  `pending_track`, `owner_tracks`; removed `effects_automationPublishOwners()`,
  `effects_automationStepBegin()`, `effects_automationStepFlush()` and their
  header declarations; removed the `held_morph`/`held_morph_valid` latch and
  `effects_seqLatchMorph()`. `effects_state_t` 84→80 B,
  `effects_automation_t` 184→120 B.
- **A2-2** added `menuHold` (u64) + `menuHoldMorph` (u8); `effects_setParameter`
  / `effects_setMorphAmount` clear the overlay + set menuHold; `effects_apply
  Automation` / `effects_setMorphAutomation` clear menuHold; new
  `effects_lockClearLayers()` at FX lock step start (in `effects_seqConsume`
  STEP and `effects_seqSelect` sel); `effects_automationClear` clears
  menuHold; `effects_service()` resolution is now menuHold > lock > overlay >
  menu, and the Morph base resolution is menuHoldMorph > lockMorph (current
  step) > fxm > menu.
- **A2-3** `sequencer.c/.h`: removed `seq_queueEffectStepMarker()`,
  `seq_effectAutomationTracks`, `seq_setEffectAutomationTracks()`,
  `SEQ_PENDING_TYPE_FX_STEP_BIT`, the marker call, the drain marker branch and
  the final `effects_automationStepFlush()`.
- **A2-4** `sequencer.c`: new `seq_fxRealignToMasterClock()` and
  `seq_requestEffectOverlayReset()`, called at the end of both realign
  functions (a STEP keeps the FX position valid; the latch clears overlays).

Gate: `make all` clean. Flash headroom 195,580 B; bss 428040→427968 (72 B
released).

### Phase A3 — done (2026-10-13)

- **A3-1** `sequencer.c`: `seq_rollPpq()` retimed to the 96-PPQ table
  `{0,48,32,24,16,12,8,6,4,3}`, guard `rate > 9`.
- **A3-2** `MenuText.h`: `stepRollRateNames` → 10 entries (drop 48, add 64t /
  128); `quantisationNames` → 6 entries (add trk). `menu.c`: STEP roll clamp
  8→9; `menu_getRollRateName` guard 8→9.
- **A3-3** `sequencer.h`: `QUANT_TRACK`. `sequencer.c`: `seq_setQuantisation()`
  clamps 0..5; `seq_loopSnapshotTracks()` computes per-track anchors for
  `trk` (each track's own last step start); `seq_loopEventAnchor()` unchanged.
- **A3-4** `buttonHandler.c`: new `bh_overlayHeldType`; BAR LEDs are (re)set
  per mode in `handleModeButtons()`; new `buttonHandler_overlaySwitchHeld()`
  re-configures the held overlay on a BAR switch/toggle-off (F-B/F-O), called
  from both PERF BAR presses. The two `liveRec_rollRelease/RateChange`
  notifications are deferred to Phase E with the module (the A3 phase must
  build without LiveRecord; noted as a schedule adjustment).
- **A3-5** `sequencer.c/.h`: removed `seq_rollRate`/`seq_rollState`, the legacy
  roll block in `seq_advanceTrackStep`, `seq_setRoll()`, `seq_setRollRate()`
  and their declarations. `menu.c`/`menuPages.h`/`MenuText.h`: retired
  `PAR_ROLL` (dtype, parse, default, name-value cases, Euklid cell) and removed
  `MENU_ROLL_RATES`/`rollRateNames`.

Gate: `make all` clean. Flash headroom 195,188 B.

### Phase B — done (2026-10-13)

- **B-1** new `Core/Sequencer/LiveRecord.c/.h`; added to `Makefile` SRCS. Mode,
  `rtk` and gate functions complete; `liveRec_service()` and the trigger/
  automation/erase entry points are stubs (filled in E/F/G).
- **B-2** `buttonHandler.c`: removed `bh_state.seqRecording`; added
  `bh_recPressTick`/`bh_recArmed`; REC press starts the hold timer; REC hold
  auto-enters overdub in `buttonHandler_tick`; REC release toggles latch or ends
  overdub; SHIFT+REC menu enter flashes the REC LED, exit restores it; COPY
  erase gate uses `liveRec_armed()`.
- **B-4** `sequencer.c`: removed the record-off inside `seq_loopEngage()`;
  `buttonHandler.c` calls `liveRec_forceOff()` + REC LED off before engage.
- **B-5** `menu.c`: `menu_setActiveVoice()` follows `rtk`; `PAR_AUTOM_TRACK`
  dtype `DTYPE_0B127`, clamp 0..6, rtk display special case, commit via
  `menu_parseGlobalParam`, excluded from the settings-dirty test;
  `menuPages.h` sub-page 2 cell 0 = rtk; `MenuText.h` names `rtk`/`RecTrk`.
- **B-6** `copyClearSession.c` and `filesystem.c` gates use
  `liveRec_armed()`/`liveRec_patternWriting()` (seq_eraseActive term kept until
  G-5).
- **B-7** `sequencer.c/.h`: removed `seq_quantize()`, `seq_recordTrigger()`,
  `seq_setRecordingMode()` and their declarations; MidiParser.c note hook now
  builds a `liveRec_trigger_t` (SINGLE/MIDI) and calls `liveRec_triggerIn()`.
- **B-8** `ledHandler.c/.h`: removed `SEQ_LED_DIRTY_REC_SUB`, `recordSubStep`
  and `led_updateRecordedSubStep()`; `led_updateRecordedMainStep()` now draws by
  absolute step; new `seq_markRecordedStep()` in `sequencer.c/.h`.
- **B-9** `timebase.c` calls `liveRec_service()` before `patSvc_tick()`; `main.c`
  calls `liveRec_init(menu_getActiveVoice())` after `copyClear_init()`.

Gate: `make all` clean. Flash headroom 194,780 B; bss +192 B (LiveRecord).

Schedule adjustment: the A3-4b `liveRec_rollRelease()`/`liveRec_rollRateChange()`
notifications land with the overlay hooks in Phase E-2 (the A3 phase had to build
before LiveRecord existed).

### Phase C — done (2026-10-13)

- **C-1..C-7** `sequencer.c/.h`: added the service state (`seq_recRing` +
  cursors, `seq_recTrack/Writing`, `seq_echoStep[7]`, `seq_liveHoldMask`,
  `seq_eraseSpecialsMask/FullMask`, `seq_ppqStampUs/PeriodUs`, `seq_playbackEpoch`,
  filter pointers) and the APIs `seq_takeRecBoundary`, `seq_setRecordTarget`,
  `seq_suppressStepOnce`, `seq_setLiveHold`, `seq_cancelPendingTrigger`,
  `seq_setEraseMasks`, `seq_getPlaybackEpoch`, `seq_setAutomationFilter`;
  PPQ us stamp written at the head of `seq_processSchedulerTick`; drain filter
  skips latched/erased entries; `seq_playbackEpoch` bumped and echo marks expired
  at the transport/Scene/realign/per-track sites; `seq_liveRecResetMarks()` at
  `seq_init`.
- **C-8** `seq_captureTrackTime()`, `seq_projectStep()`, `seq_quantiseTick()`
  (+ the header types `seq_track_time_t`, `seq_rec_boundary_t`).
- **C-9** `seq_advanceTrackStep()`: fired bit + boundary-ring push; side-effect-
  free `seq_conditionPassesPure()` for a muted `rtk`; echo/live-hold trigger
  suppression with echo consumption. `seq_evaluateStepCondition()` refactored to
  share `seq_conditionEval()`.
- **C-10** echo marks expire on effective length/scale change
  (`seq_refreshTrackEffectiveParams`), and at the reset/epoch sites.

Gate: `make all` clean. Flash headroom 193,844 B.

Minor deviation: the play-mode-change echo expiry (C-10) is not wired (it would
need a Sequencer entry point from the PatternData commit); the length/scale and
reset/epoch expiries cover the realistic cases. Noted for review.

### Phase D — done (2026-10-13)

- **D-1** `pat_writeDynamic()` gained a `trigger_policy` argument applied inside
  the two PRIMASK publications (`pat_rawTriggerBits`); all existing callers pass
  `PAT_RAW_TRIGGER_KEEP`.
- **D-2/D-3** `PatternData.c/.h`: new `pat_record_spec_t`, `pat_recordStep()`
  (trigger ON, clears rhythm, keeps the condition or sets fil) and
  `pat_eraseSpecials()` (keeps condition + automation, else releases the block).
- **D-4** `PatternStackService.c/.h`: 4-slot record staging table;
  `PATSVC_OP_RECORD_STEP` / `PATSVC_OP_ERASE_SPECIALS`; `patSvc_recordStep()`
  submit, `patSvc_enqueueEraseSpecials()` (TIM3-safe), `patSvc_requiredChunks()`
  cases, `patSvc_executeEvent()` cases, and staged-slot release on count-drops.
- **D-5** `service_target_request` + `patSvc_desiredScene()`; all eight
  `seq_activePattern` comparisons now use `patSvc_desiredScene()`;
  `patSvc_requestTarget`/`releaseTarget`/`targetOpen`/`replacePending`;
  `patSvc_idle()` is false while a target override is held.

Gate: `make all` clean. Flash headroom 192,916 B.

### Phase E — done (2026-10-13)

- **E-1** `LiveRecord.c`: `liveRec_triggerIn()` gating (LATCH/OVERDUB, running,
  P1 looper lockout, ownership), capture, `liveRec_place()` (step + delay),
  slow/fast roll classification (a duplicated 96-PPQ table),
  `liveRec_commitHit()` (write + echo/cancel), flam pairing, `fil`.
- **E-2** `buttonHandler.c`: `buttonHandler_overlayReport()` helper; overlay
  single/grace/roll-start report in `overlayTrigger`, flam-main/roll-hit report
  in `overlayTick`, `liveRec_rollRelease()` in `overlayRelease`; the deferred
  A3-4b `liveRec_rollRelease/RateChange` notifications in
  `overlaySwitchHeld`.
- **E-4** `liveRec_service()` ring drain writes carry-over span steps (fill 100)
  for the live-hold track; `liveRec_stepEnd()` is a stub filled in F/G.

Gate: `make all` clean. Flash headroom 190,532 B.

Simplifications to verify on hardware: micro-timing/shuffle are not folded into
placement (nominal step only); the roll release fill is left at 100 rather than
the elapsed fraction; MIDI/overlay sub-tick µs is not applied.

### Phase F — done (2026-10-13)

- **F-1** `menu.c`: `menu_liveRecRecordKnob()` (recordable test: mode, page,
  Morph view, track-length exclusion) called after the ordinary pot commit.
- **F-2** `LiveRecord.c`: `liveRec_paramIn()` marks dirty (and latch in LATCH);
  `liveRec_targetValue7()` resolves Instrument (Normal endpoint) and Scene
  targets (morph/audio-out/fx-send/7dc/fxm/scl/shf) and Effect (Normal).
- **F-3** drain filter already done in C-7.
- **F-4** `MidiParser.c`: CC stub hook calls `liveRec_paramIn()` with an invalid
  target (no mapping yet).
- **F-5** `LiveRecord.c`: `liveRec_stepEnd()` implements the LATCH/OVERDUB tables
  with one `pat_readStepAutomations()` read and L3 "only real changes" writes/
  clears, plus `fil` on written steps.

Gate: `make all` clean.

Simplification: F-1b (encoder path) is not hooked; only the endless-pot path
records. Noted for review.

### Phase G — done (2026-10-13)

- **G-3** `EffectsManager.c/.h`: `effects_eraseLanes`, `effects_erasedSteps`,
  `effects_erasedLanesAcc`; `effects_setLiveEraseLanes()`,
  `effects_takeErasedLocks()`; `effects_lockClearLayers()` excludes erased lanes
  (queues them) and `effects_seqOverride()` / the Morph lock skip them.
- **G-1/G-5** `buttonHandler.c`: removed `bh_state.seqErasing`; added
  `bh_shiftHeld`/`bh_clearHeld`; SHIFT+COPY enters/exits live erase; the erase
  button router handles SEQ (track or FX), VOICE (track) and PLAY (plain toggle).
- **G-2** `menu.c`: the knob-erase branch runs before the copy/clear pot
  branch and calls `liveRec_eraseParam()`.
- **G-5** `sequencer.c/.h`: the TIM3 erase branch uses `seq_eraseFullMask` /
  `seq_eraseSpecialsMask` (full queues `patSvc_enqueueErase`, specials queues
  `patSvc_enqueueEraseSpecials`, automation queues only for non-full); removed
  `seq_eraseActive`, `seq_setErasingMode()` and their terms in filesystem /
  copyClear.
- **G-6** `LiveRecord.c`: erase masks + `liveRec_pushEraseMasks()`;
  `liveRec_eraseParam/SeqHeld/FxSeqHeld/TrackHeld` implemented;
  `liveRec_stepEnd()` removes erased targets on the passed step; `liveRec_service`
  drains `effects_takeErasedLocks()` → `effects_clearSeqLanes()`.

Gate: `make all` clean.

### Phase H — done (2026-10-13)

- **H-1** target negotiation is in `liveRec_service()` (take boundary / epoch,
  `patSvc_replacePending()` suspend, `patSvc_requestTarget()` for T != A,
  `seq_setRecordTarget()` + `seq_setAutomationFilter()`).
- **H-2a** `menu.c`: `menu_parseKnobDelta()` / `menu_parseEncoder()` silently
  refuse STEP-page edits while `liveRec_activePatternLocked()` (Q13).
- **H-2b** `buttonHandler.c`: `buttonHandler_setRemoveStep()` refuses while
  locked (Q13).
- **H-3** DEV-only drop counters/trace codes are not added (no new trace stages).

Gate: `make all` clean. Flash headroom 188,236 B.

### Phase I — documentation

Deferred. The plan `§19.14` documentation list (PATTERN_DYNAMIC_STACK.md,
EFFECTS_BUS_REFERENCE.md, MODULE_INTERCHANGE_SPEC.md, STORAGE_SRAM_MANIFEST.md,
FILESYSTEM_SPEC.md, MEMORY.md) is not yet updated; it is prose work to be done at
session close and does not affect the firmware build. This implementation log
records the landed code change set in its place.

## Implementation status summary

All code phases (A1, A2, A3, B, C, D, E, F, G, H) are implemented and the full
firmware builds clean (`make all`) at every phase. The documentation phase (I)
is deferred. Known simplifications/verification items are called out per phase
above.

---

## Phase A1 — Step Reset Rule, Instrument Menu Override, Per-Target Clears

§5.8, §5.1, §5.2. Prerequisite for all recording: fixes the
trigger/automation ordering defects (§3.3-1/2) and establishes R12.

### A1-1. MidiVoiceControl: per-voice pending-reset mask

**File:** `Core/MIDI/MidiVoiceControl.c`

#### A1-1a. New static variable — ADD after line 70

```
/*
 * Per-voice pending-reset mask (S081 §5.8, §19.15).
 *
 * What: bit N (0..6) is set when voice N's next trigger must restore
 * Instrument automation to the Morph base and clear the menu override
 * before the voice fires. Why: voiceControl_noteOff() deletes queued
 * entries for that voice, so a flag stored inside VoiceTriggerEvent
 * would be lost on retrigger. A separate mask survives drops: a reset
 * stays pending until a trigger or end-of-pass consumes it. Inputs:
 * set by voiceControl_noteOn() (every live/seq first trigger),
 * voiceControl_resetOnly() (roll renewal with no trigger). Cleared by
 * voiceControl_processPending() when consumed. Outputs: gates the call
 * to seq_restoreAutomatedParameters() and the menu-override clear.
 * Affiliates: seq_triggerVoice() reset argument, main.c audio loop
 * ordering (voiceControl_processPending → seq_drainPendingAutomation).
 * RAM: +1 B SRAM1.
 */
static uint8_t voiceControl_pendingReset;
```

#### A1-1b. voiceControl_noteOn() — MODIFY at line ~108

Set the reset bit for the voice under the existing PRIMASK section.

```
/*
 * Live and sequencer first triggers always request a reset (F12, §5.8).
 * The bit is set under the queue's existing PRIMASK so that the enqueue
 * and the reset intent are published atomically. Roll retriggers 2+ use
 * voiceControl_noteOnNoReset() instead.
 */
voiceControl_pendingReset |= (uint8_t)(1u << voice);
```

#### A1-1c. New voiceControl_noteOnNoReset() — ADD after voiceControl_noteOn()

```
/*
 * Enqueue a voice trigger without requesting an automation reset (S081 §5.8).
 *
 * What: identical to voiceControl_noteOn() but does not set the
 * pending-reset bit. Why: roll retriggers 2+ must not restore the
 * Morph base or clear the menu override — only the roll's first hit
 * resets (F-L). Inputs: voice 0..6, note, vel. Outputs: ring entry
 * enqueued. Affiliates: seq_triggerVoice(…, reset=0) for roll hits,
 * voiceControl_noteOn() for first triggers.
 */
void voiceControl_noteOnNoReset(uint8_t voice, uint8_t note, uint8_t vel);
```

#### A1-1d. New voiceControl_resetOnly() — ADD

```
/*
 * Request an automation reset for a voice with no trigger (S081 §5.8).
 *
 * What: sets the pending-reset bit under PRIMASK without enqueuing a
 * ring entry. Why: a roll carried across steps renews the roll at the
 * new step boundary. The renewal resets automation (so the new step's
 * entries apply cleanly) but fires no new first hit. Inputs: voice
 * 0..6. Outputs: pending-reset bit set; consumed by the next
 * voiceControl_processPending() pass. Affiliates: seq_rollBegin()
 * carry path, seq_restoreAutomatedParameters().
 */
void voiceControl_resetOnly(uint8_t voice);
```

#### A1-1e. voiceControl_triggerNow() — MODIFY at line ~153

Gains a `reset` argument. The existing order is kept: the deferred Scene slot
apply runs first, then the restore (only when `reset`), then the trigger.

```
/*
 * Trigger funnel with conditional automation reset (S081 §5.8, §19.15).
 *
 * What: preset_applyDeferredSceneSlotForTrigger() first (unchanged), then
 * seq_restoreAutomatedParameters() only when `reset` is set, then
 * instrumentManager_triggerTrack(), the sidechain and the LED pulse
 * (unchanged). Why: only a step's first trigger, a roll renewal or a live
 * press may restore the Morph base (F-L). Keeping the deferred Scene apply
 * ahead of the restore preserves the pre-S081 order: the restore reads the
 * played Scene's morph_interpolation[] after a pending Scene image is
 * installed. Inputs: voice, note, vel, reset. Outputs: as before.
 * Affiliates: voiceControl_processPending() (the only caller).
 */
static void voiceControl_triggerNow(uint8_t voice, uint8_t note,
                                    uint8_t vel, uint8_t reset);
```

#### A1-1f. voiceControl_popPending() / voiceControl_processPending() — MODIFY at lines ~140 and ~260

```
/*
 * Atomic reset consumption (S081 §5.8, §19.15).
 *
 * What: voiceControl_popPending() gains an out-parameter `reset`. Inside its
 * existing PRIMASK section it copies the event, then tests and clears
 * voiceControl_pendingReset's bit for ev.voice in the same section. After
 * the ring is empty, processPending() takes the remaining mask under PRIMASK
 * (copy, then store 0) and, outside PRIMASK, calls
 * seq_restoreAutomatedParameters() for each bit taken: reset-only renewals,
 * and resets whose trigger entry was dropped by voiceControl_noteOff().
 * Why: TIM3 sets bits concurrently (seq_triggerVoice() runs in the ISR), so
 * every read-modify-write of the mask must be under PRIMASK, as the ring
 * cursors already are. Inputs: ring, mask. Outputs: triggers fired with
 * the right reset decision; leftover resets applied before
 * seq_drainPendingAutomation() (main.c:212-213). Affiliates:
 * voiceControl_triggerNow(reset), voiceControl_resetOnly().
 */
```

**File:** `Core/MIDI/MidiVoiceControl.h`

#### A1-1g. Header declarations — ADD after line 75

```
void voiceControl_noteOnNoReset(uint8_t voice, uint8_t note, uint8_t vel);
void voiceControl_resetOnly(uint8_t voice);
```

---

### A1-2. Sequencer: seq_triggerVoice gains reset argument

**File:** `Core/Sequencer/sequencer.c`

#### A1-2a. seq_triggerVoice() signature — MODIFY at line 1268

```
/*
 * Trigger a voice with optional automation reset (S081 §5.8).
 *
 * What: fires noteOff then noteOn (or noteOnNoReset) for the voice,
 * sends MIDI, triggers hardware trigger output. The reset argument
 * selects whether the pending-reset mask is set: 1 for first triggers
 * (step start, deferred fire, roll first hit, overlay, preview), 0 for
 * roll retriggers 2+. Why: only a step's first trigger may restore the
 * Morph base and clear the menu override; later roll hits must not
 * (F-L). Inputs: voiceNr 0..6, vol, note, reset 0/1. Outputs: voice
 * queued in the trigger ring, pending-reset bit set if reset=1.
 * Affiliates: voiceControl_noteOn/noteOnNoReset, seq_advanceTrackStep
 * callers, seq_processRollTicks, buttonHandler overlay,
 * seq_previewVoice.
 */
void seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note, uint8_t reset)
```

The body replaces `voiceControl_noteOn(voiceNr, note, vol)` with:
```c
if (reset)
    voiceControl_noteOn(voiceNr, note, vol);
else
    voiceControl_noteOnNoReset(voiceNr, note, vol);
```

#### A1-2b. All seq_triggerVoice callers — MODIFY

Every call site at base `02553df` (`grep -rn "seq_triggerVoice(" Core`):

| Caller | Line | reset | Reason |
|---|---|---|---|
| `seq_rollBegin()` first hit (`!carry`) | `sequencer.c:1972` | 1 | first hit of a new roll |
| `seq_rollBegin()` carry path | `:1974-1976` | — | no hit; calls `voiceControl_resetOnly()` (A1-3f) |
| `seq_advanceTrackStep()` early flush | `:2259` | — | replaced by `seq_fireDeferred()` (A1-3d) |
| `seq_advanceTrackStep()` on-time plain hit | `:2287` | 1 | first trigger of the step |
| legacy roll block | `:2314` | 1 | compiles until A3-5b removes it |
| `seq_processDeferredTriggers()` expiry | `:3719` | — | replaced by `seq_fireDeferred()` (A1-3d) |
| `seq_processRollTicks()` retrigger | `:3780` | 0 | roll hit 2+ |
| `seq_setRoll()` | `:4177` | 1 | compiles until A3-5c removes it |
| `SomGenerator.c` | `:139` | 1 | SOM is ignored by live record but must compile; behaviour unchanged |
| `buttonHandler_overlayTrigger()` single | `buttonHandler.c:899` | 1 | live press |
| `buttonHandler_overlayTrigger()` flam grace | `:904` | 1 | live press |
| `buttonHandler_overlayTrigger()` roll first hit | `:916` | 1 | live press |
| `buttonHandler_overlayTick()` | `:950` | `bh_overlayCount != 0xFFu` | 1 for the flam main (count 1); 0 for roll repeats (count `0xFF` = repeat while held). §5.8: each flam hit counts as a press; overlay roll repeats do not reset |

Not callers: `seq_previewVoice()` and `midiParser_noteOn()` call
`voiceControl_noteOn()` directly. That function now always requests a reset,
which is correct for live triggers (F12).

**File:** `Core/Sequencer/sequencer.h`

#### A1-2c. Header declaration — MODIFY

Update `seq_triggerVoice` declaration to include the `reset` parameter.

---

### A1-3. Sequencer: deferred trigger carries step index and roll flag

**File:** `Core/Sequencer/sequencer.c`

#### A1-3a. New deferral state — ADD after line 211

```
/*
 * Deferred-trigger step index and roll flag per track (S081 §5.8).
 *
 * What: when a trigger is deferred by shuffle or micro-timing,
 * seq_trackShuffleStep[track] records the step that produced it and
 * seq_trackShuffleRoll[track] records whether it was a roll step.
 * Why: at the deferred fire time, the step's automation entries must be
 * queued (seq_queueStepAutomations) and a roll step must start via
 * seq_rollBegin rather than a plain trigger. The existing deferral
 * state (vel, note, delay, pending) carries only the trigger data, not
 * the step context. Inputs: written by seq_advanceTrackStep at the
 * deferral point. Outputs: read by seq_processDeferredTriggers at fire
 * time. RAM: +14 B SRAM1 (7 × u8 + 7 × u8). Affiliates:
 * seq_trackShuffleDelay/Pending/Vel/Note, seq_queueStepAutomations,
 * seq_rollBegin.
 */
static uint8_t seq_trackShuffleStep[NUM_TRACKS];
static uint8_t seq_trackShuffleRoll[NUM_TRACKS];
```

#### A1-3b. seq_advanceTrackStep trigger deferral — MODIFY at lines 2250–2295

The trigger branch currently skips the deferral for roll steps
(`seq_rollBegin()` is called immediately at line 2253). Change it so roll
steps take the same shuffle/timing deferral as plain steps:

```
/*
 * Roll steps honour shuffle and micro-timing (S081 §3.3-15, F-S).
 *
 * What: both roll and non-roll triggers are deferred by the same
 * shuffle + micro-timing delay. The deferral stores the step index and a
 * roll flag so the deferred fire can start a roll (seq_rollBegin) or a
 * plain hit. When a trigger is deferred, the local `deferred_this_step`
 * is set so the step-allowed block below does not queue this step's
 * automation now: seq_fireDeferred() queues it when the trigger fires.
 * Why: pre-existing defect, roll steps ignored shuffle and micro-timing;
 * and queueing automation at the step start let the delayed trigger's
 * restore undo it (§3.3-1). Inputs: sp.roll, shuffle_val, sp.timing, step
 * index. Outputs: seq_trackShuffle* deferral state, deferred_this_step.
 * Affiliates: seq_fireDeferred(), seq_rollBegin(), the step-allowed
 * automation block (A1-3c).
 */
```

New logic in the trigger branch (`step_allowed` and bit 15 set):

1. If a deferral is still pending for this track, call
   `seq_fireDeferred(track)` first. It fires the earlier step's trigger and
   queues its automation (A1-3d). This replaces the inline flush at `:2257`,
   which only fired a plain hit and, with automation moved to fire time,
   would have lost the earlier step's automation and turned a deferred
   roll into a plain hit.
2. Compute `total_delay` from shuffle and micro-timing (existing code).
3. If `total_delay > 0`: store `seq_trackShuffleStep[track]`,
   `seq_trackShuffleRoll[track] = (sp.roll > 0)`, velocity, note and delay;
   set `deferred_this_step = 1`.
4. If `total_delay == 0` and roll: `seq_rollBegin(track, &sp)` (its first
   hit passes reset = 1).
5. If `total_delay == 0` and not roll: `seq_triggerVoice(track, sp.velocity,
   sp.note, 1)`.

#### A1-3c. Step automation queueing stays at the step start, except for deferred steps — MODIFY at lines 2296–2306

```
/*
 * Automation follows the conditional gate, not trigger state
 * (PATTERN_DYNAMIC_STACK.md §6.1; S081 §5.8).
 *
 * What: the existing call stays in the step-allowed block, so every step
 * whose condition passes queues its automation at the step start,
 * including steps with no trigger. The one exception: when this step's
 * own trigger was deferred (deferred_this_step), the call is skipped and
 * seq_fireDeferred() queues the entries at the deferred fire, together
 * with the resetting trigger. Before queueing, a still-pending deferral
 * from an earlier step on this track is fired first (seq_fireDeferred), so
 * entries stay in step order even when the next step has automation but no
 * trigger. Why: automation-only steps are a product feature and must never
 * depend on trigger state; only a step whose reset is delayed needs its
 * automation delayed with it. The live-erase guard on this call is
 * unchanged until Phase G (G-5).
 */
if (step_allowed &&
    (!seq_eraseActive || track != menu_getActiveVoice())) {
    if (seq_trackShufflePending[track] && !deferred_this_step)
        seq_fireDeferred(track);
    if (!deferred_this_step) {
        seq_queueEffectStepMarker(track, (uint8_t)seq_stepIndex[track]); /* removed in A2-3b */
        seq_queueStepAutomations(track, (uint8_t)seq_stepIndex[track]);
    }
}
```

A pending deferral whose next step queues nothing keeps its own timing; it
fires at expiry as today.

#### A1-3d. seq_fireDeferred() — ADD; seq_processDeferredTriggers — MODIFY at lines 3710–3725

```
/*
 * Fire one track's deferred trigger with its automation (S081 §5.8).
 *
 * What: queues the deferred step's automation entries
 * (seq_queueStepAutomations(track, seq_trackShuffleStep[track])), then
 * fires the trigger: for a roll step it re-reads the step's specials and
 * calls seq_rollBegin() (first hit with reset = 1); otherwise
 * seq_triggerVoice(track, vel, note, 1). Clears the pending state. Why:
 * one routine serves both the expiry path and every early flush, so a
 * deferred step always keeps its automation and its roll. Queueing in TIM3
 * right beside the trigger keeps the foreground order
 * voiceControl_processPending() (reset + trigger) →
 * seq_drainPendingAutomation() (entries), so the step's own automation
 * lands after its reset. Context: TIM3 only. Inputs:
 * seq_trackShuffleStep/Roll/Vel/Note. Outputs: entries queued, trigger
 * enqueued, pending cleared. Affiliates: seq_processDeferredTriggers()
 * (expiry), seq_advanceTrackStep() (early flush), seq_rollBegin().
 */
static void seq_fireDeferred(uint8_t track);
```

`seq_processDeferredTriggers()` (`:3710-3725`) calls `seq_fireDeferred(track)`
when a delay expires, instead of the inline `seq_triggerVoice()` at `:3719`.

#### A1-3e. Roll end rule (F-R) — MODIFY at lines 2203–2206

```
/*
 * A running roll ends only when the next step has a trigger that fires
 * (S081 F-R, §3.3-16).
 *
 * What: the previous code ended a roll at any step with sp.roll == 0,
 * including empty and automation-only steps. The new code ends a roll
 * only when the step has a trigger (bit 15 set) and its condition
 * passes. An empty step does not cut the roll; the roll engine ends it
 * naturally after its fill window (seq_rollRemaining reaches 0). Why:
 * a roll delayed into the next step's time was immediately cut by that
 * boundary, losing the last hits. With this change, a roll runs its
 * own length from its delayed first hit, as designed. Inputs: sp.roll,
 * step trigger state, condition result. Outputs: seq_rollActive may be
 * cleared. Affiliates: seq_rollBegin carry path, seq_rollRemaining,
 * seq_processRollTicks.
 */
```

New logic: replace `if (sp.roll == 0u) { seq_rollActive[track] = 0u; ... }`
with:
```c
if (seq_rollActive[track] && sp.roll == 0u &&
    pat_isStepActive(...) && step_allowed) {
    seq_rollActive[track] = 0u;
    seq_rollDuckActive[track] = 0u;
}
```

#### A1-3f. seq_rollBegin carry path — MODIFY at line 1999

```
/*
 * Roll renewal requests a reset-only (no new trigger) (S081 §5.8).
 *
 * What: when a roll carries across a step boundary (same rate/rhythm/
 * fill), the carry path adds the new step's remaining slots but fires
 * no new first hit. It now calls voiceControl_resetOnly(track) so that
 * the pending-reset mask is set, causing the next
 * voiceControl_processPending pass to restore automation and clear the
 * menu override without an audible trigger. Why: the new step may have
 * different automation entries; the restore ensures the old step's
 * entries do not persist. Inputs: track. Outputs: pending-reset bit
 * set. Affiliates: voiceControl_resetOnly, seq_restoreAutomatedParameters,
 * the ring push in seq_advanceTrackStep.
 */
voiceControl_resetOnly(track);
```

---

### A1-4. Sequencer: Instrument menu-override bitmap (R12)

**File:** `Core/Sequencer/sequencer.c`

#### A1-4a. New static variable — ADD after line 734

```
/*
 * Per-slot Instrument menu-override bitmap (S081 §5.1, R12).
 *
 * What: 6 × uint64_t, one per Instrument slot (0..5). Bit N is set
 * when the user has edited Instrument parameter N from the menu and the
 * edit should suppress that parameter's step automation until the next
 * trigger of that voice (or a transport/Scene change). Why: R12 says
 * "a menu edit of an automated Instrument parameter sounds at once and
 * suppresses that parameter's automation until the voice's next
 * trigger." This replaces the S075 F3 rule where automation always beat
 * the menu. Inputs: set by seq_menuOverrideSet() (called from
 * menu_cellCommitValue). Cleared by seq_restoreAutomatedParameters()
 * (on trigger), and by seq_setStepIndexToStart / seq_selectActivePattern
 * / seq_alignActivePatternToScene / seq_setTrackPlayedScene (on
 * transport/Scene events). Outputs: read by seq_drainPendingAutomation
 * to skip entries. RAM: +48 B SRAM1.
 * Affiliates: seq_automation_dirty[], presetMorph_writeRuntimeBase().
 */
static uint64_t seq_menuOverride[INSTRUMENT_SLOT_COUNT];
```

#### A1-4b. New seq_menuOverrideSet() — ADD

```
/*
 * Set the menu-override bit and clear the held bit for one parameter
 * (S081 §5.1).
 *
 * What: sets bit `local` in seq_menuOverride[slot] and clears the same
 * bit in seq_automation_dirty[slot]. Why: the held bit gates whether
 * presetMorph_writeRuntimeBase() applies the Morph sweep. Clearing it
 * lets the menu edit's endpoint write stand. The override bit then
 * tells the drain to skip that parameter's automation entries.
 * Foreground only — the drain and restore are foreground too, so no
 * PRIMASK. Inputs: slot 0..5, local parameter index. Outputs: override
 * and dirty bitmaps updated. Affiliates: menu_cellCommitValue
 * INSTRUMENT branch, seq_drainPendingAutomation filter,
 * seq_restoreAutomatedParameters clear.
 */
void seq_menuOverrideSet(uint8_t slot, uint8_t local);
```

#### A1-4c. seq_restoreAutomatedParameters — MODIFY at lines 2564–2602

```
/*
 * Restore automated parameters and clear the menu override (S081 §5.8).
 *
 * What: clears seq_menuOverride[slot] first, then iterates the
 * seq_automation_dirty[slot] bitmap, restoring each held parameter to its
 * Morph-interpolated base, then clears seq_automation_dirty[slot]. Runs only
 * for a trigger that requests a reset (voiceControl_triggerNow(reset)) and
 * for leftover reset-only bits. Why: the function returns early when the
 * dirty mask is zero (`if (!mask) return;`), and seq_menuOverrideSet()
 * clears the dirty bit on every menu edit. A clear placed after the dirty
 * clear would therefore be skipped exactly when an override is set, and the
 * override would outlive the trigger. Inputs: trigger_track 0..6 (track 6
 * maps to slot 5). Outputs: override cleared; held parameters restored.
 * Affiliates: voiceControl_processPending(), seq_menuOverrideSet(),
 * seq_drainPendingAutomation().
 */
```

Add `seq_menuOverride[slot] = 0u;` **immediately after `slot` is computed and
before `mask = seq_automation_dirty[slot]; if (!mask) return;`**.

#### A1-4d. seq_drainPendingAutomation — MODIFY at lines 2442–2551

```
/*
 * Skip overridden parameters in the drain (S081 §5.1, R12).
 *
 * What: for each Instrument-parameter entry in the pending buffer,
 * check seq_menuOverride[slot] bit for the parameter's local index.
 * If set, skip that entry — the menu edit takes priority. Why: R12
 * says the menu value suppresses automation until the next trigger.
 * The override bit is cleared by seq_restoreAutomatedParameters at the
 * next trigger, so automation resumes. Inputs: seq_menuOverride[slot].
 * Outputs: entry skipped or applied. Affiliates: seq_menuOverrideSet,
 * seq_restoreAutomatedParameters.
 */
```

In the `instrumentParam_isVoiceParameter(target)` branch (line 2476), after
the slot/local computation, add:
```c
if (seq_menuOverride[slot] & (1ULL << local))
    { i++; continue; }
```

#### A1-4e. Override clear at transport/Scene sites — MODIFY

The whole-array sites add `memset(seq_menuOverride, 0, sizeof(seq_menuOverride));`.
The two per-track sites clear only the affected slot
(`seq_menuOverride[slot] = 0u;`):

| Function | Line | Event |
|---|---|---|
| `seq_setStepIndexToStart()` | 4478 | transport start/stop/reset |
| `seq_selectActivePattern()` | 1386 | direct Scene change |
| `seq_alignActivePatternToScene()` | 1495 | align-to-Scene |
| `seq_setTrackPlayedScene()` | 1159 | per-track Scene reassignment: that slot only (tracks 5 and 6 → slot 5) |
| `seq_realignActivePatternToMasterClock()` | 2719 | double-click realign |
| `seq_realignTrackToMasterClock()` | 2820 | per-track realign: that slot only |

```
/*
 * Clear the menu override at transport/Scene change (S081 §5.1).
 *
 * What: zeroes the entire seq_menuOverride array (or the affected
 * slot for per-track changes). Why: R12 override ends at transport
 * start/stop/reset and at Scene changes. After clearing, step
 * automation resumes on the next drain pass. Affiliates:
 * seq_restoreAutomatedParameters (per-trigger clear),
 * seq_menuOverrideSet (set site).
 */
```

**File:** `Core/Sequencer/sequencer.h`

#### A1-4f. Header declaration — ADD

```
void seq_menuOverrideSet(uint8_t slot, uint8_t local);
```

---

### A1-5. Preset / presetMorphEngine: per-target Scene clears (§5.2)

**File:** `Core/Bank/Scene/Preset/presetManager.c`

#### A1-5a. New preset_clearFxSendStepOverride(slot) — ADD

```
/*
 * Clear a single voice slot's FX-send step-automation override (S081 §5.2).
 *
 * What: clears the per-slot FX-send override so the value returns to
 * the Scene-stored amount. Why: R12/§5.2 says a menu edit of FX send
 * clears that slot's override. Only bulk-clear versions existed before;
 * live record and live erase need per-slot granularity. Inputs: slot
 * 0..5. Outputs: the slot's FX-send override is removed; the runtime
 * reverts to the Scene base. Affiliates: menu_cellCommitValue
 * SCENE_SETTING FX_SEND branch, preset_setFxSendStepOverride (set
 * site), seq_applySceneAutomation FX_SEND branch.
 */
void preset_clearFxSendStepOverride(uint8_t slot);
```

#### A1-5b. New preset_clearAudioOutStepOverride(slot) — ADD

```
/*
 * Clear a single voice slot's audio-out step-automation override and
 * re-apply the Scene-stored route (S081 §5.2).
 *
 * What: clears the per-slot audio-out override and calls
 * preset_applyVoiceAudioOutRuntime(slot, scene_getVoiceAudioOut(A, slot))
 * to restore the Scene base route. Why: unlike FX send, clearing the
 * audio-out override alone would leave the runtime on the last
 * automation value until the next trigger. The explicit re-apply makes
 * the menu edit audible immediately. Inputs: slot 0..5. Outputs:
 * audio-out override cleared, runtime route restored. Affiliates:
 * menu_cellCommitValue SCENE_SETTING AUDIO_OUT branch,
 * preset_setAudioOutStepOverride, preset_applyVoiceAudioOutRuntime,
 * scene_getVoiceAudioOut.
 */
void preset_clearAudioOutStepOverride(uint8_t slot);
```

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.c`

#### A1-5c. New presetMorph_clearTrackParamStepOverride(track, param) — ADD (length, scale, shuffle)

```
/*
 * Clear a single track's length/scale/shuffle step-automation override and
 * refresh the effective cache (S081 §5.2).
 *
 * What: clears the per-track step override for the given param
 * (PRESETMORPH_TRACK_LENGTH, _SCALE or _SHUFFLE) and calls
 * seq_refreshTrackEffectiveParams() to recompute the effective values
 * from the Scene base. Why: when the user edits scale or shuffle from
 * the menu, the override must clear so the edit takes effect (length
 * included: R12 covers every non-Instrument parameter). The
 * effective cache must also update so TIM3 uses the new interval.
 * Inputs: track 0..6, param (PRESETMORPH_TRACK_LENGTH, _SCALE or _SHUFFLE).
 * Outputs: override cleared, effective cache refreshed. Affiliates:
 * presetMorph_setTrackParamStepOverride (set site), menu.c STEP
 * scale/shuffle commit, seq_effectiveTrackScale/Shuffle arrays,
 * seq_refreshTrackEffectiveParams.
 */
void presetMorph_clearTrackParamStepOverride(uint8_t track,
                                              presetMorph_trackParam_t param);
```

**File:** `Core/Bank/Scene/Preset/presetMorphEngine.h`

#### A1-5d. Header declaration — ADD

```
void presetMorph_clearTrackParamStepOverride(uint8_t track,
                                              presetMorph_trackParam_t param);
```

**File:** `Core/Bank/Scene/Preset/presetManager.h`

#### A1-5e. Header declarations — ADD

```
void preset_clearFxSendStepOverride(uint8_t slot);
void preset_clearAudioOutStepOverride(uint8_t slot);
```

---

### A1-6. Sequencer: per-target Scene-automation bit clear

**File:** `Core/Sequencer/sequencer.c`

#### A1-6a. New seq_clearSceneAutomationBit() — ADD

```
/*
 * Clear one bit in the Scene-automation dirty mask (S081 §5.2).
 *
 * What: clears the bit at position `index` in seq_scene_automation_dirty.
 * Why: when the user edits a Scene-target parameter from the menu, the
 * per-target clear (§5.2) must also clear that target's held bit so the
 * restore path does not revert the edit. The existing bulk clear
 * (seq_clearSceneAutomationDirty) clears all bits and is used at Scene
 * changes. This per-bit version serves the R12 menu-edit path. Inputs:
 * index, the bit position from sceneModTarget_indexFromId(). Outputs:
 * one bit cleared. Affiliates: seq_scene_automation_dirty,
 * seq_restoreAllSceneAutomation, the per-target clear functions in
 * preset/presetMorphEngine.
 */
void seq_clearSceneAutomationBit(uint8_t index);
```

**File:** `Core/Sequencer/sequencer.h`

#### A1-6b. Header declaration — ADD

```
void seq_clearSceneAutomationBit(uint8_t index);
```

---

### A1-7. Menu: R12 menu-edit call sites

**File:** `Core/Menu/menu.c`

#### A1-7a. menu_cellCommitValue INSTRUMENT branch — MODIFY at line ~4850

```
/*
 * Instrument menu edit sets the override before applying (S081 §5.1, R12).
 *
 * What: one call, seq_menuOverrideSet(cell->slot, cell->descriptor_index),
 * before the edit-mask Scene loop. It applies to Normal and Morph-image
 * edits (both change the value that sounds). It is not called per Scene:
 * the edit mask is a set of Scenes, while the override belongs to the one
 * runtime slot that the active Scene's edit writes, and the active Scene is
 * always in the commit set. Why: R12 says a menu edit suppresses that
 * parameter's step automation until the voice's next trigger. Setting it
 * before preset_setInstrumentParameter() clears the held bit first, so the
 * guarded runtime write (presetMorph_writeRuntimeBase()) goes through.
 * Inputs: cell slot and descriptor index. Outputs: override bit set, held
 * bit cleared. Affiliates: seq_menuOverrideSet(),
 * preset_setInstrumentParameter(), seq_drainPendingAutomation() filter.
 */
```

#### A1-7b. menu_cellCommitValue SCENE_SETTING branches — MODIFY at line ~4817

For `AUDIO_OUT`, `FX_SEND` branches:

```
/*
 * Scene-setting menu edit clears that slot's override (S081 §5.2).
 *
 * What: after the Scene-setting commit, calls the per-slot clear
 * (preset_clearAudioOutStepOverride or preset_clearFxSendStepOverride)
 * and seq_clearSceneAutomationBit(index). Why: §5.2 says a menu edit
 * of these parameters clears the automation override so the menu value
 * takes effect immediately. Inputs: slot, Scene automation index.
 * Outputs: override cleared, dirty bit cleared. Affiliates:
 * preset_clearAudioOutStepOverride, preset_clearFxSendStepOverride,
 * seq_clearSceneAutomationBit.
 */
```

#### A1-7c. menu_cellCommitValue KIT_SETTING 7dc — MODIFY

```
/*
 * 7dc menu edit clears its step override (S081 §5.2).
 *
 * What: calls instrumentManager_clearSlot6Track7StepDecayOverride()
 * and seq_clearSceneAutomationBit(index). The clear function already
 * exists at InstrumentManager.c:2695. Why: same as A1-7b. Affiliates:
 * instrumentManager_clearSlot6Track7StepDecayOverride.
 */
```

#### A1-7d. STEP length/scale/shuffle commits — MODIFY at lines ~15886/15896/15945 and ~4993–4999

```
/*
 * Track length/scale/shuffle menu edit clears the step override (S081 §5.2).
 *
 * What: after committing the new value (Normal commit in
 * menu_parseGlobalParam(), or the Morph endpoint commit), calls
 * presetMorph_clearTrackParamStepOverride(track, param) for
 * PRESETMORPH_TRACK_LENGTH, _SCALE or _SHUFFLE, and
 * seq_clearSceneAutomationBit(index). Why: R12 covers every non-Instrument
 * parameter, so a length edit clears the length override (targets 405..411)
 * just as scale and shuffle do. The effective cache refresh makes TIM3 use
 * the new value immediately. Inputs: track, param, Scene automation index.
 * Affiliates: presetMorph_clearTrackParamStepOverride(),
 * seq_clearSceneAutomationBit(), seq_refreshTrackEffectiveParams().
 */
```

---

## Phase A2 — Effect Parameter Rules (R13)

§5.3. Removes step markers, adds menuHold, makes Morph lane one-step.

### A2-1. EffectsManager: remove dead owner/marker state

**File:** `Core/DSP/Effects/EffectsManager.c`

#### A2-1a. Remove fields from effects_automation_t — MODIFY at line ~196

Remove: `owner[64]`, `pending_end` (u64), `pending_track` (u8),
`owner_tracks` (u8). ~75 B released.

```
/*
 * Owner and pending-end fields removed (S081 §3.3-3, §5.3, R13).
 *
 * What: under R13, Effect overlays are "set once, held until cleared"
 * with no per-step ownership tracking. The owner array, pending_end
 * mask, pending_track and owner_tracks have no remaining reader.
 * Why: step markers are removed; locks start and end at FX step
 * boundaries, not at pattern-track step boundaries. The 75 B are
 * released per policy (not reused). Affiliates: effects_automation_t
 * static assert, STORAGE_SRAM_MANIFEST.md.
 */
```

#### A2-1b. Remove effects_automationStepBegin — REMOVE at line ~1209

```
/*
 * Removed: effects_automationStepBegin (S081 §5.3, R13).
 *
 * What: this function was called by the drain when it encountered a
 * step-marker entry (SEQ_PENDING_TYPE_FX_STEP_BIT). It tracked which
 * tracks had stepped and published owner state. Why: R13 removes step
 * markers entirely. Effect overlays are now applied as simple last-
 * writer-wins, and locks are managed at FX-step boundaries. Affiliates:
 * seq_drainPendingAutomation (marker branch removed),
 * seq_queueEffectStepMarker (removed).
 */
```

#### A2-1c. Remove effects_automationStepFlush — REMOVE at line ~1258

```
/*
 * Removed: effects_automationStepFlush (S081 §5.3, R13).
 *
 * What: called at the end of seq_drainPendingAutomation to flush the
 * current step's owner state to pending_end. Removed because step
 * markers and pending_end are gone. Affiliates:
 * seq_drainPendingAutomation final call (removed).
 */
```

#### A2-1d. Remove effects_automationPublishOwners — REMOVE

If present, remove for the same reason.

#### A2-1e. Remove held_morph latch — MODIFY

Remove `held_morph`, `held_morph_valid` from `effects_state_t` (~2 B).

```
/*
 * Held Morph latch removed (S081 §5.3, R13).
 *
 * What: the FX Morph lane previously held its value through unlocked
 * steps, using held_morph/held_morph_valid. Under R13, every FX
 * parameter including Morph is one-step: a lock applies at its step
 * start and returns to the menu value at its step end. Why: the held
 * latch contradicted R13's "set once and forgotten" rule. Inputs:
 * formerly set by effects_seqLatchMorph (line 728). Affiliates:
 * effects_seqConsume, effects_seqSelect, effects_service resolution.
 */
```

Remove calls to `effects_seqLatchMorph()` in `effects_seqConsume()` (~line 761)
and `effects_seqSelect()` (~line 829), and the `held_morph` read in
`effects_service()` (~line 1707).

### A2-2. EffectsManager: add menuHold bitmap

**File:** `Core/DSP/Effects/EffectsManager.c`

#### A2-2a. New menuHold state in effects_automation_t — ADD

```
/*
 * Per-parameter menuHold bitmap and Morph menuHold flag (S081 §5.3, R13).
 *
 * What: uint64_t menuHold in effects_automation_t, plus a uint8_t
 * menuHoldMorph flag. Bit N of menuHold is set when the user has edited
 * Effect parameter N from the menu since the last automation of that
 * parameter. menuHoldMorph is the same for the Effect Morph amount.
 * Why: R13 priority is menuHold > lock > overlay > menu. A menu edit
 * clears the overlay and sets menuHold; the next automation (lock start
 * or track overlay) clears menuHold and applies. This keeps the menu
 * value sounding until new automation arrives. Inputs: set by
 * effects_setParameter/effects_setMorphAmount (menu edits). Cleared by
 * effects_applyAutomation/effects_setMorphAutomation (automation),
 * effects_automationReset (Scene change/realign), and lock step start.
 * Outputs: read by effects_service resolution. RAM: +9 B SRAM1.
 * Affiliates: effects_service resolution order, seq_drainPendingAutomation.
 */
uint64_t menuHold;
uint8_t  menuHoldMorph;
```

#### A2-2b. effects_setParameter — MODIFY at line ~581

```
/*
 * Menu edit of an Effect parameter clears its overlay and sets menuHold
 * (S081 §5.3, R13, F-Q).
 *
 * What: when the origin Scene matches the active Scene and the row is
 * in the runtime Effect type, clears the overlay (active bit) for that
 * local parameter and sets menuHold for it. Why: R13 says a menu edit
 * beats both lock and overlay until the next automation. Clearing the
 * overlay ensures the lock can see the menu value when it starts.
 * Setting menuHold prevents the lock from overriding the menu value
 * for the remainder of its step. Inputs: local parameter index, origin
 * Scene. Outputs: overlay cleared, menuHold set. Affiliates:
 * effects_applyAutomation (clears menuHold on next automation),
 * effects_service resolution.
 */
```

#### A2-2c. effects_setMorphAmount — MODIFY at line ~705

Same logic for the Effect Morph amount: clear `morph_override_valid`, set
`menuHoldMorph`.

#### A2-2d. effects_applyAutomation — MODIFY at line ~1276

```
/*
 * Track automation of an Effect parameter clears menuHold (S081 §5.3).
 *
 * What: sets the overlay value and active bit for the local parameter,
 * and clears the menuHold bit. Why: R13 says the next automation of a
 * parameter after a menu edit clears the menu hold and applies. The
 * track argument is unused after owner removal but kept for signature
 * stability. Inputs: owner_track, local, value. Outputs: overlay set,
 * menuHold cleared. Affiliates: seq_drainPendingAutomation Effect
 * branch, effects_service resolution.
 */
```

#### A2-2e. effects_setMorphAutomation — MODIFY at line ~1301

Clear `menuHoldMorph` on track automation of the Morph amount.

#### A2-2f. Lock step start: clear overlay and menuHold — MODIFY

In `effects_seqConsume()` (STEP handler) and `effects_seqSelect()` (`sel`
handler), for each lane locked on the new step:

```
/*
 * Lock step start clears overlay and menuHold (S081 §5.3, R13).
 *
 * What: for each FX lane locked on the new step, clears the overlay
 * bit and the menuHold bit of the mapped Effect parameter. For lane 0
 * (Morph), clears morph_override_valid and menuHoldMorph. Why: R13
 * says a lock beats a track overlay, and a lock start after a menu
 * edit should clear menuHold so the lock value can apply. After the
 * lock step ends, the parameter returns to the menu value (not the
 * overlay). Inputs: lane mask from the FX step. Outputs: overlay and
 * menuHold bits cleared. Affiliates: effects_service resolution
 * (lock > overlay when menuHold not set), live erase lane exclusion
 * (Phase G).
 */
```

#### A2-2g. effects_automationReset — MODIFY at line ~1238

```
/*
 * Reset clears menuHold and erase state (S081 §5.3).
 *
 * What: in addition to clearing overlay active bits, also zeroes
 * menuHold, menuHoldMorph, and the erase lane mask (Phase G). Why:
 * a Scene change or realign resets all Effect parameter state. The
 * existing reset already clears overlays; menuHold must follow.
 * Affiliates: seq_fxPublishReset → seq_drainPendingAutomation →
 * effects_automationReset call chain.
 */
```

#### A2-2h. effects_service resolution — MODIFY at line ~1679

```
/*
 * R13 resolution priority: menuHold > lock > overlay > menu (S081 §5.3).
 *
 * What: for each Effect parameter, the resolution reads menuHold before
 * checking the lock and overlay. If menuHold is set, the menu value is
 * used regardless of lock or overlay state. Otherwise: if a lock is
 * active (and the parameter's lock is not under live erase), the lock
 * value is used; if an overlay is active, the overlay value is used;
 * otherwise the menu value. For Morph: menuHoldMorph > lockMorph
 * (current step) > morph_override > menu. Why: this implements the
 * three-layer priority of R13. Inputs: menuHold, lock state, overlay
 * state. Outputs: resolved parameter value applied to the DSP chain.
 * Affiliates: effects_setParameter (sets menuHold), lock step start
 * (clears menuHold), effects_applyAutomation (clears menuHold).
 */
```

### A2-3. Sequencer: remove step markers

**File:** `Core/Sequencer/sequencer.c`

#### A2-3a. Remove seq_queueEffectStepMarker — REMOVE at lines 1698–1730

```
/*
 * Removed: Effect step markers (S081 §5.3, R13).
 *
 * What: seq_queueEffectStepMarker() queued a marker entry with
 * SEQ_PENDING_TYPE_FX_STEP_BIT for each track step. The drain called
 * effects_automationStepBegin() on these. Under R13, Effect overlays
 * are last-writer-wins with no per-step ownership tracking, so markers
 * are unnecessary. Affiliates: seq_advanceTrackStep caller (removed),
 * SEQ_PENDING_TYPE_FX_STEP_BIT (removed), seq_drainPendingAutomation
 * marker branch (removed).
 */
```

#### A2-3b. Remove the marker call in seq_advanceTrackStep — MODIFY at line ~2302

Remove `seq_queueEffectStepMarker(track, ...)` from the step-allowed block.

#### A2-3c. seq_drainPendingAutomation: remove marker branch — MODIFY at line ~2473

Remove the `if ((identity & SEQ_PENDING_TYPE_FX_STEP_BIT) != 0u)` branch
and the final `effects_automationStepFlush()` call after the drain loop.

```
/*
 * Marker branch and flush removed from drain (S081 §5.3, R13).
 *
 * What: the FX step marker entries and the final flush call are
 * removed. The drain now processes only automation entries
 * (SEQ_PENDING_TYPE_AUTOMATION_BIT). Why: step markers served the
 * per-step ownership model which R13 replaces. Affiliates:
 * effects_automationStepBegin (removed), effects_automationStepFlush
 * (removed).
 */
```

### A2-4. Sequencer: realign also realigns the FX sequence and clears Effect track overlays (§3.3-4)

**File:** `Core/Sequencer/sequencer.c`

`seq_fxPublishReset()` must **not** be used here. It sends an FX RESET event,
which invalidates the current FX step (`effects_seqConsume()` clears
`seq_step_valid`) and leaves the FX accumulator un-seeded. The FX sequence
would drop its locks until its next boundary and stay out of phase. Realign
needs two separate actions instead.

#### A2-4a. New seq_fxRealignToMasterClock() — ADD near seq_fxClockTick() (`:953`)

```
/*
 * Realign the FX sequence to the master clock (S081 §3.3-4, Appendix A-5).
 *
 * What: under PRIMASK, recomputes the FX sequencer position from
 * seq_elapsedPpqTicks with the active Effect's current step scale, using
 * the same mapping as the track realign: master_q8 = elapsed << 8,
 * position = master_q8 / interval, seq_fxAccumulator = master_q8 % interval,
 * seq_fxStepCounter = position + 1. It then publishes the step index for
 * `position` through seq_fxPublishStep() (fwd/rev/pip by the same formula as
 * seq_fxClockTick(); rnd takes a fresh draw). `sel` has no clock and is left
 * alone. Why: seq_fxStepCounter counts boundaries since the transport
 * reset, so after the user changes the FX step scale, the FX sequence drifts
 * out of alignment with the Pattern. Realign must bring it back onto the
 * master grid, not skip it. Publishing a STEP (not a RESET) keeps the FX
 * step valid; EffectsManager treats it as a lock-step start (A2-2f).
 * Inputs: seq_elapsedPpqTicks, the active Effect record (scale, length, run
 * mode). Outputs: FX accumulator, counter and published step. Affiliates:
 * seq_fxClockTick(), seq_fxPublishStep(), effects_seqConsume().
 */
static void seq_fxRealignToMasterClock(void);
```

#### A2-4b. New seq_requestEffectOverlayReset() — ADD next to seq_fxPublishReset() (`:143`)

```
/*
 * Clear Effect track overlays without touching the FX position (S081 R13).
 *
 * What: sets only seq_effectAutomationReset under PRIMASK. The next drain
 * calls effects_automationReset() (overlays, fxm override, menuHold).
 * seq_fxEvent is not touched. Why: R13 says a re-align clears track
 * overlays, but the FX sequence position must survive (A2-4a realigns it).
 * Affiliates: seq_drainPendingAutomation(), effects_automationReset().
 */
static void seq_requestEffectOverlayReset(void);
```

#### A2-4c. Realign sites — MODIFY

At the end of `seq_realignActivePatternToMasterClock()` (`:2719`) and
`seq_realignTrackToMasterClock()` (`:2820`), add:

```c
seq_fxRealignToMasterClock();
seq_requestEffectOverlayReset();
```

The per-track realign realigns the FX sequence too: the FX position depends
only on the master clock, so this is idempotent when it is already aligned,
and it corrects a drift after a scale change.

---

## Phase A3 — Roll Table, qnt trk, Overlay Fixes, PAR_ROLL Retirement

### A3-1. Roll table fix (§5.4, R17)

**File:** `Core/Sequencer/sequencer.c`

#### A3-1a. seq_rollPpq — MODIFY at lines 1841–1850

```
/*
 * Step-roll tick table corrected to 96 PPQ (S081 §5.4, R17, Q11).
 *
 * What: the table values are in 96-PPQ ticks (the TIM3 tick rate).
 * Previously the values were 24-PPQ, making every roll 4× too fast on
 * hardware. New table: {0, 48, 32, 24, 16, 12, 8, 6, 4, 3} for
 * indices 0..9 (off, 1/8, 8T, 16, 16T, 32, 32T, 64, 64T, 128).
 * 48 (was index 7) is dropped (same rate as 32T, F-A). 64T and 128 are
 * added. Guard changed from rate > 8 to rate > 9. Why: the S079 values
 * were 24-PPQ quantities applied to the 96-PPQ tick, so every roll played
 * 4× its label (confirmed on hardware). Every existing stored roll slows to its labelled
 * rate. Stored indices shift with no migration (F-H). Inputs: rate
 * index 0..9. Outputs: ticks between roll hits. Affiliates:
 * seq_processRollTicks, seq_rollBegin, stepRollRateNames (MenuText.h),
 * menu_getRollRateName, STEP roll clamp.
 */
static uint8_t seq_rollPpq(uint8_t rate)
{
    static const uint8_t table[10] = {
        0u, 48u, 32u, 24u, 16u, 12u, 8u, 6u, 4u, 3u
    };
    if (rate > 9u)
        return 0u;
    return table[rate];
}
```

### A3-2. Roll name table and clamp updates

**File:** `Core/Menu/MenuText.h`

#### A3-2a. stepRollRateNames — MODIFY at line 266

```
/*
 * Step-roll rate display names, aligned with the §5.4 table (S081 R17).
 *
 * What: 10 entries plus the count byte. Drop " 48" (was between 32T
 * and  64). Add "64t" and "128". Why: the table indices and names
 * must match the new 10-entry seq_rollPpq table. Stored bytes shift
 * with no migration. Inputs: roll index 0..9. Affiliates:
 * seq_rollPpq, menu_getRollRateName, STEP roll clamp.
 */
```

New value: `{10}, {"off"},{"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 64"},{"64t"},{"128"}`

#### A3-2b. quantisationNames — MODIFY at line 57

```
/*
 * Quantisation display names with trk option (S081 §5.6, R16).
 *
 * What: 6 entries. Adds "trk" after "64". Why: R16 adds track-grid
 * quantisation for recording and looper per-track loop starts. Inputs:
 * quantisation index 0..5. Affiliates: seq_quantisation,
 * QUANT_TRACK enum, seq_setQuantisation clamp, seq_loopEventAnchor.
 */
```

New value: `{6}, {"off"}, {"8"}, {"16"}, {"32"}, {"64"}, {"trk"}`

**File:** `Core/Menu/menu.c`

#### A3-2c. STEP roll clamp — MODIFY at line 5788 (approx `5790`)

Change `*value > 8u` to `*value > 9u`.

```
/*
 * STEP roll clamp extended for 64T and 128 (S081 §5.4).
 *
 * What: the upper bound changes from 8 to 9 to allow the two new roll
 * rates. Inputs: value from encoder. Outputs: clamped to 0..9.
 * Affiliates: seq_rollPpq table, stepRollRateNames.
 */
```

#### A3-2d. menu_getRollRateName — MODIFY at line 9974 (approx `9978`)

Change `value > 8u` guard to `value > 9u`, and `stepRollRateNames[value + 1u]`
indexing stays the same.

### A3-3. Quantisation: QUANT_TRACK

**File:** `Core/Sequencer/sequencer.h`

#### A3-3a. Enum — MODIFY at line 16

```
/*
 * QUANT_TRACK: track-grid quantisation (S081 §5.6, R16).
 *
 * What: adds QUANT_TRACK = 5 after QUANT_64. Recording snaps to the
 * nearest step start of the recorded track's own grid. The looper uses
 * it for per-track loop starts (each looped track's loop start is its
 * own current step start). Why: the existing quantisation options are
 * all master-clock divisions; trk provides a track-relative snap for
 * tracks running at non-standard scales. Affiliates: seq_quantisation,
 * seq_setQuantisation, seq_loopEventAnchor, quantisationNames.
 */
QUANT_TRACK,
```

**File:** `Core/Sequencer/sequencer.c`

#### A3-3b. seq_setQuantisation — MODIFY at lines 4042–4045

Clamp `value` to 0..5 (was implicit no clamp).

```
/*
 * Clamp quantisation to the valid range 0..QUANT_TRACK (S081 §5.6).
 *
 * What: rejects values > 5. Why: the enum now has 6 entries (0..5).
 * An out-of-range value from a corrupt file or MIDI could index past
 * the quantisationNames array. Inputs: value 0..5. Outputs:
 * seq_quantisation set. Affiliates: filesystem.c quantisation parse.
 */
```

#### A3-3c. Per-track loop anchors for `trk` — MODIFY seq_loopSnapshotTracks() (`:3125-3150`); seq_loopEventAnchor() unchanged

```
/*
 * Per-track loop anchor for QUANT_TRACK (S081 §5.6, L1, plan Appendix A-15).
 *
 * What: seq_loopEventAnchor() (:3079) is unchanged. `off` keeps the
 * fastest-track anchor; 1/8..1/64 keep the last grid boundary; `trk` falls
 * into its `default: q = 0` path, but its single return value is not used.
 * Instead, in seq_loopSnapshotTracks(), when seq_quantisation ==
 * QUANT_TRACK, each track in the mask gets its own anchor inside the
 * existing per-track loop:
 *   anchor_t = seq_initialSchedulerTick ? 0
 *            : (now − (seq_trackAccumulator[t] >> 8)), clamped at 0;
 *   back_t   = seq_initialSchedulerTick ? 0 : −(now − anchor_t + 1);
 * then seq_loopShiftPosition(track, &pos, back_t) and the stores into
 * seq_loopAnchor[t] / seq_loopStart*[t] exactly as today. That is each
 * track's own last step start: the same formula `off` applies to the
 * fastest track only.
 * Why: looper quantize exists to catch the step that was sounding at the
 * loop start. With `off`, a slower track can be anchored mid-step and its
 * hit is not caught; `trk` catches every track's own step. Anchors choose
 * only which window [anchor, anchor + L) a track replays. Every track wraps
 * on the same length L, so step timing under the loop, and between tracks,
 * is unchanged.
 * Callers: seq_loopEngage() (:3222) and seq_loopUpdateMask() (:3262, new
 * tracks only), both foreground under PRIMASK as today.
 * seq_loopRefreshSceneSnapshots() keeps anchors (unchanged); a resetting
 * change still moves every anchor to 0.
 * Edge (existing, accepted in S080): a track whose step is longer than the
 * loop is anchored more than one length back and jumps back at the next
 * wrap.
 * Invariants kept: one master-clock anchor per track, snapshot taken
 * before the anchor tick's advance, no shared loop tick counter, every
 * publish under PRIMASK.
 */
```

### A3-4. Overlay defect fixes (R19)

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### A3-4a. BAR LED on mode change — MODIFY in handleModeButtons() (`:1531`)

```
/*
 * BAR overlay LEDs shown only in PERF mode (S081 §5.7, R19).
 *
 * What: when the new mode is not PERF, led_setValue(0u, LED_BAR1) and
 * led_setValue(0u, LED_BAR2). When the new mode is PERF,
 * led_setValue((uint8_t)(bh_overlayActive == 1u), LED_BAR1) and
 * led_setValue((uint8_t)(bh_overlayActive == 2u), LED_BAR2), the same
 * expressions as buttonHandler_loopCheckDisengage() (:1281-1282).
 * bh_overlayActive itself is not changed, so the overlay is still active on
 * PERF re-entry. Why: the toggled BAR LED stayed lit in other modes, where
 * BAR LEDs are only press feedback. Inputs: old/new mode. Outputs: BAR
 * LEDs. Affiliates: bh_overlayActive, BAR1/BAR2 press handlers
 * (:2466-2468, :2501-2503).
 */
```

#### A3-4b. Overlay switch while a VOICE is held — MODIFY in the BAR1/BAR2 PERF branches (`:2466`, `:2501`)

New state: `static uint8_t bh_overlayHeldType;` holds the trigger type of the
held overlay track (0..11, or `0xFF` when none). +1 B.

```
/*
 * BAR overlay switch or toggle-off while a VOICE trigger is held
 * (S081 §5.7, R19, F-B, F-O).
 *
 * What: after a BAR press updates bh_overlayActive, if bh_overlayVoiceMask
 * is non-zero, re-configure the held track bh_overlayTrack from the new
 * overlay's type (PAR_PERF_BTN_LT/RT) and note (PAR_PERF_NOTE_LT/RT):
 *  - overlay toggled off (the same BAR, bh_overlayActive == 0), F-O:
 *    bh_overlayCount = 0 (repeats stop, no extra hit);
 *    liveRec_rollRelease(track) ends the recorded roll at that step.
 *  - roll → roll: bh_overlayInterval = the new division (same rollDiv[] and
 *    r6t triplet rule as buttonHandler_overlayTrigger());
 *    bh_overlayNext = previous hit time + new interval, so the next hit
 *    follows the new rate; liveRec_rollRateChange(track, newIndex).
 *  - single/flam → roll: bh_overlayCount = 0xFF,
 *    bh_overlayNext = now + new interval (no hit at the switch itself; the
 *    first new hit is one interval later); liveRec_rollRateChange().
 *  - roll → single/flam: bh_overlayCount = 0 (no extra hit);
 *    liveRec_rollRateChange(track, 0) ends the recorded roll.
 *  - single → single: nothing.
 * bh_overlayHeldType is updated. The note follows the new overlay.
 * Why: previously the type switched but the held trigger kept the old
 * timing. Inputs: bh_overlayVoiceMask, bh_overlayTrack, bh_overlayHeldType,
 * new overlay parameters. Outputs: bh_overlay* repeat state, LiveRecord
 * notifications (no-ops until Phase E). Affiliates:
 * buttonHandler_overlayTrigger(), buttonHandler_overlayTick(),
 * buttonHandler_overlayRelease().
 */
```

### A3-5. Legacy roll retirement

**File:** `Core/Sequencer/sequencer.c`

#### A3-5a. Remove seq_rollRate, seq_rollState globals — REMOVE at lines 170–171

```
/*
 * Removed: legacy roll state (S081 §3.2).
 *
 * What: seq_rollRate and seq_rollState were the PAR_ROLL-driven
 * performance roll mechanism from the pre-S079 era. They have no
 * remaining callers. Removing them releases 2 B SRAM1. Affiliates:
 * seq_setRoll (removed), seq_setRollRate (removed), the legacy roll
 * block in seq_advanceTrackStep (removed).
 */
```

#### A3-5b. Remove legacy roll block in seq_advanceTrackStep — REMOVE at lines 2310–2317

#### A3-5c. Remove seq_setRoll — REMOVE at lines 4169–4184

#### A3-5d. Remove seq_setRollRate — REMOVE at lines 4186–4263

#### A3-5e. PAR_ROLL retirement in menu.c — MODIFY

- `menuPages.h:159`: remove PAR_ROLL from the Euklid cell.
- `menu.c:16077`: remove the parse case for PAR_ROLL.
- `menu.c:16784`: remove the default-value entry.
- `menu.c:1101`: remove the dtype entry.
- `MenuText.h:101`: remove `rollRateNames` and `MENU_ROLL_RATES` if no
  other user.

```
/*
 * PAR_ROLL retired in place (S081 §3.2, Q14).
 *
 * What: PAR_ROLL was the performance-roll parameter bound to the legacy
 * seq_setRoll/seq_setRollRate mechanism. It is removed from the Euklid
 * menu cell, the parse/default/dtype tables, and the name table. The
 * parameter ID is not reused. Why: the performance roll is superseded
 * by the overlay trigger system (S080) and step-roll recording (S081).
 * Affiliates: seq_setRoll (removed), seq_setRollRate (removed),
 * rollRateNames (removed).
 */
```

**File:** `Core/Sequencer/sequencer.h`

#### A3-5f. Remove declarations — REMOVE

Remove `extern` declarations for `seq_rollRate`, `seq_rollState`, and function
declarations for `seq_setRoll`, `seq_setRollRate`.

---

## Phase B — LiveRecord Skeleton, REC Gestures, Gates, Removals

### B-1. LiveRecord module: new files

**File:** `Core/Sequencer/LiveRecord.c` — ADD (new file)

```
/*
 * LiveRecord — live recording and erasing engine (S081 §4).
 *
 * What: captures overlay triggers, MIDI notes and knob automation into
 * Pattern steps on the recording track (rtk). Manages mode (OFF/LATCH/
 * OVERDUB/ERASE), target-Scene negotiation, latch/dirty/erase bitmaps,
 * placement and quantisation, flam pairing, roll span tracking, and
 * FX-sequence erase fan-out. Why: the old recording path
 * (seq_recordTrigger, seq_quantize) was a trigger-bit-only bridge from
 * the fixed-grid era with no note, velocity, timing, roll or automation
 * recording. This module implements the full live-record feature set.
 * Inputs: liveRec_triggerIn (triggers), liveRec_paramIn (automation
 * gestures), liveRec_erase* (erase gestures), mode setters (from
 * buttonHandler). Outputs: patSvc_recordStep, patSvc_writeStepAutomation,
 * patSvc_removeStepAutomation, seq_suppressStepOnce, seq_setRecordTarget,
 * seq_setAutomationFilter, effects_clearSeqLanes. Does not call: Menu,
 * buttonHandler, MidiParser. The Sequencer does not call LiveRecord.
 * RAM: ~220 B SRAM1 in every build (bitmaps 192, state ~16, roll
 * capture ~8, flam grace ~4) plus ~16 B of drop counters only when
 * DEV_MODE_LOGGING is enabled (logging-only allocation policy). The 34 B
 * staging table belongs to PatternStackService (D-4a), not to this file.
 * Service: 500 Hz foreground pass (liveRec_service) before patSvc_tick().
 */
```

**File:** `Core/Sequencer/LiveRecord.h` — ADD (new file)

Public API per §4.2 — all declarations with comment blocks:

```c
void    liveRec_init(uint8_t activeTrack);
void    liveRec_service(void);

/* Mode */
void    liveRec_setLatch(uint8_t on);
void    liveRec_overdubBegin(void);
void    liveRec_overdubEnd(void);
void    liveRec_forceOff(void);
void    liveRec_eraseBegin(void);
void    liveRec_eraseEnd(void);
uint8_t liveRec_mode(void);
uint8_t liveRec_armed(void);
uint8_t liveRec_patternWriting(void);

/* Recording track */
void    liveRec_onActiveTrackChanged(uint8_t track);
void    liveRec_setRecordTrack(uint8_t track);
uint8_t liveRec_recordTrack(void);
uint8_t liveRec_activePatternLocked(void);

/* Triggers */
void    liveRec_triggerIn(const liveRec_trigger_t *t);
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex);
void    liveRec_rollRelease(uint8_t track);

/* Automation */
void    liveRec_paramIn(uint16_t target9, uint8_t source);

/* Erase */
void    liveRec_eraseParam(uint16_t target9, uint8_t fxLane);
void    liveRec_eraseSeqHeld(uint8_t held);
void    liveRec_eraseTrackHeld(uint8_t track, uint8_t held);
```

Each function carries the full comment-block description from §4.2 and §19.2.
The `liveRec_trigger_t` struct is also declared here, with `kind` one of
`LIVEREC_HIT_SINGLE`, `LIVEREC_HIT_FLAM_GRACE`, `LIVEREC_HIT_FLAM_MAIN`,
`LIVEREC_HIT_ROLL_START`, `LIVEREC_HIT_ROLL_HIT` (E-1).

In Phase B the bodies of the trigger, automation and erase entry points are
empty stubs that return. Phases E, F and G fill them. The mode, `rtk` and
gate functions are complete in Phase B.

### B-2. REC button handler rewrite

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### B-2a. REC press handler — MODIFY at lines 2377–2408

```
/*
 * REC press: start hold timer for overdub detection (S081 §6, §19.7).
 *
 * What: on REC press, if SHIFT is held → open the settings menu (unchanged);
 * if the settings menu is open → exit it (unchanged); if looping or
 * erase is enabled → ignore (looping: P1 interim lockout, lifted in P2);
 * otherwise start the hold timer
 * (bh_recPressTick = current tick, bh_recArmed = 1). The press no
 * longer toggles recording. Why: the toggle moves to release, and a
 * hold past DOUBLE_CLICK_TIMEOUT enters overdub. This matches the fill
 * hold pattern (bh_fillArmed/bh_fillPressTick). Inputs: REC press
 * event. Outputs: hold timer started or menu toggled. Affiliates:
 * buttonHandler_tick (overdub threshold), DOUBLE_CLICK_TIMEOUT,
 * liveRec_setLatch/liveRec_overdubBegin.
 */
```

#### B-2b. New state variables — ADD after line ~155

```
/*
 * REC hold timer state (S081 §6).
 *
 * What: bh_recPressTick records time_sysTick at the REC press; bh_recArmed
 * is 1 while waiting for the hold threshold. The elapsed time is computed
 * as (uint16_t)(time_sysTick - bh_recPressTick), exactly like the fill hold
 * (:972-973). Why: time_sysTick is a volatile uint16_t (timebase.h:72), so
 * a 16-bit store and a wrap-safe 16-bit subtraction are required; a 32-bit
 * copy would compare wrongly after the counter wraps. Inputs: set on REC
 * press, cleared on release or threshold. Outputs: read by
 * buttonHandler_tick. RAM: +3 B. Affiliates: bh_fillArmed/bh_fillPressTick.
 */
static uint16_t bh_recPressTick;
static uint8_t  bh_recArmed;
```

#### B-2c. buttonHandler_tick: overdub threshold — MODIFY

```
/*
 * REC hold past DOUBLE_CLICK_TIMEOUT enters overdub (S081 §6).
 *
 * What: if bh_recArmed and the current tick minus bh_recPressTick
 * exceeds DOUBLE_CLICK_TIMEOUT, call liveRec_overdubBegin() and
 * led_pulseLed(LED_REC). Clear bh_recArmed. Why: overdub is a held
 * gesture, not a toggle. The hold delay matches the fill delay for
 * consistency. Inputs: bh_recPressTick, current tick. Outputs:
 * overdub mode entered, REC LED pulsed. Affiliates:
 * liveRec_overdubBegin, led_pulseLed.
 */
```

#### B-2d. REC release handler — MODIFY

```
/*
 * REC release: toggle latch or end overdub (S081 §6).
 *
 * What: if bh_recArmed (short press): toggle liveRec_setLatch(), update
 * REC LED. If an overdub was in progress (threshold passed): call
 * liveRec_overdubEnd(), led_pulseLed(LED_REC), restore prior state.
 * Clear bh_recArmed. Why: the toggle moves to release so the hold can
 * be detected first. A short press toggles latch mode; a long press
 * was overdub and exits it on release. Inputs: REC release event.
 * Outputs: mode changed, LED updated. Affiliates: liveRec_setLatch,
 * liveRec_overdubEnd, led_pulseLed.
 */
```

#### B-2e. Remove bh_state.seqRecording — MODIFY at line ~158

`bh_state.seqErasing` stays until Phase G (G-5), together with the old erase
path.

```
/*
 * Removed: bh_state.seqRecording (S081 §19.7).
 *
 * What: the button handler's private copy of the record state. Readers now
 * query LiveRecord. Why: the record state is owned by LiveRecord.
 * Affiliates: liveRec_mode(), liveRec_armed().
 */
```

Every use, at base `02553df`:

| Line | Use | Change |
|---|---|---|
| `:1846-1847` | looper engage clears it | replaced by B-4a (`liveRec_forceOff()` before engage) |
| `:1963` | SHIFT+REC menu exit restores the REC LED | `led_setBlinkLed(LED_REC, 0)`, then `led_setValue((uint8_t)(liveRec_mode() == LIVEREC_MODE_LATCH), LED_REC)` |
| `:2404-2406` | REC toggle | replaced by B-2a/B-2d |
| `:2420` | SHIFT+COPY erase condition | `liveRec_armed() && bh_state.seqRunning` until G-5 replaces the whole erase path |

#### B-2f. REC LED flash while the SHIFT+REC menu is open and armed — MODIFY buttonHandler_shiftRecMenuEnter() (`:1943`)

```
/*
 * REC LED flashes while the settings menu is open and armed (S081 §6).
 *
 * What: if liveRec_armed(), led_setBlinkLed(LED_REC, 1) (continuous: the
 * user's "flash") instead of the plain on. buttonHandler_shiftRecMenuExit()
 * stops it and restores the record state (B-2e, :1963). Why: with the menu
 * latched, REC closes the menu instead of toggling record, so the flashing
 * LED shows that record is still armed. Affiliates: led_setBlinkLed(),
 * buttonHandler_shiftRecMenuExit().
 */
```

### B-3. Erase chord handler — moved to Phase G (G-5)

The old SHIFT+COPY erase (`seq_eraseActive`, `seq_setErasingMode()`,
`bh_state.seqErasing`) keeps working through Phase B–F, gated by
`liveRec_armed() && bh_state.seqRunning` (B-2e). G-5 replaces the chord,
the TIM3 erase branch and the removals in one phase, so no intermediate build
loses erase or fails to compile.

### B-4. Looper record-off move (§3.3-9) — P1 interim

P1 keeps the S080 rule that looping turns record off. `S081_P2_LOOP_WHILE_RECORD.md` removes it (L-2).

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### B-4a. Before seq_loopEngage calls — MODIFY at line ~1840

```
/*
 * Record forced off before looper engage (S081 §3.3-9, §19.7).
 *
 * What: before every seq_loopEngage() call, call liveRec_forceOff()
 * and turn off the REC LED. The old seq_setRecordingMode(0) call
 * inside seq_loopEngage() (sequencer.c:3214) is removed. Why: the
 * Sequencer must not call back into LiveRecord (dependency direction).
 * Moving the record-off to the caller keeps the dependency one-way:
 * buttonHandler → LiveRecord → Sequencer. Inputs: looper engage
 * request. Outputs: recording stopped, REC LED off. Affiliates:
 * liveRec_forceOff, seq_loopEngage.
 */
```

**File:** `Core/Sequencer/sequencer.c`

#### B-4b. Remove record-off from seq_loopEngage — MODIFY at line 3214

Remove `if (seq_recordActive) seq_setRecordingMode(0u);` from
`seq_loopEngage()`.

### B-5. Recording track (rtk) and RECORDING_PAGE

**File:** `Core/Menu/menu.c`

#### B-5a. menu_setActiveVoice: rtk follow — MODIFY at line ~16362

```
/*
 * Active-track change updates rtk (S081 §11, §19.6).
 *
 * What: inside the != branch of menu_setActiveVoice(), calls
 * liveRec_onActiveTrackChanged(v). Why: R7 says rtk follows every
 * active-track change. Re-selecting the same track does not reset
 * an rtk override. Each change is a new take (latches reset).
 * Inputs: new active track v. Outputs: rtk updated in LiveRecord.
 * Affiliates: liveRec_onActiveTrackChanged, liveRec_setRecordTrack.
 */
```

**File:** `Core/Menu/menuPages.h`

#### B-5b. RECORDING_PAGE sub-page 2 — MODIFY at lines 185–214

```
/*
 * rtk cell on RECORDING_PAGE sub-page 2 (S081 §11, §19.6).
 *
 * What: adds PAR_AUTOM_TRACK to sub-page 2, cell 0. The dtype changes
 * from DTYPE_0b1 to a 0..6 value shown as 1..7 (displayed with a
 * static-param name special case). The cell text is "RecTrk". Why: the
 * recording track must be user-editable from the SHIFT+REC menu.
 * Commit → liveRec_setRecordTrack(); display mirrors
 * liveRec_recordTrack(). Inputs: encoder input. Outputs: rtk changed,
 * new take started. Affiliates: PAR_AUTOM_TRACK (ParameterArray.h:103),
 * liveRec_setRecordTrack.
 */
```

#### B-5c. `rtk` cell definition and commit — MODIFY `menu.c`, `MenuText.h`

```
/*
 * rtk cell plumbing (S081 §11, §19.6).
 *
 * What:
 *  - dtype: menu.c:1144 `[PAR_AUTOM_TRACK] = DTYPE_0b1` becomes
 *    DTYPE_0B127; menu_clampCellValue() clamps PAR_AUTOM_TRACK to 0..6.
 *  - text: reuse TEXT_AUTOMATION_TRACK (menu.h:125); its short/long names in
 *    MenuText.h become `rtk` / `RecTrk`.
 *  - display: a static-param name special case (as for the S080 cells)
 *    prints liveRec_recordTrack() + 1 (1..7). It reads LiveRecord directly,
 *    so there is no parameter_values[] mirror to keep in sync when rtk
 *    follows the active track.
 *  - commit: the default static commit (menu_cellCommitValue tail,
 *    ~:5005) calls menu_sendEditedParameter(); add a PAR_AUTOM_TRACK case in
 *    menu_parseGlobalParam() that calls liveRec_setRecordTrack(value).
 *  - settings.cfg: exclude PAR_AUTOM_TRACK from the RECORDING_PAGE
 *    "mark settings dirty" test (~:5026). rtk is runtime only, so a change
 *    must not schedule a settings write.
 * Why: the cell existed only as a dtype entry; it needs a value range,
 * display, commit and no persistence. Affiliates: liveRec_setRecordTrack(),
 * liveRec_recordTrack(), PAR_AUTOM_TRACK (ParameterArray.h:103).
 */
```

### B-6. Gate moves

**File:** `Core/Menu/CopyClear/copyClearSession.c`

#### B-6a. copyClear_copyPressed refusal — MODIFY at line ~584

```
/*
 * Copy refusal uses liveRec_armed (S081 §19.10, F-N).
 *
 * What: in Phase B the guard `seq_recordActive || seq_eraseActive`
 * becomes `liveRec_armed() || seq_eraseActive`; G-5 drops the
 * seq_eraseActive term when it removes that flag. Why: the recording state
 * is now owned by LiveRecord. COPY stays refused while
 * armed (including while stopped), by decision F-N. Inputs:
 * liveRec_armed(). Outputs: copy refused. Affiliates: liveRec_armed.
 */
```

**File:** `Core/Hardware/SD/filesystem.c`

#### B-6b. AutoSave gates — MODIFY at lines 24704 and 25066

```
/*
 * Pattern AutoSave gates use liveRec_patternWriting (S081 §19.11, F-M).
 *
 * What: in Phase B the gates `seq_recordActive || seq_eraseActive` become
 * `liveRec_patternWriting() || seq_eraseActive`; G-5 drops the
 * seq_eraseActive term. Why: the AutoSave drain must not copy the
 * live Pattern region while LiveRecord can still modify it.
 * liveRec_patternWriting() returns true when armed AND the transport is
 * running, which is when writes actually happen. While armed and
 * stopped, AutoSave can run safely. Inputs: liveRec_patternWriting().
 * Outputs: AutoSave deferred or allowed. Affiliates:
 * liveRec_patternWriting, seq_recordActive (stays as the published
 * writing flag, written by LiveRecord).
 */
```

### B-7. Sequencer removals

**File:** `Core/Sequencer/sequencer.c`

#### B-7a. Remove seq_recordTrigger — REMOVE at lines 4308–4349; MidiParser call site — MODIFY at `MidiParser.c:1550`

```
/*
 * Removed: seq_recordTrigger (S081 §3.2).
 *
 * What: this function recorded a trigger-only bit onto bar 1 using the
 * legacy 128-sub-step quantizer. It is replaced by LiveRecord's trigger
 * capture (liveRec_triggerIn) with full note, velocity, timing and roll
 * recording. Affiliates: liveRec_triggerIn, pat_recordStep (Phase D).
 */
```

Its only remaining callers change in the same phase so Phase B builds:

- `MidiParser.c:1550`: `seq_recordTrigger(voice)` becomes
  `liveRec_triggerIn()` with `kind = LIVEREC_HIT_SINGLE`,
  `source = LIVEREC_SRC_MIDI`, the note and velocity (formerly E-3a). The
  call is a stub until Phase E, so MIDI note recording is inactive from B
  to E. That is accepted: the old path only recorded bar 1.
- `sequencer.c:2315` (legacy roll block) and `:4179` (`seq_setRoll()`) were
  removed in A3-5b/A3-5c.

#### B-7b. Remove seq_quantize — REMOVE at lines 4268–4305

```
/*
 * Removed: seq_quantize (S081 §3.2).
 *
 * What: the legacy quantizer using the retired 128-sub-step bar. It is
 * replaced by seq_quantiseTick (Phase C) which operates on the master
 * PPQ clock. Affiliates: seq_quantiseTick, liveRec placement (§8.3).
 */
```

#### B-7c. Remove seq_setRecordingMode — REMOVE at lines 4352–4367

```
/*
 * Removed: seq_setRecordingMode (S081 §3.2).
 *
 * What: the old mode setter that guarded against per-track-active.
 * LiveRecord writes seq_recordActive directly through
 * seq_setRecordTarget(). Affiliates: liveRec_setLatch,
 * seq_setRecordTarget.
 */
```

#### B-7d. seq_setErasingMode and seq_eraseActive — moved to Phase G (G-5)

They stay until the TIM3 erase branch is rewritten, so every phase builds and
erase keeps working.

**File:** `Core/Sequencer/sequencer.h`

#### B-7f. Remove header declarations — REMOVE

Remove the declarations for `seq_setRecordingMode` and `seq_recordTrigger`.
The `seq_setRoll`/`seq_setRollRate` declarations went in A3-5f;
`seq_eraseActive` and `seq_setErasingMode` go in G-5.

### B-8. LED handler cleanup

**File:** `Core/Hardware/frontPanel/ledHandler.c`

#### B-8a. Remove SEQ_LED_DIRTY_REC_SUB — REMOVE at lines 1391–1404

```
/*
 * Removed: SEQ_LED_DIRTY_REC_SUB endpoint (S081 §19.8).
 *
 * What: the led_updateRecordedSubStep handler and the recordSubStep
 * field in SeqLedState. This endpoint had no writer (led_updateRecordedSubStep
 * was a compatibility no-op). The absolute-step recordMainStep stays and is
 * updated by LiveRecord. Affiliates: SEQ_LED_DIRTY_REC_MAIN (kept).
 */
```

**File:** `Core/Hardware/frontPanel/ledHandler.h`

#### B-8b. Remove SEQ_LED_DIRTY_REC_SUB define — REMOVE at line 215

Remove `#define SEQ_LED_DIRTY_REC_SUB` and the `recordSubStep` field from
`SeqLedState`.

#### B-8c. Recorded-step LED uses the absolute step — MODIFY `ledHandler.c:1747`, `led_updateRecordedMainStep()`; ADD `seq_markRecordedStep()`

```
/*
 * Recorded-step feedback by absolute step (S081 §12, §19.8).
 *
 * What: seq_ledState.recordMainStep now holds the absolute step 0..127.
 * led_updateRecordedMainStep(activeTrack, shownPattern, step) draws STEP
 * (step % 16) only when step / 16 == menu_currentBar and the recorded
 * track and Scene are the visible ones; otherwise it does nothing. The
 * value is published by new seq_markRecordedStep(track, scene, step)
 * (Sequencer owns seq_ledState), which sets the field and
 * SEQ_LED_DIRTY_REC_MAIN when track == the active voice and
 * scene == the viewed pattern. LiveRecord calls it after each successful
 * trigger write. Why: the old writer passed step % 16, so a bar-3 recording
 * lit a bar-1 LED. Affiliates: led_processSeqLedState(), LiveRecord (E-1).
 */
void seq_markRecordedStep(uint8_t track, uint8_t scene, uint8_t step);
```

### B-9. Init and service wiring

**File:** `Core/Hardware/timebase.c`

#### B-9a. liveRec_service call — ADD before line 201

```
/*
 * LiveRecord service runs before patSvc_tick (S081 §19.12).
 *
 * What: calls liveRec_service() immediately before patSvc_tick() in the
 * 500 Hz foreground pass. Why: LiveRecord's step-end writes and erase
 * ops must be submitted to the Pattern Stack Service before
 * patSvc_tick() drains the queue, so that idle-direct execution can
 * process them in the same pass. Inputs: none. Outputs: LiveRecord
 * state serviced, patSvc ops submitted. Affiliates: patSvc_tick,
 * liveRec_service.
 */
liveRec_service();
```

**File:** `main.c`

#### B-9b. liveRec_init call — ADD after line ~1299

```
/*
 * LiveRecord init at boot (S081 §19.12).
 *
 * What: calls liveRec_init(menu_getActiveVoice()) after copyClear_init().
 * Why: initialises rtk to the boot-time active track, clears all
 * bitmaps and state. Must run after the menu and Pattern systems are
 * initialised. Inputs: initial active track. Outputs: LiveRecord ready.
 * Affiliates: liveRec_init, menu_getActiveVoice, copyClear_init.
 */
liveRec_init(menu_getActiveVoice());
```

---

## Phase C — Sequencer Services

Capture, projection, quantiser, ring, echo/live-hold, µs stamp, epoch,
filter registration. These are the Sequencer-side APIs that LiveRecord calls
(LiveRecord → Sequencer direction).

### C-1. Step-boundary ring

**File:** `Core/Sequencer/sequencer.c`

#### C-1a. Ring state — ADD

```
/*
 * Step-boundary ring for LiveRecord (S081 §19.1 item 1).
 *
 * What: a 16-entry ring of uint16_t. Each entry packs: step (7 bits),
 * track (3 bits), fired (1 bit), plus 5 spare. TIM3 writes (producer);
 * LiveRecord drains (consumer) via seq_takeRecBoundary(). Why: LiveRecord
 * runs in the foreground and cannot read TIM3 state directly. The ring
 * communicates step boundaries so LiveRecord can perform its step-end
 * table (write/clear/re-stamp) at each step of rtk. A full ring drops
 * the entry and increments a drop counter. Inputs: written by
 * seq_advanceTrackStep when seq_recWriting is set. Outputs: read by
 * liveRec_service → seq_takeRecBoundary. RAM: +34 B SRAM1 (16 × 2 B
 * entries + producer byte + consumer byte). Affiliates:
 * seq_advanceTrackStep, liveRec_service.
 */
static uint16_t seq_recRing[16];
static uint8_t  seq_recRingHead;
static uint8_t  seq_recRingTail;
```

#### C-1b. seq_takeRecBoundary — ADD

```
/*
 * Pop one step-boundary entry from the ring (S081 §19.1).
 *
 * What: if the ring is non-empty, copies the tail entry into the output
 * struct and advances the tail. Returns 1 on success, 0 if empty.
 * Why: single-producer (TIM3) single-consumer (foreground) with byte
 * head/tail needs no PRIMASK. LiveRecord calls this in its service loop.
 * Inputs: output pointer. Outputs: step, track, fired. Affiliates:
 * liveRec_service, seq_advanceTrackStep ring push.
 */
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out);
```

### C-2. Published record target

**File:** `Core/Sequencer/sequencer.c`

#### C-2a. Target state — ADD

```
/*
 * Published recording target (S081 §19.1 item 1).
 *
 * What: seq_recTrack (the track being recorded) and seq_recWriting
 * (nonzero when LiveRecord is armed and running). Written by LiveRecord
 * through seq_setRecordTarget(). TIM3 reads both to decide ring pushes,
 * echo suppression, and live-hold filtering. Why: TIM3 must know which
 * track is being recorded and whether recording is active, but cannot
 * call LiveRecord. These are single-byte stores, atomic on Cortex-M7.
 * Inputs: set by seq_setRecordTarget. Outputs: read by TIM3 in
 * seq_advanceTrackStep. RAM: +2 B. Affiliates: liveRec_service,
 * seq_recRing, seq_echoStep, seq_liveHoldMask.
 */
static uint8_t seq_recTrack;
static uint8_t seq_recWriting;
```

#### C-2b. seq_setRecordTarget — ADD

```
void seq_setRecordTarget(uint8_t track, uint8_t writing);
```

Also stores `seq_recordActive = writing`. `seq_recordActive` stays as the
published "Pattern writing" flag. Its former filesystem and copy/clear
readers moved to LiveRecord queries in B-6.

### C-3. Echo suppression and live hold

**File:** `Core/Sequencer/sequencer.c`

#### C-3a. Echo state — ADD

```
/*
 * Echo-suppression step per track (S081 §8.6, §19.1 item 12).
 *
 * What: seq_echoStep[track] is the step index (0..127) of a recorded
 * trigger that is ahead of the playhead in the current pass, or −1 if
 * no suppression is pending. TIM3 reads it: when the playhead reaches
 * that step, the trigger is skipped once (fired bit still set for
 * automation and ring purposes). The mark expires at the next pass, a
 * Scene change, transport stop, or a length/run-mode change. Why:
 * without echo suppression, a trigger recorded ahead of the playhead
 * would play back within the same bar, causing a double hit. Inputs:
 * set by seq_suppressStepOnce() from LiveRecord. Cleared by TIM3 on
 * consumption. Outputs: trigger skipped once. RAM: +7 B.
 * Affiliates: seq_advanceTrackStep, liveRec capture (Phase E).
 */
static int8_t seq_echoStep[NUM_TRACKS];
```

#### C-3b. Live-hold mask — ADD

```
/*
 * Live-hold mask for overlay roll suppression (S081 §8.8, F-P, §19.1).
 *
 * What: bit N set means track N has an overlay trigger held and
 * recording is active. TIM3 skips stored triggers for that track while
 * the bit is set (automation still plays). Why: while the user holds a
 * VOICE for a recorded roll, the stored triggers on that track should
 * not double-fire alongside the live roll. Inputs: set/cleared by
 * seq_setLiveHold(track, on) from LiveRecord via buttonHandler.
 * Outputs: read by TIM3 in seq_advanceTrackStep trigger branch.
 * RAM: +1 B. Affiliates: buttonHandler overlay hold, liveRec_triggerIn.
 */
static uint8_t seq_liveHoldMask;
```

#### C-3c. API functions — ADD

```
void seq_suppressStepOnce(uint8_t track, uint8_t step);
void seq_setLiveHold(uint8_t track, uint8_t on);
void seq_cancelPendingTrigger(uint8_t track);
```

`seq_cancelPendingTrigger` clears `seq_trackShufflePending[track]`.

### C-4. Erase masks

**File:** `Core/Sequencer/sequencer.c`

#### C-4a. Erase mask state — ADD

```
/*
 * Per-track erase masks (S081 §10, §19.1 item 13).
 *
 * What: seq_eraseSpecialsMask (bit per track): clear bit 15 + specials,
 * keep automation. seq_eraseFullMask (bit per track): clear everything
 * including automation. Set by seq_setEraseMasks() from LiveRecord.
 * TIM3 reads them in seq_advanceTrackStep. Why: live erase has two
 * modes — SEQ hold erases triggers/specials (automation plays),
 * TRACK hold erases everything. The masks let TIM3 apply the right
 * erase without calling LiveRecord. Inputs: set by seq_setEraseMasks.
 * Outputs: read by seq_advanceTrackStep erase branches. RAM: +2 B.
 * Affiliates: liveRec_eraseSeqHeld, liveRec_eraseTrackHeld.
 */
static uint8_t seq_eraseSpecialsMask;
static uint8_t seq_eraseFullMask;
```

#### C-4b. seq_setEraseMasks — ADD

```
void seq_setEraseMasks(uint8_t specials, uint8_t full);
```

### C-5. PPQ microsecond stamp

**File:** `Core/Sequencer/sequencer.c`

#### C-5a. Stamp state — ADD

```
/*
 * PPQ tick microsecond stamp and period (S081 §19.1 item 11, Q3).
 *
 * What: seq_ppqStampUs is the timebase_tim2Now() value at the head of
 * seq_processSchedulerTick(). seq_ppqPeriodUs is the interval since
 * the previous tick. Why: LiveRecord needs sub-tick timing for
 * placement: the trigger's µs timestamp minus seq_ppqStampUs gives the
 * fraction of the current tick. Inputs: written by
 * seq_processSchedulerTick at each tick. Outputs: read by
 * seq_captureTrackTime(). RAM: +8 B. Affiliates:
 * timebase_tim2Now, liveRec placement (§8.3).
 */
static uint32_t seq_ppqStampUs;
static uint32_t seq_ppqPeriodUs;
```

#### C-5b. Write in seq_processSchedulerTick — MODIFY at line ~3789

Add at the head of the function body:
```c
{
    uint32_t now = timebase_tim2Now();
    seq_ppqPeriodUs = now - seq_ppqStampUs;
    seq_ppqStampUs = now;
}
```

### C-6. Playback epoch

**File:** `Core/Sequencer/sequencer.c`

#### C-6a. Epoch counter — ADD

```
/*
 * Playback epoch counter (S081 §19.1 item 6).
 *
 * What: uint8_t seq_playbackEpoch, incremented by every Scene change,
 * realign, transport start/stop, and pattern load. LiveRecord polls it
 * in liveRec_service() to detect take boundaries. Why: LiveRecord must
 * reset latches, discard pending work and re-evaluate the target Scene
 * whenever the playback context changes. A single counter is simpler
 * than tracking each event type individually. Wraps at 255. Inputs:
 * incremented at the sites listed in A1-4e plus seq_setRunning.
 * Outputs: read by liveRec_service. RAM: +1 B. Affiliates:
 * liveRec_service take boundary check, seq_setStepIndexToStart,
 * seq_selectActivePattern, seq_alignActivePatternToScene.
 */
static uint8_t seq_playbackEpoch;
```

Add `seq_playbackEpoch++` at the same sites that clear `seq_menuOverride` (A1-4e) plus `seq_setRunning()`.

#### C-6b. seq_getPlaybackEpoch — ADD

```
uint8_t seq_getPlaybackEpoch(void);
```

### C-7. Automation filter registration

**File:** `Core/Sequencer/sequencer.c`

#### C-7a. Filter state — ADD

```
/*
 * Automation drain filter registration (S081 §19.1 item 2).
 *
 * What: pointers to the LiveRecord latch and erase bitmaps, plus the
 * erase track. seq_drainPendingAutomation reads these to skip entries
 * whose targets are latched or being erased. Why: the Sequencer cannot
 * call LiveRecord (dependency direction). Instead, LiveRecord registers
 * its bitmaps and the drain reads them directly. When LiveRecord is
 * off, the pointers are NULL and the drain runs unfiltered. Inputs:
 * set by seq_setAutomationFilter() from LiveRecord. Outputs: read by
 * seq_drainPendingAutomation. RAM: +9 B (2 pointers + 1 byte).
 * Affiliates: liveRec_service, seq_drainPendingAutomation.
 */
static const uint8_t *seq_filterLatch;
static const uint8_t *seq_filterErase;
static uint8_t        seq_filterEraseTrack;
```

#### C-7b. seq_setAutomationFilter — ADD

```
void seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                              uint8_t eraseTrack);
```

#### C-7c. seq_drainPendingAutomation filter additions — MODIFY at lines 2442–2551

Add to the entry processing, after the target is decoded:

```
/*
 * Filter entries against registered latch and erase bitmaps (S081 §19.1).
 *
 * What: for each pending entry, check if the target's bit is set in
 * seq_filterLatch (any owner track: skip if latched, because LiveRecord
 * writes its own value at step end) or in seq_filterErase with
 * owner_track == seq_filterEraseTrack (skip if that track's automation
 * is being erased). For Instrument parameters, also check
 * seq_menuOverride (A1-4d). Why: latched parameters must not have
 * their stored automation played back — the menu value is what sounds.
 * Erased parameters must not play back either — their entries are being
 * removed. Inputs: seq_filterLatch, seq_filterErase, target bits.
 * Outputs: entry skipped or applied. Affiliates: liveRec latch bitmap,
 * liveRec erase bitmap, seq_menuOverride.
 */
```

### C-8. Capture and projection helpers

**File:** `Core/Sequencer/sequencer.c`

#### C-8a. seq_captureTrackTime — ADD

```
/*
 * PRIMASK snapshot of a track's timing state (S081 §19.1 item 11).
 *
 * What: captures under PRIMASK: seq_elapsedPpqTicks, seq_ppqStampUs,
 * seq_ppqPeriodUs, seq_stepIndex[track], seq_trackAccumulator[track],
 * effective interval/length/shuffle, play mode/state, played Scene.
 * Returns them in a seq_track_time_t struct. Why: LiveRecord runs in
 * the foreground and needs a consistent snapshot of TIM3-mutated state
 * for placement math. The PRIMASK read is a handful of byte/word copies,
 * fast enough for the ISR blackout. Inputs: track 0..6. Outputs:
 * filled struct. Affiliates: liveRec placement (§8.3),
 * seq_projectStep.
 */
void seq_captureTrackTime(uint8_t track, seq_track_time_t *out);
```

#### C-8b. seq_projectStep — ADD

```
/*
 * Project a master-clock tick to a track's step and offset (S081 §19.1).
 *
 * What: given a master tick, computes which step of the track would be
 * playing and the Q8 offset within that step. Built on the same math as
 * seq_loopCanonicalPreAdvance (line 3036). Returns "unknown" for future
 * ticks in rnd run mode. Why: LiveRecord needs to convert a captured
 * trigger's master-clock time to a step index for placement. Inputs:
 * track, tick. Outputs: step index, offsetQ8, validity flag.
 * Affiliates: seq_loopCanonicalPreAdvance, liveRec placement.
 */
uint8_t seq_projectStep(uint8_t track, uint32_t tick,
                         uint8_t *step, uint16_t *offsetQ8);
```

#### C-8c. seq_quantiseTick — ADD

```
/*
 * Quantise a master-clock tick to the nearest boundary (S081 §19.1 item 10).
 *
 * What: for 1/8..1/64, snaps the tick to the nearest master-clock grid
 * (48/24/12/6 PPQ). For QUANT_TRACK, snaps to the nearest step start of
 * the given track's own grid. For NO_QUANTISATION, returns the input.
 * Why: LiveRecord uses this to quantise captured triggers before
 * placement. The looper's seq_loopEventAnchor already does a similar
 * snap for the master clock; this generalises it for per-track grids.
 * Inputs: track, tick. Outputs: quantised tick. Affiliates:
 * seq_loopEventAnchor, seq_quantisation, QUANT_TRACK.
 */
uint32_t seq_quantiseTick(uint8_t track, uint32_t tick);
```

**File:** `Core/Sequencer/sequencer.h`

#### C-8d. Header declarations and types — ADD

```
typedef struct {
    uint32_t elapsedTick;
    uint32_t stampUs;
    uint32_t periodUs;
    int16_t  stepIndex;
    uint16_t accumulator;
    uint16_t interval;
    uint8_t  length;
    uint8_t  shuffle;
    uint8_t  playMode;
    uint8_t  playState;
    uint8_t  playedScene;
} seq_track_time_t;

typedef struct {
    uint8_t step;
    uint8_t track;
    uint8_t fired;
} seq_rec_boundary_t;

void    seq_setRecordTarget(uint8_t track, uint8_t writing);
void    seq_setAutomationFilter(const uint8_t *latch, const uint8_t *erase,
                                 uint8_t eraseTrack);
void    seq_setEraseMasks(uint8_t specials, uint8_t full);
void    seq_suppressStepOnce(uint8_t track, uint8_t step);
void    seq_setLiveHold(uint8_t track, uint8_t on);
void    seq_cancelPendingTrigger(uint8_t track);
uint8_t seq_takeRecBoundary(seq_rec_boundary_t *out);
void    seq_captureTrackTime(uint8_t track, seq_track_time_t *out);
uint8_t seq_projectStep(uint8_t track, uint32_t tick,
                         uint8_t *step, uint16_t *offsetQ8);
uint32_t seq_quantiseTick(uint8_t track, uint32_t tick);
uint8_t seq_getPlaybackEpoch(void);
void    seq_clearSceneAutomationBit(uint8_t index);
```

### C-9. seq_advanceTrackStep TIM3 changes — MODIFY at lines 2140–2320

These use the state declared in C-1 to C-4. The erase-mask branch is Phase G
(G-5).

#### C-9a. Fired bit and ring push

```
/*
 * Report the recording track's step boundaries (S081 §9.1a, §19.1 item 1).
 *
 * What: when seq_recWriting is set and (track == seq_recTrack or the
 * track's seq_liveHoldMask bit is set), push {step, track, fired} into
 * seq_recRing after the step is processed. The entry marks the START of
 * step N. LiveRecord keeps the last entry per track and treats the arrival
 * of that track's next entry as "the previous step ended", then runs the
 * step-end work for the previous entry with its fired bit. A run-mode or
 * loop jump is just a different next step. fired = bit 15 set &&
 * step_allowed (the condition passed), computed at the step start whether
 * or not the trigger is deferred, and set for an echo-suppressed step (the
 * live hit was its sound). A full ring drops the entry and counts the drop
 * (DEV builds). Why: LiveRecord's step-end table needs every step of rtk
 * and whether its first trigger fired. Inputs: step state, condition
 * result. Outputs: ring entry. Affiliates: seq_takeRecBoundary(),
 * liveRec_service().
 */
```

#### C-9b. Muted recording track: side-effect-free condition

```
/*
 * Would-have-fired for a muted rtk (S081 F-D, §9.1a).
 *
 * What: when track == seq_recTrack, seq_recWriting is set and the track is
 * muted (process_step == 0, :2151), read the specials and evaluate new
 * static seq_conditionPassesPure(&sp, track). It shares the dispatch of
 * seq_evaluateStepCondition() (:1755) but writes nothing:
 * seq_trackLastConditionFired is not updated, and a probability condition
 * takes a fresh RNG draw. The result only sets the ring's fired bit; nothing
 * plays. Why: F-D counts steps that would have fired on a muted track, and
 * muted tracks skip evaluation today. Affiliates:
 * seq_evaluateStepCondition(), C-9a.
 */
```

#### C-9c. Echo and live-hold trigger skips

```
/*
 * Skip a stored trigger once (echo) or while a recorded roll is held
 * (S081 §8.6, F-P).
 *
 * What: in the trigger branch, before any deferral:
 *  - if seq_echoStep[track] == step: clear the mark and skip the trigger
 *    (fired still 1);
 *  - else if the track's seq_liveHoldMask bit is set: skip the trigger.
 * Automation still queues in both cases (A1-3c). Why: a write ahead of the
 * playhead would otherwise play twice; while a recorded roll is held, the
 * steps it is overwriting must not play under it. Affiliates:
 * seq_suppressStepOnce(), seq_setLiveHold().
 */
```

### C-10. Echo-mark expiry

```
/*
 * Echo marks never outlive their pass (S081 §8.6, R-8).
 *
 * What: a mark is cleared (set to -1) when:
 *  - TIM3 consumes it (C-9c);
 *  - the track wraps to the start of its pass without reaching the marked
 *    step (run-mode wrap in seq_advanceTrackStep);
 *  - seq_setStepIndexToStart() runs (transport start/stop/reset);
 *  - any epoch site runs (Scene change, per-track reassignment, realign);
 *  - the track's effective length or play mode changes
 *    (seq_refreshTrackEffectiveParams(), the play-mode commit).
 * Why: a stale mark would silence a later legitimate trigger. Affiliates:
 * seq_echoStep[], C-6 epoch sites.
 */
```

---

## Phase D — PatternData / PatternStackService

### D-1. pat_writeDynamic trigger policy

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-1a. pat_writeDynamic — MODIFY at line ~640

```
/*
 * Trigger-policy argument for pat_writeDynamic (S081 §19.3).
 *
 * What: adds a trigger_policy parameter (PAT_RAW_TRIGGER_KEEP,
 * PAT_RAW_TRIGGER_OFF, PAT_RAW_TRIGGER_ON). Applied inside the
 * existing PRIMASK publish (line 741 and the detach path at 690):
 * KEEP preserves bit 15 as-is, ON sets it, OFF clears it. Why:
 * pat_recordStep needs to write specials/automation and set the trigger
 * in one atomic operation. The existing callers always want KEEP
 * (their trigger state is set separately). Inputs: trigger_policy enum
 * value. Outputs: bit 15 of the step address set, cleared, or kept.
 * Affiliates: all existing callers pass KEEP; pat_recordStep passes ON.
 */
```

### D-2. New pat_recordStep

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-2a. pat_recordStep — ADD

```
/*
 * Write a recorded step's specials and trigger (S081 §8.4, §19.3).
 *
 * What: reads the existing specials and automation for the step, applies
 * the record spec (note, velocity, timing, roll, fill; clears rhythm; keeps
 * the condition or sets fil), and writes the result with trigger ON. If the
 * result has no specials and no automation, it only sets bit 15
 * (pat_setStepActive). Why: the old seq_recordTrigger only set the trigger
 * bit; live recording writes the full step in one block replacement.
 * Inputs: scene, track, step, const pat_record_spec_t*. Outputs: nonzero on
 * commit, 0 when allocation fails (old state untouched), which the service
 * classifies like every other failed write (reactive recovery or counted
 * drop). Which hit wins when two land on one step is decided by LiveRecord
 * before submitting (E-1), not here. Affiliates: pat_writeDynamic (trigger
 * ON), pat_readStepSpecials, pat_readStepAutomations,
 * PATSVC_OP_RECORD_STEP.
 */
uint8_t pat_recordStep(uint8_t scene, uint8_t track, uint8_t step,
                       const pat_record_spec_t *spec);
```

**File:** `Core/Bank/Scene/Pattern/PatternData.h`

#### D-2b. pat_record_spec_t — ADD

```
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t timing;
    uint8_t roll;
    uint8_t fill;
    uint8_t setFil;
    uint8_t noteIsDefault;
} pat_record_spec_t;
```

### D-3. New pat_eraseSpecials

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

#### D-3a. pat_eraseSpecials — ADD

```
/*
 * Erase a step's specials while keeping automation (S081 §10.2, §19.3).
 *
 * What: if automation is present, keeps the condition and removes all
 * other specials (note, velocity, timing, roll, rhythm, fill). If no
 * automation, releases the block entirely (trigger KEEP — TIM3 already
 * cleared bit 15). Why: SEQ-hold erase removes triggers and specials
 * but automation still plays. Keeping the condition on steps with
 * automation preserves the automation's playback gate. Inputs: scene,
 * track, step. Outputs: step block trimmed or released. Affiliates:
 * pat_writeDynamic (trigger KEEP), PATSVC_OP_ERASE_SPECIALS.
 */
void pat_eraseSpecials(uint8_t scene, uint8_t track, uint8_t step);
```

### D-4. PatternStackService: new operations

**File:** `Core/Bank/Scene/Pattern/PatternStackService.c`

#### D-4a. Staging table — ADD

```
/*
 * Record-step staging table (S081 §19.3).
 *
 * What: 4 slots of pat_record_spec_t plus a scene/track/step triplet.
 * The patSvc_recordStep() caller copies the spec into a free slot; the
 * queue entry's payload carries the slot index. The service execution
 * calls pat_recordStep and frees the slot. Why: the queue's 16-bit
 * payload cannot hold the full record spec. The staging table is owned
 * by the service (no LiveRecord dependency). A full table rejects and
 * counts. RAM: +34 B. Affiliates: patSvc_recordStep,
 * PATSVC_OP_RECORD_STEP.
 */
```

#### D-4b. PATSVC_OP_RECORD_STEP — ADD to patSvc_operation_t enum

```
/*
 * Record-step service operation (S081 §19.3).
 *
 * What: reads the staging slot from the payload, calls pat_recordStep
 * with the staged spec, then frees the slot. Chunk requirement: at
 * most the current block size plus new specials overhead. Why: record
 * writes go through the service to respect the admission/handover
 * protocol and to serialise with other Pattern mutations. Affiliates:
 * patSvc_recordStep (submit), pat_recordStep (execution).
 */
```

#### D-4c. PATSVC_OP_ERASE_SPECIALS — ADD to patSvc_operation_t enum

```
/*
 * Erase-specials service operation (S081 §19.3).
 *
 * What: calls pat_eraseSpecials for the given scene, track, step.
 * Chunk requirement: at most the current block size. Why: same service
 * serialisation as other Pattern mutations. Affiliates:
 * patSvc_enqueueEraseSpecials (TIM3-safe submit), pat_eraseSpecials.
 */
```

#### D-4d. patSvc_enqueueEraseSpecials — ADD

```
/*
 * TIM3-safe erase-specials enqueue (S081 §19.3).
 *
 * What: modelled on patSvc_enqueueErase(). Packs the scene/track/step
 * into the queue entry and enqueues PATSVC_OP_ERASE_SPECIALS. Why:
 * TIM3 calls this when a SEQ-hold erase reaches a step; it cannot call
 * pat_eraseSpecials directly (no pool mutation from TIM3). Inputs:
 * scene, track, step. Outputs: queue entry added. Affiliates:
 * seq_advanceTrackStep erase branch, patSvc_enqueueErase.
 */
void patSvc_enqueueEraseSpecials(uint8_t scene, uint8_t track, uint8_t step);
```

#### D-4e. patSvc_recordStep submit, chunk sizing and execution — ADD/MODIFY

```
/*
 * Record-step submission (S081 §19.3).
 *
 * What: uint8_t patSvc_recordStep(scene, track, step,
 * const pat_record_spec_t *spec) takes a free staging slot, copies the spec
 * and the step coordinates into it, and submits PATSVC_OP_RECORD_STEP with
 * the slot index as payload through patSvc_submit() (direct when idle,
 * queued when busy). Returns 0 when the table is full, the Scene is not the
 * service target, or admission is closed (the slot is released). The slot
 * is freed after execution or a counted drop. Also:
 *  - patSvc_requiredChunks() (:947) gains RECORD_STEP (the current block
 *    plus the specials the spec adds) and ERASE_SPECIALS (at most the
 *    current block);
 *  - patSvc_executeEvent() (:1022) gains both cases, calling
 *    pat_recordStep() and pat_eraseSpecials().
 * Why: the 16-bit event payload cannot carry a full spec, and failure
 * classification needs the chunk requirement. Affiliates: LiveRecord (E-1),
 * pat_recordStep(), the staging table (D-4a).
 */
uint8_t patSvc_recordStep(uint8_t scene, uint8_t track, uint8_t step,
                          const pat_record_spec_t *spec);
```

### D-5. Target override

**File:** `Core/Bank/Scene/Pattern/PatternStackService.c`

#### D-5a. Target request state — ADD

```
/*
 * Target override for recording into a non-active Scene (S081 §7, §19.3).
 *
 * What: service_target_request (0xFF = none) holds the requested
 * recording-target Scene. patSvc_desiredScene() returns the request if
 * set, otherwise seq_activePattern. This replaces the eight direct
 * seq_activePattern comparisons (§3.3-8) in patSvc_submit,
 * patSvc_tick, patSvc_idle, patSvc_finishSceneReplace,
 * patSvc_endExclusive, and the submit variants. Why: when rtk plays a
 * per-track Scene different from the active Scene, Pattern writes must
 * target that Scene. The override redirects all service writes to the
 * desired Scene. Inputs: set by patSvc_requestTarget(). Outputs: read
 * by patSvc_desiredScene(). RAM: +2 B. Affiliates: liveRec target
 * negotiation (Phase H), patSvc_targetOpen.
 */
static uint8_t service_target_request;
```

#### D-5b. patSvc_requestTarget / patSvc_releaseTarget / patSvc_targetOpen / patSvc_replacePending — ADD

```
/*
 * Target override and replacement queries (S081 §7, §19.3).
 *
 * What:
 *  - patSvc_requestTarget(scene) returns 1 when the request is recorded,
 *    0 when refused: while an exclusive copy/clear claim or a filesystem
 *    replacement (service_replace_pending) is active, or for a
 *    non-resident Scene;
 *  - patSvc_releaseTarget() drops the request (the service hands back to
 *    seq_activePattern through the normal handover);
 *  - patSvc_targetOpen(scene) is 1 when the service target is `scene` and
 *    admission is open;
 *  - patSvc_replacePending() is 1 while a filesystem Pattern replacement
 *    is being prepared or applied (service_replace_pending).
 * Why: LiveRecord must know when it may write and when a Load is in
 * progress, without asking Menu (Appendix A-11). Affiliates: liveRec_service
 * (H-1), patSvc_prepareSceneReplace(), patSvc_finishSceneReplace().
 */
uint8_t patSvc_requestTarget(uint8_t scene);
void    patSvc_releaseTarget(void);
uint8_t patSvc_targetOpen(uint8_t scene);
uint8_t patSvc_replacePending(void);
```

#### D-5c. patSvc_desiredScene — ADD (static helper)

Replaces every `seq_activePattern` reference with `patSvc_desiredScene()`.

#### D-5d. patSvc_idle — MODIFY at line ~1609

```
/*
 * patSvc_idle returns false while a target override is held (S081 §19.3).
 *
 * What: adds `service_target_request == 0xFF` to the idle conditions.
 * Why: while a recording target is held, Pattern AutoSave must not
 * snapshot the overridden Scene mid-take. patSvc_idle() returning false
 * defers AutoSave. Affiliates: filesystem.c AutoSave drain check,
 * service_target_request.
 */
```

---

## Phase E — Trigger Recording

### E-1. liveRec_triggerIn implementation

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Trigger capture entry point (S081 §8, §19.2).
 *
 * What: called by the overlay path (buttonHandler) and by MidiParser for
 * each hit when it sounds. kind is one of SINGLE, FLAM_GRACE, FLAM_MAIN,
 * ROLL_START, ROLL_HIT. Performs:
 *   1. Gate: mode LATCH or OVERDUB, transport running, looper not engaged
 *      (P1 interim; P2 removes it), ownership (§7: P(track) == T, slot
 *      types match).
 *   2. Capture: seq_captureTrackTime() + the µs stamp.
 *   3. Roll classification at ROLL_START: if the roll interval (96-PPQ
 *      ticks) is equal to or longer than the track's step interval, the
 *      roll is "slow" (F-L): ROLL_START and every ROLL_HIT are recorded as
 *      individual SINGLE captures (each with its own quantize and
 *      placement), with no live hold. Otherwise it is a "fast" roll:
 *      seq_setLiveHold(track, 1), the start step is written with the roll
 *      index and fill 100, and E-4 extends it. ROLL_HIT is ignored for fast
 *      rolls. After liveRec_rollRateChange() the test is repeated for the
 *      new segment.
 *   4. Quantise: seq_quantiseTick().
 *   5. Placement (§8.3): project to step + delay.
 *   6. Flam pairing (§8.7): FLAM_GRACE is buffered; FLAM_MAIN (reported
 *      from buttonHandler_overlayTick) writes both when they land on
 *      different steps, or the main only.
 *   7. Same-step rule: when two hits land on one step in one pass, the last
 *      wins, except that a grace never replaces its own main.
 *   8. fil: setFil = seq_fillActive.
 *   9. Write: patSvc_recordStep(); seq_cancelPendingTrigger() when the
 *      write targets the current step before its deferred trigger fired;
 *      seq_suppressStepOnce() when the step is ahead of the playhead.
 *  10. LED: seq_markRecordedStep() (B-8c) after a successful submit.
 * Inputs: liveRec_trigger_t pointer. Outputs: step written, echo mark,
 * live hold, LED. Affiliates: buttonHandler overlay (E-2), MidiParser
 * (B-7a), seq_captureTrackTime, seq_projectStep, seq_quantiseTick,
 * patSvc_recordStep.
 */
```

### E-2. Overlay trigger hooks

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### E-2a. buttonHandler_overlayTrigger — MODIFY at lines ~871–927

```
/*
 * Overlay presses report to LiveRecord (S081 §19.7).
 *
 * What: after each seq_triggerVoice() in buttonHandler_overlayTrigger():
 * single types → LIVEREC_HIT_SINGLE (:899); flam → LIVEREC_HIT_FLAM_GRACE
 * (:904, the grace fires here); rolls → LIVEREC_HIT_ROLL_START with the
 * step roll index from the §8.8 table (:916). bh_overlayHeldType is set
 * (A3-4b). Why: LiveRecord captures each hit when it sounds. Affiliates:
 * liveRec_triggerIn(), buttonHandler_overlayTick().
 */
```

#### E-2b. buttonHandler_overlayTick — MODIFY at line ~950

```
/*
 * Timer hits report to LiveRecord (S081 §19.7).
 *
 * What: after seq_triggerVoice() at :950, report
 * LIVEREC_HIT_FLAM_MAIN when bh_overlayCount was 1 (the flam's delayed
 * main), or LIVEREC_HIT_ROLL_HIT when it is 0xFF (a roll repeat). The
 * reset argument follows A1-2b. Why: the flam main fires from this timer,
 * not from overlayTrigger(); roll repeats are needed for slow rolls, which
 * are recorded hit by hit (E-1 step 3). Affiliates: liveRec_triggerIn().
 */
```

#### E-2c. buttonHandler_overlayRelease — MODIFY

```
/*
 * Overlay release reports roll end to LiveRecord (S081 §19.7).
 *
 * What: calls liveRec_rollRelease(track). Why: LiveRecord needs to
 * know when the roll ends to write the release fill on the last
 * spanned step. Inputs: track. Outputs: roll capture finalised.
 * Affiliates: liveRec_rollRelease.
 */
```

### E-3. MIDI note hook — moved to Phase B (B-7a)

The call site changes in Phase B so that `seq_recordTrigger()` can be
removed. Phase E only fills in the `liveRec_triggerIn()` body (E-1), which
makes MIDI recording live.

### E-4. Fast-roll spans from the boundary ring — ADD in liveRec_service

```
/*
 * Extend a held fast roll across steps (S081 §8.8, §19.2 step 3).
 *
 * What: when a ring entry arrives for a live-hold track, the previous
 * entry's step (which has just ended, C-9a) is written as an active step
 * with the roll index and
 * fill 100 (patSvc_recordStep). At liveRec_rollRelease(), or at a rate
 * change, Scene change, stop, REC off or rtk change, the step containing
 * the end gets fill = (end − step start) × 100 / interval, and
 * seq_setLiveHold(track, 0) is called. A roll released before its second
 * hit is rewritten as a plain trigger. Why: the actual played steps come
 * from TIM3, so rev/pip/rnd run modes are covered. Affiliates: C-9a,
 * seq_setLiveHold(), patSvc_recordStep().
 */
```

### E-5. Live-hold ownership

`seq_setLiveHold()` is called only by LiveRecord: on at a fast-roll
`ROLL_START` that passed the gates, off at the roll's end (E-4) and at every
take boundary (H-1). buttonHandler never calls it (Appendix A-12).

---

## Phase F — Automation Recording

### F-1. Menu record hook

**File:** `Core/Menu/menu.c`

#### F-1a. menu_parseKnobDelta hook — MODIFY at line ~13916

```
/*
 * Automation record hook in knob delta path (S081 §9, §19.6).
 *
 * What: after the value clamp, when liveRec_mode() is LATCH or OVERDUB
 * and the cell is recordable (menu_knobClearTarget resolves a Pattern
 * target, the page is not PERFORMANCE/RECORDING/MIDI/LOAD/SAVE, not in
 * a Morph view, and the target is not track length), calls
 * liveRec_paramIn(target9, SOURCE_KNOB). Why: every user edit of a
 * recordable parameter must mark it as dirty (and latch it in LATCH
 * mode) so that LiveRecord writes it at the next step boundary.
 * liveRec_paramIn carries no value — the step-end write reads the
 * current retained value. Inputs: target9 from menu_knobClearTarget,
 * source. Outputs: dirty bit set, latch bit set (in LATCH mode).
 * Affiliates: liveRec_paramIn, menu_knobClearTarget, liveRec_service
 * step-end table.
 */
```

#### F-1b. menu_parseEncoder hook — ADD at line ~13466

Same hook for the encoder path (used for Scene-setting cells and other
encoder-driven parameters).

#### F-1c. SEQ-page scale/shuffle gate — part of the F-1a/F-1b recordable test

The STEP page reads and edits the **viewed** Pattern
(`menu_getViewedPattern()`). With FOLLOW off, a single SEQ click changes the
viewed Scene without changing playback. `scl`/`shf` gestures are therefore
recordable only when `menu_getViewedPattern() == scene_getActiveIndex()`. The
value LiveRecord reads (F-2a) is then the one the page shows.

### F-2. Value resolver

**File:** `Core/Sequencer/LiveRecord.c`

#### F-2a. liveRec_targetValue7 — ADD

```
/*
 * Read the current 7-bit menu value for a target (S081 §9.2, §19.2,
 * Appendix A-6).
 *
 * What: reads the value the menu shows for the target, from the take's
 * origin Scene A. A is scene_getActiveIndex() captured at the take start:
 * the Scene the VOICE, EFFECT and PERF pages display and edit. It is never
 * read from the target Scene T. Why: under per-track playback the recording
 * track may play a different Scene T = P(rtk). The VOICE page still shows
 * and edits A's image, and the active-Scene runtime write
 * (presetMorph_applyParameterNow) puts A's edited value on the slot that is
 * sounding. So the value the user hears is A's menu value, while T's image
 * still holds T's own, unedited value. LiveRecord must not read Menu, so it
 * reads the same owner data the menu reads.
 *
 * Value sources (all from A):
 *   Instrument:  scene_instrumentSlotConst(A, slot)->parameter_images
 *                .instrument_parameters[local] (the Normal endpoint, which
 *                is the displayed menu value; not morph_interpolation[],
 *                which is the Morph-interpolated runtime image)
 *   Nvm:         scene_getVoiceMorphAmount(A, slot), stored 7-bit (255 → 127)
 *   Nou:         scene_getVoiceAudioOut(A, slot)
 *   Nfx:         scene_getVoiceFxSendAmount(A, slot)
 *   7dc:         scene_getSlot6Track7AmpEnvelopeDecay(A)
 *   scl, shf:    pat_sceneRegion(A)->track_scale / track_shuffle (F-1c:
 *                recorded only when the viewed Pattern is A)
 *   fxm:         scene_getEffectMorphAmount(A), stored 7-bit
 *   Effect:      A's retained Normal value, WIDE8 rows compressed to 7 bits
 *
 * Ownership (§7) is checked separately: an Instrument target is recorded
 * only if its slot's played Scene is T and the slot's instrument type in A
 * equals the type in T, so the descriptor index means the same parameter
 * on playback. Effect and Scene targets require T == A. Inputs: target9.
 * Outputs: 7-bit value. Affiliates: liveRec_service step-end write (F-5),
 * SceneData getters, EffectsManager retained values.
 */
static uint8_t liveRec_targetValue7(uint16_t target9);
```

### F-3. Drain filter for latched/erased parameters

Already covered in C-7c. The filter in `seq_drainPendingAutomation` checks the
registered latch and erase bitmaps and skips matching entries.

### F-4. MIDI CC stub hook

**File:** `Core/MIDI/MidiParser.c`

#### F-4a. midiParser_enterTaggedParameter — MODIFY at line ~128

```
/*
 * CC automation stub hook for LiveRecord (S081 §19.9).
 *
 * What: after a successful parameter store, calls
 * liveRec_paramIn(target, SOURCE_MIDI). The target ID comes from the
 * tagged key; the MIDI rework owns that mapping, so for now this is a
 * stub (the call is present but the mapping always returns 0 / invalid).
 * Why: R4 says MIDI CC automation records the same way as knob edits.
 * The stub ensures the call site exists and the API is exercised.
 * Inputs: tagged key → target9 (stub). Outputs: liveRec_paramIn called
 * (no-op until mapping exists). Affiliates: liveRec_paramIn,
 * midiParser_enterTaggedParameter, MIDI rework (future).
 */
```

### F-5. Step-end automation processing — ADD in liveRec_service

```
/*
 * Step-end write / clear / re-stamp (S081 §9.3, §9.4, §19.2 step 3).
 *
 * What: when a ring entry for rtk arrives, the previous rtk entry (step N)
 * has ended (C-9a). For that step N:
 *  LATCH: for every set bit in `latch`:
 *    - dirty: write liveRec_targetValue7() to N, clear dirty;
 *    - not dirty, N fired, own Instrument parameter: write (re-stamp);
 *    - not dirty, N fired, foreign: clear an existing entry;
 *    - not dirty, N not fired, entry exists: clear;
 *    - otherwise nothing.
 *  OVERDUB: for every set bit in `dirty`: write to N, clear dirty. Nothing
 *    latches, clears or re-stamps.
 *  Only real changes are queued (L3): one pat_readStepAutomations() read
 *  of step N (foreground) answers every latched target. A write is
 *  queued only when the step has no entry for the target or a different
 *  value; a clear only when an entry exists. Writes use
 *  patSvc_writeStepAutomation(), clears patSvc_removeStepAutomation().
 *  Why L3: LATCH re-stamps every fired step of rtk on every pattern pass,
 *  so an unchanged held value would otherwise rewrite the same entries
 *  every bar, and a short loop (P2) would do so many times a second.
 *  fil (R10): when seq_fillActive and anything was written to N, one
 *  patSvc_setStepCondition(T, rtk, N, 21).
 *  At a take boundary (H-1), a pending dirty write for the current step is
 *  flushed first.
 * Why: this is the core of automation recording; the hook (F-1) only marks
 * bits, and the drain filter (C-7c) only silences playback. Inputs: ring
 * entries, bitmaps. Outputs: Pattern writes and clears in T. Affiliates:
 * C-9a, liveRec_targetValue7(), patSvc automation ops.
 */
```

---

## Phase G — Live Erase

### G-1. Erase button router

**File:** `Core/Hardware/frontPanel/buttonHandler.c`

#### G-1a. Erase router in processPress/processRelease — ADD

```
/*
 * Live erase button router (S081 §10.2, §19.7).
 *
 * What: while liveRec_mode() == ERASE, intercepts SEQ, VOICE and PLAY
 * presses/releases right after copyClear_buttonPressed() (:2295):
 *
 *   button | VOICE, STEP              | FX                 | PERF            | LOAD_SAVE, MENU, SOM_GEN
 *   SEQ    | liveRec_eraseSeqHeld()   | liveRec_eraseSeqHeld() (FX seq) | consumed | consumed
 *   VOICE  | liveRec_eraseTrackHeld() | liveRec_eraseTrackHeld()        | liveRec_eraseTrackHeld() | consumed
 *   PLAY   | plain transport toggle (buttonHandler_setRunStopState() +
 *            seq_setRunning()), bypassing bh_chainPlayArmed, in every mode
 *
 * "Consumed" means returned without any action. All other buttons pass
 * through unchanged. Why: live erase must not leak into step toggles, voice
 * selection, mutes, overlay triggers, Scene changes or FX hooks; in modes
 * where it does nothing, the presses must still not act. Affiliates:
 * liveRec_eraseSeqHeld(), liveRec_eraseTrackHeld(), seq_setRunning().
 */
```

### G-2. Erase knob handler

**File:** `Core/Menu/menu.c`

#### G-2a. Knob erase in menu_parseKnobDelta — ADD as its own branch immediately before the copy/clear pot-ownership branch (~13960)

```
/*
 * Knob erase during live erase (S081 §10.2, §19.6).
 *
 * What: a separate branch placed before the copy/clear pot-ownership
 * test. That branch never runs while armed, because copyClear_copyPressed()
 * refuses while liveRec_armed(). If liveRec_mode() == ERASE: resolves the target via
 * menu_knobClearTarget(), calls liveRec_eraseParam(target, fx_lane),
 * does not adjust the value, and calls menu_automationTargetCleared()
 * to drop the underline. The search restarts when erase ends. Works
 * on any page whose cells resolve, including the Morph view and PERF
 * Morph cells (selection, not adjustment). Why: knob erase selects a
 * parameter for erase by turning its knob. The parameter is not
 * adjusted — only its automation is removed from rtk as the playhead
 * passes. Inputs: knob ID → target9 + fx_lane. Outputs:
 * liveRec_eraseParam called, underline cleared. Affiliates:
 * liveRec_eraseParam, menu_knobClearTarget, menu_automationTargetCleared.
 */
```

### G-3. EffectsManager FX-sequence erase

**File:** `Core/DSP/Effects/EffectsManager.c`

#### G-3a. New effects_setLiveEraseLanes — ADD

```
/*
 * Register lanes for FX-sequence live erase (S081 §10.4, §19.4).
 *
 * What: sets the live-erase lane mask. At each FX step start,
 * EffectsManager excludes these lanes' locks from resolution and records
 * the (step, lanes) in its pending mask. Why: FX-sequence erase removes
 * locks as the FX playhead passes, without the locks ever sounding
 * (not even for one render block). The pending mask is drained by
 * liveRec_service. Inputs: laneMask (16 bits). Outputs: erase lanes
 * registered. RAM: +34 B (16 × 16-bit pending mask + lane mask).
 * Affiliates: effects_seqConsume/effects_seqSelect (exclude lanes),
 * effects_takeErasedLocks (drain), effects_clearSeqLanes (fan-out),
 * liveRec_service.
 */
void effects_setLiveEraseLanes(uint16_t laneMask);
```

#### G-3b. New effects_takeErasedLocks — ADD

```
/*
 * Drain the FX-erase pending mask (S081 §10.4, §19.4).
 *
 * What: copies the pending mask (which steps had which lanes erased)
 * into the output, clears it. Called by liveRec_service on each 500 Hz
 * pass. Why: the pending mask accumulates at each FX step start, which
 * effects_seqConsume() handles in the foreground render path;
 * LiveRecord drains it at 500 Hz and calls effects_clearSeqLanes to
 * remove the locks from the origin Scene and its linked Scenes.
 * Inputs: output stepMask, laneMask pointers. Outputs: masks copied,
 * pending cleared. Affiliates: liveRec_service, effects_clearSeqLanes,
 * effects_fanoutMask.
 */
uint8_t effects_takeErasedLocks(uint16_t *stepMask, uint16_t *laneMask);
```

#### G-3c. Lock exclusion at FX step start — MODIFY effects_seqConsume() (STEP, `:746`) and effects_seqSelect() (`:815`)

```
/*
 * Erased lanes never sound (S081 §10.4).
 *
 * What: at the FX step start (after A2-2f's lock-start clearing), the
 * new step's locks on lanes in the live-erase mask are recorded as
 * (step, lanes) in the pending mask and excluded from resolution for this
 * step: effects_service() treats them as unlocked (A2-2h). The retained
 * lock is removed later by LiveRecord through effects_clearSeqLanes(),
 * with linked-Scene fan-out. Why: the lock must not sound even for one
 * render block. Affiliates: effects_setLiveEraseLanes(),
 * effects_takeErasedLocks(), effects_service().
 */
```

#### G-3d. Who sets the erase lanes — LiveRecord

- SEQ hold in FX mode: `effects_setLiveEraseLanes(0xFFFF)` while held,
  `0x0000` on release (all lanes, including Morph).
- Knob erase whose `menu_knobClearTarget()` returned an `fx_lane`: that lane's
  bit is OR-ed into the mask until erase ends (F-J).
- Erase end and every take boundary: `effects_setLiveEraseLanes(0)`, then a
  final `effects_takeErasedLocks()` → `effects_clearSeqLanes()` flush.

### G-4. Overdub/erase exclusivity

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Overdub and erase are mutually exclusive (S081 §10.3, F8).
 *
 * What: entering erase (liveRec_eraseBegin) disables overdub; entering
 * overdub while erase is enabled does nothing. Releasing both SHIFT and
 * CLEAR ends erase. Releasing REC during erase restores the prior
 * LATCH/OFF state without a toggle. Why: simultaneous overdub and erase
 * would create contradictory writes on the same step. The simpler rule
 * is mutual exclusion. Affiliates: liveRec_eraseBegin, liveRec_overdubBegin,
 * buttonHandler REC release handler.
 */
```

### G-5. Erase chord, TIM3 erase branch and old-erase removal — MODIFY (moved from B-3, B-7d, B-7e)

All three land together so every build has exactly one working erase path.

1. **Chord** (`buttonHandler.c:2421-2428`, `:2615-2624`, `:2812-2817`):
   while `liveRec_armed()`, SHIFT+COPY in either order calls
   `liveRec_eraseBegin()` and lights both LEDs; `copyClear_copyPressed()` is
   not called. Releasing one key keeps erase on; releasing both calls
   `liveRec_eraseEnd()` and restores the LEDs. `bh_shiftHeld`/`bh_clearHeld`
   replace `bh_state.seqErasing`, which is removed.
2. **TIM3 branch** (`sequencer.c:2211-2222` and the automation guard in
   A1-3c): `seq_eraseActive && track == menu_getActiveVoice()` becomes the
   masks.
   - Full erase (`seq_eraseFullMask` bit): clear bit 15,
     `patSvc_enqueueErase()`, skip automation queueing (today's path).
   - Specials erase (`seq_eraseSpecialsMask` bit): clear bit 15,
     `patSvc_enqueueEraseSpecials()`; automation still queues.
   - Both obey §7 (the service accepts only the target Scene).
3. **Removals:** `seq_eraseActive` (`:675`), `seq_setErasingMode()`
   (`:4369`), their `sequencer.h` declarations, and the `seq_eraseActive`
   terms left in B-6a/B-6b.

### G-6. Knob-erase removals — ADD in liveRec_service

```
/*
 * Remove erased targets as the playhead passes (S081 §10.2, §19.2 step 3).
 *
 * What: for each ring entry with track == rtk while erase is on, every
 * target in the `erase` bitmap that has an entry on the ended step is
 * removed (pat_readStepAutomations() check, then
 * patSvc_removeStepAutomation()). The drain filter (C-7c) already kept
 * those entries from playing. Why: knob erase removes stored automation,
 * not only its playback. Affiliates: C-7c, C-9a, patSvc.
 */
```

---

## Phase H — Target Negotiation, Scene/Epoch Handling, Q13 Refusal

### H-1. liveRec_service target and epoch handling

**File:** `Core/Sequencer/LiveRecord.c`

```
/*
 * Take boundary and target negotiation (S081 §7, §19.2).
 *
 * What: at the top of liveRec_service(), checks for take boundaries:
 *   - epoch change (seq_getPlaybackEpoch() != last seen);
 *   - rtk change or mode change;
 *   - patSvc_replacePending() (a Load is replacing Pattern data: release
 *     the target, suspend writing; the load's completion bumps the epoch
 *     through seq_alignActivePatternToScene()).
 * A new take clears latch/dirty, discards open roll and pending grace,
 * flushes FX-erase pending mask, resets the take origin.
 * Then evaluates T = seq_getTrackPlayedScene(rtk). If T != active
 * Scene, calls patSvc_requestTarget(T); otherwise patSvc_releaseTarget().
 * Publishes seq_setRecordTarget and seq_setAutomationFilter.
 * While !patSvc_targetOpen(T), all writes are dropped and counted.
 * A new take also clears the live hold (seq_setLiveHold(track, 0)) and
 * the FX erase lanes. LiveRecord never calls Menu (Appendix A-11).
 * Why: the recording target must track rtk's played Scene and reset
 * on any context change. Inputs: seq_getPlaybackEpoch,
 * seq_getTrackPlayedScene, patSvc_replacePending. Outputs: target set,
 * filter registered, bitmaps cleared on new take. Affiliates:
 * patSvc_requestTarget/releaseTarget/targetOpen, seq_setRecordTarget,
 * seq_setAutomationFilter.
 */
```

### H-2. Q13 refusal

**File:** `Core/Menu/menu.c`

#### H-2a. menu_parseKnobDelta/menu_parseEncoder — MODIFY

```
/*
 * Q13 silent refusal while T != A (S081 §7.5, §19.6).
 *
 * What: at the top of the knob/encoder handler, if
 * liveRec_activePatternLocked() returns true and the cell writes the
 * viewed Pattern (PAR_STEP_*, PAR_TRACK_LENGTH/SCALE/SHUFFLE/PLAY_MODE/
 * LOOPABLE, step-automation editor cells, va_writeAutomationFromKnob,
 * sa_* writers, menu_broadcastStepSpecial), return before parameter_values
 * or the display changes. No message. Why: while the recording target
 * is a non-active Scene, edits to the active Scene's Pattern would
 * conflict with recording. The refusal is silent per Q13. Inputs:
 * liveRec_activePatternLocked(). Outputs: edit suppressed. Affiliates:
 * liveRec_activePatternLocked (T != A check).
 */
```

#### H-2b. STEP button toggle refusal — MODIFY buttonHandler_setRemoveStep() (`buttonHandler.c:1052`)

```
/*
 * Silent refusal of STEP toggles while T != A (S081 Q13, §3.3-11).
 *
 * What: at the top, if liveRec_activePatternLocked(), return before any
 * LED, menu or Pattern change. Why: this is the only UI path that writes
 * bit 15 directly (pat_toggleStep() at :1111), so Pattern Stack Service
 * admission never sees it. buttonHandler_selectActiveStep() only selects a
 * step and needs no refusal. Euklid is compiled out; copy/clear is refused
 * while armed. Affiliates: liveRec_activePatternLocked().
 */
```

### H-3. Drop counters and trace codes

`liveRec_drops` (per cause: handover, ownership, staging full, ring full,
service rejected) exists only when `DEV_MODE_LOGGING` is enabled. Each drop
records one `patternTrace_record()` entry with a new stage code (listed in
`PATTERN_DYNAMIC_STACK.md` §12.16 at close). Production builds allocate
nothing for them.

---

## Phase I — Specs and Memory

### I-1. Documentation updates

At session close (no code change required during implementation), per plan
§19.14:

| Document | Section | Change |
|---|---|---|
| `PATTERN_DYNAMIC_STACK.md` | new section | Live record (LiveRecord module, take/target rules, step-end tables, erase) |
| `PATTERN_DYNAMIC_STACK.md` | §6.1/§6.2 | §5.8 step reset rule, deferred automation, delayed rolls, spill rule |
| `PATTERN_DYNAMIC_STACK.md` | §6.2a | R12 amendment to the S075 F3 priority rule |
| `PATTERN_DYNAMIC_STACK.md` | §12.3 | Queue format (6-bit op, 10-bit step, 16-bit payload) |
| `PATTERN_DYNAMIC_STACK.md` | §12.12 | Record/erase ops, target override, replace-pending query |
| `PATTERN_DYNAMIC_STACK.md` | §12.16 | LiveRecord trace stage codes |
| `EFFECTS_BUS_REFERENCE.md` | §6, §10, §11.2 | R13 rules, one-step Morph lane, FX realign |
| `MODULE_INTERCHANGE_SPEC.md` | new section + rows | LiveRecord; Sequencer, PatternStackService, EffectsManager, Preset, MidiVoiceControl rows; PERF surface (REC hold, erase chord, overlay recording); stale rows (plan §3.2) |
| `STORAGE_SRAM_MANIFEST.md` | RAM tables | Every allocation in the RAM summary; `effects_automation_t` size |
| `FILESYSTEM_SPEC.md` | settings.cfg | `quantisation` value 5 (`trk`) |
| `MEMORY.md` | Volatile notes | S075 F3 amendment (automation priority bullet); stale "BAR1/BAR2 use `midiParser_playVoiceMidiNote`" line; live-record invariants |

### I-2. config.h

No changes required. `DEV_MODE_LOGGING=1` stays for development.
`AUTOSAVE_TRACE_RECORD_COUNT=2048` stays temporary (existing carried debt).

---

## RAM Summary

| Phase | Item | Bytes |
|---|---|---|
| A1 | `voiceControl_pendingReset` (u8) | +1 |
| A1 | `seq_trackShuffleStep` + `seq_trackShuffleRoll` (7+7) | +14 |
| A1 | `seq_menuOverride` (6 × u64) | +48 |
| A2 | `menuHold` (u64) + `menuHoldMorph` (u8) | +9 |
| A2 | Remove owner/pending/held fields | −77 |
| A3 | `bh_overlayHeldType` | +1 |
| B | LiveRecord state (bitmaps 192, mode/rtk/T/origin/epoch 16, roll 8, grace 4) | +220 |
| B | REC hold timer | +3 |
| C | Ring (34), echo (7), hold (1), erase masks (2), stamps (8), epoch (1), filter pointers (9) | +62 |
| D | Staging table (34), target request (2) | +36 |
| G | FX-erase pending mask + lane mask | +34 |
| **Total added (every build)** | | **~428** |
| DEV_MODE_LOGGING only | LiveRecord drop counters | +16 |
| **Released** | owner/pending (75), held_morph (2), legacy roll (2), `recordSubStep` (1) | **~80** |
| **Net (every build)** | | **~348** |

The plan's §14 listed about 410 B added. The difference is the filter
pointers (9 B), the unpacked deferral bytes (14 B against 7), the overlay
held type and REC hold timer (4 B). That is covered by the RAM approval,
which includes follow-ons. Exact counts are fixed during implementation.

---

## Dependency Order

```
A1 ──► A2 ──► A3 ──► B ──┬──► C ──┐
                         └──► D ──┴──► E ──► F ──► G ──► H ──► I
```

C (Sequencer services) and D (PatternData/Service) are independent of each
other; both must land before E. A1 must land first (trigger/automation
ordering fix). A2 builds on A1's drain changes. A3 is independent of A2 but
logically grouped. B creates the LiveRecord module with stub bodies that
E, F and G fill. Erase changes all land in G (G-5) and the MIDI call site in
B (B-7a), so every phase builds. I is documentation only.

Each phase is verified on hardware before the next begins (§15 verify column).

---

## Code review — P1 implementation (2026-10-13)

**Scope.** The working tree against `a6c96a0`: 28 files, about 2,900 lines,
including the new `Core/Sequencer/LiveRecord.c/.h`. Checked against this
schedule (revision 4) and `S081_P1_LIVE_RECORD.md` (revision 11).

**Build** (re-run for this review):

- `make all` is clean. The only warnings predate this work:
  `PatternData.c:200` packed-member address, and the libc stub notes.
- text 565,016, data 412, bss 428,316: +340 B bss since the S080 close, which
  is within the approved budget.
- Flash headroom 188,236 B.

**Verdict.**

- Phases A1–A3 (step reset rule, R12/R13, roll table, overlay fixes, `trk`
  loop anchors) are sound and can go to hardware now.
- The recording paths (E/F/G/H) have defects that would record wrong data or
  lock out normal editing. Fix the **must-fix** items before hardware
  recording tests.

### Must fix

| # | Where | Problem | Fix |
|---|---|---|---|
| 1 | `LiveRecord.c:573` `liveRec_activePatternLocked()`; callers `menu.c` `menu_parseKnobDelta()`/`menu_parseEncoder()`, `buttonHandler.c` `buttonHandler_setRemoveStep()` | Returns true whenever P(rtk) ≠ A, **including with REC off**. With per-track playback (S077), selecting a track that plays another Scene silently locks STEP editing and step toggles. Regression. | Locked only while armed **and** the target override is held (`liveRec_armed() && T ≠ A`). |
| 2 | `LiveRecord.c:236` | `seq_setAutomationFilter(liveRec_latch, liveRec_dirty, liveRec_rtk)` passes the **dirty** bitmap as the erase filter. Knob-erased automation still plays until removed (spec: removed before it plays). In LATCH/OVERDUB the drain wrongly skips rtk entries of dirty targets (OVERDUB should play automation). | Pass `liveRec_erase`. |
| 3 | `LiveRecord.c:256`, `:712`, `:215` | Fast-roll span writing, live-hold release and the take-boundary clear all use `liveRec_rtk`, but the live hold is set on the **rolled** track (`:655`). An overlay roll on a track other than rtk: no span steps are written, and that track's live hold is never cleared, so its stored triggers stay silent. | Keep `liveRec_rollTrack`; use it for spans, release and the take-boundary clear. |
| 4 | `sequencer.c` `seq_advanceTrackStep()` muted-rtk branch (`rec_fired = seq_conditionPassesPure(&rsp, track)`) | Ignores bit 15. Every step of a muted rtk, including empty ones, counts as fired, so LATCH re-stamps own-Instrument values onto **every** step (pool growth, wrong data). | `rec_fired = pat_isStepActive(…) && seq_conditionPassesPure(…)`. |
| 5 | `sequencer.c:3765` `seq_projectStep()`, `seq_quantiseTick()` (`trk` branch); `LiveRecord.c:118` `liveRec_place()` | Placement maps the tick through the track's **canonical master-clock** position (`tick·256 / current interval`), not its actual position. After any scale or length change during playback (menu edit, Morph, scale/length automation) they differ, and hits land on the wrong step with the wrong delay. Also: the delay ignores shuffle, so odd shuffled steps play late by the shuffle amount (implementer-noted); and "delay rounds to 100 → step + 1" is wrong for rev/pip. | Place relative to the captured live state: current step start = now − (accumulator >> 8); walk forward/back with the live interval (`seq_loopShiftPosition()`-style). Subtract the shuffle deferral on odd steps. Take the next step from the run mode. |
| 6 | `LiveRecord.c:596-599` `liveRec_commitHit()`; `sequencer.c` C-10 | Every placement not on the current step gets an echo mark, including steps **behind** the playhead (backward quantize, a flam grace on the previous step). With no pass-wrap expiry (C-10 not implemented for wraps), the newly recorded hit is silenced on its next pass. | Mark only steps ahead of the playhead in the current pass. Add the pass-wrap expiry. |
| 7 | `sequencer.c:924` `seq_cancelPendingTrigger()` | Clears the pending deferral only. Since A1-3 moved a deferred step's automation to fire time, this also drops that step's automation for the pass. It also cancels a deferral that belongs to an earlier step. | Cancel only if the pending deferral is for the recorded step, and queue that step's automation (without the trigger). |
| 8 | `buttonHandler.c` `processPress()`/`processRelease()` erase router | The §10.2 per-mode table is not applied. SEQ in PERF, Load/Save, MENU and SOM_GEN starts a SEQ erase (PERF Scene buttons would erase triggers). VOICE in Load/Save, MENU and SOM_GEN erases tracks. | SEQ: VOICE/STEP → track erase, FX → FX-sequence erase, otherwise consumed. VOICE: VOICE/STEP/PERF/FX → track erase, otherwise consumed. |
| 9 | `LiveRecord.c:755` `liveRec_eraseSeqHeld()` | SEQ-hold erase uses rtk, not the **active** track (F7). These differ after an rtk override. | Pass the active track from buttonHandler. |
| 10 | `LiveRecord.c:209-218`; epoch bump in `seq_setRunning()` | Every transport start/stop is a take boundary. It zeroes the Sequencer erase masks but keeps LiveRecord's own copies. Pressing PLAY while holding SHIFT+CLEAR+TRACK drops the held erase, so "playback starts with live erase enabled" fails, and the stale local masks come back on the next gesture. | On a take boundary, re-push LiveRecord's current erase state (held gestures persist), or clear both copies together. Transport start must not end a held erase. |
| 11 | `LiveRecord.c:717` `liveRec_paramIn()` / `liveRec_stepEnd()`; `:629` trigger gate | Ownership (plan §7) is missing in the automation path: with T ≠ A, Effect/Scene edits are recorded into T, and an Instrument target is not checked for "slot plays T" or a type match. The trigger path checks P(track) == T but not the slot type. | Apply the §7 table in `liveRec_paramIn()`/step end and the type check for triggers. |
| 12 | `buttonHandler.c` BUT_SHIFT release and press | `bh_shiftHeld = 0` comes after the early `break`s (consumed release, latched menus), so the flag can stick and erase cannot end; on press, the set can be skipped the same way. On SHIFT release the SHIFT LED goes off while CLEAR is still held. The CLEAR LED is never lit. Spec: SHIFT and CLEAR lit while either is held. | Update `bh_shiftHeld` first in both handlers. While ERASE, keep both LEDs lit until both keys are released. |
| 13 | `buttonHandler.c` `handleSelectButton()` before `seq_loopEngage()` | `liveRec_forceOff()` runs even when the engage does nothing (stopped transport), so a SELECT press in PERF while stopped disarms record. S080 cleared record only if `seq_loopActive` after engage. | Call `liveRec_forceOff()` only if the loop actually engaged (P1 interim; P2 removes the lockout). |

### Should fix / spec gaps

The implementer's noted simplifications are included here.

| # | Where | Problem |
|---|---|---|
| 14 | `menu.c` `menu_parseEncoder()` | F-1b not hooked: encoder edits are not recorded. |
| 15 | `buttonHandler.c` `buttonHandler_overlayReport()`; `LiveRecord.c:157` | `note_is_default` is never set by the overlay and is forced to 0 in `liveRec_writeStep()`, so Q12 (no note special for `def`) is not met. Velocity 100 and fill 100 are always stored explicitly (no "absent" form): harmless but costs bytes. |
| 16 | `LiveRecord.c` roll paths | Release fill fixed at 100. "Released before the second hit → plain trigger" missing. single → roll switch sends no ROLL_START, so a fast roll is recorded as many single hits (last wins per step). No slow/fast re-check after a rate switch. Span steps don't get `fil`. |
| 17 | `LiveRecord.c` `liveRec_place()` | µs sub-tick (Q3) not used; tick resolution only. |
| 18 | `LiveRecord.c:346` | Effect values are not compressed to 7 bits for WIDE8 rows. A value above 127 wraps when packed (`& 0x7F`). Latent unless an AUTOMATABLE Effect row is WIDE8; verify. `fxm` stores m/2 while playback expands with `effect_expand7Linear()` (rounding mismatch). |
| 19 | `menu.c` `menu_liveRecRecordKnob()` | F-1c (`scl`/`shf` only while the viewed Pattern is A) not implemented. Effect-page Morph view is not excluded (only `voiceModeShowMorph` and the STEP view are checked); verify the Effect view flag. |
| 20 | `menu.c` rtk cell | The display reads `liveRec_recordTrack()`, but the edit seed is `parameter_values[PAR_AUTOM_TRACK]`, which never follows rtk. The first detent jumps from a stale value. |
| 21 | `LiveRecord.c` erase masks; `sequencer.c` TIM3 erase branch | Track/SEQ erase masks are not gated by P(track) == T. TIM3 clears bit 15 in non-target Scenes (pool reclaim is then rejected). Gate the masks in LiveRecord. |
| 22 | `LiveRecord.c:230-232` | The target override is requested whenever armed, including while stopped. `patSvc_idle()` is then false and Pattern AutoSave is blocked while armed and stopped with T ≠ A, against F-M. Request only while writing. |
| 23 | `sequencer.c` step-allowed block | A pending deferral is fired at every allowed step start, not only when that step queues automation (A1-3c). Delays longer than one step are cut at the boundary even when the next step is empty. |
| 24 | `sequencer.c:828-837` | TIM3-shared ring cursors and entries (`seq_recRing`, head/tail) and the echo/mask bytes are not `volatile` and have no barrier. Under LTO the foreground drain may cache `seq_recRingHead` or read an entry before its publication. Follow the `seq_pending_automation` precedent. |
| 25 | C-10 | Play-mode-change echo expiry not wired (implementer-noted). |
| 26 | H-3 | DEV-only drop counters not added; staging-full, ring-full and service rejections are silent. |
| 27 | `menu.c` knob erase | Calls `menu_automationTargetCleared()`, which is for whole-Scene removals only (`MEMORY.md`). Live erase is progressive and rtk-only, so the underline can vanish while automation remains. Restart the search at erase end instead. |
| 28 | `EffectsManager.c` `effects_erasedSteps`/`effects_erasedLanesAcc` | One step mask and one lane mask accumulate between drains, so two FX steps with different lane sets in one 2 ms window clear the union on both. Low probability; a per-step lane mask would make it exact. |
| 29 | A2-4 realign | Open from the schedule review: non-resetting Scene changes now realign and publish the FX step immediately, changing the documented "menu values until the next FX boundary" rule (`EFFECTS_BUS_REFERENCE.md` §10). Needs the user's decision. |

### Verified correct

- **Voice queue reset:** per-voice mask; test-and-clear inside the
  `voiceControl_popPending()` PRIMASK section; apply → restore (if reset) →
  trigger order kept; leftover resets restored under PRIMASK before the drain.
- **Trigger callers:** every `seq_triggerVoice()` caller passes the A1-2b
  reset value (overlay timer `count != 0xFF`, roll retriggers 0, SOM 1).
- **Deferred steps:** `seq_fireDeferred()` on expiry and flush; automation-only
  steps still queue at the step start; roll steps honour shuffle and
  micro-timing; F-R roll end on a firing trigger only; carry path
  `voiceControl_resetOnly()`.
- **R12:** `seq_menuOverride` cleared **before** the restore's early return;
  drain skip; transport/Scene/realign clears; Instrument override set once per
  cell slot; per-target clears for audio out, FX send, `7dc`, and track
  length/scale/shuffle (Normal and Morph).
- **R13 (EffectsManager):** markers and owner state removed (`effects_automation_t`
  184 → 120 B); `menuHold` > lock > overlay > menu; one-step Morph lane;
  lock-start clearing in clock and `sel` modes.
- **FX realign:** `seq_fxRealignToMasterClock()` plus the overlay-only reset
  latch at both realign sites.
- **Roll table and names:** 96-PPQ table, 10 names, clamps 0..9; `PAR_ROLL`
  retired; `qnt trk` list and clamp.
- **`trk` loop anchors:** per-track anchor and back offset inside
  `seq_loopSnapshotTracks()`; `off` and the grid keep their anchors (L1).
- **Pattern Stack Service:** all eight `seq_activePattern` sites use
  `patSvc_desiredScene()`; `patSvc_requestTarget()` refuses during claim,
  replacement or busy queue; `patSvc_idle()` false under override;
  record/erase ops, staging and chunk sizing in place.
- **PatternData:** `pat_recordStep()` (trigger ON in the same publish,
  automation and condition kept, `fil`); `pat_eraseSpecials()`.
- **Condition evaluation:** refactor into `seq_conditionEval()` with a
  side-effect-free variant.
- **Boundary ring:** packing and decode consistent; step-start semantics and
  the previous-entry step end implemented as specified (M-9).
- **Smaller pieces:** LED recorded step by absolute step; rtk commit via
  `menu_parseGlobalParam()`; rtk excluded from settings-dirty; AutoSave and
  copy/clear gates; MIDI note and CC call sites; `liveRec_service()` before
  `patSvc_tick()`; `liveRec_init()` at boot.
- **REC gestures:** short press toggles on release; hold enters overdub with a
  pulse; overdub and erase exclusivity per §10.3.

### Suggested order

1. Must-fix 1, 2, 3, 4 and 8: small, local changes with the largest impact.
2. Must-fix 5, 6 and 7: placement and echo, the core of trigger recording.
3. Must-fix 9–13.
4. Then hardware-test A1–A3 and the recording matrix.
5. Should-fix items next; 14, 15, 16, 21, 22 and 24 before release.

