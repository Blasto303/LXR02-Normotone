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
 *   2 turmoil   gain / feedback / fb_tune: dark input, three overdrive
 *               stages with a resonant band-pass (a vowel) between them
 *   3 mutiny    gain / feedback / fb_tune: overdrive into a near-linear
 *               tuned comb that rings at the fb_tune pitch (shrill)
 *   4 riot      feedback / fb_tune / demur: a decimator tuned 100 Hz..11 kHz
 *               (pitched aliasing, slower when loud), bit reduction 12..2
 *               bits (demur, plus a fixed bit-flip at the top), and the word
 *               XORed with its own past (feedback = how many top bits)
 *   5 assault   gain / feedback / fb_tune: three bands distorted apart,
 *               tube-like asymmetric lows, hard-clipped mids ringing in a
 *               comb tuned by fb_tune, wavefolded highs
 *   6 bloodshed gain / pitchback / grit: a granular +1 semitone shifter whose
 *               overdriven output is fed back into it (rising spiral)
 *   7 bloodbath feedback / dirt / dirtrate: short metallic pseudo-reverb,
 *               driven hard, with a bit-crushed held feedback
 *   8 mudslide  feedback / dirt / dirtrate: dark, heavily crushed and
 *               decimated pseudo-reverb with a wavefolded feedback
 *  10..16      the bloodshed loop with other intervals: shm +7 (fifth),
 *               dwn -12 (octave down), fal -1, spl +1 and -1 together,
 *               qtr +0.5, itv any interval -12..+12 (pot 1), tmp +1 with
 *               the cascade restarted every tempo division (pot 1: 1/32,
 *               1/16, 1/8, 1/4; free-running at the sequencer tempo)
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
#include "sequencer.h"
#include <math.h>

#define DSM_FS        44108.07f
#define DSM_SCALE     8192.0f
#define DSM_INV       (1.0f / 8192.0f)

static const uint16_t dsm_ap_len[DSM_AP_COUNT] = { 142u, 107u, 379u, 277u };

