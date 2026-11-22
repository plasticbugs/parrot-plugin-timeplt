// lua_libs.c -- the rest of Lua 5.4.8 (vendor/lua-5.4.8), as one file: the
// lexer, the parser and the code generator, the auxiliary library, and the
// standard libraries the cart has: base, coroutine, math, string and table.
// There is no io, os, package, debug or utf8. require, loadfile and dofile
// are the runtime's own, and read from the SD card (api.c).
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
#define LUA_LIB
#include "vendor/lua-5.4.8/lprefix.h"

#include "vendor/lua-5.4.8/llex.c"
#include "vendor/lua-5.4.8/lcode.c"
#include "vendor/lua-5.4.8/lparser.c"
#include "vendor/lua-5.4.8/lundump.c"
#include "vendor/lua-5.4.8/ldump.c"
#include "vendor/lua-5.4.8/lauxlib.c"

// loadfile() and dofile(): from the card, not the C library's files
int cvlua_loadfilex(lua_State *L, const char *filename, const char *mode);
#define luaL_loadfilex(L, f, m) cvlua_loadfilex(L, f, m)

#include "vendor/lua-5.4.8/lbaselib.c"

// coroutines: the runtime keeps track of which one is running (main.c)
int cvlua_resume(lua_State *L, lua_State *from, int nargs, int *nresults);
#define lua_resume(L, from, n, r) cvlua_resume(L, from, n, r)
#include "vendor/lua-5.4.8/lcorolib.c"
#undef lua_resume
#include "vendor/lua-5.4.8/lmathlib.c"
#include "vendor/lua-5.4.8/lstrlib.c"
#include "vendor/lua-5.4.8/ltablib.c"
