/*
 * Core/DSP/Effects/Destroy/DestroyEffect.c
 *
 * Normotone `dst` DSP (fast-math, foreground render).
 *
 * Signal path per channel, at the codec rate:
 *   split   one-pole low-pass at crs (40..400 Hz); 0 = no split, full band
 *   curve   high band x drive (0..+36 dB) through a curve that goes from a
 *           rational soft clip (fld 0) to a sine wavefolder (fld 127),
 *           evaluated at 2x (midpoint and sample, averaged) to tame aliasing
 *   sub     a square one octave down (flip-flop on the rising zero
 *           crossings of the 120 Hz low-passed mono sum, shaped by its
 *           envelope), injected into the curve; above 64 it also flips
 *           the distorted band (octave-down ring modulation)
 *   tone    tilt around a 1.2 kHz one-pole: 0 dark, 64 flat, 127 thin
 *   mix     dry MAIN to destroyed MAIN, ramped per block
 *   plate   the DlyVerb plate at a quarter rate on the mono mix (pla send,
 *           pdc length), added after the mix
 *   ceiling soft knee from 0.9 of full scale
 * Cost: about 3 % CPU, plus about 3 % with the plate (estimates; see the
 * cpu_bench). Nothing runs while the type is off.
 */
#include "DestroyEffect.h"
#include "DestroyParameters.h"
#include "DlyVerbEffect.h"
#include "DestroyModels.h"
#include <math.h>
#include <string.h>

#define DS_FS          44108.07f
#define DS_TWO_PI      6.28318531f
#define DS_RVB_MASK    (DESTROY_RVB_SAMPLES - 1u)
/* Model output level (they peak near full scale; the dry mix is lower). */
#define DS_MODEL_GAIN  0.5f

typedef struct {
    float split[2];        /* crossover low-pass                    */
    float dprev[2];        /* previous curve input (2x midpoint)    */
    float tone[2];         /* tilt low-pass                         */
    float sub_lp;          /* 120 Hz mono low-pass                  */
    float sub_prev;        /* its previous sample (zero crossing)   */
    float sub_env;         /* its envelope                          */
    float sub_sq;          /* flip-flop, +-1                        */
    float sub_out;         /* 200 Hz smoothed sub                   */
    float mix;             /* ramp ends                             */
    float drive;
    float plate_st[5];     /* bw, damp[2], tank[2]                  */
    float plate_in;        /* 4-sample accumulator                  */
    float plate_prev[2];   /* previous quarter-rate output          */
    float plate_cur[2];
    float plate_mod;       /* plate modulation phase                */
    uint32_t ptr;
    uint32_t primed;
    float env;             /* input dynamics follower (models)      */
    DsModelState models;   /* alg 1..8 state (DestroyModels.c)      */
} DestroyState;

_Static_assert(sizeof(DestroyState) <= DESTROY_STATE_BYTES,
               "DestroyState must fit its reserved share bytes");
_Static_assert(sizeof(DestroyRuntime) <= 76u,
               "DestroyRuntime must fit the 76 B Effect runtime union");

#define DS_ROW(rt, idx) ((rt)->raw[(idx) - DESTROY_PARAM_DRIVE])

/* Bipolar parabolic sine of a phase (any real; period 1). */
static inline float ds_psin(float ph)
{
    float x;
    const int32_t i = (int32_t)ph;     /* fraction without floorf */

    ph -= (float)i;
    ph += (ph < 0.0f) ? 1.0f : 0.0f;
    x = 2.0f * ph - 1.0f;
    return -4.0f * x * (1.0f - fabsf(x));
}

/* Soft clip (fld 0) to sine fold (fld 1). */
static inline float ds_soft(float u)
{
    return u / (1.0f + fabsf(u));
}

static inline float ds_curve(float u, float fold, float kf)
{
    const float soft = ds_soft(u);
    const float folded = ds_psin(0.25f * kf * u);

    return soft + fold * (folded - soft);
}

/*
 * One channel through the curve. With fld 0 the soft clip alone runs once
 * per sample (its harmonics fall fast); with folding, at 2x (midpoint and
 * sample, averaged) to tame the folder's aliasing.
 */
