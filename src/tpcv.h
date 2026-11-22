// tpcv: the cart's side of Time Pilot on the ColecoVision. Portable C (no
// pico-sdk), used by the plugin and by the Mac tests.
//
// The console runs the SDK's frame kernel (gbkernel.asm): each TV frame it
// takes a command list from the cart and runs it. This makes those lists
// from the board (tp.h) and its sound (tpsnd.h):
//
//   once:                         tpcv_init(e)
//   at each VBLANK the cart uses: tpcv_picture(e, board)   (or tpcv_canvas for a message)
//   each list:                    tpcv_sound_pcm(e, ...) or tpcv_sound_psg(e, ...)
//                                 len = tpcv_tv_frame(e, list)
//
// The picture. MAME shows the board's 256 x 224 turned (ROT90): 224 wide
// and 256 high, 32 rows of 28 tiles. The play area (rows 4 to 30, 224 x
// 216) is scaled to the TMS9918's 192 lines: 8 lines of 9 and 5 columns of
// 7, 160 x 192, in the middle of the screen (x 48 to 207, whole 8-pixel
// places, so the side margins share no place with the sky). Two lines or
// columns become one the way sdk/cvscale.h does it (a stroke one pixel
// wide survives). The board's sprites are scaled one by one (16 x 16 to 12
// x 14), so they keep their shape as they move. The scores, the lives, the
// stages, the era and the progress bar (rows 0 to 3 and 31) go to the
// margins in pieces, scaled by 3/4; the progress bar stands on end.
//
// Each era's sky is a colour of its own on the TMS9918 (tpcv.c, sky[]),
// and a sprite's colour that would match the sky takes its next nearest.
//
// A list carries the bytes that differ from what the console holds, as
// many as the frame has time for.
//
// The sound goes one of two ways (tpsnd.c chooses): samples through the
// SN76489's three tones parked at a period of 1, their attenuators summed
// as a DAC, TPCV_PCM_N a list with the console running the lists back to
// back (Donkey Kong's way); or the chip's own tones and noise, set once a
// list.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tp.h"

#define TPCV_W      256
#define TPCV_H      192
#define TPCV_BYTES  6144            // pattern bytes, and as many colour bytes
#define TPCV_LIST_MAX 3000
#define TPCV_PCM_N  100             // samples a list
#define TPCV_LIST_T 60036           // a list's length in the console's T (3,579,545 a second)
#define TPCV_CONSOLE_HZ 3579545u
// the board's main CPU cycles (3.072 MHz) in a list, << 16
#define TPCV_LIST_CYCLES_Q16 ((uint64_t)TPCV_LIST_T * TP_CLOCK * 65536u / TPCV_CONSOLE_HZ)
// the samples' rate, << 8: TPCV_PCM_N a list (5,962 a second)
#define TPCV_RATE_Q8 ((uint32_t)((uint64_t)TPCV_PCM_N * TPCV_CONSOLE_HZ * 256u / TPCV_LIST_T))

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TPCV_HOT __attribute__((section(".time_critical.tpcv")))
#else
#define TPCV_HOT
#endif

typedef struct {
    uint32_t tv_frames, pictures, pat_bytes, col_bytes, spr_bytes, runs, refreshed, pcm_frames;
    uint32_t settled;               // lists that ended with nothing left to send
    uint32_t t_max;                 // the longest list, in T
    uint32_t tiles;                 // play-area tiles drawn again
    uint32_t cells;                 // 8 x 1 places made again
    uint32_t clipped;               // samples the soft knee took in
} tpcv_stats_t;

