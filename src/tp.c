// license:BSD-3-Clause
// copyright-holders:Nicola Salmoria, Aaron Giles, O. Galibert
//
// tp.c -- the Time Pilot board as MAME 0.288 emulates it, in C for the cart:
//
//   timeplt.cpp (Nicola Salmoria): the memory map, the inputs, the NMI,
//     the LS259, the palette, the tiles and the sprites
//   timeplt_a.cpp (Nicola Salmoria): the sound board's map, its timer on
//     the AY's port B, the sound latch and IRQ, the filters' control
//   gen_latch.cpp, 74259.cpp, ay8910.cpp's register ports
//   schedule.cpp, diexec.cpp, screen.cpp (Aaron Giles), sound.cpp (O.
//     Galibert, Aaron Giles): MAME's timeslices (two CPUs and the 1/60 s
//     quantum), timers, input line events, the screen's timers and the
//     streams' 50 Hz update
//
// Times are MAME's attotimes as attoseconds from a whole second (epoch),
// moved on a second at a time between timeslices: the same sums MAME does
// on (seconds, attoseconds), in one 64-bit number. The scheduler is Donkey
// Kong's (Parrot Cart's dkong/dk.c) without its DMA. MAME's license
// (BSD-3-Clause) is in LICENSE (THIRD-PARTY.md).
#include "tp.h"

#include <string.h>

#include "tpz80.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define TP_HOT __attribute__((section(".time_critical.tp")))
#else
#define TP_HOT
#endif

// ---------------------------------------------------------------------------
// time

typedef int64_t at_t;                   // attoseconds since the epoch (epoch_s seconds)
#define AS_PER_SEC 1000000000000000000LL
#define AT_NEVER INT64_MAX
static uint32_t epoch_s;

// floor(a * f / 1e18) for 0 <= a < 1e18, without 128-bit sums
static inline uint64_t mul_frac(int64_t a, uint32_t f) {
	uint64_t ah = (uint64_t)a / 1000000000u, al = (uint64_t)a % 1000000000u;
	uint64_t x = ah * f, y = al * f;
	uint64_t q1 = x / 1000000000u, r1 = x % 1000000000u;
	return q1 + (r1 * 1000000000u + y) / (uint64_t)AS_PER_SEC;
}

static uint64_t at_ticks(at_t t, uint32_t f) {
	uint64_t s = epoch_s;
	while (t >= AS_PER_SEC) { t -= AS_PER_SEC; s++; }
	return s * f + mul_frac(t, f);
}

// the screen: set_refresh_hz(60), 256 x 256, lines 16-239 shown (screen.cpp's sums)
#define FRAME_AS (AS_PER_SEC / 60)              // HZ_TO_ATTOSECONDS(60)
#define VTOTAL 256
#define HTOTAL 256
#define SCAN_AS (FRAME_AS / VTOTAL)
#define PIX_AS (FRAME_AS / (VTOTAL * HTOTAL))
#define VBSTART 240                             // the visible area's bottom + 1
#define VBEND 16
#define VBLANK_AS (SCAN_AS * (VTOTAL - (VBSTART - VBEND)))
#define QUANTUM_AS FRAME_AS                     // the 60 Hz quantum (no other is set)
#define SND50_AS (AS_PER_SEC / 50)              // the sound manager's update
#define AY_RATE (TP_SND_CLOCK / 8)              // the AYs' streams (223,721 Hz)

static const tp_roms_t *roms;
tp_inputs_t tp_in = {0xff, 0xff, 0xff, TP_DSW0, TP_DSW1};
uint8_t tp_colorram[0x400], tp_videoram[0x400], tp_ram[0x800], tp_spriteram[2][0x100];
uint8_t tp_snd_ram[0x400];
uint8_t tp_latch;
uint8_t tp_soundlatch;
uint8_t tp_ay_regs[2][16];
uint32_t tp_frames;
void (*tp_on_vblank)(void);
void (*tp_on_line)(int line);
tp_spans_t tp_spans;
bool tp_video_on_frame;
const uint8_t *tp_proms;
static uint8_t ff_page[0x100];          // the main CPU's unmapped space reads 0xff (unmap_value_high)
static uint8_t zero_page[0x100];        // the sound ROM's region past tm7, and its unmapped reads: 0
static uint8_t sink[0x100];             // writes that go nowhere

