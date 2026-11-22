// cart_host.c -- the cart's side on the Mac: the board run a list at a time
// as the plugin runs it (src/tprun.c), the picture and the sound through
// src/tpcv.c. For previews, the lists' numbers and the lists themselves.
// (Donkey Kong's.)
//
//   cart_host ROMDIR [-lists N] [-fscript FILE] [-bot FROM] [-sound pcm|tones|mix]
//             [-png DIR -at L,L,...] [-wav FILE] [-out LISTS.bin] [-from L]
//             [-stats FILE] [-dump FILE] [-poke FRAME:ADDR:VALUE]
//
// -fscript: tp_host's script (frames, the bits pressed), each line's
// inputs set at that frame's VBLANK as tp_host and MAME set them, so the
// game is MAME's frame for frame. -bot: tp_host's bot from that frame.
// -sound: the plugin's samples through the DAC (pcm), or the comparison's
// tones or mix (tpsnd.h). -png: at each list in -at, what the console
// shows (cv_LIST.png, 256 x 192 doubled), the picture before the
// TMS9918's two colours a place (ideal_LIST.png), MAME's (mame_LIST.png,
// 224 x 256) and MAME's and the console's side by side (pair_LIST.png).
// -wav: the DAC's samples, the attenuators the lists carry as levels (the
// measured ladder) at the lists' 5,962 Hz (pcm only). -out: the lists
// from list -from on, as test/host/host.c records them, for
// tools/mame-replay.sh (the real kernel in MAME: its picture and, with
// MAME_WAV, its sound). -stats: a line a list. -dump: tp_host's record
// at each VBLANK, to check the board run a list at a time is the board run
// a frame at a time. -poke: a byte of the main CPU's RAM set at a frame's
// VBLANK (previews of later stages). All of these are made from the ROMs:
// keep them under $TMPDIR.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tp.h"
#include "tpay.h"
#include "tpcv.h"
#include "tprun.h"
#include "tpsnd.h"
#include "tpz80.h"

static uint8_t maincpu[0x6000], sound[0x1000], tiles[0x2000], sprites[0x4000], proms[0x240];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "cart_host: can't read %s\n", path); exit(1); }
    fclose(f);
}

// ---- PNG, stored (no compression) ------------------------------------------------------
static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    be32(h, crc(crc(0, (const uint8_t *)type, 4), d, n));
    fwrite(h, 1, 4, f);
}

static void png(const char *name, int w, int h, const uint8_t *rgb, int scale) {
    int W = w * scale, H = h * scale;
    size_t row = (size_t)W * 3 + 1, raw_n = row * H;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < H; y++) {
        raw[y * row] = 0;
        for (int x = 0; x < W; x++) memcpy(raw + y * row + 1 + x * 3, rgb + ((size_t)(y / scale) * w + x / scale) * 3, 3);
    }
    size_t blocks = (raw_n + 65534) / 65535, z_n = 2 + raw_n + blocks * 5 + 4;
    uint8_t *z = malloc(z_n), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        size_t n = raw_n - off < 65535 ? raw_n - off : 65535;
        *p++ = off + n == raw_n;
        *p++ = (uint8_t)n; *p++ = (uint8_t)(n >> 8); *p++ = (uint8_t)~n; *p++ = (uint8_t)(~n >> 8);
        memcpy(p, raw + off, n);
        p += n;
    }
    be32(p, b << 16 | a);
    p += 4;
    FILE *f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "cart_host: can't write %s\n", name); exit(1); }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)W); be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (size_t)(p - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void cv_png(const char *name, const uint8_t *px) {
    static uint8_t rgb[TPCV_W * TPCV_H * 3];
    for (int i = 0; i < TPCV_W * TPCV_H; i++) {
        uint32_t c = tpcv_tms_rgb[px[i] & 15];
        rgb[i * 3] = (uint8_t)(c >> 16); rgb[i * 3 + 1] = (uint8_t)(c >> 8); rgb[i * 3 + 2] = (uint8_t)c;
    }
    png(name, TPCV_W, TPCV_H, rgb, 2);
}

// ---- MAME's picture: drawn a line at a time, kept for the last few VBLANKs -------------
static uint16_t fb[TP_H][TP_W];
#define KEEP 4
static struct { uint32_t frame; uint16_t px[TP_H][TP_W]; } kept[KEEP];
static void on_line(int line) {
    if (line >= 16 && line < 240) tp_render_line(fb[line - 16], line);
}

// MAME's (turned, ROT90: 224 x 256) picture of frame f, as RGB
static bool mame_rgb(uint32_t f, uint8_t *rgb) {
    for (int k = 0; k < KEEP; k++) {
        if (kept[k].frame != f) continue;
        for (int Y = 0; Y < 256; Y++)
            for (int X = 0; X < 224; X++) {
                uint32_t c = tp_rgb[kept[k].px[239 - X - 16][Y]];
                uint8_t *o = rgb + (Y * 224 + X) * 3;
                o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
            }
        return true;
    }
    return false;
}

// MAME's picture (224 x 256) and the console's (256 x 192 at the TV's 4:3,
// so 256 high it is 341 wide), both doubled, on grey
static void pair_png(const char *name, const uint8_t *m, const uint8_t *px) {
    enum { MW = 448, MH = 512, CW = 683, PW = MW + 16 + CW, PH = MH };
    static uint8_t out[PW * PH * 3];
    memset(out, 0x30, sizeof out);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) memcpy(out + (y * PW + x) * 3, m + ((y / 2) * 224 + x / 2) * 3, 3);
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < CW; x++) {
            uint32_t c = tpcv_tms_rgb[px[(y * TPCV_H / MH) * TPCV_W + x * TPCV_W / CW] & 15];
            uint8_t *o = out + (y * PW + MW + 16 + x) * 3;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c;
        }
    png(name, PW, PH, out, 1);
}

