/*
 * PLAITS mode kit conversion (contract in plaitsMode.h).
 *
 * Every Plaits row is filled in this order: the slot's base preset (the
 * shipped SD presets pk808, ps808, pmTom, phMetal, ph808 and a synthetic
 * kick), then the same storage key in the source instrument (amp attack,
 * voice mix, LFO, velocity), then the semantic rows below. Both endpoint
 * images (normal and Morph) are converted, so Morph keeps working.
 *
 * Sound length. An LXR amp envelope falls linearly by `decay` per control
 * block and is shaped by the slope curve y = (1+k)v / (1+k|v|); it is below
 * -40 dB after (1 - v40) / decay blocks, v40 = 0.01 / (1 + 0.99 k). Each
 * Plaits engine's -40 dB time against its Decay row was measured on the host
 * render (tools: 17 points, Decay 0, 8, ..., 120, 127). The conversion picks
 * the Decay whose time matches, and leaves the Plaits amp envelope open as
 * in the shipped presets, so the Plaits decay shapes the sound.
 */
#include "plaitsMode.h"

#include <string.h>

#include "InstrumentManager.h"
#include "SlopeEg2.h"
#include "menu.h"

uint8_t plaitsMode_mapActive;

/* Semantic conversion kinds. */
enum {
    PM_COPY = 0u,   /* same value                                   */
    PM_FOLD,        /* MIDI note folded by octaves into [arg, arg+23] */
    PM_AVG,         /* (base + value) / 2, base = arg                */
    PM_DECAY,       /* LXR amp decay -> Plaits Decay (time match)    */
    PM_DECAY_OPEN,  /* same, open hat table                          */
    PM_VOL,         /* volume x arg / 128 (engine level trim)        */
};

typedef struct {
    uint8_t dst;    /* destination descriptor index, 0xFF = none */
    uint8_t kind;
    uint8_t arg;
} pm_map_t;

static uint8_t pm_slope[INSTRUMENT_SLOT_COUNT];      /* own kit amp slopes */
static uint8_t pm_slopeOpen;                          /* slot 6 open decay  */

/* -40 dB times in seconds at Decay 0, 8, ..., 120, 127 (host render). */
static const float pm_t40_kick[17] = {
    0.093f, 0.112f, 0.131f, 0.152f, 0.191f, 0.241f, 0.303f, 0.395f, 0.517f,
    0.670f, 0.884f, 1.170f, 1.557f, 2.067f, 2.741f, 3.650f, 4.701f };
static const float pm_t40_kickSynth[17] = {
    0.114f, 0.115f, 0.116f, 0.117f, 0.134f, 0.152f, 0.171f, 0.207f, 0.261f,
    0.332f, 0.424f, 0.568f, 0.769f, 1.078f, 1.551f, 2.315f, 3.351f };
static const float pm_t40_snare[17] = {
    0.064f, 0.077f, 0.095f, 0.118f, 0.142f, 0.173f, 0.209f, 0.253f, 0.311f,
    0.389f, 0.486f, 0.637f, 0.882f, 1.295f, 2.020f, 3.357f, 5.541f };
static const float pm_t40_hat[17] = {
    0.037f, 0.048f, 0.064f, 0.084f, 0.113f, 0.150f, 0.203f, 0.274f, 0.369f,
    0.501f, 0.681f, 0.925f, 1.250f, 1.700f, 2.301f, 3.124f, 4.077f };
static const float pm_t40_hatRing[17] = {
    0.043f, 0.047f, 0.056f, 0.069f, 0.080f, 0.095f, 0.117f, 0.142f, 0.175f,
    0.219f, 0.274f, 0.346f, 0.430f, 0.556f, 0.706f, 0.904f, 1.124f };
static const float pm_t40_modal[17] = {
    0.078f, 0.122f, 0.186f, 0.294f, 0.430f, 0.668f, 1.004f, 1.569f, 2.443f,
    3.792f, 7.644f, 15.0f, 20.0f, 25.0f, 30.0f, 35.0f, 40.0f };

