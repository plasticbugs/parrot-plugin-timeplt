# Time Pilot

This plugin lets Parrot Cart play Konami's 1982 Time Pilot arcade game on the ColecoVision. The cart emulates the arcade board, ported from MAME. You supply the game's ROMs from your own copy of MAME's `timeplt` set.

## What goes on the card

1. Copy `TIMEPLT.CVP` into the `/PLUGINS` folder.
2. Make a folder named `/TIMEPLT` and put MAME's `timeplt.zip` in it as it is, or the 11 files from it loose. You can also put the zip in the card's top folder as `/TIMEPLT.ZIP`.
3. Start Time Pilot from the menu.

The plugin knows each ROM by its size and contents, so the file names don't matter. If a ROM is missing, the screen names it. Only MAME's `timeplt` set works. The clones have different ROMs.

## Controls

On the ColecoVision controller:

- The joystick steers the plane in eight directions.
- Either fire button fires.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- RESET on the console goes back to the menu.

On a USB pad, through a powered hub:

- The D-pad or the left stick steers.
- Any face button fires.
- Minus adds a coin, and plus starts a one-player game.

## Picture and sound

The arcade screen is taller than the TV's, so the sky is scaled down to fit in the middle. The scores, lives, stage and year go in the margins on both sides at the arcade's size, so every letter stays readable.

With the nearest ColecoVision colors, three of the five eras would get a black or dark blue sky. So each era has its own color: dark blue for 1910, light blue for 1940, dark green for 1970, magenta for 1982 and black for 2001.

The board's two sound chips have six voices between them. They're mixed as in MAME and played as samples on the ColecoVision's sound chip, at about the loudness of an ordinary cartridge.

High scores last until you go back to the menu.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Nicola Salmoria, for the Time Pilot driver and its sound board.
- Juergen Buchmueller, for the Z80 CPU, and Andrei I. Holub for its instruction list.
- Couriersud, for the AY-3-8910 sound chip.
- Derrick Renaud and Couriersud, for the sound board's filters.
- Aaron Giles and O. Galibert, for MAME's scheduler, screen timing and sound streams.
- Miodrag Milanovic, AJR and Manuel Abadia, for the sound latch, the LS259 latch and Konami's input ports.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin is by Parrot Cart, and it's free. Its source is here: https://github.com/plasticbugs/parrot-plugin-timeplt

Time Pilot is a trademark of Konami. Parrot Cart isn't connected with them. This plugin doesn't include any game ROMs.

## Build it yourself

You need the Raspberry Pi Pico SDK 2.1.1, an Arm GNU toolchain, CMake,
Ninja, Python 3, sjasmplus and picotool. Releases are built with the Arm
GNU Toolchain 9-2019-q4 (GCC 9.2.1) and picotool 2.3.1. With those you get
the same file as the release, byte for byte.

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
ninja -C build
```

This makes `build/TIMEPLT.CVP`. On Ubuntu, `tools/ci/setup.sh` installs the
same tools GitHub uses to build each release.

`CART-README.md` is the text the cart shows on the plugin's page.
`test/tp/README.md` explains how the picture and sound are made, and the
tests against MAME. The rest of this repository is the Parrot Cart starter
kit, and `docs/` is its manual.

## License

The plugin is under the BSD 3-Clause license (`LICENSE`). `THIRD-PARTY.md`
lists the parts that came from elsewhere, and their licenses.
