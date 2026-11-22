// input.c -- the desktop's keyboard, mouse and game pads, as what the
// console and the cart's USB port would have plugged in.
//
// Every TV frame, input_poll() reads SDL's events and state and hands the
// models (model.c) what each device holds now:
//
//   the console's controller 1   the keyboard (arrows or WASD, Z and X,
//                                the number keys, - and =), and the first
//                                game pad (d-pad or left stick, face
//                                buttons, Back and Start as * and #)
//   USB pads 1 and 2 (PADS)      the first two game pads, each in the slot
//                                it took when it came, as cvpad.c's slots;
//                                all their buttons, both sticks, and what
//                                they are (a Switch Pro Controller, or a
//                                plain HID pad). Pad 1 is also the keyboard,
//                                in its "USB pad" role
//   a USB keyboard and mouse     the keyboard and mouse, in their "USB
//   (KEYBOARD, MOUSE)            keyboard and mouse" role: every key as a
//                                keyboard's boot report (SDL's scancodes
//                                are HID's usages), read by the SDK's own
//                                cvusb_keys.c; the mouse's movement, buttons
//                                and wheel, in relative mouse mode
//
// F2 moves the keyboard between those roles. The simulator keeps the F
// keys (F1 help, F2, F5 RESET, F12 screenshot) and, outside the USB
// keyboard role, Esc (quit).
//
// Headless, a script's sim* words (input_script) make these devices up:
// keys and the mouse as SDL events, game pads as SDL's virtual ones. They
// go through everything here as a window's would, for sim/test.sh.
#include "sim.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cvusb.h"                          // the plugin's CVUSB_* switches
#include "wired.h"

#define SLOTS CVUSB_PAD_MAX
static SDL_Window *win;
static SDL_Gamepad *slot[SLOTS];            // USB pads 1 and 2
static int role = ROLE_CONSOLE;
static int last_key = -1;                   // the keypad key pressed last on the keyboard
static uint8_t mouse_held, mouse_buttons;   // the mouse's buttons, by their events; and as last sent
static bool scripted;                       // headless: the script made devices up (input_script)

static const char *const role_names[ROLE_COUNT] = {"controller", "USB pad 1", "USB keyboard and mouse"};
static const char *const role_labels[ROLE_COUNT] = {"THE CONTROLLER", "USB PAD 1", "USB KEYBOARD AND MOUSE"};

int input_role(void) { return role; }
const char *input_role_name(void) { return role_names[role]; }
const char *input_role_label(void) { return role_labels[role]; }

// the USB keyboard and mouse let go of everything (the role changed, or the window lost the keys)
static void usb_let_go(void) {
    model_usb_keys_up();
    if (mouse_buttons) model_usb_mouse(0, 0, 0, 0);
    mouse_held = mouse_buttons = 0;
}

void input_set_role(int r) {
    if (role == ROLE_USB_KEYBOARD) usb_let_go();    // leaving it: nothing stays held
    role = r % ROLE_COUNT;
    if (win) SDL_SetWindowRelativeMouseMode(win, role == ROLE_USB_KEYBOARD);
    if (role == ROLE_USB_KEYBOARD) model_usb_mouse(0, 0, 0, 0);   // plugged in
    printf("keyboard: %s%s\n", role_names[role],
           role == ROLE_USB_KEYBOARD ? " (the mouse is the USB mouse; F2 lets it go)" : "");
    // why the plugin may not see them
    if (role == ROLE_USB_PAD && !CVUSB_PADS)
        printf("  (the plugin doesn't turn on PADS in cvcart_features(): it may not read a USB pad)\n");
    if (role == ROLE_USB_KEYBOARD && !CVUSB_KEYBOARD && !CVUSB_MOUSE)
        printf("  (the plugin turns on neither KEYBOARD nor MOUSE in cvcart_features(): it may not read them)\n");
    if (role != ROLE_CONSOLE && (CVUSB_PADS || CVUSB_KEYBOARD || CVUSB_MOUSE) && model_usb_mode() != CVUSB_HOST) {
        const int m = model_usb_mode();
        printf("  (the cart's USB port isn't a host now: %s. Pads, keyboards and mice\n"
               "   reach the plugin only through a host%s)\n",
               m == CVUSB_NONE ? "the plugin hasn't started it" : m == CVUSB_WAITING ? "it is still looking for a computer"
               : m == CVUSB_ASKING ? "a computer is asking its user" : "a computer has it, for MIDI",
               m == CVUSB_DEVICE || m == CVUSB_ASKING ? ". --usb host starts it as one, with no computer" : "");
    }
}

