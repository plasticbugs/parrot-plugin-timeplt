// tms9918.c -- see tms9918.h. The table addresses and masks are the
// TMS9918A data sheet's; sprites as MAME's tms9928a.cpp draws them.
#include "tms9918.h"

#include <stdbool.h>
#include <string.h>

enum { G1, G2, TEXT, MC, MIXED };

static int mode(const uint8_t reg[8]) {
    const int m1 = reg[1] >> 4 & 1, m2 = reg[1] >> 3 & 1, m3 = reg[0] >> 1 & 1;
    if (m1 + m2 + m3 > 1) return MIXED;
    return m1 ? TEXT : m2 ? MC : m3 ? G2 : G1;
}

const char *tms_mode_name(const uint8_t reg[8]) {
    static const char *const name[] = {"graphics 1", "graphics 2", "text", "multicolor", "mixed"};
    return name[mode(reg)];
}

static void background(const uint8_t *vram, const uint8_t reg[8], int m, uint8_t back, uint8_t pic[TMS_H][TMS_W]) {
    const unsigned names = (reg[2] & 0x0Fu) << 10;
    for (int y = 0; y < TMS_H; y++) {
        uint8_t *row = pic[y];
        const int line = y & 7;
        if (m == TEXT) {                    // 40 x 24 characters 6 pixels wide, from x 6 (the data sheet's border is 6 wider than in the other modes)
            const uint8_t fg = reg[7] >> 4;
            const unsigned pg = (reg[4] & 7u) << 11;
            memset(row, back, TMS_W);
            for (int cx = 0; cx < 40; cx++) {
                const uint8_t ch = vram[names + (y >> 3) * 40 + cx], bits = vram[pg + ch * 8u + line];
                for (int b = 0; b < 6; b++) row[6 + cx * 6 + b] = bits >> (7 - b) & 1 && fg ? fg : back;
            }
            continue;
        }
        for (int cx = 0; cx < 32; cx++) {
            const uint8_t ch = vram[names + (y >> 3) * 32 + cx];
            uint8_t bits, col;
            if (m == MC) {                  // 4x4 blocks: a byte holds two, left and right
                const uint8_t b = vram[((reg[4] & 7u) << 11) + ch * 8u + ((y >> 3) & 3) * 2 + (line >> 2)];
                bits = 0xF0;
                col = b;
            } else if (m == G2) {           // a pattern and a colour byte for each 8x1 run, the screen in thirds
                const unsigned t = ((unsigned)(y >> 6) << 8 | ch) * 8u + (unsigned)line;
                bits = vram[((reg[4] & 4u) << 11) | (t & ((reg[4] & 3u) << 11 | 0x7FFu))];
                col = vram[((reg[3] & 0x80u) << 6) | (t & ((reg[3] & 0x7Fu) << 6 | 0x3Fu))];
            } else {                        // Graphics I (and the undocumented mixes, drawn as it)
                bits = vram[((reg[4] & 7u) << 11) + ch * 8u + (unsigned)line];
                col = vram[((unsigned)reg[3] << 6) + (ch >> 3)];
            }
            for (int b = 0; b < 8; b++) {
                const uint8_t c = bits >> (7 - b) & 1 ? col >> 4 : col & 15;
                row[cx * 8 + b] = c ? c : back;
            }
        }
    }
}

static uint8_t sprites(const uint8_t *vram, const uint8_t reg[8], uint8_t pic[TMS_H][TMS_W]) {
    const unsigned sat = (reg[5] & 0x7Fu) << 7, spg = (reg[6] & 7u) << 11;
    const int size = reg[1] & 2 ? 16 : 8, mag = reg[1] & 1, h = size << mag;
    uint8_t status = 0, last = 31;
    for (int y = 0; y < TMS_H; y++) {
        uint8_t hit[TMS_W];                 // a sprite's pixel is here already (collisions)
        bool shown[TMS_W];                  // a sprite's colour is here already (priority)
        memset(hit, 0, sizeof hit);
        memset(shown, 0, sizeof shown);
        int on_line = 0;
        for (int s = 0; s < 32; s++) {
            const uint8_t *a = vram + sat + 4 * s;
            int top = a[0];
            if (top == 0xD0) { last = (uint8_t)s; break; }      // the end of the table
            if (top > 0xE0) top -= 256;     // near the bottom of its 256: partly above the screen
            top++;                          // Y 255 is the first line
            if (y < top || y >= top + h) continue;
            if (++on_line > 4) {            // the fifth: not drawn, and noted once
                if (!(status & TMS_STATUS_5S)) status = (uint8_t)((status & TMS_STATUS_C) | TMS_STATUS_5S | s);
                break;
            }
            const int x0 = a[1] - (a[3] & 0x80 ? 32 : 0);
            const uint8_t colour = a[3] & 15;
            const unsigned name = size == 16 ? a[2] & 0xFCu : a[2];
            const int r = (y - top) >> mag;
            const unsigned bits = (unsigned)vram[spg + name * 8 + (unsigned)r] << 8 | (size == 16 ? vram[spg + name * 8 + 16 + (unsigned)r] : 0);
            for (int p = 0; p < size << mag; p++) {
                const int x = x0 + p;
                if (x < 0 || x >= TMS_W || !(bits >> (15 - (p >> mag)) & 1)) continue;
                if (hit[x]) status |= TMS_STATUS_C;
                hit[x] = 1;
                if (colour && !shown[x]) {  // a lower number is in front; a transparent one shows what is behind
                    shown[x] = true;
                    pic[y][x] = colour;
                }
            }
        }
    }
    if (!(status & TMS_STATUS_5S)) status = (uint8_t)(status | last);
    return status;
}

uint8_t tms_picture(const uint8_t vram[0x4000], const uint8_t reg[8], uint8_t pic[TMS_H][TMS_W]) {
    const uint8_t back = reg[7] & 15;
    if (!(reg[1] & 0x40)) {                 // blanked: the backdrop only
        memset(pic, back, TMS_H * TMS_W);
        return 0;
    }
    const int m = mode(reg);
    background(vram, reg, m, back, pic);
    return m == TEXT ? 0 : sprites(vram, reg, pic);
}
