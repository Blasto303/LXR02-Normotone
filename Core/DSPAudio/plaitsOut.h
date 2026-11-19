/*
 * Core/DSPAudio/plaitsOut.h
 *
 * Normotone: soft output stage of the Plaits engines.
 *
 * What: converts one engine sample in int16 units to int16. Below the knee
 * (0.6 of full scale, -4.4 dBFS) it is untouched; above, a rational curve
 * with unit slope at the knee bends it towards full scale, which it never
 * reaches. Why: the engines peak up to about +4 dB over full scale at high
 * velocity (hat, modal), and the former hard clamp turned those peaks into
 * audible clicks. Cost: one compare per sample, a division only above the
 * knee. Accessors: PlaitsKick, PlaitsSnare, PlaitsHat, PlaitsModal voices.
 */
#ifndef PLAITS_OUT_H_
#define PLAITS_OUT_H_

#include <stdint.h>
#include <math.h>

#define PLAITS_OUT_FS    32767.0f
#define PLAITS_OUT_KNEE  (0.6f * PLAITS_OUT_FS)

static inline int16_t plaitsOut_toInt16(float s)
{
    const float a = fabsf(s);

    if (a > PLAITS_OUT_KNEE) {
        const float t = (a - PLAITS_OUT_KNEE) *
                        (1.0f / (PLAITS_OUT_FS - PLAITS_OUT_KNEE));
        const float y = PLAITS_OUT_KNEE +
                        (PLAITS_OUT_FS - PLAITS_OUT_KNEE) * t / (1.0f + t);

        s = (s < 0.0f) ? -y : y;
    }
    return (int16_t)s;
}

#endif /* PLAITS_OUT_H_ */
