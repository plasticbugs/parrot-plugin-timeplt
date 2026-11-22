// tpcv.c -- see tpcv.h. The lists, the DAC and the two-colour picker are
// Donkey Kong's (Parrot Cart's dkong/dkcv.c, from Pac-Man's and Q*bert's);
// the picture is new: the play area at 5/7 x 8/9, the sprites line by
// line, the margins' pieces.
#include "tpcv.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cvscale.h"
#include "tpdac3.h"

// ---- lists (from gbkernel.asm's costs) -----------------------------------------------
#define FRAME_T    (262 * 228)
#define VBLANK_T   (70 * 228)
#define C_START    480
#define C_RUN_VB   106
#define C_BYTE_VB  23
#define C_RUN_ACT  106
#define C_BYTE_ACT 31
#define C_PSG      75
#define C_PSG_BYTE 52
#define C_REG      114
#define C_END_VB   54
#define C_END      52
#define VB_LIMIT   (VBLANK_T - 600)
#define LIST_LIMIT (FRAME_T - 1200)

// The console goes from one list straight into the next, so a list's
// length is the sample clock
#define C_FRAME    357
#define PCM_L      TPCV_LIST_T
#define PCM_FIRST  (C_FRAME + C_END_VB)
#define C_SAMPLE   (C_PSG + 3 * C_PSG_BYTE)
#define PCM_STEP   (((PCM_L - PCM_FIRST - C_SAMPLE - C_END) << 8) / (TPCV_PCM_N - 1))

#define PATTERNS 0x0000
#define COLORS   0x2000
#define NAMES    0x1800
#define BLACK    1

typedef struct {
    uint8_t *b;
    size_t n;
    uint32_t t;
    bool vblank;
} list_t;

static void l_run_head(list_t *l, uint16_t addr, int n) {
    l->t += l->vblank ? C_RUN_VB + n * C_BYTE_VB : C_RUN_ACT + n * C_BYTE_ACT;
    l->b[l->n++] = 1;
    l->b[l->n++] = (uint8_t)addr;
    l->b[l->n++] = (uint8_t)((addr >> 8) & 0x3F) | 0x40;
    l->b[l->n++] = (uint8_t)n;
}

static void l_psg(list_t *l, const uint8_t *d, int n) {
    l->t += C_PSG + C_PSG_BYTE * n;
    l->b[l->n++] = 2;
    l->b[l->n++] = (uint8_t)n;
    memcpy(l->b + l->n, d, n);
    l->n += n;
}

static void l_end_vblank(list_t *l) {
    if (!l->vblank) return;
    l->vblank = false;
    l->t += C_END_VB;
    l->b[l->n++] = 0;
}

static int room(const list_t *l) {
    int t = l->vblank ? ((int)VB_LIMIT - (int)l->t - C_END_VB - C_RUN_VB) / C_BYTE_VB
                      : ((int)LIST_LIMIT - (int)l->t - C_END - C_RUN_ACT) / C_BYTE_ACT;
    return t > 255 ? 255 : t;
}

// ---- colours ------------------------------------------------------------------------
const uint32_t tpcv_tms_rgb[16] = {0x000000, 0x000000, 0x21C842, 0x5EDC78, 0x5455ED, 0x7D76FC, 0xD4524D, 0x42EBF5,
                                   0xFC5554, 0xFF7978, 0xD4C154, 0xE6CE80, 0x21B03B, 0xC95BBA, 0xCCCCCC, 0xFFFFFF};

// Each era's sky (the tiles' colour sets 1 to 5, pen 0) as a TMS9918
// colour of its own, so the eras stay apart: 1910's navy dark blue, 1940's
// teal light blue, 1970's green dark green, 1982's purple magenta, 2001's
// black black. (The nearest colours would make 1910, 1940 and 2001 all
// black or dark blue.)
const uint8_t tpcv_sky[6] = {0, 4, 5, 12, 13, 1};

static float tms_lab[16][3];
static uint16_t nearer[16][16];         // [c][a] bit b: c is nearer a than b
#define NEARER(c, a, b) ((nearer[c][a] >> (b)) & 1)
static bool colors_ready;

static void oklab(uint32_t rgb, float *out) {
    float c[3];
    for (int i = 0; i < 3; i++) {
        float v = (float)((rgb >> (16 - 8 * i)) & 255) / 255.0f;
        c[i] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    float l = cbrtf(0.4122214708f * c[0] + 0.5363325363f * c[1] + 0.0514459929f * c[2]);
    float m = cbrtf(0.2119034982f * c[0] + 0.6806995451f * c[1] + 0.1073969566f * c[2]);
    float s = cbrtf(0.0883024619f * c[0] + 0.2817188376f * c[1] + 0.6299787005f * c[2]);
    out[0] = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out[1] = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out[2] = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
}

static float d2(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z;
}

static void colors_init(void) {
    for (int i = 1; i < 16; i++) oklab(tpcv_tms_rgb[i], tms_lab[i]);
    for (int c = 1; c < 16; c++)
        for (int a = 1; a < 16; a++)
            for (int b = 1; b < 16; b++)
                if (d2(tms_lab[c], tms_lab[a]) <= d2(tms_lab[c], tms_lab[b])) nearer[c][a] |= (uint16_t)(1u << b);
    colors_ready = true;
}

// the nearest TMS9918 colour to rgb in OKLab, but `not`; a grey (little
// chroma) only to black, grey or white, so the clouds stay grey
static uint8_t nearest(uint32_t rgb, int not) {
    float lab[3];
    oklab(rgb, lab);
    bool grey = lab[1] * lab[1] + lab[2] * lab[2] < 0.03f * 0.03f;
    uint8_t best = 1;
    float bd2 = 1e9f;
    for (int t = 1; t < 16; t++) {
        if (t == not || (grey && t != 1 && t != 14 && t != 15)) continue;
        float d = d2(lab, tms_lab[t]);
        if (d < bd2) { bd2 = d; best = (uint8_t)t; }
    }
    return best;
}

// The board's pens (tp_rgb: the PROMs through MAME's resistors) as TMS9918
// colours: a tile's, and a sprite's for each era's sky (its own colour
// left out of theirs)
static uint8_t tile_tms[128];
static uint8_t spr_tms[6][256];
static bool pens_ready;

static void pens_init(void) {
    if (!colors_ready) colors_init();
    for (int i = 0; i < 128; i++) {
        int set = i >> 2;
        tile_tms[i] = (set >= 1 && set <= 5 && !(i & 3)) ? tpcv_sky[set] : nearest(tp_rgb[i], 0);
    }
    for (int era = 0; era < 6; era++)
        for (int i = 0; i < 256; i++) spr_tms[era][i] = (i & 3) ? nearest(tp_rgb[128 + i], era ? tpcv_sky[era] : 0) : 0;
    pens_ready = true;
}

// ---- the picture's state -------------------------------------------------------------
// The play area: MAME's tiles turned (224 x 216, pens: colour * 4 + pixel,
// 0x80 for a tile in front of the sprites), scaled (160 x 192, the same
// pens), and the whole screen in TMS9918 colours (the margins' pieces and
// the play area's tiles; the sprites go over it place by place). Kept from
// one picture to the next, and the plugin's room before the game.
#define PF_X   48                       // the play area on the TMS9918's screen
#define PF_W   160
#define PF_H   192
#define DROW0  4                        // its first row of MAME's (turned) tiles
#define DROWS  27
#define DH     (DROWS * 8)              // 216 lines
#define DW     224

typedef struct {
    uint32_t key;                       // what it was made from (0: nothing)
    uint32_t stamp;                     // the last picture that used it
    uint8_t px[14][12];                 // the sprite's pens (colour * 4 + pixel), 0 where it has none
} simg_t;

typedef struct {
    int16_t x, y;                       // its 12 x 14's top left on the TMS9918's screen
    int16_t x0, y0, x1, y1;             // that, clipped
    const simg_t *img;
} vis_t;

// The picture's state, in the plugin's room before the game (tpcv_scratch).
// bgpen is the play area scaled, as TMS9918 colours (bit 7: a tile in
// front of the sprites); margin has the
// colours of the margins, x 0-47 then 208-255 (MX). The sprites' images
// are kept in scache (the game has 24 sprites).
#define SCACHE 64
#define MW     (TPCV_W - PF_W)
#define MX(x)  ((x) < PF_X ? (x) : (x) - PF_W)
static union {
    struct {
        uint8_t bgpen[PF_H][PF_W];
        uint8_t margin[TPCV_H][MW];
        simg_t scache[SCACHE];
        uint32_t ov_hash[24][32];
    } p;
    uint8_t room[64 * 1024];
    uint64_t align;
} pic_u;
#define bgpen  pic_u.p.bgpen
#define margin pic_u.p.margin
#define scache pic_u.p.scache
#define ov_hash pic_u.p.ov_hash
static bool pic_clear;
static bool play_black;                 // the board's picture off: the play area black

static vis_t vis[64];
static int nvis;

uint8_t *tpcv_scratch(size_t *n) {
    *n = sizeof pic_u.room;
    pic_clear = false;
    return pic_u.room;
}

// ---- the 8 x 8 places to make again ----------------------------------------------------
static uint32_t cells[24];              // a bit a column, a word a band

static void mark(int x0, int y0, int x1, int y1) {     // pixels x0..x1 - 1, y0..y1 - 1
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > TPCV_W) x1 = TPCV_W;
    if (y1 > TPCV_H) y1 = TPCV_H;
    if (x0 >= x1 || y0 >= y1) return;
    uint32_t m = (x1 + 7) / 8 >= 32 ? ~0u : (1u << ((x1 + 7) / 8)) - 1;
    m &= ~((1u << (x0 / 8)) - 1);
    for (int b = y0 / 8; b <= (y1 - 1) / 8; b++) cells[b] |= m;
}

