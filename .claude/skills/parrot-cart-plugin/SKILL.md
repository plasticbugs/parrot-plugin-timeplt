---
name: parrot-cart-plugin
description: Build, test and release plugins for Parrot Cart, the ColecoVision SD cartridge that runs .CVP plugins on its RP2350 chip. Use this whenever someone wants to make, port, fix or publish a Parrot Cart plugin, such as a game, app or tool in C or Lua, an emulator port, or something that uses USB pads, a keyboard, a mouse, MIDI, the SD card or the PSRAM. Also use it for questions about the starter kit (parrot-plugin-template), cvcart_plugin(), the SDK's cvk_/cvs_/cvf_ calls, drawing on the TMS9918 or playing the SN76489 from the cart, the plugin simulator, new-plugin.py, .CVP files, or getting a plugin onto the Parrot Cart plugins site. Use it whenever the working folder is a copy of the starter kit, even if the user never says "plugin".
---

# Parrot Cart plugins

Parrot Cart is a cartridge for the ColecoVision with an RP2350 chip, an SD
card slot and a USB-C port. A plugin is a program for that chip, packed
into one `.CVP` file. People copy it to the card's `/PLUGINS` folder and
start it from the cart's menu. The starter kit
(<https://github.com/plasticbugs/parrot-plugin-template>) has the SDK, a
simulator, tests, examples and docs, and this skill is how to use them
well.

## How a plugin works

Keep this picture in mind, because most mistakes come from forgetting it.

- The console's Z80 still runs the show on the TV side, but the program it
  runs is a small kernel that the cart serves. The kernel writes the
  pictures and sounds the plugin asks for and reports the controllers.
- The RP2350 has two Cortex-M33 cores at 150 MHz. **Core 1 belongs to the
  SDK.** It answers every bus read the console makes, and a late answer
  crashes the console. The plugin runs on core 0, in one loop that never
  waits for long.
- Memory: 520 KB of SRAM, of which the plugin gets everything below
  `0x2007EC00` (about 507 KB) for code, data and heap. 8 MB of PSRAM is
  there for big things. Plugins are copied from flash into SRAM and run
  from there.
- The TV side is a TMS9918A: 256x192 pixels, 15 colours, and only **two
  colours in each run of 8 pixels along a row**. There are 32 sprites, one
  colour each, and only 4 can share a line. Sound is an SN76489: three
  square-wave voices and a noise voice. Each controller has a stick, two
  fire buttons and a 12-key keypad.

There are three ways to write a plugin. Pick one before writing code:

| The plugin | Use | Why |
|---|---|---|
| A small game, a toy, a demo, someone learning | **Lua** (`docs/lua.md`) | No cross-compiler needed to try it, and the simulator runs a script in seconds. About 50,000 Lua steps a TV frame, enough for a few dozen moving things. Too slow for an emulator. |
| An app, a tool, text, menus, music, a turn-based or light game | **C with the screen kernel** (`--screen text`) | You draw into a 256x192 canvas and the SDK sends the console only what changed, about 1,900 bytes a frame. Small changes reach the TV at up to 60 a second, and a whole new screen at about 4 to 8. Lua runs on this kernel too. |
| An emulator, video, full-screen motion | **C with the frame kernel** (`--screen frames`) | Each TV frame you hand the console one list of video RAM writes and sound bytes. You control every frame, and you also manage the budget (about 1,800 bytes of video RAM writes a frame). See `references/porting.md`. |

If the user asks for C or Lua, use what they asked for. A plugin whose
code won't fit in SRAM can run from flash with `XIP` (the SCUMM plugin
does), but try PSRAM for data first.

## Get set up

1. **Find the kit.** A copy of the kit has `cmake/parrotcart.cmake` and
   `sdk/SOURCE.txt`. If the working folder isn't one, the user needs a
   copy: "Use this template" on GitHub, or
   `git clone https://github.com/plasticbugs/parrot-plugin-template`.
   Making a repository on their GitHub account is their call, so ask
   before running `gh repo create`.
2. **Check the tools.** Run `scripts/check-tools.sh` from this skill's
   folder. It lists what's missing and how to install it on macOS or
   Debian/Ubuntu. The versions that matter are pico-sdk **2.1.1** and
   picotool **2.3.1**: another picotool builds a different image. The
   simulator needs SDL3, and nothing else needs the cross-compiler, so a
   Lua plugin can be tried before the ARM tools are installed.
3. **Set `PICO_SDK_PATH`** (usually `$HOME/pico-sdk`), or pass
   `-DPICO_SDK_PATH=...` to CMake.

## Start a plugin

