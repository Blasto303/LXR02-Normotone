/*
 * Core/DSP/Effects/Destroy/DestroyParameters.c
 *
 * Descriptor table of the Normotone `dst` Effect (see DestroyParameters.h).
 * Every type row is a 0..127 control with Morph, LFO and Pattern automation.
 */
#include "DestroyParameters.h"
#include "DestroyModels.h"
#include "menuEffects.h"
#include <ctype.h>
#include <string.h>
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
    EFFECT_ROW("dst_alg", "Destroy", "Model", "alg", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
               EFFECT_MOD_NONE, 0u, DSM_ALG_COUNT - 1u),
};

_Static_assert(sizeof(destroy_descriptors) / sizeof(destroy_descriptors[0]) ==
                   DESTROY_PARAM_COUNT,
               "DESTROY_PARAM_COUNT must match the descriptor table");
_Static_assert(DESTROY_PARAM_ENUM_COUNT == DESTROY_PARAM_COUNT,
               "destroy_param_t must match the descriptor table");

/*
 * Effect page (Normotone dst): AEG = alg + the model's three knobs, MOD =
 * ton mix pla pdc, FM = crs. The AEG names follow the model (painted).
 */
const effect_select_layout_t destroy_layout = {
    .screen_count = { 0u, 1u, 1u, 1u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = { { DESTROY_PARAM_ALG, DESTROY_PARAM_DRIVE,
                  DESTROY_PARAM_FOLD, DESTROY_PARAM_SUB } },
        [2] = { { DESTROY_PARAM_TONE, DESTROY_PARAM_MIX,
                  DESTROY_PARAM_PLATE, DESTROY_PARAM_PLATE_DECAY } },
        [3] = { { DESTROY_PARAM_CROSS, EFFECT_LANE_NONE,
                  EFFECT_LANE_NONE, EFFECT_LANE_NONE } },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

/* AEG names: "alg" then the model's knobs; the selected cell stays upper. */
static void destroy_uiPaintRow0(uint8_t sub_page, uint8_t screen, char *row0)
{
    const char *k = dsm_knobNames(menuEffects_shownParam(DESTROY_PARAM_ALG));
    char names[4][3];
    uint8_t col;
    uint8_t i;

    (void)sub_page;
    (void)screen;
    memcpy(names[0], "alg", 3);
    memcpy(names[1], &k[0], 3);
    memcpy(names[2], &k[3], 3);
    memcpy(names[3], &k[6], 3);
    for (col = 0u; col < 4u; col++) {
        const uint8_t upper = (uint8_t)isupper((unsigned char)row0[4u * col]);

        for (i = 0u; i < 3u; i++)
            row0[4u * col + i] = upper
                ? (char)toupper((unsigned char)names[col][i]) : names[col][i];
        if (col < 3u)
            row0[4u * col + 3u] = ' ';
    }
}

static uint8_t destroy_uiFormatValue3(uint8_t index, uint8_t value, char *out)
{
    if (index == DESTROY_PARAM_ALG) {
        memcpy(out, dsm_algName(value), 3);
        return 1u;
    }
    /* itv (alg 15) shows semitones, smo (alg 16) its trigger voice. */
    if (index == DESTROY_PARAM_DRIVE) {
        const uint8_t alg = menuEffects_shownParam(DESTROY_PARAM_ALG);
        const float a = (float)value * (1.0f / 127.0f);

        if (alg == 15u) {
            const int8_t n = (int8_t)((int16_t)(a * 24.0f + 0.5f) - 12);
            const uint8_t m = (uint8_t)((n < 0) ? -n : n);

            out[0] = (n < 0) ? '-' : ((n > 0) ? '+' : ' ');
            out[1] = (m >= 10u) ? (char)('0' + m / 10u) : (char)('0' + m);
            out[2] = (m >= 10u) ? (char)('0' + m % 10u) : ' ';
            return 1u;
        }
        if (alg == 16u) {
            static const char trg[8][4] = {
                "fix", "DR1", "DR2", "DR3", "SNR", "CYM", "CHH", "OHH" };

            memcpy(out, trg[(uint8_t)(a * 7.99f)], 3);
            return 1u;
        }
    }
    return 0u;
}

const effect_ui_hooks_t destroy_ui = {
    .select = NULL,
    .track = NULL,
    .bar = NULL,
    .render_leds = NULL,
    .paint_row0 = destroy_uiPaintRow0,
    .format_value3 = destroy_uiFormatValue3,
    .flags = 0u,
};