// ---- MAME's tiles, turned -----------------------------------------------------------
static const tpcv_board_t *bd;          // the board the picture is made from

// the tile RAM index of the picture's tile at column dc (0-27), row dr
// (0-31): MAME's ROT90, the tilemap flipped as flip_screen has it
static inline int tile_at(int flip, int dc, int dr) { return flip ? (2 + dc) * 32 + (31 - dr) : (29 - dc) * 32 + dr; }

// a tile's pens as the picture has it: out[dy][dx]
static void tile_pens(int dc, int dr, uint8_t out[8][8]) {
    int i = tile_at(bd->flip, dc, dr);
    uint8_t attr = bd->cram[i];
    int code = bd->vram[i] + 8 * (attr & 0x20);
    uint8_t base = (uint8_t)((attr & 0x1f) * 4), front = (attr & 0x10) ? 0x80 : 0;
    bool fx = attr & 0x40, fy = attr & 0x80;
    if (bd->flip) fx = !fx, fy = !fy;
    for (int dy = 0; dy < 8; dy++) {
        int u = fx ? 7 - dy : dy;
        for (int dx = 0; dx < 8; dx++) {
            int v = fy ? dx : 7 - dx;
            out[dy][dx] = (uint8_t)((base + ((tp_tile_rows[code][v] >> (2 * u)) & 3)) | front);
        }
    }
}

// ---- the play area's scaling: 5 columns of 7, 8 lines of 9 ------------------------
// TMS9918 column c (0-159) is MAME's column scol[c] (and the one after it,
// for two made one); line r (0-191) is the play area's line srow[r]
static uint8_t scol[PF_W], spair_c[PF_W], srow[PF_H], spair_r[PF_H];
static uint8_t col_of[DW], row_of[DH];  // where each of MAME's columns and lines goes
static bool maps_ready;

static void maps_init(void) {
    static const uint8_t c7[5] = {0, 1, 3, 4, 6}, p7[5] = {0, 1, 0, 1, 0};
    static const uint8_t r9[8] = {0, 1, 2, 3, 4, 6, 7, 8}, p9[8] = {0, 0, 0, 0, 1, 0, 0, 0};
    for (int c = 0; c < PF_W; c++) {
        scol[c] = (uint8_t)(7 * (c / 5) + c7[c % 5]);
        spair_c[c] = p7[c % 5];
        col_of[scol[c]] = (uint8_t)c;
        if (spair_c[c]) col_of[scol[c] + 1] = (uint8_t)c;
    }
    for (int r = 0; r < PF_H; r++) {
        srow[r] = (uint8_t)(9 * (r / 8) + r9[r % 8]);
        spair_r[r] = p9[r % 8];
        row_of[srow[r]] = (uint8_t)r;
        if (spair_r[r]) row_of[srow[r] + 1] = (uint8_t)r;
    }
    maps_ready = true;
}

// MAME's lines of the play area, as pens: a ring of 4, as a TMS9918 line
// needs its line of MAME's and, for two made one, the one after and those
// either side. Each is drawn from the board's tile RAM, the columns a
// rescale needs, when first needed.
static uint8_t ring[4][DW];
static int ring_y[4];

static void play_line(int y, int x0, int x1) {
    uint8_t *o = ring[y & 3];
    int dr = DROW0 + (y >> 3), dy = y & 7;
    for (int dc = x0 >> 3; dc <= x1 >> 3; dc++) {
        int i = tile_at(bd->flip, dc, dr);
        uint8_t attr = bd->cram[i];
        const uint16_t *rows = tp_tile_rows[bd->vram[i] + 8 * (attr & 0x20)];
        uint8_t base = (uint8_t)((attr & 0x1f) * 4 | ((attr & 0x10) ? 0x80 : 0));   // (as tile_pens)
        bool fx = attr & 0x40, fy = attr & 0x80;
        if (bd->flip) fx = !fx, fy = !fy;
        int sh = 2 * (fx ? 7 - dy : dy);
        uint8_t *q = o + dc * 8;
        if (fy)
            for (int dx = 0; dx < 8; dx++) q[dx] = (uint8_t)(base + ((rows[dx] >> sh) & 3));
        else
            for (int dx = 0; dx < 8; dx++) q[dx] = (uint8_t)(base + ((rows[7 - dx] >> sh) & 3));
    }
    ring_y[y & 3] = y;
}

// a line of the play area's pens at TMS9918 column c
static inline uint8_t hpick(const uint8_t *row, int c) {
    int x = scol[c];
    if (!spair_c[c]) return row[x];
    return cvscale_pick(row[x], row[x + 1], row[x - 1], row[x + 2]);   // (a pair is never at an edge)
}

static inline uint8_t scaled_pen(int c, int r) {
    int y = srow[r];
    if (!spair_r[r]) return hpick(ring[y & 3], c);
    return cvscale_pick(hpick(ring[y & 3], c), hpick(ring[(y + 1) & 3], c), hpick(ring[(y - 1) & 3], c),
                        hpick(ring[(y + 2) & 3], c));
}

// the play area's TMS9918 columns c0..c1 and lines r0..r1 scaled again,
// from the board's tiles now
static void rescale(int c0, int r0, int c1, int r1) {
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > PF_W - 1) c1 = PF_W - 1;
    if (r1 > PF_H - 1) r1 = PF_H - 1;
    int x0 = scol[c0] - 1, x1 = scol[c1] + 2;   // (a pair's columns, and those either side)
    if (x0 < 0) x0 = 0;
    if (x1 > DW - 1) x1 = DW - 1;
    for (int k = 0; k < 4; k++) ring_y[k] = -1;
    for (int r = r0; r <= r1; r++) {
        int y = srow[r], ya = spair_r[r] ? y - 1 : y, yb = spair_r[r] ? y + 2 : y;   // (a pair is never at an edge)
        for (int yy = ya; yy <= yb; yy++)
            if (ring_y[yy & 3] != yy) play_line(yy, x0, x1);
        for (int c = c0; c <= c1; c++) {
            uint8_t p = scaled_pen(c, r);
            bgpen[r][c] = (uint8_t)(tile_tms[p & 0x7f] | (p & 0x80));
        }
    }
    mark(PF_X + c0, r0, PF_X + c1 + 1, r1 + 1);
}

