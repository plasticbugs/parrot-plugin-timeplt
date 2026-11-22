// main.c -- the Parrot Cart plugin simulator: a plugin, built for the
// desktop, in a window, with sound and controls (docs/simulator.md).
//
// The plugin runs on the main thread, as on the cart's core 0, and calls
// the SDK as it always does. The SDK's calls are host.c's models
// (model.c), whose clock moves on with each call the plugin makes. Each
// time that clock passes a TV frame, the models run the frame's list and
// call sim_on_frame() here, which draws the TV, plays that frame's sound,
// reads the keyboard and pads, and waits until the frame's time has come
// on the desktop too. So the plugin sees the console's 59.94 frames a
// second, whatever the desktop's speed.
#include "sim.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "vdp.h"

#if defined(__SANITIZE_ADDRESS__)
#define SIM_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SIM_ASAN 1
#endif
#endif
#ifdef SIM_ASAN
// the plugin's own bugs, yes; SDL's and the system's allocations at exit, no
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

#define FRAME_NS 16683000ull                // a TV frame, as host.c's FRAME_US
#define TV_W 280                            // --tv: the picture with the border a TV shows
#define TV_H 240
#define TV_X 12                             // (MAME's ColecoVision has the same sides)
#define TV_Y 24

sim_opts_t opt;
static char **args;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static bool ending, quitting, strict_stop, midi_list_only;
static int mark_frames, label_frames, fails_seen, shown_fails = -1, shown_role = -1;
static uint64_t frames;

// ---- usage -------------------------------------------------------------------------------
static const char KEYS[] =
    "Keys:\n"
    "  arrows or WASD   the console's stick\n"
    "  Z, X             its left and right fire buttons\n"
    "  0-9, -, =        its keypad: the digits, * and #\n"
    "  game pads        the first is the console's controller too (d-pad or left\n"
    "                   stick, face buttons as fire, Back and Start as * and #); the\n"
    "                   first two are the plugin's USB pads 1 and 2\n"
    "  F2               the keyboard's role: the controller, a USB pad, or a USB\n"
    "                   keyboard and mouse (then every key but F1, F2, F5 and F12 is\n"
    "                   the plugin's, Esc too, and the window keeps the mouse)\n"
    "  F5               the console's RESET: the plugin ends, the menu's note shows\n"
    "  F12              a screenshot, 256x192, the size the plugin registry wants\n"
    "  F1               these keys\n"
    "  Esc              quit (closing the window too)\n"
    "In the USB pad role: arrows the d-pad; Z X A S south, east, west, north; Q W L\n"
    "and R; E R ZL and ZR; Enter + (Start); Backspace - (Back); H Home; I J K L the\n"
    "left stick.\n";

