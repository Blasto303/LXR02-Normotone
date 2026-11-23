/*
 * Foreground + interrupt CPU bench (host): everything except the audio DSP.
 *
 * usage: fg_bench card.img <bank> <seconds> [gestures 0|1]
 *
 * Simulated real time in 50 us steps: SysTick and TIM3 (sequencer) at 4 kHz,
 * TIM7 (LCD drain) at 5 kHz, TIM6 at 1 kHz, one main-loop pass per 1 ms
 * (every service once) and the render-side control work every 725 us
 * (voice triggers, automation; the DSP render itself is not run). The
 * peripheral address ranges are mapped as plain memory so the real drivers
 * (LCD queue, LEDs, trigger jacks) run. Every category has its own noinline
 * wrapper: run under callgrind and read inclusive counts per wrapper.
 *
 * Gesture script (gestures=1), 130 BPM, sequencer running from t=0:
 *   3.0 s SHIFT+FX (effect page)     3.2-5.0 s pot turns every 20 ms
 *   5.0 s PERF, SHIFT+step 5          6.0 s SHIFT+step 9 (pattern changes)
 *   7.0 s SHIFT+LAST STEP (RANDOM)    7.2-8.0 s dose turns every 20 ms
 *   8.0 s LOAD+KIT, step 3            AutoSave writes follow the edits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <valgrind/callgrind.h>
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
#include "sequencer.h"
#include "ledHandler.h"
#include "liveMode.h"

__asm__(".globl _sfxbuf\n.globl _efxbuf\n.globl _fxbuf_size\n"
        ".section .bss\n.balign 32\n_sfxbuf:\n.space 126592\n_efxbuf:\n"
        ".set _fxbuf_size, 126592\n.text\n");

extern void fxbuf_init(void);
extern void mixer_init(void);
extern void parameterArray_init(void);
extern int ram_load(const char *);
extern void sim_advance(unsigned int us);
extern float slider_vol[];
extern volatile uint32_t systick_ticks;
extern void TIM3_IRQHandler_body(void);
extern void lcd_tim7_tick(void);
extern void seq_drainPendingAutomation(void);
extern void seq_rampService(void);
extern void led_processSeqLedState(void);
extern void led_tickHandler(void);
extern void screensaver_check(void);

#define TIM3_SR (*((volatile uint32_t *)0x40000410UL))

static sample_mx_t outA[64], outB[64];

/* ---- one wrapper per category (callgrind inclusive counts) ---- */
#define W(name, body) __attribute__((noinline)) static void name(void) { body; }
W(isr_seq, TIM3_SR = 1u; TIM3_IRQHandler_body())
W(isr_lcd, lcd_tim7_tick())   /* TIM7 body (timebase.c is simulated by ramsd.c) */
W(svc_led, led_processSeqLedState(); led_tickHandler())
W(svc_mnu, menu_serviceRuntimeWidgets())
W(svc_scr, screensaver_check())
W(svc_btn, buttonHandler_processEvents(); buttonHandler_tick(); buttonHandler_processEvents())
W(svc_prs, menu_pollPresetStatus())
W(svc_mrp, preset_morphTick())
W(svc_sd, filesystem_tick())
W(svc_bgl, preset_tickBackgroundBankLoad())
W(ctl_render, voiceControl_processPending(); seq_drainPendingAutomation(); seq_rampService())
static void ev_knob_raw(int knob, int d) { menu_parseKnobDelta((uint8_t)knob, (int8_t)d); }
__attribute__((noinline)) static void ev_knob(int knob, int d) { ev_knob_raw(knob, d); }
__attribute__((noinline)) static void ev_enc(int d) { menu_parseEncoder((int8_t)d, 0u); }
__attribute__((noinline)) static void ev_press(uint8_t b) { buttonHandler_buttonPressed(b); }
__attribute__((noinline)) static void ev_release(uint8_t b) { buttonHandler_buttonReleased(b); }

/* ---- live stress (gestures = 2) ---- */
static const uint8_t seq_btn[16] = { BUT_SEQ1, BUT_SEQ2, BUT_SEQ3, BUT_SEQ4,
    BUT_SEQ5, BUT_SEQ6, BUT_SEQ7, BUT_SEQ8, BUT_SEQ9, BUT_SEQ10, BUT_SEQ11,
    BUT_SEQ12, BUT_SEQ13, BUT_SEQ14, BUT_SEQ15, BUT_SEQ16 };
static const uint8_t sel_btn[8] = { BUT_SELECT1, BUT_SELECT2, BUT_SELECT3,
    BUT_SELECT4, BUT_SELECT5, BUT_SELECT6, BUT_SELECT7, BUT_SELECT8 };
