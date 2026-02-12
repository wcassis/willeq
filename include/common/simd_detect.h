#ifndef EQT_SIMD_DETECT_H
#define EQT_SIMD_DETECT_H

// Platform SIMD detection
// Defines EQT_HAS_NEON for ARM NEON and EQT_HAS_SSE2 for x86 SSE2.
// Every SIMD path must have a scalar #else fallback.

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
  #define EQT_HAS_NEON 1
  #include <arm_neon.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #define EQT_HAS_SSE2 1
  #include <emmintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
  #define EQT_ALIGN(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
  #define EQT_ALIGN(n) __declspec(align(n))
#else
  #define EQT_ALIGN(n)
#endif

#endif // EQT_SIMD_DETECT_H