static void usage(FILE *f) {
    fprintf(f,
            "parrotsim -- a Parrot Cart plugin on this computer (sim/run.sh builds it)\n"
            "\n"
            "  --card PATH      the SD card: a folder (as a FAT32 card; writes land in it)\n"
            "                   or a disk image\n"
            "  --open PATH      start as if PATH had been picked in the menu: a file on the\n"
            "                   card (\"/NES/game.nes\"), or on this computer (its folder is\n"
            "                   then the card, unless --card says otherwise)\n"
            "  --id ID          the plugin's ID in the launch block (default: its\n"
            "                   CMakeLists.txt's)\n"
            "  --no-launch      start with no launch block, as from picotool\n"
            "  --name NAME      the window's title (default: the plugin's name)\n"
            "  --scale N        window pixels to a console pixel (default 3)\n"
            "  --tv             the TV's shape: the border, and the picture 4:3\n"
            "  --mute           no sound\n"
            "  --strict         stop at the first rule the plugin breaks\n"
            "  --shots DIR      where F12 saves screenshots (default: the current folder)\n"
            "  --out DIR        log.txt and screen.png (default: out/ beside the program)\n"
            "  --lists          also lists.bin there, for tools/mame-replay.sh\n"
            "  --no-psram       as a cart whose PSRAM doesn't answer\n"
            "\n"
            "The USB port and MIDI (the plugin's cvcart_features()):\n"
            "  --usb WHAT       what is on the cart's USB port, for a plugin with MIDI_OUT:\n"
            "                   device (a computer that takes the cart: the default), host\n"
            "                   (a hub: the port looks for a computer for 1.5 s, then is a\n"
            "                   host), ask (a computer that asks its user first)\n"
            "  --midi-in NAME   read only this MIDI input (any part of its name); all (the\n"
            "                   default with MIDI_IN) or none\n"
            "  --midi-out NAME  send the plugin's MIDI to this port; virtual (the default\n"
            "                   with MIDI_OUT, not on Windows: a port named after the\n"
            "                   plugin, both ways, as the cart on a computer) or none\n"
            "  --midi-log FILE  what the plugin sends, a line a message: its time in ms\n"
            "                   and its bytes\n"
            "  --midi-list      list this computer's MIDI ports, and stop\n"
            "\n"
            "Headless, for tests (no window, no sound, no MIDI port unless --midi-in or\n"
            "--midi-out names one; it always ends by itself):\n"
            "  --headless       on: SDL's dummy video and audio, lists.bin kept\n"
            "  --frames N       end after N TV frames (default 600: 10 seconds)\n"
            "  --ms N           end after N milliseconds of console time (as PC_END)\n"
            "  --script S       inputs, in test/host's PC_SCRIPT language: \"500:joy=up 900:key=5\";\n"
            "                   and the simulator's own: simkey=, simmouse=, simpad= ...\n"
            "                   (docs/simulator.md)\n"
            "  --shot FILE.png  the last picture\n"
            "  --wav FILE.wav   the sound (48 kHz, 16-bit, mono)\n"
            "\n"
            "The end of a session is the console's RESET: the plugin's on-exit runs, the\n"
            "card is brought up to date, and the window shows the note the plugin left for\n"
            "the menu. F5 or fire there starts the plugin again; Esc quits.\n"
            "\n"
            "%s",
            KEYS);
}

static const char *need(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) { fprintf(stderr, "%s needs a value (--help)\n", argv[*i]); exit(2); }
    return argv[++*i];
}

static long number(const char *opt_name, const char *v, long lo, long hi) {
    char *end;
    const long n = strtol(v, &end, 10);
    if (*end || n < lo || n > hi) { fprintf(stderr, "%s: %s isn't a number from %ld to %ld\n", opt_name, v, lo, hi); exit(2); }
    return n;
}

static void parse(int argc, char **argv) {
    opt.scale = 3;
    opt.end_us = -2;                        // (not given)
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); exit(0); }
        else if (!strcmp(a, "--card")) opt.card = need(argc, argv, &i);
        else if (!strcmp(a, "--open")) opt.open = need(argc, argv, &i);
        else if (!strcmp(a, "--id")) opt.id = need(argc, argv, &i);
        else if (!strcmp(a, "--no-launch")) opt.no_launch = true;
        else if (!strcmp(a, "--name")) opt.name = need(argc, argv, &i);
        else if (!strcmp(a, "--scale")) opt.scale = (int)number(a, need(argc, argv, &i), 1, 16);
        else if (!strcmp(a, "--tv")) opt.tv = true;
        else if (!strcmp(a, "--mute")) opt.mute = true;
        else if (!strcmp(a, "--strict")) opt.strict = true;
        else if (!strcmp(a, "--shots")) opt.shots = need(argc, argv, &i);
        else if (!strcmp(a, "--out")) opt.out = need(argc, argv, &i);
        else if (!strcmp(a, "--lists")) opt.lists = true;
        else if (!strcmp(a, "--no-psram")) opt.no_psram = true;
        else if (!strcmp(a, "--headless")) opt.headless = true;
        else if (!strcmp(a, "--frames")) opt.end_us = number(a, need(argc, argv, &i), 1, 100000000) * 16683;
        else if (!strcmp(a, "--ms")) opt.end_us = number(a, need(argc, argv, &i), 1, 2000000000) * 1000;
        else if (!strcmp(a, "--script")) opt.script = need(argc, argv, &i);
        else if (!strcmp(a, "--shot")) opt.shot = need(argc, argv, &i);
        else if (!strcmp(a, "--wav")) opt.wav = need(argc, argv, &i);
        else if (!strcmp(a, "--usb")) {
            const char *v = need(argc, argv, &i);
            if (!strcmp(v, "device")) opt.usb = USB_DEVICE;
            else if (!strcmp(v, "host")) opt.usb = USB_HOST;
            else if (!strcmp(v, "ask")) opt.usb = USB_ASK;
            else { fprintf(stderr, "--usb: device, host or ask\n"); exit(2); }
        }
        else if (!strcmp(a, "--midi-in")) opt.midi_in = need(argc, argv, &i);
        else if (!strcmp(a, "--midi-out")) opt.midi_out = need(argc, argv, &i);
        else if (!strcmp(a, "--midi-log")) opt.midi_log = need(argc, argv, &i);
        else if (!strcmp(a, "--midi-list")) midi_list_only = true;
        else { fprintf(stderr, "what is %s? (--help)\n", a); exit(2); }
    }
    if (opt.end_us == -2) opt.end_us = opt.headless ? 600 * 16683 : -1;
    if (opt.headless) {
        opt.lists = true;
        opt.mute = false;                   // (the dummy driver: nothing is heard)
    }
    if (!opt.shots) opt.shots = ".";
    static char id[] = SIM_PLUGIN_ID, name[] = SIM_PLUGIN_NAME;   // (CMakeLists.txt)
    if (!opt.name) opt.name = name[0] ? name : "Parrot Cart plugin";
    if (!opt.id && id[0]) opt.id = id;
}