static bool is_switch(SDL_Gamepad *p) {
    switch (SDL_GetGamepadType(p)) {
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return true;
    default:
        return false;
    }
}

static void pad_added(SDL_JoystickID id) {
    if (opt.headless && !SDL_IsJoystickVirtual(id)) return;    // headless: the script's pads only
    for (int i = 0; i < SLOTS; i++)
        if (slot[i] && SDL_GetGamepadID(slot[i]) == id) return;
    for (int i = 0; i < SLOTS; i++)
        if (!slot[i]) {
            slot[i] = SDL_OpenGamepad(id);
            if (slot[i]) {
                const char *name = SDL_GetGamepadName(slot[i]);
                printf("game pad: %s, USB pad %d (%s)%s\n", name ? name : "(no name)", i + 1,
                       is_switch(slot[i]) ? "SWITCH PRO" : "HID PAD", i == 0 ? ", and the console's controller" : "");
            }
            return;
        }
    const char *name = SDL_GetGamepadNameForID(id);
    printf("game pad: %s: not used (the cart takes two)\n", name ? name : "(no name)");
}

static void pad_scan(void) {
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) pad_added(ids[i]);
    SDL_free(ids);
}

static void pad_removed(SDL_JoystickID id) {
    for (int i = 0; i < SLOTS; i++)
        if (slot[i] && SDL_GetGamepadID(slot[i]) == id) {
            printf("game pad: USB pad %d unplugged\n", i + 1);
            SDL_CloseGamepad(slot[i]);
            slot[i] = NULL;
        }
}

void input_init(void *window) {
    win = window;
    pad_scan();
}

// the keypad, from the keyboard: 0-9 (either row), * on -, # on =
static int keypad_key(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (int)(s - SDL_SCANCODE_1) + 1;
    if (s == SDL_SCANCODE_0 || s == SDL_SCANCODE_KP_0) return 0;
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return (int)(s - SDL_SCANCODE_KP_1) + 1;
    if (s == SDL_SCANCODE_MINUS || s == SDL_SCANCODE_KP_MULTIPLY) return WIRED_STAR;
    if (s == SDL_SCANCODE_EQUALS) return WIRED_HASH;
    return -1;
}

static bool keypad_held(const bool *ks, int key) {
    for (int s = 0; s < SDL_SCANCODE_COUNT; s++)
        if (ks[s] && keypad_key((SDL_Scancode)s) == key) return true;
    return false;
}

static int16_t axis(SDL_Gamepad *p, SDL_GamepadAxis a, bool flip) {
    int v = SDL_GetGamepadAxis(p, a) / 16;  // -2048..2047
    if (flip) v = -v;                       // SDL's y is down, gamepad.h's up
    return (int16_t)(v < -2048 ? -2048 : v > 2047 ? 2047 : v);
}

