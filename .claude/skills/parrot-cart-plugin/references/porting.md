# Porting an emulator or other big code

The published plugins are worked examples, each with its full source:

| Plugin | Repository | Core | What to look at |
|---|---|---|---|
| NES | `parrot-plugin-nes` | QuickNES (LGPL-2.1) | `src/nescv.c`, the picture encoder the others reuse, and `nesapu.h` for sound |
| Game Boy | `parrot-plugin-gameboy` | Peanut-GB (MIT) | saves written in `cvf_on_exit()`, a speed-up that skips the game's wait loops |
| Master System, Game Gear | `parrot-plugin-sms-gamegear` | SMS Plus (GPL) | another machine on `nescv` |
| PC Engine | `parrot-plugin-pcengine` | PCE-GO (GPL-2.0) | a picture bigger than the TV: view keys and squeezing |
| Vectrex | `parrot-plugin-vectrex` | its own, following MAME's Vectrex driver (BSD-3-Clause) | vectors drawn into a bitmap |
| SCUMM | `parrot-plugin-scumm` | ScummVM's SCUMM engine (GPL) | a 900 KB plugin run from flash (`XIP`), a mouse pointer as a sprite, MIDI out to a computer |

They are at `https://github.com/plasticbugs/<repository>`. Read the one
closest to the job before designing anything.

## Plan around the budgets

| Budget | Size |
|---|---|
| CPU | one Cortex-M33 at 150 MHz for the plugin (core 1 serves the bus) |
| a TV frame | 16.7 ms |
| video RAM writes | about 1,800 bytes a frame, in the frame kernel |
| SRAM | about 507 KB for code, data and heap |
| PSRAM | 8 MB, slower than SRAM and read through a cache |

- **Keep the game's speed and the picture's rate apart.** Run the core at
  its own speed, 60 frames of game time a second, and send the TV a new
  picture only when the last one has fully arrived. A busy picture then
  takes a few frames, the game doesn't slow down, and the console always
  shows a whole picture. The NES and PC Engine plugins work this way,
  and the PC Engine plugin's note shows the pictures a second it got.
- **Don't change the system clock.** The SDK's bus loop on core 1 is
  timed for 150 MHz. A 200 MHz build of the PC Engine plugin crashed the
  console.
- **Big data goes in PSRAM**: the game's ROM, buffers, tables. Hot loops
  and the data they touch every cycle stay in SRAM. A cache miss to PSRAM
  costs far more than an SRAM read.
- **Measure on the cart.** The simulator runs the loop far faster than the
  M33 does. Put the slowest frame time, pictures a second and frames
  missed into the note (`cvf_note`), so the user can read them after
  RESET and tell you.

## Timing

`parrotcart_frames()` builds the plugin with the frame kernel and the
SDK's two-sample bus loop (`cvframes_serve2()`, `cvfserve.S`). Core 1
acts on an address only when two samples of the console's pins, 60 ns
apart, agree. The older loop acted on one sample. On a console, the
74HCT245 switching the data bus spoils a sample now and then, and with
the old loop The Empire Strikes Back got wrong bytes: commands out of
place, a green border, a restart. With the new loop it played clean, and
all the cart's frame plugins are built with it now. Don't switch it off,
and leave the clock at 150 MHz, which it is timed for.

- **The budget.** A frame is 59,736 Z80 cycles, and the vertical blank
  is the first 15,960 of them. Add up each list's cost as you build it,
  by the costs in `gbkernel.asm`'s header: 480 for the kernel's start, a
  video RAM run 106 plus 23 a byte in the blank and 31 a byte after it,
  sound 75 plus 52 a byte (the chip holds the Z80 for about 29 cycles at
  each byte), a register 114, and 54 and 52 for the two ends. The models
  in `test/run.sh` and the simulator fail a list whose blank part runs
  past 15,360 cycles or whose whole runs past 58,536. The template keeps
  2,000 and 4,000 cycles in hand. That leaves room for about 1,800 bytes
  of video RAM a frame.
- **Safe to run twice.** When no new list has come, the console runs the
  last one again. Absolute video RAM writes, registers and sound bytes do
  no harm twice. Writing the noise's kind and rate restarts the noise, so
  write that register only when it changes.
- **Publishing.** `cvf_publish()` copies the list and returns false while
  the one before is still waiting. If you keep a copy of the console's
  video RAM, never drop a list. Keep it and offer it again until it's
  taken, or your copy and the console part ways. Make the next list as
  soon as the last is taken. A list that isn't there in time means the
  console runs the last one again, and its sound repeats.
- **The note.** The cart's arcade plugins leave one like
  `60/s 0L 8.7ms s0 h1`: pictures a second, lists the console ran again
  since the start, the slowest list lately, and from `cvf_bus_counts()`
  the addresses the bus loop refused (`s` looked like a select, `h` like
  the header). A few refusals are normal. They are addresses the old loop
  would have acted on.

## The picture

