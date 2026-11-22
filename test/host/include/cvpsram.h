// The Mac's stand-in for sdk/cvpsram.h: 8 MB of ordinary memory.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CVPSRAM_SIZE     0x00800000u
extern uint8_t host_psram[CVPSRAM_SIZE];
#define CVPSRAM_CACHED   (host_psram)
#define CVPSRAM_UNCACHED ((volatile uint32_t *)(void *)host_psram)
bool cvpsram_init(void);
const uint8_t *cvpsram_id(void);
