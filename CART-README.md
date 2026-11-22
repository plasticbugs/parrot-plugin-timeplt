# Time Pilot

This plugin plays Konami's 1982 Time Pilot arcade game on the ColecoVision. It needs your own copy of the game's ROMs.

## Setup

1. Copy `TIMEPLT.CVP` into the `/PLUGINS` folder on the card.
2. Put the ROMs from MAME's `timeplt` set in a folder named `/TIMEPLT`. You can copy the 11 files loose, or copy `timeplt.zip` as it is. You can also put the zip in the card's top folder as `/TIMEPLT.ZIP`.
3. Start Time Pilot from the Plugins page in the menu.

The files can have any names, because the plugin knows each ROM by its size and contents. If a ROM is missing, the screen names it. Only MAME's `timeplt` set works. The clones have different ROMs.

## Controls

On the ColecoVision controller:

- The joystick steers the plane in eight directions.
- Either fire button fires.
- Keypad * or # adds a coin.
- Keypad 1 starts a one-player game, and keypad 2 a two-player game.
- Press RESET on the console to go back to the menu.

On a USB pad, through a powered hub:

- The D-pad or the left stick steers.
- Any face button fires.
- Minus adds a coin, and plus starts a one-player game.

## The picture and the sound

The arcade screen is taller than the TV's, so the sky is scaled to fit in the middle of the screen. The scores, the lives, the stage and the year go in the margins on both sides, at the arcade's size. Each era's sky is a different colour.

The sound is the arcade board's two sound chips, mixed and played as samples on the ColecoVision's sound chip.

## Credits

Parts of this plugin are ports of MAME's code, or were checked against it. That code is under the BSD-3-Clause license, and its authors are:

- Nicola Salmoria, for the Time Pilot driver and its sound board.
- Juergen Buchmueller, for the Z80 CPU, and Andrei I. Holub for its instruction list.
- Couriersud, for the AY-3-8910 sound chip.
- Derrick Renaud and Couriersud, for the sound board's filters.
- Aaron Giles and O. Galibert, for MAME's scheduler, screen timing and sound streams.
- Miodrag Milanovic, AJR and Manuel Abadia, for the sound latch, the LS259 latch and Konami's input ports.

You can find MAME at https://www.mamedev.org. The zip reader follows Mark Adler's puff, from zlib. USB support comes from TinyUSB by Ha Thach, under the MIT license. The plugin is built with the Raspberry Pi Pico SDK (BSD-3-Clause) and the Parrot Cart SDK (MIT).

The plugin itself is by Parrot Cart, and it's free. Its source is linked next to its download.

Time Pilot is a trademark of Konami. This plugin contains none of the game.
