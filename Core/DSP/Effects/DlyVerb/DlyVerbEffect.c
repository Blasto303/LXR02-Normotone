/*
 * Core/DSP/Effects/DlyVerb/DlyVerbEffect.c
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * DlyVerb DSP (contract in DlyVerbEffect.h). Compiled with -Ofast.
 *
 * Signal flow, per channel, at the 22,054 Hz half rate:
 *
 *   in -> 9 kHz LP -> /2 -> drive -> (+ feedback, + age noise) -> sat -> ring
 *   ring -> two read taps (crossfade) + modulation -> LP -> HP = echo
 *   echo, or the drive output (par) -> Dattorro plate -> reverb
 *   feedback = echo, or a mix of echo and reverb (fbk route)
 *   wet = dlv * echo + rvl * reverb -> x2 -> 9 kHz LP -> out (100 % wet)
 *
 * Every stage runs on every sample whatever its settings (constant CPU): the
 * models differ only in coefficients (glide, saturation, modulation shape,
 * clock-limited bandwidth), not in code paths.
 */

#include "DlyVerbEffect.h"
#include "DlyVerbParameters.h"
#include "StepScale.h"
#include "sequencer.h"
#include <math.h>
#include <string.h>

/*
 * Private DSP constants.
 *
 * DV_FS            the half rate every time and frequency conversion uses.
 * DV_SAMPLES_PER_TICK  half-rate samples per 96-PPQ tick at 1 BPM.
 * DV_B0..DV_A2     9 kHz Butterworth low-pass at 44,108 Hz (decimator and
 *                  interpolator image filter).
 * DV_MOD_MAX       largest modulation excursion, 4 ms in half-rate samples.
 * DV_TAP_MIN       shortest delay a tap may read (guard + modulation).
 * DV_FADE_INC      digital-model tap crossfade, 1,024 half-rate samples
 *                  (46 ms).
 * DV_FEEDBACK_MAX  feedback gain at 127: just over unity, so the loop can
 *                  build up into its saturation (self-oscillation).
 * DV_INV16         int16 to float.
 * DV_RVB_SCALE     reverb ring storage scale: +-4.0 full scale, the tank
 *                  can exceed unity at long decays.
 * DV_LIMIT         wet output limiter: soft clip of 0.75 x, scaled back, so
 *                  the wet sum is transparent at normal levels and never
 *                  exceeds 1.33.
 */
#define DV_FS                 22054.0f
#define DV_SAMPLES_PER_TICK   13783.75f
#define DV_B0                 0.213146705f
#define DV_B1                 0.42629341f
#define DV_B2                 0.213146705f
#define DV_A1                 (-0.339426408f)
#define DV_A2                 0.192013228f
#define DV_MOD_MAX            88.0f
#define DV_TAP_MIN            (DV_MOD_MAX + 4.0f)
#define DV_FADE_INC           (1.0f / 1024.0f)
#define DV_FEEDBACK_MAX       1.05f
#define DV_INV16              (1.0f / 32768.0f)
#define DV_RVB_SCALE          8192.0f
#define DV_RVB_INV            (1.0f / 8192.0f)
#define DV_TWO_PI             6.28318531f
#define DV_HALF_FRAMES_MAX    32u
#define DV_LIMIT              0.75f
#define DV_LIMIT_INV          (1.0f / 0.75f)

/*
 * Dattorro plate geometry at 22,054 Hz: the paper's 29,761 Hz lengths times
 * 0.72 (the room stays the same size within 3 %). Every line lives in one
 * 16,384-sample ring addressed relative to a decrementing pointer; line k
 * occupies DV_L_k + 1 slots from DV_B_k. The modulated tank all-passes get
 * 2 * DV_RVB_EXC extra slots for their moving read.
 */
#define DV_RVB_MASK   (DLYVERB_RVB_SAMPLES - 1u)
#define DV_RVB_EXC    16
#define DV_L_IN1      102
#define DV_L_IN2      77
#define DV_L_IN3      273
#define DV_L_IN4      199
#define DV_L_AP1L     484
#define DV_L_D1L      3206
#define DV_L_AP2L     1296
#define DV_L_D2L      2678
#define DV_L_AP1R     654
#define DV_L_D1R      3036
#define DV_L_AP2R     1912
#define DV_L_D2R      2277
#define DV_B_IN1      0
#define DV_B_IN2      (DV_B_IN1 + DV_L_IN1 + 1)
#define DV_B_IN3      (DV_B_IN2 + DV_L_IN2 + 1)
#define DV_B_IN4      (DV_B_IN3 + DV_L_IN3 + 1)
#define DV_B_AP1L     (DV_B_IN4 + DV_L_IN4 + 1)
#define DV_B_D1L      (DV_B_AP1L + DV_L_AP1L + 2 * DV_RVB_EXC + 1)
#define DV_B_AP2L     (DV_B_D1L + DV_L_D1L + 1)
#define DV_B_D2L      (DV_B_AP2L + DV_L_AP2L + 1)
#define DV_B_AP1R     (DV_B_D2L + DV_L_D2L + 1)
#define DV_B_D1R      (DV_B_AP1R + DV_L_AP1R + 2 * DV_RVB_EXC + 1)
#define DV_B_AP2R     (DV_B_D1R + DV_L_D1R + 1)
#define DV_B_D2R      (DV_B_AP2R + DV_L_AP2R + 1)
#define DV_B_END      (DV_B_D2R + DV_L_D2R + 1)

