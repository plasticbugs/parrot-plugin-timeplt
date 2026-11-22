#!/bin/sh
# trace.sh [seconds] -- both Z80s against MAME's, instruction by instruction.
#
# MAME's debugger (headless, -debug -debugger none) traces each CPU from
# its second frame in the attract mode: each instruction's registers and
# the CPU's total cycles. tp_host writes the same from the model (-trace,
# -strace). Every line must be the same. Everything goes under $TMPDIR
# (TP_WORK). TP_ROMS is the timeplt.zip to use.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../src" && pwd)
secs=${1:-2}
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
set_dir=$("$here/setup.sh")
out="$work/trace"
rm -rf "${out:?}"; mkdir -p "$out"
cc -O2 -Wall -I"$src" -o "$out/tp_host" "$here/tp_host.c" "$src/tp.c" "$src/tpay.c" "$src/tpz80.c" -lm
"$out/tp_host" "$set_dir" -frames $(( secs * 60 + 10 )) -trace "$out/model.tr" -strace "$out/model.str" 2>/dev/null

# MAME's tracelog reads the registers of the debugger's visible CPU, so
# each CPU is traced in a run of its own, made the visible one
fmt="@ %04X %04X %04X %04X %04X %04X %04X %04X %d"
regs="pc,sp,af,bc,de,hl,ix,iy,totalcycles"
run_trace() {           # CPU-TAG FILE
    cat > "$out/trace.lua" <<LUA
if not _G.tp_traced then
  _G.tp_traced = true
  _G.tp_n = 0
  _G.tp_sub = emu.register_frame_done(function()
    _G.tp_n = _G.tp_n + 1
    if _G.tp_n ~= 2 then return end
    manager.machine.debugger.visible_cpu = manager.machine.devices['$1']
    manager.machine.debugger:command('trace $out/$2,$1,noloop,{tracelog "$fmt\\\\n",$regs}')
  end)
end
LUA
    rp=$(mktemp -d "${TMPDIR:-/tmp}/tprp.XXXXXX")
    cp "$TP_ROMS" "$rp/timeplt.zip"
    mkdir "$rp/cfg" "$rp/nvram"
    SDL_VIDEODRIVER=dummy mame timeplt -rompath "$rp" -debug -debugger none \
        -video none -videodriver dummy -window -nomaximize -sound none \
        -nothrottle -skip_gameinfo -seconds_to_run "$secs" \
        -cfg_directory "$rp/cfg" -nvram_directory "$rp/nvram" -snapshot_directory "$rp/snap" \
        -autoboot_delay 0 -autoboot_script "$out/trace.lua" >"$out/mame-$2.log" 2>&1 || true
    rm -rf "${rp:?}"
}
run_trace :maincpu mame.tr
run_trace :timeplt_audio:tpsound mame.str

python3 -I - "$out" <<'PY'
import sys
out = sys.argv[1]

def at_lines(path):
    try:
        with open(path) as f:
            return [l.split() for l in f if l.startswith("@ ")]
    except FileNotFoundError:
        return []

bad = 0
for cpu, mt, dt in (("main CPU", "mame.tr", "model.tr"), ("sound CPU", "mame.str", "model.str")):
    mame = at_lines(f"{out}/{mt}")[:-1]         # (the last may be cut off by the exit)
    model = at_lines(f"{out}/{dt}")
    if not mame:
        print(f"{cpu}: MAME wrote no trace (see mame-{mt}.log)"); bad = 1; continue
    start = next((i for i, l in enumerate(model) if l == mame[0]), None)
    if start is None:
        print(f"{cpu}: MAME's first line isn't in the model's: {' '.join(mame[0])}"); bad = 1; continue
    n = min(len(mame), len(model) - start)
    for i in range(n):
        if mame[i] != model[start + i]:
            print(f"{cpu}: instruction {i} differs\n  MAME  {' '.join(mame[i])}\n  model {' '.join(model[start + i])}")
            bad = 1
            break
    else:
        print(f"{cpu}: {n} instructions the same, registers and cycle counts (from cycle {mame[0][-1]})")
sys.exit(bad)
PY
