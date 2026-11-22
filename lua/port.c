// port.c -- what differs between the cart and a computer (cvlua.h).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cvlua.h"
#undef time                             // (cvlua_conf.h points Lua's at cvlua_seed)

#include "pico/stdlib.h"
#include "cvkernel.h"
#include "cvpsram.h"

#if defined(__arm__)
// =====================================================================================
// the cart
// =====================================================================================
#include "pico/rand.h"
#include "pico/time.h"
#include "hardware/watchdog.h"

// Lua's C stack. The parser and nested C calls go as deep as LUAI_MAXCCALLS
// lets them (cvlua_conf.h); test/onarm.py measures how deep that is.
#ifndef CVLUA_STACK
#define CVLUA_STACK (22 * 1024)
#endif
#define STACK_PAINT 0xA5A5A5A5u
static uint32_t stack[CVLUA_STACK / 4] __attribute__((aligned(8)));

__attribute__((naked, noreturn)) static void on_stack(void (*fn)(void), void *top, void *limit) {
    (void)fn; (void)top; (void)limit;
    __asm volatile("msr msplim, r2\n"
                   "mov sp, r1\n"
                   "blx r0\n"
                   "1: b 1b\n");
}

void port_run(void (*fn)(void)) {
    for (size_t i = 0; i < CVLUA_STACK / 4; i++) stack[i] = STACK_PAINT;
    on_stack(fn, stack + CVLUA_STACK / 4, stack);
}

size_t port_stack_used(void) {
    size_t i = 0;
    while (i < CVLUA_STACK / 4 && stack[i] == STACK_PAINT) i++;
    return CVLUA_STACK - 4 * i;
}

// A fault (the stack past its limit, a bad pointer): there is nothing left
// to run the script with, so the cart goes back to the menu and says so.
// The console is still running the screen kernel, so the menu comes up when
// RESET is pressed.
void cvlua_fault(void) {
    cvl_t *b = CVL_BLOCK;
    if (cvl_ok(b)) cvl_return(b, "Lua stopped with a fault");
    watchdog_reboot(0, 0, 1);
    for (;;) tight_loop_contents();
}

__attribute__((naked)) void isr_hardfault(void) {
    __asm volatile("movs r0, #0\n"
                   "msr msplim, r0\n"
                   "ldr r0, =__StackTop\n"
                   "mov sp, r0\n"
                   "b cvlua_fault\n");
}

void *port_sram(size_t *size) {
    extern char end;                    // the end of .bss (the linker's)
    uintptr_t a = ((uintptr_t)&end + 7) & ~(uintptr_t)7;
    *size = a < CVL_ADDR ? CVL_ADDR - a : 0;
    return (void *)a;
}

void *port_psram(size_t *size) {
    *size = 0;
    if (!cvpsram_init()) return NULL;
    *size = CVPSRAM_SIZE;
    return CVPSRAM_CACHED;
}

// Every 4 ms a timer sets Lua's hook, the way the lua program does for
// Ctrl+C: the next VM instruction runs cvlua_hook(), which serves the
// console and takes the hook off again. A hook that counted instructions
// would do the same job, but Lua then checks it before every instruction
// and runs at a third of the speed (test/onarm.sh measures both).
//
// Lua inside one C function for a long time (a pattern match that tries
// every way to fail, say) never reaches the hook. If the loop hasn't been
// served for 300 ms, the timer runs the console's task itself, so RESET and
// the power switch still work.
static volatile uint32_t served_us;
static repeating_timer_t watch_timer;

void cvlua_tick(void) {
#if CVLUA_TIMER_HOOK
    lua_State *l = cvlua_running;
    if (l) lua_sethook(l, cvlua_hook, LUA_MASKCOUNT, 1);
#endif
}

static bool watch_tick(repeating_timer_t *t) {
    (void)t;
    const uint32_t now = time_us_32();
    if (now - served_us > 300000) cvk_task(now);
    cvlua_tick();
    return true;
}

void port_watch_start(void) {
    served_us = time_us_32();
    add_repeating_timer_ms(4, watch_tick, NULL, &watch_timer);
}

void port_served(uint32_t now_us) { served_us = now_us; }

