# Screenshots

The plugins site shows up to three screenshots of each plugin, inside a
picture of a TV.

- Each one is a PNG of exactly 256x192 pixels, showing only the console's
  picture. Leave out the border, the TV frame and any scaling, because
  the site draws the frame and scales the picture itself.
- Each can be up to 100 KB, though a real one is usually 1 to 10 KB.
- You can attach up to 3 to the GitHub release, as `screenshot-1.png`,
  `screenshot-2.png` and `screenshot-3.png`. The first one is the one
  shown in lists.

Keep them in the repository's `screenshots/` folder under those names.
The release workflow checks them and attaches whatever it finds there.

```sh
python3 tools/png256.py check screenshots/*.png     # size and weight
```

## Taking them

On your computer, `tools/screenshot.sh` builds your plugin's code against
the kit's model of the console (`docs/testing.md`), plays it a script of
inputs, and saves the last picture at 256x192:

```sh
tools/screenshot.sh screenshots/screenshot-1.png                      # src/, after 2 seconds
tools/screenshot.sh screenshots/screenshot-2.png src 4000 "500:key=3 1200:joy=up+fire"
```

The script is a list of `MS:WHAT=VALUE` steps, and `test/host/host.c`
lists them all. A step can press the stick, the fire buttons or the
keypad keys, or use a USB pad and its sticks, a USB keyboard or a mouse.
With `PC_CARD=image PC_OPEN=/path`, the plugin starts with a file from a
disk image, the same way the menu would start it.

This works for a plugin whose `main.c` builds with just the SDK's
libraries, like the examples. For a plugin with a big core, such as an
emulator, write a small host program that calls your drawing code, which
is what we do for our plugins.

To use the real kernel in MAME, run `MAME=1 tools/screenshot.sh ...`.
It sends the lists your plugin made to the SDK's Z80 kernel running on
MAME's ColecoVision, and crops MAME's snapshot. The picture comes out the
same, so use this when you want to double-check. It needs `mame` and the
ColecoVision BIOS (`MAME_ROMPATH=dir`, a folder with `coleco.zip`).

You can also crop a snapshot from any emulator. A MAME snapshot of the
ColecoVision is 280x216, which is the picture plus a 12-pixel border.

```sh
python3 tools/png256.py crop snapshot.png screenshots/screenshot-1.png
```

`png256.py crop` takes the middle 256x192 of a bigger PNG.

A capture card's picture of a real console is scaled and soft. It's fine
for showing off, but the plugins site needs the exact pixels.

## Running MAME headless

Every MAME run in this kit uses the command in `tools/mame-replay.sh`:

```sh
SDL_VIDEODRIVER=dummy mame coleco -video none -videodriver dummy -window -nomaximize \
    -sound none -nothrottle -skip_gameinfo -seconds_to_run N \
    -snapshot_directory DIR -snapname NAME -autoboot_script script.lua ...
```

It opens no window, never goes full screen, and has a time limit so it
can't hang. Pictures come out as snapshots (`-snapname`, or
`manager.machine.video:snapshot()` in Lua). If you script MAME yourself
for a plugin, please use the same flags. A full-screen MAME takes over
the display of whoever is at the computer, every time a test runs.
