/* CPU profile (contract in cpuProfile.h). RAM: about 0.5 KB. */
#include "cpuProfile.h"

#include <string.h>

#define PROF_HZ 216000000UL

volatile uint32_t prof_isrCycles;

static volatile uint32_t prof_sum[PROF_COUNT];
static volatile uint32_t prof_max[PROF_COUNT];
static uint32_t prof_shownSum[PROF_COUNT];
static uint32_t prof_shownMax[PROF_COUNT];
static uint32_t prof_shownWindow = 1u;
static uint32_t prof_windowStart;
static uint8_t prof_serial;

static const char prof_names[PROF_COUNT][4] = {
    "RND", "LED", "FPN", "MID", "ENC", "POT", "KNB", "MNU", "ADC", "SCR",
    "BTN", "PRS", "MRP", "SD ", "BGL",
    "SEQ", "LCD", "T6 ", "MDI", "ENI", "CLK", "RST", "DMA", "USB"
};

void prof_addFg(uint8_t id, uint32_t cycles)
{
    if (id >= PROF_FG_COUNT || (int32_t)cycles < 0)
        return;
    prof_sum[id] += cycles;
    if (cycles > prof_max[id])
        prof_max[id] = cycles;
}

void prof_addIsr(uint8_t id, uint32_t cycles)
{
    if (id < PROF_FG_COUNT || id >= PROF_COUNT)
        return;
    prof_sum[id] += cycles;
    if (cycles > prof_max[id])
        prof_max[id] = cycles;
}

void prof_service(void)
{
    uint32_t now = PROF_DWT_CYCCNT;
    uint32_t primask;
    uint8_t i;

    if ((uint32_t)(now - prof_windowStart) < PROF_HZ)
        return;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    for (i = 0u; i < PROF_COUNT; i++) {
        prof_shownSum[i] = prof_sum[i];
        prof_shownMax[i] = prof_max[i];
        prof_sum[i] = 0u;
        prof_max[i] = 0u;
    }
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
    prof_shownWindow = now - prof_windowStart;
    prof_serial++;
    prof_windowStart = now;
}

uint8_t prof_entryCount(void)
{
    return (uint8_t)(PROF_COUNT + 2u);
}

/* "NAM 71.2% 690us" or "NAM  0.3% 1.2ms" (16 chars). */
void prof_formatEntry(uint8_t entry, char row[16])
{
    uint64_t sum = 0u;
    uint32_t max = 0u;
    uint32_t tenth;
    uint32_t us;
    const char *name;
    uint8_t i;

    memset(row, ' ', 16);
    if (entry == 0u) {
        name = "IRQ";
        for (i = PROF_FG_COUNT; i < PROF_COUNT; i++) {
            sum += prof_shownSum[i];
            if (prof_shownMax[i] > max) max = prof_shownMax[i];
        }
    } else if (entry == 1u) {
        name = "FG ";
        for (i = 1u; i < PROF_FG_COUNT; i++) {
            sum += prof_shownSum[i];
            if (prof_shownMax[i] > max) max = prof_shownMax[i];
        }
    } else if ((uint8_t)(entry - 2u) < PROF_COUNT) {
        name = prof_names[entry - 2u];
        sum = prof_shownSum[entry - 2u];
        max = prof_shownMax[entry - 2u];
    } else {
        return;
    }
    memcpy(row, name, 3);
    tenth = (uint32_t)((sum * 1000u) / (prof_shownWindow ? prof_shownWindow : 1u));
    if (tenth > 999u)
        tenth = 999u;
    row[4] = (char)(tenth >= 100u ? '0' + tenth / 100u : ' ');
    row[5] = (char)('0' + (tenth / 10u) % 10u);
    row[6] = '.';
    row[7] = (char)('0' + tenth % 10u);
    row[8] = '%';
    us = max / 216u;
    if (us < 1000u) {
        row[10] = (char)(us >= 100u ? '0' + us / 100u : ' ');
        row[11] = (char)(us >= 10u ? '0' + (us / 10u) % 10u : ' ');
        row[12] = (char)('0' + us % 10u);
        row[13] = 'u';
        row[14] = 's';
    } else {
        uint32_t ms10 = us / 100u;    /* tenths of ms */

        if (ms10 > 999u) ms10 = 999u;
        row[10] = (char)(ms10 >= 100u ? '0' + ms10 / 100u : ' ');
        row[11] = (char)('0' + (ms10 / 10u) % 10u);
        row[12] = '.';
        row[13] = (char)('0' + ms10 % 10u);
        row[14] = 'm';
        row[15] = 's';
    }
}

uint8_t prof_snapshotSerial(void)
{
    return prof_serial;
}
