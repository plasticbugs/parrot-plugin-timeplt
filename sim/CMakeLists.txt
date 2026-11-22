# sim/CMakeLists.txt -- the Parrot Cart simulator, for one plugin.
#
#   cmake -S sim -B build-sim/NAME -DPLUGIN_DIR=/path/to/the/plugin/src
#   cmake --build build-sim/NAME --config Release
#   build-sim/NAME/parrotsim [options]            (parrotsim.exe on Windows)
#
# sim/run.sh (or sim\run.bat) does all three. PLUGIN_DIR is the folder with
# the plugin's own .c and .cpp files, as test/host/build.sh takes it; a
# file that talks to TinyUSB (#include "tusb.h") is left out, as there,
# since the models answer for it, unless it holds main(): then it is built
# against sim/include/tusb.h, a port with nothing plugged in. More, when a
# plugin needs them:
#
#   PLUGIN_SOURCES    more source files or patterns, relative to PLUGIN_DIR
#                     (a ;-list): a vendored library, say
#   PLUGIN_INCLUDES   more include folders
#   PLUGIN_DEFINES    more defines
#   SIM_SANITIZE      ASan and UBSan, for the plugin's own memory bugs (ON
#                     where the compiler has them: Clang and GCC, not on
#                     Windows)
#
# Needs SDL3 (3.2 or later): docs/simulator.md says how to install it, and
# sim/get-sdl.sh builds one into build-sim/sdl3, where this looks too. The
# MIDI ports are RtMidi's, built here from sim/third_party/rtmidi: CoreMIDI
# on macOS, WinMM on Windows, ALSA on Linux (libasound2-dev; without it, no
# ports: --midi-log still works).
cmake_minimum_required(VERSION 3.16)
project(parrotsim C CXX)

set(PLUGIN_DIR "" CACHE PATH "the plugin's own sources")
set(PLUGIN_SOURCES "" CACHE STRING "more sources or patterns, relative to PLUGIN_DIR")
set(PLUGIN_INCLUDES "" CACHE STRING "more include folders")
set(PLUGIN_DEFINES "" CACHE STRING "more defines")
if(MSVC OR MINGW)
  option(SIM_SANITIZE "ASan and UBSan" OFF)
else()
  option(SIM_SANITIZE "ASan and UBSan" ON)
endif()
if(NOT PLUGIN_DIR OR NOT IS_DIRECTORY "${PLUGIN_DIR}")
  message(FATAL_ERROR "PLUGIN_DIR: the folder with the plugin's .c files, please (-DPLUGIN_DIR=...)")
endif()
get_filename_component(PLUGIN_DIR "${PLUGIN_DIR}" ABSOLUTE)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(KIT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SDK "${KIT}/sdk/firmware/revc")

# ---- SDL3, zlib --------------------------------------------------------------------
if(EXISTS "${KIT}/build-sim/sdl3/cmake/SDL3Config.cmake")       # get-sdl.sh, Windows
  set(SDL3_DIR "${KIT}/build-sim/sdl3/cmake")
endif()
list(APPEND CMAKE_PREFIX_PATH "${KIT}/build-sim/sdl3")             # get-sdl.sh, elsewhere
find_package(SDL3 3.2 CONFIG QUIET)
if(NOT SDL3_FOUND)
  message(FATAL_ERROR "SDL3 isn't installed. docs/simulator.md says how (brew install sdl3, "
                      "MSYS2 or vcpkg), or sim/get-sdl.sh builds one into build-sim/sdl3.")
endif()
find_package(ZLIB QUIET)

# ---- the plugin --------------------------------------------------------------------
file(GLOB found_sources "${PLUGIN_DIR}/*.c" "${PLUGIN_DIR}/*.cpp" "${PLUGIN_DIR}/*.cc")
foreach(pattern IN LISTS PLUGIN_SOURCES)
  get_filename_component(pattern "${pattern}" ABSOLUTE BASE_DIR "${PLUGIN_DIR}")
  file(GLOB more "${pattern}")
  if(NOT more)
    message(FATAL_ERROR "PLUGIN_SOURCES: nothing is ${pattern}")
  endif()
  list(APPEND found_sources ${more})
