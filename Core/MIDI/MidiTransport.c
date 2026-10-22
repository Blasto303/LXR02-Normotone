/*
 * MidiTransport.c - clock, transport and position from MIDI (S083).
 *
 * Carried over from MidiRealtime.c and the realtime/MTC half of
 * MidiParser.c (original LXR firmware by Julian Schmidt); rewritten for the
 * S083 module split.
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

#include "MidiTransport.h"
#include "MidiIn.h"
#include "MidiOut.h"
#include "sequencer.h"
#include "clockSync.h"
#include "buttonHandler.h"
#include "triggerJacks.h"
#include "timebase.h"
#include "globals.h"
#include "config.h"

/*
 * Timestamped realtime ring (carried over from MidiRealtime.c).
 *
 * What: 32 events, each one realtime status byte with its source and full
 * TIM2 stamp. Why: clock and transport must bypass the channel path, and a
 * stale clock is worse than a missing one, so the oldest event is dropped
 * when the ring is full. Producers: the USART3 and OTG ISRs. Consumer: TIM3
 * through midiTransport_processRealtime() only.
 */
#define MIDI_REALTIME_RING_SIZE 32u
#define MIDI_REALTIME_RING_MASK (MIDI_REALTIME_RING_SIZE - 1u)

static volatile MidiRealtimeEvent midiTransport_ring[MIDI_REALTIME_RING_SIZE];
static volatile uint8_t midiTransport_head = 0;
static volatile uint8_t midiTransport_tail = 0;
static volatile uint32_t midiTransport_droppedCount = 0;

/* MTC state (from MidiParser.c). */
static uint8_t midiTransport_mtcIgnore = 1;
static uint32_t volatile midiTransport_lastMtcReceived = 0x0;
static uint8_t midiTransport_mtcIsRunning = 0;

/* AUTO source hold (from MidiParser.c). */
#define MIDI_AUTO_SYNC_SOURCE_HOLD_US 500000UL
static uint32_t midiTransport_lastDinSyncUs = 0;
static uint8_t midiTransport_haveDinSync = 0;

static uint32_t midiTransport_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void midiTransport_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

uint8_t midiTransport_isRealtime(uint8_t status)
{
	switch (status) {
	case MIDI_CLOCK:
	case MIDI_START:
	case MIDI_CONTINUE:
	case MIDI_STOP:
		return 1u;
	default:
		return 0u;
	}
}

void midiTransport_push(uint8_t status, uint8_t source, uint32_t timestampUs)
{
	uint8_t next;
	uint32_t primask;

	if (!midiTransport_isRealtime(status))
		return;

	primask = midiTransport_irqSave();
	next = (uint8_t)((midiTransport_head + 1u) & MIDI_REALTIME_RING_MASK);
	if (next == midiTransport_tail) {
		/* Realtime clock is more useful fresh than complete. Drop the oldest
		** event rather than blocking an ISR or letting a stale clock burst play
		** catch-up later. The counter is exposed for diagnostics. */
		midiTransport_tail = (uint8_t)((midiTransport_tail + 1u) &
		                              MIDI_REALTIME_RING_MASK);
		midiTransport_droppedCount++;
	}

	midiTransport_ring[midiTransport_head].status = status;
	midiTransport_ring[midiTransport_head].source = source;
	midiTransport_ring[midiTransport_head].timestampUs = timestampUs;
	midiTransport_head = next;
	midiTransport_irqRestore(primask);
}

static uint8_t midiTransport_pop(MidiRealtimeEvent *event)
{
	uint32_t primask = midiTransport_irqSave();

	if (midiTransport_tail == midiTransport_head) {
		midiTransport_irqRestore(primask);
		return 0u;
	}
	*event = midiTransport_ring[midiTransport_tail];
	midiTransport_tail = (uint8_t)((midiTransport_tail + 1u) &
	                              MIDI_REALTIME_RING_MASK);
	midiTransport_irqRestore(primask);
	return 1u;
}

uint32_t midiTransport_dropEvents(void)
{
	return midiTransport_droppedCount;
}

/*
 * Is this realtime event from the source `snc` selects? (from MidiParser.c)
 *
 * What: applies the AUTO priority (internal, USB, DIN, jack pulse) and
 * records activity for AUTO. Why: only one clock source may drive the
 * sequencer. Output: 1 = act on the event.
 */
