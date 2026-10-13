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
#include <string.h>

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
 * Open fast-roll capture (S081 §8.8, E-4).
 *
 * What: when a fast roll starts, liveRec_rollHeld is 1 and liveRec_rollStep/
 * liveRec_rollIndex/liveRec_rollStartTick describe the step the roll began on.
 * E-4 extends it across the steps the playhead reaches until liveRec_rollRelease().
 */
static uint8_t  liveRec_rollHeld = 0u;
static uint8_t  liveRec_rollStep = 0u;
static uint8_t  liveRec_rollIndex = 0u;

/* Pending flam grace (S081 §8.7, E-1 step 6). */
static uint8_t liveRec_graceStep = 0u;
static uint8_t liveRec_graceDelay = 0u;
static uint8_t liveRec_graceVel = 0u;
static uint8_t liveRec_graceValid = 0u;

static uint8_t liveRec_rollNote = 0u;
static uint8_t liveRec_rollVel = 0u;

/* Last boundary-ring step per track, so the next entry ends it (S081 C-9a). */
static uint8_t liveRec_prevStep[NUM_TRACKS];
static uint8_t liveRec_prevValid[NUM_TRACKS];

/* Step-end table (F-5) and knob-erase removals (G-6); defined below. */
static void liveRec_stepEnd(uint8_t track, uint8_t step, uint8_t fired);

/* Live-erase masks owned by LiveRecord, pushed to the Sequencer/Effects (G). */
static uint8_t  liveRec_eraseSpecials;
static uint8_t  liveRec_eraseFull;
static uint16_t liveRec_eraseFxLanes;

