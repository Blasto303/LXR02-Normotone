/*
 * LIVE mode (contract in liveMode.h).
 *
 * Kit swap: the playing Scene's kit is replaced by Scene N's kit through the
 * same retained commit as `copy kit` (scene_commitKit() + the drumset apply
 * worker). The Scene's own kit is kept in live_kitBackup and committed back
 * when the pattern changes, so a Bank keeps "pattern N plays kit N".
 *
 * Bar quantisation reuses Helicase's queued Scene change: a kit selected
 * while the transport runs queues a change to the playing Scene itself (the
 * "carrier") at the qcd division; when it fires, the kit is applied and the
 * Scene change is skipped. A pattern queued after the kit replaces the
 * carrier; the kit then lands on that new pattern at the same boundary.
 *
 * Mix lock: before any Scene change the incoming Scene takes the outgoing
 * Scene's Effect record, bus compressor, Effect Morph amount and per-voice
 * output / FX send / fader settings. The Effect type then matches, so
 * effects_activateScene() keeps the running delay/reverb tails.
 *
 * PLAITS (DATA click on the KIT page, or LOAD + RELOAD): the playing kit is
 * replaced by its Plaits conversion (plaitsMode.c) and every pattern change
 * converts the incoming kit, so the patterns play on Plaits engines. The
 * pattern's own kit stays in live_kitBackup, as for a kit swap. The Plaits
 * kit is written without an AutoSave mark: a power cut never leaves a
 * converted kit in the Bank.
 *
 * New Bank: when a Bank load ends, pattern 1 of the new Bank is queued for
 * the end of the bar (live_service), so the change is heard without a
 * SHIFT + step.
 *
 * RAM: two kit_t (backup, conversion) plus a few bytes.
 */
#include "liveMode.h"

#include <string.h>

#include "SceneData.h"
#include "BankData.h"
#include "presetManager.h"
#include "sequencer.h"
#include "ledHandler.h"
#include "menu.h"
#include "buttonHandler.h"
#include "InstrumentManager.h"
#include "Autosave.h"
#include "random.h"
#include "cpuProfile.h"
#include "plaitsMode.h"
#include "adcPots.h"
#include "timebase.h"
#include "TrackGen.h"
#include "PatternData.h"
#include "DlyVerbParameters.h"

static kit_t    live_kitBackup;
static uint8_t  live_backupScene = 0xFFu;   /* Scene whose kit is swapped */
static uint16_t live_backupBank = 0xFFFFu;  /* Bank slot of that Scene    */
static uint8_t  live_kitSource = 0xFFu;     /* kit shown on the KIT page  */
static uint8_t  live_pendingKit = 0xFFu;    /* kit waiting for the bar    */
static uint8_t  live_carrier;               /* queue only carries the kit */
static uint8_t  live_page;                  /* 0 none, 1 KIT, 2 PATTERN, 3 RND, 4 CPU, 5 GEN */
/* GEN page (LOAD + LAST STEP): the track whose generator is edited. */
static uint8_t  live_genTrk;
static uint32_t live_genShown;              /* LED signature on the GEN page */
static uint8_t  live_cpuEntry;              /* first entry on the CPU page */
static uint8_t  live_cpuSerial;
static uint8_t  live_shownQueue = 0xFFu;    /* queued pattern on the LCD  */
static uint8_t  live_kitHold;               /* 1: kit stays on pattern change */
static uint8_t  live_libLeds;               /* step LEDs drawn for the kit library */
static uint8_t  live_flash;                 /* 1: PLAITS on/off message showing */
static uint16_t live_flashStart;
static uint8_t  live_plaits;                /* PLAITS wanted                  */
static uint8_t  live_plaitsOn;              /* PLAITS applied on the backup Scene */
static uint8_t  live_tablesReady;           /* plaitsMode tables built        */
static kit_t    live_kitTemp;               /* Plaits conversion              */
static uint8_t  live_bankLoading;           /* background Bank load seen      */
static uint16_t live_bankBefore = 0xFFFFu;  /* Bank slot when it started      */
/*
 * RANDOM (SHIFT + LAST STEP): dose 0..127 on the pattern (sequencer layer,
 * seq_liveRndDose), the kit (random Morph target + voice Morph amount =
 * dose, slots 2..6) or both. DRUM1 is never randomised. The Scene whose
 * voice Morph amounts were changed keeps their old values for restore.
 */
static uint8_t  live_rndDose;
static uint8_t  live_rndTarget = 1u;        /* pattern only (simple Morph) */
static uint8_t  live_rndScene = 0xFFu;
/*
 * Simple Morph (LOAD + MORPH): one target kit for the whole machine. MRP
 * slides every voice of the playing kit towards it (sound engine only). The
 * target is the kit of a pattern (steps 1..16) or a card kit (KitMrp load,
 * captured in live_morphKit). It follows the player: each kit put in place
 * gets the target in its Morph endpoints, which are runtime-only (reset to
 * normal at every load, never marked for AutoSave).
 */
#define LIVE_MORPH_NONE  0xFFu
#define LIVE_MORPH_CARD  0xFEu
static kit_t    live_morphKit;
static uint8_t  live_morphSrc = LIVE_MORPH_NONE;
static uint8_t  live_morphLibLeds;
static void live_morphApply(uint8_t scene);
static uint16_t live_rndBank = 0xFFFFu;
static uint8_t  live_rndSavedAmt[INSTRUMENT_SLOT_COUNT];
/*
 * The Morph endpoints RANDOM overwrote on live_rndScene (slots 2..6), with
 * the slot types they belong to. They are restored with the amounts, and the
 * random values are never marked for AutoSave: the player's own Morph sounds
 * (and any lent or Plaits kit) never reach the card through RANDOM.
 */
static instrument_param_value_t live_rndSavedMorph[INSTRUMENT_SLOT_COUNT][INSTRUMENT_PARAM_COUNT];
static uint8_t  live_rndSavedType[INSTRUMENT_SLOT_COUNT];

