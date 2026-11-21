/*
 * Core/DSP/Effects/Destroy/DestroyModels.c
 *
 * Normotone `dst` models 1..8 (fast-math, foreground render). Each one is a
 * small nonlinear feedback network; all loops pass a saturator, so they stay
 * bounded at any setting (some choke on purpose at high feedback).
 *
 *   1 hustle    feedback / fb_shift / disorder: overdrive blended into a
 *               rectifier, fed back through a micro delay (2..400 samples)
 *   2 turmoil   gain / feedback / fb_tune: three overdrive stages with a
 *               band boost and a low-pass between them, in a tuned loop
 *   3 mutiny    gain / feedback / fb_tune: one overdrive inside a tuned
 *               feedback comb
 *   4 riot      feedback / fb_tune / demur: overdrive, a gate and bit
 *               flipping on the 16-bit word, an allpass in the loop
 *   5 assault   gain / feedback / fb_tune: three parallel overdrives (soft,
 *               hard, high-passed), tuned loop
 *   6 bloodshed gain / pitchback / grit: a crude +1 semitone shifter whose
 *               overdriven output is fed back into it (rising spiral)
 *   7 bloodbath feedback / dirt / dirtrate: short overdriven pseudo-reverb
 *               (4 allpasses and a delay), bit-crushed and held feedback
 *   8 mudslide  feedback / dirt / dirtrate: heavily crushed pseudo-reverb
 *               with a wavefolded (extremely nonlinear) feedback
 *
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
static const float dsm_trim[DSM_ALG_COUNT] = {
    1.0f, 1.6f, 1.0f, 0.8f, 1.15f, 0.55f, 0.6f, 1.0f, 0.9f
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
    "cls", "hus", "tur", "mut", "rio", "ass", "bls", "blb", "mud"
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
        m->k_fb = 1.05f * a;
        m->k_tap = 2.0f * expf(b * 5.3f);         /* 2..400 samples */
        m->k_x = c;                               /* disorder */
        tune = -1.0f;
        break;
    case 2u: case 3u: case 5u:                    /* turmoil mutiny assault */
        m->k_g = 1.0f + 39.0f * a * a;
        m->k_fb = 0.97f * b;
        break;
    case 4u:                                      /* riot */
        m->k_fb = 0.97f * a;
        tune = b;
        m->k_x = c;                               /* demur */
        break;
    case 6u:                                      /* bloodshed */
        m->k_g = 1.0f + 15.0f * a;
        m->k_fb = 0.95f * b;                      /* pitchback */
        m->k_x = 1.0f + 15.0f * c;                /* grit */
        tune = -1.0f;
        break;
    case 7u: case 8u: {                           /* bloodbath mudslide */
        const float bits = 15.0f - 13.0f * b;     /* dirt: 15 .. 2 bits */

        m->k_fb = (alg == 7u) ? 0.9f * a : a;
        m->k_q = exp2f(bits - 1.0f);
        m->k_hold = (uint16_t)(1.0f + 47.0f * c * c);   /* dirtrate */
        tune = -1.0f;
        break;
    }
    default:
        break;
    }
    if (tune >= 0.0f) {
        const float hz = 30.0f * exp2f(tune * 6.0f);    /* 30 Hz .. 1.9 kHz */
        float tap = DSM_FS / hz;

        m->k_tap = (tap > (float)(DSM_MD_LEN - 2u)) ? (float)(DSM_MD_LEN - 2u)
                                                    : tap;
    }
}

