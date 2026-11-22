# Time Pilot: how it works and how it's tested

The cart runs the arcade board the way MAME 0.288 emulates it: the main Z80, the sound Z80, the two AY-3-8910s and the filters on their outputs. It also follows MAME's scheduler, so the two CPUs take turns exactly as they do in MAME. The tests in this folder run the same game in MAME and in this code and compare RAM, both CPUs' registers and the picture at every frame, through the attract mode and a game into the second era, and they match at every frame.

## The picture

MAME shows Time Pilot turned on its side, 224 pixels wide and 256 high. The play area is 224 by 216 of that. It's scaled to 160 by 192 and drawn in the middle of the TV, from x 48 to 207. Each group of 9 lines becomes 8 and each group of 7 columns becomes 5, by merging neighbours, so a line one pixel wide still shows. The sprites are scaled one at a time, from 16 by 16 to 12 by 14, so they keep their shape as they move.

The scores, the lives, the stage flags, the year and the progress bar sit above and below the play area in the arcade. Here they go in the 48-pixel margins at the arcade's size, so every letter keeps its shape. Player 1's score, the high score, the lives and the credits are on the left. Player 2's score, the flags, the year and the progress bar are on the right, with the bar stood on end.

A score of a million or more has 7 digits, 8 pixels too wide for a margin, so each digit loses a column. Of the columns it could lose, the one used keeps every stroke and gap of all ten digits.

With the nearest ColecoVision colours, three of the arcade's five skies would all be black or dark blue, so each era gets a different colour:

| Era | Sky |
|---|---|
| 1910 | dark blue |
| 1940 | light blue |
| 1970 | dark green |
| 1982 | magenta |
| 2001 | black |

A plane or a cloud that would come out the same colour as the sky takes its next nearest colour.

The console gets one list of changes for each TV frame. In a game that's about 59 new pictures a second, out of the arcade's 60. At 150 MHz a list takes the cart 12.8 ms of its 16.8 ms on average, and 15.2 ms at worst. No list was late in the tests.

## The sound

The board has two AY-3-8910s, six square-wave voices in all, each through an RC filter the game switches. The ColecoVision has one SN76489 with three voices. Three ways of fitting six voices into three were tried. Each was played through the SDK's frame kernel on MAME's ColecoVision and compared with the arcade game in MAME (`test/tp/soundcmp.sh`):

| Way | Spectral distance from MAME | Loudness (rms) |
|---|---|---|
| Samples: all six voices mixed and filtered as MAME does, 5,962 a second, played through the three voices as a DAC | 5.6 dB | -24.5 dBFS |
| Tones: the three loudest voices as the SN76489's | 10.4 dB | -16.9 dBFS |
| A mix: the loudest voice as a tone, the rest as samples | 9.5 dB | -23.1 dBFS |

The plugin uses the samples. They keep all six voices and the filters, which shape the game's explosions, and they measure closest to MAME. Tones drop three of the six voices.

Ordinary ColecoVision cartridges measured the same way give -20.5 to -23.9 dBFS. The samples are set to -24.5, a little under them. Peaks are rounded off with a soft knee from 3/4 of full scale, and about 11% of the samples in a game reach it.

Each list also writes the whole sound chip, so a byte the console misreads lasts only one list.

## The tests

- `test/run.sh` and `sim/test.sh` run the plugin on the kit's model of the console and in the simulator. With `TP_ROMS=path/to/timeplt.zip` they play a game from a card with the ROMs on it. Without it they check only the missing-ROM message.
- `test/tp/compare.sh` and `test/tp/trace.sh` check the board against MAME, frame by frame and instruction by instruction.
- `test/tp/hudcheck.sh` draws every piece of the margins from the attract mode and from games, and checks that no stroke or gap is lost on the way to the console.
- `test/tp/card.sh` loads the ROMs from card images: zipped, loose, from `/TIMEPLT.ZIP`, with one missing and with none.
- `test/tp/soundcmp.sh` compares the sound with MAME's and measures how loud it is.
- `test/tp/armcount.sh` times the plugin's code on an Arm model at 150 MHz.

They need MAME's `timeplt.zip` (`TP_ROMS`), and for the console's side the ColecoVision BIOS in `MAME_ROMPATH`. MAME runs headless. Neither file is in this repository.

The kit's console model expects every list to end 1,200 cycles before its TV frame. This plugin's lists run back to back, 60,036 cycles each, because their length is the sample clock. The kit's porting notes describe this for sample sound. The plugin's entries in `test/run.sh` and `sim/test.sh` allow those lines and nothing else, and MAME runs every list.