/* One base preset row. */
typedef struct {
    const char *key;
    uint8_t value;
} pm_row_t;

/*
 * Per destination slot: engine, base rows, semantic keys and note range.
 * NULL keys mean "no such row". note_lo: notes fold into [lo, lo + 23].
 */
typedef struct {
    instrument_type_t type;
    const pm_row_t *base;
    uint8_t base_count;
    const char *note;
    const char *fine;
    const char *tone;
    const char *decay;
    const char *extra;          /* Harmonics / Snappy / Structure */
    const char *extra_src;      /* source key that moves `extra`  */
    const char *decay_open;     /* open hat Decay (slot 6 only)   */
    const float *t40;
    uint8_t note_lo;
    uint8_t vol_trim;           /* x / 128, from host level comparisons */
} pm_slot_t;

#define PM_AMP_OPEN \
    { "amp_envelope_decay", 100u }, { "amp_envelope_slope", 127u }

static const pm_row_t pm_base0[] = {        /* pk808 */
    { "kick_note", 31u }, { "kick_harmonics", 20u }, { "kick_tone", 50u },
    { "kick_decay", 75u }, { "kick_synth", 0u }, PM_AMP_OPEN };
static const pm_row_t pm_base1[] = {        /* synthetic kick */
    { "kick_note", 33u }, { "kick_harmonics", 30u }, { "kick_tone", 50u },
    { "kick_decay", 60u }, { "kick_synth", 1u }, PM_AMP_OPEN };
static const pm_row_t pm_base2[] = {        /* pmTom */
    { "modal_note", 45u }, { "modal_structure", 20u }, { "modal_bright", 30u },
    { "modal_damping", 50u }, { "modal_position", 60u }, PM_AMP_OPEN };
static const pm_row_t pm_base3[] = {        /* ps808 */
    { "snare_note", 55u }, { "snare_snappy", 70u }, { "snare_tone", 40u },
    { "snare_decay", 45u }, { "snare_synth", 0u }, PM_AMP_OPEN };
static const pm_row_t pm_base4[] = {        /* phMetal, the cymbal */
    { "phat_note", 55u }, { "phat_ring", 1u }, { "phat_noise", 10u },
    { "phat_tone", 110u }, { "phat_decay", 60u }, { "phat_decay_choke", 85u },
    PM_AMP_OPEN };
static const pm_row_t pm_base5[] = {        /* ph808 */
    { "phat_note", 60u }, { "phat_ring", 0u }, { "phat_noise", 25u },
    { "phat_tone", 70u }, { "phat_decay", 30u }, { "phat_decay_choke", 80u },
    PM_AMP_OPEN };

#define PM_N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const pm_slot_t pm_slots[INSTRUMENT_SLOT_COUNT] = {
    { INSTRUMENT_TYPE_PBD, pm_base0, PM_N(pm_base0), "kick_note", "kick_fine",
      "kick_tone", "kick_decay", "kick_harmonics", "pitch_envelope_amount",
      0, pm_t40_kick, 24u, 128u },
    { INSTRUMENT_TYPE_PBD, pm_base1, PM_N(pm_base1), "kick_note", "kick_fine",
      "kick_tone", "kick_decay", "kick_harmonics", "pitch_envelope_amount",
      0, pm_t40_kickSynth, 24u, 128u },
    { INSTRUMENT_TYPE_PMD, pm_base2, PM_N(pm_base2), "modal_note", "modal_fine",
      "modal_bright", "modal_damping", "modal_structure", "osc2_mod_amount",
      0, pm_t40_modal, 36u, 170u },
    { INSTRUMENT_TYPE_PSN, pm_base3, PM_N(pm_base3), "snare_note", "snare_fine",
      "snare_tone", "snare_decay", "snare_snappy", "osc1_noise_mix",
      0, pm_t40_snare, 48u, 128u },
    { INSTRUMENT_TYPE_PHT, pm_base4, PM_N(pm_base4), "phat_note", "phat_fine",
      "phat_tone", "phat_decay", 0, 0, 0, pm_t40_hatRing, 48u, 64u },
    { INSTRUMENT_TYPE_PHT, pm_base5, PM_N(pm_base5), "phat_note", "phat_fine",
      "phat_tone", "phat_decay", 0, 0, "phat_decay_choke", pm_t40_hat, 52u, 84u },
};

