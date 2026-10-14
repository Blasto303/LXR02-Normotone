/*
 * LiveRecord.c
 *
 * S081 P1 live-record and live-erase engine (plan §4, §19.2).
 *
 * What: captures overlay triggers, MIDI notes and knob automation into Pattern
 * steps on the recording track (rtk). Manages mode (OFF/LATCH/OVERDUB/ERASE),
 * target-Scene negotiation, latch/dirty/erase bitmaps, placement and
 * quantisation, flam pairing, roll span tracking, and FX-sequence erase
 * fan-out. Why: the old recording path (seq_recordTrigger, seq_quantize) was a
 * trigger-bit-only bridge from the fixed-grid era with no note, velocity,
 * timing, roll or automation recording.
 *
 * Inputs: liveRec_triggerIn (triggers), liveRec_paramIn (automation gestures),
 * liveRec_erase* (erase gestures), mode setters (from buttonHandler). Outputs:
 * patSvc_recordStep, patSvc_writeStepAutomation, patSvc_removeStepAutomation,
 * seq_suppressStepOnce, seq_setRecordTarget, seq_setAutomationFilter,
 * effects_clearSeqLanes. Does not call: Menu, buttonHandler, MidiParser. The
 * Sequencer does not call LiveRecord.
 *
 * Created on: 13.10.2026
 * Modifications Copyright 2026 Brendan Clarke
 * Part of the LXR02 open-source software under the same license as the LXR
 * firmware.
 */

#include "LiveRecord.h"
#include "sequencer.h"
#include "SceneData.h"
#include "PatternData.h"
#include "PatternStackService.h"
#include "EffectsManager.h"
#include "timebase.h"
#include "InstrumentManager.h"
#include "SceneModTargets.h"
#include "EffectTypes.h"
#include "PatternTrace.h"
#include "menu.h"
#include "config.h"
#include <string.h>

/* S081 P2 follow-up: the step-value rule must be chosen explicitly; an
 * undefined switch would silently read as 0 in the #if tests below. */
#ifndef LIVEREC_AUTOMATION_FIRST_CHANGE
#error "config.h must define LIVEREC_AUTOMATION_FIRST_CHANGE (0 or 1)"
#endif

/* 9-bit target ID space for the latch/dirty/erase bitmaps (S081 §9.2). */
#define LIVEREC_TARGET_BITS   512u
#define LIVEREC_TARGET_BYTES  (LIVEREC_TARGET_BITS / 8u)

/*
 * LiveRecord runtime state (S081 §19.2, revision 6).
 *
 * modeState  LIVEREC_MODE_*. rtk  the recording track. target  T = P(rtk).
 * origin  the take's origin Scene A (the active Scene at the take start).
 * priorMode  the LATCH/OFF state to restore after OVERDUB or ERASE ends.
 * lastEpoch/epochValid  playback-epoch poll state for take boundaries.
 * latch/dirty/erase  the three 512-bit target bitmaps (64 B each).
 *   dirty with LIVEREC_AUTOMATION_FIRST_CHANGE 0: adjusted since its last
 *   write (written at the step end). With 1: the current step of rtk already
 *   has this target's value (its first change), or, while no step is known,
 *   a change waiting for the next step start (S081 P2 follow-up).
 * RAM: about 220 B SRAM1 in every build.
 */
static uint8_t liveRec_modeState = LIVEREC_MODE_OFF;
static uint8_t liveRec_rtk = 0u;
static uint8_t liveRec_target = 0u;
static uint8_t liveRec_origin = 0u;
static uint8_t liveRec_priorMode = LIVEREC_MODE_OFF;
static uint8_t liveRec_lastEpoch = 0u;
static uint8_t liveRec_epochValid = 0u;
static uint8_t liveRec_latch[LIVEREC_TARGET_BYTES];
static uint8_t liveRec_dirty[LIVEREC_TARGET_BYTES];
static uint8_t liveRec_erase[LIVEREC_TARGET_BYTES];

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
 * liveRec_rollSpan is set; the start and the end drain the ring first
 * (liveRec_syncRing()), so both are decided on the capture's step. Why:
 * P1B rewrote the start step with timing 0 when it ended, and wrote a roll
 * onto the step before a forward-quantised start; the end writer had no
 * start delay to keep. RAM: +2 B.
 * Affiliates: liveRec_triggerIn() (start), liveRec_service() (span),
 * liveRec_endRoll().
 */
static uint8_t  liveRec_rollHeld = 0u;
static uint8_t  liveRec_rollStep = 0u;
static uint8_t  liveRec_rollIndex = 0u;
static uint8_t  liveRec_rollStartDelay = 0u;
static uint8_t  liveRec_rollSpan = 0u;

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
static int32_t liveRec_graceDeltaQ8;
static uint32_t liveRec_graceTick;
static uint8_t liveRec_graceVel = 0u;
static uint8_t liveRec_graceValid = 0u;

static uint8_t liveRec_rollNote = 0u;
static uint8_t liveRec_rollNoteDefault = 0u;
static uint8_t liveRec_rollVel = 0u;

/* Last boundary-ring step per track, so the next entry ends it (S081 C-9a). */
static uint8_t liveRec_prevStep[NUM_TRACKS];
static uint8_t liveRec_prevValid[NUM_TRACKS];

/*
 * S081 P1B/P1C added state (items 3/9/10/16/26; P1C 2/4/10/15). RAM about 14 B
 * (P1B 10 B + P1C 4 B), plus DEV-only counters.
 *  lastSceneEpoch/sceneEpochValid  last seen seq_getSceneEpoch() (LR-9);
 *  rollTrack  the track the open fast roll belongs to (LR-3);
 *  rollHits  retriggers seen since ROLL_START (LR-13 plain-trigger fallback);
 *  rollPendingIdx  a fast roll to start at the next ROLL_HIT (LR-14);
 *  rollStartDelay/rollSpan  the roll start's micro-timing and whether the
 *  playhead has reached the start step (P1C LC-7);
 *  eraseSeqCount/eraseSeqTrack  held SEQ erase buttons and the captured
 *  active track (LR-8); eraseFxSeqCount  held FX-mode SEQ buttons (LC-2).
 */
