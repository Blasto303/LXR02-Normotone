/*
 * Core/DSP/Effects/Destroy/DestroyModels.c
 *
 * Normotone `dst` models 1..8 (fast-math, foreground render). Each one is a
 * small nonlinear feedback network; all loops pass a saturator, so they stay
 * bounded at any setting (some choke on purpose at high feedback).
 *
 *   1 hustle    feedback / fb_shift / disorder: the mids through a shaper
 *               that morphs from overdrive to full-wave (octave up) to a
 *               double rectifier (two octaves up), fed back through a micro
 *               delay (2..1000 samples) that the dynamics stretch (flange);
 *               the loop starves the input at high feedback (it chokes)
 *   2 dom       drive / doom / detune: saturation, then a suboctave chorus
 *               (two octave-down shifters detuned by a slow LFO) that gets
 *               louder and more out of tune with doom; overtone buzz in its
 *               top quarter (after the "DOOM" block of a known stereo
 *               distortion module, written from its public manual)
 *   3 mutiny    gain / feedback / fb_tune: overdrive into a near-linear
 *               tuned comb that rings at the fb_tune pitch (shrill)
 *   4 fld       fold / 8vize / drive, plus chn and dom on the FM screen:
 *               fold, DOOM, rectify, saturate in the order UND (fold doom
 *               rectify saturate), X (fold saturate rectify doom) or OVR
 *               (UND then a second fold); the folders run at 2x
 *   5 assault   gain / feedback / fb_tune: three bands distorted apart,
 *               tube-like asymmetric lows, hard-clipped mids ringing in a
 *               comb tuned by fb_tune, wavefolded highs
 *   6 bloodshed gain / pitchback / grit: a granular +1 semitone shifter whose
 *               overdriven output is fed back into it (rising spiral)
 *   7 bloodbath feedback / dirt / dirtrate: short metallic pseudo-reverb,
 *               driven hard, with a bit-crushed held feedback
 *   8 mbs       drive / center / notch: three bands saturated apart, drive
 *               moving the emphasis from the low band to the high band,
 *               center placing the middle band (200 Hz..4 kHz); notch off,
 *               fixed at 1 kHz, or tracking the center
 *  10..16      the bloodshed loop with other intervals: shm +7 (fifth),
 *               dwn -12 (octave down), fal -1, spl +1 and -1 together,
 *               qtr +0.5, itv any interval -12..+12 (pot 1)
 *  16 smo       trigger / smoosh / phase: up to ~126 dB of gain into a hard
 *               clip, opened by a voice's triggers (pot 1: fixed, DR1..OHH)
 *               and decaying 20 ms..1 s, or fixed; phase = allpass stereo
 *               width / phasing
 *   9 saw       dist / low / high: no feedback. High-passed input, high
 *               gain into a hard diode-style clip, then two resonant
 *               band boosts after the clip (about 100 Hz and 1 kHz) with
 *               the mids between them left out: the "chainsaw" pedal sound
 *               when both are up. Written from the general principle of
 *               such pedals, not from any schematic.
 *
 * Every loop gain stays at or below the feedback knob, so nothing
 * oscillates on its own once the input stops.
 * "Tuned" loops: fb_tune sets the comb pitch from 30 Hz to about 1.9 kHz.
 * The input dynamics (env) scale the feedback between 0.55x and 1x.
 */
#include "DestroyModels.h"
#include <math.h>

#define DSM_FS        44108.07f
#define DSM_SCALE     8192.0f
#define DSM_INV       (1.0f / 8192.0f)

static const uint16_t dsm_ap_len[DSM_AP_COUNT] = { 142u, 107u, 379u, 277u };

/* Output trims so the models sit near the dry level (bench, mix 127). */
/* Voices triggered since the last block (bit = track), for smo. */
static volatile uint8_t dsm_trig_mask;

void dsm_noteTrigger(uint8_t track, uint8_t velocity)
{
    if (track < 7u && velocity != 0u)
        dsm_trig_mask = (uint8_t)(dsm_trig_mask | (1u << track));
}

static const float dsm_trim[DSM_ALG_COUNT] = {
    1.0f, 3.2f, 2.0f, 3.2f, 1.0f, 1.8f, 0.9f, 1.5f, 1.8f, 1.0f,
    0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 1.45f
};

/* Pool layout, per channel. */
#define DSM_MD_OFF(c)     ((c) * DSM_MD_LEN)
#define DSM_AP_BASE       (2u * DSM_MD_LEN)
#define DSM_AP_SPAN       (142u + 107u + 379u + 277u)
#define DSM_RV_BASE       (DSM_AP_BASE + 2u * DSM_AP_SPAN)
#define DSM_RV_OFF(c)     (DSM_RV_BASE + (c) * DSM_RV_LEN)

