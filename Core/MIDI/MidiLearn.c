/*
 * MidiLearn.c - learned CCs and the Learn gesture (S083).
 */

/*
 *  Modified on: 21.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "MidiLearn.h"
#include "MidiDefs.h"
#include "MidiIn.h"
#include "MidiControl.h"
#include "menu.h"
#include "buttonHandler.h"
#include "filesystem.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "InstrumentManager.h"
#include "EffectsManager.h"
#include "timebase.h"
#include "config.h"
#include <string.h>

/*
 * One learned CC (plan 3.6, F36).
 * ccKind: bits 0..6 CC, bit 7 channel kind (0 = G, 1 = target voice's
 * channel). space: 0..5 voice slot, 6 Scene, 7 Effect. type:
 * instrument_type_t (voice), effect_type_id_t (Effect), 0xFF (Scene).
 * index: descriptor 0..63, Scene-local index, or Effect-local 0..62.
 */
typedef struct {
	uint8_t ccKind;
	uint8_t space;
	uint8_t type;
	uint8_t index;
} midiLearn_entry_t;

#define MIDI_LEARN_KIND_VOICE   0x80u
#define MIDI_LEARN_SPACE_SCENE  6u
#define MIDI_LEARN_SPACE_EFFECT 7u
#define MIDI_LEARN_TYPE_NONE    0xFFu

enum {
	MIDI_LEARN_IDLE = 0,
	MIDI_LEARN_WAIT_TARGET,	/* armed: touch a parameter */
	MIDI_LEARN_WAIT_CC,	/* target shows LRN: send a CC */
	MIDI_LEARN_FULL		/* refused: FUL shows briefly */
};

static midiLearn_entry_t midiLearn_table[MIDI_LEARN_MAX_ENTRIES];
static uint8_t  midiLearn_used;
static uint8_t  midiLearn_state;
static uint16_t midiLearn_target = MIDI_TARGET_OFF;
static uint16_t midiLearn_fullUntil;

/* Row key of target `id` in the active Scene; 0 when not addressable. */
static uint8_t midiLearn_keyOf(uint16_t id, uint8_t *space, uint8_t *type,
                               uint8_t *index)
{
	if (instrumentParam_isVoiceParameter(id)) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), instrumentParam_slot(id));

		if (!instr)
			return 0u;
		*space = instrumentParam_slot(id);
		*type = (uint8_t)instr->type;
		*index = instrumentParam_local(id);
		return 1u;
	}
	if (sceneModTarget_isSceneTarget(id)) {
		*space = MIDI_LEARN_SPACE_SCENE;
		*type = MIDI_LEARN_TYPE_NONE;
		return sceneModTarget_indexFromId(id, index);
	}
	if (effectTarget_isEffectId(id)) {
		*space = MIDI_LEARN_SPACE_EFFECT;
		*type = (uint8_t)effects_activeType();
		*index = effectTarget_local(id);
		return (uint8_t)(*index < EFFECT_TARGET_PATTERN_LOCAL_LIMIT);
	}
	return 0u;
}

static uint16_t midiLearn_targetOf(const midiLearn_entry_t *e)
{
	if (e->space < MIDI_LEARN_SPACE_SCENE)
		return (uint16_t)instrumentParam_make(e->space, e->index);
	if (e->space == MIDI_LEARN_SPACE_SCENE)
		return sceneModTarget_idFromIndex(e->index);
	return effectTarget_id(e->index);
}

/* F51: a row applies only while its type is loaded (active Scene). */
static uint8_t midiLearn_typeMatches(const midiLearn_entry_t *e)
{
	if (e->space < MIDI_LEARN_SPACE_SCENE) {
		const kit_instrument_slot_t *instr = scene_instrumentSlotConst(
		    scene_getActiveIndex(), e->space);
		return (uint8_t)(instr && (uint8_t)instr->type == e->type);
	}
	if (e->space == MIDI_LEARN_SPACE_EFFECT)
		return (uint8_t)((uint8_t)effects_activeType() == e->type);
	return 1u;
}

static int16_t midiLearn_find(uint8_t space, uint8_t type, uint8_t index)
{
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		if (e->space == space && e->type == type && e->index == index)
			return (int16_t)i;
	}
	return -1;
}