// ---------------------------------------------------------------------------
// the scheduler (src/emu/schedule.cpp, src/emu/diexec.cpp)

enum { D_MAIN, D_SND, D_N };            // MAME's device order: maincpu, timeplt_audio:tpsound
#define SUSPEND_REASON_RESET 0x0001

typedef struct {
	at_t local;                         // m_localtime
	int64_t aspc;                       // m_attoseconds_per_cycle (and per clock: no divider)
	uint32_t cps;                       // m_cycles_per_second
	int divshift;
	uint32_t divisor;
	uint32_t inv;                       // 2^62 / divisor, for div_const
	uint64_t total;                     // m_totalcycles
	uint32_t suspend, nextsuspend;
} exec_t;

static exec_t ex[D_N];
static int executing = -1;
static bool suspend_changes_pending;
static at_t basetime;

static void exec_init(exec_t *e, uint32_t clock) {
	memset(e, 0, sizeof *e);
	e->cps = clock;
	e->aspc = AS_PER_SEC / clock;
	int64_t attos = e->aspc;
	e->divshift = 0;
	while (attos >= (1LL << 31)) { e->divshift++; attos >>= 1; }
	e->divisor = (uint32_t)attos;
	e->inv = (uint32_t)((1ull << 62) / e->divisor);
}

// MAME's divu_64x32(delta >> divshift, divisor), without the library's
// 64-bit divide (the M33 has none): x times 2^62 / d, one short at most,
// put right (the divisors here are over 2^30). Past 2^40, the divide.
static inline __attribute__((always_inline)) int div_const(uint64_t x, uint32_t d, uint32_t m) {
	if (__builtin_expect(x >> 40 != 0, 0)) return (int)(x / d);
	uint32_t xl = (uint32_t)x, xh = (uint32_t)(x >> 32);
	uint64_t t = (uint64_t)xh * m + (((uint64_t)xl * m) >> 32);
	uint32_t q = (uint32_t)(t >> 30);
	if (x - (uint64_t)q * d >= d) q++;
	return (int)q;
}

static inline int dev_done(int d) { return d == D_MAIN ? tpm_done() : tps_done(); }

static void abort_timeslice(void) {     // device_scheduler::abort_timeslice: the executing device's
	if (executing == D_MAIN) tpm_abort();
	else if (executing == D_SND) tps_abort();
}

static void apply_suspend_changes(void) {
	for (int d = 0; d < D_N; d++) ex[d].suspend = ex[d].nextsuspend;
	suspend_changes_pending = false;
}

// machine().time(): the timer's time in a callback, the running device's while it runs, else the base
static bool in_callback;
static at_t cb_expire;
static inline at_t now(void) {
	if (in_callback) return cb_expire;
	if (executing >= 0) return ex[executing].local + (at_t)dev_done(executing) * ex[executing].aspc;
	return basetime;
}

// ---- timers: a list in expiry order, ties first come first ---------------------------------
enum {
	TM_SND50,                           // the sound manager's streams update (made first)
	TM_VBSTART, TM_VBEND, TM_SCAN0,     // the screen's
	TM_SCANLINE,                        // its scanline timer (VIDEO_UPDATE_SCANLINE)
	TM_SYNC,                            // temporary timers (synchronize()) from here
	TM_COUNT = TM_SYNC + 24
};
enum { SY_NONE, SY_SOUNDLATCH, SY_INPUT };
enum { IN_MAIN_NMI, IN_SND_IRQ, IN_N };
#define CLEAR_LINE 0
#define ASSERT_LINE 1
#define HOLD_LINE 2

typedef struct {
	at_t expire, period;
	int8_t next;                        // the next in the list, -1 at its end
	bool enabled, listed, used;
	uint8_t type;
	int param;
} tmr_t;

static tmr_t tm[TM_COUNT];
static int tm_head = -1;
static int cb_timer = -1;
static bool cb_modified;

static void tm_remove(int i) {
	if (!tm[i].listed) return;
	if (tm_head == i) tm_head = tm[i].next;
	else {
		int p = tm_head;
		while (tm[p].next != i) p = tm[p].next;
		tm[p].next = tm[i].next;
	}
	tm[i].listed = false;
}

