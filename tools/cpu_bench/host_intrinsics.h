/* Host versions of Core/compat/cmsis_intrinsics.h (ARM saturation asm).
 * Force-included by tools/cpu_bench/build.sh before any firmware header. */
#ifndef CMSIS_INTRINSICS_H_
#define CMSIS_INTRINSICS_H_
#include <stdint.h>
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
static inline int32_t host_sat(int64_t v, int bits)
{
    int64_t hi = ((int64_t)1 << (bits - 1)) - 1, lo = -hi - 1;
    return (int32_t)(v > hi ? hi : (v < lo ? lo : v));
}
static inline uint32_t host_usat(int64_t v, int bits)
{
    int64_t hi = ((int64_t)1 << bits) - 1;
    return (uint32_t)(v > hi ? hi : (v < 0 ? 0 : v));
}
__STATIC_FORCEINLINE uint32_t __QADD16(uint32_t a, uint32_t b)
{
    int32_t l = host_sat((int16_t)a + (int16_t)b, 16), h = host_sat((int16_t)(a >> 16) + (int16_t)(b >> 16), 16);
    return ((uint32_t)(uint16_t)l) | ((uint32_t)(uint16_t)h << 16);
}
__STATIC_FORCEINLINE uint32_t __QSUB16(uint32_t a, uint32_t b)
{
    int32_t l = host_sat((int16_t)a - (int16_t)b, 16), h = host_sat((int16_t)(a >> 16) - (int16_t)(b >> 16), 16);
    return ((uint32_t)(uint16_t)l) | ((uint32_t)(uint16_t)h << 16);
}
__STATIC_FORCEINLINE int32_t __QADD(int32_t a, int32_t b) { return host_sat((int64_t)a + b, 32); }
__STATIC_FORCEINLINE int32_t __QSUB(int32_t a, int32_t b) { return host_sat((int64_t)a - b, 32); }
#define __SSAT(v, n) host_sat((int64_t)(v), (n))
#define __USAT(v, n) host_usat((int64_t)(v), (n))
#define __CLZ(v) ((uint8_t)((uint32_t)(v) ? __builtin_clz((uint32_t)(v)) : 32))
typedef union {
    struct { uint32_t r0:16, GE:4, r1:7, Q:1, V:1, C:1, Z:1, N:1; } b;
    uint32_t w;
} APSR_Type;
__STATIC_FORCEINLINE uint32_t __get_APSR(void) { return 0; }
__STATIC_FORCEINLINE uint32_t __get_PRIMASK(void) { return 0; }
__STATIC_FORCEINLINE void __set_PRIMASK(uint32_t v) { (void)v; }
__STATIC_FORCEINLINE void __disable_irq(void) {}
__STATIC_FORCEINLINE void __enable_irq(void) {}
#endif
