// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles, O. Galibert
//
// tp.h -- the Time Pilot board (Konami, 1982) as MAME 0.288 emulates it
// (timeplt.cpp, timeplt_a.cpp, gen_latch.cpp, 74259.cpp, the Z80 core),
// with MAME's scheduler, so both CPUs run the cycles MAME's do. The two
// AY-3-8910s and their filters are tpay.c: this file hands them each
// register write with its time.
#ifndef TP_BOARD_H
#define TP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#define TP_CLOCK 3072000u               // the main Z80: XTAL(18'432'000) / 3 / 2
#define TP_SND_CLOCK 1789772u           // the sound Z80 and the AYs: 14'318'181 / 8
#define TP_W 256                        // the raw screen, before MAME's ROT90
#define TP_H 224                        // (lines 16-239 of 256)
#define TP_LINE_CYCLES 200              // the main Z80's cycles a line (60 Hz, 256 lines)

typedef struct {
	const uint8_t *maincpu;             // 24K: tm1, tm2, tm3
	const uint8_t *sound;               // 4K: tm7
	const uint8_t *tiles;               // 8K: tm6
	const uint8_t *sprites;             // 16K: tm4, tm5
	const uint8_t *proms;               // 0x240: timeplt.b4, b5 (palette), e9 (sprites), e12 (tiles)
} tp_roms_t;

// the ports as the game reads them (active low, as MAME's are wired).
// MAME's defaults: DSW0 0xff (1 coin 1 credit both ways), DSW1 0x4b (3
// lives, upright, bonus 10000 50000, difficulty 4, demo sounds on)
typedef struct {
	uint8_t in0;                        // 0x01 coin 1, 0x02 coin 2, 0x04 service, 0x08 start 1, 0x10 start 2
	uint8_t in1;                        // 0x01 left, 0x02 right, 0x04 up, 0x08 down, 0x10 fire
	uint8_t in2;                        // player 2's (cocktail), the same
	uint8_t dsw0, dsw1;
} tp_inputs_t;
#define TP_DSW0 0xff
#define TP_DSW1 0x4b
extern tp_inputs_t tp_in;

void tp_init(const tp_roms_t *roms);    // power on
void tp_frame(void);                    // run to the next VBLANK start

// The cart's way: run to the main CPU's cycle `until` (from power on),
// stopping at the first of MAME's timeslice ends at or past it. Returns
// the VBLANKs passed; tp_on_vblank, if set, is called at each.
uint32_t tp_run_to(uint64_t until);
uint64_t tp_time(void);                 // where the board is, in main CPU cycles
extern void (*tp_on_vblank)(void);

// RAM, as the main CPU sees it
extern uint8_t tp_colorram[0x400];      // 0xa000
extern uint8_t tp_videoram[0x400];      // 0xa400
extern uint8_t tp_ram[0x800];           // 0xa800
extern uint8_t tp_spriteram[2][0x100];  // 0xb000, 0xb400 (mirrored at 0x0b00)
extern uint8_t tp_latch;                // the LS259 (B3): bit n is Q n
extern uint32_t tp_frames;              // VBLANKs so far
#define TP_NMI_ENABLE (tp_latch & 0x01)
#define TP_FLIP       (!(tp_latch & 0x02))      // flip_screen_set(!Q1)
#define TP_VIDEO_ON   (tp_latch & 0x10)

// The sprites as the screen saw them: MAME updates the screen a line at a
// time (VIDEO_UPDATE_SCANLINE) and the game moves the clouds while the
// beam is on the screen, so a frame's sprites are segments: entry k of
// spans[] holds from line spans[k].line (16-239) to the next one's. The
// last frame's are kept at each VBLANK.
#define TP_SPANS 24
typedef struct {
	uint8_t line;
	uint8_t s0[0x30], s1[0x30];         // sprite RAM 0x10-0x3f, both banks
} tp_span_t;
typedef struct {
	int n;
	tp_span_t span[TP_SPANS];
} tp_spans_t;
extern tp_spans_t tp_spans;             // the frame that ended at the last VBLANK
extern bool tp_video_on_frame;          // the video enable as the frame was drawn (at its VBLANK)

// The picture: MAME's screen bitmap (raw, 256 x 224: lines 16-239), in
// pens: 0-127 the tiles' (color * 4 + pixel), 128-383 the sprites' (128 +
// color * 4 + pixel); tp_rgb[] is each as MAME's palette makes it.
// tp_render_line draws line L as MAME's update_partial does at that line,
// from the board as it is; tp_on_line (tests) is called at each line's
// update with the line.
void tp_render_line(uint16_t *row, int line);
extern void (*tp_on_line)(int line);
extern uint32_t tp_rgb[384];            // 0xRRGGBB
// the graphics, decoded: a tile's row (raw orientation), 8 pens of 2 bits,
// pixel x at bits 2x; a sprite's row, 16 pens the same way
extern uint16_t tp_tile_rows[512][8];
extern uint32_t tp_sprite_rows[256][16];
extern const uint8_t *tp_proms;

// sound: each AY write, with its time in the AYs' own ticks (the sound
// CPU's clock / 8, MAME's stream rate), and the filters' settings
// (tpay.c). The board calls these as MAME's devices get them.
void tp_ay_write(uint64_t tick, int chip, int reg, uint8_t value);
void tp_ay_filter(uint64_t tick, uint16_t bits);   // the filter control's address bits 0-11
void tp_ay_mute(uint64_t tick, int on);            // the LS259's Q3 (the amplifier's mute)
uint64_t tp_ay_now(void);                          // the AYs' tick now (from the sound CPU's time)

// for the tests
uint64_t tp_cycles(void);               // the main CPU's cycles so far
uint64_t tp_snd_cycles(void);           // the sound CPU's
uint8_t tp_peek(uint16_t a);            // as the main CPU reads, without side effects
extern uint8_t tp_snd_ram[0x400];       // the sound CPU's RAM, 0x3000
extern uint8_t tp_soundlatch;
extern uint8_t tp_ay_regs[2][16];       // as the AYs hold them

#endif