endforeach()
set(plugin_sources "")
set(main_in_cxx OFF)
foreach(f IN LISTS found_sources)
  file(STRINGS "${f}" has_main REGEX "^[ \t]*int[ \t]+main[ \t]*\\(")
  file(STRINGS "${f}" tusb REGEX "#[ \t]*include[ \t]*\"tusb\\.h\"")
  if(tusb AND NOT has_main)
    message(STATUS "left out, as test/host leaves it out (it talks to TinyUSB): ${f}")
    continue()
  endif()
  if(has_main AND f MATCHES "\\.(cpp|cc)$")
    set(main_in_cxx ON)
  endif()
  list(APPEND plugin_sources "${f}")
endforeach()
if(NOT plugin_sources)
  message(FATAL_ERROR "no .c or .cpp files in ${PLUGIN_DIR}")
endif()

# its ID, name and USB features, from its CMakeLists.txt (the folder's own,
# or the one above src/), read as test/host/build.sh reads them: the ID and
# name are the defaults for --id and --name; each feature cvcart_features()
# turns on is a CVUSB_* switch, for the plugin and the models alike
include("${KIT}/test/host/plugin.cmake")
plugin_read("${PLUGIN_DIR}" plugin_features plugin_id name)
foreach(cm "${PLUGIN_DIR}/CMakeLists.txt" "${PLUGIN_DIR}/../CMakeLists.txt")
  if(EXISTS "${cm}")                  # (changed: read again at the next build)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${cm}")
  endif()
endforeach()
if(name)
  set(plugin_name "${name}")
else()
  get_filename_component(plugin_name "${PLUGIN_DIR}" NAME)
  if(plugin_name STREQUAL "src")
    get_filename_component(up "${PLUGIN_DIR}" DIRECTORY)
    get_filename_component(plugin_name "${up}" NAME)
  endif()
endif()
message(STATUS "the plugin: ${plugin_name} (${plugin_id}), from ${PLUGIN_DIR}")
string(REPLACE "=1" "" shown "${plugin_features}")
string(REPLACE "CVUSB_" "" shown "${shown}")
message(STATUS "its USB features: ${shown}")

set(includes
  "${KIT}/test/host/include"          # the models' stand-ins for the pico-sdk (first: cvpsram.h too)
  "${CMAKE_CURRENT_LIST_DIR}/include" # and the simulator's
  "${PLUGIN_DIR}" ${PLUGIN_INCLUDES}
  "${KIT}/lib" "${SDK}/sdk" "${SDK}/midihello" "${SDK}/sdplayer")

if(MSVC)
  set(common_options /W3 /utf-8 "/FI${CMAKE_CURRENT_LIST_DIR}/compat/msvc/msvc.h")
  list(APPEND includes "${CMAKE_CURRENT_LIST_DIR}/compat/msvc")
else()
  # (-Wno-deprecated-declarations: macOS's sprintf, fine in code for the Pico)
  set(common_options -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Wno-deprecated-declarations)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # (GCC at -O2: text cut to fit 32 columns on purpose, in the SDK and the examples)
    list(APPEND common_options -Wno-format-truncation -Wno-stringop-truncation)
  endif()
