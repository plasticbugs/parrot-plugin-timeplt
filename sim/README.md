# sim/: the plugin simulator

These are notes for anyone working on the simulator itself. To use it,
see `docs/simulator.md`.

## How it works

`test/host/host.c` already models everything a plugin talks to: the two
console kernels and their rules, the card through the SDK's file system,
the menu's launch block, the SDK's USB (`cvusb`) and the PSRAM.
`test/run.sh` drives those models with a script and keeps the last
picture. The simulator builds the same `host.c`, unchanged, and drives it
from a window instead. It draws the picture on the screen each TV frame,
sends the sound chip's bytes to the speakers, uses the keyboard, mouse
and game pads as the controllers and USB devices, and uses the desktop's
MIDI ports as the cart's USB MIDI.

There's only one set of models, so a rule added to `host.c` is checked in
both places. `sim/test.sh` checks that the simulator's pictures and logs
match the ones from `test/run.sh`.

## The parts

| File | What it does |
|---|---|
| `main.c` | the options, SDL, the window, each TV frame, RESET, and the end of a session |
| `model.c` | `#include "../test/host/host.c"` with its hooks connected, plus the few functions the other files call into it |
| `audio.c`, `psg.c` | the PSG bytes, each stamped with the console time it reaches the chip, an SN76489A model, SDL audio and the WAV file |
| `input.c` | the keyboard, mouse and game pads, mapped to the console's controller, USB pads 1 and 2, and a USB keyboard and mouse. It also handles the F2 role and the script's `sim*` words, which make virtual devices for headless tests |
| `midi.c` | the desktop's MIDI ports, through RtMidi's C API. Input goes to the model, output comes from the `SIM_MIDI` hook, and it writes `--midi-log` |
| `third_party/rtmidi/` | RtMidi 6.1.0, unchanged (its `VENDORED.md` has the checksums), built as a static library |
| `card.c` | the SD card, as a FAT32 volume made from a folder, or a disk image |
| `extra.c` | SDK calls that `host.c` doesn't handle, because its examples don't use them, but real plugins do |
| `cxxmain.cpp` | for a plugin whose `main()` is C++ |
| `include/` | stand-ins for a few pico-sdk and TinyUSB headers that `test/host/include` doesn't have |
| `compat/` | what MSVC is missing (`unistd.h`), and a stored-only zlib for a computer without zlib |
| `CMakeLists.txt` | the build for one plugin (`PLUGIN_DIR`), with the USB features that the plugin's `CMakeLists.txt` turns on (`test/host/plugin.cmake` reads them) |
| `run.sh`, `run.bat` | builds and runs in one command |
| `get-sdl.sh` | puts SDL3 in `build-sim/sdl3`, pinned by version and SHA-256 |
| `test.sh`, `wavcheck.py` | the simulator's headless checks |

## How time works

The plugin runs on the main thread and calls the SDK as it would on the
cart. `host.c`'s clock moves forward with each call the plugin makes
(`time_us_32()` advances it). Each time the clock passes a TV frame,
`host.c` runs that frame's list and calls `SIM_FRAME()`, which is
`sim_on_frame()` in `main.c`. There, the simulator draws the frame,
renders its sound, reads the input, and then waits until it's time for
that frame on the desktop. It goes by the audio device's queue when
there's sound, and by the wall clock when there isn't. In headless mode,
it doesn't wait at all.

This way the plugin sees 59.94 frames a second whatever the computer's
speed, and the sound stays in step with the picture. The catch is that a
plugin that never calls the SDK never lets a frame pass. All of our
plugins call it regularly.

## The hooks in test/host/host.c

`host.c` has seven macros. They're empty unless `SIM_HOOKS` is defined,
and model.c defines them:

| Hook | Where | What the simulator does |
|---|---|---|
| `SIM_FRAME()` | after each TV frame and its list | draws, plays the sound, reads the input, and paces the frame |
| `SIM_PSG(bytes, n, t)` | the frame kernel writes `n` PSG bytes, `t` Z80 cycles into the frame | adds them to the audio queue at their cycle |
| `SIM_MAILBOX()` | the plugin publishes the screen kernel's mailbox | takes its sound bytes, if the sound changed |
| `SIM_FAIL()` | after `fail()` reports which rule was broken | shows the red mark, updates the title, and handles `--strict` |
| `SIM_END()` | the end of the session, before the exit | syncs the card, writes the WAV, and shows RESET's screen |
| `SIM_MIDI(bytes, n, to_dev)` | the plugin sends MIDI to a computer, or (`to_dev`) to the MIDI device on the hub | calls `midi_send()`, which writes to the out port and `--midi-log` |
| `SIM_EVENT(what, v)` | a script word that `host.c` doesn't recognize | passes the `sim*` words to `input_script()`, and returns false for any other |

## What changed in test/ for the simulator

These notes are for merging with other work on `test/host`:

- `test/host/tms9918.c` and `.h` are new. They draw the TV picture from
  VRAM and the registers in every TMS9918A mode, with sprites and their
  rules. `host.c` now draws both kernels' pictures with them (before, it
  had separate Graphics II drawing code for the frame kernel).
  `test/vdp/main.c` and the `vdp` block in `test/run.sh` check it against
  MAME, mode by mode.
