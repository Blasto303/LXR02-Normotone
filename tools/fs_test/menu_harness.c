/*
 * Menu harness: the real Menu, buttonHandler and filesystem on a RAM FAT32
 * card. Boots like main.c, loads a Bank, then plays a script of panel
 * gestures and prints the LCD after each one.
 *
 * usage: menu_harness card.img <bank slot> <action> ...
 *   m:N  mode button 1..4 (panel VOICE, PERF, LOAD, SAVE)
 *   s:N  SELECT 1..8 (panel OSC..MIX)      v:N  track button 1..7
 *   q:N  step button 1..16                 b:< / b:>  BAR buttons
 *   e:N  DATA encoder N detents (signed)   c    DATA click
 *   k:I:D  knob I (1..4) delta D           S+ / S-  SHIFT down / up
 *   C+ / C-  COPY/CLR down / up            w:MS  idle MS milliseconds
 *   d:B / u:B  raw button B down / up      K  print kit/effect identity
 *   Z    DATA click while a background card scan holds the filesystem
 *   O    set the active Scene's Effect to off      L  open the Load page
 *   Y    SHIFT+SAVE (config) while a Preset Kit save is in flight
 * Each action is followed by main-loop passes until storage is idle; the
 * line shows SD reads and simulated foreground time for that gesture.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "filesystem.h"
#include "SceneData.h"
#include "BankData.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "FxBuffer.h"
#include "liveMode.h"
#include "PatternData.h"
#include "buttonHandler.h"
#include "menu.h"
#include "presetManager.h"
#include "BankData.h"
#include "sequencer.h"

/* The MCU linker supplies this DTCM arena. Reserve its guaranteed minimum
 * here so selecting a real effect exercises the same allocator on the host. */
__asm__(".pushsection .bss\n.balign 32\n.globl _sfxbuf\n_sfxbuf:\n"
        ".space 0x1e000\n.globl _efxbuf\n_efxbuf:\n.popsection");

extern int ram_load(const char *);
extern unsigned long sd_reads, sd_writes;
extern unsigned long long sim_us;
extern void sim_advance(unsigned int us);

/* Minimal scene-queue model of the sequencer: R starts the transport, B is a
 * bar boundary (fires a queued change), a stopped transport fires at once. */
static int h_running, h_queue, h_fire;
static uint8_t h_target;
uint8_t seq_isRunning(void) { return (uint8_t)h_running; }
void seq_queueSceneChange(uint8_t scene, uint8_t div)
{
    (void)div; h_target = scene; h_queue = 1; if (!h_running) h_fire = 1;
}
void seq_requestSceneChange(uint8_t scene, uint8_t div)
{
    (void)div; h_target = scene; h_queue = 0; h_fire = 1;
}
uint8_t seq_sceneChangeFirePending(void) { return (uint8_t)h_fire; }
uint8_t seq_pendingSceneIsQueue(void) { return (uint8_t)h_queue; }
uint8_t seq_pendingSceneTarget(void) { return h_target; }
uint8_t seq_queuedSceneTarget(void) { return h_queue ? h_target : 0xFFu; }
uint8_t seq_pendingSceneIsChain(void) { return 0u; }
void seq_clearPendingSceneChange(void) { h_fire = 0; h_queue = 0; }

static void loop_pass(void)
{
    buttonHandler_processEvents();
    buttonHandler_tick();
    buttonHandler_processEvents();
    menu_pollPresetStatus();
    preset_morphTick();
    filesystem_tick();
    preset_tickBackgroundBankLoad();
    menu_serviceKnobRepaint();
    menu_serviceRuntimeWidgets();
    effects_service();   /* the mixer runs it every render block */
    sim_advance(50); /* foreground bookkeeping per pass */
}

static void settle(void)
{
    /* Run until storage and Preset are idle for 20 consecutive passes. */
    int quiet = 0;
    long guard = 0;

    while (quiet < 20 && guard++ < 2000000) {
        loop_pass();
        if (filesystem_status() == FS_STATUS_BUSY ||
            preset_getStatus() != PRESET_IDLE ||
            menu_loadInstrumentTransactionBusy() ||
            menu_isStorageBusy())
            quiet = 0;
        else
            quiet++;
    }
}

static void show(const char *what, unsigned long r0, unsigned long long t0)
{
    printf("%-8s |%-16.16s|%-16.16s| reads=%-4lu fg=%6.1f ms bank=%03u\n", what,
           editDisplayBuffer[0], editDisplayBuffer[1], sd_reads - r0,
           (sim_us - t0) / 1000.0, (unsigned)bank_restoreBankSlot());
}

