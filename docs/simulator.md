# The simulator

The simulator runs your plugin on your computer in a window, with sound
and controls. It builds your plugin's C files for your computer against
models of the console and the cart, the same models `test/run.sh` uses.
Then you play it with the keyboard or a game pad.

```sh
sim/run.sh src                       # the template's plugin
sim/run.sh examples/sound            # an example
sim/run.sh src --card ~/sdcard       # with a folder as the SD card
sim/run.sh src --usb host            # USB pads, a keyboard and a mouse on the cart
```

The first build takes a few seconds, and after that only what changed is
built again, so it's a quick way to try ideas. Before you release your
plugin, try it on a real console too (see "What the simulator can't show
you" below).

## 1. Install SDL3

The simulator needs CMake, a C compiler, and [SDL3](https://libsdl.org)
(3.2 or later).

macOS, with [Homebrew](https://brew.sh):

```sh
brew install sdl3 cmake ninja
```

On Linux, Ubuntu 25.04 and later and Debian 13 have SDL3:

```sh
sudo apt install libsdl3-dev cmake ninja-build build-essential libasound2-dev
```

Older versions (Ubuntu 24.04, Debian 12) don't. Install what SDL needs to
build, then build SDL into the kit's `build-sim/sdl3`. It takes a few
minutes, and you only do it once:

```sh
sudo apt install cmake ninja-build build-essential libx11-dev libxext-dev \
    libwayland-dev libxkbcommon-dev libegl-dev libasound2-dev libpulse-dev libudev-dev
sim/get-sdl.sh
```

On Linux, `libasound2-dev` adds MIDI ports (section 4). Without it, the
simulator still builds, but with no MIDI ports.

On Windows with Visual Studio 2022, use the "Desktop development with
C++" workload, which includes CMake. In Git Bash, `sim/get-sdl.sh` puts
the SDL team's Windows build in `build-sim\sdl3`. You can also use
[vcpkg](https://vcpkg.io). Run `vcpkg install sdl3`, then set
`CMAKE_TOOLCHAIN_FILE` to vcpkg's `scripts\buildsystems\vcpkg.cmake`.

On Windows with [MSYS2](https://www.msys2.org), in its UCRT64 shell:

```sh
pacman -S mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gcc
```

## 2. Run

```sh
sim/run.sh PLUGIN_DIR [options]
```

`PLUGIN_DIR` is the folder with your plugin's `.c` and `.cpp` files. On
Windows, `sim\run.bat PLUGIN_DIR [options]` does the same from the
command prompt, and `sim/run.sh` works in Git Bash and MSYS2.

`sim/run.sh src --help` lists every option. These are the ones you'll use
most:

| Option | What it does |
|---|---|
| `--card FOLDER` | uses the folder as the SD card (section 5) |
| `--open PATH` | starts as if `PATH` had been picked in the cart's menu (section 5) |
| `--usb host` | makes the cart's USB port a host, for pads, keyboards and mice (section 4) |
| `--midi-in NAME`, `--midi-out NAME` | chooses which MIDI ports the plugin uses (section 4) |
| `--tv` | shows the TV's shape, with a border and a 4:3 picture |
| `--scale N` | sets how many window pixels each console pixel takes (default 3) |
| `--mute` | turns the sound off |
| `--strict` | stops at the first rule the plugin breaks (section 6) |
| `--shots DIR` | sets where F12 saves screenshots |

If your plugin needs more files to build, give them before the
simulator's options:

```sh
sim/run.sh ../my-plugin/src --src '../vendor/lib/*.c' \
    --include ../my-plugin/vendor/lib --define USE_THIS=1
```

`--src` takes a file or pattern, relative to `PLUGIN_DIR`. `--include`
adds a folder for `#include`, and `--define` adds a define.
`--no-sanitize` builds without the memory checks (section 6), which
makes it faster.

With `sim\run.bat`, or with your own build, use CMake directly:

```sh
cmake -S sim -B build-sim/myplugin -DPLUGIN_DIR=/path/to/my-plugin/src \
    "-DPLUGIN_SOURCES=../vendor/lib/*.c" -DPLUGIN_INCLUDES=/path/to/vendor/lib
cmake --build build-sim/myplugin --config Release
build-sim/myplugin/parrotsim --card ~/sdcard
```

## 3. Keys

| Key | On the console |
|---|---|
| arrows, or W A S D | the controller's stick |
| Z, X | its left and right fire buttons |
| 0 to 9 | its keypad's digits |
| `-` and `=` | its keypad's `*` and `#` |

Any game pad works too. Its d-pad or left stick is the controller's
stick, its face buttons are the fire buttons, and Back and Start are `*`
and `#`. Game pads are also the plugin's USB pads (section 4).

| Key | In the simulator |
|---|---|
| F1 | lists the keys in the terminal |
| F2 | changes the keyboard's role (below) |
| F5 | the console's RESET |
| F12 | takes a screenshot |
| Esc | quits (so does closing the window) |

F2 switches the keyboard between three roles:

1. **The controller** (the default), with the keys above.
2. **USB pad 1**, for a plugin that reads one when you don't have a pad.
   The arrows are the d-pad, Z X A S are the face buttons (south, east,
   west, north), Q W are L and R, E R are ZL and ZR, Enter and Backspace
   are + and -, H is Home, and I J K L are the left stick.
3. **A USB keyboard and mouse**. Every key goes to the plugin as if it
   came from a USB keyboard, and the mouse acts as a USB mouse
   (section 4). The window holds on to the mouse until you press F2
   again. In this role, Esc goes to the plugin too, and only F1, F2, F5
   and F12 still control the simulator.

The window's title always shows the keyboard's role. When the simulator
starts, and after you press F2, a label in the bottom left corner of the
window shows it for three seconds too.

F5 works like the console's RESET. The plugin's session ends, the
simulator saves the plugin's changes to the card, and the window shows
the note your plugin left for the menu (`cvk_note()`). Press F5 or a
fire button there to start the plugin again from the beginning, as a
fresh program, or press Esc to quit.

## 4. USB: pads, a keyboard, a mouse, MIDI

A plugin turns on the cart's USB features with `cvcart_features()` in its
`CMakeLists.txt` (`docs/features.md`). The simulator reads that line and
sets up the same features.

On the cart, the USB port is either a MIDI device on a computer or a host
for a hub (`cvusb_mode()`). The simulator picks the same way the cart
does. A plugin with `MIDI_OUT` finds a computer listening and becomes its
MIDI device after 0.3 seconds. Any other plugin is a host. You can change
that with `--usb`:

| Option | On the cart's USB port |
|---|---|
| `--usb device` | a computer that is listening (the default) |
| `--usb host` | a hub and no computer, so the port becomes a host after 1.5 seconds |
| `--usb ask` | a computer that asks its user first, and gets no answer |

Pads, keyboards and mice only reach a plugin whose port is a host. To try
them with a plugin that has `MIDI_OUT`, start the simulator with
`--usb host`. F2 reminds you of this in the terminal.

**Game pads** (`PADS`). The first two game pads are the plugin's USB pads
1 and 2, with all their buttons and both sticks. `cvusb_pad_kind()`
reports a Switch Pro Controller as `SWITCH PRO` and any other pad as
`HID PAD`. As on the cart, a pad keeps its number until you unplug it. If
you unplug pad 1, pad 2 is still pad 2, and the next pad you plug in
becomes pad 1. A third pad isn't used. Pad 1 is also the console's
controller, or pad 2 while there's no pad 1.

**A keyboard and a mouse** (`KEYBOARD`, `MOUSE`). In F2's third role,
each key goes to the plugin the way a USB keyboard sends it. The SDK's
code reads it (`cvusb_keys.c`), so `cvusb_key()` gives the same usage,
character and modifiers as on the cart. Keys don't repeat, just like on a
USB keyboard. The mouse moves, clicks and scrolls like a USB mouse. When
you press F2 again, or the window loses focus, the plugin sees every key
and button released.

**MIDI** (`MIDI_IN`, `MIDI_OUT`). The plugin's MIDI goes to and from your
computer's MIDI ports, through CoreMIDI on macOS, ALSA on Linux and WinMM
on Windows.

- **Out**: on a computer, the cart is a MIDI device named after the
  plugin. On macOS and Linux, the simulator makes a MIDI port with the
  plugin's name, and your music apps see it as an input and an output.
  What the plugin sends comes out of that port, and what the apps send
  to it reaches the plugin. Windows can't make ports like that, so use
  `--midi-out NAME` to send to a port that already exists, such as a
  synth or a port made with
  [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html).
- **In**: on the cart, a MIDI keyboard plugs into the hub. The simulator
  reads every MIDI input on your computer, or only the one you name with
  `--midi-in NAME`.

| Option | What it does |
|---|---|
| `--midi-in NAME` | reads the MIDI input called `NAME`, or one with `NAME` in its name. `all` reads every input (the default with `MIDI_IN`), and `none` reads none |
| `--midi-out NAME` | sends the plugin's MIDI to the output called `NAME`. `virtual` makes a port named after the plugin (the default with `MIDI_OUT`, on macOS and Linux), and `none` sends it nowhere |
| `--midi-log FILE` | writes each message the plugin sends to `FILE`, with the time in milliseconds |
| `--midi-list` | lists the MIDI ports and quits |

F1 shows which MIDI ports are open. With `--usb host`, what the plugin
sends to the MIDI device on the hub goes to the same output.

The plugin's MIDI goes out once a TV frame, so its timing can be up to
1/60 of a second off. Input reaches the plugin once a frame too. As on
the cart, SysEx doesn't reach the plugin.

## 5. The SD card

`--card FOLDER` turns a folder on your computer into the plugin's SD
card. The plugin sees a FAT32 card through the SDK's file system code,
with the folder's files and subfolders on it. The simulator reads each
file from your folder when the plugin reads it.

What the plugin writes is copied back to the folder when the session
ends, which happens when you press RESET or Esc, or close the window. A
save file the plugin makes (`fs_create()`, then `fs_write_sec()`) shows
up as a new file in the folder, and a file it changes is changed there
too. Please don't change the folder while the simulator runs, since the
plugin's changes are written back to it at the end.

A few notes about the folder:

- Files and folders whose names start with `.` aren't on the card.
- Files of 4 GB or more aren't on the card, because FAT32 can't hold
  them.
- The card has extra free space for new files, and the terminal shows
  its size when the simulator starts.

`--card` also takes a disk image of a whole card, such as a `.img` file.
The plugin reads and writes that file directly.

`--open PATH` starts the plugin as if `PATH` had been picked in the
cart's menu, so `cvsd_open_launched()` opens it. `PATH` is either a path
on the card (`--open /NES/game.nes`) or a file on your computer. For a
file on your computer, its folder becomes the card, unless you also give
`--card`. In Git Bash, which turns a leading `/` into a Windows path,
leave out the slash (`--open NES/game.nes`).

```sh
sim/run.sh examples/open-file --open ~/Documents/notes.txt
```

## 6. Problems the models find

The models check the same rules as `test/run.sh`. They catch a list for
the console that's malformed, too long or sent too early, and a frame
list that runs past its TV frame. The simulator prints each problem in
the terminal, shows a red mark in the corner of the window for a second,
and counts the problems in the window's title. The plugin keeps running.
With `--strict`, the simulator stops at the first problem.

On macOS and Linux, the simulator is built with the compiler's memory
checks (AddressSanitizer and UndefinedBehaviorSanitizer). A plugin that
writes past the end of an array, uses freed memory, or overflows a signed
number stops with a report that names the line. On the cart, the same bug
just does something odd later. `--no-sanitize` builds without the checks.

Each session also leaves a log of what the plugin did in
`build-sim/NAME/out/log.txt`, next to `screen.png`, the last picture.

## 7. Screenshots

F12 saves the picture as `screenshot-1.png`, then `screenshot-2.png`, and
so on, in the current folder (or in `--shots DIR`). They're 256x192,
exactly the console's pixels, which is what the plugins site needs
(`docs/screenshots.md`).

## 8. Without a window: tests

`--headless` runs with no window and no sound, as fast as your computer
can go, and always stops by itself. Use it for tests and scripts.

```sh
sim/run.sh src --headless --ms 3000 --script "500:joy=up 900:key=5 1200:key=" \
    --shot hello.png --wav hello.wav
```

| Option | What it does |
|---|---|
| `--frames N` | stops after N TV frames (the default is 600, which is 10 seconds) |
| `--ms N` | stops after N milliseconds of console time |
| `--script S` | gives the inputs, in the script language of `test/run.sh` (docs/testing.md), plus the words below |
| `--shot FILE.png` | saves the last picture |
| `--wav FILE.wav` | saves the sound (48 kHz, 16-bit, mono) |
| `--midi-log FILE` | saves what the plugin sent over MIDI (section 4) |

In headless mode, the simulator opens no MIDI ports unless `--midi-in` or
`--midi-out` names one, so a test never touches your MIDI devices. Use
`--midi-log` to see what the plugin sent, and the script's `midiin=` word
to send it MIDI. With a port open, a headless run goes at the console's
real speed instead of as fast as it can.

The script words from `docs/testing.md` (`pad=`, `keydown=`, `mouse=`,
`midiin=` and the rest) go straight to the models. The words below go
through the simulator's keyboard, mouse and game pad code instead, the
same way input from the window does. They only work in the simulator:

| Word | What it does |
|---|---|
| `simrole=controller`, `pad` or `keyboard` | sets the keyboard's role, like F2 |
| `simkey=04`, `simkeyup=04` | presses or releases a key, given as its USB usage in hex (`04` is A, `28` is Enter, `E1` is left Shift) |
| `simmouse=DX,DY,BUTTONS[,WHEEL]` | moves the mouse, holds its buttons (1 left, 2 right, 4 middle) and turns the wheel |
| `simpad=switch` or `hid` | plugs in a game pad, either a Switch Pro Controller or another pad |
| `simbuttons=N:south+l` | holds these buttons on the Nth pad plugged in (with the names `pad=` uses). `simbuttons=N:` releases them all |
| `simsticks=N:LX,LY,RX,RY` | sets the Nth pad's sticks as the computer reads them, from -32768 to 32767, with y down |
| `simunplug=N` | unplugs the Nth pad plugged in |

While a `simpad=` pad is plugged in, it's the console's controller, just
like a real pad, and the script's `joy=` and `key=` don't reach the
console.

```sh
sim/run.sh test/usb --headless --usb host --ms 3000 \
    --script "1600:simrole=keyboard 1620:simkey=0B 1640:simkeyup=0B 1700:simpad=switch 1750:simbuttons=1:south"
```

`sim/test.sh` tests the simulator itself this way. It runs the
template's plugin and every example headless, and compares their logs
and pictures with the ones from `test/run.sh`. It also checks that the
Sound example's scale is in tune in the WAV, that the Frames example
beeps when its lists say it should, and that a save ends up in a card
folder. Last, it runs `test/usb`, a plugin with every USB feature on,
through the keyboard, mouse, pads and MIDI.

## What the simulator can't show you

The simulator shows you what your plugin draws and plays, and how it
responds to the controls. It can't show you how the plugin runs on the
cart:

- Your computer runs the plugin's code many times faster than the cart's
  chip. The simulator keeps the console at 60 frames a second, but a loop
  that's too slow on the cart still looks fine here. You'll only see that
  on a console.
- On the cart, a plugin's code, data and heap share about 507 KB, and its
  stack is 4 KB (`docs/hardware.md`). Your computer has far more memory,
  so a plugin that runs out on the cart may run fine here.
- A desktop game pad acts as a perfect USB pad, and your computer's
  keyboard, mouse and MIDI ports act as perfect USB devices. The cart's
  USB code (TinyUSB) doesn't run, so hubs, how a device starts up, and
  devices TinyUSB doesn't support aren't tested. Neither is MIDI timing,
  which moves once a TV frame here.
- The colors are exactly the video chip's, while a TV blurs them and
  makes them bleed. The sound comes from a model of the console's sound
  chip, which is close but not exact.
- Reads and writes on the card take a fixed time here. A real card is
  slower, and some are much slower.
- The cart's start-up isn't simulated, including the menu copying the
  plugin and the switch between the menu and the plugin.

`docs/testing.md` has more about testing, including a test plan for a
real console.
