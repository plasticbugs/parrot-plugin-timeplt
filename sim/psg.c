// psg.c -- the ColecoVision's SN76489A, from the bytes the console writes
// to it, as 16-bit samples. See sim.h.
//
// The chip counts at its clock / 16 (3,579,545 / 16 Hz on the console).
// Each tone's counter reloads from its 10-bit period and flips the tone's
// output when it runs out, so a period n sounds at clock / (32 n); a period
// of 0 counts as 1024. The noise shifts a 15-bit register (white noise taps
// bits 0 and 1, as MAME's sn76489a) on each rising edge of its own counter,
// which reloads from 16, 32 or 64, or follows tone 2. Volumes are 2 dB a
// step, 15 off. Each output is 0 or its level, as on the chip, and the mix
// goes through a DC block, as the console's own coupling capacitor does.
// The samples are box-filtered from the chip's rate.
#include "sim.h"

#include <math.h>
#include <string.h>

void psg_init(psg_t *p, int rate) {
    memset(p, 0, sizeof *p);
    for (int i = 0; i < 4; i++) p->vol[i] = 15;
    p->lfsr = 0x4000;
    p->ticks_per_sample = PSG_CLOCK / 16.0 / rate;
    for (int i = 0; i < 15; i++) p->level[i] = (float)pow(10.0, -0.1 * i);
    p->level[15] = 0;
    p->count[0] = p->count[1] = p->count[2] = p->count[3] = 1;
}

void psg_write(psg_t *p, uint8_t b) {
    if (b & 0x80) {                         // latch: a register, and its low 4 bits
        p->latch = (uint8_t)(b >> 4 & 7);
        const unsigned r = p->latch;
        if (r == 6) { p->noise = b & 7; p->lfsr = 0x4000; }
        else if (r & 1) p->vol[r >> 1] = b & 15;
        else p->period[r >> 1] = (uint16_t)((p->period[r >> 1] & 0x3F0) | (b & 15));
    } else {                                // data: the latched register's other bits
        const unsigned r = p->latch;
        if (r == 6) { p->noise = b & 7; p->lfsr = 0x4000; }
        else if (r & 1) p->vol[r >> 1] = b & 15;
        else p->period[r >> 1] = (uint16_t)((p->period[r >> 1] & 0x00F) | (b & 0x3F) << 4);
    }
}

static inline void tick(psg_t *p) {
    for (int ch = 0; ch < 3; ch++)
        if (--p->count[ch] <= 0) {
            p->count[ch] = p->period[ch] ? p->period[ch] : 0x400;
            p->out[ch] ^= 1;
        }
    if (--p->count[3] <= 0) {
        const unsigned rate = p->noise & 3;
        p->count[3] = rate == 3 ? (p->period[2] ? p->period[2] : 0x400) : 0x10 << rate;
        p->nflip ^= 1;
        if (p->nflip) {                     // a rising edge: shift
            const unsigned fb = p->noise & 4 ? (p->lfsr ^ p->lfsr >> 1) & 1 : p->lfsr & 1;
            p->lfsr = (uint16_t)(p->lfsr >> 1 | fb << 14);
        }
    }
}

void psg_render(psg_t *p, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        p->frac += p->ticks_per_sample;
        int ticks = (int)p->frac;
        p->frac -= ticks;
        float sum = 0;
        for (int k = 0; k < ticks; k++) {
            tick(p);
            sum += (p->out[0] ? p->level[p->vol[0]] : 0) + (p->out[1] ? p->level[p->vol[1]] : 0) +
                   (p->out[2] ? p->level[p->vol[2]] : 0) + (p->lfsr & 1 ? p->level[p->vol[3]] : 0);
        }
        const float x = ticks ? sum / (float)ticks : p->last_x;
        const float y = x - p->last_x + 0.9995f * p->last_y;     // the DC block
        p->last_x = x;
        p->last_y = y;
        float s = y * 7000.0f;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
}
