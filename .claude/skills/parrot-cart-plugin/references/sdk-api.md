# The SDK's calls, by area

A map, not the whole reference. Each section names the header to read for
the details. Paths are from the kit's top folder. `SDK` is
`sdk/firmware/revc/sdk`, `MH` is `sdk/firmware/revc/midihello`.

## Contents

- CMake
- The screen kernel (`cvk_`, `cvs_`, the canvas)
- The frame kernel (`cvf_`) and its lists
- Scaling a picture (`cvscale34`)
- The controller
- Sound
- USB (`cvusb_`)
- The SD card (`cvsd_`, `fs_`)
- PSRAM and the heap
- The launch block and notes

## CMake

```cmake
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)          # sets up the pico-sdk and the SDK
project(myplugin C CXX ASM)
parrotcart_init()

add_executable(myplugin src/main.c)
parrotcart_screen(myplugin)              # or parrotcart_frames(myplugin)
cvcart_features(myplugin PADS KEYBOARD MOUSE MIDI_IN MIDI_OUT PSRAM CARD)   # only what it uses
cvfont_use(myplugin)                     # optional: the 6x8 font
cvcart_plugin(myplugin ID you.myplugin NAME "My Plugin" VERSION 0.1.0 BY "You"
              HOME /MYPLUGIN OPENS .ext ICON src/icon.txt FILE_ICON src/file_icon.txt
              README README.md [USES standalone] [HEAP] [XIP] [FILE OTHER.CVP])
```

`cmake/parrotcart.cmake` explains `parrotcart_*`, and
`SDK/cvplugin.cmake` explains the rest. `cvcart_features()` sets the
`.CVP`'s USES for you. `USES standalone` is for a plugin with `OPENS`
that can also start without a file (`docs/plugin-rules.md`).
`parrotcart_frames()` is `cvframes_use()` and `cvframes_serve2()` (the
two-sample bus loop) with `cvscale.c` built in. Use it rather than
`cvframes_use()` alone. A Lua plugin uses `parrotcart_lua(target MAIN
src/main.lua FILES ... HOME /FOLDER)` instead of `parrotcart_screen()`
(`docs/lua.md`).

## The screen kernel

`SDK/cvkernel.h`, `SDK/cvscreen.h`, `MH/vdp.h`. `src/main.c` in the kit is
the shape every screen plugin has.

| Call | What it does |
|---|---|
| `cvk_start(&cfg)` | first thing in `main()`. `cfg` is `{rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN}` from the generated `rom_image.h` and `handoff.h` |
| `cvk_task(now_us)` | every pass of the loop. Doesn't return on RESET |
| `cvk_report(now_us, &kind, &bits)` | what the console said: `REPORT_KIND_HELLO`, `REPORT_KIND_ACK` (pass them to `cvs_report`), or controller reports (pass them to `wired_report`) |
| `cvs_init(&scr)` | the screen |
| `cvs_update(&scr, draw, ctx, list, sizeof list, now_us)` | calls `draw(canvas, all, ctx)` when the console is ready, and returns the list of changes. Call it every 20 ms or so |
| `cvk_list(list, n, scr.out_full, scr.seq)` | sends that list |
| `cvs_line(c, x, y, w, text, fg, bg, scale)` | a line of text in the 8x8 font, filling `w` pixels |
| `canvas_fill(c, x, y, w, h, colour)` | a filled box |
| `canvas_text(c, x, y, text, colour, scale)` | text with no background |
| `cvfont_line(c, x, y, cols, text, fg, bg)`, `cvfont_char(...)` | the 6x8 font with lower case (`cvfont_use`) |

The canvas is 256x192 with two colours per 8x1 run. Colours are `C_BLACK`
to `C_WHITE` in `MH/vdp.h`. Write the whole picture in `draw()` each time,
because the SDK sends only what changed.

## The frame kernel

`SDK/cvframes.h`, and the costs in the header of
`sdk/gbcv/kernel/gbkernel.asm`. `examples/frames/main.c` is the example.

