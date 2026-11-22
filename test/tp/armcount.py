#!/usr/bin/env python3
"""armcount.py BENCH.elf SETDIR [lists] [--from n] [--profile [--blocks FN]] [--top n] [--trace FILE] [--lists FILE]

What a list (a TV frame) of Time Pilot costs the cart, in Cortex-M33
instructions (Unicorn). BENCH.elf is test/tp/arm_bench.c with the board, tpay, tpsnd,
tpcv and tprun, built by test/tp/armcount.sh with the plugin's flags. The game
runs `lists` lists (2400), playing itself (arm_bench.c); lists
from --from (1) on are counted, and the report gives the average and the
worst, and what a queue of lists makes of it (below). (Pac-Man's.) --profile: the cycles by function (the --top n,
16). --blocks FN: FN's costliest blocks and their source lines. --trace FILE: each list's count. --lists FILE: each list's length and a hash of its
bytes, as the M33 made them (to set builds side by side).

A list has 16.77 ms (TPCV_LIST_T of the console's T), which at the cart's
150 MHz is 2.52 million cycles, and core 0 has it to itself (core 1 serves
the console). The cycles are the Cortex-M33's book times for the
instructions run (capstone decodes them): one each, a load two, a load or
store of several registers one more each, a taken branch or a call three
(a block's last instruction, taken as taken), a divide six, the FPU's
divide and square root fourteen. That leaves out what the bus costs when
core 1 or the DMA has the same RAM bank, so the plugin's count of late
lists is the last word.

The plugin makes each list as soon as the last is published, and
cvframes holds one published list while the console runs another, so a
list may take longer than its time if those before it took less: the
console runs a list again (a repeat of its sound) only when the lists so
far took more than their time plus one list's. "late" counts those.

Needs unicorn, capstone and pyelftools (pip, in a venv: ARM_PY in armcount.sh).
"""
import bisect, os, sys
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS, CS_MODE_V8
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_CPU_ARM_CORTEX_M33

BASE, SIZE, STOP, SCS = 0x20000000, 0x100000, 0x30000000, 0xE000E000
LIST_CYCLES = 60036 / 3579545 * 150e6
cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS | CS_MODE_V8)

def cycles_of(code, addr):
    """a block's instructions and its cycles (above)"""
    ins = list(cs.disasm(bytes(code), addr))
    cyc = 0
    for k, i in enumerate(ins):
        m, ops = i.mnemonic.split(".")[0], i.op_str
        c = 1
        if m.startswith(("ldr", "vldr", "ldrex")):
            c = 2
        elif m in ("ldm", "ldmia", "ldmdb", "pop", "vpop", "vldmia", "vldmdb", "stm", "stmia", "stmdb", "push", "vpush",
                   "vstmia", "vstmdb"):
            c = 1 + ops.count(",") + 1 + (ops.count("-") * 2)
        elif m in ("sdiv", "udiv"):
            c = 6
        elif m in ("vdiv", "vsqrt"):
            c = 14
        if k == len(ins) - 1 and (m in ("b", "bl", "blx", "bx", "cbz", "cbnz", "tbb", "tbh") or
                                  (m.startswith("b") and len(m) == 3 and m not in ("bic", "bfi", "bfc", "bkpt")) or
                                  "pc" in ops.split(",")[0] or (m == "pop" and "pc" in ops)):
            c += 2
        cyc += c
    return len(ins), cyc

