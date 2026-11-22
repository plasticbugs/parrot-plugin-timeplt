# parrotcart.cmake -- what every Parrot Cart plugin's CMakeLists.txt starts with.
#
#   cmake_minimum_required(VERSION 3.13)
#   include(cmake/parrotcart.cmake)       # the board, the pico-sdk, TinyUSB
#   project(myplugin C CXX ASM)
#   parrotcart_init()                     # pico_sdk_init() and the cart's SDK
#
#   add_executable(myplugin src/main.c)
#   parrotcart_screen(myplugin)           # text and pictures: cvkernel + cvscreen
#                                         # (or parrotcart_frames: a list every TV frame)
#   cvcart_plugin(myplugin ID you.myplugin NAME "My Plugin" VERSION 0.1.0 ...)
#
# The cart's SDK is the copy in sdk/ (tools/update-sdk.sh refreshes it); its
# own CMake functions are in sdk/firmware/revc/sdk/cvplugin.cmake:
# cvkernel_use, cvframes_use, cvcart_features (the USB features, the PSRAM
# and the card: docs/features.md), cvcart_plugin; and by hand cvsd_use,
# cvpsram_use, cvpad_use.
#
# The pico-sdk: -DPICO_SDK_PATH=dir, or the environment's PICO_SDK_PATH, or
# -DPICO_SDK_FETCH_FROM_GIT=ON (version 2.1.1).
get_filename_component(PARROTCART_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(PARROTCART_SDK ${PARROTCART_DIR}/sdk/firmware/revc)

# the cart: an RP2350B with 16 MB of flash and 8 MB of PSRAM
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if (NOT PICO_SDK_FETCH_FROM_GIT_TAG)
    set(PICO_SDK_FETCH_FROM_GIT_TAG 2.1.1)
endif()

# USB from the SDK's TinyUSB (0.21 and fixes), not the pico-sdk's 0.18, whose
# host stops the plugin for half a second whenever something is plugged in
# (sdk/firmware/revc/sdk/tinyusb/VENDORED.md). Before pico_sdk_import.
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

macro(parrotcart_init)
    pico_sdk_init()
    if (NOT PICO_SDK_VERSION_STRING VERSION_EQUAL "2.1.1")
        message(WARNING "The cart's SDK is built and tested with pico-sdk 2.1.1; this is ${PICO_SDK_VERSION_STRING}")
    endif()
    include(${PARROTCART_SDK}/sdk/cvplugin.cmake)
endmacro()

# A plugin that draws its screen on the cart (text, shapes, 256x192 in 15
# colours): the console runs the SDK's screen kernel, which also plays 4
# PSG voices and reports the wired controller. Most plugins want this.
function(parrotcart_screen TARGET)
    cvkernel_use(${TARGET})
    target_include_directories(${TARGET} PRIVATE ${PARROTCART_DIR}/lib ${PARROTCART_SDK}/midihello)
    _parrotcart_common(${TARGET})
endfunction()

# A plugin that makes a whole picture every TV frame (an emulator, a video
# player): the console runs the SDK's frame kernel, one command list a frame.
# Core 1 serves the lists with the two-sample bus loop (cvframes_serve2,
# sdk/.../cvfserve.S), which acts on an address only once two samples of the
# pins agree. That cured The Empire Strikes Back's glitches on a console,
# and all the cart's frame plugins are built with it. cvscale.c (the 3/4
# scaler, cvscale.h) is built in too. The linker leaves it out if nothing
# calls it.
function(parrotcart_frames TARGET)
    cvframes_use(${TARGET})
    cvframes_serve2(${TARGET})
    target_sources(${TARGET} PRIVATE ${PARROTCART_SDK}/sdk/cvscale.c)
    target_include_directories(${TARGET} PRIVATE ${PARROTCART_DIR}/lib ${PARROTCART_SDK}/midihello)
    _parrotcart_common(${TARGET})
endfunction()

# A plugin written in Lua (docs/lua.md): the Lua runtime in lua/, with the
# app's scripts built in.
#
#   add_executable(mygame)
#   cvcart_features(mygame PSRAM CARD PADS)     # PSRAM and CARD always; the USB ones you use
#   parrotcart_lua(mygame MAIN src/main.lua [FILES src/enemy.lua src/level1.txt ...] [HOME /MYGAME])
#   cvcart_plugin(mygame ID you.mygame NAME "My Game" VERSION 0.1.0 HOME /MYGAME ...)
#
# MAIN is the script the plugin starts. FILES are the others: more scripts
# for require, and anything file.read() should find. Each is known by its
# path from MAIN's folder (src/enemy.lua is "enemy.lua"). HOME is the
# folder on the card where the app's saves go, the same one cvcart_plugin()
# is given.
#
# Without MAIN it's the Lua plugin itself (lua/CMakeLists.txt), which runs
# scripts from the card.
function(parrotcart_lua TARGET)
    cmake_parse_arguments(P "" "MAIN;HOME" "FILES" ${ARGN})
    get_target_property(features ${TARGET} CVCART_FEATURES)
    if (NOT features MATCHES "PSRAM" OR NOT features MATCHES "CARD")
        message(FATAL_ERROR "parrotcart_lua(${TARGET}): call cvcart_features(${TARGET} PSRAM CARD ...) first. "
            "Lua keeps big values in the PSRAM and reads the card")
    endif()
    set(LUA ${PARROTCART_DIR}/lua)
    target_sources(${TARGET} PRIVATE ${LUA}/main.c ${LUA}/api.c ${LUA}/card.c ${LUA}/heap.c ${LUA}/port.c
                   ${LUA}/sdk_cvfont.c ${LUA}/sdk_cvedit.c
                   ${LUA}/lua_vm.c ${LUA}/lua_libs.c)
    # A plugin's code is in the same RAM as Lua's values, so only the part
    # that runs a script is built for speed, and the rest for size.
    set_source_files_properties(${LUA}/lua_vm.c ${LUA}/heap.c PROPERTIES COMPILE_OPTIONS "-O2")
    set_source_files_properties(${LUA}/lua_libs.c ${LUA}/main.c ${LUA}/api.c ${LUA}/card.c ${LUA}/port.c
                                ${LUA}/sdk_cvfont.c ${LUA}/sdk_cvedit.c
                                PROPERTIES COMPILE_OPTIONS "-Os")
    parrotcart_screen(${TARGET})
    target_include_directories(${TARGET} PRIVATE ${LUA})
    target_link_libraries(${TARGET} pico_rand)
    if (P_HOME)
        target_compile_definitions(${TARGET} PRIVATE "CVLUA_HOME=\"${P_HOME}\"")
    endif()
    if (P_MAIN)
        get_filename_component(main ${P_MAIN} ABSOLUTE)
        get_filename_component(base ${main} DIRECTORY)
        set(files ${main})
        foreach (f ${P_FILES})
            get_filename_component(f ${f} ABSOLUTE)
            list(APPEND files ${f})
        endforeach()
        set(header ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_lua_app.h)
        add_custom_command(
            OUTPUT ${header}
            COMMAND python3 ${PARROTCART_DIR}/tools/lua-embed.py ${header} ${base} ${files}
            DEPENDS ${files} ${PARROTCART_DIR}/tools/lua-embed.py
            VERBATIM)
        target_sources(${TARGET} PRIVATE ${header})
        target_compile_definitions(${TARGET} PRIVATE "CVLUA_APP_HEADER=\"${header}\"")
    elseif (P_FILES)
        message(FATAL_ERROR "parrotcart_lua(${TARGET}): FILES without MAIN, the script the plugin starts")
    endif()
endfunction()

function(_parrotcart_common TARGET)
    # no serial console: the USB port is the plugin's to use, and printf
    # would bring in 19K of newlib that a plugin in SRAM can't spare
    pico_enable_stdio_usb(${TARGET} 0)
    pico_enable_stdio_uart(${TARGET} 0)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra -Wno-stringop-truncation)
endfunction()
