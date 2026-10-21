/*
 * usb_manager.h
 *
 * Ported from original LXR firmware by Julian Schmidt.
 * stm32f4xx.h dependency replaced with stdint.h.
 * USB_DETECT_PIN/PORT removed (not used in LXR-02, see usb_manager.c).
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#ifndef USB_MANAGER_H_
#define USB_MANAGER_H_

#include <stdint.h>
#include "usbd_conf.h"
#include "usbd_usr.h"
#include "usb_midi_core.h"
#include "MidiDefs.h"

void    usb_init(void);
void    usb_stop(void);
void    usb_start(void);
void    usb_tick(void);
/*
 * Queue one 4-byte USB-MIDI event packet (S083).
 * Inputs: code index (cable 0) and the three MIDI bytes (unused bytes 0).
 * Output: bytes in the USB IN buffer, flushed by usb_tick(). Context: any
 * (PRIMASK). Why: MidiOut now computes the code index (usb_sendMidi() used
 * status >> 4 for every message). Accessor: midiOut_sendMsg() only.
 */
void    usb_sendMidiPacket(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2);
uint8_t usb_getMidi(MidiMsg *msg);
void    usb_flushMidi(void);

#endif /* USB_MANAGER_H_ */