The TMS9918 has 15 fixed colours, two per 8x1 run, and 256x192 pixels.
Most machines have more colours, finer colour placement and a taller
picture.

- **Colours**: map the machine's palette to the nearest of the 15 (the
  encoders use a table), then pick two colours per 8x1 run. Dark colours
  mostly vanish, because the palette has almost none.
- **Tiles**: if the machine draws from 8x8 tiles, as the NES, SMS and PC
  Engine do, cut the picture along its tile grid. Most cells repeat, so
  each third of the screen keeps a cache of 256 cell patterns and a
  picture costs 16 bytes for each new cell plus the names that change.
  `nescv.c` does this with double-buffered name tables, so a picture is
  never shown half drawn.
- **Height**: games draw 224 to 288 lines and the TV shows 192. Scaling
  the whole picture by 3/4 often fits it with nothing cut off, and the
  SDK does it: `cvscale34()` in `cvscale.h`, built into every frame
  plugin. Of each 4 lines it keeps the 1st and the 4th and turns the
  middle two into one. Where one of those two differs from the lines on
  both sides of it, it keeps that one, so a stroke 1 pixel wide survives.
  Then it does the same with the columns. It works on pens (the machine's
  colour numbers, any 8-bit values), so scale first and map to the
  TMS9918's colours after. A 224x256 picture becomes 168x192, and a
  240x256 one 180x192, with black at the sides.

  ```c
  // w x h pens (multiples of 4) to 3w/4 x 3h/4; columns cx0 to cx1 - 1 only (0, w: all)
  cvscale34(src, src_stride, w, h, 0, w, dst, dst_stride);
  ```

  A scaled picture no longer lines up with 8x8 tiles, so send it as a
  bitmap: keep a copy of the console's pattern and colour tables and send
  the bytes that change. A view the user moves with keypad keys is the
  other way, for games where fine detail matters more than seeing the
  whole screen, such as ones full of text. Pick a sensible start for it.
- **The arcade layout**: a portrait arcade screen 224 wide comes out 168
  wide at 3/4. Put it in the middle, at x 44, and five 8-pixel columns on
  each side are free, since none of them shares an 8x1 run with the game.
  The cart's Donkey Kong and Pac-Man plugins cut the score, high score,
  lives, level and bonus out of the status rows and set them in those
  margins, with the play area in the middle. Scale each piece with
  `cvscale34()`'s `cx0` and `cx1` as part of the whole picture. Then a
  piece scaled alone, after it changes, looks just as it would in the
  whole. If the play area is still too tall, scale only it, and move the
  rest to the margins the same way.
- **Sprites**: the TMS9918's 32 sprites, 4 to a line, are useful for a
  mouse pointer or a few objects, not for another machine's 64.
- **Model video RAM.** Keep a copy of what the console holds, send only
  the bytes that differ, and keep each list until `cvf_publish()` takes it
  so the model stays in step.

## The sound

The SN76489 has three square-wave voices and a noise voice. Read the
machine's sound registers once a TV frame and turn them into PSG bytes:
the three most important tones, and noise for drums. Samples and voices
don't carry over. `nesapu.h` and the SMS plugin are examples. Send the
bytes in the frame list (command 2), and expect a sound write to cost the
Z80 about 29 cycles more than the chip's datasheet says.

Write the whole chip in every list: each voice's tone and volume and the
noise volume, including what hasn't changed and the volumes of voices
you don't use. Now and then the console reads a byte from the cart
wrong, and if that byte was a sound write, the register stays wrong
until something writes it again. An arcade plugin that set its tones up
once, and never wrote the voices it didn't use, got a low buzz under the
game after one bad byte. It lasted until RESET. Ten bytes cost about 1%
of a frame. If you play samples through the volume registers, set the
tones' pitch again in every list as well. Leave the noise's kind and
rate until they change, because writing that register restarts the
noise.

Samples can go out through two tones parked at a period of 1, with their
volumes summed as a DAC. The cart's Q*bert, Pac-Man and Donkey Kong
plugins play their sound that way, about 6,000 samples a second. Then
every list is the same length and the console runs the lists back to
back, so a list's length is the sample clock. Their lists leave the blank
part empty and put each sample's bytes at its time between the picture's
runs (31 cycles a byte). It's more work than tones, so try tones first.

### How loud

The cart's first arcade plugins came out far too quiet next to a
cartridge, so the TV had to be turned well up for them. Aim for the
loudness of an ordinary ColecoVision cartridge at the same TV setting.

Four cartridges recorded in MAME with `-wavwrite` and measured with
`tools/wavlevel.py` give an `rms` of -20.5 to -23.9 dBFS (Donkey Kong
-20.5, Smurf -21.1, Lady Bug -23.9, Mr. Do! -23.9), about -22 on
average. Aim within 3 or 4 dB of that and not above it. The first
arcade plugins measured about 12 dB below it.

- With tones, map the machine's full volume to the chip's full volume
  (attenuation 0), not to the middle of its range, and keep the balance
  between the voices.
