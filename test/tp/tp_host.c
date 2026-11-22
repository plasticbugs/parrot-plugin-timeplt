// tp_host.c -- the Time Pilot board model on the Mac, for the tests against MAME.
//
//   tp_host ROMDIR [-frames N] [-script FILE] [-dump FILE] [-trace FILE]
//           [-strace FILE] [-pix FILE -pixevery N] [-wav FILE] [-aylog FILE]
//           [-bot FROM -record FILE]
//
// ROMDIR holds the set's files, unzipped (test/tp/setup.sh makes one under
// $TMPDIR). -dump writes the records tpdump.lua writes in MAME; -trace and
// -strace the main and the sound CPU's traces in the formats trace.sh asks
// MAME's debugger for; -pix the pictures as MAME draws them (a line at a
// time); -wav the sound at 48 kHz (tpay.c a tick at a time, as MAME's
// streams, each output's ticks averaged to 48 kHz and filtered there);
// -aylog each AY write (frame, tick, chip, register, value). -bot plays
// from frame FROM on (it turns toward the nearest enemy and fires), and
// -record writes what it pressed as a script, for MAME to play the same.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tp.h"
#include "tpay.h"
#include "tpz80.h"

static uint8_t maincpu[0x6000], sound[0x1000], tiles[0x2000], sprites[0x4000], proms[0x240];

static void load(const char *dir, const char *name, uint8_t *dst, size_t size) {
	char path[1024];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "rb");
	if (!f || fread(dst, 1, size, f) != size) { fprintf(stderr, "tp_host: can't read %s\n", path); exit(1); }
	fclose(f);
}

static FILE *trace, *strace, *aylog;
static int cur_frame;

static void minsn(void) {
	z80_regs_t r;
	tpm_regs(&r);
	fprintf(trace, "@ %04X %04X %04X %04X %04X %04X %04X %04X %llu\n", r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy,
	        (unsigned long long)tp_cycles());
}

static void sinsn(void) {
	z80_regs_t r;
	tps_regs(&r);
	fprintf(strace, "@ %04X %04X %04X %04X %04X %04X %04X %04X %llu\n", r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy,
	        (unsigned long long)tp_snd_cycles());
}