static uint8_t midiTransport_sourceAllowed(const MidiRealtimeEvent *event)
{
	const uint8_t mode = seq_getExtSyncSource();
	const uint8_t source = event->source;
	const uint32_t nowUs = event->timestampUs;

	switch (mode) {
	case SEQ_EXT_SYNC_DIN:
		if (source == MIDI_REALTIME_SOURCE_DIN) {
			seq_noteExtSyncActivity(SEQ_EXT_SYNC_DIN, nowUs);
			return 1u;
		}
		return 0u;
	case SEQ_EXT_SYNC_USB:
		if (source == MIDI_REALTIME_SOURCE_USB) {
			seq_noteExtSyncActivity(SEQ_EXT_SYNC_USB, nowUs);
			return 1u;
		}
		return 0u;
	case SEQ_EXT_SYNC_PULSE:
		return 0u;
	case SEQ_EXT_SYNC_AUTO:
		/* AUTO priority, low to high: internal, USB, DIN, jack pulse. Jack
		** activity suppresses MIDI clocks; DIN suppresses USB while fresh. */
		if (triggerJacks_clockInputRecently(nowUs))
			return 0u;
		if (source == MIDI_REALTIME_SOURCE_DIN) {
			midiTransport_lastDinSyncUs = nowUs;
			midiTransport_haveDinSync = 1u;
			seq_noteExtSyncActivity(SEQ_EXT_SYNC_DIN, nowUs);
			return 1u;
		}
		if (source == MIDI_REALTIME_SOURCE_USB) {
			if (midiTransport_haveDinSync &&
			    timebase_tim2Delta(nowUs, midiTransport_lastDinSyncUs) <=
			        MIDI_AUTO_SYNC_SOURCE_HOLD_US)
				return 0u;
			seq_noteExtSyncActivity(SEQ_EXT_SYNC_USB, nowUs);
			return 1u;
		}
		return 0u;
	case SEQ_EXT_SYNC_OFF:
	default:
		return 0u;
	}
}

/*
 * Apply one realtime event after forwarding it (from MidiParser.c).
 *
 * What: forwards per MRT first (always, consumed or not, plan 3.2), then
 * gates on RXF R and the `snc` source. Why: routing follows MRT only, while
 * sync follows the selected source. Phase A keeps the pre-S083 rule that
 * Continue maps to Start; Phase C splits them. Affiliates: clockSync.c,
 * sequencer.c, midiOut_forwardRealtime().
 */
static void midiTransport_handleEvent(const MidiRealtimeEvent *event)
{
	midiOut_forwardRealtime(event->status, event->source);

	if (!midiIn_rxAllows(MIDI_FILTER_REALTIME))
		return;
	if (!midiTransport_sourceAllowed(event))
		return;

	switch (event->status) {
	case MIDI_CLOCK:
		sync_tickTimestamp(event->timestampUs);
		break;
	case MIDI_START:
		sync_midiStartStop(1);
		break;
	case MIDI_CONTINUE:
		/* S083 T1: resume, not restart. */
		sync_midiContinue();
		break;
	case MIDI_STOP:
		sync_midiStartStop(0);
		break;
	default:
		break;
	}
}

void midiTransport_processRealtime(void)
{
	MidiRealtimeEvent event;
	uint8_t budget = 32u;

	while (budget-- && midiTransport_pop(&event))
		midiTransport_handleEvent(&event);
}

/*
 * Is `source` the MIDI source `snc` selects? (S083 Q11)
 *
 * What: the foreground twin of midiTransport_sourceAllowed() without its
 * activity side effects: din/usb accept their port; aut accepts DIN, and
 * USB only while no DIN sync was seen within MIDI_AUTO_SYNC_SOURCE_HOLD_US;
 * off and pls accept nothing. Inputs: source port. Output: 0/1.
 * Accessor: midiTransport_handleSystemCommon() (SPP).
 */
