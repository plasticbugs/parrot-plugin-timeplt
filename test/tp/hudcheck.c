// hudcheck.c -- every piece of the margins as src/tpcv.c draws it, checked
// for strokes lost. The board runs the attract mode, a one-player game, a
// two-player game and a game set to 2001 (a RAM poke); at each frame the
// picture is made as the plugin makes it, and each piece drawn (the labels,
// the scores, the lives, the flags, the year, the credits, the progress
// bar) is checked against its tiles as the arcade shows them: along each
// line and each column, every run of a lit pen, and every dark run between
// two lit ones, must still be there in what goes to the console (at least
// one pixel of it, where the scaling puts it). A second pass draws every
// score the way scores of a million and over are drawn (7 tiles in 48
// pixels), so all the digits are checked that way too.
//
//   hudcheck ROMDIR       (test/tp/hudcheck.sh builds and runs it)
//
// It includes tpcv.c, to see each piece before its colours.
#define TPCV_HUD_CHECK
#include "../../src/tpcv.c"

#include <stdio.h>
#include <stdlib.h>

#include "tpay.h"

static const char *const piece_name[] = {"1-UP", "score 1", "HI-", "SCORE", "high score", "lives", "lives 2",
                                         "progress bar", "2-UP", "score 2", "flags", "A.D.", "year", "CREDIT", "credits"};
_Static_assert(sizeof piece_name / sizeof piece_name[0] == NPIECES, "a name a piece");

static long checked, failed, runs_seen;
static int fail_by[16][2];
static uint32_t seen_hash[NPIECES][4096];
static int distinct[NPIECES];

static const uint8_t *hud_src_now;      // the piece being checked
// where src pixel (x, y) of piece p goes in out, or -1 (left out)
static int map_x(const piece_t *pc, int x, int y, int w, bool squeezed) {
    (void)y;
    if (pc->kind == HUD_BAR) return y;
    if (pc->kind != HUD_SCORE) return x;
    int t = x / 8, c = x % 8;
    if (!squeezed) {                    // tiles 1-6 a pixel left, tile 1's column 0 (blank) at the end
        if (!t) return -1;
        bool space1 = true;
        for (int yy = 0; yy < 8; yy++)
            if (hud_src_now[yy * w + 8] & 3) space1 = false;
        if (!space1) return x - 8;
        return x == 8 ? 47 : x - 9;
    }
    int in = c <= 4 ? c : c - 1;        // columns 4 and 5 made one
    (void)w;
    if (t == 0) return in ? in - 1 : 0;  // and the first tile's 0 and 1
    return 7 * t - 1 + in;
}
static int map_y(const piece_t *pc, int x, int y, int w) {
    if (pc->kind != HUD_BAR) return y;
    return w - 1 - x;                   // on end
}

static void show(const uint8_t *img, int w, int h) {
    for (int y = 0; y < h; y++) {
        printf("    ");
        for (int x = 0; x < w; x++) putchar(img[y * w + x] & 3 ? "abcdefghijklmnopqrstuvwxyzABCDEF"[img[y * w + x] >> 2] : '.');
        putchar('\n');
    }
}

// one line (or column) of the piece: its runs, each looked for in out
static int check_line(const piece_t *pc, const uint8_t *src, int w, int h, const uint8_t *out, int ow, bool squeezed,
                      bool along_x, int at) {
    const int n = along_x ? w : h;
#define PEN_AT(k) (src[(along_x ? at : (k)) * w + (along_x ? (k) : at)] & 3 ? src[(along_x ? at : (k)) * w + (along_x ? (k) : at)] : 0)
    int lost = 0;
    for (int i = 0; i < n;) {
        const uint8_t v = PEN_AT(i);
        int j = i + 1;
        while (j < n && PEN_AT(j) == v) j++;
        bool lit = v != 0, inner = i > 0 && j < n;
        if (lit || inner) {
            bool mapped = false, there = false;
            for (int k = i; k < j && !there; k++) {
                int xx = along_x ? k : at, yy = along_x ? at : k;
                int ox = map_x(pc, xx, yy, w, squeezed), oy = map_y(pc, xx, yy, w);
                if (ox < 0) continue;
                mapped = true;
                uint8_t o = out[oy * ow + ox];
                if (!(o & 3)) o = 0;
                if (o == v) there = true;
            }
            if ((mapped && !there) || (!mapped && lit)) {   // (unmapped and lit: the blank first tile wasn't)
                lost++;
                if (getenv("HUD_SHOW") && strstr(piece_name[pc - pieces], getenv("HUD_SHOW")))
                    printf("  lost: %s %d, %s %d-%d, pen %02x\n", along_x ? "line" : "column", at, along_x ? "x" : "y", i, j - 1, v);
            }
            runs_seen++;
        }
        i = j;
    }
#undef PEN_AT
    return lost;
}

