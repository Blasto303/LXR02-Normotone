/*
 * Core/DSP/Instruments/PlaitsSnare/PlaitsSnareVoice.c
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  The snare DSP is a C translation of Plaits' AnalogSnareDrum and
 *  SnareDrumEngine mapping. Copyright 2016 Emilie Gillet, MIT license (full
 *  notice in PlaitsSnareVoice.h).
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Plaits runs at 48 kHz. Times are seconds x the codec rate, per-sample
 * decay factors are raised to 48000/44108, and every Q uses f0 as a fraction
 * of 48 kHz as Plaits does, so pitch, ring and noise decay match Plaits at
 * 44,108 Hz. Compiled -Ofast. Constant CPU: all five modes, the noise and the
 * post-chain run on every sample.
 */

#include "PlaitsSnareVoice.h"
#include "BufferTools.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "MidiNoteNumbers.h"
#include <math.h>

/*
 * Rates and Plaits constants.
 *
 * PS_RATE_RATIO       48000 / 44108, the exponent that converts a Plaits
 *                     per-sample decay factor to the codec rate.
 * PS_TRIGGER_PULSE    1 ms trigger pulse (Q45/Q46).
 * PS_PULSE_DECAY_K    1 / 0.1 ms pulse decay.
 * PS_PULSE_LP_K       Plaits' 0.75 one-pole (R189/C57) at 44,108 Hz:
 *                     1 - 0.25^(48000/44108).
 * PS_OUT_GAIN         Plaits' snare post-processing gain.
 * PS_TAN_A, PS_TAN_B  stmlib FREQUENCY_FAST tangent coefficients.
 */
#define PS_FS               44108.0f
#define PS_PLAITS_FS        48000.0f
#define PS_RATE_RATIO       (PS_PLAITS_FS / PS_FS)
#define PS_TRIGGER_PULSE    ((int32_t)(1.0e-3f * PS_FS))
#define PS_PULSE_DECAY_K    (1.0f / (0.1e-3f * PS_FS))
#define PS_PULSE_LP_K       0.77875f
#define PS_OUT_GAIN         0.8f
#define PS_PI               3.14159265f
#define PS_TAN_A            (3.260e-01f * PS_PI * PS_PI * PS_PI)
#define PS_TAN_B            (1.823e-01f * PS_PI * PS_PI * PS_PI * PS_PI * PS_PI)

static const float ps_mode_ratio[PLAITS_SNARE_MODES] = {
	1.00f, 2.00f, 3.18f, 4.16f, 5.62f
};

/* stmlib SemitonesToRatio. */
static inline float ps_semitones(float s)
{
	return exp2f(s * (1.0f / 12.0f));
}

/* stmlib OnePole::tan<FREQUENCY_FAST>. */
static inline float ps_tan(float f)
{
	const float f2 = f * f;

	return f * (PS_PI + f2 * (PS_TAN_A + PS_TAN_B * f2));
}

/* stmlib SoftClip. */
static inline float ps_softClip(float x)
{
	if (x < -3.0f)
		return -1.0f;
	if (x > 3.0f)
		return 1.0f;
	return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* Svf::set_f_q: g = tan(pi f), r = 1/Q, h = 1/(1 + r g + g^2). */
static inline void ps_setFq(float f, float resonance, float *g, float *r,
                            float *h)
{
	*g = ps_tan(f);
	*r = 1.0f / resonance;
	*h = 1.0f / (1.0f + *r * *g + *g * *g);
}

/* Svf::Process<FILTER_MODE_BAND_PASS>. */
static inline float ps_bandPass(float in, float g, float r, float h,
                                float *s1, float *s2)
{
	const float hp = (in - r * *s1 - g * *s1 - *s2) * h;
	const float bp = g * hp + *s1;
	float lp;

	*s1 = g * hp + bp;
	lp = g * bp + *s2;
	*s2 = g * bp + lp;
	return bp;
}

void PlaitsSnare_initVoice(PlaitsSnareVoice *voice, uint8_t seed_index)
{
	/*
	 * Initialize one snare instance with silent state and neutral rows.
	 * seed_index decorrelates the noise of two snares in one Kit.
	 */
	if (!voice)
		return;
	voice->rng = 0x21u + 0x9E3779B9u * (uint32_t)(seed_index + 1u);
	voice->baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->note = 55u;
	voice->fine = 63u;
	voice->snappy = 0.5f;
	voice->tone = 0.5f;
	voice->decay = 0.5f;
	voice->vol = 0.8f;
	voice->pan = 64u;
	voice->volumeMod = 1u;
	voice->velo = 1.0f;
	voice->accent = 1.0f;
	lfo_init(&voice->lfo);
	slopeEg2_init(&voice->oscVolEg);
	setDistortionShape(&voice->distortion, 0u);
	PlaitsSnare_calcVoiceAsync(voice);
}

void PlaitsSnare_triggerVoice(PlaitsSnareVoice *voice,
                              const uint8_t source_slot, const uint8_t vol,
                              const uint8_t note)
{
	/*
	 * Trigger one snare instance: LFO retrigger and velocity modulation for
	 * the source slot, Plaits accent = velocity, and the shared amp envelope.
	 * The pulse and noise burst start in the next render block.
	 */
	if (!voice)
		return;
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vol / 127.f);
	instrumentManager_applyVelocityModulationTarget(source_slot, vol / 127.f);
	voice->baseNote = note;
	voice->accent = vol / 127.f;
	voice->velo = vol / 127.f;
	voice->trigger = 1u;
	slopeEg2_trigger(&voice->oscVolEg);
}

