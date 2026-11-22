#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Juergen Buchmueller, Andrei I. Holub
"""z80gen.py Z80.LST OUT.H -- MAME's Z80 as C.

MAME 0.288 describes the Z80 (src/devices/cpu/z80/z80.lst, by Juergen
Buchmueller and Andrei I. Holub, BSD-3-Clause; sha256 b2050f64...cac949eec)
as code per instruction, with a step wherever a cycle count is taken, and
builds its core from that with z80make.py. This does what z80make.py does,
in C, for the plain Z80 (no gen: prefixed variants):

  - each "+ N" and each "X !! access" takes the cycles and, if the cycles
    have run out, stops there; the next call carries on from that step,
    so the CPU stops and starts on exactly the cycles MAME's does
  - MAME keeps two copies of each instruction, one to carry on from a
    step and one without the steps' labels; this keeps one copy with a
    label at each step a stop can come back to, and z80.c's resume switch
    jumps there
  - the C++ of the list (members, templates, callbacks) becomes z80.c's
    names (see the replacements below); BUSACK's callback is kept, as
    Donkey Kong's 8257 DMA needs it (Z80_BUSACK, z80.c)

The output is #included by z80.c inside its execute function.
"""
import re, sys

class Line:
    def __init__(self, src, indent=None):
        self.str = src.strip()
        self.indent = indent if indent is not None else src[:len(src) - len(src.lstrip())]
    def has_indent(self):
        return self.indent != '' or self.str[0] in '{}'
    def is_comment(self):
        return self.str.startswith('#') and not self.str.startswith('#if') and not self.str.startswith('#endif')
    def with_str(self, s):
        return Line(s, self.indent)

class Macro:
    def __init__(self, name, args):
        self.name, self.args, self.src = name, args, []
    def apply(self, args):
        src = self.src
        if self.args is not None:
            for i, a in enumerate(args.split(',')):
                src = [Line(l.str.replace(self.args[i], a), l.indent) for l in src]
        return src

# the irq vector: the board's acknowledge callback (MAME's standard_irq_callback
# with the driver's IRQ callback, no daisy chain)
OVERRIDES = {
    'irqfetch': ['\tZ.tmp_irq_vector = z80_irq_ack();'],
}

def load(fname):
    macros, ops, cur = {}, {}, None
    for raw in open(fname):
        line = Line(raw)
        if not line.str or line.is_comment():
            continue
        if line.has_indent():
            if cur is None:
                continue
            if isinstance(cur, Macro):
                cur.src.append(line)
            else:
                cur[1].extend(expand(line, macros))
            continue
        tok = line.str.split()
        if tok[0] == 'macro':
            names = tok[1].split(':')
            m = Macro(names[-1], tok[2:] if len(tok) > 2 else None)
            if len(names) == 1:             # a variant's macro (r800:rop) is left out
                macros[names[0]] = m
            cur = m
            if names[0] in OVERRIDES and len(names) == 1:
                m.src = [Line(s) for s in OVERRIDES[names[0]]]
                cur = Macro('ignored', None)
            continue
        names = tok[0].split(':')
        if len(names) == 2:                 # a variant's opcode
            cur = ('ignored', [])
            continue
        key = names[0]
        cur = (key, [])
        ops.setdefault(key[:2], []).append(cur)
    return ops

def expand(line, macros):
    out, s = [], line.str
    tok = s.split()
    times = 1
    if len(tok) > 2 and tok[1] == '*':
        times = int(tok[0])
        tok = tok[2:]
        s = ' '.join(tok)
    for _ in range(times):
        if tok[0].startswith('@'):
            name = tok[0][1:]
            args = ' '.join(tok[1:]) if len(tok) > 1 else None
            if name not in macros:
                sys.exit('z80gen: no macro %s' % name)
            for ml in macros[name].apply(args):
                out.extend(expand(ml, macros))
        else:
            out.append(line.with_str(s))
    return out