// ---- the window ------------------------------------------------------------------------------
static uint32_t rgb(int c) { return (uint32_t)vdp_rgb[c & 15][0] << 16 | (uint32_t)vdp_rgb[c & 15][1] << 8 | vdp_rgb[c & 15][2]; }

static void present(uint8_t pic[SIM_H][SIM_W], uint8_t backdrop) {
    static uint32_t px[TV_H * TV_W];
    if (!renderer) return;
    const int w = opt.tv ? TV_W : SIM_W, h = opt.tv ? TV_H : SIM_H, x0 = opt.tv ? TV_X : 0, y0 = opt.tv ? TV_Y : 0;
    if (opt.tv)
        for (int i = 0; i < TV_W * TV_H; i++) px[i] = rgb(backdrop);
    for (int y = 0; y < SIM_H; y++)
        for (int x = 0; x < SIM_W; x++) px[(y + y0) * w + x + x0] = rgb(pic[y][x]);
    SDL_UpdateTexture(texture, NULL, px, w * 4);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, NULL, NULL);
    if (mark_frames > 0) {                  // a rule broken lately: a red mark in the corner
        mark_frames--;
        const float lw = opt.tv ? 320.0f : (float)SIM_W;
        const SDL_FRect r = {lw - 10, 2, 8, 8};
        SDL_SetRenderDrawColor(renderer, 252, 85, 84, 255);
        SDL_RenderFillRect(renderer, &r);
    }
    if (label_frames > 0) {                 // the keyboard's role, for a moment after it changes
        label_frames--;
        char t[64];
        snprintf(t, sizeof t, "F2 KEYS: %s", input_role_label());
        const float lh = opt.tv ? 240.0f : (float)SIM_H;
        const SDL_FRect r = {0, lh - 11, 8.0f * (float)strlen(t) + 4, 11};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, &r);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer, 2, lh - 9, t);
    }
    (void)h;
    SDL_RenderPresent(renderer);
}

static void set_title(void) {
    if (!window || (shown_fails == fails_seen && shown_role == input_role())) return;
    shown_fails = fails_seen;
    shown_role = input_role();
    char t[512];
    if (fails_seen)
        snprintf(t, sizeof t, "%s - keys: %s (F2) - %d problem%s (see the terminal) - F1 help", opt.name, input_role_name(),
                 fails_seen, fails_seen == 1 ? "" : "s");
    else snprintf(t, sizeof t, "%s - keys: %s (F2) - F1 help", opt.name, input_role_name());
    SDL_SetWindowTitle(window, t);
}

