/*
 * Core/Sequencer/TrackGen.c
 *
 * Runtime of the per-track generators (see TrackGen.h). No allocation, no
 * floating point: one modulo for the Euclidean hit, three shifts and one
 * compare for the Turing step. Called from TIM3 once per track step.
 */
#include "TrackGen.h"

static uint8_t tgen_euc_pos[NUM_TRACKS];
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
                  uint16_t rnd)
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
