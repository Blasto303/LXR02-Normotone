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
        if (dump) fwrite(outA, sizeof(outA[0]), 64, dump);
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

    for (b = 0; b < blocks; b++) {
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
        if (dump) fwrite(outA, sizeof(outA[0]), 64, dump);
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

static void activate(uint8_t s)
{
    scene_selectActive(s);
    bank_selectActiveSceneForEditMask(s);
    preset_startDrumsetApply();
    preset_applySceneSettings(s);
    effects_activateScene(s);
    settle();
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