_Static_assert(DV_B_END <= (int)DLYVERB_RVB_SAMPLES,
               "the plate lines must fit the 16,384-sample reverb ring");
_Static_assert(sizeof(DlyVerbRuntime) <= 76u,
               "DlyVerbRuntime must fit the 76 B Effect runtime union");

/* The 14 musical StepScale stops (EFFECTS_BUS_REFERENCE.md §10). */
static const uint8_t dv_division_cc[DLYVERB_DIVISION_COUNT] = {
    0u, 16u, 38u, 54u, 60u, 76u, 83u, 86u, 93u, 100u, 103u, 110u, 120u, 127u
};

/*
 * DSP state, seated at the Effect share base.
 *
 * What: everything that must survive between blocks. Index [0] is left,
 * [1] right. Taps are delays in half-rate samples: tap_a plays, tap_b is the
 * digital model's crossfade destination. Ramps (fb, dlv, rvl) hold the end
 * of the previous block. Cleared on every seat.
 */
typedef struct {
    float dec_z[2][2];
    float out_z[2][2];
    float prev_out[2];
    float tap_a[2];
    float tap_b[2];
    float xfade;
    float xfade_inc;
    float lp[2];
    float hp_x[2];
    float hp_y[2];
    float ph_wow;
    float ph_flutter;
    float ph_tri;
    float ph_rvb;
    float fb;
    float dlv;
    float rvl;
    float rv_bw;
    float rv_damp[2];
    float rv_tank[2];
    uint32_t dly_len;
    uint32_t wp;
    uint32_t valid;
    uint32_t rv_ptr;
    uint32_t noise;
    uint32_t primed;
} DlyVerbState;

_Static_assert(sizeof(DlyVerbState) <= DLYVERB_STATE_BYTES,
               "DlyVerbState must fit its reserved share bytes");

uint8_t dlyVerb_divisionCc(uint8_t division)
{
    return dv_division_cc[(division >= DLYVERB_DIVISION_COUNT)
                          ? (DLYVERB_DIVISION_COUNT - 1u) : division];
}

/* One 9 kHz low-pass biquad step (transposed direct form II). */
static inline float dv_biquad(float x, float *z)
{
    const float y = DV_B0 * x + z[0];

    z[0] = DV_B1 * x - DV_A1 * y + z[1];
    z[1] = DV_B2 * x - DV_A2 * y;
    return y;
}

/* Hard clamp to +-lim. */
static inline float dv_clamp(float x, float lim)
{
    x = (x > lim) ? lim : x;
    return (x < -lim) ? -lim : x;
}

/*
 * Cubic soft clip: x - 4x^3/27 on +-1.5, which reaches exactly +-1 with a
 * zero slope at the clamp. No division.
 */
static inline float dv_soft(float x)
{
    x = dv_clamp(x, 1.5f);
    return x - 0.148148148f * x * x * x;
}

/* Bipolar parabolic sine of a 0..1 phase (peak error about 5 %). */
static inline float dv_sine(float ph)
{
    const float x = 2.0f * ph - 1.0f;

    return -4.0f * x * (1.0f - fabsf(x));
}

/* 32-bit LCG noise in -1..1 (audio grade; the hardware RNG is not needed). */
static inline float dv_noise(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(int32_t)*seed * (1.0f / 2147483648.0f);
}

/*
 * Linear-interpolated read of one delay ring, `d` half-rate samples behind
 * the write position. d must be in 1..len-2. Wrap by compare, not modulo.
 */
