/*
 * Core/DSP/Instruments/PlaitsHat/PlaitsHatVoice.c
 *
 *  Created on: 22.10.2026
 * -----------------------------------------------------------------------------
 *  The hi-hat DSP is a C translation of Plaits' HiHat, SquareNoise,
 *  RingModNoise and band-limited Oscillator. Copyright 2016 Emilie Gillet,
 *  MIT license (full notice in PlaitsHatVoice.h).
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Frequencies stay in Hz, per-sample envelope factors are raised to
 * 48000/44108, and the ring-mod ratio uses f0 as a fraction of 48 kHz, so the
 * hats match Plaits at 44,108 Hz. Compiled -Ofast. The model row selects the
 * metallic source (an algorithm choice, like an oscillator waveform; budget
 * the `rng` model). Everything else runs on every sample.
 */

#include "PlaitsHatVoice.h"
#include "BufferTools.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "MidiPitch.h"
#include <math.h>

#define PH_FS            44108.0f
#define PH_PLAITS_FS     48000.0f
#define PH_RATE_RATIO    (PH_PLAITS_FS / PH_FS)
#define PH_OUT_GAIN      0.8f
#define PH_PI            3.14159265f

/* SquareNoise ratios (nominal f0: 414 Hz). */
static const float ph_square_ratio[PLAITS_HAT_OSCS] = {
	1.0f, 1.304f, 1.466f, 1.787f, 1.932f, 2.536f
};

/* RingModNoise pair frequencies in Hz: square a, saw b. */
static const float ph_ring_hz[3][2] = {
	{ 200.0f, 7530.0f }, { 510.0f, 8075.0f }, { 730.0f, 10500.0f }
};

static inline float ph_semitones(float s)
{
	return exp2f(s * (1.0f / 12.0f));
}

/* Svf::set_f_q<FREQUENCY_ACCURATE> (exact tangent; block rate). */
static inline void ph_setFq(float f, float resonance, float *g, float *r,
                            float *h)
{
	*g = tanf(PH_PI * f);
	*r = 1.0f / resonance;
	*h = 1.0f / (1.0f + *r * *g + *g * *g);
}

/* One SVF step; returns band-pass, writes high-pass. */
static inline float ph_svf(float in, float g, float r, float h, float *s1,
                           float *s2, float *hp_out)
{
	const float hp = (in - r * *s1 - g * *s1 - *s2) * h;
	const float bp = g * hp + *s1;
	float lp;

	*s1 = g * hp + bp;
	lp = g * bp + *s2;
	*s2 = g * bp + lp;
	*hp_out = hp;
	return bp;
}

/* Plaits Oscillator<SAW>, constant frequency, poly-BLEP. */
static inline float ph_saw(float *phase, float *next, float f, float inv_f)
{
	float this_sample = *next;
	float next_sample = 0.0f;

	*phase += f;
	if (*phase >= 1.0f) {
		float t;

		*phase -= 1.0f;
		t = *phase * inv_f;
		this_sample -= 0.5f * t * t;
		next_sample += 0.5f * (1.0f - t) * (1.0f - t);
	}
	next_sample += *phase;
	*next = next_sample;
	return 2.0f * this_sample - 1.0f;
}

/* Plaits Oscillator<SQUARE>, pw 0.5, constant frequency, poly-BLEP. */
static inline float ph_square(float *phase, float *next, uint8_t *high,
                              float f, float inv_f)
{
	float this_sample = *next;
	float next_sample = 0.0f;

	*phase += f;
	if (*high ^ (uint8_t)(*phase >= 0.5f)) {
		const float t = (*phase - 0.5f) * inv_f;

		this_sample += 0.5f * t * t;
		next_sample -= 0.5f * (1.0f - t) * (1.0f - t);
		*high = (uint8_t)(*phase >= 0.5f);
	}
	if (*phase >= 1.0f) {
		float t;

		*phase -= 1.0f;
		t = *phase * inv_f;
		this_sample -= 0.5f * t * t;
		next_sample += 0.5f * (1.0f - t) * (1.0f - t);
		*high = 0u;
	}
	next_sample += (*phase < 0.5f) ? 0.0f : 1.0f;
	*next = next_sample;
	return 2.0f * this_sample - 1.0f;
}

