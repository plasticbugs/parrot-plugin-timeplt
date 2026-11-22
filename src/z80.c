// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// z80.c -- see z80.h. MAME 0.288's z80_device (z80.cpp, z80.h, z80.inc) in
// C for one CPU: its registers, the helpers z80.lst calls, device_start,
// device_reset, execute_set_input and execute_run. The instructions are
// z80_ops.h (tools/tp/z80gen.py). Left out, as Time Pilot has none of them:
// the daisy chain, WAIT, the refresh and no-MREQ callbacks, and the
// variants (NSC800, R800, Z80N, T6A84). Time Pilot has two Z80s, and they
// share this code (it is 100 KB, and the cart's RAM holds one): tpz80.c
// builds it once for Z80_CPUS CPUs. The one running has its registers in Z;
// each of the others has them in z_saved, and z80_run swaps them as a slice
// starts (a straight copy of words: cheaper than reaching the registers
// through a pointer, which costs every instruction). Each CPU has its own
// page tables and hooks. From Donkey Kong's (Parrot Cart's dkong/z80.c).
// MAME's license is in LICENSE (THIRD-PARTY.md).
// (tpz80.c includes this file. Built alone, as the kit's test/host/build.sh
// builds every .c in src/, it is empty.)
#ifdef Z80_CPUS
#include "z80.h"

#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#define Z80_HOT __attribute__((section(".time_critical.z80")))
#else
#define Z80_HOT
#endif

typedef union { struct { u8 l, h; } b; u16 w; } pair16_t;

typedef struct {
	pair16_t prvpc, pc, sp, af, bc, de, hl, ix, iy, wz, af2, bc2, de2, hl2;
	u8 r, r2, halt, im, i, nmi_state, irq_state, busack_state, service_attention;
	bool iff1, iff2;
	int busreq_state;
	u16 ea;
	struct {
		u8 s_val, z_val, yx_val, h_val, pv_val;
		bool n, c;
		u8 q, qtemp;
	} f;
	int icount, tmp_irq_vector;
	pair16_t shared_data, shared_data2;
	u32 ref;
	int cycles_running, cycles_stolen;
} zstate_t;

static zstate_t Z;                      // the running CPU's (z_live's)
static zstate_t z_saved[Z80_CPUS];      // each other CPU's
static int z_live;

// a CPU's registers wherever they are (for the calls from the board: a
// CPU's line set while the other runs, say)
static inline zstate_t *zs(int cpu) { return cpu == z_live ? &Z : &z_saved[cpu]; }

typedef uint32_t __attribute__((may_alias)) zword_t;
#define ZW (sizeof(zstate_t) / sizeof(zword_t))
_Static_assert(sizeof(zstate_t) % sizeof(zword_t) == 0 && ZW <= 24, "zstate_t: whole words, 24 at most");
static inline void zcopy(zstate_t *d, const zstate_t *s) {
	zword_t *dw = (zword_t *)(void *)d;
	const zword_t *sw = (const zword_t *)(const void *)s;
#define ZC(i) if ((i) < ZW) dw[i] = sw[i];
	ZC(0) ZC(1) ZC(2) ZC(3) ZC(4) ZC(5) ZC(6) ZC(7) ZC(8) ZC(9) ZC(10) ZC(11)
	ZC(12) ZC(13) ZC(14) ZC(15) ZC(16) ZC(17) ZC(18) ZC(19) ZC(20) ZC(21) ZC(22) ZC(23)
#undef ZC
}

// the CPU's registers into Z (z80_run as a slice starts; and the board's
// start and reset, which come before any CPU runs)
static inline void z_select(int cpu) {
	if (cpu == z_live) return;
	zcopy(&z_saved[z_live], &Z);
	zcopy(&Z, &z_saved[cpu]);
	z_live = cpu;
}