// ---- the margins' pieces -------------------------------------------------------------
// Rows of MAME's tiles (turned) put in the margins: x 0-47 on the left
// and 208-255 on the right, at the arcade's size, so every letter keeps
// its shape (at 3/4 the T's bar was the I's, and "CREDIT" read "CREDII").
// Rows 0 and 1 hold the labels and the scores, rows 2 and 3 the lives and
// the stage flags, row 31 the progress bar and the credits. The left
// margin has player 1's, the high score, the lives and the credits; the
// right one player 2's, the flags, the year and the progress bar, on end
// (112 pixels tall, its first column at the bottom). The year ("A.D." and
// "1910", two lines) is the game's, as it writes it at each stage's start
// (PIECE_YEAR, its tiles from r0 on).
//
// A score is 7 tiles, 56 pixels. Below a million the first is blank and
// the other 6 fill the margin's 48, a pixel to the left (the space before
// the second tile's digit goes after the last, off the sky). From a
// million on, each tile's columns 4 and 5 are made one (cvscale's pick),
// and the first tile's 0 and 1 too (0 is the space before a digit): 7 x 7
// - 1 = 48. Of the pairs to make one, only 4 and 5 (and 6 and 7) keep
// every stroke and gap of all ten digits, along the lines and down the
// columns (test/tp/hudcheck.c).
enum { HUD_TEXT, HUD_SCORE, HUD_BAR };
typedef struct { uint8_t c0, r0, nc, nr, x, y, kind; } piece_t;
#define PIECE_YEAR 0xff
static const piece_t pieces[] = {
    {2, 0, 4, 1, 8, 2, HUD_TEXT},               // "1-UP"
    {0, 1, 7, 1, 0, 11, HUD_SCORE},             // its score
    {10, 0, 3, 1, 12, 26, HUD_TEXT},            // "HI-"
    {13, 0, 5, 1, 4, 35, HUD_TEXT},             // "SCORE"
    {10, 1, 7, 1, 0, 44, HUD_SCORE},            // the high score
    {1, 2, 6, 2, 0, 60, HUD_TEXT},              // the lives
    {7, 2, 6, 2, 0, 78, HUD_TEXT},              // (more of them)
    {1, 31, 14, 1, 228, 74, HUD_BAR},           // the progress bar, on end
    {22, 0, 4, 1, 216, 2, HUD_TEXT},            // "2-UP"
    {20, 1, 7, 1, 208, 11, HUD_SCORE},          // its score
    {21, 2, 6, 2, 208, 26, HUD_TEXT},           // the stage flags
    {PIECE_YEAR, 0, 3, 1, 220, 50, HUD_TEXT},   // "A.D."
    {PIECE_YEAR, 3, 4, 1, 216, 59, HUD_TEXT},   // "1910"
    {19, 31, 6, 1, 0, 168, HUD_TEXT},           // "CREDIT"
    {26, 31, 2, 1, 16, 178, HUD_TEXT},          // the credits
};
#define NPIECES (int)(sizeof pieces / sizeof pieces[0])
_Static_assert(NPIECES <= 16, "piece_dirty and tile_piece have a bit a piece");
static uint16_t piece_dirty;

// the pieces each of MAME's (turned) tiles is in, a bit each
static uint16_t tile_piece[32][28];

static void layout_init(void) {
    memset(tile_piece, 0, sizeof tile_piece);
    for (int p = 0; p < NPIECES; p++) {
        if (pieces[p].c0 == PIECE_YEAR) continue;
        for (int r = pieces[p].r0; r < pieces[p].r0 + pieces[p].nr; r++)
            for (int c = pieces[p].c0; c < pieces[p].c0 + pieces[p].nc; c++) tile_piece[r][c] |= (uint16_t)(1u << p);
    }
}

#ifdef TPCV_HUD_CHECK                   // test/tp/hudcheck.c: each piece as drawn, before its colours
static bool hud_score_squeezed;         // (the test's: every score the million-and-over way)
static void hud_check(int p, const uint8_t *src, int w, int h, const uint8_t *out, int ow, int oh, bool squeezed);
#else
#define hud_score_squeezed false
#endif

static void draw_piece(const tpcv_t *e, int p) {
    const piece_t *pc = &pieces[p];
    static uint8_t src[16 * 112], out[16 * 112];  // (the largest: 14 x 1 tiles, or 6 x 2)
    int w = pc->nc * 8, h = pc->nr * 8;
    bool blank0 = true;                 // the first tile has nothing in it
    bool space1 = true;                 // nor the second's column 0 (a digit's space)
    for (int r = 0; r < pc->nr; r++)
        for (int c = 0; c < pc->nc; c++) {
            uint8_t px[8][8];
            if (pc->c0 == PIECE_YEAR) {
                if (!e->year[0]) memset(px, 0, sizeof px);
                else {                  // the year's tile as the game wrote it
                    int code = e->year[pc->r0 + c];
                    for (int dy = 0; dy < 8; dy++)
                        for (int dx = 0; dx < 8; dx++)
                            px[dy][dx] = (uint8_t)(e->year_col * 4 + ((tp_tile_rows[code][7 - dx] >> (2 * dy)) & 3));
                }
            } else {
                tile_pens(pc->c0 + c, pc->r0 + r, px);
            }
            for (int dy = 0; dy < 8; dy++)
                for (int dx = 0; dx < 8; dx++) {
                    uint8_t v = px[dy][dx] & 0x7f;
                    if (c == 0 && (v & 3)) blank0 = false;
                    if (c == 1 && dx == 0 && (v & 3)) space1 = false;
                    src[(r * 8 + dy) * w + c * 8 + dx] = v;
                }
        }
    int ow = w, oh = h;
    bool squeezed = false;
    if (pc->kind == HUD_BAR) {          // on end: its first column at the bottom
        ow = h;
        oh = w;
        for (int k = 0; k < oh; k++)
            for (int x = 0; x < ow; x++) out[k * ow + x] = src[x * w + (w - 1 - k)];
    } else if (pc->kind == HUD_SCORE && (!blank0 || hud_score_squeezed)) {
        squeezed = true;                // 7 tiles in 48
        ow = 48;
        for (int y = 0; y < h; y++) {
            const uint8_t *q = &src[y * w];
            uint8_t *o = &out[y * ow];
            for (int t = 0; t < 7; t++, q += 8) {
                if (t) {
                    *o++ = q[0];
                    *o++ = q[1];
                } else {
                    *o++ = q[1] & 3 ? q[1] : q[0];     // (0 is a digit's space: what's lit)
                }
                *o++ = q[2];
                *o++ = q[3];
                *o++ = cvscale_pick(q[4], q[5], q[3], q[6]);
                *o++ = q[6];
                *o++ = q[7];
            }
        }
    } else if (pc->kind == HUD_SCORE) { // its first tile (blank) left out, and the second's space moved to
        ow = w - 8;                     // the end, so the last digit doesn't touch the sky
        const int skip = space1 ? 9 : 8;
        for (int y = 0; y < h; y++) {
            memcpy(&out[y * ow], &src[y * w + skip], (size_t)(w - skip));
            if (space1) out[y * ow + ow - 1] = src[y * w + 8];
        }
    } else {                            // as it is
        for (int y = 0; y < h; y++) memcpy(&out[y * ow], &src[y * w], (size_t)ow);
    }
#ifdef TPCV_HUD_CHECK
    hud_check(p, src, w, h, out, ow, oh, squeezed);
#endif
    (void)squeezed;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) margin[pc->y + y][MX(pc->x) + x] = tile_tms[out[y * ow + x]];
    mark(pc->x, pc->y, pc->x + ow, pc->y + oh);
}

