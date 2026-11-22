# The hardware

The SDK handles the wiring between the RP2350 and the console, so you
don't need to know it to write a plugin. The pin map, which GPIO carries
each of the console's address and data lines, is in `sdk/hw3/pins.py`,
and the SDK builds from it.

## At a glance

| | |
|---|---|
| Chip | Raspberry Pi RP2350B, the chip in the Pico 2, with two Arm Cortex-M33 cores at 150 MHz and floating point and DSP instructions |
| RAM | 520 KB on the chip. A plugin can use everything below `0x2007EC00`, about 507 KB, for its code, data and heap |
| PSRAM | 8 MB (APS6404L), QPI at 75 MHz, mapped into the address space through the chip's cache |
| Flash | 16 MB (W25Q128). A plugin can be up to 2 MB, less 4 KB. The cart copies each plugin into a 1 MB slot, or two side by side for a plugin over 1 MB, and starts it from there |
| microSD | SPI at 25 MHz, reading about 1.1 MB/s. FAT16, FAT32 and exFAT cards work |
| USB-C | Full speed (12 Mbit/s). As a host, it takes game pads, keyboards, mice and MIDI keyboards through a powered hub. As a device, it plugs into a computer, for example to send MIDI |
| LED | One red LED |
| The console | The ColecoVision's Z80A at 3.58 MHz, its TMS9918A video chip with 16 KB of video RAM, its SN76489 sound chip, and its controllers |

## The two cores

The RP2350 has two cores. The SDK uses core 1 to talk to the
ColecoVision. It starts core 1 for you, and your plugin shouldn't use
it, because the console needs a quick reply every time it reads the
cartridge. Its bus loop is timed for the 150 MHz clock, so leave the
clock as it is. A 200 MHz build crashed the console.

Your plugin runs on core 0. Call the SDK's task function (`cvk_task()`
or `cvf_task()`) regularly in your main loop. It checks whether the
console is still on, and it takes you back to the menu when you press
RESET.

## Memory

- A plugin runs from SRAM. The cart copies it in from flash when it
  starts, so the plugin's code, data and heap share the 507 KB. The
  stacks are in the chip's two 4 KB scratch banks. To use `malloc`, add
  `HEAP` to `cvcart_plugin()`. Every KB of code is a KB less for the
  heap. For example, USB host support takes 26 KB, and newlib's `printf`
  takes 19 KB.
- The PSRAM has 8 MB for big things, like a game's ROM, buffers and
  pictures. Reads go through the cache, and there's an uncached alias for
  filling it fast (`cvpsram.h`). The PSRAM is slower than SRAM, so keep
  tight loops and their data in SRAM.
- Each plugin also has a 1 MB slot in flash, or two for a plugin over 1 MB. A big plugin can run
  straight from flash instead of SRAM, as the SCUMM plugin does.

## The console, as a plugin sees it

The cart doesn't replace the ColecoVision's processor. The console's Z80
runs a small program from the cart, called a kernel, and that program
writes the picture and the sound. The SDK has two kernels:

- **The screen kernel** (`cvkernel` and `cvscreen`). You draw into a
  256x192 canvas on the cart, and the SDK sends the changes to the
  console. It suits menus, text, tools and music, as in PSG Synth, NES
  Info and the pad test.
- **The frame kernel** (`cvframes`). Each TV frame, you give the console
  one command list of video RAM writes and sound bytes. It suits
  emulators and anything that moves, as in the NES, Game Boy and Master
  System plugins. A list can carry about 2 KB of video RAM writes a
  frame, about 650 bytes in the vertical blank and about 1,350 more while
  the picture is drawn. The costs are in the header of `gbkernel.asm`.
  Core 1 serves the lists with a loop that acts on an address only when
  two samples of the console's pins agree, because a single sample is
  now and then wrong on a real console. `parrotcart_frames()` builds it
  in.

On the console side, you have:

- The TMS9918A video chip, with 256x192 pixels, 15 colors plus
  transparent, and 16 KB of video RAM. In Graphics II mode, each 8-pixel
  row of a tile has two colors. There are 32 sprites, and up to 4 can
  share a line.
- The SN76489 sound chip, with three square-wave tones and one noise
  channel, each with 16 volume steps. On a real console, every sound
  write stalls the Z80 for about 29 cycles. The kernels and their cost
  tables allow for that.
- The controllers, each with a joystick, two fire buttons and a 12-key
  keypad. The kernels read them on the console and send their state to
  the cart (`cvf_input()` in the frame kernel).
- The TV timing, which is 60 frames a second (NTSC), with 262 lines of
  228 Z80 cycles each.

## USB

- As a host, the cart reads up to two game pads, keyboards, mice and MIDI
  devices with `cvusb` (`docs/features.md`). The cart can't power USB
  devices, so plug them into a powered hub. The SDK finds what's on the
  hub when the plugin starts, and what's plugged in or pulled out while
  it runs. Some hubs never report a change, so it reads every hub's
  ports every 100 ms.
- As a device, the cart plugs into a computer, and your plugin can act
  as any kind of USB device. The SCUMM plugin is a USB MIDI device, so
  its music plays on the computer's synth.

## What your plugin can use

While it runs, your plugin can use all of SRAM below `0x2007EC00`, the
PSRAM, the USB port and the console. Please keep its flash writes to the
plugin's log area, so the menu and the other plugins stay safe. On the
card, write to files only in place (`fs_write_sec`). Pressing RESET or
turning the console off takes you back to the menu, and so does a reboot.

## What the cart doesn't have

The cart has no Wi-Fi or Bluetooth, and no clock that keeps time while
it's off. It has no sound output either, so all sound goes through the
console's sound chip.