endif()
if(SIM_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# the plugin's files, its main() renamed (in C++, then handed on as C by cxxmain.cpp)
add_library(plugin OBJECT ${plugin_sources})
target_include_directories(plugin PRIVATE ${includes})
target_compile_options(plugin PRIVATE ${common_options})
target_compile_definitions(plugin PRIVATE $<$<COMPILE_LANGUAGE:C>:main=plugin_main>
                                          $<$<COMPILE_LANGUAGE:CXX>:main=plugin_main_cxx> ${plugin_features} ${PLUGIN_DEFINES})
if(APPLE)
  # an ELF section by name (QuickNES: __attribute__((section(".time_critical.x"))))
  # isn't a Mach-O one: there, a plain "used" instead
  target_compile_options(plugin PRIVATE "-Dsection(x)=used")   # (as a definition, CMake drops it)
endif()

# ---- RtMidi: this computer's MIDI ports (sim/third_party/rtmidi, as it is) ------------
set(RTMIDI "${CMAKE_CURRENT_LIST_DIR}/third_party/rtmidi")
add_library(rtmidi STATIC "${RTMIDI}/RtMidi.cpp" "${RTMIDI}/rtmidi_c.cpp")
target_include_directories(rtmidi PUBLIC "${RTMIDI}")
if(MSVC)
  target_compile_options(rtmidi PRIVATE /w /EHsc)       # (its warnings aren't ours to fix)
else()
  target_compile_options(rtmidi PRIVATE -w)
endif()
if(APPLE)
  target_compile_definitions(rtmidi PRIVATE RTMIDI_API_CORE)
  target_link_libraries(rtmidi PUBLIC "-framework CoreServices" "-framework CoreAudio" "-framework CoreMIDI"
                                      "-framework CoreFoundation")
elseif(WIN32)
  target_compile_definitions(rtmidi PRIVATE RTMIDI_API_WINMM)
  target_link_libraries(rtmidi PUBLIC winmm)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(rtmidi PUBLIC Threads::Threads)
  find_package(ALSA QUIET)
  if(ALSA_FOUND)
    target_compile_definitions(rtmidi PRIVATE RTMIDI_API_ALSA)
    target_link_libraries(rtmidi PUBLIC ALSA::ALSA)
  else()
    message(STATUS "no ALSA (libasound2-dev): no MIDI ports, only --midi-log")
  endif()
endif()

# ---- the simulator -----------------------------------------------------------------
add_executable(parrotsim
  main.c model.c audio.c psg.c input.c card.c extra.c midi.c
  "${KIT}/test/host/tms9918.c"
  "${SDK}/midihello/vdp.c" "${SDK}/sdplayer/fsro.c" "${SDK}/sdk/cvscreen.c" "${SDK}/sdk/swpro.c"
  "${SDK}/sdk/cvusb_keys.c" "${SDK}/sdk/cvusb_midi.c" "${SDK}/sdk/cvscale.c"
  $<TARGET_OBJECTS:plugin>)
if(main_in_cxx)
  target_sources(parrotsim PRIVATE cxxmain.cpp)
endif()
# host.c wraps the SDK's cvs_update to watch it (as test/host/build.sh builds it)
set_source_files_properties("${SDK}/sdk/cvscreen.c" PROPERTIES COMPILE_DEFINITIONS cvs_update=sdk_cvs_update)
target_include_directories(parrotsim PRIVATE ${includes})
target_compile_options(parrotsim PRIVATE ${common_options})
target_compile_definitions(parrotsim PRIVATE SIM_PLUGIN_ID="${plugin_id}" SIM_PLUGIN_NAME="${plugin_name}" ${plugin_features})
target_link_libraries(parrotsim PRIVATE SDL3::SDL3 rtmidi)
if(ZLIB_FOUND)
  target_link_libraries(parrotsim PRIVATE ZLIB::ZLIB)
else()
  message(STATUS "no zlib: PNGs are stored uncompressed (sim/compat/zlib)")
  target_sources(parrotsim PRIVATE compat/zlib/minizlib.c)
  set_source_files_properties(model.c PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/compat/zlib")
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(parrotsim PRIVATE m)
endif()
# the program where run.sh and run.bat look, whatever the generator
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${config} c)
  set_target_properties(parrotsim PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${c} "${CMAKE_BINARY_DIR}")
endforeach()
# a shared SDL3.dll beside it (vcpkg's, or the SDL3-devel zip's)
if(WIN32 AND NOT CMAKE_VERSION VERSION_LESS 3.21)
  add_custom_command(TARGET parrotsim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:parrotsim> $<TARGET_FILE_DIR:parrotsim>
    COMMAND_EXPAND_LISTS)
endif()