// ---- the sprites -----------------------------------------------------------------------
// A sprite of the board's, turned and scaled by itself: 16 columns to 12
// (3 of 4, cvscale's way), 16 lines to 14 (lines 4 and 5, 10 and 11
// made one), kept while its code, colour and flips stay the same
static uint32_t pic_stamp;              // the pictures made, for the cache's entries in use
static const simg_t *sprite_image(int code, int color, bool flipx, bool flipy) {
    uint32_t key = 1u << 16 | (uint32_t)(flipy ? 1 : 0) << 15 | (uint32_t)(flipx ? 1 : 0) << 14 | (uint32_t)color << 8 | (uint32_t)code;
    // a slot near its hash: its own, else one no sprite of this picture uses
    unsigned hs = (unsigned)(code ^ (color * 37) ^ (flipx ? 0x55 : 0) ^ (flipy ? 0x2a : 0));
    simg_t *s = NULL;
    for (unsigned k = 0; k < SCACHE; k++) {
        simg_t *t = &scache[(hs + k) & (SCACHE - 1)];
        if (t->key == key) { t->stamp = pic_stamp; return t; }
        if (!s && t->stamp != pic_stamp) s = t;
        if (!t->key || (s && k >= 8)) break;
    }
    s->stamp = pic_stamp;
    // turned: picture column u (MAME's X, 0-15) is the sprite's line 15 - u,
    // picture line v its pixel v
    uint8_t img[16][16];
    for (int u = 0; u < 16; u++) {
        int row = 15 - u;
        uint32_t bits = tp_sprite_rows[code][flipy ? 15 - row : row];
        for (int v = 0; v < 16; v++) {
            int col = flipx ? 15 - v : v;
            int pen = (bits >> (2 * col)) & 3;
            img[v][u] = pen ? (uint8_t)(color * 4 + pen) : 0;
        }
    }
    uint8_t h[16][12];                  // the columns first
    for (int v = 0; v < 16; v++)
        for (int g = 0; g < 4; g++) {
            const uint8_t *p = &img[v][4 * g];
            h[v][3 * g] = p[0];
            h[v][3 * g + 1] = cvscale_pick(p[1], p[2], p[0], p[3]);
            h[v][3 * g + 2] = p[3];
        }
    static const int8_t rows[14] = {0, 1, 2, 3, -4, 6, 7, 8, 9, -10, 12, 13, 14, 15};
    for (int r = 0; r < 14; r++) {
        int y = rows[r];
        if (y >= 0) { memcpy(s->px[r], h[y], 12); continue; }
        y = -y;
        for (int x = 0; x < 12; x++) s->px[r][x] = cvscale_pick(h[y][x], h[y + 1][x], h[y - 1][x], h[y + 2][x]);
    }
    s->key = key;
    return s;
}

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// where MAME's column X (0-224, a boundary) is on the TMS9918's screen
static inline int x_bound(int X) { return PF_X + (X * 5 + 3) / 7; }

// The board's sprites as the picture's, from its sprite RAM line by line:
// each entry's runs of lines that held the same sprite, where it is,
// scaled, clipped to its lines and the play area. In the board's order:
// 0x3e first, 0x10 last (in front).
static void find_sprites(void) {
    nvis = 0;
    const tp_spans_t *sp = &bd->spans;
    int n = sp->n;
    if (!n) return;
    for (int j = 0x2e; j >= 0; j -= 2) {
        int k = 0;
        while (k < n) {
            const tp_span_t *a = &sp->span[k];
            int k2 = k + 1;
            while (k2 < n && sp->span[k2].s0[j] == a->s0[j] && sp->span[k2].s0[j + 1] == a->s0[j + 1] &&
                   sp->span[k2].s1[j] == a->s1[j] && sp->span[k2].s1[j + 1] == a->s1[j + 1])
                k2++;
            int L0 = k ? a->line : 0, L1 = k2 < n ? sp->span[k2].line : 256;
            k = k2;
            int sx = a->s0[j], code = a->s0[j + 1], sy = 241 - a->s1[j + 1], attr = a->s1[j];
            int lo = sy, hi = sy + 16;                  // its lines on the screen, in this run
            if (lo < L0) lo = L0;
            if (lo < 16) lo = 16;
            if (hi > L1) hi = L1;
            if (hi > 240) hi = 240;
            if (lo >= hi) continue;
            if (sx + 16 <= DROW0 * 8 || sx >= (DROW0 + DROWS) * 8) continue;
            if (nvis == (int)(sizeof vis / sizeof vis[0])) return;
            vis_t *v = &vis[nvis];
            int X0 = 224 - sy, Y0 = sx;                 // MAME's (turned) picture: its top left
            v->x = (int16_t)(PF_X + fdiv((X0 + 8) * 10 + 7, 14) - 6);
            v->y = (int16_t)(fdiv((Y0 + 8 - DROW0 * 8) * 16 + 9, 18) - 7);
            // clipped to its lines (if they cut it) and to the play area
            int x0 = v->x, x1 = v->x + 12;
            if (lo > sy || hi < sy + 16) {
                int b0 = x_bound(240 - hi), b1 = x_bound(240 - lo);
                if (x0 < b0) x0 = b0;
                if (x1 > b1) x1 = b1;
            }
            if (x0 < PF_X) x0 = PF_X;
            if (x1 > PF_X + PF_W) x1 = PF_X + PF_W;
            int y0 = v->y < 0 ? 0 : v->y, y1 = v->y + 14 > PF_H ? PF_H : v->y + 14;
            if (x0 >= x1 || y0 >= y1) continue;
            v->x0 = (int16_t)x0, v->x1 = (int16_t)x1, v->y0 = (int16_t)y0, v->y1 = (int16_t)y1;
            v->img = sprite_image(code, attr & 0x3f, !(attr & 0x40), (attr & 0x80) != 0);
            nvis++;
        }
    }
}

// ---- the two colours of 8 pixels -------------------------------------------------------
// 8 pixels (TMS9918 colours) as a pattern byte and a colour byte, the
// colour byte kept as the console has it where it will do (dkcv.c's span,
// without its hardware sprites)
static inline __attribute__((always_inline)) void span(tpcv_t *e, unsigned a, const uint8_t *c) {
    uint8_t vc = e->vram_col[a], vp = e->vram_pat[a];
    uint8_t c0 = c[0];
    if (c[1] == c0 && c[2] == c0 && c[3] == c0 && c[4] == c0 && c[5] == c0 && c[6] == c0 && c[7] == c0) {
        uint8_t vf = vc >> 4, vb = vc & 15;
        if (vf == c0 || vb == c0) {
            e->want_col[a] = vc;
            e->want_pat[a] = vf == vb ? vp : vf == c0 ? 0xFF : 0x00;
        } else {
            e->want_col[a] = (uint8_t)(c0 << 4 | c0);
            e->want_pat[a] = vp;
        }
        return;
    }
    {                                   // two colours
        uint8_t d = 0;
        unsigned m0 = 0;
        int i = 0;
        for (; i < 8; i++) {
            uint8_t x = c[i];
            if (x == c0) m0 |= 0x80u >> i;
            else if (d && x != d) break;
            else d = x;
        }
        if (i == 8) {
            static const uint8_t pop4[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
            int n0 = pop4[m0 & 15] + pop4[m0 >> 4];
            uint8_t a1 = n0 > 4 ? c0 : n0 < 4 ? d : (c0 < d ? c0 : d), a2 = a1 == c0 ? d : c0;
            uint8_t vf = vc >> 4, vb = vc & 15, fg, bg;
            if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
            else { fg = a2; bg = a1; }
            e->want_pat[a] = (uint8_t)(fg == c0 ? m0 : ~m0 & 0xFF);
            e->want_col[a] = (uint8_t)(fg << 4 | bg);
            return;
        }
    }
    uint8_t cnt[16] = {0};
    for (int i = 0; i < 8; i++) cnt[c[i]]++;
    // the two most used (ties: the lower colour)
    uint8_t a1 = 0, a2 = 0;
    for (int k = 1; k < 16; k++)
        if (cnt[k] > cnt[a1] || (!a1 && cnt[k])) a1 = (uint8_t)k;
    for (int k = 1; k < 16; k++)
        if (k != a1 && cnt[k] && (!a2 || cnt[k] > cnt[a2])) a2 = (uint8_t)k;
    uint8_t fg, bg, vf = vc >> 4, vb = vc & 15;
    if (vf == a1 && vb == a2) { fg = a1; bg = a2; }
    else if (vf == a2 && vb == a1) { fg = a2; bg = a1; }
    else { fg = a2; bg = a1; }
    uint8_t pat = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t x = c[i];
        bool on = x == fg || (x != bg && NEARER(x, fg, bg));
        pat = (uint8_t)(pat << 1 | on);
    }
    e->want_pat[a] = pat;
    e->want_col[a] = (uint8_t)(fg << 4 | bg);
}