Use `tools/new-plugin.py`. It writes a plugin that builds, with each
feature you ask for already working on the TV and `YOUR CODE` marking
where the plugin's own code goes. It's quicker and safer than editing
Hello by hand. Give it everything on the command line so it doesn't stop
to ask:

```sh
tools/new-plugin.py --name "Star Sweeper" --by "Ann Lee" --id annlee.starsweeper \
    --about "Sweep the stars before they reach the ground." \
    --screen text --features pads,saves --home /SWEEPER --here
```

- `--here` turns this copy of the kit into the plugin, the way the kit's
  README does it. It removes Hello and `examples/` (`--examples` keeps
  them), so say so. `--out DIR` makes a new folder and leaves the kit as
  it is.
- `--screen text` or `frames`. `--features` takes `pads, keyboard, mouse,
  midi-in, midi-out, psram, saves` (or `all`, `none`).
  `--opens ".swp .lvl"` makes the menu open those files with the plugin.
  It turns on the card (`CARD`), and `src/main.c` opens the file the
  plugin was started with and shows its name and size. If the plugin can
  also start without a file, add `USES standalone` to `cvcart_plugin()`,
  and the menu lists it with the apps (`docs/plugin-rules.md`).
- `--license` takes an SPDX ID (default MIT). An emulator core usually
  decides this, so check the core's license first.

**The ID is permanent.** It's `developer.pluginname`: lower case, digits,
`.`, `-` and `_`, up to 32 characters. The menu and the plugins site tell
plugins apart by it. The developer part should be the user's own name or
GitHub username. Never use `parrotcart.`, which is for the cart's own
plugins. If you don't know the user's developer name, ask. If nobody is
there to answer, use `yourname.` as a placeholder and say so, because
`scripts/check-release.py` will stop a release that still has it.

For a Lua plugin, the script goes in `src/main.lua` and the CMake uses
`parrotcart_lua()` (`docs/lua.md`, "A script as a plugin").

## The loop: build, look, fix

You can't see a TV, but the simulator can show you the picture. Use it
after every change that affects the screen.

```sh
# C: the plugin in src/, run headless for 3 s of console time, keys from a script
sim/run.sh src --headless --strict --ms 3000 \
    --script "500:joy=right 1500:joy= 1600:key=5 1800:key=" \
    --shot /tmp/shot.png --wav /tmp/sound.wav

# Lua
sim/lua.sh src/main.lua --run --headless --ms 3000 --shot /tmp/shot.png
```

Then read `/tmp/shot.png`. It is exactly the console's 256x192 pixels.
Look at it the way a player would: is the text inside the screen, are
the colours right, is anything smeared where three colours met in one
8-pixel run? The run also prints a summary (lists sent, sound changes,
the note left for the menu) and writes `build-sim/NAME/out/log.txt`.
`--strict` stops at the first broken rule (a bad list, a list sent too
early, a frame list over its time). Fix those before anything else,
because on a console they are glitches or crashes.

The script's words: `joy=up+fire` (`up down left right fire fire2`),
`joy=` to let go, `key=5` (`0-9 * #`), `key=` to let go, `snap=NAME` for a
picture at that moment, and USB pads, keyboard, mouse and MIDI words
listed at the top of `test/host/host.c`. Times are milliseconds from the
start.

