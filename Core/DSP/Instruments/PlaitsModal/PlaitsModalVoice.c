/*
 * Core/DSP/Instruments/PlaitsModal/PlaitsModalVoice.c
 *
 *  Created on: 22.10.2026
 * -----------------------------------------------------------------------------
 *  The modal DSP is a C translation of Plaits' ModalVoice, Resonator,
 *  ResonatorSvf, CosineOscillator and Limiter, with the lut_stiffness table
 *  (Plaits' Rings-derived modal engine). Copyright 2016 Emilie Gillet, MIT
 *  license (full notice in PlaitsModalVoice.h).
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * 24 band-pass modes tuned in Hz; each mode's Q and Nyquist attenuation use
 * its frequency as a fraction of 48 kHz, as in Plaits, so ring times and
 * spectra match at 44,108 Hz. `pos` (Rings' strike position) sets the mode
 * amplitudes; Plaits fixes it at 0.015 (row 0). Compiled -Ofast. All 24
 * modes run on every sample (constant CPU): this is an Advanced type.
 */

#include "PlaitsModalVoice.h"
#include "BufferTools.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "MidiPitch.h"
#include <math.h>

#define PM_FS           44108.0f
#define PM_PLAITS_FS    48000.0f
#define PM_TO_PLAITS    (PM_FS / PM_PLAITS_FS)
#define PM_PI           3.14159265f
#define PM_TAN_A        (3.260e-01f * PM_PI * PM_PI * PM_PI)
#define PM_TAN_B        (1.823e-01f * PM_PI * PM_PI * PM_PI * PM_PI * PM_PI)
/* ModalEngine smooths Harmonics with 0.01 per 12-sample Plaits block:
 * 1 - 0.99^(32 x 48000 / 44108 / 12) per 32-sample LXR block. */
#define PM_STRUCTURE_LP 0.02874f

/* Plaits lut_stiffness (65 entries, indexed by structure x 64). */
static const float pm_lut_stiffness[65] = {
	-6.250000000e-02f, -5.859375000e-02f, -5.468750000e-02f, -5.078125000e-02f,
	-4.687500000e-02f, -4.296875000e-02f, -3.906250000e-02f, -3.515625000e-02f,
	-3.125000000e-02f, -2.734375000e-02f, -2.343750000e-02f, -1.953125000e-02f,
	-1.562500000e-02f, -1.171875000e-02f, -7.812500000e-03f, -3.906250000e-03f,
	0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f, 0.000000000e+00f,
	1.009582073e-03f, 2.416076364e-03f, 4.002252878e-03f, 5.791066350e-03f,
	7.808404022e-03f, 1.008346028e-02f, 1.264915914e-02f, 1.554263074e-02f,
	1.880574864e-02f, 2.248573583e-02f, 2.663584813e-02f, 3.131614488e-02f,
	3.659435812e-02f, 4.254687278e-02f, 4.925983210e-02f, 5.683038428e-02f,
	6.536808837e-02f, 7.499649981e-02f, 8.585495846e-02f, 9.810060511e-02f,
	1.119106556e-01f, 1.274849653e-01f, 1.450489216e-01f, 1.648567056e-01f,
	1.871949702e-01f, 2.123869891e-01f, 2.407973346e-01f, 2.728371538e-01f,
	3.089701187e-01f, 3.497191360e-01f, 3.956739150e-01f, 4.474995013e-01f,
	5.059459012e-01f, 5.718589358e-01f, 6.461924814e-01f, 7.300222738e-01f,
	8.245614757e-01f, 9.311782340e-01f, 1.000037649e+00f, 1.005639154e+00f,
	1.048005353e+00f, 1.183990632e+00f, 1.457101344e+00f, 2.000000000e+00f,
	2.000000000e+00f,
};

static inline float pm_semitones(float s)
{
	return exp2f(s * (1.0f / 12.0f));
}

/* stmlib OnePole::tan<FREQUENCY_FAST>. */
static inline float pm_tan(float f)
{
	const float f2 = f * f;

	return f * (PM_PI + f2 * (PM_TAN_A + PM_TAN_B * f2));
}

/* Plaits NthHarmonicCompensation. */
static float pm_harmonicCompensation(int n, float stiffness)
{
	float stretch = 1.0f;
	int i;

	for (i = 0; i < n - 1; ++i) {
		stretch += stiffness;
		stiffness *= (stiffness < 0.0f) ? 0.93f : 0.98f;
	}
	return 1.0f / stretch;
}