// timer_list_insert: before the first that expires later (a disabled or never one isn't listed)
static void tm_insert(int i) {
	tmr_t *t = &tm[i];
	if (!t->enabled || t->expire == AT_NEVER) return;
	int prev = -1, cur = tm_head;
	while (cur >= 0 && tm[cur].expire <= t->expire) { prev = cur; cur = tm[cur].next; }
	t->next = (int8_t)cur;
	if (prev < 0) tm_head = i;
	else tm[prev].next = (int8_t)i;
	t->listed = true;
}

static inline at_t head_expire(void) { return tm_head >= 0 ? tm[tm_head].expire : AT_NEVER; }

static void tm_adjust(int i, at_t delay, int param, at_t period) {
	tmr_t *t = &tm[i];
	if (in_callback && cb_timer == i) cb_modified = true;
	t->param = param;
	t->enabled = true;
	t->used = true;
	if (delay < 0) delay = 0;
	t->expire = delay == AT_NEVER ? AT_NEVER : now() + delay;
	t->period = period;
	tm_remove(i);
	tm_insert(i);
	if (tm_head == i) abort_timeslice();
}

static void synchronize(int type, int param) {
	for (int i = TM_SYNC; i < TM_COUNT; i++) {
		if (tm[i].used) continue;
		tm[i].type = (uint8_t)type;
		tm[i].used = true;
		tm[i].param = param;
		tm[i].period = AT_NEVER;
		tm[i].expire = now();
		tm[i].enabled = true;
		tm_insert(i);
		if (tm_head == i) abort_timeslice();
		return;
	}
}

// ---- input lines (device_input::set_state_synced): a batch of events, applied by a timer now
static struct { uint16_t q[32]; int n; uint8_t cur; } inq[IN_N];

static void input_apply(int line, int state) {
	inq[line].cur = (uint8_t)state;
	switch (line) {
	case IN_MAIN_NMI: tpm_set_nmi(state != CLEAR_LINE, ex[D_MAIN].total != 0); break;
	case IN_SND_IRQ: tps_set_irq(state != CLEAR_LINE); break;
	}
}

static void empty_event_queue(int line) {
	for (int i = 0; i < inq[line].n; i++) input_apply(line, inq[line].q[i] & 0xff);
	inq[line].n = 0;
}

static void set_input_line(int line, int state) {
	if (inq[line].n == (int)(sizeof inq[line].q / sizeof inq[line].q[0])) {    // full: applied now (MAME logs it)
		inq[line].n--;
		empty_event_queue(line);
	}
	inq[line].q[inq[line].n++] = (uint16_t)state;
	if (inq[line].n == 1) synchronize(SY_INPUT, line);
}

// the CPUs' acknowledge (standard_irq_callback): a held line is let go
uint8_t tpm_hook_irq_ack(void) { return 0xff; }
uint8_t tps_hook_irq_ack(void) {
	if (inq[IN_SND_IRQ].cur == HOLD_LINE) {
		tps_set_irq(0);
		inq[IN_SND_IRQ].cur = CLEAR_LINE;
	}
	return 0xff;                        // set_input_line_and_vector(0, HOLD_LINE, 0xff): IM 1 anyway
}

// ---------------------------------------------------------------------------
// the screen's timers (screen.cpp)

static at_t vblank_start_time;
static bool frame_done;

static at_t time_until_pos_at(int v, at_t t) {
	v += VTOTAL - VBSTART;
	v %= VTOTAL;
	int64_t target = (int64_t)v * SCAN_AS;
	int64_t cur = t - vblank_start_time;
	if (target <= cur + PIX_AS / 2) target += FRAME_AS;
	while (target <= cur) target += FRAME_AS;
	return target - cur;
}
static at_t time_until_pos(int v) { return time_until_pos_at(v, now()); }

static int vpos(void) {
	int64_t delta = now() - vblank_start_time;
	delta += PIX_AS / 2;
	int v = (int)(delta / SCAN_AS);
	return (VBSTART + v) % VTOTAL;
}

// ---- the sprites as each line saw them ----------------------------------------------
static tp_spans_t spans_cur;            // this frame's, so far
static bool spr_dirty = true;           // sprite RAM written since the last span was taken