static uint8_t midiTransport_sourceSelected(uint8_t source)
{
	switch (seq_getExtSyncSource()) {
	case SEQ_EXT_SYNC_DIN:
		return (uint8_t)(source == MIDI_REALTIME_SOURCE_DIN);
	case SEQ_EXT_SYNC_USB:
		return (uint8_t)(source == MIDI_REALTIME_SOURCE_USB);
	case SEQ_EXT_SYNC_AUTO:
		if (source == MIDI_REALTIME_SOURCE_DIN)
			return 1u;
		if (source == MIDI_REALTIME_SOURCE_USB)
			return (uint8_t)(!(midiTransport_haveDinSync &&
			    timebase_tim2Delta(timebase_tim2Now(),
			                       midiTransport_lastDinSyncUs) <=
			    MIDI_AUTO_SYNC_SOURCE_HOLD_US));
		return 0u;
	case SEQ_EXT_SYNC_OFF:
	case SEQ_EXT_SYNC_PULSE:
	default:
		return 0u;
	}
}

/* Contract in MidiTransport.h. */
uint8_t midiTransport_handleSystemCommon(const MidiMsg *msg)
{
	if (!msg || !midiIn_rxAllows(MIDI_FILTER_REALTIME))
		return 0u;
	switch (msg->status) {
	case MIDI_MTC_QFRAME:
		/*
		 * MTC start strategy (unchanged from MidiParser.c).
		 *
		 * If all 8 quarter-frame messages arrive with a value of 0, the song
		 * has just started at 0:0:0:0; only then is the sequencer started. A
		 * running sequencer or external clock sync makes MTC informational
		 * only. Why: starting mid-song from tape would desynchronise the LXR.
		 */
		if ((msg->data1 & 0x70u) == 0u)
			midiTransport_lastMtcReceived = systick_ticks;
		if (seq_isRunning())
			return 0u;
		if (seq_getExtSync())
			return 0u;
		if ((msg->data1 & 0x7Fu) == 0u) {
			midiTransport_mtcIgnore = 0u;
		} else if (midiTransport_mtcIgnore) {
			return 0u;
		} else if ((msg->data1 & 0x70u) != 0x70u) {
			if ((msg->data1 & 0x0Fu) != 0u) {
				midiTransport_mtcIgnore = 1u;
				return 0u;
			}
		} else {
			if ((msg->data1 & 0x01u) == 0u) {
				buttonHandler_setRunStopState(1);
				midiTransport_mtcIgnore = 1u;
				midiTransport_mtcIsRunning = 1u;
				midiTransport_lastMtcReceived = systick_ticks;
				seq_setRunning(1);
				return 1u;
			}
		}
		return 0u;
	case MIDI_SONG_POSITION:
		/*
		 * Song Position Pointer (S083 T1, Q11).
		 *
		 * What: while stopped, from the `snc` source, stores the position
		 * (in 16th notes, 14 bits) as the resume tick: x 6 MIDI clocks x 4
		 * internal ticks = x 24. The next Continue starts there. Ignored
		 * while running (plan 3.8). Output: 1 when stored.
		 */
		{
			uint8_t source = (msg->bits.source == midiSourceUSB)
			                 ? MIDI_REALTIME_SOURCE_USB
			                 : MIDI_REALTIME_SOURCE_DIN;
			uint32_t pos = (uint32_t)msg->data1 |
			               ((uint32_t)msg->data2 << 7);

			if (seq_isRunning() || !midiTransport_sourceSelected(source))
				return 0u;
			seq_setResumeTick(pos * 24u);
			return 1u;
		}
	default:
		return 0u;
	}
}

/* Contract in MidiTransport.h. Body carried over from MidiParser.c. */
void midiTransport_checkMtc(void)
{
	if (!midiTransport_mtcIsRunning)
		return;
	if (!seq_isRunning()) {
		/* inform mtc that his services are no longer needed */
		midiTransport_mtcIsRunning = 0u;
		return;
	}
	/* At 24 fps (the lowest rate) a completed message arrives about every
	** 83 ms. Use a 100 ms timeout scaled to the 0.25 ms systick. */
	if (systick_ticks - midiTransport_lastMtcReceived >
	    (100u * SYSTICK_TICKS_PER_MS)) {
		midiTransport_mtcIsRunning = 0u;
		buttonHandler_setRunStopState(0);
		seq_setRunning(0);
	}
}