/*
 * Mode amplitudes for one strike position: stmlib CosineOscillator
 * <COSINE_OSCILLATOR_APPROXIMATE> stepped once per mode, x 0.25, as
 * Resonator::Init. Inputs: voice, position (Plaits 0.015). Output: amp[].
 */
static void pm_setPosition(PlaitsModalVoice *voice, float position)
{
	float sign = 16.0f;
	float f = position - 0.25f;
	float coef;
	float y0;
	float y1;
	uint8_t i;

	if (f < 0.0f) {
		f = -f;
	} else if (f > 0.5f) {
		f -= 0.5f;
	} else {
		sign = -16.0f;
	}
	coef = sign * f * (1.0f - 2.0f * f);
	y1 = coef * 0.25f;
	y0 = 0.5f;
	for (i = 0u; i < PLAITS_MODAL_MODES; i++) {
		const float temp = y0;

		y0 = coef * y0 - y1;
		y1 = temp;
		voice->amp[i] = (temp + 0.5f) * 0.25f;
	}
	voice->amp_position = position;
}

void PlaitsModal_initVoice(PlaitsModalVoice *voice)
{
	if (!voice)
		return;
	voice->baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->note = 48u;
	voice->fine = 63u;
	voice->structure = 0.4f;
	voice->brightness = 0.5f;
	voice->damping = 0.5f;
	voice->position = 0.0f;
	voice->structure_lp = 0.4f;
	voice->limiter_peak = 0.5f;
	voice->vol = 0.8f;
	voice->pan = 64u;
	voice->volumeMod = 1u;
	voice->velo = 1.0f;
	voice->accent = 1.0f;
	pm_setPosition(voice, 0.015f);
	lfo_init(&voice->lfo);
	slopeEg2_init(&voice->oscVolEg);
	setDistortionShape(&voice->distortion, 0u);
}