static void ph_resetOscillators(PlaitsHatVoice *voice)
{
	uint8_t i;

	for (i = 0u; i < PLAITS_HAT_OSCS; i++) {
		voice->sq_phase[i] = 0u;
		voice->rm_phase[i] = 0.5f;
		voice->rm_next[i] = 0.0f;
		voice->rm_high[i] = 1u;
	}
}

void PlaitsHat_initVoice(PlaitsHatVoice *voice, uint8_t seed_index)
{
	if (!voice)
		return;
	ph_resetOscillators(voice);
	voice->rng = 0x5EEDu + 0x9E3779B9u * (uint32_t)(seed_index + 1u);
	voice->baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->note = 60u;
	voice->fine = 63u;
	voice->model = PLAITS_HAT_MODEL_SQUARE;
	voice->noise = 0.3f;
	voice->tone = 0.5f;
	voice->decay = 0.3f;
	voice->decay_choke = 0.7f;
	voice->vol = 0.8f;
	voice->pan = 64u;
	voice->volumeMod = 1u;
	voice->velo = 1.0f;
	voice->accent = 1.0f;
	lfo_init(&voice->lfo);
	slopeEg2_init(&voice->oscVolEg);
	setDistortionShape(&voice->distortion, 0u);
}

void PlaitsHat_triggerVoice(PlaitsHatVoice *voice, const uint8_t source_slot,
                            const uint8_t vol, const uint8_t alternate,
                            const uint8_t note)
{
	/*
	 * Trigger one hat. alternate is nonzero when visible track 7 triggered
	 * slot 6: that hit uses the open (`_choke`) decay. Any later hit restarts
	 * the same envelope, so a closed hit chokes an open one.
	 */
	if (!voice)
		return;
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vol / 127.f);
	instrumentManager_applyVelocityModulationTarget(source_slot, vol / 127.f);
	voice->baseNote = note;
	voice->accent = vol / 127.f;
	voice->velo = vol / 127.f;
	voice->open = alternate ? 1u : 0u;
	voice->trigger = 1u;
	slopeEg2_trigger(&voice->oscVolEg);
}

