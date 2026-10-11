/*
 * ParameterArray.h
 *
 *  Created on: 06.01.2013
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

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */



#ifndef PARAMETERARRAY_H_
#define PARAMETERARRAY_H_
#include <stdint.h>


#define TYPE_UINT8 				0	// byte
#define TYPE_FLT 				1	// float
#define TYPE_SPECIAL_F			2	// float value targeting modNodeValue (as opposed to actual parameter)
#define TYPE_UINT32				3	// 32 bit int
#define TYPE_SPECIAL_P			4	// pan
#define TYPE_SPECIAL_FILTER_F	5	// not used apparently

/*
 * Non-sound flat parameter ids.
 *
 * Instrument sound parameters no longer live in this namespace. SceneData
 * allocates generic per-slot storage, and Core/DSP/Instruments descriptors
 * define what each instrument slot cell means. This enum remains only for
 * menu/performance/pattern/global values that still use parameter_values[].
 */
enum ParamEnums
{
	PAR_NONE = 0,
	PAR_MOD_WHEEL = PAR_NONE,

	END_OF_SOUND_PARAMETERS,

	PAR_ROLL = END_OF_SOUND_PARAMETERS,
	PAR_MORPH,

	PAR_ACTIVE_STEP,
	PAR_STEP_VOLUME,
	/*
	 * Step-special flat parameter mirrors (S079).
	 *
	 * What: one id per editable step special. PAR_STEP_CONDITION replaces the
	 * former PAR_STEP_PROB probability gate; PAR_STEP_TIMING, PAR_STEP_ROLL,
	 * PAR_STEP_RHYTHM, and PAR_STEP_FILL are new. Why: the STEP page displays
	 * and edits each special in its own cell through parameter_values[].
	 * Inputs: pat_applyStepToMenu(), menu_parseGlobalParam(). Affiliates:
	 * menuPages.h SEQ_PAGE, menu.c DTYPE/static-param dispatch.
	 */
	PAR_STEP_CONDITION,
	PAR_STEP_NOTE,
	PAR_STEP_TIMING,
	PAR_STEP_ROLL,
	PAR_STEP_RHYTHM,
	PAR_STEP_FILL,

	PAR_EUKLID_LENGTH,
	PAR_EUKLID_STEPS,
	PAR_EUKLID_ROTATION,

	PAR_AUTOM_TRACK,

	PAR_P1_DEST,
	PAR_P2_DEST,

	PAR_P1_VAL,
	PAR_P2_VAL,

	PAR_SHUFFLE,

	PAR_PATTERN_BEAT,
	PAR_PATTERN_NEXT,
	PAR_TRACK_LENGTH,

	PAR_POS_X,
	PAR_POS_Y,
	PAR_FLUX,
	PAR_SOM_FREQ,
	PAR_TRACK_ROTATION,
	PAR_TRACK_SCALE,
	PAR_TRACK_MIDI_CHAN,
	PAR_TRACK_MIDI_NOTE,
	/*
	 * Per-track play mode flat parameter (S078 §4.1).
	 *
	 * What: menu display/edit mirror for the track's play mode byte. Input:
	 * Menu track settings page. Output: parameter_values[PAR_TRACK_PLAY_MODE]
	 * holds 0..5. Affiliates: pat_applyTrackSettingsToMenu(),
	 * pat_setTrackPlayMode(), menuPages.h SEQ_PAGE layout.
	 */
	PAR_TRACK_PLAY_MODE,

	PAR_BEGINNING_OF_GLOBALS,
	PAR_BPM = PAR_BEGINNING_OF_GLOBALS,

	PAR_MIDI_CHAN_1,
	PAR_MIDI_CHAN_2,
	PAR_MIDI_CHAN_3,
	PAR_MIDI_CHAN_4,
	PAR_MIDI_CHAN_5,
	PAR_MIDI_CHAN_6,

	PAR_EXT_SYNC,
	PAR_FOLLOW,

	PAR_QUANTISATION,

	PAR_SCREENSAVER_ON_OFF,
	PAR_MIDI_MODE,
	PAR_MIDI_CHAN_7,
	PAR_MIDI_ROUTING,
	PAR_MIDI_FILT_TX,
	PAR_MIDI_FILT_RX,
	PAR_PRESCALER_CLOCK_IN,
	PAR_PRESCALER_CLOCK_OUT1,
	PAR_PRESCALER_CLOCK_OUT2,
	PAR_TRIG_GATE_MODE,

	PAR_BAR_RESET_MODE,
	PAR_MIDI_CHAN_GLOBAL,
	PAR_OSC_WAVE_INTERP,

