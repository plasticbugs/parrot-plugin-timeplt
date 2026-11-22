#!/usr/bin/env python3
"""onarm.py PLUGIN.elf [--cache] [--want NOTE] [--stops TEXT] [--seconds N] [--save FILE] [--vm FILE]

A Lua app's ARM code (the cart's build) on an emulated Cortex-M33
(Unicorn), from main(). The SDK's edge is stubbed: the console answers as
the screen kernel does, a TV frame at a time; there is no card and nothing
on USB; the PSRAM is 8 MB of memory. Everything else is the ELF's own code:
Lua, the runtime, the allocator, the C library.

The clock is the instruction count: 1.5 cycles an instruction at 150 MHz,
the figure the cart's other plugins are measured with. So the script's
time limit, its 60 updates a second and its pictures all happen when they
would on the cart.

It prints each note the script leaves (menu_note) with the ARM
instructions and Lua VM instructions since the one before, and at the end
how deep the C stack went and how much of each heap was used. The run
ends at the note --want names (default: one starting "end" or "deep done"
or "api"), or fails after --seconds of cart time (default 120).

--save FILE writes each note's counts. --vm FILE reads them from a run of
the same script built with a count hook (-DCVLUA_COUNT_HOOK), which is how
the Lua VM instructions are counted: the cart's own build has no count
hook, because one slows Lua down.

--stops TEXT: the script is meant to end on the runtime's error screen,
with TEXT in the message.

--cache also models the chip's cache in front of the PSRAM (16 KB, 2 ways,
8-byte lines) and counts the reads and writes that miss it. Treat it as a
rough guide: with these memory hooks on, the emulator itself sometimes
misreads the PSRAM, and the run then fails.

Needs unicorn and pyelftools (pip, in a venv). Exit status 1 for a fault,
a stack overflow, a panic, or a script that didn't reach its last note.
"""
import struct
import sys

from elftools.elf.elffile import ELFFile
from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                     UC_HOOK_MEM_UNMAPPED, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE)
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
                               UC_ARM_REG_LR, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M33, UC_ARM_REG_C1_C0_2,
                               UC_ARM_REG_FPEXC)

TIMER_RAWH, TIMER_RAWL = 0x400B0024, 0x400B0028
STOP = 0x30000000
FRAME_US = 16683
PSRAM, PSRAM_SIZE = 0x11000000, 0x800000
CYCLES = 1.5                                # an instruction, as the cart's other plugins are measured
MHZ = 150

argv = sys.argv[1:]
def option(name, default=None):
    if name in argv:
        i = argv.index(name)
        v = argv[i + 1]
        del argv[i:i + 2]
        return v
    return default
want = option("--want")
save, vm_file = option("--save"), option("--vm")
stops = option("--stops")
limit_s = float(option("--seconds", "120"))
cache_on = "--cache" in argv
if cache_on: argv.remove("--cache")
quiet = "--quiet" in argv
if quiet: argv.remove("--quiet")
if len(argv) != 1: sys.exit(__doc__)
elf_path = argv[0]

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
uc.mem_map(0x20000000, 0x84000)             # SRAM and the scratch banks
uc.mem_map(PSRAM, PSRAM_SIZE)
uc.mem_map(0x40000000, 0x200000)            # peripherals: memory, but for the timer
uc.mem_map(0x50000000, 0x400000)
uc.mem_map(0xD0000000, 0x40000)
uc.mem_map(0xE0000000, 0x100000)
uc.mem_map(STOP, 0x1000)
uc.mem_write(STOP, b"\xfe\xe7" * 8)
sym, size_of_sym = {}, {}
elf = ELFFile(open(elf_path, "rb"))
for seg in elf.iter_segments():
    if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
        uc.mem_write(seg["p_vaddr"], seg.data())
for s in elf.get_section_by_name(".symtab").iter_symbols():
    if s.name:
        sym.setdefault(s.name, s["st_value"])
        size_of_sym.setdefault(s.name, s["st_size"])
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))    # the FPU on, as the SDK leaves it
uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

insns = [0]                                 # ARM instructions run so far
st = dict(failure=None, hooks=0, lists=0, list_bytes=0, sounds=0, done=False)
notes = []                                  # (text, insns, hooks)

