#!/usr/bin/env python3
"""cvserve_sim.py -- core 1's game bus loop, run against a model of the
ColecoVision bus.

    python3 -I test/cvserve_sim.py ../cvplay/build/cvgames.elf [--flat] [--old] [--seed N] [--ops N] [--quiet]

It runs the firmware's own object code (objdump's disassembly of
cv_serve_game, interpreted here with Cortex-M33 cycle counts: load or store
2, taken branch 2, push/pop 1 + registers, the rest 1; 6.67 ns a cycle)
against Z80 bus cycles laid out as CVSERVE.md describes them, and checks,
for every cart read, that the right byte was on the cart's pins from set-up
time before the Z80 samples it, and that every select was taken, once, and
nothing else was. It prints the smallest margin seen.

--old runs the loop this one replaced (serve_game, on_game_select,
on_game_header from an e8ad860 build), for comparison.

Bus models (--model; nominal unless one is named):
  nominal   Z80A data-sheet timing through the console's decoder: nCART
            falls half a clock after the address changes, and rises with or
            before it. The address is on the way to the refresh address
            while nCART is still low after an opcode fetch (nCART lags).
            The console's captures look like this.
--bounce adds what they also show: address lines and nCART may read wrong
(either way) for 30 ns from a change of the cart's byte while nCART is low,
and for 30 ns from nCART's fall (the 74HCT245 switching the console's bus).
The other models were guesses made before the captures:
  early     as measured on the console (docs/console-facts.md): nCART is
            already low while address lines are still settling, so a read
            can show another address for its first tens of nanoseconds.
  slow      early, with rising address lines up to 150 ns late (a loaded
            bus, 2.0 V input thresholds).
  late      nominal, but nCART falls 150-350 ns late (a read as short as
            200 ns: the SGM holding the console's decoder off after a cycle
            in its RAM).
"""
import argparse, bisect, random, re, subprocess, sys

CYCLE = 1e9 / 150e6
T = 1e9 / 3579545
ADDR_GPIO = [0, 1, 2, 11, 3, 10, 4, 13, 14, 5, 6, 12, 7, 9, 8]     # hw3/netlist.py: GPIO g carries A[ADDR_GPIO[g]]
LINE_GPIO = [ADDR_GPIO.index(l) for l in range(15)]
NCART = 0x8000
SIO_IN, SIO_OUT, TIMER = 0xd0000004, 0xd0000010, 0x400b0028
PAGES = 0x30000000
RET = 0xfffffff0


def raw(a):                                # cart address (15 bits) -> GPIO 0-14
    return sum(1 << LINE_GPIO[l] for l in range(15) if a >> l & 1)


# ---- the ROM, as gamecart.h lays it out -------------------------------------
class Cart:
    def __init__(self, nbanks, rng):
        self.nbanks = nbanks
        self.rom = [bytes(rng.randrange(256) for _ in range(0x4000)) for _ in range(nbanks)]
        self.mem = {}                      # page (bank * 4 + quarter) -> bytearray(4096)
        for b in range(nbanks):
            for k in range(4):
                self.mem[b * 4 + k] = bytearray(4096)
        for a in range(0x4000):
            r = raw(a)
            t, i = r >> 7 & 31, (r & 0x7f) | (r >> 5 & 0x380)
            k = (t & 1) << 1 | (t >> 4 & 1)
            off = (t >> 2 & 3) * 1024 + i
            for b in range(nbanks):
                self.mem[b * 4 + k][off] = self.rom[b][a]

    def tab_entry(self, n, t):             # gc_finish's
        b = n % self.nbanks if t & 2 else self.nbanks - 1
        k = (t & 1) << 1 | (t >> 4 & 1)
        return PAGES + (b * 4 + k) * 4096 + (t >> 2 & 3) * 1024

    def byte(self, bank, a):               # what the console should read at cart address a (15 bits)
        return self.rom[self.nbanks - 1][a] if a < 0x4000 else self.rom[bank][a - 0x4000]