void PlaitsModal_triggerVoice(PlaitsModalVoice *voice,
                              const uint8_t source_slot, const uint8_t vol,
                              const uint8_t note)
{
	/* Strike: the excitation impulse starts the next render block. */
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

void PlaitsModal_calcVoiceAsync(PlaitsModalVoice *voice)
{
	/* Harmonics smoothing (ModalEngine) and the shared amp envelope. */
	if (!voice)
		return;
	voice->structure_lp += PM_STRUCTURE_LP *
	                       (voice->structure - voice->structure_lp);
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

void PlaitsModal_calcVoiceSyncBlock(PlaitsModalVoice *voice, int16_t *buf,
                                    const uint8_t size)
{
	/*
	 * Render one block: ModalVoice::Render (non-sustain), Resonator::Process,
	 * Plaits' output limiter, then the shared LXR post-chain.
	 */
	static float exc[OUTPUT_DMA_SIZE];
	static float out[OUTPUT_DMA_SIZE];
	float g[PLAITS_MODAL_MODES];
	float rg[PLAITS_MODAL_MODES];
	float h[PLAITS_MODAL_MODES];
	float a[PLAITS_MODAL_MODES];
	float n;
	float f0_hz;
	float f0;
	float accent;
	float structure;
	float brightness;
	float damping;
	float position;
	uint8_t i;
	uint8_t m;

	if (!voice || !buf || size == 0u || size > OUTPUT_DMA_SIZE)
		return;

	n = (float)voice->note + (float)voice->baseNote -
	    (float)MIDI_DEFAULT_TRIGGER_NOTE +
	    ((float)voice->fine * (1.0f / 127.0f) - 0.5f);
	n = (n < 0.0f) ? 0.0f : ((n > 127.0f) ? 127.0f : n);
	f0_hz = 440.0f * pm_semitones(n - 69.0f);
	f0 = f0_hz * (1.0f / PM_FS);

	/* Rings' strike position: 0 = Plaits' 0.015, 127 = the middle. */
	position = 0.015f + voice->position * (0.5f - 0.015f);
	if (position != voice->amp_position)
		pm_setPosition(voice, position);

	accent = voice->accent;
	structure = voice->structure_lp;
	structure = (structure > 0.9999f) ? 0.9999f : structure;
	brightness = voice->brightness + 0.25f * accent *
	             (1.0f - voice->brightness);
	damping = voice->damping + 0.25f * accent * (1.0f - voice->damping);

	/* Excitation: one impulse through a 2 x f0-relative low-pass. */
	{
		const float f = 2.0f * f0;
		float cutoff = f * pm_semitones((brightness * (2.0f - brightness) -
		                                 0.5f) * 60.0f);
		float eg;
		float er;
		float eh;

		cutoff = (cutoff > 0.499f) ? 0.499f : cutoff;
		for (i = 0u; i < size; i++)
			exc[i] = 0.0f;
		if (voice->trigger) {
			const float amplitude = (0.12f + 0.08f * accent) *
			                        (1.0f - damping * 0.5f);

			voice->trigger = 0u;
			exc[0] = amplitude * pm_semitones(cutoff * cutoff * 24.0f) /
			         cutoff;
		}
		eg = pm_tan(cutoff);
		er = 1.0f / 1.5f;
		eh = 1.0f / (1.0f + er * eg + eg * eg);
		for (i = 0u; i < size; i++) {
			const float hp = (exc[i] - (er + eg) * voice->exc_s1 -
			                  voice->exc_s2) * eh;
			const float bp = eg * hp + voice->exc_s1;
			float lp;

			voice->exc_s1 = eg * hp + bp;
			lp = eg * bp + voice->exc_s2;
			voice->exc_s2 = eg * bp + lp;
			exc[i] = lp;
		}
	}

	/* Resonator::Process coefficients for the 24 modes. */
	{
		float stiffness;
		float index = structure * 64.0f;
		int idx = (int)index;
		float harmonic;
		float stretch = 1.0f;
		float q_sqrt;
		float q;
		float q_loss;
		float bright = brightness;

		stiffness = pm_lut_stiffness[idx] +
		            (pm_lut_stiffness[idx + 1] - pm_lut_stiffness[idx]) *
		            (index - (float)idx);
		f0 *= pm_harmonicCompensation(3, stiffness);
		harmonic = f0;
		q_sqrt = pm_semitones(damping * 79.7f);
		q = 500.0f * q_sqrt * q_sqrt;
		bright *= 1.0f - structure * 0.3f;
		bright *= 1.0f - damping * 0.3f;
		q_loss = bright * (2.0f - bright) * 0.85f + 0.15f;

		for (m = 0u; m < PLAITS_MODAL_MODES; m++) {
			float mf = harmonic * stretch;
			float mf_plaits;
			float r;

			mf = (mf >= 0.499f) ? 0.499f : mf;
			mf_plaits = mf * PM_TO_PLAITS;
			g[m] = pm_tan(mf);
			r = 1.0f / (1.0f + mf_plaits * q);
			h[m] = 1.0f / (1.0f + r * g[m] + g[m] * g[m]);
			rg[m] = r + g[m];
			a[m] = voice->amp[m] * (1.0f - mf_plaits * 2.0f);
			stretch += stiffness;
			stiffness *= (stiffness < 0.0f) ? 0.93f : 0.98f;
			harmonic += f0;
			q *= q_loss;
		}
	}

	/* ResonatorSvf band-pass bank, summed. */
	for (i = 0u; i < size; i++) {
		const float s_in = exc[i];
		float s_out = 0.0f;

		for (m = 0u; m < PLAITS_MODAL_MODES; m++) {
			const float hp = (s_in - rg[m] * voice->mode_s1[m] -
			                  voice->mode_s2[m]) * h[m];
			const float bp = g[m] * hp + voice->mode_s1[m];
			float lp;

			voice->mode_s1[m] = g[m] * hp + bp;
			lp = g[m] * bp + voice->mode_s2[m];
			voice->mode_s2[m] = g[m] * bp + lp;
			s_out += a[m] * bp;
		}
		out[i] = s_out;
	}

	/* Plaits post-processing for the modal engine: stmlib Limiter, x0.8. */
	for (i = 0u; i < size; i++) {
		const float s = out[i];
		const float mag = fabsf(s);
		const float err = mag - voice->limiter_peak;
		float y;

		voice->limiter_peak += ((err > 0.0f) ? 0.05f : 0.00002f) * err;
		y = s * ((voice->limiter_peak <= 1.0f) ? 1.0f
		                                       : 1.0f / voice->limiter_peak);
		y *= 0.8f * 32767.0f;
		y = (y > 32767.0f) ? 32767.0f : ((y < -32768.0f) ? -32768.0f : y);
		buf[i] = (int16_t)y;
	}

	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
}
