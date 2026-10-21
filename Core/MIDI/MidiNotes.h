/*
 * MidiNotes.h
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

#ifndef MIDINOTES_H_
#define MIDINOTES_H_

/*
 * MidiNotes - what a received note does (S083, plan 3.4, N6, C8).
 *
 * What: resolves a note-on/off through the active Scene's note map, then
 * either triggers the track once at the note's velocity and pitch, or,
 * when a BAR overlay is selected (a held BAR in PERF, else CC2), performs
 * that overlay's action through the per-track overlay executor. Reports
 * each single trigger to LiveRecord with the note's arrival age. Holds the
 * CC2 note-action state.
 * Why: N1-N6; Q5 (concurrent rolls per track); Q6 (arrival time).
 * Context: foreground (midiIn_dispatch()).
 * RAM: CC2 state 1 B SRAM1 (approved). Held notes live in the overlay
 * table (buttonHandler.c).
 * Affiliates: MidiNoteMap, buttonHandler overlay executor, sequencer
 * (seq_triggerVoice(), seq_isTrackMuted()), LiveRecord, MidiIn.
 */

#include <stdint.h>
#include "MidiDefs.h"

/*
 * Act on one note-on/off (RXF N already checked by the caller).
 * Output: 1 = consumed (it resolved to a track, even a muted one),
 * 0 = unmapped (the caller forwards it). Accessor: midiIn_dispatch().
 */
uint8_t midiNotes_handle(const MidiMsg *msg);
/*
 * CC2 on G (C8): 0 single trigger, 1 BAR `<` overlay, 2 BAR `>` overlay.
 * Output: 1 consumed (0..2), 0 other values (ignored and forwarded).
 * The value persists until the next CC2; reset at boot. A change
 * re-configures MIDI-held overlay entries while no BAR is held.
 * Accessors: midiIn_dispatch() (B), midiControl_handleCc() (E).
 */
uint8_t midiNotes_setNoteAction(uint8_t value);
/* Current CC2 state (0..2). */
uint8_t midiNotes_noteAction(void);

#endif /* MIDINOTES_H_ */