static void hud_check(int p, const uint8_t *src, int w, int h, const uint8_t *out, int ow, int oh, bool squeezed) {
    const piece_t *pc = &pieces[p];
    hud_src_now = src;
    uint32_t hsh = 2166136261u ^ (squeezed ? 1u : 0u);
    for (int i = 0; i < w * h; i++) hsh = (hsh ^ src[i]) * 16777619u;
    for (int k = 0; k < distinct[p]; k++)
        if (seen_hash[p][k] == hsh) return;
    if (distinct[p] < 4096) seen_hash[p][distinct[p]++] = hsh;
    checked++;
    int lost = 0;
    for (int y = 0; y < h; y++) lost += check_line(pc, src, w, h, out, ow, squeezed, true, y);
    for (int x = 0; x < w; x++) lost += check_line(pc, src, w, h, out, ow, squeezed, false, x);
    if (lost) {
        if (getenv("HUD_SHOW") ? strstr(piece_name[p], getenv("HUD_SHOW")) != NULL : failed < 8) {
            printf("%s%s: %d runs lost. As the arcade shows it:\n", piece_name[p], squeezed ? " (7 in 48)" : "", lost);
            show(src, w, h);
            printf("  and to the console:\n");
            show(out, ow, oh);
        }
        failed++;
        fail_by[p][squeezed]++;
    }
}

static uint8_t maincpu[0x6000], sound[0x1000], tiles[0x2000], sprites[0x4000], proms[0x240];
static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "hudcheck: can't read %s\n", path); exit(1); }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "hudcheck ROMDIR\n"); return 2; }
    const char *d = argv[1];
    load(d, "tm1", maincpu, 0x2000); load(d, "tm2", maincpu + 0x2000, 0x2000); load(d, "tm3", maincpu + 0x4000, 0x2000);
    load(d, "tm7", sound, 0x1000); load(d, "tm6", tiles, 0x2000);
    load(d, "tm4", sprites, 0x2000); load(d, "tm5", sprites + 0x2000, 0x2000);
    load(d, "timeplt.b4", proms, 0x20); load(d, "timeplt.b5", proms + 0x20, 0x20);
    load(d, "timeplt.e9", proms + 0x40, 0x100); load(d, "timeplt.e12", proms + 0x140, 0x100);
    tp_roms_t roms = {maincpu, sound, tiles, sprites, proms};
    static tpcv_t cv;
    static tpcv_board_t b;
    for (int pass = 0; pass < 2; pass++) {
        hud_score_squeezed = pass == 1;
        for (int run = 0; run < 4; run++) {
            tpcv_init(&cv);
            tpay_reset();
            tp_init(&roms);
            tpcv_prepare();
            const int frames = run == 0 ? 12000 : run == 3 ? 2400 : 9000;
            for (int f = 0; f < frames; f++) {
                uint8_t in0 = 0, in1 = 0;
                if (run > 0) {
                    if (f % 1500 == 300 || (run == 2 && f == 320)) in0 |= 0x01;     // coins
                    if (f >= 360 && f < 366) in0 |= run == 2 ? 0x10 : 0x08;       // a start
                    if (f > 420) {                                                  // round and round, firing
                        static const uint8_t dirs[8] = {0x01, 0x05, 0x04, 0x06, 0x02, 0x0a, 0x08, 0x09};
                        in1 = dirs[(f / 25) % 8];
                        if ((f / 3) & 1) in1 |= 0x10;
                    }
                }
                tp_in.in0 = (uint8_t)~in0;
                tp_in.in1 = (uint8_t)~in1;
                if (run == 3 && f == 380) { tp_ram[0xad0c - 0xa800] = 5; tp_ram[0xaff0 - 0xa800] = 5; }
                tp_frame();
                tpcv_board(&b);
                tpcv_picture(&cv, &b);
            }
        }
    }
    printf("hudcheck: %ld pieces checked (", checked);
    for (int p = 0; p < NPIECES; p++) printf("%s%s %d", p ? ", " : "", piece_name[p], distinct[p]);
    printf("), %ld runs; %ld with a stroke or a gap lost\n", runs_seen, failed);
    for (int p = 0; p < NPIECES; p++)
        if (fail_by[p][0] || fail_by[p][1]) printf("  %s: %d, %d drawn 7 in 48\n", piece_name[p], fail_by[p][0], fail_by[p][1]);
    return failed ? 1 : 0;
}
