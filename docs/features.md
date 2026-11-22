# Features you can turn on

You turn on the features your plugin uses with one line of CMake. Each
feature adds its code, sets up the USB port with only what that feature
needs, and adds its flag to the `.CVP`'s `USES`. Features you don't turn
on add no code to your plugin.

```cmake
cvcart_features(myplugin PADS KEYBOARD MOUSE MIDI_IN MIDI_OUT PSRAM CARD)
```

Put it before `cvcart_plugin()`. `tools/new-plugin.py` writes this line
for you, and puts working code for each feature in `src/main.c` so you
can see it on the TV:

```sh
tools/new-plugin.py                         # asks, a question at a time
tools/new-plugin.py --name "Star Sweeper" --by "Ann" \
    --features pads,keyboard,midi-in --screen text
```

The new plugin leaves out the kit's `examples/`, unless you add
`--examples`. The examples named below are in the kit.

| Feature | What plugs in | The USB port | `USES` |
|---|---|---|---|
| `PADS` | up to two game pads | host | usb-host |
| `KEYBOARD` | a USB keyboard | host | usb-host |
| `MOUSE` | a USB mouse | host | usb-host |
| `MIDI_IN` | a USB MIDI keyboard, or any class-compliant MIDI device | host | usb-host |
| `MIDI_OUT` | a computer, with the cart as a MIDI device on it | device | usb-device |
| `PSRAM` | nothing (it's the cart's 8 MB PSRAM) | | psram |
| `CARD` | nothing (it's the SD card, for files and saves) | | |

`tools/new-plugin.py --name "Feature Test" --by You --features all`
makes a plugin with every feature turned on and every input shown on the
TV. It's a quick way to try a pad, a hub or a keyboard on the console.

## The USB port: host or device

The cart's USB port works as either a host or a device.

- As a host, it's for `PADS`, `KEYBOARD`, `MOUSE` and `MIDI_IN`. The
  cart can't power what's plugged in, so put a powered USB hub between
  the devices and the cart. A hub can take several devices at once, such
  as two pads, a keyboard, a mouse and a MIDI keyboard together. The SDK
  finds what's already on the hub when the plugin starts, and what's
  plugged in or pulled out while it runs, so a plugin only reads its
  devices. Some hubs never report a change, so it reads every hub's
  ports every 100 ms.
- As a device, it's for `MIDI_OUT`. The cart shows up as a MIDI port on a
  computer, named after the plugin.

If you turn on host features and `MIDI_OUT` together, the port starts as
a device. If a computer connects to the cart within 1.5 seconds, the
port stays a device. Otherwise (with a hub, or with nothing at all), it
becomes a host. A Mac asks its user before it accepts a new MIDI device
("Allow accessory to connect?"). While the Mac is asking, the port waits
for up to a minute, and `cvusb_mode()` returns `CVUSB_ASKING`. Show that
on the screen, and let a button call `cvusb_host_now()` to stop waiting.
The port picks host or device once, at the start.

MIDI works in both directions on whichever MIDI connection there is. As
a device, `cvusb_midi_read()` gets what the computer sends, and
`cvusb_midi_send()` sends to the computer. As a host with `MIDI_IN`, both
talk to the MIDI keyboard.

## The calls

They're all in `sdk/firmware/revc/sdk/cvusb.h`:

```c
#include "cvusb.h"

cvusb_start();                  // once, after cvk_start() or cvf_start()
for (;;) {
    cvusb_task();               // every pass of the loop: it never waits
    ...
}
```

If you call a function for a feature that's turned off, the build fails
with an error that names the feature, such as `turn on KEYBOARD in
cvcart_features()`.

### Game pads: `PADS`

```c
gamepad_t p;
if (cvusb_pad(0, &p)) {         // pad 0 or 1: false if it isn't there (yet)
    p.buttons                   // GP_SOUTH, GP_EAST, GP_UP, GP_L, GP_ZR ... held (gamepad.h)
    p.lx, p.ly, p.rx, p.ry      // the sticks, -2048..2047, y up
}
cvusb_pad_kind(0)               // "SWITCH PRO" or "HID PAD": what it is
cvusb_pad_dirs(&p)              // GP_UP GP_DOWN GP_LEFT GP_RIGHT: the d-pad, or the left stick past about 40%
```

Buttons are named by where they are, so `GP_SOUTH` is the bottom face
button (B on a Nintendo pad, A on an Xbox one). The Switch Pro
Controller, pads in Switch mode and plain HID pads are supported. A
Switch pad takes a moment to start after it's plugged in, so
`cvusb_pad_kind()` can name it before `cvusb_pad()` returns it. The
triggers are buttons (`GP_ZL`, `GP_ZR`). On a PS4 pad, Share is
`GP_MINUS` and Options `GP_PLUS`.

On a console, the cart has been tried with the Switch Pro Controller, a
PS4 controller, 8BitDo pads in their Switch mode, and an Anker hub with a
Genesys GL3523 chip. That hub never reports a change, which is why the
SDK reads the ports. With it, a pad was found when plugged in, when
pulled out, and when it was already on the hub at RESET.

`cvinput.h` puts USB pad 1 together with the console's controller, for
plugins that want one set of controls. The arcade plugins' keys are in
the skill's `references/porting.md` ("Arcade games").

### The keyboard: `KEYBOARD`

```c
cvkey_t k;
while (cvusb_key(&k)) {         // each key as it goes down and as it comes up
    k.usage                     // the key's USB code: 0x04 is A, 0x28 Enter (HID's usage table)
    k.ascii                     // its character on a US keyboard, 0 for none (the arrows, F1)
    k.mods                      // CVKEY_SHIFT, CVKEY_CTRL, CVKEY_ALT, CVKEY_GUI: held with it
    k.down                      // true: it went down
}
```

The modifier keys come in as keys too (usage 0xE0-0xE7). Up to 32 keys
can be waiting to be read.

### The mouse: `MOUSE`

```c
cvmouse_t m;
if (cvusb_mouse(&m)) {          // false: no mouse
    m.dx, m.dy                  // how far it moved since the last call, y down
    m.wheel                     // the same, for the wheel
    m.buttons                   // 1 left, 2 right, 4 middle
}
```

### MIDI: `MIDI_IN`, `MIDI_OUT`

```c
uint8_t m[3];
int n;
while ((n = cvusb_midi_read(m)) > 0) {      // n bytes: m[0] the status (0x90 note on ... | channel)
    ...
}
const uint8_t note_on[3] = {0x90, 60, 100};
cvusb_midi_send(note_on, 3);    // whole messages (a SysEx too); false: nowhere to send it, or no room now
```

Incoming MIDI includes note, controller, program and pitch messages, the
clock, and the other one-byte messages, but not SysEx. Up to 64 messages
can be waiting, and `cvusb_midi_dropped()` counts any that didn't fit.

### The port

```c
cvusb_mode()                    // CVUSB_HOST, CVUSB_DEVICE, CVUSB_WAITING, CVUSB_ASKING
cvusb_mode_name(cvusb_mode())   // "HOST", "ON A COMPUTER" ...: for the screen
cvusb_status_t s;
cvusb_status(&s);               // s.pads, s.keyboards, s.mice: how many; s.midi: a MIDI connection
```

As a host, the USB work runs every millisecond from a timer interrupt, so
the pads are still read while your plugin is busy. If your plugin uses
these calls, please don't also call TinyUSB's functions (`tuh_*`,
`tud_*`) directly, since `cvusb` is already running TinyUSB for you.

### Player 1: `cvinput.h`

`cvinput.h` combines the console's controller and USB pad 1 into one
player, for either kernel. With `PADS` on, a USB pad works like the
console's controller, the same way it does in the cart's menu. The d-pad
or left stick is the stick, B and A are the fire buttons, and X and Y are
the keypad's `*` and `#`. Without `PADS`, it reads only the console's
controller.

```c
cvin_t in;
cvin_init(&in);
cvin_report(&in, kind, bits);   // the screen kernel: each cvk_report()
cvin_frame(&in, joy, fire, key);    // the frame kernel: each frame's cvf_input()
cvin_update(&in);               // once a step
in.held                         // CVIN_UP, CVIN_FIRE_L ...: down now
in.key                          // 0-9, CVIN_STAR, CVIN_HASH, or -1
cvin_pressed(&in), cvin_key(&in)    // what went down since you last asked
```

### The PSRAM: `PSRAM`

```c
if (cvpsram_init()) {           // once, at the start
    CVPSRAM_CACHED[i]           // a uint8_t pointer to all 8 MB
}
```

`examples/psram` has more.

### The card: `CARD`

`cvsd.h` covers the card and its FAT or exFAT file system, reading
files, and writing a file in place. `examples/sd-card` lists a folder and
keeps a save file, and `examples/open-file` reads the file the menu
started the plugin with. A plugin made by `tools/new-plugin.py` keeps its
save in its home folder (`HOME`), or in `/PLUGINS` next to the plugin if
it has no home folder. With `--opens .ext`, `tools/new-plugin.py` turns
`CARD` on even without saves, and its `src/main.c` opens the file the
menu started it with and shows the file's name and size. The file system can't make folders, so the user
needs to make the home folder.

## What it costs

A plugin runs from the cart's RAM, which is about 500 KB, so its code
uses RAM too. Here's what each feature adds to a plugin with no features
(GCC 9.2.1):

| Features | Code | Data |
|---|---|---|
| `PADS` | 41.7K | 4.8K |
| `KEYBOARD` | 32.5K | 3.8K |
| `MOUSE` | 30.2K | 3.7K |
| `MIDI_IN` | 28.9K | 3.4K |
| `MIDI_OUT` | 14.1K | 7.1K |
| `PADS MIDI_OUT` | 53.8K | 12.0K |
| all five USB features | 60.8K | 13.2K |

Most of that is the USB host code, which the host features share, so
each one after the first adds only a few KB. `PSRAM` and `CARD` only
cost what your plugin calls.

## Trying it on a Mac

`test/run.sh` builds your plugin's code for the Mac and runs it against a
model of the console and the USB port. It reads `cvcart_features()` from
`CMakeLists.txt`, and its script can plug in devices (docs/testing.md):

| Script | What happens |
|---|---|
| `pad=south+left`, `lstick=1500,-900`, `rstick=0,2047`, `pad=off` | a game pad |
| `pad2=east`, `lstick2=...`, `rstick2=...` | a second game pad |
| `padkind=hid`, `pad2kind=hid` | a pad that isn't a Switch Pro Controller (`switch` is one) |
| `keydown=04`, `keyup=04`, `mods=02` | a keyboard, by USB code (`E0` to `E7` are the modifiers) |
| `mouse=40,20,1,1` | a mouse, with dx, dy, buttons and wheel |
| `midiin=90,3C,64` | MIDI coming in |
| `computer=0`, `computer=ask` | no computer on the port, or one that asks its user first |

The simulator (docs/simulator.md) reads `cvcart_features()` too, and
gives the plugin your computer's game pads, keyboard, mouse and MIDI
ports.

The model can't check real devices, a real hub or a real computer, so
try those on a console.

## Not here yet

- Keyboards and mice in report protocol. They're read in the simpler
  boot protocol, so a keyboard's media keys and a mouse's extra buttons
  don't come through.
- Keyboard layouts other than US. `usage` is right, but `ascii` is the
  character of the US key.
- MIDI on more than one cable (port) of a device, or from more than one
  MIDI device at a time.
- Switching the port between host and device after the start, for
  example when a computer is plugged in later.
- A DualShock 4's right stick. A pad's right stick is read from its Rx
  and Ry when it has them, and from Z and Rz otherwise. A DualShock 4 has
  both, with its triggers on Rx and Ry, so its right stick reads as its
  triggers.