static void span_take(int line) {
	if (spans_cur.n && !spr_dirty) return;
	spr_dirty = false;
	if (spans_cur.n == TP_SPANS) spans_cur.n--;     // (never seen: the last one is moved on)
	tp_span_t *s = &spans_cur.span[spans_cur.n++];
	s->line = (uint8_t)line;
	memcpy(s->s0, tp_spriteram[0] + 0x10, 0x30);
	memcpy(s->s1, tp_spriteram[1] + 0x10, 0x30);
}

static void cb_vbstart(void) {
	vblank_start_time = now();
	tp_frames++;                        // video().frame_update(): the picture is RAM as it was drawn
	frame_done = true;
	tp_spans = spans_cur;
	tp_video_on_frame = TP_VIDEO_ON;
	spans_cur.n = 0;
	spr_dirty = true;
	if (tp_on_vblank) tp_on_vblank();
	if (TP_NMI_ENABLE) set_input_line(IN_MAIN_NMI, ASSERT_LINE);    // screen_vblank -> vblank_irq
	tm_adjust(TM_VBSTART, time_until_pos(VBSTART), 0, AT_NEVER);
	tm_adjust(TM_VBEND, vblank_start_time + VBLANK_AS - now(), 0, AT_NEVER);
}

// scanline_tick: update_partial(line), then the next visible line
static void cb_scanline(int line) {
	span_take(line);
	if (tp_on_line) tp_on_line(line);
	line++;
	if (line > VBSTART - 1) line = VBEND;
	tm_adjust(TM_SCANLINE, time_until_pos(line), line, AT_NEVER);
}

// ---------------------------------------------------------------------------
// the sound board (timeplt_a.cpp)

uint64_t tp_ay_now(void) {
	at_t t = executing == D_SND ? ex[D_SND].local + (at_t)tps_done() * ex[D_SND].aspc : now();
	return at_ticks(t, AY_RATE) + 1;    // (sound_stream::update makes the samples to now's, including it)
}

static struct { bool active; uint8_t latch; } ay[2];

static void ay_address_w(int c, uint8_t d) {
	ay[c].active = (d >> 4) == 0;
	if (ay[c].active) ay[c].latch = d & 0x0f;
}

static void ay_data_w(int c, uint8_t d) {
	if (!ay[c].active) return;
	int r = ay[c].latch;
	tp_ay_regs[c][r] = d;
	tp_ay_write(tp_ay_now(), c, r, d);
}

// the AY's port B on the first: the sound CPU's clock / 512, then / 10
static uint8_t port_b(void) {
	static const uint8_t timer[10] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0};
	uint64_t total = ex[D_SND].total + (executing == D_SND ? (uint64_t)tps_done() : 0);
	return timer[(total / 512) % 10];
}

static uint8_t ay_data_r(int c) {
	static const uint8_t mask[16] = {0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff, 0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff};
	if (!ay[c].active) return 0xff;
	int r = ay[c].latch;
	if (c == 0 && r == 14) tp_ay_regs[0][14] = tp_soundlatch;     // port A: the sound latch
	if (c == 0 && r == 15) tp_ay_regs[0][15] = port_b();
	return tp_ay_regs[c][r] & mask[r];
}

uint8_t tps_hook_rd(uint16_t a) {
	switch (a >> 12) {
	case 4: return ay_data_r(0);
	case 6: return ay_data_r(1);
	default: return 0;                  // the AYs' address ports, the filters: unmapped
	}
}

void tps_hook_wr(uint16_t a, uint8_t d) {
	switch (a >> 12) {
	case 4: ay_data_w(0, d); return;
	case 5: ay_address_w(0, d); return;
	case 6: ay_data_w(1, d); return;
	case 7: ay_address_w(1, d); return;
	default:
		if (a & 0x8000) tp_ay_filter(tp_ay_now(), (uint16_t)(a & 0x0fff));    // the address is the data
		return;
	}
}

uint8_t tps_hook_in(uint16_t port) { (void)port; return 0; }
void tps_hook_out(uint16_t port, uint8_t d) { (void)port; (void)d; }

static uint8_t last_irq_state;

// ---------------------------------------------------------------------------
// the main board (timeplt.cpp)

