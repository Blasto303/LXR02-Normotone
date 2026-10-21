/*
 * Core/DSP/Effects/DlyVerb/DlyVerbEffect.h
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef DLY_VERB_EFFECT_H_
#define DLY_VERB_EFFECT_H_

#include "EffectsManager.h"

/*
 * DlyVerb arena geometry.
 *
 * The whole type runs at half the codec rate (22,054 Hz) between a 2:1
 * decimator and a 1:2 interpolator, which halves both its CPU and the bytes
 * per second of delay. The Effect share holds, from its base:
 * - DLYVERB_STATE_BYTES of DSP state (filters, taps, phases, ramps);
 * - DLYVERB_RVB_BYTES: one 16,384-sample int16 ring for every reverb line
 *   (input diffusers and the two tank halves);
 * - two int16 delay rings, one per channel, sharing the rest equally.
 * With the whole arena (126,592 B, no voice units claimed) each delay ring
 * holds 23,392 samples (1.06 s); at the 73,600 B minimum share, 10,144
 * samples (0.46 s). A division longer than the ring plays at half (or a
 * quarter) of its length, so it stays on the beat.
 * DLYVERB_BUFFER_MIN_BYTES is the registry minimum: the state, the reverb and
 * two 4,096-sample delay rings.
 */
#define DLYVERB_HALF_RATE_HZ      22054u
#define DLYVERB_STATE_BYTES       256u
#define DLYVERB_RVB_SAMPLES       16384u
#define DLYVERB_RVB_BYTES         (DLYVERB_RVB_SAMPLES * 2u)
#define DLYVERB_BUFFER_MIN_BYTES  (DLYVERB_STATE_BYTES + DLYVERB_RVB_BYTES + \
                                   2u * 4096u * 2u)

/* Musical delay divisions: indices into the 14 StepScale stops. */
#define DLYVERB_DIVISION_COUNT    14u

/* Type rows 3..18 kept raw in the runtime. */
#define DLYVERB_TYPE_ROWS         16u

/*
 * DlyVerb runtime (a member of EffectsManager's 76 B DTCM runtime union).
 *
 * What: the last resolved values of rows 3..18 (raw, converted once per block
 * in process(), so LFO rewrites cost nothing extra), and where the DSP state
 * is seated in the Effect share. All audio and DSP state lives in the share.
 * - raw[i]: row 3 + i;
 * - state: the seated DSP state at the share base, NULL until seated;
 * - seated_bytes / seated_offset: the share the state was seated in; a
 *   different share re-seats (clears the reverb, mutes the delay rings).
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active; 28 B fits the union without any RAM change.
 * Accessors: DlyVerbEffect.c only; no other module may keep a pointer.
 */
typedef struct {
    uint8_t raw[DLYVERB_TYPE_ROWS];
    void *state;
    uint32_t seated_bytes;
    uint32_t seated_offset;
} DlyVerbRuntime;

/* DSP operations for the registry row (init, export, write, process, share). */
extern const effect_type_ops_t dlyVerb_ops;

/*
 * StepScale CC of one `div` value.
 *
 * What: maps 0..13 to the musical stops 1/64, 32t, 1/32, 16t, d32, 1/16, 8Tr,
 * d16, 1/8, 4Tr, d/8, 1/4, d/4, 1/2 (larger values are treated as 13). Why
 * shared: the DSP length and the Effect-page label must name one division.
 * Accessors: DlyVerbEffect.c (target), DlyVerbParameters.c (label).
 * Affiliates: stepScale_ticksQ8(), stepScale_formatShort().
 */
uint8_t dlyVerb_divisionCc(uint8_t division);

#endif /* DLY_VERB_EFFECT_H_ */
