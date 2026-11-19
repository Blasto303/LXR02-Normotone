/*
 * Core/DSP/Instruments/PlaitsKick/PlaitsKickVoice.c
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  The kick DSP is a C translation of Plaits' AnalogBassDrum,
 *  SyntheticBassDrum, BassDrumEngine parameter mapping and Overdrive. Copyright 2016 Emilie Gillet, MIT license
 *  (full notice in PlaitsKickVoice.h).
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Plaits runs at 48 kHz; every time constant here is in seconds times the
 * codec rate, and the resonator Q and excitation scale are computed from f0 in
 * Hz, so pitch, decay and level match Plaits at 44,108 Hz. Compiled -Ofast.
 * Constant CPU: the resonator, overdrive and post-chain run on every sample
 * whether or not the kick is sounding.
 */

#include "plaitsOut.h"
#include "PlaitsKickVoice.h"
#include "BufferTools.h"
#include "modulationNode.h"
#include "InstrumentManager.h"
#include "voicePostChain.h"
#include "MidiPitch.h"
#include <math.h>

/*
 * Rates and Plaits constants.
 *
 * PK_FS                codec rate.
 * PK_PLAITS_FS         Plaits' rate, used where its formulas take f0 as a
 *                      fraction of 48 kHz (Q, excitation scale, drive limit).
 * PK_TRIGGER_PULSE     1 ms trigger pulse (Q39/Q40).
 * PK_FM_PULSE          6 ms attack FM pulse (Q41/Q42).
 * PK_PULSE_DECAY_K     1 / 0.2 ms pulse decay.
 * PK_PULSE_FILTER_K    1 / 0.1 ms pulse low-pass (C40, C39).
 * PK_RETRIG_K          1 / 50 ms retrigger pulse decay (C39/R161).
 * PK_OUT_GAIN          Plaits' bass drum post-processing gain.
 * PK_TAN_A             stmlib FREQUENCY_DIRTY tangent coefficient.
 */
#define PK_FS               44108.0f
#define PK_PLAITS_FS        48000.0f
#define PK_TRIGGER_PULSE    ((int32_t)(1.0e-3f * PK_FS))
#define PK_FM_PULSE         ((int32_t)(6.0e-3f * PK_FS))
#define PK_PULSE_DECAY_K    (1.0f / (0.2e-3f * PK_FS))
#define PK_PULSE_FILTER_K   (1.0f / (0.1e-3f * PK_FS))
#define PK_RETRIG_K         (1.0f / (0.05f * PK_FS))
#define PK_OUT_GAIN         0.8f
#define PK_PI               3.14159265f
#define PK_TAN_A            (3.736e-01f * PK_PI * PK_PI * PK_PI)
/*
 * SyntheticBassDrum constants. Plaits' fixed per-sample one-pole and slope
 * coefficients are re-derived for the codec rate: c' = 1 - (1 - c)^(48/44.1).
 * PKS_TAN_A/B          stmlib FREQUENCY_FAST tangent coefficients.
 * PKS_CLICK_G/R/H      click low-pass Svf, 5 kHz, Q 2 (FREQUENCY_FAST).
 */
#define PK_RATE_RATIO       (PK_PLAITS_FS / PK_FS)
#define PKS_ENV_LP          0.10833f    /* ONE_POLE 0.1    */
#define PKS_PHASE_NOISE_LP  0.0021763f  /* ONE_POLE 0.002  */
#define PKS_NOISE_LP        0.05429f    /* ONE_POLE 0.05   */
#define PKS_NOISE_HP        0.0054400f  /* ONE_POLE 0.005  */
#define PKS_CLICK_HP        0.043452f   /* ONE_POLE 0.04   */
#define PKS_CLICK_UP        0.52966f    /* SLOPE +0.5      */
#define PKS_CLICK_DOWN      0.10833f    /* SLOPE -0.1      */
#define PKS_BODY_PULSE      ((int32_t)(1.0e-3f * PK_FS))
#define PKS_FM_PULSE        ((int32_t)(1.3e-3f * PK_FS))
#define PKS_TRANSIENT_DECAY (1.0f - 1.0f / (0.005f * PK_FS))
#define PKS_TAN_A           (3.260e-01f * PK_PI * PK_PI * PK_PI)
#define PKS_TAN_B           (1.823e-01f * PK_PI * PK_PI * PK_PI * PK_PI * PK_PI)