// the LS259's outputs, each to its callback when it changes
static void latch_q(int bit, int state) {
	switch (bit) {
	case 0:                             // nmi_enable_w
		if (!state) set_input_line(IN_MAIN_NMI, CLEAR_LINE);
		break;
	case 2:                             // sh_irqtrigger_w: the sound CPU's IRQ on a rising edge
		if (last_irq_state == 0 && state) set_input_line(IN_SND_IRQ, HOLD_LINE);
		last_irq_state = (uint8_t)state;
		break;
	case 3:                             // mute_w (the LA4460's mute pin)
		tp_ay_mute(tp_ay_now(), state);
		break;
	default: break;                     // flip, video enable (read from tp_latch), the coin counters
	}
}

static void latch_write(int bit, int d) {
	if (((tp_latch >> bit) & 1) == d) return;
	tp_latch = (uint8_t)((tp_latch & ~(1u << bit)) | (unsigned)d << bit);
	latch_q(bit, d);
}

uint8_t tpm_hook_rd(uint16_t a) {
	if ((a & 0xf000) != 0xc000) return 0xff;
	switch (a & 0x300) {
	case 0x000: return (uint8_t)vpos();                 // scanline_r
	case 0x200: return tp_in.dsw1;
	case 0x300:
		switch (a & 0x60) {
		case 0x00: return tp_in.in0;
		case 0x20: return tp_in.in1;
		case 0x40: return tp_in.in2;
		default: return tp_in.dsw0;
		}
	default: return 0xff;
	}
}

void tpm_hook_wr(uint16_t a, uint8_t d) {
	if ((a & 0xf000) == 0xb000) {       // sprite RAM (its writes are watched for the lines' spans)
		tp_spriteram[(a >> 10) & 1][a & 0xff] = d;
		spr_dirty = true;
		return;
	}
	if ((a & 0xf000) != 0xc000) return;
	if (a >= 0xc300 && a <= 0xc30f) { latch_write((a & 0x0f) >> 1, d & 1); return; }
	switch (a & 0x300) {
	case 0x000: synchronize(SY_SOUNDLATCH, d); return;  // the sound latch (generic_latch_8::write)
	default: return;                                    // the watchdog (never set to fire), unmapped
	}
}

uint8_t tpm_hook_in(uint16_t port) { (void)port; return 0; }
void tpm_hook_out(uint16_t port, uint8_t d) { (void)port; (void)d; }

static void map_pages(void) {
	for (int p = 0; p < 256; p++) {
		tpm_rd_page[p] = ff_page;
		tpm_wr_page[p] = sink;
		if (p < 0x60) tpm_rd_page[p] = roms->maincpu + (p << 8);
		else if (p >= 0xa0 && p < 0xa4) tpm_rd_page[p] = tpm_wr_page[p] = tp_colorram + ((p - 0xa0) << 8);
		else if (p >= 0xa4 && p < 0xa8) tpm_rd_page[p] = tpm_wr_page[p] = tp_videoram + ((p - 0xa4) << 8);
		else if (p >= 0xa8 && p < 0xb0) tpm_rd_page[p] = tpm_wr_page[p] = tp_ram + ((p - 0xa8) << 8);
		else if (p >= 0xb0 && p < 0xc0) {
			tpm_rd_page[p] = tp_spriteram[(p >> 2) & 1];
			tpm_wr_page[p] = 0;
		} else if (p >= 0xc0 && p < 0xd0) tpm_rd_page[p] = 0, tpm_wr_page[p] = 0;
	}
	for (int p = 0; p < 256; p++) {
		tps_rd_page[p] = zero_page;
		tps_wr_page[p] = sink;
		if (p < 0x10) tps_rd_page[p] = roms->sound + (p << 8);
		else if (p >= 0x30 && p < 0x40) tps_rd_page[p] = tps_wr_page[p] = tp_snd_ram + ((p & 3) << 8);
		else if (p >= 0x40) tps_rd_page[p] = 0, tps_wr_page[p] = 0;
	}
	memset(ff_page, 0xff, sizeof ff_page);
}

uint8_t tp_peek(uint16_t a) {
	const uint8_t *p = tpm_rd_page[a >> 8];
	if (p) return p[a & 0xff];
	if ((a & 0xf000) == 0xc000 && (a & 0x300) == 0) return 0;  // (the line: not without a side effect's time)
	return tpm_hook_rd(a);
}

