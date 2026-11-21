/*
 * CPU profile (Normotone): real cycle accounting on the device.
 *
 * Every main-loop service and every interrupt handler is timed with the DWT
 * cycle counter. Foreground figures exclude the interrupt time that landed
 * inside them. Each second the totals become a snapshot shown on the CPU page
 * (LIVE: hold LOAD + CONFIG, step 12): share of the CPU (% of 216 MHz) and the
 * longest single call (us). A nested interrupt is also counted inside the
 * interrupt it preempted, so ISR figures can overlap slightly.
 */
#ifndef CPU_PROFILE_H_
#define CPU_PROFILE_H_

#include <stdint.h>

enum {
    PROF_RND = 0,   /* audio render (audio_check_and_render)          */
    PROF_LED,       /* led_processSeqLedState + led_tickHandler       */
    PROF_FPN,       /* timebase_serviceFrontPanel                     */
    PROF_MID,       /* midi_service                                   */
    PROF_ENC,       /* main_encoder_check                             */
    PROF_POT,       /* endless_pot_check                              */
    PROF_KNB,       /* service_knob_repaint                           */
    PROF_MNU,       /* menu_serviceRuntimeWidgets                     */
    PROF_ADC,       /* adc_checkPots                                  */
    PROF_SCR,       /* screensaver_check                              */
    PROF_BTN,       /* buttonHandler_processEvents + tick             */
    PROF_PRS,       /* menu_pollPresetStatus                          */
    PROF_MRP,       /* preset_morphTick                               */
    PROF_SD,        /* filesystem_tick                                */
    PROF_BGL,       /* main_serviceBackgroundLoadIdle                 */
    PROF_FG_COUNT,
    PROF_SEQ = PROF_FG_COUNT, /* TIM3: sequencer clock                */
    PROF_LCD,       /* TIM7: LCD queue drain                          */
    PROF_T6,        /* TIM6: front-panel tick                         */
    PROF_MDI,       /* USART3: MIDI DIN                               */
    PROF_ENI,       /* TIM1_CC: encoder                               */
    PROF_CLK,       /* EXTI4: CLK IN                                  */
    PROF_RST,       /* EXTI9_5: RST IN                                */
    PROF_DMA,       /* DMA1 streams 4/7: audio                        */
    PROF_USB,       /* OTG_FS: USB                                    */
    PROF_COUNT
};

#define PROF_DWT_CYCCNT (*((volatile uint32_t *)0xE0001004UL))

extern volatile uint32_t prof_isrCycles;
void prof_addFg(uint8_t id, uint32_t cycles);
void prof_addIsr(uint8_t id, uint32_t cycles);
/* Main loop, once per pass: closes a 1 s window into the snapshot. */
void prof_service(void);

/* Snapshot access for the CPU page: entries 0 = all ISRs, 1 = foreground
 * services (render excluded), then every id. */
uint8_t prof_entryCount(void);
void prof_formatEntry(uint8_t entry, char row[16]);
/* Increments at every new snapshot (once per second). */
uint8_t prof_snapshotSerial(void);

#define PROF_FG(id, call) do {                                   \
        uint32_t prof_t0_ = PROF_DWT_CYCCNT;                     \
        uint32_t prof_i0_ = prof_isrCycles;                      \
        call;                                                    \
        prof_addFg((id), PROF_DWT_CYCCNT - prof_t0_ -            \
                   (prof_isrCycles - prof_i0_));                 \
    } while (0)

/* Defines `name` as a timed wrapper around `name##_body`. */
#define PROF_ISR_WRAP(name, id)                                  \
    void name(void)                                              \
    {                                                            \
        uint32_t prof_t0_ = PROF_DWT_CYCCNT;                     \
        uint32_t prof_c_;                                        \
        name##_body();                                           \
        prof_c_ = PROF_DWT_CYCCNT - prof_t0_;                    \
        prof_isrCycles += prof_c_;                               \
        prof_addIsr((id), prof_c_);                              \
    }

#endif /* CPU_PROFILE_H_ */