static const uint8_t voice_btn[7] = { BUT_VOICE_1, BUT_VOICE_2, BUT_VOICE_3,
    BUT_VOICE_4, BUT_VOICE_5, BUT_VOICE_6, BUT_VOICE_7 };
static uint32_t stress_rng = 12345u;
static uint32_t srnd(uint32_t n) { stress_rng = stress_rng * 1664525u + 1013904223u; return (stress_rng >> 8) % n; }
/* A chord pressed now and released 2 ms later (pending list). */
static uint8_t chord[4]; static uint8_t chord_n; static long chord_release = -1;
static void hold(long ms, uint8_t a, uint8_t b, uint8_t c)
{
    chord_n = 0;
    if (a != 255) { ev_press(a); chord[chord_n++] = a; }
    if (b != 255) { ev_press(b); chord[chord_n++] = b; }
    if (c != 255) { ev_press(c); chord[chord_n++] = c; }
    chord_release = ms + 2;
}
static void stress_tick(long ms)
{
    long c = ms % 8000;            /* 8 s cycle */
    /* Bank change while playing has priority (LOAD+PROJECT, previous Bank). */
    if (ms >= 30000 && ms < 30200) {
        if (chord_release >= 0 && ms >= chord_release) {
            while (chord_n) ev_release(chord[--chord_n]);
            chord_release = -1;
        }
        if (ms == 30000) { while (chord_n) ev_release(chord[--chord_n]); hold(ms, BUT_MODE3, BUT_SEQ11, 255); }
        if (ms == 30050) ev_enc(-1);
        if (ms == 30100) { menu_parseEncoder(0, 1u); menu_parseEncoder(0, 0u); printf("t=30.1 s bank load requested\n"); }
        if (ms == 30150) hold(ms, BUT_MODE2, 255, 255);
        if (getenv("FG_TRACE") && ms % 25 == 0)
            printf("t=%ld |%.16s|%.16s| bank %u\n", ms, editDisplayBuffer[0], editDisplayBuffer[1], (unsigned)bank_restoreBankSlot());
        return;
    }
    if (chord_release >= 0 && ms >= chord_release) {
        while (chord_n) ev_release(chord[--chord_n]);
        chord_release = -1;
    }
    if (chord_release >= 0) return;
    if (ms % 1846 == 0) { hold(ms, BUT_SHIFT, seq_btn[srnd(11)], 255); return; }   /* pattern / bar */
    if (ms % 923 == 400) { hold(ms, BUT_MODE3, BUT_SEQ8, 255); return; }          /* KIT page */
    if (ms % 923 == 410) { hold(ms, seq_btn[srnd(16)], 255, 255); return; }      /* kit swap */
    if (ms % 923 == 420) { hold(ms, BUT_MODE2, 255, 255); return; }              /* back to PERF */
    if (c == 2000) { hold(ms, BUT_SHIFT, BUT_SEQ14, 255); return; }              /* RANDOM */
    if (c == 2010) { hold(ms, BUT_SEQ3, 255, 255); return; }                     /* p+k */
    if (c == 2020) { ev_enc(32); return; }                                       /* dose 127 */
    if (c > 2020 && c < 4000 && c % 500 == 0) { menu_parseEncoder(0, 1u); menu_parseEncoder(0, 0u); return; } /* new draws */
    if (c == 3900) { ev_enc(-16); return; }
    if (c == 4000) { hold(ms, BUT_SHIFT, BUT_SEQ15, 255); return; }              /* Effect page */
    if (c > 4000 && c < 6000) {
        if (c % 250 == 5) { hold(ms, sel_btn[srnd(5)], 255, 255); return; }       /* OSC..CLICK */
        if (c % 10 == 0) { ev_knob(srnd(4), (int)srnd(9) - 4); return; }          /* pot sweeps */
    }
    if (c == 6000) { hold(ms, BUT_MODE2, 255, 255); return; }                    /* PERF */
    if (c > 6000 && c < 7000 && c % 10 == 0) { ev_knob(srnd(4), (int)srnd(7) - 3); return; } /* morph pots */
    if (c > 6000 && c % 300 == 150) { hold(ms, voice_btn[srnd(7)], 255, 255); return; }      /* mutes */
    if (c == 7000) { ev_press(BUT_SELECT3); return; }                            /* looper on */
    if (c == 7600) { ev_release(BUT_SELECT3); return; }                          /* looper off */
    if (ms % 1000 == 500) seq_setBpm((uint16_t)(90 + (ms / 1000) * 9 % 91)); /* tempo moves */
}