static inline float dv_ringRead(const int16_t *ring, uint32_t len,
                                uint32_t wp, float d)
{
    float rp = (float)wp - d;
    uint32_t i0;
    uint32_t i1;
    float frac;

    rp += (rp < 0.0f) ? (float)len : 0.0f;
    i0 = (uint32_t)rp;
    frac = rp - (float)i0;
    i0 -= (i0 >= len) ? len : 0u;
    i1 = i0 + 1u;
    i1 -= (i1 >= len) ? len : 0u;
    return ((float)ring[i0] + frac * (float)(ring[i1] - ring[i0])) * DV_INV16;
}

/* Reverb ring access relative to the decrementing pointer. */
#define DV_RD(b, d) ((float)rv[(ptr + (uint32_t)(b) + (uint32_t)(d)) & \
                               DV_RVB_MASK] * DV_RVB_INV)
#define DV_WR(b, v) (rv[(ptr + (uint32_t)(b)) & DV_RVB_MASK] = \
                     (int16_t)(dv_clamp((v), 3.99f) * DV_RVB_SCALE))

/*
 * One Schroeder all-pass on a reverb line: w = x + g*d is stored, the output
 * is d - g*w.
 */
static inline float dv_allpass(int16_t *rv, uint32_t ptr, int base, int len,
                               float g, float x)
{
    const float d = DV_RD(base, len);
    const float w = x + g * d;

    DV_WR(base, w);
    return d - g * w;
}

/*
 * One sample of the Dattorro plate.
 *
 * What: input band-limit, four input diffusers, then the two cross-coupled
 * tank halves (modulated all-pass, delay, damping, decay, all-pass, delay)
 * and the paper's seven output taps per side. The cross-feed is soft-clipped
 * (transparent below about 0.3): a sustained tone on a tank mode, which the
 * `fbk` route can create, then saturates instead of growing to the ring's
 * +-4 storage limit. The pointer is decremented
 * by the caller once per sample. Inputs: state, ring, pointer, mono input,
 * decay, the second tank all-pass gain, damping, modulation 0..1.
 * Outputs: left and right reverb.
 */
static inline void dv_plate(DlyVerbState *st, int16_t *rv, uint32_t ptr,
                            float x, float decay, float dd2, float damp,
                            float mod, float *out_l, float *out_r)
{
    const float tank_l = st->rv_tank[0];
    const float tank_r = st->rv_tank[1];
    float a;
    float d;
    float pos;
    int i;
    float f;
    float w;

    st->rv_bw += 0.85f * (x - st->rv_bw);
    a = dv_allpass(rv, ptr, DV_B_IN1, DV_L_IN1, 0.75f, st->rv_bw);
    a = dv_allpass(rv, ptr, DV_B_IN2, DV_L_IN2, 0.75f, a);
    a = dv_allpass(rv, ptr, DV_B_IN3, DV_L_IN3, 0.625f, a);
    a = dv_allpass(rv, ptr, DV_B_IN4, DV_L_IN4, 0.625f, a);

    /* Left half: modulated all-pass (fractional read), delay, damping. */
    pos = (float)(DV_L_AP1L + DV_RVB_EXC) + (float)DV_RVB_EXC * 0.75f * mod;
    i = (int)pos;
    f = pos - (float)i;
    d = DV_RD(DV_B_AP1L, i) + f * (DV_RD(DV_B_AP1L, i + 1) -
                                   DV_RD(DV_B_AP1L, i));
    w = a + decay * dv_soft(tank_r) + 0.7f * d;
    DV_WR(DV_B_AP1L, w);
    DV_WR(DV_B_D1L, d - 0.7f * w);
    d = DV_RD(DV_B_D1L, DV_L_D1L);
    st->rv_damp[0] = d + damp * (st->rv_damp[0] - d);
    d = dv_allpass(rv, ptr, DV_B_AP2L, DV_L_AP2L, dd2,
                   st->rv_damp[0] * decay);
    DV_WR(DV_B_D2L, d);

    /* Right half, modulated in quadrature. */
    pos = (float)(DV_L_AP1R + DV_RVB_EXC) - (float)DV_RVB_EXC * 0.75f * mod;
    i = (int)pos;
    f = pos - (float)i;
    d = DV_RD(DV_B_AP1R, i) + f * (DV_RD(DV_B_AP1R, i + 1) -
                                   DV_RD(DV_B_AP1R, i));
    w = a + decay * dv_soft(tank_l) + 0.7f * d;
    DV_WR(DV_B_AP1R, w);
    DV_WR(DV_B_D1R, d - 0.7f * w);
    d = DV_RD(DV_B_D1R, DV_L_D1R);
    st->rv_damp[1] = d + damp * (st->rv_damp[1] - d);
    d = dv_allpass(rv, ptr, DV_B_AP2R, DV_L_AP2R, dd2,
                   st->rv_damp[1] * decay);
    DV_WR(DV_B_D2R, d);

    st->rv_tank[0] = DV_RD(DV_B_D2L, DV_L_D2L);
    st->rv_tank[1] = DV_RD(DV_B_D2R, DV_L_D2R);

    /* Dattorro output taps (paper values x 0.72). */
    *out_l = 0.6f * (DV_RD(DV_B_D1R, 192) + DV_RD(DV_B_D1R, 2141)
                     - DV_RD(DV_B_AP2R, 1377) + DV_RD(DV_B_D2R, 1437)
                     - DV_RD(DV_B_D1L, 1433) - DV_RD(DV_B_AP2L, 135)
                     - DV_RD(DV_B_D2L, 768));
    *out_r = 0.6f * (DV_RD(DV_B_D1L, 254) + DV_RD(DV_B_D1L, 2611)
                     - DV_RD(DV_B_AP2L, 884) + DV_RD(DV_B_D2L, 1925)
                     - DV_RD(DV_B_D1R, 1520) - DV_RD(DV_B_AP2R, 241)
                     - DV_RD(DV_B_D2R, 87));
}

