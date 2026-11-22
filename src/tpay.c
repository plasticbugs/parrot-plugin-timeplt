// license:BSD-3-Clause
// copyright-holders:Couriersud
//
// tpay.c -- see tpay.h. MAME 0.288's ay8910.cpp (Couriersud) for the
// AY-3-8910 as Time Pilot has it: no expanded mode, one envelope, the
// legacy output tables, three outputs. MAME's license is in LICENSE (THIRD-PARTY.md).
#include "tpay.h"

#include <math.h>
#include <string.h>

#include "tp.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define AY_HOT __attribute__((section(".time_critical.tpay")))
#else
#define AY_HOT
#endif

tpay_t tpay;
float tpay_vol_table[16], tpay_env_table[16];
uint32_t tpay_queue_max, tpay_dropped;
void (*tpay_on_write)(uint64_t tick, int chip, int reg, uint8_t value);

// ---------------------------------------------------------------------------
// the queue the board fills

enum { EV_REG, EV_FILTER, EV_MUTE };
typedef struct {
	uint64_t tick;
	uint8_t kind, chip, reg, val;
	uint16_t bits;
} ev_t;
#define QN 256                          // (a power of 2; the games fill 43 at most: tpay_queue_max)
static ev_t q[QN];
static uint32_t qhead, qtail;           // taken from head, put at tail

static void put(const ev_t *e) {
	if (qtail - qhead == QN) { tpay_dropped++; return; }
	q[qtail++ & (QN - 1)] = *e;
	if (qtail - qhead > tpay_queue_max) tpay_queue_max = qtail - qhead;
}

int tpay_queued(void) { return (int)(qtail - qhead); }

// MAME updates the stream only for a changed value (or the shape), and the
// same value written again changes nothing else: those aren't queued
static uint8_t shadow[2][16];

void tp_ay_write(uint64_t tick, int chip, int reg, uint8_t value) {
	if (reg != 13 && shadow[chip][reg] == value) return;
	shadow[chip][reg] = value;
	if (reg >= 14) return;              // the ports (inputs here): no sound
	if (tpay_on_write) tpay_on_write(tick, chip, reg, value);
	ev_t e = { tick, EV_REG, (uint8_t)chip, (uint8_t)reg, value, 0 };
	put(&e);
}

void tp_ay_filter(uint64_t tick, uint16_t bits) {
	ev_t e = { tick, EV_FILTER, 0, 0, 0, bits };
	put(&e);
}

void tp_ay_mute(uint64_t tick, int on) {
	ev_t e = { tick, EV_MUTE, 0, 0, (uint8_t)(on != 0), 0 };
	put(&e);
}

// ---------------------------------------------------------------------------
// the chip

// build_single_table (ay8910_param: AY8910, 1k load, normalized as
// AY8910_LEGACY_OUTPUT has it)
static void build_table(float *tab, int zero_is_off) {
	static const double res[16] = { 15950, 15350, 15090, 14760, 14275, 13620, 12890, 11370,
	                                10600, 8590, 7190, 5985, 4820, 3945, 3017, 2345 };
	const double r_up = 800000, r_down = 8000000, rl = 1000;
	double temp[16], mn = 10.0, mx = 0.0;
	for (int j = 0; j < 16; j++) {
		double rt = 1.0 / r_down + 1.0 / rl;
		double rw = 1.0 / res[j];
		rt += 1.0 / res[j];
		if (!(zero_is_off && j == 0)) {
			rw += 1.0 / r_up;
			rt += 1.0 / r_up;
		}
		temp[j] = rw / rt;
		if (temp[j] < mn) mn = temp[j];
		if (temp[j] > mx) mx = temp[j];
	}
	for (int j = 0; j < 16; j++) tab[j] = (float)(1.0 * (((temp[j] - mn) / (mx - mn)) - 0.25) * 0.5);
}

