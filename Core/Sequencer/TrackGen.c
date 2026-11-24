/*
 * Core/Sequencer/TrackGen.c
 *
 * Runtime of the per-track generators (see TrackGen.h). No allocation, no
 * floating point: one modulo for the Euclidean hit, three shifts and one
 * compare for the Turing step. Called from TIM3 once per track step.
 */
#include "TrackGen.h"

static uint8_t tgen_euc_pos[NUM_TRACKS];
static pat_track_gen_t tgen_glob[NUM_TRACKS];
static uint8_t tgen_globPrime;
static volatile uint8_t tgen_globOn;

uint8_t tgen_globalOn(void)
{
    return tgen_globOn;
}

void tgen_globalLatch(uint8_t on, const pat_track_gen_t *recs, uint8_t prime)
{
    uint8_t t;

    if (on && recs) {
        tgen_globOn = 0u;                 /* the ISR keeps the Pattern's */
        for (t = 0u; t < NUM_TRACKS; t++)
            tgen_glob[t] = recs[t];
        tgen_globPrime = (prime < PAT_GEN_NUM_PRIMES) ? prime : 0u;
    }
    tgen_globOn = (uint8_t)(on && recs);
}

pat_track_gen_t *tgen_globalRec(uint8_t track)
{
    return &tgen_glob[(track < NUM_TRACKS) ? track : 0u];
}

uint8_t tgen_globalPrime(void)
{
    return tgen_globPrime;
}

void tgen_setGlobalPrime(uint8_t prime)
{
    tgen_globPrime = (prime < PAT_GEN_NUM_PRIMES) ? prime : 0u;
}

/*
 * The 32 prime rhythms, read cell by cell from the manual's table (bit 15
 * = step 1): bank X 0..15, bank Y 16..31, "four on the floor" first. All
 * 32 satisfy the manual's eight selection rules, and its two masked
 * tables equal these ANDed with 0x0F0F and 0xF003.
 */
static const uint16_t tgen_primes[PAT_GEN_NUM_PRIMES] = {
    0x8888u, 0x888Au, 0x8892u, 0x8894u, 0x88A2u, 0x88A4u, 0x8912u, 0x8914u,
    0x8922u, 0x8924u, 0x8A8Au, 0x8AAAu, 0x9292u, 0x92AAu, 0x94AAu, 0x952Au,
    0x8282u, 0x828Au, 0x8292u, 0x82A2u, 0x8484u, 0x848Au, 0x8492u, 0x8494u,
    0x84A2u, 0x84A4u, 0x850Au, 0x8512u, 0x8514u, 0x8522u, 0x8524u, 0x8544u
};
/* Factor knob positions, as printed on the module's panel. */
static const uint8_t tgen_factors[16] = {
    1u, 2u, 4u, 8u, 16u, 3u, 6u, 9u, 12u, 5u, 10u, 15u, 7u, 14u, 11u, 13u
};
static const uint16_t tgen_masks[3] = { 0xFFFFu, 0x0F0Fu, 0xF003u };

uint8_t tgen_numFactor(uint8_t index)
{
    return tgen_factors[index & 15u];
}

uint16_t tgen_numPattern(const pat_track_gen_t *g, uint8_t prime)
{
    uint32_t r;
    uint8_t msk;

    if (!g)
        return 0u;
    msk = g->v[PAT_GEN_F_NUM_MSK];
    r = (uint32_t)(tgen_primes[prime % PAT_GEN_NUM_PRIMES] &
                   tgen_masks[(msk > 2u) ? 0u : msk]) *
        tgen_factors[g->v[PAT_GEN_F_NUM_FCT] & 15u];
    /* The measure is a circle: what leaves bit 15 comes back at bit 0. */
    while (r >> 16)
        r = (r & 0xFFFFu) + (r >> 16);
    return (uint16_t)r;
}
static uint16_t tgen_reg[NUM_TRACKS];

void tgen_init(void)
{
    uint8_t t;

    /* Distinct non-zero seeds; the register mutates from there. */
    for (t = 0u; t < NUM_TRACKS; t++) {
        tgen_euc_pos[t] = 0u;
        tgen_reg[t] = (uint16_t)(0x9E37u * (uint16_t)(t + 1u) ^ 0x5A5Au);
    }
}

void tgen_resetTrack(uint8_t track)
{
    if (track < NUM_TRACKS)
        tgen_euc_pos[track] = 0u;
}