/*
 * Seat the DSP state in the current Effect share.
 *
 * What: when the share differs from the seated one (first block after init,
 * or a share change), the state is cleared, the reverb ring is zeroed (32 KB,
 * once) and the delay rings are muted through `valid` instead of a bulk
 * clear ("clear unless you adopt"; the previous owner's content is never
 * played). Inputs: runtime, share (may be NULL). Output: the state, or NULL
 * if the share is too small. Callers: dlyVerb_process(),
 * dlyVerb_bufferChanged().
 */
static DlyVerbState *dv_seat(DlyVerbRuntime *rt, const fx_share_t *share)
{
    DlyVerbState *st;

    if (!share || !share->base || share->bytes < DLYVERB_BUFFER_MIN_BYTES) {
        rt->state = NULL;
        rt->seated_bytes = 0u;
        return NULL;
    }
    if (rt->state && rt->seated_bytes == share->bytes &&
        rt->seated_offset == share->offset)
        return (DlyVerbState *)rt->state;

    st = (DlyVerbState *)share->base;
    memset(st, 0, sizeof(*st));
    memset(share->base + DLYVERB_STATE_BYTES, 0, DLYVERB_RVB_BYTES);
    st->dly_len = (share->bytes - DLYVERB_STATE_BYTES - DLYVERB_RVB_BYTES)
                  / 4u;
    st->noise = 0x2545F491u;
    rt->state = st;
    rt->seated_bytes = share->bytes;
    rt->seated_offset = share->offset;
    return st;
}

/*
 * Initialise a newly live runtime: unseated, so the first block seats it.
 * The handoff is never adopted.
 */
static void dlyVerb_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    DlyVerbRuntime *rt = (DlyVerbRuntime *)rt_void;

    (void)handoff;
    rt->state = NULL;
    rt->seated_bytes = 0u;
    rt->seated_offset = 0u;
}

/*
 * Describe the share use when DlyVerb is exited or refreshed: 2 channels,
 * 16 bits, 22,054 Hz, and FXBUF_STATE_EFFECT_WRITTEN once anything was
 * written. Positions are not exported (no type adopts this layout).
 */
static void dlyVerb_exportHandoff(const void *rt_void, fxbuf_handoff_t *out)
{
    const DlyVerbRuntime *rt = (const DlyVerbRuntime *)rt_void;
    const DlyVerbState *st;

    if (!rt || !out)
        return;
    out->effect_channels = 2u;
    out->effect_bits = 16u;
    out->effect_rate_hz = (uint16_t)DLYVERB_HALF_RATE_HZ;
    st = (const DlyVerbState *)rt->state;
    if (st && st->valid != 0u)
        out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
}

/* Store one resolved row value (rows 3..18); conversions happen per block. */
static void dlyVerb_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    DlyVerbRuntime *rt = (DlyVerbRuntime *)rt_void;

    if (index >= DLYVERB_PARAM_DIVISION &&
        index < DLYVERB_PARAM_DIVISION + DLYVERB_TYPE_ROWS)
        rt->raw[index - DLYVERB_PARAM_DIVISION] = value;
}

/* Raw row value by descriptor index. */
#define DV_ROW(rt, idx) ((rt)->raw[(idx) - DLYVERB_PARAM_DIVISION])

/* 0..127 row to an exponential frequency between lo and hi (Hz). */
static inline float dv_expHz(uint8_t v, float lo, float hi)
{
    return lo * expf((float)v * (1.0f / 127.0f) * logf(hi / lo));
}

