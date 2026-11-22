// cvlua_conf.h -- Lua's settings for the Parrot Cart. The vendored
// luaconf.h includes this at its end (vendor/lua-5.4.8/VENDORED.md), so
// every file that includes lua.h gets the same settings.
//
// Numbers are 32-bit integers and 32-bit floats (LUA_32BITS, set in
// luaconf.h). The cart's Cortex-M33 has a single-precision FPU, so floats
// are one instruction each where doubles would be library calls, and a Lua
// value is 8 bytes instead of 16.
#pragma once
#include <stddef.h>
#include <time.h>

// The C stack. Each nested C call (a metamethod, pcall, a callback from
// string.gsub or table.sort, a level of brackets in the parser) counts as
// one, and Lua stops with "C stack overflow" at this many. The cart runs
// Lua on a stack of its own (CVLUA_STACK in port.c). The deepest kind of
// level takes about 615 bytes of it (test/onarm.sh measures them), and Lua
// lets an error handler go a tenth past the limit.
#ifndef LUAI_MAXCCALLS
#define LUAI_MAXCCALLS 30
#endif
// string.find and its family call themselves for each item of a pattern,
// 80 bytes or so a time: at most this deep ("pattern too complex").
#define MAXCCALLS 100

// Two buffers the string library keeps on the C stack, made smaller: they
// are most of what a nested string.gsub or string.format costs. A string
// being built moves to Lua's memory past 128 bytes, and a pattern has 16
// captures at most.
#undef LUAL_BUFFERSIZE
#define LUAL_BUFFERSIZE 128
#define LUA_MAXCAPTURES 16

// print() and Lua's own warnings go to the simulator's terminal. On the
// cart they go nowhere: no C library stdio is linked.
void cvlua_write(const char *s, size_t n);
void cvlua_write_error(const char *fmt, const char *arg);
#define lua_writestring(s, l)      cvlua_write((s), (l))
#define lua_writeline()            cvlua_write("\n", 1)
#define lua_writestringerror(s, p) cvlua_write_error((s), (p))

// no locale
#undef lua_getlocaledecpoint
#define lua_getlocaledecpoint() '.'

// The seeds for math.random and for the string hash: the chip's random
// number generator on the cart. On a computer it's the clock, or LUA_SEED
// in the environment so a test runs the same way every time.
unsigned int cvlua_seed(void);
#define luai_makeseed(L)    cvlua_seed()
#define l_randomizePivot()  cvlua_seed()
#define time(p)             ((time_t)cvlua_seed())

// Numbers to and from text, on the cart. The C library's strtof needs
// malloc there, and the pico-sdk's printf writes %g its own way (0.5 comes
// out as 0.5000000), so the runtime has its own of each (port.c). Hex
// floats use Lua's own code (lobject.c, lstrlib.c).
#if defined(__arm__)
float cvlua_strtof(const char *s, char **end);
int cvlua_snprintf(char *s, size_t size, const char *fmt, ...);
#undef lua_str2number
#define lua_str2number(s, p) cvlua_strtof((s), (p))
#undef lua_strx2number
#undef lua_number2strx
#undef l_sprintf
#define l_sprintf(s, sz, f, i) cvlua_snprintf((s), (sz), (f), (i))
#endif