/* Index of `key` in `type`, 0xFF when absent. */
static uint8_t pm_index(instrument_type_t type, const char *key)
{
    uint8_t index = 0xFFu;

    if (key)
        (void)instrumentManager_descriptorIndexByKey(type, key, &index);
    return index;
}

static uint8_t pm_baseValue(const pm_slot_t *s, const char *key, uint8_t *out)
{
    uint8_t i;

    for (i = 0u; i < s->base_count; i++) {
        if (strcmp(s->base[i].key, key) == 0) {
            *out = s->base[i].value;
            return 1u;
        }
    }
    return 0u;
}

/* -40 dB time of an LXR amp envelope (decay and slope rows), seconds. */
static float pm_lxrTime(uint8_t decay, uint8_t slope)
{
    const float c = slopeEg2_calcDecay(decay);
    const float amount = ((float)slope / 127.0f - 0.5f) * 1.999f;
    const float k = 2.0f * amount / (1.0f - amount);
    const float v40 = 0.01f / (1.0f + 0.99f * k);

    if (c <= 0.0f)
        return 1000.0f;
    return (1.0f - v40) / c * ((float)OUTPUT_DMA_SIZE / 44108.0f);
}

/* Plaits Decay row whose -40 dB time is `t`. */
static uint8_t pm_decayForTime(const float *t40, float t)
{
    uint8_t i;

    if (t <= t40[0])
        return 0u;
    for (i = 0u; i < 16u; i++) {
        if (t < t40[i + 1u]) {
            const float lo = (float)(i * 8u);
            const float hi = (i == 15u) ? 127.0f : (float)((i + 1u) * 8u);
            const float f = (t - t40[i]) / (t40[i + 1u] - t40[i]);

            return (uint8_t)(lo + (hi - lo) * f + 0.5f);
        }
    }
    return 127u;
}

static uint8_t pm_fold(uint8_t note, uint8_t lo)
{
    int16_t n = (int16_t)note;

    while (n < (int16_t)lo)
        n += 12;
    while (n > (int16_t)lo + 23)
        n -= 12;
    return (uint8_t)n;
}

/* Apply one semantic conversion to a raw source value. */
static uint8_t pm_convert(uint8_t slot, const pm_map_t *m, uint8_t value,
                          uint8_t slope)
{
    const pm_slot_t *s = &pm_slots[slot];

    switch (m->kind) {
    case PM_FOLD:
        return pm_fold(value, m->arg);
    case PM_AVG:
        return (uint8_t)(((uint16_t)m->arg + value + 1u) >> 1);
    case PM_DECAY:
        return pm_decayForTime(s->t40, pm_lxrTime(value, slope));
    case PM_DECAY_OPEN:
        return pm_decayForTime(s->t40, pm_lxrTime(value, slope));
    case PM_VOL: {
        const uint16_t v = (uint16_t)(((uint16_t)value * m->arg + 64u) >> 7);

        return (uint8_t)(v > 127u ? 127u : v); }
    default:
        return value;
    }
}

/*
 * Where a source row goes in the Plaits engine of `slot`. Semantic rows
 * first, then the same storage key; amp decay/slope never land on the open
 * Plaits amp envelope.
 */