- `test/host/host.c` has the hooks above. It also keeps the screen
  kernel's registers in `k_reg[8]`. Before, a list's register writes were
  dropped, so its picture assumed the registers from midihello.asm.
  `end_us` is now 64 bits, so it can mean "never".
- `test/host/build.sh` builds `tms9918.c` too.
- `test/host/plugin.cmake` is new. It reads a plugin's `CMakeLists.txt`
  (its ID, name and `cvcart_features()`) for both `build.sh` and
  `sim/CMakeLists.txt`, so the models and the simulator always build a
  plugin with the same `CVUSB_*` switches.
- `sim/test.sh` has the same "the plugin in src/" and "the examples"
  markers as `test/run.sh`. `tools/new-plugin.py` writes a new plugin's
  tests into both files and leaves the examples out of both.
- `test/host/include/pico/stdlib.h` now has `MIN` and `MAX`, and
  `extern "C"` for C++ plugins, like the pico-sdk's header.
- `test/run.sh` has the `vdp` checks. Also, `PC_SCRIPT` and the other
  `PC_*` settings no longer carry over from one run to the next (sh keeps
  a function's `VAR=value` prefixes set after the call).
- `test/host/host.c`'s USB model was extended for the simulator's phase
  2. It has two pads (`pad2=`, `lstick2=`, `rstick2=`, `padkind=`,
  `pad2kind=`), each in a separate slot the way `cvpad.c` keeps them.
  `keydown=E0` to `E7` set the modifiers, the same way a keyboard's
  report has them. The key and MIDI queues are rings of the SDK's sizes
  (32 and 64). The script words and the simulator both call `usb_key()`,
  `usb_mouse()` and `usb_midi_bytes()`.
- `test/usb/` is new. It's a plugin with every USB feature on, and it
  writes each input to its note. The `usb` block in `test/run.sh` and
  `sim/test.sh` runs it with `test/usb/host.script` and `device.script`,
  and `sim/test.sh` runs it again with the `sim*` words.

## Phase 2: USB and MIDI

Here's what `input.c` does with the desktop's devices in each role (F2):

- In the USB keyboard and mouse role, each key press or release calls
  `model_usb_key()`, which builds the keyboard's boot report and passes it
  to the SDK's `cvkeys_report()` (`cvusb_keys.c`), as the cart does.
  SDL's scancodes are HID usages, so nothing needs translating. The mouse
  is in relative mode. Its motion, buttons and wheel go through
  `model_usb_mouse()`, sent as boot reports of up to 127 counts each.
- Game pads use two slots, kept the way `cvpad.c` keeps them, so a pad
  stays in the slot it took. `model_usb_pad()` sets each slot's state and
  kind every frame. The first pad in a slot is also the console's
  controller.
- Leaving the role, or losing focus, releases every key and button.

`midi.c` reads the inputs once a TV frame and passes them to
`model_midi_in()`, which packs them as USB-MIDI packets into the SDK's
queue. What the plugin sends comes through `SIM_MIDI` and goes out right
away, one whole message at a time, with running status written out. With
`MIDI_OUT`, the port named after the plugin is a virtual port in both
directions. Windows doesn't have virtual ports, so there it uses a named
port. In headless mode, no port opens unless one is named, and when one
is open, the run is paced by the wall clock.

`--usb` sets the model's `computer` and `computer_asks` before the plugin
starts. Real MIDI inputs with `MIDI_IN` count as a MIDI device on the hub
(`model_midi_device()`), so the host side can send to it right away.

Headless tests can't press real keys, so the script's `sim*` words (the
`SIM_EVENT` hook) push SDL events for keys, mouse motion, buttons and the
wheel, and attach SDL virtual joysticks for pads. They go through
`input_poll()` the same way a window's events do. In headless mode, the
computer's pads are ignored (their drivers are off), and pad events work
without focus.

## Hook points for later

The simulator doesn't do these things yet. Here's where each one would
go:

- Pad Test doesn't show a device yet, because `include/tusb.h` is a port
  with nothing plugged in. `cvpad_info()` in `host.c` could describe the
  desktop pad.
- There's no per-frame CPU warning, because the simulator can't time the
  cart's code. A rough warning could count the plugin's SDK calls between
  frames, or time the host CPU and scale by a measured factor. We left it
  out because it would mislead more than it would help.
- F12 saves one PNG, but there's no video or GIF yet. A GIF or video key
  would collect `model_picture()` each frame in `sim_on_frame()`.
- MIDI timing could be better. MIDI out leaves as soon as the plugin
  sends it, which can be ahead of the picture and sound in the audio
  queue by up to the queue's length, and it moves in frame-sized steps.
  Stamping messages with the console time and sending them on a thread,
  as `audio.c` does with the PSG, would fix both.
- You can't plug a computer in later. The model, like the cart, picks
  host or device once at the start. A key to plug a computer in or out
  would first need `cvusb` to support switching.