/* Called before audio starts, only when no saved Bank or Scene owns the mix. */
void live_initFactoryRumble(void)
{
    /* Same dark rumble as the converted kits. Both Morph endpoints start alike. */
    static const uint8_t values[DLYVERB_PARAM_COUNT] = {
        [DLYVERB_PARAM_DIVISION] = 5u, /* 1/16 */
        [DLYVERB_PARAM_FEEDBACK] = 96u,
        [DLYVERB_PARAM_DLY_LEVEL] = 100u,
        [DLYVERB_PARAM_RVB_LEVEL] = 127u,
        [DLYVERB_PARAM_MODEL] = DLYVERB_MODEL_TAPE,
        [DLYVERB_PARAM_STEREO] = DLYVERB_STEREO_MONO,
        [DLYVERB_PARAM_WIDTH] = 64u,
        [DLYVERB_PARAM_DRIVE] = 120u,
        [DLYVERB_PARAM_LOWPASS] = 10u,
        [DLYVERB_PARAM_HIGHPASS] = 4u,
        [DLYVERB_PARAM_MOD_DEPTH] = 22u,
        [DLYVERB_PARAM_MOD_RATE] = 30u,
        [DLYVERB_PARAM_AGE] = 0u,
        [DLYVERB_PARAM_RVB_DECAY] = 118u,
        [DLYVERB_PARAM_RVB_DAMP] = 124u,
        [DLYVERB_PARAM_RVB_ROUTE] = DLYVERB_ROUTE_FEEDBACK,
        /* Plate bus: sends closed, return and tone ready. */
        [DLYVERB_PARAM_PLATE_LEVEL] = 100u,
        [DLYVERB_PARAM_PLATE_TONE] = 64u,
        /* Rumble duck: about the old pumping (21 dB, on the rumble only). */
        [DLYVERB_PARAM_DUCK] = 90u,
        [DLYVERB_PARAM_DUCK_REL] = 40u
    };

    for (uint8_t i = 0; i < SCENE_COUNT; ++i) {
        scene_t *scene = scene_get(i);
        effects_recordDefaultsForType(&scene->effect, EFFECT_TYPE_DLYVERB);
        for (uint8_t row = DLYVERB_PARAM_DIVISION; row < DLYVERB_PARAM_COUNT; ++row) {
            scene->effect.normal[row] = values[row];
            scene->effect.morph[row] = values[row];
        }
        scene->settings.audio_out[0] = 1u; /* DRUM1 dry on St2, FX return on St1. */
        scene->settings.fx_send_amount[0] = 127u;
        scene->settings.fx_send_morph[0] = 127u;
        scene->settings.bus_comp[SCENE_BUS_COMP_MODE] = SCENE_BUS_COMP_MODE_ST1;
        scene->settings.bus_comp[SCENE_BUS_COMP_AMOUNT] = 75u;
        scene->settings.bus_comp[SCENE_BUS_COMP_TIME] = 25u;
        scene->settings.bus_comp[SCENE_BUS_COMP_SIDECHAIN] = 1u; /* DRUM1 */
    }
}

static uint8_t live_rndKitOn(void)
{
    /* Normotone simple Morph: RANDOM never touches the kit's Morph. */
    return 0u;
}

static void live_rndSetAmounts(uint8_t scene, uint8_t amount);
static void live_rndTake(uint8_t scene);

/* New random Morph target for slots 2..6 of `scene`. */
static void live_rndRollKit(uint8_t scene)
{
    uint8_t slot;

    live_rndTake(scene);

    for (slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        kit_instrument_slot_t *inst = scene_instrumentSlot(scene, slot);
        const instrument_registry_entry_t *entry;
        uint8_t i;

        if (!inst)
            continue;
        entry = instrumentManager_registryEntry(inst->type);
        if (!entry || !entry->descriptors)
            continue;
        for (i = 0u; i < entry->descriptor_count; i++) {
            const ParamDescriptor *d = &entry->descriptors[i];
            int16_t lo = d->mod_domain.min_value;
            int16_t hi = d->mod_domain.max_value;
            int16_t span;
            int16_t v;

            if ((d->flags & (INSTRUMENT_PARAM_FLAG_MORPHABLE |
                             INSTRUMENT_PARAM_FLAG_MODULATABLE)) !=
                    (INSTRUMENT_PARAM_FLAG_MORPHABLE |
                     INSTRUMENT_PARAM_FLAG_MODULATABLE) || hi <= lo)
                continue;
            span = (int16_t)(hi - lo);
            v = (int16_t)(inst->parameter_images.instrument_parameters[i] +
                          (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) %
                                    (uint16_t)(span + 1)) - span / 2);
            if (v < lo)
                v = lo;
            if (v > hi)
                v = hi;
            inst->parameter_images.morph_instrument_parameters[i] =
                (instrument_param_value_t)v;
        }
    }
    bank_invalidateSdCleanScene(scene);
}

/* Give `scene` back the voice Morph amounts it had before RANDOM. */
static void live_rndRestoreAmounts(void)
{
    uint8_t slot;

    if (live_rndScene != 0xFFu && live_rndBank == bank_restoreBankSlot()) {
        /* Morph endpoints back, on slots that still hold the same type. */
        for (slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
            kit_instrument_slot_t *inst =
                scene_instrumentSlot(live_rndScene, slot);

            if (inst && inst->type == live_rndSavedType[slot])
                memcpy(inst->parameter_images.morph_instrument_parameters,
                       live_rndSavedMorph[slot], INSTRUMENT_PARAM_COUNT);
        }
        if (live_rndScene == scene_getActiveIndex())
            preset_rebuildMorph();
        for (slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++)
            preset_morphVoiceScene(live_rndScene, slot, live_rndSavedAmt[slot]);
    }
    live_rndScene = 0xFFu;
}

/*
 * Make `scene` the RANDOM Scene: the previous one gets its values back, then
 * the amounts and Morph endpoints of `scene` are kept for the restore. A
 * re-roll on the same Scene keeps the first saved values.
 */
static void live_rndTake(uint8_t scene)
{
    uint8_t slot;

    if (live_rndScene == scene)
        return;
    live_rndRestoreAmounts();
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        const kit_instrument_slot_t *inst = scene_instrumentSlotConst(scene, slot);

        live_rndSavedAmt[slot] = scene_getVoiceMorphAmount(scene, slot);
        live_rndSavedType[slot] = inst ? (uint8_t)inst->type : 0xFFu;
        if (inst)
            memcpy(live_rndSavedMorph[slot],
                   inst->parameter_images.morph_instrument_parameters,
                   INSTRUMENT_PARAM_COUNT);
    }
    live_rndScene = scene;
    live_rndBank = bank_restoreBankSlot();
}


