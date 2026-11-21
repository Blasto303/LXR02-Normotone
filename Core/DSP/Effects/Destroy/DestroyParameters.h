/*
 * Core/DSP/Effects/Destroy/DestroyParameters.h
 *
 * Normotone `dst` (Destroy): a multi-stage distortion inserted on MAIN, with
 * dry/wet, a sub-octave, a tone tilt and a light plate on the whole mix.
 * Original design; the stage list is inspired by multi-distortion modules.
 * Choosing it replaces the rumble (one Effect per Scene).
 */
#ifndef DESTROY_PARAMETERS_H_
#define DESTROY_PARAMETERS_H_

#include "EffectsManager.h"

#define DESTROY_PARAM_COUNT 12u

/* Append only; never renumber (FX lanes, automation ids, `.fx` keys). */
typedef enum {
    DESTROY_PARAM_AUDIO_OUT = EFFECT_COMMON_PARAM_AUDIO_OUT,
    DESTROY_PARAM_LEVEL     = EFFECT_COMMON_PARAM_LEVEL,
    DESTROY_PARAM_PAN       = EFFECT_COMMON_PARAM_PAN,
    DESTROY_PARAM_DRIVE     = 3,   /* dri: 0..+36 dB into the curve      */
    DESTROY_PARAM_FOLD,            /* fld: soft clip -> wavefold          */
    DESTROY_PARAM_SUB,             /* sub: sub-octave level               */
    DESTROY_PARAM_CROSS,           /* crs: 0 full band, else clean lows   */
    DESTROY_PARAM_TONE,            /* ton: 0 dark .. 64 flat .. 127 thin  */
    DESTROY_PARAM_MIX,             /* mix: dry/wet on MAIN                */
    DESTROY_PARAM_PLATE,           /* pla: plate on the whole mix         */
    DESTROY_PARAM_PLATE_DECAY,     /* pdc: plate length                   */
    /*
     * alg: 0 cls (the curve above), 1..8 feedback-network models. For a
     * model, dri/fld/sub are its three knobs (names on the Effect page
     * follow the model).
     */
    DESTROY_PARAM_ALG,
    DESTROY_PARAM_ENUM_COUNT
} destroy_param_t;

extern const effect_param_descriptor_t destroy_descriptors[];
extern const char destroy_token3[];
extern const char destroy_abbrev5[];
extern const char destroy_full8[];
extern const effect_select_layout_t destroy_layout;
extern const effect_ui_hooks_t destroy_ui;

#endif /* DESTROY_PARAMETERS_H_ */
