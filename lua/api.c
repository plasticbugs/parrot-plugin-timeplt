// api.c -- the functions a Lua script calls. docs/lua.md is the reference
// for script writers; this file's comments say how each one works.
#include <math.h>
#include <string.h>

#include "cvlua.h"

#include "pico/stdlib.h"
#include "card.h"
#include "cvkernel.h"
#include "cvfont.h"
#include "cvusb.h"
#include "mailbox.h"

// ---- arguments -----------------------------------------------------------------------
// A coordinate: any number, rounded down, kept within reach of the screen
// so a wild value can't make a loop run for long.
static int geti(lua_State *L, int i) {
    const lua_Number v = luaL_checknumber(L, i);
    if (!(v > -4096)) return -4096;     // (NaN too)
    if (v > 4096) return 4096;
    return (int)l_mathop(floor)(v);
}

static int opti(lua_State *L, int i, int def) { return lua_isnoneornil(L, i) ? def : geti(L, i); }

// A colour, 0 to 15, as the video chip numbers them. 0 is its transparent,
// which on this screen is black. Left out: the last colour used.
static uint8_t pen = C_WHITE;
static uint8_t colour(lua_State *L, int i) {
    if (lua_isnoneornil(L, i)) return pen;
    const lua_Integer c = (lua_Integer)l_mathop(floor)(luaL_checknumber(L, i));
    luaL_argcheck(L, c >= 0 && c <= 15, i, "a colour is 0 to 15");
    pen = c ? (uint8_t)c : C_BLACK;
    return pen;
}

// ---- drawing -------------------------------------------------------------------------
static inline void put(canvas_t *c, int x, int y, uint8_t col) {
    if ((unsigned)x >= VDP_W || (unsigned)y >= VDP_H || c->px[y][x] == col) return;
    c->px[y][x] = col;
    c->dirty[y >> 3][x >> 3] = 1;
}

static void fill(canvas_t *c, int x, int y, int w, int h, uint8_t col) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VDP_W) w = VDP_W - x;
    if (y + h > VDP_H) h = VDP_H - y;
    if (w <= 0 || h <= 0) return;
    canvas_fill(c, x, y, w, h, col);
}

static int print_y;
static int l_cls(lua_State *L) {
    print_y = 0;
    fill(rt_canvas(), 0, 0, VDP_W, VDP_H, lua_isnoneornil(L, 1) ? C_BLACK : colour(L, 1));
    return 0;
}

static int l_pset(lua_State *L) {
    const int x = geti(L, 1), y = geti(L, 2);
    put(rt_canvas(), x, y, colour(L, 3));
    return 0;
}

static int l_pget(lua_State *L) {
    const int x = geti(L, 1), y = geti(L, 2);
    if ((unsigned)x >= VDP_W || (unsigned)y >= VDP_H) lua_pushinteger(L, 0);
    else lua_pushinteger(L, rt_canvas()->px[y][x]);
    return 1;
}