typedef struct {
    // the console's VRAM as the lists so far leave it, and the newest picture
    uint8_t vram_pat[TPCV_BYTES], vram_col[TPCV_BYTES];
    uint8_t want_pat[TPCV_BYTES], want_col[TPCV_BYTES];
    bool names_done;
    uint16_t names_at;
    uint16_t cursor;                // where the next list's scan starts
    uint16_t refresh_at;
    bool dirty;                     // want differs from vram somewhere (or may)
    uint32_t bands;                 // the same, for each band of 8 lines (256 bytes)
    bool regs_done;                 // the sprites off (R1) sent once

    // the picture as it was made last
    bool pic_valid;
    uint8_t era;                    // the sky's colour set (1-5, 0: none of them)
    uint8_t flip;
    bool video_on;
    uint8_t vram_seen[0x400], cram_seen[0x400];   // the board's tile RAM the picture was drawn from
    uint32_t cells_prev[24];        // the 8 x 8 places the sprites were in
    uint8_t year[7];                // "A.D.1910" as the game last wrote it (its tiles, 0: none yet)
    uint8_t year_col;

    // the sound
    bool pcm;                       // the lists carry samples
    uint8_t pcm_gain;               // 0-32: the DAC comes up over the first lists
    uint8_t pcm_bytes[TPCV_PCM_N][3];  // each sample's three attenuators, in the order written
    uint8_t pcm_nb;                 // its bytes (3; the mix's 2)
    uint8_t dac_att[3];             // the attenuators as the last sample left them
    uint8_t tone_bytes[12];
    uint8_t tone_n;
    uint8_t resync;
    tpcv_stats_t stats;
} tpcv_t;

void tpcv_init(tpcv_t *e);
// the colours of the board's pens, and the tiles' and sprites' tables, from
// its PROMs and ROMs (after tp_init), before the first picture
void tpcv_prepare(void);
// room the plugin may use before the first tpcv_picture (a message's
// canvas, the card's reads); tpcv_canvas and the sound leave it alone
uint8_t *tpcv_scratch(size_t *n);

// what a picture is made from: the board's tile RAM and its sprites line
// by line, at a VBLANK
typedef struct {
    uint8_t vram[0x400], cram[0x400];
    tp_spans_t spans;
    uint8_t flip;
    bool video_on;
    uint32_t frame;                     // the VBLANK it was taken at (tp_frames; the tests)
} tpcv_board_t;
void tpcv_board(tpcv_board_t *b);       // the board as it is (at its VBLANK)
// a new picture of b
void tpcv_picture(tpcv_t *e, const tpcv_board_t *b);
// a new picture: 256 x 192 pixels, a TMS9918 colour each (1-15); for messages
void tpcv_canvas(tpcv_t *e, const uint8_t *px);

// The next list's sound. Samples: n at TPCV_RATE_Q8 in the DAC's levels
// either side of its rest, 64 a level (tpsnd.h; n = 0: none, a message: the
// console waits for the vertical blank again, and the DAC is let go).
// Tones: the SN76489's eleven bytes (tpsnd_tones), written whole in the
// list. The mix: samples through tones 0 and 1 (a DAC of two), tone 2's
// three bytes as tpsnd_mix makes them.
void tpcv_sound_pcm(tpcv_t *e, const int16_t *s, int n);
void tpcv_sound_psg(tpcv_t *e, const uint8_t psg[11]);
void tpcv_sound_mix(tpcv_t *e, const int16_t *s, int n, const uint8_t psg3[3]);

// the next list into out (TPCV_LIST_MAX bytes); returns its length
size_t tpcv_tv_frame(tpcv_t *e, uint8_t *out);

// the picture the console shows once the lists so far have run, a TMS9918
// colour a pixel (tests, previews); the picture wanted; the picture before
// the TMS9918's two colours a place (every pixel its own colour)
void tpcv_screen(const tpcv_t *e, uint8_t *px);
void tpcv_wanted(const tpcv_t *e, uint8_t *px);
void tpcv_ideal(const tpcv_t *e, uint8_t *px);
// bytes the console still needs (tests; a whole scan)
uint32_t tpcv_backlog(const tpcv_t *e);
// the DAC's level for the three tones' attenuators (tests: the lists' sound as a WAV)
float tpcv_dac_level(int att0, int att1, int att2);
// the TMS9918's colours as 0xRRGGBB (previews)
extern const uint32_t tpcv_tms_rgb[16];
// the sky's TMS9918 colour for each era (1-5)
extern const uint8_t tpcv_sky[6];