// a desktop pad as cvpad's gamepad_t (buttons by where they are, as both name them)
static void read_pad(SDL_Gamepad *p, gamepad_t *g) {
    static const struct { SDL_GamepadButton b; uint32_t gp; } map[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, GP_UP},         {SDL_GAMEPAD_BUTTON_DPAD_DOWN, GP_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, GP_LEFT},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, GP_RIGHT},
        {SDL_GAMEPAD_BUTTON_SOUTH, GP_SOUTH},        {SDL_GAMEPAD_BUTTON_EAST, GP_EAST},
        {SDL_GAMEPAD_BUTTON_WEST, GP_WEST},          {SDL_GAMEPAD_BUTTON_NORTH, GP_NORTH},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GP_L},    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GP_R},
        {SDL_GAMEPAD_BUTTON_BACK, GP_MINUS},         {SDL_GAMEPAD_BUTTON_START, GP_PLUS},
        {SDL_GAMEPAD_BUTTON_GUIDE, GP_HOME},         {SDL_GAMEPAD_BUTTON_MISC1, GP_CAPTURE},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, GP_LSTICK},  {SDL_GAMEPAD_BUTTON_RIGHT_STICK, GP_RSTICK},
    };
    memset(g, 0, sizeof *g);
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (SDL_GetGamepadButton(p, map[i].b)) g->buttons |= map[i].gp;
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) g->buttons |= GP_ZL;
    if (SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) g->buttons |= GP_ZR;
    g->lx = axis(p, SDL_GAMEPAD_AXIS_LEFTX, false);
    g->ly = axis(p, SDL_GAMEPAD_AXIS_LEFTY, true);
    g->rx = axis(p, SDL_GAMEPAD_AXIS_RIGHTX, false);
    g->ry = axis(p, SDL_GAMEPAD_AXIS_RIGHTY, true);
    g->raw[0] = (uint16_t)(g->lx + 2048);
    g->raw[1] = (uint16_t)(g->ly + 2048);
    g->raw[2] = (uint16_t)(g->rx + 2048);
    g->raw[3] = (uint16_t)(g->ry + 2048);
}

