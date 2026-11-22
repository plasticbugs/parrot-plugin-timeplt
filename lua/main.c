// Lua for the Parrot Cart: a plugin that edits and runs .lua scripts.
//
// A script is opened from the cart's menu (the menu starts this plugin with
// the file in its launch block), picked from the list this plugin shows of
// the card's /LUA folder when it's started by itself, or built into the
// plugin (a Lua app as its own .CVP: cmake/parrotcart.cmake's
// parrotcart_lua()). The script gets the SDK as Lua functions (api.c;
// docs/lua.md is the reference) and is called the way PICO-8 and LOVE call
// theirs:
//
//   init()      once, after the file has run
//   update()    60 times a second
//   draw()      whenever the console can take a new picture
//
// With a USB keyboard feature on and no script built in, a script opens in
// the SDK's editor (cvedit.h): Ctrl+R or keypad 4 saves and runs it, Esc or the
// keypad's * held stops it, and an error goes back to the editor at its
// line. A Lua app built with the kit starts its script at once, and an
// error there shows on a screen of its own.
//
// The plugin's loop is the SDK's usual one, and it never waits on Lua: a
// hook runs every few milliseconds and serves the console from inside
// whatever the script is doing, so RESET goes back to the menu even from
// `while true do end`. A function that runs for more than 5 seconds is
// stopped with an error.
//
// The same file runs in the simulator and the tests (sim/, test/host).
#include <stdio.h>
#include <string.h>

#include "cvlua.h"

#include "pico/stdlib.h"

#include "card.h"
#include "cvkernel.h"
#include "cvusb.h"
#include "handoff.h"
#include "heap.h"
#include "mailbox.h"
#include "rom_image.h"

// ---- a Lua app's own files (parrotcart_lua() makes the header) ----------------------
#ifdef CVLUA_APP_HEADER
#include CVLUA_APP_HEADER
#else
static const cvlua_file_t cvlua_app[1] = {{NULL, NULL, 0}};
#define CVLUA_APP_COUNT 0
#endif
// where a script's files are when it gives no folder: an app's HOME, or the
// folder of the script that was opened
#ifndef CVLUA_HOME
#define CVLUA_HOME "/LUA"
#endif
// Blocks this big go to the PSRAM first, smaller ones to the chip's RAM
// (docs/lua.md, "Memory", has the measurements behind the number).
#ifndef CVLUA_PSRAM_MIN
#define CVLUA_PSRAM_MIN 4096
#endif
// On a computer the clock is the models' own, and moves when it's read. One
// run of the count hook stands for this many microseconds, so that a script
// takes about as long as it does on the cart (test/onarm.py measures it).
#ifndef CVLUA_HOST_HOOK_US
#define CVLUA_HOST_HOOK_US 330
#endif

#define CVLUA_USB (CVUSB_HOST_FEATURES || CVUSB_MIDI_OUT)

static cvs_t screen;
static uint8_t *list;                   // CVS_LIST_MAX bytes (aux())
static cvin_t in;
static uint8_t pressed;
static int key_pressed = -1;

// The editor: in the Lua plugin itself, when it has a keyboard to type on.
#if !CVLUA_APP_COUNT && CVUSB_KEYBOARD
#define CVLUA_EDITOR 1
#include "cvedit.h"
#else
#define CVLUA_EDITOR 0
#endif
#define EDIT_MAX   (128u * 1024u)       // the longest text, with the PSRAM
#define EDIT_SMALL (6u * 1024u)         // ...and without it
#define STOP_HOLD_US 2000000            // the keypad's * held this long stops a script

static enum { ST_WAIT, ST_EDIT, ST_RUN, ST_ERROR } state;
static const char *script_src;          // the editor's text, when that's what runs
static size_t script_len;
static bool stop_request;
static uint32_t star_us;                // since when * has been held (0: it isn't)
static cvkey_t keyq[32];                // the USB keyboard's keys, waiting
#if CVLUA_EDITOR
static char *edit_mem;                  // the editor's text
static size_t edit_size;
#endif
static uint8_t key_in, key_out;
static lua_State *L;
static heap_t sram, psram;
static void *sram_mem, *psram_mem;
static size_t sram_size, psram_size;