static int l_line(lua_State *L) {
    int x0 = geti(L, 1), y0 = geti(L, 2);
    const int x1 = geti(L, 3), y1 = geti(L, 4);
    const uint8_t col = colour(L, 5);
    canvas_t *c = rt_canvas();
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put(c, x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 0;
}

static int l_rect(lua_State *L) {
    const int x = geti(L, 1), y = geti(L, 2), w = geti(L, 3), h = geti(L, 4);
    const uint8_t col = colour(L, 5);
    canvas_t *c = rt_canvas();
    if (w <= 0 || h <= 0) return 0;
    fill(c, x, y, w, 1, col);
    fill(c, x, y + h - 1, w, 1, col);
    fill(c, x, y, 1, h, col);
    fill(c, x + w - 1, y, 1, h, col);
    return 0;
}

static int l_rectfill(lua_State *L) {
    const int x = geti(L, 1), y = geti(L, 2), w = geti(L, 3), h = geti(L, 4);
    fill(rt_canvas(), x, y, w, h, colour(L, 5));
    return 0;
}

static void circle(lua_State *L, bool filled) {
    const int cx = geti(L, 1), cy = geti(L, 2);
    int r = geti(L, 3);
    const uint8_t col = colour(L, 4);
    canvas_t *c = rt_canvas();
    if (r < 0) return;
    if (r > 512) r = 512;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        if (filled) {
            fill(c, cx - x, cy + y, 2 * x + 1, 1, col);
            fill(c, cx - x, cy - y, 2 * x + 1, 1, col);
            fill(c, cx - y, cy + x, 2 * y + 1, 1, col);
            fill(c, cx - y, cy - x, 2 * y + 1, 1, col);
        } else {
            put(c, cx + x, cy + y, col); put(c, cx - x, cy + y, col);
            put(c, cx + x, cy - y, col); put(c, cx - x, cy - y, col);
            put(c, cx + y, cy + x, col); put(c, cx - y, cy + x, col);
            put(c, cx + y, cy - x, col); put(c, cx - y, cy - x, col);
        }
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

static int l_circ(lua_State *L) { circle(L, false); return 0; }
static int l_circfill(lua_State *L) { circle(L, true); return 0; }

// print(s [, x, y [, colour [, scale]]]): the SDK's 6x8 font, 42 characters
// across. Only the letters' own pixels are drawn. Without x and y it goes
// on the next line down, as on a terminal (cls() starts again at the top),
// and to the simulator's terminal too. Returns the x after the last
// character.
static int l_print(lua_State *L) {
    size_t n;
    lua_settop(L, 5);
    const char *s = luaL_tolstring(L, 1, &n);
    const bool placed = !lua_isnil(L, 2);
    const int x = placed ? geti(L, 2) : 0, y = placed ? geti(L, 3) : print_y;
    const uint8_t col = colour(L, 4);
    int scale = opti(L, 5, 1);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    canvas_t *c = rt_canvas();
    if (!placed) {
        print_y += CVFONT_H * scale;
        lua_writestring(s, n);
        lua_writeline();
    }
    const int w = CVFONT_W * scale;
    int at = x;
    for (size_t i = 0; i < n && at < VDP_W; i++, at += w) {
        if (at + w <= 0) continue;
        const int ch = (unsigned char)s[i];
        const uint8_t *g = cvfont[(ch < 32 || ch > 126 ? '?' : ch) - 32];
        for (int r = 0; r < CVFONT_H; r++)
            for (int b = 0; b < CVFONT_W; b++)
                if (g[r] >> (7 - b) & 1) {
                    if (scale == 1) put(c, at + b, y + r, col);
                    else fill(c, at + b * scale, y + r * scale, scale, scale, col);
                }
    }
    lua_pushinteger(L, x + (int)n * w);
    return 1;
}

// ---- sprites -------------------------------------------------------------------------
// The video chip's own 32 sprites, drawn over the picture by the console:
// moving one costs 4 bytes, and it never disturbs what's under it. Each is
// one colour, 8x8 or 16x16, and the chip shows 4 on any one line.
//
// What the script wants is kept here, and api_sprites() adds what differs
// from the console's video RAM to the next list.
#define SPRITES     32
#define VRAM_SPAT   0x1800          // the kernel's sprite pattern table (register 6)
static struct {
    bool on;
    int16_t x, y;
    uint8_t shape, colour;
} spr[SPRITES];
static uint8_t spr_pat[2048];       // the shapes: 256 of 8x8, or 64 of 16x16
static bool spr_big, spr_zoom;      // 16x16; shown at twice the size
static uint8_t spr_r1;              // register 1 as the console has it (0: not known)

// shape(n, rows): 8 strings of 8 characters or 16 of 16; '.' and ' ' are
// see-through, anything else is a pixel
static int l_shape(lua_State *L) {
    const int n = geti(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    const int rows = (int)lua_rawlen(L, 2);
    luaL_argcheck(L, rows == 8 || rows == 16, 2, "a shape is 8 rows of 8 or 16 rows of 16");
    const bool big = rows == 16;
    luaL_argcheck(L, n >= 1 && n <= (big ? 64 : 256), 1, big ? "a 16x16 shape is 1 to 64" : "an 8x8 shape is 1 to 256");
    uint8_t bits[32] = {0};
    for (int r = 0; r < rows; r++) {
        size_t len;
        luaL_argcheck(L, lua_rawgeti(L, 2, r + 1) == LUA_TSTRING, 2, "each row of a shape is a string");
        const char *s = lua_tolstring(L, -1, &len);
        for (int i = 0; i < rows && i < (int)len; i++)
            if (s[i] != '.' && s[i] != ' ') bits[(i >> 3) * 16 + r] |= (uint8_t)(0x80 >> (i & 7));  // (the chip's order: the left half's rows, then the right's)
        lua_pop(L, 1);
    }
    spr_big = big;
    memcpy(spr_pat + (size_t)(n - 1) * (big ? 32 : 8), bits, big ? 32 : 8);
    return 0;
}

// sprite(n, x, y, shape [, colour]) shows sprite n there; sprite(n) hides it
static int l_sprite(lua_State *L) {
    const int n = geti(L, 1);
    luaL_argcheck(L, n >= 1 && n <= SPRITES, 1, "a sprite is 1 to 32");
    if (lua_isnoneornil(L, 2)) {
        spr[n - 1].on = false;
        return 0;
    }
    const int x = geti(L, 2), y = geti(L, 3), shape = geti(L, 4);
    luaL_argcheck(L, shape >= 1 && shape <= 256, 4, "a shape is 1 to 256");
    spr[n - 1].colour = colour(L, 5);
    spr[n - 1].x = (int16_t)x;
    spr[n - 1].y = (int16_t)y;
    spr[n - 1].shape = (uint8_t)(shape - 1);
    spr[n - 1].on = true;
    return 0;
}

// zoom(true): every sprite at twice its size
static int l_zoom(lua_State *L) {
    spr_zoom = lua_toboolean(L, 1);
    return 0;
}

// want[0..n) to VRAM at addr where it differs from what the console has,
// as copy commands; false when the list is full
static bool vram_diff(vdp_t *v, uint8_t *list, size_t *used, size_t max, uint16_t addr, const uint8_t *want, int n) {
    int i = 0;
    while (i < n) {
        if (v->vram[addr + i] == want[i]) { i++; continue; }
        int j = i + 1, last = i;
        for (; j < n && j - i < LIST_COPY_MAX && j - last <= 3; j++)
            if (v->vram[addr + j] != want[j]) last = j;
        const int len = last - i + 1;
        if (*used + 3 + (size_t)len + 1 > max) return false;
        uint8_t *p = list + *used;
        p[0] = (uint8_t)len;
        p[1] = (uint8_t)(addr + i);
        p[2] = (uint8_t)((addr + i) >> 8 | 0x40);
        memcpy(p + 3, want + i, (size_t)len);
        memcpy(v->vram + addr + i, want + i, (size_t)len);
        *used += 3 + (size_t)len;
        i += len;
    }
    return true;
}

size_t api_sprites(vdp_t *v, uint8_t *list, size_t max, bool whole) {
    size_t used = 0;
    if (whole) spr_r1 = 0;
    const uint8_t r1 = (uint8_t)(0xE0 | (spr_big ? 2 : 0) | (spr_zoom ? 1 : 0));    // the kernel's own value, and the sprites' size
    if (r1 != spr_r1 && (spr_r1 || r1 != 0xE0) && max >= 4) {
        list[used++] = 0;
        list[used++] = r1;
        list[used++] = 0x81;
        spr_r1 = r1;
    } else if (!spr_r1) spr_r1 = 0xE0;
    // the attribute table: every sprite up to the last one shown, then the
    // chip's end mark
    uint8_t sat[SPRITES * 4 + 1];
    int count = 0;
    for (int i = 0; i < SPRITES; i++) if (spr[i].on) count = i + 1;
    const int size = (spr_big ? 16 : 8) * (spr_zoom ? 2 : 1);
    for (int i = 0; i < count; i++) {
        int x = spr[i].x, y = spr[i].y;
        uint8_t *a = sat + 4 * i;
        const bool shown = spr[i].on && x > -size && x < VDP_W && y > -size && y < VDP_H && x >= -32;
        a[0] = shown ? (uint8_t)(y - 1) : 0xC0;         // (a row of 192 is below the picture; 208 would end the table)
        a[1] = (uint8_t)(x < 0 ? x + 32 : x);
        a[2] = (uint8_t)(spr_big ? spr[i].shape * 4 : spr[i].shape);
        a[3] = (uint8_t)(spr[i].colour | (x < 0 ? 0x80 : 0));   // the early clock bit: 32 pixels to the left
        if (!shown) a[1] = a[3] = 0;
    }
    if (count < SPRITES) sat[4 * count] = 0xD0;
    if (!vram_diff(v, list, &used, max, VRAM_SAT, sat, count < SPRITES ? 4 * count + 1 : 4 * count)) return used;
    // only the shapes in use need to be there... but which those are can
    // change with one byte of the table above, so all of them are kept up
    vram_diff(v, list, &used, max, VRAM_SPAT, spr_pat, sizeof spr_pat);
    return used;
}

// ---- images --------------------------------------------------------------------------
// A small picture: a byte a pixel, 0 for see-through. image{...} makes one
// from rows of text, a character a pixel: 0-9 and a-f are the colours, and
// '.', ' ' and '0' are see-through.
#define IMAGE "cvlua.image"
typedef struct {
    uint16_t w, h;
    uint8_t px[];
} image_t;

static int l_image(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const lua_Integer h = (lua_Integer)lua_rawlen(L, 1);
    luaL_argcheck(L, h >= 1 && h <= VDP_H, 1, "an image has 1 to 192 rows");
    size_t w = 0;
    for (lua_Integer r = 1; r <= h; r++) {
        luaL_argcheck(L, lua_rawgeti(L, 1, r) == LUA_TSTRING, 1, "each row of an image is a string");
        const size_t n = lua_rawlen(L, -1);
        if (n > w) w = n;
        lua_pop(L, 1);
    }
    luaL_argcheck(L, w >= 1 && w <= VDP_W, 1, "an image is 1 to 256 pixels wide");
    image_t *im = lua_newuserdatauv(L, sizeof *im + w * (size_t)h, 0);
    im->w = (uint16_t)w;
    im->h = (uint16_t)h;
    memset(im->px, 0, w * (size_t)h);
    for (lua_Integer r = 1; r <= h; r++) {
        size_t n;
        lua_rawgeti(L, 1, r);
        const char *s = lua_tolstring(L, -1, &n);
        for (size_t i = 0; i < n; i++) {
            const char ch = s[i];
            uint8_t v;
            if (ch >= '0' && ch <= '9') v = (uint8_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') v = (uint8_t)(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') v = (uint8_t)(ch - 'A' + 10);
            else if (ch == '.' || ch == ' ') v = 0;
            else return luaL_error(L, "row %d of the image has '%c': use 0-9, a-f, and . for see-through", (int)r, ch);
            im->px[(size_t)(r - 1) * w + i] = v;
        }
        lua_pop(L, 1);
    }
    luaL_setmetatable(L, IMAGE);
    return 1;
}

// blit(image, x, y [, flip_x [, flip_y [, colour]]]): with a colour, every
// pixel of the image is drawn in it (one image, many colours).
static int l_blit(lua_State *L) {
    const image_t *im = luaL_checkudata(L, 1, IMAGE);
    const int x = geti(L, 2), y = geti(L, 3);
    const bool fx = lua_toboolean(L, 4), fy = lua_toboolean(L, 5);
    const uint8_t one = lua_isnoneornil(L, 6) ? 0 : colour(L, 6);
    canvas_t *c = rt_canvas();
    for (int j = 0; j < im->h; j++) {
        const int dy = y + j;
        if (dy < 0 || dy >= VDP_H) continue;
        const uint8_t *row = im->px + (size_t)(fy ? im->h - 1 - j : j) * im->w;
        for (int i = 0; i < im->w; i++) {
            const uint8_t v = row[fx ? im->w - 1 - i : i];
            if (v) put(c, x + i, dy, one ? one : v);
        }
    }
    return 0;
}

static int image_index(lua_State *L) {
    const image_t *im = luaL_checkudata(L, 1, IMAGE);
    const char *k = lua_tostring(L, 2);
    if (k && !strcmp(k, "w")) lua_pushinteger(L, im->w);
    else if (k && !strcmp(k, "h")) lua_pushinteger(L, im->h);
    else lua_pushnil(L);
    return 1;
}

// ---- sound ---------------------------------------------------------------------------
// The chip's 10 bytes as the kernel takes them (mailbox.h), and for each
// voice the updates it still has to play, 0 for until told otherwise.
#define PSG_CLOCK 3579545
static uint8_t snd[10];
static uint16_t snd_left[4];
static bool snd_dirty;
static uint8_t noise_seq;

static void voice_off(int ch) {
    if (ch < 3) snd[3 * ch + 2] = (uint8_t)(0x9F | ch << 5);
    else snd[9] = 0xFF;
    snd_left[ch] = 0;
    snd_dirty = true;
}

static int volume(lua_State *L, int i) {
    const int v = opti(L, i, 15);
    luaL_argcheck(L, v >= 0 && v <= 15, i, "a volume is 0 to 15");
    return v;
}

static int updates(lua_State *L, int i) {
    const int n = opti(L, i, 0);
    return n < 0 ? 0 : n > 65535 ? 65535 : n;
}

static void play(int ch, lua_Number hz, int vol, int n) {
    if (vol == 0 || !(hz > 0)) {
        voice_off(ch);
        return;
    }
    lua_Number d = (lua_Number)PSG_CLOCK / (32 * hz) + (lua_Number)0.5;
    unsigned div = d > 1023 ? 1023 : d < 1 ? 1 : (unsigned)d;
    snd[3 * ch + 0] = (uint8_t)(0x80 | ch << 5 | (div & 15));
    snd[3 * ch + 1] = (uint8_t)(div >> 4);
    snd[3 * ch + 2] = (uint8_t)(0x90 | ch << 5 | (15 - vol));
    snd_left[ch] = (uint16_t)n;
    snd_dirty = true;
}

static int voice(lua_State *L) {
    const int v = geti(L, 1);
    luaL_argcheck(L, v >= 1 && v <= 3, 1, "a voice is 1, 2 or 3");
    return v - 1;
}

// tone(voice, hz [, volume [, updates]])
static int l_tone(lua_State *L) {
    const int ch = voice(L);
    const lua_Number hz = luaL_checknumber(L, 2);
    const int vol = volume(L, 3);
    play(ch, hz, vol, updates(L, 4));
    return 0;
}

// "C4", "F#3", "Bb5" (middle C is C4), or a MIDI note number -> hertz
static lua_Number pitch(lua_State *L, int i) {
    lua_Number m;
    if (lua_type(L, i) == LUA_TNUMBER) m = lua_tonumber(L, i);
    else {
        static const int8_t semis[7] = {9, 11, 0, 2, 4, 5, 7};      // A B C D E F G
        const char *s = luaL_checkstring(L, i);
        char c = s[0];
        if (c >= 'a' && c <= 'g') c = (char)(c - 32);
        if (c < 'A' || c > 'G') return luaL_argerror(L, i, "a note is like C4, F#3 or Bb5");
        int n = semis[c - 'A'];
        s++;
        if (*s == '#') { n++; s++; }
        else if (*s == 'b') { n--; s++; }
        int oct = 4;
        if (*s) {
            const bool neg = *s == '-';
            if (neg) s++;
            if (*s < '0' || *s > '9' || s[1]) return luaL_argerror(L, i, "a note is like C4, F#3 or Bb5");
            oct = neg ? -(*s - '0') : *s - '0';
        }
        m = (lua_Number)(12 * (oct + 1) + n);
    }
    return (lua_Number)440 * l_mathop(pow)((lua_Number)2, (m - 69) / 12);
}

// note(voice, pitch [, volume [, updates]])
static int l_note(lua_State *L) {
    const int ch = voice(L);
    const lua_Number hz = pitch(L, 2);
    const int vol = volume(L, 3);
    play(ch, hz, vol, updates(L, 4));
    return 0;
}

// noise(kind [, volume [, updates]]): kinds 1-3 are a hiss from high to
// low and 4 a hiss at voice 3's pitch; 5-8 are the same as a buzz.
static int l_noise(lua_State *L) {
    const int kind = geti(L, 1);
    luaL_argcheck(L, kind >= 1 && kind <= 8, 1, "a noise is 1 to 8");
    const int vol = volume(L, 2);
    const int n = updates(L, 3);
    if (vol == 0) {
        voice_off(3);
        return 0;
    }
    // the control byte restarts the noise, so it's sent apart, once
    cvk_mb(MB_NCTL, (uint8_t)(0xE0 | (kind <= 4 ? 4 : 0) | ((kind - 1) & 3)));
    cvk_mb(MB_NSEQ, ++noise_seq);
    snd[9] = (uint8_t)(0xF0 | (15 - vol));
    snd_left[3] = (uint16_t)n;
    snd_dirty = true;
    return 0;
}

static int l_mute(lua_State *L) {
    (void)L;
    for (int ch = 0; ch < 4; ch++) voice_off(ch);
    return 0;
}

// ---- the controller ------------------------------------------------------------------
static const char *const BUTTONS[] = {"up", "right", "down", "left", "fire", "fire2", NULL};    // CVIN_*'s order

static int l_btn(lua_State *L) {
    lua_pushboolean(L, rt_input()->held >> luaL_checkoption(L, 1, NULL, BUTTONS) & 1);
    return 1;
}

static int l_btnp(lua_State *L) {
    lua_pushboolean(L, rt_pressed() >> luaL_checkoption(L, 1, NULL, BUTTONS) & 1);
    return 1;
}

static int push_key(lua_State *L, int k) {
    if (k < 0) lua_pushnil(L);
    else {
        const char s[2] = {k == CVIN_STAR ? '*' : k == CVIN_HASH ? '#' : (char)('0' + k), 0};
        lua_pushstring(L, s);
    }
    return 1;
}

static int l_keypad(lua_State *L) { return push_key(L, rt_input()->key); }
static int l_keypadp(lua_State *L) { return push_key(L, rt_key_pressed()); }

// ---- USB -----------------------------------------------------------------------------
#define CVLUA_USB (CVUSB_HOST_FEATURES || CVUSB_MIDI_OUT)

static int off(lua_State *L, const char *feature) {
    return luaL_error(L, "this plugin was built without %s (cvcart_features in its CMakeLists.txt)", feature);
}

#if CVUSB_PADS
static const char *const PAD_BUTTONS[GP_BUTTONS] = {
    "up", "down", "left", "right", "south", "east", "west", "north", "l", "r", "zl", "zr",
    "minus", "plus", "home", "capture", "lstick", "rstick"};
#endif

// pad(n): USB pad 1 or 2 as a table, the same one each time, or nil
static int l_pad(lua_State *L) {
#if CVUSB_PADS
    const int n = opti(L, 1, 1);
    luaL_argcheck(L, n >= 1 && n <= CVUSB_PAD_MAX, 1, "a pad is 1 or 2");
    gamepad_t p;
    if (!cvusb_pad(n - 1, &p)) {
        lua_pushnil(L);
        return 1;
    }
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &PAD_BUTTONS[n - 1]) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_createtable(L, 0, GP_BUTTONS + 5);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &PAD_BUTTONS[n - 1]);
    }
    for (int b = 0; b < GP_BUTTONS; b++) {
        lua_pushboolean(L, p.buttons >> b & 1);
        lua_setfield(L, -2, PAD_BUTTONS[b]);
    }
    lua_pushinteger(L, p.lx); lua_setfield(L, -2, "lx");
    lua_pushinteger(L, p.ly); lua_setfield(L, -2, "ly");
    lua_pushinteger(L, p.rx); lua_setfield(L, -2, "rx");
    lua_pushinteger(L, p.ry); lua_setfield(L, -2, "ry");
    lua_pushstring(L, cvusb_pad_kind(n - 1)); lua_setfield(L, -2, "kind");
    return 1;
#else
    return off(L, "PADS");
#endif
}