/* Leave the gesture: LEDs back to their own state, display refreshed. */
static void midiLearn_end(void)
{
	midiLearn_state = MIDI_LEARN_IDLE;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(0u);
	menu_learnDisplayChanged();
}

/* Contracts in MidiLearn.h. */
void midiLearn_clearAll(void)
{
	midiLearn_used = 0u;
}

uint8_t midiLearn_count(void)
{
	return midiLearn_used;
}

void midiLearn_arm(void)
{
	midiLearn_state = MIDI_LEARN_WAIT_TARGET;
	midiLearn_target = MIDI_TARGET_OFF;
	buttonHandler_setLearnLeds(1u);
	menu_learnDisplayChanged();
}

void midiLearn_cancel(void)
{
	if (midiLearn_state != MIDI_LEARN_IDLE)
		midiLearn_end();
}

uint8_t midiLearn_active(void)
{
	return (uint8_t)(midiLearn_state != MIDI_LEARN_IDLE);
}

uint8_t midiLearn_selectingTarget(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_WAIT_TARGET ||
	                 midiLearn_state == MIDI_LEARN_WAIT_CC);
}

void midiLearn_selectTarget(uint16_t id)
{
	if (!midiLearn_selectingTarget() || id >= MIDI_TARGET_OFF)
		return;
	midiLearn_target = id;
	midiLearn_state = MIDI_LEARN_WAIT_CC;
	menu_learnDisplayChanged();
}

uint16_t midiLearn_displayTarget(void)
{
	return (midiLearn_state == MIDI_LEARN_WAIT_CC ||
	        midiLearn_state == MIDI_LEARN_FULL) ? midiLearn_target
	                                            : MIDI_TARGET_OFF;
}

uint8_t midiLearn_showingFull(void)
{
	return (uint8_t)(midiLearn_state == MIDI_LEARN_FULL);
}

void midiLearn_service(void)
{
	if (midiLearn_state == MIDI_LEARN_FULL &&
	    (uint16_t)(time_sysTick - midiLearn_fullUntil) < 32768u)
		midiLearn_end();
}

uint8_t midiLearn_captureCc(uint8_t channel, uint8_t cc)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t space, type, index;
	int16_t row;

	if (midiLearn_state != MIDI_LEARN_WAIT_CC || midiDefs_ccReserved(cc))
		return 0u;
	/* Valid on G, or on the target voice's own channel (voice targets). */
	if (!onG && !midiControl_targetOnChannel(midiLearn_target, channel))
		return 0u;
	if (!midiLearn_keyOf(midiLearn_target, &space, &type, &index))
		return 0u;
	row = midiLearn_find(space, type, index);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES) {
			/* F29: a full table refuses a new row; FUL shows briefly. */
			midiLearn_state = MIDI_LEARN_FULL;
			midiLearn_fullUntil =
			    (uint16_t)(time_sysTick + MIDI_LEARN_FULL_SHOW_MS);
			menu_learnDisplayChanged();
			return 1u;
		}
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)((cc & 0x7Fu) | (onG ? 0u : MIDI_LEARN_KIND_VOICE));
	midiLearn_table[row].space = space;
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	/*
	 * S083 §16: every stored or replaced row rewrites midilrn.cfg in full
	 * (no debounce; the writer waits for an idle facade and writes once
	 * more if a row arrives during a write). A FUL refusal above writes
	 * nothing.
	 */
	filesystem_requestMidiLearnWrite();
	midiLearn_end();
	return 1u;
}

uint8_t midiLearn_applyCc(uint8_t channel, uint8_t cc, uint8_t value7)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t onG = (uint8_t)(global != 0u && channel == global);
	uint8_t consumed = 0u;
	uint8_t i;

	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];
		uint8_t voiceKind =
		    (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		uint16_t id;

		if ((uint8_t)(e->ccKind & 0x7Fu) != cc)
			continue;
		if (voiceKind ? onG : !onG)
			continue;
		if (!midiLearn_typeMatches(e))
			continue;
		id = midiLearn_targetOf(e);
		/* F50: a `v` row moves only its own slot on a shared channel. */
		if (voiceKind && !midiControl_targetOnChannel(id, channel))
			continue;
		consumed |= midiControl_writeTarget(id, value7);
	}
	return consumed;
}

