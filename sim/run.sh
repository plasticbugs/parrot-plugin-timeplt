#!/bin/sh
# sim/run.sh PLUGIN_DIR [options] -- a plugin in the simulator: built for
# this computer against the console's models, then run in a window with
# sound and controls (docs/simulator.md).
#
#   sim/run.sh src                      the template's plugin
#   sim/run.sh examples/sound           an example
#   sim/run.sh src --card ~/sdcard      with a folder as the SD card
#   sim/run.sh src --help               the simulator's options and keys
#
# PLUGIN_DIR is the folder with the plugin's .c (and .cpp) files. For a
# plugin that needs more to build, before the simulator's own options:
#   --src PATTERN     more sources, relative to PLUGIN_DIR (a vendored
#                     library: --src 'vendor/lib/*.c')
#   --include DIR     another include folder
#   --define NAME[=V] a define
#   --no-sanitize     without ASan and UBSan (faster)
# Everything else goes to the simulator. It is built in build-sim/NAME;
# building again takes seconds. On Windows it runs in Git Bash (with Visual
# Studio) and in MSYS2's shells (with their compilers).
set -e
case "$1" in
    ""|-h|--help) sed -n '2,21p' "$0" | grep '^#' | sed 's/^# \{0,1\}//'; exit 0 ;;
esac
# a folder's full path, as the compiler wants it (C:/... on Windows)
full() { (cd "$1" && { pwd -W 2>/dev/null || pwd; }); }
kit=$(full "$(dirname "$0")/..")
dir=$(full "$1") || exit 2
shift
name=$(basename "$dir")
[ "$name" = src ] && name=$(basename "$(dirname "$dir")")
build=$kit/build-sim/$name

sources= includes= defines= sanitize=-USIM_SANITIZE
while [ $# -gt 0 ]; do
    case "$1" in
        --src) sources="$sources${sources:+;}$2"; shift 2 ;;
        --include) includes="$includes${includes:+;}$(full "$2")"; shift 2 ;;
        --define) defines="$defines${defines:+;}$2"; shift 2 ;;
        --no-sanitize) sanitize=-DSIM_SANITIZE=OFF; shift ;;
        *) break ;;
    esac
done

# the build tool: CMAKE_GENERATOR if set; else Ninja, but on Windows only in
# an MSYS2 shell with its own compiler (Git Bash: CMake's choice, Visual Studio)
mkdir -p "$build"
gen=
if [ -z "$CMAKE_GENERATOR" ] && [ ! -f "$build/CMakeCache.txt" ]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            if [ -n "$MSYSTEM_PREFIX" ] && { [ -x "$MSYSTEM_PREFIX/bin/gcc.exe" ] || [ -x "$MSYSTEM_PREFIX/bin/clang.exe" ]; }; then
                if command -v ninja >/dev/null 2>&1; then gen=Ninja; else gen="MSYS Makefiles"; fi
            fi ;;
        *) command -v ninja >/dev/null 2>&1 && gen=Ninja ;;
    esac
fi
if ! cmake -S "$kit/sim" -B "$build" ${gen:+-G "$gen"} -DPLUGIN_DIR="$dir" -DPLUGIN_SOURCES="$sources" \
        -DPLUGIN_INCLUDES="$includes" -DPLUGIN_DEFINES="$defines" $sanitize >"$build/configure.log" 2>&1; then
    cat "$build/configure.log" >&2
    exit 1
fi
if ! cmake --build "$build" --config Release >"$build/build.log" 2>&1; then
    grep -E 'error|Error' "$build/build.log" | head -40 >&2
    echo "sim/run.sh: the build failed; all of it is in $build/build.log" >&2
    exit 1
fi
warnings=$(grep -v '^ld: ' "$build/build.log" | grep -Ec 'warning( C[0-9]+)?:' || true)
[ "$warnings" -gt 0 ] && echo "($warnings compiler warnings: $build/build.log)"
exe=$build/parrotsim
[ -x "$exe.exe" ] && exe=$exe.exe
exec "$exe" --out "$build/out" "$@"