/* Voice Morph amount = dose on slots 2..6 of `scene`. */
static void live_rndSetAmounts(uint8_t scene, uint8_t amount)
{
    uint8_t slot;

    live_rndTake(scene);
    for (slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        preset_morphVoiceScene(scene, slot, amount);
}

/* Push the RANDOM state to the sequencer and the active kit. */
static void live_rndApply(uint8_t reroll)
{
    uint8_t active = scene_getActiveIndex();

    seq_liveRndDose = (live_rndTarget & 1u) ? live_rndDose : 0u;
    if (live_rndKitOn() && active < SCENE_COUNT) {
        if (reroll || live_rndScene != active) {
            live_rndRollKit(active);
            preset_rebuildMorph();
        }
        live_rndSetAmounts(active, live_rndDose);
    } else {
        live_rndRestoreAmounts();
    }
}

static uint8_t live_sceneValid(uint8_t scene)
{
    return (uint8_t)(scene < SCENE_COUNT && scene < 16u &&
                     bank_sceneAvailable(scene));
}

/* Drop a backup that no longer belongs to the resident Bank. */
static void live_checkBackup(void)
{
    if (live_backupScene != 0xFFu &&
        live_backupBank != bank_restoreBankSlot()) {
        live_backupScene = 0xFFu;
        live_kitSource = 0xFFu;
        live_plaitsOn = 0u;
        plaitsMode_mapOff();
    }
}

/* Kit write without an AutoSave mark (Plaits kits never reach the card). */
static void live_commitQuiet(uint8_t scene, const kit_t *kit)
{
    scene_t *s = scene_get(scene);

    if (!s || &s->kit == kit)
        return;
    s->kit = *kit;
    bank_invalidateSdCleanScene(scene);
}

/*
 * Put on `scene` the kit of Scene `src`, as a Plaits conversion when PLAITS
 * is on. The Scene's own kit is kept in live_kitBackup while it plays
 * anything else, and committed back when it plays its own kit again.
 */
static uint8_t live_install(uint8_t scene, uint8_t src)
{
    const scene_t *own = scene_getConst(scene);
    const kit_t *source;

    if (!own)
        return 0u;
    if (src == scene && !live_plaits) {
        if (live_backupScene == scene) {
            (void)scene_commitKit(scene, &live_kitBackup);
            live_backupScene = 0xFFu;
        }
        plaitsMode_mapOff();
        live_kitSource = src;
        live_plaitsOn = 0u;
        live_morphApply(scene);
        return 1u;
    }
    if (live_backupScene != scene) {
        live_kitBackup = own->kit;
        live_backupScene = scene;
        live_backupBank = bank_restoreBankSlot();
    }
    if (src == scene) {
        source = &live_kitBackup;
    } else {
        const scene_t *other = scene_getConst(src);

        if (!other)
            return 0u;
        source = &other->kit;
    }
    if (live_plaits) {
        plaitsMode_convertKit(&live_kitTemp, source);
        live_commitQuiet(scene, &live_kitTemp);
        plaitsMode_mapOn(&live_kitBackup);
    } else {
        (void)scene_commitKit(scene, source);
        plaitsMode_mapOff();
    }
    live_kitSource = src;
    live_plaitsOn = live_plaits;
    live_morphApply(scene);
    return 1u;
}

/*
 * Put the Morph target in the Morph endpoints of `scene`'s kit. Slots whose
 * instrument type differs from the target's stay on their normal sound (no
 * Morph). With PLAITS sounding on that Scene, the target is converted first.
 */
static void live_morphApply(uint8_t scene)
{
    scene_t *s = scene_get(scene);
    const kit_t *t = NULL;
    uint8_t slot;

    if (!s || scene >= 16u)
        return;
    if (live_morphSrc < 16u && live_sceneValid(live_morphSrc)) {
        const scene_t *src = scene_getConst(live_morphSrc);

        t = (live_morphSrc == live_backupScene) ? &live_kitBackup
            : (src ? &src->kit : NULL);
    } else if (live_morphSrc == LIVE_MORPH_CARD) {
        t = &live_morphKit;
    }
    if (t && live_plaitsOn && live_backupScene == scene) {
        plaitsMode_convertKit(&live_kitTemp, t);
        t = &live_kitTemp;
    }
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        kit_instrument_slot_t *in = &s->kit.instruments[slot];
        const instrument_param_value_t *from =
            (t && t->instruments[slot].type == in->type)
            ? t->instruments[slot].parameter_images.instrument_parameters
            : in->parameter_images.instrument_parameters;

        memcpy(in->parameter_images.morph_instrument_parameters, from,
               INSTRUMENT_PARAM_COUNT);
    }
    s->kit.settings.slot6_track7_morph_amp_envelope_decay =
        (t && t->instruments[INSTRUMENT_SLOT_COUNT - 1u].type ==
                  s->kit.instruments[INSTRUMENT_SLOT_COUNT - 1u].type)
        ? t->settings.slot6_track7_amp_envelope_decay
        : s->kit.settings.slot6_track7_amp_envelope_decay;
    if (scene == scene_getActiveIndex())
        preset_rebuildMorph();
}

void live_morphLeds(void)
{
    uint16_t present = bank_scenePresentMask();
    uint8_t s;

    live_morphLibLeds = menu_liveMorphLibraryActive();
    if (!live_morphLibLeds)
        return;
    for (s = 0u; s < 16u; s++) {
        uint8_t led = (uint8_t)(LED_SEQ1 + s);

        led_setBlinkLed(led, (uint8_t)(s == live_morphSrc));
        led_setValue((uint8_t)((present & (uint16_t)(1u << s)) != 0u), led);
    }
}

/* Step N in the Morph library: the kit of pattern N; again = no target. */
void live_morphSelect(uint8_t step)
{
    if (step >= 16u || !live_sceneValid(step))
        return;
    live_morphSrc = (live_morphSrc == step) ? LIVE_MORPH_NONE : step;
    live_morphApply(scene_getActiveIndex());
    live_morphLeds();
    menu_repaintAll();
}

/* A KitMrp load from the card just filled the playing kit's Morph side. */
void live_morphCaptured(void)
{
    const scene_t *s = scene_getConst(scene_getActiveIndex());
    uint8_t slot;

    if (!s)
        return;
    live_morphKit = s->kit;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        memcpy(live_morphKit.instruments[slot].parameter_images.instrument_parameters,
               s->kit.instruments[slot].parameter_images.morph_instrument_parameters,
               INSTRUMENT_PARAM_COUNT);
    live_morphKit.settings.slot6_track7_amp_envelope_decay =
        s->kit.settings.slot6_track7_morph_amp_envelope_decay;
    live_morphSrc = LIVE_MORPH_CARD;
    live_morphLeds();
}

uint8_t live_morphSource(void)
{
    return live_morphSrc;
}

/* A successfully loaded SD kit becomes the scene's own kit. The
 * performance substitution may no longer restore its previous backup. */