void PlaitsSnare_calcVoiceAsync(PlaitsSnareVoice *voice)
{
	/*
	 * Control-rate block: Plaits AnalogSnareDrum coefficients and the shared
	 * amp envelope. Snappy is Plaits' Harmonics, Tone its Timbre, Decay its
	 * Morph. Pitch is note + (trigger note - default note) + fine.
	 */
	float n;
	float f0_hz;
	float f0;
	float f0_plaits;
	float decay;
	float decay_xt;
	float q;
	float snappy;
	float tone;
	uint8_t i;

	if (!voice)
		return;
	n = (float)voice->note + (float)voice->baseNote -
	    (float)MIDI_DEFAULT_TRIGGER_NOTE +
	    ((float)voice->fine * (1.0f / 127.0f) - 0.5f);
	n = (n < 0.0f) ? 0.0f : ((n > 127.0f) ? 127.0f : n);
	f0_hz = 440.0f * ps_semitones(n - 69.0f);
	f0 = f0_hz * (1.0f / PS_FS);
	f0_plaits = f0_hz * (1.0f / PS_PLAITS_FS);

	decay = voice->decay;
	snappy = voice->snappy;
	decay_xt = decay * (1.0f + decay * (decay - 1.0f));
	q = 2000.0f * ps_semitones(decay_xt * 84.0f);
	voice->noise_decay = powf(1.0f - 0.0017f *
	                          ps_semitones(-decay * (50.0f + snappy * 10.0f)),
	                          PS_RATE_RATIO);
	voice->exciter_leak = snappy * (2.0f - snappy) * 0.1f;
	snappy = snappy * 1.1f - 0.05f;
	voice->snappy_mix = (snappy < 0.0f) ? 0.0f
		: ((snappy > 1.0f) ? 1.0f : snappy);

	/* Shell modes: Q from the 48 kHz fraction, as in Plaits. */
	for (i = 0u; i < PLAITS_SNARE_MODES; i++) {
		float f = f0 * ps_mode_ratio[i];
		float fp = f0_plaits * ps_mode_ratio[i];

		f = (f > 0.499f) ? 0.499f : f;
		fp = (fp > 0.499f) ? 0.499f : fp;
		ps_setFq(f, 1.0f + fp * (i == 0u ? q : q * 0.25f),
		         &voice->mode_g[i], &voice->mode_r[i], &voice->mode_h[i]);
	}

	tone = voice->tone;
	if (tone < 0.666667f) {
		/* 808-style (2 modes). */
		tone *= 1.5f;
		voice->mode_gain[0] = 1.5f + (1.0f - tone) * (1.0f - tone) * 4.5f;
		voice->mode_gain[1] = 2.0f * tone + 0.15f;
		for (i = 2u; i < PLAITS_SNARE_MODES; i++)
			voice->mode_gain[i] = 0.0f;
	} else {
		/* "What the 808 could have been if there were extra modes!" */
		tone = (tone - 0.666667f) * 3.0f;
		voice->mode_gain[0] = 1.5f - tone * 0.5f;
		voice->mode_gain[1] = 2.15f - tone * 0.7f;
		for (i = 2u; i < PLAITS_SNARE_MODES; i++) {
			voice->mode_gain[i] = tone;
			tone *= tone;
		}
	}

	{
		float fn = f0 * 16.0f;
		float fnp = f0_plaits * 16.0f;

		fn = (fn > 0.499f) ? 0.499f : fn;
		fnp = (fnp > 0.499f) ? 0.499f : fnp;
		ps_setFq(fn, 1.0f + fnp * 1.5f,
		         &voice->noise_g, &voice->noise_r, &voice->noise_h);
	}

	/* Shared amp envelope, as Drum_calcVoiceAsync (AMP_EG_SYNC == 0). */
	if ((voice->oscVolEg.attack == 1) &&
	    ((voice->oscVolEg.state == EG_A) ||
	     (voice->oscVolEg.state == EG_REPEAT))) {
		voice->ampFilterInput = slopeEg2_calc(&voice->oscVolEg);
		voice->lastGain = voice->ampFilterInput;
	} else {
		voice->lastGain = voice->ampFilterInput;
		voice->ampFilterInput = slopeEg2_calc(&voice->oscVolEg);
	}
}

