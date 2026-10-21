#include "PlaitsHatParameters.h"
#include "PlaitsHatVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Plaits hi-hat parameter source of truth.
 *
 * Plaits macros under LXR names: Noise = Harmonics (clocked noise mixed into
 * the metallic source), Tone = Timbre (colour band-pass and high-pass
 * cutoff), Decay = Morph. RingMod off is the 808 square-oscillator hat (Plaits
 * main output), on the ring-modulated hat (Plaits aux output). Decay Open
 * (`phat_decay_choke`) is the open-hat decay, substituted on the VOICE7 page.
 */
const char plaits_hat_instrument_display_label[] = "PlHat";
const uint8_t plaits_hat_instrument_type_flags =
    PLAITS_HAT_INSTRUMENT_TYPE_FLAGS;

#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(PlaitsHatVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

#define MOD_NONE \
    { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 \
    { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63 \
    { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
#define ROW_SLOT_DECIMATION(key_, cat_, long_, short_, dtype_) \
    ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, MOD_0_127, INSTRUMENT_BIND_SLOT_DECIMATION)

/* Local descriptor indices; stored in Scene images, automation and LFO
 * tokens: append only. */
typedef enum {
    PH_PARAM_NOTE = 0u,
    PH_PARAM_FINE,
    PH_PARAM_RING,
    PH_PARAM_NOISE,
    PH_PARAM_TONE,
    PH_PARAM_DECAY,
    PH_PARAM_DECAY_CHOKE,
    PH_PARAM_AMP_ENVELOPE_ATTACK,
    PH_PARAM_AMP_ENVELOPE_DECAY,
    PH_PARAM_AMP_ENVELOPE_SLOPE,
    PH_PARAM_INSTRUMENT_VOL,
    PH_PARAM_INSTRUMENT_PAN,
    PH_PARAM_INSTRUMENT_DRIVE,
    PH_PARAM_INSTRUMENT_DECIMATION,
    PH_PARAM_LFO_RATE,
    PH_PARAM_LFO_AMOUNT,
    PH_PARAM_LFO_AMOUNT_2,
    PH_PARAM_LFO_WAVE,
    PH_PARAM_LFO_RETRIGGER_VOICE,
    PH_PARAM_LFO_POLARITY,
    PH_PARAM_LFO_SYNC,
    PH_PARAM_LFO_OFFSET,
    PH_PARAM_VELO_VOL_ON_OFF,
    PH_PARAM_VELO_MOD_AMOUNT,
    PH_PARAM_VELO_MOD_DEST,
    PH_PARAM_LFO_TARGET_VOICE,
    PH_PARAM_LFO_TARGET_PARAM,
    PH_PARAM_LFO_TARGET_VOICE_2,
    PH_PARAM_LFO_TARGET_PARAM_2,
    PH_PARAM_COUNT
} plaits_hat_param_index_t;

_Static_assert(PLAITS_HAT_PARAM_DESCRIPTOR_COUNT == PH_PARAM_COUNT,
               "PLAITS_HAT_PARAM_DESCRIPTOR_COUNT must match the enum");

const ParamDescriptor plaits_hat_param_descriptors[] = {
    ROW("phat_note", "Plaits", "Note", "not", DTYPE_0B127, MOD_0_127, note, TYPE_UINT8),
    ROW("phat_fine", "Plaits", "Fine", "fin", DTYPE_PM63, MOD_PM63, fine, TYPE_UINT8),
    { "phat_ring", "rng", "RingMod", "Plaits", DTYPE_ON_OFF, INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_AUTOMATABLE, MOD_NONE, BIND(model, TYPE_UINT8) },
    ROW("phat_noise", "Plaits", "Noise", "nse", DTYPE_0B127, MOD_0_127, noise, TYPE_FLT),
    ROW("phat_tone", "Plaits", "Tone", "ton", DTYPE_0B127, MOD_0_127, tone, TYPE_FLT),
    ROW("phat_decay", "Plaits", "Decay", "dcy", DTYPE_0B127, MOD_0_127, decay, TYPE_FLT),
    ROW("phat_decay_choke", "Plaits", "Dcy Open", "d2", DTYPE_0B127, MOD_0_127, decay_choke, TYPE_FLT),
    ROW_SPECIAL("amp_envelope_attack", "Veloc EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, oscVolEg.attack, TYPE_FLT, IM_SPECIAL_AMP_ATTACK),
    ROW_SPECIAL("amp_envelope_decay", "Veloc EG", "Decay", "dec", DTYPE_0B127, MOD_0_127, oscVolEg.decay, TYPE_FLT, IM_SPECIAL_AMP_DECAY),
    ROW_SPECIAL("amp_envelope_slope", "Veloc EG", "Slope", "slp", DTYPE_0B127, MOD_0_127, oscVolEg.slope, TYPE_FLT, IM_SPECIAL_AMP_SLOPE),
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    ROW_SPECIAL("instrument_drive", "Voice", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, distortion.shape, TYPE_FLT, IM_SPECIAL_INSTRUMENT_DRIVE),
    ROW_SLOT_DECIMATION("instrument_decimation", "Voice", "SampleRt", "srt", DTYPE_0B127),
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    ROW("velo_vol_on_off", "Velocity", "Vol mod", "vel", DTYPE_ON_OFF, MOD_NONE, volumeMod, TYPE_UINT8),
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
};

const uint8_t plaits_hat_param_descriptor_count =
    (uint8_t)(sizeof(plaits_hat_param_descriptors) /
              sizeof(plaits_hat_param_descriptors[0]));

_Static_assert(sizeof(plaits_hat_param_descriptors) /
                   sizeof(plaits_hat_param_descriptors[0]) == PH_PARAM_COUNT,
               "plaits_hat_param_descriptors must match the enum");

#define E INSTRUMENT_MENU_EMPTY
#define S INSTRUMENT_MENU_SKIP

/* VOICE pages in the LXR positions; on VOICE7 the Decay cell shows Dcy Open. */
const instrument_menu_page_t plaits_hat_menu_pages[] = {
    {{ PH_PARAM_NOTE, PH_PARAM_FINE, PH_PARAM_TONE, PH_PARAM_DECAY, E, E, E, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_AMP_ENVELOPE_ATTACK, PH_PARAM_AMP_ENVELOPE_DECAY, PH_PARAM_AMP_ENVELOPE_SLOPE, E, E, E, E, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_NOISE, PH_PARAM_TONE, PH_PARAM_DECAY, S, PH_PARAM_VELO_MOD_DEST, PH_PARAM_VELO_MOD_AMOUNT, PH_PARAM_VELO_VOL_ON_OFF, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_RING, PH_PARAM_NOISE, PH_PARAM_TONE, PH_PARAM_DECAY, E, E, E, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_NOTE, PH_PARAM_FINE, PH_PARAM_DECAY_CHOKE, E, E, E, E, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_TONE, PH_PARAM_DECAY, PH_PARAM_DECAY_CHOKE, PH_PARAM_NOISE, E, E, E, E, E, E, E, E, E, E, E, E }},
    {{ PH_PARAM_LFO_RATE, PH_PARAM_LFO_SYNC, PH_PARAM_LFO_WAVE, PH_PARAM_LFO_OFFSET, PH_PARAM_LFO_RETRIGGER_VOICE, PH_PARAM_LFO_POLARITY, PH_PARAM_LFO_AMOUNT, PH_PARAM_LFO_AMOUNT_2, PH_PARAM_LFO_TARGET_VOICE, PH_PARAM_LFO_TARGET_PARAM, PH_PARAM_LFO_TARGET_VOICE_2, PH_PARAM_LFO_TARGET_PARAM_2, E, E, E, E }},
    {{ PH_PARAM_INSTRUMENT_VOL, PH_PARAM_INSTRUMENT_PAN, PH_PARAM_INSTRUMENT_DECIMATION, PH_PARAM_INSTRUMENT_DRIVE, E, E, E, E, E, E, E, E, E, E, E, E }},
};

#undef S
#undef E

const uint8_t plaits_hat_menu_page_count =
    (uint8_t)(sizeof(plaits_hat_menu_pages) / sizeof(plaits_hat_menu_pages[0]));

_Static_assert(PLAITS_HAT_MENU_PAGE_COUNT ==
                   (sizeof(plaits_hat_menu_pages) / sizeof(plaits_hat_menu_pages[0])),
               "PLAITS_HAT_MENU_PAGE_COUNT must match plaits_hat_menu_pages");

#undef FLAGS_IMAGE
#undef BIND
#undef BIND_SPECIAL
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_SLOT_DECIMATION
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