static pm_map_t pm_mapRow(uint8_t slot, instrument_type_t src_type,
                          uint8_t src_local)
{
    const pm_slot_t *s = &pm_slots[slot];
    const ParamDescriptor *d = instrumentManager_descriptor(src_type, src_local);
    pm_map_t m = { 0xFFu, PM_COPY, 0u };
    const char *key;
    uint8_t base = 64u;

    if (!d || !d->file_key)
        return m;
    key = d->file_key;
    if (strcmp(key, "osc1_pitch_coarse") == 0) {
        m.dst = pm_index(s->type, s->note);
        m.kind = PM_FOLD;
        m.arg = s->note_lo;
    } else if (strcmp(key, "osc1_pitch_fine") == 0) {
        m.dst = pm_index(s->type, s->fine);
    } else if (strcmp(key, "filter_freq") == 0) {
        m.dst = pm_index(s->type, s->tone);
        m.kind = PM_AVG;
        (void)pm_baseValue(s, s->tone, &base);
        m.arg = base;
    } else if (strcmp(key, "amp_envelope_decay") == 0) {
        m.dst = pm_index(s->type, s->decay);
        m.kind = PM_DECAY;
    } else if (strcmp(key, "amp_envelope_decay_choke") == 0) {
        if (s->decay_open) {
            m.dst = pm_index(s->type, s->decay_open);
            m.kind = PM_DECAY_OPEN;
        }
    } else if (strcmp(key, "instrument_vol") == 0) {
        m.dst = pm_index(s->type, key);
        m.kind = PM_VOL;
        m.arg = s->vol_trim;
    } else if (strcmp(key, "amp_envelope_slope") == 0) {
        /* Folded into the Decay match. */
    } else if (s->extra_src && strcmp(key, s->extra_src) == 0) {
        m.dst = pm_index(s->type, s->extra);
        m.kind = PM_AVG;
        (void)pm_baseValue(s, s->extra, &base);
        m.arg = base;
    } else {
        m.dst = pm_index(s->type, key);
    }
    return m;
}

/*
 * Conversion tables, one per (destination slot, source engine type).
 *
 * The descriptor tables are matched by storage key with strcmp, far too slow
 * for a Scene change (the audio queue holds 2 x 2.2 ms). Each table is built
 * once, in the background after boot (plaitsMode_service), so a conversion
 * at a Scene change is a few table lookups per row.
 *
 * fwd[i]: where destination row i comes from: a source row, PM_FWD_BASE (the
 * base preset value in base[i]), PM_FWD_CENTRE (63) or PM_FWD_ZERO.
 * map[j]: where source row j goes (semantic or same key), for conversion,
 * LFO / velocity tokens and automation. PM_SEM in kind marks the semantic
 * rows, PM_NOAUTO a destination that automation may not write.
 */
#define PM_FWD_BASE    0xFEu
#define PM_FWD_CENTRE  0xFDu
#define PM_FWD_ZERO    0xFFu
#define PM_SEM         0x40u
#define PM_NOAUTO      0x80u
#define PM_KIND(k)     ((uint8_t)((k) & 0x0Fu))
#define PM_TYPE_COUNT  ((uint8_t)INSTRUMENT_TYPE_UNKNOWN)

typedef struct {
    uint8_t fwd[INSTRUMENT_PARAM_COUNT];
    pm_map_t map[INSTRUMENT_PARAM_COUNT];
    uint8_t slope_index;        /* source amp_envelope_slope row, 0xFF none */
    uint8_t transient_index;    /* source transient_vol row, 0xFF none      */
    uint8_t vol_index;          /* destination instrument_vol row           */
} pm_table_t;

static pm_table_t pm_tables[INSTRUMENT_SLOT_COUNT][PM_TYPE_COUNT];
static uint8_t pm_base[INSTRUMENT_SLOT_COUNT][INSTRUMENT_PARAM_COUNT];
static uint8_t pm_ready[INSTRUMENT_SLOT_COUNT];      /* bit per source type */
static uint8_t pm_baseReady;
static const pm_map_t *pm_active[INSTRUMENT_SLOT_COUNT];

