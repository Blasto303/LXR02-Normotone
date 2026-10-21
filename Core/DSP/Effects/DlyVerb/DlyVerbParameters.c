/*
 * Core/DSP/Effects/DlyVerb/DlyVerbParameters.c
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#include "DlyVerbParameters.h"
#include "DlyVerbEffect.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"
#include "StepScale.h"
#include <string.h>

/* Persisted token (permanent), UI abbreviation (5) and full name (<= 8). */
const char dlyVerb_token3[] = "dvb";
const char dlyVerb_abbrev5[] = "DlyVb";
const char dlyVerb_full8[] = "DlyVerb";

/* Enumerated rows: Morph and Pattern automation, but no LFO. */
#define DLYVERB_FLAGS_ENUM \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/*
 * DlyVerb descriptor table.
 *
 * What: rows 0..2 are the shared common rows (the return level defaults to
 * 127, unity, so a voice at full `d/w` send is exactly as loud wet as dry).
 * - div (3): the delay time as one of the 14 musical StepScale stops,
 *   0 = 1/64 .. 13 = 1/2; always tempo-synced. Default 10 = dotted 1/8.
 * - mdl (7), stm (8), rrt (18): enumerations labelled by the format hook,
 *   because every DTYPE_MENU table id is already in use (as CrumpBit `sub`).
 * - every other row is a 0..127 continuous control with Morph, LFO and
 *   Pattern automation, like voice rows.
 * Inputs: none (const). Output: 19 rows. Accessors: EffectsManager
 * (resolution, clamps), menuEffects/menu.c (labels), `.fx` and AutoSave
 * (file keys). Affiliates: DlyVerbEffect.c write_param().
 */
const effect_param_descriptor_t dlyVerb_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("dvb_division", "Delay", "Division", "div", DTYPE_0B127,
               DLYVERB_FLAGS_ENUM, EFFECT_MOD_NONE,
               10u, DLYVERB_DIVISION_COUNT - 1u),
    EFFECT_ROW("dvb_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 60u, 127u),
    EFFECT_ROW("dvb_dly_level", "Delay", "DlyLevel", "dlv", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 110u, 127u),
    EFFECT_ROW("dvb_rvb_level", "Reverb", "RvbLevel", "rvl", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 70u, 127u),
    EFFECT_ROW("dvb_model", "Delay", "Model", "mdl", DTYPE_0B127,
               DLYVERB_FLAGS_ENUM, EFFECT_MOD_NONE,
               DLYVERB_MODEL_BBD, DLYVERB_MODEL_MAX),
    EFFECT_ROW("dvb_stereo", "Delay", "Stereo", "stm", DTYPE_0B127,
               DLYVERB_FLAGS_ENUM, EFFECT_MOD_NONE,
               DLYVERB_STEREO_PINGPONG, DLYVERB_STEREO_MAX),
    EFFECT_ROW("dvb_width", "Delay", "Width", "wid", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("dvb_drive", "Delay", "Drive", "drv", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("dvb_lowpass", "Delay", "LowPass", "lpf", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 90u, 127u),
    EFFECT_ROW("dvb_highpass", "Delay", "HighPass", "hpf", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 15u, 127u),
    EFFECT_ROW("dvb_mod_depth", "Delay", "ModDepth", "mod", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 25u, 127u),
    EFFECT_ROW("dvb_mod_rate", "Delay", "ModRate", "mrt", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW("dvb_age", "Delay", "Age", "age", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 10u, 127u),
    EFFECT_ROW("dvb_rvb_decay", "Reverb", "RvbDecay", "rdc", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 80u, 127u),
    EFFECT_ROW("dvb_rvb_damp", "Reverb", "RvbDamp", "rdp", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 50u, 127u),
    EFFECT_ROW("dvb_rvb_route", "Reverb", "RvbRoute", "rrt", DTYPE_0B127,
               DLYVERB_FLAGS_ENUM, EFFECT_MOD_NONE,
               DLYVERB_ROUTE_SERIAL, DLYVERB_ROUTE_MAX),
};

_Static_assert(sizeof(dlyVerb_descriptors) /
                   sizeof(dlyVerb_descriptors[0]) == DLYVERB_PARAM_COUNT,
               "DLYVERB_PARAM_COUNT must match the descriptor table");
_Static_assert(DLYVERB_PARAM_ENUM_COUNT == DLYVERB_PARAM_COUNT,
               "dlyverb_param_t must match the descriptor table");

/*
 * Format hook: DlyVerb value text on the Effect page.
 *
 * What: `div` shows the StepScale symbol of its musical stop (`/16`, `d/8`,
 * `4Tr` ...); `mdl`, `stm` and `rrt` show three-letter names. Every other
 * row falls back to the page's numeric formatting. Storage and the STEP
 * automation page keep the raw values. Inputs: row index, value (already
 * clamped), 3-character output. Output: nonzero when handled. Accessor:
 * menuEffects through dlyVerb_ui. Affiliates: dlyVerb_divisionCc().
 */
static uint8_t dlyVerb_uiFormatValue3(uint8_t index, uint8_t value, char *out)
{
    static const char models[DLYVERB_MODEL_MAX + 1u][3] = {
        { 'd', 'i', 'g' }, { 'b', 'b', 'd' }, { 't', 'a', 'p' } };
    static const char stereo[DLYVERB_STEREO_MAX + 1u][3] = {
        { 's', 't', 'r' }, { 'p', 'n', 'g' }, { 'w', 'i', 'd' } };
    static const char routes[DLYVERB_ROUTE_MAX + 1u][3] = {
        { 'p', 'a', 'r' }, { 's', 'e', 'r' }, { 'f', 'b', 'k' } };

    switch (index) {
    case DLYVERB_PARAM_DIVISION:
        stepScale_formatShort(dlyVerb_divisionCc(value), out);
        return 1u;
    case DLYVERB_PARAM_MODEL:
        memcpy(out, models[(value > DLYVERB_MODEL_MAX)
                           ? DLYVERB_MODEL_MAX : value], 3u);
        return 1u;
    case DLYVERB_PARAM_STEREO:
        memcpy(out, stereo[(value > DLYVERB_STEREO_MAX)
                           ? DLYVERB_STEREO_MAX : value], 3u);
        return 1u;
    case DLYVERB_PARAM_RVB_ROUTE:
        memcpy(out, routes[(value > DLYVERB_ROUTE_MAX)
                           ? DLYVERB_ROUTE_MAX : value], 3u);
        return 1u;
    default:
        return 0u;
    }
}

/* Labels only: DlyVerb uses the default Effect-page layout. */
const effect_ui_hooks_t dlyVerb_ui = {
    .select = NULL,
    .track = NULL,
    .bar = NULL,
    .render_leds = NULL,
    .paint_row0 = NULL,
    .format_value3 = dlyVerb_uiFormatValue3,
    .flags = 0u,
};
