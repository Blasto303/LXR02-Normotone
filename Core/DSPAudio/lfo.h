/*
 * lfo.h
 *
 *  Created on: 14.01.2013
 *  Modified on 17.05.2026 by Brendan Clarke
 *
 *      Non bandlimited low freq. oscillator
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


#ifndef LFO_H_
#define LFO_H_
//-------------------------------------------------------------
#include "stm32f4xx.h"
#include "Oscillator.h"
#include "wavetable.h"	//sine
#include "config.h"
// TODO DSP_PORT
// #include "sequencer.h"
#include "modulationNode.h"
//-------------------------------------------------------------
#define LFO_SINE 		0x00
#define LFO_TRI			0x01
#define LFO_SAW_UP		0x02
#define LFO_SAW_DOWN	0x03
#define LFO_REC			0x04
#define LFO_NOISE		0x05
#define LFO_EXP_UP		0x06
#define LFO_EXP_DOWN	0x07
/*
 * One-shot waveforms (LXR Catalyst): the same eight shapes, played once after
 * a retrigger, then held. For these, lfo_offset is a delay before the cycle
 * instead of a phase offset. Before and after the cycle each shape holds its
 * natural end value: saw down and exp down hold 1 before and 0 after, saw up
 * and exp up 0 before and 1 after, noise holds its one random value, the
 * others hold 0. The square starts high. Menu names si1..xd1.
 */
#define LFO_SINE_ONE		0x08
#define LFO_TRI_ONE			0x09
#define LFO_SAW_UP_ONE		0x0a
#define LFO_SAW_DOWN_ONE	0x0b
#define LFO_REC_ONE			0x0c
#define LFO_NOISE_ONE		0x0d
#define LFO_EXP_UP_ONE		0x0e
#define LFO_EXP_DOWN_ONE	0x0f


#define LFO_MAX_F 		200 //[Hz]
#define LFO_SR 			(REAL_FS/(float)OUTPUT_DMA_SIZE)
/*
 * LFO retrigger source: Scene change (S076 P2).
 *
 * What:       value 7 in the lfo.retrigger field. When set, a Scene change
 *             resets the LFO phase to phaseOffset using the incoming Scene's
 *             configured offset. When NOT set, the LFO inherits the outgoing
 *             Scene's running phase so the cycle continues uninterrupted.
 * Why:        gives the user per-LFO control over whether a Scene change
 *             interrupts the LFO cycle. This value is outside the voice
 *             trigger range 1..6 and must not match voice retrigger
 *             comparisons.
 * Inputs:     stored as uint8_t in lfo.retrigger via the MENU_RETRIGGER
 *             selector (retriggerNames[] index 7 = "scn").
 * Outputs:    instrumentManager_restoreLfoPhaseIfNeeded() tests this value
 *             to decide reset vs. continue; instrumentManager_retriggerRuntimeLfos()
 *             excludes it from voice trigger matching.
 * Affiliates: retriggerNames[] (MenuText.h),
 *             instrumentManager_retriggerRuntimeLfos() (InstrumentManager.c),
 *             instrumentManager_restoreLfoPhaseIfNeeded() (InstrumentManager.c).
 */
#define LFO_RETRIGGER_SCENE  7u
//-------------------------------------------------------------
typedef struct LfoStruct
{
	uint32_t 	phase;		// the current phase of the LFO (upper bits cnt 0-255)
	uint32_t 	phaseInc;	// the phase increment controls the LFO frequency
	uint8_t 	waveform;	// selects the waveform
	uint8_t 	retrigger;	// defines the voice nr that retriggers the LFO (0=no retrigger)
	uint8_t 	delay;		// one-shot: nonzero while the retrigger delay (offset) runs
	uint32_t 	phaseOffset;// the phase value to which the LFO is retriggered
	float 		rnd;
	uint8_t 	sync;
	float 		freq;
	ModulationNode modTarget;
	/*
	 * Second destination for the same LFO oscillator.
	 *
	 * modTarget and modTarget2 are not two independent LFOs. They share phase,
	 * waveform, rate, sync, offset, retrigger, and polarity, but each target
	 * owns its own amount and any remaining legacy direct backend state.
	 * Descriptor instrument targets may now be installed in InstrumentManager
	 * adapters rather than these raw ModulationNode slots; the slots remain the
	 * per-pair amount owners and still support any intentionally retained direct
	 * pointer backend.
	 */
	ModulationNode modTarget2;
	/*
	 * Shared application polarity for both destinations.
	 *
	 * Inputs: descriptor/menu writes store a mod_node_polarity_t value here.
	 * Output: lfo_dispatchNextValue() passes it to ModulationNode for legacy
	 * direct backends and to InstrumentManager for descriptor/supplemental
	 * adapters. Polarity belongs to Lfo rather than each destination because
	 * the requested UI shape is one shared LFO with two destinations and
	 * independent amounts.
	 */
	uint8_t		polarity;
	float		modNodeValue;
} Lfo;
//-------------------------------------------------------------
void lfo_init(Lfo *lfo);
/*
 * Dispatch one LFO block with explicit source-slot identity.
 *
 * Inputs: lfo is the oscillator/modulation state for one voice, and source_slot
 * identifies which instrument slot owns it. Output: the shared waveform updates
 * both direct ModulationNode targets and any installed Scene/supplemental
 * target adapters for the same LFO pair slots.
 *
 * The source slot cannot be inferred reliably by Scene target code from the
 * Lfo pointer without hardcoding voice object addresses. Passing it from
 * mixer.c keeps LFO math generic while allowing InstrumentManager to route
 * Scene modulation by the same source slot used when the target was installed.
 */
void lfo_dispatchNextValue(Lfo* lfo, uint8_t source_slot);
uint32_t lfo_calcPhaseInc(float freq, uint8_t sync);
void lfo_setFreq(Lfo *lfo, float f);
void lfo_setSync(Lfo* lfo, uint8_t sync);
void lfo_recalcSync();
void lfo_retrigger(uint8_t voiceNr);
/*
 * Restart one LFO for a matching retrigger.
 *
 * Inputs: the LFO. Output: a looping waveform jumps to phaseOffset; a one-shot
 * waveform starts its delay (phase = -phaseOffset, so the cycle begins when the
 * phase wraps to 0) and, for noise without delay, draws its value now.
 * Caller: instrumentManager_retriggerRuntimeLfos().
 */
void lfo_restart(Lfo *lfo);

#endif /* LFO_H_ */