static char script_dir[256] = CVLUA_HOME;       // where the script's other files are
static char script_name[FS_NAME] = "main.lua";  // its name, for messages
static fs_file_t script_file;                   // on the card (not for a built-in script)
static bool script_on_card;
static char error_text[640];
static uint32_t frames, start_us, next_us, call_us;
static bool limit_on;
static uint32_t fps_now, fps_count, fps_us;

// ---- what api.c asks for -------------------------------------------------------------
canvas_t *rt_canvas(void) { return &screen.canvas; }
cvin_t *rt_input(void) { return &in; }
uint8_t rt_pressed(void) { return pressed; }
int rt_key_pressed(void) { return key_pressed; }
uint32_t rt_frame(void) { return frames; }
uint32_t rt_start_us(void) { return start_us; }
uint32_t rt_fps(void) { return fps_now; }
void rt_clock_restart(void) { call_us = time_us_32(); }

void rt_memory(size_t *su, size_t *ss, size_t *pu, size_t *ps) {
    *su = sram.used;
    *ss = sram.used + heap_room(&sram);
    *pu = psram.used;
    *ps = psram.used + heap_room(&psram);
}

const cvlua_file_t *rt_builtin(const char *name) {
    for (int i = 0; i < CVLUA_APP_COUNT; i++)
        if (!strcmp(cvlua_app[i].name, name)) return &cvlua_app[i];
    return NULL;
}

bool rt_path(const char *p, char *out, size_t max) {
    const int n = p[0] == '/' ? snprintf(out, max, "%s", p)
                              : snprintf(out, max, "%s/%s", strcmp(script_dir, "/") ? script_dir : "", p);
    return n > 0 && (size_t)n < max;
}

static void serve(uint32_t t) {
    port_served(t);
    cvk_task(t);                        // on RESET this doesn't return: the cart goes back to the menu
    int kind;
    uint8_t bits;
    while (cvk_report(t, &kind, &bits)) {
        if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
            cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
        else
            cvin_report(&in, kind, bits);
    }
#if CVLUA_USB
    cvusb_task();
#endif
#if CVUSB_KEYBOARD
    cvkey_t k;
    while (cvusb_key(&k)) {
        if (CVLUA_EDITOR && state == ST_RUN && script_src && k.usage == 0x29) {     // Esc stops the script
            if (k.down) stop_request = true;
            continue;
        }
        if ((uint8_t)(key_in - key_out) < sizeof keyq / sizeof keyq[0]) keyq[key_in++ % (sizeof keyq / sizeof keyq[0])] = k;
    }
#endif
    if (CVLUA_EDITOR && state == ST_RUN && script_src) {
        if (in.console_key != CVIN_STAR) star_us = 0;
        else if (!star_us) star_us = t | 1;
        else if (t - star_us > STOP_HOLD_US) stop_request = true;
    }
}

bool rt_next_key(cvkey_t *k) {
    if (key_in == key_out) return false;
    *k = keyq[key_out++ % (sizeof keyq / sizeof keyq[0])];
    return true;
}

void rt_service(void) { serve(time_us_32()); }

// ---- Lua's memory --------------------------------------------------------------------
// Two heaps. Small blocks live in the chip's RAM, which is fast; blocks of
// CVLUA_PSRAM_MIN bytes or more (long strings, big tables) go to the PSRAM,
// which is big. Either takes what the other has no room for.
static void *alloc(void *ud, void *p, size_t osize, size_t nsize) {
    (void)ud;
    if (!nsize) {
        if (p) heap_free(heap_owns(&sram, p) ? &sram : &psram, p);
        return NULL;
    }
    const size_t big = CVLUA_PSRAM_MIN;
    heap_t *first = nsize >= big && psram.size ? &psram : &sram;
    heap_t *second = first == &sram ? &psram : &sram;
    if (!p) {
        void *q = heap_alloc(first, nsize);
        return q ? q : heap_alloc(second, nsize);
    }
    heap_t *home = heap_owns(&sram, p) ? &sram : &psram;
    if (home == first || nsize <= osize) {
        void *q = heap_realloc(home, p, nsize);
        if (q) return q;
    }
    // it moves: to the heap its new size belongs in, or out of a full one
    void *q = heap_alloc(home == first ? second : first, nsize);
    if (q) {
        memcpy(q, p, osize < nsize ? osize : nsize);
        heap_free(home, p);
        return q;
    }
    return home == first ? NULL : heap_realloc(home, p, nsize);
}