void PlaitsSnare_calcVoiceSyncBlock(PlaitsSnareVoice *voice, int16_t *buf,
                                    const uint8_t size)
{
	/*
	 * Render one snare block into a mono, PRE-VOLUME int16 buffer: Plaits
	 * AnalogSnareDrum::Render (non-sustain path), then the shared LXR
	 * post-chain. Channel volume is applied by the mixer.
	 */
	uint8_t i;
	uint8_t m;

	if (!voice || !buf || size == 0u || size > OUTPUT_DMA_SIZE)
		return;

	if (voice->trigger) {
		voice->trigger = 0u;
		voice->pulse_remaining = PS_TRIGGER_PULSE;
		voice->pulse_height = 3.0f + 7.0f * voice->accent;
		voice->noise_envelope = 2.0f;
	}

	for (i = 0u; i < size; i++) {
		float pulse;
		float shell = 0.0f;
		float noise;
		float s;

		/* Q45 / Q46 */
		if (voice->pulse_remaining) {
			--voice->pulse_remaining;
			pulse = voice->pulse_remaining ? voice->pulse_height
			                               : voice->pulse_height - 1.0f;
			voice->pulse = pulse;
		} else {
			voice->pulse *= 1.0f - PS_PULSE_DECAY_K;
			pulse = voice->pulse;
		}

		/* R189 / C57 / R190 + C58 / C59 / R197 / R196 / IC14 */
		voice->pulse_lp += PS_PULSE_LP_K * (pulse - voice->pulse_lp);

		for (m = 0u; m < PLAITS_SNARE_MODES; m++) {
			const float excitation = (m == 0u)
				? (pulse - voice->pulse_lp) + 0.006f * pulse
				: 0.026f * pulse;

			shell += voice->mode_gain[m] *
				(ps_bandPass(excitation, voice->mode_g[m], voice->mode_r[m],
				             voice->mode_h[m], &voice->mode_s1[m],
				             &voice->mode_s2[m]) +
				 excitation * voice->exciter_leak);
		}
		shell = ps_softClip(shell);

		/* C56 / R194 / Q48 / C54 / R188 / D54 (stmlib Random::GetFloat) */
		voice->rng = voice->rng * 1664525u + 1013904223u;
		noise = 2.0f * ((float)voice->rng * (1.0f / 4294967296.0f)) - 1.0f;
		noise = (noise < 0.0f) ? 0.0f : noise;
		voice->noise_envelope *= voice->noise_decay;
		noise *= voice->noise_envelope * voice->snappy_mix * 2.0f;

		/* C66 / R201 / C67 / R202 / R203 / Q49 */
		noise = ps_bandPass(noise, voice->noise_g, voice->noise_r,
		                    voice->noise_h, &voice->noise_s1,
		                    &voice->noise_s2);

		/* IC13 */
		s = (noise + shell * (1.0f - voice->snappy_mix)) *
		    PS_OUT_GAIN * 32767.0f;
		s = (s > 32767.0f) ? 32767.0f : ((s < -32768.0f) ? -32768.0f : s);
		buf[i] = (int16_t)s;
	}

	/* Shared LXR post-chain: amp envelope ramp, velocity, distortion. */
	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
}
