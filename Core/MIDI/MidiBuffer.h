/*
 * MidiBuffer.h
 *
 *  Derived from FIFO.h, ported from the original LXR firmware by Julian
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

#ifndef MIDIBUFFER_H_
#define MIDIBUFFER_H_

/*
 * MidiBuffer - single-producer single-consumer byte buffers (S083).
 *
 * What: the DIN transmit buffers (normal and realtime lanes) and the DIN
 * receive buffer, which also holds a 16-bit arrival stamp per byte.
 * Why: replaces FIFO.c/h (user request, S083 Q7) and adds the receive
 * stamps that live-record placement needs (S083 Q6, Q13).
 * Contract: one producer and one consumer per buffer. The producer writes
 * the payload (and stamp) before publishing the write index; the consumer
 * reads (byte and stamp, S083 F-6) before publishing the read index. One
 * slot is kept free to tell full from empty, so a 64-entry buffer holds
 * 63 bytes (as FIFO.c did).
 * RAM: midiBuffer_t 66 B; midiBuffer_rx_t 194 B (66 + 128 stamp bytes,
 * accepted 2026-10-21, Q13). Instances live in MidiDin.c.
 * Affiliates: MidiDin.c (ISR side), MidiIn.c (receive consumer).
 */

#include <stdint.h>

#define MIDI_BUFFER_SIZE  64u
#define MIDI_BUFFER_MASK  (MIDI_BUFFER_SIZE - 1u)

typedef struct {
	volatile uint8_t data[MIDI_BUFFER_SIZE];
	volatile uint8_t read;
	volatile uint8_t write;
} midiBuffer_t;

typedef struct {
	midiBuffer_t bytes;
	volatile uint16_t stamp[MIDI_BUFFER_SIZE];
} midiBuffer_rx_t;

/* Empty the buffer. Call before the producer or consumer runs (boot). */
void    midiBuffer_init(midiBuffer_t *buf);
/* Append one byte. Returns 1 when stored, 0 when full (byte dropped). */
uint8_t midiBuffer_in(midiBuffer_t *buf, uint8_t byte);
/* Take the oldest byte. Returns 1 with *byte set, 0 when empty. */
uint8_t midiBuffer_out(midiBuffer_t *buf, uint8_t *byte);
/* Free slots (0..MIDI_BUFFER_SIZE-1). Exact only under the producer's
** lock; used by MidiDin to queue whole messages or nothing. */
uint8_t midiBuffer_free(const midiBuffer_t *buf);
/* Receive variants: the stamp travels with its byte. */
void    midiBuffer_rxInit(midiBuffer_rx_t *buf);
uint8_t midiBuffer_rxIn(midiBuffer_rx_t *buf, uint8_t byte, uint16_t stamp);
uint8_t midiBuffer_rxOut(midiBuffer_rx_t *buf, uint8_t *byte,
                         uint16_t *stamp);

#endif /* MIDIBUFFER_H_ */