# ---- the bus ----------------------------------------------------------------
class Bus:
    """Pin changes in time. Each op is a Z80 machine cycle."""
    def __init__(self, model, rng):
        self.model, self.rng = model, rng
        self.ev = []                       # (t, kind, value): kind 0..14 an address line, 15 nCART
        self.addr = 0
        self.t = 2000.0
        self.checks = []                   # (t_sample, setup, cart address, kind)
        self.selects = []                  # (t nCART fell, t it rose, n)
        self.last_rise = 0.0

    def _addr(self, t, a):
        m = self.model
        for l in range(15):
            if (self.addr ^ a) >> l & 1:
                d = self.rng.uniform(30, 110)
                if a >> l & 1 and m == 'slow': d += self.rng.uniform(0, 150)
                self.ev.append((t + d, l, a >> l & 1))
        self.addr = a

    def _ncart(self, t_fall, t_rise):
        self.ev.append((t_fall, 15, 0))
        self.ev.append((t_rise, 15, 1))

    def cycle(self, kind, a, refresh=0x0000):
        """kind: 'm1' (opcode fetch, with the console's wait state), 'rd', 'wr', 'io'"""
        t0 = self.t
        self._addr(t0, a & 0x7fff)
        lag = lambda: self.rng.uniform(35, 75)        # LS138 + HCT21 + R11 into GP15
        dm = lambda: self.rng.uniform(20, 85)         # MREQ after its clock edge
        cart = a >= 0x8000 and kind != 'io'
        if kind == 'm1':
            lo, hi, length = t0 + T / 2 + dm(), t0 + 3 * T + dm(), 5 * T
            sample, setup = t0 + 3 * T, 35
        elif kind == 'rd':
            lo, hi, length = t0 + T / 2 + dm(), t0 + 2.5 * T + dm(), 3 * T
            sample, setup = t0 + 2.5 * T, 50
        elif kind == 'wr':
            lo, hi, length = t0 + T / 2 + dm(), t0 + 2.5 * T + dm(), 3 * T
            sample = None
        else:
            lo = hi = None; length = 4 * T; sample = None
        if cart:
            fall, rise = lo + lag(), hi + lag()
            if self.model == 'late':       # the select falls late (the SGM lets go of the decoder slowly)
                fall += self.rng.uniform(150, 350)
            elif self.model != 'nominal':  # nCART seen low while the address still settles
                fall = max(t0 + self.rng.uniform(15, 60), self.last_rise + 10)
            self.last_rise = rise
            self._ncart(fall, rise)
            if sample is not None: self.checks.append((sample, setup, a & 0x7fff, kind))
            if (a & 0x7fc0) == 0x7fc0: self.selects.append((fall, rise, a & 63))
        if kind == 'm1':
            self._addr(t0 + 3 * T, refresh & 0x7fff)
        self.t = t0 + length

    def idle(self, n=1):
        self.t += n * T

    def finish(self):
        self.ev.sort()
        self.times, self.vals = [0.0], [NCART]
        v = NCART
        for t, k, x in self.ev:
            if k == 15: v = v | NCART if x else v & ~NCART
            else:
                bit = 1 << LINE_GPIO[k]
                v = v | bit if x else v & ~bit
            self.times.append(t); self.vals.append(v)

    def pins(self, t):
        return self.vals[bisect.bisect_right(self.times, t) - 1]


# ---- a Thumb-2 interpreter for what these loops use ---------------------------
class Stop(Exception): pass

