/*
 * MidiDin.h
 *
 *  Derived from Uart.h, ported from the original LXR firmware by Julian
 *  Schmidt.
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

#ifndef MIDIDIN_H_
#define MIDIDIN_H_

/*
 * MidiDin - the MIDI DIN port driver (USART3, PB10 TX / PB11 RX, 31250
 * baud, AF7; S083, was Uart.c).
 *
 * What: interrupt-driven receive and transmit. The RX ISR stamps every byte
 * with TIM2 us; realtime bytes (0xF8..0xFF) go to the MidiTransport ring
 * with their full stamp, every other byte goes to the receive buffer with
 * the low 16 bits of the stamp. Transmit has a realtime lane that drains
 * before the normal lane, so clock is never queued behind notes.
 * Why: S083 Q7 rebuild; Q6/Q13 arrival stamps; whole-message queuing so a
 * full buffer never leaves half a message on the wire (new, replaces the
 * byte-wise uart_sendMidi()).
 * Context: RX/TX in USART3_IRQHandler (priority 5); senders from the
 * foreground and TIM3 under a short PRIMASK section.
 * Affiliates: MidiBuffer, MidiTransport (realtime push), MidiIn (receive
 * consumer), MidiOut (only sender).
 */

#include <stdint.h>

void     midiDin_init(void);
/*
 * Queue `count` bytes (1..3, or one realtime byte) as one unit.
 * Inputs: bytes and count. Output: 1 when all bytes were queued, 0 when the
 * lane had too little room (nothing queued; the drop counter counts it).
 * A single byte >= 0xF8 goes to the realtime lane. Context: any (PRIMASK).
 * Accessor: MidiOut only.
 */
uint8_t  midiDin_sendBytes(const uint8_t *bytes, uint8_t count);
/* Free bytes in the normal transmit lane (MidiOut's parameter throttle). */
uint8_t  midiDin_txFree(void);
/* Take one received byte and its 16-bit arrival stamp (MidiIn). */
uint8_t  midiDin_rxPop(uint8_t *byte, uint16_t *stamp);
/* Diagnostics: messages refused for lack of room, per lane. */
uint32_t midiDin_txDropCount(void);
uint32_t midiDin_realtimeTxDropCount(void);

#endif /* MIDIDIN_H_ */