/* Output trims so the models sit near the dry level (bench, mix 127). */
static const float dsm_trim[DSM_ALG_COUNT] = {
    1.0f, 3.2f, 1.1f, 3.2f, 1.0f, 1.8f, 0.9f, 1.5f, 1.2f, 1.0f,
    0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f
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
    "cls", "hus", "tur", "mut", "rio", "ass", "bls", "blb", "mud", "saw",
    "shm", "dwn", "fal", "spl", "qtr", "itv", "tmp"
};
static const char dsm_knobs[DSM_ALG_COUNT][10] = {
    "drifldsub",   /* cls: drive fold sub     */
    "fbkfshdis",   /* hustle                  */
    "gaifbkftn",   /* turmoil                 */
    "gaifbkftn",   /* mutiny                  */
    "fbkftndmr",   /* riot                    */
    "gaifbkftn",   /* assault                 */
    "gaipbkgrt",   /* bloodshed               */
    "fbkdrtdrr",   /* bloodbath               */
    "fbkdrtdrr",   /* mudslide                */
    "dislowhig",   /* saw: dist, low, high    */
    "gaipbkgrt",   /* shm +7                  */
    "gaipbkgrt",   /* dwn -12                 */
    "gaipbkgrt",   /* fal -1                  */
    "gaipbkgrt",   /* spl +1 / -1             */
    "gaipbkgrt",   /* qtr +0.5                */
    "itvpbkgrt",   /* itv: interval           */
    "divpbkgrt",   /* tmp: tempo division     */
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
    case 2u: {                                    /* turmoil */
        const float hz = 80.0f * exp2f(c * 5.0f);         /* 80 Hz..2.5 kHz */

        m->k_g = 1.0f + 15.0f * a * a;
        m->k_fb = 0.97f * b;
        m->k_f = 2.0f * sinf(3.14159265f * hz / DSM_FS);
        break;
    }
    case 3u:                                      /* mutiny */
        m->k_g = 1.0f + 30.0f * a * a;
        m->k_fb = 0.985f * b;
        break;
    case 5u:                                      /* assault */
        m->k_g = 1.0f + 24.0f * a * a;
        m->k_fb = 0.96f * b;
        break;
    case 4u: {                                    /* riot (block) */
        const float hz = 100.0f * exp2f(b * 6.8f);        /* 100 Hz..11 kHz */
        const uint8_t nb = (uint8_t)(a * 9.0f + 0.5f);    /* XOR top bits */
        const float bits = 12.0f - 10.0f * c;             /* 12..2 bits */

        m->k_rate = hz / DSM_FS;
        m->k_tap = DSM_FS / hz;
        if (m->k_tap > (float)(DSM_MD_LEN - 2u))
            m->k_tap = (float)(DSM_MD_LEN - 2u);
        if (m->k_tap < 1.0f)
            m->k_tap = 1.0f;
        m->k_xmask = (uint16_t)(((1u << nb) - 1u) << (15u - nb));
        m->k_q = exp2f(bits - 1.0f);
        m->k_mask = (c > 0.6f)
            ? (uint16_t)((uint32_t)((c - 0.6f) * 2.5f * 1023.0f) << 4) : 0u;
        tune = -1.0f;
        break;
    }
    case 6u:                                      /* bloodshed */
        m->k_g = 1.0f + 15.0f * a;
        m->k_fb = 0.98f * b;                      /* pitchback */
        m->k_x = 1.0f + 15.0f * c;                /* grit */
        m->k_q = (1.0f + m->k_x) / m->k_x;        /* 1 / soft(grit) */
        tune = -1.0f;
        break;
    case 7u: case 8u: {                           /* bloodbath mudslide */
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
    case 10u: case 11u: case 12u: case 13u: case 14u: case 15u: case 16u:
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
        } else if (alg == 15u) {                  /* itv: -12..+12 */
            const int8_t n = (int8_t)((int16_t)(a * 24.0f + 0.5f) - 12);

            m->k_step = exp2f((float)n * (1.0f / 12.0f)) - 1.0f;
            if (n < -6)
                m->k_win = 860.0f;
        } else {                                  /* tmp: +1, per division */
            static const float beats[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
            const uint16_t bpm = seq_getBpm();
            float len = DSM_FS * 60.0f / (float)((bpm < 30u) ? 30u : bpm) *
                        beats[(uint8_t)(a * 3.99f)];

            m->k_step = 0.0594631f;
            m->k_tlen = (uint16_t)((len > 65000.0f) ? 65000.0f : len);
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
    case 2u: {                                    /* turmoil */
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
        float s;
        float hp;

        m->lp_a[c] += 0.17f * (x - m->lp_a[c]);          /* ~1.3 kHz */
        s = dsm_soft(m->k_g * m->lp_a[c] + fb * (1.0f / 16.0f) * z);
        /* Band-pass at fb_tune (Q ~3.3), gain 3 at its centre. */
        m->lp_b[c] += m->k_f * m->lp_c[c];
        hp = s - m->lp_b[c] - 0.3f * m->lp_c[c];
        m->lp_c[c] += m->k_f * hp;
        s = dsm_soft(2.0f * (s + 0.9f * m->lp_c[c]));
        m->lp_d[c] += 0.25f * (s - m->lp_d[c]);          /* ~2 kHz */
        y = dsm_dc(m, c, dsm_soft(2.0f * m->lp_d[c]));
        dsm_write(md, mw, y);
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
    case 4u: {                                    /* riot */
        /*
         * Decimator (sample and hold) tuned by fb_tune: pitched aliasing.
         * Loud passages slow it down (dirtier), quiet ones stay finer. On
         * each held sample: bit reduction (demur), the word XORed with its
         * own past one decimation period earlier (feedback = number of top
         * bits), and a fixed bit flip at the top of demur. A gate closes
         * the XOR on silence so nothing sustains on its own.
         */
        m->acc[c] += m->k_rate * (c ? 1.01f : 1.0f) * (1.15f - 0.6f * env);
        if (m->acc[c] >= 1.0f) {
            const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
            const float h = dsm_soft(3.0f * x);
            int32_t i;

            m->acc[c] -= 1.0f;
            m->acc[c] = (m->acc[c] >= 1.0f) ? 0.0f : m->acc[c];
            i = (int32_t)(dsm_crush(m, h) * 32767.0f);
            if (fabsf(h) > 0.004f) {
                i ^= ((int32_t)(z * 32767.0f) & (int32_t)m->k_xmask);
                i ^= (int32_t)m->k_mask;
            } else {
                i = 0;
            }
            i = (i > 32767) ? 32767 : ((i < -32768) ? -32768 : i);
            m->hold_out[c] = (float)i * (1.0f / 32767.0f);
        }
        y = m->hold_out[c];
        dsm_write(md, mw, y);
        m->lp_b[c] += 0.004f * (y - m->lp_b[c]);         /* DC-free */
        y -= m->lp_b[c];
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
    case 7u: case 8u: {                           /* bloodbath mudslide */
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
    case 10u: case 11u: case 12u: case 13u: case 14u: case 15u: case 16u: {
        /* The bloodshed loop with another interval (see dsm_block). */
        const float win = m->k_winc[c];
        float fbm = 1.0f;
        float in;

        if (m->alg == 16u) {
            /* tmp: the loop is cut for ~18 ms at each division, then
             * ramps back: the cascade climbs, restarts, climbs. */
            uint16_t k = m->cnt[c];

            k = (uint16_t)((k + 1u >= m->k_tlen) ? 0u : k + 1u);
            m->cnt[c] = k;
            fbm = (k < 800u) ? 0.0f
                : ((k < 1020u) ? (float)(k - 800u) * (1.0f / 220.0f) : 1.0f);
        }
        in = dsm_soft(m->k_x * x) * m->k_q +
             fb * fbm * dsm_soft(m->last[c]);
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
