// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// z80.h -- the Zilog Z80 as MAME 0.288 emulates it (src/devices/cpu/z80:
// z80.cpp, z80.h, z80.inc and z80.lst), in C, for one CPU. z80_ops.h is
// made from MAME's z80.lst by tools/z80gen.py; z80.c is the rest of
// z80_device. An instruction can stop at any of MAME's steps when the
// cycles run out and carry on there in the next call, as MAME's does.
//
// The board supplies the memory and I/O below. Reads and writes in a page
// of a CPU's page tables go straight to it; the rest call the board's
// hooks. Time Pilot has two Z80s, sharing this code (z80.c): tpz80.h gives
// each its own names. From Donkey Kong's (Parrot Cart's dkong/z80.h).
// MAME's license (BSD-3-Clause) is in LICENSE (THIRD-PARTY.md).
#ifndef Z80_H
#define Z80_H

#include <stdbool.h>
#include <stdint.h>

// The board (tpz80.c) supplies, for CPU cpu (0 .. Z80_CPUS-1):
//   z80_rd_pages(cpu), z80_wr_pages(cpu)   its page tables (256 each)
//   z80_hooks(cpu)                         its hooks (a z80_hooks_t)
//   z80_trace(cpu)                         its trace hook (or NULL)
typedef struct {
	uint8_t (*rd)(uint16_t a);
	void (*wr)(uint16_t a, uint8_t d);
	uint8_t (*in)(uint16_t port);
	void (*out)(uint16_t port, uint8_t d);
	uint8_t (*irq_ack)(void);       // the vector the board puts on the bus (the IRQ acknowledged)
} z80_hooks_t;
void z80_start(int cpu);                    // device_start: power on
void z80_reset(int cpu);                    // device_reset
int z80_run(int cpu, int cycles);           // a timeslice: MAME's execute_run; returns the cycles run
void z80_abort(int cpu);                    // abort_timeslice: the rest of the timeslice taken back
void z80_keep(int cpu, int n);              // the rest cut to n cycles (if more), as if given that many
void z80_eat(int cpu);                      // eat_cycles: the rest of the timeslice run (counted) without running
bool z80_stalled(int cpu);
bool z80_bus_settled(int cpu);              // BUSACK as BUSREQ (no change of it to come in a run)
int z80_done(int cpu);                      // cycles run so far in this timeslice (m_cycles_running - m_icount)
void z80_set_irq(int cpu, int state);       // INPUT_LINE_IRQ0: ASSERT_LINE (1) or CLEAR_LINE (0)
void z80_set_nmi(int cpu, int state, bool ran);  // INPUT_LINE_NMI; ran: the CPU's total cycles aren't 0
void z80_set_busreq(int cpu, int state);    // Z80_INPUT_LINE_BUSREQ

#ifndef Z80_REGS_T
#define Z80_REGS_T
typedef struct {
	uint16_t pc, sp, af, bc, de, hl, ix, iy, af2, bc2, de2, hl2, wz;
	uint8_t r, i, im, iff1, iff2, halt;
} z80_regs_t;
#endif
void z80_regs(int cpu, z80_regs_t *r);  // as MAME's state interface gives them (F from the flags, R from r and r2)

#endif