/* Render blocks timed one by one (host ns, CLOCK_MONOTONIC). */
static unsigned rnd_count;
static float rnd_ns[200000];
__attribute__((noinline)) static void rnd_block(void)
{
    struct timespec a, b;
    static int win = -1; static unsigned w0, w1;
    if (win < 0) { const char *e = getenv("FG_DUMPWIN"); float x, y; win = 0;
        if (e && sscanf(e, "%f,%f", &x, &y) == 2) { win = 1; w0 = (unsigned)(x * 44108 / 32); w1 = (unsigned)(y * 44108 / 32); } }
    if (win && rnd_count >= w0 && rnd_count < w1) CALLGRIND_ZERO_STATS;
    clock_gettime(CLOCK_MONOTONIC, &a);
    mixer_calcNextSampleBlock(outA, outB);
    if (win && rnd_count >= w0 && rnd_count < w1) CALLGRIND_DUMP_STATS;
    clock_gettime(CLOCK_MONOTONIC, &b);
    if (rnd_count < 200000u) {
        float ns = (float)((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec));
        rnd_ns[rnd_count++] = ns;
        if (getenv("FG_PEAK") && ns > 30000.0f)
            printf("peak block %u (t=%.3f s) %.0f ns |%.16s|%.16s| fx type %u stm %u mdl %u\n", rnd_count,
                   rnd_count * 32.0 / 44108.0, ns, editDisplayBuffer[0], editDisplayBuffer[1],
                   scene_effectConst(scene_getActiveIndex())->type,
                   scene_effectConst(scene_getActiveIndex())->normal[8],
                   scene_effectConst(scene_getActiveIndex())->normal[7]);
    }
}