_Static_assert(DSM_RV_BASE + 2u * DSM_RV_LEN <= DSM_POOL_SAMPLES,
               "Destroy model lines must fit the pool");

static const char dsm_names[DSM_ALG_COUNT][4] = {
    "cls", "hus", "dom", "mut", "fld", "ass", "bls", "blb", "mbs", "saw",
    "shm", "dwn", "fal", "spl", "qtr", "itv", "smo"
};
static const char dsm_knobs[DSM_ALG_COUNT][10] = {
    "drifldsub",   /* cls: drive fold sub     */
    "fbkfshdis",   /* hustle                  */
    "drvdomdtn",   /* dom: drive doom detune    */
    "gaifbkftn",   /* mutiny                  */
    "fld8vzdrv",   /* fld: fold 8vize drive     */
    "gaifbkftn",   /* assault                 */
    "gaipbkgrt",   /* bloodshed               */
    "fbkdrtdrr",   /* bloodbath               */
    "drvctrntc",   /* mbs: drive center notch */
    "dislowhig",   /* saw: dist, low, high    */
    "gaipbkgrt",   /* shm +7                  */
    "gaipbkgrt",   /* dwn -12                 */
    "gaipbkgrt",   /* fal -1                  */
    "gaipbkgrt",   /* spl +1 / -1             */
    "gaipbkgrt",   /* qtr +0.5                */
    "itvpbkgrt",   /* itv: interval           */
    "trgsmophs",   /* smo: trigger smoosh phase */
};

const char *dsm_algName(uint8_t alg)
{
    return dsm_names[(alg < DSM_ALG_COUNT) ? alg : 0u];
}

const char *dsm_knobNames(uint8_t alg)
{
    return dsm_knobs[(alg < DSM_ALG_COUNT) ? alg : 0u];
}

static inline float dsm_soft(float x)
{
    return x / (1.0f + fabsf(x));
}

static inline float dsm_clamp(float x, float l)
{
    return (x > l) ? l : ((x < -l) ? -l : x);
}

/* Bipolar parabolic sine, period 1 (as ds_psin). */
static inline float dsm_psin(float ph)
{
    const int32_t i = (int32_t)ph;
    float x;

    ph -= (float)i;
    ph += (ph < 0.0f) ? 1.0f : 0.0f;
    x = 2.0f * ph - 1.0f;
    return -4.0f * x * (1.0f - fabsf(x));
}

/* Line read `d` samples behind the write index w (linear, 1 <= d < len). */
static inline float dsm_read(const int16_t *line, uint32_t len, uint32_t w,
                             float d)
{
    float rp = (float)w - d;
    uint32_t i0;
    uint32_t i1;
    float f;

    rp += (rp < 0.0f) ? (float)len : 0.0f;
    i0 = (uint32_t)rp;
    f = rp - (float)i0;
    i0 -= (i0 >= len) ? len : 0u;
    i1 = i0 + 1u;
    i1 -= (i1 >= len) ? len : 0u;
    return ((float)line[i0] + f * (float)(line[i1] - line[i0])) * DSM_INV;
}

/* Integer read `d` samples behind w (fixed line lengths). */
static inline float dsm_readI(const int16_t *line, uint32_t len, uint32_t w,
                              uint32_t d)
{
    const uint32_t i = (w >= d) ? (w - d) : (w + len - d);

    return (float)line[i] * DSM_INV;
}

static inline void dsm_write(int16_t *line, uint32_t w, float v)
{
    line[w] = (int16_t)(dsm_clamp(v, 3.99f) * DSM_SCALE);
}

static inline uint16_t dsm_next(uint16_t w, uint32_t len)
{
    w++;
    return (uint16_t)((w >= len) ? 0u : w);
}

static inline float dsm_dc(DsModelState *m, uint32_t c, float x)
{
    const float y = x - m->dc_x[c] + 0.995f * m->dc_y[c];

    m->dc_x[c] = x;
    m->dc_y[c] = y;
    return y;
}

/* Quantize to k_q levels (bit crush). */
static inline float dsm_crush(const DsModelState *m, float x)
{
    return floorf(x * m->k_q + 0.5f) / m->k_q;
}