static void pm_buildBase(void)
{
    uint8_t slot;
    uint8_t i;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        const pm_slot_t *s = &pm_slots[slot];

        memset(pm_base[slot], 0, INSTRUMENT_PARAM_COUNT);
        for (i = 0u; i < s->base_count; i++) {
            const uint8_t d = pm_index(s->type, s->base[i].key);

            if (d < INSTRUMENT_PARAM_COUNT)
                pm_base[slot][d] = s->base[i].value;
        }
    }
    pm_baseReady = 1u;
}

static void pm_buildTable(uint8_t slot, instrument_type_t src_type)
{
    const pm_slot_t *s = &pm_slots[slot];
    pm_table_t *t = &pm_tables[slot][src_type];
    const instrument_registry_entry_t *de = instrumentManager_registryEntry(s->type);
    const instrument_registry_entry_t *se = instrumentManager_registryEntry(src_type);
    uint8_t i;

    if (!pm_baseReady)
        pm_buildBase();
    memset(t->fwd, PM_FWD_ZERO, sizeof(t->fwd));
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        t->map[i].dst = 0xFFu;
        t->map[i].kind = PM_COPY;
        t->map[i].arg = 0u;
    }
    t->slope_index = pm_index(src_type, "amp_envelope_slope");
    t->transient_index = pm_index(src_type, "transient_vol");
    t->vol_index = pm_index(s->type, "instrument_vol");
    if (de) {
        for (i = 0u; i < de->descriptor_count; i++) {
            const char *key = de->descriptors[i].file_key;
            uint8_t v;
            uint8_t j;

            if (pm_baseValue(s, key, &v))
                t->fwd[i] = PM_FWD_BASE;
            else if ((j = pm_index(src_type, key)) < INSTRUMENT_PARAM_COUNT)
                t->fwd[i] = j;
            else if (de->descriptors[i].dtype == DTYPE_PM63)
                t->fwd[i] = PM_FWD_CENTRE;
        }
    }
    if (se) {
        for (i = 0u; i < se->descriptor_count; i++) {
            const char *key = se->descriptors[i].file_key;
            pm_map_t m = pm_mapRow(slot, src_type, i);

            if (!strcmp(key, "osc1_pitch_coarse") || !strcmp(key, "osc1_pitch_fine") ||
                !strcmp(key, "filter_freq") || !strcmp(key, "amp_envelope_decay") ||
                !strcmp(key, "amp_envelope_decay_choke") ||
                !strcmp(key, "instrument_vol") ||
                (s->extra_src && !strcmp(key, s->extra_src)))
                m.kind |= PM_SEM;
            if (m.dst < INSTRUMENT_PARAM_COUNT && de &&
                (m.dst >= de->descriptor_count ||
                 !(de->descriptors[m.dst].flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE)))
                m.kind |= PM_NOAUTO;
            t->map[i] = m;
        }
    }
    pm_ready[slot] |= (uint8_t)(1u << src_type);
}

static const pm_table_t *pm_table(uint8_t slot, instrument_type_t src_type)
{
    if (slot >= INSTRUMENT_SLOT_COUNT || (uint8_t)src_type >= PM_TYPE_COUNT)
        return 0;
    if (!(pm_ready[slot] & (1u << src_type)))
        pm_buildTable(slot, src_type);
    return &pm_tables[slot][src_type];
}

uint8_t plaitsMode_service(void)
{
    uint8_t slot;
    uint8_t type;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        for (type = 0u; type < PM_TYPE_COUNT; type++) {
            if (!(pm_ready[slot] & (1u << type))) {
                pm_buildTable(slot, (instrument_type_t)type);
                return 0u;
            }
        }
    }
    return 1u;
}

/* Remap a target token pointing into slot `voice` of `src` (0xFF = off). */
static uint8_t pm_token(const kit_t *src, uint8_t voice, uint8_t token)
{
    const pm_table_t *t;

    if (token == INSTRUMENT_TARGET_TOKEN_OFF ||
        token > INSTRUMENT_TARGET_TOKEN_MAX_LOCAL ||
        voice >= INSTRUMENT_SLOT_COUNT)
        return token;
    t = pm_table(voice, src->instruments[voice].type);
    return t ? t->map[token].dst : INSTRUMENT_TARGET_TOKEN_OFF;
}