// z80.inc
#define HAS_LDAIR_QUIRK 0
#define CF  0x01
#define NF  0x02
#define PF  0x04
#define VF  PF
#define HF  0x10
#define YXF 0x28
#define ZF  0x40
#define SF  0x80
#define PRVPC  Z.prvpc.w
#define PC     Z.pc.w
#define SP     Z.sp.w
#define Q      Z.f.q
#define QT     Z.f.qtemp
#define I      Z.i
#define R      Z.r
#define R2     Z.r2
#define AF     Z.af.w
#define A      Z.af.b.h
#define F      Z.af.b.l
#define BC     Z.bc.w
#define B      Z.bc.b.h
#define C      Z.bc.b.l
#define DE     Z.de.w
#define D      Z.de.b.h
#define E      Z.de.b.l
#define HL     Z.hl.w
#define H      Z.hl.b.h
#define L      Z.hl.b.l
#define IX     Z.ix.w
#define HX     Z.ix.b.h
#define LX     Z.ix.b.l
#define IY     Z.iy.w
#define HY     Z.iy.b.h
#define LY     Z.iy.b.l
#define WZ     Z.wz.w
#define WZ_H   Z.wz.b.h
#define WZ_L   Z.wz.b.l
#define TDAT   Z.shared_data.w
#define TDAT2  Z.shared_data2.w
#define TDAT_H Z.shared_data.b.h
#define TDAT_L Z.shared_data.b.l
#define TDAT8  Z.shared_data.b.l

// z80.h: the service attention bits and the flags' accessors
#define SA_BUSREQ      0
#define SA_NMI_PENDING 1
#define SA_IRQ_ON      2
#define SA_HALT        3
#define SA_AFTER_EI    4
#define SA_AFTER_LDAIR 5
#define SET_SA(bit, state) ((state) ? (Z.service_attention |= (u8)(1u << (bit))) : (Z.service_attention &= (u8)~(1u << (bit))))
#define GET_SA(bit) ((Z.service_attention >> (bit)) & 1)
#define F_S()  (Z.f.s_val & 0x80)
#define F_Z()  (Z.f.z_val ? 0 : 0x40)
#define F_YX() (Z.f.yx_val & 0x28)
#define F_H()  (Z.f.h_val & 0x10)
#define F_PV() flag_pv(Z.f.pv_val)
#define CLEAR_LINE 0

// a standard Z80's bus timing (z80_device's m_m1_cycles, m_mreq_cycles, m_iorq_cycles)
#define Z80_M1   4
#define Z80_MREQ 3
#define Z80_IORQ 4

// no refresh or no-MREQ callback is set (pacman's), so nothing is called
#define refresh_en 0
#define nomreq_en 0
#define NOCB(...) ((void)0)

#define swap(a, b) do { __typeof__(a) t_ = (a); (a) = (b); (b) = t_; } while (0)
#define SWAP16(a, b) swap(a, b)

#define Z80_INLINE static inline __attribute__((always_inline))

Z80_INLINE u8 flag_pv(u8 val) {
	val ^= val >> 4;
	val ^= val << 2;
	val ^= val >> 1;
	return ~val & 0x04;
}

// ---- memory and I/O ------------------------------------------------------------------------
// (ic, the cycles left, is the execute function's own: the board's code
// sees it in Z.icount, and its abort_timeslice changes it there)
#define RD(a) rd(&ic, rdp, hk, (u16)(a))
#define WR(a, v) wr(&ic, wrp, hk, (u16)(a), (u8)(v))
#define RD_OP() RD(PC)
#define RD_ARG() RD(PC)
#define IO_IN(p) io_in(&ic, hk, (u16)(p))
#define IO_OUT(p, v) io_out(&ic, hk, (u16)(p), (u8)(v))

Z80_INLINE u8 rd(int *ic, const u8 *const *rdp, const z80_hooks_t *hk, u16 a) {
	const u8 *p = rdp[a >> 8];
	if (__builtin_expect(p != 0, 1)) return p[a & 0xff];
	Z.icount = *ic;
	u8 v = hk->rd(a);
	*ic = Z.icount;
	return v;
}
Z80_INLINE void wr(int *ic, u8 *const *wrp, const z80_hooks_t *hk, u16 a, u8 v) {
	u8 *p = wrp[a >> 8];
	if (__builtin_expect(p != 0, 1)) {
		p[a & 0xff] = v;
		return;
	}
	Z.icount = *ic;
	hk->wr(a, v);
	*ic = Z.icount;
}
Z80_INLINE u8 io_in(int *ic, const z80_hooks_t *hk, u16 p) { Z.icount = *ic; u8 v = hk->in(p); *ic = Z.icount; return v; }
Z80_INLINE void io_out(int *ic, const z80_hooks_t *hk, u16 p, u8 v) { Z.icount = *ic; hk->out(p, v); *ic = Z.icount; }
#define z80_irq_ack() hk->irq_ack()
// BUSACK goes nowhere on this board
#define Z80_BUSACK(state) do { (void)(state); } while (0)
#ifdef Z80_NO_TRACE                      // the cart: no hook
#define Z80_HOOK() do { } while (0)
#else
#define Z80_HOOK() do { void (*t_)(void) = z80_trace(z_live); if (t_) { Z.icount = ic; t_(); } } while (0)
#endif