static uint8_t liveRec_lastSceneEpoch;
static uint8_t liveRec_sceneEpochValid;
static uint8_t liveRec_rollTrack = 0u;
static uint8_t liveRec_rollHits;
static uint8_t liveRec_rollPendingIdx;
static uint8_t liveRec_eraseSeqCount;
static uint8_t liveRec_eraseSeqTrack;
#if DEV_MODE_LOGGING
static uint16_t liveRec_drops[7];
#endif

/* Step-end table (F-5) and knob-erase removals (G-6); defined below. */
static void liveRec_stepEnd(uint8_t track, uint8_t step, uint8_t fired);
#if LIVEREC_AUTOMATION_FIRST_CHANGE
/* First-change writes (S081 P2 follow-up); defined beside the step-end table. */
static void liveRec_writeCurrentValue(uint8_t step, uint16_t target);
static void liveRec_writePendingFirst(uint8_t step);
#endif
/* End an open fast roll; defined below with the roll helpers (LR-13). */
static void liveRec_endRoll(uint8_t writeEnd);

/* Live-erase masks owned by LiveRecord, pushed to the Sequencer/Effects (G). */
static uint8_t  liveRec_eraseSpecials;
static uint8_t  liveRec_eraseFull;
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
static uint16_t liveRec_eraseKnobFxLanes;
static uint8_t  liveRec_eraseFxSeqCount;
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
#if DEV_MODE_LOGGING
static void liveRec_drop(uint8_t cause, uint8_t track, uint8_t step)
{
    if (cause < 7u)
        liveRec_drops[cause]++;
    patternTrace_record(PAT_TRACE_STAGE_LIVEREC_DROP, cause,
                        (uint32_t)(((uint32_t)track << 8u) | step));
}
#else
static void liveRec_drop(uint8_t cause, uint8_t track, uint8_t step)
{
    (void)cause;
    (void)track;
    (void)step;
}
#endif


/* Push the current erase masks to the Sequencer and EffectsManager. */
static void liveRec_pushEraseMasks(void)
{
    /*
     * Push the live-erase masks through the ownership gate (S081 §7, §10.2,
     * P1B item 21).
     *
     * What: only tracks that play the target Scene T receive their
     * specials/full erase bits; the FX-sequence erase lanes apply only when
     * T == A. The FX lane mask is 16 bits (lane 0 = Morph, 1..15 = parameter
     * lanes): the knob-erase lanes OR all lanes while an FX-mode SEQ hold is
     * down (P1C items 1/2). P1B cast it to 8 bits, dropping lanes 8..15.
     * Called on every gesture change and on every service pass, so a Scene
     * change re-gates held erases at once. Why: TIM3 clears bit 15 in the
     * track's played Scene directly. Ungated, a held TRACK erase on a track
     * playing another Scene wrote into a non-target Pattern (and its pool
     * reclaim was then refused). Inputs: gesture masks, seq_getTrackPlayedScene,
     * T, A. Outputs: seq_setEraseMasks(), effects_setLiveEraseLanes().
     * Accessors: liveRec_erase*Held(), liveRec_eraseParam(), liveRec_service().
     */
    uint8_t owned = 0u;
    uint8_t t;

    for (t = 0u; t < NUM_TRACKS; t++) {
        if (seq_getTrackPlayedScene(t) == liveRec_target)
            owned |= (uint8_t)(1u << t);
    }
    seq_setEraseMasks((uint8_t)(liveRec_eraseSpecials & owned),
                      (uint8_t)(liveRec_eraseFull & owned));
    effects_setLiveEraseLanes(
        (uint16_t)((liveRec_target == scene_getActiveIndex())
                   ? (uint16_t)(liveRec_eraseKnobFxLanes |
                                ((liveRec_eraseFxSeqCount != 0u) ? 0xFFFFu : 0u))
                   : 0u));
}

/* Step-roll tick table duplicated for the slow/fast classification (§5.4). */
static uint8_t liveRec_rollPpqTicks(uint8_t rate)
{
    static const uint8_t t[10] = {0u, 48u, 32u, 24u, 16u, 12u, 8u, 6u, 4u, 3u};

    return (rate <= 9u) ? t[rate] : 0u;
}

/*
 * Place a captured trigger on a track's step grid (S081 §8.3, E-1 step 5).
 *
 * What: projects the master tick to the latest step whose start is at or before
 * it, and returns the delay percent within that step (0..99). A rounded 100 goes
 * to the next step at delay 0. Returns 0 when the mode cannot be projected (rnd).
 */
/* Submit one recorded step (trigger ON) with the current target gate. */
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
static uint8_t liveRec_writeStep(uint8_t track, uint8_t step, uint8_t note,
                                 uint8_t noteIsDefault, uint8_t vel,
                                 uint8_t timing, uint8_t roll, uint8_t fill,
                                 uint8_t setFil)
{
    pat_record_spec_t spec;

    spec.note = note;
    spec.noteIsDefault = noteIsDefault;
    spec.velocity = vel;
    spec.timing = timing;
    spec.roll = roll;
    spec.fill = fill;
    spec.setFil = setFil;
    if (patSvc_recordStep(seq_getTrackPlayedScene(track), track, step, &spec)) {
        seq_markRecordedStep(track, seq_getTrackPlayedScene(track), step);
        return 1u;
    }
    liveRec_drop(4u, track, step);
    return 0u;
}

/*
 * Start a new take (S081 §9.2).
 *
 * What: clears the latch and dirty bitmaps and resets the take origin to the
 * current active Scene. Why: every take boundary (REC off, overdub entry/exit,
 * track or rtk change, any Scene change) discards pending automation intent.
 * The erase bitmap is not touched (live erase owns its own lifetime).
 */
static void liveRec_newTake(void)
{
    memset(liveRec_latch, 0, sizeof(liveRec_latch));
    memset(liveRec_dirty, 0, sizeof(liveRec_dirty));
    liveRec_origin = scene_getActiveIndex();
    liveRec_target = seq_getTrackPlayedScene(liveRec_rtk);
}

