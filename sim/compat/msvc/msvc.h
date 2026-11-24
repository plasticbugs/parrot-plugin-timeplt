// Included first in every file when building with MSVC (sim/CMakeLists.txt).
// The SDK's headers mark a few functions with GCC's attributes; MSVC has
// none, and none of those the SDK uses changes a struct's layout.
#pragma once
#define __attribute__(x)
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

// GCC's builtins that plugins use, made from MSVC's own (clang-cl has GCC's).
// ctz and clz of 0 are undefined in GCC too.
#ifndef __clang__
#include <intrin.h>
#include <stdlib.h>
#define __builtin_expect(x, v) (x)
#define __builtin_trap() abort()
#define __builtin_unreachable() __assume(0)
#define __builtin_bswap16(x) _byteswap_ushort(x)
#define __builtin_bswap32(x) _byteswap_ulong(x)
#define __builtin_bswap64(x) _byteswap_uint64(x)
static __inline int __builtin_ctz(unsigned x) { unsigned long i; _BitScanForward(&i, x); return (int)i; }
static __inline int __builtin_clz(unsigned x) { unsigned long i; _BitScanReverse(&i, x); return 31 - (int)i; }
static __inline int __builtin_clzl(unsigned long x) { return __builtin_clz((unsigned)x); }   // long is 32 bits here
static __inline int __builtin_ctzll(unsigned long long x) {
    unsigned long i;
    if ((unsigned)x) { _BitScanForward(&i, (unsigned)x); return (int)i; }
    _BitScanForward(&i, (unsigned)(x >> 32)); return 32 + (int)i;
}
static __inline int __builtin_clzll(unsigned long long x) {
    unsigned long i;
    if (x >> 32) { _BitScanReverse(&i, (unsigned)(x >> 32)); return 31 - (int)i; }
    _BitScanReverse(&i, (unsigned)x); return 63 - (int)i;
}
static __inline int __builtin_popcount(unsigned x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    return (int)((((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
}
#endif