// the keyboard as a USB pad (the "USB pad" role)
static void keyboard_pad(const bool *ks, gamepad_t *g) {
    static const struct { SDL_Scancode s; uint32_t gp; } map[] = {
        {SDL_SCANCODE_UP, GP_UP},     {SDL_SCANCODE_DOWN, GP_DOWN},  {SDL_SCANCODE_LEFT, GP_LEFT},
        {SDL_SCANCODE_RIGHT, GP_RIGHT}, {SDL_SCANCODE_Z, GP_SOUTH},  {SDL_SCANCODE_X, GP_EAST},
        {SDL_SCANCODE_A, GP_WEST},    {SDL_SCANCODE_S, GP_NORTH},    {SDL_SCANCODE_Q, GP_L},
        {SDL_SCANCODE_W, GP_R},       {SDL_SCANCODE_E, GP_ZL},       {SDL_SCANCODE_R, GP_ZR},
        {SDL_SCANCODE_BACKSPACE, GP_MINUS}, {SDL_SCANCODE_RETURN, GP_PLUS}, {SDL_SCANCODE_H, GP_HOME},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (ks[map[i].s]) g->buttons |= map[i].gp;
    if (ks[SDL_SCANCODE_J]) g->lx = -2048;  // IJKL: the left stick, all the way
    if (ks[SDL_SCANCODE_L]) g->lx = 2047;
    if (ks[SDL_SCANCODE_I]) g->ly = 2047;
    if (ks[SDL_SCANCODE_K]) g->ly = -2048;
}

static bool sim_key(SDL_Scancode s, sim_keys_t *k) {
    switch (s) {
    case SDL_SCANCODE_F1: k->help = true; return true;
    case SDL_SCANCODE_F2: k->role_next = true; return true;
    case SDL_SCANCODE_F5: k->reset = true; return true;
    case SDL_SCANCODE_F12: k->shot = true; return true;
    case SDL_SCANCODE_ESCAPE:
        if (role == ROLE_USB_KEYBOARD) return false;
        k->quit = true;
        return true;
    default: return false;
    }
}

// a key that is a USB keyboard's: HID's usages 04 (A) to E7 (right GUI), not the simulator's own
static bool usb_usage(SDL_Scancode s) {
    return s >= 4 && s <= 0xE7 && s != SDL_SCANCODE_F1 && s != SDL_SCANCODE_F2 && s != SDL_SCANCODE_F5 && s != SDL_SCANCODE_F12;
}

// whole counts from a float that adds up (a trackpad's fractions of a count)
static int whole(float *acc, float add) {
    *acc += add;
    const int n = (int)truncf(*acc);
    *acc -= (float)n;
    return n;
}

void input_poll(sim_keys_t *k) {
    memset(k, 0, sizeof *k);
    SDL_Event e;
    static float mfx, mfy, mfw;             // the mouse's fractions of a count
    int dx = 0, dy = 0, dw = 0;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            k->quit = true;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (sim_key(e.key.scancode, k) || e.key.repeat) break;     // (a USB keyboard repeats nothing)
            if (role == ROLE_USB_KEYBOARD && usb_usage(e.key.scancode)) model_usb_key((uint8_t)e.key.scancode, true);
            else if (role == ROLE_CONSOLE && keypad_key(e.key.scancode) >= 0) last_key = keypad_key(e.key.scancode);
            break;
        case SDL_EVENT_KEY_UP:
            if (role == ROLE_USB_KEYBOARD && usb_usage(e.key.scancode)) model_usb_key((uint8_t)e.key.scancode, false);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            dx += whole(&mfx, e.motion.xrel);
            dy += whole(&mfy, e.motion.yrel);
            break;
        case SDL_EVENT_MOUSE_WHEEL:             // + away from you, as a USB mouse's wheel
            dw += whole(&mfw, e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {        // USB's bits: 1 left, 2 right, 4 middle
            const uint8_t bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2
                              : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
            // (one pressed before the role came isn't the USB mouse's)
            if (!e.button.down) mouse_held = (uint8_t)(mouse_held & ~bit);
            else if (role == ROLE_USB_KEYBOARD) mouse_held = (uint8_t)(mouse_held | bit);
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:       // the keys and buttons held go elsewhere: let go of them
            if (role == ROLE_USB_KEYBOARD) usb_let_go();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            pad_added(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            pad_removed(e.gdevice.which);
            pad_scan();                         // one that had no slot takes it
            break;
        default:
            break;
        }
    }
    if (opt.headless && !scripted) return;  // headless: the models have the script's own words
    const bool *ks = SDL_GetKeyboardState(NULL);

    // the console's controller 1: the keyboard in its role, and the first pad
    uint8_t held = 0;
    int key = -1;
    if (role == ROLE_CONSOLE) {
        if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W]) held |= WIRED_UP;
        if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D]) held |= WIRED_RIGHT;
        if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]) held |= WIRED_DOWN;
        if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) held |= WIRED_LEFT;
        if (ks[SDL_SCANCODE_Z]) held |= WIRED_FIRE_L;
        if (ks[SDL_SCANCODE_X]) held |= WIRED_FIRE_R;
        if (last_key >= 0 && !keypad_held(ks, last_key)) last_key = -1;
        if (last_key < 0)                   // let go of the last one: another still held?
            for (int s = 0; s < SDL_SCANCODE_COUNT && last_key < 0; s++)
                if (ks[s] && keypad_key((SDL_Scancode)s) >= 0) last_key = keypad_key((SDL_Scancode)s);
        key = last_key;
    }
    gamepad_t usb[SLOTS];
    bool console_pad = false;
    for (int i = 0; i < SLOTS; i++) {
        memset(&usb[i], 0, sizeof usb[i]);
        if (!slot[i]) continue;
        read_pad(slot[i], &usb[i]);
        if (console_pad) continue;          // the first pad there is the controller too
        console_pad = true;
        const gamepad_t *g = &usb[i];
        const uint32_t b = g->buttons;
        if (b & GP_UP || g->ly > 1024) held |= WIRED_UP;
        if (b & GP_DOWN || g->ly < -1024) held |= WIRED_DOWN;
        if (b & GP_LEFT || g->lx < -1024) held |= WIRED_LEFT;
        if (b & GP_RIGHT || g->lx > 1024) held |= WIRED_RIGHT;
        if (b & (GP_SOUTH | GP_WEST)) held |= WIRED_FIRE_L;
        if (b & (GP_EAST | GP_NORTH)) held |= WIRED_FIRE_R;
        if (key < 0 && b & GP_MINUS) key = WIRED_STAR;
        if (key < 0 && b & GP_PLUS) key = WIRED_HASH;
    }
    // (headless, the script's joy= and key= have it, but for while a made-up pad is in)
    static bool pad_had;
    if (!opt.headless || console_pad || pad_had) model_console(held, key);
    pad_had = console_pad;

    // USB pads: the desktop's, pad 1 with the keyboard in its "USB pad" role
    if (role == ROLE_USB_PAD) keyboard_pad(ks, &usb[0]);
    for (int i = 0; i < SLOTS; i++)
        model_usb_pad(i, slot[i] || (i == 0 && role == ROLE_USB_PAD), &usb[i], slot[i] && !is_switch(slot[i]));

    // the USB mouse: what moved, and the buttons, when anything did
    if (role == ROLE_USB_KEYBOARD) {
        if (dx || dy || dw || mouse_held != mouse_buttons) model_usb_mouse(dx, dy, mouse_held, dw);
        mouse_buttons = mouse_held;
    }
}