static inline float ds_shape(float d, float *dprev, uint32_t folding,
                             float fold, float kf)
{
    float y;

    if (!folding)
        y = ds_soft(d);
    else
        y = 0.5f * (ds_curve(0.5f * (*dprev + d), fold, kf) +
                    ds_curve(d, fold, kf));
    *dprev = d;
    return y;
}

static inline float ds_ceiling(float v)
{
    const float a = fabsf(v);

    if (a > 0.9f) {
        const float t = (a - 0.9f) * 10.0f;
        const float y = 0.9f + 0.1f * t / (1.0f + t);

        return (v < 0.0f) ? -y : y;
    }
    return v;
}

static DestroyState *ds_seat(DestroyRuntime *rt, const fx_share_t *share)
{
    DestroyState *st;

    if (!share || !share->base || share->bytes < DESTROY_BUFFER_MIN_BYTES) {
        rt->state = NULL;
        rt->seated_bytes = 0u;
        return NULL;
    }
    if (rt->state && rt->seated_bytes == share->bytes &&
        rt->seated_offset == share->offset)
        return (DestroyState *)rt->state;
    st = (DestroyState *)share->base;
    memset(share->base, 0, DESTROY_BUFFER_MIN_BYTES);
    st->sub_sq = 1.0f;
    rt->state = st;
    rt->seated_bytes = share->bytes;
    rt->seated_offset = share->offset;
    return st;
}

static void destroy_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    DestroyRuntime *rt = (DestroyRuntime *)rt_void;

    (void)handoff;
    rt->state = NULL;
    rt->seated_bytes = 0u;
    rt->seated_offset = 0u;
}

static void destroy_exportHandoff(const void *rt_void, fxbuf_handoff_t *out)
{
    const DestroyRuntime *rt = (const DestroyRuntime *)rt_void;

    if (!rt || !out)
        return;
    out->effect_channels = 2u;
    out->effect_bits = 16u;
    out->effect_rate_hz = 11027u;
    if (rt->state)
        out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
}

static void destroy_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    DestroyRuntime *rt = (DestroyRuntime *)rt_void;

    if (index >= DESTROY_PARAM_DRIVE &&
        index < DESTROY_PARAM_DRIVE + DESTROY_TYPE_ROWS)
        rt->raw[index - DESTROY_PARAM_DRIVE] = value;
}

