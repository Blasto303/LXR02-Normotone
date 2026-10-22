/*
 * MidiDefs.h
 *
 *  Created on: 02.04.2012
 *  Modified on 17.05.2026 by Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
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

#ifndef MIDIDEFS_H_
#define MIDIDEFS_H_

/*
 * MidiDefs.h - shared MIDI vocabulary for the LXR-02 (S083).
 *
 * What: the message type passed between the MIDI modules, status bytes,
 * controller numbers, the reserved-CC rule, the RXF/TXF filter bits, the
 * MRT routing table, channel and origin conventions, and the target-ID
 * range shared with step automation and LiveRecord.
 * Why: replaces MidiMessages.h, whose legacy LXR CC/CC2 parameter maps are
 * retired with the NRPN/Learn rework (plan section 3.1, Phase A). Every
 * Core/MIDI module and the USB driver include this header; nothing in it
 * allocates.
 * Conventions: channel numbers are 1..16 with 0 = off/unassigned; the wire
 * nibble is channel - 1. Target IDs are the canonical 9-bit IDs (0..383
 * voices, 384..447 Scene, 448..510 Effect, 511 off).
 * Affiliates: MidiIn, MidiOut, MidiTransport, MidiNotes, MidiControl,
 * MidiLearn, MidiProgram, usb_midi_core.c/h, usb_manager.c/h.
 */

#include <stdint.h>

/* Source port of a received message (stored in MidiBits.source). */
enum MidiSource {
	midiSourceMIDI = 0,	/* DIN */
	midiSourceUSB  = 1
};

/* Output port mask for midiOut_sendMsg(). */
#define MIDI_PORT_DIN   0x01u
#define MIDI_PORT_USB   0x02u
#define MIDI_PORT_BOTH  (MIDI_PORT_DIN | MIDI_PORT_USB)

/*
 * Framing bits of one message.
 *
 * source: the port it arrived on. sysxbyte: 1 when the message is SysEx
 * payload passed through byte-wise (DIN) or packet-wise (USB, length = the
 * packet's byte count - 1); consumers ignore it, routing forwards it.
 * length: data bytes after the status byte (0..2).
 */
struct MidiBits {
	/*
	 * One byte, deliberately (S083 RAM): the fields are uint8_t bitfields,
	 * so MidiMsg is 6 B (status/data1/data2/bits/stamp) instead of taking an
	 * enum/int storage unit per bitfield group. The bit layout is shared with
	 * the USB ring and the DIN framer.
	 */
	uint8_t source:1;
	uint8_t sysxbyte:1;
	uint8_t length:2;
	uint8_t :4;
};

/*
 * One MIDI message (S083: 6 bytes, was 4).
 *
 * stamp: low 16 bits of TIM2 us at arrival (S083 Q6/Q13). DIN: the first
 * byte of the message; USB: the packet. Only live-record placement reads
 * it (MidiNotes -> liveRec_triggerIn() age). LXR-generated messages leave
 * it 0.
 */
typedef struct MidiStruct {
	uint8_t status;
	uint8_t data1;
	uint8_t data2;
	struct MidiBits bits;
	uint16_t stamp;
} MidiMsg;

/* Channel voice status bytes (high nibble). */
#define NOTE_OFF            0x80u
#define NOTE_ON             0x90u
#define MIDI_AT             0xA0u	/* polyphonic key pressure */
#define MIDI_CC             0xB0u
#define PROG_CHANGE         0xC0u
#define CHANNEL_PRESSURE    0xD0u
#define MIDI_PITCH_WHEEL    0xE0u

/* System common and realtime. */
#define SYSEX_START         0xF0u
#define MIDI_MTC_QFRAME     0xF1u
#define MIDI_SONG_POSITION  0xF2u	/* S083 Phase A: framed (2 data bytes) */
#define MIDI_SONG_SEL       0xF3u
#define MIDI_TUNE_REQUEST   0xF6u
#define SYSEX_END           0xF7u
#define MIDI_CLOCK          0xF8u
#define MIDI_START          0xFAu
#define MIDI_CONTINUE       0xFBu
#define MIDI_STOP           0xFCu
#define MIDI_ACTIVE_SENSE   0xFEu
#define MIDI_SYSTEM_RESET   0xFFu

/* Controller numbers with a fixed meaning (MIDI_SPEC section 5.1). */
#define CC_BANK_CHANGE       0u
#define CC_MOD_WHEEL         1u
#define CC_NOTE_ACTION       2u	/* S083 C8: 0 single, 1 BAR <, 2 BAR > */
#define CC_DATA_ENTRY_MSB    6u
#define CC_BANK_SELECT_LSB   32u
#define CC_DATA_ENTRY_LSB    38u
#define CC_DATA_INCREMENT    96u
#define CC_DATA_DECREMENT    97u
#define CC_NRPN_LSB          98u
#define CC_NRPN_MSB          99u
#define CC_RPN_LSB           100u
#define CC_RPN_MSB           101u
#define CC_CHANNEL_MODE_FIRST 120u
#define CC_ALL_SOUND_OFF     120u
#define CC_ALL_NOTES_OFF     123u