// ---------------------------------------------------------------------------
// video (timeplt.cpp's palette, tiles and sprites)

uint32_t tp_rgb[384];
uint16_t tp_tile_rows[512][8];
uint32_t tp_sprite_rows[256][16];

static void palette_init(void) {
	const uint8_t *p = roms->proms;
	uint32_t pal[32];
	for (int i = 0; i < 32; i++) {
		uint8_t lo = p[i], hi = p[32 + i];
		int r = 0x19 * ((hi >> 1) & 1) + 0x24 * ((hi >> 2) & 1) + 0x35 * ((hi >> 3) & 1) + 0x40 * ((hi >> 4) & 1) + 0x4d * ((hi >> 5) & 1);
		int g = 0x19 * ((hi >> 6) & 1) + 0x24 * ((hi >> 7) & 1) + 0x35 * ((lo >> 0) & 1) + 0x40 * ((lo >> 1) & 1) + 0x4d * ((lo >> 2) & 1);
		int b = 0x19 * ((lo >> 3) & 1) + 0x24 * ((lo >> 4) & 1) + 0x35 * ((lo >> 5) & 1) + 0x40 * ((lo >> 6) & 1) + 0x4d * ((lo >> 7) & 1);
		pal[i] = (uint32_t)(r << 16 | g << 8 | b);
	}
	for (int i = 0; i < 128; i++) tp_rgb[i] = pal[(p[0x140 + i] & 0x0f) + 0x10];    // the tiles: e12
	for (int i = 0; i < 256; i++) tp_rgb[128 + i] = pal[p[0x40 + i] & 0x0f];        // the sprites: e9
}

static void gfx_init(void) {
	for (int t = 0; t < 512; t++)
		for (int y = 0; y < 8; y++) {
			uint16_t w = 0;
			for (int x = 0; x < 8; x++) {
				uint8_t b = roms->tiles[t * 16 + (x >= 4 ? 8 : 0) + y];
				int k = x & 3;
				int pen = ((b >> (3 - k)) & 1) << 1 | ((b >> (7 - k)) & 1);
				w |= (uint16_t)(pen << (2 * x));
			}
			tp_tile_rows[t][y] = w;
		}
	for (int s = 0; s < 256; s++)
		for (int y = 0; y < 16; y++) {
			uint32_t w = 0;
			for (int x = 0; x < 16; x++) {
				uint8_t b = roms->sprites[s * 64 + 8 * (x >> 2) + (y & 7) + (y >= 8 ? 32 : 0)];
				int k = x & 3;
				int pen = ((b >> (3 - k)) & 1) << 1 | ((b >> (7 - k)) & 1);
				w |= (uint32_t)pen << (2 * x);
			}
			tp_sprite_rows[s][y] = w;
		}
}

// One line of screen_update: the tiles of category 0, the sprites (0x3e
// down to 0x10, so the lower entries are in front), then the tiles of
// category 1, all of them opaque. flip_screen flips the tilemap only.
static void tile_line(uint16_t *row, int line, int cat) {
	bool flip = TP_FLIP;
	for (int x = 0; x < TP_W; x++) {
		int tx = flip ? 255 - x : x, ty = flip ? 255 - line : line;
		int i = (ty >> 3) * 32 + (tx >> 3);
		uint8_t attr = tp_colorram[i];
		if (((attr >> 4) & 1) != cat) continue;
		int code = tp_videoram[i] + 8 * (attr & 0x20);
		int u = tx & 7, v = ty & 7;
		if (attr & 0x40) u = 7 - u;
		if (attr & 0x80) v = 7 - v;
		int pen = (tp_tile_rows[code][v] >> (2 * u)) & 3;
		row[x] = (uint16_t)((attr & 0x1f) * 4 + pen);
	}
}

