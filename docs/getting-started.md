# Getting started

A Parrot Cart plugin is a program that runs on the cart. The cart uses
the RP2350, the same chip as the Raspberry Pi Pico 2. You build a plugin
into a single `.CVP` file and copy it to the `/PLUGINS` folder on the SD
card, and the cart's menu lists it there for you to start. You can build
plugins on a Mac or a Linux computer with the same free tools a Pico
project uses.

## 1. Tools

| Tool | Version | For |
|---|---|---|
| Arm GNU toolchain (`arm-none-eabi-gcc`) | 9 or later (developed with 9.2.1) | compiling for the cart |
| CMake, Ninja | CMake 3.13 or later | the build |
| Python 3 | 3.8 or later, no packages | packing the `.CVP`, the README, the tools |
| [sjasmplus](https://github.com/z00m128/sjasmplus) | 1.20 or later (developed with 1.23.1) | assembling the SDK's Z80 kernels |
| [pico-sdk](https://github.com/raspberrypi/pico-sdk) | **2.1.1** | the chip's SDK |
| picotool | **2.3.1** (2.1.1 makes a different, 512 KB image) | packaging. Install it, because without it pico-sdk 2.1.1 downloads and builds picotool's development branch instead |

macOS, with [Homebrew](https://brew.sh):

```sh
brew install cmake ninja sjasmplus picotool
brew install --cask gcc-arm-embedded
```

Debian or Ubuntu:

```sh
sudo apt install cmake ninja-build python3 build-essential \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
# sjasmplus isn't packaged: build it
git clone --depth 1 -b v1.23.1.1 --recurse-submodules https://github.com/z00m128/sjasmplus
make -C sjasmplus && sudo make -C sjasmplus install
# and picotool, from https://github.com/raspberrypi/picotool (tag 2.3.1),
# built against pico-sdk 2.3.1 with its mbedtls, as tools/ci/setup.sh does
```

`tools/ci/setup.sh` runs exactly these steps. The GitHub workflows use it
on Ubuntu 24.04.

Clone the pico-sdk wherever you like. You don't need its submodules,
because the cart's SDK comes with TinyUSB:

```sh
git clone --depth 1 -b 2.1.1 https://github.com/raspberrypi/pico-sdk ~/pico-sdk
```

## 2. Build

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

The last line it prints is the plugin file:

```
build/HELLO.CVP: Hello 0.1.0 (yourname.hello), image 55552 bytes from 0x10e00000
```

If you add `export PICO_SDK_PATH=$HOME/pico-sdk` to your shell's
profile, you won't need to type it each time. You can also use
`-DPICO_SDK_FETCH_FROM_GIT=ON` instead, and the build downloads pico-sdk
2.1.1 into the build folder.

You build all the examples at once the same way:

```sh
cmake -S examples -B build-examples -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build-examples
```

## 3. On the cart

1. Put the SD card in your computer and copy the `.CVP` into its
   `/PLUGINS` folder. If the card doesn't have that folder yet, make it.
2. Put the card back in the cart and turn the console on. In the menu,
   press B in the card's top folder to go to Home, then open
   **Plugins**. It lists everything in `/PLUGINS` with its icon, name
   and version.
3. Press A on your plugin to see its details: the version, developer, ID,
   the files it opens, and its README. Press A again to start it. The
   first time, the cart copies the plugin into its flash and shows a
   progress bar. After that, it starts right away.
4. Press RESET on the console to go back to the menu.

If your plugin opens files (`OPENS .txt`), it also starts when you pick
one of those files in the card's folders and press A.

To put a new build on the card, copy the new file over the old one. The
cart copies a changed plugin into flash again. To remove a plugin, delete
its file, or press `#` on its details page.

You don't need to flash the cart from a computer to run a plugin, and a
plugin that crashes can't harm the cart. Press RESET, or turn the console
off and on, to get back to the menu.

## 4. Make it yours

The quickest way is `tools/new-plugin.py`. It asks for the plugin's
name, your name, the screen and the features you want (game pads, a
keyboard, a mouse, MIDI in and out, the PSRAM, saves). Then it writes a
plugin that builds, with each feature working on the TV and `YOUR CODE`
marking where your code goes:

```sh
tools/new-plugin.py                   # a new folder beside this one
tools/new-plugin.py --here            # this copy, in place of Hello
```

The new plugin leaves out `examples/`, unless you add `--examples`. Its
license is MIT unless you pick another one. `tools/new-plugin.py`
includes the full license text for MIT, BSD-2-Clause, BSD-3-Clause, ISC,
Apache-2.0, GPL-2.0 and GPL-3.0 (each `-only` or `-or-later`) and
LGPL-2.1-or-later. `docs/features.md` describes the features.

To do it by hand instead, edit this line in `CMakeLists.txt`:

```cmake
cvcart_plugin(hello ID yourname.hello NAME "Hello" VERSION 0.1.0 BY "Your Name"
              ICON src/icon.txt README README.md)
```

- `ID` stays the same for the life of your plugin, so pick it carefully
  (`docs/plugin-rules.md`).
- `ICON` is the plugin's icon (`docs/icons.md`).
- `README` is the file the cart shows on the plugin's details page
  (`docs/readme-spec.md`).
- The target's name (`hello`, which appears three times in the file) sets
  the output's name, `HELLO.CVP`. To use a different file name, add
  `FILE MYPLUGIN.CVP`.

Then open `src/main.c` and read it from the top. Every plugin has the
same basic shape. `examples/README.md` lists what each example shows, and
`sdk/firmware/revc/sdk/README.md` is the SDK reference.

## 5. Try it without the console

```sh
sim/run.sh src             # your plugin in a window, with sound and controls
test/run.sh hello          # your plugin, on a model of the console
tools/screenshot.sh shot.png src 3000 "500:joy=up 1000:key=5"
```

The simulator needs SDL3. `docs/simulator.md` explains how to install it
and which keys act as the controller. `docs/testing.md` covers the rest,
including what you can only test on a real console.

## Things that go wrong

| What you see | Why |
|---|---|
| `sjasmplus: command not found` during the build | sjasmplus isn't installed. See section 1. |
| `the plugin's .bss reaches the switch's reserved RAM` | The plugin's data is too big for the chip's RAM. Move big buffers to the PSRAM (`examples/psram`). |
| `links malloc: its heap would grow into the launch block` | Something calls `malloc`, `new`, or `printf` with a float. Remove that call, or add `HEAP` to `cvcart_plugin()` (`sdk/.../cvheap.h`). |
| a warning that the pico-sdk isn't 2.1.1 | It will probably still work. We build and test the cart with 2.1.1. |
| on the TV: `NEEDS NEWER CART FIRMWARE` | The plugin was built with a newer SDK than the cart's menu supports. Update the cart's firmware. |
| the plugin isn't listed | The file isn't in `/PLUGINS`, or the menu can't read it as a `.CVP`, in which case it shows up under its file name instead. |