SUBS = [
    (r'set_service_attention<SA_(\w+), *(\d)>\(\)', r'SET_SA(SA_\1, \2)'),
    (r'get_service_attention<SA_(\w+)>\(\)', r'GET_SA(SA_\1)'),
    (r'm_f\.(s|z|yx|h|pv)\(\)', lambda m: 'F_%s()' % m.group(1).upper()),
    (r'\bm_mreq_cycles\b', 'Z80_MREQ'), (r'\bm_m1_cycles\b', 'Z80_M1'), (r'\bm_iorq_cycles\b', 'Z80_IORQ'),
    (r'\bm_icount\b', 'ic'),
    (r'TDAT8 = m_io\.read_interruptible\((.*)\);', r'TDAT8 = IO_IN(\1);'),
    (r'm_io\.write_interruptible\((.*), TDAT8\);', r'IO_OUT(\1, TDAT8);'),
    (r'\bm_(refresh|nomreq)_cb\(', 'NOCB('),
    (r'\bm_(\w+)', r'Z.\1'),
    (r'\bu8\(', '(u8)('), (r'\bu16\(', '(u16)('),
    (r'\bdata_read\(', 'RD('), (r'\bstack_read\(', 'RD('),
    (r'\bdata_write\(', 'WR('), (r'\bstack_write\(', 'WR('),
    (r'\bopcode_read\(\)', 'RD_OP()'), (r'\barg_read\(\)', 'RD_ARG()'),
    (r'\binc\(', 'inc(&'), (r'\bdec\(', 'dec(&'),
    (r'swap\(Z\.af, Z\.af2\);', 'SWAP16(Z.af, Z.af2);'),
    (r'debugger_instruction_hook\(PC\);', 'Z80_HOOK();'),
    (r'^Z\.busack_cb\((\d)\);$', r'Z80_BUSACK(\1);'),
]
DROP = [r'^using std::swap;$', r'^LOGMASKED\(', r'^logerror\(', r'^debugger_wait_hook\(\);$', r'^Z\.irqack_cb\(',
        r'^daisy_call_reti_device\(\);$']

def conv(s):
    for a, b in SUBS:
        s = re.sub(a, b, s)
    return s

def body(key, src, out):
    """an opcode's code: steps as MAME's, a label after each one a stop comes back to"""
    step = 0
    for i, l in enumerate(src):
        tok = l.str.split()
        last = i + 1 == len(src)
        ind = '\t' + l.indent
        if tok[0] == '+':
            step += 1
            out.append('%sic -= %s;' % (ind, conv(' '.join(tok[1:]))))
            if last:
                out.append('%sif (ic <= 0) { Z.ref = 0xffff00; goto suspend; }' % ind)
            else:
                out.append('%sif (ic <= 0) { Z.ref = 0x%s%02x; goto suspend; }' % (ind, key, step))
                out.append('S_%s%02x:;' % (key, step))
        elif len(tok) > 2 and tok[1] == '!!':
            step += 1                       # (the step before the access: only for an access to be redone, never here)
            out.append('%s%s' % (ind, conv(' '.join(tok[2:]))))
            out.append('%sic -= %s;' % (ind, conv(tok[0])))
            if last:
                out.append('%sif (ic <= 0) { Z.ref = 0xffff00; goto suspend; }' % ind)
            else:
                step += 1
                out.append('%sif (ic <= 0) { Z.ref = 0x%s%02x; goto suspend; }' % (ind, key, step))
                out.append('S_%s%02x:;' % (key, step))
        else:
            s = conv(l.str)
            if any(re.search(d, s) for d in DROP):
                continue
            # the jumps to another opcode table (MAME's m_ref and "goto process")
            m = re.match(r'Z\.ref = \(0x(\w\w) << 16\) \| \(TDAT8 << 8\);$', s)
            if m:
                out.append('%sZ.ref = (0x%s << 16) | (TDAT8 << 8);' % (ind, m.group(1)))
                out.append('%sgoto T_%s;' % (ind, m.group(1)))
                continue
            m = re.match(r'Z\.ref = 0x(\w\w)(\w\w)00;$', s)
            if m and m.group(1) != 'ff':
                out.append('%sZ.ref = 0x%s%s00;' % (ind, m.group(1), m.group(2)))
                out.append('%sgoto O_%s%s;' % (ind, m.group(1), m.group(2)))
                continue
            if s == 'goto process;':
                continue
            s = re.sub(r'\breturn;', 'goto suspend;', s)
            out.append(ind + s)
    return step

