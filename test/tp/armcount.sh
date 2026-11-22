#!/bin/sh
# armcount.sh [lists] [--from n] [--profile [--blocks FN]] [--top n] [--trace FILE] [--lists FILE]
# -- what a list costs the cart: the board, the AYs, the sound, the
# picture and the list built for the Cortex-M33 with the plugin's flags
# (its FPU on), run on Unicorn with its instructions and cycles counted
# (test/tp/armcount.py, Donkey Kong's). The game plays itself
# (arm_bench.c). ARM_PY: a Python with unicorn, capstone and pyelftools.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src=$here/../../src
work=${TP_WORK:-${TMPDIR:-/tmp}/timeplt-test}
set_dir=$("$here/setup.sh")
out="$work/arm"
mkdir -p "$out"
# each file with the plugin's flags (CMakeLists.txt): the picture -O2, the rest -O3
cflags="-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -mfpu=fpv5-sp-d16 -g -DNDEBUG \
    -DZ80_NO_TRACE ${TP_CFLAGS:-} -ffunction-sections -I$src -I$here/../../sdk/firmware/revc/sdk"
objs=
for f in "$here/arm_bench.c" "$src/tp.c" "$src/tpz80.c" "$src/tpay.c" "$src/tpsnd.c" \
         "$src/tpcv.c" "$src/tprun.c" "$here/../../sdk/firmware/revc/sdk/cvscale.c"; do
    case $(basename "$f") in tpcv.c) opt=${TP_OPT_CV:--O2} ;; *) opt=-O3 ;; esac
    o="$out/$(basename "$f" .c).o"
    # shellcheck disable=SC2086
    arm-none-eabi-gcc $cflags $opt -c -o "$o" "$f"
    objs="$objs $o"
done
# shellcheck disable=SC2086
arm-none-eabi-gcc $cflags -Wl,--gc-sections -nostartfiles \
    -Wl,-e,bench_init -Wl,-Ttext=0x20000000 -Wl,--undefined=bench_list -Wl,--undefined=bench_maincpu \
    -o "$out/bench.elf" $objs -lm 2>&1 |
    grep -v "warning: cannot find entry\|_exit\|not implemented\|_sbrk\|_write\|_close\|_lseek\|_read\|_fstat\|_isatty\|_getpid\|_kill" || true
arm-none-eabi-size "$out/bench.elf"
exec "${ARM_PY:-python3}" -I "$here/armcount.py" "$out/bench.elf" "$set_dir" "$@"