bool port_run_at_once(void) { return false; }

unsigned int cvlua_seed(void) { return (unsigned int)get_rand_32(); }

void cvlua_write(const char *s, size_t n) { (void)s; (void)n; }
void cvlua_write_error(const char *fmt, const char *arg) { (void)fmt; (void)arg; }

// Decimal text to a float. The digits are gathered as an integer and scaled
// by a power of ten in double arithmetic, which is exact for the numbers a
// script is likely to write (up to 15 digits and exponents within 22) and
// within a unit in the last place for the rest.
float cvlua_strtof(const char *s, char **end) {
    static const double p10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
    bool neg = false;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    uint64_t m = 0;
    int e = 0, digits = 0;
    bool any = false, dot = false;
    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            any = true;
            if (digits < 18) {
                m = m * 10 + (uint64_t)(*p - '0');
                if (m) digits++;
                if (dot) e--;
            } else if (!dot) e++;
        } else if (*p == '.' && !dot) dot = true;
        else break;
    }
    if (!any) {
        if (end) *end = (char *)s;
        return 0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        bool eneg = false;
        if (*q == '-' || *q == '+') eneg = *q++ == '-';
        if (*q >= '0' && *q <= '9') {
            int x = 0;
            while (*q >= '0' && *q <= '9') {
                if (x < 10000) x = x * 10 + (*q - '0');
                q++;
            }
            e += eneg ? -x : x;
            p = q;
        }
    }
    if (end) *end = (char *)p;
    double v = (double)m;
    if (m) {
        if (e > 60) e = 60;             // past a float's range either way
        if (e < -80) e = -80;
        while (e > 22) { v *= 1e22; e -= 22; }
        while (e < -22) { v /= 1e22; e += 22; }
        if (e > 0) v *= p10[e];
        else if (e < 0) v /= p10[-e];
    }
    const float f = (float)v;
    return neg ? -f : f;
}

// One number as %g writes it: `prec` significant digits, the shortest of
// the plain and the exponent forms, trailing zeros dropped (kept with #).
static int format_g(char *out, size_t size, double v, int prec, bool alt, bool upper, bool plus, bool space,
                    bool left, bool zero, int width) {
    static const double big[] = {1e16, 1e8, 1e4, 1e2, 1e1};
    static const int bige[] = {16, 8, 4, 2, 1};
    char body[40], sign = 0;
    int n = 0;
    if (prec <= 0) prec = prec < 0 ? 6 : 1;
    if (prec > 17) prec = 17;
    union { double d; uint64_t u; } bits = {v};
    if (bits.u >> 63) { sign = '-'; v = -v; }
    else if (plus) sign = '+';
    else if (space) sign = ' ';
    if (v != v) { strcpy(body, upper ? "NAN" : "nan"); n = 3; zero = false; }
    else if (v > 1.7e308) { strcpy(body, upper ? "INF" : "inf"); n = 3; zero = false; }
    else {
        int e = 0;
        uint64_t m = 0, top = 1;
        for (int i = 1; i < prec; i++) top *= 10;       // 10^(prec-1)
        if (v != 0) {
            for (int i = 0; i < 5; i++) while (v >= big[i]) { v /= big[i]; e += bige[i]; }
            for (int i = 0; i < 5; i++) while (v * big[i] < 10) { v *= big[i]; e -= bige[i]; }
            if (v < 1) { v *= 10; e--; }                // now 1 <= v < 10
            m = (uint64_t)(v * (double)top + 0.5);
            if (m >= top * 10) { m /= 10; e++; }
        }
        char digits[20];
        for (int i = prec - 1; i >= 0; i--) { digits[i] = (char)('0' + m % 10); m /= 10; }
        int keep = prec;                                // digits worth writing
        if (!alt) while (keep > 1 && digits[keep - 1] == '0') keep--;
        if (e < -4 || e >= prec) {                      // d.ddde+XX
            body[n++] = digits[0];
            if (keep > 1 || alt) body[n++] = '.';
            for (int i = 1; i < keep; i++) body[n++] = digits[i];
            body[n++] = upper ? 'E' : 'e';
            body[n++] = e < 0 ? '-' : '+';
            int a = e < 0 ? -e : e;
            if (a >= 100) body[n++] = (char)('0' + a / 100);
            body[n++] = (char)('0' + a / 10 % 10);
            body[n++] = (char)('0' + a % 10);
        } else if (e < 0) {                             // 0.000ddd
            body[n++] = '0';
            body[n++] = '.';
            for (int i = -1; i > e; i--) body[n++] = '0';
            for (int i = 0; i < keep; i++) body[n++] = digits[i];
        } else {                                        // ddd.ddd
            for (int i = 0; i <= e; i++) body[n++] = digits[i];
            if (keep > e + 1 || alt) body[n++] = '.';
            for (int i = e + 1; i < keep; i++) body[n++] = digits[i];
        }
    }
    const int len = n + (sign ? 1 : 0), pad = width > len ? width - len : 0;
    size_t o = 0;
#define PUT(ch) do { if (o + 1 < size) out[o] = (ch); o++; } while (0)
    if (!left && !zero) for (int i = 0; i < pad; i++) PUT(' ');
    if (sign) PUT(sign);
    if (!left && zero) for (int i = 0; i < pad; i++) PUT('0');
    for (int i = 0; i < n; i++) PUT(body[i]);
    if (left) for (int i = 0; i < pad; i++) PUT(' ');
#undef PUT
    if (size) out[o < size ? o : size - 1] = 0;
    return (int)o;
}

