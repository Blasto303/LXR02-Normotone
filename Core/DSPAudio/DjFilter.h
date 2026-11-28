/*
 * Core/DSPAudio/DjFilter.h
 *
 * One-knob DJ filter on the whole machine (PERF pot 4). Centre = off; turn
 * left for a low-pass from 20 kHz down to 100 Hz, right for a high-pass
 * from 20 Hz up to 8 kHz, with a slight resonance. It runs at the very end
 * of the mixer, after the bus compressor, on both outputs (MAIN and OUT 2).
 * Runtime only: it starts at off on every boot and is never saved, so a
 * filter left closed cannot carry over into the next set.
 */
#ifndef DJ_FILTER_H_
#define DJ_FILTER_H_

#include <stdint.h>
#include "sample_mix.h"

#define DJF_RANGE 64          /* knob value -64 (LP closed) .. 0 .. +64 (HP) */

void djFilter_adjust(int8_t delta);   /* PERF pot 4 deltas (UI thread) */
int8_t djFilter_value(void);
/* In place on both stereo interleaved blocks (OUTPUT_DMA_SIZE frames). */
void djFilter_processBlock(sample_mx_t *st1, sample_mx_t *st2);

#endif /* DJ_FILTER_H_ */