void dsm_block(DsModelState *m, uint8_t alg, uint8_t p1, uint8_t p2,
               uint8_t p3)
{
    const float a = (float)p1 * (1.0f / 127.0f);
    const float b = (float)p2 * (1.0f / 127.0f);
    const float c = (float)p3 * (1.0f / 127.0f);
    float tune = c;

    m->alg = alg;
    m->k_g = 1.0f;
    m->k_fb = 0.0f;
    m->k_x = 0.0f;
    m->k_q = 32768.0f;
    m->k_hold = 1u;
    switch (alg) {
    case 1u:                                      /* hustle */
        m->k_fb = a;
        m->k_tap = 2.0f * expf(b * 6.2f);         /* 2..985 samples */
        m->k_x = c;                               /* disorder */
        tune = -1.0f;
        break;
    case 2u:                                      /* dom (block) */
        m->k_g = 1.0f + 20.0f * a * a;            /* drive */
        m->k_x = b;                               /* doom */
        m->k_q = 0.002f + 0.035f * c + 0.03f * b; /* detune grows with doom */
        tune = -1.0f;
        break;
    case 3u:                                      /* mutiny */
        m->k_g = 1.0f + 30.0f * a * a;
        m->k_fb = 0.985f * b;
        break;
    case 5u:                                      /* assault */
        m->k_g = 1.0f + 24.0f * a * a;
        m->k_fb = 0.96f * b;
        break;
    case 4u:                                      /* fld (block) */
        m->k_g = 1.0f + 15.0f * a;                /* fold gain */
        m->k_x = b;                               /* 8vize */
        m->k_q = 1.0f + 12.0f * c * c;            /* saturate */
        tune = -1.0f;
        break;
    case 6u:                                      /* bloodshed */
        m->k_g = 1.0f + 15.0f * a;
        m->k_fb = 0.98f * b;                      /* pitchback */
        m->k_x = 1.0f + 15.0f * c;                /* grit */
        m->k_q = (1.0f + m->k_x) / m->k_x;        /* 1 / soft(grit) */
        tune = -1.0f;
        break;
    case 8u: {                                    /* mbs (block) */
        const float fcen = 200.0f * exp2f(b * 4.32f);      /* 200 Hz..4 kHz */
        const float e = 2.0f * a;                          /* emphasis 0..2 */
        uint8_t i;

        for (i = 0u; i < 3u; i++) {
            const float d = 1.0f - fabsf(e - (float)i);
            m->k_w[i] = 1.0f + 5.0f * ((d > 0.0f) ? d : 0.0f);
        }
        m->k_g = 2.0f + 6.0f * a;
        m->k_f = 1.0f - expf(-3.14159265f * fcen / DSM_FS);        /* fcen/2 */
        m->k_rate = 1.0f - expf(-12.5663706f * fcen / DSM_FS);     /* 2 fcen */
        m->k_notch = (uint8_t)(c >= 0.333f);
        if (m->k_notch) {
            const float fn = (c < 0.666f) ? 1000.0f : fcen;
            const float g = tanf(3.14159265f * fn / DSM_FS);

            m->k_n[0] = 1.0f / (1.0f + g * (g + 0.5f));
            m->k_n[1] = g * m->k_n[0];
            m->k_n[2] = g * m->k_n[1];
        }
        tune = -1.0f;
        break;
    }
    case 16u: {                                   /* smo (block) */
        const uint8_t sel = (uint8_t)(a * 7.99f);  /* 0 fixed, 1..7 track */
        const uint8_t tm = dsm_trig_mask;

        dsm_trig_mask = (uint8_t)(dsm_trig_mask & (uint8_t)~tm);
        m->k_x = (float)sel;
        m->k_q = b;                                 /* fixed smoosh amount */
        m->k_rate = expf(-1.0f / (DSM_FS * (0.02f + 0.98f * b * b)));
        m->k_hold = (uint16_t)((sel != 0u &&
                                (tm & (1u << (sel - 1u))) != 0u) ? 1u : 0u);
        m->k_f = c;                                 /* phase amount */
        m->k_step = 0.9f * c;                       /* allpass coefficient */
        tune = -1.0f;
        break;
    }
    case 7u: {                                    /* bloodbath */
        const float bits = (alg == 7u) ? 15.0f - 11.0f * b    /* 15..4 */
                                       : 12.0f - 10.0f * b;   /* 12..2 */

        m->k_fb = (alg == 7u) ? 0.92f * a : 0.95f * a;
        m->k_x = 2.0f + 10.0f * a;                /* mudslide fold depth */
        m->k_q = exp2f(bits - 1.0f);
        m->k_hold = (uint16_t)((alg == 7u) ? 1.0f + 23.0f * c * c
                                           : 2.0f + 46.0f * c);
        tune = -1.0f;
        break;
    }
    case 10u: case 11u: case 12u: case 13u: case 14u: case 15u:
    {                                             /* shifter family (block) */
        static const float steps[5] = {
            0.4983071f,     /* shm +7  */
            -0.5f,          /* dwn -12 */
            -0.0561257f,    /* fal -1  */
            0.0594631f,     /* spl +1 (and -1 below) */
            0.0293022f      /* qtr +0.5 */
        };

        m->k_g = (alg >= 15u) ? 7.0f : 1.0f + 15.0f * a;
        m->k_fb = 0.98f * b;                      /* pitchback */
        m->k_x = 1.0f + 15.0f * c;                /* grit */
        m->k_q = (1.0f + m->k_x) / m->k_x;
        m->k_win = 600.0f;
        m->k_step2 = -0.0561257f;
        if (alg <= 14u) {
            m->k_step = steps[alg - 10u];
            if (alg == 11u)
                m->k_win = 860.0f;                /* octave down: long grain */
        } else {                                  /* itv: -12..+12 */
            const int8_t n = (int8_t)((int16_t)(a * 24.0f + 0.5f) - 12);

            m->k_step = exp2f((float)n * (1.0f / 12.0f)) - 1.0f;
            if (n < -6)
                m->k_win = 860.0f;
        }
        /* Per-channel windows (L, R a bit longer) and tap speeds, once
         * per block: no division per sample. */
        m->k_winc[0] = m->k_win;
        m->k_winc[1] = (m->k_win * 1.1667f > 1000.0f) ? 1000.0f
                                                      : m->k_win * 1.1667f;
        m->k_inc[0] = m->k_step / m->k_winc[0];
        m->k_inc[1] = m->k_step / m->k_winc[1];
        m->k_inc2[0] = m->k_step2 / (m->k_winc[0] * 0.93f);
        m->k_inc2[1] = m->k_step2 / (m->k_winc[1] * 0.93f);
        tune = -1.0f;
        break;
    }
    case 9u:                                      /* saw */
        m->k_g = 2.0f + 250.0f * a * a;           /* dist: up to ~+48 dB */
        m->k_x = 4.0f * b;                        /* low boost, 0..x4    */
        m->k_q = 4.0f * c;                        /* high boost, 0..x4   */
        tune = -1.0f;
        break;
    default:
        break;
    }
    if (tune >= 0.0f) {
        const float hz = 30.0f * exp2f(tune * 6.0f);    /* 30 Hz .. 1.9 kHz */
        float tap = DSM_FS / hz;

        m->k_tap = (tap > (float)(DSM_MD_LEN - 2u)) ? (float)(DSM_MD_LEN - 2u)
                                                    : tap;
        m->k_tap2 = (uint16_t)(m->k_tap * 0.6667f + 0.5f);
        m->k_tap3 = (uint16_t)(m->k_tap * 0.5f + 0.5f);
    }
}

