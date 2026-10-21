/*
 * Core/DSP/Effects/DlyVerb/DlyVerbParameters.h
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef DLY_VERB_PARAMETERS_H_
#define DLY_VERB_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * DlyVerb (`dvb`) registry exports.
 *
 * What: the immutable descriptor table, display tokens and value-label hook
 * of the DlyVerb Effect type: input drive, a tempo-synced stereo character
 * delay (digital, BBD or tape model; stereo, ping-pong or width mode) with
 * filters, modulation and age noise in its feedback path, and a Dattorro
 * plate reverb that runs in parallel, after the delay, or inside the delay
 * feedback. The type is 100 % wet; the per-voice dry/wet is the `d/w` fader
 * mode (mixer.h MIXER_FADER_DW), which turns each voice's FX send into an
 * equal-power dry/wet crossfade.
 * Why a separate file: EffectsManager owns the registry row but not a type's
 * control tables. This side is ordinary -O2 control code; DlyVerbEffect.c is
 * the -Ofast DSP.
 * Accessors: EffectsManager.c (registry row), menuEffects.c (format hook).
 * Affiliates: DlyVerbEffect.c/.h, EffectParamRows.h, StepScale.h.
 */

/* Descriptor rows: common rows 0..2 plus type rows 3..18. */
#define DLYVERB_PARAM_COUNT 19u

/*
 * Local descriptor indices.
 *
 * Permanent identities (FX lanes, Pattern automation IDs 448 + index, LFO
 * tokens, `.fx` and AutoSave keys). Append only; never renumber. The order
 * also places the rows on the default Effect page, four per SELECT:
 * SELECT 2 div fbk dlv rvl, SELECT 3 mdl stm wid drv, SELECT 4 lpf hpf mod
 * mrt, SELECT 5 age rdc rdp rrt.
 */
typedef enum {
    DLYVERB_PARAM_AUDIO_OUT = EFFECT_COMMON_PARAM_AUDIO_OUT,
    DLYVERB_PARAM_LEVEL     = EFFECT_COMMON_PARAM_LEVEL,
    DLYVERB_PARAM_PAN       = EFFECT_COMMON_PARAM_PAN,
    DLYVERB_PARAM_DIVISION  = 3,
    DLYVERB_PARAM_FEEDBACK,
    DLYVERB_PARAM_DLY_LEVEL,
    DLYVERB_PARAM_RVB_LEVEL,
    DLYVERB_PARAM_MODEL,
    DLYVERB_PARAM_STEREO,
    DLYVERB_PARAM_WIDTH,
    DLYVERB_PARAM_DRIVE,
    DLYVERB_PARAM_LOWPASS,
    DLYVERB_PARAM_HIGHPASS,
    DLYVERB_PARAM_MOD_DEPTH,
    DLYVERB_PARAM_MOD_RATE,
    DLYVERB_PARAM_AGE,
    DLYVERB_PARAM_RVB_DECAY,
    DLYVERB_PARAM_RVB_DAMP,
    DLYVERB_PARAM_RVB_ROUTE,
    DLYVERB_PARAM_ENUM_COUNT
} dlyverb_param_t;

/* Delay model (row `mdl`). */
#define DLYVERB_MODEL_DIGITAL 0u
#define DLYVERB_MODEL_BBD     1u
#define DLYVERB_MODEL_TAPE    2u
#define DLYVERB_MODEL_MAX     2u

/* Stereo mode (row `stm`). */
#define DLYVERB_STEREO_STEREO   0u
#define DLYVERB_STEREO_PINGPONG 1u
#define DLYVERB_STEREO_WIDTH    2u
#define DLYVERB_STEREO_MAX      2u

/* Reverb routing (row `rrt`). */
#define DLYVERB_ROUTE_PARALLEL 0u
#define DLYVERB_ROUTE_SERIAL   1u
#define DLYVERB_ROUTE_FEEDBACK 2u
#define DLYVERB_ROUTE_MAX      2u

extern const effect_param_descriptor_t dlyVerb_descriptors[];
extern const char dlyVerb_token3[];
extern const char dlyVerb_abbrev5[];
extern const char dlyVerb_full8[];

/* Value labels only (division, model, stereo mode, routing); default page. */
extern const effect_ui_hooks_t dlyVerb_ui;

#endif /* DLY_VERB_PARAMETERS_H_ */
