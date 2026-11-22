# Testing

## On your computer

```sh
test/run.sh                    # the template's plugin and every example
test/run.sh hello sound        # some of them
NO_MAME=1 test/run.sh          # without the MAME step
```

`test/run.sh` compiles each plugin's `main.c` for your computer and runs
it against models of everything on the cart (`test/host/host.c`):

| The model | What it does | What it catches |
|---|---|---|
| the screen kernel | runs each list a frame after it's sent, sends HELLO and ACK reports, and reports the controller from a script | a malformed list, a list sent too early, three colors in an 8-pixel run, and a screen where the SDK and the console don't match |
| the frame kernel | runs one list each TV frame and adds up its time from the kernel's costs | a list that runs past its frame or its vertical blank, and bad commands |
| the card | a disk image, read through the SDK's file system (FAT16, FAT32, exFAT) | wrong reads and saves that get lost. Afterward, macOS's `fsck` checks the image |
| the menu | a launch block for a file on the image | a plugin that can't open its file |
| USB | a pad, keyboard, mouse or listening computer, from the script | how the plugin handles them (USB itself isn't tested) |
| the PSRAM | 8 MB of ordinary memory | the plugin's arithmetic |

Then the test plays the plugin's lists to the SDK's real Z80 kernel on
MAME's ColecoVision (`tools/mame-replay.sh`, headless). MAME's picture
has to match the model's pixel for pixel, and for the frame kernel, no
list can make a frame start late.

That step needs `mame`, `sjasmplus` and the ColecoVision BIOS. MAME
calls the BIOS `coleco.zip`, and the kit doesn't include it, so bring
your own. Put it in a folder outside the plugin's repository and name
that folder in `MAME_ROMPATH`:

```sh
MAME_ROMPATH=$HOME/mame/roms test/run.sh
```

You can also put it where MAME already looks: the `rompath` in its
`mame.ini`, or, with no `mame.ini`, a `roms` folder in the folder you run
the test from. If MAME can't find it, `test/run.sh` says so and skips the
MAME step, as `NO_MAME=1` does.

To test your plugin this way, add a block for it to `test/run.sh` by
copying the `hello` one. A plugin made by `tools/new-plugin.py` already
has a block. You can also run it by hand. The script's events are listed
at the top of `test/host/host.c`, and the USB ones are in
docs/features.md:

```sh
test/host/build.sh src /tmp/myplugin
PC_OUT=/tmp PC_END=3000 PC_SCRIPT="500:joy=up 900:key=5" /tmp/myplugin
open /tmp/screen.png; cat /tmp/log.txt
```

The card tests need macOS (`hdiutil`), and the rest also run on Linux.

To play your plugin instead of scripting it, use the simulator. It runs
the same models in a window, with sound, the keyboard, mouse and game
pads, your computer's MIDI ports, and a folder as the SD card
(docs/simulator.md). Its headless mode takes the same scripts, plus a
few extra words for the keyboard, mouse and pads, and `sim/test.sh` runs
every example that way.

## How loud

A plugin should sound about as loud as an ordinary ColecoVision
cartridge with the TV at the same volume. The cart's first arcade
plugins were far too quiet next to one. Compare the two in MAME,
recorded the same way:

```sh
# a cartridge you own, 60 seconds of its title and attract mode
SDL_VIDEODRIVER=dummy mame coleco -rompath "$MAME_ROMPATH" -cart cart.col \
    -video none -videodriver dummy -window -nomaximize -sound none \
    -nothrottle -skip_gameinfo -seconds_to_run 60 -wavwrite cart.wav
# your plugin's lists from its test/run.sh run, on the same machine
MAME_WAV=plugin.wav tools/mame-replay.sh "${TMPDIR:-/tmp}/parrotcart-test/myplugin/lists.bin" /tmp/replay 60
python3 tools/wavlevel.py cart.wav plugin.wav
```

`-sound none` keeps MAME quiet on the computer, and `-wavwrite` still
records. `wavlevel.py` gives each file's level in dBFS, leaving out its
silent seconds. Get the plugin's `rms` within a few dB of the
cartridge's. A `peak` at 0 means the sound clips. The lists are only as
long as the test's script, so give it a script that plays the game's
sound for a while. Then listen on a TV, which MAME can't stand in for.

Ordinary cartridges measured this way give an `rms` of -20.5 to -23.9
dBFS (Donkey Kong, Smurf, Lady Bug and Mr. Do!), about -22 on average.
Aim within 3 or 4 dB of that and not above it.

## What you can only test on a console

The models assume a console and devices that behave perfectly. These
things only get tested when your plugin runs on a ColecoVision with the
cart:

- Starting the plugin and going back to the menu, including the menu
  copying the plugin into flash, RESET, and switching the console off
  and on while the plugin runs.
- USB devices, since every one is different. Try hubs, how pads start
  up, and a computer's first "Allow this accessory?" prompt.
- A real SD card, with its start-up time and slow writes, and what
  happens when someone pulls it out.
- The PSRAM chip itself.
- How the sound really sounds, how loud it is next to a cartridge, and
  how the picture looks on a TV, where colors bleed and one-pixel lines
  shimmer.
- The bus. A frame plugin's note can say how many addresses the bus loop
  refused (`cvf_bus_counts()`). A few are normal, and many point at the
  cart's seating or the console.
- Memory, meaning the plugin's real heap and stack. The Mac's are
  bigger.
- Speed. The Mac runs your loop a thousand times faster, so a loop pass
  that takes 30 ms on the cart only shows up as a sluggish screen on the
  console.

## A test plan for the examples on a console

Copy the ten `.CVP` files to the card's `/PLUGINS` folder, and put a text
file or two anywhere on the card. Check these steps for every plugin
first:

| # | Do | Expect |
|---|---|---|
| A | Go to Home, then Plugins | Each plugin is listed with its icon in magenta, its name and its version |
| B | Press A on it | Its details (version, developer `Parrot Cart` or yours, ID, what it uses) and its README, 30 columns wide, with pages turning |
| C | Press A again | A progress bar the first time, then the plugin's screen within a second or two |
| D | Press RESET | The menu, on the Plugins page, with the plugin's note on the bottom line |
| E | Start it again. Switch the console off, wait 5 seconds, and switch it on | The menu, as after a cold start |

Then check each plugin:

| Plugin | Do | Expect |
|---|---|---|
| Hello (`HELLO.CVP`) | Use the stick, both fire buttons and each keypad key | Each one is named on the TV while you hold it, and `PRESSES` counts them. After RESET, the menu says `Hello: N presses` |
| Screen Example | Watch it, then press fire | The ball moves smoothly inside its black box, with no trail and no flicker in the parts that don't move. Fire changes its color. The 15 swatches are 15 different colors. The bricks are red on gray, and the coins are yellow |
| Sound Example | Press keypad 1 to 8, move the stick up and down, and press left fire | A C major scale, in tune, with each note stopping when you let go of the key. The stick moves the octave up and down. Left fire plays a drum hit that fades in a third of a second. There are no clicks or stuck notes, and it's silent after RESET |
| Game Pad Example | Start with nothing plugged in. Then plug in a powered hub with a pad, press everything, move both sticks to their corners, and pull the pad out and put it back | `NO PAD YET` at first, then `SWITCH PRO` or `HID PAD` within a few seconds. Each button lights its name, under the right name for where it is (the bottom face button is `SOUTH`). Each dot follows its stick, rests near the center (numbers near 0) and reaches about +-2047 at the edges, with up being positive. `DIRECTIONS` follows the d-pad and the left stick. Unplugging goes back to `NO PAD YET` without freezing, and plugging in works again |
| Keyboard Example | Plug a keyboard and a mouse into the powered hub. Type, press Enter and Backspace, move the mouse and click | `KEYBOARDS 1 MICE 1`. Letters appear as you type, with none lost or doubled. Enter moves the lines up. The pointer follows the mouse to all four edges of the blue field, and turns red (left button) or green (right button) |
| MIDI Out Example | Plug the cart's USB-C into a Mac and allow the accessory if asked. Open Audio MIDI Setup or GarageBand, and press keypad 1 to 8 | The TV says `A COMPUTER IS LISTENING`, and the Mac lists `Parrot Cart MIDI Example`. Each key plays a note that stops when you let go, and `SENT` counts two for each key. With nothing plugged in, the TV says `PLUG THE CART INTO A COMPUTER`, and the keypad still counts without hanging |
| SD Card Example | Run it, press RESET, run it again, then look at the card on a computer | The top folder's first 8 entries with their sizes, the first file's first characters, and `RUN 1, SAVED AND READ BACK`, then `RUN 2`. The menu's note gives the run. On the computer, `/SDCARD.SAV` is 512 bytes and the card checks clean. Try a FAT32 card and an exFAT card. If it can't read the card, it says so in red |
| Text Viewer | In the card's folders, press A on a `.txt` file (it has the plugin's green icon). Move the stick down and up, hold left fire and move the stick, then press RESET | The file's name and text, wrapped at 30 columns. The stick scrolls by a line, and by a page with left fire held. Back in the menu, you see the same folder with the same file selected, and `Read NAME` on the bottom line. Started from the Plugins page instead, it says `OPEN A .TXT FILE FROM THE MENU`. A name with an accented letter still opens |
| Frames Example | Watch for a minute, and steer with the stick | Eight color bars appear within a quarter of a second. A round white ball moves smoothly, with no tearing, no stray blocks left behind and no stutter. There's a short beep at each bounce and silence between, with no drone, and the stick turns the ball |
| PSRAM Example | Run it | `ID 0D 5D ...`, the bar filling twice, then `ALL 8 MB READ BACK RIGHT` and two times in milliseconds. Write the times down, because they're the PSRAM's real speed |

If one fails, look first at the menu's note after RESET, and at the
handoff trace on the menu's USB serial (`t`).