/* The four allpasses of the pseudo-reverb, channel c, in series. */
static float dsm_apChain(DsModelState *m, int16_t *pool, uint32_t c, float x,
                         float g)
{
    uint32_t off = DSM_AP_BASE + c * DSM_AP_SPAN;
    uint32_t k;

    for (k = 0u; k < DSM_AP_COUNT; k++) {
        int16_t *line = &pool[off];
        const uint32_t len = dsm_ap_len[k];
        const uint16_t w = m->ap_w[c][k];
        const float d = dsm_readI(line, len, w, len - 1u);
        const float v = x + g * d;

        dsm_write(line, w, v);
        m->ap_w[c][k] = dsm_next(w, len);
        x = d - g * v;
        off += len;
    }
    return x;
}

void dsm_chain(DsModelState *m, uint8_t chain, uint8_t doom)
{
    m->k_chain = (chain > 2u) ? 0u : chain;
    m->k_doom = (float)doom * (1.0f / 127.0f);
}

/*
 * Two-tap pitch shifter on the micro-delay line (as bloodshed): the taps
 * move by step = ratio - 1 per sample (inc = step / win, precomputed)
 * across a window of `win` samples,
 * half a window apart, crossfaded by a triangle. step < 0 shifts down.
 */
static float dsm_shift(const int16_t *md, uint16_t mw, float *ph, float inc,
                       float win)
{
    float p = *ph;
    float p2 = p + 0.5f;
    float w1;
    float y;

    p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
    w1 = 1.0f - fabsf(2.0f * p - 1.0f);
    y = w1 * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p * win) +
        (1.0f - w1) * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p2 * win);
    p -= inc;                                   /* inc = step / win */
    p += (p < 0.0f) ? 1.0f : 0.0f;
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    *ph = p;
    return y;
}