def now_us():
    return 1000 + int(insns[0] * CYCLES / MHZ)

def cstr(a, n=128):
    return bytes(uc.mem_read(a, n)).split(b"\0")[0].decode("latin1") if a else ""

# ---- the console, as test/host/host.c models the screen kernel -----------------------
k = dict(frame=0, reports=[], hello=True, done=0, pending=None)

def console_step():
    while now_us() >= (k["frame"] + 1) * FRAME_US:
        k["frame"] += 1
        if k["pending"] is not None:        # the list sent last frame runs now
            full, seq = k["pending"]
            if full or not k["hello"]:
                k["hello"] = False
            k["done"] = seq
            k["pending"] = None
        k["reports"] = [(2, 0x1F), (3, 0x1F), (1 if k["hello"] else 0, k["done"] & 31)]

def cvk_report():
    console_step()
    if not k["reports"]: return 0
    kind, bits = k["reports"].pop(0)
    uc.mem_write(uc.reg_read(UC_ARM_REG_R1), struct.pack("<i", kind))
    uc.mem_write(uc.reg_read(UC_ARM_REG_R2), bytes([bits]))
    return 1

def cvk_list():
    n, full, seq = uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2), uc.reg_read(UC_ARM_REG_R3)
    if k["pending"] is not None: st["failure"] = st["failure"] or "a list sent before the last one ran"
    k["pending"] = (bool(full), seq)
    st["lists"] += 1
    st["list_bytes"] += n

def cvk_note():
    s = cstr(uc.reg_read(UC_ARM_REG_R0))
    if not s or (notes and notes[-1][0] == s): return
    notes.append((s, insns[0], st["hooks"]))
    if not quiet: print(f"  note {s!r}", flush=True)
    last = want or ("end", "deep done", "api ")
    if s.startswith(last):
        st["done"] = True
        uc.emu_stop()

# the runtime's error screen
def stop_with():
    msg = cstr(uc.reg_read(UC_ARM_REG_R0), 600)
    if not quiet: print("  the script stopped: " + msg.replace("\n", "\n    "), flush=True)
    if stops is not None and stops in msg: st["done"] = True
    else: st["failure"] = st["failure"] or "the script stopped: " + msg.split("\n")[0]
    uc.emu_stop()
if "stop_with" in sym:
    uc.hook_add(UC_HOOK_CODE, lambda u, a, n, _: stop_with(), begin=sym["stop_with"] & ~1, end=sym["stop_with"] & ~1)

def cvk_sound():
    st["sounds"] += 1

def cvusb_status():
    uc.mem_write(uc.reg_read(UC_ARM_REG_R0), bytes(4))

def fail(what):
    def f():
        st["failure"] = st["failure"] or what + f" (lr {uc.reg_read(UC_ARM_REG_LR):08x})"
        uc.emu_stop()
    return f

ret0, ret1 = (lambda: 0), (lambda: 1)
stubs = {"cvk_start": ret0, "cvk_task": ret0, "cvk_report": cvk_report, "cvk_list": cvk_list, "cvk_note": cvk_note,
         "cvk_sound": cvk_sound, "cvk_mb": ret0, "cvk_publish": ret0, "cvk_launch": ret0, "cvk_console_on": ret1,
         "cvusb_start": ret0, "cvusb_task": ret0, "cvusb_host_now": ret0, "cvusb_mode": (lambda: 4),
         "cvusb_status": cvusb_status, "cvusb_pad": ret0, "cvusb_pad_kind": ret0, "cvusb_pad_dirs": ret0,
         "cvusb_key": ret0, "cvusb_mouse": ret0, "cvusb_midi_read": ret0, "cvusb_midi_send": ret0,
         "cvsd_card_in": ret0, "cvsd_init": ret0, "cvsd_read": ret0, "cvsd_write": ret0,
         "cvpsram_init": ret1, "port_watch_start": ret0, "get_rand_32": (lambda: 0x12345678),
         "_retrieve_unique_id_on_boot": ret0, "get_rand_64": ret0,
         "watchdog_reboot": fail("watchdog_reboot()"), "cvlua_fault": fail("a fault"),
         "isr_hardfault": fail("a hard fault"), "panic": fail("panic()"), "_exit": fail("_exit()")}
