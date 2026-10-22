/*
 * MidiBuffer.c
 *
 *  Derived from FIFO.c, ported from the original LXR firmware by Julian
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

#include "MidiBuffer.h"
#include <string.h>

/* Contracts in MidiBuffer.h. Logic carried over from FIFO.c. */
void midiBuffer_init(midiBuffer_t *buf)
{
	memset((void *)buf->data, 0, MIDI_BUFFER_SIZE);
	buf->read = 0u;
	buf->write = 0u;
}

uint8_t midiBuffer_in(midiBuffer_t *buf, uint8_t byte)
{
	uint8_t next = (uint8_t)((buf->write + 1u) & MIDI_BUFFER_MASK);

	if (next == buf->read)
		return 0u;
	buf->data[buf->write] = byte;
	buf->write = next;
	return 1u;
}

uint8_t midiBuffer_out(midiBuffer_t *buf, uint8_t *byte)
{
	if (buf->read == buf->write)
		return 0u;
	*byte = buf->data[buf->read];
	buf->read = (uint8_t)((buf->read + 1u) & MIDI_BUFFER_MASK);
	return 1u;
}

uint8_t midiBuffer_free(const midiBuffer_t *buf)
{
	return (uint8_t)((buf->read - buf->write - 1u) & MIDI_BUFFER_MASK);
}

void midiBuffer_rxInit(midiBuffer_rx_t *buf)
{
	midiBuffer_init(&buf->bytes);
	memset((void *)buf->stamp, 0, sizeof(buf->stamp));
}

/*
 * The stamp is written into the slot before midiBuffer_in() publishes the
 * write index, so the consumer never reads a byte with a stale stamp.
 */
uint8_t midiBuffer_rxIn(midiBuffer_rx_t *buf, uint8_t byte, uint16_t stamp)
{
	buf->stamp[buf->bytes.write] = stamp;
	return midiBuffer_in(&buf->bytes, byte);
}

/*
 * Take one byte and its stamp (S083 assessment F-6).
 * The stamp is read BEFORE midiBuffer_out() publishes the new read index:
 * once the slot is released, the producer ISR may reuse it (only when the
 * buffer was full) and overwrite the stamp, giving the byte a wrong
 * arrival age. Mirrors the producer rule in midiBuffer_rxIn().
 */
uint8_t midiBuffer_rxOut(midiBuffer_rx_t *buf, uint8_t *byte,
                         uint16_t *stamp)
{
	uint16_t slotStamp;

	if (buf->bytes.read == buf->bytes.write)
		return 0u;
	slotStamp = buf->stamp[buf->bytes.read];
	if (!midiBuffer_out(&buf->bytes, byte))
		return 0u;
	*stamp = slotStamp;
	return 1u;
}