static void set_shape(tpay_chip_t *c, uint8_t shape) {
	c->eatt = (shape & 0x04) ? 0x0f : 0x00;
	if ((shape & 0x08) == 0) {
		c->ehold = 1;
		c->ealt = c->eatt;
	} else {
		c->ehold = shape & 0x01;
		c->ealt = shape & 0x02;
	}
	c->estep = 0x0f;
	c->eholding = 0;
	c->evol = (uint8_t)(c->estep ^ c->eatt);
}

static void noise_catch_up(tpay_chip_t *c);
static void write_reg(tpay_chip_t *c, int r, uint8_t v) {
	if (r == 6 || r == 7) noise_catch_up(c);
	c->regs[r] = v;
	switch (r) {
	case 0: case 1: c->tper[0] = (uint16_t)(c->regs[0] | (c->regs[1] & 0x0f) << 8); break;
	case 2: case 3: c->tper[1] = (uint16_t)(c->regs[2] | (c->regs[3] & 0x0f) << 8); break;
	case 4: case 5: c->tper[2] = (uint16_t)(c->regs[4] | (c->regs[5] & 0x0f) << 8); break;
	case 8: case 9: case 10: c->tvol[r - 8] = v; break;
	case 11: case 12: c->eper = (uint32_t)(c->regs[11] | c->regs[12] << 8); break;
	case 13: set_shape(c, v); break;
	default: break;
	}
}

static void chip_reset(tpay_chip_t *c) {
	memset(c, 0, sizeof *c);
	c->rng = 1;
	for (int i = 0; i < 14; i++) write_reg(c, i, 0);
}

void tpay_reset(void) {
	build_table(tpay_vol_table, 1);
	build_table(tpay_env_table, 0);
	chip_reset(&tpay.chip[0]);
	chip_reset(&tpay.chip[1]);
	tpay.filter = 0;
	tpay.mute = false;
	tpay.tick = 0;
	qhead = qtail = 0;
	memset(shadow, 0, sizeof shadow);
}

// the queue's events due by tick t (those at t take effect for it)
static AY_HOT void apply_due(uint64_t t) {
	while (qhead != qtail && q[qhead & (QN - 1)].tick <= t) {
		ev_t *e = &q[qhead++ & (QN - 1)];
		switch (e->kind) {
		case EV_REG: write_reg(&tpay.chip[e->chip], e->reg, e->val); break;
		case EV_FILTER: tpay.filter = e->bits; break;
		default: tpay.mute = e->val; break;
		}
	}
}

// the next event's tick (after the due ones), or never
static inline uint64_t next_due(void) { return qhead != qtail ? q[qhead & (QN - 1)].tick : UINT64_MAX; }

// ---- one tick, as sound_stream_update's loop has it ----------------------------------
static void chip_tick(tpay_chip_t *c, float *out) {
	for (int ch = 0; ch < 3; ch++) {
		int period = c->tper[ch] < 1 ? 1 : c->tper[ch];
		c->tcount[ch]++;
		while (c->tcount[ch] >= period) {
			c->tout[ch] ^= 1;
			c->tcount[ch] -= period;
		}
	}
	if (++c->ncount >= (c->regs[6] & 0x1f)) {
		c->ncount = 0;
		c->npre ^= 1;
		if (!c->npre) c->rng = (c->rng >> 1) | (((c->rng ^ (c->rng >> 3)) & 1) << 16);
	}
	int noise = c->rng & 1;
	uint8_t en = c->regs[7];
	if (!c->eholding) {
		uint32_t period = c->eper * 2;
		if ((uint32_t)(++c->ecount) >= period) {
			c->ecount = 0;
			c->estep--;
			if (c->estep < 0) {
				if (c->ehold) {
					if (c->ealt) c->eatt ^= 0x0f;
					c->eholding = 1;
					c->estep = 0;
				} else {
					if (c->ealt && (c->estep & 0x10)) c->eatt ^= 0x0f;
					c->estep &= 0x0f;
				}
			}
		}
	}
	c->evol = (uint8_t)(c->estep ^ c->eatt);
	for (int ch = 0; ch < 3; ch++) {
		int on = (c->tout[ch] | ((en >> ch) & 1)) & (noise | ((en >> (3 + ch)) & 1));
		if (c->tvol[ch] & 0x10) out[ch] = tpay_env_table[on ? c->evol : 0];
		else out[ch] = tpay_vol_table[on ? c->tvol[ch] & 0x0f : 0];
	}
}