	/*
	 * Scene-level PERF controls added after existing globals to avoid
	 * renumbering established Pattern/MIDI/global ids.
	 *
	 * PAR_MORPH remains the overall bulk-set control near the legacy PERF ids.
	 * These six voice Morph ids are contiguous so Preset/Menu can map
	 * PAR_VOICE1_MORPH + slot without hardcoding per-instrument parameter
	 * lists. PAR_EFFECT_MORPH (S075) occupies the slot of the retired global
	 * decimation id: it is the flat PERF mirror of the active Scene's Effect
	 * Morph amount (scene_settings_t::effect_morph_amount, 0..255), shown as
	 * PERF "fxm". It is refresh-only in menu_parseGlobalParam(); page edits
	 * commit through menu_commitEffectMorphParam() -> effects_setMorphAmount(),
	 * which fans out through the edit mask. Keeping the position means no later
	 * flat id is renumbered.
	 */
	PAR_VOICE1_MORPH,
	PAR_VOICE2_MORPH,
	PAR_VOICE3_MORPH,
	PAR_VOICE4_MORPH,
	PAR_VOICE5_MORPH,
	PAR_VOICE6_MORPH,
	PAR_EFFECT_MORPH,

	/*
	 * Persistent Global AutoSave policy byte.
	 *
	 * What: appends one on/off id without renumbering any established flat
	 * parameter. Why: Menu, settings.cfg, and filesystem lifecycle need one
	 * shared logical value. Inputs: settings default/load and Global-page edits.
	 * Output/effects: zero disables hidden autosave-record I/O; one enables it.
	 * Affiliates: menu.c/menuPages.h and filesystem settings/autosave policy.
	 * NUM_PARAMS remains fixed, so this consumes an existing parameter_values[]
	 * byte rather than increasing BSS.
	 */
	PAR_AUTOSAVE_ENABLED,

	/*
	 * S074 bus compressor page mirrors (cmp, cam, ctm, csc).
	 *
	 * These ids are active-Scene display/edit mirrors in the same order as
	 * scene_settings_t::bus_comp[] and AutoSave 41..44. They consume existing
	 * parameter_values[] capacity and are never serialized as Globals.
	 */
	PAR_BUS_COMP_MODE,
	PAR_BUS_COMP_AMOUNT,
	PAR_BUS_COMP_TIME,
	PAR_BUS_COMP_SIDECHAIN,

	/*
	 * S080 BAR overlay settings (perf_btn_lt/rt, perf_note_lt/rt).
	 *
	 * What: four persisted global parameters for the SHIFT+REC overlay page.
	 * The *_BTN ids select a trigger type (vmx, v10, v64, v32, flm, fls, r/8,
	 * r16, r32, r64, r6t, 128); the *_NOTE ids select a MIDI note (0 = def, else
	 * 0..127). Why: the overlay behaviour must survive power cycles, so it is
	 * written to settings.cfg alongside the existing globals. Inputs: SHIFT+REC
	 * edits and settings load/default. Outputs: parameter_values[] reads by
	 * buttonHandler's VOICE trigger dispatch. Affiliates: filesystem settings
	 * writer/parser (S080 E10/E11), RECORDING_PAGE (S080 E3).
	 */
	PAR_PERF_BTN_LT,
	PAR_PERF_NOTE_LT,
	PAR_PERF_BTN_RT,
	PAR_PERF_NOTE_RT,

	/*
	 * S080 scene queue / change division globals (scd, qcd, qac, pcr).
	 *
	 * What: PAR_SCENE_CHANGE_DIV (scd) is the musical division at which direct
	 * scene changes take effect; PAR_SCENE_QUEUE_DIV (qcd) is the same for queued
	 * (SHIFT+SEQ) changes; PAR_SCENE_QUEUE_ACTION (qac) selects the action on a
	 * queued fire (normal / reset mutes / reset morphs / both / reload);
	 * PAR_SCENE_PC_RESET (pcr) forces tracks to restart on scene change. Why:
	 * adds musical timing and options to scene changes. Inputs: global settings
	 * page edits and settings load/default. Outputs: sequencer scene-change
	 * deferral and the foreground scene-change executor. Affiliates: filesystem
	 * settings writer/parser (S080 F5/F6), MENU_MIDI_PAGE sub-page 3 (S080 F3).
	 */
	PAR_SCENE_CHANGE_DIV,
	PAR_SCENE_QUEUE_DIV,
	PAR_SCENE_QUEUE_ACTION,
	PAR_SCENE_PC_RESET,

	NUM_PARAMS = 384,
};

_Static_assert(PAR_AUTOSAVE_ENABLED < NUM_PARAMS,
	"AutoSave parameter must fit the fixed flat parameter allocation");
_Static_assert(PAR_BUS_COMP_SIDECHAIN < NUM_PARAMS,
	"S074 bus compressor mirrors must fit the fixed flat parameter allocation");

#include "stm32f4xx.h"

typedef union
{
	float 	 flt;
	uint32_t itg;
} ptrValue;

typedef struct ParameterStruct
{
	void* 	ptr;
	uint8_t type;

} Parameter;

extern Parameter parameterArray[];
void paramArray_setParameter(uint16_t idx, ptrValue newValue);
void parameterArray_init();

#endif /* PARAMETERARRAY_H_ */
