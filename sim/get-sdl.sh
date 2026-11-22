#!/bin/sh
# sim/get-sdl.sh -- SDL3 for the simulator, in build-sim/sdl3, where
# sim/CMakeLists.txt looks first. For a machine whose package manager has
# no SDL3 (Ubuntu before 25.04, say), and for CI. docs/simulator.md has the
# usual ways (brew install sdl3, MSYS2, vcpkg); this is the other one.
#
# On Windows (Git Bash, MSYS2), the SDL team's own build for Visual C++;
# elsewhere, SDL's source, built here with CMake (a few minutes). Both are
# pinned: the version and the file's SHA-256 below.
#
# On Linux, SDL builds only the parts whose headers it finds: for a window
# and sound, first (Debian, Ubuntu):
#   sudo apt install libx11-dev libxext-dev libwayland-dev libxkbcommon-dev \
#       libegl-dev libasound2-dev libpulse-dev libudev-dev
set -e
version=3.4.18
src_sha=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
vc_sha=78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd
kit=$(cd "$(dirname "$0")/.." && pwd)
dest=$kit/build-sim/sdl3
work=$kit/build-sim/sdl3-work
url=https://github.com/libsdl-org/SDL/releases/download/release-$version

# Linux without the X11 and Wayland headers: an SDL for --headless only
# (CI's), built again once they are installed
console=
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*|Darwin) ;;
    *) if [ ! -f /usr/include/X11/Xlib.h ] && [ ! -f /usr/include/wayland-client.h ]; then console=" (headless only)"; fi ;;
esac
stamp="$version$console"
if [ -f "$dest/.version" ] && [ "$(cat "$dest/.version")" = "$stamp" ]; then
    echo "SDL $stamp is in $dest already"
    exit 0
fi
if [ -n "$console" ]; then
    echo "get-sdl.sh: no X11 or Wayland headers here, so this SDL can't open a window: the"
    echo "simulator will run with --headless only. For a window, install the packages that"
    echo "docs/simulator.md lists, then run this again."
fi

fetch() {   # URL FILE SHA256
    curl -fsSL --retry 3 -o "$2" "$1"
    if command -v sha256sum >/dev/null 2>&1; then got=$(sha256sum "$2" | cut -d' ' -f1)
    else got=$(shasum -a 256 "$2" | cut -d' ' -f1); fi
    if [ "$got" != "$3" ]; then
        echo "get-sdl.sh: $2 isn't the file it should be (SHA-256 $got)" >&2
        exit 1
    fi
}

rm -rf "$work" "$dest"
mkdir -p "$work"
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*)
    fetch "$url/SDL3-devel-$version-VC.zip" "$work/sdl.zip" "$vc_sha"
    (cd "$work" && { unzip -q sdl.zip 2>/dev/null || powershell.exe -NoProfile -Command "Expand-Archive -Force sdl.zip ."; })
    mv "$work/SDL3-$version" "$dest"
    ;;
*)
    fetch "$url/SDL3-$version.tar.gz" "$work/sdl.tar.gz" "$src_sha"
    tar xzf "$work/sdl.tar.gz" -C "$work"
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
    cmake -S "$work/SDL3-$version" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$dest" -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_INSTALL_DOCS=OFF \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DCMAKE_INSTALL_RPATH='$ORIGIN' \
        ${console:+-DSDL_UNIX_CONSOLE_BUILD=ON} >"$work/configure.log" || { cat "$work/configure.log"; exit 1; }
    cmake --build "$work/build" --parallel "$jobs" >"$work/build.log" || { tail -30 "$work/build.log"; exit 1; }
    cmake --install "$work/build" >/dev/null
    ;;
esac
echo "$stamp" > "$dest/.version"
rm -rf "$work"
echo "SDL $stamp is in $dest"