def main():
    ops = load(sys.argv[1])
    out = ['/* Generated by tools/z80gen.py from MAME 0.288\'s src/devices/cpu/z80/z80.lst. Do not edit. */',
           '/* license:BSD-3-Clause, copyright-holders:Juergen Buchmueller, Andrei I. Holub */', '']
    resume = []
    order = ['ff', '00', 'cb', 'dd', 'ed', 'fd', 'fe']
    assert sorted(order) == sorted(ops), sorted(ops)
    for pre in order:
        if pre == 'ff':
            out.append('for (;;) {')
            # (not MAME's: the same steps, with fewer checks, when the cycles
            # left cover the whole opcode fetch and nothing needs attention;
            # a fetch that took the cycles, from the board's I/O, goes on as below)
            # (Z.ref is left: every stop sets its own, and nothing else reads it)
            out += ['\tif (__builtin_expect(ic > Z80_M1 && !Z.service_attention, 1)) {',
                    '\t\tu16 pc_ = PC;', '\t\tPRVPC = pc_;', '\t\tZ80_HOOK();', '\t\tTDAT8 = RD(pc_);', '\t\tic -= Z80_M1-2;',
                    '\t\tif (__builtin_expect(ic <= 2, 0)) goto F_fetch;', '\t\tic -= 2;',
                    '\t\tPC = (u16)(pc_ + 1);', '\t\tR++;', '\t\tQ = QT;', '\t\tQT = YXF;',
                    '\t\tgoto T_00;', '\t}']
            out.append('/* ffff: the instruction boundary: cycles, interrupts, HALT, the opcode fetch */')
            key, src = ops['ff'][0]
            start = len(out)
            n = body(key, src, out)
            # F_fetch: the plain fetch's first check (after the HALT's own fetch)
            fetch = [i for i in range(start, len(out)) if out[i].strip() == 'TDAT8 = RD_OP();'][-1]
            assert out[fetch + 1].strip() == 'ic -= Z80_M1-2;' and out[fetch + 2].strip().startswith('if (ic <= 0)'), out[fetch:fetch + 3]
            out.insert(fetch + 2, 'F_fetch:;')
            resume += ['%s%02x' % (key, s) for s in range(1, n + 1)]
            out.append('\tcontinue;')
            continue
        out.append('T_%s:' % pre)
        out.append('\tswitch (TDAT8) {')
        for key, src in ops[pre]:
            out.append('\tcase 0x%s: O_%s:' % (key[2:], key))
            n = body(key, src, out)
            resume += ['%s%02x' % (key, s) for s in range(1, n + 1)]
            out.append('\t\tcontinue;')
        out.append('\t}')
    out.append('}')
    text = '\n'.join(out)
    used = set(re.findall(r'goto (O_\w+);', text))     # (only the opcodes jumped to keep their labels)
    out = [re.sub(r' (O_\w+):$', lambda m: ' ' + m.group(1) + ':' if m.group(1) in used else '', l) for l in out]
    labels = set(re.findall(r'^S_(\w+):;', text, re.M))
    # (not MAME's m_ref, which also holds the opcode table to go on in: here
    # the code does that, and Z.ref is only where a stop carries on, read
    # by nothing but the switch below. So each place is a small number, 0
    # the instruction boundary, and the switch is a table, not a search.)
    keys = [k for k in resume if k in labels]
    index = {k: i + 1 for i, k in enumerate(keys)}
    def renum(l):
        m = re.match(r'^(.*)Z\.ref = 0x(\w{6});(.*)$', l)
        if m and m.group(2) == 'ffff00':
            return '%sZ.ref = 0;%s' % (m.group(1), m.group(3))
        if m and m.group(3).strip().startswith('goto suspend;'):
            return '%sZ.ref = %d;%s' % (m.group(1), index[m.group(2)], m.group(3))
        if m and m.group(2).endswith('00') and not m.group(3).strip():
            return None             # (a jump to another opcode's code: goto O_ follows)
        if re.match(r'^\s*Z\.ref = \(0x\w\w << 16\) \| \(TDAT8 << 8\);$', l):
            return None             # (a prefix's table: goto T_ follows)
        assert 'Z.ref' not in l, l
        return l
    out = [l for l in (renum(l) for l in out) if l is not None]
    res = ['/* where a stopped instruction carries on (Z.ref: 0 the instruction boundary, else a step) */', 'switch (Z.ref) {']
    res += ['case %d: goto S_%s;' % (index[k], k) for k in keys]
    res += ['default: break;', '}', '']
    with open(sys.argv[2], 'w') as f:
        f.write('\n'.join(out[:3] + res + out[3:]) + '\n')

main()