| Call | What it does |
|---|---|
| `cvf_start(&cfg)` | first thing. `cfg` is `{gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN}` |
| `cvf_task(now_us)` | every pass |
| `cvf_frames()` | TV frames started so far. When it changes, make the next list |
| `cvf_waiting()` | a list is published and not yet taken |
| `cvf_publish(list, n)` | the console runs it at the next frame's start |
| `cvf_input(&joy, &fire, &key)` | the controller as the kernel read it, low means pressed (or use `cvinput.h`) |
| `cvf_on_exit(fn)` | runs once on the way back to the menu, with the console parked: write saves here |
| `cvf_note(text)` | the note for the menu |
| `cvf_bus_counts(&unsel, &early, &unhdr)` | what the bus loop refused since the start: addresses that looked like a select, selects taken as their read ended, addresses that looked like the header. For the note |

A list has two parts, each ended by `0`: the first runs in the vertical
blank (a video RAM byte every 23 Z80 cycles), the second while the
picture is drawn (a byte every 31). Commands:

```
1, low, high | 0x40, n, n bytes    write n bytes of video RAM at an address
2, n, n bytes                      write n bytes to the sound chip
3, value, 0x80 | r                 set video register r (first part only)
0                                  the end of a part
```

A frame is 59,736 Z80 cycles, about 1,800 bytes of writes. The kernel
sets up Graphics II with patterns at `0x0000`, colours at `0x2000` (6,144
bytes each) and names at `0x1800`. A full picture is 12,288 bytes, so
keep a copy of what the console's video RAM holds and send what differs,
as many bytes as fit. Every list must be safe to run twice, because the
console runs the last one again when no new one has come. `cvf_publish()`
returns false while the last list is still waiting. Keep the list and
offer it again, and never drop one that your copy of video RAM counts on
(`porting.md`, "Timing").

## Scaling a picture

`SDK/cvscale.h`. Every frame plugin has it (`parrotcart_frames()`). A
screen plugin adds `${PARROTCART_SDK}/sdk/cvscale.c` to its sources.

| Call | What it does |
|---|---|
| `cvscale34(src, sstride, w, h, cx0, cx1, dst, dstride)` | `w` x `h` pens (multiples of 4) to 3/4 of that each way. Of each 4 lines, then each 4 columns, it keeps the 1st and 4th and makes the middle two one, so 1-pixel strokes and edges stay. Only columns `cx0` to `cx1 - 1` (multiples of 4; `0, w` for all), which land at `3 * cx0 / 4` in `dst`: a piece comes out as the whole picture would have it there |
| `cvscale_pick()`, `cvscale_mid()` | the same rules, inline, for a plugin with its own loop |

Pens are any 8-bit values, compared only for being the same, so scale the
machine's colour numbers and map them to the TMS9918's after. Make pen 0
the background: the line rule counts it as that, and outside the picture
the neighbours are 0.

## The controller

- Screen kernel: `lib/wired.h`. `wired_init(&pad)`, `wired_report(&pad,
  kind, bits)` for each controller report, then `pad.held` (`WIRED_UP
  RIGHT DOWN LEFT FIRE_L FIRE_R` bits), `wired_pressed(&pad)` (newly
  pressed, then cleared) and `wired_key(&pad)` (0 to 9, `WIRED_STAR`,
  `WIRED_HASH`, or -1).
- Either kernel, with USB pad 1 as player 1: `SDK/cvinput.h`
  (`cvin_init`, `cvin_report` or `cvin_frame`, `cvin_update`,
  `cvin_pressed`, `cvin_key`).

## Sound

Ten bytes in the SN76489's own format: for voices 0 to 2, tone latch,
tone high and volume latch, then the noise volume. `cvk_sound(snd)` sends
them on the screen kernel, and in the frame kernel they go in the list
(command 2). `cvk_silence` is ten bytes of silence (all zeros is a tone).

