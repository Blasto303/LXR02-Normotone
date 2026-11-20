/*
 * Core/DSP/Effects/Destroy/DestroyParameters.c
 *
 * Descriptor table of the Normotone `dst` Effect (see DestroyParameters.h).
 * Every type row is a 0..127 control with Morph, LFO and Pattern automation.
 */
#include "DestroyParameters.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"

const char destroy_token3[] = "dst";
const char destroy_abbrev5[] = "Destr";
const char destroy_full8[] = "Destroy";

const effect_param_descriptor_t destroy_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("dst_drive", "Destroy", "Drive", "dri", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 48u, 127u),
    EFFECT_ROW("dst_fold", "Destroy", "Fold", "fld", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dst_sub", "Destroy", "Sub", "sub", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dst_cross", "Destroy", "Cross", "crs", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dst_tone", "Destroy", "Tone", "ton", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("dst_mix", "Destroy", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 80u, 127u),
    EFFECT_ROW("dst_plate", "Destroy", "Plate", "pla", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dst_plate_decay", "Destroy", "PlDecay", "pdc", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 80u, 127u),
};

_Static_assert(sizeof(destroy_descriptors) / sizeof(destroy_descriptors[0]) ==
                   DESTROY_PARAM_COUNT,
               "DESTROY_PARAM_COUNT must match the descriptor table");
_Static_assert(DESTROY_PARAM_ENUM_COUNT == DESTROY_PARAM_COUNT,
               "destroy_param_t must match the descriptor table");
