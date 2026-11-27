/*
 * Core/Sequencer/TrackGen.h
 *
 * Per-track generators: Euclidean rhythm and Turing Machine (a 16-bit
 * probabilistic shift register). Non-destructive: the Pattern decides the
 * step position, specials and automation; a generator only decides whether
 * the step fires. The Turing register also runs in standard and Euclidean
 * mode and can modulate the trigger velocity (vdp), which then reaches the
 * voice's own velocity target.
 *
 * Settings live in pat_scene_region_t.track_gen[] (PatternData.h); this
 * module holds only the runtime state (8 B per track). Clients: sequencer.c
 * (TIM3, one call per track step), the GEN page and PERF pot 2 (liveMode.c).
 */
#ifndef TRACK_GEN_H_
#define TRACK_GEN_H_

#include <stdint.h>
#include "PatternData.h"

void tgen_init(void);
/* The track restarts (transport start, Scene change, step 0). */
void tgen_resetTrack(uint8_t track);

/*
 * One track step (call once per step, muted or not, so the register keeps
 * time). pat_on: the Pattern step's trigger; rnd: a random 16-bit value.
 * Returns the gate: pat_on in standard mode, else the generator's.
 */
uint8_t tgen_step(uint8_t track, const pat_track_gen_t *g, uint8_t pat_on,
                  uint16_t rnd, uint8_t prime);
/* Velocity after Turing modulation: vel + (volt - 128) * vdp / 127. */
uint8_t tgen_velocity(uint8_t track, const pat_track_gen_t *g, uint8_t vel);

/* Pure helpers (also used for the LEDs and the host test). */
uint8_t tgen_eucHit(const pat_track_gen_t *g, uint8_t pos);
uint8_t tgen_eucSteps(const pat_track_gen_t *g);
uint8_t tgen_tmLen(const pat_track_gen_t *g);
uint8_t tgen_eucPos(uint8_t track);

/*
 * NUM mode (after Noise Engineering's Numeric Repetitor, from its public
 * manual): 32 prime rhythms of 16 steps; a track plays the prime, ANDed
 * with its mask (0x0F0F or 0xF003), multiplied by its factor as binary
 * numbers on a circle (bit 15 = step 1, overflow folded back in: a
 * multiplication modulo 2^16 - 1). Returns the 16-step word.
 */
uint16_t tgen_numPattern(const pat_track_gen_t *g, uint8_t prime);
uint8_t tgen_numFactor(uint8_t index);          /* 1 2 4 8 16 3 6 ... */

/* NUM progression (PERF, SHIFT + BAR): AAAB phrases of 4 / 8 / 16 bars; B
 * moves tracks 2..7 one factor position for that bar; the prime steps every
 * 16 bars and swaps bank every 32. Runtime only. */
void tgen_progStart(void);                      /* sequencer start (ISR) */
void tgen_progBar(void);                        /* every bar line (ISR) */
void tgen_progCycleLen(void);                   /* off -> 4 -> 8 -> 16 -> off */
void tgen_progToggleHold(void);
void tgen_progRebase(void);                     /* the prime pot moved */
uint8_t tgen_progLength(void);
uint8_t tgen_progHeld(void);
uint8_t tgen_progSerial(void);
uint8_t tgen_progBars(void);                    /* bar lines since start (wraps) */
uint8_t tgen_progLenShown(void);
void tgen_progHideLen(void);
uint8_t tgen_progPos(void);                     /* 1..len, 0 when off */
uint8_t tgen_progInB(void);
uint8_t tgen_progPrime(uint8_t prime);          /* prime after the walk */

/*
 * Global generator layer (live): SHIFT + pot 1 on the GEN page sets the
 * same mode on the 7 tracks; any mode but STD also latches these 7 records
 * and the prime here, and the sequencer then uses them for every Pattern
 * (pattern changes keep NUM, EUC or TM). Back to STD releases it. Runtime
 * only: at boot each Pattern plays its own settings again.
 */
uint8_t tgen_globalOn(void);
void tgen_globalLatch(uint8_t on, const pat_track_gen_t *recs, uint8_t prime);
pat_track_gen_t *tgen_globalRec(uint8_t track);
uint8_t tgen_globalPrime(void);
void tgen_setGlobalPrime(uint8_t prime);

/* Turing register of a track; Write: flip bit n (0 = newest). */
uint16_t tgen_register(uint8_t track);
void tgen_flipBit(uint8_t track, uint8_t bit);

#endif /* TRACK_GEN_H_ */