static void video_open(void) {
    const int lw = opt.tv ? 320 : SIM_W, lh = opt.tv ? 240 : SIM_H;
    int scale = opt.headless ? 1 : opt.scale;
    SDL_Rect usable;
    if (!opt.headless && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        while (scale > 1 && (lw * scale > usable.w || lh * scale + 40 > usable.h)) scale--;
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (opt.headless ? SDL_WINDOW_HIDDEN : 0);
    window = SDL_CreateWindow(opt.name, lw * scale, lh * scale, flags);
    if (window) renderer = SDL_CreateRenderer(window, NULL);
    if (renderer) texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                              opt.tv ? TV_W : SIM_W, opt.tv ? TV_H : SIM_H);
    if (!texture) {
        fprintf(stderr, "no window: %s\n", SDL_GetError());
        if (!opt.headless) exit(2);
        renderer = NULL;
        return;
    }
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_SetTextureScaleMode(texture, opt.tv ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_NEAREST);
#else
    SDL_SetTextureScaleMode(texture, opt.tv ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
#endif
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, opt.tv ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    shown_fails = -1;
    set_title();
}

// ---- each TV frame -------------------------------------------------------------------------
static void pace_wall(void) {
    static uint64_t next;
    const uint64_t now = SDL_GetTicksNS();
    if (!next || now > next + 100000000ull) next = now;     // far behind (a slow step): from now on
    next += FRAME_NS;
    if (next > now) SDL_DelayPrecise(next - now);
}

static void screenshot(void) {
    static uint8_t pic[SIM_H][SIM_W];
    char path[4096];
    for (int n = 1; n < 10000; n++) {
        SDL_PathInfo info;
        snprintf(path, sizeof path, "%s/screenshot-%d.png", opt.shots, n);
        if (!SDL_GetPathInfo(path, &info)) break;
    }
    model_picture(pic);
    model_png(path, pic);
    printf("screenshot: %s\n", path);
}

void sim_on_frame(uint64_t t_us) {
    static uint8_t pic[SIM_H][SIM_W];
    if (ending) return;
    frames++;
    audio_frame(t_us);
    midi_poll();
    if (strict_stop) {
        printf("--strict: stopped at the first broken rule, %.0f ms in\n", (double)t_us / 1e3);
        model_end();
    }
    if (opt.headless) {
        if (window) {
            sim_keys_t k;
            input_poll(&k);                 // the script's made-up devices (sim* words), if any
            if (k.quit) { quitting = true; model_end(); }
            model_picture(pic);
            present(pic, model_backdrop());
        }
        if (midi_ports_open()) pace_wall();     // MIDI with the world: at the console's own speed
        return;
    }
    model_picture(pic);
    present(pic, model_backdrop());
    sim_keys_t k;
    input_poll(&k);
    if (k.help) {
        printf("%s", KEYS);
        if (midi_summary()[0]) printf("%s\n", midi_summary());
    }
    if (k.role_next) {
        input_set_role(input_role() + 1);
        label_frames = 180;
    }
    if (k.shot) screenshot();
    if (k.quit) { quitting = true; model_end(); }
    if (k.reset) {
        printf("RESET\n");
        model_end();
    }
    set_title();
    if (audio_live()) audio_pace();
    else pace_wall();
}

// the script's words for the desktop's devices: simkey=, simmouse=, simpad= ... (input.c)
bool sim_on_event(const char *what, const char *v) {
    if (strncmp(what, "sim", 3)) return false;
    if (!window) {
        fprintf(stderr, "PC_SCRIPT: %s= needs SDL's dummy video, and there is none here\n", what);
        exit(2);
    }
    return input_script(what + 3, v);
}

void sim_on_fail(void) {
    fails_seen++;
    mark_frames = 60;
    if (opt.strict) strict_stop = true;
}

// ---- the end ------------------------------------------------------------------------------
static void restart(void) {
    fflush(NULL);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
#ifdef _WIN32
    SDL_Process *p = SDL_CreateProcess((const char *const *)args, false);
    int code = 2;
    if (!p) fprintf(stderr, "can't start again: %s\n", SDL_GetError());
    else SDL_WaitProcess(p, true, &code);
    exit(code);
#else
    SDL_Quit();
    execv(args[0], args);
    execvp(args[0], args);
    fprintf(stderr, "can't start again: %s\n", strerror(errno));
    exit(2);
#endif
}

// RESET's screen: the plugin's note on the bottom line, as the menu shows it
static void menu(void) {
    static canvas_t c;
    canvas_init(&c);
    canvas_text(&c, 24, 24, "PARROT CART SIMULATOR", C_LYELLOW, 1);
    canvas_text(&c, 24, 56, "RESET: THE PLUGIN HAS ENDED,", C_WHITE, 1);
    canvas_text(&c, 24, 68, "AND IS BACK IN THE MENU.", C_WHITE, 1);
    canvas_text(&c, 24, 100, "F5 OR FIRE: START IT AGAIN", C_LGREEN, 1);
    canvas_text(&c, 24, 112, "ESC: QUIT", C_LGREEN, 1);
    canvas_text(&c, 24, 144, "THE MENU'S BOTTOM LINE:", C_GRAY, 1);
    canvas_fill(&c, 0, 176, SIM_W, 16, C_DBLUE);
    canvas_text(&c, 0, 180, model_note()[0] ? model_note() : "(NO NOTE)", C_WHITE, 1);
    bool armed = false;                     // fire must be let go of first
    for (;;) {
        present(c.px, C_BLACK);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) exit(model_fails() ? 1 : 0);
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) exit(model_fails() ? 1 : 0);
                if (e.key.scancode == SDL_SCANCODE_F5) restart();
            }
        }
        const bool start = input_start_pressed();
        if (start && armed) restart();
        if (!start) armed = true;
        SDL_Delay(20);
    }
}