void liveRec_init(uint8_t activeTrack)
{
    liveRec_modeState = LIVEREC_MODE_OFF;
    liveRec_priorMode = LIVEREC_MODE_OFF;
    liveRec_rtk = (activeTrack < NUM_TRACKS) ? activeTrack : 0u;
    liveRec_target = seq_getTrackPlayedScene(liveRec_rtk);
    liveRec_origin = scene_getActiveIndex();
    liveRec_lastEpoch = 0u;
    liveRec_epochValid = 0u;
    memset(liveRec_latch, 0, sizeof(liveRec_latch));
    memset(liveRec_dirty, 0, sizeof(liveRec_dirty));
    memset(liveRec_erase, 0, sizeof(liveRec_erase));
}

/* Position epoch -> end per-step capture state (latches/erase kept). */
static void liveRec_syncPosition(void)
{
    uint8_t posEpoch = seq_getPlaybackEpoch();

    if (!liveRec_epochValid) {
        liveRec_epochValid = 1u;
        liveRec_lastEpoch = posEpoch;
    } else if (posEpoch != liveRec_lastEpoch) {
        liveRec_lastEpoch = posEpoch;
        memset(liveRec_prevValid, 0, sizeof(liveRec_prevValid));
        liveRec_endRoll(0u);
        liveRec_graceValid = 0u;
    }
}

/* Boundary-ring drain: span writes and the step-end tables. */
static void liveRec_drainRing(void)
{
    seq_rec_boundary_t b;

    while (seq_takeRecBoundary(&b)) {
        if (b.track < NUM_TRACKS && liveRec_prevValid[b.track]) {
            uint8_t prev = liveRec_prevStep[b.track];

            /*
             * Extend a held fast roll over the step that just ended
             * (S081 §8.8 E-4, P1B items 3/16, P1C item 15).
             *
             * What: when the rolled track's next ring entry arrives, its
             * previous step has ended under the held roll. That step is
             * written as an active step with the roll rate, timing 0 and
             * fill 100 (carry-over), but only if the roll's start step has
             * been reached (liveRec_rollSpan) and the ended step is not the
             * start step itself. The start step keeps the delay and fill it
             * was written with at the start; the end step is written by
             * liveRec_endRoll(). fil applies while PERF is held. Roll start
             * and end drain the ring first (liveRec_syncRing()), so prev is
             * never a step from before the capture. Why: the steps the roll
             * spans must replay it. P1B rewrote the start step with timing 0
             * and, for a forward-quantised start, wrote a roll on the step
             * before it. Affiliates: seq_takeRecBoundary(),
             * liveRec_writeStep(), LC-7 state.
             */
            if (liveRec_rollHeld && b.track == liveRec_rollTrack &&
                liveRec_rollSpan && prev != liveRec_rollStep) {
                liveRec_writeStep(liveRec_rollTrack, prev, liveRec_rollNote,
                                  liveRec_rollNoteDefault, liveRec_rollVel,
                                  0u, liveRec_rollIndex, 100u,
                                  seq_fillActive);
            }
            liveRec_stepEnd(b.track, prev, b.fired);
        }
#if LIVEREC_AUTOMATION_FIRST_CHANGE
        /*
         * First step of rtk after a start or playhead break (S081 P2
         * follow-up).
         *
         * What: changes made while no step of rtk was known are waiting as
         * dirty bits; their current values are written into the step that
         * starts with this entry, which then counts as having them.
         * Why: with the first-change rule a value is written into the step
         * it was made in; with no step known the next step start is the
         * nearest one. Affiliates: liveRec_paramIn(),
         * liveRec_writePendingFirst().
         */
        if (b.track == liveRec_rtk && b.track < NUM_TRACKS &&
            !liveRec_prevValid[b.track])
            liveRec_writePendingFirst(b.step);
#endif
        if (b.track < NUM_TRACKS) {
            liveRec_prevStep[b.track] = b.step;
            liveRec_prevValid[b.track] = 1u;
            /* LC-8: a forward-quantised roll start is reached here. */
            if (liveRec_rollHeld && b.track == liveRec_rollTrack &&
                b.step == liveRec_rollStep)
                liveRec_rollSpan = 1u;
        }
    }
}

/*
 * Bring the ring view up to a live capture (S081 §8.8, P1C review C-1).
 *
 * What: the service's position-epoch check and ring drain, also run at a
 * fast-roll start and a roll end, right after their live capture. Every
 * step that began before the capture already has its entry in the ring, so
 * afterwards liveRec_prevStep[] and liveRec_rollSpan match the capture.
 * Why: the 500 Hz drain lags the playhead by up to 2 ms; a roll started or
 * released in that window was decided on the previous step (span written
 * onto the step before the start, or a covered step left unwritten).
 * No drain while a filesystem replacement is pending (as the service).
 * Context: foreground only. Accessors: liveRec_triggerIn(),
 * liveRec_endRoll(). Affiliates: liveRec_syncPosition(),
 * liveRec_drainRing(), liveRec_service().
 */
static void liveRec_syncRing(void)
{
    liveRec_syncPosition();
    if (!patSvc_replacePending())
        liveRec_drainRing();
}

void liveRec_service(void)
{
    uint8_t sceneEpoch = seq_getSceneEpoch();

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

    /* 1. Scene epoch -> new take. */
    if (!liveRec_sceneEpochValid) {
        liveRec_sceneEpochValid = 1u;
        liveRec_lastSceneEpoch = sceneEpoch;
    } else if (sceneEpoch != liveRec_lastSceneEpoch) {
        liveRec_lastSceneEpoch = sceneEpoch;
        liveRec_newTake();
    }

    /* 2. Position epoch -> end per-step capture state (latches/erase kept). */
    liveRec_syncPosition();

    /* 3. A filesystem Pattern replacement suspends writing. */
    if (patSvc_replacePending()) {
        patSvc_releaseTarget();
        seq_setRecordTarget(liveRec_rtk, 0u);
        seq_setAutomationFilter(0, 0, 0);
        return;
    }

    /* 4. Target negotiation: T = P(rtk), override only while writing. */
    liveRec_target = seq_getTrackPlayedScene(liveRec_rtk);
    if (liveRec_patternWriting() && liveRec_target != scene_getActiveIndex()) {
        if (!patSvc_requestTarget(liveRec_target))
            liveRec_drop(1u, liveRec_rtk, 0u);
    } else {
        patSvc_releaseTarget();
    }
    seq_setRecordTarget(liveRec_rtk, liveRec_patternWriting());
    if (liveRec_armed())
        seq_setAutomationFilter(liveRec_latch, liveRec_erase, liveRec_rtk);
    else
        seq_setAutomationFilter(0, 0, 0);
    /* LR-17: re-push erase masks every pass; ownership can change with Scene. */
    if (liveRec_modeState == LIVEREC_MODE_ERASE)
        liveRec_pushEraseMasks();

    /* 5. Boundary-ring drain. */
    liveRec_drainRing();

    /* 6. FX-sequence erase drain: one step at a time (P1B item 28). */
    {
        uint8_t fxStep;
        uint16_t lanes;

        while (effects_takeErasedLocks(&fxStep, &lanes))
            effects_clearSeqLanes(liveRec_origin,
                                  (uint16_t)(1u << fxStep), lanes);
    }
}