uint8_t midiLearn_ccForTarget(uint16_t id, uint8_t *cc, uint8_t *voiceKind)
{
	uint8_t i;

	if (!cc || !voiceKind)
		return 0u;
	for (i = 0u; i < midiLearn_used; i++) {
		const midiLearn_entry_t *e = &midiLearn_table[i];

		if (midiLearn_targetOf(e) != id || !midiLearn_typeMatches(e))
			continue;
		*cc = (uint8_t)(e->ccKind & 0x7Fu);
		*voiceKind = (uint8_t)((e->ccKind & MIDI_LEARN_KIND_VOICE) != 0u);
		return 1u;
	}
	return 0u;
}

/*
 * Append up to maxLen characters of `text` at dst[pos] (no printf: the
 * firmware links no printf family, and S083 does not add it). Returns the
 * new position; stops one byte short of cap for the terminator.
 */
static uint16_t midiLearn_append(char *dst, uint16_t cap, uint16_t pos,
                                 const char *text, uint8_t maxLen)
{
	while (text && *text != '\0' && maxLen-- != 0u &&
	       (uint16_t)(pos + 1u) < cap)
		dst[pos++] = *text++;
	return pos;
}

/* Contract in MidiLearn.h. */
uint8_t midiLearn_formatFileLine(uint8_t index, char *dst, uint16_t cap)
{
	const midiLearn_entry_t *e;
	const char *space = 0;
	const char *type = 0;
	const char *key = 0;
	char slotText[3] = { 'v', '1', '\0' };
	char ccText[4];
	uint8_t cc;
	uint16_t pos = 0u;

	if (index >= midiLearn_used || !dst || cap < 32u)
		return 0u;
	e = &midiLearn_table[index];
	cc = (uint8_t)(e->ccKind & 0x7Fu);
	ccText[0] = (char)('0' + cc / 100u);
	ccText[1] = (char)('0' + (cc / 10u) % 10u);
	ccText[2] = (char)('0' + cc % 10u);
	ccText[3] = '\0';
	if (e->space < MIDI_LEARN_SPACE_SCENE) {
		const instrument_registry_entry_t *reg =
		    instrumentManager_registryEntry((instrument_type_t)e->type);
		const ParamDescriptor *d =
		    instrumentManager_descriptor((instrument_type_t)e->type, e->index);

		slotText[1] = (char)('1' + e->space);
		space = slotText;
		type = reg ? reg->type_text : 0;
		key = d ? d->file_key : 0;
	} else if (e->space == MIDI_LEARN_SPACE_EFFECT) {
		const effect_param_descriptor_t *d =
		    effects_descriptor((effect_type_id_t)e->type, e->index);

		space = "fx";
		type = effects_typeToken((effect_type_id_t)e->type);
		key = d ? d->base.file_key : 0;
	} else {
		const scene_mod_target_descriptor_t *d = sceneModTarget_descriptor(
		    sceneModTarget_idFromIndex(e->index));

		space = "scn";
		key = d ? d->short_name : 0;
	}
	if (key && (type || e->space == MIDI_LEARN_SPACE_SCENE)) {
		pos = midiLearn_append(dst, cap, pos, "learn=", 6u);
		pos = midiLearn_append(dst, cap, pos, ccText, 3u);
		pos = midiLearn_append(dst, cap, pos,
		                       (e->ccKind & MIDI_LEARN_KIND_VOICE) ? ",v," : ",g,",
		                       3u);
		pos = midiLearn_append(dst, cap, pos, space, 3u);
		if (type) {
			pos = midiLearn_append(dst, cap, pos, ",", 1u);
			pos = midiLearn_append(dst, cap, pos, type, 3u);
		}
		pos = midiLearn_append(dst, cap, pos, ",", 1u);
		pos = midiLearn_append(dst, cap, pos, key, 40u);
		if ((uint16_t)(pos + 2u) <= cap && key[0] != '\0') {
			dst[pos++] = '\n';
			dst[pos] = '\0';
			/*
			 * The line LENGTH: filesystem_writeTextLine() writes exactly
			 * this many bytes (S083 §16 fix; returning 1 wrote only "l").
			 */
			return (uint8_t)pos;
		}
	}
	/* Unresolvable or too long: a comment line keeps `lines=` exact. */
	pos = midiLearn_append(dst, cap, 0u, "# learn row unresolved\n", 24u);
	dst[pos] = '\0';
	return (uint8_t)pos;
}

