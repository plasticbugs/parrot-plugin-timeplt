// The Mac's stand-in for the pico-sdk, for test/host (see host.c): just
// what the examples' loops use.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef unsigned int uint;
#ifndef MAX                         // (as the pico-sdk's pico/platform/compiler.h)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((b) > (a) ? (a) : (b))
#endif
#ifdef __cplusplus                  // (C++ plugins include it as they would the pico-sdk's)
extern "C" {
#endif
uint32_t time_us_32(void);          // the test's clock: it moves on with every call
void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);
#ifdef __cplusplus
}
#endif
static inline void tight_loop_contents(void) {}