Then build the real thing:

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build        # the last line names the .CVP, its version and its size
```

`test/run.sh NAME` runs the plugin against models of the console, the
card and USB, then plays what it sent to the real Z80 kernel in MAME and
compares pictures pixel for pixel (`docs/testing.md`). A plugin made by
`new-plugin.py` already has its block in `test/run.sh`. Without MAME,
`NO_MAME=1 test/run.sh NAME` runs the rest. If you run MAME yourself,
keep it headless the way `tools/mame-replay.sh` does.

When it works in the simulator, give the user `sim/run.sh src` (or
`sim/lua.sh src/main.lua --run`) to play it themselves with the keyboard
or a pad, and the `.CVP` to copy into `/PLUGINS` on the card.

## The rules the cart depends on

These are the things the simulator can't always catch and the console
won't forgive. Each one has a reason.

- **Start the console side first.** `cvk_start()` (screen kernel) or
  `cvf_start()` (frame kernel) at the top of `main()`. Until then the
  console sits parked, waiting for a cartridge.
- **Call the task function on every pass of every loop**, `cvk_task()`
  or `cvf_task()`, and keep each pass short. It turns the cart's data bus
  on once it sees the console, watches power and RESET, and takes the cart
  back to the menu. A loop without it (a title screen waiting for fire,
  `sleep_ms`, waiting on a flag) shows black or garbage on a real console
  and RESET does nothing. The simulator fails a plugin that polls the
  console for 100 ms without it. One long step at the start, like
  starting the SD card (up to 1.5 s), is fine.
- **Leave core 1 alone.** Don't call `multicore_launch_core1`, and don't
  disable interrupts for long.
- **Don't write to the cart's flash.** The menu and other plugins live
  there.
- **`malloc`, `new` and `printf` with floats need `HEAP`** in
  `cvcart_plugin()`, and `cvheap_reserve()` before the first allocation.
  Without `HEAP` the build fails on purpose, because the heap would grow
  into the menu's launch block. Every KB of code is a KB less of heap
  (USB host support is about 26 KB, newlib's `printf` about 19 KB). Big
  buffers belong in PSRAM (`USES psram`, `cvpsram.h`).
- **On the card, read anything, but write only in place** to files the
  plugin made (`fs_create()` at their final size, then `fs_write_sec()`)
  or to the file it was launched with. Never delete or overwrite the
  user's other files. Open the launched file with `cvsd_open_launched()`,
  not by its path, because names with non-ASCII characters have `?` in
  the path.
- **Two colours per 8-pixel run.** A third colour turns into one of the
  other two. Line things up on the 8-pixel grid, and keep text on a plain
  background. Things that move over a busy background are better as
  sprites. Lua has them built in (`sprite()`). In C, the screen kernel's
  canvas has no sprites: `api_sprites()` in `lua/api.c` shows how to add
  the sprite table to a screen list, and with the frame kernel you write
  the sprite tables yourself.
- **Frame lists** (frame kernel). Use `parrotcart_frames()`, which serves
  the lists with the SDK's two-sample bus loop. It cured the glitches the
  older loop gave on a console, so don't call `cvframes_use()` on its own.
  Keep each list within its frame by the costs in `gbkernel.asm`
  (`--strict` reports overruns). The console repeats the last list when
  no new one arrives, so a list must be safe to run twice. Absolute video
  RAM writes, registers and sound bytes are. If you keep a copy of the
  console's video RAM, never drop a list. Offer it to `cvf_publish()`
  until it's taken (`references/porting.md`, "Timing").
- **Leave the clock at 150 MHz.** The bus loop on core 1 is timed for it,
  and a 200 MHz build crashed the console.
- **One kernel per plugin**, and with `cvcart_features()` don't call
  TinyUSB's `tuh_*` functions yourself.
- **USB pads only need reading.** The SDK finds a pad that is already on
  a powered hub when the plugin starts, and one plugged in or pulled out
  while it runs. Some hubs never report a change, so it reads their ports
  every 100 ms. Call `cvusb_pad()`, or use `cvinput.h`.
- **Leave a note** for the menu's bottom line when there's something to
  say: `cvk_note("Saved")`, 32 characters at most.
- **Arcade games share their keys**: keypad * or # adds a coin, 1 and 2
  start a one- or two-player game, and on a USB pad - is a coin and + a
  start (`references/porting.md`, "Arcade games").

## Pictures and sound

- The visible area is about 240 of the 256 pixels across on most TVs,
  so keep text and controls at least 8 pixels from the left and right
  edges.
- `cvs_line()` and `canvas_text()` use the menu's 8x8 font, which has
  upper case, digits and some punctuation. For lower case and 40 columns,
  use `cvfont` (`cvfont_use()`, a 6x8 font).
- The colours are `C_BLACK C_MGREEN C_LGREEN C_DBLUE C_LBLUE C_DRED
  C_CYAN C_MRED C_LRED C_DYELLOW C_LYELLOW C_DGREEN C_MAGENTA C_GRAY
  C_WHITE` (`midihello/vdp.h`). There are no dark greys or browns, so
  dark scenes lose their shading.
- Redraw the whole canvas in your draw function every time. The SDK
  works out what changed, so there's nothing to gain by drawing only the
  differences yourself.
- Sound is ten bytes, the chip's own: tone low, tone high and volume for
  voices 0 to 2, then the noise volume. Send them with `cvk_sound()`
  when they change, and again every few frames even when they don't.
  The console misreads a byte from the cart now and then, and a sound
  register stays wrong until it's written again, so a sound sent once
  can turn into a drone that lasts until RESET. All zeros is a tone, not
  silence; use `cvk_silence`. `examples/sound/main.c` has `psg_tone()`,
  which turns a frequency into those bytes (the lowest note is about
  110 Hz). In the frame kernel, sound bytes go in the list. Put the whole
  chip in every list (`references/porting.md`, "The sound").
- Make it as loud as an ordinary ColecoVision cartridge at the same TV
  volume. The cart's first arcade plugins were far too quiet next to one.
  Round off peaks with a soft knee instead of clipping, and measure the
  level against a cartridge in MAME (`docs/testing.md`, "How loud").
- A picture taller than 192 lines, such as an arcade game's, fits whole
  when scaled by 3/4 with `cvscale34()` (`cvscale.h`, built into every
  frame plugin). The score and lives go in the side margins that leaves
  (`references/porting.md`, "The picture").

`references/sdk-api.md` lists the SDK's calls by area. The headers in
`sdk/firmware/revc/sdk/` are the full reference.

## Where to look

In a copy of the kit, read the file. Elsewhere, fetch it from
`https://raw.githubusercontent.com/plasticbugs/parrot-plugin-template/main/PATH`.

