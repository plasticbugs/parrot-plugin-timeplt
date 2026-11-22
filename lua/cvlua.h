// cvlua.h -- the Lua runtime's own parts, shared between its files.
//
//   main.c   the plugin's loop: the console, the script's update() and
//            draw(), the file list, the error screen
//   api.c    the functions a script calls (docs/lua.md lists them)
//   card.c   files on the SD card
//   heap.c   the memory Lua's values live in
//   port.c   what differs between the cart and a computer
//   lua_vm.c, lua_libs.c   Lua 5.4 itself (vendor/lua-5.4.8)
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vendor/lua-5.4.8/lua.h"
#include "vendor/lua-5.4.8/lauxlib.h"
#include "vendor/lua-5.4.8/lualib.h"

#include "cvinput.h"
#include "cvscreen.h"
#include "cvusb_keys.h"

#define CVLUA_VERSION "0.1.0"

// A TV frame, in microseconds: update() runs once for each.
#define CVLUA_FRAME_US 16683
// A script's function that runs longer than this is stopped with an error.
#define CVLUA_LIMIT_US 5000000
// On a computer the hook runs every this many VM instructions. (The cart
// can be built that way too, to measure it: -DCVLUA_COUNT_HOOK.)
#define CVLUA_HOOK_COUNT 1000
#if defined(__arm__) && !defined(CVLUA_COUNT_HOOK)
#define CVLUA_TIMER_HOOK 1
#else
#define CVLUA_TIMER_HOOK 0
#endif

// ---- files built into the plugin (a Lua app as its own .CVP) ---------------------
typedef struct {
    const char *name;                   // "main.lua", "levels/one.txt"
    const unsigned char *data;
    unsigned len;
} cvlua_file_t;
const cvlua_file_t *rt_builtin(const char *name);

// ---- main.c ------------------------------------------------------------------------
canvas_t *rt_canvas(void);
cvin_t *rt_input(void);
uint8_t rt_pressed(void);               // CVIN_* that went down this update
int rt_key_pressed(void);               // the keypad key that went down this update, -1 none
uint32_t rt_frame(void);                // update() calls so far
uint32_t rt_start_us(void);             // when the script started
uint32_t rt_fps(void);                  // pictures sent in the last second
bool rt_next_key(cvkey_t *k);           // the USB keyboard's next key, pressed or let go
void rt_service(void);                  // the console's task and reports, USB
void rt_clock_restart(void);            // a long card read doesn't count against CVLUA_LIMIT_US
// A path as a script gives it, made whole: "/..." as it is, anything else
// in the script's folder. False if it's too long.
bool rt_path(const char *in, char *out, size_t max);
// A file's bytes as a Lua string on the stack: built in, or on the card.
// NULL, or what went wrong.
const char *rt_load_file(lua_State *L, const char *name);
// memory: Lua's two heaps
void rt_memory(size_t *sram_used, size_t *sram_size, size_t *psram_used, size_t *psram_size);

// The Lua thread that's running now (a coroutine, or the script itself),
// and the hook that serves the console from inside it. On the cart a timer
// sets the hook every few milliseconds (port.c). On a computer it's a count
// hook, which is slower but always the same from run to run.
extern lua_State *volatile cvlua_running;
void cvlua_hook(lua_State *l, lua_Debug *ar);

// ---- api.c -------------------------------------------------------------------------
void api_open(lua_State *L);            // the functions, into the globals
void api_reset(void);                   // a new script: the sound off, the pen white, the mouse centred
void api_tick(lua_State *L);            // once an update: USB events to the script's functions
// The sprites' changes as list commands (no end mark), at most max bytes.
// whole: the console's screen was started over.
size_t api_sprites(vdp_t *v, uint8_t *list, size_t max, bool whole);
void api_tick_end(void);                // ...and after it: sounds that have run their time; the chip told

unsigned int cvlua_seed(void);          // (cvlua_conf.h)

// ---- port.c ------------------------------------------------------------------------
// main() on a stack big enough for Lua (the cart's own is 4 KB). On a
// computer it just calls fn.
void port_run(void (*fn)(void));
size_t port_stack_used(void);
// The chip's RAM left over for Lua: from the end of the plugin's own
// variables to the launch block.
void *port_sram(size_t *size);
// The PSRAM, or NULL if it didn't answer.
void *port_psram(size_t *size);
// A timer, every 4 ms: it sets Lua's hook, and it keeps the console served
// if Lua stays inside one C function for a long time. port_served() says
// the loop is alive.
void port_watch_start(void);
// The opened script runs without a stop in the editor (the simulator's --run).
bool port_run_at_once(void);
void port_served(uint32_t now_us);