// Lua's snprintf: every format it passes has one conversion. %g is done
// here, and the rest is the pico-sdk's.
int cvlua_snprintf(char *s, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r;
    const char *p = strchr(fmt, '%');
    bool alt = false, plus = false, space = false, left = false, zero = false;
    int width = 0, prec = -1;
    if (p) {
        for (p++; *p == '#' || *p == '+' || *p == ' ' || *p == '-' || *p == '0'; p++) {
            if (*p == '#') alt = true;
            else if (*p == '+') plus = true;
            else if (*p == ' ') space = true;
            else if (*p == '-') left = true;
            else zero = true;
        }
        while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            prec = 0;
            for (p++; *p >= '0' && *p <= '9'; p++) prec = prec * 10 + (*p - '0');
        }
    }
    if (p && (*p == 'g' || *p == 'G') && p == strchr(fmt, '%') + strcspn(strchr(fmt, '%'), "gG") && !p[1])
        r = format_g(s, size, va_arg(ap, double), prec, alt, *p == 'G', plus, space, left, zero, width);
    else r = vsnprintf(s, size, fmt, ap);
    va_end(ap);
    return r;
}

#else
// =====================================================================================
// a computer: the simulator and the tests
// =====================================================================================
void port_run(void (*fn)(void)) { fn(); }
size_t port_stack_used(void) { return 0; }

// The same room as on the cart, doubled: a Lua value is 16 bytes on a
// 64-bit computer and 8 on the cart, so a script that fits here fits there.
#ifndef CVLUA_HOST_SRAM
#define CVLUA_HOST_SRAM (2 * 200 * 1024)
#endif
void *port_sram(size_t *size) {
    static union { double d; void *p; long long l; } ram[CVLUA_HOST_SRAM / 8];
    *size = sizeof ram;
    return ram;
}

void *port_psram(size_t *size) {
    *size = 0;
    if (!cvpsram_init()) return NULL;
    *size = CVPSRAM_SIZE;
    return CVPSRAM_CACHED;
}

void port_watch_start(void) {}
void port_served(uint32_t now_us) { (void)now_us; }

// LUA_RUN in the environment (sim/lua.sh --run): the script runs at once
bool port_run_at_once(void) { return getenv("LUA_RUN") != NULL; }

unsigned int cvlua_seed(void) {
    const char *s = getenv("LUA_SEED");
    if (s) return (unsigned int)strtoul(s, NULL, 0);
    return (unsigned int)time(NULL);
}

void cvlua_write(const char *s, size_t n) {
    fwrite(s, 1, n, stdout);
    if (n && s[n - 1] == '\n') fflush(stdout);
}
void cvlua_write_error(const char *fmt, const char *arg) {
    fprintf(stderr, fmt, arg);
    fflush(stderr);
}
#endif