void sim_on_end(int fails) {
    static uint8_t pic[SIM_H][SIM_W];
    ending = true;
    card_sync();
    audio_close();
    midi_close();
    if (opt.shot) {
        model_picture(pic);
        model_png(opt.shot, pic);
    }
    if (opt.headless) {
        printf("  out: %s (log.txt, screen.png, lists.bin)%s%s%s%s\n", opt.out, opt.shot ? ", " : "", opt.shot ? opt.shot : "",
               opt.wav ? ", " : "", opt.wav ? opt.wav : "");
        return;                             // host.c exits: 1 if a rule was broken
    }
    if (window) SDL_SetWindowRelativeMouseMode(window, false);
    printf("the menu's bottom line: \"%s\"\n", model_note());
    if (quitting || strict_stop) exit(fails ? 1 : 0);
    menu();
}

// ---- the start --------------------------------------------------------------------------------
static void headless_sdl(void) {
    // never a window, never a sound, whatever the environment says
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    // game pads: only the script's made-up ones (simpad=), never this computer's.
    // Its pad drivers off, where SDL has a switch for them (by name: an SDL
    // that doesn't know one leaves it be), and input.c ignores any other pad
    static const char *const pad_drivers[] = {"SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_IOKIT", "SDL_JOYSTICK_MFI",
                                              "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_JOYSTICK_GAMEINPUT",
                                              "SDL_JOYSTICK_DIRECTINPUT", "SDL_XINPUT_ENABLED"};
    for (size_t i = 0; i < sizeof pad_drivers / sizeof pad_drivers[0]; i++)
        SDL_SetHintWithPriority(pad_drivers[i], "0", SDL_HINT_OVERRIDE);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   // (a hidden window never has the focus)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "headless: no SDL video (%s): pictures from the models only\n", SDL_GetError());
        return;
    }
    const char *vd = SDL_GetCurrentVideoDriver(), *ad = SDL_GetCurrentAudioDriver();
    if (!vd || strcmp(vd, "dummy") || !ad || strcmp(ad, "dummy")) {
        fprintf(stderr, "headless: SDL chose %s video and %s audio; stopping rather than show or play anything\n", vd ? vd : "no",
                ad ? ad : "no");
        exit(2);
    }
    atexit(SDL_Quit);
    video_open();
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    args = argv;
    parse(argc, argv);
    if (midi_list_only) {
        midi_list();
        return 0;
    }
    static char out[4096];
    if (!opt.out) {
        const char *base = SDL_GetBasePath();
        snprintf(out, sizeof out, "%sout", base ? base : "./");
        opt.out = out;
    }
    SDL_CreateDirectory(opt.out);
    if (opt.open) opt.open = card_resolve_open(opt.open, &opt.card);
    if (opt.card && !card_open(opt.card)) return 2;
    if (opt.headless) headless_sdl();
    else {
        SDL_SetHint(SDL_HINT_APP_NAME, opt.name);
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 2; }
        atexit(SDL_Quit);
        if (!opt.mute && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            fprintf(stderr, "no sound: %s\n", SDL_GetError());
            opt.mute = true;
        }
        video_open();
        input_init(window);
        printf("%s on the Parrot Cart simulator. F1 shows the keys; the keyboard is the %s.\n", opt.name, input_role_name());
        label_frames = 180;
    }
    audio_open();
    midi_open();
    model_init();
    model_run();
    return 0;
}
