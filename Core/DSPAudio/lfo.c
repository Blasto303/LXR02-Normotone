/*
 * lfo.c
 *
 *  Created on: 14.01.2013
 *  Modified on 17.05.2026 by Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

//-------------------------------------------------------------
#include "lfo.h"
#include "random.h"
#include <math.h>

#include "DrumVoice.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "Snare.h"
#include "sequencer.h"
#include "valueShaper.h"
#include "InstrumentManager.h"
//-------------------------------------------------------------
void lfo_init(Lfo *lfo)
{
	lfo->phase 			= 0;
	lfo->phaseInc 		= 1;
	lfo->phaseOffset	= 0;
	lfo->retrigger		= 0;
	lfo->delay			= 0;
	lfo->waveform		= SINE;
	lfo->sync			= 0;
	lfo->freq			= 1;
	lfo->modNodeValue	= 1;
	lfo->polarity		= MOD_NODE_POLARITY_NEGATIVE;

	modNode_init(&lfo->modTarget);
	modNode_init(&lfo->modTarget2);
}
//-------------------------------------------------------------
float lfo_calc(Lfo *lfo)
{
	uint32_t oldPhase 		= lfo->phase;
	float inc = lfo->phaseInc*lfo->modNodeValue;
	uint32_t incInt = inc;
	lfo->phase 			   += incInt;//lfo->phaseInc*lfo->modNodeValue;
	const uint8_t overflow 	= oldPhase>lfo->phase;

	/*
	 * One-shots (LXR Catalyst): a wrap either ends the retrigger delay or ends
	 * the cycle. After the cycle the phase is pinned and the shape holds its
	 * end value; during the delay it holds its start value.
	 */
	if(lfo->waveform>=LFO_SINE_ONE)
	{
		if(overflow)
		{
			if(lfo->delay)
			{
				lfo->delay = 0;
				if(lfo->waveform==LFO_NOISE_ONE)
					lfo->rnd = (float)(GetRngValue()) / 32767.0f;
			}
			else
			{
				lfo->phase = oldPhase;
				switch(lfo->waveform)
				{
				case LFO_EXP_UP_ONE:
				case LFO_SAW_UP_ONE:
					return 1.f;
				case LFO_NOISE_ONE:
					return lfo->rnd;
				default:
					return 0.f;
				}
			}
		}
		else if(lfo->delay)
		{
			switch(lfo->waveform)
			{
			case LFO_EXP_DOWN_ONE:
			case LFO_SAW_DOWN_ONE:
				return 1.f;
			case LFO_NOISE_ONE:
				return lfo->rnd;
			default:
				return 0.f;
			}
		}
	}

	switch(lfo->waveform)
	{
	//---
		case LFO_SINE:
		case LFO_SINE_ONE:
			return ((sine_table[(lfo->phase>>20)]/32767.f) + 1)/2.f;
		break;
	//---
		case LFO_TRI:
		case LFO_TRI_ONE:
			return (1.f-fabsf( (lfo->phase/(float)0xffffffff)*2-1 ) );
			break;
	//---
		case LFO_SAW_UP:
		case LFO_SAW_UP_ONE:
		return lfo->phase/(float)0xffffffff ;
		break;
	//---
		case LFO_REC:
		if(lfo->phase > 0x7fffffff)
		{
			return 1.f;
		}
		else
		{
			return 0.f;
		}
		break;
	//---
		case LFO_REC_ONE: /* the one-shot square starts high */
		return (lfo->phase > 0x7fffffff) ? 0.f : 1.f;
	//---
		case LFO_NOISE:
		case LFO_NOISE_ONE:
		if(overflow)
		{
			// lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f;
			lfo->rnd = (float)(GetRngValue()) / 32767.0f;
		}
		return lfo->rnd;
		break;
	//---
		case LFO_SAW_DOWN:
		case LFO_SAW_DOWN_ONE:
		return (0xffffffff-(lfo->phase)) / (float)0xffffffff;
		break;
	//---
		case LFO_EXP_UP:
		case LFO_EXP_UP_ONE:
			{
				float x = lfo->phase/(float)0xffffffff;
				return x*x*x;
			}
			break;
	//---
		case LFO_EXP_DOWN:
		case LFO_EXP_DOWN_ONE:
			{
				float x = (0xffffffff-(lfo->phase)) / (float)0xffffffff;
				return x*x*x;//valueShaperF2F(x,-0.9f);
			}
			break;
	//---
		default:
		return 0;
		break;
	}
	return 0;
}
//-------------------------------------------------------------
void lfo_dispatchNextValue(Lfo* lfo, uint8_t source_slot)
{
	float val = lfo_calc(lfo);
	/*
	 * Fan one raw oscillator value out to both destination nodes.
	 *
	 * Inputs: lfo_calc() returns the shared 0..1 waveform for this block, and
	 * lfo->polarity stores the shared negative/positive/bipolar mode. The
	 * source slot tells InstrumentManager which descriptor/supplemental/Scene
	 * target adapters belong to this LFO. Outputs: any remaining legacy direct
	 * ModulationNode target is updated first, then InstrumentManager applies
	 * owner-backed adapters for pair 1 and pair 2.
	 *
	 * This stays separate from lfo_calc() because only destination owners know
	 * whether the target is a legacy direct pointer, descriptor-backed runtime
	 * writer, slot decimation, Scene Decimation, or the hidden per-voice Morph
	 * layer.
	 */
	modNode_updateValuePolarity(&lfo->modTarget,val,lfo->polarity);
	modNode_updateValuePolarity(&lfo->modTarget2,val,lfo->polarity);
	instrumentManager_updateLfoAdapters(source_slot, 0u, val,
										lfo->polarity,
										lfo->modTarget.amount);
	instrumentManager_updateLfoAdapters(source_slot, 1u, val,
										lfo->polarity,
										lfo->modTarget2.amount);
}
//-------------------------------------------------------------
uint32_t lfo_calcPhaseInc(float freq, uint8_t sync)
{
	if(sync==0)//no sync
	{
		return freq/(LFO_SR) * 0xffffffff;
	}

	//sync is on

	/* Synced LFOs are the DSP modulation path that follows sequencer tempo.
	** Keep this tied to seq_getBpm() like the reference LXR; a fixed BPM here
	** makes modulation drift from the selected/internal/external clock source. */
	float bpm =  seq_getBpm();

	float tempoAsFrequency = bpm > 0.0 ? bpm / 60.f /4: 0.0f; //bpm/60 = beat duration /4 = bar duration

	float scaler;

	switch(sync)
	{
		default: //no sync

		return freq/(LFO_SR) * 0xffffffff;
		break;

		case 1: // 4/1
		scaler = 0.25f;//NoteTypeScalers[Quadruple] * NoteDivisionScalers[Whole];
		break;

		case 2: // 2/1
		scaler = 0.5f;//NoteTypeScalers[Double] * NoteDivisionScalers[Whole];
		break;

		case 3: // 1/1
		scaler = 1;//NoteTypeScalers[Regular] * NoteDivisionScalers[Whole];
		break;

		case 4: // 1/2
		scaler = 2;// NoteTypeScalers[Regular] * NoteDivisionScalers[Half];
		break;

		case 5: //1/3
		scaler = 3;// NoteTypeScalers[Regular] * NoteDivisionScalers[Third];
		break;

		case 6: // 1/4
		scaler = 4;// NoteTypeScalers[Regular] * NoteDivisionScalers[Quarter];
		break;

		case 7: // 1/6
		scaler = 6;// NoteTypeScalers[Regular] * NoteDivisionScalers[Sixth];
		break;

		case 8: // 1/8
		scaler = 8;// NoteTypeScalers[Regular] * NoteDivisionScalers[Eighth];
		break;

		case 9: // 1/12
		scaler = 12;//NoteTypeScalers[Regular] * NoteDivisionScalers[Twelfth];
		break;

		case 10: // 1/16
		scaler = 16;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;

		case 11: // 1/32
		scaler = 32;//NoteTypeScalers[Regular] * NoteDivisionScalers[Sixteenth];
		break;
	}

	float lfoSyncfreq = tempoAsFrequency * scaler;
	return (lfoSyncfreq / (LFO_SR)) * 0xffffffff;
}
//-------------------------------------------------------------
void lfo_setFreq(Lfo *lfo, float f)
{
	f += 1;
	f = f/128.f;
	f = f*f*f;
	lfo->freq = f*LFO_MAX_F;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_setSync(Lfo* lfo, uint8_t sync)
{
	lfo->sync = sync;
	lfo->phaseInc = lfo_calcPhaseInc(lfo->freq,lfo->sync);
}
//-------------------------------------------------------------
void lfo_recalcSync()
{
	/*
	 * Recalculate sync against the active InstrumentManager runtime slots.
	 *
	 * Inputs: none; tempo comes from sequencer through lfo_calcPhaseInc().
	 * Output: every current slot LFO receives a refreshed phase increment.
	 * This delegates instead of walking voiceArray/snareVoice/cymbalVoice/
	 * hatVoice directly because Instrument Load can move those engine types
	 * into different runtime slots.
	 */
	instrumentManager_recalcRuntimeLfoSync();
}
//-------------------------------------------------------------
void lfo_retrigger(uint8_t voice)
{
	/*
	 * Retrigger LFOs through the dynamic runtime dispatcher.
	 *
	 * Input: zero-based visible trigger track. Output: any current slot LFO
	 * whose retrigger selector matches that track resets to phaseOffset. The
	 * old fixed-global scan is no longer correct once instruments can be loaded
	 * into arbitrary slots.
	 */
	instrumentManager_retriggerRuntimeLfos(voice);
}
//-------------------------------------------------------------
//-------------------------------------------------------------
void lfo_restart(Lfo *lfo)
{
	const uint8_t one_shot = (lfo->waveform >= LFO_SINE_ONE);

	if(!one_shot)
	{
		lfo->phase = lfo->phaseOffset;
		return;
	}
	lfo->delay = (lfo->phaseOffset != 0u);
	lfo->phase = 0u - lfo->phaseOffset;
	if(lfo->waveform == LFO_NOISE_ONE && !lfo->delay)
		lfo->rnd = (float)(GetRngValue()) / 32767.0f;
}