static void press(uint8_t b) { buttonHandler_buttonPressed(b); loop_pass(); }
static void release(uint8_t b) { buttonHandler_buttonReleased(b); loop_pass(); }
static void tap(uint8_t b) { press(b); release(b); }

int main(int argc, char **argv)
{
    static const uint8_t mode_btn[5] = {0, BUT_MODE1, BUT_MODE2, BUT_MODE3, BUT_MODE4};
    static const uint8_t sel_btn[9] = {0, BUT_SELECT1, BUT_SELECT2, BUT_SELECT3,
        BUT_SELECT4, BUT_SELECT5, BUT_SELECT6, BUT_SELECT7, BUT_SELECT8};
    static const uint8_t voice_btn[8] = {0, BUT_VOICE_1, BUT_VOICE_2, BUT_VOICE_3,
        BUT_VOICE_4, BUT_VOICE_5, BUT_VOICE_6, BUT_VOICE_7};
    static const uint8_t seq_btn[17] = {0, BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4,
        BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8, BUT_SEQ9, BUT_SEQ10, BUT_SEQ11,
        BUT_SEQ12, BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16};
    uint8_t enc_btn = 0u;

    if (argc < 3 || ram_load(argv[1])) {
        printf("usage: menu_harness card.img bank actions...\n");
        return 1;
    }
    fxbuf_init();
    scene_initAll(); bank_init(); instrumentManager_runtimeInit(); effects_init();
    if (!filesystem_initCardAndMountBlocking()) { printf("mount failed\n"); return 1; }
    filesystem_requestScanKits(NULL); settle(); filesystem_ack();
    (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT);
    filesystem_requestScanScenes(NULL); settle(); filesystem_ack();
    (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_SCENE);
    filesystem_requestScanBanks(NULL); settle(); filesystem_ack();
    (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_BANK);
    filesystem_requestScanPatterns(NULL); settle(); filesystem_ack();
    (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_PATTERN);
    (void)filesystem_createBootIndexBlocking();
    filesystem_requestLoadBankIndex(NULL); settle(); filesystem_ack();
    filesystem_requestLoadBank((uint16_t)atoi(argv[2]), 0xFFFFu, 0u, 0x03u, NULL);
    settle(); filesystem_ack();
    if (argc > 3 && !strcmp(argv[3], "--rumble"))
        live_initFactoryRumble();
    effects_activateScene(scene_getActiveIndex());
    menu_init();
    preset_syncBusCompMirrors();
    settle();
    show("boot", sd_reads, sim_us);

    for (int a = argc > 3 && !strcmp(argv[3], "--rumble") ? 4 : 3; a < argc; a++) {
        const char *s = argv[a];
        unsigned long r0 = sd_reads;
        unsigned long long t0 = sim_us;
        int n = s[1] == ':' ? (int)strtol(s + 2, NULL, 10) : 0;

        switch (s[0]) {
        case 'm': tap(mode_btn[n]); break;
        case 's': tap(sel_btn[n]); break;
        case 'v': tap(voice_btn[n]); break;
        case 'q': tap(seq_btn[n]); break;
        case 'b': tap(s[2] == '<' ? BUT_BAR1 : BUT_BAR2); break;
        case 'S': if (s[1] == '+') press(BUT_SHIFT); else release(BUT_SHIFT); break;
        case 'C': if (s[1] == '+') press(BUT_COPY); else release(BUT_COPY); break;
        case 'e': {
            int step = n < 0 ? -1 : 1;
            for (int i = 0; i != n; i += step) { menu_parseEncoder((int8_t)step, enc_btn); loop_pass(); }
            break; }
        case 'c':
            enc_btn = 1u; menu_parseEncoder(0, enc_btn); loop_pass();
            enc_btn = 0u; menu_parseEncoder(0, enc_btn); loop_pass();
            break;
        case 'k': {
            int knob = s[2] - '0';
            int d = (int)strtol(s + 4, NULL, 10);
            menu_parseKnobDelta((uint8_t)(knob - 1), (int8_t)d); loop_pass();
            break; }
        case 'd': press((uint8_t)atoi(s + 2)); break;
        case 'u': release((uint8_t)atoi(s + 2)); break;
        case 'R': h_running = !h_running; break;
        case 'L': menu_switchPage(LOAD_PAGE); break;
        case 'y':
            press(BUT_SHIFT); tap(BUT_MODE4); release(BUT_SHIFT);
            break;
        case 'Y':
            /* SHIFT+SAVE (config) while a Preset Kit save is in flight. */
            filesystem_ack(); if (getenv("Y_PAT")) preset_savePattern(50u); else preset_saveDrumset(100u, 0u, scene_getActiveIndex());
            printf("  preset status before %u\n", preset_getStatus());
            if (getenv("Y_PERF")) tap(BUT_MODE2);
            else { press(BUT_SHIFT); tap(BUT_MODE4); release(BUT_SHIFT); }
            printf("  preset status after %u page %u\n", preset_getStatus(), menu_activePage);
            break;
        case 'O': {
            /* Set the active Scene's Effect to off (record + runtime). */
            scene_t *sc = scene_get(scene_getActiveIndex());
            if (sc) effects_recordDefaultsForType(&sc->effect, EFFECT_TYPE_OFF);
            effects_activateScene(scene_getActiveIndex());
            break; }
        case 'Z':
            /* DATA click while the card is busy (a background scan runs). */
            filesystem_ack(); filesystem_requestScanKits(NULL);
            enc_btn = 1u; menu_parseEncoder(0, enc_btn);
            enc_btn = 0u; menu_parseEncoder(0, enc_btn);
            break;
        case 'B': if (h_queue) h_fire = 1; break;
        case 'K': {
            /* Which resident Scene's kit / effect equals the active one. */
            uint8_t a = scene_getActiveIndex(), i;
            printf("  active=%u kit=", a);
            for (i = 0; i < 16; i++)
                if (scene_getConst(i) && !memcmp(&scene_getConst(i)->kit,
                        &scene_getConst(a)->kit, sizeof(kit_t))) printf("%u ", i);
            printf(" fx-same-as-active:");
            for (i = 0; i < 16; i++)
                if (scene_effectConst(i) && !memcmp(scene_effectConst(i),
                        scene_effectConst(a), sizeof(effect_record_t))) printf("%u ", i);
            { extern uint8_t mixer_audioRouting[6]; printf(" route="); for (i = 0; i < 6; i++) printf("%u", mixer_audioRouting[i]); printf(" scene_out="); for (i = 0; i < 6; i++) printf("%u", scene_getVoiceAudioOut(a, i)); printf(" types="); for (i = 0; i < 6; i++) printf("%u", scene_instrumentSlotConst(a, i)->type); }
            printf(" vm2=%u vm1=%u rnd=%u", scene_getVoiceMorphAmount(a, 1), scene_getVoiceMorphAmount(a, 0), seq_liveRndDose);
            printf(" send1=%u k0=%02x%02x%02x%02x k4=%02x%02x%02x%02x\n", scene_getVoiceFxSendAmount(a, 0),
                ((const uint8_t*)&scene_getConst(a)->kit)[8],((const uint8_t*)&scene_getConst(a)->kit)[9],((const uint8_t*)&scene_getConst(a)->kit)[40],((const uint8_t*)&scene_getConst(a)->kit)[41],
                ((const uint8_t*)&scene_getConst(4)->kit)[8],((const uint8_t*)&scene_getConst(4)->kit)[9],((const uint8_t*)&scene_getConst(4)->kit)[40],((const uint8_t*)&scene_getConst(4)->kit)[41]);
            break; }
        case 'F': {
            /* Effect probe: rows 3.. of the active Scene record vs runtime. */
            uint8_t a = scene_getActiveIndex(), i;
            const effect_record_t *r = scene_effectConst(a);
            printf("  fx scene %u runtime-scene %u type %u mrp %u\n   row:", a, effects_runtimeScene(), r ? r->type : 99, scene_getEffectMorphAmount(a));
            for (i = 3; i < 19; i++) printf(" %3u", i);
            printf("\n   nrm:"); for (i = 3; i < 19; i++) printf(" %3u", r ? r->normal[i] : 0);
            printf("\n   mrp:"); for (i = 3; i < 19; i++) printf(" %3u", r ? r->morph[i] : 0);
            printf("\n   run:"); for (i = 3; i < 19; i++) printf(" %3u", effects_lastApplied(i));
            printf("\n");
            break; }
        case 'P': {
            /* Pattern probe: bar 1 of every track of the active Scene. */
            uint8_t a = scene_getActiveIndex(), t, st;
            const pat_scene_region_t *r = pat_sceneRegion(a);
            for (t = 0; t < 7; t++) {
                printf("  trk%u len%3u ", t + 1, r ? r->track_length[t] : 0);
                for (st = 0; st < 16; st++) putchar(pat_isStepActive(t, st, a) ? 'x' : '.');
                putchar('\n');
            }
            break; }
        case 'w':
            for (unsigned long long end = sim_us + 1000ull * n; sim_us < end;) loop_pass();
            break;
        default: printf("unknown action %s\n", s); continue;
        }
        settle();
        show(s, r0, t0);
    }
    return 0;
}
