/*
 * MidiTransport.h
 *
 *  Derived from MidiRealtime.h and the realtime/MTC parts of MidiParser.c,
 *  original LXR firmware by Julian Schmidt.
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

#ifndef MIDITRANSPORT_H_
#define MIDITRANSPORT_H_

/*
 * MidiTransport - clock, transport and position from MIDI (S083).
 *
 * What: the timestamped realtime ring (was MidiRealtime.c), the TIM3
 * realtime handler with the `snc` source gate (was
 * midiParser_processRealtimeEvents()), Song Position Pointer (Phase C) and
 * MIDI Time Code start/timeout (was in midiParser_parseMidiMessage() and
 * midiParser_checkMtc()).
 * Why: S083 Q7 rebuild. Realtime stays on its own timestamped path so clock
 * and transport never wait behind SysEx, channel messages, LCD or SD work.
 * Context: producers in the USART3 and OTG ISRs; the realtime handler in
 * TIM3 only (binding: never call it from the main loop); SPP/MTC from the
 * foreground dispatcher; midiTransport_checkMtc() from TIM3 and the
 * foreground.
 * Affiliates: MidiDin.c and usb_midi_core.c (push), sequencerTimer.c
 * (TIM3 call), clockSync.c, sequencer.c, MidiIn.c (system common),
 * MidiOut.c (realtime forwarding per MRT).
 */

#include <stdint.h>
#include "MidiDefs.h"

#define MIDI_REALTIME_SOURCE_DIN    ((uint8_t)midiSourceMIDI)
#define MIDI_REALTIME_SOURCE_USB    ((uint8_t)midiSourceUSB)
#define MIDI_REALTIME_SOURCE_PULSE  2u

typedef struct {
	uint8_t status;
	uint8_t source;
	uint32_t timestampUs;
} MidiRealtimeEvent;

/* 1 for Clock, Start, Continue, Stop (the bytes the ring carries). */
uint8_t  midiTransport_isRealtime(uint8_t status);
/* ISR producer: queue one realtime byte; oldest dropped when full. */
void     midiTransport_push(uint8_t status, uint8_t source,
                            uint32_t timestampUs);
uint32_t midiTransport_dropCount(void);
/*
 * TIM3 consumer (binding: TIM3 only).
 * What: for each queued event, forwards it per MRT (always, consumed or
 * not), then, when RXF R allows it and the source is the one `snc`
 * selects, applies it: Clock -> clockSync, Start -> start from the Pattern
 * start, Continue -> (Phase C) resume, Stop -> stop. Budget: 32 events per
 * call. Accessor: TIM3_IRQHandler().
 */
void     midiTransport_processRealtime(void);
/*
 * Foreground: one framed system-common message (MTC quarter frame, Song
 * Position Pointer, Song Select). Output: 1 when it acted (MTC start,
 * SPP stored). Routing is the caller's: transport messages are always
 * forwarded. Accessor: midiIn_dispatch().
 */
uint8_t  midiTransport_handleSystemCommon(const MidiMsg *msg);
/* MTC timeout: stops the sequencer when MTC that started it goes away. */
void     midiTransport_checkMtc(void);

#endif /* MIDITRANSPORT_H_ */