void tpay_ref_tick(float out[6]) {
	apply_due(tpay.tick);
	chip_tick(&tpay.chip[0], out);
	chip_tick(&tpay.chip[1], out + 3);
	tpay.tick++;
}

// ---- a run of ticks at once ---------------------------------------------------------

// a tone over n ticks: the ticks its output is high, the state moved on.
// MAME's loop a tick at a time (count up, a toggle each time the count
// reaches the period) summed in closed form: after the first tick, the
// count runs on from cnt to cnt + n; the ticks before the first toggle
// keep the output, then each period's ticks have it the other way, the
// last period cut short
static inline AY_HOT int tone_run(tpay_chip_t *c, int ch, int n) {
	int32_t p = c->tper[ch] < 1 ? 1 : c->tper[ch];
	int32_t cnt = c->tcount[ch] + 1;
	int o = c->tout[ch];
	if (cnt >= p) {                     // (after a shorter period was set, it can be well past)
		int32_t t = cnt / p;
		cnt -= t * p;
		o ^= t & 1;
	}
	int32_t high = o;
	n--;
	int32_t T = cnt + n, K = T / p;
	if (K == 0) {
		high += o * n;
		cnt = T;
	} else {
		int32_t odd = K / 2, even = K - 1 - K / 2;
		high += o * (p - 1 - cnt) + p * (odd * (o ^ 1) + even * o) + (T - K * p + 1) * (o ^ (K & 1));
		o ^= K & 1;
		cnt = T - K * p;
	}
	c->tcount[ch] = cnt;
	c->tout[ch] = (uint8_t)o;
	return (int)high;
}

// the noise over n ticks: the ticks it is high
static inline AY_HOT int noise_run(tpay_chip_t *c, int n) {
	int np = c->regs[6] & 0x1f;
	if (np < 1) np = 1;
	int high = 0;
	int cnt = c->ncount;
	while (n > 0) {
		// the ticks to the next count that fires (the counter may be past a shorter period)
		int until = cnt + 1 >= np ? 1 : np - cnt;
		if (until > n) { high += (int)(c->rng & 1) * n; cnt += n; break; }
		high += (int)(c->rng & 1) * (until - 1);
		cnt = 0;
		c->npre ^= 1;
		if (!c->npre) c->rng = (c->rng >> 1) | (((c->rng ^ (c->rng >> 3)) & 1) << 16);
		high += (int)(c->rng & 1);
		n -= until;
	}
	c->ncount = (uint8_t)cnt;
	return high;
}

// the ticks the noise was left to run, run (before it is heard or its
// period changes); its state repeats every 2 x 131,071 counts of its period
static AY_HOT void noise_catch_up(tpay_chip_t *c) {
	uint32_t n = c->npend;
	if (!n) return;
	c->npend = 0;
	int np = c->regs[6] & 0x1f;
	if (np < 1) np = 1;
	uint32_t first = n < 64u ? n : 64u;          // (puts a count past a shorter period right)
	noise_run(c, (int)first);
	n -= first;
	n %= 2u * 131071u * (uint32_t)np;
	while (n) {
		uint32_t k = n > 0x40000000u ? 0x40000000u : n;
		noise_run(c, (int)k);
		n -= k;
	}
}