static uint8_t pm_rowIndex(uint8_t slot, const char *key)
{
    /* Destination rows used at conversion time, resolved once. */
    static uint8_t cache[INSTRUMENT_SLOT_COUNT][6];
    static uint8_t ready;
    static const char *const keys[6] = {
        "lfo_target_voice", "lfo_target_param", "lfo_target_voice_2",
        "lfo_target_param_2", "velo_mod_dest", 0 };
    uint8_t k;

    if (!ready) {
        uint8_t sl;

        for (sl = 0u; sl < INSTRUMENT_SLOT_COUNT; sl++) {
            for (k = 0u; k < 5u; k++)
                cache[sl][k] = pm_index(pm_slots[sl].type, keys[k]);
            cache[sl][5] = pm_index(pm_slots[sl].type, pm_slots[sl].decay_open);
        }
        ready = 1u;
    }
    if (!key)
        return cache[slot][5];
    for (k = 0u; k < 5u; k++)
        if (keys[k] == key)
            return cache[slot][k];
    return 0xFFu;
}

/* Fill one endpoint image of slot `slot`. */
static void pm_image(const kit_t *src, uint8_t slot, uint8_t morph,
                     instrument_param_value_t *out)
{
    const pm_slot_t *s = &pm_slots[slot];
    const kit_instrument_slot_t *in = &src->instruments[slot];
    const pm_table_t *t = pm_table(slot, in->type);
    const instrument_param_value_t *img = morph
        ? in->parameter_images.morph_instrument_parameters
        : in->parameter_images.instrument_parameters;
    uint8_t slope = 64u;
    uint8_t i;

    if (!t)
        return;
    /* 1. Base preset, same key in the source, or a neutral value. */
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        const uint8_t f = t->fwd[i];

        out[i] = (f < INSTRUMENT_PARAM_COUNT) ? img[f]
               : (f == PM_FWD_BASE) ? pm_base[slot][i]
               : (f == PM_FWD_CENTRE) ? 63u : 0u;
    }
    if (t->slope_index < INSTRUMENT_PARAM_COUNT)
        slope = img[t->slope_index];
    /* 2. Semantic rows of the source. */
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        const pm_map_t *m = &t->map[i];

        if ((m->kind & PM_SEM) && m->dst < INSTRUMENT_PARAM_COUNT) {
            pm_map_t c = *m;

            c.kind = PM_KIND(m->kind);
            out[m->dst] = pm_convert(slot, &c, img[i], slope);
        }
    }
    /*
     * Plaits engines have no transient layer: an LXR voice carried by a loud
     * transient over a low volume would come out nearly silent. Its volume
     * is raised to half the transient level at least.
     */
    if (t->transient_index < INSTRUMENT_PARAM_COUNT &&
        t->vol_index < INSTRUMENT_PARAM_COUNT) {
        const uint8_t floor = (uint8_t)(((uint16_t)img[t->transient_index] *
                                         s->vol_trim) >> 8);

        if (out[t->vol_index] < floor)
            out[t->vol_index] = floor;
    }
    /* Open hat: a non-Choke source keeps its track-7 decay in the kit. */
    if (s->decay_open &&
        !(instrumentManager_typeFlags(in->type) & INSTRUMENT_FLAG_CHOKE)) {
        const uint8_t di = pm_rowIndex(slot, 0);
        const uint8_t dv = morph ? src->settings.slot6_track7_morph_amp_envelope_decay
                                 : src->settings.slot6_track7_amp_envelope_decay;

        if (di < INSTRUMENT_PARAM_COUNT)
            out[di] = pm_decayForTime(s->t40, pm_lxrTime(dv, slope));
    }
    /* 3. LFO and velocity destinations follow their parameter. */
    {
        const uint8_t v1 = pm_rowIndex(slot, "lfo_target_voice");
        const uint8_t p1 = pm_rowIndex(slot, "lfo_target_param");
        const uint8_t v2 = pm_rowIndex(slot, "lfo_target_voice_2");
        const uint8_t p2 = pm_rowIndex(slot, "lfo_target_param_2");
        const uint8_t vd = pm_rowIndex(slot, "velo_mod_dest");

        if (v1 < 64u && p1 < 64u && out[v1] >= INSTRUMENT_TARGET_VOICE_FIRST &&
            out[v1] <= INSTRUMENT_TARGET_VOICE_LAST)
            out[p1] = pm_token(src, (uint8_t)(out[v1] - 1u), out[p1]);
        if (v2 < 64u && p2 < 64u && out[v2] >= INSTRUMENT_TARGET_VOICE_FIRST &&
            out[v2] <= INSTRUMENT_TARGET_VOICE_LAST)
            out[p2] = pm_token(src, (uint8_t)(out[v2] - 1u), out[p2]);
        if (vd < 64u)
            out[vd] = pm_token(src, slot, out[vd]);
    }
}

