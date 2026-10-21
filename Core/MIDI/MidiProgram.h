/*
 * MidiProgram.h
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

#ifndef MIDIPROGRAM_H_
#define MIDIPROGRAM_H_

/*
 * MidiProgram - received program change and Bank change (S083, plan 3.7,
 * P1-P4, MIDI_SPEC 7).
 *
 * What: PC 0..15 on G changes the whole Scene exactly like a PERF SEQ
 * press, in any mode (scd division, pcr reset, releases Scene 17); PC on a
 * voice channel moves the tracks whose effective channel it is (no queue,
 * Scene 17 stays); CC0 on G requests the background Bank load of slot
 * 0..127. Only available Scenes are selected (S082 P3 D52). PC and CC0
 * arriving during a Bank load never reach here (S082 drop filter).
 * Why: P1-P4. RXF P governs both (F37; the caller checks it).
 * Context: foreground. RAM: none.
 * Affiliates: MidiIn (caller), menu_perfModeSceneChangeFrom(),
 * seq_assignTracksToScene(), preset_requestBackgroundBankLoad(),
 * bank_sceneAvailable(), MidiNoteMap.
 */

#include <stdint.h>

/* PC (channel 1..16, program 0..127). Output: 1 = acted (consumed). */
uint8_t midiProgram_programChange(uint8_t channel, uint8_t program);
/* CC0 (channel 1..16, value 0..127). Output: 1 = load accepted. */
uint8_t midiProgram_bankChange(uint8_t channel, uint8_t value);

#endif /* MIDIPROGRAM_H_ */