// ---- making the places -------------------------------------------------------------------
static uint8_t era_now;                 // the era the sprites' colours are for

// the sprites over a place (8 x 8 at cell cx, band b), as TMS9918 colours
// (0: none), in o; false if none is there
TPCV_HOT static bool overlay(int b, int cx, uint8_t o[8][8]) {
    int px0 = cx * 8, py0 = b * 8;
    bool any = false;
    for (int k = 0; k < nvis; k++) {
        const vis_t *v = &vis[k];
        if (v->x0 >= px0 + 8 || v->x1 <= px0 || v->y0 >= py0 + 8 || v->y1 <= py0) continue;
        if (!any) memset(o, 0, 64), any = true;
        int x0 = v->x0 > px0 ? v->x0 : px0, x1 = v->x1 < px0 + 8 ? v->x1 : px0 + 8;
        int y0 = v->y0 > py0 ? v->y0 : py0, y1 = v->y1 < py0 + 8 ? v->y1 : py0 + 8;
        for (int y = y0; y < y1; y++) {
            const uint8_t *src = v->img->px[y - v->y];
            for (int x = x0; x < x1; x++) {
                uint8_t p = src[x - v->x];
                if (p) o[y - py0][x - px0] = spr_tms[era_now][p];
            }
        }
    }
    return any;
}

TPCV_HOT static void make_cell(tpcv_t *e, int b, int cx, const uint8_t (*o)[8]) {
    uint8_t c[8];
    e->bands |= 1u << b;
    bool play = cx >= PF_X / 8 && cx < (PF_X + PF_W) / 8;
    for (int k = 0; k < 8; k++) {
        int y = b * 8 + k;
        if (!play) {
            memcpy(c, &margin[y][MX(cx * 8)], 8);
            if (o)
                for (int i = 0; i < 8; i++)
                    if (o[k][i]) c[i] = o[k][i];
        } else if (play_black) {
            memset(c, BLACK, 8);
        } else {
            const uint8_t *p = &bgpen[y][cx * 8 - PF_X];
            uint32_t w0, w1;
            memcpy(&w0, p, 4);
            memcpy(&w1, p + 4, 4);
            if (o && ((w0 | w1) & 0x80808080u))     // the sprites, but behind the tiles in front of them
                for (int i = 0; i < 8; i++) c[i] = (o[k][i] && !(p[i] & 0x80)) ? o[k][i] : p[i] & 0x7f;
            else {
                w0 &= 0x7f7f7f7fu;
                w1 &= 0x7f7f7f7fu;
                memcpy(c, &w0, 4);
                memcpy(c + 4, &w1, 4);
                if (o)
                    for (int i = 0; i < 8; i++)
                        if (o[k][i]) c[i] = o[k][i];
            }
        }
        span(e, (unsigned)(b * 256 + cx * 8 + k), c);
    }
    e->stats.cells += 8;
}

// The places to make again: those marked (tiles, pieces) and those the
// sprites are in now or were in last time whose sprites differ (a hash of
// each place's overlay, kept)
TPCV_HOT static void make_places(tpcv_t *e) {
    uint32_t now[24];
    memset(now, 0, sizeof now);
    for (int k = 0; k < nvis; k++) {
        const vis_t *v = &vis[k];
        uint32_t m = (1u << ((v->x1 - 1) / 8 + 1)) - 1;
        if ((v->x1 - 1) / 8 >= 31) m = ~0u;
        m &= ~((1u << (v->x0 / 8)) - 1);
        for (int b = v->y0 / 8; b <= (v->y1 - 1) / 8; b++) now[b] |= m;
    }
    uint8_t o[8][8];
    for (int b = 0; b < 24; b++) {
        uint32_t m = now[b] | e->cells_prev[b] | cells[b];
        for (; m; m &= m - 1) {
            int cx = __builtin_ctz(m);
            bool over = (now[b] >> cx & 1) && overlay(b, cx, o);
            uint32_t h = 2166136261u;
            if (over)
                for (int k = 0; k < 8; k++) {
                    uint32_t w0, w1;
                    memcpy(&w0, &o[k][0], 4);
                    memcpy(&w1, &o[k][4], 4);
                    h = (h ^ w0) * 16777619u;
                    h = (h ^ w1) * 16777619u;
                }
            if (h != ov_hash[b][cx] || (cells[b] >> cx & 1)) {
                ov_hash[b][cx] = h;
                make_cell(e, b, cx, over ? (const uint8_t (*)[8])o : NULL);
            }
        }
        cells[b] = 0;
    }
    memcpy(e->cells_prev, now, sizeof now);
}

// ---- the picture -----------------------------------------------------------------------
void tpcv_board(tpcv_board_t *b) {
    memcpy(b->vram, tp_videoram, sizeof b->vram);
    memcpy(b->cram, tp_colorram, sizeof b->cram);
    b->spans.n = tp_spans.n;
    memcpy(b->spans.span, tp_spans.span, (size_t)tp_spans.n * sizeof tp_spans.span[0]);
    b->flip = TP_FLIP ? 1 : 0;
    b->video_on = tp_video_on_frame;
    b->frame = tp_frames;
}

// the era: the colour set (1-5) most of a sample of the play area's tiles have
static uint8_t era_of(const tpcv_board_t *b) {
    uint8_t n[32] = {0};
    for (int dr = DROW0 + 1; dr < DROW0 + DROWS; dr += 5)
        for (int dc = 1; dc < 28; dc += 4) n[b->cram[tile_at(b->flip, dc, dr)] & 0x1f]++;
    uint8_t best = 0;
    for (int s = 1; s <= 5; s++)
        if (n[s] > n[best]) best = (uint8_t)s;
    return n[best] >= 20 ? best : 0;
}

static inline bool same(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    for (size_t i = 0; i < n; i += 4) {
        uint32_t x, y;
        memcpy(&x, p + i, 4);
        memcpy(&y, q + i, 4);
        if (x != y) return false;
    }
    return true;
}