/* Same shifter with integer reads (spl's second voice: cheaper). */
static float dsm_shiftI(const int16_t *md, uint16_t mw, float *ph, float inc,
                        float win)
{
    float p = *ph;
    float p2 = p + 0.5f;
    float w1;
    float y;

    p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
    w1 = 1.0f - fabsf(2.0f * p - 1.0f);
    y = w1 * dsm_readI(md, DSM_MD_LEN, mw, (uint32_t)(8.0f + p * win)) +
        (1.0f - w1) * dsm_readI(md, DSM_MD_LEN, mw,
                                (uint32_t)(8.0f + p2 * win));
    p -= inc;
    p += (p < 0.0f) ? 1.0f : 0.0f;
    p -= (p >= 1.0f) ? 1.0f : 0.0f;
    *ph = p;
    return y;
}

float dsm_sample(DsModelState *m, int16_t *pool, uint32_t c, float x,
                 float env)
{
    int16_t *md = &pool[DSM_MD_OFF(c)];
    const uint16_t mw = m->md_w[c];
    const float fb = m->k_fb * (0.55f + 0.45f * env);
    float y = 0.0f;

    switch (m->alg) {
    case 1u: {                                    /* hustle */
        /* The dynamics stretch the micro delay: a flange that breathes. */
        float d = (c ? 1.013f : 1.0f) * m->k_tap * (1.0f + 0.5f * env);
        float z;
        float h;
        float v;
        float s1;
        float s2;
        float t;

        d = (d > 1000.0f) ? 1000.0f : d;
        z = dsm_read(md, DSM_MD_LEN, mw, d);
        m->lp_d[c] += 0.03f * (x - m->lp_d[c]);          /* ~210 Hz HP */
        h = x - m->lp_d[c];
        /* Choke: a loud loop starves the input (input-only term). */
        v = dsm_soft(4.0f * h * (1.0f - 0.85f * fb * fabsf(z)) +
                     0.25f * fb * z);
        s1 = 2.0f * fabsf(v) - 0.64f;                    /* octave up */
        s2 = 2.0f * fabsf(s1) - 0.64f;                   /* two octaves */
        t = 2.0f * m->k_x;                               /* disorder 0..2 */
        y = (t < 1.0f) ? v + t * (s1 - v) : s1 + (t - 1.0f) * (s2 - s1);
        y = dsm_dc(m, c, 0.8f * y);                      /* loop gain <= fb */
        dsm_write(md, mw, y);
        break;
    }
    case 2u: {                                    /* dom */
        /*
         * Saturation, then a suboctave chorus: two octave-down shifters on
         * the saturated signal, detuned up and down by a slow LFO (doom
         * raises both the level and the detune); in the top quarter of
         * doom the clipped suboctave adds an overtone buzz. No loop.
         */
        const float s = dsm_soft(m->k_g * x);
        const float det = m->k_q * (1.0f + 0.5f * dsm_psin(m->acc[c]));
        const float lvl = ((m->k_x < 0.75f) ? m->k_x * 1.3333f : 1.0f) * 1.6f;
        const float bz = (m->k_x > 0.75f) ? (m->k_x - 0.75f) * 4.0f : 0.0f;
        const float w1 = c ? 900.0f : 860.0f;
        const float w2 = w1 * 0.93f;
        float sub;

        m->acc[c] += c ? 0.0000771f : 0.0000680f;        /* ~3 Hz LFO */
        m->acc[c] -= (m->acc[c] >= 1.0f) ? 1.0f : 0.0f;
        dsm_write(md, mw, s);
        sub = 0.5f * (dsm_shift(md, mw, &m->ph[c],
                                (0.5f * (1.0f + det) - 1.0f) / w1, w1) +
                      dsm_shiftI(md, mw, &m->ph2[c],
                                 (0.5f * (1.0f - det) - 1.0f) / w2, w2));
        {
            float g = (env - 0.01f) * 20.0f;      /* gate (noise between hits) */

            g = (g < 0.0f) ? 0.0f : ((g > 1.0f) ? 1.0f : g);
            y = 0.8f * s + g * (lvl * sub +
                                bz * 0.6f * dsm_clamp(12.0f * sub, 0.8f));
        }
        y = dsm_dc(m, c, dsm_soft(1.2f * y));
        break;
    }
    case 3u: {                                    /* mutiny */
        const float z = dsm_read(md, DSM_MD_LEN, mw,
                                 c ? 1.006f * m->k_tap : m->k_tap);

        m->lp_a[c] += 0.6f * (z - m->lp_a[c]);           /* light damping */
        m->lp_d[c] += 0.02f * (x - m->lp_d[c]);          /* ~140 Hz HP */
        y = dsm_soft(0.5f * dsm_soft(m->k_g * (x - m->lp_d[c])) +
                     fb * m->lp_a[c]);
        dsm_write(md, mw, y);
        y = dsm_dc(m, c, y);
        break;
    }
    case 4u: {                                    /* fld */
        /*
         * Folders at 2x (midpoint, then the sample, averaged). With UND and
         * no DOOM: fold -> 8vize -> saturate entirely at 2x (the original
         * fld). Otherwise the folded signal runs the chain at the base
         * rate, the DOOM stage being the dom model's suboctave chorus.
         */
        const float mid = 0.5f * (x + m->last[c]);
        float o = 0.0f;
        uint8_t k;

        m->last[c] = x;
        if (m->k_chain == 0u && m->k_doom == 0.0f) {
            for (k = 0u; k < 2u; k++) {
                float f = dsm_psin(0.35f * m->k_g * (k ? x : mid));

                f += m->k_x * (2.0f * fabsf(f) - 0.64f - f); /* octave up */
                o += dsm_soft(m->k_q * f);
            }
            y = dsm_dc(m, c, 0.6f * o);
            break;
        }
        for (k = 0u; k < 2u; k++)
            o += dsm_psin(0.35f * m->k_g * (k ? x : mid));
        y = 0.5f * o;                                    /* folded */
        if (m->k_chain == 1u) {                          /* X */
            y = 1.2f * dsm_soft(m->k_q * y);
            y += m->k_x * (2.0f * fabsf(y) - 0.64f - y);
        }
        if (m->k_doom > 0.0f) {
            /* DOOM: suboctave chorus, louder and more out of tune. */
            const float dm = m->k_doom;
            const float det = (0.002f + 0.035f * dm) *
                              (1.0f + 0.5f * dsm_psin(m->acc[c]));
            const float lvl = ((dm < 0.75f) ? dm * 1.3333f : 1.0f) * 1.6f;
            const float bz = (dm > 0.75f) ? (dm - 0.75f) * 4.0f : 0.0f;
            const float w1 = c ? 900.0f : 860.0f;
            const float w2 = w1 * 0.93f;
            float sub;

            m->acc[c] += c ? 0.0000771f : 0.0000680f;
            m->acc[c] -= (m->acc[c] >= 1.0f) ? 1.0f : 0.0f;
            dsm_write(md, mw, y);
            sub = 0.5f * (dsm_shiftI(md, mw, &m->ph[c],
                                     (0.5f * (1.0f + det) - 1.0f) / w1, w1) +
                          dsm_shiftI(md, mw, &m->ph2[c],
                                     (0.5f * (1.0f - det) - 1.0f) / w2, w2));
            {
                /* Gate: the buzz must not lift the noise between hits. */
                float g = (env - 0.01f) * 20.0f;

                g = (g < 0.0f) ? 0.0f : ((g > 1.0f) ? 1.0f : g);
                y = 0.8f * y + g * (lvl * sub +
                                    bz * 0.6f * dsm_clamp(12.0f * sub, 0.8f));
            }
        }
        if (m->k_chain != 1u) {                          /* UND, OVR */
            y += m->k_x * (2.0f * fabsf(y) - 0.64f - y);
            y = 1.2f * dsm_soft(m->k_q * y);
        }
        if (m->k_chain == 2u) {                          /* OVR: fold again */
            const float m2 = 0.5f * (y + m->hold_in[c]);

            m->hold_in[c] = y;
            y = 0.5f * (dsm_psin(0.35f * m->k_g * m2) +
                        dsm_psin(0.35f * m->k_g * y));
        }
        {
            /* Level matched across the orders (bench, mid settings). */
            static const float lv[3] = { 0.84f, 1.08f, 0.78f };

            y = dsm_dc(m, c, lv[m->k_chain] * y);
        }
        break;
    }
    case 5u: {                                    /* assault */
        /*
         * Three bands distorted apart: tube-like asymmetric lows (even
         * harmonics), hard-clipped mids ringing in a comb tuned by
         * fb_tune (loop gain = fb), wavefolded highs.
         */
        const float z = dsm_read(md, DSM_MD_LEN, mw,
                                 c ? 1.007f * m->k_tap : m->k_tap);
        float lo;
        float mid;
        float hi;
        float bl;
        float bm;
        float cm;
        float bh;

        m->lp_a[c] += 0.035f * (x - m->lp_a[c]);         /* ~250 Hz */
        m->lp_b[c] += 0.30f * (x - m->lp_b[c]);          /* ~2.5 kHz */
        lo = m->lp_a[c];
        mid = m->lp_b[c] - lo;
        hi = x - m->lp_b[c];
        bl = dsm_soft(m->k_g * lo + 0.35f) - 0.2592593f;
        bm = dsm_clamp(2.0f * m->k_g * mid, 0.6f) * 1.6666667f;
        cm = dsm_soft(bm + m->k_fb * z);
        dsm_write(md, mw, cm);
        bh = dsm_psin((0.5f + 0.08f * m->k_g) * hi);
        y = dsm_dc(m, c, 0.6f * (0.9f * bl + cm + 0.5f * bh));
        break;
    }
    case 6u: {                                    /* bloodshed */
        const float win = c ? 700.0f : 600.0f;    /* grains, per channel */
        const float g0 = dsm_soft(m->k_x * x) * m->k_q;   /* grit */
        const float in = g0 + m->k_fb * (0.55f + 0.45f * env) *
                              dsm_soft(m->last[c]);
        float p = m->ph[c];
        float p2;
        float w1;

        dsm_write(md, mw, in);
        p2 = p + 0.5f;
        p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
        w1 = 1.0f - fabsf(2.0f * p - 1.0f);
        y = w1 * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p * win) +
            (1.0f - w1) * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p2 * win);
        /* +1 semitone: the taps close in at (2^(1/12) - 1) per sample. */
        p -= 0.0594631f / win;
        p += (p < 0.0f) ? 1.0f : 0.0f;
        m->ph[c] = p;
        m->lp_a[c] += 0.002f * (y - m->lp_a[c]);
        m->last[c] = y - m->lp_a[c];
        y = dsm_dc(m, c, dsm_soft(m->k_g * y));
        break;
    }
    case 8u: {                                    /* mbs */
        float lo;
        float mid;
        float hi;

        m->lp_a[c] += m->k_f * (x - m->lp_a[c]);
        m->lp_b[c] += m->k_rate * (x - m->lp_b[c]);
        lo = m->lp_a[c];
        mid = m->lp_b[c] - lo;
        hi = x - m->lp_b[c];
        y = 0.8f * dsm_soft(m->k_g * m->k_w[0] * lo) +
            dsm_soft(m->k_g * m->k_w[1] * mid) +
            0.8f * dsm_soft(m->k_g * m->k_w[2] * hi);
        if (m->k_notch) {
            /* ZDF state-variable notch, Q 2: notch = input - k * band. */
            const float v3 = y - m->hold_out[c];
            const float v1 = m->k_n[0] * m->hold_in[c] + m->k_n[1] * v3;
            const float v2 = m->hold_out[c] + m->k_n[1] * m->hold_in[c] +
                             m->k_n[2] * v3;

            m->hold_in[c] = 2.0f * v1 - m->hold_in[c];
            m->hold_out[c] = 2.0f * v2 - m->hold_out[c];
            y -= 0.5f * v1;
        }
        y = dsm_dc(m, c, 0.6f * y);
        break;
    }
    case 16u: {                                   /* smo */
        /*
         * Smoosh: up to ~126 dB of gain into a hard clip. With a trigger
         * track, each hit of that voice opens it fully and it decays; on
         * "fixed" the smoosh knob sets a constant amount. Between hits the
         * signal keeps a light saturation. Phase: four first-order
         * allpasses with opposite coefficients on L and R, mixed with the
         * direct signal (stereo width, phasing).
         */
        const float gate_in = env;               /* input dynamics 0..1 */
        float base;
        float s;

        env = m->fbs[c];
        if (m->k_x == 0.0f)
            env = m->k_q;
        else
            env = m->k_hold ? 1.0f : env * m->k_rate;
        m->fbs[c] = env;
        base = 1.2f * dsm_soft(1.5f * x);
        s = dsm_clamp(exp2f(21.0f * env) * x, 0.7f) * 1.4286f;
        {
            /* Gate on the input dynamics: ~100 dB of gain must not turn
             * the noise floor between hits into full-scale hiss. */
            float gate = (gate_in - 0.02f) * 10.0f;

            gate = (gate < 0.0f) ? 0.0f : ((gate > 1.0f) ? 1.0f : gate);
            y = base + env * gate * (s - base);
        }
        if (m->k_f > 0.0f) {
            const float ac = c ? -m->k_step : m->k_step;
            float z = y;
            uint8_t k;

            for (k = 0u; k < 4u; k++) {
                const float o = -ac * z + m->ap_s[c][k];

                m->ap_s[c][k] = z + ac * o;
                z = o;
            }
            y += m->k_f * (z - y);
        }
        y = dsm_dc(m, c, 0.8f * y);
        break;
    }
    case 7u: {                                    /* bloodbath */
        const uint32_t mud = (m->alg == 8u);

        if (mud)
            m->lp_d[c] += 0.1f * (x - m->lp_d[c]);       /* ~700 Hz: mud */
        /* The pseudo-reverb runs every other sample (held in between). */
        m->ph[c] = (m->ph[c] > 0.5f) ? 0.0f : 1.0f;
        if (m->ph[c] > 0.5f) {
            int16_t *rv = &pool[DSM_RV_OFF(c)];
            const uint16_t rw = m->rv_w[c];
            const float z = dsm_readI(rv, DSM_RV_LEN, rw, DSM_RV_LEN - 1u);
            float in;
            float ap;

            if (++m->cnt[c] >= m->k_hold) {
                m->cnt[c] = 0u;
                m->hold_in[c] = dsm_crush(m, mud ? 2.0f * m->lp_d[c] : x);
                m->hold_out[c] = dsm_crush(m, z);
                m->fbs[c] = mud
                    ? dsm_psin(0.25f * m->k_x * z) / (1.5708f * m->k_x)
                    : m->hold_out[c];
            }
            m->lp_d[c] += mud ? 0.0f : 0.06f * (x - m->lp_d[c]);
            if (!mud)                     /* lows thinned: a bright splash */
                in = dsm_soft(6.0f * (x - 0.8f * m->lp_d[c]) + fb * m->fbs[c]);
            else
                in = m->hold_in[c] + fb * m->fbs[c];
            ap = dsm_apChain(m, pool, c, in, mud ? 0.5f : 0.72f);
            dsm_write(rv, rw, ap);
            m->rv_w[c] = dsm_next(rw, DSM_RV_LEN);
            y = mud ? dsm_soft(1.5f * (m->hold_out[c] + m->hold_in[c]) +
                               0.3f * ap)
                    : dsm_soft(1.5f * (0.5f * z + ap));
            m->last[c] = dsm_dc(m, c, y);
        }
        y = m->last[c];
        break;
    }
    case 10u: case 11u: case 12u: case 13u: case 14u: case 15u: {
        /* The bloodshed loop with another interval (see dsm_block). */
        const float win = m->k_winc[c];
        float in;

        in = dsm_soft(m->k_x * x) * m->k_q +
             fb * dsm_soft(m->last[c]);
        dsm_write(md, mw, in);
        y = dsm_shift(md, mw, &m->ph[c], m->k_inc[c], win);
        if (m->alg == 13u)
            y = 0.5f * (y + dsm_shiftI(md, mw, &m->ph2[c], m->k_inc2[c],
                                       win * 0.93f));
        m->lp_a[c] += 0.002f * (y - m->lp_a[c]);
        m->last[c] = y - m->lp_a[c];
        y = dsm_dc(m, c, dsm_soft(m->k_g * y));
        break;
    }
    case 9u: {                                    /* saw */
        /*
         * High-pass (~150 Hz) so the clip bites the mids, then a hard
         * clip with a soft knee. Two Chamberlin band-passes on the clipped
         * signal, unity peak, centres ~100 Hz (Q 1.4) and ~1 kHz (Q 1.8),
         * scaled by low/high and added to a thin direct part. A ~6 kHz
         * low-pass takes the fizz off. No loop: nothing can drone.
         */
        float h;
        float k;
        float hp;
        float bl;
        float bh;

        m->lp_a[c] += 0.021f * (x - m->lp_a[c]);         /* ~150 Hz */
        h = m->k_g * (x - m->lp_a[c]);
        k = dsm_clamp(h, 1.2f);
        k = k - 0.13f * k * k * k;                       /* knee, |k| <= 1 */
        /* Low band: lp = lp_b, bp = lp_c, f = 2 sin(pi 100 / fs). */
        m->lp_b[c] += 0.01425f * m->lp_c[c];
        hp = k - m->lp_b[c] - 0.71f * m->lp_c[c];
        m->lp_c[c] += 0.01425f * hp;
        bl = 0.71f * m->lp_c[c];
        /* High band: lp = hold_in, bp = hold_out, f = 2 sin(pi 1000 / fs). */
        m->hold_in[c] += 0.1424f * m->hold_out[c];
        hp = k - m->hold_in[c] - 0.55f * m->hold_out[c];
        m->hold_out[c] += 0.1424f * hp;
        bh = 0.55f * m->hold_out[c];
        /* Direct part thins out as the boosts come up (0.85 -> 0.35). */
        y = (0.85f - 0.0625f * (m->k_x + m->k_q)) * k +
            m->k_x * bl + m->k_q * bh;
        m->lp_d[c] += 0.58f * (y - m->lp_d[c]);          /* ~6 kHz */
        /* Level follows the boosts so low/high do not just add volume. */
        y = dsm_dc(m, c, 0.45f * m->lp_d[c] /
                         (1.0f + 0.08f * (m->k_x + m->k_q)));
        break;
    }
    default:
        y = x;
        break;
    }
    m->md_w[c] = dsm_next(mw, DSM_MD_LEN);
    return y * dsm_trim[m->alg];
}