#if CVUSB_KEYBOARD
static uint8_t key_held[32];            // a bit a HID usage

// A key's name: its character without Shift when it has one ("a", "1",
// "/"), or a word.
static void key_name(uint8_t usage, char out[12]) {
    static const struct { uint8_t usage; const char *name; } WORDS[] = {
        {0x28, "return"}, {0x29, "escape"}, {0x2A, "backspace"}, {0x2B, "tab"}, {0x2C, "space"},
        {0x39, "capslock"}, {0x49, "insert"}, {0x4A, "home"}, {0x4B, "pageup"}, {0x4C, "delete"},
        {0x4D, "end"}, {0x4E, "pagedown"}, {0x4F, "right"}, {0x50, "left"}, {0x51, "down"}, {0x52, "up"},
        {0xE0, "lctrl"}, {0xE1, "lshift"}, {0xE2, "lalt"}, {0xE3, "lgui"},
        {0xE4, "rctrl"}, {0xE5, "rshift"}, {0xE6, "ralt"}, {0xE7, "rgui"}};
    for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
        if (WORDS[i].usage == usage) {
            strcpy(out, WORDS[i].name);
            return;
        }
    if (usage >= 0x3A && usage <= 0x45) {       // F1 to F12
        const int n = usage - 0x39;
        out[0] = 'f';
        if (n >= 10) { out[1] = '1'; out[2] = (char)('0' + n - 10); out[3] = 0; }
        else { out[1] = (char)('0' + n); out[2] = 0; }
        return;
    }
    const char ch = cvkey_ascii(usage, false);
    if (ch > 32 && ch < 127) {
        out[0] = ch;
        out[1] = 0;
        return;
    }
    out[0] = 'k'; out[1] = 'e'; out[2] = 'y';
    out[3] = (char)('0' + usage / 100); out[4] = (char)('0' + usage / 10 % 10); out[5] = (char)('0' + usage % 10);
    out[6] = 0;
}
#endif