/*
 * Process one Effect bus block in place (100 % wet).
 *
 * What: converts the rows once, decimates the block to 16 half-rate frames,
 * runs the delay and plate on each, then interpolates back to 32 frames and
 * writes the wet signal over io->l/io->r. Inputs: runtime and normalized
 * stereo io (frames <= 32, even). Output: io in place, rings in the share.
 * Caller: effects_process(). Affiliates: seq_getBpm(), stepScale_ticksQ8().
 */
static void dlyVerb_process(void *rt_void, effect_io_t *io)
{
    DlyVerbRuntime *rt = (DlyVerbRuntime *)rt_void;
    DlyVerbState *st;
    float *left;
    float *right;
    int16_t *rv;
    int16_t *ring_l;
    int16_t *ring_r;
    float xs_l[DV_HALF_FRAMES_MAX / 2u];
    float xs_r[DV_HALF_FRAMES_MAX / 2u];
    float ys_l[DV_HALF_FRAMES_MAX / 2u];
    float ys_r[DV_HALF_FRAMES_MAX / 2u];
    uint32_t half;
    uint32_t n;
    uint32_t len;
    uint32_t wp;
    uint32_t valid;
    uint32_t ptr;
    uint8_t model;
    uint8_t stereo;
    uint8_t route;
    uint16_t bpm;
    float per_tick;
    float t_l;
    float t_r;
    float cap;
    float glide_k;
    float sat_k;
    float w_tri;
    float w_wow;
    float w_flutter;
    float lp_a;
    float hp_r;
    float mod_depth;
    float inc_mod;
    float inc_flutter;
    float inc_rvb;
    float drive_mix;
    float drive_gain;
    float g_ll;
    float g_lr;
    float g_rl;
    float g_rr;
    float cross;
    float r_par;
    float r_fbk;
    float decay;
    float dd2;
    float damp;
    float hiss;
    float crackle_p;
    float crackle_amp;
    float fb;
    float dlv;
    float rvl;
    float fb_inc;
    float dlv_inc;
    float rvl_inc;
    float lp_hz;

    if (!rt || !io || !io->l || io->frames == 0u ||
        io->frames > DV_HALF_FRAMES_MAX)
        return;
    left = io->l;
    right = io->r;
    st = dv_seat(rt, io->share);
    if (!st) {
        for (n = 0u; n < io->frames; n++) {
            left[n] = 0.0f;
            if (right)
                right[n] = 0.0f;
        }
        return;
    }
    rv = (int16_t *)((uint8_t *)st + DLYVERB_STATE_BYTES);
    ring_l = rv + DLYVERB_RVB_SAMPLES;
    len = st->dly_len;
    ring_r = ring_l + len;
    half = io->frames / 2u;

    model = DV_ROW(rt, DLYVERB_PARAM_MODEL);
    model = (model > DLYVERB_MODEL_MAX) ? DLYVERB_MODEL_MAX : model;
    stereo = DV_ROW(rt, DLYVERB_PARAM_STEREO);
    route = DV_ROW(rt, DLYVERB_PARAM_RVB_ROUTE);

    /*
     * Tempo-synced targets. Width mode lengthens the right side by up to a
     * third. A length that does not fit the ring is halved until it does,
     * which keeps it on the beat.
     */
    bpm = seq_getBpm();
    per_tick = DV_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    t_l = ((float)stepScale_ticksQ8(dlyVerb_divisionCc(
               DV_ROW(rt, DLYVERB_PARAM_DIVISION))) / 256.0f) * per_tick;
    t_r = t_l;
    if (stereo == DLYVERB_STEREO_WIDTH)
        t_r = t_l * (1.0f + (float)DV_ROW(rt, DLYVERB_PARAM_WIDTH) *
                             (1.0f / (3.0f * 127.0f)));
    cap = (float)len - DV_TAP_MIN;
    while (t_r > cap && t_r > 2.0f * DV_TAP_MIN) {
        t_l *= 0.5f;
        t_r *= 0.5f;
    }
    t_l = (t_l < DV_TAP_MIN) ? DV_TAP_MIN : t_l;
    t_r = (t_r < DV_TAP_MIN) ? DV_TAP_MIN : t_r;
    t_l = (t_l > cap) ? cap : t_l;
    t_r = (t_r > cap) ? cap : t_r;
    if (!st->primed) {
        st->tap_a[0] = st->tap_b[0] = t_l;
        st->tap_a[1] = st->tap_b[1] = t_r;
        st->primed = 1u;
    }

    /*
     * Model coefficients. dig: no glide (a time change crossfades two taps),
     * no saturation beyond a hard clamp, triangle modulation. bbd: fast
     * glide, gentle saturation, triangle modulation, bandwidth that falls as
     * the delay gets longer (a real BBD clocks slower). tap: slow glide,
     * full saturation, wow and flutter.
     */
    if (model == DLYVERB_MODEL_DIGITAL) {
        glide_k = 0.0f; sat_k = 0.0f;
        w_tri = 1.0f; w_wow = 0.0f; w_flutter = 0.0f;
        lp_hz = 10500.0f;
    } else if (model == DLYVERB_MODEL_BBD) {
        glide_k = 1.0f / (0.06f * DV_FS); sat_k = 0.5f;
        w_tri = 1.0f; w_wow = 0.0f; w_flutter = 0.0f;
        lp_hz = 4500.0f * (0.25f * DV_FS) / t_l;
        lp_hz = (lp_hz > 9000.0f) ? 9000.0f : lp_hz;
        lp_hz = (lp_hz < 1800.0f) ? 1800.0f : lp_hz;
    } else {
        glide_k = 1.0f / (0.25f * DV_FS); sat_k = 1.0f;
        w_tri = 0.0f; w_wow = 1.0f; w_flutter = 0.3f;
        lp_hz = 10500.0f;
    }
    {
        const float user_hz = dv_expHz(DV_ROW(rt, DLYVERB_PARAM_LOWPASS),
                                       400.0f, 10500.0f);
        lp_hz = (user_hz < lp_hz) ? user_hz : lp_hz;
    }
    lp_a = 1.0f - expf(-DV_TWO_PI * lp_hz / DV_FS);
    hp_r = expf(-DV_TWO_PI * dv_expHz(DV_ROW(rt, DLYVERB_PARAM_HIGHPASS),
                                      15.0f, 2000.0f) / DV_FS);

    /* Digital model: a new target starts a crossfade to a second tap. */
    if (st->xfade_inc == 0.0f) {
        if (model == DLYVERB_MODEL_DIGITAL &&
            (fabsf(t_l - st->tap_a[0]) > 0.5f ||
             fabsf(t_r - st->tap_a[1]) > 0.5f)) {
            st->tap_b[0] = t_l;
            st->tap_b[1] = t_r;
            st->xfade = 0.0f;
            st->xfade_inc = DV_FADE_INC;
        }
    }

    {
        const float m = (float)DV_ROW(rt, DLYVERB_PARAM_MOD_DEPTH) *
                        (1.0f / 127.0f);
        const float rate = dv_expHz(DV_ROW(rt, DLYVERB_PARAM_MOD_RATE),
                                    0.05f, 8.0f);
        float fl = rate * 6.7f;

        fl = (fl > 25.0f) ? 25.0f : fl;
        mod_depth = m * m * DV_MOD_MAX;
        inc_mod = rate / DV_FS;
        inc_flutter = fl / DV_FS;
        inc_rvb = 0.7f / DV_FS;
    }
    {
        const float d = (float)DV_ROW(rt, DLYVERB_PARAM_DRIVE) *
                        (1.0f / 127.0f);

        drive_mix = d;
        drive_gain = 1.0f + 7.0f * d * d;
    }
    {
        const float a = (float)DV_ROW(rt, DLYVERB_PARAM_AGE) *
                        (1.0f / 127.0f);

        hiss = a * a * 0.03f;
        crackle_p = a * a * 0.0004f;
        crackle_amp = 0.25f * a;
    }

    /* Stereo routing: ping-pong feeds the mono input into the left ring. */
    if (stereo == DLYVERB_STEREO_PINGPONG) {
        g_ll = 0.5f; g_lr = 0.5f; g_rl = 0.0f; g_rr = 0.0f; cross = 1.0f;
    } else {
        g_ll = 1.0f; g_lr = 0.0f; g_rl = 0.0f; g_rr = 1.0f; cross = 0.0f;
    }
    r_par = (route == DLYVERB_ROUTE_PARALLEL) ? 1.0f : 0.0f;
    r_fbk = (route == DLYVERB_ROUTE_FEEDBACK) ? 1.0f : 0.0f;
    {
        const float r = (float)DV_ROW(rt, DLYVERB_PARAM_RVB_DECAY) *
                        (1.0f / 127.0f);

        decay = 0.2f + 0.76f * sqrtf(r);
        dd2 = decay + 0.15f;
        dd2 = (dd2 > 0.5f) ? 0.5f : dd2;
        dd2 = (dd2 < 0.25f) ? 0.25f : dd2;
        damp = 0.05f + 0.85f * (float)DV_ROW(rt, DLYVERB_PARAM_RVB_DAMP) *
                       (1.0f / 127.0f);
    }

    /* Gain ramps across the block's half-rate frames. */
    fb = st->fb;
    dlv = st->dlv;
    rvl = st->rvl;
    {
        const float fb_to = (float)DV_ROW(rt, DLYVERB_PARAM_FEEDBACK) *
                            (DV_FEEDBACK_MAX / 127.0f);
        const float dlv_to = (float)DV_ROW(rt, DLYVERB_PARAM_DLY_LEVEL) *
                             (1.0f / 127.0f);
        const float rvl_to = (float)DV_ROW(rt, DLYVERB_PARAM_RVB_LEVEL) *
                             (1.0f / 127.0f);
        const float per = 1.0f / (float)(half ? half : 1u);

        fb_inc = (fb_to - fb) * per;
        dlv_inc = (dlv_to - dlv) * per;
        rvl_inc = (rvl_to - rvl) * per;
        st->fb = fb_to;
        st->dlv = dlv_to;
        st->rvl = rvl_to;
    }

    /* 2:1 decimation behind the 9 kHz image filter. */
    for (n = 0u; n < half; n++) {
        const float r0 = right ? right[2u * n] : left[2u * n];
        const float r1 = right ? right[2u * n + 1u] : left[2u * n + 1u];

        (void)dv_biquad(left[2u * n], st->dec_z[0]);
        (void)dv_biquad(r0, st->dec_z[1]);
        xs_l[n] = dv_biquad(left[2u * n + 1u], st->dec_z[0]);
        xs_r[n] = dv_biquad(r1, st->dec_z[1]);
    }

    wp = st->wp;
    valid = st->valid;
    ptr = st->rv_ptr;
    for (n = 0u; n < half; n++) {
        float in_l;
        float in_r;
        float mod;
        float d_l;
        float d_r;
        float e_l;
        float e_r;
        float rin;
        float rv_l;
        float rv_r;
        float s_l;
        float s_r;
        float w_l;
        float w_r;
        float nz;

        fb += fb_inc;
        dlv += dlv_inc;
        rvl += rvl_inc;

        /* Drive: crossfade from clean to the soft clipper. */
        in_l = xs_l[n] + drive_mix * (dv_soft(drive_gain * xs_l[n]) - xs_l[n]);
        in_r = xs_r[n] + drive_mix * (dv_soft(drive_gain * xs_r[n]) - xs_r[n]);

        /* Modulation (constant cost: all three shapes every sample). */
        mod = mod_depth * (w_tri * (1.0f - 4.0f * fabsf(st->ph_tri - 0.5f)) +
                           w_wow * dv_sine(st->ph_tri) +
                           w_flutter * dv_sine(st->ph_flutter));

        /* Two taps per side; reads beyond what was written are silent. */
        {
            const float al = st->tap_a[0] + mod;
            const float bl = st->tap_b[0] + mod;
            const float ar = st->tap_a[1] - mod;
            const float br = st->tap_b[1] - mod;
            float va;
            float vb;

            va = dv_ringRead(ring_l, len, wp, al);
            vb = dv_ringRead(ring_l, len, wp, bl);
            va = ((al + 2.0f) <= (float)valid) ? va : 0.0f;
            vb = ((bl + 2.0f) <= (float)valid) ? vb : 0.0f;
            d_l = va + st->xfade * (vb - va);
            va = dv_ringRead(ring_r, len, wp, ar);
            vb = dv_ringRead(ring_r, len, wp, br);
            va = ((ar + 2.0f) <= (float)valid) ? va : 0.0f;
            vb = ((br + 2.0f) <= (float)valid) ? vb : 0.0f;
            d_r = va + st->xfade * (vb - va);
        }

        /* Echo tone: one-pole low-pass then one-pole high-pass. */
        st->lp[0] += lp_a * (d_l - st->lp[0]);
        st->lp[1] += lp_a * (d_r - st->lp[1]);
        st->hp_y[0] = hp_r * (st->hp_y[0] + st->lp[0] - st->hp_x[0]);
        st->hp_x[0] = st->lp[0];
        st->hp_y[1] = hp_r * (st->hp_y[1] + st->lp[1] - st->hp_x[1]);
        st->hp_x[1] = st->lp[1];
        e_l = st->hp_y[0];
        e_r = st->hp_y[1];

        /* Plate: fed by the echoes, or by the driven input (par). */
        rin = 0.5f * ((e_l + e_r) + r_par * ((in_l + in_r) - (e_l + e_r)));
        dv_plate(st, rv, ptr, 0.5f * rin, decay, dd2, damp,
                 dv_sine(st->ph_rvb), &rv_l, &rv_r);
        ptr = (ptr - 1u) & DV_RVB_MASK;

        /*
         * Feedback: the echo, or (fbk route) partly the reverb at half
         * level, because the plate's resonant modes have more than unity
         * gain and would self-oscillate the loop at moderate feedback.
         */
        s_l = e_l + r_fbk * rvl * (0.5f * rv_l - e_l);
        s_r = e_r + r_fbk * rvl * (0.5f * rv_r - e_r);

        /* Age: hiss and sparse crackle enter the loop and echo with it. */
        nz = dv_noise(&st->noise);
        nz = hiss * nz + ((fabsf(nz) < crackle_p) ? crackle_amp : 0.0f);

        w_l = g_ll * in_l + g_lr * in_r +
              fb * (s_l + cross * (s_r - s_l)) + nz;
        w_r = g_rl * in_l + g_rr * in_r +
              fb * (s_r + cross * (s_l - s_r)) - nz;
        w_l = dv_clamp(w_l, 1.0f) + sat_k * (dv_soft(w_l) -
                                             dv_clamp(w_l, 1.0f));
        w_r = dv_clamp(w_r, 1.0f) + sat_k * (dv_soft(w_r) -
                                             dv_clamp(w_r, 1.0f));
        ring_l[wp] = (int16_t)(w_l * 32767.0f);
        ring_r[wp] = (int16_t)(w_r * 32767.0f);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;

        /* Wet sum through a gentle limiter (2 % at 0.5, ceiling 1.33). */
        ys_l[n] = DV_LIMIT_INV * dv_soft(DV_LIMIT * (dlv * e_l + rvl * rv_l));
        ys_r[n] = DV_LIMIT_INV * dv_soft(DV_LIMIT * (dlv * e_r + rvl * rv_r));

        /* Taps: glide (bbd/tap) and crossfade (dig). */
        st->tap_a[0] += (t_l - st->tap_a[0]) * glide_k;
        st->tap_a[1] += (t_r - st->tap_a[1]) * glide_k;
        st->xfade += st->xfade_inc;
        if (st->xfade >= 1.0f) {
            st->tap_a[0] = st->tap_b[0];
            st->tap_a[1] = st->tap_b[1];
            st->xfade = 0.0f;
            st->xfade_inc = 0.0f;
        }

        st->ph_tri += inc_mod;
        st->ph_tri -= (st->ph_tri >= 1.0f) ? 1.0f : 0.0f;
        st->ph_flutter += inc_flutter;
        st->ph_flutter -= (st->ph_flutter >= 1.0f) ? 1.0f : 0.0f;
        st->ph_rvb += inc_rvb;
        st->ph_rvb -= (st->ph_rvb >= 1.0f) ? 1.0f : 0.0f;
    }
    /* A model change away from dig mid-fade lets the glide take over. */
    if (model != DLYVERB_MODEL_DIGITAL && st->xfade_inc == 0.0f) {
        st->tap_b[0] = st->tap_a[0];
        st->tap_b[1] = st->tap_a[1];
    }
    st->wp = wp;
    st->valid = valid;
    st->rv_ptr = ptr;

    /* 1:2 interpolation (linear, then the 9 kHz image filter). */
    for (n = 0u; n < half; n++) {
        const float ml = 0.5f * (st->prev_out[0] + ys_l[n]);
        const float mr = 0.5f * (st->prev_out[1] + ys_r[n]);
        const float o0l = dv_biquad(ml, st->out_z[0]);
        const float o0r = dv_biquad(mr, st->out_z[1]);
        const float o1l = dv_biquad(ys_l[n], st->out_z[0]);
        const float o1r = dv_biquad(ys_r[n], st->out_z[1]);

        st->prev_out[0] = ys_l[n];
        st->prev_out[1] = ys_r[n];
        if (right) {
            left[2u * n] = o0l;
            left[2u * n + 1u] = o1l;
            right[2u * n] = o0r;
            right[2u * n + 1u] = o1r;
        } else {
            left[2u * n] = 0.5f * (o0l + o0r);
            left[2u * n + 1u] = 0.5f * (o1l + o1r);
        }
    }
}

/* Share change: re-seat (clears the reverb, mutes the delay rings). */
static void dlyVerb_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)dv_seat((DlyVerbRuntime *)rt_void, share);
}

/* DlyVerb operations; the delay folds its length, so no buffer clamp. */
const effect_type_ops_t dlyVerb_ops = {
    dlyVerb_init,
    dlyVerb_exportHandoff,
    dlyVerb_writeParam,
    dlyVerb_process,
    dlyVerb_bufferChanged,
    NULL,
};
