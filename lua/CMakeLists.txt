# The Lua plugin: it runs .lua scripts from the card (docs/lua.md).
#
#   cmake -S lua -B build-lua -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build-lua                 -> build-lua/LUA.CVP
#
# To make a Lua app a plugin of its own, with its scripts built in, see
# "Your script as its own plugin" in docs/lua.md.
cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/parrotcart.cmake)
project(parrotcart_lua C CXX ASM)
parrotcart_init()

add_executable(lua)
cvcart_features(lua PSRAM CARD PADS KEYBOARD MOUSE MIDI_IN)
parrotcart_lua(lua)
cvcart_plugin(lua ID parrotcart.lua NAME "Lua" VERSION 0.1.3 BY "Parrot Cart"
              HOME /LUA OPENS .lua USES standalone ICON icon.txt FILE_ICON file_icon.txt
              README README.md FILE LUA.CVP)