// key(name): is that key of the USB keyboard held?
static int l_key(lua_State *L) {
#if CVUSB_KEYBOARD
    const char *want = luaL_checkstring(L, 1);
    bool down = false;
    for (int u = 4; u < 256 && !down; u++) {
        if (!(key_held[u >> 3] >> (u & 7) & 1)) continue;
        char name[12];
        key_name((uint8_t)u, name);
        down = !strcmp(name, want);
    }
    lua_pushboolean(L, down);
    return 1;
#else
    return off(L, "KEYBOARD");
#endif
}

#if CVUSB_MOUSE
static struct {
    bool there;
    int x, y, dx, dy, wheel;
    uint8_t buttons;
} mouse;
#endif

// mouse(): a table, the same one each time, or nil with no mouse plugged in
static int l_mouse(lua_State *L) {
#if CVUSB_MOUSE
    if (!mouse.there) {
        lua_pushnil(L);
        return 1;
    }
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &mouse) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 8);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &mouse);
    }
    lua_pushinteger(L, mouse.x); lua_setfield(L, -2, "x");
    lua_pushinteger(L, mouse.y); lua_setfield(L, -2, "y");
    lua_pushinteger(L, mouse.dx); lua_setfield(L, -2, "dx");
    lua_pushinteger(L, mouse.dy); lua_setfield(L, -2, "dy");
    lua_pushinteger(L, mouse.wheel); lua_setfield(L, -2, "wheel");
    lua_pushboolean(L, mouse.buttons & 1); lua_setfield(L, -2, "left");
    lua_pushboolean(L, mouse.buttons & 2); lua_setfield(L, -2, "right");
    lua_pushboolean(L, mouse.buttons & 4); lua_setfield(L, -2, "middle");
    return 1;
