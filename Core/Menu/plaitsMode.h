/*
 * PLAITS mode for LIVE (Normotone fork).
 *
 * A kit is played with Plaits engines on all seven tracks while the patterns
 * stay as they are:
 *   DRUM1  PlKick analog          DRUM2  PlKick synthetic
 *   DRUM3  PlModal (tom, perc)    SNARE  PlSnare analog
 *   CYMBAL PlHat ring mod         HH/OH  PlHat 808 (open hat on track 7)
 *
 * Conversion keeps what the pattern relies on: the pitch (LXR coarse is a
 * MIDI note, as in Plaits, folded by octaves into each engine's range), the
 * sound length (the LXR amp envelope's -40 dB time is matched to the Plaits
 * Decay that gives the same time), fine tune, volume, pan, drive, sample
 * rate, LFOs and velocity, with a level trim per engine (Plaits hats are
 * louder, the modal quieter; a voice carried by its transient keeps half
 * the transient level). Filter cutoff moves Tone, the pitch envelope
 * amount moves the kick's Harmonics, the snare noise mix moves Snappy.
 * LFO and velocity destinations, and the pattern's automation (p-locks), are
 * redirected to the matching Plaits parameter; a destination with no match
 * is switched off.
 *
 * RAM: 12.3 KB of conversion tables (6 slots x 8 engine types), built in
 * the background after boot by plaitsMode_service().
 */
#ifndef PLAITS_MODE_H_
#define PLAITS_MODE_H_

#include <stdint.h>
#include "SceneData.h"

/*
 * Background table build, one table per call (about 1 ms). Returns 1 once
 * every table is ready. A conversion before that builds what it needs.
 */
uint8_t plaitsMode_service(void);

/* Build the Plaits kit for `src` into `dst` (dst != src). */
void plaitsMode_convertKit(kit_t *dst, const kit_t *src);

/*
 * Automation redirection for the pattern whose own kit is `own`.
 * plaitsMode_mapOn(own) builds the map and enables it; plaitsMode_mapOff()
 * disables it. While enabled, the sequencer passes every instrument
 * automation write through plaitsMode_remap().
 */
void plaitsMode_mapOn(const kit_t *own);
void plaitsMode_mapOff(void);
extern uint8_t plaitsMode_mapActive;
/* 1 = write *target / *value, 0 = drop the write. */
uint8_t plaitsMode_remap(uint16_t *target, uint8_t *value);

#endif /* PLAITS_MODE_H_ */