/*
 * Step-end table (S081 §9.3/§9.4, F-5) and knob-erase removals (G-6).
 *
 * Stub in Phase E; filled by Phase F (automation write/clear/re-stamp) and
 * Phase G (erase removals). Inputs: the ended step, its fired bit.
 */
/*
 * Read the current 7-bit menu value for a target (S081 §9.2, F-2a).
 *
 * What: reads the value the menu shows for the target, from the take's origin
 * Scene A (liveRec_origin). Instrument values come from the Normal endpoint
 * image; Scene targets use their SceneData getters; Effect targets use the
 * retained Normal endpoint. scl/shf come from A's Pattern region.
 */
static uint8_t liveRec_targetValue7(uint16_t target)
{
    uint8_t a = liveRec_origin;

    if (instrumentParam_isVoiceParameter(target)) {
        uint8_t slot = instrumentParam_slot(target);
        uint8_t local = instrumentParam_local(target);
        const kit_instrument_slot_t *instr = scene_instrumentSlotConst(a, slot);

        if (instr && local < INSTRUMENT_PARAM_COUNT)
            return instr->parameter_images.instrument_parameters[local];
        return 0u;
    }
    if (sceneModTarget_isSceneTarget(target)) {
        const scene_mod_target_descriptor_t *d =
            sceneModTarget_descriptor(target);
        uint8_t slot;

        if (!d)
            return 0u;
        slot = d->voice_slot;
        switch (d->kind) {
        case SCENE_MOD_TARGET_KIND_VOICE_MORPH: {
            uint8_t m = scene_getVoiceMorphAmount(a, slot);

            return (uint8_t)((m == 255u) ? 127u : (m / 2u));
        }
        case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
            return scene_getVoiceAudioOut(a, slot);
        case SCENE_MOD_TARGET_KIND_FX_SEND:
            return scene_getVoiceFxSendAmount(a, slot);
        case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            return scene_getSlot6Track7AmpEnvelopeDecay(a);
        case SCENE_MOD_TARGET_KIND_EFFECT_MORPH: {
            uint8_t m = scene_getEffectMorphAmount(a);

            return (uint8_t)((m == 255u) ? 127u : (m / 2u));
        }
        case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE: {
            const pat_scene_region_t *r = pat_sceneRegion(a);

            if (!r || slot >= NUM_TRACKS)
                return 0u;
            return (d->kind == SCENE_MOD_TARGET_KIND_TRACK_SCALE)
                ? r->track_scale[slot] : r->track_shuffle[slot];
        }
        default:
            return 0u;
        }
    }
    if (effectTarget_isEffectId(target)) {
        /* P1B item 18: WIDE8 retained values stored as their 7-bit inverse. */
        uint8_t local = effectTarget_local(target);

        return effects_storeValue7(a, local,
            effects_getParameter(a, local, EFFECT_IMAGE_NORMAL));
    }
    return 0u;
}

/* Value of an existing automation entry, or 0 with *present = 0. */
static uint8_t liveRec_autoValue(const pat_automation_entry_t *autos,
                                 uint8_t count, uint16_t target,
                                 uint8_t *present)
{
    uint8_t i;

    for (i = 0u; i < count; i++) {
        if (autos[i].target == target) {
            *present = 1u;
            return autos[i].value;
        }
    }
    *present = 0u;
    return 0u;
}

/* Nonzero when the target is an Instrument parameter of rtk's own slot. */
static uint8_t liveRec_isOwnInstrument(uint16_t target)
{
    uint8_t slot;

    if (!instrumentParam_isVoiceParameter(target))
        return 0u;
    slot = instrumentParam_slot(target);
    return (uint8_t)(slot == ((liveRec_rtk >= INSTRUMENT_SLOT_COUNT)
                             ? (INSTRUMENT_SLOT_COUNT - 1u) : liveRec_rtk));
}

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
static uint8_t liveRec_targetOwned(uint16_t target)
{
    uint8_t a = scene_getActiveIndex();

    if (instrumentParam_isVoiceParameter(target)) {
        uint8_t slot = instrumentParam_slot(target);
        uint8_t track;
        const kit_instrument_slot_t *slot_a;
        const kit_instrument_slot_t *slot_t;

        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        /* Slot 5 (voice 6/7) is played by rtk when rtk is 5 or 6, else by 5. */
        if (slot == (INSTRUMENT_SLOT_COUNT - 1u))
            track = (uint8_t)((liveRec_rtk >= 5u) ? liveRec_rtk : 5u);
        else
            track = slot;
        if (seq_getTrackPlayedScene(track) != liveRec_target)
            return 0u;
        slot_a = scene_instrumentSlotConst(a, slot);
        slot_t = scene_instrumentSlotConst(liveRec_target, slot);
        if (!slot_a || !slot_t || slot_a->type != slot_t->type)
            return 0u;
        return 1u;
    }
    /* Effect and Scene targets belong to the active Scene only. */
    return (uint8_t)(liveRec_target == a);
}

