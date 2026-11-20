/*
 * CPU bench: the real DSP render (mixer_calcNextSampleBlock, voices, Effect,
 * bus compressor) of every Scene of a Bank, on the host.
 *
 * usage: bench card.img <bank slot> [blocks]
 *
 * For each Scene: the Scene is activated as a PERF change does, then the
 * render runs `blocks` 32-frame blocks while all seven tracks retrigger every
 * 24 blocks (about every 17 ms, harder than any 16th-note groove). Printed:
 *   all   : full render (kit + Effect as stored), mean and worst block
 *   nofx  : same with the Effect switched off
 *   v1..v6: one voice alone, Effect off (cost of each instrument)
 * Mean and 99th percentile. Times are host microseconds; the columns that matter are the ratios.
 * Calibrate against the device `cpu` widget (see CPU_BENCH.md).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "filesystem.h"
#include "SceneData.h"
#include "BankData.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "buttonHandler.h"
#include "menu.h"
#include "presetManager.h"
#include "mixer.h"
#include "MidiVoiceControl.h"
#include "modulationNode.h"
#include "liveMode.h"
#include "plaitsMode.h"
#include "DlyVerbParameters.h"
#include "storageTypes.h"
#include "PatternData.h"

/* The DTCM FX/voice arena of the device (126,592 B, .dtcm_fxbuf). */
__asm__(".globl _sfxbuf\n.globl _efxbuf\n.globl _fxbuf_size\n"
        ".section .bss\n.balign 32\n_sfxbuf:\n.space 126592\n_efxbuf:\n"
        ".set _fxbuf_size, 126592\n.text\n");
extern void initRng(void);
extern void fxbuf_init(void);
extern void mixer_init(void);
extern void parameterArray_init(void);

extern int ram_load(const char *);
extern void sim_advance(unsigned int us);

static void loop_pass(void)
{
    menu_pollPresetStatus();
    preset_morphTick();
    filesystem_tick();
    preset_tickBackgroundBankLoad();
    sim_advance(50);
}

static void settle(void)
{
    int quiet = 0;
    long guard = 0;

    while (quiet < 20 && guard++ < 2000000) {
        loop_pass();
        if (filesystem_status() == FS_STATUS_BUSY ||
            preset_getStatus() != PRESET_IDLE || !preset_applyWorkersIdle())
            quiet = 0;
        else
            quiet++;
    }
}

/* Sequencer is stubbed: no Scene queued (LIVE kit choices apply at once). */
uint8_t seq_queuedSceneTarget(void) { return 0xFFu; }

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static sample_mx_t outA[64] __attribute__((aligned(16)));
static sample_mx_t outB[64] __attribute__((aligned(16)));

/* Render `blocks` blocks, retriggering the voices in `mask` every 24. */
static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Mean and 99th percentile (host scheduler spikes would make max useless). */
static int trig_every = 24;
static FILE *dump;
extern uint8_t mixer_sleepEnabled;
extern float slider_vol[];
static void run(int blocks, uint8_t mask, double *mean, double *worst)
{
    static double dts[20000];
    int n = 0;
    double sum = 0.0;
    int b, v;

    for (b = 0; b < blocks; b++) {
        double t0, dt;

        if (b % trig_every == 0)
            for (v = 0; v < 7; v++)
                if (mask & (1u << v)) {
                    voiceControl_noteOff((uint8_t)v);
                    voiceControl_noteOn((uint8_t)v, 60, 127);
                }
        voiceControl_processPending();
        t0 = now_us();
        mixer_calcNextSampleBlock(outA, outB);
        dt = now_us() - t0;
        if (dump) { fwrite(outA, sizeof(outA[0]), 64, dump); fwrite(outB, sizeof(outB[0]), 64, dump); }
        if (b >= 24 && n < 20000) { sum += dt; dts[n++] = dt; }
        loop_pass();
    }
    qsort(dts, (size_t)n, sizeof(double), cmpd);
    *mean = n ? sum / n : 0.0;
    *worst = n ? dts[(n * 99) / 100] : 0.0;
}