#else
    return off(L, "MOUSE");
#endif
}

// midi.send(byte, ...) or midi.send(string): whole messages. True if it went.
static int l_midi_send(lua_State *L) {
#if CVUSB_MIDI
    if (lua_type(L, 1) == LUA_TSTRING) {
        size_t n;
        const char *s = lua_tolstring(L, 1, &n);
        lua_pushboolean(L, cvusb_midi_send((const uint8_t *)s, (uint32_t)n));
        return 1;
    }
    uint8_t msg[16];
    const int n = lua_gettop(L);
    luaL_argcheck(L, n >= 1 && n <= (int)sizeof msg, 1, "1 to 16 bytes, or a string");
    for (int i = 0; i < n; i++) {
        const lua_Integer b = luaL_checkinteger(L, i + 1);
        luaL_argcheck(L, b >= 0 && b <= 255, i + 1, "a byte is 0 to 255");
        msg[i] = (uint8_t)b;
    }
    lua_pushboolean(L, cvusb_midi_send(msg, (uint32_t)n));
    return 1;
#else
    return off(L, "MIDI_IN or MIDI_OUT");
#endif
}

// usb.mode(): "off", "waiting", "asking", "device" or "host"
static int l_usb_mode(lua_State *L) {
#if CVLUA_USB
    static const char *const MODES[] = {"off", "waiting", "asking", "device", "host"};
    lua_pushstring(L, MODES[cvusb_mode()]);
#else
    lua_pushstring(L, "off");
#endif
    return 1;
}

// usb.host(): stop waiting for a computer and take pads, keyboards and mice now
static int l_usb_host(lua_State *L) {
    (void)L;
#if CVLUA_USB
    cvusb_host_now();
#endif
    return 0;
}

// usb.status(): what is plugged in
static int l_usb_status(lua_State *L) {
    lua_createtable(L, 0, 4);
#if CVLUA_USB
    cvusb_status_t s;
    cvusb_status(&s);
#else
    const struct { int pads, keyboards, mice; bool midi; } s = {0, 0, 0, false};
#endif
    lua_pushinteger(L, s.pads); lua_setfield(L, -2, "pads");
    lua_pushinteger(L, s.keyboards); lua_setfield(L, -2, "keyboards");
    lua_pushinteger(L, s.mice); lua_setfield(L, -2, "mice");
    lua_pushboolean(L, s.midi); lua_setfield(L, -2, "midi");
    return 1;
}