/*
 * Split "CCC,k,space,type,key" (Scene rows: "CCC,k,scn,key") into fields
 * in a local copy; resolve names to indices once (F34). Duplicate keys:
 * the later line replaces the earlier row (one parameter -> one CC).
 */
uint8_t midiLearn_parseFileValue(const char *value)
{
	char buf[64];
	char *field[5];
	uint8_t count = 0u;
	uint16_t cc = 0u;
	uint8_t voiceKind;
	uint8_t space, type = MIDI_LEARN_TYPE_NONE, index = 0u;
	uint8_t i;
	int16_t row;
	char *p;

	if (!value || strlen(value) >= sizeof(buf))
		return 0u;
	strcpy(buf, value);
	for (p = buf; count < 5u; ) {
		field[count++] = p;
		p = strchr(p, ',');
		if (!p)
			break;
		*p++ = '\0';
	}
	if (count < 4u)
		return 0u;
	for (i = 0u; field[0][i] != '\0'; i++) {
		if (field[0][i] < '0' || field[0][i] > '9' || i > 2u)
			return 0u;
		cc = (uint16_t)(cc * 10u + (uint16_t)(field[0][i] - '0'));
	}
	if (i == 0u || cc > 127u || midiDefs_ccReserved((uint8_t)cc))
		return 0u;
	if (strcmp(field[1], "g") == 0) voiceKind = 0u;
	else if (strcmp(field[1], "v") == 0) voiceKind = 1u;
	else return 0u;
	if (field[2][0] == 'v' && field[2][1] >= '1' && field[2][1] <= '6' &&
	    field[2][2] == '\0' && count == 5u) {
		instrument_type_t t = instrumentManager_typeFromText(field[3]);

		if (t == INSTRUMENT_TYPE_UNKNOWN ||
		    !instrumentManager_descriptorIndexByKey(t, field[4], &index))
			return 0u;
		space = (uint8_t)(field[2][1] - '1');
		type = (uint8_t)t;
	} else if (strcmp(field[2], "fx") == 0 && count == 5u) {
		effect_type_id_t t;

		if (strlen(field[3]) != 3u || voiceKind ||
		    !effects_typeFromToken(field[3], &t) ||
		    !effects_descriptorByKey(t, field[4], &index) ||
		    index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
			return 0u;
		space = MIDI_LEARN_SPACE_EFFECT;
		type = (uint8_t)t;
	} else if (strcmp(field[2], "scn") == 0 && count == 4u) {
		uint8_t n = sceneModTarget_count();

		for (i = 0u; i < n; i++) {
			const scene_mod_target_descriptor_t *d =
			    sceneModTarget_descriptor(sceneModTarget_idFromIndex(i));
			if (d && d->kind != SCENE_MOD_TARGET_KIND_RETIRED &&
			    d->short_name && strcmp(d->short_name, field[3]) == 0)
				break;
		}
		if (i >= n)
			return 0u;
		/* kind v only for per-voice Scene targets (not fxm). */
		if (voiceKind && sceneModTarget_descriptor(
		        sceneModTarget_idFromIndex(i))->kind ==
		        SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
			return 0u;
		space = MIDI_LEARN_SPACE_SCENE;
		index = i;
	} else {
		return 0u;
	}
	row = midiLearn_find(space, type, index);
	if (row < 0) {
		if (midiLearn_used >= MIDI_LEARN_MAX_ENTRIES)
			return 0u;
		row = (int16_t)midiLearn_used++;
	}
	midiLearn_table[row].ccKind =
	    (uint8_t)(cc | (voiceKind ? MIDI_LEARN_KIND_VOICE : 0u));
	midiLearn_table[row].space = space;
	midiLearn_table[row].type = type;
	midiLearn_table[row].index = index;
	return 1u;
}