// ---- errors --------------------------------------------------------------------------
#if CVLUA_EDITOR
static cvedit_t *editor;                // (aux(): it's 11 KB)
#define ed (*editor)
static bool run_when_saved;             // a run is waiting for the text's first name
static char said[2][44];                // a message over the editor's top and bottom lines
static bool said_top;

// Back to the editor with something to say (NULL: nothing) and the cursor
// on a line (0: where it was). A long message runs on from the top line to
// the bottom one. It stays until the next key.
static void to_editor(const char *message, bool bad, int line) {
    api_reset();                        // the sound off, the sprites gone
    stop_request = false;
    star_us = 0;
    key_in = key_out = 0;
    cvedit_redraw(&ed);
    if (line > 0) cvedit_goto(&ed, (uint32_t)line);
    size_t n = message ? strlen(message) : 0;
    if (n > 40) {                       // cut at a space, if there's one near
        size_t cut = 40;
        while (cut > 28 && message[cut] != ' ') cut--;
        if (message[cut] != ' ') cut = 40;
        snprintf(said[0], sizeof said[0], "%.*s", (int)cut, message);
        snprintf(said[1], sizeof said[1], "%.40s", message + cut + (message[cut] == ' '));
        cvedit_bars(&ed, said[0], NULL);
        cvedit_message(&ed, said[1], bad);
        said_top = true;
    } else if (n) cvedit_message(&ed, message, bad);
    state = ST_EDIT;
}
#endif

static void stop_with(const char *msg) {
    snprintf(error_text, sizeof error_text, "%s", msg ? msg : "an error with no message");
    lua_writestring(error_text, strlen(error_text));
    lua_writeline();
    char note[33];
    snprintf(note, sizeof note, "%.32s", error_text);
    cvk_note(note);
#if CVLUA_EDITOR
    if (script_src) {
        // "name.lua:12: what went wrong": the editor goes to line 12
        char first[96];
        snprintf(first, sizeof first, "%.95s", error_text);
        char *nl = strchr(first, '\n');
        if (nl) *nl = 0;
        int line = 0;
        const char *what = first;
        const size_t n = strlen(script_name);
        const char *at = strstr(first, script_name);
        if (at && at[n] == ':' && at[n + 1] >= '0' && at[n + 1] <= '9') {
            const char *p = at + n + 1;
            while (*p >= '0' && *p <= '9') line = line * 10 + (*p++ - '0');
            if (at == first) {
                static char shown[96];
                snprintf(shown, sizeof shown, "Line %d%s", line, p);
                what = shown;
            }
        }
        to_editor(what, true, line);
        return;
    }
#endif
    state = ST_ERROR;
    api_reset();                        // the sound off
}

// the message, then where the script was: the functions it was in, innermost first
static int with_traceback(lua_State *l) {
    const char *msg = lua_tostring(l, 1);
    if (!msg) msg = luaL_tolstring(l, 1, NULL);
    luaL_traceback(l, l, msg, 1);
    return 1;
}

// The function and its n arguments on the stack, called with the time
// limit on. False, and the error screen, if it failed.
static bool call(int n) {
    const int base = lua_gettop(L) - n;
    lua_pushcfunction(L, with_traceback);
    lua_insert(L, base);
    call_us = time_us_32();
    limit_on = true;
    const int r = lua_pcall(L, n, 0, base);
    limit_on = false;
    lua_remove(L, base);
    if (r == LUA_OK) return true;
#if CVLUA_EDITOR
    if (stop_request) {                 // stopped from the keyboard or the keypad: not an error
        lua_pop(L, 1);
        to_editor("Stopped", false, 0);
        return false;
    }
#endif
    stop_with(lua_tostring(L, -1));
    lua_pop(L, 1);
    return false;
}

lua_State *volatile cvlua_running;

int cvlua_resume(lua_State *l, lua_State *from, int nargs, int *nresults) {
    cvlua_running = l;
    const int r = lua_resume(l, from, nargs, nresults);
    cvlua_running = from;
    return r;
}

