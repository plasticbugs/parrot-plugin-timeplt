// tproms.h -- where the ROMs go: MAME's "timeplt" set, each file known by
// its size and CRC-32, never by its name. Portable: the plugin and the Mac
// tests share it. (Donkey Kong's dkroms.h.)
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "tp.h"

typedef struct {
    const char *name;                   // MAME's name, for messages
    uint32_t size, crc;
    uint32_t at;                        // where its bytes go: in the store, or (TP_GFX_AT on) in the graphics' room
} tp_rom_file_t;

#define TP_ROM_FILES 11
#define TPROMS_ALL   ((1u << TP_ROM_FILES) - 1)
#define TP_GFX_AT    0x80000000u        // (in tp_rom_gfx, which tp_init decodes and then leaves alone)
#define TP_GFX_BYTES 0x6000             // tm6, then tm4 and tm5
extern const tp_rom_file_t tp_rom_files[TP_ROM_FILES];

typedef struct {
    uint8_t maincpu[0x6000], sound[0x1000], proms[0x240];
} tp_rom_store_t;
extern tp_rom_store_t tp_rom_store;
extern uint8_t *tp_rom_gfx;             // TP_GFX_BYTES of room for the graphics, set before loading

uint32_t tp_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
// which of the files still to find (not in found) has this size and CRC-32 (-1: none)
int tproms_wanted(uint32_t size, uint32_t crc, uint32_t found);
bool tproms_size_ok(uint32_t size);     // one of the files' sizes
// a file's bytes (size of them, its CRC-32 right) to its place
void tproms_put(int i, const uint8_t *data);
// the board's ROMs, from the store and the graphics' room
void tproms_roms(tp_roms_t *r);