#if LIVEREC_AUTOMATION_FIRST_CHANGE
/*
 * Write one target's current value into one step of rtk now (S081 P2
 * follow-up, LIVEREC_AUTOMATION_FIRST_CHANGE).
 *
 * What: reads the target's current menu value (liveRec_targetValue7()) and
 * writes it into `step` of rtk in T = P(rtk), unless the step already holds
 * that value (L3: only real changes are queued; the read uses
 * pat_findStepAutomation(), so a write still in the service FIFO can cause
 * one redundant write, never a missing one). A successful write marks the
 * STEP underline (menu_automationTargetRecorded()); a refused one is a
 * counted drop (cause 4). fil (R10) is set on the step while PERF is held,
 * as the step-end table does for its writes. Why: the first-change rule
 * writes at the change, not at the step end. Inputs: step, target. Context:
 * foreground. Accessors: liveRec_paramIn(), liveRec_writePendingFirst().
 * Affiliates: liveRec_stepEnd() (the same write rules at the step end).
 */
static void liveRec_writeCurrentValue(uint8_t step, uint16_t target)
{
    uint8_t t = seq_getTrackPlayedScene(liveRec_rtk);
    uint8_t v = liveRec_targetValue7(target);
    uint8_t old;

    if (pat_findStepAutomation(t, liveRec_rtk, step, target, &old) &&
        old == v)
        return;
    if (patSvc_writeStepAutomation(t, liveRec_rtk, step, target, v))
        menu_automationTargetRecorded(t, liveRec_rtk, target);
    else
        liveRec_drop(4u, liveRec_rtk, step);
    if (seq_fillActive)
        patSvc_setStepCondition(t, liveRec_rtk, step, 21u);
}

/*
 * Write the waiting changes into the first known step (S081 P2 follow-up).
 *
 * What: in LATCH or OVERDUB, writes every dirty target's current value into
 * `step` (the first step of rtk after a transport start or a playhead
 * break); the dirty bits stay set, so that step counts as having its value
 * and the step end only clears them. Why: changes made while no step was
 * known (stopped, or between a break and the next step start) have no step
 * of their own; this also carries a change from the step a break cut short,
 * as the step-end rule (0) does. Inputs: the starting step. Context:
 * foreground, from liveRec_drainRing(). Affiliates: liveRec_paramIn(),
 * liveRec_writeCurrentValue().
 */