void cvlua_hook(lua_State *l, lua_Debug *ar) {
#if CVLUA_TIMER_HOOK
    lua_sethook(l, NULL, 0, 0);         // until the timer's next tick
#elif !defined(__arm__)
    sleep_us(CVLUA_HOST_HOOK_US - 100);
#endif
    const uint32_t t = time_us_32();
    serve(t);
    if (stop_request) luaL_error(l, "stopped");
    if (limit_on && t - call_us > CVLUA_LIMIT_US) {
        call_us = t;
        lua_getinfo(l, "Sl", ar);       // the line the script was on
        luaL_error(l, "%s:%d: stopped after %d seconds. Is a loop stuck?", ar->short_src, ar->currentline,
                   CVLUA_LIMIT_US / 1000000);
    }
}

static int lua_panicked(lua_State *l) {
    stop_with(lua_tostring(l, -1));
    for (;;) rt_service();              // nothing to go back to: RESET still works
    return 0;
}

// ---- starting a script ---------------------------------------------------------------
// (in a protected call) the script's file run, then its init()
static int boot(lua_State *l) {
    const char *err = NULL;
    if (script_src) {                   // the editor's text, as it stands
        lua_pushfstring(l, "=%s", script_name);
        if (luaL_loadbufferx(l, script_src, script_len, lua_tostring(l, -1), "t") != LUA_OK) return lua_error(l);
        lua_remove(l, -2);
        goto loaded;
    }
    if (script_on_card) {
        if (script_file.size > 1024 * 1024) err = "a script is 1 MB at most";
        else {
            luaL_Buffer b;
            const uint32_t n = (uint32_t)script_file.size;
            char *p = luaL_buffinitsize(l, &b, n ? n : 1);
            err = card_read(&script_file, 0, (uint8_t *)p, n);
            luaL_pushresultsize(&b, err ? 0 : n);
        }
    } else err = rt_load_file(l, script_name);
    if (err) return luaL_error(l, "%s: %s", script_name, err);
    size_t n;
    const char *src = lua_tolstring(l, -1, &n);
    lua_pushfstring(l, "=%s", script_name);
    if (luaL_loadbufferx(l, src, n, lua_tostring(l, -1), "t") != LUA_OK) return lua_error(l);
    lua_remove(l, -2);
    lua_remove(l, -2);                  // the source: the chunk is all that's kept
loaded:
    call_us = time_us_32();
    lua_call(l, 0, 0);
    const bool has_update = lua_getglobal(l, "update") == LUA_TFUNCTION;
    const bool has_draw = lua_getglobal(l, "draw") == LUA_TFUNCTION;
    lua_pop(l, 2);
    if (!has_update && !has_draw) return luaL_error(l, "%s has no update() or draw() function", script_name);
    if (lua_getglobal(l, "init") == LUA_TFUNCTION) {
        call_us = time_us_32();
        lua_call(l, 0, 0);
    }
    return 0;
}