// ---- the inputs at each VBLANK ----------------------------------------------------------
static struct { int frame; char port[4]; int v; } fev[16384];
static int nfev, botfrom;
static struct { int frame; uint16_t addr; uint8_t v; } pokes[16];
static int npokes;
static void (*tprun_vblank)(void);
static FILE *fdump;

static uint8_t bot(uint32_t n) {           // tp_host's
    const uint8_t *s0 = tp_spriteram[0], *s1 = tp_spriteram[1];
    int px = s0[0x16] + 8, py = 241 - s1[0x17] + 8;
    int best = 1 << 30, dx = 0, dy = 0;
    for (int offs = 0x24; offs <= 0x34; offs += 2) {
        if (!s0[offs + 1]) continue;
        int col = s1[offs] & 0x3f;
        if (col == 12 || col == 44) continue;
        int ex = s0[offs] + 8, ey = 241 - s1[offs + 1] + 8;
        int ddx = -(ey - py), ddy = ex - px;
        int d2 = ddx * ddx + ddy * ddy;
        if (d2 < best) { best = d2; dx = ddx; dy = ddy; }
    }
    uint8_t v = (n & 4) ? 0x10 : 0;
    if (best == 1 << 30) return v;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax * 5 > ay * 2) v |= dx > 0 ? 0x02 : 0x01;
    if (ay * 5 > ax * 2) v |= dy > 0 ? 0x08 : 0x04;
    return v;
}

static void on_vblank(void) {
    uint32_t n = tp_frames;
    // MAME's picture of the frame that just ended
    memcpy(kept[n % KEEP].px, fb, sizeof fb);
    kept[n % KEEP].frame = n;
    tprun_vblank();
    if (fdump) {                        // tp_host's record (taken where tp_host takes it)
        static uint8_t rec[8 + 0x400 + 0x400 + 0x800 + 0x200 + 32 + 32 + 0x400];
        uint8_t *p = rec;
        memcpy(p, "TPFR", 4); p += 4;
        memcpy(p, &n, 4); p += 4;
        memcpy(p, tp_colorram, 0x400); p += 0x400;
        memcpy(p, tp_videoram, 0x400); p += 0x400;
        memcpy(p, tp_ram, 0x800); p += 0x800;
        memcpy(p, tp_spriteram[0], 0x100); p += 0x100;
        memcpy(p, tp_spriteram[1], 0x100); p += 0x100;
        for (int c = 0; c < 2; c++) {
            z80_regs_t r;
            if (c == 0) tpm_regs(&r); else tps_regs(&r);
            uint16_t w[13] = {r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy, r.af2, r.bc2, r.de2, r.hl2, r.wz};
            memcpy(p, w, 26); p += 26;
            *p++ = r.r; *p++ = r.i; *p++ = r.im; *p++ = r.iff1; *p++ = r.iff2; *p++ = r.halt;
        }
        memcpy(p, tp_snd_ram, 0x400); p += 0x400;
        fwrite(rec, 1, (size_t)(p - rec), fdump);
    }
    for (int i = 0; i < nfev; i++) {
        if (fev[i].frame != (int)n) continue;
        uint8_t v = (uint8_t)~fev[i].v;
        if (!strcmp(fev[i].port, "in0")) tp_in.in0 = v;
        if (!strcmp(fev[i].port, "in1")) tp_in.in1 = v;
    }
    if (botfrom && (int)n >= botfrom) tp_in.in1 = (uint8_t)~bot(n);
    for (int i = 0; i < npokes; i++)
        if (pokes[i].frame == (int)n && pokes[i].addr >= 0xa800 && pokes[i].addr < 0xb000) tp_ram[pokes[i].addr - 0xa800] = pokes[i].v;
}