void PlaitsHat_calcVoiceAsync(PlaitsHatVoice *voice)
{
	/* Shared amp envelope, as Drum_calcVoiceAsync (AMP_EG_SYNC == 0). */
	if (!voice)
		return;
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

void PlaitsHat_calcVoiceSyncBlock(PlaitsHatVoice *voice, int16_t *buf,
                                  const uint8_t size)
{
	/*
	 * Render one hat block: Plaits HiHat::Render (non-sustain) with the
	 * selected metallic source, then the shared LXR post-chain.
	 */
	const uint8_t ring = (voice && voice->model == PLAITS_HAT_MODEL_RING);
	float n;
	float f0_hz;
	float f0;
	float decay;
	float env_decay;
	float cut_decay;
	float cutoff;
	float noisiness;
	float noise_f;
	float bp_g, bp_r, bp_h;
	float hp_g, hp_r, hp_h;
	float sq_f[PLAITS_HAT_OSCS];
	uint32_t sq_inc[PLAITS_HAT_OSCS];
	float rm_f[PLAITS_HAT_OSCS];
	float rm_inv[PLAITS_HAT_OSCS];
	uint8_t i;
	uint8_t k;

	if (!voice || !buf || size == 0u || size > OUTPUT_DMA_SIZE)
		return;

	n = (float)voice->note + (float)voice->baseNote -
	    (float)MIDI_DEFAULT_TRIGGER_NOTE +
	    ((float)voice->fine * (1.0f / 127.0f) - 0.5f);
	n = (n < 0.0f) ? 0.0f : ((n > 127.0f) ? 127.0f : n);
	f0_hz = 440.0f * ph_semitones(n - 69.0f);
	f0 = f0_hz * (1.0f / PH_FS);

	decay = voice->open ? voice->decay_choke : voice->decay;
	env_decay = powf(1.0f - 0.003f * ph_semitones(-decay * 84.0f),
	                 PH_RATE_RATIO);
	cut_decay = powf(1.0f - 0.0025f * ph_semitones(-decay * 36.0f),
	                 PH_RATE_RATIO);
	if (voice->trigger) {
		voice->trigger = 0u;
		voice->envelope = (1.5f + 0.5f * (1.0f - decay)) *
		                  (0.3f + 0.7f * voice->accent);
	}

	/* Metallic source frequencies (the hat renders it at 2 x f0). */
	for (i = 0u; i < PLAITS_HAT_OSCS; i++) {
		float f = 2.0f * f0 * ph_square_ratio[i];

		f = (f >= 0.499f) ? 0.499f : f;
		sq_f[i] = f;
		sq_inc[i] = (uint32_t)(f * 4294967296.0f);
	}
	{
		const float f2_plaits = 2.0f * f0_hz * (1.0f / PH_PLAITS_FS);
		const float ratio = f2_plaits / (0.01f + f2_plaits);

		for (k = 0u; k < 3u; k++) {
			for (i = 0u; i < 2u; i++) {
				float f = ph_ring_hz[k][i] / PH_FS * ratio;

				f = (f < 0.000001f) ? 0.000001f : ((f > 0.25f) ? 0.25f : f);
				rm_f[2u * k + i] = f;
				rm_inv[2u * k + i] = 1.0f / f;
			}
		}
	}
	(void)sq_f;

	/* Colouration band-pass and output high-pass at the same cutoff. */
	cutoff = 150.0f / PH_FS * ph_semitones(voice->tone * 72.0f);
	cutoff = (cutoff > 16000.0f / PH_FS) ? 16000.0f / PH_FS : cutoff;
	ph_setFq(cutoff, ring ? 1.0f : 3.0f + 3.0f * voice->tone,
	         &bp_g, &bp_r, &bp_h);
	ph_setFq(cutoff, 0.5f, &hp_g, &hp_r, &hp_h);

	/* Clocked noise (not part of the 808, as in Plaits). */
	noisiness = voice->noise * voice->noise;
	noise_f = f0 * (16.0f + 16.0f * (1.0f - noisiness));
	noise_f = (noise_f > 0.5f) ? 0.5f : noise_f;

	for (i = 0u; i < size; i++) {
		float s;
		float hp;

		if (ring) {
			s = 0.0f;
			for (k = 0u; k < 3u; k++) {
				const float a = ph_square(&voice->rm_phase[2u * k],
				                          &voice->rm_next[2u * k],
				                          &voice->rm_high[2u * k],
				                          rm_f[2u * k], rm_inv[2u * k]);
				const float b = ph_saw(&voice->rm_phase[2u * k + 1u],
				                       &voice->rm_next[2u * k + 1u],
				                       rm_f[2u * k + 1u],
				                       rm_inv[2u * k + 1u]);

				s += a * b;
			}
		} else {
			uint32_t count = 0u;

			for (k = 0u; k < PLAITS_HAT_OSCS; k++) {
				voice->sq_phase[k] += sq_inc[k];
				count += voice->sq_phase[k] >> 31;
			}
			s = 0.33f * (float)count - 1.0f;
		}

		s = ph_svf(s, bp_g, bp_r, bp_h, &voice->bp_s1, &voice->bp_s2, &hp);

		voice->noise_clock += noise_f;
		if (voice->noise_clock >= 1.0f) {
			voice->noise_clock -= 1.0f;
			voice->rng = voice->rng * 1664525u + 1013904223u;
			voice->noise_sample =
				(float)voice->rng * (1.0f / 4294967296.0f) - 0.5f;
		}
		s += noisiness * (voice->noise_sample - s);

		/* VCA: two-stage linear (rng) or single-stage swing (sqr). */
		if (ring) {
			voice->envelope *= (voice->envelope > 0.5f) ? env_decay
			                                            : cut_decay;
			s *= voice->envelope;
		} else {
			voice->envelope *= env_decay;
			s *= (s > 0.0f) ? 4.0f : 0.1f;
			s = s / (1.0f + fabsf(s));
			s = (s + 0.1f) * voice->envelope;
		}

		(void)ph_svf(s, hp_g, hp_r, hp_h, &voice->hp_s1, &voice->hp_s2, &hp);
		s = hp * PH_OUT_GAIN * 32767.0f;
		s = (s > 32767.0f) ? 32767.0f : ((s < -32768.0f) ? -32768.0f : s);
		buf[i] = (int16_t)s;
	}

	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
}
