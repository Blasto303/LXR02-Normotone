/*
 * Core/DSP/Effects/Destroy/DestroyModels.h
 *
 * Normotone `dst` models (alg 1..8): original feedback-network distortions
 * written from public descriptions of a "deconstruction" effects bank
 * (hustle, turmoil, mutiny, riot, assault, bloodshed, bloodbath, mudslide).
 * No third-party code or data is used. alg 0 (`cls`) is the curve in
 * DestroyEffect.c and does not come here.
 *
 * Memory: the per-channel state below lives in the Destroy state bytes; the
 * delay lines live in a DSM_POOL_SAMPLES int16 pool in the Effect share.
 */
#ifndef DESTROY_MODELS_H_
#define DESTROY_MODELS_H_

#include <stdint.h>

#define DSM_ALG_COUNT      9u          /* cls + 8 models */
#define DSM_POOL_SAMPLES   8192u

/* Line lengths (samples, per channel). */
#define DSM_MD_LEN         1024u        /* micro delay / pitch shifter */
#define DSM_AP_COUNT       4u
#define DSM_RV_LEN         1600u        /* pseudo-reverb delay */

typedef struct {
    /* per channel */
    uint16_t md_w[2];
    uint16_t ap_w[2][DSM_AP_COUNT];
    uint16_t rv_w[2];
    float dc_x[2], dc_y[2];             /* DC blocker */
    float lp_a[2], lp_b[2], lp_c[2], lp_d[2];   /* model filters */
    float sq[2];                        /* riot logic state (+-1) */
    float fbs[2];                       /* held feedback (bit-crushed) */
    float hold_in[2], hold_out[2];      /* sample-and-hold values */
    float ph[2];                        /* pitch shifter phase 0..1 */
    float last[2];                      /* previous output */
    uint16_t cnt[2];                    /* sample-and-hold counter */
    /* per block (from the three knobs) */
    float k_fb, k_g, k_tap, k_x, k_q, k_f;
    uint16_t k_hold;
    uint16_t k_tap2, k_tap3;            /* assault fifth / octave taps */
    uint8_t alg;
} DsModelState;

/* Per-block coefficients from alg and the three knobs (0..127). */
void dsm_block(DsModelState *m, uint8_t alg, uint8_t p1, uint8_t p2,
               uint8_t p3);

/*
 * One sample of channel c (0/1). x is the band to destroy, env 0..1 the
 * input dynamics (raises the feedback on loud passages).
 */
float dsm_sample(DsModelState *m, int16_t *pool, uint32_t c, float x,
                 float env);

/* Three-letter knob names of alg (3 x 3 chars), for the Effect page. */
const char *dsm_knobNames(uint8_t alg);
/* Three-letter model name. */
const char *dsm_algName(uint8_t alg);

#endif /* DESTROY_MODELS_H_ */