/* The four allpasses of the pseudo-reverb, channel c, in series. */
static float dsm_apChain(DsModelState *m, int16_t *pool, uint32_t c, float x)
{
    uint32_t off = DSM_AP_BASE + c * DSM_AP_SPAN;
    uint32_t k;

    for (k = 0u; k < DSM_AP_COUNT; k++) {
        int16_t *line = &pool[off];
        const uint32_t len = dsm_ap_len[k];
        const uint16_t w = m->ap_w[c][k];
        const float d = dsm_readI(line, len, w, len - 1u);
        const float v = x + 0.62f * d;

        dsm_write(line, w, v);
        m->ap_w[c][k] = dsm_next(w, len);
        x = d - 0.62f * v;
        off += len;
    }
    return x;
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
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
        float u = 3.0f * x + fb * z;

        u += m->k_x * (2.0f * fabsf(u) - 0.6f - u);    /* rectifier blend */
        y = dsm_dc(m, c, dsm_soft(2.5f * u));
        dsm_write(md, mw, y);
        break;
    }
    case 2u: {                                    /* turmoil */
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
        float s = dsm_soft(m->k_g * (x + fb * z));

        m->lp_a[c] += 0.35f * (s - m->lp_a[c]);        /* ~2.9 kHz */
        m->lp_b[c] += 0.043f * (s - m->lp_b[c]);       /* ~300 Hz  */
        s = dsm_soft(3.0f * (s + 0.8f * (m->lp_a[c] - m->lp_b[c])));
        m->lp_c[c] += 0.45f * (s - m->lp_c[c]);        /* ~4 kHz   */
        y = dsm_dc(m, c, dsm_soft(2.0f * m->lp_c[c]));
        dsm_write(md, mw, y);
        break;
    }
    case 3u: {                                    /* mutiny */
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);

        y = dsm_dc(m, c, dsm_soft(m->k_g * (x + fb * z)));
        dsm_write(md, mw, y);
        break;
    }
    case 4u: {                                    /* riot */
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
        const float s = dsm_soft(3.0f * x + fb * z);
        int32_t i = (int32_t)(s * 32767.0f);
        float g;

        if (m->k_x > 0.0f) {
            const int32_t mask = (int32_t)(m->k_x * m->k_x * 16383.0f) & 0x3FF0;

            if (fabsf(s) < 0.15f * m->k_x)
                i = 0;                                   /* gate */
            i ^= mask;                                   /* bit flipping */
        }
        y = (float)i * (1.0f / 32767.0f);
        /* Allpass (first line of the pool's chain) inside the loop. */
        {
            int16_t *line = &pool[DSM_AP_BASE + c * DSM_AP_SPAN];
            const uint16_t w = m->ap_w[c][0];
            const float d = dsm_readI(line, dsm_ap_len[0], w,
                                      dsm_ap_len[0] - 1u);
            const float v = y + 0.6f * d;

            dsm_write(line, w, v);
            m->ap_w[c][0] = dsm_next(w, dsm_ap_len[0]);
            g = d - 0.6f * v;
        }
        dsm_write(md, mw, dsm_dc(m, c, g));
        break;
    }
    case 5u: {                                    /* assault */
        const float z = dsm_read(md, DSM_MD_LEN, mw, m->k_tap);
        const float u = x + fb * z;
        float s;

        m->lp_a[c] += 0.043f * (u - m->lp_a[c]);
        s = dsm_soft(m->k_g * u) +
            dsm_clamp(1.7f * m->k_g * u, 0.7f) * (1.0f / 0.7f) +
            dsm_soft(3.0f * m->k_g * (u - m->lp_a[c]));
        y = dsm_dc(m, c, s * (1.0f / 3.0f));
        dsm_write(md, mw, y);
        break;
    }
    case 6u: {                                    /* bloodshed */
        const float in = x + fb * dsm_soft(m->k_x * m->last[c]);
        float p = m->ph[c];
        float p2;
        float w1;

        dsm_write(md, mw, in);
        p2 = p + 0.5f;
        p2 -= (p2 >= 1.0f) ? 1.0f : 0.0f;
        w1 = 1.0f - fabsf(2.0f * p - 1.0f);
        y = w1 * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p * 1000.0f) +
            (1.0f - w1) * dsm_read(md, DSM_MD_LEN, mw, 8.0f + p2 * 1000.0f);
        /* +1 semitone: the taps close in at (2^(1/12) - 1) per sample. */
        p -= 0.0594631f / 1000.0f;
        p += (p < 0.0f) ? 1.0f : 0.0f;
        m->ph[c] = p;
        /* The loop sees y without its DC, or it locks on a constant. */
        m->lp_a[c] += 0.002f * (y - m->lp_a[c]);
        m->last[c] = y - m->lp_a[c];
        y = dsm_dc(m, c, dsm_soft(m->k_g * y));
        break;
    }
    case 7u: case 8u: {                           /* bloodbath mudslide */
        /*
         * The pseudo-reverb runs every other sample (the output is held in
         * between): it halves the cost and fits the crushed sound.
         */
        m->ph[c] = (m->ph[c] > 0.5f) ? 0.0f : 1.0f;
        if (m->ph[c] > 0.5f) {
            int16_t *rv = &pool[DSM_RV_OFF(c)];
            const uint16_t rw = m->rv_w[c];
            const float z = dsm_readI(rv, DSM_RV_LEN, rw, DSM_RV_LEN - 1u);
            float in;
            float ap;

            if (++m->cnt[c] >= m->k_hold) {
                m->cnt[c] = 0u;
                m->hold_in[c] = dsm_crush(m, x);
                m->hold_out[c] = dsm_crush(m, z);
                m->fbs[c] = (m->alg == 7u)
                    ? m->hold_out[c]
                    : dsm_psin(z * (2.0f + 6.0f * m->k_fb));   /* folded */
            }
            if (m->alg == 7u)
                in = dsm_soft(4.0f * (x + fb * m->fbs[c]));
            else
                in = m->hold_in[c] + fb * m->fbs[c];
            ap = dsm_apChain(m, pool, c, in);
            dsm_write(rv, rw, ap);
            m->rv_w[c] = dsm_next(rw, DSM_RV_LEN);
            y = (m->alg == 7u) ? 0.7f * dsm_soft(2.0f * (z + ap))
                               : m->hold_out[c] + 0.3f * ap;
            m->last[c] = dsm_dc(m, c, y);
        }
        y = m->last[c];
        break;
    }
    default:
        y = x;
        break;
    }
    m->md_w[c] = dsm_next(mw, DSM_MD_LEN);
    return y * dsm_trim[m->alg];
}
