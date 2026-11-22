# CLAUDE.md

This folder is a Parrot Cart plugin: a program for the RP2350 chip in
Parrot Cart, the ColecoVision SD cartridge, built into one `.CVP` file.
It started as a copy of the starter kit,
<https://github.com/plasticbugs/parrot-plugin-template>.

Use the `parrot-cart-plugin` skill in `.claude/skills/` for writing,
testing and releasing the plugin. It explains how a plugin works, the
rules the cart depends on, and how to check your work in the simulator.

## This plugin

Time Pilot: `build/TIMEPLT.CVP`, ID `parrotcart.timeplt`, CMake target `timeplt`,
home folder `/TIMEPLT`. It runs Konami's 1982 Time Pilot arcade board (two Z80s and two AY-3-8910s), ported from MAME 0.288,
from the player's own copy of MAME's `timeplt` set. The code is in `src/`,
and `test/tp/` checks it against MAME (its README says how; the
tests need the set in `TP_ROMS`). License: BSD 3-Clause (LICENSE,
THIRD-PARTY.md). `CART-README.md` is packed into the .CVP, so changing it
changes the file. A release is a tag `vX.Y.Z` that matches `VERSION` in
CMakeLists.txt; `BUILD_EPOCH` keeps the build byte for byte the same.
Never add the game's ROMs or anything made from them (pictures, sounds,
dumps) to this repository or its screenshots.

## Commands

```sh
cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk && ninja -C build   # build/NAME.CVP
sim/run.sh src --headless --strict --ms 3000 --shot /tmp/shot.png          # then look at the PNG
sim/run.sh src                       # play it in a window (for the user)
sim/lua.sh src --run                 # a Lua plugin
test/run.sh                          # models + MAME (NO_MAME=1 without MAME)
python3 tools/wavlevel.py cart.wav plugin.wav   # how loud, next to a cartridge (docs/testing.md)
python3 .claude/skills/parrot-cart-plugin/scripts/check-release.py         # before a release
```

## Layout

- `CMakeLists.txt`: the `cvcart_plugin()` line holds the ID, name,
  version, icon and README, and `cvcart_features()` the USB features.
- `src/`: the plugin.
- `sdk/`: a copy of the cart's SDK. Don't edit it, because
  `tools/update-sdk.sh` replaces it. Its `firmware/revc/sdk/README.md` and
  headers are the API reference.
- `docs/`: the manual. `examples/`: one small plugin for each thing a
  plugin can do (left out of a plugin made by `tools/new-plugin.py`
  unless it was given `--examples`).
- `sim/`, `test/`: the simulator and the models the tests run against.
  `lua/`: the Lua runtime.

## Don't

- Don't edit `sdk/`, and don't touch core 1 or the cart's flash from the
  plugin. Leave the clock at 150 MHz, which core 1's bus loop is timed for.
- Never commit games, ROMs, BIOS images, saves or built `.CVP` files.
- Don't tag, push, make releases or open issues for the user unless they
  ask. Get everything ready and give them the commands.
- Run MAME headless only, the way `tools/mame-replay.sh` does.