void live_kitLoadCommitted(uint16_t scene_mask)
{
    uint8_t active = scene_getActiveIndex();

    live_checkBackup();
    /* RANDOM: the saved Morph belonged to the replaced kit; keep the new one. */
    if (live_rndScene < 16u && (scene_mask & (uint16_t)(1u << live_rndScene)))
        for (uint8_t slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++)
            live_rndSavedType[slot] = 0xFFu;
    if (active < 16u && (scene_mask & (uint16_t)(1u << active)))
        live_pendingKit = 0xFFu;
    if (live_backupScene < 16u &&
        (scene_mask & (uint16_t)(1u << live_backupScene))) {
        live_backupScene = 0xFFu;
        live_kitSource = 0xFFu;
        live_plaitsOn = 0u;
        plaitsMode_mapOff();
    }
    /* The loaded kit's Morph side was reset: the target goes back on. */
    if (active < 16u && (scene_mask & (uint16_t)(1u << active)))
        live_morphApply(active);
    /* PLAITS on: the new kit is converted at once, not at the bar. */
    if (live_plaits && active < 16u && (scene_mask & (uint16_t)(1u << active))) {
        live_pendingKit = active;
    }
}

static uint8_t live_currentKit(void)
{
    uint8_t active = scene_getActiveIndex();

    live_checkBackup();
    if (live_backupScene == active && live_kitSource < 16u)
        return live_kitSource;
    return active;
}

/* Commit kit of `src` onto the active Scene now. 0 = workers busy, retry. */
/* PLAITS state of the playing kit matches the wish. */
static uint8_t live_plaitsSettled(void)
{
    uint8_t active = scene_getActiveIndex();

    live_checkBackup();
    if (!live_plaits)
        return (uint8_t)(!live_plaitsOn || live_backupScene != active);
    return (uint8_t)(live_plaitsOn && live_backupScene == active);
}

static uint8_t live_applyKit(uint8_t src)
{
    uint8_t active = scene_getActiveIndex();

    if (!preset_applyWorkersIdle() || preset_backgroundBankLoadUnlocked())
        return 0u;
    live_checkBackup();
    if (active >= SCENE_COUNT || !live_sceneValid(src))
        return 1u;
    if (src == live_currentKit() && live_plaitsSettled()) {
        live_kitSource = src;
        return 1u;
    }
    /* RANDOM: the kit gets its own Morph back before it is kept as backup. */
    if (live_rndScene == active)
        live_rndRestoreAmounts();
    if (!live_install(active, src))
        return 1u;
    preset_startDrumsetApply();
    if (live_rndKitOn())
        live_rndApply(1u);
    return 1u;
}

void live_patternPageOpen(void)
{
    if (live_page == 1u)
        live_kitPageClose();
    live_page = 2u;
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

uint8_t live_patternPageActive(void)
{
    return (uint8_t)(live_page == 2u);
}

void live_patternSelect(uint8_t scene)
{
    if (!live_sceneValid(scene))
        return;
    live_carrier = 0u;
    seq_queueSceneChange(scene, parameter_values[PAR_SCENE_QUEUE_DIV]);
    menu_repaintAll();
}

void live_reloadKit(void)
{
    /* RELOAD also ends RANDOM: the original pattern and kit come back. */
    live_rndDose = 0u;
    live_rndApply(0u);
    live_kitSelect(scene_getActiveIndex());
}

/*
 * GEN page (LOAD + LAST STEP, in PERF): the per-track generators.
 *
 * What: pot 1 picks the track mode (std, euc, tm); pots 2-4 set the mode's
 * three values; with SHIFT held, pots 2-4 set the Turing velocity
 * modulation (vdp, len, prb), active in every mode. DATA = pot 2. VOICE
 * buttons pick the track. Step LEDs show the Euclid rhythm (playhead
 * blinking) or the Turing register (bit n on step n+1, Length blinking);
 * a step press sets the Euclid rotation or flips that register bit (Write).
 * Non-destructive: values go to pat_scene_region_t.track_gen[] of the
 * active Scene (AutoSave), the Pattern steps are never rewritten.
 */
static const uint8_t live_tmLens[8] = { 2u, 3u, 4u, 5u, 6u, 8u, 12u, 16u };

static const pat_track_gen_t *live_genRec(void)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_getActiveIndex());

    return (r && live_genTrk < NUM_TRACKS) ? &r->track_gen[live_genTrk] : NULL;
}

static uint8_t live_genGet(uint8_t field)
{
    return pat_trackGen(scene_getActiveIndex(), live_genTrk, field);
}

static void live_genSet(uint8_t field, int16_t v)
{
    pat_setTrackGen(scene_getActiveIndex(), live_genTrk, field,
                    (uint8_t)((v < 0) ? 0 : ((v > 255) ? 255 : v)));
}

/* Turing Length steps through 2,3,4,5,6,8,12,16. */
static void live_genStepLen(int8_t delta)
{
    uint8_t cur = live_genGet(PAT_GEN_F_TM_LEN);
    int8_t i = 5;                               /* 8 = default */
    uint8_t k;

    for (k = 0u; k < 8u; k++)
        if (live_tmLens[k] == cur)
            i = (int8_t)k;
    i = (int8_t)(i + ((delta > 0) ? 1 : -1));
    i = (int8_t)((i < 0) ? 0 : ((i > 7) ? 7 : i));
    live_genSet(PAT_GEN_F_TM_LEN, live_tmLens[i]);
}

static void live_genLeds(uint8_t force)
{
    const pat_track_gen_t *g = live_genRec();
    uint32_t sig;
    uint8_t s;

    if (live_page != 5u || !g)
        return;
    if (g->v[PAT_GEN_F_MODE] == PAT_GEN_EUC)
        sig = 0x80000000u | ((uint32_t)tgen_eucPos(live_genTrk) << 24) |
              ((uint32_t)g->v[PAT_GEN_F_EUC_STEPS] << 16) |
              ((uint32_t)g->v[PAT_GEN_F_EUC_HITS] << 8) |
              g->v[PAT_GEN_F_EUC_ROT];
    else
        sig = ((uint32_t)tgen_tmLen(g) << 16) | tgen_register(live_genTrk);
    sig ^= (uint32_t)live_genTrk << 28;
    if (!force && sig == live_genShown)
        return;
    live_genShown = sig;
    if (g->v[PAT_GEN_F_MODE] == PAT_GEN_EUC) {
        /* Hit k plays at step k+1 of its own cycle; the playhead blinks. */
        const uint8_t n = tgen_eucSteps(g);
        const uint8_t pos = tgen_eucPos(live_genTrk);
        const uint8_t now = (uint8_t)((pos == 0u ? n : pos) - 1u);

        for (s = 0u; s < 16u; s++) {
            led_setBlinkLed((uint8_t)(LED_SEQ1 + s),
                            (uint8_t)(s == now && seq_isRunning()));
            led_setValue((uint8_t)(s < n && tgen_eucHit(g, s)),
                         (uint8_t)(LED_SEQ1 + s));
        }
    } else {
        const uint16_t r = tgen_register(live_genTrk);
        const uint8_t len = tgen_tmLen(g);

        for (s = 0u; s < 16u; s++) {
            led_setBlinkLed((uint8_t)(LED_SEQ1 + s),
                            (uint8_t)(s == (uint8_t)(len - 1u)));
            led_setValue((uint8_t)((r >> s) & 1u), (uint8_t)(LED_SEQ1 + s));
        }
    }
}

