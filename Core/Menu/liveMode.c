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
#include "DlyVerbParameters.h"

static kit_t    live_kitBackup;
static uint8_t  live_backupScene = 0xFFu;   /* Scene whose kit is swapped */
static uint16_t live_backupBank = 0xFFFFu;  /* Bank slot of that Scene    */
static uint8_t  live_kitSource = 0xFFu;     /* kit shown on the KIT page  */
static uint8_t  live_pendingKit = 0xFFu;    /* kit waiting for the bar    */
static uint8_t  live_carrier;               /* queue only carries the kit */
static uint8_t  live_page;                  /* 0 none, 1 KIT, 2 PATTERN, 3 RND, 4 CPU */
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
static uint8_t  live_rndTarget = 3u;        /* 1 pattern, 2 kit, 3 both    */
static uint8_t  live_rndScene = 0xFFu;
static uint16_t live_rndBank = 0xFFFFu;
static uint8_t  live_rndSavedAmt[INSTRUMENT_SLOT_COUNT];

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
        [DLYVERB_PARAM_RVB_ROUTE] = DLYVERB_ROUTE_FEEDBACK
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
    return (uint8_t)(live_rndDose && (live_rndTarget & 2u));
}

/* New random Morph target for slots 2..6 of `scene`. */
static void live_rndRollKit(uint8_t scene)
{
    uint8_t slot;

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
        autosave_markInstrumentMorphDirty(scene, slot);
    }
    bank_invalidateSdCleanScene(scene);
}

/* Give `scene` back the voice Morph amounts it had before RANDOM. */
static void live_rndRestoreAmounts(void)
{
    uint8_t slot;

    if (live_rndScene != 0xFFu && live_rndBank == bank_restoreBankSlot()) {
        for (slot = 1u; slot < INSTRUMENT_SLOT_COUNT; slot++)
            preset_morphVoiceScene(live_rndScene, slot, live_rndSavedAmt[slot]);
    }
    live_rndScene = 0xFFu;
}

/* Voice Morph amount = dose on slots 2..6 of `scene`. */
static void live_rndSetAmounts(uint8_t scene, uint8_t amount)
{
    uint8_t slot;

    if (live_rndScene != scene) {
        live_rndRestoreAmounts();
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
            live_rndSavedAmt[slot] = scene_getVoiceMorphAmount(scene, slot);
        live_rndScene = scene;
        live_rndBank = bank_restoreBankSlot();
    }
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
    return 1u;
}

/* A successfully loaded SD kit becomes the scene's own kit. The
 * performance substitution may no longer restore its previous backup. */
void live_kitLoadCommitted(uint16_t scene_mask)
{
    uint8_t active = scene_getActiveIndex();

    live_checkBackup();
    if (active < 16u && (scene_mask & (uint16_t)(1u << active)))
        live_pendingKit = 0xFFu;
    if (live_backupScene < 16u &&
        (scene_mask & (uint16_t)(1u << live_backupScene))) {
        live_backupScene = 0xFFu;
        live_kitSource = 0xFFu;
        live_plaitsOn = 0u;
        plaitsMode_mapOff();
    }
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
        led_setValue((uint8_t)(s < 3u && (uint8_t)(s + 1u) == live_rndTarget),
                     (uint8_t)(LED_SEQ1 + s));
    }
}

void live_rndStep(uint8_t step)
{
    if (step > 2u)
        return;
    live_rndTarget = (uint8_t)(step + 1u);
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
        }
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

uint8_t live_plaitsActive(void)
{
    return live_plaits;
}

void live_service(void)
{
    /* Kit library step LEDs follow the page (it may open a few ms late). */
    if (menu_liveKitLibraryActive() != live_libLeds) {
        if (live_libLeds)
            live_kitPageClose();
        else
            live_kitPageLeds();
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
        seq_queuedSceneTarget() >= 16u && scene_getActiveIndex() < 16u &&
        !live_plaitsSettled())
        live_kitSelect(live_currentKit());
    /* CPU page: a new snapshot every second. */
    /* CPU page fader line: repaint as soon as a fader moves one step. */
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
        memcpy(&row0[4], names[live_rndTarget & 3u], 4);
        memcpy(&row0[8], "dose", 4);
        row0[13] = (char)('0' + live_rndDose / 100u);
        row0[14] = (char)('0' + (live_rndDose / 10u) % 10u);
        row0[15] = (char)('0' + live_rndDose % 10u);
        memcpy(row1, "1pat 2kit 3p+k", 14);
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