static void liveRec_writePendingFirst(uint8_t step)
{
    uint16_t target;

    if (liveRec_modeState != LIVEREC_MODE_LATCH &&
        liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    for (target = 0u; target < LIVEREC_TARGET_BITS; target++) {
        if (liveRec_dirty[target >> 3] & (uint8_t)(1u << (target & 7u)))
            liveRec_writeCurrentValue(step, target);
    }
}
#endif

/*
 * Step-end table (S081 §9.3/§9.4/§9.5, F-5).
 *
 * What: when the previous step of rtk has ended, apply the latch/overdub rules to
 * every set bit: LATCH writes latched values (and re-stamps own Instrument
 * parameters on fired steps, clears others), OVERDUB writes only dirty targets.
 * Only real changes are queued (L3): one pat_readStepAutomations() read answers
 * every target. fil (R10) is set when the step received a write.
 * S081 P2 follow-up: with LIVEREC_AUTOMATION_FIRST_CHANGE 1 a dirty target
 * was already written at its first change in this step (or at the step
 * start); the end only clears its dirty bit. The not-dirty LATCH rules
 * (re-stamp / clear) are unchanged.
 */
static void liveRec_stepEnd(uint8_t track, uint8_t step, uint8_t fired)
{
    uint8_t t;
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    uint8_t count;
    uint16_t target;
    uint8_t wrote = 0u;
    uint8_t overdub;

    if (track != liveRec_rtk)
        return;
    t = seq_getTrackPlayedScene(liveRec_rtk);
    count = pat_readStepAutomations(t, liveRec_rtk, step, autos,
                                    PAT_BLOCK_AUTO_COUNT_MASK);
    if (liveRec_modeState == LIVEREC_MODE_ERASE) {
        /* S081 §10.2/G-6: remove the erased targets present on the passed step. */
        for (target = 0u; target < LIVEREC_TARGET_BITS; target++) {
            uint8_t present;

            if ((liveRec_erase[target >> 3] &
                 (uint8_t)(1u << (target & 7u))) == 0u)
                continue;
            (void)liveRec_autoValue(autos, count, target, &present);
            if (present)
                if (!patSvc_removeStepAutomation(t, liveRec_rtk, step, target))
                    liveRec_drop(4u, liveRec_rtk, step);
        }
        return;
    }
    if (liveRec_modeState != LIVEREC_MODE_LATCH &&
        liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    overdub = (uint8_t)(liveRec_modeState == LIVEREC_MODE_OVERDUB);

    for (target = 0u; target < LIVEREC_TARGET_BITS; target++) {
        uint8_t bi = (uint8_t)(target >> 3u);
        uint8_t bit = (uint8_t)(1u << (target & 7u));
        uint8_t dirty = (uint8_t)(liveRec_dirty[bi] & bit);
        uint8_t latching = (uint8_t)(liveRec_latch[bi] & bit);
        uint8_t present;
        uint8_t old_val;

        if (overdub) {
            if (!dirty)
                continue;
        } else if (!latching) {
            continue;
        }
        old_val = liveRec_autoValue(autos, count, target, &present);

        if (dirty) {
            uint8_t v;

#if LIVEREC_AUTOMATION_FIRST_CHANGE
            /*
             * First-change rule (S081 P2 follow-up): this step already got
             * the value of its first change (liveRec_paramIn()) or of a
             * waiting change at its start (liveRec_writePendingFirst()).
             * The end only closes it; later changes are not written here.
             */
            liveRec_dirty[bi] &= (uint8_t)~bit;
            continue;
#endif
            v = liveRec_targetValue7(target);
            if (!present || old_val != v) {
                if (patSvc_writeStepAutomation(t, liveRec_rtk, step, target, v))
                    menu_automationTargetRecorded(t, liveRec_rtk, target);
                else
                    liveRec_drop(4u, liveRec_rtk, step);
                wrote = 1u;
            }
            liveRec_dirty[bi] &= (uint8_t)~bit;
            continue;
        }
        if (overdub)
            continue;               /* overdub never re-stamps or clears */
        /* LATCH, not dirty (and not overdub). */
        if (liveRec_isOwnInstrument(target)) {
            if (fired) {
                uint8_t v = liveRec_targetValue7(target);

                if (!present || old_val != v) {
                    if (patSvc_writeStepAutomation(t, liveRec_rtk, step, target,
                                                   v))
                        menu_automationTargetRecorded(t, liveRec_rtk, target);
                    else
                        liveRec_drop(4u, liveRec_rtk, step);
                    wrote = 1u;
                }
            } else if (present) {
                if (!patSvc_removeStepAutomation(t, liveRec_rtk, step, target))
                    liveRec_drop(4u, liveRec_rtk, step);
            }
        } else if (present) {
            if (!patSvc_removeStepAutomation(t, liveRec_rtk, step, target))
                liveRec_drop(4u, liveRec_rtk, step);
        }
    }
    if (wrote && seq_fillActive)
        patSvc_setStepCondition(t, liveRec_rtk, step, 21u);
}



/* Mode. */

void liveRec_setLatch(uint8_t on)
{
    if (liveRec_modeState == LIVEREC_MODE_OVERDUB ||
        liveRec_modeState == LIVEREC_MODE_ERASE)
        return;
    if (on) {
        liveRec_newTake();
        liveRec_modeState = LIVEREC_MODE_LATCH;
    } else {
        liveRec_modeState = LIVEREC_MODE_OFF;
        liveRec_newTake();
    }
}

void liveRec_overdubBegin(void)
{
    /* Erase and overdub are mutually exclusive (S081 §10.3). */
    if (liveRec_modeState == LIVEREC_MODE_ERASE ||
        liveRec_modeState == LIVEREC_MODE_OVERDUB)
        return;
    liveRec_priorMode = liveRec_modeState;
    liveRec_newTake();
    liveRec_modeState = LIVEREC_MODE_OVERDUB;
}

void liveRec_overdubEnd(void)
{
    if (liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    liveRec_modeState = liveRec_priorMode;
    liveRec_newTake();
}

/*
 * Force record and erase off (S081 P1 looper interim; P1C item 16).
 *
 * What: clears every erase source (knob bitmap, knob FX lanes, SEQ/FX-SEQ
 * hold counts, TRACK holds), pushes the empty masks and starts a new take.
 * Why: P1B left the SEQ-hold count set, so a later release could stretch
 * the next erase session. Accessor: handleSelectButton() (BH-7).
 * Affiliates: liveRec_eraseReleaseHeld().
 */
void liveRec_forceOff(void)
{
    liveRec_modeState = LIVEREC_MODE_OFF;
    liveRec_priorMode = LIVEREC_MODE_OFF;
    memset(liveRec_erase, 0, sizeof(liveRec_erase));
    liveRec_eraseKnobFxLanes = 0u;
    liveRec_eraseReleaseHeld();          /* counts, specials, full; pushes */
    liveRec_newTake();
}

void liveRec_eraseBegin(void)
{
    if (liveRec_modeState == LIVEREC_MODE_ERASE)
        return;
    /* Enabling erase ends an overdub; the prior LATCH/OFF still returns later. */
    if (liveRec_modeState != LIVEREC_MODE_OVERDUB)
        liveRec_priorMode = liveRec_modeState;
    liveRec_newTake();
    liveRec_modeState = LIVEREC_MODE_ERASE;
}

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
void liveRec_eraseEnd(void)
{
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    memset(liveRec_erase, 0, sizeof(liveRec_erase));
    liveRec_eraseKnobFxLanes = 0u;
    liveRec_eraseReleaseHeld();          /* counts, specials, full; pushes */
    liveRec_modeState = liveRec_priorMode;
    liveRec_newTake();
}

uint8_t liveRec_mode(void) { return liveRec_modeState; }
uint8_t liveRec_armed(void)
{
    return (uint8_t)(liveRec_modeState != LIVEREC_MODE_OFF);
}
uint8_t liveRec_patternWriting(void)
{
    return (uint8_t)(liveRec_armed() && seq_isRunning());
}

/* Recording track. */

void liveRec_onActiveTrackChanged(uint8_t track)
{
    if (track >= NUM_TRACKS)
        return;
    liveRec_rtk = track;
    liveRec_newTake();
}

void liveRec_setRecordTrack(uint8_t track)
{
    if (track >= NUM_TRACKS)
        return;
    liveRec_rtk = track;
    liveRec_newTake();
}

uint8_t liveRec_recordTrack(void) { return liveRec_rtk; }

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
uint8_t liveRec_activePatternLocked(void)
{
    return (uint8_t)(liveRec_patternWriting() &&
                     liveRec_target != scene_getActiveIndex());
}

/* Triggers / automation / erase entry points (Phase E/F/G fill these). */

/*
 * Commit one placed hit (S081 §8.4/§8.6, E-1 steps 7-10).
 *
 * What: writes the step (trigger ON) through patSvc_recordStep, then applies the
 * echo rules: a write onto the current step cancels a pending deferred trigger;
 * a write ahead of the playhead suppresses the stored trigger once. The LED is
 * marked by liveRec_writeStep on success.
 */
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
static uint8_t liveRec_commitHit(uint8_t track, const seq_place_t *pl,
                                 uint8_t note, uint8_t noteIsDefault,
                                 uint8_t vel, uint8_t roll, uint8_t setFil)
{
    if (!liveRec_writeStep(track, pl->step, note, noteIsDefault, vel,
                           pl->delay, roll, (uint8_t)(roll ? 100u : 0u),
                           setFil))
        return 0u;
    if (pl->rel > 0)
        seq_suppressStepOnce(track, pl->step, pl->ahead);
    else if (pl->rel == 0)
        seq_cancelPendingTrigger(track, pl->step);
    return 1u;
}

/* Sub-tick fraction (0..255) of the capture within the current PPQ tick. */
static uint8_t liveRec_frac8(const seq_track_time_t *snap)
{
    uint32_t f;

    if (snap->periodUs == 0u)
        return 0u;
    f = (uint32_t)(((snap->nowUs - snap->stampUs) * 256u) / snap->periodUs);
    return (uint8_t)((f > 255u) ? 255u : f);
}

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
        /* Span every step that ended before the release; a position break
         * found here has already ended the roll (P1C review C-1). */
        liveRec_syncRing();
        if (!liveRec_rollHeld)
            return;
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

/*
 * Capture one live hit and record it (S081 §8, P1B items 5/11/17).
 *
 * What: gates (LATCH/OVERDUB, running, P1 looper lockout, P(track) == T,
 * same instrument type in A and T); captures the track's live state with
 * the µs time; derives the sub-tick fraction; quantises on the master grid
 * (1/8..1/64) or snaps to the track's own steps (trk); places the hit with
 * seq_placeFromSnapshot() (live cursor, nominal step grid, run-mode next
 * step; shuffle applies only at playback); then records it by kind
 * (single, flam, roll). Why: placement
 * must follow the track that is playing, not its canonical master-clock
 * position; the type check keeps an Instrument trigger from being recorded
 * into a Scene whose slot holds a different instrument (plan §7). Inputs:
 * liveRec_trigger_t. Outputs: Pattern writes, echo marks, live hold, LED.
 * Accessors: buttonHandler overlay (E-2), midiParser_noteOn().
 * Affiliates: seq_captureTrackTime(), seq_quantiseTick(),
 * seq_placeFromSnapshot(), liveRec_commitHit(), liveRec_placeFlam().
 */
void liveRec_triggerIn(const liveRec_trigger_t *t)
{
    seq_track_time_t snap;
    seq_place_t pl;
    uint8_t track;
    uint8_t frac8;
    int32_t deltaQ8;
    uint8_t snapToStep = 0u;
    uint8_t setFil;

    if (!t || t->track >= NUM_TRACKS)
        return;
    track = t->track;
    if (liveRec_modeState != LIVEREC_MODE_LATCH &&
        liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    if (!seq_isRunning())
        return;
    if (seq_loopActive)
        return;                       /* P1 interim looper lockout (P2 removes) */
    if (seq_getTrackPlayedScene(track) != liveRec_target)
        return;                       /* S081 §7 ownership */

    /* Slot-type check when recording into a per-track Scene T != A (item 11). */
    if (liveRec_target != scene_getActiveIndex()) {
        uint8_t slot = (uint8_t)((track >= INSTRUMENT_SLOT_COUNT)
                                 ? (INSTRUMENT_SLOT_COUNT - 1u) : track);
        const kit_instrument_slot_t *slot_a =
            scene_instrumentSlotConst(scene_getActiveIndex(), slot);
        const kit_instrument_slot_t *slot_t =
            scene_instrumentSlotConst(liveRec_target, slot);

        if (slot_a && slot_t && slot_a->type != slot_t->type) {
            liveRec_drop(2u, track, 0u);
            return;
        }
    }

    seq_captureTrackTime(track, &snap);
    frac8 = liveRec_frac8(&snap);
    setFil = seq_fillActive;

    /* Quantise: master grid for 1/8..1/64, own steps for trk, none for off. */
    switch (seq_getQuantisation()) {
    case QUANT_TRACK:
        deltaQ8 = frac8;
        snapToStep = 1u;
        break;
    case QUANT_8:
    case QUANT_16:
    case QUANT_32:
    case QUANT_64: {
        uint32_t q = seq_quantiseTick(snap.elapsedTick, frac8);

        deltaQ8 = (int32_t)(((int32_t)q - (int32_t)snap.elapsedTick) * 256);
        break; }
    default:
        deltaQ8 = frac8;
        break;
    }
    if (!seq_placeFromSnapshot(track, &snap, deltaQ8, snapToStep, &pl)) {
        liveRec_drop(2u, track, 0u);
        return;
    }

    if (t->kind == LIVEREC_HIT_ROLL_HIT && liveRec_rollHeld) {
        /* Fast roll: spanned by E-4, no per-hit write. */
        liveRec_rollHits++;
        return;
    }
    if (t->kind == LIVEREC_HIT_ROLL_START || t->kind == LIVEREC_HIT_ROLL_HIT) {
        uint8_t rollIndex;
        uint8_t rollTicks;
        uint16_t stepTicks;

        if (t->kind == LIVEREC_HIT_ROLL_HIT) {
            /* A single/flam -> roll switch starts the roll at this hit. */
            if (liveRec_rollPendingIdx == 0u) {
                (void)liveRec_commitHit(track, &pl, t->note,
                                        t->note_is_default, t->velocity, 0u,
                                        setFil);
                return;
            }
            rollIndex = liveRec_rollPendingIdx;
            liveRec_rollPendingIdx = 0u;
        } else {
            rollIndex = t->roll;
        }
        stepTicks = (uint16_t)(snap.interval >> 8u);
        if (stepTicks == 0u)
            stepTicks = 1u;
        rollTicks = liveRec_rollPpqTicks(rollIndex);
        if (rollTicks == 0u || rollTicks >= stepTicks) {
            /* Slow roll: recorded hit by hit (F-L). */
            (void)liveRec_commitHit(track, &pl, t->note, t->note_is_default,
                                    t->velocity, 0u, setFil);
            return;
        }
        /* Fast roll: live-hold and record the start step with the rate.
         * Drain first so prevStep is the capture's step (P1C review C-1). */
        liveRec_syncRing();
        seq_setLiveHold(track, 1u);
        liveRec_rollHeld = 1u;
        liveRec_rollTrack = track;
        liveRec_rollStep = pl.step;
        liveRec_rollStartDelay = pl.delay;
        /* A forward-quantised start is reached at its own ring entry (LC-8). */
        liveRec_rollSpan = (uint8_t)(pl.rel <= 0);
        liveRec_rollIndex = rollIndex;
        liveRec_rollHits = 0u;
        liveRec_rollNote = t->note;
        liveRec_rollNoteDefault = t->note_is_default;
        liveRec_rollVel = t->velocity;
        (void)liveRec_commitHit(track, &pl, t->note, t->note_is_default,
                                t->velocity, rollIndex, setFil);
        return;
    }
    if (t->kind == LIVEREC_HIT_FLAM_GRACE) {
        liveRec_graceDeltaQ8 = deltaQ8;
        liveRec_graceTick = snap.elapsedTick;
        liveRec_graceVel = t->velocity;
        liveRec_graceValid = 1u;
        return;
    }
    if (t->kind == LIVEREC_HIT_FLAM_MAIN) {
        liveRec_placeFlam(track, &snap, &pl, deltaQ8, t, setFil);
        return;
    }
    (void)liveRec_commitHit(track, &pl, t->note, t->note_is_default,
                            t->velocity, 0u, setFil);
}

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
void liveRec_rollRateChange(uint8_t track, uint8_t rollIndex)
{
    seq_track_time_t snap;
    uint16_t stepTicks;
    uint8_t rollTicks;

    if (rollIndex == 0u) {
        liveRec_endRoll(1u);
        return;
    }
    seq_captureTrackTime(track, &snap);
    stepTicks = (uint16_t)(snap.interval >> 8u);
    if (stepTicks == 0u)
        stepTicks = 1u;
    rollTicks = liveRec_rollPpqTicks(rollIndex);
    if (rollTicks == 0u || rollTicks >= stepTicks) {
        /* New rate is slow: end any fast roll; later hits record as singles. */
        if (liveRec_rollHeld)
            liveRec_endRoll(1u);
        liveRec_rollPendingIdx = 0u;
        return;
    }
    if (liveRec_rollHeld)
        liveRec_rollIndex = rollIndex;       /* fast -> fast */
    else
        liveRec_rollPendingIdx = rollIndex;  /* single/flam -> roll */
}

void liveRec_rollRelease(uint8_t track)
{
    (void)track;
    liveRec_endRoll(1u);
}


/*
 * Mark an adjusted parameter for recording (S081 §9, P1B item 11).
 *
 * What: every recordable gesture (pots, encoder; MIDI CC later) marks its
 * target dirty, and in LATCH also latched, provided the target belongs to
 * the recording target Scene. Why: see liveRec_targetOwned(). The value is
 * not passed: the write reads the current menu value (callers commit the
 * edit first). Inputs: target ID, source. Outputs: liveRec_dirty/latch bits.
 * Accessors: menu_liveRecRecordCell(), midiParser_enterTaggedParameter().
 * Affiliates: liveRec_stepEnd(), liveRec_targetValue7().
 *
 * S081 P2 follow-up (LIVEREC_AUTOMATION_FIRST_CHANGE 1): the FIRST change of
 * a target in a step is written at once into the step rtk is playing (the
 * live step from seq_captureTrackTime(), after the ring is brought up to
 * date so the previous step's end has run); the dirty bit then marks the
 * step as having its value and further changes in the step are ignored for
 * it. With no step known (not writing, Pattern replacement pending, or no
 * ring entry for rtk since a start/break) the change waits as a dirty bit
 * and liveRec_writePendingFirst() writes it at the next step start. With 0
 * the step end writes the value held then (P1 rule).
 */
void liveRec_paramIn(uint16_t target9, uint8_t source)
{
    (void)source;
    if (target9 >= LIVEREC_TARGET_BITS)
        return;
    if (liveRec_modeState != LIVEREC_MODE_LATCH &&
        liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    if (!liveRec_targetOwned(target9)) {
        liveRec_drop(2u, liveRec_rtk, (uint8_t)(target9 & 0x7Fu));
        return;
    }
#if LIVEREC_AUTOMATION_FIRST_CHANGE
    {
        uint8_t bit = (uint8_t)(1u << (target9 & 7u));
        uint8_t stepKnown = 0u;

        if (liveRec_modeState == LIVEREC_MODE_LATCH)
            liveRec_latch[target9 >> 3] |= bit;
        /* Run any pending step ends first, so the dirty bit below belongs
         * to the step that is playing now. */
        if (liveRec_patternWriting() && !patSvc_replacePending()) {
            liveRec_syncRing();
            stepKnown = liveRec_prevValid[liveRec_rtk];
        }
        if (liveRec_dirty[target9 >> 3] & bit)
            return;                 /* this step already has its value */
        liveRec_dirty[target9 >> 3] |= bit;
        if (stepKnown) {
            seq_track_time_t now;

            seq_captureTrackTime(liveRec_rtk, &now);
            if (now.stepIndex >= 0)
                liveRec_writeCurrentValue((uint8_t)now.stepIndex, target9);
        }
    }
#else
    liveRec_dirty[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
    if (liveRec_modeState == LIVEREC_MODE_LATCH)
        liveRec_latch[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
#endif
}

/*
 * Knob erase target mark (S081 §10.2, G-2; P1C item 2).
 *
 * What: adds the target to the erase bitmap (removed from rtk step by step)
 * and, for an Effect cell, its FX lane to the knob-erase lane mask. Both
 * stay until erase ends; SEQ/TRACK releases and MODE presses do not clear
 * them. Affiliates: menu_parseKnobDelta() (erase branch),
 * liveRec_pushEraseMasks().
 */
void liveRec_eraseParam(uint16_t target9, uint8_t fxLane)
{
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    if (target9 < LIVEREC_TARGET_BITS)
        liveRec_erase[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
    if (fxLane < 16u) {
        liveRec_eraseKnobFxLanes |= (uint16_t)(1u << fxLane);
        liveRec_pushEraseMasks();
    }
}

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
void liveRec_eraseSeqHeld(uint8_t track, uint8_t held)
{
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    if (held) {
        if (liveRec_eraseSeqCount == 0u)
            liveRec_eraseSeqTrack = track;
        liveRec_eraseSeqCount++;
        liveRec_eraseSpecials |= (uint8_t)(1u << liveRec_eraseSeqTrack);
    } else if (liveRec_eraseSeqCount > 0u) {
        liveRec_eraseSeqCount--;
        if (liveRec_eraseSeqCount == 0u)
            liveRec_eraseSpecials &= (uint8_t)~(1u << liveRec_eraseSeqTrack);
    }
    liveRec_pushEraseMasks();
}

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

void liveRec_eraseTrackHeld(uint8_t track, uint8_t held)
{
    /* TRACK hold erase: the whole track, automation included (S081 §10.2). */
    if (liveRec_modeState != LIVEREC_MODE_ERASE || track >= NUM_TRACKS)
        return;
    if (held)
        liveRec_eraseFull |= (uint8_t)(1u << track);
    else
        liveRec_eraseFull &= (uint8_t)~(1u << track);
    liveRec_pushEraseMasks();
}

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
void liveRec_eraseReleaseHeld(void)
{
    liveRec_eraseSeqCount = 0u;
    liveRec_eraseFxSeqCount = 0u;
    liveRec_eraseSpecials = 0u;
    liveRec_eraseFull = 0u;
    liveRec_pushEraseMasks();
}