// ---- time, memory, the menu's note ---------------------------------------------------
static int l_time(lua_State *L) {
    lua_pushnumber(L, (lua_Number)(time_us_32() - rt_start_us()) / (lua_Number)1000000);
    return 1;
}

static int l_frame(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)rt_frame());
    return 1;
}

static int l_fps(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)rt_fps());
    return 1;
}

// mem(): bytes Lua is using, and bytes still free
static int l_mem(lua_State *L) {
    size_t su, ss, pu, ps;
    rt_memory(&su, &ss, &pu, &ps);
    lua_pushinteger(L, (lua_Integer)(su + pu));
    lua_pushinteger(L, (lua_Integer)(ss - su + ps - pu));
    return 2;
}

static int l_note_for_menu(lua_State *L) {
    cvk_note(luaL_checkstring(L, 1));
    return 0;
}

// ---- files ---------------------------------------------------------------------------
#define READ_MAX (4u * 1024u * 1024u)

static int card_error(lua_State *L, const char *path, const char *err) {
    return luaL_error(L, "%s: %s", path, err);
}

// the file at arg i, which must be there
static void find(lua_State *L, int i, char path[256], fs_file_t *e) {
    const char *name = luaL_checkstring(L, i);
    if (!rt_path(name, path, 256)) luaL_argerror(L, i, "the path is too long");
    const char *err = card_find(path, e);
    if (err) card_error(L, path, err);
    if (e->is_dir) card_error(L, path, "that's a folder");
}

// n bytes of a file from off, as a string on the stack
static const char *push_part(lua_State *L, const fs_file_t *e, uint32_t off, uint32_t n) {
    luaL_Buffer b;
    char *p = luaL_buffinitsize(L, &b, n ? n : 1);
    const char *err = card_read(e, off, (uint8_t *)p, n);
    luaL_pushresultsize(&b, err ? 0 : n);
    rt_clock_restart();
    if (err) lua_pop(L, 1);
    return err;
}

const char *rt_load_file(lua_State *L, const char *name) {
    if (name[0] != '/') {
        const cvlua_file_t *b = rt_builtin(name);
        if (b) {
            lua_pushlstring(L, (const char *)b->data, b->len);
            return NULL;
        }
    }
    char path[256];
    if (!rt_path(name, path, sizeof path)) return "the path is too long";
    fs_file_t e;
    const char *err = card_find(path, &e);
    if (err) return err;
    if (e.is_dir) return "that's a folder";
    if (e.size > READ_MAX) return "the file is over 4 MB";
    return push_part(L, &e, 0, (uint32_t)e.size);
}

// file.read(path [, offset [, count]]): the file's bytes as a string
static int l_file_read(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const lua_Integer off = luaL_optinteger(L, 2, 0);
    luaL_argcheck(L, off >= 0, 2, "an offset is 0 or more");
    const cvlua_file_t *b = name[0] != '/' ? rt_builtin(name) : NULL;
    if (b) {
        lua_Integer n = (lua_Integer)b->len - off;
        if (n < 0) n = 0;
        if (!lua_isnoneornil(L, 3) && luaL_checkinteger(L, 3) < n) n = lua_tointeger(L, 3);
        if (n < 0) n = 0;
        lua_pushlstring(L, (const char *)b->data + (n ? off : 0), (size_t)n);
        return 1;
    }
    char path[256];
    fs_file_t e;
    find(L, 1, path, &e);
    uint64_t n = (uint64_t)off < e.size ? e.size - (uint64_t)off : 0;
    if (!lua_isnoneornil(L, 3)) {
        const lua_Integer want = luaL_checkinteger(L, 3);
        luaL_argcheck(L, want >= 0, 3, "a count is 0 or more");
        if ((uint64_t)want < n) n = (uint64_t)want;
    }
    if (n > READ_MAX) return card_error(L, path, "over 4 MB at once: read it in parts");
    const char *err = push_part(L, &e, (uint32_t)off, (uint32_t)n);
    if (err) return card_error(L, path, err);
    return 1;
}

// file.size(path): its size in bytes, or nil if there's no such file
static int l_file_size(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const cvlua_file_t *b = name[0] != '/' ? rt_builtin(name) : NULL;
    if (b) {
        lua_pushinteger(L, (lua_Integer)b->len);
        return 1;
    }
    char path[256];
    if (!rt_path(name, path, sizeof path)) return luaL_argerror(L, 1, "the path is too long");
    fs_file_t e;
    const char *err = card_find(path, &e);
    if (err || e.is_dir || e.size > 0x7FFFFFFF) lua_pushnil(L);
    else lua_pushinteger(L, (lua_Integer)e.size);
    return 1;
}

#define LIST_MAX 500
typedef struct {
    lua_State *L;
    int n;
} list_ctx;

static bool list_one(const char *name, const fs_file_t *e, void *ctx) {
    list_ctx *c = ctx;
    lua_State *L = c->L;
    lua_createtable(L, 0, 3);
    lua_pushstring(L, name); lua_setfield(L, -2, "name");
    lua_pushinteger(L, e->size > 0x7FFFFFFF ? 0x7FFFFFFF : (lua_Integer)e->size); lua_setfield(L, -2, "size");
    lua_pushboolean(L, e->is_dir); lua_setfield(L, -2, "folder");
    lua_rawseti(L, -2, ++c->n);
    return c->n < LIST_MAX;
}

// file.list([folder]): its entries, each {name=, size=, folder=}
static int l_file_list(lua_State *L) {
    char path[256];
    if (!rt_path(luaL_optstring(L, 1, ""), path, sizeof path)) return luaL_argerror(L, 1, "the path is too long");
    lua_newtable(L);
    list_ctx c = {L, 0};
    const char *err = card_list(path, list_one, &c);
    rt_clock_restart();
    if (err) return card_error(L, path, err);
    return 1;
}

