#!/bin/sh
# check-tools.sh -- are the tools for building Parrot Cart plugins here?
#
#   sh check-tools.sh            the cart build, the simulator, and MAME for test/run.sh
#
# Prints one line per tool, with how to install what's missing, and exits
# 1 if the cart build can't run. The simulator and MAME are optional.
# Reads PICO_SDK_PATH, or looks in $HOME/pico-sdk.

os=$(uname -s)
fail=0
say() { printf '%-10s %-8s %s\n' "$1" "$2" "$3"; }
hint() {
    case "$os" in
        Darwin) echo "$1" ;;
        *) echo "$2" ;;
    esac
}

need() {    # name, command, mac install, linux install
    if command -v "$2" >/dev/null 2>&1; then
        say "$1" ok "$("$2" --version 2>&1 | head -1)"
    else
        say "$1" MISSING "$(hint "$3" "$4")"
        fail=1
    fi
}

need gcc-arm arm-none-eabi-gcc "brew install --cask gcc-arm-embedded" \
    "sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib"
need cmake cmake "brew install cmake" "sudo apt install cmake"
need ninja ninja "brew install ninja" "sudo apt install ninja-build"
need python3 python3 "brew install python" "sudo apt install python3"
need sjasmplus sjasmplus "brew install sjasmplus" \
    "build it: git clone --depth 1 -b v1.23.1.1 --recurse-submodules https://github.com/z00m128/sjasmplus && make -C sjasmplus && sudo make -C sjasmplus install"

if command -v picotool >/dev/null 2>&1; then
    v=$(picotool version 2>/dev/null | head -1)
    case "$v" in
        *v2.3.1*) say picotool ok "$v" ;;
        *) say picotool WRONG "$v: the kit wants 2.3.1, because other versions make a different image" ;;
    esac
else
    say picotool MISSING "$(hint "brew install picotool" "build tag 2.3.1 from https://github.com/raspberrypi/picotool (tools/ci/setup.sh in the kit does it)")"
    fail=1
fi

sdk=${PICO_SDK_PATH:-$HOME/pico-sdk}
if [ -f "$sdk/pico_sdk_version.cmake" ]; then
    maj=$(sed -n 's/^set(PICO_SDK_VERSION_MAJOR \([0-9]*\)).*/\1/p' "$sdk/pico_sdk_version.cmake")
    min=$(sed -n 's/^set(PICO_SDK_VERSION_MINOR \([0-9]*\)).*/\1/p' "$sdk/pico_sdk_version.cmake")
    rev=$(sed -n 's/^set(PICO_SDK_VERSION_REVISION \([0-9]*\)).*/\1/p' "$sdk/pico_sdk_version.cmake")
    if [ "$maj.$min.$rev" = 2.1.1 ]; then
        say pico-sdk ok "2.1.1 in $sdk"
    else
        say pico-sdk OTHER "$maj.$min.$rev in $sdk: the cart is built and tested with 2.1.1"
    fi
    [ -n "$PICO_SDK_PATH" ] || echo "           (PICO_SDK_PATH isn't set: pass -DPICO_SDK_PATH=$sdk to cmake, or export it)"
else
    say pico-sdk MISSING "git clone --depth 1 -b 2.1.1 https://github.com/raspberrypi/pico-sdk ~/pico-sdk"
    fail=1
fi

# the simulator: SDL3, from a package or from the kit's sim/get-sdl.sh
if pkg-config --exists sdl3 2>/dev/null; then
    say SDL3 ok "$(pkg-config --modversion sdl3) (for the simulator)"
elif [ -d build-sim/sdl3 ]; then
    say SDL3 ok "build-sim/sdl3 (for the simulator)"
elif [ "$os" = Darwin ] && command -v brew >/dev/null 2>&1 && [ -d "$(brew --prefix sdl3 2>/dev/null)/include/SDL3" ]; then
    say SDL3 ok "$(brew --prefix sdl3) (for the simulator)"
else
    say SDL3 missing "$(hint "brew install sdl3" "sudo apt install libsdl3-dev, or sim/get-sdl.sh on Ubuntu 24.04 / Debian 12") (only for the simulator)"
fi

if command -v mame >/dev/null 2>&1; then
    say mame ok "for test/run.sh's check against the real kernel (needs the ColecoVision BIOS, coleco.zip, where MAME finds it: docs/testing.md)"
else
    say mame missing "optional: test/run.sh skips that step with NO_MAME=1"
fi

[ $fail = 0 ] && echo "The cart build can run." || echo "Install what's MISSING above, then build again."
exit $fail