- With samples, scale the mix up to that level, and round the peaks off
  with a soft knee instead of clipping them. Below the knee k (about 3/4
  of full scale) a sample stays as it is. Above it, with x and y as
  fractions of full scale and the sign kept:
  `y = k + (1 - k) * (1 - exp(-(x - k) / (1 - k)))`. Put it in a table.
- Measure it. Record a cartridge you own in MAME with `-wavwrite`, and
  the plugin's lists with `MAME_WAV=plugin.wav tools/mame-replay.sh`, then
  compare them with `python3 tools/wavlevel.py cart.wav plugin.wav`
  (`docs/testing.md`, "How loud"). Then ask the user to listen on a TV.

## Arcade games

Every arcade plugin uses the same keys, so someone who knows one knows
them all:

| | ColecoVision controller | USB pad |
|---|---|---|
| move | the stick | the d-pad or the left stick |
| fire (or jump) | either fire button | any face button |
| coin | keypad * or # | - (minus; Share on a PS4 pad) |
| one-player start | keypad 1 | + (plus; Options on a PS4 pad) |
| two-player start | keypad 2 | |
| back to the menu | RESET | |

If the game also starts on a fire button, keep that. A one-player game
leaves keypad 2 unused. A game with two fire buttons can give each one
its own: the left fire and `GP_SOUTH`, the right fire and `GP_EAST`. Make
a coin a pulse of about 6 frames from the press, however long the key is
held, and need the key up for 3 frames before the next one. Some games
ignore a coin switch held shut for long, and a key that bounces should
still give one coin.

Read the pad yourself, as below, and not through `cvin_update()`. That
one presses * and # with the pad's top and left buttons, which in an
arcade game would be coins.

```c
// once a list, for player 1: the console's controller and USB pad 1
uint8_t joy, fire, lines;
cvf_input(&joy, &fire, &lines);                     // low = pressed
const int key = cvin_decode_key(lines);             // cvinput.h: 0-9, CVIN_STAR, CVIN_HASH, -1
uint32_t dirs = 0, buttons = 0;
gamepad_t p;
if (cvusb_pad(0, &p)) { dirs = cvusb_pad_dirs(&p); buttons = p.buttons; }
const bool up = !(joy & 1) || (dirs & GP_UP);      // right 2, down 4, left 8, the same way
const bool shoot = (fire & 3) != 3 || (buttons & (GP_SOUTH | GP_EAST | GP_WEST | GP_NORTH));
const bool coin = key == CVIN_STAR || key == CVIN_HASH || (buttons & GP_MINUS);
const bool start1 = key == 1 || (buttons & GP_PLUS);
const bool start2 = key == 2;
static int coin_left, coin_wait;                    // one coin a press
if (coin && !coin_wait) coin_left = 6;
if (coin) coin_wait = 3; else if (coin_wait) coin_wait--;
const bool coin_switch = coin_left > 0;             // the game's coin input for this list
if (coin_left) coin_left--;
```

The pads need nothing more from the plugin. The SDK finds a pad that's
already on a powered hub when the plugin starts, and one plugged in or
pulled out while it runs (it reads the ports of hubs that never report a
change). On a console that's been tried with the Switch Pro Controller, a
PS4 controller, 8BitDo pads in their Switch mode, and an Anker hub with a
Genesys GL3523 chip. A game whose stick has four ways, not eight, should
turn a diagonal into one of them the way MAME does: the way that came on
last wins.

## Memory and the build

- Emulator cores usually call `malloc`: add `HEAP`, call
  `cvheap_reserve()` first, and watch `cvheap_largest()`.
- Remove `printf` and file I/O from the core, or point them at your own
  functions. A stray `fprintf` pulls in about 19 KB of newlib.
- Build the core with `-O2` or `-O3`, and its vendored code with its
  warnings off. Keep the core in `vendor/` with a `VENDORED.md` that lists
  your changes.
- If it doesn't fit below `0x2007EC00`, try `-Os` for cold code first.
  `XIP` (run from flash) is the last step, and the code that talks to the
  console still has to be in RAM (`cvxip.h`).

## Testing on the Mac first

Compile the core and your encoder for the Mac against the kit's models
(`test/host/build.sh`, `sim/run.sh --src ... --include ...`). Then:

- play it in the simulator with a game the user owns, from a folder given
  as `--card`;
- look at `--shot` pictures against what the machine would show;
- add a block to `test/run.sh` so the encoder's lists are checked
  against the real kernel in MAME.

Never commit or attach a game, a BIOS or a ROM. Tests read them from the
user's own folders, and README files tell users where to put their games
on the card (`HOME`) and which files the plugin opens (`OPENS`).

## Licenses

The core's license usually decides the plugin's. GPL and LGPL code needs
the README to link to the source of the exact release (the tag), and
every core, font and table that isn't yours goes in the README's credits
and `THIRD-PARTY.md`, with its license.
