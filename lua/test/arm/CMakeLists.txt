# The cart's builds that lua/test/onarm.sh runs on an emulated Cortex-M33:
# test scripts as Lua apps (which also checks parrotcart_lua()'s packaging),
# with the heap in the chip's RAM first, with all of it in the PSRAM, and
# with a hook that counts instructions.
cmake_minimum_required(VERSION 3.13)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/parrotcart.cmake)
project(parrotcart_lua_tests C CXX ASM)
parrotcart_init()

set(SCRIPTS ${CMAKE_CURRENT_LIST_DIR}/../scripts)

function(lua_test NAME SCRIPT)
    add_executable(${NAME})
    cvcart_features(${NAME} PSRAM CARD PADS KEYBOARD MOUSE MIDI_IN)
    parrotcart_lua(${NAME} MAIN ${SCRIPTS}/${SCRIPT} ${ARGN})
    cvcart_plugin(${NAME} ID parrotcart.lua-test NAME "Lua Test" VERSION 0.1.0)
endfunction()

lua_test(bench bench.lua)
lua_test(bench_count bench.lua)
target_compile_definitions(bench_count PRIVATE CVLUA_COUNT_HOOK=1)    # a count hook, as on a computer
lua_test(bench_psram bench.lua)
target_compile_definitions(bench_psram PRIVATE CVLUA_PSRAM_MIN=0)     # every block in the PSRAM
lua_test(deep deep.lua)
lua_test(api api.lua FILES ${SCRIPTS}/helper.lua ${SCRIPTS}/data.txt)