```c
#define PSG_CLOCK 3579545      /* pitch = PSG_CLOCK / (32 * n), n 1 to 1023 */
static void psg_tone(uint8_t snd[10], int ch, unsigned hz, unsigned att) {   /* att 0 loud .. 15 off */
    unsigned n = hz ? PSG_CLOCK / (32 * hz) : 1;
    if (n > 1023) n = 1023;
    snd[3 * ch + 0] = (uint8_t)(0x80 | ch << 5 | (n & 15));
    snd[3 * ch + 1] = (uint8_t)(n >> 4);
    snd[3 * ch + 2] = (uint8_t)(0x90 | ch << 5 | att);
}
```

`examples/sound/main.c` also has the noise voice and a fading drum. On a
real console each sound write stalls the Z80 for about 29 cycles, which
the kernels already allow for.

## USB

`SDK/cvusb.h`, `docs/features.md`. Turn features on with
`cvcart_features()`. Then:

```c
cvusb_start();                                  // once, after cvk_start/cvf_start
cvusb_task();                                   // every pass, never waits
gamepad_t p;   if (cvusb_pad(0, &p)) ...        // PADS
cvkey_t k;     while (cvusb_key(&k)) ...        // KEYBOARD: usage, mods, ascii, down
cvmouse_t m;   if (cvusb_mouse(&m)) ...         // MOUSE
uint8_t msg[3]; while (cvusb_midi_read(msg) > 0) ...   // MIDI_IN or MIDI_OUT
cvusb_midi_send(bytes, 3);
```

USB devices need a powered hub, because the cart can't power them. The
SDK finds a pad already on the hub at the start, and one plugged in or
pulled out later, so a plugin only reads pads. `cvusb_pad_dirs(&p)` gives
the d-pad, or the left stick past about 40%, as `GP_UP GP_DOWN GP_LEFT
GP_RIGHT`. A getter for a feature that is off is a build error that names
the switch.

## The SD card

`SDK/cvsd.h`, `sdk/firmware/revc/sdplayer/fsro.h`,
`examples/sd-card`, `examples/open-file`. `CARD` in `cvcart_features()`.

| Call | What it does |
|---|---|
| `cvsd_init()` | starts the card (0.1 to 1.5 s) |
| `cvsd_resume()` | uses the card as the menu left it, without starting it again. Falls back to `cvsd_init()` when it returns false |
| `cvsd_open_launched(&fs, launch, writable, &file)` | mounts the card and opens the file the menu launched the plugin with |
| `fs_find(&fs, "/DIR/NAME", &e)`, `fs_dir_open_path`, `fs_dir_next` | find files, list folders |
| `fs_open(&fs, &f)`, `fs_lba(&fs, sector)` + `cvsd_read(lba, buf)` | read a file by 512-byte sectors |
| `fs_create(&fs, dir, name16, n, size, &f, &made)` | a new file at its final size (names are UTF-16) |
| `fs_mkdir(...)` | a folder |
| `fs_write_sec(&fs, sector, buf)` | write a sector of a file, in place |

## PSRAM and the heap

- `SDK/cvpsram.h`: `cvpsram_init()`, then 8 MB at `CVPSRAM_CACHED` (read
  through the cache) and `CVPSRAM_UNCACHED` (32-bit writes, for filling
  it fast). Slower than SRAM: keep hot loops and their data in SRAM.
- `SDK/cvheap.h`: with `HEAP`, call `cvheap_reserve()` before the first
  `malloc`. `cvheap_free()` and `cvheap_largest()` measure it.

## The launch block and notes

- `cvk_launch()` / `cvf_launch()` return the launch block (`SDK/cvlaunch.h`)
  or NULL: `path` is the file the menu opened the plugin with ("" for
  none). Open it with `cvsd_open_launched()`, not by `path`.
- `cvk_note(text)` / `cvf_note(text)`: up to 32 characters, shown on the
  menu's bottom line after RESET.
