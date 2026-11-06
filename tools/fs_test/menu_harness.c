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
#include "buttonHandler.h"
#include "menu.h"
#include "presetManager.h"
#include "BankData.h"

extern int ram_load(const char *);
extern unsigned long sd_reads, sd_writes;
extern unsigned long long sim_us;
extern void sim_advance(unsigned int us);

static void loop_pass(void)
{
    buttonHandler_processEvents();
    buttonHandler_tick();
    buttonHandler_processEvents();
    menu_pollPresetStatus();
    preset_morphTick();
    filesystem_tick();
    preset_tickBackgroundBankLoad();
    menu_serviceRuntimeWidgets();
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
    menu_init();
    settle();
    show("boot", sd_reads, sim_us);

    for (int a = 3; a < argc; a++) {
        const char *s = argv[a];
        unsigned long r0 = sd_reads;
        unsigned long long t0 = sim_us;
        int n = (int)strtol(s + 2, NULL, 10);

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
