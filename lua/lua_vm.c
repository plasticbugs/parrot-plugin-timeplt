// lua_vm.c -- Lua 5.4.8's core (vendor/lua-5.4.8, unchanged but for two
// lines of luaconf.h), as one file: the parts that run a script. The parts
// that read one, and the libraries, are in lua_libs.c. On the cart this
// file is built for speed and that one for size.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

#define LUA_CORE
#include "vendor/lua-5.4.8/lprefix.h"

#include "vendor/lua-5.4.8/lzio.c"
#include "vendor/lua-5.4.8/lctype.c"
#include "vendor/lua-5.4.8/lopcodes.c"
#include "vendor/lua-5.4.8/lmem.c"
#include "vendor/lua-5.4.8/lstate.c"
#include "vendor/lua-5.4.8/lgc.c"
#include "vendor/lua-5.4.8/ldebug.c"
#include "vendor/lua-5.4.8/lfunc.c"
#include "vendor/lua-5.4.8/lobject.c"
#include "vendor/lua-5.4.8/ltm.c"
#include "vendor/lua-5.4.8/lstring.c"
#include "vendor/lua-5.4.8/ltable.c"
#include "vendor/lua-5.4.8/ldo.c"
#include "vendor/lua-5.4.8/lvm.c"
#include "vendor/lua-5.4.8/lapi.c"
