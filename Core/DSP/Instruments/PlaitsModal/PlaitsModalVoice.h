/*
 * Core/DSP/Instruments/PlaitsModal/PlaitsModalVoice.h
 *
 *  Created on: 22.10.2026
 * -----------------------------------------------------------------------------
 *  The modal DSP is a C translation of Plaits' ModalVoice and Resonator
 *  (plaits/dsp/physical_modelling/), the modal synthesis Plaits derives from
 *  Rings, and the stmlib parts they use:
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

#ifndef PLAITS_MODAL_VOICE_H_
#define PLAITS_MODAL_VOICE_H_

#include "config.h"
#include "distortion.h"
#include "SlopeEg2.h"
#include "lfo.h"

#define PLAITS_MODAL_MODES 24

/*
 * Plaits modal resonator runtime (one tagged InstrumentManager slot member).
 *
 * What: a struck bank of 24 band-pass modes. Structure (Plaits Harmonics)
 * stretches the partials from harmonic to bell-like, Brightness (Timbre)
 * shapes the strike and the high-mode damping, Damping (Morph) sets the ring
 * time, and Position (Rings' strike position, fixed in Plaits) sets the mode
 * amplitudes. Then the LXR voice parts every engine shares. Coefficients are
 * derived per block in the render function (block rate, not stored).
 * Accessors: PlaitsModalVoice.c and the InstrumentManager dispatchers.
 */
typedef struct {
	float mode_s1[PLAITS_MODAL_MODES];
	float mode_s2[PLAITS_MODAL_MODES];
	float amp[PLAITS_MODAL_MODES];
	float amp_position;
	float exc_s1;
	float exc_s2;
	float limiter_peak;
	float structure_lp;
	float accent;

	/* Row values. */
	float structure;
	float brightness;
	float damping;
	float position;
	uint8_t note;
	uint8_t fine;
	uint8_t baseNote;
	uint8_t trigger;

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
} PlaitsModalVoice;

void PlaitsModal_initVoice(PlaitsModalVoice *voice);
void PlaitsModal_triggerVoice(PlaitsModalVoice *voice,
                              const uint8_t source_slot, const uint8_t vol,
                              const uint8_t note);
void PlaitsModal_calcVoiceAsync(PlaitsModalVoice *voice);
void PlaitsModal_calcVoiceSyncBlock(PlaitsModalVoice *voice, int16_t *buf,
                                    const uint8_t size);

#endif /* PLAITS_MODAL_VOICE_H_ */
