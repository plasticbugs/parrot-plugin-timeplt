# Time Pilot (TIMEPLT.CVP): Konami's 1982 arcade game on the ColecoVision.
# The cart runs the arcade board (two Z80s and two AY-3-8910s: src/tp.c,
# src/tpay.c, from MAME 0.288) from your own ROMs on the card (/TIMEPLT),
# and src/tpcv.c sends the picture and the sound to the console as the
# frame kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/TIMEPLT.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Started from tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(timeplt C CXX ASM)
parrotcart_init()

# The same source makes the same file, whenever it's built: __DATE__ and
# __TIME__ come from SOURCE_DATE_EPOCH, fixed here (2026-11-21).
set(BUILD_EPOCH 1795219200)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(EMU src/tp.c src/tpz80.c src/tpay.c src/tpsnd.c src/tprun.c)
set(CARD src/tpcard.c src/tproms.c src/swzip.c)
add_executable(timeplt src/main.c src/tpcv.c ${EMU} ${CARD} ${PARROTCART_SDK}/midihello/vdp.c)
parrotcart_frames(timeplt)              # a whole picture every TV frame: the frame kernel
target_include_directories(timeplt PRIVATE src ${PARROTCART_SDK}/sdplayer)
# The emulation -O3, its state not split into a section a variable (every
# use would need its own address loaded); the picture (tpcv.c) -O2 for
# room; the card's code runs once, at the start: built for size.
# test/tp/armcount.sh builds each file the same way.
set_source_files_properties(src/main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
set_source_files_properties(${EMU} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O3;-fno-data-sections")
set_source_files_properties(src/tpcv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O2")
set_source_files_properties(${CARD} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-Os")
# no tracing hook in the cart's Z80s; core 0's stack 4K (scratch Y),
# core 1's 2K (scratch X also holds its bus loop)
target_compile_definitions(timeplt PRIVATE Z80_NO_TRACE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(timeplt PADS CARD)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(timeplt ID parrotcart.timeplt NAME "Time Pilot" VERSION 0.1.2 BY "Parrot Cart"
              ICON src/icon.txt README CART-README.md FILE TIMEPLT.CVP
              HOME /TIMEPLT USES usb-host)