| Need | File |
|---|---|
| tools, building, the first run on a cart | `docs/getting-started.md` |
| the chip, memory, the two kernels, budgets | `docs/hardware.md` |
| ID, version, size, HOME, OPENS, USES, what a plugin must do | `docs/plugin-rules.md` |
| USB pads, keyboard, mouse, MIDI, PSRAM, saves | `docs/features.md` |
| the simulator, its keys and options | `docs/simulator.md` |
| tests, how loud, and what only a console shows | `docs/testing.md` |
| Lua: the API, speed, memory, scripts as plugins | `docs/lua.md` |
| the README the cart shows (30 columns) | `docs/readme-spec.md` |
| icons, screenshots | `docs/icons.md`, `docs/screenshots.md` |
| releasing and the plugins site | `docs/release-checklist.md` |
| one small working plugin per topic | `examples/` (screen, sound, usb-gamepad, usb-keyboard-mouse, usb-midi-out, sd-card, open-file, frames, psram) |
| the SDK's calls | `sdk/firmware/revc/sdk/README.md` and its headers |
| full plugins with source (NES, Game Boy, Master System, PC Engine, Vectrex, SCUMM, Lua, Text Editor) | `https://github.com/plasticbugs/parrot-plugin-*` |

## Release it and get it listed

Run `scripts/check-release.py` from this skill's folder in the plugin's
folder (add `--cvp build/NAME.CVP` after a build). It checks the ID, the
version against the last tag, the README's credits and source link, the
screenshots, the license files and the timing (the frame lists' bus loop
and the clock), and looks for ROMs or game data that must not be in the
repository. Fix what it reports, then:

1. **Screenshots**: up to three, `screenshots/screenshot-1.png` to
   `-3.png`, each exactly 256x192 and under 100 KB. The simulator's
   `--shot` (or F12 in its window) makes them at that size. Check with
   `python3 tools/png256.py check screenshots/*.png`.
2. **README**: what it is, its controls as a list, what to put on the
   card, credits (with license and source link for each), its license
   and a link to its source. Preview what the cart shows with
   `python3 sdk/firmware/revc/sdk/cvreadme.py README.md`.
3. **Release notes** in `RELEASE_NOTES.md`: what changed, and whether it
   needs newer cart firmware.
4. **Tag and release**: the tag is `v` plus `VERSION` from
   `CMakeLists.txt`. Pushing the tag makes `.github/workflows/release.yml`
   build the `.CVP` and publish the release, and it fails if the tag and
   the version disagree.
5. **List it once**: open an issue at
   <https://github.com/plasticbugs/parrot-cart-plugins> with the
   repository as `owner/repo`. The site picks up later releases by itself.

Tagging, pushing and opening issues act on the user's accounts, so
prepare everything and hand them the commands, or run them only when
they ask. Plugins are free downloads and always link to their source.
Never put ROMs, BIOS files, game data or commercial art in a repository
or a release. A plugin that plays games reads them from the user's card.
GPL or LGPL code needs a source link to the exact tag released.

## What only a console shows

The models don't catch everything a console does. Before a release, ask the
user to try it on a real ColecoVision: start it from the Plugins page
(and by opening a file, if it opens any), press RESET and read the note,
switch the console off and on while it runs, try their USB devices
through a powered hub (and plug a pad in while it runs), and listen to
the sound and look at the picture on the TV. The sound should be about
as loud as a ColecoVision cartridge at the same volume. The simulator
runs a loop much faster than the cart does, so code that is slow on the
cart only shows up there. `docs/testing.md` has
the full list.

## Bigger projects

For an emulator, a game engine, or other code that wasn't written for
the cart, read `references/porting.md` first. It covers the frame kernel's
budget, turning another machine's picture and sound into the
ColecoVision's, memory, speed, and what the published plugins did. A
picture taller than 192 lines can often be scaled by 3/4 to fit whole
with `cvscale34()`, which is usually better than a view the user has to
move around. For an arcade game, read its "Arcade games" section too.