TPCV_HOT void tpcv_picture(tpcv_t *e, const tpcv_board_t *b) {
    bd = b;
    if (!colors_ready) colors_init();
    if (!pens_ready) pens_init();
    if (!maps_ready) maps_init();
    if (!pic_clear) {                   // (the plugin's room before the game)
        memset(&pic_u, 0, sizeof pic_u.p);
        memset(ov_hash, 0xff, sizeof ov_hash);
        pic_clear = true;
        e->pic_valid = false;
    }
    if (b->flip != e->flip || b->video_on != e->video_on) {
        e->pic_valid = false;
        e->flip = b->flip;
        e->video_on = b->video_on;
    }
    if (!b->video_on) {                 // (the board's picture off: black)
        if (!e->pic_valid) {
            memset(margin, BLACK, sizeof margin);
            play_black = true;
            for (int i = 0; i < 24; i++) cells[i] = ~0u;
            nvis = 0;
            make_places(e);
            e->pic_valid = true;
            e->stats.pictures++;
            e->dirty = true;
        }
        return;
    }
    uint8_t era = era_of(b);
    if (era != era_now) {               // the sprites' colours for this sky: all of them again
        era_now = era;
        memset(ov_hash, 0xff, sizeof ov_hash);
    }
    e->era = era;
    // the tiles that changed (all of them after a message, or a flip)
    bool all = !e->pic_valid;
    if (all) {
        memset(margin, BLACK, sizeof margin);
        play_black = false;
        for (int i = 0; i < 24; i++) cells[i] = ~0u;
        piece_dirty = (uint16_t)((1u << NPIECES) - 1);
        memset(ov_hash, 0xff, sizeof ov_hash);
    }
    // the play area's tiles that changed, drawn, then the TMS9918 columns
    // and lines each reaches (with its neighbours', two made one) scaled
    // again; many of them: the whole of it
    static uint16_t changed[DROWS * 28];
    int nchanged = 0;
    if (all || !same(b->vram, e->vram_seen, 0x400) || !same(b->cram, e->cram_seen, 0x400)) {
        for (int i = 2 * 32; i < 30 * 32; i++) {
            if (!all && b->vram[i] == e->vram_seen[i] && b->cram[i] == e->cram_seen[i]) continue;
            int dc = b->flip ? i / 32 - 2 : 29 - i / 32, dr = b->flip ? 31 - i % 32 : i % 32;
            if (dr >= DROW0 && dr < DROW0 + DROWS) {
                e->stats.tiles++;
                changed[nchanged++] = (uint16_t)(dr << 8 | dc);
            } else {
                piece_dirty |= tile_piece[dr][dc];
            }
        }
        memcpy(e->vram_seen, b->vram, 0x400);
        memcpy(e->cram_seen, b->cram, 0x400);
    }
    if (all || nchanged > 120) rescale(0, 0, PF_W - 1, PF_H - 1);
    else
        for (int k = 0; k < nchanged; k++) {
            int dc = changed[k] & 0xff, dr = changed[k] >> 8, y0 = (dr - DROW0) * 8;
            rescale(col_of[dc ? dc * 8 - 1 : 0], row_of[y0 ? y0 - 1 : 0], col_of[dc * 8 + 8 < DW ? dc * 8 + 8 : DW - 1],
                    row_of[y0 + 8 < DH ? y0 + 8 : DH - 1]);
        }
    // the year, as the game writes it at a stage's start ("A.D.1910", row 19)
    {
        int i0 = tile_at(b->flip, 10, 19);
        if (b->vram[i0] == 0x7e && !(b->cram[i0] & 0x20) && b->vram[tile_at(b->flip, 11, 19)] == 0x29) {
            uint8_t y[7], col = 19;     // (the game's flashes; the margin's stays as CREDIT's)
            for (int c = 0; c < 7; c++) {
                int i = tile_at(b->flip, 10 + c, 19);
                y[c] = b->vram[i];
            }
            if (memcmp(y, e->year, 7) || col != e->year_col) {
                memcpy(e->year, y, 7);
                e->year_col = col;
                for (int p = 0; p < NPIECES; p++)
                    if (pieces[p].c0 == PIECE_YEAR) piece_dirty |= (uint16_t)(1u << p);
            }
        }
    }
    for (int p = 0; p < NPIECES; p++)
        if (piece_dirty >> p & 1) draw_piece(e, p);
    piece_dirty = 0;
    e->pic_valid = true;
    pic_stamp++;
    find_sprites();
    make_places(e);
    e->stats.pictures++;
    e->dirty = true;
}

void tpcv_canvas(tpcv_t *e, const uint8_t *px) {
    if (!colors_ready) colors_init();
    e->pic_valid = false;
    uint8_t c[8];
    for (int y = 0; y < TPCV_H; y++)
        for (int cx = 0; cx < 32; cx++) {
            for (int i = 0; i < 8; i++) {
                uint8_t v = px[y * TPCV_W + cx * 8 + i];
                c[i] = v ? v : BLACK;
            }
            span(e, (unsigned)((y >> 3) * 256 + cx * 8 + (y & 7)), c);
        }
    e->bands = ~0u;
    memset(e->cells_prev, 0xff, sizeof e->cells_prev);
    memset(ov_hash, 0xff, sizeof ov_hash);
    e->stats.pictures++;
    e->dirty = true;
}

