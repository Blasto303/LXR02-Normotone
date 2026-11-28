/*
 * Core/DSP/Effects/Destroy/DestroyEffect.h
 *
 * Normotone `dst` DSP: inserted on MAIN (EFFECT_IO_MAIN_INSERT), the mixer
 * hands it the summed MAIN block and takes the result back in place.
 * Share layout: DESTROY_STATE_BYTES of state, then the 16,384-sample plate
 * ring (the DlyVerb plate, run at a quarter of the codec rate).
 */
#ifndef DESTROY_EFFECT_H_
#define DESTROY_EFFECT_H_

#include "EffectsManager.h"

#define DESTROY_TYPE_ROWS         14u
#define DESTROY_STATE_BYTES       512u
#define DESTROY_RVB_SAMPLES       16384u
/* + the model delay pool (DestroyModels.h, 8,192 int16) after the ring. */
#define DESTROY_POOL_SAMPLES      8192u
#define DESTROY_BUFFER_MIN_BYTES  (DESTROY_STATE_BYTES + DESTROY_RVB_SAMPLES * 2u + \
                                   DESTROY_POOL_SAMPLES * 2u)

typedef struct {
    uint8_t raw[DESTROY_TYPE_ROWS];
    void *state;
    uint32_t seated_bytes;
    uint32_t seated_offset;
} DestroyRuntime;

extern const effect_type_ops_t destroy_ops;

#endif /* DESTROY_EFFECT_H_ */
