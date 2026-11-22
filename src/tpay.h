// license:BSD-3-Clause
// copyright-holders:Couriersud
//
// tpay.h -- Time Pilot's sound: two AY-3-8910s as MAME 0.288's ay8910.cpp
// makes them (legacy output levels, three outputs each), and the board's
// RC filter on each of the six outputs (flt_rc.cpp, LOWPASS_3R, 1k, 5.1k).
//
// The board (tp.c) queues each register write, filter change and mute
// with the AY tick it happens at (the AYs' stream rate, 223,721 a
// second). The sound is then made from the queue a stretch at a time:
// tpay_ref_ticks one tick at a time as MAME does (the tests), or
// tpay_mean the mean of each output over a run of ticks (the cart).
#ifndef TPAY_H
#define TPAY_H

#include <stdbool.h>
#include <stdint.h>

#define TPAY_RATE 223721u               // the sound CPU's clock / 8 (1,789,772 / 8)

typedef struct {
	uint8_t regs[16];
	int32_t tcount[3];
	uint16_t tper[3];
	uint8_t tout[3];
	uint8_t tvol[3];
	uint32_t eper;
	int32_t ecount;
	int8_t estep;
	uint8_t evol, ehold, ealt, eatt, eholding;
	uint8_t ncount, npre;
	uint32_t rng;
	uint32_t npend;                     // ticks the noise is yet to run (tpay_mean: none of its channels hears it)
} tpay_chip_t;

typedef struct {
	tpay_chip_t chip[2];
	uint16_t filter;                    // the filter control's 12 bits (2 a channel: AY2 A B C, then AY1 A B C)
	bool mute;
	uint64_t tick;                      // the next tick to make
} tpay_t;

extern tpay_t tpay;
extern float tpay_vol_table[16], tpay_env_table[16];  // an output's level for each volume (MAME's tables)

void tpay_reset(void);                  // power on (ay8910_reset_ym on both, the filters off)
int tpay_queued(void);                  // the writes not yet made into sound
extern uint32_t tpay_queue_max;         // the most ever queued (for the tests and the notes)
extern uint32_t tpay_dropped;           // writes lost to a full queue (never, it is hoped)
extern void (*tpay_on_write)(uint64_t tick, int chip, int reg, uint8_t value);   // each write queued (tests)

// the six outputs at one tick, as MAME's streams have them (AY1 A B C,
// AY2 A B C), each from tpay_vol_table / tpay_env_table; the queue's
// writes for that tick are made first
void tpay_ref_tick(float out[6]);

// the mean of each output over the next n ticks (the writes in the queue
// made at their ticks); the state moves on exactly as MAME's does, and
// the means are exact but where a channel has both its tone and the noise
// on (taken as unrelated over the run)
void tpay_mean(int n, float out[6]);

// the filter's coefficient for a channel (0-5 as above) at a rate:
// 1 - exp(-1 / (Req C) / rate), 1 when the channel's filter is off
float tpay_filter_k(int ch, uint32_t rate);

#endif