static void screen_of(const uint8_t *pat, const uint8_t *colt, uint8_t *px) {
    for (int y = 0; y < TPCV_H; y++)
        for (int x = 0; x < TPCV_W; x++) {
            unsigned a = (unsigned)(((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7));
            uint8_t c = colt[a];
            c = (pat[a] & (0x80 >> (x & 7))) ? c >> 4 : c & 15;
            px[y * TPCV_W + x] = c ? c : BLACK;
        }
}

void tpcv_screen(const tpcv_t *e, uint8_t *px) { screen_of(e->vram_pat, e->vram_col, px); }
void tpcv_wanted(const tpcv_t *e, uint8_t *px) { screen_of(e->want_pat, e->want_col, px); }

void tpcv_ideal(const tpcv_t *e, uint8_t *px) {
    (void)e;
    uint8_t o[8][8];
    for (int b = 0; b < 24; b++)
        for (int cx = 0; cx < 32; cx++) {
            bool over = overlay(b, cx, o);
            for (int k = 0; k < 8; k++)
                for (int i = 0; i < 8; i++) {
                    int y = b * 8 + k, x = cx * 8 + i;
                    bool play = x >= PF_X && x < PF_X + PF_W;
                    uint8_t c = !play ? margin[y][MX(x)] : play_black ? BLACK : bgpen[y][x - PF_X] & 0x7f;
                    if (over && o[k][i] && !(play && !play_black && (bgpen[y][x - PF_X] & 0x80))) c = o[k][i];
                    px[y * TPCV_W + x] = c ? c : BLACK;
                }
        }
}

// ---- what the console needs ----------------------------------------------------------
static inline bool need_pat(const tpcv_t *e, unsigned j) { return e->want_pat[j] != e->vram_pat[j]; }
static inline bool need_col(const tpcv_t *e, unsigned j) { return e->want_col[j] != e->vram_col[j]; }

// The next bytes to send, from the cursor on (qbcv.c's next_run)
TPCV_HOT static bool next_run(tpcv_t *e, bool *color, unsigned *s, int *m, unsigned most) {
    if (!e->dirty) return false;
    unsigned i = e->cursor;
    int band = -1;
    for (unsigned k = 0; k < TPCV_BYTES;) {
        if (!(i & 255)) {
            if (band >= 0) e->bands &= ~(1u << band);
            band = -1;
            if (k + 256 <= TPCV_BYTES && !(e->bands >> (i >> 8) & 1)) {
                k += 256;
                i = i + 256 == TPCV_BYTES ? 0 : i + 256;
                continue;
            }
            band = (int)(i >> 8);
        }
        if (!(i & 3) && k + 4 <= TPCV_BYTES) {
            uint32_t w, v, wc, vc;
            memcpy(&w, e->want_pat + i, 4);
            memcpy(&v, e->vram_pat + i, 4);
            memcpy(&wc, e->want_col + i, 4);
            memcpy(&vc, e->vram_col + i, 4);
            if (w == v && wc == vc) {
                k += 4;
                i = i + 4 == TPCV_BYTES ? 0 : i + 4;
                continue;
            }
        }
        bool nc = need_col(e, i);
        if (!nc && !need_pat(e, i)) {
            k++;
            i = i + 1 == TPCV_BYTES ? 0 : i + 1;
            continue;
        }
        e->cursor = (uint16_t)i;
        *color = nc;
        *s = i;
        unsigned end = i + 1, gap = 0;
        for (unsigned j = i + 1; j < TPCV_BYTES && j - i < most; j++) {
            if (nc ? need_col(e, j) : need_pat(e, j)) { end = j + 1; gap = 0; }
            else if (++gap > 3) break;
        }
        *m = (int)(end - i);
        return true;
    }
    e->dirty = false;
    return false;
}

static void put_run(tpcv_t *e, list_t *l, bool color, unsigned s, int n) {
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    uint8_t *d = l->b + l->n;
    if (color) {
        memcpy(d, e->want_col + s, n);
        memcpy(e->vram_col + s, d, n);
        e->stats.col_bytes += (unsigned)n;
    } else {
        memcpy(d, e->want_pat + s, n);
        memcpy(e->vram_pat + s, d, n);
        e->stats.pat_bytes += (unsigned)n;
    }
    l->n += n;
    e->stats.runs++;
}

// what's on screen written again as it is (qbcv.c's)
static void refresh_run(tpcv_t *e, list_t *l, int n) {
    unsigned at = e->refresh_at;
    bool color = at >= TPCV_BYTES;
    unsigned s = color ? at - TPCV_BYTES : at;
    if (s + n > TPCV_BYTES) n = (int)(TPCV_BYTES - s);
    l_run_head(l, (uint16_t)((color ? COLORS : PATTERNS) + s), n);
    memcpy(l->b + l->n, (color ? e->vram_col : e->vram_pat) + s, n);
    l->n += n;
    e->refresh_at = (uint16_t)((at + n) % (2 * TPCV_BYTES));
    e->stats.refreshed += (unsigned)n;
}

uint32_t tpcv_backlog(const tpcv_t *e) {
    uint32_t n = 0;
    for (unsigned j = 0; j < TPCV_BYTES; j++) n += need_pat(e, j) + need_col(e, j);
    return n;
}

// the name table: name i at place i in each third (it changes never)
static void names_run(tpcv_t *e, list_t *l, int n) {
    int left = 768 - e->names_at;
    if (n > left) n = left;
    l_run_head(l, (uint16_t)(NAMES + e->names_at), n);
    for (int k = 0; k < n; k++) l->b[l->n++] = (uint8_t)(e->names_at + k);
    e->names_at = (uint16_t)(e->names_at + n);
    if (e->names_at == 768) e->names_done = true;
}

// ---- a list, the console waiting for the vertical blank (messages, and the tones) ------
static void plain_list(tpcv_t *e, list_t *l) {
    if (e->resync) {
        l_end_vblank(l);
        if (e->tone_n) l_psg(l, e->tone_bytes, e->tone_n);
        if (e->resync-- == 2) return;
    } else if (e->tone_n) {
        l_psg(l, e->tone_bytes, e->tone_n);
    }
    while (!e->names_done) {
        int n = room(l);
        if (n < 16 && l->vblank) { l_end_vblank(l); continue; }
        if (n < 1) break;
        names_run(e, l, n);
    }
    bool color;
    unsigned s;
    int m;
    while (e->names_done && next_run(e, &color, &s, &m, 255)) {
        int n = room(l);
        if (n < m && l->vblank) {
            if (n >= 8) put_run(e, l, color, s, n);
            l_end_vblank(l);
            continue;
        }
        if (n < 1) break;
        put_run(e, l, color, s, n < m ? n : m);
    }
    for (int k = 0; k < 2 && e->names_done; k++) {
        int n = room(l);
        if (n < 32) break;
        refresh_run(e, l, n);
    }
    l_end_vblank(l);
}

// ---- a list with samples in it (qbcv.c's pcm_list) -------------------------------------
TPCV_HOT static void fill(tpcv_t *e, list_t *l, uint32_t until) {
    int avail = (int)until - (int)l->t;
    if (avail < C_RUN_ACT + C_BYTE_ACT) return;
    int n = (avail - C_RUN_ACT) / C_BYTE_ACT;
    if (n > 255) n = 255;
    if (!e->names_done) { names_run(e, l, n); return; }
    bool color;
    unsigned s;
    int m;
    if (!next_run(e, &color, &s, &m, 1)) {
        if (e->refresh_at % TPCV_BYTES + (unsigned)n > TPCV_BYTES) e->refresh_at = (uint16_t)(e->refresh_at / TPCV_BYTES * TPCV_BYTES);
        refresh_run(e, l, n);
        return;
    }
    if (s + (unsigned)n > TPCV_BYTES) s = TPCV_BYTES - (unsigned)n;
    put_run(e, l, color, s, n);
}

TPCV_HOT static void pcm_list(tpcv_t *e, list_t *l) {
    l->t = C_FRAME;
    l_end_vblank(l);
    unsigned tone_at = 0;
    for (int k = 0; k < TPCV_PCM_N; k++) {
        uint8_t b[10];
        int n = 0;
        for (int j = 0; j < e->pcm_nb; j++) b[n++] = e->pcm_bytes[k][j];
        while (tone_at < e->tone_n && n < 5) {
            unsigned g = 1;
            while (tone_at + g < e->tone_n && !(e->tone_bytes[tone_at + g] & 0x80)) g++;
            if (n + (int)g > 6) break;
            memcpy(b + n, e->tone_bytes + tone_at, g);
            n += (int)g;
            tone_at += g;
        }
        if (k + 1 == TPCV_PCM_N) {
            l_psg(l, b, n);
            break;
        }
        uint32_t until = PCM_FIRST + (((uint32_t)(k + 1) * PCM_STEP) >> 8);
        int avail = (int)until - (int)l->t - (C_PSG + C_PSG_BYTE * n);
        // a short run of the picture's as one of two (qbcv.c's)
        bool color;
        unsigned s;
        int m = 0;
        bool two = e->names_done && next_run(e, &color, &s, &m, 7) && m <= 2 &&
                   avail >= 2 * C_RUN_ACT + C_BYTE_ACT * (m + 2);
        int need = two ? m + 1 : 1;
        int base = avail - (two ? 2 : 1) * C_RUN_ACT, best = 0, best_left = 99;
        for (int r = 0; r < 3 && base - C_PSG_BYTE * r >= C_BYTE_ACT * need; r++) {
            int left = (base - C_PSG_BYTE * r) % C_BYTE_ACT;
            if (left < best_left) { best_left = left; best = r; }
        }
        while (best--) b[n++] = e->pcm_bytes[k][e->pcm_nb - 1];     // (the last attenuator again: no change)
        l_psg(l, b, n);
        if (two) put_run(e, l, color, s, m);
        fill(e, l, until);
    }
    e->stats.pcm_frames++;
    e->resync = 2;
}

size_t tpcv_tv_frame(tpcv_t *e, uint8_t *out) {
    list_t l = {out, 0, C_START, true};
    if (e->pcm) pcm_list(e, &l);
    else plain_list(e, &l);
    l.t += C_END;
    l.b[l.n++] = 0;
    e->tone_n = 0;
    e->stats.tv_frames++;
    if (l.t > e->stats.t_max) e->stats.t_max = l.t;
    if (!e->dirty) e->stats.settled++;
    return l.n;
}

// ---- the sound: samples through the DAC ------------------------------------------------
// The DAC: the three tones at a period of 1, their attenuators as measured
// on a console (tools/tp/ladder.json), summed. tpdac3.h's 512 levels go
// from 0 to 2.40, each a triple of attenuators chosen so a small change
// makes small partial sums between a sample's three writes
// (tools/tp/mkdac3.py). The sound rests at the middle (DAC_MID). tpsnd.c
// hands over samples already scaled to the DAC's levels either side of
// the middle (+-32767 for +-DAC_HALF); a soft knee from 3/4 of the way
// rounds the loudest off, never clipping.
#define DAC_LEVELS TPDAC3_LEVELS
#define DAC_MID    255
#define DAC_HALF   255.0f
#define DAC_KNEE   191.0f
static const float ladder[3][16] = {
    {1.0f, 0.7914f, 0.6320f, 0.5084f, 0.4019f, 0.3161f, 0.2459f, 0.1912f, 0.1396f, 0.1080f, 0.0844f, 0.0672f, 0.0524f, 0.0421f, 0.0310f, 0.0f},
    {1.0f, 0.7816f, 0.6125f, 0.4850f, 0.3925f, 0.3109f, 0.2416f, 0.1872f, 0.1373f, 0.1058f, 0.0828f, 0.0653f, 0.0533f, 0.0412f, 0.0306f, 0.0f},
    {1.0f, 0.7979f, 0.6229f, 0.4921f, 0.3942f, 0.3004f, 0.2348f, 0.1874f, 0.1419f, 0.1106f, 0.0846f, 0.0677f, 0.0534f, 0.0420f, 0.0328f, 0.0f}};
static uint8_t limit[512];                  // |level| (0-511) to 0-255: straight to DAC_KNEE, then soft
static int16_t lq[3][16];                   // ladder in 1/16384ths, for put_sample's integer sums
static bool dac_ready;

float tpcv_dac_level(int a0, int a1, int a2) { return ladder[0][a0 & 15] + ladder[1][a1 & 15] + ladder[2][a2 & 15]; }

static void dac_init(void) {
    for (int i = 0; i < 512; i++) {
        float v = (float)i;
        if (v > DAC_KNEE) v = DAC_KNEE + (DAC_HALF - DAC_KNEE) * (1.0f - expf(-(v - DAC_KNEE) / (DAC_HALF - DAC_KNEE)));
        limit[i] = (uint8_t)(v + 0.5f);
    }
    for (int c = 0; c < 3; c++)
        for (int a = 0; a < 16; a++) lq[c][a] = (int16_t)lrintf(ladder[c][a] * 16384.0f);
    dac_ready = true;
}

// a sample's three writes, in the order whose two partial sums (the chip
// between the writes, 52 T each) are nearest the new level, in integers
// (dkcv.c's put_sample)
static const uint8_t order[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
static inline int32_t iabs(int32_t x) { return x < 0 ? -x : x; }
static inline void put_sample(uint8_t *out, int *a0, int *a1, int *a2, uint16_t t) {
    const int w0 = t >> 8, w1 = t >> 4 & 15, w2 = t & 15;
    const int32_t d0 = lq[0][w0] - lq[0][*a0], d1 = lq[1][w1] - lq[1][*a1], d2 = lq[2][w2] - lq[2][*a2];
    const int32_t sum = d0 + d1 + d2;
    const int32_t f0 = iabs(sum - d0), f1 = iabs(sum - d1), f2 = iabs(sum - d2);
    const int32_t l0 = iabs(d0), l1 = iabs(d1), l2 = iabs(d2);
    int32_t best = f0 + l2, v;
    int pick = 0;
    if ((v = f0 + l1) < best) { best = v; pick = 1; }
    if ((v = f1 + l2) < best) { best = v; pick = 2; }
    if ((v = f1 + l0) < best) { best = v; pick = 3; }
    if ((v = f2 + l1) < best) { best = v; pick = 4; }
    if ((v = f2 + l0) < best) pick = 5;
    const uint32_t b = (uint32_t)(0x90 | w0) | (uint32_t)(0xB0 | w1) << 8 | (uint32_t)(0xD0 | w2) << 16;
    out[0] = (uint8_t)(b >> 8 * order[pick][0]);
    out[1] = (uint8_t)(b >> 8 * order[pick][1]);
    out[2] = (uint8_t)(b >> 8 * order[pick][2]);
    *a0 = w0; *a1 = w1; *a2 = w2;
}

TPCV_HOT void tpcv_sound_pcm(tpcv_t *e, const int16_t *s, int n) {
    if (!dac_ready) dac_init();
    e->tone_n = 0;
    if (n <= 0) {                       // silence: the whole chip, every list (a misread byte lasts a list)
        static const uint8_t off[] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};
        memcpy(e->tone_bytes, off, sizeof off);
        e->tone_n = sizeof off;
        e->pcm = false;
        return;
    }
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;     // (as the lists before left them)
    }
    e->pcm_nb = 3;
    // Every list: the three tones parked at a period of 1, the noise silent,
    // so every register is written each list (a byte the console misreads
    // lasts a list at most)
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00, 0xC1, 0x00, 0xE4, 0xFF};
    memcpy(e->tone_bytes, park, sizeof park);
    e->tone_n = sizeof park;
    int32_t gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1], a2 = e->dac_att[2];
    uint32_t clipped = 0;
    for (int k = 0; k < TPCV_PCM_N; k++) {
        int32_t v = s[k < n ? k : n - 1] >> 6;          // +-32767 to +-511 levels
        int32_t a = v < 0 ? -v : v;
        if (a > (int32_t)DAC_KNEE) clipped++;
        a = limit[a > 511 ? 511 : a];
        int32_t lvl = DAC_MID + (v < 0 ? -a : a);
        if (gain < 32 && !(k & 7)) gain++;              // (up to its rest over 43 ms)
        lvl = lvl * gain / 32;
        put_sample(e->pcm_bytes[k], &a0, &a1, &a2, tp_dac3[lvl < 0 ? 0 : lvl > DAC_LEVELS - 1 ? DAC_LEVELS - 1 : lvl]);
    }
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
    e->dac_att[2] = (uint8_t)a2;
    e->stats.clipped += clipped;
}

