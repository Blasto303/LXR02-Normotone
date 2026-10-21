/*
 * Core/DSP/Instruments/PlaitsHat/PlaitsHatVoice.h
 *
 *  Created on: 22.10.2026
 * -----------------------------------------------------------------------------
 *  The hi-hat DSP is a C translation of Plaits' HiHat, SquareNoise,
 *  RingModNoise and Oscillator (plaits/dsp/drums/hi_hat.h,
 *  plaits/dsp/oscillator/oscillator.h):
 *
 *  Copyright 2016 Emilie Gillet.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef PLAITS_HAT_VOICE_H_
#define PLAITS_HAT_VOICE_H_

#include "config.h"
#include "distortion.h"
#include "SlopeEg2.h"
#include "lfo.h"

#define PLAITS_HAT_OSCS 6

/* Hi-hat model (row `mdl`): Plaits' main and aux hi-hats. */
#define PLAITS_HAT_MODEL_SQUARE 0u
#define PLAITS_HAT_MODEL_RING   1u

/*
 * Plaits hi-hat runtime (one tagged InstrumentManager slot member).
 *
 * What: Plaits' two hi-hats. `sqr` is the 808 circuit (six Schmitt-trigger
 * square oscillators, resonant band-pass, swing VCA); `rng` is the ring-
 * modulated square/saw pairs with a linear, two-stage VCA. Both add clocked
 * noise (Snappy-like Noise row) and end in a high-pass. The open hat is the
 * Choke variant: visible track 7 plays the `_choke` decay. Then the LXR voice
 * parts every engine shares (amp envelope, velocity, distortion, LFO, vol,
 * pan). Accessors: PlaitsHatVoice.c and the InstrumentManager dispatchers.
 */
typedef struct {
	/* Metallic sources. */
	uint32_t sq_phase[PLAITS_HAT_OSCS];
	float rm_phase[PLAITS_HAT_OSCS];
	float rm_next[PLAITS_HAT_OSCS];
	uint8_t rm_high[PLAITS_HAT_OSCS];
	uint8_t open;
	uint8_t trigger;
	uint8_t model;
	/* Plaits HiHat state. */
	float envelope;
	float noise_clock;
	float noise_sample;
	float bp_s1;
	float bp_s2;
	float hp_s1;
	float hp_s2;
	uint32_t rng;
	float accent;

	/* Row values. */
	float noise;
	float tone;
	float decay;
	float decay_choke;
	uint8_t note;
	uint8_t fine;
	uint8_t baseNote;

	/* Shared LXR voice parts. */
	float vol;
	float velo;
	uint8_t pan;
	uint8_t volumeMod;
	Lfo lfo;
	SlopeEg2 oscVolEg;
	float ampFilterInput;
	float lastGain;
	Distortion distortion;
} PlaitsHatVoice;

void PlaitsHat_initVoice(PlaitsHatVoice *voice, uint8_t seed_index);
void PlaitsHat_triggerVoice(PlaitsHatVoice *voice, const uint8_t source_slot,
                            const uint8_t vol, const uint8_t alternate,
                            const uint8_t note);
void PlaitsHat_calcVoiceAsync(PlaitsHatVoice *voice);
void PlaitsHat_calcVoiceSyncBlock(PlaitsHatVoice *voice, int16_t *buf,
                                  const uint8_t size);

#endif /* PLAITS_HAT_VOICE_H_ */
