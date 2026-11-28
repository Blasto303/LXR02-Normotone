/*
 * Core/DSP/Instruments/PlaitsSnare/PlaitsSnareVoice.h
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  The snare DSP is a C translation of Plaits' AnalogSnareDrum and
 *  SyntheticSnareDrum (plaits/dsp/drums/analog_snare_drum.h,
 *  plaits/dsp/drums/synthetic_snare_drum.h) and the stmlib parts they use:
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

#ifndef PLAITS_SNARE_VOICE_H_
#define PLAITS_SNARE_VOICE_H_

#include "config.h"
#include "distortion.h"
#include "SlopeEg2.h"
#include "lfo.h"

#define PLAITS_SNARE_MODES 5

/*
 * Plaits analog snare runtime (one tagged InstrumentManager slot member).
 *
 * What: the 808-style snare of Plaits (a trigger pulse into five resonant
 * band-pass "shell" modes, two below Tone 2/3 and five above, plus
 * band-passed noise for the snares), followed by the LXR voice parts every
 * engine shares (amp envelope, velocity, distortion, LFO, vol, pan). The
 * snare is self-enveloped; the amp envelope defaults fully open.
 *
 * Rows write note, fine (raw), snappy, tone, decay (0..1, the Plaits
 * Harmonics/Timbre/Morph macros). Coefficients are derived once per block in
 * PlaitsSnare_calcVoiceAsync(). Row `syn` picks the model: off =
 * AnalogSnareDrum (Plaits' OUT), on = SyntheticSnareDrum (Plaits' AUX, the
 * 909-style snare). Accessors: PlaitsSnareVoice.c and the InstrumentManager
 * dispatchers.
 */
#define PLAITS_SNARE_MODEL_ANALOG 0u
#define PLAITS_SNARE_MODEL_SYNTH  1u

typedef struct {
	/* Plaits AnalogSnareDrum state. */
	int32_t pulse_remaining;
	float pulse;
	float pulse_height;
	float pulse_lp;
	float noise_envelope;
	float mode_s1[PLAITS_SNARE_MODES];
	float mode_s2[PLAITS_SNARE_MODES];
	float noise_s1;
	float noise_s2;
	uint32_t rng;
	/* Plaits SyntheticSnareDrum state (model 1). */
	int32_t ss_hold;
	float ss_phase[2];
	float ss_drum_amp;
	float ss_snare_amp;
	float ss_fm;
	float ss_drum_lp;
	float ss_snare_hp;
	float ss_lp_s1;
	float ss_lp_s2;
	/* Per-block SyntheticSnareDrum coefficients. */
	float ss_drum_decay;
	float ss_snare_decay;
	float ss_drum_level;
	float ss_snare_level;
	float ss_reset_amount;
	float ss_fm_amount;
	float ss_dlp_g;
	float ss_hp_g;
	float ss_lp_g;
	float ss_lp_r;
	float ss_lp_h;
	float ss_f0;

	/* Per-block coefficients (PlaitsSnare_calcVoiceAsync). */
	float mode_g[PLAITS_SNARE_MODES];
	float mode_r[PLAITS_SNARE_MODES];
	float mode_h[PLAITS_SNARE_MODES];
	float mode_gain[PLAITS_SNARE_MODES];
	float noise_g;
	float noise_r;
	float noise_h;
	float noise_decay;
	float exciter_leak;
	float snappy_mix;
	float accent;

	/* Row values. */
	float snappy;
	float tone;
	float decay;
	uint8_t note;
	uint8_t fine;
	uint8_t baseNote;
	uint8_t trigger;
	uint8_t model;      /* row `syn`: 0 analog (OUT), 1 synthetic (AUX) */

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
} PlaitsSnareVoice;

void PlaitsSnare_initVoice(PlaitsSnareVoice *voice, uint8_t seed_index);
void PlaitsSnare_triggerVoice(PlaitsSnareVoice *voice,
                              const uint8_t source_slot, const uint8_t vol,
                              const uint8_t note);
void PlaitsSnare_calcVoiceAsync(PlaitsSnareVoice *voice);
void PlaitsSnare_calcVoiceSyncBlock(PlaitsSnareVoice *voice, int16_t *buf,
                                    const uint8_t size);

#endif /* PLAITS_SNARE_VOICE_H_ */