/*
 * Play the Scene's own pattern at 130 BPM (159 blocks per 16th) for `blocks`
 * blocks: every active step triggers its track (conditions and velocities
 * ignored, so a little denser than the real groove).
 */
static void run_pattern(uint8_t sc, int blocks)
{
    const pat_scene_region_t *r = pat_sceneRegion(sc);
    int b, t;
    int step = -1;

    /* BENCH_CHANGE="block:key=val;block:key=val": Effect row moves mid-run. */
    int change_at[8]; uint8_t change_idx[8], change_val[8]; int nchg = 0, ci;
    if (getenv("BENCH_CHANGE")) {
        char buf[256]; char *tok, *save = NULL;
        strncpy(buf, getenv("BENCH_CHANGE"), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
        for (tok = strtok_r(buf, ";", &save); tok && nchg < 8; tok = strtok_r(NULL, ";", &save)) {
            char *c = strchr(tok, ':'), *e = c ? strchr(c, '=') : NULL;
            if (!c || !e) continue;
            *c = 0; *e = 0;
            if (!effects_descriptorByKey(scene_effectConst(sc)->type, c + 1, &change_idx[nchg]))
                continue;
            change_at[nchg] = atoi(tok); change_val[nchg] = (uint8_t)atoi(e + 1);
            nchg++;
        }
    }
    for (b = 0; b < blocks; b++) {
        for (ci = 0; ci < nchg; ci++)
            if (b == change_at[ci])
                scene_setEffectNormalParameter(sc, change_idx[ci], change_val[ci]);
        if (b % 159 == 0) {
            step++;
            for (t = 0; t < 7; t++) {
                int len = (r && r->track_length[t]) ? r->track_length[t] : 16;

                if (pat_isStepActive((uint8_t)t, (uint8_t)(step % len), sc)) {
                    voiceControl_noteOff((uint8_t)t);
                    voiceControl_noteOn((uint8_t)t, 60, 127);
                }
            }
        }
        voiceControl_processPending();
        mixer_calcNextSampleBlock(outA, outB);
        if (dump) { fwrite(outA, sizeof(outA[0]), 64, dump); fwrite(outB, sizeof(outB[0]), 64, dump); }
        loop_pass();
    }
}

/* Wrappers named for callgrind (noinline: one entry per category). */
static volatile int bench_tag;
__attribute__((noinline)) static void render_steady(void) { bench_tag = 0; mixer_calcNextSampleBlock(outA, outB); }
__attribute__((noinline)) static void render_after_param(void) { bench_tag = 1; mixer_calcNextSampleBlock(outA, outB); }
__attribute__((noinline)) static void render_after_scene(void) { bench_tag = 2; mixer_calcNextSampleBlock(outA, outB); }
__attribute__((noinline)) static void render_after_rnd(void) { bench_tag = 3; mixer_calcNextSampleBlock(outA, outB); }
__attribute__((noinline)) static void fg_loop(void) { loop_pass(); }
__attribute__((noinline)) static void ev_param(uint8_t idx, uint8_t val)
{
    scene_setEffectNormalParameter(scene_getActiveIndex(), idx, val);
}
__attribute__((noinline)) static void ev_scene(uint8_t s)
{
    live_beforeSceneChange(s);
    scene_selectActive(s);
    bank_selectActiveSceneForEditMask(s);
    preset_startDrumsetApply();
}
__attribute__((noinline)) static void ev_rnd(void)
{
    live_rndStep(1);          /* kit */
    live_encoder(16, 0);      /* dose 64 */
    live_encoder(0, 1);       /* new draw */
}

__attribute__((noinline)) static void pm_cold(kit_t *t, const kit_t *k) { plaitsMode_convertKit(t, k); plaitsMode_mapOn(k); }
__attribute__((noinline)) static void pm_warm(kit_t *t, const kit_t *k) { plaitsMode_convertKit(t, k); plaitsMode_mapOn(k); }
__attribute__((noinline)) static uint8_t pm_step(void) { return plaitsMode_service(); }

static void activate(uint8_t s)
{
    scene_selectActive(s);
    bank_selectActiveSceneForEditMask(s);
    preset_startDrumsetApply();
    preset_applySceneSettings(s);
    effects_activateScene(s);
    settle();
}

/* Kit equality without the runtime Morph interpolation image. */
static int kit_same(const kit_t *a, const kit_t *b)
{
    int k;

    if (memcmp(&a->settings, &b->settings, sizeof(a->settings)))
        return 0;
    for (k = 0; k < 6; k++)
        if (a->instruments[k].type != b->instruments[k].type ||
            memcmp(a->instruments[k].parameter_images.instrument_parameters,
                   b->instruments[k].parameter_images.instrument_parameters, 64) ||
            memcmp(a->instruments[k].parameter_images.morph_instrument_parameters,
                   b->instruments[k].parameter_images.morph_instrument_parameters, 64))
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    int blocks = (argc > 3) ? atoi(argv[3]) : 600;
    uint8_t s, v;

    if (argc < 3 || ram_load(argv[1])) {
        printf("usage: bench card.img bank [blocks]\n");
        return 1;
    }
    scene_initAll(); bank_init();
    initRng(); fxbuf_init(); instrumentManager_runtimeInit(); effects_init();
    mixer_init(); parameterArray_init();
    for (v = 0; v < 6; v++)
        slider_vol[v] = 0.8f;   /* the six sliders (ADC is not simulated) */
    if (getenv("BENCH_SLIDERS")) {      /* BENCH_SLIDERS="0.8,0.8,0.8,0.8,0,0" */
        char *p = getenv("BENCH_SLIDERS");
        for (v = 0; v < 6 && p; v++) {
            slider_vol[v] = (float)atof(p);
            p = strchr(p, ',');
            if (p) p++;
        }
    }
    for (v = 0; v < 6; v++)
        modNode_init(&velocityModulators[v]);
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
    menu_init();
    settle();
    for (v = 0; v < 50 && bank_scenePresentMask() != 0xFFFFu; v++)
        settle();
    printf("present mask %04x fx types:", bank_scenePresentMask());
    for (v = 0; v < 16; v++)
        printf(" %u", scene_effectConst(v) ? scene_effectConst(v)->type : 99);
    printf("\n");

    /*
     * Chain demo: bench card bank blocks_per_pattern chain 1,5,9 [plaits]
     * plays the patterns in turn through the LIVE pattern change (the
     * Effect and its tails carry over), writing BENCH_DUMP.
     */
    if (argc > 5 && !strcmp(argv[4], "chain")) {
        char list[128], *tok, *save = NULL;
        int first = 1, g;

        if (getenv("BENCH_DUMP"))
            dump = fopen(getenv("BENCH_DUMP"), "wb");
        strncpy(list, argv[5], sizeof(list) - 1);
        list[sizeof(list) - 1] = 0;
        for (tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            uint8_t sc = (uint8_t)(atoi(tok) - 1);

            if (first) {
                activate(sc);
                if (argc > 6 && !strcmp(argv[6], "plaits")) {
                    live_plaitsToggle();
                    for (g = 0; g < 50; g++) { live_service(); settle(); }
                }
                first = 0;
            } else {
                ev_scene(sc);
                activate(sc);
                for (g = 0; g < 4; g++) { live_service(); settle(); }
            }
            printf("pattern %u types", sc + 1);
            for (v = 0; v < 6; v++) printf(" %u", scene_instrumentSlotConst(sc, v)->type);
            printf("\n");
            run_pattern(sc, blocks);
        }
        return 0;
    }

    /* PLAITS scenario: bench card bank 1 plaits */
    if (argc > 4 && !strcmp(argv[4], "plaits")) {
        uint8_t sc;
        int g, k;
        static kit_t orig[16];

        for (sc = 0; sc < 16; sc++)
            orig[sc] = scene_getConst(sc)->kit;
#define TYPES(sc_) do { printf("  scene %2u:", (sc_) + 1); \
            for (k = 0; k < 6; k++) printf(" %u", scene_instrumentSlotConst((sc_), k)->type); \
            printf("  own kit %s\n", kit_same(&scene_getConst(sc_)->kit, &orig[sc_]) ? "intact" : "CHANGED"); } while (0)
#define PUMP() for (g = 0; g < 60; g++) { live_service(); settle(); }
        activate(0);
        printf("start\n"); TYPES(0);
        {
            const kit_t *a = &scene_getConst(0)->kit, *b = &orig[0];
            if (memcmp(&a->settings, &b->settings, sizeof(a->settings))) printf("  settings differ\n");
            for (k = 0; k < 6; k++) if (a->instruments[k].type != b->instruments[k].type) printf("  type %d: %u vs %u\n", k, a->instruments[k].type, b->instruments[k].type);
            printf("  kit_same %d sizeof settings %u\n", kit_same(a, b), (unsigned)sizeof(a->settings));
            for (k = 0; k < 6; k++) for (g = 0; g < 64; g++) {
                if (a->instruments[k].parameter_images.instrument_parameters[g] != b->instruments[k].parameter_images.instrument_parameters[g])
                    printf("  slot %d row %d main %u -> %u\n", k, g, b->instruments[k].parameter_images.instrument_parameters[g], a->instruments[k].parameter_images.instrument_parameters[g]);
                if (a->instruments[k].parameter_images.morph_instrument_parameters[g] != b->instruments[k].parameter_images.morph_instrument_parameters[g])
                    printf("  slot %d row %d morph %u -> %u\n", k, g, b->instruments[k].parameter_images.morph_instrument_parameters[g], a->instruments[k].parameter_images.morph_instrument_parameters[g]);
            }
        }
        live_plaitsToggle(); PUMP();
        printf("PLAITS on\n"); TYPES(0);
        {
            static const uint8_t tests[][3] = {   /* slot, Drum row, value */
                {0, 1, 40}, {0, 1, 4}, {0, 7, 100}, {0, 12, 43}, {0, 13, 103},
                {0, 8, 90}, {0, 17, 100}, {0, 18, 20}, {3, 1, 40}, {5, 1, 74}, {4, 17, 43} };
            unsigned t;

            for (t = 0; t < sizeof(tests) / 3; t++) {
                uint16_t id = instrumentParam_make(tests[t][0], tests[t][1]);
                uint8_t val = tests[t][2];
                const ParamDescriptor *sd = instrumentManager_descriptor(orig[0].instruments[tests[t][0]].type, tests[t][1]);
                uint8_t ok = plaitsMode_remap(&id, &val);
                const ParamDescriptor *dd = ok ? instrumentManager_descriptor(scene_instrumentSlotConst(0, tests[t][0])->type, instrumentParam_local(id)) : 0;

                printf("  p-lock slot %u %-20s %3u -> %s %u\n", tests[t][0], sd ? sd->file_key : "?", tests[t][2],
                       ok ? (dd ? dd->file_key : "?") : "(dropped)", ok ? val : 0);
            }
        }
        ev_scene(4); activate(4); PUMP();
        printf("pattern 5\n"); TYPES(4); TYPES(0);
        live_kitPageOpen(); live_kitSelect(8); PUMP();
        printf("KIT 9 on pattern 5 (Plaits)\n"); TYPES(4); TYPES(8);
        live_encoder(1, 0);  /* fixe */
        ev_scene(6); activate(6); PUMP();
        printf("fixe, pattern 7\n"); TYPES(6); TYPES(4);
        live_encoder(-1, 0); /* suit */
        live_plaitsToggle(); PUMP();
        printf("PLAITS off\n"); TYPES(6);
        ev_scene(1); activate(1); PUMP();
        printf("pattern 2\n"); TYPES(1); TYPES(6);
        for (sc = 0; sc < 16; sc++)
            if (!kit_same(&scene_getConst(sc)->kit, &orig[sc]) && sc != 1)
                printf("  scene %u kit differs from the card!\n", sc + 1);
        printf("map active %u\n", plaitsMode_mapActive);
        return 0;
    }
    /* Loaded kits: Morph rows that differ from normal. bench card bank blocks morphload */
    if (argc > 4 && !strcmp(argv[4], "morphload")) {
        uint8_t sc;

        for (sc = 0u; sc < 4u; sc++) {
            printf("scene %u rows morph != normal:", sc + 1);
            for (v = 0; v < 6; v++) {
                const kit_instrument_slot_t *in = scene_instrumentSlotConst(sc, v);
                int i, n = 0;

                for (i = 0; in && i < INSTRUMENT_PARAM_COUNT; i++)
                    n += in->parameter_images.morph_instrument_parameters[i] !=
                         in->parameter_images.instrument_parameters[i];
                printf(" %2d", n);
            }
            printf("\n");
        }
        return 0;
    }
    /* Effect edit at full Effect Morph: bench card bank blocks fxlink. */
    if (argc > 4 && !strcmp(argv[4], "fxlink")) {
        live_initFactoryRumble();
        effects_activateScene(0); settle(); effects_service();
        scene_setEffectMorphAmount(0, 255); settle(); effects_service();
        printf("fxm 255, lpf runtime before: %u\n", effects_lastApplied(DLYVERB_PARAM_LOWPASS));
        effects_setParameter(0, DLYVERB_PARAM_LOWPASS, EFFECT_IMAGE_NORMAL, 90); settle(); effects_service();
        printf("after a normal edit to 90: runtime %u (normal %u, morph %u)\n",
               effects_lastApplied(DLYVERB_PARAM_LOWPASS),
               scene_effectConst(0)->normal[DLYVERB_PARAM_LOWPASS],
               scene_effectConst(0)->morph[DLYVERB_PARAM_LOWPASS]);
        effects_setParameter(0, DLYVERB_PARAM_LOWPASS, EFFECT_IMAGE_MORPH, 30); settle(); effects_service();
        effects_setParameter(0, DLYVERB_PARAM_LOWPASS, EFFECT_IMAGE_NORMAL, 60); settle(); effects_service();
        printf("designed morph 30, normal edit to 60: normal %u morph %u runtime %u\n",
               scene_effectConst(0)->normal[DLYVERB_PARAM_LOWPASS],
               scene_effectConst(0)->morph[DLYVERB_PARAM_LOWPASS],
               effects_lastApplied(DLYVERB_PARAM_LOWPASS));
        return 0;
    }
    /* Starting Morph check: bench card bank blocks morphcheck. */
    if (argc > 4 && !strcmp(argv[4], "morphcheck")) {
        static kit_t k, pk;
        int pass;

        k = scene_getConst(0)->kit;
        plaitsMode_convertKit(&pk, &k);
        for (pass = 0; pass < 2; pass++) {
            kit_t *kk = pass ? &pk : &k;

            for (v = 0; v < 6; v++) {
                kit_instrument_slot_t *in = &kk->instruments[v];
                const instrument_registry_entry_t *e = instrumentManager_registryEntry(in->type);
                int i;

                memcpy(in->parameter_images.morph_instrument_parameters,
                       in->parameter_images.instrument_parameters, INSTRUMENT_PARAM_COUNT);
                storage_instrumentStartMorph((storage_instrument_type_t)in->type, in);
                printf("type %u:", in->type);
                for (i = 0; e && i < e->descriptor_count; i++)
                    if (in->parameter_images.morph_instrument_parameters[i] !=
                        in->parameter_images.instrument_parameters[i])
                        printf(" %s %u>%u", e->descriptors[i].file_key,
                               in->parameter_images.instrument_parameters[i],
                               in->parameter_images.morph_instrument_parameters[i]);
                printf("\n");
                /* second call: endpoints now differ, must not change */
                {
                    uint8_t before[INSTRUMENT_PARAM_COUNT];
                    memcpy(before, in->parameter_images.morph_instrument_parameters, sizeof(before));
                    storage_instrumentStartMorph((storage_instrument_type_t)in->type, in);
                    if (memcmp(before, in->parameter_images.morph_instrument_parameters, sizeof(before)))
                        printf("  second call changed it (BAD)\n");
                }
            }
        }
        return 0;
    }
    /* PLAITS cold build cost: bench card bank blocks pmcold (under callgrind). */
    if (argc > 4 && !strcmp(argv[4], "pmcold")) {
        static kit_t tmp, own;
        int n = 0;

        activate(0);
        own = scene_getConst(0)->kit;
        if (getenv("PM_STEPS")) {           /* background path, one table per call */
            while (!pm_step()) n++;
            printf("service steps %d\n", n);
        } else {
            pm_cold(&tmp, &own);
            pm_warm(&tmp, &own);
            printf("cold/warm done\n");
        }
        return 0;
    }
    /* RANDOM restore check: bench card bank blocks rndtest. */
    if (argc > 4 && !strcmp(argv[4], "rndtest")) {
        static kit_t own0, own4;
        int g;
#define PUMPR() for (g = 0; g < 60; g++) { live_service(); settle(); }
        activate(0); PUMPR();
        own0 = scene_getConst(0)->kit;
        own4 = scene_getConst(4)->kit;
        live_rndPageOpen();
        ev_rnd(); PUMPR();
        printf("rnd on, scene 1 morph changed: %s\n",
               kit_same(&scene_getConst(0)->kit, &own0) ? "no (BAD)" : "yes");
        live_beforeProjectSave();
        printf("before save, scene 1 kit = own: %s\n",
               kit_same(&scene_getConst(0)->kit, &own0) ? "yes" : "NO");
        live_afterProjectSave(); PUMPR();
        printf("after save, random back: %s\n",
               kit_same(&scene_getConst(0)->kit, &own0) ? "no (BAD)" : "yes");
        live_kitPageOpen(); live_kitSelect(8); PUMPR();
        live_kitSelect(0); PUMPR();
        live_kitPageClose();
        ev_scene(4); PUMPR();
        printf("after lend + pattern change, scene 1 kit = own: %s\n",
               kit_same(&scene_getConst(0)->kit, &own0) ? "yes" : "NO");
        ev_scene(0); PUMPR();
        printf("back on 1 (new draw), scene 5 kit = own: %s\n",
               kit_same(&scene_getConst(4)->kit, &own4) ? "yes" : "NO");
        live_rndStep(0); PUMPR();   /* pattern only: kit random off */
        printf("kit random off, scene 1 kit = own: %s\n",
               kit_same(&scene_getConst(0)->kit, &own0) ? "yes" : "NO");
        return 0;
    }
    /* Peak mode: bench card bank blocks peaks. Run under callgrind; each
     * block is rendered by the wrapper named after the event before it. */
    if (argc > 4 && !strcmp(argv[4], "peaks")) {
        int b;
        uint8_t sc = 0;

        activate(0);
        live_rndPageOpen();
        for (b = 0; b < blocks; b++) {
            int phase = b % 64;
            int after = 0;

            if (b % 24 == 0)
                for (v = 0; v < 7; v++) {
                    voiceControl_noteOff(v);
                    voiceControl_noteOn(v, 60, 127);
                }
            voiceControl_processPending();
            if (phase == 8) {               /* rumble parameter edit */
                uint8_t idx = (uint8_t)(3 + (b / 64) % 16);
                ev_param(idx, (uint8_t)((b * 37) & 127));
                after = 1;
            } else if (phase == 24) {       /* pattern change (LIVE path) */
                sc = (uint8_t)((sc + 5) & 15);
                ev_scene(sc);
                after = 2;
            } else if (phase == 40) {       /* RANDOM kit draw, dose 64 */
                ev_rnd();
                after = 3;
            }
            for (v = 0; v < 4; v++) {
                if (after == 1) render_after_param();
                else if (after == 2) render_after_scene();
                else if (after == 3) render_after_rnd();
                else render_steady();
                fg_loop();
                after = (v == 0) ? after : 0;
            }
        }
        printf("peaks done\n");
        return 0;
    }
    /* Profile mode: bench card bank blocks <scene 1..16> <fx 0|1> [voice mask]
     * renders one Scene only (run it under callgrind, see CPU_BENCH.md). */
    if (argc > 5) {
        uint8_t sc = (uint8_t)(atoi(argv[4]) - 1);
        uint8_t mask = (uint8_t)((argc > 6) ? strtol(argv[6], NULL, 0) : 0x7F);

        if (getenv("BENCH_DUMP"))
            dump = fopen(getenv("BENCH_DUMP"), "wb");
        if (argc > 7)
            trig_every = atoi(argv[7]);
        if (getenv("BENCH_DUMP"))
            dump = fopen(getenv("BENCH_DUMP"), "wb");
        if (getenv("BENCH_LEVEL")) {
            extern int16_t mixer_sleepLevel;
            mixer_sleepLevel = (int16_t)atoi(getenv("BENCH_LEVEL"));
        }
        if (getenv("BENCH_NOSLEEP"))
            mixer_sleepEnabled = 0u;
        double m, w;

        activate(sc);
        if (getenv("BENCH_PLAITS") && *getenv("BENCH_PLAITS")) {
            /* PLAITS mode: the kit is converted, the pattern stays. */
            int g;

            if (atoi(getenv("BENCH_PLAITS")) == 2)
                while (!plaitsMode_service()) {}   /* tables built at boot */
            live_plaitsToggle();
            for (g = 0; g < 50; g++) {
                live_service();
                settle();
            }
            printf("plaits: slot types");
            for (v = 0; v < 6; v++)
                printf(" %u", scene_instrumentSlotConst(sc, v)->type);
            printf("\n");
        }
        /* BENCH_OUT="slot:route,...": per-voice output (0 St1, 1 St2, 2 L1, 3 R1, 4 L2, 5 R2). */
        if (getenv("BENCH_OUT") && *getenv("BENCH_OUT")) {
            char buf[128], *tok, *save = NULL;

            strncpy(buf, getenv("BENCH_OUT"), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
                uint8_t sl = (uint8_t)atoi(tok);
                char *c = strchr(tok, ':');

                if (!c || sl > 5) continue;
                scene_setVoiceAudioOut(sc, sl, (uint8_t)atoi(c + 1));
                (void)preset_applyKitAudioRouting(sc, sl);
            }
            settle();
            printf("routing:");
            for (v = 0; v < 6; v++) printf(" %u", mixer_audioRouting[v]);
            printf("\n");
        }
        /* BENCH_DVB=1: the factory dark rumble on every Scene. */
        if (getenv("BENCH_DVB")) {
            live_initFactoryRumble();
            effects_activateScene(sc);
            settle();
        }
        /* BENCH_DST=1: the Scene's Effect becomes `dst` (defaults). */
        if (getenv("BENCH_DST")) {
            static effect_record_t rec;

            rec = *scene_effectConst(sc);
            effects_recordDefaultsForType(&rec, EFFECT_TYPE_DESTROY);
            (void)scene_commitEffectRecord(sc, &rec);
            effects_activateScene(sc);
            settle();
        }
        /* BENCH_FX="key=val,...": Effect rows by storage key; BENCH_COMP=amount. */
        if (getenv("BENCH_FX") && *getenv("BENCH_FX")) {
            char buf[512], *tok, *save = NULL;

            strncpy(buf, getenv("BENCH_FX"), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = 0;
            for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
                char *eq = strchr(tok, '=');
                uint8_t idx;

                if (!eq) continue;
                *eq = 0;
                if (effects_descriptorByKey(scene_effectConst(sc)->type, tok, &idx))
                    scene_setEffectNormalParameter(sc, idx, (uint8_t)atoi(eq + 1));
                else
                    printf("unknown fx key %s\n", tok);
            }
            settle();
        }
        if (getenv("BENCH_COMP"))
            scene_setBusCompSetting(sc, SCENE_BUS_COMP_AMOUNT, (uint8_t)atoi(getenv("BENCH_COMP")));
        if (getenv("BENCH_COMPT"))
            scene_setBusCompSetting(sc, SCENE_BUS_COMP_TIME, (uint8_t)atoi(getenv("BENCH_COMPT")));
        if (!atoi(argv[5])) {
            static effect_record_t off;
            off = *scene_effectConst(sc);
            off.type = EFFECT_TYPE_OFF;
            (void)scene_commitEffectRecord(sc, &off);
            effects_activateScene(sc);
            settle();
        }
        if (argc > 6 && !strcmp(argv[6], "pat")) {
            extern uint32_t mixer_sleepBlocks[6];
            run_pattern(sc, blocks);
            printf("scene %u fx %s pattern, asleep %%:", sc + 1, argv[5]);
            for (v = 0; v < 6; v++)
                printf(" %u", (unsigned)(mixer_sleepBlocks[v] * 100u / (unsigned)blocks));
            printf("\n");
            return 0;
        }
        run(blocks, mask, &m, &w);
        printf("scene %u fx %s mask %02x: %.1f us mean\n", sc + 1, argv[5], mask, m);
        if (getenv("BENCH_FX")) {
            uint8_t i;
            printf("runtime rows 3..25:");
            for (i = 3; i < 26; i++) printf(" %u", effects_lastApplied(i));
            printf("\n");
        }
        return 0;
    }
    printf("bank %s, %d blocks, host us per 32-frame block (mean/worst)\n",
           argv[2], blocks);
    printf("sc fx   all        nofx       | v1        v2        v3        v4        v5        v6        | kit\n");
    for (s = 0; s < 16; s++) {
        double m, w, mn, wn, mv[6], wv[6];
        static effect_record_t saved, off;
        uint8_t fxtype;
        char kit[80] = "";

        if (!bank_sceneAvailable(s))
            continue;
        activate(s);
        saved = *scene_effectConst(s);
        fxtype = saved.type;
        run(blocks, 0x7F, &m, &w);
        /* Effect off for the remaining runs, this Scene only (no fan-out). */
        off = saved;
        off.type = EFFECT_TYPE_OFF;
        (void)scene_commitEffectRecord(s, &off);
        effects_activateScene(s);
        settle();
        run(blocks, 0x7F, &mn, &wn);
        for (v = 0; v < 6; v++)
            run(blocks / 2, (uint8_t)((v == 5) ? 0x60 : (1u << v)), &mv[v], &wv[v]);
        for (v = 0; v < 6; v++) {
            const kit_instrument_slot_t *in = scene_instrumentSlotConst(s, v);
            const instrument_registry_entry_t *e =
                in ? instrumentManager_registryEntry(in->type) : NULL;
            strcat(kit, e && e->display_label ? e->display_label : "?");
            strcat(kit, " ");
        }
        (void)scene_commitEffectRecord(s, &saved);
        printf("%02u %-3s %5.1f/%5.1f %5.1f/%5.1f |", s + 1,
               fxtype ? "dvb" : "off", m, w, mn, wn);
        for (v = 0; v < 6; v++)
            printf(" %4.1f/%4.1f", mv[v], wv[v]);
        printf(" | %s\n", kit);
    }
    return 0;
}
