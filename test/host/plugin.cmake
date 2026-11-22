# test/host/plugin.cmake -- what a plugin's CMakeLists.txt says, for the
# builds that run it on this computer: test/host/build.sh and the simulator
# (sim/CMakeLists.txt). One reader, so the two always agree.
#
# The CMakeLists.txt is the plugin folder's own, if it calls cvcart_plugin()
# or cvcart_features(), else the one above it (a plugin's src/).
#
#   include(test/host/plugin.cmake)
#   plugin_read(DIR FEATURES_VAR ID_VAR NAME_VAR)
#       FEATURES_VAR  the USB features cvcart_features() turns on, as a list
#                     of CVUSB_<FEATURE>=1 (PADS KEYBOARD MOUSE MIDI_IN
#                     MIDI_OUT; PSRAM and CARD need no switch)
#       ID_VAR        cvcart_plugin()'s ID, or ""
#       NAME_VAR      its NAME, or "" (a ${VAR}: the file's last set(VAR ...))
#
#   cmake -DPLUGIN_DIR=DIR -P test/host/plugin.cmake
#       prints the switches as compiler flags: -DCVUSB_PADS=1 ...
#
# Comments are left out. The last cvcart_features() in the file counts,
# and only the words written in it: a feature in a ${VARIABLE} isn't seen.

set(PLUGIN_USB_FEATURES PADS KEYBOARD MOUSE MIDI_IN MIDI_OUT)

function(plugin_read dir features_var id_var name_var)
  set(text "")
  foreach(cm "${dir}/CMakeLists.txt" "${dir}/../CMakeLists.txt")
    if(EXISTS "${cm}")
      file(READ "${cm}" text)
      # its comments out, one at a time: a # that isn't in a "string"
      while(text MATCHES "^[^\"#]*(\"[^\"]*\"[^\"#]*)*#")
        string(REGEX REPLACE "^([^\"#]*(\"[^\"]*\"[^\"#]*)*)#[^\n]*" "\\1" text "${text}")
      endwhile()
      if(text MATCHES "cvcart_(plugin|features)[ \t]*\\(")
        break()
      endif()
      set(text "")
    endif()
  endforeach()

  # cvcart_features(TARGET WORD ...): the words, the target left out
  set(features "")
  string(REGEX MATCHALL "cvcart_features[ \t]*\\([^)]*\\)" calls "${text}")
  if(calls)
    list(GET calls -1 call)
    string(REGEX REPLACE "^cvcart_features[ \t]*\\(([^)]*)\\)$" "\\1" words "${call}")
    string(STRIP "${words}" words)
    string(REGEX REPLACE "[ \t\r\n]+" ";" words "${words}")
    if(words)
      list(REMOVE_AT words 0)
    endif()
    foreach(w IN LISTS words)
      list(FIND PLUGIN_USB_FEATURES "${w}" at)       # (not IN_LIST: CMake 3 in script mode lacks it)
      if(NOT at EQUAL -1)
        list(APPEND features "CVUSB_${w}=1")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES features)
  endif()

  # cvcart_plugin(TARGET ID id NAME "name" ...)
  set(id "")
  set(name "")
  if(text MATCHES "cvcart_plugin[ \t]*\\(([^)]*)\\)")
    set(args "${CMAKE_MATCH_1}")
    if(args MATCHES "ID[ \t\r\n]+([^ \t\r\n)]+)")
      set(id "${CMAKE_MATCH_1}")
    endif()
    if(args MATCHES "NAME[ \t\r\n]+\"([^\"]*)\"")
      set(name "${CMAKE_MATCH_1}")
    elseif(args MATCHES "NAME[ \t\r\n]+(\\$\\{[A-Za-z0-9_]+\\})")
      set(name "${CMAKE_MATCH_1}")
    endif()
    # given as ${VAR}: the last set(VAR value) in the file
    foreach(v id name)
      if(${v} MATCHES "^\\$\\{([A-Za-z0-9_]+)\\}$")
        set(var "${CMAKE_MATCH_1}")
        string(REGEX MATCHALL "set\\(${var}[ \t]+(\"[^\"]*\"|[^ \t)]+)" sets "${text}")
        set(${v} "")
        foreach(one IN LISTS sets)
          string(REGEX REPLACE "^set\\(${var}[ \t]+\"?([^\"]*)\"?$" "\\1" ${v} "${one}")
        endforeach()
      endif()
    endforeach()
  endif()

  set(${features_var} "${features}" PARENT_SCOPE)
  set(${id_var} "${id}" PARENT_SCOPE)
  set(${name_var} "${name}" PARENT_SCOPE)
endfunction()

# cmake -P: the switches, as flags for the compiler
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  if(NOT PLUGIN_DIR)
    message(FATAL_ERROR "usage: cmake -DPLUGIN_DIR=DIR -P plugin.cmake")
  endif()
  plugin_read("${PLUGIN_DIR}" features id name)
  set(flags "")
  foreach(f IN LISTS features)
    string(APPEND flags " -D${f}")
  endforeach()
  string(STRIP "${flags}" flags)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${flags}")
endif()
