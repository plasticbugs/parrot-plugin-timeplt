#!/bin/sh
# lua/test/onarm.sh [BUILD_DIR] -- the Lua runtime's ARM code, as the cart
# runs it, on an emulated Cortex-M33 (test/onarm.py has the details). It
# builds lua/test/arm for the cart, then:
#
#   api     every function a script can call, checked from a script
#   deep    every way to nest C calls, taken to Lua's limit: the C stack
#           must hold them all
#   bench   how fast Lua runs, and the same with every value in the PSRAM
#
# Needs the cart's toolchain (docs/getting-started.md) and a Python with
# unicorn and pyelftools:
#
#   python3 -m venv /tmp/onarm && /tmp/onarm/bin/pip install unicorn pyelftools
#   ONARM_PY=/tmp/onarm/bin/python lua/test/onarm.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
build=${1:-${TMPDIR:-/tmp}/parrotcart-test/lua-arm}
py=${ONARM_PY:-python3}
"$py" -c 'import unicorn, elftools' 2>/dev/null || { echo "onarm.sh: $py has no unicorn and pyelftools (set ONARM_PY)"; exit 2; }
mkdir -p "$build"
cmake -S "$here/arm" -B "$build" -G Ninja ${PICO_SDK_PATH:+-DPICO_SDK_PATH="$PICO_SDK_PATH"} > "$build/configure.log" 2>&1 || { cat "$build/configure.log"; exit 1; }
ninja -C "$build" > "$build/build.log" 2>&1 || { grep -E 'error|FAILED' "$build/build.log" | head -20; exit 1; }
status=0
run() { echo "== $1"; shift; "$py" -I "$here/onarm.py" "$@" || status=1; }
run "api" "$build/api.elf" --quiet
run "deep" "$build/deep.elf" --quiet
"$py" -I "$here/onarm.py" "$build/bench_count.elf" --quiet --save "$build/vm.json" > "$build/bench_count.txt" || status=1
echo "== bench, with a hook that counts instructions (as on a computer)"; grep -E '^(test|all|a TV)' "$build/bench_count.txt"
run "bench, as the cart is built" "$build/bench.elf" --quiet --vm "$build/vm.json"
run "bench, every value in the PSRAM" "$build/bench_psram.elf" --quiet --vm "$build/vm.json"
[ $status = 0 ] && echo "all checks on the emulated chip passed"
exit $status