// file.create(path, size): makes the file, full of zeros, if it isn't there.
// True if it made it.
static int l_file_create(lua_State *L) {
    char path[256];
    if (!rt_path(luaL_checkstring(L, 1), path, sizeof path)) return luaL_argerror(L, 1, "the path is too long");
    const lua_Integer size = luaL_checkinteger(L, 2);
    luaL_argcheck(L, size >= 1 && size <= (lua_Integer)CARD_CREATE_MAX, 2, "a new file is 1 byte to 1 MB");
    fs_file_t e;
    bool made;
    const char *err = card_create(path, (uint32_t)size, &e, &made);
    rt_clock_restart();
    if (err) return card_error(L, path, err);
    lua_pushboolean(L, made);
    return 1;
}

// file.write(path, offset, data): over the bytes that are there. A file
// can't grow.
static int l_file_write(lua_State *L) {
    char path[256];
    fs_file_t e;
    find(L, 1, path, &e);
    const lua_Integer off = luaL_checkinteger(L, 2);
    size_t n;
    const char *data = luaL_checklstring(L, 3, &n);
    luaL_argcheck(L, off >= 0, 2, "an offset is 0 or more");
    if ((uint64_t)off + n > e.size) return card_error(L, path, "the write goes past the file's end, and a file can't grow");
    const char *err = card_write(&e, (uint32_t)off, (const uint8_t *)data, (uint32_t)n);
    rt_clock_restart();
    if (err) return card_error(L, path, err);
    return 0;
}

// file.save(path, text [, room]) and file.load(path): a string kept in a
// file of its own. The file is made with room for `room` bytes (4000 unless
// given) and never grows: 8 bytes of header ("LUASAVE1" would be nicer to
// read, but the length has to be somewhere), then the text.
#define SAVE_MAGIC "LSV1"
#define SAVE_HEAD  8

