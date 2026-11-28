/*
 * Core/DSPAudio/DjFilter.c
 *
 * Zero-delay-feedback state-variable filter (trapezoidal integration, stable
 * at any cutoff), one per channel and per output: four in all. The cutoff
 * is smoothed per block (about 10 ms) and coefficients are computed once
 * per block. Near the centre the filtered signal is crossfaded with the dry
 * one, so passing from low-pass to high-pass, or to off, is click-free; at
 * rest the block is skipped entirely.
 */
#include "DjFilter.h"
#include "config.h"
#include <math.h>

#define DJF_FULL_SCALE 8388352.0f
#define DJF_K          1.1f               /* 1/Q: a mild bump, no overshoot */

static volatile int8_t djf_value;         /* UI: -64..+64 */
static float djf_pos;                     /* smoothed -1..+1 */
static float djf_ic1[4], djf_ic2[4];      /* [output * 2 + channel] */

void djFilter_adjust(int8_t delta)
{
    int16_t v = (int16_t)djf_value + delta;

    v = (v < -DJF_RANGE) ? -DJF_RANGE : ((v > DJF_RANGE) ? DJF_RANGE : v);
    djf_value = (int8_t)v;
}

int8_t djFilter_value(void)
{
    return djf_value;
}

void djFilter_processBlock(sample_mx_t *st1, sample_mx_t *st2)
{
    const float target = (float)djf_value * (1.0f / (float)DJF_RANGE);
    float t;
    float fc;
    float g;
    float a1;
    float a2;
    float a3;
    float wet;
    uint8_t o;

    djf_pos += 0.07f * (target - djf_pos);
    if (fabsf(djf_pos) < 0.004f && djf_value == 0) {
        djf_pos = 0.0f;
        return;                                   /* off: untouched */
    }
    t = fabsf(djf_pos);
    /* Crossfade in over the first 6 % of travel either side. */
    wet = (t - 0.01f) * (1.0f / 0.05f);
    wet = (wet < 0.0f) ? 0.0f : ((wet > 1.0f) ? 1.0f : wet);
    fc = (djf_pos < 0.0f) ? 20000.0f * powf(0.005f, t)    /* LP 20k..100 */
                          : 20.0f * powf(400.0f, t);      /* HP 20..8k   */
    fc = (fc > 0.45f * REAL_FS) ? 0.45f * REAL_FS : fc;
    g = tanf(3.14159265f * fc / REAL_FS);
    a1 = 1.0f / (1.0f + g * (g + DJF_K));
    a2 = g * a1;
    a3 = g * a2;
    for (o = 0u; o < 2u; o++) {
        sample_mx_t *buf = o ? st2 : st1;
        uint8_t c;

        for (c = 0u; c < 2u; c++) {
            const uint8_t s = (uint8_t)(o * 2u + c);
            float ic1 = djf_ic1[s];
            float ic2 = djf_ic2[s];
            uint8_t i;

            for (i = 0u; i < OUTPUT_DMA_SIZE; i++) {
                const float x = (float)buf[2u * i + c] * (1.0f / DJF_FULL_SCALE);
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                const float y = (djf_pos < 0.0f) ? v2
                                                 : x - DJF_K * v1 - v2;

                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                buf[2u * i + c] = (sample_mx_t)
                    ((x + wet * (y - x)) * DJF_FULL_SCALE);
            }
            djf_ic1[s] = ic1;
            djf_ic2[s] = ic2;
        }
    }
}