/*
 * Reserved controllers (MIDI_SPEC section 5.1, plan C6).
 *
 * What: 1 for a CC that can never be learned: 0, 1, 2, 6, 32, 38,
 * 96..101 and the channel-mode CCs 120..127. Why: these CCs have fixed
 * meanings (Bank change, Morph, note action, NRPN/RPN protocol, Bank LSB,
 * channel mode). Inputs: controller number 0..127. Output: 0/1.
 * Accessors: MidiLearn (capture and lookup), MidiControl.
 */
static inline uint8_t midiDefs_ccReserved(uint8_t cc)
{
	return (uint8_t)(cc <= CC_NOTE_ACTION || cc == CC_DATA_ENTRY_MSB ||
	                 cc == CC_BANK_SELECT_LSB || cc == CC_DATA_ENTRY_LSB ||
	                 (cc >= CC_DATA_INCREMENT && cc <= CC_RPN_MSB) ||
	                 cc >= CC_CHANNEL_MODE_FIRST);
}

/* RXF/TXF filter bits (settings midi_filt_rx/tx, menu midiFilterNames). */
#define MIDI_FILTER_NOTE      0x01u
#define MIDI_FILTER_REALTIME  0x02u
#define MIDI_FILTER_CONTROL   0x04u
#define MIDI_FILTER_PROGRAM   0x08u

/*
 * MRT routing (MIDI_SPEC section 3.3, plan F22).
 *
 * What: the eight menu values off UtD DtD DtU bth DtA AtD all and the three
 * routes they combine. USB -> USB is never routed. Inputs: menu value 0..7
 * (out of range = off). Output: MIDI_ROUTE_* mask. Accessors:
 * midiIn_setRouting(). Affiliates: midiRoutingNames (MenuText.h),
 * midiOut_forward(), midiOut_forwardRealtime().
 */
#define MIDI_ROUTE_USB_TO_DIN  0x01u
#define MIDI_ROUTE_DIN_TO_DIN  0x02u
#define MIDI_ROUTE_DIN_TO_USB  0x04u

static inline uint8_t midiDefs_routeMask(uint8_t mrt)
{
	static const uint8_t table[8] = {
		0u,                                             /* off */
		MIDI_ROUTE_USB_TO_DIN,                          /* UtD */
		MIDI_ROUTE_DIN_TO_DIN,                          /* DtD */
		MIDI_ROUTE_DIN_TO_USB,                          /* DtU */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_USB,  /* bth */
		MIDI_ROUTE_DIN_TO_DIN | MIDI_ROUTE_DIN_TO_USB,  /* DtA */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_DIN,  /* AtD */
		MIDI_ROUTE_USB_TO_DIN | MIDI_ROUTE_DIN_TO_DIN |
		    MIDI_ROUTE_DIN_TO_USB                       /* all */
	};
	return (mrt < 8u) ? table[mrt] : 0u;
}

/* Channels: 1..16, 0 = off (G) / unassigned (track). */
#define MIDI_CHANNEL_NONE   0u
#define MIDI_CHANNEL_COUNT  16u

/*
 * Track MIDI settings `ch` and `nte`: stored "off" values (S083 follow-up,
 * user 2026-10-22).
 *
 * Track channel byte: 0 = `glb` (follow G), 1..16 = channel,
 * MIDI_TRACK_CHANNEL_OFF = `off`: the track has no MIDI channel at all (no
 * notes in or out, no voice-channel CC/NRPN/PC/Learn, no per-track PC out).
 * Track note byte: 0 = `on` (the default: no root, the track takes part in
 * the stripe/split note map), 1..127 = root, MIDI_TRACK_NOTE_OFF = `off`:
 * the track takes no notes in and sends none out; its channel still carries
 * CC/NRPN/PC/Learn. The menu shows both lists as off, glb/on, values
 * (menu.c converts between the menu order and these stored values).
 * Accessors: SceneData setters/getters, MidiNoteMap, storageTypes parse.
 */
#define MIDI_TRACK_CHANNEL_OFF  17u
#define MIDI_TRACK_NOTE_OFF     128u

/* Origin of a change: echo suppression and record exclusions (S083 Q1/Q9). */
#define MIDI_ORIGIN_PANEL   0u
#define MIDI_ORIGIN_MIDI    1u

/* 9-bit target IDs (plan C1). 511 is "off" and never a target. */
#define MIDI_TARGET_OFF     511u

#endif /* MIDIDEFS_H_ */