uint8_t tgen_eucSteps(const pat_track_gen_t *g)
{
    uint8_t s = g->v[PAT_GEN_F_EUC_STEPS];

    return (s == 0u) ? 16u : ((s > 64u) ? 64u : s);
}

uint8_t tgen_tmLen(const pat_track_gen_t *g)
{
    uint8_t l = g->v[PAT_GEN_F_TM_LEN];

    return (l < 2u || l > 16u) ? 8u : l;
}

/*
 * Euclidean hit at position pos (Bresenham / modulo form): a hit when
 * ((pos - rot) * hits) mod steps < hits. Same rhythms as Bjorklund up to
 * a rotation; E(3,8) = x..x..x. with rot 0.
 */
uint8_t tgen_eucHit(const pat_track_gen_t *g, uint8_t pos)
{
    const uint8_t s = tgen_eucSteps(g);
    uint8_t k = g->v[PAT_GEN_F_EUC_HITS];
    uint16_t j;

    if (k > s)
        k = s;
    if (k == 0u)
        return 0u;
    /* Rotation moves the rhythm later: rot 1 puts the first hit on 1. */
    j = (uint16_t)((uint16_t)(pos % s) + s - (g->v[PAT_GEN_F_EUC_ROT] % s)) % s;
    return (uint8_t)(((j * k) % s) < k);
}

uint8_t tgen_eucPos(uint8_t track)
{
    return (track < NUM_TRACKS) ? tgen_euc_pos[track] : 0u;
}

uint8_t tgen_step(uint8_t track, const pat_track_gen_t *g, uint8_t pat_on,
                  uint16_t rnd, uint8_t prime)
{
    uint16_t r;
    uint8_t out;
    uint8_t gate = pat_on;
    uint8_t mode;

    if (track >= NUM_TRACKS || !g)
        return pat_on;
    mode = g->v[PAT_GEN_F_MODE];

    /* Turing: the bit at Length re-enters, flipped with chance prb/128. */
    r = tgen_reg[track];
    out = (uint8_t)((r >> (tgen_tmLen(g) - 1u)) & 1u);
    if (g->v[PAT_GEN_F_TM_PRB] >= 127u ||
        (uint8_t)(rnd & 0x7Fu) < g->v[PAT_GEN_F_TM_PRB])
        out ^= 1u;                          /* 127: always (2 x Length) */
    r = (uint16_t)((r << 1) | out);
    tgen_reg[track] = r;

    if (mode == PAT_GEN_EUC) {
        const uint8_t s = tgen_eucSteps(g);
        uint8_t p = tgen_euc_pos[track];

        if (p >= s)
            p = 0u;
        gate = tgen_eucHit(g, p);
        p++;
        tgen_euc_pos[track] = (uint8_t)((p >= s) ? 0u : p);
    } else if (mode == PAT_GEN_NUM) {
        /* 16-step measure counted from the track's start (as a reset). */
        uint8_t p = tgen_euc_pos[track];

        p = (uint8_t)(p & 15u);
        gate = (uint8_t)((tgen_numPattern(g, prime) >> (15u - p)) & 1u);
        tgen_euc_pos[track] = (uint8_t)((p + 1u) & 15u);
    } else if (mode == PAT_GEN_TM) {
        const uint8_t gat = g->v[PAT_GEN_F_TM_GAT];

        gate = gat ? (uint8_t)((uint8_t)r > (uint8_t)(255u - 2u * gat))
                   : (uint8_t)(r & 1u);
    }

    return gate;
}

uint8_t tgen_velocity(uint8_t track, const pat_track_gen_t *g, uint8_t vel)
{
    int16_t v;

    if (track >= NUM_TRACKS || !g || g->v[PAT_GEN_F_VDP] == 0u)
        return vel;
    v = (int16_t)vel + (int16_t)(((int16_t)(tgen_reg[track] & 0xFFu) - 128) *
                                 (int16_t)g->v[PAT_GEN_F_VDP] / 127);
    return (uint8_t)((v < 1) ? 1 : ((v > 127) ? 127 : v));
}

uint16_t tgen_register(uint8_t track)
{
    return (track < NUM_TRACKS) ? tgen_reg[track] : 0u;
}

void tgen_flipBit(uint8_t track, uint8_t bit)
{
    if (track < NUM_TRACKS && bit < 16u)
        tgen_reg[track] ^= (uint16_t)(1u << bit);
}
