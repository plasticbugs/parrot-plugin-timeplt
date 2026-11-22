# What in this repository came from elsewhere

The plugin's code is BSD-3-Clause (`LICENSE`). The plugin template it
started from (its build files, `lib/`, `test/`, `tools/`, `docs/` and
workflows) and the copy of the cart's SDK in `sdk/` are by the Parrot Cart
contributors, under the MIT license below. These parts came from other
projects and keep their original licenses:

| Where | What | License |
|---|---|---|
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `sdk/firmware/revc/midihello/tinyusb_fix/hub.c` | TinyUSB 0.18's hub driver with one fix (used only when a plugin builds against the pico-sdk's TinyUSB) | MIT (its header) |
| `sim/third_party/rtmidi/` | [RtMidi](https://github.com/thestk/rtmidi) 6.1.0 (Gary P. Scavone and contributors), unchanged, listed in its `VENDORED.md`: the simulator's MIDI ports | MIT-style (`sim/third_party/rtmidi/LICENSE`) |
| `lua/vendor/lua-5.4.8/` | [Lua](https://www.lua.org) 5.4.8 (Roberto Ierusalimschy, Luiz Henrique de Figueiredo and Waldemar Celes, PUC-Rio), with two lines of `luaconf.h` changed, listed in its `VENDORED.md`: the Lua plugin's language | MIT (`lua/vendor/lua-5.4.8/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |
| `tools/licenses/GPL-2.0.txt`, `GPL-3.0.txt`, `LGPL-2.1.txt` | the GNU licenses, as the [Free Software Foundation](https://www.gnu.org/licenses/) publishes them | verbatim copies, as each text allows |
| `tools/licenses/Apache-2.0.txt` | the Apache License 2.0, as the [Apache Software Foundation](https://www.apache.org/licenses/) publishes it | verbatim copy |

A built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and, if it uses USB, TinyUSB (MIT). Say so in your plugin's
credits (`docs/release-checklist.md`). A plugin written in Lua (`docs/lua.md`)
also contains Lua (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!

## MAME

The code in `src/` was written for this plugin. Parts of it are ports of
MAME's code, or were written with MAME's code as the reference. That code
is under the BSD 3-Clause license (`LICENSE`). It comes from MAME
(<https://github.com/mamedev/mame>, tag `mame0288`), and its copyright
holders are listed here.

| Here | From MAME | Copyright holders |
|---|---|---|
| `src/tp.c`, `src/tp.h`: the memory maps, the inputs, the NMI, the palette, the tiles and the sprites, the sound board's map, its timer and its filter switches | `src/mame/konami/timeplt.cpp`, `timeplt_a.cpp` | Nicola Salmoria |
| `src/tp.c`: the sound latch, the LS259 and the input ports' layout | `src/devices/machine/gen_latch.cpp`, `74259.cpp`, `src/mame/konami/konamipt.h` | Miodrag Milanovic; AJR; Manuel Abadia |
| `src/tp.c`: the scheduler (timeslices, timers, input line events), the screen's timers and the sound streams' updates | `src/emu/schedule.cpp`, `diexec.cpp`, `screen.cpp`, `sound.cpp` | Aaron Giles; O. Galibert |
| `src/z80.c`, `src/z80.h`, `src/tpz80.c`, `src/tpz80.h` | `src/devices/cpu/z80/z80.cpp`, `z80.h` | Juergen Buchmueller |
| `src/z80_ops.h` (generated from `z80.lst` by `tools/tp/z80gen.py`) | `src/devices/cpu/z80/z80.lst` | Juergen Buchmueller, Andrei I. Holub |
| `src/tpay.c`, `src/tpay.h`: the AY-3-8910 | `src/devices/sound/ay8910.cpp` | Couriersud |
| `src/tpay.c`, `src/tpsnd.c`: the RC filters on the AYs' outputs | `src/devices/sound/flt_rc.cpp` | Derrick Renaud, Couriersud |
| `src/tproms.c`: the ROMs' names, sizes and CRC-32s | `src/mame/konami/timeplt.cpp` | Nicola Salmoria |

The zip reader in `src/swzip.c` follows RFC 1951 and the method of
zlib's `contrib/puff` by Mark Adler (zlib license). It was written afresh.

`src/tpcv.c`, `src/tprun.c` and the card and sample code follow the cart's
Donkey Kong, Pac-Man and Q*bert plugins, by Parrot Cart.

Time Pilot is a trademark of Konami. The game is Konami's, 1982. This
plugin contains none of the game.

## The plugin template's license

```
MIT License

Copyright (c) 2026 Parrot Cart contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