# Double arithmetic on the cart is the RP2350's own coprocessor, which the
# emulator doesn't have: the SDK's routines for it are answered here. (Lua's
# numbers are floats, on the FPU. Doubles only come up when a number is
# printed or read.)
import math
def dreg(lo):
    return struct.unpack("<d", struct.pack("<II", uc.reg_read(lo), uc.reg_read(lo + 1)))[0]
def dret(v):
    try: b = struct.pack("<d", v)
    except OverflowError: b = struct.pack("<d", math.inf if v > 0 else -math.inf)
    lo, hi = struct.unpack("<II", b)
    uc.reg_write(UC_ARM_REG_R0, lo)
    uc.reg_write(UC_ARM_REG_R1, hi)
def i32(v): return v - (1 << 32) if v & 0x80000000 else v
def i64(lo, hi):
    v = lo | hi << 32
    return v - (1 << 64) if v & (1 << 63) else v
def r(n): return uc.reg_read(UC_ARM_REG_R0 + n)
def clamp(v, lo, hi): return lo if v != v or v < lo else hi if v > hi else int(v)
def ddiv():
    a, b = dreg(UC_ARM_REG_R0), dreg(UC_ARM_REG_R2)
    dret(a / b if b else (math.nan if a == 0 or a != a else math.copysign(math.inf, a) * math.copysign(1, b)))
def d2f():
    v = dreg(UC_ARM_REG_R0)
    try: f = struct.pack("<f", v)
    except OverflowError: f = struct.pack("<f", math.inf if v > 0 else -math.inf)
    uc.reg_write(UC_ARM_REG_R0, struct.unpack("<I", f)[0])
def safe(fn):
    def g():
        try: dret(fn(dreg(UC_ARM_REG_R0)))
        except (ValueError, OverflowError): dret(math.nan)
    return g
