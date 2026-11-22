// tms9918.h -- the TMS9918A's picture, from its 16K of video RAM and its 8
// registers, as the console shows it.
//
// Every mode a plugin can set: Graphics I, Graphics II, Text and
// Multicolor; sprites, 8x8 and 16x16, magnified or not, with the early
// clock bit; 4 sprites on a line at most, a fifth sets the status
// register's 5S flag; the backdrop (register 7) wherever nothing is drawn,
// and over the whole screen when the display is blanked.
//
// The models (host.c) take their PNGs from it and the simulator (sim/) its
// window, so both show what MAME's ColecoVision shows (test/run.sh compares
// them pixel for pixel). Plain C: sdk/firmware/revc/midihello/vdp.c has the
// SDK's own Graphics II bitmap, which this extends; the SDK's copy isn't
// edited here (sdk/README.md).
#pragma once
#include <stdint.h>

#define TMS_W 256
#define TMS_H 192

// the status register, as the frame leaves it
#define TMS_STATUS_5S   0x40        // a fifth sprite on a line (its number in bits 0-4)
#define TMS_STATUS_C    0x20        // two sprites' pixels met

// The picture: a colour index a pixel, 0-15 as vdp_rgb has them (0 only if
// the backdrop is 0). Returns the status register's bits 0-6.
uint8_t tms_picture(const uint8_t vram[0x4000], const uint8_t reg[8], uint8_t pic[TMS_H][TMS_W]);

// The mode's name for the registers: "graphics 1", "graphics 2", "text",
// "multicolor", or "mixed" (a combination the chip doesn't document).
const char *tms_mode_name(const uint8_t reg[8]);
