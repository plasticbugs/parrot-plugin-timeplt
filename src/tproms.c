// tproms.c -- see tproms.h. The sizes and CRC-32s are MAME 0.288's
// (timeplt.cpp's timeplt).
#include "tproms.h"

#include <stddef.h>
#include <string.h>

tp_rom_store_t tp_rom_store;
uint8_t *tp_rom_gfx;

#define M(o) (uint32_t)(offsetof(tp_rom_store_t, maincpu) + (o))
#define G(o) (TP_GFX_AT | (o))
const tp_rom_file_t tp_rom_files[TP_ROM_FILES] = {
    {"tm1", 0x2000, 0x1551f1b9u, M(0)},
    {"tm2", 0x2000, 0x58636cb5u, M(0x2000)},
    {"tm3", 0x2000, 0xff4e0d83u, M(0x4000)},
    {"tm7", 0x1000, 0xd66da813u, (uint32_t)offsetof(tp_rom_store_t, sound)},
    {"tm6", 0x2000, 0xc2507f40u, G(0)},
    {"tm4", 0x2000, 0x7e437c3eu, G(0x2000)},
    {"tm5", 0x2000, 0xe8ca87b9u, G(0x4000)},
    {"timeplt.b4", 0x0020, 0x34c91839u, (uint32_t)offsetof(tp_rom_store_t, proms)},
    {"timeplt.b5", 0x0020, 0x463b2b07u, (uint32_t)offsetof(tp_rom_store_t, proms) + 0x20},
    {"timeplt.e9", 0x0100, 0x4bbb2150u, (uint32_t)offsetof(tp_rom_store_t, proms) + 0x40},
    {"timeplt.e12", 0x0100, 0xf7b7663eu, (uint32_t)offsetof(tp_rom_store_t, proms) + 0x140},
};
#undef M
#undef G

uint32_t tp_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                   0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                   0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

int tproms_wanted(uint32_t size, uint32_t crc, uint32_t found) {
    for (int i = 0; i < TP_ROM_FILES; i++)
        if (tp_rom_files[i].size == size && tp_rom_files[i].crc == crc && !(found & 1u << i)) return i;
    return -1;
}

bool tproms_size_ok(uint32_t size) {
    for (int i = 0; i < TP_ROM_FILES; i++)
        if (tp_rom_files[i].size == size) return true;
    return false;
}

void tproms_put(int i, const uint8_t *data) {
    const tp_rom_file_t *f = &tp_rom_files[i];
    uint8_t *to = f->at & TP_GFX_AT ? tp_rom_gfx + (f->at & ~TP_GFX_AT) : (uint8_t *)&tp_rom_store + f->at;
    memmove(to, data, f->size);
}

void tproms_roms(tp_roms_t *r) {
    r->maincpu = tp_rom_store.maincpu;
    r->sound = tp_rom_store.sound;
    r->tiles = tp_rom_gfx;
    r->sprites = tp_rom_gfx + 0x2000;
    r->proms = tp_rom_store.proms;
}