/* Push the current erase masks to the Sequencer and EffectsManager. */
static void liveRec_pushEraseMasks(void)
{
    seq_setEraseMasks(liveRec_eraseSpecials, liveRec_eraseFull);
    effects_setLiveEraseLanes(liveRec_eraseFxLanes);
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
static uint8_t liveRec_place(uint8_t track, uint32_t tick,
                             const seq_track_time_t *snap,
                             uint8_t *stepOut, uint8_t *delayOut)
{
    uint8_t step = 0u;
    uint16_t offQ8 = 0u;
    uint16_t interval;
    uint8_t len;
    uint32_t delay;

    if (!seq_projectStep(track, tick, &step, &offQ8))
        return 0u;
    interval = snap->interval;
    if (interval == 0u)
        interval = 1u;
    len = snap->length ? snap->length : 16u;
    delay = ((uint32_t)offQ8 * 100u) / interval;
    if (delay >= 100u) {
        step = (uint8_t)((step + 1u) % len);
        delay = 0u;
    }
    *stepOut = step;
    *delayOut = (uint8_t)delay;
    return 1u;
}

/* Submit one recorded step (trigger ON) with the current target gate. */
static void liveRec_writeStep(uint8_t track, uint8_t step, uint8_t note,
                              uint8_t vel, uint8_t timing, uint8_t roll,
                              uint8_t fill, uint8_t setFil)
{
    pat_record_spec_t spec;

    spec.note = note;
    spec.velocity = vel;
    spec.timing = timing;
    spec.roll = roll;
    spec.fill = fill;
    spec.setFil = setFil;
    spec.noteIsDefault = 0u;
    if (patSvc_recordStep(seq_getTrackPlayedScene(track), track, step, &spec)) {
        seq_markRecordedStep(track, seq_getTrackPlayedScene(track), step);
    }
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

void liveRec_service(void)
{
    uint8_t epoch = seq_getPlaybackEpoch();

    /*
     * S081 P1 live-record service (plan §19.2, H-1).
     *
     * Order: (1) take boundary; (2) target negotiation; (3) boundary-ring drain
     * (E-4 roll spans; F-5/G-6 step-end tables). Runs before patSvc_tick() so
     * idle-direct writes execute in the same pass.
     */

    /* 1. Take boundary (epoch change is the single catch-all). */
    if (!liveRec_epochValid) {
        liveRec_epochValid = 1u;
        liveRec_lastEpoch = epoch;
    } else if (epoch != liveRec_lastEpoch) {
        liveRec_lastEpoch = epoch;
        liveRec_newTake();
        liveRec_rollHeld = 0u;
        liveRec_graceValid = 0u;
        memset(liveRec_prevValid, 0, sizeof(liveRec_prevValid));
        seq_setLiveHold(liveRec_rtk, 0u);
        seq_setEraseMasks(0u, 0u);
        effects_setLiveEraseLanes(0u);
    }

    /* A filesystem Pattern replacement suspends writing (H-1, Appendix A-11). */
    if (patSvc_replacePending()) {
        patSvc_releaseTarget();
        seq_setRecordTarget(liveRec_rtk, 0u);
        seq_setAutomationFilter(0, 0, 0);
        return;
    }

    /* 2. Target negotiation: T = P(rtk). */
    liveRec_target = seq_getTrackPlayedScene(liveRec_rtk);
    if (liveRec_armed()) {
        if (liveRec_target != scene_getActiveIndex())
            (void)patSvc_requestTarget(liveRec_target);
        else
            patSvc_releaseTarget();
        seq_setRecordTarget(liveRec_rtk, liveRec_patternWriting());
        seq_setAutomationFilter(liveRec_latch, liveRec_dirty, liveRec_rtk);
    } else {
        patSvc_releaseTarget();
        seq_setRecordTarget(liveRec_rtk, 0u);
        seq_setAutomationFilter(0, 0, 0);
    }

    /* 3. Boundary-ring drain. */
    {
        seq_rec_boundary_t b;

        while (seq_takeRecBoundary(&b)) {
            if (b.track < NUM_TRACKS && liveRec_prevValid[b.track]) {
                uint8_t prev = liveRec_prevStep[b.track];

                /*
                 * E-4 fast-roll span: the previous step of the live-hold track
                 * has ended; write it as a carry-over step (trigger + rate, fill
                 * 100) so the roll spans the step.
                 */
                if (liveRec_rollHeld && b.track == liveRec_rtk) {
                    liveRec_writeStep(liveRec_rtk, prev, liveRec_rollNote,
                                      liveRec_rollVel, 0u, liveRec_rollIndex,
                                      100u, 0u);
                }
                liveRec_stepEnd(b.track, prev, b.fired);
            }
            if (b.track < NUM_TRACKS) {
                liveRec_prevStep[b.track] = b.step;
                liveRec_prevValid[b.track] = 1u;
            }
        }
    }

    /* 4. FX-sequence erase drain (S081 §10.4, G-3d). */
    {
        uint16_t stepMask = 0u;
        uint16_t laneMask = 0u;

        while (effects_takeErasedLocks(&stepMask, &laneMask))
            effects_clearSeqLanes(liveRec_origin, stepMask, laneMask);
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
    if (effectTarget_isEffectId(target))
        return effects_getParameter(a, effectTarget_local(target),
                                    EFFECT_IMAGE_NORMAL);
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
 * Step-end table (S081 §9.3/§9.4/§9.5, F-5).
 *
 * What: when the previous step of rtk has ended, apply the latch/overdub rules to
 * every set bit: LATCH writes latched values (and re-stamps own Instrument
 * parameters on fired steps, clears others), OVERDUB writes only dirty targets.
 * Only real changes are queued (L3): one pat_readStepAutomations() read answers
 * every target. fil (R10) is set when the step received a write.
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
                patSvc_removeStepAutomation(t, liveRec_rtk, step, target);
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
            uint8_t v = liveRec_targetValue7(target);

            if (!present || old_val != v) {
                patSvc_writeStepAutomation(t, liveRec_rtk, step, target, v);
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
                    patSvc_writeStepAutomation(t, liveRec_rtk, step, target, v);
                    wrote = 1u;
                }
            } else if (present) {
                patSvc_removeStepAutomation(t, liveRec_rtk, step, target);
            }
        } else if (present) {
            patSvc_removeStepAutomation(t, liveRec_rtk, step, target);
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

void liveRec_forceOff(void)
{
    liveRec_modeState = LIVEREC_MODE_OFF;
    liveRec_priorMode = LIVEREC_MODE_OFF;
    memset(liveRec_erase, 0, sizeof(liveRec_erase));
    liveRec_eraseSpecials = 0u;
    liveRec_eraseFull = 0u;
    liveRec_eraseFxLanes = 0u;
    liveRec_pushEraseMasks();
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

void liveRec_eraseEnd(void)
{
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    memset(liveRec_erase, 0, sizeof(liveRec_erase));
    liveRec_eraseSpecials = 0u;
    liveRec_eraseFull = 0u;
    liveRec_eraseFxLanes = 0u;
    liveRec_pushEraseMasks();
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

uint8_t liveRec_activePatternLocked(void)
{
    /* Q13: locked when the recording target Scene differs from the active one. */
    return (uint8_t)(seq_getTrackPlayedScene(liveRec_rtk) !=
                     scene_getActiveIndex());
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
static void liveRec_commitHit(uint8_t track, uint8_t step, uint8_t delay,
                              uint8_t note, uint8_t vel, uint8_t roll,
                              uint8_t curStep, uint8_t setFil)
{
    liveRec_writeStep(track, step, note, vel, delay, roll,
                      (uint8_t)(roll ? 100u : 0u), setFil);
    if (step == curStep)
        seq_cancelPendingTrigger(track);
    else
        seq_suppressStepOnce(track, step);
}

void liveRec_triggerIn(const liveRec_trigger_t *t)
{
    seq_track_time_t snap;
    uint8_t track;
    uint32_t qt;
    uint8_t step = 0u;
    uint8_t delay = 0u;
    uint8_t curStep;
    uint8_t setFil;

    /*
     * Trigger capture entry point (S081 §8, §19.2, E-1).
     *
     * Gate (LATCH/OVERDUB, transport running, P1 looper lockout, ownership
     * P(track) == T); capture; slow/fast roll classification; quantise; place;
     * flam pairing; same-step rule; fil; write; echo/cancel; LED.
     */
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

    seq_captureTrackTime(track, &snap);
    qt = seq_quantiseTick(track, snap.elapsedTick);
    if (!liveRec_place(track, qt, &snap, &step, &delay)) {
        if (snap.stepIndex < 0)
            return;
        step = (uint8_t)snap.stepIndex;
        delay = 0u;
    }
    curStep = (snap.stepIndex >= 0) ? (uint8_t)snap.stepIndex : step;
    setFil = seq_fillActive;

    if (t->kind == LIVEREC_HIT_ROLL_START) {
        uint8_t rollTicks = liveRec_rollPpqTicks(t->roll);
        uint16_t stepTicks = (uint16_t)(snap.interval >> 8u);

        if (stepTicks == 0u)
            stepTicks = 1u;
        if (rollTicks == 0u || rollTicks >= stepTicks) {
            /* Slow roll: recorded hit by hit (F-L). */
            liveRec_commitHit(track, step, delay, t->note, t->velocity, 0u,
                              curStep, setFil);
        } else {
            /* Fast roll: live-hold and record the start step with the rate. */
            seq_setLiveHold(track, 1u);
            liveRec_rollHeld = 1u;
            liveRec_rollStep = step;
            liveRec_rollIndex = t->roll;
            liveRec_rollNote = t->note;
            liveRec_rollVel = t->velocity;
            liveRec_commitHit(track, step, delay, t->note, t->velocity, t->roll,
                              curStep, setFil);
        }
        return;
    }
    if (t->kind == LIVEREC_HIT_ROLL_HIT) {
        if (liveRec_rollHeld)
            return;                   /* fast roll: spanned by E-4 */
        liveRec_commitHit(track, step, delay, t->note, t->velocity, 0u,
                          curStep, setFil);
        return;
    }
    if (t->kind == LIVEREC_HIT_FLAM_GRACE) {
        liveRec_graceStep = step;
        liveRec_graceDelay = delay;
        liveRec_graceVel = t->velocity;
        liveRec_graceValid = 1u;
        return;
    }
    if (t->kind == LIVEREC_HIT_FLAM_MAIN) {
        if (liveRec_graceValid && liveRec_graceStep != step)
            liveRec_commitHit(track, liveRec_graceStep, liveRec_graceDelay,
                              t->note, liveRec_graceVel, 0u, curStep, setFil);
        liveRec_graceValid = 0u;
        liveRec_commitHit(track, step, delay, t->note, t->velocity, 0u,
                          curStep, setFil);
        return;
    }
    liveRec_commitHit(track, step, delay, t->note, t->velocity, 0u,
                      curStep, setFil);
}

void liveRec_rollRateChange(uint8_t track, uint8_t rollIndex)
{
    /* A held fast roll changed rate (S081 §8.8, F-C); 0 ends the roll. */
    (void)track;
    if (!liveRec_rollHeld)
        return;
    if (rollIndex == 0u) {
        liveRec_rollRelease(track);
        return;
    }
    liveRec_rollIndex = rollIndex;
}

void liveRec_rollRelease(uint8_t track)
{
    /* Held roll released: clear the live hold and end the recorded roll. */
    (void)track;
    if (!liveRec_rollHeld)
        return;
    seq_setLiveHold(liveRec_rtk, 0u);
    liveRec_rollHeld = 0u;
}


void liveRec_paramIn(uint16_t target9, uint8_t source)
{
    /*
     * Automation record hook (S081 §9, §19.2, F-1).
     *
     * What: marks the target dirty (every adjustment) and, in LATCH mode, latched
     * so the step-end table writes or re-stamps it. OVERDUB marks dirty only.
     */
    (void)source;
    if (target9 >= LIVEREC_TARGET_BITS)
        return;
    if (liveRec_modeState != LIVEREC_MODE_LATCH &&
        liveRec_modeState != LIVEREC_MODE_OVERDUB)
        return;
    liveRec_dirty[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
    if (liveRec_modeState == LIVEREC_MODE_LATCH)
        liveRec_latch[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
}

void liveRec_eraseParam(uint16_t target9, uint8_t fxLane)
{
    /* Knob erase target mark (S081 §10.2, G-2). */
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    if (target9 < LIVEREC_TARGET_BITS)
        liveRec_erase[target9 >> 3] |= (uint8_t)(1u << (target9 & 7u));
    if (fxLane < 16u) {
        liveRec_eraseFxLanes |= (uint16_t)(1u << fxLane);
        liveRec_pushEraseMasks();
    }
}

void liveRec_eraseSeqHeld(uint8_t held)
{
    /* SEQ hold erase: the active track's triggers/specials (S081 §10.2). */
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    if (held)
        liveRec_eraseSpecials |= (uint8_t)(1u << liveRec_rtk);
    else
        liveRec_eraseSpecials &= (uint8_t)~(1u << liveRec_rtk);
    liveRec_pushEraseMasks();
}

void liveRec_eraseFxSeqHeld(uint8_t held)
{
    /* FX-mode SEQ hold erase: every FX lock lane (S081 §10.2). */
    if (liveRec_modeState != LIVEREC_MODE_ERASE)
        return;
    liveRec_eraseFxLanes = (uint16_t)(held ? 0xFFFFu : 0u);
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
