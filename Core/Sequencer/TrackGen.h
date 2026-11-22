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
                  uint16_t rnd);
/* Velocity after Turing modulation: vel + (volt - 128) * vdp / 127. */
uint8_t tgen_velocity(uint8_t track, const pat_track_gen_t *g, uint8_t vel);

/* Pure helpers (also used for the LEDs and the host test). */
uint8_t tgen_eucHit(const pat_track_gen_t *g, uint8_t pos);
uint8_t tgen_eucSteps(const pat_track_gen_t *g);
uint8_t tgen_tmLen(const pat_track_gen_t *g);
uint8_t tgen_eucPos(uint8_t track);

/* Turing register of a track; Write: flip bit n (0 = newest). */
uint16_t tgen_register(uint8_t track);
void tgen_flipBit(uint8_t track, uint8_t bit);

#endif /* TRACK_GEN_H_ */