// ---- made-up devices, for headless tests ------------------------------------------------
// The script's sim* words (main.c hands them here without the "sim"):
//
//   simrole=controller|pad|keyboard   the keyboard's role, as F2 sets it
//   simkey=04 simkeyup=04             a key down, up: SDL's scancode, which is the HID usage (hex)
//   simmouse=DX,DY,BUTTONS[,WHEEL]    the mouse moved, its buttons (1 left, 2 right, 4 middle), its wheel
//   simpad=switch|hid                 a game pad plugged in: a Switch Pro Controller, or another
//   simunplug=N                       the Nth plugged in, unplugged
//   simbuttons=N:south+l              pad N's buttons held (host.c's pad= names)
//   simsticks=N:LX,LY,RX,RY           pad N's sticks, as SDL has them (-32768..32767, y down)
static SDL_JoystickID made[8];
static SDL_Joystick *made_js[8];
static int n_made;

static void push_key(SDL_Scancode sc, bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    e.key.down = down;
    SDL_PushEvent(&e);
}

static SDL_Joystick *made_pad(const char *v, const char **rest) {
    const int n = atoi(v);
    const char *colon = strchr(v, ':');
    if (n < 1 || n > n_made || !made_js[n - 1] || !colon) return NULL;
    *rest = colon + 1;
    return made_js[n - 1];
}