void tp_render_line(uint16_t *row, int line) {
	if (!TP_VIDEO_ON) {
		memset(row, 0, TP_W * sizeof *row);         // (the bitmap as it was: MAME leaves it; black at first)
		return;
	}
	tile_line(row, line, 0);
	for (int offs = 0x3e; offs >= 0x10; offs -= 2) {
		int sx = tp_spriteram[0][offs];
		int sy = 241 - tp_spriteram[1][offs + 1];
		int code = tp_spriteram[0][offs + 1];
		int color = tp_spriteram[1][offs] & 0x3f;
		bool flipx = !(tp_spriteram[1][offs] & 0x40);
		bool flipy = tp_spriteram[1][offs] & 0x80;
		int v = line - sy;
		if (v < 0 || v > 15) continue;
		uint32_t bits = tp_sprite_rows[code][flipy ? 15 - v : v];
		for (int u = 0; u < 16; u++) {
			int x = sx + u;
			if (x > 255) break;
			int pen = (bits >> (2 * (flipx ? 15 - u : u))) & 3;
			if (pen) row[x] = (uint16_t)(128 + color * 4 + pen);
		}
	}
	tile_line(row, line, 1);
}

// ---------------------------------------------------------------------------
// the timers' callbacks, and the timeslices

static void cb_sync(int type, int param) {
	switch (type) {
	case SY_SOUNDLATCH: tp_soundlatch = (uint8_t)param; break;  // generic_latch_8::sync_callback
	case SY_INPUT: empty_event_queue(param); break;
	default: break;
	}
}

static TP_HOT void execute_timers(void) {
	while (tm_head >= 0 && tm[tm_head].expire <= basetime) {
		int h = tm_head;
		tmr_t *t = &tm[h];
		bool was_enabled = t->enabled;
		if (t->period == 0 || t->period == AT_NEVER) t->enabled = false;
		in_callback = true;
		cb_timer = h;
		cb_expire = t->expire;
		cb_modified = false;
		if (was_enabled) {
			switch (h) {
			case TM_SND50: break;                       // the streams' update: nothing to do here
			case TM_VBSTART: cb_vbstart(); break;
			case TM_VBEND: break;                       // vblank_irq(0): nothing
			case TM_SCAN0: tm_adjust(TM_SCAN0, time_until_pos(0), 0, AT_NEVER); break;
			case TM_SCANLINE: cb_scanline(t->param); break;
			default: cb_sync(t->type, t->param); break;
			}
		}
		if (!cb_modified) {
			if (h >= TM_SYNC) {
				tm_remove(h);
				t->used = false;
			} else {                            // schedule_next_period
				t->expire = t->period == AT_NEVER ? AT_NEVER : t->expire + t->period;
				tm_remove(h);
				tm_insert(h);
			}
		}
		in_callback = false;
		cb_timer = -1;
	}
}

static void rebase(void) {             // a second on: every time less 1e18
	epoch_s++;
	basetime -= AS_PER_SEC;
	for (int d = 0; d < D_N; d++) ex[d].local -= AS_PER_SEC;
	for (int i = 0; i < TM_COUNT; i++) if (tm[i].used && tm[i].expire != AT_NEVER) tm[i].expire -= AS_PER_SEC;
	vblank_start_time -= AS_PER_SEC;
}

// one device's part of a slice (the loop in device_scheduler::timeslice)
static inline __attribute__((always_inline)) void slice_dev(int d, at_t *target) {
	exec_t *e = &ex[d];
	if (e->suspend) return;
	int64_t delta = *target - e->local;
	if (delta < e->aspc) return;
	int cycles = div_const((uint64_t)delta >> e->divshift, e->divisor, e->inv);
	executing = d;
	int ran = d == D_MAIN ? tpm_run(cycles) : tps_run(cycles);
	executing = -1;
	e->total += (uint64_t)ran;
	e->local += (int64_t)ran * e->aspc;
	if (e->local < *target) *target = e->local > basetime ? e->local : basetime;
}

static TP_HOT void timeslice(void) {
	if (basetime >= AS_PER_SEC) rebase();
	at_t he;
	while (basetime < (he = head_expire())) {
		at_t target = basetime + QUANTUM_AS;
		if (he < target) target = he;
		if (suspend_changes_pending) apply_suspend_changes();
		slice_dev(D_MAIN, &target);
		slice_dev(D_SND, &target);
		basetime = target;
	}
	execute_timers();
}

// ---------------------------------------------------------------------------