class CPU:
    def __init__(self, elf, funcs):
        self.code = {}
        self.half = {}
        self.sym = {}
        nm = subprocess.run(['arm-none-eabi-nm', '-S', elf], capture_output=True, text=True, check=True).stdout
        for line in nm.splitlines():
            p = line.split()
            if len(p) == 4: self.sym[p[3]] = (int(p[0], 16), int(p[1], 16))
            elif len(p) == 3: self.sym[p[2]] = (int(p[0], 16), 0)
        self.elf = elf
        for f in funcs: self.load(f)
        self.r = [0] * 16
        self.it = []
        self.n = self.z = self.c = self.v = 0
        self.ram = {}
        self.t = 0.0
        self.outs = []                     # (t, byte on GP16-23)
        self.mmio_r = self.mmio_w = None

    def load(self, f):
        cands = [k for k in self.sym if k == f or k.startswith(f + '.')]
        if not cands and f.endswith('_hdr'): return None
        assert cands, f
        a, size = self.sym[cands[0]]
        a &= ~1
        d = subprocess.run(['arm-none-eabi-objdump', '-D', '-j', '.scratch_x', '-j', '.data', '-M', 'force-thumb',
                            '--start-address=%d' % a, '--stop-address=%d' % (a + (size or 0x400)), self.elf],
                           capture_output=True, text=True, check=True).stdout
        for line in d.splitlines():
            m = re.match(r'\s*([0-9a-f]+):\s+((?:[0-9a-f]{4} )+|[0-9a-f]{8} )\s*(\S+)\s*(.*)', line)
            if not m: continue
            ad, hexs, mn, ops = int(m.group(1), 16), m.group(2).split(), m.group(3), m.group(4)
            ops = ops.split(';')[0].split('<')[0].strip()
            size = sum(len(h) // 2 for h in hexs)
            self.code[ad] = (mn, ops, size, hexs)
            if len(hexs[0]) == 8: hexs = [hexs[0][4:], hexs[0][:4]]
            for i, h in enumerate(hexs): self.half[ad + 2 * i] = int(h, 16)   # (literals read back from here)
        return a

    # memory
    def rd(self, a, size=4):
        if a >= 0x40000000 and a < 0xe0000000 and not (PAGES <= a < PAGES + 0x10000000):
            return self.mmio_r(a)
        if PAGES <= a < PAGES + 0x10000000:
            return self.page_rd(a)
        if a in self.half and size == 4:
            return self.half[a] | self.half[a + 2] << 16
        if size == 1: return self.ram.get(a & ~3, 0) >> (8 * (a & 3)) & 0xff
        return self.ram.get(a, 0)

    def wr(self, a, v, size=4):
        if a >= 0x40000000 and a < 0xe0000000: self.mmio_w(a, v); return
        if size == 1:
            w = self.ram.get(a & ~3, 0); s = 8 * (a & 3)
            self.ram[a & ~3] = (w & ~(0xff << s)) | (v & 0xff) << s
        else: self.ram[a] = v & 0xffffffff

    def reg(self, s):
        s = s.strip()
        return {'sl': 10, 'fp': 11, 'ip': 12, 'sp': 13, 'lr': 14, 'pc': 15}.get(s) if not re.match(r'r\d+$', s) else int(s[1:])

    def nz(self, v):
        v &= 0xffffffff
        self.n, self.z = v >> 31, int(v == 0)
        return v

    def cond(self, c):
        return {'eq': self.z, 'ne': not self.z, 'mi': self.n, 'pl': not self.n, 'hi': self.c and not self.z,
                'ls': not self.c or self.z, 'cs': self.c, 'cc': not self.c, 'hs': self.c, 'lo': not self.c,
                'ge': self.n == self.v, 'lt': self.n != self.v, 'gt': not self.z and self.n == self.v,
                'le': self.z or self.n != self.v, '': True}[c]

    def operand2(self, parts):
        """parts after rd[, rn]: '#imm' | rm | rm, lsl #n | rm, lsr #n"""
        if parts[0].startswith('#'): return int(parts[0][1:], 0) & 0xffffffff
        v = self.r[self.reg(parts[0])]
        if len(parts) > 1:
            m = re.match(r'(lsl|lsr|asr) #(\d+)', parts[1])
            n = int(m.group(2))
            v = (v << n) & 0xffffffff if m.group(1) == 'lsl' else v >> n
        return v

    def sub(self, a, b, setf):
        res = (a - b) & 0xffffffff
        if setf:
            self.nz(res); self.c = int(a >= b)
            self.v = int(((a ^ b) & (a ^ res)) >> 31 & 1)
        return res

    def step(self):
        pc = self.r[15]
        if pc == RET: raise Stop()
        mn, ops, size, _ = self.code[pc]
        nxt = pc + size
        cyc = 1
        base = mn.split('.')[0]
        parts = [p.strip() for p in re.split(r',\s*(?![^\[]*\])(?![^{]*})', ops)] if ops else []
        r = self.r
        if self.it:                        # in an IT block: the condition is the mnemonic's suffix
            c = self.it.pop(0)
            if not self.cond(c):
                r[15] = nxt; self.t += CYCLE; return
            base = base[:-2]
        elif re.match(r'i[te]+$', base):
            inv = {'eq': 'ne', 'ne': 'eq', 'cs': 'cc', 'cc': 'cs', 'mi': 'pl', 'pl': 'mi', 'hi': 'ls', 'ls': 'hi',
                   'ge': 'lt', 'lt': 'ge', 'gt': 'le', 'le': 'gt'}
            self.it = [ops if ch == 't' else inv[ops] for ch in 't' + base[2:]]
            r[15] = nxt; self.t += CYCLE; return
        def mem_addr(s):
            m = re.match(r'\[(\w+)(?:,\s*([^\]]+))?\]', s)
            a = r[self.reg(m.group(1))]
            if m.group(1) == 'pc': a = (pc + 4) & ~3
            if m.group(2):
                o = [x.strip() for x in m.group(2).split(',')]
                a += self.operand2(o)
            return a & 0xffffffff
        if base in ('ldr', 'ldrb'):
            rt = self.reg(parts[0])
            if len(parts) == 3 and parts[1].startswith('[') and parts[1].endswith(']'):   # post-index: [sp], #4
                a = r[self.reg(parts[1][1:-1])]
                r[self.reg(parts[1][1:-1])] = (a + int(parts[2][1:], 0)) & 0xffffffff
            else:
                a = mem_addr(', '.join(parts[1:]))
            if a == SIO_IN: v = self.mmio_r(a)
            else: v = self.rd(a, 1 if base == 'ldrb' else 4)
            if rt == 15: nxt = v & ~1; cyc = 3
            else: r[rt] = v; cyc = 2
        elif base in ('str', 'strb'):
            a = mem_addr(', '.join(parts[1:]))
            self.wr(a, r[self.reg(parts[0])], 1 if base == 'strb' else 4); cyc = 2
        elif base == 'strd':
            a = mem_addr(', '.join(parts[2:]))
            self.wr(a, r[self.reg(parts[0])]); self.wr(a + 4, r[self.reg(parts[1])]); cyc = 3
        elif base in ('push', 'stmdb', 'pop', 'ldmia'):
            regs = [self.reg(x) for x in re.search(r'\{(.*)\}', ops).group(1).split(',')]
            if base in ('push', 'stmdb'):
                for x in sorted(regs, reverse=True):
                    r[13] -= 4; self.wr(r[13], r[x])
            else:
                for x in sorted(regs):
                    v = self.rd(r[13]); r[13] += 4
                    if x == 15: nxt = v & ~1
                    else: r[x] = v
            cyc = 1 + len(regs)
        elif base in ('mov', 'movs', 'movw'):
            v = self.operand2(parts[1:])
            r[self.reg(parts[0])] = v
            if base == 'movs': self.nz(v)
        elif base in ('and', 'ands', 'orr', 'orrs', 'eor', 'eors', 'add', 'adds', 'sub', 'subs', 'bic', 'bics'):
            rd = self.reg(parts[0])
            if len(parts) == 2 or (len(parts) == 3 and re.match(r'(lsl|lsr) ', parts[2]) and False):
                a, b = r[rd], self.operand2(parts[1:])
            else:
                a, b = r[self.reg(parts[1])], self.operand2(parts[2:])
            op = base.rstrip('s') if base not in ('ands', 'adds', 'subs', 'orrs', 'eors', 'bics') else base[:-1]
            setf = base.endswith('s') and base not in ('bics',) or base == 'bics'
            if op == 'and': v = a & b
            elif op == 'orr': v = a | b
            elif op == 'eor': v = a ^ b
            elif op == 'bic': v = a & ~b
            elif op == 'add':
                v = (a + b) & 0xffffffff
                if setf: self.c = int(a + b > 0xffffffff)
            else: v = self.sub(a, b, setf)
            if setf and op != 'sub': self.nz(v)
            r[rd] = v & 0xffffffff
        elif base == 'sbc':                # (no flags)
            r[self.reg(parts[0])] = (r[self.reg(parts[1])] - self.operand2(parts[2:]) - (0 if self.c else 1)) & 0xffffffff
        elif base == 'rsb':
            r[self.reg(parts[0])] = (self.operand2(parts[2:]) - r[self.reg(parts[1])]) & 0xffffffff
        elif base in ('lsls', 'lsrs', 'lsl', 'lsr'):
            rd = self.reg(parts[0])
            src = r[self.reg(parts[1])] if len(parts) == 3 else r[rd]
            n = int(parts[-1][1:], 0)
            if base.startswith('lsl'):
                if n: self.c = src >> (32 - n) & 1
                v = (src << n) & 0xffffffff
            else:
                if n: self.c = src >> (n - 1) & 1
                v = src >> n
            if base.endswith('s'): self.nz(v)
            r[rd] = v
        elif base == 'ubfx':
            r[self.reg(parts[0])] = r[self.reg(parts[1])] >> int(parts[2][1:]) & ((1 << int(parts[3][1:])) - 1)
        elif base == 'uxth': r[self.reg(parts[0])] = r[self.reg(parts[1])] & 0xffff
        elif base == 'uxtb': r[self.reg(parts[0])] = r[self.reg(parts[1])] & 0xff
        elif base == 'cmp': self.sub(r[self.reg(parts[0])], self.operand2(parts[1:]), True)
        elif base == 'tst': self.nz(r[self.reg(parts[0])] & self.operand2(parts[1:]))
        elif base in ('cbz', 'cbnz'):
            if (r[self.reg(parts[0])] == 0) == (base == 'cbz'): nxt = int(parts[1], 16); cyc = 2
        elif base == 'bl':
            r[14] = nxt | 1; nxt = int(parts[0], 16); cyc = 3
        elif base == 'bx': nxt = r[self.reg(parts[0])] & ~1; cyc = 3
        elif base == 'umull':
            v = r[self.reg(parts[2])] * r[self.reg(parts[3])]
            r[self.reg(parts[0])], r[self.reg(parts[1])] = v & 0xffffffff, v >> 32
        elif base == 'dmb' or base == 'nop': pass
        elif base[0] == 'b' and len(base) in (1, 3):
            if self.cond(base[1:]): nxt = int(parts[0], 16); cyc = 2
        else:
            raise NotImplementedError('%x: %s %s' % (pc, mn, ops))
        r[15] = nxt
        self.t += cyc * CYCLE


# ---- one run ----------------------------------------------------------------
def program(bus, cart, rng, nops, ramcopy=True, selects=True):
    """Bus cycles shaped like the user's MegaCart + SGM ports (Mappy: selects by
    ld a,(hl) and ld a,(nn) from the fixed bank and from RAM, data copied out
    of the selected bank), with header reads and odd addresses mixed in."""
    R = [0]
    def m1(a):
        R[0] = (R[0] + 1) & 0x7f
        bus.cycle('m1', a, R[0])
    pc = 0x8100
    bank = 0
    expect = []                            # (index into bus.checks from which this bank holds)
    banks_at = []
    def mark(): banks_at.append((len(bus.checks), bank))
    mark()
    for _ in range(nops):
        k = rng.random()
        if not selects and k < 0.39: k = 0.5          # a plain ROM: $FFC0-$FFFF is data like the rest
        pc = 0x8000 + rng.randrange(0x3ff0)
        if k < 0.18:                       # ld a,(hl) at HL=$FFC0+n, then ret
            n = rng.randrange(64)
            m1(pc); bus.cycle('rd', 0xffc0 + n)
            bank = n % cart.nbanks; mark()
            m1(pc + 1); bus.cycle('rd', 0x03f0); bus.cycle('rd', 0x03f1)
        elif k < 0.28:                     # ld a,($FFC0+n)
            n = rng.randrange(64)
            m1(pc); bus.cycle('rd', pc + 1); bus.cycle('rd', pc + 2); bus.cycle('rd', 0xffc0 + n)
            bank = n % cart.nbanks; mark()
        elif k < 0.34:                     # the same from RAM (SGM): only the select touches the cart
            n = rng.randrange(64)
            ra = 0x5c00 + rng.randrange(0x100)
            m1(ra); bus.cycle('rd', ra + 1); bus.cycle('rd', ra + 2); bus.cycle('rd', 0xffc0 + n)
            bank = n % cart.nbanks; mark()
            m1(ra + 3)
        elif k < 0.37:                     # pop hl at SP=$FFC0+n: two selects back to back, the second wins
            n = rng.randrange(63)
            m1(pc); bus.cycle('rd', 0xffc0 + n); bus.cycle('rd', 0xffc1 + n)
            bank = (n + 1) % cart.nbanks; mark()
        elif k < 0.39:                     # ld ($FFC0+n),a: a write selects too
            n = rng.randrange(64)
            m1(pc); bus.cycle('rd', pc + 1); bus.cycle('rd', pc + 2); bus.cycle('wr', 0xffc0 + n)
            bank = n % cart.nbanks; mark()
        elif k < 0.60:                     # ld a,(hl): data in the selected bank, often high in it
            hl = rng.choice([0xc000 + rng.randrange(0x4000 if not selects else 0x3fc0), 0xff00 + rng.randrange(0xc0), 0xffb0 + rng.randrange(16),
                             0xfe00 + rng.randrange(0x1c0)])
            m1(pc); bus.cycle('rd', hl)
        elif k < 0.70:                     # ldi from the bank to RAM (stack/RAM addresses of any kind)
            hl = 0xc000 + rng.randrange(0x3fc0)
            m1(pc); m1(pc + 1); bus.cycle('rd', hl); bus.cycle('wr', rng.choice([0x7fc0 + rng.randrange(64), 0x2000 + rng.randrange(0x6000)])); bus.idle(2)
        elif k < 0.78:                     # code in the bank, calling: stack at any address
            a = 0xc000 + rng.randrange(0x3f00)
            m1(a); bus.cycle('rd', a + 1); bus.cycle('rd', a + 2); bus.idle(1)
            sp = rng.choice([0x03f0, 0x7ffe, 0x73fe])
            bus.cycle('wr', sp); bus.cycle('wr', sp - 1)
        elif k < 0.82:                     # ld a,($8000) / ld hl,($8000): a game reading its header
            m1(pc); bus.cycle('rd', pc + 1); bus.cycle('rd', pc + 2); bus.cycle('rd', 0x8000)
            if rng.random() < 0.5: bus.cycle('rd', 0x8001)
            m1(pc + 3)                     # (its next instruction)
        elif k < 0.83 and ramcopy:         # code in RAM: ld hl,($8000), then 30 us in RAM (what the console showed
            ra = 0x5e00                    # in Time Pilot: $8000, $8001, a quiet bus, and no reset)
            m1(ra); bus.cycle('rd', ra + 1); bus.cycle('rd', ra + 2); bus.cycle('rd', 0x8000); bus.cycle('rd', 0x8001)
            for i in range(22): m1(ra + 3 + i)
        elif k < 0.84 and ramcopy:         # code in RAM copying from $8000 (ldir), then on in RAM
            ra = 0x5d00
            for a in range(0x8000, 0x8000 + rng.randrange(1, 4)):
                m1(ra); m1(ra + 1); bus.cycle('rd', a); bus.cycle('wr', 0x6000 + a % 256); bus.idle(7)
            for i in range(rng.randrange(12)): m1(ra + 2 + i)
        elif k < 0.90:                     # in a,(n) / out (n),a with any A: the AY, the PSG, the pads
            m1(pc); bus.cycle('rd', pc + 1); bus.cycle('io', rng.choice([0xff50, 0xff51, 0x7fff, 0xffff, 0xffbe, 0x0052]))
        elif k < 0.95:                     # low addresses in the fixed bank ($80xx), where lines read 0 on the way
            a = 0x8000 + rng.randrange(1, 0x100)
            m1(a); bus.cycle('rd', 0x8000 + rng.randrange(2, 0x100))
        else:                              # RAM only for a while
            ra = 0x2000 + rng.randrange(0x5ff0)
            for i in range(4): m1(ra + i)
            bus.cycle('rd', 0x7fc0 + rng.randrange(64)); bus.cycle('wr', 0x7fff)
    return banks_at


def run(elf, old, model, seed, nops, quiet=False, reset_at_end=True, ramcopy=True, flat=False, title=False, busy=False, phantom=False, bounce=False):
    rng = random.Random(seed)
    cart = Cart(8, rng)
    bus = Bus(model, rng)
    banks_at = program(bus, cart, rng, nops, ramcopy, not flat)
    if flat: bus.selects = []
    t_end_game = bus.t
    if reset_at_end:                       # RESET: the BIOS (3aa93ef3) from $0000, as MAME times it
        bus.idle(40)
        for a in (0x0000, 0x0003, 0x0069, 0x006a, 0x006b, 0x006e):
            bus.cycle('m1', a)
        bus.cycle('rd', 0x006f); bus.cycle('rd', 0x0070)
        t_reset = bus.t
        bus.cycle('rd', 0x8000); bus.cycle('rd', 0x8001)
        if not title:                      # 55 AA: the start address 18.2 us after $8000, then the game
            for i in range(12): bus.cycle('m1', 0x0071 + i)
            bus.idle(-1)
            t_done = bus.t
            bus.cycle('rd', 0x800a); bus.cycle('rd', 0x800b)
            bus.idle(20)
        else:                              # AA 55: $8008-9 at 95 us, the header again at 251.76 ms
            for i in range(68): bus.cycle('m1', 0x1100 + i)
            bus.cycle('rd', 0x8008); bus.cycle('rd', 0x8009)
            if busy:                       # (not the BIOS: a game that reads the cart all the while)
                for i in range(16000): bus.cycle('m1', 0x9000 + rng.randrange(0x100))   # 15 ms of nCART low
            if phantom:                    # (nor a game waiting in RAM: Mr. Do! Too's coin screen reads the
                for i in range(170):       # cart some 170 times in 250 ms)
                    bus.cycle('rd', 0xc000 + rng.randrange(0x3f00)); bus.idle(5000)
            bus.t = t_reset + 251761.0e3
            t_done = bus.t
            bus.cycle('rd', 0x8000)
            for i in range(6): bus.cycle('m1', 0x13b0 + i)
            bus.cycle('rd', 0x8001)
            bus.idle(20)
    bus.finish()

    funcs = ['serve_game', 'on_game_select', 'on_game_header'] if old else ['cv_serve_flat' if flat else 'cv_serve_game']
    if not old: funcs.append(funcs[0] + '_hdr')        # (absent before the loops were split: load() skips it)
    if flat:                               # one 32K image, indexed by the pins: the fixed bank and bank 0
        img = bytearray(0x8000)
        for a in range(0x8000): img[raw(a)] = cart.byte(0, a)
    cpu = CPU(elf, funcs)
    S, TABS, GC = 0x20100000, 0x20110000, 0x20120000
    cpu.page_rd = (lambda a: img[a - PAGES]) if flat else (lambda a: cart.mem[(a - PAGES) >> 12][(a - PAGES) & 0xfff])
    takes = []                             # (t, n) as the loop took them
    T0_US = 5_000_000                      # the timer when the run starts (t_commit = 0: past the boot window)
    brng = random.Random(seed + 1000)
    falls = sorted(t for t, k, x in bus.ev if k == 15 and x == 0)
    chg = [-1e9, None]                     # when the byte on GP16-23 last changed
    def mmio_r(a):
        if a == SIO_IN:
            ts = cpu.t - 2 * CYCLE         # the input synchroniser
            v = bus.pins(ts)
            if bounce:                     # the 74HCT245 switching the console's bus shows on the inputs, either
                hit = False                # way: for 30 ns from a change of the cart's byte while nCART is low,
                if chg[0] - 5 <= ts <= chg[0] + 30 and not bus.pins(chg[0]) & NCART: hit = brng.random() < 0.6
                i = bisect.bisect_right(falls, ts) - 1          # and for 30 ns from nCART's fall (it turns on)
                if i >= 0 and ts - falls[i] <= 30 and brng.random() < 0.5: hit = True
                if hit:
                    for g in range(16):
                        if brng.random() < 0.2: v ^= 1 << g
            return v
        if a == TIMER: return T0_US + int(cpu.t / 1000)
        raise KeyError(hex(a))
    def mmio_w(a, v):
        assert a == SIO_OUT, hex(a)
        cpu.outs.append((cpu.t + 3 * CYCLE, v >> 16 & 0xff))   # the store's second cycle, then the pad
        if chg[1] != v >> 16 & 0xff: chg[0], chg[1] = cpu.t + 3 * CYCLE, v >> 16 & 0xff
    cpu.mmio_r, cpu.mmio_w = mmio_r, mmio_w
    if old:
        for t in range(32): cpu.ram[GC + 4 * t] = cart.tab_entry(0, t)
        for n in range(64):
            for k in range(4): cpu.ram[GC + 128 + 16 * n + 4 * k] = PAGES + ((n % cart.nbanks) * 4 + k) * 4096
        cpu.ram[cpu.sym['gc'][0]] = GC
        sel_addr = cpu.sym['game_selects'][0]
        entry = cpu.sym['serve_game'][0] & ~1
    else:
        for n in range(64):
            for t in range(32): cpu.ram[TABS + 128 * n + 4 * t] = cart.tab_entry(n, t)
        cpu.ram[S] = PAGES if flat else TABS
        cpu.ram[S + 44] = 0x20130000        # the loader's image (zeros)
        cpu.r[0] = S
        sel_addr = S + 8
        entry = cpu.sym['cv_serve_flat' if flat else 'cv_serve_game'][0] & ~1
    cpu.r[13], cpu.r[14], cpu.r[15] = 0x20200000, RET | 1, entry
    last_sel = 0
    returned = None
    top = min(a for a, c in cpu.code.items() if c[0] == 'ldr' and c[1] == 'r0, [r6, #4]') if not old else None   # the main loop's
    seen = None                            # (a quiet bus is skipped a whole number of the main loop's turns at a time)
    try:
        while cpu.t < bus.t:
            if cpu.r[15] == top:
                nxt_ev = bus.times[min(bisect.bisect_right(bus.times, cpu.t), len(bus.times) - 1)]
                snap = (tuple(cpu.r[:14]), cpu.n, cpu.z, cpu.c)
                if seen and seen[0][0][:14] == snap[0][:14] and nxt_ev - cpu.t > 50000:
                    period = cpu.t - seen[1]
                    k = int((nxt_ev - cpu.t - 2000) / period)
                    if 0 < period < 400 and k > 0 and not (bus.pins(cpu.t) & NCART) == 0:
                        cpu.t += k * period
                seen = (snap, cpu.t)
            cpu.step()
            s = cpu.ram.get(sel_addr, 0)
            if s != last_sel:
                last_sel = s
                takes.append(cpu.t)
    except Stop:
        returned = cpu.t

    # every cart read: the right byte from set-up time (less the 74HCT245) until it is sampled
    HCT245 = 25
    ot = [o[0] for o in cpu.outs]
    worst = {'m1': 1e9, 'rd': 1e9}
    bad = []
    bi = 0
    t_limit = returned if returned is not None and reset_at_end and returned < t_reset else 1e18   # a false RESET ends the run
    for i, (ts, setup, a, kind) in enumerate(bus.checks):
        if (reset_at_end and ts > t_end_game) or ts > t_limit: break
        while bi + 1 < len(banks_at) and banks_at[bi + 1][0] <= i: bi += 1
        if (a & 0x7fc0) == 0x7fc0 and not flat: continue   # a select's own byte: either bank will do
        want = cart.byte(banks_at[bi][1], a)
        need = ts - setup - HCT245
        j = bisect.bisect_right(ot, ts - HCT245) - 1   # the last output before the sample
        k = j
        while k >= 0 and cpu.outs[k][1] == want: k -= 1
        since = ot[k + 1] if k + 1 <= j else None      # right from here on
        margin = need - since if since is not None else -1e9
        worst[kind] = min(worst[kind], margin)
        if margin < 0: bad.append((ts, a, kind, margin))
    # selects: each taken once, during its read (or just after: the address holds half a clock)
    missed = extra = 0
    LATE = 1100                            # (the next cart read's data is due no sooner: 4 clocks)
    ti = 0                                 # in order: each select's take is the next one, inside its window
    for fall, rise, n in bus.selects:
        if rise + LATE > t_limit: break
        while ti < len(takes) and takes[ti] < fall: extra += 1; ti += 1
        if ti < len(takes) and takes[ti] <= rise + LATE: ti += 1
        else: missed += 1
    extra += sum(1 for t in takes[ti:] if t < t_limit)
    ok = not bad and not missed and not extra
    if reset_at_end:
        cause = cpu.ram.get(S + 60, 0)
        if busy or phantom: ok = ok and returned is None
        else: ok = ok and returned is not None and t_done < returned < t_done + (11000 if title else 700) and cause == (2 if title else 1)
    if not quiet or not ok:
        print('  %-7s %s seed %-3d: %d cart reads, %d selects | missed %d, false %d | wrong bytes %d | margin: opcode %4.0f ns, data %4.0f ns%s'
              % (model, 'old' if old else 'flat' if flat else 'new', seed, len(bus.checks), len(bus.selects), missed, extra, len(bad),
                 worst['m1'], worst['rd'],
                 '' if not reset_at_end else ' | RESET %s' % ('not taken' if returned is None else
                    'taken %.1f us after the $8000 read%s' % ((returned - t_reset) / 1000,
                                                              '' if returned > t_reset else ' (BEFORE it: a false RESET)'))
                    + ('' if old else '; candidates %d, a read followed %d, busy before the second read %d, no $8001 %d'
                       % (cpu.ram.get(S + 56, 0), cpu.ram.get(S + 64, 0), cpu.ram.get(S + 68, 0), cpu.ram.get(S + 40, 0)))))
        for ts, a, kind, m in bad[:int(__import__("os").environ.get("NBAD", "4"))]:
            print('      $%04X (%s) at %.1f us: %s' % (0x8000 + a, 'opcode' if kind == 'm1' else 'data', ts / 1000,
                                                  'late by %.0f ns' % -m if m > -1e8 else 'the wrong byte'))
    if not ok and not old and not quiet: print('      cvs:', [cpu.ram.get(S + 4 * i, 0) for i in range(2, 10)])
    return ok, len(bad), missed, extra, worst


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('elf')
    ap.add_argument('--old', action='store_true')
    ap.add_argument('--model', default='all')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--seeds', type=int, default=6)
    ap.add_argument('--ops', type=int, default=1500)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--flat', action='store_true', help='the plain ROM loop (cv_serve_flat)')
    ap.add_argument('--bounce', action='store_true', help="inputs may read wrong for 30 ns from each change of the cart's byte")
    ap.add_argument('--title', action='store_true', help='end with the BIOS title path (AA 55): RESET at its second header read')
    ap.add_argument('--busy', action='store_true', help='...with a game reading the cart all the while: no RESET')
    ap.add_argument('--phantom', action='store_true', help='...with a game that reads the cart 170 times in between: no RESET')
    ap.add_argument('--no-ramcopy', action='store_true', help="leave out code in RAM that reads $8000 and stays in RAM")
    a = ap.parse_args()
    models = ['nominal'] if a.model == 'all' else [a.model]   # (early, slow, late: by name; not what the console shows)
    fails = 0
    for m in models:
        tot = [0, 0, 0]
        w = {'m1': 1e9, 'rd': 1e9}
        seeds = [a.seed] if a.seed is not None else range(a.seeds)
        for s in seeds:
            ok, bad, missed, extra, worst = run(a.elf, a.old, m, s, a.ops, a.quiet, ramcopy=not a.no_ramcopy, flat=a.flat,
                                                title=a.title or a.busy or a.phantom, busy=a.busy, phantom=a.phantom, bounce=a.bounce)
            fails += not ok
            tot[0] += bad; tot[1] += missed; tot[2] += extra
            for k in w: w[k] = min(w[k], worst[k])
        print('%-7s %s: wrong bytes %d, selects missed %d, false %d; smallest margin: opcode %.0f ns, data %.0f ns'
              % (m, 'old' if a.old else 'plain' if a.flat else 'new', tot[0], tot[1], tot[2], w['m1'], w['rd']))
    print('cvserve_sim: %s' % ('ok' if not fails else '%d run(s) FAILED' % fails))
    sys.exit(1 if fails and not a.old else 0)