// the envelope over n ticks: the sum of env_table over its volume at each tick
static inline AY_HOT float env_run(tpay_chip_t *c, int n) {
	float sum = 0;
	while (n > 0) {
		if (c->eholding) { sum += tpay_env_table[c->evol] * (float)n; break; }
		uint32_t period = c->eper * 2;
		uint32_t cnt = (uint32_t)c->ecount + 1;
		int until = cnt >= period ? 1 : (int)(period - cnt) + 1;
		if (until > n) { sum += tpay_env_table[c->evol] * (float)n; c->ecount += n; break; }
		sum += tpay_env_table[c->evol] * (float)(until - 1);
		c->ecount = 0;
		c->estep--;
		if (c->estep < 0) {
			if (c->ehold) {
				if (c->ealt) c->eatt ^= 0x0f;
				c->eholding = 1;
				c->estep = 0;
			} else {
				if (c->ealt && (c->estep & 0x10)) c->eatt ^= 0x0f;
				c->estep &= 0x0f;
			}
		}
		c->evol = (uint8_t)(c->estep ^ c->eatt);
		sum += tpay_env_table[c->evol];
		n -= until;
	}
	return sum;
}

static AY_HOT void chip_run(tpay_chip_t *c, int n, float *sum) {
	uint8_t en = c->regs[7];
	int th[3];
	for (int ch = 0; ch < 3; ch++) th[ch] = tone_run(c, ch, n);
	// the noise only when a channel has it on (Time Pilot never does); its
	// ticks are kept till then, and run in one go (noise_catch_up)
	int nh = 0;
	if ((en & 0x38) != 0x38) {
		noise_catch_up(c);
		nh = noise_run(c, n);
	} else {
		c->npend += (uint32_t)n;
	}
	bool env = (c->tvol[0] | c->tvol[1] | c->tvol[2]) & 0x10;
	float esum = env ? env_run(c, n) : 0;
	if (!env && !c->eholding) {         // (the envelope runs on unheard: kept in step)
		uint32_t period = c->eper * 2;
		if (period > (uint32_t)c->ecount + (uint32_t)n) c->ecount += n;
		else env_run(c, n);
	}
	float fn = (float)n;
	for (int ch = 0; ch < 3; ch++) {
		bool toff = (en >> ch) & 1, noff = (en >> (3 + ch)) & 1;
		float on;                       // the ticks the output is on
		if (toff && noff) on = fn;
		else if (noff) on = (float)th[ch];
		else if (toff) on = (float)nh;
		else on = (float)th[ch] * (float)nh / fn;
		if (c->tvol[ch] & 0x10) sum[ch] = tpay_env_table[0] * (fn - on) + esum * on / fn;
		else sum[ch] = tpay_vol_table[0] * (fn - on) + tpay_vol_table[c->tvol[ch] & 0x0f] * on;
	}
}

void AY_HOT tpay_mean(int n, float out[6]) {
	float acc[6] = { 0, 0, 0, 0, 0, 0 };
	int left = n;
	apply_due(tpay.tick);
	while (left > 0) {
		uint64_t nd = next_due();
		int run = left;
		if (nd != UINT64_MAX && nd < tpay.tick + (uint64_t)left) run = (int)(nd - tpay.tick);
		if (run > 0) {
			float s[6];
			chip_run(&tpay.chip[0], run, s);
			chip_run(&tpay.chip[1], run, s + 3);
			for (int i = 0; i < 6; i++) acc[i] += s[i];
			tpay.tick += (uint64_t)run;
			left -= run;
		}
		apply_due(tpay.tick);
	}
	float inv = 1.0f / (float)n;
	for (int i = 0; i < 6; i++) out[i] = acc[i] * inv;
}

float tpay_filter_k(int ch, uint32_t rate) {
	int shift = ch < 3 ? 6 + 2 * ch : 2 * (ch - 3);
	int d = (tpay.filter >> shift) & 3;
	double c = (d & 1 ? 220000e-12 : 0) + (d & 2 ? 47000e-12 : 0);
	if (c == 0) return 1.0f;
	double req = (1000.0 * (5100.0 + 0.0)) / (1000.0 + 5100.0 + 0.0);
	return (float)(1.0 - exp(-1.0 / (req * c) / (double)rate));
}