static void start_script(void) {
    // a fresh Lua each time, in fresh heaps (no need to free the old one piece by piece)
    cvlua_running = NULL;               // (the timer must not touch the old one)
    heap_init(&sram, sram_mem, sram_size);
    heap_init(&psram, psram_mem, psram_size);
    api_reset();
    cvk_note(NULL);
    frames = 0;
    pressed = 0;
    key_pressed = -1;
    L = lua_newstate(alloc, NULL);
    if (!L) {
        stop_with("no memory to start Lua");
        return;
    }
    lua_atpanic(L, lua_panicked);
    static const luaL_Reg LIBS[] = {{LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
                                    {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                    {LUA_COLIBNAME, luaopen_coroutine}, {NULL, NULL}};
    for (const luaL_Reg *lib = LIBS; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    // math.random's seed: the chip's random numbers on the cart; on a
    // computer the clock, or LUA_SEED (Lua's own seed mixes in an address,
    // which differs from run to run there)
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, (lua_Integer)(cvlua_seed() & 0x7FFFFFFF));
    lua_call(L, 1, 0);
    lua_pop(L, 1);
    api_open(L);
#if !CVLUA_TIMER_HOOK
    lua_sethook(L, cvlua_hook, LUA_MASKCOUNT, CVLUA_HOOK_COUNT);
#endif
    cvlua_running = L;
    state = ST_RUN;
    stop_request = false;
    star_us = 0;
    key_in = key_out = 0;
    start_us = time_us_32();
    lua_pushcfunction(L, boot);
    if (!call(0)) return;
    next_us = time_us_32();
}

// ---- the editor ----------------------------------------------------------------------
#if CVLUA_EDITOR
static fs_t *editor_card(void *ctx, bool failed) {
    (void)ctx;
    return card_fs(failed);
}

static void editor_start(void) {
    const cvedit_config_t cfg = {
        .text = (uint8_t *)edit_mem, .cap = (uint32_t)edit_size, .x = 8, .y = 0, .cols = 40, .rows = 24,
        .ext = ".lua", .home = CVLUA_HOME, .stem = "SCRIPT",
        .keys = "1 Save  2 Open  3 New  4 Run (Ctrl+R)", .card = editor_card};
    cvedit_init(&ed, &cfg);
    state = ST_EDIT;
}

// the editor's text, run
static void editor_run(void) {
    run_when_saved = false;
    snprintf(script_name, sizeof script_name, "%s", ed.name[0] ? ed.name : "script");
    snprintf(script_dir, sizeof script_dir, "%s", ed.dir[0] ? ed.dir : CVLUA_HOME);
    script_src = (const char *)ed.cfg.text;
    script_len = ed.len;
    start_script();
}

// what the editor reports: a save done, a key that's this plugin's
static void editor_event(int ev) {
    if (ev == CVEDIT_HOST_CTRL + 'R' || ev == CVEDIT_HOST_KEYPAD + 4) {
        // saved first, so what runs is what's on the card. A new text is
        // asked for its name, and runs when it has one. With no card to
        // save to it runs all the same.
        if (cvedit_modified(&ed) || !ed.name_n) {
            if (cvedit_save(&ed) == CVEDIT_NONE) {
                run_when_saved = true;
                return;
            }
        }
        editor_run();
    } else if ((ev == CVEDIT_SAVED || ev == CVEDIT_SAVE_FAILED) && run_when_saved) editor_run();
    else if (ev == CVEDIT_OPENED) run_when_saved = false;
}

static void editor_keys(void) {
    cvkey_t k;
    while (state == ST_EDIT && rt_next_key(&k)) {
        if (k.down && said_top) {       // a message's top half goes with the key, as its bottom half does
            said_top = false;
            cvedit_bars(&ed, NULL, NULL);
        }
        const int ev = cvedit_key(&ed, &k);
        if (ev == CVEDIT_NONE && k.down && cvedit_editing(&ed)) run_when_saved = false;   // (back in the text: the name wasn't given)
        editor_event(ev);
    }
    if (state == ST_EDIT) editor_event(cvedit_pad(&ed, pressed, key_pressed));
}
#endif

// ---- this plugin's own screens -------------------------------------------------------
// text, wrapped at 30 characters, from row y: the y after it
static int wrapped(canvas_t *c, int y, int max_y, const char *s, uint8_t colour) {
    while (*s && y <= max_y) {
        char line[31];
        int n = 0;
        while (s[n] && s[n] != '\n' && n < 30) n++;
        if (n == 30 && s[n] && s[n] != '\n') {          // break at a space, if the line has one
            int k = n;
            while (k > 10 && s[k] != ' ') k--;
            if (k > 10) n = k;
        }
        for (int i = 0; i < n; i++) line[i] = s[i] == '\t' ? ' ' : s[i];
        line[n] = 0;
        cvs_line(c, 8, y, 240, line, colour, C_BLACK, 1);
        y += 9;
        s += n;
        while (*s == ' ') s++;
        if (*s == '\n') s++;
    }
    return y;
}

static void draw_error(canvas_t *c) {
    canvas_fill(c, 0, 0, VDP_W, VDP_H, C_BLACK);
    cvs_line(c, 8, 8, 240, "LUA STOPPED", C_LRED, C_BLACK, 2);
    // the message, then the traceback in grey
    char msg[sizeof error_text];
    snprintf(msg, sizeof msg, "%s", error_text);
    char *trace = strstr(msg, "\nstack traceback:");
    if (trace) *trace++ = 0;
    int y = wrapped(c, 32, 120, msg, C_WHITE);
    if (trace) wrapped(c, y + 5, 156, trace, C_GRAY);
    cvs_line(c, 8, 172, 240, "FIRE: RUN IT AGAIN", C_LYELLOW, C_BLACK, 1);
    cvs_line(c, 8, 182, 240, "RESET: BACK TO THE MENU", C_GRAY, C_BLACK, 1);
}

static void draw_wait(canvas_t *c) {
    char s[33];
    cvs_line(c, 8, 4, 240, "LUA " CVLUA_VERSION, C_LYELLOW, C_BLACK, 1);
    if (script_name[0]) snprintf(s, sizeof s, "STARTING %.21s", script_name);
    else snprintf(s, sizeof s, "READING THE CARD");
    cvs_line(c, 8, 90, 240, CVLUA_APP_COUNT ? "" : s, C_WHITE, C_BLACK, 1);
}

static int last_state = -1;
static void draw(canvas_t *c, bool all, void *ctx) {
    (void)ctx;
    if (state == ST_RUN) {
        if (lua_getglobal(L, "draw") == LUA_TFUNCTION) {
            if (last_state != ST_RUN) canvas_fill(c, 0, 0, VDP_W, VDP_H, C_BLACK);
            lua_pushboolean(L, all || last_state != ST_RUN);
            last_state = ST_RUN;
            call(1);
        } else lua_pop(L, 1);
    }
    if (state == ST_RUN) return;
    if (last_state != (int)state) canvas_fill(c, 0, 0, VDP_W, VDP_H, C_BLACK);     // a screen of this plugin's, from clean
    const bool fresh = all || last_state != (int)state;
    last_state = state;
    if (state == ST_ERROR) draw_error(c);
#if CVLUA_EDITOR
    else if (state == ST_EDIT) cvedit_draw(&ed, c, fresh);
#endif
    else draw_wait(c);
    (void)fresh;
}

// ---- once a TV frame -----------------------------------------------------------------
// (in a protected call) what USB brought, then the script's update()
static int update(lua_State *l) {
    api_tick(l);
    if (lua_getglobal(l, "update") == LUA_TFUNCTION) lua_call(l, 0, 0);
    return 0;
}

static void tick(void) {
    cvin_update(&in);
    pressed = cvin_pressed(&in);
    key_pressed = cvin_key(&in);
    cvkey_t k;
    switch (state) {
    case ST_RUN:
#if CVLUA_EDITOR
        if (stop_request) {
            to_editor("Stopped", false, 0);
            break;
        }
#endif
        frames++;
        lua_pushcfunction(L, update);
        if (call(0)) api_tick_end();
        break;
#if CVLUA_EDITOR
    case ST_EDIT:
        editor_keys();
        break;
#endif
    case ST_ERROR:
        while (rt_next_key(&k)) {}
        if (pressed & (CVIN_FIRE_L | CVIN_FIRE_R)) start_script();
        break;
    default:
        break;
    }
}

// What to start with: the plugin's own script; the file the menu opened,
// in the editor; or the editor and its list of the card's scripts. Without
// an editor (no keyboard feature) the file runs, and with no file there is
// nothing to do but say so.
static void begin(void) {
    const cvl_t *block = cvk_launch();
    if (CVLUA_APP_COUNT) {
        snprintf(script_name, sizeof script_name, "%s", cvlua_app[0].name);
        start_script();
        return;
    }
    if (block && block->path[0]) {
        const char *slash = strrchr(block->path, '/');
        snprintf(script_name, sizeof script_name, "%.63s", slash ? slash + 1 : block->path);
        const size_t n = slash ? (size_t)(slash - block->path) : 0;
        if (n && n < sizeof script_dir) {
            memcpy(script_dir, block->path, n);
            script_dir[n] = 0;
        } else strcpy(script_dir, "/");
        const char *err = card_mount_launched(block, &script_file);
        if (err) {
            char msg[128];
            snprintf(msg, sizeof msg, "%.63s: %s", script_name, err);
            stop_with(msg);
            return;
        }
        script_on_card = true;
#if CVLUA_EDITOR
        if (script_file.size <= edit_size && !port_run_at_once()) {
            editor_start();
            cvedit_load(&ed, block->path, &script_file);    // (if it fails, the editor says why)
            return;
        }
#endif
        start_script();                 // too long for the editor, or no editor: it just runs
        return;
    }
#if CVLUA_EDITOR
    editor_start();
    // a save that was cut short last time is offered first; else the list
    // of scripts to open (keypad 2)
    if (!cvedit_check(&ed, NULL)) cvedit_pad(&ed, 0, 2);
#else
    stop_with("Open a .lua file from the cart's menu");
#endif
}

// The picture: what draw() changed, and the sprites, as one list. A list
// goes out as soon as the console has run the one before, so a script that
// changes little gets a new picture every TV frame.
#define SPRITE_ROOM 300                 // kept for the sprites' table in every list
static void show(uint32_t t) {
    if (!screen.ready || screen.out) return;
    const bool whole = screen.full;
    const size_t n = cvs_update(&screen, draw, NULL, list, CVS_LIST_MAX - SPRITE_ROOM, t);
    const size_t at = n ? n - 1 : 0;    // (over its end mark)
    const size_t m = api_sprites(&screen.vdp, list + at, CVS_LIST_MAX - at, whole);
    if (!n && !m) return;
    if (!n) {                           // nothing drawn, sprites moved: a list of this file's own making
        screen.seq++;
        screen.out = true;
        screen.out_full = whole;
        screen.sent_us = t;
        screen.lists++;
    }
    list[at + m] = LIST_END;
    cvk_list(list, at + m + 1, screen.out_full, screen.seq);
    fps_count++;
}

// n bytes that stay for good, from the top of the PSRAM, or from the
// chip's RAM if there's no PSRAM (then `small` bytes, if that's given)
static void *aux(size_t n, size_t small) {
    void *p = NULL;
    n = (n + 7) & ~(size_t)7;
    small = small ? (small + 7) & ~(size_t)7 : n;
    if (psram_size >= n + 1024 * 1024) {
        psram_size -= n;
        p = (char *)psram_mem + psram_size;
    } else if (sram_size >= small + 24 * 1024) {
        sram_size -= small;
        p = (char *)sram_mem + sram_size;
        n = small;
    }
    if (p) memset(p, 0, n);
    return p;
}

static void run(void) {
    // First thing: the console is parked, waiting for a cartridge to appear.
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
    cvin_init(&in);
#if CVLUA_USB
    cvusb_start();
#endif
    port_watch_start();
    card_set_poll(rt_service);
    sram_mem = port_sram(&sram_size);
    psram_mem = port_psram(&psram_size);
    // What needn't be fast lives at the top of the PSRAM, to leave the
    // chip's RAM for Lua: the list being sent, the file system's state, the
    // editor and its text.
    list = aux(CVS_LIST_MAX, 0);
    card_memory(aux(sizeof(fs_t), 0));
#if CVLUA_EDITOR
    editor = aux(sizeof *editor, 0);
    edit_size = EDIT_MAX;
    edit_mem = aux(EDIT_MAX, EDIT_SMALL);
    if ((char *)edit_mem >= (char *)sram_mem && (char *)edit_mem < (char *)sram_mem + sram_size + EDIT_SMALL) edit_size = EDIT_SMALL;
    if (!edit_mem) edit_size = 0;
#endif
    {   // which script, for the first screen
        const cvl_t *block = cvk_launch();
        if (!CVLUA_APP_COUNT && block && block->path[0]) {
            const char *slash = strrchr(block->path, '/');
            snprintf(script_name, sizeof script_name, "%.63s", slash ? slash + 1 : block->path);
        } else if (!CVLUA_APP_COUNT) script_name[0] = 0;
    }

    const uint32_t t0 = time_us_32();
    next_us = fps_us = t0;
    for (;;) {
        const uint32_t t = time_us_32();
        serve(t);

        // The card's start can take a second and a half: once, after the
        // console has its first screen.
        if (state == ST_WAIT) {
            if (screen.lists && !screen.out && t - t0 > 100000) {
                begin();
                next_us = time_us_32();
            }
        } else if ((int32_t)(t - next_us) >= 0) {
            // 60 times a second. A script that falls behind catches up at
            // most 4 updates in a row; past that the time is let go.
            int n = 0;
            do {
                tick();
                next_us += CVLUA_FRAME_US;
            } while (++n < 4 && (int32_t)(time_us_32() - next_us) >= 0);
            const uint32_t now = time_us_32();
            if ((int32_t)(now - next_us) >= 0) next_us = now + CVLUA_FRAME_US;
        }

        const uint32_t t2 = time_us_32();
#if CVLUA_EDITOR
        if (state == ST_EDIT) editor_event(cvedit_task(&ed, t2));      // a held key's repeats, a save that's due
#endif
        show(t2);
        if (t2 - fps_us >= 1000000) {
            fps_us = t2;
            fps_now = fps_count;
            fps_count = 0;
        }
    }
}

int main(void) {
    port_run(run);
    return 0;
}
