/*
 * MidiNoteMap.c - note <-> (track, internal note) for one Scene (S083).
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

#include "MidiNoteMap.h"
#ifndef MIDI_NOTEMAP_HOST_TEST
#include "MidiIn.h"
#include "MidiPitch.h"
#include "SceneData.h"
_Static_assert(MIDI_NOTEMAP_ROOT_INTERNAL == MIDI_DEFAULT_TRIGGER_NOTE,
               "note map root must be the default trigger note");
#endif

/* Contracts in MidiNoteMap.h. */
uint8_t midiNoteMap_effectiveChannel(const midiNoteMap_settings_t *s,
                                     uint8_t track)
{
	uint8_t ch;

	if (!s || track >= MIDI_NOTEMAP_TRACKS)
		return 0u;
	ch = s->channel[track];
	if (ch > 16u)
		ch = 0u;
	return (ch != 0u) ? ch : ((s->global <= 16u) ? s->global : 0u);
}

uint8_t midiNoteMap_tracksOnChannel(const midiNoteMap_settings_t *s,
                                    uint8_t channel)
{
	uint8_t mask = 0u;
	uint8_t t;

	if (channel < 1u || channel > 16u)
		return 0u;
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if (midiNoteMap_effectiveChannel(s, t) == channel)
			mask |= (uint8_t)(1u << t);
	}
	return mask;
}

/*
 * Lowest assigned note among `set` (128 when none): the top of the
 * unassigned region (rule 2).
 */
static uint8_t midiNoteMap_lowestAssigned(const midiNoteMap_settings_t *s,
                                          uint8_t set)
{
	uint8_t low = 128u;
	uint8_t t;

	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		if ((set & (1u << t)) && s->note[t] != 0u && s->note[t] < low)
			low = s->note[t];
	}
	return low;
}

/*
 * Rule 2 block of unassigned track `u` (index among U in track order) on a
 * non-G channel: start and size of its contiguous block in 0..region-1.
 */
static void midiNoteMap_splitBlock(uint8_t region, uint8_t count,
                                   uint8_t index, uint8_t *start,
                                   uint8_t *size)
{
	uint8_t base = (uint8_t)(region / count);
	uint8_t rem = (uint8_t)(region % count);

	*start = (uint8_t)(index * base + ((index < rem) ? index : rem));
	*size = (uint8_t)(base + ((index < rem) ? 1u : 0u));
}

static uint8_t midiNoteMap_clampInternal(uint16_t internal)
{
	return (uint8_t)((internal > 127u) ? 127u : internal);
}

uint8_t midiNoteMap_resolve(const midiNoteMap_settings_t *s, uint8_t channel,
                            uint8_t note, uint8_t *track, uint8_t *internal)
{
	uint8_t set;
	uint8_t owner = 0xFFu;
	uint8_t ownerNote = 0u;
	uint8_t unassigned = 0u;
	uint8_t count = 0u;
	uint8_t region;
	uint8_t t;

	if (!s || !track || !internal || note > 127u)
		return 0u;
	set = midiNoteMap_tracksOnChannel(s, channel);
	if (set == 0u)
		return 0u;
	/* Rule 1: the assigned track with the greatest root <= note; on equal
	** roots the first (lowest) track wins (rule 3 / N5). */
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		uint8_t root;

		if ((set & (1u << t)) == 0u)
			continue;
		root = s->note[t];
		if (root == 0u) {
			unassigned |= (uint8_t)(1u << t);
			count++;
			continue;
		}
		if (root <= note && (owner == 0xFFu || root > ownerNote)) {
			owner = t;
			ownerNote = root;
		}
	}
	if (owner != 0xFFu) {
		*track = owner;
		*internal = midiNoteMap_clampInternal(
		    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL + (note - ownerNote)));
		return 1u;
	}
	/* Rule 2: the note lies below every assigned root. */
	if (count == 0u)
		return 0u;
	region = midiNoteMap_lowestAssigned(s, set);
	if (channel == s->global) {
		uint8_t want = (uint8_t)(note % count);
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			if ((unassigned & (1u << t)) == 0u)
				continue;
			if (i++ == want) {
				*track = t;
				*internal = MIDI_NOTEMAP_ROOT_INTERNAL;
				return 1u;
			}
		}
		return 0u;
	}
	{
		uint8_t i = 0u;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			uint8_t start;
			uint8_t size;

			if ((unassigned & (1u << t)) == 0u)
				continue;
			midiNoteMap_splitBlock(region, count, i++, &start, &size);
			if (size != 0u && note >= start &&
			    note < (uint16_t)start + size) {
				*track = t;
				*internal = midiNoteMap_clampInternal(
				    (uint16_t)(MIDI_NOTEMAP_ROOT_INTERNAL +
				               (note - start)));
				return 1u;
			}
		}
	}
	return 0u;
}