// ---- the sound: the chip's own tones ---------------------------------------------------
void tpcv_sound_psg(tpcv_t *e, const uint8_t psg[11]) {
    if (e->pcm) {                       // (from samples: the DAC let go first)
        e->pcm = false;
        e->resync = 0;
    }
    memcpy(e->tone_bytes, psg, 11);
    e->tone_n = 11;
}

// ---- the sound: the mix (tones 0 and 1 a DAC of two, tone 2 a tone) -------------------
// The two tones' 256 levels from 0 to 1.60 (the ladder's), each the pair
// of attenuators nearest it, ties to the pair nearest the level below's
#define DAC2_TOP 1.60f
static uint8_t dac2[256];               // tone 0's attenuation << 4 | tone 1's
static bool dac2_ready;

static void dac2_init(void) {
    int prev = 0xff;
    for (int i = 0; i < 256; i++) {
        float want = DAC2_TOP * (float)i / 255.0f, bd = 1e9f;
        int best = 0xff;
        for (int a = 0; a < 16; a++)
            for (int b = 0; b < 16; b++) {
                float d = fabsf(ladder[0][a] + ladder[1][b] - want);
                d += 0.002f * (float)(abs(a - (prev >> 4)) + abs(b - (prev & 15)));
                if (d < bd) { bd = d; best = a << 4 | b; }
            }
        dac2[i] = (uint8_t)best;
        prev = best;
    }
    dac2_ready = true;
}

void tpcv_sound_mix(tpcv_t *e, const int16_t *s, int n, const uint8_t psg3[3]) {
    if (!dac_ready) dac_init();
    if (!dac2_ready) dac2_init();
    if (!e->pcm) {
        e->pcm = true;
        e->pcm_gain = 0;
        e->dac_att[0] = e->dac_att[1] = e->dac_att[2] = 15;
    }
    e->pcm_nb = 2;
    static const uint8_t park[] = {0x81, 0x00, 0xA1, 0x00};
    memcpy(e->tone_bytes, park, sizeof park);
    memcpy(e->tone_bytes + 4, psg3, 3);
    e->tone_bytes[7] = 0xE4;
    e->tone_bytes[8] = 0xFF;
    e->tone_n = 9;
    int32_t gain = e->pcm_gain;
    int a0 = e->dac_att[0], a1 = e->dac_att[1];
    for (int k = 0; k < TPCV_PCM_N; k++) {
        // s in the three tones' levels (2.40 / 511 each, 64 a level), to the two's
        float v = (float)s[k < n ? k : n - 1] * (TPDAC3_TOP / 511.0f / 64.0f) * (255.0f / DAC2_TOP);
        if (gain < 32 && !(k & 7)) gain++;
        int lvl = (int)lrintf((127.5f + v) * (float)gain / 32.0f);
        if (lvl < 0) lvl = 0, e->stats.clipped++;
        if (lvl > 255) lvl = 255, e->stats.clipped++;
        int w0 = dac2[lvl] >> 4, w1 = dac2[lvl] & 15;
        float d0 = fabsf(ladder[0][w0] - ladder[0][a0]), d1 = fabsf(ladder[1][w1] - ladder[1][a1]);
        uint8_t b0 = (uint8_t)(0x90 | w0), b1 = (uint8_t)(0xB0 | w1);
        // the larger change first: between the writes the sound is off by the second's
        e->pcm_bytes[k][0] = d0 >= d1 ? b0 : b1;
        e->pcm_bytes[k][1] = d0 >= d1 ? b1 : b0;
        a0 = w0;
        a1 = w1;
    }
    e->pcm_gain = (uint8_t)gain;
    e->dac_att[0] = (uint8_t)a0;
    e->dac_att[1] = (uint8_t)a1;
}

void tpcv_prepare(void) {
    if (!colors_ready) colors_init();
    if (!pens_ready) pens_init();
    if (!maps_ready) maps_init();
}

void tpcv_init(tpcv_t *e) {
    memset(e, 0, sizeof *e);
    e->dirty = true;
    e->bands = (1u << (TPCV_BYTES / 256)) - 1;
    if (!colors_ready) colors_init();
    if (!dac_ready) dac_init();
    layout_init();
    if (!maps_ready) maps_init();
    pens_ready = false;                 // (made at the first picture, from the board's PROMs)
    era_now = 0;
    pic_clear = false;
}