static void map_peripherals(void)
{
    void *a = mmap((void *)0x40000000UL, 0x00080000UL, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    void *c = mmap((void *)0x50000000UL, 0x00100000UL, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    void *b = mmap((void *)0xE0000000UL, 0x00100000UL, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (a != (void *)0x40000000UL || b != (void *)0xE0000000UL || c != (void *)0x50000000UL) {
        printf("peripheral map failed (%p %p %p)\n", a, b, c);
        exit(1);
    }
}

/*
 * Trigger log (FG_GEN): every voice note-on through the real sequencer,
 * with its time, so a generator's rhythm can be checked step by step.
 * Linked with -Wl,--wrap=voiceControl_noteOn (build_fg.sh).
 */
#include "TrackGen.h"
extern void __real_voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel);
static long fg_now_ms;
static long fg_hit_ms[7][256];
static uint8_t fg_hit_vel[7][256];
static int fg_hit_n[7];
void __wrap_voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel)
{
    if (voice < 7 && fg_hit_n[voice] < 256) {
        fg_hit_vel[voice][fg_hit_n[voice]] = vel;
        fg_hit_ms[voice][fg_hit_n[voice]++] = fg_now_ms;
    }
    __real_voiceControl_noteOn(voice, note, vel);
}

static void settle(void)
{
    int quiet = 0;
    long guard = 0;
    while (quiet < 20 && guard++ < 2000000) {
        menu_pollPresetStatus(); preset_morphTick(); filesystem_tick();
        preset_tickBackgroundBankLoad(); sim_advance(50);
        if (filesystem_status() == FS_STATUS_BUSY || preset_getStatus() != PRESET_IDLE ||
            !preset_applyWorkersIdle())
            quiet = 0;
        else
            quiet++;
    }
}

int main(int argc, char **argv)
{
    long steps, st;
    int gestures = (argc > 4) ? atoi(argv[4]) : 1;
    uint8_t v;

    if (argc < 4) { printf("usage: fg_bench card.img bank seconds [gestures]\n"); return 1; }
    map_peripherals();
    if (ram_load(argv[1])) return 1;
    scene_initAll(); bank_init(); fxbuf_init(); instrumentManager_runtimeInit();
    effects_init(); mixer_init(); parameterArray_init();
    for (v = 0; v < 6; v++) { modNode_init(&velocityModulators[v]); slider_vol[v] = 0.8f; }
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
    seq_init();
    menu_init();
    settle();
    for (v = 0; v < 50 && bank_scenePresentMask() != 0xFFFFu; v++) settle();
    preset_startDrumsetApply(); settle();
    seq_setBpm(130);
    /*
     * FG_GEN="t:f=v,f=v,...": generator fields of track t (pat_gen_field_t
     * indices) on the active Scene before the run; the log of track t is
     * printed as one character per 16th step at the end.
     */
    if (getenv("FG_GEN")) {
        const char *e = getenv("FG_GEN");
        int t = atoi(e), f, val;
        const char *c = strchr(e, ':');
        while (c && sscanf(c + 1, "%d=%d", &f, &val) == 2) {
            pat_setTrackGen(scene_getActiveIndex(), (uint8_t)t, (uint8_t)f, (uint8_t)val);
            c = strchr(c + 1, ',');
        }
    }
    if (getenv("FG_PRIME"))      /* NUM mode global prime 0..31 */
        pat_setGenPrime(scene_getActiveIndex(), (uint8_t)atoi(getenv("FG_PRIME")));
    /* PERF page, transport running. */
    ev_press(BUT_MODE2); ev_release(BUT_MODE2);
    seq_setRunning(1);

    steps = (long)atoi(argv[3]) * 20000L;          /* 50 us steps */
    for (st = 0; st < steps; st++) {
        long us = st * 50L;
        long ms = us / 1000L;

        fg_now_ms = ms;
        if (st % 5 == 0) { systick_ticks++; isr_seq(); }
        if (st % 4 == 0) isr_lcd();
        if (st % 20 == 0) {
            /* gestures, once per ms boundary */
            if (gestures == 2) stress_tick(ms);
            if (getenv("FG_TRACE") && ms > 30200 && ms % 500 == 0)
                printf("t=%ld |%.16s|%.16s| bank %u status %d\n", ms, editDisplayBuffer[0], editDisplayBuffer[1],
                       (unsigned)bank_restoreBankSlot(), (int)preset_getStatus());
            if (gestures == 1) {
                if (ms == 3000) { ev_press(BUT_SHIFT); ev_press(BUT_SEQ15); }
                if (ms == 3001) { ev_release(BUT_SEQ15); ev_release(BUT_SHIFT); }
                if (ms >= 3200 && ms < 5000 && ms % 20 == 0) ev_knob(1 + (ms / 20) % 3, (ms / 100) % 2 ? 1 : -1);
                if (ms == 5000) { ev_press(BUT_MODE2); ev_release(BUT_MODE2); }
                if (ms == 5002) { ev_press(BUT_SHIFT); ev_press(BUT_SEQ5); }
                if (ms == 5003) { ev_release(BUT_SEQ5); ev_release(BUT_SHIFT); }
                if (ms == 6000) { ev_press(BUT_SHIFT); ev_press(BUT_SEQ9); }
                if (ms == 6001) { ev_release(BUT_SEQ9); ev_release(BUT_SHIFT); }
                if (ms == 7000) { ev_press(BUT_SHIFT); ev_press(BUT_SEQ14); }
                if (ms == 7001) { ev_release(BUT_SEQ14); ev_release(BUT_SHIFT); }
                if (ms >= 7200 && ms < 8000 && ms % 20 == 0) ev_enc(1);
                if (ms == 8000) { ev_press(BUT_MODE3); ev_press(BUT_SEQ8); }
                if (ms == 8001) { ev_release(BUT_SEQ8); ev_release(BUT_MODE3); }
                if (ms == 8100) { ev_press(BUT_SEQ3); ev_release(BUT_SEQ3); }
            }
            svc_led(); svc_mnu(); svc_scr(); svc_btn();
            svc_prs(); svc_mrp(); svc_sd(); svc_bgl();
        }
        if (st % 14 == 0 || st % 15 == 0) {     /* ~725 us: one 32-frame block */
            static long last_block = -1000;
            if (us - last_block >= 725 && !getenv("FG_NORENDER")) {
                last_block = us;
                ctl_render();
                rnd_block();   /* render, timed per block */
            }
        }
        sim_advance(50);
    }
    if (getenv("FG_GEN")) {
        int t = atoi(getenv("FG_GEN")), i, k;
        const double step_ms = 60000.0 / 130.0 / 4.0;
        long t0 = fg_hit_n[t] ? fg_hit_ms[t][0] : 0;
        char line[129];
        memset(line, '.', 128); line[128] = 0;
        for (i = 0; i < fg_hit_n[t]; i++) {
            k = (int)((fg_hit_ms[t][i] - t0) / step_ms + 0.5);
            if (k >= 0 && k < 128) line[k] = 'x';
        }
        printf("track %d hits %d first at %ld ms\n", t, fg_hit_n[t], t0);
        for (i = 0; i < 128; i += 32) printf("  %.32s\n", line + i);
        printf("  vel:");
        for (i = 0; i < fg_hit_n[t] && i < 24; i++) printf(" %u", fg_hit_vel[t][i]);
        printf("\n");
    }
    printf("final screen |%.16s|%.16s|\n", editDisplayBuffer[0], editDisplayBuffer[1]);
    printf("done: %s s, gestures %d, scene %u, bank %u, blocks %u\n", argv[3], gestures,
           scene_getActiveIndex() + 1, (unsigned)bank_restoreBankSlot(), rnd_count);
    if (getenv("FG_TIMES")) {
        FILE *f = fopen(getenv("FG_TIMES"), "wb");
        fwrite(rnd_ns, sizeof(float), rnd_count, f);
        fclose(f);
    }
    return 0;
}