void plaitsMode_convertKit(kit_t *dst, const kit_t *src)
{
    uint8_t slot;

    if (!dst || !src || dst == src)
        return;
    dst->settings = src->settings;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        kit_instrument_slot_t *o = &dst->instruments[slot];

        o->type = pm_slots[slot].type;
        if (src->instruments[slot].type == o->type) {
            /* Already this Plaits engine: keep it as it is. */
            *o = src->instruments[slot];
            continue;
        }
        pm_image(src, slot, 0u, o->parameter_images.instrument_parameters);
        pm_image(src, slot, 1u, o->parameter_images.morph_instrument_parameters);
        memcpy(o->parameter_images.morph_interpolation,
               o->parameter_images.instrument_parameters, INSTRUMENT_PARAM_COUNT);
    }
}

void plaitsMode_mapOn(const kit_t *own)
{
    uint8_t slot;

    plaitsMode_mapActive = 0u;
    if (!own)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        const kit_instrument_slot_t *in = &own->instruments[slot];
        const pm_table_t *t = pm_table(slot, in->type);

        pm_active[slot] = t ? t->map : 0;
        pm_slope[slot] = (t && t->slope_index < INSTRUMENT_PARAM_COUNT)
            ? in->parameter_images.instrument_parameters[t->slope_index] : 64u;
        /* A slot already on its Plaits engine needs no redirection. */
        if (in->type == pm_slots[slot].type)
            pm_active[slot] = 0;
    }
    pm_slopeOpen = pm_slope[INSTRUMENT_SLOT_COUNT - 1u];
    plaitsMode_mapActive = 1u;
}

void plaitsMode_mapOff(void)
{
    plaitsMode_mapActive = 0u;
}

uint8_t plaitsMode_remap(uint16_t *target, uint8_t *value)
{
    uint8_t slot;
    uint8_t local;
    pm_map_t m;

    if (!plaitsMode_mapActive || !instrumentParam_isVoiceParameter(*target))
        return 1u;
    slot = instrumentParam_slot(*target);
    local = instrumentParam_local(*target);
    if (slot >= INSTRUMENT_SLOT_COUNT || local >= INSTRUMENT_PARAM_COUNT)
        return 0u;
    if (!pm_active[slot])
        return 1u;
    m = pm_active[slot][local];
    if (m.dst >= INSTRUMENT_PARAM_COUNT || (m.kind & PM_NOAUTO))
        return 0u;
    m.kind = PM_KIND(m.kind);
    *value = pm_convert(slot, &m, *value,
                        m.kind == PM_DECAY_OPEN ? pm_slopeOpen : pm_slope[slot]);
    *target = instrumentParam_make(slot, m.dst);
    return 1u;
}