static void destroy_process(void *rt_void, effect_io_t *io)
{
    DestroyRuntime *rt = (DestroyRuntime *)rt_void;
    DestroyState *st;
    int16_t *rv;
    float *L;
    float *R;
    uint32_t n;
    uint32_t frames;
    float fold, kf, split_a, use_split, sub_lvl, sub_rm, tone_t, tone_a;
    float mix0, mix1, mix_inc, drv0, drv1, drv_inc;
    float pla, decay, dd2, damp2, sub_lp_a, sub_out_a, env_rel;
    uint32_t folding;
    float comp0, comp_inc;
    uint8_t alg;
    int16_t *pool;

    if (!rt || !io || !io->l || !io->r || io->frames == 0u)
        return;
    st = ds_seat(rt, io->share);
    if (!st)
        return;                         /* no share: MAIN stays dry */
    rv = (int16_t *)((uint8_t *)st + DESTROY_STATE_BYTES);
    pool = rv + DESTROY_RVB_SAMPLES;
    alg = DS_ROW(rt, DESTROY_PARAM_ALG);
    alg = (alg < DSM_ALG_COUNT) ? alg : 0u;
    if (alg)
        dsm_block(&st->models, alg, DS_ROW(rt, DESTROY_PARAM_DRIVE),
                  DS_ROW(rt, DESTROY_PARAM_FOLD), DS_ROW(rt, DESTROY_PARAM_SUB));
    L = io->l;
    R = io->r;
    frames = io->frames;

    /* Controls, once per block. */
    fold = (float)DS_ROW(rt, DESTROY_PARAM_FOLD) * (1.0f / 127.0f);
    folding = (uint32_t)(DS_ROW(rt, DESTROY_PARAM_FOLD) != 0u);
    kf = 1.0f + 4.0f * fold;
    {
        const uint8_t c = DS_ROW(rt, DESTROY_PARAM_CROSS);
        const float hz = 40.0f * expf((float)c * (1.0f / 127.0f) * 2.3026f);

        use_split = (c > 0u) ? 1.0f : 0.0f;
        split_a = 1.0f - expf(-DS_TWO_PI * hz / DS_FS);
    }
    sub_lvl = (float)DS_ROW(rt, DESTROY_PARAM_SUB) * (1.0f / 127.0f);
    if (alg)
        sub_lvl = 0.0f;           /* sub-octave: cls only (sub = a knob) */
    sub_rm = (sub_lvl > 0.5f) ? 2.0f * (sub_lvl - 0.5f) : 0.0f;
    tone_t = ((float)DS_ROW(rt, DESTROY_PARAM_TONE) - 64.0f) * (1.0f / 63.0f);
    tone_t = (tone_t > 1.0f) ? 1.0f : ((tone_t < -1.0f) ? -1.0f : tone_t);
    tone_a = 1.0f - expf(-DS_TWO_PI * 1200.0f / DS_FS);
    sub_lp_a = 1.0f - expf(-DS_TWO_PI * 120.0f / DS_FS);
    sub_out_a = 1.0f - expf(-DS_TWO_PI * 1000.0f / DS_FS);  /* keeps grit */
    env_rel = expf(-1.0f / (0.05f * DS_FS));
    mix1 = (float)DS_ROW(rt, DESTROY_PARAM_MIX) * (1.0f / 127.0f);
    drv1 = exp2f((float)DS_ROW(rt, DESTROY_PARAM_DRIVE) * (6.0f / 127.0f));
    if (!st->primed) {
        st->mix = mix1;
        st->drive = drv1;
        st->primed = 1u;
    }
    mix0 = st->mix;
    drv0 = st->drive;
    mix_inc = (mix1 - mix0) / (float)frames;
    drv_inc = (drv1 - drv0) / (float)frames;
    st->mix = mix1;
    st->drive = drv1;
    /* Level compensation: 1/sqrt(drive), capped at +18 dB of drive where
     * the curve is saturated anyway; ramped like the drive. */
    comp0 = 1.0f / sqrtf((drv0 < 8.0f) ? drv0 : 8.0f);
    comp_inc = (1.0f / sqrtf((drv1 < 8.0f) ? drv1 : 8.0f) - comp0) /
               (float)frames;
    pla = (float)DS_ROW(rt, DESTROY_PARAM_PLATE) * (1.0f / 127.0f);
    {
        const float r = (float)DS_ROW(rt, DESTROY_PARAM_PLATE_DECAY) *
                        (1.0f / 127.0f);
        const float damp = 0.05f + 0.85f * 0.75f;   /* fixed, fairly dark */

        decay = 0.2f + 0.76f * sqrtf(r);
        dd2 = decay + 0.15f;
        dd2 = (dd2 > 0.5f) ? 0.5f : ((dd2 < 0.25f) ? 0.25f : dd2);
        damp2 = damp * damp;
    }

    for (n = 0u; n < frames; n++) {
        const float mix = mix0 + mix_inc * (float)(n + 1u);
        const float drv = drv0 + drv_inc * (float)(n + 1u);
        const float comp = comp0 + comp_inc * (float)(n + 1u);
        const float xl = L[n];
        const float xr = R[n];
        float yl;
        float yr;

        /* Sub-octave from the mono sum. */
        {
            const float m = 0.5f * (xl + xr);
            float a;

            st->sub_lp += sub_lp_a * (m - st->sub_lp);
            if (st->sub_prev <= 0.0f && st->sub_lp > 0.0f)
                st->sub_sq = -st->sub_sq;
            st->sub_prev = st->sub_lp;
            a = fabsf(st->sub_lp);
            st->sub_env = (a > st->sub_env) ? a : a + env_rel * (st->sub_env - a);
            st->sub_out += sub_out_a * (st->sub_sq * st->sub_env - st->sub_out);
        }

        /*
         * Split, curve at 2x, recombine (per channel). sub, first half of its
         * travel: the sub-octave goes INTO the curve (1.5x), so it is driven
         * and saturated with everything (a roar, not an added bass). Second
         * half: the distorted band is also flipped every other sub half-
         * period (octave-down ring modulation, with hard edges): it tears.
         */
        if (alg) {
            /* Models 1..8: the band above crs goes through the network. */
            const float a = (fabsf(xl) > fabsf(xr)) ? fabsf(xl) : fabsf(xr);
            float env01;
            float lo;

            st->env = (a > st->env) ? st->env + 0.01f * (a - st->env)
                                    : a + env_rel * (st->env - a);
            env01 = 3.0f * st->env;
            env01 = (env01 > 1.0f) ? 1.0f : env01;
            st->split[0] += split_a * (xl - st->split[0]);
            lo = use_split * st->split[0];
            yl = lo + DS_MODEL_GAIN * dsm_sample(&st->models, pool, 0u,
                                                 xl - lo, env01);
            st->split[1] += split_a * (xr - st->split[1]);
            lo = use_split * st->split[1];
            yr = lo + DS_MODEL_GAIN * dsm_sample(&st->models, pool, 1u,
                                                 xr - lo, env01);
        } else {
            const float inj = 1.5f * sub_lvl * st->sub_out;
            const float flip = 1.0f + sub_rm * (st->sub_sq - 1.0f);
            float lo;

            st->split[0] += split_a * (xl - st->split[0]);
            lo = use_split * st->split[0];
            yl = lo + comp * ds_shape(((xl - lo) * flip + inj) * drv,
                                      &st->dprev[0], folding, fold, kf);
            st->split[1] += split_a * (xr - st->split[1]);
            lo = use_split * st->split[1];
            yr = lo + comp * ds_shape(((xr - lo) * flip + inj) * drv,
                                      &st->dprev[1], folding, fold, kf);
        }
        yl += 0.5f * sub_lvl * st->sub_out;
        yr += 0.5f * sub_lvl * st->sub_out;

        /* Tone tilt. */
        st->tone[0] += tone_a * (yl - st->tone[0]);
        st->tone[1] += tone_a * (yr - st->tone[1]);
        if (tone_t < 0.0f) {
            yl += -tone_t * (st->tone[0] - yl);
            yr += -tone_t * (st->tone[1] - yr);
        } else {
            yl -= tone_t * st->tone[0];
            yr -= tone_t * st->tone[1];
        }

        /* Dry/wet. */
        yl = xl + mix * (yl - xl);
        yr = xr + mix * (yr - xr);

        /* Plate on the mix, a quarter rate, interpolated back. */
        if (pla > 0.0f) {
            const float k = 0.25f * (float)((n & 3u) + 1u);

            st->plate_in += 0.5f * (yl + yr);
            if ((n & 3u) == 3u) {
                float pl;
                float pr;

                st->plate_mod += 0.7f * 4.0f / DS_FS;
                st->plate_mod -= floorf(st->plate_mod);
                st->plate_prev[0] = st->plate_cur[0];
                st->plate_prev[1] = st->plate_cur[1];
                dlyVerb_plateQuarter(st->plate_st, rv, st->ptr,
                                     0.125f * pla * st->plate_in, decay, dd2,
                                     damp2, ds_psin(st->plate_mod), &pl, &pr);
                st->ptr = (st->ptr - 1u) & DS_RVB_MASK;
                st->plate_cur[0] = pl;
                st->plate_cur[1] = pr;
                st->plate_in = 0.0f;
            }
            yl += st->plate_prev[0] + k * (st->plate_cur[0] - st->plate_prev[0]);
            yr += st->plate_prev[1] + k * (st->plate_cur[1] - st->plate_prev[1]);
        }

        L[n] = ds_ceiling(yl);
        R[n] = ds_ceiling(yr);
    }
}

static void destroy_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)ds_seat((DestroyRuntime *)rt_void, share);
}

const effect_type_ops_t destroy_ops = {
    destroy_init,
    destroy_exportHandoff,
    destroy_writeParam,
    destroy_process,
    destroy_bufferChanged,
    NULL,
};