void live_genPageOpen(void)
{
    if (live_page == 1u)
        live_kitPageClose();
    live_page = 5u;
    live_genTrk = menu_getActiveVoice();
    if (live_genTrk >= NUM_TRACKS)
        live_genTrk = 0u;
    live_genLeds(1u);
    menu_repaintAll();
}

uint8_t live_genPageActive(void)
{
    return (uint8_t)(live_page == 5u);
}

void live_genTrack(uint8_t track)
{
    if (track >= NUM_TRACKS)
        return;
    live_genTrk = track;
    menu_setActiveVoice(track);     /* PERF pot 2 follows this track */
    live_genLeds(1u);
    menu_repaintAll();
}

/* A step press: Euclid rotation, or Turing Write (flip that bit). */
void live_genStep(uint8_t step)
{
    const pat_track_gen_t *g = live_genRec();

    if (!g || step >= 16u)
        return;
    if (g->v[PAT_GEN_F_MODE] == PAT_GEN_EUC)
        live_genSet(PAT_GEN_F_EUC_ROT, (int16_t)(step % tgen_eucSteps(g)));
    else
        tgen_flipBit(live_genTrk, step);
    live_genLeds(1u);
    menu_repaintAll();
}

/* Pot 1 mode; pots 2-4 by mode (SHIFT: vdp len prb). Endless deltas. */
void live_genKnob(uint8_t knob, int8_t delta)
{
    const pat_track_gen_t *g = live_genRec();
    uint8_t mode;

    if (delta == 0 || !g)
        return;
    mode = g->v[PAT_GEN_F_MODE];
    if (knob == 0u) {
        int16_t m = (int16_t)mode + ((delta > 0) ? 1 : -1);

        live_genSet(PAT_GEN_F_MODE,
                    (m < 0) ? 0 : ((m >= (int16_t)PAT_GEN_MODES)
                                   ? (int16_t)(PAT_GEN_MODES - 1u) : m));
    } else if (buttonHandler_getShift() || mode == PAT_GEN_STD) {
        if (knob == 1u)
            live_genSet(PAT_GEN_F_VDP, (int16_t)live_genGet(PAT_GEN_F_VDP) + delta);
        else if (knob == 2u)
            live_genStepLen(delta);
        else if (knob == 3u)
            live_genSet(PAT_GEN_F_TM_PRB,
                        (int16_t)live_genGet(PAT_GEN_F_TM_PRB) + delta);
    } else if (mode == PAT_GEN_EUC) {
        if (knob == 1u)
            live_genSet(PAT_GEN_F_EUC_STEPS,
                        (int16_t)tgen_eucSteps(g) + delta);
        else if (knob == 2u)
            live_genSet(PAT_GEN_F_EUC_HITS,
                        (int16_t)live_genGet(PAT_GEN_F_EUC_HITS) + delta);
        else if (knob == 3u) {
            int16_t n = (int16_t)tgen_eucSteps(g);
            int16_t v = (int16_t)live_genGet(PAT_GEN_F_EUC_ROT) + delta;

            while (v < 0)
                v += n;
            live_genSet(PAT_GEN_F_EUC_ROT, (int16_t)(v % n));
        }
    } else {
        if (knob == 1u)
            live_genStepLen(delta);
        else if (knob == 2u)
            live_genSet(PAT_GEN_F_TM_PRB,
                        (int16_t)live_genGet(PAT_GEN_F_TM_PRB) + delta);
        else if (knob == 3u)
            live_genSet(PAT_GEN_F_TM_GAT,
                        (int16_t)live_genGet(PAT_GEN_F_TM_GAT) + delta);
    }
    live_genLeds(1u);
    menu_repaintAll();
}

/*
 * PERF pot 2: the main generator value of the active track (hits in
 * Euclid, prb in Turing or standard). Shown in PERF cell 2.
 */
void live_genMacro(int8_t delta)
{
    uint8_t keep = live_genTrk;

    live_genTrk = menu_getActiveVoice();
    if (live_genTrk < NUM_TRACKS && delta != 0) {
        if (live_genGet(PAT_GEN_F_MODE) == PAT_GEN_EUC)
            live_genSet(PAT_GEN_F_EUC_HITS,
                        (int16_t)live_genGet(PAT_GEN_F_EUC_HITS) + delta);
        else
            live_genSet(PAT_GEN_F_TM_PRB,
                        (int16_t)live_genGet(PAT_GEN_F_TM_PRB) + delta);
        menu_repaintAll();
    }
    live_genTrk = keep;
}

static void live_put3(char *dst, uint8_t v)
{
    dst[0] = (char)(v >= 100u ? '0' + v / 100u : ' ');
    dst[1] = (char)(v >= 10u ? '0' + (v / 10u) % 10u : ' ');
    dst[2] = (char)('0' + v % 10u);
}

void live_cpuPageOpen(void)
{
    if (live_page == 1u)
        live_kitPageClose();
    live_page = 4u;
    menu_repaintAll();
}

uint8_t live_cpuPageActive(void)
{
    return (uint8_t)(live_page == 4u);
}

void live_rndPageOpen(void)
{
    if (live_page == 1u)
        live_kitPageClose();
    live_page = 3u;
    live_rndPageLeds();
    menu_repaintAll();
}

uint8_t live_rndPageActive(void)
{
    return (uint8_t)(live_page == 3u);
}

void live_rndPageLeds(void)
{
    uint8_t s;

    if (live_page != 3u)
        return;
    for (s = 0u; s < 16u; s++) {
        led_setBlinkLed((uint8_t)(LED_SEQ1 + s), 0u);
        led_setValue((uint8_t)(s == 0u), (uint8_t)(LED_SEQ1 + s));
    }
}

void live_rndStep(uint8_t step)
{
    /* Pattern only: the kit modes used the Morph endpoints. */
    (void)step;
    live_rndTarget = 1u;
    live_rndApply(0u);
    live_rndPageLeds();
    menu_repaintAll();
}