// ---- the lists' sound (the DAC's levels) ---------------------------------------------------
static int a0 = 15, a1 = 15, a2 = 15;
static int list_samples(const uint8_t *l, size_t n, int16_t *out) {
    int k = 0;
    size_t i = 0;
    bool vblank = true;
    while (i < n) {
        uint8_t c = l[i];
        if (c == 1) i += 4 + l[i + 3];
        else if (c == 2) {
            int m = l[i + 1];
            bool got = false;
            for (int j = 0; j < m; j++) {
                uint8_t b = l[i + 2 + j];
                if ((b & 0xF0) == 0x90) { a0 = b & 15; got = true; }
                if ((b & 0xF0) == 0xB0) { a1 = b & 15; got = true; }
                if ((b & 0xF0) == 0xD0) { a2 = b & 15; got = true; }
            }
            if (got && k < TPCV_PCM_N) out[k++] = (int16_t)((tpcv_dac_level(a0, a1, a2) - 1.20f) * 12500.0f);
            i += 2 + (size_t)m;
        } else if (c == 3 && vblank) i += 3;
        else if (c == 0 && vblank) { vblank = false; i++; }
        else break;
    }
    return k;
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
    uint32_t v;
    uint16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    fwrite(&rate, 4, 1, f); v = rate * 2; fwrite(&v, 4, 1, f);
    h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: cart_host ROMDIR [options]\n"); return 2; }
    const char *dir = argv[1], *fscript = NULL, *pngdir = NULL, *at = "", *wavname = NULL, *statsname = NULL,
               *outname = NULL;
    int lists = 2400, from = 1;
    uint8_t mode = TPRUN_PCM;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-lists")) lists = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fscript")) fscript = argv[++i];
        else if (!strcmp(argv[i], "-bot")) botfrom = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-png")) pngdir = argv[++i];
        else if (!strcmp(argv[i], "-at")) at = argv[++i];
        else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
        else if (!strcmp(argv[i], "-stats")) statsname = argv[++i];
        else if (!strcmp(argv[i], "-out")) outname = argv[++i];
        else if (!strcmp(argv[i], "-from")) from = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-dump")) fdump = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "-poke") && npokes < 16) {
            unsigned f, a, v;
            if (sscanf(argv[++i], "%u:%x:%x", &f, &a, &v) != 3) { fprintf(stderr, "cart_host: -poke FRAME:ADDR:VALUE\n"); return 2; }
            pokes[npokes].frame = (int)f; pokes[npokes].addr = (uint16_t)a; pokes[npokes].v = (uint8_t)v; npokes++;
        } else if (!strcmp(argv[i], "-sound")) {
            const char *m = argv[++i];
            mode = !strcmp(m, "tones") ? TPRUN_TONES : !strcmp(m, "mix") ? TPRUN_MIX : TPRUN_PCM;
        } else { fprintf(stderr, "cart_host: what is %s?\n", argv[i]); return 2; }
    }
    load(dir, "tm1", maincpu, 0x2000);
    load(dir, "tm2", maincpu + 0x2000, 0x2000);
    load(dir, "tm3", maincpu + 0x4000, 0x2000);
    load(dir, "tm7", sound, 0x1000);
    load(dir, "tm6", tiles, 0x2000);
    load(dir, "tm4", sprites, 0x2000);
    load(dir, "tm5", sprites + 0x2000, 0x2000);
    load(dir, "timeplt.b4", proms, 0x20);
    load(dir, "timeplt.b5", proms + 0x20, 0x20);
    load(dir, "timeplt.e9", proms + 0x40, 0x100);
    load(dir, "timeplt.e12", proms + 0x140, 0x100);
    tp_roms_t roms = {maincpu, sound, tiles, sprites, proms};
    if (fscript) {
        FILE *f = fopen(fscript, "r");
        if (!f) { fprintf(stderr, "cart_host: can't read %s\n", fscript); return 1; }
        char line[256];
        while (fgets(line, sizeof line, f) && nfev < 16384)
            if (sscanf(line, "%d %3s %x", &fev[nfev].frame, fev[nfev].port, &fev[nfev].v) == 3) nfev++;
        fclose(f);
    }
    static tpcv_t cv;
    static tprun_t run;
    tpcv_init(&cv);
    tpay_reset();
    tp_init(&roms);
    tpcv_prepare();
    tprun_start(&run, &cv);
    run.sound = mode;
    tprun_vblank = tp_on_vblank;
    tp_on_vblank = on_vblank;
    tp_on_line = on_line;

    FILE *wav = wavname ? fopen(wavname, "wb") : NULL;
    if (wav) wav_header(wav, (uint32_t)(TPCV_RATE_Q8 >> 8), 0);
    uint32_t nsamples = 0;
    FILE *stats = statsname ? fopen(statsname, "w") : NULL;
    FILE *out = outname ? fopen(outname, "wb") : NULL;
    static uint8_t list[TPCV_LIST_MAX], px[TPCV_W * TPCV_H];
    uint64_t bytes_sum = 0;
    uint32_t len_max = 0, t_late = 0, pics0 = 0, sec_pics = 0, worst_sec = 1000, best_sec = 0, secs = 0;
    for (int l = 1; l <= lists; l++) {
        uint32_t t0 = cv.stats.pat_bytes + cv.stats.col_bytes;
        size_t n = tprun_list(&run, list);
        uint32_t t1 = cv.stats.pat_bytes + cv.stats.col_bytes;
        bytes_sum += t1 - t0;
        if (n > len_max) len_max = (uint32_t)n;
        if (wav) {
            int16_t s[TPCV_PCM_N];
            int k = list_samples(list, n, s);
            fwrite(s, 2, (size_t)k, wav);
            nsamples += (uint32_t)k;
        }
        if (out && l >= from) {
            const uint8_t h[9] = {'F', 0, 0, (uint8_t)n, (uint8_t)(n >> 8), 0, 0, 0, 0};
            fwrite(h, 1, 9, out);
            fwrite(list, 1, n, out);
        }
        if (stats) fprintf(stats, "%d %zu %u %u %u\n", l, n, t1 - t0, run.pictures, cv.dirty ? 1 : 0);
        // pictures a second: each 60 lists (about a second)
        if (l % 60 == 0) {
            uint32_t p = run.pictures - pics0;
            pics0 = run.pictures;
            if (l > 120) {          // (after the start)
                sec_pics += p;
                secs++;
                if (p < worst_sec) worst_sec = p;
                if (p > best_sec) best_sec = p;
            }
        }
        if (pngdir && *at) {
            char key[16];
            snprintf(key, sizeof key, "%d", l);
            const char *s = at;
            bool hit = false;
            while (*s) {
                const char *e = strchr(s, ',');
                size_t len = e ? (size_t)(e - s) : strlen(s);
                if (len == strlen(key) && !strncmp(s, key, len)) hit = true;
                s = e ? e + 1 : s + len;
            }
            if (hit) {
                char name[1024];
                tpcv_screen(&cv, px);
                snprintf(name, sizeof name, "%s/cv_%d.png", pngdir, l);
                cv_png(name, px);
                tpcv_ideal(&cv, px);
                snprintf(name, sizeof name, "%s/ideal_%d.png", pngdir, l);
                cv_png(name, px);
                static uint8_t m[224 * 256 * 3];
                tpcv_screen(&cv, px);
                if (run.shown && mame_rgb(run.shown->frame, m)) {
                    snprintf(name, sizeof name, "%s/mame_%d.png", pngdir, l);
                    png(name, 224, 256, m, 1);
                    snprintf(name, sizeof name, "%s/pair_%d.png", pngdir, l);
                    pair_png(name, m, px);
                }
                printf("list %d: frame %u, era %d, backlog %u\n", l, run.shown ? run.shown->frame : 0, cv.era, tpcv_backlog(&cv));
            }
        }
    }
    (void)t_late;
    if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, (uint32_t)(TPCV_RATE_Q8 >> 8), nsamples); fclose(wav); }
    if (stats) fclose(stats);
    if (out) fclose(out);
    if (fdump) fclose(fdump);
    const tpcv_stats_t *st = &cv.stats;
    printf("cart_host: %d lists, %u pictures (%.1f a second; seconds %u to %u), waited %u\n", lists, run.pictures,
           secs ? (double)sec_pics / secs * 59.62 / 60.0 : 0.0, worst_sec, best_sec, run.waited);
    printf("  bytes %llu (pattern %u, colour %u, refreshed %u), %.0f a list, %.0f a picture; runs %u; longest list %u bytes, %u T\n",
           (unsigned long long)bytes_sum, st->pat_bytes, st->col_bytes, st->refreshed, (double)bytes_sum / lists,
           run.pictures ? (double)bytes_sum / run.pictures : 0.0, st->runs, len_max, st->t_max);
    printf("  settled %u lists, tiles %u, cells %u, clipped samples %u, AY queue max %u, dropped %u\n", st->settled, st->tiles,
           st->cells / 8, st->clipped, tpay_queue_max, tpay_dropped);
    return 0;
}