// ---- z80.cpp's helpers ---------------------------------------------------------------------
static u8 get_f_of(const zstate_t *z) {
	u8 f = 0;
	f |= z->f.s_val & 0x80;
	f |= z->f.z_val ? 0 : 0x40;
	f |= z->f.yx_val & 0x28;
	f |= z->f.h_val & 0x10;
	f |= flag_pv(z->f.pv_val);
	f |= z->f.n ? NF : 0;
	f |= z->f.c ? CF : 0;
	return f;
}
#define get_f() get_f_of(&Z)

static void set_f(u8 f) {
	Z.f.s_val = f;
	Z.f.z_val = !(f & ZF);
	Z.f.yx_val = f;
	Z.f.h_val = f;
	Z.f.pv_val = !(f & PF);
	Z.f.n = f & NF;
	Z.f.c = f & CF;
}

Z80_INLINE void halt(void) {
	if (!Z.halt) {
		Z.halt = 1;
		SET_SA(SA_HALT, 1);
	}
}

Z80_INLINE void leave_halt(void) {
	if (Z.halt) {
		Z.halt = 0;
		SET_SA(SA_HALT, 0);
	}
}

Z80_INLINE void inc(u8 *r) {
	++*r;
	QT = 0;
	// keep C
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = *r;
	Z.f.pv_val = *r != 0x80;
	Z.f.h_val = (*r & 0x0f) == 0x00 ? HF : 0;
	Z.f.n = 0;
}

Z80_INLINE void dec(u8 *r) {
	--*r;
	QT = 0;
	// keep C
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = *r;
	Z.f.pv_val = *r != 0x7f;
	Z.f.h_val = (*r & 0x0f) == 0x0f ? HF : 0;
	Z.f.n = 1;
}

Z80_INLINE void rlca(void) {
	A = (A << 1) | (A >> 7);
	QT = 0;
	// keep SZP
	Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x01;
}

Z80_INLINE void rrca(void) {
	const u8 a0 = A;
	A = (a0 >> 1) | (a0 << 7);
	QT = 0;
	// keep SZP
	Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = a0 & 0x01;
}

Z80_INLINE void rla(void) {
	u8 res = (A << 1) + Z.f.c;
	QT = 0;
	// keep SZP
	Z.f.yx_val = res;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x80;
	A = res;
}

Z80_INLINE void rra(void) {
	u8 res = (Z.f.c << 7) | (A >> 1);
	QT = 0;
	// keep SZP
	Z.f.yx_val = res;
	Z.f.h_val = Z.f.n = 0;
	Z.f.c = A & 0x01;
	A = res;
}

Z80_INLINE void add_a(u8 value) {
	const u16 res = A + value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) + (value & 0x0f);
	Z.f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
	Z.f.n = 0;
	A = (u8)res;
}

Z80_INLINE void adc_a(u8 value) {
	const int c = Z.f.c;
	const u16 res = A + value + c;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) + (value & 0x0f) + c;
	Z.f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
	Z.f.n = 0;
	A = (u8)res;
}

Z80_INLINE void sub_a(u8 value) {
	const u16 res = A - value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f);
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
	A = (u8)res;
}

Z80_INLINE void sbc_a(u8 value) {
	const int c = Z.f.c;
	const u16 res = A - value - c;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.yx_val = (u8)res;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f) - c;
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
	A = (u8)res;
}

Z80_INLINE void neg(void) {
	u8 value = A;
	A = 0;
	sub_a(value);
}

Z80_INLINE void daa(void) {
	u8 a = A;
	if (Z.f.n) {
		if (F_H() || ((A & 0xf) > 9)) a -= 6;
		if (Z.f.c || (A > 0x99)) a -= 0x60;
	} else {
		if (F_H() || ((A & 0xf) > 9)) a += 6;
		if (Z.f.c || (A > 0x99)) a += 0x60;
	}
	QT = 0;
	// keep N
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = a;
	Z.f.h_val = A ^ a;
	Z.f.c = Z.f.c || A > 0x99;
	A = a;
}

Z80_INLINE void and_a(u8 value) {
	A &= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.n = Z.f.c = 0;
	Z.f.h_val = HF;
}

Z80_INLINE void or_a(u8 value) {
	A |= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = Z.f.c = 0;
}