uint64_t tp_cycles(void) { return ex[D_MAIN].total + (executing == D_MAIN ? (uint64_t)tpm_done() : 0); }
uint64_t tp_snd_cycles(void) { return ex[D_SND].total + (executing == D_SND ? (uint64_t)tps_done() : 0); }
uint64_t tp_time(void) { return at_ticks(basetime, TP_CLOCK); }

static void board_reset(void) {         // device_reset in MAME's order
	for (int d = 0; d < D_N; d++) {     // interface_pre_reset: total cycles to 0, resumed
		ex[d].total = 0;
		ex[d].nextsuspend = 0;
		suspend_changes_pending = true;
	}
	tpm_reset();
	inq[IN_MAIN_NMI].n = 0;             // interface_post_reset: the input lines
	inq[IN_MAIN_NMI].cur = CLEAR_LINE;
	// the LS259: every output cleared (its start left them 0xff), each to its callback
	for (int b = 0; b < 8; b++) latch_write(b, 0);
	tps_reset();
	inq[IN_SND_IRQ].n = 0;
	inq[IN_SND_IRQ].cur = CLEAR_LINE;
	last_irq_state = 0;
	ay[0].active = ay[1].active = false;
	ay[0].latch = ay[1].latch = 0;
	memset(tp_ay_regs, 0, sizeof tp_ay_regs);     // (tpay.c resets its own: ay8910_reset_ym)
}

void tp_init(const tp_roms_t *r) {
	roms = r;
	tp_proms = r->proms;
	memset(tm, 0, sizeof tm);
	tm_head = -1;
	epoch_s = 0;
	basetime = 0;
	executing = -1;
	in_callback = false;
	cb_timer = -1;
	exec_init(&ex[D_MAIN], TP_CLOCK);
	exec_init(&ex[D_SND], TP_SND_CLOCK);
	for (int d = 0; d < D_N; d++) ex[d].suspend = ex[d].nextsuspend = SUSPEND_REASON_RESET;
	memset(tp_colorram, 0, sizeof tp_colorram);
	memset(tp_videoram, 0, sizeof tp_videoram);
	memset(tp_ram, 0, sizeof tp_ram);
	memset(tp_spriteram, 0, sizeof tp_spriteram);
	memset(tp_snd_ram, 0, sizeof tp_snd_ram);
	memset(inq, 0, sizeof inq);
	tp_latch = 0xff;                    // the LS259's start
	tp_soundlatch = 0;
	tp_frames = 0;
	vblank_start_time = 0;
	spans_cur.n = 0;
	tp_spans.n = 0;
	spr_dirty = true;
	palette_init();
	gfx_init();
	map_pages();

	// the sound manager's 50 Hz timer, made before the devices start
	tm_adjust(TM_SND50, SND50_AS, 0, SND50_AS);
	// device_start, in MAME's order: the CPU, the screen's timers, the sound CPU
	tpm_start();
	tm_adjust(TM_VBSTART, time_until_pos(VBSTART), 0, AT_NEVER);
	tm_adjust(TM_SCAN0, time_until_pos(0), 0, AT_NEVER);
	tm_adjust(TM_SCANLINE, time_until_pos(0), 0, AT_NEVER);
	tps_start();
	board_reset();
}

// Run while tp_time() < until: until's cycle as the first time it is
// reached, ceil(until * 1e18 / TP_CLOCK) attoseconds, in seconds and the
// rest (sums of 64 bits, made once: tp_time()'s divides each timeslice
// cost the M33 5% of a list)
uint32_t tp_run_to(uint64_t until) {
	uint32_t f0 = tp_frames;
	const uint64_t q = (uint64_t)AS_PER_SEC / TP_CLOCK, r = (uint64_t)AS_PER_SEC % TP_CLOCK;
	const uint64_t sec = until / TP_CLOCK, rem = until % TP_CLOCK;
	const at_t thr = (at_t)(rem * q + (rem * r + TP_CLOCK - 1) / TP_CLOCK);
	for (;;) {
		int64_t ds = (int64_t)sec - (int64_t)epoch_s;
		if (ds < 0 || (ds <= 8 && basetime >= ds * AS_PER_SEC + thr)) break;
		timeslice();
	}
	return tp_frames - f0;
}

void tp_frame(void) {
	frame_done = false;
	while (!frame_done) timeslice();
}