static int l_file_save(lua_State *L) {
    char path[256];
    if (!rt_path(luaL_checkstring(L, 1), path, sizeof path)) return luaL_argerror(L, 1, "the path is too long");
    size_t n;
    const char *text = luaL_checklstring(L, 2, &n);
    const lua_Integer room = luaL_optinteger(L, 3, 4000);
    luaL_argcheck(L, room >= 1 && room <= (lua_Integer)CARD_CREATE_MAX - SAVE_HEAD, 3, "the room is 1 byte to 1 MB");
    fs_file_t e;
    bool made;
    const char *err = card_create(path, (uint32_t)room + SAVE_HEAD, &e, &made);
    if (err) return card_error(L, path, err);
    if (n + SAVE_HEAD > e.size) return luaL_error(L, "%s: the save holds %d bytes, and this is %d", path, (int)(e.size - SAVE_HEAD), (int)n);
    uint8_t headb[SAVE_HEAD] = {'L', 'S', 'V', '1', (uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
    // the text first, its length last: a write cut short leaves the old length
    err = card_write(&e, SAVE_HEAD, (const uint8_t *)text, (uint32_t)n);
    if (!err) err = card_write(&e, 0, headb, SAVE_HEAD);
    rt_clock_restart();
    if (err) return card_error(L, path, err);
    return 0;
}

static int l_file_load(lua_State *L) {
    char path[256];
    if (!rt_path(luaL_checkstring(L, 1), path, sizeof path)) return luaL_argerror(L, 1, "the path is too long");
    fs_file_t e;
    uint8_t headb[SAVE_HEAD];
    if (card_find(path, &e) || e.is_dir || e.size < SAVE_HEAD || card_read(&e, 0, headb, SAVE_HEAD) ||
        memcmp(headb, SAVE_MAGIC, 4)) {
        lua_pushnil(L);                 // no save yet (or not one of ours)
        return 1;
    }
    const uint32_t n = headb[4] | (uint32_t)headb[5] << 8 | (uint32_t)headb[6] << 16 | (uint32_t)headb[7] << 24;
    if (n + SAVE_HEAD > e.size) {
        lua_pushnil(L);
        return 1;
    }
    const char *err = push_part(L, &e, SAVE_HEAD, n);
    if (err) return card_error(L, path, err);
    return 1;
}

// ---- more scripts --------------------------------------------------------------------
// loadfile() and dofile() read from the card (lua_libs.c points Lua's at this)
int cvlua_loadfilex(lua_State *L, const char *filename, const char *mode) {
    (void)mode;
    if (!filename) {
        lua_pushliteral(L, "a file name, please");
        return LUA_ERRFILE;
    }
    const char *err = rt_load_file(L, filename);
    if (err) {
        lua_pushfstring(L, "%s: %s", filename, err);
        return LUA_ERRFILE;
    }
    size_t n;
    const char *s = lua_tolstring(L, -1, &n);
    lua_pushfstring(L, "=%s", filename);
    const int r = luaL_loadbufferx(L, s, n, lua_tostring(L, -1), "t");
    lua_remove(L, -2);                  // the chunk's name
    lua_remove(L, -2);                  // the source
    return r;
}

// require "name": name.lua beside the script (or built in), run once
static int l_require(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    lua_settop(L, 1);
    luaL_getsubtable(L, LUA_REGISTRYINDEX, "cvlua.loaded");     // 2
    if (lua_getfield(L, 2, name) != LUA_TNIL) return 1;
    lua_pop(L, 1);
    char file[80];
    size_t n = strlen(name);
    if (n > sizeof file - 5) return luaL_argerror(L, 1, "the name is too long");
    for (size_t i = 0; i < n; i++) file[i] = name[i] == '.' ? '/' : name[i];
    strcpy(file + n, ".lua");
    if (cvlua_loadfilex(L, file, NULL) != LUA_OK) return luaL_error(L, "require \"%s\": %s", name, lua_tostring(L, -1));
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, 2, name);
    return 1;
}

// ---- the script's own functions for what USB brings ---------------------------------
#if CVUSB_KEYBOARD || CVUSB_MIDI
// the global `name` called with the n values on the stack, if it's a function
static void event(lua_State *L, const char *name, int n) {
    if (lua_getglobal(L, name) != LUA_TFUNCTION) {
        lua_pop(L, n + 1);
        return;
    }
    lua_insert(L, -(n + 1));
    lua_call(L, n, 0);
}
#endif

void api_tick(lua_State *L) {
    (void)L;
#if CVUSB_KEYBOARD
    cvkey_t k;
    while (rt_next_key(&k)) {
        if (k.down) key_held[k.usage >> 3] |= (uint8_t)(1u << (k.usage & 7));
        else key_held[k.usage >> 3] &= (uint8_t)~(1u << (k.usage & 7));
        char name[12];
        key_name(k.usage, name);
        lua_pushstring(L, name);
        if (k.down && k.ascii) {
            const char s[2] = {k.ascii == '\r' ? '\n' : k.ascii, 0};
            lua_pushstring(L, s);
        } else lua_pushnil(L);
        event(L, k.down ? "keypressed" : "keyreleased", 2);
    }
#endif
#if CVUSB_MOUSE
    cvmouse_t m;
    mouse.there = cvusb_mouse(&m);
    if (mouse.there) {
        mouse.dx = (int)m.dx;
        mouse.dy = (int)m.dy;
        mouse.wheel = (int)m.wheel;
        mouse.buttons = m.buttons;
        mouse.x += mouse.dx;
        mouse.y += mouse.dy;
        if (mouse.x < 0) mouse.x = 0;
        if (mouse.x > VDP_W - 1) mouse.x = VDP_W - 1;
        if (mouse.y < 0) mouse.y = 0;
        if (mouse.y > VDP_H - 1) mouse.y = VDP_H - 1;
    }
#endif
#if CVUSB_MIDI
    uint8_t msg[3];
    int n;
    while ((n = cvusb_midi_read(msg)) > 0) {
        for (int i = 0; i < 3; i++) {
            if (i < n) lua_pushinteger(L, msg[i]);
            else lua_pushnil(L);
        }
        event(L, "midimessage", 3);
    }
#endif
}

// The sound goes out when it changes, and every 6 ticks (0.1 s) anyway: the
// console misreads a byte from the cart now and then, and a sound register
// stays wrong until it's written again, so a sound sent once could drone
// until RESET.
void api_tick_end(void) {
    static uint8_t snd_age;             // ticks since the sound last went out
    for (int ch = 0; ch < 4; ch++)
        if (snd_left[ch] && --snd_left[ch] == 0) voice_off(ch);
    if (snd_dirty || ++snd_age >= 6) {
        snd_dirty = false;
        snd_age = 0;
        cvk_sound(snd);
    }
}

void api_reset(void) {
    pen = C_WHITE;
    print_y = 0;
    memset(spr, 0, sizeof spr);
    memset(spr_pat, 0, sizeof spr_pat);
    spr_big = spr_zoom = false;
    memcpy(snd, cvk_silence, sizeof snd);
    memset(snd_left, 0, sizeof snd_left);
    snd_dirty = false;
    cvk_sound(snd);
#if CVUSB_KEYBOARD
    memset(key_held, 0, sizeof key_held);
#endif
#if CVUSB_MOUSE
    memset(&mouse, 0, sizeof mouse);
    mouse.x = VDP_W / 2;
    mouse.y = VDP_H / 2;
#endif
}

// ---- the globals ---------------------------------------------------------------------
static const luaL_Reg GLOBALS[] = {
    {"cls", l_cls}, {"pset", l_pset}, {"pget", l_pget}, {"line", l_line}, {"rect", l_rect},
    {"rectfill", l_rectfill}, {"circ", l_circ}, {"circfill", l_circfill}, {"print", l_print},
    {"shape", l_shape}, {"sprite", l_sprite}, {"zoom", l_zoom},
    {"image", l_image}, {"blit", l_blit},
    {"tone", l_tone}, {"note", l_note}, {"noise", l_noise}, {"mute", l_mute},
    {"btn", l_btn}, {"btnp", l_btnp}, {"keypad", l_keypad}, {"keypadp", l_keypadp},
    {"pad", l_pad}, {"key", l_key}, {"mouse", l_mouse},
    {"time", l_time}, {"frame", l_frame}, {"fps", l_fps}, {"mem", l_mem}, {"menu_note", l_note_for_menu},
    {"require", l_require},
    {NULL, NULL}};
static const luaL_Reg FILE_LIB[] = {
    {"read", l_file_read}, {"size", l_file_size}, {"list", l_file_list}, {"create", l_file_create},
    {"write", l_file_write}, {"save", l_file_save}, {"load", l_file_load}, {NULL, NULL}};
static const luaL_Reg MIDI_LIB[] = {{"send", l_midi_send}, {NULL, NULL}};
static const luaL_Reg USB_LIB[] = {{"mode", l_usb_mode}, {"host", l_usb_host}, {"status", l_usb_status}, {NULL, NULL}};

void api_open(lua_State *L) {
    (void)off;                          // (unused with every feature on)
    static const char *const COLOURS[16] = {
        "CLEAR", "BLACK", "GREEN", "LIGHT_GREEN", "DARK_BLUE", "BLUE", "DARK_RED", "CYAN", "RED",
        "LIGHT_RED", "DARK_YELLOW", "YELLOW", "DARK_GREEN", "MAGENTA", "GRAY", "WHITE"};
    lua_pushglobaltable(L);
    luaL_setfuncs(L, GLOBALS, 0);
    for (int i = 0; i < 16; i++) {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, COLOURS[i]);
    }
    lua_pushinteger(L, VDP_W); lua_setfield(L, -2, "WIDTH");
    lua_pushinteger(L, VDP_H); lua_setfield(L, -2, "HEIGHT");
    luaL_newlib(L, FILE_LIB); lua_setfield(L, -2, "file");
    luaL_newlib(L, MIDI_LIB); lua_setfield(L, -2, "midi");
    luaL_newlib(L, USB_LIB); lua_setfield(L, -2, "usb");
    lua_pop(L, 1);

    luaL_newmetatable(L, IMAGE);
    lua_pushcfunction(L, image_index);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}
