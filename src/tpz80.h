// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// tpz80.h -- Time Pilot's two Z80s, sharing one copy of z80.c (tpz80.c):
// tpm_* is the main CPU (CPU 0), tps_* the sound CPU (CPU 1). Each name is
// z80.h's with z80_ changed and the CPU given; the board (tp.c) supplies
// the page tables' pages and the hooks.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "z80.h"

#define TPZ_MAIN 0
#define TPZ_SND  1

extern const uint8_t *tpm_rd_page[256], *tps_rd_page[256];
extern uint8_t *tpm_wr_page[256], *tps_wr_page[256];
extern void (*tpm_trace)(void), (*tps_trace)(void);   // before each instruction (MAME's debugger_instruction_hook), tests only

#define TPZ80_DECLARE(p, cpu) \
	uint8_t p##_hook_rd(uint16_t a); \
	void p##_hook_wr(uint16_t a, uint8_t d); \
	uint8_t p##_hook_in(uint16_t port); \
	void p##_hook_out(uint16_t port, uint8_t d); \
	uint8_t p##_hook_irq_ack(void); \
	static inline void p##_start(void) { z80_start(cpu); } \
	static inline void p##_reset(void) { z80_reset(cpu); } \
	static inline int p##_run(int cycles) { return z80_run(cpu, cycles); } \
	static inline void p##_abort(void) { z80_abort(cpu); } \
	static inline int p##_done(void) { return z80_done(cpu); } \
	static inline void p##_set_irq(int state) { z80_set_irq(cpu, state); } \
	static inline void p##_set_nmi(int state, bool ran) { z80_set_nmi(cpu, state, ran); } \
	static inline void p##_regs(z80_regs_t *r) { z80_regs(cpu, r); }
TPZ80_DECLARE(tpm, TPZ_MAIN)
TPZ80_DECLARE(tps, TPZ_SND)