void live_kitPageOpen(void)
{
    live_page = 1u;
    live_kitPageLeds();
    menu_repaintAll();
}

void live_kitPageClose(void)
{
    if (live_libLeds) {
        live_libLeds = 0u;
        led_clearAllBlinkLeds();
        menu_refreshPerfSceneLeds();
    }
    if (!live_page)
        return;
    /* Closes either page (KIT or PATTERN). */
    live_page = 0u;
    led_clearAllBlinkLeds();
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

uint8_t live_kitPageActive(void)
{
    return (uint8_t)(live_page == 1u);
}

void live_kitSelect(uint8_t scene)
{
    if (!live_sceneValid(scene))
        return;
    if (!seq_isRunning()) {
        live_pendingKit = scene;
        live_service();
    } else {
        live_pendingKit = scene;
        if (seq_queuedSceneTarget() >= 16u) {
            live_carrier = 1u;
            seq_queueSceneChange(scene_getActiveIndex(),
                                 parameter_values[PAR_SCENE_QUEUE_DIV]);
        }
    }
    live_kitPageLeds();
    menu_repaintAll();
}

void live_kitPageLeds(void)
{
    uint16_t present = bank_scenePresentMask();
    uint8_t current = live_currentKit();
    uint8_t s;

    if (live_page != 1u && !menu_liveKitLibraryActive())
        return;
    live_libLeds = (uint8_t)(live_page != 1u);
    for (s = 0u; s < 16u; s++) {
        uint8_t led = (uint8_t)(LED_SEQ1 + s);

        led_setBlinkLed(led, (uint8_t)(s == current || s == live_pendingKit));
        led_setValue((uint8_t)((present & (uint16_t)(1u << s)) != 0u), led);
    }
}

void live_beforeSceneChange(uint8_t target)
{
    uint8_t active = scene_getActiveIndex();
    const scene_t *src;
    uint8_t i;

    uint8_t carry;

    /*
     * From Scene 17 (the old project still playing after a project load):
     * no mix to carry, but PLAITS must already sound on the first pattern.
     */
    if (active >= SCENE_COUNT && live_plaits && live_sceneValid(target)) {
        live_checkBackup();
        (void)live_install(target, target);
        return;
    }
    if (target == active || !live_sceneValid(target) || active >= SCENE_COUNT)
        return;
    /* Kit playing now (its source Scene), for the "kit stays" option. */
    carry = live_currentKit();
    /* RANDOM: the leaving pattern gets its Morph amounts back. */
    if (live_rndScene == active)
        live_rndRestoreAmounts();
    /* The leaving pattern gets its own kit back. */
    live_checkBackup();
    if (live_backupScene == active) {
        (void)scene_commitKit(active, &live_kitBackup);
        live_backupScene = 0xFFu;
    }
    live_kitSource = 0xFFu;
    /*
     * Kit stays: the incoming pattern plays the current kit. PLAITS: the
     * incoming kit is converted. Either way its own kit is kept in the backup
     * (committed back when it is left), and the kit is in place before the
     * Scene activation applies it, so nothing glitches.
     */
    {
        uint8_t src = target;

        if (live_kitHold && carry < 16u && carry != target &&
            live_sceneValid(carry))
            src = carry;
        if (src != target || live_plaits) {
            (void)live_install(target, src);
        } else {
            live_plaitsOn = 0u;
            plaitsMode_mapOff();
            live_morphApply(target);
        }
        /* MRP follows the player like the mix. */
        preset_morphScene(target, scene_getVoiceMorphAmount(active, 0u));
    }

    /* RANDOM on the kit follows the player: new draw for the new pattern. */
    if (live_rndKitOn()) {
        live_rndRollKit(target);
        live_rndSetAmounts(target, live_rndDose);
    }

    src = scene_getConst(active);
    if (!src)
        return;
    {
        const effect_record_t *fx = scene_effectConst(active);
        const effect_record_t *dst = scene_effectConst(target);

        if (fx && dst && memcmp(fx, dst, sizeof(*fx)) != 0)
            (void)scene_commitEffectRecord(target, fx);
    }
    for (i = 0u; i < INSTRUMENT_SLOT_COUNT; i++) {
        scene_setVoiceAudioOut(target, i, src->settings.audio_out[i]);
        scene_setVoiceFxSendAmount(target, i, src->settings.fx_send_amount[i]);
        scene_setVoiceFxSendMorph(target, i, src->settings.fx_send_morph[i]);
        scene_setVoiceFaderSetting(target, i, src->settings.fader_setting[i]);
    }
    for (i = 0u; i < SCENE_BUS_COMP_FIELD_COUNT; i++)
        scene_setBusCompSetting(target, i, src->settings.bus_comp[i]);
    scene_setEffectMorphAmount(target, src->settings.effect_morph_amount);
}

void live_encoder(int8_t inc, uint8_t clicked)
{
    uint8_t before = live_kitHold;

    if (live_page == 4u) {
        int16_t e = (int16_t)live_cpuEntry + inc;
        int16_t last = (int16_t)prof_entryCount() - 1;  /* + fader line */

        if (last < 0) last = 0;
        if (e > last) e = last;
        if (e < 0) e = 0;
        if (e != live_cpuEntry) {
            live_cpuEntry = (uint8_t)e;
            menu_repaintAll();
        }
        return;
    }
    if (live_page == 5u) {
        live_genKnob(1u, inc);          /* DATA = pot 2 */
        return;
    }
    if (live_page == 3u) {
        int16_t d = (int16_t)live_rndDose + (int16_t)inc * 4;

        if (d < 0)
            d = 0;
        if (d > 127)
            d = 127;
        if (clicked || d != live_rndDose) {
            live_rndDose = (uint8_t)d;
            live_rndApply(clicked);
            menu_repaintAll();
        }
        return;
    }

    if (clicked) {
        live_plaitsToggle();
        return;
    }
    if (inc > 0)
        live_kitHold = 1u;
    else if (inc < 0)
        live_kitHold = 0u;
    if (live_kitHold != before)
        menu_repaintAll();
}

uint8_t live_queuedFire(uint8_t scene)
{
    uint8_t carrier = live_carrier;

    live_carrier = 0u;
    return (uint8_t)(carrier && scene == scene_getActiveIndex());
}

/* Pattern queued by SHIFT+step (not a kit carrier), 0xFF when none. */
static uint8_t live_queuedPattern(void)
{
    uint8_t q = seq_queuedSceneTarget();

    if (q >= 16u || (live_carrier && q == scene_getActiveIndex()))
        return 0xFFu;
    return q;
}

/*
 * Project Save in LIVE: the playing Scene goes back to its own kit for the
 * save (PLAITS and lent kits are never written to a project), then the
 * performance state comes back when the save completes.
 */
static uint8_t live_saveSrc = 0xFFu;

static uint8_t live_saveRnd;

void live_beforeProjectSave(void)
{
    uint8_t active = scene_getActiveIndex();
    uint8_t keep = live_plaits;

    /*
     * RANDOM: its Morph sounds and amounts stay out of the project. The
     * pattern gets its own values back for the save; a new draw follows it.
     */
    live_saveRnd = (uint8_t)(live_rndScene != 0xFFu);
    live_rndRestoreAmounts();
    live_checkBackup();
    if (active >= 16u || live_backupScene != active)
        return;
    live_saveSrc = live_currentKit();
    live_plaits = 0u;
    (void)live_install(active, active);
    live_plaits = keep;
    preset_startDrumsetApply();
}

void live_afterProjectSave(void)
{
    if (live_saveRnd) {
        live_saveRnd = 0u;
        /* A lent or Plaits kit comes back below and re-draws in
         * live_applyKit; the pattern's own kit re-draws here. */
        if (live_saveSrc == 0xFFu && live_rndKitOn())
            live_rndApply(1u);
    }
    if (live_saveSrc == 0xFFu)
        return;
    live_pendingKit = live_saveSrc;
    live_saveSrc = 0xFFu;
}

void live_kitHoldToggle(void)
{
    live_kitHold = (uint8_t)!live_kitHold;
    menu_repaintAll();
}

void live_plaitsToggle(void)
{
    live_plaits = (uint8_t)!live_plaits;
    live_flash = 1u;
    live_flashStart = time_sysTick;
    /* Same path as a kit choice: now when stopped, at the bar when playing. */
    live_kitSelect(live_currentKit());
    menu_repaintAll();
}

void live_buildPlaitsTables(void)
{
    while (!plaitsMode_service())
        ;
    live_tablesReady = 1u;
}

uint8_t live_plaitsActive(void)
{
    return live_plaits;
}

void live_service(void)
{
    menu_liveKitSaveAutoSlot();
    /* Kit library step LEDs follow the page (it may open a few ms late). */
    if (menu_liveKitLibraryActive() != live_libLeds) {
        if (live_libLeds)
            live_kitPageClose();
        else
            live_kitPageLeds();
    }
    /* Morph library step LEDs follow the page the same way. */
    if (menu_liveMorphLibraryActive() != live_morphLibLeds) {
        if (live_morphLibLeds) {
            live_morphLibLeds = 0u;
            led_clearAllBlinkLeds();
            menu_refreshPerfSceneLeds();
        } else {
            live_morphLeds();
        }
    }
    /* PLAITS on/off message: about one second. */
    if (live_flash && (uint16_t)(time_sysTick - live_flashStart) > 1200u) {
        live_flash = 0u;
        menu_repaintAll();
    }
    /*
     * New Bank: when a background Bank load (SAVE / LOAD + PROJECT, or MIDI
     * CC0) ends on another Bank, its pattern 1 is queued for the end of the
     * bar (at once when stopped). Helicase alone keeps the old pattern
     * playing from Scene 17 until a pattern is chosen.
     */
    if (preset_backgroundBankLoadRunning()) {
        if (!live_bankLoading) {
            live_bankLoading = 1u;
            live_bankBefore = bank_restoreBankSlot();
        }
    } else if (live_bankLoading) {
        live_bankLoading = 0u;
        if (bank_restoreBankSlot() != live_bankBefore && live_sceneValid(0u))
            live_patternSelect(0u);
    }
    /* PLAITS tables: built in the background while the transport is stopped. */
    if (!live_tablesReady && !seq_isRunning())
        live_tablesReady = plaitsMode_service();
    /* PLAITS on and a new Bank (or Scene) came in: convert its kit too. */
    if (live_plaits && live_pendingKit == 0xFFu && !live_carrier &&
        live_saveSrc == 0xFFu &&
        seq_queuedSceneTarget() >= 16u && scene_getActiveIndex() < 16u &&
        !live_plaitsSettled())
        live_kitSelect(live_currentKit());
    /* CPU page: a new snapshot every second. */
    /* CPU page fader line: repaint as soon as a fader moves one step. */
    if (live_page == 5u)
        live_genLeds(0u);               /* register / playhead move */
    if (live_page == 4u && live_cpuEntry == 0u) {
        static uint32_t shown;
        uint32_t sig = 0u;
        uint8_t i;

        for (i = 0u; i < 6u; i++)
            sig = sig * 10u + (uint32_t)(slider_vol[i] * 9.0f + 0.5f);
        if (sig != shown) {
            shown = sig;
            menu_repaintAll();
        }
    }
    if (live_page == 4u && prof_snapshotSerial() != live_cpuSerial) {
        live_cpuSerial = prof_snapshotSerial();
        menu_repaintAll();
    }
    /* Repaint when the "Select Pattern" screen appears or goes. */
    {
        uint8_t q = live_queuedPattern();

        if (q != live_shownQueue) {
            live_shownQueue = q;
            menu_repaintAll();
        }
    }
    /* A transport stop cancels the queue: the carrier is gone, apply now. */
    if (live_carrier && seq_queuedSceneTarget() >= 16u &&
        !seq_sceneChangeFirePending())
        live_carrier = 0u;
    if (live_pendingKit != 0xFFu && !live_carrier &&
        seq_queuedSceneTarget() >= 16u) {
        if (live_applyKit(live_pendingKit)) {
            live_pendingKit = 0xFFu;
            live_kitPageLeds();
            if (live_page)
                menu_repaintAll();
        }
    }
}

uint8_t live_formatOverlay(char *row0, char *row1)
{
    uint8_t kit;

    if (live_flash) {
        memset(row0, ' ', 16);
        memcpy(row0, live_plaits ? "Mode Plaits  ON" : "Mode Plaits OFF", 15);
        return 1u;
    }
    /* Kit library: the kit option, top right ("Load: Kit   suit"). */
    if (menu_liveKitLibraryActive()) {
        memcpy(&row0[12], live_kitHold ? "fixe" : "suit", 4);
        return 1u;
    }

    if (live_page == 5u) {
        static const char trk[NUM_TRACKS][4] = {
            "DR1", "DR2", "DR3", "SNR", "CYM", "CHH", "OHH" };
        const pat_track_gen_t *g = live_genRec();
        uint8_t mode;

        if (buttonHandler_getMode() != SELECT_MODE_PERF || !g)
            return 0u;
        mode = g->v[PAT_GEN_F_MODE];
        memset(row0, ' ', 16);
        memset(row1, ' ', 16);
        memcpy(row1, trk[live_genTrk < NUM_TRACKS ? live_genTrk : 0], 3);
        if (buttonHandler_getShift() || mode == PAT_GEN_STD) {
            memcpy(row0, buttonHandler_getShift() ? "MOD vdp len prb"
                                                  : "STD vdp len prb", 15);
            live_put3(&row1[4], g->v[PAT_GEN_F_VDP]);
            live_put3(&row1[8], tgen_tmLen(g));
            live_put3(&row1[12], g->v[PAT_GEN_F_TM_PRB]);
        } else if (mode == PAT_GEN_EUC) {
            memcpy(row0, "EUC stp hit rot", 15);
            live_put3(&row1[4], tgen_eucSteps(g));
            live_put3(&row1[8], g->v[PAT_GEN_F_EUC_HITS]);
            live_put3(&row1[12], g->v[PAT_GEN_F_EUC_ROT]);
        } else {
            memcpy(row0, "TM  len prb gat", 15);
            live_put3(&row1[4], tgen_tmLen(g));
            live_put3(&row1[8], g->v[PAT_GEN_F_TM_PRB]);
            live_put3(&row1[12], g->v[PAT_GEN_F_TM_GAT]);
        }
        return 1u;
    }
    /* PERF page: pot 2 = the active track's generator (hit or prb). */
    if (live_page == 0u && buttonHandler_getMode() == SELECT_MODE_PERF &&
        menu_activePage == PERFORMANCE_PAGE && menu_getSubPage() == 0u) {
        const pat_scene_region_t *r = pat_sceneRegion(scene_getActiveIndex());
        const uint8_t t = menu_getActiveVoice();

        if (r && t < NUM_TRACKS) {
            const pat_track_gen_t *g = &r->track_gen[t];
            const uint8_t euc = (uint8_t)(g->v[PAT_GEN_F_MODE] == PAT_GEN_EUC);

            memcpy(&row0[4], euc ? "hit" : "prb", 3);
            live_put3(&row1[4], euc ? g->v[PAT_GEN_F_EUC_HITS]
                                    : g->v[PAT_GEN_F_TM_PRB]);
        }
    }
    if (live_page == 4u) {
        if (buttonHandler_getMode() != SELECT_MODE_PERF)
            return 0u;
        /*
         * First line: the six faders as read from the ADC, 0..9 each
         * ("Fdr 9 9 9 9 9 9"), DRUM1 .. HIHATS, to check a dead fader.
         */
        if (live_cpuEntry == 0u) {
            uint8_t i;

            memset(row0, ' ', 16);
            memcpy(row0, "Fdr", 3);
            for (i = 0u; i < 6u; i++) {
                uint8_t v = (uint8_t)(slider_vol[i] * 9.0f + 0.5f);

                row0[4u + 2u * i] = (char)('0' + (v > 9u ? 9u : v));
            }
            prof_formatEntry(0u, row1);
        } else {
            prof_formatEntry((uint8_t)(live_cpuEntry - 1u), row0);
            prof_formatEntry(live_cpuEntry, row1);
        }
        return 1u;
    }
    if (live_page == 3u) {
        static const char names[4][5] = {"    ", "pat ", "kit ", "p+k "};

        if (buttonHandler_getMode() != SELECT_MODE_PERF)
            return 0u;
        memset(row0, ' ', 16);
        memset(row1, ' ', 16);
        memcpy(row0, "RND", 3);
        memcpy(&row0[4], names[1], 4);
        memcpy(&row0[8], "dose", 4);
        row0[13] = (char)('0' + live_rndDose / 100u);
        row0[14] = (char)('0' + (live_rndDose / 10u) % 10u);
        row0[15] = (char)('0' + live_rndDose % 10u);
        memcpy(row1, "pattern", 7);
        return 1u;
    }
    if (live_page != 1u) {
        uint8_t q = live_queuedPattern();
        uint8_t cur = (uint8_t)(scene_getActiveIndex() + 1u);

        /* Stock LXR-02 screen: PATTERN page, or a pattern waiting. */
        if ((q >= 16u && live_page != 2u) ||
            buttonHandler_getMode() != SELECT_MODE_PERF)
            return 0u;
        memset(row0, ' ', 16);
        memset(row1, ' ', 16);
        memcpy(row0, "Select Pattern", 14);
        memcpy(row1, "Current: ", 9);
        /* Scene 17: the old Bank's pattern still playing after a load. */
        row1[9] = (cur > 16u) ? '-' : (char)('0' + cur / 10u);
        row1[10] = (cur > 16u) ? '-' : (char)('0' + cur % 10u);
        if (q < 16u) {
            row1[12] = '>';
            row1[14] = (char)('0' + (q + 1u) / 10u);
            row1[15] = (char)('0' + (q + 1u) % 10u);
        }
        return 1u;
    }
    if (buttonHandler_getMode() != SELECT_MODE_PERF)
        return 0u;
    kit = live_currentKit();
    memset(row0, ' ', 16);
    memset(row1, ' ', 16);
    memcpy(row0, "KIT", 3);
    if (kit < 16u) {
        row0[4] = (char)('0' + (kit + 1u) / 10u);
        row0[5] = (char)('0' + (kit + 1u) % 10u);
    }
    if (live_pendingKit < 16u) {
        row0[7] = '>';
        row0[8] = (char)('0' + (live_pendingKit + 1u) / 10u);
        row0[9] = (char)('0' + (live_pendingKit + 1u) % 10u);
    }
    /* Kit option: "suit" = the pattern brings its kit, "fixe" = kit stays. */
    memcpy(&row0[12], live_kitHold ? "fixe" : "suit", 4);
    /* Row 2: "PAT03 BK207 PLT" (PLT = Plaits engines, LXR = the kit's). */
    memcpy(row1, "PAT", 3);
    {
        uint8_t pat = (uint8_t)(scene_getActiveIndex() + 1u);
        uint16_t bank = bank_restoreBankSlot();

        row1[3] = (char)('0' + pat / 10u);
        row1[4] = (char)('0' + pat % 10u);
        if (bank < 1000u) {
            memcpy(&row1[6], "BK", 2);
            row1[8] = (char)('0' + bank / 100u);
            row1[9] = (char)('0' + (bank / 10u) % 10u);
            row1[10] = (char)('0' + bank % 10u);
        }
        memcpy(&row1[12], live_plaits ? "PLT" : "LXR", 3);
    }
    return 1u;
}