bool input_script(const char *what, const char *v) {
    scripted = true;
    if (!strcmp(what, "role")) {
        const int r = !strcmp(v, "controller") ? ROLE_CONSOLE : !strcmp(v, "pad") ? ROLE_USB_PAD : !strcmp(v, "keyboard") ? ROLE_USB_KEYBOARD : -1;
        if (r < 0) return false;
        if (r != role) input_set_role(r);
    } else if (!strcmp(what, "key") || !strcmp(what, "keyup")) {
        push_key((SDL_Scancode)strtol(v, NULL, 16), what[3] != 'u');
    } else if (!strcmp(what, "mouse")) {
        int dx = 0, dy = 0, b = 0, w = 0;
        if (sscanf(v, "%d,%d,%d,%d", &dx, &dy, &b, &w) < 3) return false;
        SDL_Event e;
        if (dx || dy) {
            SDL_zero(e);
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.xrel = (float)dx;
            e.motion.yrel = (float)dy;
            SDL_PushEvent(&e);
        }
        static uint8_t was;
        static const uint8_t sdl_button[3] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
        for (int i = 0; i < 3; i++)
            if (((b ^ was) >> i) & 1) {
                SDL_zero(e);
                e.type = (b >> i) & 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.button = sdl_button[i];
                e.button.down = (b >> i) & 1;
                SDL_PushEvent(&e);
            }
        was = (uint8_t)b;
        if (w) {
            SDL_zero(e);
            e.type = SDL_EVENT_MOUSE_WHEEL;
            e.wheel.y = (float)w;
            e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            SDL_PushEvent(&e);
        }
    } else if (!strcmp(what, "pad")) {
        if ((strcmp(v, "switch") && strcmp(v, "hid")) || n_made >= 8) return false;
        if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            fprintf(stderr, "simpad: %s\n", SDL_GetError());
            exit(2);
        }
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        const bool sw = v[0] == 's';
        d.vendor_id = sw ? 0x057E : 0x045E;     // Nintendo's Switch Pro Controller; Microsoft's Xbox 360 pad
        d.product_id = sw ? 0x2009 : 0x028E;
        d.name = sw ? "Made-up Switch Pro Controller" : "Made-up pad";
        made[n_made] = SDL_AttachVirtualJoystick(&d);
        made_js[n_made] = made[n_made] ? SDL_OpenJoystick(made[n_made]) : NULL;
        if (!made_js[n_made]) { fprintf(stderr, "simpad: %s\n", SDL_GetError()); exit(2); }
        n_made++;
    } else if (!strcmp(what, "unplug")) {
        const int n = atoi(v);
        if (n < 1 || n > n_made || !made_js[n - 1]) return false;
        SDL_CloseJoystick(made_js[n - 1]);
        SDL_DetachVirtualJoystick(made[n - 1]);
        made_js[n - 1] = NULL;
    } else if (!strcmp(what, "buttons")) {
        static const struct { const char *name; int b; } names[] = {
            {"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
            {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"east", SDL_GAMEPAD_BUTTON_EAST},
            {"west", SDL_GAMEPAD_BUTTON_WEST}, {"north", SDL_GAMEPAD_BUTTON_NORTH}, {"l", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
            {"r", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"minus", SDL_GAMEPAD_BUTTON_BACK}, {"plus", SDL_GAMEPAD_BUTTON_START},
            {"home", SDL_GAMEPAD_BUTTON_GUIDE}, {"capture", SDL_GAMEPAD_BUTTON_MISC1}, {"ls", SDL_GAMEPAD_BUTTON_LEFT_STICK},
            {"rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"zl", -1 - SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"zr", -1 - SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
        };
        const char *rest;
        SDL_Joystick *js = made_pad(v, &rest);
        if (!js) return false;
        bool down[sizeof names / sizeof names[0]] = {false};
        char buf[128];
        snprintf(buf, sizeof buf, "%s", rest);
        for (char *w = strtok(buf, "+"); w; w = strtok(NULL, "+")) {
            size_t i = 0;
            while (i < sizeof names / sizeof names[0] && strcmp(w, names[i].name)) i++;
            if (i == sizeof names / sizeof names[0]) return false;
            down[i] = true;
        }
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (names[i].b >= 0) SDL_SetJoystickVirtualButton(js, names[i].b, down[i]);
            else SDL_SetJoystickVirtualAxis(js, -1 - names[i].b, down[i] ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
    } else if (!strcmp(what, "sticks")) {
        const char *rest;
        SDL_Joystick *js = made_pad(v, &rest);
        int a[4];
        if (!js || sscanf(rest, "%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3]) != 4) return false;
        static const int axes[4] = {SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY};
        for (int i = 0; i < 4; i++) SDL_SetJoystickVirtualAxis(js, axes[i], (Sint16)(a[i] < -32768 ? -32768 : a[i] > 32767 ? 32767 : a[i]));
    } else {
        return false;
    }
    return true;
}

// for the menu's screen after RESET: any of these starts the plugin again
bool input_start_pressed(void) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_X]) return true;
    for (int i = 0; i < SLOTS; i++)
        if (slot[i] && (SDL_GetGamepadButton(slot[i], SDL_GAMEPAD_BUTTON_SOUTH) || SDL_GetGamepadButton(slot[i], SDL_GAMEPAD_BUTTON_EAST) ||
                        SDL_GetGamepadButton(slot[i], SDL_GAMEPAD_BUTTON_START)))
            return true;
    return false;
}
