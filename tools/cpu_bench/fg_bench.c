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
    /* PERF page, transport running. */
    ev_press(BUT_MODE2); ev_release(BUT_MODE2);
    seq_setRunning(1);

    steps = (long)atoi(argv[3]) * 20000L;          /* 50 us steps */
    for (st = 0; st < steps; st++) {
        long us = st * 50L;
        long ms = us / 1000L;

        if (st % 5 == 0) { systick_ticks++; isr_seq(); }
        if (st % 4 == 0) isr_lcd();
        if (st % 20 == 0) {
            /* gestures, once per ms boundary */
            if (gestures) {
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
            if (us - last_block >= 725) {
                last_block = us;
                ctl_render();
                mixer_calcNextSampleBlock(outA, outB);   /* keeps voices alive, not counted */
            }
        }
        sim_advance(50);
    }
    printf("done: %s s, gestures %d, scene %u\n", argv[3], gestures, scene_getActiveIndex() + 1);
    return 0;
}