Z80_INLINE void xor_a(u8 value) {
	A ^= value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = A;
	Z.f.h_val = Z.f.n = Z.f.c = 0;
}

Z80_INLINE void cp(u8 value) {
	const u16 res = A - value;
	QT = 0;
	Z.f.s_val = Z.f.z_val = (u8)res;
	Z.f.yx_val = value;
	Z.f.c = res & 0x100;
	Z.f.h_val = (A & 0x0f) - (value & 0x0f);
	Z.f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
	Z.f.n = 1;
}

Z80_INLINE void exx(void) {
	swap(Z.bc, Z.bc2);
	swap(Z.de, Z.de2);
	swap(Z.hl, Z.hl2);
}

#define SHIFT_FLAGS(res, carry)                                                  \
	do {                                                                         \
		QT = 0;                                                                  \
		Z.f.s_val = Z.f.z_val = Z.f.pv_val = Z.f.yx_val = (res);                 \
		Z.f.h_val = Z.f.n = 0;                                                   \
		Z.f.c = (carry);                                                         \
	} while (0)
Z80_INLINE u8 rlc(u8 value) { const u8 res = ((value << 1) | (value >> 7)) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_INLINE u8 rrc(u8 value) { const u8 res = ((value >> 1) | (value << 7)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_INLINE u8 rl(u8 value) { const u8 res = ((value << 1) + Z.f.c) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_INLINE u8 rr(u8 value) { const u8 res = ((value >> 1) | (Z.f.c << 7)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_INLINE u8 sla(u8 value) { const u8 res = (value << 1) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_INLINE u8 sra(u8 value) { const u8 res = ((value >> 1) | (value & 0x80)) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }
Z80_INLINE u8 sll(u8 value) { const u8 res = ((value << 1) | 0x01) & 0xff; SHIFT_FLAGS(res, value & 0x80); return res; }
Z80_INLINE u8 srl(u8 value) { const u8 res = (value >> 1) & 0xff; SHIFT_FLAGS(res, value & 0x01); return res; }

Z80_INLINE void bit(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = value;
}

Z80_INLINE void bit_hl(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = WZ_H;
}

Z80_INLINE void bit_xy(int b, u8 value) {
	QT = 0;
	Z.f.s_val = Z.f.z_val = Z.f.pv_val = value & (1 << b);
	Z.f.h_val = HF;
	Z.f.n = 0;
	Z.f.yx_val = Z.ea >> 8;
}

Z80_INLINE u8 res(int b, u8 value) { return value & ~(1 << b); }
Z80_INLINE u8 set(int b, u8 value) { return value | (1 << b); }

Z80_INLINE void block_io_interrupted_flags(void) {
	Z.f.yx_val = PC >> 8;
	const u8 pv_old = F_PV();
	if (Z.f.c) {
		Z.f.h_val = 0;
		if (TDAT8 & 0x80) {
			Z.f.pv_val = (B - 1) & 0x07;
			if ((B & 0x0f) == 0x00) Z.f.h_val = HF;
		} else {
			Z.f.pv_val = (B + 1) & 0x07;
			if ((B & 0x0f) == 0x0f) Z.f.h_val = HF;
		}
	} else {
		Z.f.pv_val = B & 0x07;
	}
	Z.f.pv_val = (pv_old ^ F_PV()) & PF;
}

Z80_INLINE void ei(void) {
	Z.iff1 = Z.iff2 = 1;
	SET_SA(SA_AFTER_EI, 1);
}

Z80_INLINE void illegal_1(void) {}
Z80_INLINE void illegal_2(void) {}

// ---- device_start, device_reset, execute_set_input -----------------------------------------
// (each for CPU cpu: start and reset swap it in, as they come before any
// CPU runs; the rest work on its registers where they are, through zs)
#define ZSA(z, bit, state) ((state) ? ((z)->service_attention |= (u8)(1u << (bit))) : ((z)->service_attention &= (u8)~(1u << (bit))))

void z80_start(int cpu) {
	z_select(cpu);
	memset(&Z, 0, sizeof Z);
	PRVPC = 0; PC = 0; SP = 0; AF = 0;
	set_f(0);
	Q = 0; QT = 0;
	BC = 0; DE = 0; HL = 0; IX = 0; IY = 0; WZ = 0;
	Z.af2.w = Z.bc2.w = Z.de2.w = Z.hl2.w = 0;
	R = R2 = 0;
	Z.iff1 = Z.iff2 = 0;
	Z.halt = 0;
	Z.im = 0;
	Z.i = 0;
	Z.nmi_state = Z.irq_state = 0;
	Z.busreq_state = Z.busack_state = 0;
	Z.ea = 0;
	Z.service_attention = 0;
	IX = IY = 0xffff;       // IX and IY are FFFF after a reset!
	Z.f.z_val = 0;          // Zero flag is set
	Z.ref = 0;                 // (the instruction boundary: z80gen.py)
}

void z80_reset(int cpu) {
	z_select(cpu);
	leave_halt();
	Z.ref = 0;                 // (the instruction boundary: z80gen.py)
	PC = 0;
	WZ = PC;
	Z.i = 0;
	Z.r = 0;
	Z.r2 = 0;
	Z.iff1 = 0;
	Z.iff2 = 0;
	SET_SA(SA_NMI_PENDING, 0);
	SET_SA(SA_AFTER_EI, 0);
	SET_SA(SA_AFTER_LDAIR, 0);
}

void z80_set_irq(int cpu, int state) {
	zstate_t *z = zs(cpu);
	z->irq_state = (u8)state;
	if (z->irq_state != CLEAR_LINE) ZSA(z, SA_IRQ_ON, 1);
	else ZSA(z, SA_IRQ_ON, 0);
}

void z80_set_nmi(int cpu, int state, bool ran) {
	zstate_t *z = zs(cpu);
	// mark an NMI pending on the rising edge (not at the same time RESET is cleared)
	if (z->nmi_state == CLEAR_LINE && state != CLEAR_LINE && ran)
		ZSA(z, SA_NMI_PENDING, 1);
	z->nmi_state = (u8)state;
}

void z80_set_busreq(int cpu, int state) {
	zstate_t *z = zs(cpu);
	if (z->busreq_state == CLEAR_LINE && state != CLEAR_LINE)
		ZSA(z, SA_BUSREQ, 1);
	z->busreq_state = state;
}

// ---- execute_run ---------------------------------------------------------------------------
static Z80_HOT __attribute__((noinline)) void execute(void) {
	int ic = Z.icount;
	const u8 *const *const rdp = z80_rd_pages(z_live);     // (in registers: no reload after a write)
	u8 *const *const wrp = z80_wr_pages(z_live);
	const z80_hooks_t *const hk = z80_hooks(z_live);
#include "z80_ops.h"
suspend:
	Z.icount = ic;
}

int z80_run(int cpu, int cycles) {
	z_select(cpu);
	Z.cycles_running = cycles;
	Z.cycles_stolen = 0;
	Z.icount = cycles;
	execute();
	return cycles - Z.icount - Z.cycles_stolen;
}

void z80_abort(int cpu) {
	zstate_t *z = zs(cpu);
	if (z->icount > 0) {
		int delta = z->icount;
		z->cycles_stolen += delta;
		z->cycles_running -= delta;
		z->icount = 0;
	}
}

void z80_keep(int cpu, int n) {
	zstate_t *z = zs(cpu);
	if (z->icount > n) {
		int delta = z->icount - n;
		z->cycles_stolen += delta;
		z->cycles_running -= delta;
		z->icount = n;
	}
}

void z80_eat(int cpu) {                 // eat_cycles(1000000000): the rest of the timeslice, as run
	zs(cpu)->icount = 0;
}

int z80_done(int cpu) { const zstate_t *z = zs(cpu); return z->cycles_running - z->icount; }

bool z80_bus_settled(int cpu) { const zstate_t *z = zs(cpu); return (z->busreq_state != 0) == (z->busack_state != 0); }

bool z80_stalled(int cpu) { const zstate_t *z = zs(cpu); return z->busreq_state && z->busack_state && z->ref == 0; }

void z80_regs(int cpu, z80_regs_t *r) {
	const zstate_t *z = zs(cpu);
	r->pc = z->pc.w; r->sp = z->sp.w;
	r->af = (u16)(z->af.b.h << 8 | get_f_of(z));
	r->bc = z->bc.w; r->de = z->de.w; r->hl = z->hl.w; r->ix = z->ix.w; r->iy = z->iy.w;
	r->af2 = z->af2.w; r->bc2 = z->bc2.w; r->de2 = z->de2.w; r->hl2 = z->hl2.w;
	r->wz = z->wz.w;
	r->r = (u8)((z->r & 0x7f) | (z->r2 & 0x80));
	r->i = z->i; r->im = z->im; r->iff1 = z->iff1; r->iff2 = z->iff2; r->halt = z->halt;
}

#endif  // Z80_CPUS