def opt(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

skip = set()
for name in ("--from", "--top", "--trace", "--lists", "--blocks"):
    if name in sys.argv:
        skip.update((sys.argv.index(name), sys.argv.index(name) + 1))
args = [a for i, a in enumerate(sys.argv) if i and i not in skip and not a.startswith("--")]
elf_path, setdir = args[:2]
lists = int(args[2]) if len(args) > 2 else 2400
first = int(opt("--from", "1"))
profile = "--profile" in sys.argv

files = {"bench_maincpu": ["tm1", "tm2", "tm3"], "bench_sound": ["tm7"], "bench_tiles": ["tm6"],
         "bench_sprites": ["tm4", "tm5"], "bench_proms": ["timeplt.b4", "timeplt.b5", "timeplt.e9", "timeplt.e12"]}

mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
mu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
mu.mem_map(BASE, SIZE)
mu.mem_map(STOP, 0x1000)
try:
    mu.mem_map(SCS, 0x1000)
except Exception:
    pass
elf = ELFFile(open(elf_path, "rb"))
for seg in elf.iter_segments():
    if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
        mu.mem_write(seg["p_paddr"], seg.data())
symtab = elf.get_section_by_name(".symtab")
sym = {s.name: s["st_value"] for s in symtab.iter_symbols()}
funcs = sorted((s["st_value"] & ~1, s["st_size"], s.name) for s in symtab.iter_symbols()
               if s["st_info"]["type"] == "STT_FUNC" and s["st_size"])
starts = [f[0] for f in funcs]
for name, parts in files.items():
    data = [open(os.path.join(setdir, f), "rb").read() for f in parts]
    mu.mem_write(sym[name], b"".join(data))

size_of, runs, total = {}, {}, [0, 0]

def on_block(uc, addr, size, _):
    n = size_of.get(addr)
    if n is None:
        n = size_of[addr] = cycles_of(uc.mem_read(addr, size), addr)
    total[0] += n[0]
    total[1] += n[1]
    if profile:
        runs[addr] = runs.get(addr, 0) + 1

# the stack is painted, and how much of the paint is gone at the end says
# how deep it went (the bench's calls, from the top of the map)
STACK_TOP, STACK_PAINT = BASE + SIZE - 16, 0x10000
mu.mem_write(STACK_TOP - STACK_PAINT, b"\xa5" * STACK_PAINT)

def call(name):
    mu.reg_write(UC_ARM_REG_SP, STACK_TOP)
    mu.reg_write(UC_ARM_REG_LR, STOP | 1)
    mu.emu_start(sym[name] | 1, STOP)

call("bench_init")
hooked = False
counts = []
# the queue: the console runs list k from (k + 1) L to (k + 2) L (L a
# list's time; the first one list in), so list k is due by (k + 1) L. It is
# made after list k - 1 is published, and published once list k - 1 is
# taken, at k L. A late list (the console ran the one before again) puts
# the rest back by L.
# Kept list by list, so the bench is told when the plugin would have
# published a list at once, list k - 1 already taken (bench_behind:
# unused by this bench). Lists before --from count as on time.
q_pub = 0.0
q_shift = 0
q_late = 0
q_behind = 0
lists_out = open(opt("--lists"), "w") if opt("--lists") else None
for l in range(1, lists + 1):
    if l >= first and not hooked:
        mu.hook_add(UC_HOOK_BLOCK, on_block)
        mu.ctl_flush_tb()
        hooked = True
    total[0] = total[1] = 0
    mu.mem_write(sym["bench_behind"], bytes([q_behind]))
    call("bench_list")
    if lists_out:
        n_out = int.from_bytes(mu.mem_read(sym["bench_len"], 4), "little")
        h = 2166136261
        for b in mu.mem_read(sym["bench_out"], n_out):
            h = (h ^ b) * 16777619 & 0xffffffff
        lists_out.write(f"{l} {n_out} {h:08x}\n")
    if l >= first:
        counts.append((total[1], l, total[0]))
        k = len(counts) - 1
        done = q_pub + total[1]
        q_behind = 1 if done >= (k + q_shift) * LIST_CYCLES else 0
        q_pub = max(done, (k + q_shift) * LIST_CYCLES)
        if q_pub > (k + 1 + q_shift) * LIST_CYCLES:
            q_late += 1
            q_shift += 1

n = len(counts)
avg = sum(c[0] for c in counts) / n
avg_ins = sum(c[2] for c in counts) / n
worst = max(counts)
ms = lambda k: k / 150e6 * 1000
over = sum(1 for c in counts if c[0] > LIST_CYCLES)
srt = sorted(c[0] for c in counts)
late = q_late
hurried = int.from_bytes(mu.mem_read(sym["bench_hurried"], 4), "little")
pictures = int.from_bytes(mu.mem_read(sym["bench_pictures"], 4), "little")
print(f"lists {first}-{lists}: {avg_ins:.0f} instructions, "
      f"{avg:.0f} cycles a list on average ({avg / avg_ins:.2f} an instruction): {ms(avg):.1f} ms of the list's "
      f"{LIST_CYCLES / 150e3:.2f} at 150 MHz ({100 * avg / LIST_CYCLES:.0f}%)")
print(f"99th percentile {ms(srt[n * 99 // 100]):.1f} ms; the worst, list {worst[1]}: {ms(worst[0]):.1f} ms; "
      f"lists over their time: {over}, lists late with the queue: {late}; pictures {pictures}, lists that waited for one: {hurried}")
paint = bytes(mu.mem_read(STACK_TOP - STACK_PAINT, STACK_PAINT))
print(f"stack: {STACK_PAINT - (len(paint) - len(paint.lstrip(b'\xa5')))} bytes deep at most, below bench_list")
if opt("--trace"):
    with open(opt("--trace"), "w") as f:
        for c, l, i in counts:
            f.write(f"{l} {i} {c}\n")
if profile:
    by_func = {}
    for addr, k in runs.items():
        i = bisect.bisect_right(starts, addr) - 1
        name = funcs[i][2] if i >= 0 and addr < funcs[i][0] + funcs[i][1] else "?"
        by_func[name] = by_func.get(name, 0) + k * size_of[addr][1]
    tot = sum(by_func.values())
    if opt("--blocks"):                 # the function's blocks by cost, with their source lines
        import subprocess
        fn = opt("--blocks")
        blk = sorted(((k * size_of[a][1], a) for a, k in runs.items()
                      if (lambda i: i >= 0 and funcs[i][2] == fn and a < funcs[i][0] + funcs[i][1])(bisect.bisect_right(starts, a) - 1)),
                     reverse=True)[:25]
        for c, a in blk:
            line = subprocess.run(["arm-none-eabi-addr2line", "-e", elf_path, "-i", hex(a)], capture_output=True, text=True).stdout.split()
            print(f"  {fn}+{a:08x} {c // n:9d} cycles a list  {' <- '.join(x.split('/')[-1] for x in line)}")
    for name, k in sorted(by_func.items(), key=lambda kv: -kv[1])[:int(opt("--top", "16"))]:
        print(f"  {name:30s} {k // n:9d} cycles a list  {100 * k / tot:5.1f}%")