doubles = {
    "__wrap___aeabi_dadd": lambda: dret(dreg(UC_ARM_REG_R0) + dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dsub": lambda: dret(dreg(UC_ARM_REG_R0) - dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dmul": lambda: dret(dreg(UC_ARM_REG_R0) * dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_ddiv": ddiv,
    "__wrap___aeabi_d2f": d2f,
    "__wrap___aeabi_d2iz": lambda: clamp(dreg(UC_ARM_REG_R0), -(1 << 31), (1 << 31) - 1) & 0xFFFFFFFF,
    "__wrap___aeabi_d2uiz": lambda: clamp(dreg(UC_ARM_REG_R0), 0, (1 << 32) - 1),
    "__wrap___aeabi_dcmplt": lambda: int(dreg(UC_ARM_REG_R0) < dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dcmple": lambda: int(dreg(UC_ARM_REG_R0) <= dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dcmpgt": lambda: int(dreg(UC_ARM_REG_R0) > dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dcmpge": lambda: int(dreg(UC_ARM_REG_R0) >= dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dcmpeq": lambda: int(dreg(UC_ARM_REG_R0) == dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_dcmpun": lambda: int(dreg(UC_ARM_REG_R0) != dreg(UC_ARM_REG_R0) or dreg(UC_ARM_REG_R2) != dreg(UC_ARM_REG_R2)),
    "__wrap___aeabi_i2d": lambda: dret(float(i32(r(0)))),
    "__wrap___aeabi_ui2d": lambda: dret(float(r(0))),
    "__wrap___aeabi_l2d": lambda: dret(float(i64(r(0), r(1)))),
    "__wrap___aeabi_ul2d": lambda: dret(float(r(0) | r(1) << 32)),
    "__wrap_exp": safe(math.exp), "__wrap_log": safe(math.log),
}
stubs.update(doubles)

stub_at = {}
for name, fn in stubs.items():
    a = sym.get(name)
    if a is None: continue
    a &= ~1
    uc.mem_write(a, b"\x70\x47")            # bx lr
    stub_at[a] = fn

def on_stub(u, addr, size, _):
    r = stub_at[addr]()
    if r is not None: u.reg_write(UC_ARM_REG_R0, int(r))
for a in stub_at: uc.hook_add(UC_HOOK_CODE, on_stub, begin=a, end=a)

# the runtime's hook, each time the timer has set it
def on_hook(u, addr, size, _):
    st["hooks"] += 1
if "cvlua_hook" in sym: uc.hook_add(UC_HOOK_CODE, on_hook, begin=sym["cvlua_hook"] & ~1, end=sym["cvlua_hook"] & ~1)

def on_timer(u, access, addr, size, value, _):
    t = now_us()
    if t > limit_s * 1e6:
        st["failure"] = st["failure"] or f"still running after {limit_s:.0f} s of cart time; the last note was {notes[-1][0] if notes else None!r}"
        u.emu_stop()
    u.mem_write(TIMER_RAWH, struct.pack("<II", 0, t & 0xFFFFFFFF))
uc.hook_add(UC_HOOK_MEM_READ, on_timer, begin=TIMER_RAWH, end=TIMER_RAWL + 3)

def on_unmapped(u, access, addr, size, value, _):
    st["failure"] = f"fault: access to {addr:08x} at pc {u.reg_read(UC_ARM_REG_PC):08x} lr {u.reg_read(UC_ARM_REG_LR):08x}"
    return False
uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)

# ---- the PSRAM behind the chip's cache: 16 KB, 2 ways, 8-byte lines -------------------
SETS = 1024
cache = [[-1, -1] for _ in range(SETS)]     # each set: the line used last, the other
dirty = [[False, False] for _ in range(SETS)]
c = dict(reads=0, writes=0, misses=0, writebacks=0)

def touch(line, write):
    s = cache[line & (SETS - 1)]
    d = dirty[line & (SETS - 1)]
    if s[0] == line:
        if write: d[0] = True
        return
    if s[1] == line:
        s[0], s[1] = s[1], s[0]
        d[0], d[1] = d[1], d[0]
        if write: d[0] = True
        return
    c["misses"] += 1
    if d[1]: c["writebacks"] += 1
    s[1], d[1] = s[0], d[0]
    s[0], d[0] = line, write

def on_psram(u, access, addr, size, value, _):
    write = access == 17 or access == UC_HOOK_MEM_WRITE     # UC_MEM_WRITE
    c["writes" if write else "reads"] += 1
    first, last = addr >> 3, (addr + size - 1) >> 3
    touch(first, write)
    if last != first: touch(last, write)
if cache_on:
    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_psram, begin=PSRAM, end=PSRAM + PSRAM_SIZE - 1)

block_len = {}
def on_block(u, addr, size, _):
    n = block_len.get(addr)
    if n is None:
        code = u.mem_read(addr, size)
        n = i = 0
        while i < size:
            i += 4 if (code[i + 1] >> 3) in (0x1D, 0x1E, 0x1F) else 2
            n += 1
        block_len[addr] = n
    insns[0] += n
uc.hook_add(UC_HOOK_BLOCK, on_block)

def call(fn):
    uc.reg_write(UC_ARM_REG_SP, top)
    uc.reg_write(UC_ARM_REG_LR, STOP | 1)
    uc.emu_start(fn | 1, STOP)

# The cart's 4 ms timer, which sets Lua's hook (port.c): the emulator has
# no interrupts, so the run is stopped at the end of a block, cvlua_tick()
# is called with the registers set aside, and the run goes on from where it
# was.
TICK_US = 4000
tick = dict(next=TICK_US, due=False, on=False)
def on_block_tick(u, addr, size, _):
    if tick["on"] and insns[0] * CYCLES / MHZ >= tick["next"]:
        tick["due"] = True
        u.emu_stop()
uc.hook_add(UC_HOOK_BLOCK, on_block_tick)

top = sym["__StackTop"]
try:
    call(sym["runtime_init_mutex"])
    for at in range(sym["__init_array_start"], sym["__init_array_end"], 4):
        call(struct.unpack("<I", uc.mem_read(at, 4))[0])
    insns[0] = 0
    uc.reg_write(UC_ARM_REG_SP, top)
    uc.reg_write(UC_ARM_REG_LR, STOP | 1)
    pc = sym["main"]
    tick["on"] = "cvlua_tick" in sym
    while True:
        uc.emu_start(pc | 1, STOP)
        if not tick["due"] or st["done"] or st["failure"]: break
        tick["due"] = False
        tick["next"] = insns[0] * CYCLES / MHZ + TICK_US
        pc = uc.reg_read(UC_ARM_REG_PC)
        ctx = uc.context_save()
        tick["on"] = False
        uc.reg_write(UC_ARM_REG_LR, STOP | 1)
        uc.emu_start(sym["cvlua_tick"] | 1, STOP)
        tick["on"] = True
        uc.context_restore(ctx)
except UcError as e:
    sp, lim = uc.reg_read(UC_ARM_REG_SP), sym.get("stack", 0)
    over = " (the stack pointer is at its limit: a stack overflow)" if lim and sp <= lim + 64 else ""
    st["failure"] = st["failure"] or f"emulator: {e} at pc {uc.reg_read(UC_ARM_REG_PC):08x} sp {sp:08x}{over}"

# ---- the report ----------------------------------------------------------------------
ms = lambda n: n * CYCLES / (MHZ * 1e6) * 1000
import json
if save: json.dump({s1: [i1 - i0, (h1 - h0) * 1000] for (s0, i0, h0), (s1, i1, h1) in zip(notes, notes[1:])}, open(save, "w"))
vm = json.load(open(vm_file)) if vm_file else None
if len(notes) > 1 and notes[0][0] == "start":
    print(f"{'test':10s} {'ARM instr':>12s} {'ms on the cart':>15s} {'Lua VM instr':>13s} {'ARM a VM instr':>15s} {'VM instr a second':>18s}")
    tot_i = tot_v = 0
    for (s0, i0, h0), (s1, i1, h1) in zip(notes, notes[1:]):
        if s1.startswith("end"): break
        di = i1 - i0
        dv = vm[s1][1] if vm and s1 in vm else 0 if vm_file else (h1 - h0) * 1000
        tot_i += di; tot_v += dv
        per = di / dv if dv else 0
        rate = dv / (ms(di) / 1000) if di else 0
        print(f"{s1:10s} {di:12d} {ms(di):15.1f} {dv:13d} {per:15.1f} {rate:18.0f}")
    if tot_v:
        rate = tot_v / (ms(tot_i) / 1000)
        print(f"{'all':10s} {tot_i:12d} {ms(tot_i):15.1f} {tot_v:13d} {tot_i / tot_v:15.1f} {rate:18.0f}")
        print(f"a TV frame (16.7 ms) is {rate * FRAME_US / 1e6:.0f} Lua VM instructions; 1000 of them take {1000 / rate * 1e6:.0f} us")
if "stack" in sym:
    n = size_of_sym["stack"]
    data = bytes(uc.mem_read(sym["stack"], n))
    clean = 0
    while clean < n and data[clean:clean + 4] == b"\xA5\xA5\xA5\xA5": clean += 4
    print(f"the C stack went {n - clean} bytes deep of {n}")
def heap(name):
    a = sym.get(name)
    if a is None: return None
    base, size, used, peak = struct.unpack("<IIII", uc.mem_read(a, 16))
    return size, used, peak
for name, label in (("sram", "the chip's RAM"), ("psram", "the PSRAM")):
    h = heap(name)
    if h: print(f"Lua's heap in {label}: {h[0]} bytes, {h[1]} in use at the end, {h[2]} at the most")
print(f"{st['lists']} pictures sent ({st['list_bytes']} bytes), {st['sounds']} sound changes, "
      f"{ms(insns[0]) / 1000:.2f} s of cart time, {insns[0]} instructions")
if cache_on:
    acc = c["reads"] + c["writes"]
    print(f"PSRAM: {c['reads']} reads, {c['writes']} writes, {c['misses']} missed the cache "
          f"({100 * c['misses'] / acc if acc else 0:.1f}%), {c['writebacks']} lines written back")
    # a miss reads 8 bytes over QPI at 75 MHz: 8 command and address clocks,
    # 6 to wait, 16 of data, and the cache's own turn: about 70 system
    # clocks. A line written back is about 55 more.
    extra = c["misses"] * 70 + c["writebacks"] * 55
    base = insns[0] * CYCLES
    print(f"at 70 clocks a miss and 55 a line written back: {extra / (MHZ * 1e6) * 1000:.1f} ms more, "
          f"{100 * extra / base if base else 0:.1f}% on top of the instructions")
if st["failure"]: print("FAILED:", st["failure"])
elif not st["done"]: print("FAILED: the script didn't reach its last note")
sys.exit(0 if st["done"] and not st["failure"] else 1)