/* stmlib SemitonesToRatio. */
static inline float pk_semitones(float s)
{
	return exp2f(s * (1.0f / 12.0f));
}

/* Plaits Diode (D83 and the R170 leakage). */
static inline float pk_diode(float x)
{
	float y;

	if (x >= 0.0f)
		return x;
	y = 2.0f * x;
	return 0.7f * y / (1.0f + fabsf(y));
}

/* stmlib SoftClip: SoftLimit on +-3, then +-1. */
static inline float pk_softClip(float x)
{
	if (x < -3.0f)
		return -1.0f;
	if (x > 3.0f)
		return 1.0f;
	return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* sin(2 pi x) for x in [0, 1): odd 7th-order fit, |error| < 2e-4. */
static inline float pk_sin2pi(float x)
{
	float y;
	float y2;

	x = (x < 0.5f) ? x : x - 1.0f;          /* [-0.5, 0.5) */
	y = (x > 0.25f) ? 0.5f - x : ((x < -0.25f) ? -0.5f - x : x);
	y *= 2.0f * PK_PI;
	y2 = y * y;
	return y * (1.0f - y2 * (0.16666667f - y2 * (0.0083333f -
	            y2 * 0.00019841f)));
}

/* SyntheticBassDrum::DistortedSine. */
static inline float pk_distortedSine(float phase, float phase_noise,
                                     float dirtiness)
{
	float triangle;
	float sine;
	float clean;
	float c;

	phase += phase_noise * dirtiness;
	phase -= floorf(phase);
	triangle = (phase < 0.5f ? phase : 1.0f - phase) * 4.0f - 1.0f;
	sine = 2.0f * triangle / (1.0f + fabsf(triangle));
	c = phase + 0.75f;
	clean = pk_sin2pi(c - (float)(int32_t)c);
	return sine + (1.0f - dirtiness) * (clean - sine);
}

/* SyntheticBassDrum::TransistorVCA. */
static inline float pk_transistorVca(float s, float gain)
{
	s = (s - 0.6f) * gain;
	return 3.0f * s / (2.0f + fabsf(s)) + gain * 0.3f;
}

/* SyntheticBassDrum::Render, non-sustain path, into out[]. */
static void pk_renderSynth(PlaitsKickVoice *voice, float *out, uint8_t size)
{
	/* Click Svf: 5 kHz low-pass, Q 2, FREQUENCY_FAST at the codec rate. */
	const float cf = 5000.0f / PK_FS;
	const float cf2 = cf * cf;
	const float cg = cf * (PK_PI + cf2 * (PKS_TAN_A + PKS_TAN_B * cf2));
	const float cr = 0.5f;
	const float ch = 1.0f / (1.0f + cr * cg + cg * cg);
	const float f0 = voice->f0;
	const float tone_level = voice->tone;
	uint8_t i;

	if (voice->trigger) {
		voice->trigger = 0u;
		voice->s_fm = 1.0f;
		voice->s_body_env = voice->s_transient_env =
			0.3f + 0.7f * voice->accent;
		voice->s_body_pw = PKS_BODY_PULSE;
		voice->s_fm_pw = PKS_FM_PULSE;
	}

	for (i = 0u; i < size; i++) {
		float noise;
		float mix;
		float body;
		float click_in;
		float transient;
		float hp;
		float bp;

		voice->rng = voice->rng * 1664525u + 1013904223u;
		noise = (float)voice->rng * (1.0f / 4294967296.0f);
		voice->s_phase_noise += PKS_PHASE_NOISE_LP *
		                        (noise - 0.5f - voice->s_phase_noise);

		if (voice->s_fm_pw) {
			--voice->s_fm_pw;
			voice->s_phase = 0.25f;
		} else {
			float inc;

			voice->s_fm *= voice->s_fm_decay;
			inc = f0 * (1.0f + voice->s_fm_amount * 3.5f * voice->s_fm_lp);
			voice->s_phase += (inc < 0.5f) ? inc : 0.5f;
			if (voice->s_phase >= 1.0f)
				voice->s_phase -= 1.0f;
		}

		if (voice->s_body_pw) {
			--voice->s_body_pw;
		} else {
			voice->s_body_env *= voice->s_body_decay;
			voice->s_transient_env *= PKS_TRANSIENT_DECAY;
		}

		voice->s_body_env_lp += PKS_ENV_LP *
		                        (voice->s_body_env - voice->s_body_env_lp);
		voice->s_transient_env_lp += PKS_ENV_LP *
			(voice->s_transient_env - voice->s_transient_env_lp);
		voice->s_fm_lp += PKS_ENV_LP * (voice->s_fm - voice->s_fm_lp);

		body = pk_distortedSine(voice->s_phase, voice->s_phase_noise,
		                        voice->s_dirt);

		/* SyntheticBassDrumClick: slope, one-pole high-pass, Svf LP. */
		click_in = voice->s_body_pw ? 0.0f : 1.0f;
		{
			const float e = click_in - voice->s_click_lp;

			voice->s_click_lp += (e > 0.0f ? PKS_CLICK_UP : PKS_CLICK_DOWN) * e;
		}
		voice->s_click_hp += PKS_CLICK_HP *
		                     (voice->s_click_lp - voice->s_click_hp);
		hp = ((voice->s_click_lp - voice->s_click_hp) -
		      cr * voice->s_click_s1 - cg * voice->s_click_s1 -
		      voice->s_click_s2) * ch;
		bp = cg * hp + voice->s_click_s1;
		voice->s_click_s1 = cg * hp + bp;
		transient = cg * bp + voice->s_click_s2;
		voice->s_click_s2 = cg * bp + transient;

		/* SyntheticBassDrumAttackNoise. */
		voice->rng = voice->rng * 1664525u + 1013904223u;
		noise = (float)voice->rng * (1.0f / 4294967296.0f);
		voice->s_noise_lp += PKS_NOISE_LP * (noise - voice->s_noise_lp);
		voice->s_noise_hp += PKS_NOISE_HP *
		                     (voice->s_noise_lp - voice->s_noise_hp);
		transient += voice->s_noise_lp - voice->s_noise_hp;

		mix = -pk_transistorVca(body, voice->s_body_env_lp);
		mix -= transient * voice->s_transient_env_lp * tone_level;

		voice->s_tone_lp += voice->tone_f * (mix - voice->s_tone_lp);
		out[i] = voice->s_tone_lp;
	}
}

void PlaitsKick_initVoice(PlaitsKickVoice *voice)
{
	/*
	 * Initialize one kick instance with silent DSP state and neutral rows.
	 * Instrument files then apply the stored values. The amp envelope is the
	 * shared LXR SlopeEg2 so Scene switching and track-7 decay work as for
	 * every engine.
	 */
	if (!voice)
		return;
	voice->baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->note = 31u;
	voice->fine = 63u;
	voice->harmonics = 0.3f;
	voice->tone = 0.5f;
	voice->decay = 0.5f;
	voice->vol = 0.8f;
	voice->pan = 64u;
	voice->volumeMod = 1u;
	voice->velo = 1.0f;
	voice->accent = 1.0f;
	voice->model = PLAITS_KICK_MODEL_ANALOG;
	voice->rng = 0x5EEDu;
	lfo_init(&voice->lfo);
	slopeEg2_init(&voice->oscVolEg);
	setDistortionShape(&voice->distortion, 0u);
	PlaitsKick_calcVoiceAsync(voice);
	voice->od_pre = 0.0f;
	voice->od_post = 0.0f;
}

void PlaitsKick_triggerVoice(PlaitsKickVoice *voice, const uint8_t source_slot,
                             const uint8_t vol, const uint8_t note)
{
	/*
	 * Trigger one kick instance.
	 *
	 * Inputs: instance, logical source slot (LFO retrigger and velocity
	 * modulation), velocity and note. Output: the next render block starts
	 * the Plaits trigger and FM pulses (accent = velocity, as Plaits' accent
	 * input), and the shared amp envelope restarts.
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

void PlaitsKick_calcVoiceAsync(PlaitsKickVoice *voice)
{
	/*
	 * Control-rate block: Plaits BassDrumEngine parameter mapping and the
	 * shared amp envelope.
	 *
	 * Pitch is note + (trigger note - default note) with a +-half-semitone
	 * fine tune, as the LXR oscillators. Harmonics sweeps attack FM, then self
	 * FM, then the overdrive; Tone is Plaits' Timbre; Decay is its Morph.
	 */
	float n;
	float f0_hz;
	float f0_plaits;
	float h4;

	if (!voice)
		return;
	n = (float)voice->note + (float)voice->baseNote -
	    (float)MIDI_DEFAULT_TRIGGER_NOTE +
	    ((float)voice->fine * (1.0f / 127.0f) - 0.5f);
	n = (n < 0.0f) ? 0.0f : ((n > 127.0f) ? 127.0f : n);
	f0_hz = 440.0f * pk_semitones(n - 69.0f);
	f0_plaits = f0_hz * (1.0f / PK_PLAITS_FS);
	voice->f0 = f0_hz * (1.0f / PK_FS);

	/* Q = 1 + q * f must match Plaits at the same f0 in Hz. */
	voice->q = 1500.0f * pk_semitones(voice->decay * 80.0f) *
	           (PK_FS / PK_PLAITS_FS);
	voice->scale = 0.001f / f0_plaits;
	voice->tone_f = 4.0f * voice->f0 * pk_semitones(voice->tone * 108.0f);
	voice->tone_f = (voice->tone_f > 1.0f) ? 1.0f : voice->tone_f;
	voice->exciter_leak = 0.08f * (voice->tone + 0.25f);

	h4 = voice->harmonics * 4.0f;
	voice->attack_fm = (h4 < 1.0f) ? h4 : 1.0f;
	voice->self_fm = h4 - 1.0f;
	voice->self_fm = (voice->self_fm < 0.0f) ? 0.0f
		: ((voice->self_fm > 1.0f) ? 1.0f : voice->self_fm);
	voice->drive = voice->harmonics * 2.0f - 1.0f;
	voice->drive = (voice->drive < 0.0f) ? 0.0f : voice->drive;
	{
		const float limit = 1.0f - 16.0f * f0_plaits;

		voice->drive *= (limit < 0.0f) ? 0.0f : limit;
	}

	/*
	 * SyntheticBassDrum (BassDrumEngine AUX): Tone = timbre, Decay = morph,
	 * dirtiness = 0.4 - 0.25 morph^2, FM envelope amount and decay from
	 * Harmonics. Time constants are in seconds x the codec rate.
	 */
	if (voice->model == PLAITS_KICK_MODEL_SYNTH) {
		const float d = voice->decay * voice->decay;
		float fm_env_decay = voice->harmonics * 2.0f - 1.0f;
		float dirt = 0.4f - 0.25f * d;
		const float dlim = 1.0f - 8.0f * f0_plaits;

		fm_env_decay = (fm_env_decay < 0.0f) ? 0.0f : fm_env_decay;
		fm_env_decay *= fm_env_decay;
		voice->s_fm_amount = voice->harmonics * 2.0f;
		voice->s_fm_amount = (voice->s_fm_amount > 1.0f) ? 1.0f
		                                                 : voice->s_fm_amount;
		dirt *= (dlim < 0.0f) ? 0.0f : dlim;
		voice->s_dirt = dirt;
		voice->s_fm_decay = 1.0f - 1.0f /
			(0.008f * (1.0f + fm_env_decay * 4.0f) * PK_FS);
		voice->s_body_decay = 1.0f - 1.0f / (0.02f * PK_FS) *
			pk_semitones(-d * 60.0f);
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

void PlaitsKick_calcVoiceSyncBlock(PlaitsKickVoice *voice, int16_t *buf,
                                   const uint8_t size)
{
	/*
	 * Render one kick block into a mono, PRE-VOLUME int16 buffer.
	 *
	 * Plaits AnalogBassDrum::Render (non-sustain path) and Overdrive::Process,
	 * then the shared LXR post-chain (amp envelope ramp, velocity, distortion).
	 * Channel volume is applied by the mixer.
	 */
	static float out[OUTPUT_DMA_SIZE];
	float pre_gain;
	float post_gain;
	float pre_inc;
	float post_inc;
	float pre;
	float post;
	uint8_t i;

	if (!voice || !buf || size == 0u || size > OUTPUT_DMA_SIZE)
		return;

	if (voice->model == PLAITS_KICK_MODEL_SYNTH) {
		/* Plaits' AUX: no overdrive, the engine's 0.8 output gain. */
		pk_renderSynth(voice, out, size);
		for (i = 0u; i < size; i++) {
			float s = out[i] * PK_OUT_GAIN * 32767.0f;

			buf[i] = plaitsOut_toInt16(s);
		}
		voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
		               voice->volumeMod ? voice->velo : 1.0f,
		               &voice->distortion, size);
		return;
	}

	if (voice->trigger) {
		voice->trigger = 0u;
		voice->pulse_remaining = PK_TRIGGER_PULSE;
		voice->fm_pulse_remaining = PK_FM_PULSE;
		voice->pulse_height = 3.0f + 7.0f * voice->accent;
		voice->lp_out = 0.0f;
	}

	for (i = 0u; i < size; i++) {
		float pulse;
		float fm_pulse;
		float punch;
		float f;
		float g;
		float r;
		float h;
		float hp;
		float bp;
		float in;

		/* Q39 / Q40 */
		if (voice->pulse_remaining) {
			--voice->pulse_remaining;
			pulse = voice->pulse_remaining ? voice->pulse_height
			                               : voice->pulse_height - 1.0f;
			voice->pulse = pulse;
		} else {
			voice->pulse *= 1.0f - PK_PULSE_DECAY_K;
			pulse = voice->pulse;
		}

		/* C40 / R163 / R162 / D83 */
		voice->pulse_lp += PK_PULSE_FILTER_K * (pulse - voice->pulse_lp);
		pulse = pk_diode((pulse - voice->pulse_lp) + pulse * 0.044f);

		/* Q41 / Q42 */
		fm_pulse = 0.0f;
		if (voice->fm_pulse_remaining) {
			--voice->fm_pulse_remaining;
			fm_pulse = 1.0f;
			/* C39 / C52 */
			voice->retrig_pulse = voice->fm_pulse_remaining ? 0.0f : -0.8f;
		} else {
			/* C39 / R161 */
			voice->retrig_pulse *= 1.0f - PK_RETRIG_K;
		}
		voice->fm_pulse_lp += PK_PULSE_FILTER_K *
		                      (fm_pulse - voice->fm_pulse_lp);

		/* Q43 and R170 leakage */
		punch = 0.7f + pk_diode(10.0f * voice->lp_out - 1.0f);

		/* Q43 / R165 */
		f = voice->f0 * (1.0f + voice->fm_pulse_lp * 1.7f * voice->attack_fm +
		                 punch * 0.08f * voice->self_fm);
		f = (f < 0.0f) ? 0.0f : ((f > 0.4f) ? 0.4f : f);

		/* stmlib Svf, set_f_q<FREQUENCY_DIRTY>, band-pass and low-pass. */
		g = f * (PK_PI + PK_TAN_A * f * f);
		r = 1.0f / (1.0f + voice->q * f);
		h = 1.0f / (1.0f + r * g + g * g);
		in = (pulse - voice->retrig_pulse * 0.2f) * voice->scale;
		hp = (in - r * voice->svf_s1 - g * voice->svf_s1 - voice->svf_s2) * h;
		bp = g * hp + voice->svf_s1;
		voice->svf_s1 = g * hp + bp;
		voice->lp_out = g * bp + voice->svf_s2;
		voice->svf_s2 = g * bp + voice->lp_out;

		voice->tone_lp += voice->tone_f *
		                  (pulse * voice->exciter_leak + bp - voice->tone_lp);
		out[i] = voice->tone_lp;
	}

	/* Plaits Overdrive, gains interpolated across the block. */
	{
		const float drive = 0.5f + 0.5f * voice->drive;
		const float drive_2 = drive * drive;
		const float pre_a = drive * 0.5f;
		const float pre_b = drive_2 * drive_2 * drive * 24.0f;
		const float drive_squashed = drive * (2.0f - drive);

		pre_gain = pre_a + (pre_b - pre_a) * drive_2;
		post_gain = 1.0f / pk_softClip(0.33f + drive_squashed *
		                               (pre_gain - 0.33f));
	}
	pre = voice->od_pre;
	post = voice->od_post;
	pre_inc = (pre_gain - pre) / (float)size;
	post_inc = (post_gain - post) / (float)size;
	for (i = 0u; i < size; i++) {
		float s;

		pre += pre_inc;
		post += post_inc;
		s = pk_softClip(pre * out[i]) * post * PK_OUT_GAIN * 32767.0f;
		buf[i] = plaitsOut_toInt16(s);
	}
	voice->od_pre = pre_gain;
	voice->od_post = post_gain;

	/* Shared LXR post-chain: amp envelope ramp, velocity, distortion. */
	voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
	               voice->volumeMod ? voice->velo : 1.0f,
	               &voice->distortion, size);
}
