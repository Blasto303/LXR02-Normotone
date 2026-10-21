/*
 * MidiNoteMap.h
 *
 *  Created on: 21.10.2026
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

#ifndef MIDINOTEMAP_H_
#define MIDINOTEMAP_H_

/*
 * MidiNoteMap - note <-> (track, internal note) for one Scene (S083,
 * plan 3.3, MIDI_SPEC 4.1).
 *
 * What: resolves a received (channel, note) to at most one track and the
 * internal note the voice plays, and the inverse for transmit. Rules:
 *  1. assigned ranges: a track with a note owns [note, next assigned - 1]
 *     (or 127), internal = 63 + (n - note); on equal notes the lower track
 *     owns the range and the other owns nothing;
 *  2. below the lowest assigned note (all of 0..127 if none), the channel's
 *     unassigned tracks share the notes: on G a stripe (n mod |U|, default
 *     pitch), on another channel contiguous near-equal blocks in track
 *     order (lower tracks take the remainder), chromatic from each block
 *     start;
 *  3. one voice per note; 4. internal notes clamp at 127 (+64).
 * A track's effective channel is its own channel, else G; a track set
 * explicitly to G's number follows the G rules; with G off unassigned
 * tracks have no channel.
 * Why: N1-N5. Stateless and table-free: at most seven tracks per lookup,
 * no RAM. The pure functions take a settings snapshot, so the host test
 * (tools/midi_test/) links this file alone (MIDI_NOTEMAP_HOST_TEST).
 * Affiliates: MidiNotes (receive), MidiOut (transmit, Phase G),
 * MidiControl/MidiProgram/MidiLearn (tracks on a channel).
 */

#include <stdint.h>

/* Internal note for the voice's default pitch (= MIDI_DEFAULT_TRIGGER_NOTE). */
#define MIDI_NOTEMAP_ROOT_INTERNAL  63u
#define MIDI_NOTEMAP_TRACKS         7u

/*
 * One Scene's note-map inputs (stack only, 15 B).
 * global: G 0..16 (0 off). channel[t]: 0 unassigned or 1..16.
 * note[t]: 0 unassigned or 1..127 (MIDI note 0 cannot be a root).
 */
typedef struct {
	uint8_t global;
	uint8_t channel[MIDI_NOTEMAP_TRACKS];
	uint8_t note[MIDI_NOTEMAP_TRACKS];
} midiNoteMap_settings_t;

/* Effective channel of track t: own channel, else G; 0 = none. */
uint8_t midiNoteMap_effectiveChannel(const midiNoteMap_settings_t *s,
                                     uint8_t track);
/* Bit t set for every track whose effective channel is `channel` (1..16). */
uint8_t midiNoteMap_tracksOnChannel(const midiNoteMap_settings_t *s,
                                    uint8_t channel);
/*
 * Receive: (channel 1..16, note 0..127) -> track and internal note.
 * Output: 1 with *track and *internal set, 0 when unmapped (the caller then
 * does not consume the note).
 */
uint8_t midiNoteMap_resolve(const midiNoteMap_settings_t *s, uint8_t channel,
                            uint8_t note, uint8_t *track, uint8_t *internal);
/*
 * Transmit: (track, internal note) -> (channel, note) such that resolve()
 * returns the same track and internal note. Output: 0 when no such note
 * exists (stripe pitch other than default, below the default pitch, past
 * the range end, a tie loser, no channel): nothing is sent (S083 Q4).
 */
uint8_t midiNoteMap_inverse(const midiNoteMap_settings_t *s, uint8_t track,
                            uint8_t internal, uint8_t *channel, uint8_t *note);

#ifndef MIDI_NOTEMAP_HOST_TEST
/* Fill *out from Scene `scene_index`'s track settings and G (firmware). */
void midiNoteMap_load(uint8_t scene_index, midiNoteMap_settings_t *out);
#endif

#endif /* MIDINOTEMAP_H_ */