static void wav_header(FILE *f, uint32_t rate, uint32_t n) {
	uint32_t bytes = n * 2;
	fwrite("RIFF", 1, 4, f);
	uint32_t v = 36 + bytes; fwrite(&v, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	v = 16; fwrite(&v, 4, 1, f);
	uint16_t h = 1; fwrite(&h, 2, 1, f);
	h = 1; fwrite(&h, 2, 1, f);
	fwrite(&rate, 4, 1, f);
	v = rate * 2; fwrite(&v, 4, 1, f);
	h = 2; fwrite(&h, 2, 1, f);
	h = 16; fwrite(&h, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&bytes, 4, 1, f);
}

// the picture a line at a time, as MAME's partial updates draw it; MAME's
// screen:pixels() in frame_done reads the bitmap finished at the VBLANK
// before (the screen keeps two and has just swapped them)
static uint16_t fb[TP_H][TP_W], done[TP_H][TP_W], shown[TP_H][TP_W];
static void on_line(int line) {
	if (line >= 16 && line < 240) tp_render_line(fb[line - 16], line);
}
static void on_vblank(void) {
	memcpy(shown, done, sizeof fb);
	memcpy(done, fb, sizeof fb);
}

// the sound at 48 kHz, as MAME makes it: each output's ticks to 48 kHz,
// times 0.6 into its filter (at 48 kHz), the six summed, muted by the chunk
static FILE *wav;
static uint32_t nsamples;
static double fmem[6];
static void sound_to(uint64_t tick) {
	static double acc[6];
	static int nacc;
	static uint64_t next_edge;
	if (!next_edge) next_edge = TPAY_RATE / 48000 + 1;
	while (tpay.tick < tick) {
		float o[6];
		tpay_ref_tick(o);
		for (int i = 0; i < 6; i++) acc[i] += o[i];
		nacc++;
		// a 48 kHz sample ends where tick * 48000 / rate moves on
		if ((tpay.tick * 48000) / TPAY_RATE != ((tpay.tick - 1) * 48000) / TPAY_RATE) {
			double sum = 0;
			for (int i = 0; i < 6; i++) {
				double x = acc[i] / nacc * 0.60;
				double k = tpay_filter_k(i, 48000);
				if (k >= 1.0) fmem[i] = 0, sum += x;
				else { fmem[i] += (x - fmem[i]) * k; sum += fmem[i]; }
				acc[i] = 0;
			}
			nacc = 0;
			if (tpay.mute) sum = 0;
			double s = sum * 32768.0;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			int16_t v = (int16_t)lrint(s);
			if (wav) fwrite(&v, 2, 1, wav);
			nsamples++;
		}
	}
}

// the AY log: each write as the board makes it
static void log_write(uint64_t tick, int chip, int reg, uint8_t value) {
	if (aylog) fprintf(aylog, "%d %llu %d %d %02x\n", cur_frame, (unsigned long long)tick, chip, reg, value);
}

// the bot: the stick toward the nearest enemy (as the screen shows it,
// turned), fire pressed and let go every 4 frames
static uint8_t bot(int n) {
	const uint8_t *s0 = tp_spriteram[0], *s1 = tp_spriteram[1];
	int px = s0[0x16] + 8, py = 241 - s1[0x17] + 8;
	int best = 1 << 30, dx = 0, dy = 0;
	for (int offs = 0x24; offs <= 0x34; offs += 2) {
		if (!s0[offs + 1]) continue;
		int col = s1[offs] & 0x3f;
		if (col == 12 || col == 44) continue;
		int ex = s0[offs] + 8, ey = 241 - s1[offs + 1] + 8;
		int ddx = -(ey - py), ddy = ex - px;        // on the screen (the raw picture turned)
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

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: tp_host ROMDIR [options]\n"); return 2; }
	const char *dir = argv[1], *script = NULL, *dumpname = NULL, *pixname = NULL, *wavname = NULL;
	int frames = 600, pixevery = 0, botfrom = 0;
	FILE *record = NULL;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-frames")) frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-script")) script = argv[++i];
		else if (!strcmp(argv[i], "-dump")) dumpname = argv[++i];
		else if (!strcmp(argv[i], "-trace")) trace = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-strace")) strace = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-pix")) pixname = argv[++i];
		else if (!strcmp(argv[i], "-pixevery")) pixevery = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-wav")) wavname = argv[++i];
		else if (!strcmp(argv[i], "-aylog")) aylog = fopen(argv[++i], "w");
		else if (!strcmp(argv[i], "-bot")) botfrom = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-record")) record = fopen(argv[++i], "w");
		else { fprintf(stderr, "tp_host: what is %s?\n", argv[i]); return 2; }
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
	tp_roms_t roms = { maincpu, sound, tiles, sprites, proms };
	static struct { int frame; char port[4]; int v; } ev[16384];
	int nev = 0;
	if (script) {
		FILE *f = fopen(script, "r");
		if (!f) { fprintf(stderr, "tp_host: can't read %s\n", script); return 1; }
		char line[256];
		while (fgets(line, sizeof line, f) && nev < 16384)
			if (sscanf(line, "%d %3s %x", &ev[nev].frame, ev[nev].port, &ev[nev].v) == 3) nev++;
		fclose(f);
	}
	if (trace) tpm_trace = minsn;
	if (strace) tps_trace = sinsn;
	tpay_reset();
	tp_init(&roms);
	if (pixname) { tp_on_line = on_line; tp_on_vblank = on_vblank; }
	FILE *dump = dumpname ? fopen(dumpname, "wb") : NULL;
	FILE *pix = pixname ? fopen(pixname, "wb") : NULL;
	wav = wavname ? fopen(wavname, "wb") : NULL;
	if (wav) wav_header(wav, 48000, 0);
	if (aylog) tpay_on_write = log_write;
	for (int n = 1; n <= frames; n++) {
		cur_frame = n;
		tp_frame();
		if (pix && pixevery > 0 && n % pixevery == 0) {
			uint32_t fn = (uint32_t)n;
			uint16_t wh[2] = { TP_W, TP_H };
			fwrite(&fn, 4, 1, pix);
			fwrite(wh, 2, 2, pix);
			for (int y = 0; y < TP_H; y++)
				for (int x = 0; x < TP_W; x++) {
					uint32_t c = tp_rgb[shown[y][x]];
					uint8_t px[4] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16), 0xff };   // B G R A
					fwrite(px, 1, 4, pix);
				}
		}
		for (int i = 0; i < nev; i++) {
			if (ev[i].frame != n) continue;
			uint8_t v = (uint8_t)~ev[i].v;          // (the script's 1 is pressed: the ports are active low)
			if (!strcmp(ev[i].port, "in0")) tp_in.in0 = v;
			if (!strcmp(ev[i].port, "in1")) tp_in.in1 = v;
			if (record) fprintf(record, "%d %s %02X\n", n, ev[i].port, ev[i].v);
		}
		if (botfrom && n >= botfrom) {
			uint8_t v = bot(n);
			if ((uint8_t)~v != tp_in.in1) {
				tp_in.in1 = (uint8_t)~v;
				if (record) fprintf(record, "%d in1 %02X\n", n, v);
			}
		}
		if (dump) {
			static uint8_t rec[8 + 0x400 + 0x400 + 0x800 + 0x200 + 32 + 32 + 0x400];
			uint8_t *p = rec;
			memcpy(p, "TPFR", 4); p += 4;
			uint32_t fn = (uint32_t)n; memcpy(p, &fn, 4); p += 4;
			memcpy(p, tp_colorram, 0x400); p += 0x400;
			memcpy(p, tp_videoram, 0x400); p += 0x400;
			memcpy(p, tp_ram, 0x800); p += 0x800;
			memcpy(p, tp_spriteram[0], 0x100); p += 0x100;
			memcpy(p, tp_spriteram[1], 0x100); p += 0x100;
			for (int c = 0; c < 2; c++) {
				z80_regs_t r;
				if (c == 0) tpm_regs(&r); else tps_regs(&r);
				uint16_t w[13] = { r.pc, r.sp, r.af, r.bc, r.de, r.hl, r.ix, r.iy, r.af2, r.bc2, r.de2, r.hl2, r.wz };
				memcpy(p, w, 26); p += 26;
				*p++ = r.r; *p++ = r.i; *p++ = r.im; *p++ = r.iff1; *p++ = r.iff2; *p++ = r.halt;
			}
			memcpy(p, tp_snd_ram, 0x400); p += 0x400;
			fwrite(rec, 1, (size_t)(p - rec), dump);
		}
		sound_to(tp_ay_now());
	}
	if (dump) fclose(dump);
	if (pix) fclose(pix);
	if (wav) { fseek(wav, 0, SEEK_SET); wav_header(wav, 48000, nsamples); fclose(wav); }
	if (trace) fclose(trace);
	if (strace) fclose(strace);
	if (aylog) fclose(aylog);
	if (record) fclose(record);
	fprintf(stderr, "tp_host: %d frames, %llu main cycles, %llu sound cycles, AY queue max %u, dropped %u\n", frames,
	        (unsigned long long)tp_cycles(), (unsigned long long)tp_snd_cycles(), tpay_queue_max, tpay_dropped);
	return 0;
}