uint8_t midiNoteMap_inverse(const midiNoteMap_settings_t *s, uint8_t track,
                            uint8_t internal, uint8_t *channel, uint8_t *note)
{
	uint8_t ch;
	uint8_t set;
	uint16_t candidate;
	uint8_t gotTrack;
	uint8_t gotInternal;

	if (!s || !channel || !note || track >= MIDI_NOTEMAP_TRACKS ||
	    internal < MIDI_NOTEMAP_ROOT_INTERNAL || internal > 127u)
		return 0u;
	ch = midiNoteMap_effectiveChannel(s, track);
	if (ch == 0u)
		return 0u;
	set = midiNoteMap_tracksOnChannel(s, ch);
	if (s->note[track] != 0u) {
		candidate = (uint16_t)(s->note[track] +
		                       (internal - MIDI_NOTEMAP_ROOT_INTERNAL));
	} else {
		uint8_t region = midiNoteMap_lowestAssigned(s, set);
		uint8_t count = 0u;
		uint8_t index = 0xFFu;
		uint8_t t;

		for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
			if ((set & (1u << t)) == 0u || s->note[t] != 0u)
				continue;
			if (t == track)
				index = count;
			count++;
		}
		if (index == 0xFFu)
			return 0u;
		if (ch == s->global) {
			if (internal != MIDI_NOTEMAP_ROOT_INTERNAL)
				return 0u;	/* stripe notes play default pitch only */
			candidate = index;	/* lowest stripe note of this track */
		} else {
			uint8_t start;
			uint8_t size;

			midiNoteMap_splitBlock(region, count, index, &start, &size);
			candidate = (uint16_t)(start +
			                       (internal - MIDI_NOTEMAP_ROOT_INTERNAL));
		}
	}
	if (candidate > 127u)
		return 0u;
	/* Q4: send only what plays back as the same track and pitch. */
	if (!midiNoteMap_resolve(s, ch, (uint8_t)candidate, &gotTrack,
	                         &gotInternal) ||
	    gotTrack != track || gotInternal != internal)
		return 0u;
	*channel = ch;
	*note = (uint8_t)candidate;
	return 1u;
}

#ifndef MIDI_NOTEMAP_HOST_TEST
/*
 * Snapshot one Scene's map inputs (S083).
 * What: G from MidiIn, channel and note per track from SceneData. Scene
 * index 16 (Scene 17) resolves through the SceneData accessors (S082 5).
 * Read per message, never cached (S082 R14). Accessors: MidiNotes, MidiOut,
 * MidiControl, MidiProgram, MidiLearn.
 */
void midiNoteMap_load(uint8_t scene_index, midiNoteMap_settings_t *out)
{
	uint8_t t;

	if (!out)
		return;
	out->global = midiIn_globalChannel();
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++) {
		out->channel[t] = scene_getTrackMidiChannel(scene_index, t);
		out->note[t] = scene_getTrackMidiNote(scene_index, t);
	}
}
#endif
