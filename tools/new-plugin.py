#!/usr/bin/env python3
# tools/new-plugin.py -- start a plugin: the features you pick, wired in and
# shown on the TV, in a plugin that builds as it is.
#
#   tools/new-plugin.py                  asks, a question at a time
#   tools/new-plugin.py --name "Star Sweeper" --by "Ann" \
#       --features pads,keyboard,midi-in --screen text
#                                        asks nothing: the rest are defaults
#
# Where it writes:
#   --out DIR   a new folder: a copy of this kit, made into the plugin
#               (the default: a folder beside this kit, named after the plugin)
#   --here      this copy of the kit, made into the plugin in place of Hello
#   --examples  keep examples/ (left out by default: they are the kit's, to
#               read, not the plugin's)
#
# What it writes: CMakeLists.txt (the features on), src/main.c (each
# feature on the TV, working, and YOUR CODE where yours goes), src/icon.txt,
# README.md (as docs/readme-spec.md says), LICENSE, THIRD-PARTY.md, the
# plugin's tests in test/run.sh and sim/test.sh, and the workflow's list of
# them. The rest of the kit (cmake/, sdk/, lib/, sim/, test/, tools/, docs/,
# the workflows) comes as it is. docs/features.md has each feature.
#
# Python 3.8 or newer, nothing beyond its standard library.
import argparse
import datetime
import os
import re
import shutil
import sys

KIT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# key, the CMake word, what it is (for the question), what is on the screen
FEATURES = [
    ("pads", "PADS", "USB game pads, two at once (pad 1 is player 1 too)"),
    ("keyboard", "KEYBOARD", "a USB keyboard"),
    ("mouse", "MOUSE", "a USB mouse"),
    ("midi-in", "MIDI_IN", "MIDI in, from a USB MIDI keyboard"),
    ("midi-out", "MIDI_OUT", "MIDI out to a computer, with the cart as its MIDI device"),
    ("psram", "PSRAM", "the 8 MB PSRAM"),
    ("saves", "CARD", "a save file on the card"),
]
FEATURE_KEYS = [f[0] for f in FEATURES]
HOST_FEATURES = {"pads", "keyboard", "mouse", "midi-in"}
USB_FEATURES = HOST_FEATURES | {"midi-out"}

SCREENS = [
    ("text", "the SDK draws text and pictures for you (most plugins)"),
    ("frames", "you send a whole picture every TV frame (an emulator, a video player)"),
]

ICONS = {
    "star": ["...##...", "...##...", "########", ".######.", "..####..", ".##..##.", ".#....#.", "........"],
    "pad": ["........", ".######.", "##.###.#", "#...#.##", "##.#####", "###..###", "##....##", "........"],
    "note": ["..######", "..##..##", "..#....#", "..#....#", "###..###", "###..###", "........", "........"],
    "heart": [".##..##.", "########", "########", ".######.", "..####..", "...##...", "........", "........"],
    "smile": ["........", ".##..##.", ".##..##.", "........", "#......#", ".#....#.", "..####..", "........"],
    "blank": ["........"] * 8,
}
FILE_ICON = ["#####...", "#...##..", "#....##.", "#.....#.", "#.....#.", "#.....#.", "#######.", "........"]

# Licenses whose full text the tool writes, from tools/licenses/: the GNU
# and Apache texts as gnu.org and apache.org publish them, the short ones
# with <year> and <copyright holders> filled in. Any other SPDX ID gets a
# LICENSE to paste its text into.
LICENSES = {
    "MIT": "MIT.txt",
    "BSD-2-Clause": "BSD-2-Clause.txt",
    "BSD-3-Clause": "BSD-3-Clause.txt",
    "ISC": "ISC.txt",
    "Apache-2.0": "Apache-2.0.txt",
    "GPL-2.0-only": "GPL-2.0.txt",
    "GPL-2.0-or-later": "GPL-2.0.txt",
    "GPL-3.0-only": "GPL-3.0.txt",
    "GPL-3.0-or-later": "GPL-3.0.txt",
    "LGPL-2.1-or-later": "LGPL-2.1.txt",
}


def license_file(spdx, year, holder):
    with open(os.path.join(KIT, "tools", "licenses", LICENSES[spdx])) as f:
        text = f.read()
    if "<copyright holders>" not in text:
        return text                                     # the GNU and Apache texts: as they are
    return text.replace("<year>", str(year)).replace("<copyright holders>", holder)


TEMPLATE_URL = "https://github.com/plasticbugs/parrot-plugin-template"
TEMPLATE_HOLDER = "Parrot Cart contributors"


# ---- what the answers must be ------------------------------------------------------------
class Bad(Exception):
    pass


def check_name(s):
    s = s.strip()
    if not s:
        raise Bad("it needs a name")
    if len(s) > 32:
        raise Bad("it can be up to 32 characters, the length of a menu line")
    if not all(32 <= ord(c) < 127 for c in s) or any(c in s for c in '"\\'):
        raise Bad('use plain letters, digits and punctuation, without " or \\')
    return s


def check_by(s):
    return check_name(s)


def slug(s):
    s = re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")
    return s


def default_id(by, name):
    dev, plug = slug(by) or "me", slug(name) or "plugin"
    if len(dev) + 1 + len(plug) > 32:
        dev = dev[:12].strip("-")
        plug = plug[: 32 - len(dev) - 1].strip("-")
    return f"{dev}.{plug}"


def check_id(s):
    s = s.strip()
    if not re.fullmatch(r"[a-z0-9_-]+(\.[a-z0-9_-]+)+", s) or len(s) > 32:
        raise Bad("use developer.pluginname, with lowercase letters, digits, . - and _, up to 32 characters (docs/plugin-rules.md)")
    return s


def check_version(s):
    s = s.strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", s):
        raise Bad("use major.minor.patch, like 0.1.0")
    return s


def check_home(s):
    s = s.strip().rstrip("/")
    if not s:
        return ""
    if not s.startswith("/"):
        s = "/" + s
    if len(s) > 32 or not re.fullmatch(r"(/[A-Za-z0-9 _.\-]+)+", s):
        raise Bad("use a folder on the card, like /SWEEPER, with letters, digits, spaces, . - and _, up to 32 characters")
    return s


def check_opens(s):
    exts = [e if e.startswith(".") else "." + e for e in re.split(r"[\s,]+", s.strip()) if e]
    if len(exts) > 8 or any(not re.fullmatch(r"\.[A-Za-z0-9]{1,7}", e) for e in exts):
        raise Bad("list up to 8 extensions, each a dot and 1-7 letters or digits, like .swp .lvl")
    return exts


def check_features(s):
    words = [w for w in re.split(r"[\s,]+", s.strip().lower()) if w]
    if words in (["none"], []):
        return []
    if words == ["all"]:
        return list(FEATURE_KEYS)
    out = []
    for w in words:
        if w.isdigit() and 1 <= int(w) <= len(FEATURES):
            w = FEATURE_KEYS[int(w) - 1]
        w = {"midi_in": "midi-in", "midi_out": "midi-out", "card": "saves", "save": "saves", "pad": "pads"}.get(w, w)
        if w not in FEATURE_KEYS:
            raise Bad(f"{w}? The features are {', '.join(FEATURE_KEYS)} (or all, none)")
        if w not in out:
            out.append(w)
    return [k for k in FEATURE_KEYS if k in out]


def check_screen(s):
    s = s.strip().lower()
    if s in ("1", "text", "screen"):
        return "text"
    if s in ("2", "frames", "frame"):
        return "frames"
    raise Bad("text or frames")


def check_icon(s):
    s = s.strip()
    if s.isdigit() and 1 <= int(s) <= len(ICONS):
        return list(ICONS)[int(s) - 1]
    if s.lower() in ICONS:
        return s.lower()
    if os.path.isfile(s):
        rows = [r.strip() for r in open(s) if r.strip()]
        if len(rows) == 8 and all(len(r) == 8 and not set(r) - set("#.") for r in rows):
            return os.path.abspath(s)
        raise Bad(f"{s}: an icon file needs 8 lines of 8 # and . characters (docs/icons.md)")
    raise Bad(f"pick one of {', '.join(ICONS)}, or a file of 8 lines of 8 # and .")


def check_license(s):
    s = s.strip() or "MIT"
    for k in LICENSES:
        if s.lower() == k.lower():
            return k
    if not re.fullmatch(r"[A-Za-z0-9.+\-]+", s):
        raise Bad("use an SPDX license ID, like MIT, BSD-3-Clause, Apache-2.0 or GPL-3.0-or-later")
    m = re.fullmatch(r"(?i)(L?GPL-\d\.\d)\+?", s)
    if m:
        v = m.group(1).upper()
        raise Bad(f"which one, {v}-only or {v}-or-later?")
    return s


# ---- the plugin's parts ------------------------------------------------------------------
class Plugin:
    def __init__(self, a):
        self.name = a.name
        self.by = a.by
        self.id = a.id or default_id(a.by, a.name)
        self.version = a.version
        self.about = a.about or "A plugin for the Parrot Cart, the ColecoVision SD cartridge."
        self.screen = a.screen
        self.features = a.features
        self.home = a.home
        self.opens = a.opens
        self.icon = a.icon or ("note" if {"midi-in", "midi-out"} & set(a.features) else "pad" if "pads" in a.features else "star")
        self.license = a.license
        s = slug(self.name) or "plugin"
        self.test = s                                          # test/run.sh's name for it
        t = s.replace("-", "_")
        self.target = t if t[0].isalpha() else "p_" + t
        stem = re.sub(r"[^A-Z0-9]", "", self.name.upper())[:8] or "PLUGIN"
        self.stem = stem
        self.file = stem + ".CVP"
        self.save_file = stem + ".SAV"
        self.save_folder = self.home or "/PLUGINS"
        self.save_magic = (stem[:7] + "_" * 7)[:7] + "1"
        self.note_name = self.name[:12].rstrip()              # "Name: 4294967295 presses" fits the menu's 32

    def has(self, *keys):
        return any(k in self.features for k in keys)

    @property
    def words(self):
        words = [w for k, w, _ in FEATURES if k in self.features]
        if self.opens and "CARD" not in words:
            words.append("CARD")                                # the file it opens is on the card
        return words

    def tests(self):
        names = [self.test]
        if self.has(*HOST_FEATURES):
            names.append(self.test + "-usb")
        if self.has("saves"):
            names.append(self.test + "-card")
        if self.opens:
            names.append(self.test + "-open")
        return names

    def ci_tests(self):
        return [t for t in self.tests() if not t.endswith(("-card", "-open"))]   # (the card's tests need a Mac's hdiutil)


def c_str(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


# A tiny preprocessor for the C below: //@if a|b&c|!d, //@else, //@end around
# lines, and @KEY@ for values.
def render(template, flags, values):
    out, stack = [], []
    for line in template.splitlines():
        m = re.match(r"\s*//@(if|else|end)\b\s*(.*)$", line)
        if m:
            what, expr = m.groups()
            if what == "if":
                on = all(stack) if stack else True
                ok = any(all((t[1:] not in flags) if t.startswith("!") else (t in flags) for t in alt.split("&"))
                         for alt in expr.strip().split("|"))
                stack.append(on and ok)
                stack.append(on)                                # (the parent's state, for else)
            elif what == "else":
                parent = stack.pop()
                stack[-1] = parent and not stack[-1]
                stack.append(parent)
            else:
                stack.pop()
                stack.pop()
            continue
        if all(stack[0::2]) if stack else True:
            out.append(line)
    text = "\n".join(out) + "\n"
    for k, v in values.items():
        text = text.replace("@" + k + "@", v)
    left = re.findall(r"@[A-Z_]+@", text)
    if left:
        raise SystemExit(f"new-plugin.py: unfilled {left[:3]}")
    return text


MAIN_C = r"""@HEADER@
//
// How a plugin runs. The cart has two cores:
//
//   core 1  is the cartridge: it answers every read the console makes,
//@if text
//           from a small Z80 program (the SDK's screen kernel) that draws
//           what the plugin sends, plays its sound and reports the
//           controller. cvk_start() starts it; you never touch it again.
//@end
//@if frames
//           from a small Z80 program (the SDK's frame kernel) that runs one
//           list of video and sound writes every TV frame and reports the
//           controller. cvf_start() starts it; you never touch it again.
//@end
//   core 0  is this file: one loop that never waits for long.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

//@if text
#include "cvkernel.h"           // the console: cvk_*
#include "cvscreen.h"           // the screen: cvs_*, and vdp.h's canvas_* and colors
#include "handoff.h"            // made by the build: the ROM that parks the console between firmwares
#include "rom_image.h"          // made by the build: the screen kernel
//@end
//@if frames
#include "cvdiag.h"             // a crash becomes a note on the menu's bottom line
#include "cvframes.h"           // the console: cvf_*
#include "font8.h"              // the menu's 8x8 font, ASCII 32-95
#include "gbkernel.h"           // made by the build: the frame kernel
#include "handoff.h"            // made by the build: the ROM that parks the console between firmwares
//@end
#include "cvinput.h"            // player 1: @PLAYER_TEXT@
//@if usb
#include "cvusb.h"              // the USB port: @USB_TEXT@
//@end
//@if psram
#include "cvpsram.h"            // the 8 MB PSRAM
//@end
//@if saves|opens
#include "cvsd.h"               // the card: cvsd_*, and fsro.h's files and folders
//@end

#define NAME "@NAME_C@"
#define BY   "@BY_C@"

// ---- the screen: 24 rows of 28 characters -------------------------------------------------
// Each thing shown has its row. say() fills a row; @SHOWER@ shows the rows.
// The font has capitals, digits and ! # & ' ( ) * + , - . / : ? (the rest
// show as ?).
enum {
@ROWS@
};
#define ROWS 24
#define COLS 28                 // (the TV cuts the edges: 2 characters each side are left off)
static char text[ROWS][COLS + 1];
//@if text
static uint8_t ink[ROWS];
//@end
//@if frames
// (this picture is in one color; these are the console's numbers for them)
enum { C_BLACK = 1, C_LGREEN = 3, C_LRED = 9, C_LYELLOW = 11, C_GRAY = 14, C_WHITE = 15 };
//@end

static void __attribute__((format(printf, 3, 4))) say(int row, uint8_t color, const char *fmt, ...) {
    char s[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    int i = 0;
    for (; i < COLS && s[i]; i++) text[row][i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];   // the font is upper case
    text[row][i] = 0;
//@if text
    ink[row] = color;
//@end
//@if frames
    (void)color;
//@end
}

static void leave_note(const char *s) {
//@if text
    cvk_note(s);                // the menu's bottom line when RESET takes the console back (32 characters at most)
//@end
//@if frames
    cvf_note(s);                // the menu's bottom line when RESET takes the console back (32 characters at most)
//@end
}

// ---- player 1 ---------------------------------------------------------------------------
static cvin_t in;
static unsigned presses;

static void player_step(void) {
    cvin_update(&in);
    const uint8_t pressed = cvin_pressed(&in);
    const int key = cvin_key(&in);
    // YOUR CODE: player 1. in.held (CVIN_UP, CVIN_FIRE_L ...) and in.key
    // (0-9, CVIN_STAR, CVIN_HASH, -1) are what is down now; pressed and key
    // what went down since the last step.
    if (pressed || key >= 0) {
        presses++;
        static char note[33];
        snprintf(note, sizeof note, "@NOTE_NAME_C@: %u presses", presses);
        leave_note(note);
    }
    const uint8_t h = in.held;
    say(ROW_STICK, C_WHITE, "STICK %s%s%s%s FIRE %s%s", h & CVIN_UP ? "UP " : "", h & CVIN_DOWN ? "DOWN " : "",
        h & CVIN_LEFT ? "LEFT " : "", h & CVIN_RIGHT ? "RIGHT " : "", h & CVIN_FIRE_L ? "L " : "", h & CVIN_FIRE_R ? "R" : "");
    const char k = in.key < 0 ? '-' : in.key == CVIN_STAR ? '*' : in.key == CVIN_HASH ? '#' : (char)('0' + in.key);
    say(ROW_KEYPAD, C_WHITE, "KEYPAD %c  PRESSES %u", k, presses);
}
//@if usb

// ---- the USB port -----------------------------------------------------------------------
static void usb_step(void) {
    const cvusb_mode_t mode = cvusb_mode();
//@if ask
    // A computer that answers and doesn't take the cart is asking its user
    // ("Allow accessory to connect?" on a Mac). A fire button stops waiting.
    if (mode == CVUSB_ASKING && (in.held & (CVIN_FIRE_L | CVIN_FIRE_R))) cvusb_host_now();
//@end
    say(ROW_USB, mode == CVUSB_HOST || mode == CVUSB_DEVICE ? C_WHITE : C_LYELLOW, "USB %s", cvusb_mode_name(mode));
}
//@end
//@if pads

// ---- USB game pads ----------------------------------------------------------------------
// Buttons by where they are: S the bottom face button (B on a Nintendo pad),
// E the right one, W the left, N the top.
static const char *const BUTTON[GP_BUTTONS] = {"UP", "DN", "LT", "RT", "S", "E", "W", "N", "L", "R",
                                               "ZL", "ZR", "-", "+", "HOME", "CAP", "L3", "R3"};

static void pads_step(void) {
    for (int n = 0; n < CVUSB_PAD_MAX; n++) {
        const int row = ROW_PAD + 3 * n;
        const char *kind = cvusb_pad_kind(n);
        gamepad_t p;
        if (!cvusb_pad(n, &p)) {
            say(row, C_GRAY, "PAD %d %s", n + 1, kind ? "STARTING" : "-");
            say(row + 1, C_GRAY, "%s", "");
            say(row + 2, C_GRAY, "%s", "");
            continue;
        }
        // YOUR CODE: pad n. p.buttons: the GP_* held (gamepad.h); p.lx, p.ly,
        // p.rx, p.ry: the sticks, -2048..2047, y up. Pad 1 is player 1 too.
        char b[64] = "";
        size_t at = 0;
        for (int i = 0; i < GP_BUTTONS; i++)
            if (p.buttons & 1u << i) at += (size_t)snprintf(b + at, sizeof b - at, "%s ", BUTTON[i]);
        say(row, C_WHITE, "PAD %d %s", n + 1, kind ? kind : "");
        say(row + 1, C_WHITE, "  %s", at ? b : "-");
        say(row + 2, C_WHITE, "  L%+5d%+5d R%+5d%+5d", p.lx, p.ly, p.rx, p.ry);
    }
}
//@end
//@if keyboard

// ---- the keyboard -----------------------------------------------------------------------
#define TYPED (COLS - 6)
static char typed[TYPED + 1];
static bool any_key;

// Each key as it goes down and up: k->usage is its USB code (0x04 is A,
// 0x28 Enter: HID's usage table), k->ascii its character on a US keyboard
// (0 for none, as the arrows), k->mods the modifiers held (CVKEY_SHIFT,
// CVKEY_CTRL, CVKEY_ALT, CVKEY_GUI).
static void key_in(const cvkey_t *k) {
    any_key = true;
    say(ROW_KEY, C_WHITE, "KEY %02X %c %s %s%s%s%s", k->usage, k->ascii > 32 && k->ascii < 127 ? k->ascii : ' ',
        k->down ? "DOWN" : "UP", k->mods & CVKEY_SHIFT ? "SHIFT " : "", k->mods & CVKEY_CTRL ? "CTRL " : "",
        k->mods & CVKEY_ALT ? "ALT " : "", k->mods & CVKEY_GUI ? "GUI" : "");
    // YOUR CODE: a key
    if (!k->down) return;
    size_t n = strlen(typed);
    if (k->ascii == '\b') {
        if (n) typed[n - 1] = 0;
    } else if (k->ascii == '\r') {
        typed[0] = 0;
    } else if (k->ascii >= 32 && k->ascii < 127) {
        if (n == TYPED) memmove(typed, typed + 1, n--);     // the oldest goes
        typed[n] = k->ascii;
        typed[n + 1] = 0;
    }
    say(ROW_TYPED, C_LGREEN, "TYPED %s", typed);
}

static void keyboard_step(void) {
    if (any_key) return;
    cvusb_status_t s;
    cvusb_status(&s);
    say(ROW_KEY, C_GRAY, s.keyboards ? "KEYBOARD: TYPE SOMETHING" : "KEYBOARD -");
}
//@end
//@if mouse

// ---- the mouse --------------------------------------------------------------------------
static int mouse_x = 128, mouse_y = 96, wheel;

static void mouse_step(void) {
    cvmouse_t m;
    if (!cvusb_mouse(&m)) {
        say(ROW_MOUSE, C_GRAY, "MOUSE -");
        return;
    }
    // YOUR CODE: the mouse. m.dx and m.dy are how far it moved since the
    // last call (y down), m.wheel the same, m.buttons what is held (1 left,
    // 2 right, 4 middle).
    mouse_x += (int)m.dx;
    mouse_y += (int)m.dy;
    mouse_x = mouse_x < 0 ? 0 : mouse_x > 255 ? 255 : mouse_x;
    mouse_y = mouse_y < 0 ? 0 : mouse_y > 191 ? 191 : mouse_y;
    wheel += (int)m.wheel;
    say(ROW_MOUSE, C_WHITE, "MOUSE %d,%d %s%s%s WHEEL %d", mouse_x, mouse_y, m.buttons & 1 ? "L" : "-",
        m.buttons & 4 ? "M" : "-", m.buttons & 2 ? "R" : "-", wheel);
}
//@end
//@if midi-in

// ---- MIDI in ----------------------------------------------------------------------------
static unsigned midi_in_count;

// Each message as it comes: m[0] the status (0x90 a note on, 0x80 a note
// off, 0xB0 a controller ..., plus the channel, 0-15), then up to 2 data
// bytes: n in all.
// @MIDI_IN_FROM@
static void midi_in(const uint8_t *m, int n) {
    midi_in_count++;
    // YOUR CODE: a MIDI message
    char hex[12] = "";
    for (int i = 0; i < n && i < 3; i++) snprintf(hex + 3 * i, 4, " %02X", m[i]);
    say(ROW_MIDI_IN, C_WHITE, "MIDI IN %u %s", midi_in_count, hex);
}

static void midi_in_step(void) {
    if (midi_in_count) return;
    cvusb_status_t s;
    cvusb_status(&s);
    say(ROW_MIDI_IN, C_GRAY, s.midi ? "MIDI IN: PLAY SOMETHING" : "MIDI IN -");
}
//@end
//@if midi-out

// ---- MIDI out ---------------------------------------------------------------------------
// The keypad's 1-8 play C to C on channel 1: a note on as a key goes down,
// its note off as it comes up.
// @MIDI_OUT_TO@
static int sounding = -1;
static unsigned midi_out_count;

static void midi_send(uint8_t status, int note, uint8_t velocity) {
    const uint8_t m[3] = {status, (uint8_t)note, velocity};
    // YOUR CODE: MIDI out, a whole message at a time. False when there is
    // nowhere to send it, or no room for it now.
    if (cvusb_midi_send(m, 3)) {
        midi_out_count++;
        say(ROW_MIDI_OUT, C_WHITE, "MIDI OUT %u  %02X %02X %02X", midi_out_count, m[0], m[1], m[2]);
    } else {
        say(ROW_MIDI_OUT, C_GRAY, "MIDI OUT: NOWHERE TO SEND");
    }
}

static void midi_out_step(void) {
    static const uint8_t NOTE_OF[9] = {0, 60, 62, 64, 65, 67, 69, 71, 72};
    const int note = in.key >= 1 && in.key <= 8 ? NOTE_OF[in.key] : -1;
    if (note == sounding) return;
    if (sounding >= 0) midi_send(0x80, sounding, 0);
    if (note >= 0) midi_send(0x90, note, 100);
    sounding = note;
}
//@end
//@if psram

// ---- the PSRAM --------------------------------------------------------------------------
// 8 MB, the plugin's once cvpsram_init() says so: CVPSRAM_CACHED is a
// uint8_t pointer to all of it (cvpsram.h). Here a word in each megabyte is
// written and read back, once, at the start.
static void psram_start(void) {
    if (!cvpsram_init()) {
        say(ROW_PSRAM, C_LRED, "PSRAM: NONE");
        return;
    }
    volatile uint32_t *w = CVPSRAM_UNCACHED;
    for (uint32_t mb = 0; mb < 8; mb++) w[mb << 18] = 0xC0DE0000u | mb;
    int good = 0;
    for (uint32_t mb = 0; mb < 8; mb++) good += w[mb << 18] == (0xC0DE0000u | mb);
    say(ROW_PSRAM, good == 8 ? C_WHITE : C_LRED, "PSRAM: %d MB OF 8 RIGHT", good);
    // YOUR CODE: CVPSRAM_CACHED[0] to [CVPSRAM_SIZE - 1] are yours
}
//@end
//@if saves

// ---- the save file ----------------------------------------------------------------------
// SAVE_FILE in SAVE_FOLDER, 512 bytes, found or made at the start. Its
// layout is yours. Keep SAVE_MAGIC at its start, and change its last
// character when the layout changes, so a new release can tell an old save.
#define SAVE_FOLDER "@SAVE_FOLDER@"
#define SAVE_FILE   "@SAVE_FILE@"
#define SAVE_MAGIC  "@SAVE_MAGIC@"
typedef struct {
    char magic[8];
    uint32_t runs;              // how many times the plugin has started
    // YOUR CODE: what your plugin keeps
} save_t;
static save_t save;
static fs_t fs;                 // the file system's structures are big (an open file's map is 12K): static
static fs_file_t save_f;
static bool save_ok;
static uint8_t sector[512];

// Write `save` to the card, in place. A write takes milliseconds: call it
// when there is something new to keep, not every step.
static bool save_write(void) {
    if (!save_ok) return false;
    memset(sector, 0, sizeof sector);
    memcpy(sector, &save, sizeof save);
    return fs_write_sec(&fs, 0, sector) == FS_OK;
}

// Once, at the start: the card's start takes a second or so, with the
// screen standing still.
static void save_start(void) {
    const char *why = NULL;
    if (!cvsd_card_in()) {
        why = "NO CARD";
    } else if (!cvsd_init()) {
        why = "THE CARD DIDN'T START";
    } else if (fs_mount(&fs, cvsd_read) != FS_OK) {
        why = "NO FAT OR EXFAT ON THE CARD";
    } else {
        fs_set_writer(&fs, cvsd_write);                     // without this the card is read-only
        uint16_t name[16];                                  // the file system takes names as UTF-16
        int n = 0;
        for (; SAVE_FILE[n]; n++) name[n] = (uint8_t)SAVE_FILE[n];
        bool made;
        const int r = fs_create(&fs, SAVE_FOLDER, name, n, 512, &save_f, &made);   // found, or made with its size
        if (r == FS_ENOFILE) {
            why = "NO FOLDER " SAVE_FOLDER;
        } else if (r != FS_OK || fs_open(&fs, &save_f) != FS_OK) {
            why = "THE SAVE WON'T OPEN";
        } else {
            memcpy(save.magic, SAVE_MAGIC, 8);
            if (!made && cvsd_read(fs_lba(&fs, 0), sector) && !memcmp(sector, SAVE_MAGIC, 8))
                memcpy(&save, sector, sizeof save);         // it was there, and it is ours
            save_ok = true;
            save.runs++;
            if (!save_write()) why = "THE SAVE DIDN'T WRITE";
        }
    }
    if (why) say(ROW_SAVE, C_LRED, "SAVE: %s", why);
    else say(ROW_SAVE, C_WHITE, "SAVE: RUN %lu, %s", (unsigned long)save.runs, SAVE_FILE);
}
//@end
//@if opens

// ---- the file it was opened with --------------------------------------------------------
// Pressing A on one of its files (@OPENS_TEXT@) in the card's folders starts
// the plugin with that file: the menu puts its path, and where it is on the
// card, in the launch block (cvlaunch.h). Started from the Plugins page,
// the block has no path. Open the file with cvsd_open_launched(), not by
// its path, because a character outside ASCII is ? in the path.
static fs_t open_fs;            // (static: the file system's structures are big)
static fs_file_t open_f;
static uint8_t open_sector[512];

// Once, at the start: the card's start takes a second or so, with the
// screen standing still.
static void open_start(void) {
//@if text
    const cvl_t *b = cvk_launch();                      // the launch block, or NULL
//@end
//@if frames
    const cvl_t *b = cvf_launch();                      // the launch block, or NULL
//@end
    if (!b || !b->path[0]) {
        say(ROW_FILE, C_GRAY, "FILE -");
        say(ROW_FILE_SIZE, C_GRAY, "  PICK ONE IN THE MENU");
        return;
    }
    const char *slash = strrchr(b->path, '/');
    const char *name = slash ? slash + 1 : b->path;
    say(ROW_FILE, C_WHITE, "FILE %s", name);
    static char note[33];
    const int r = cvsd_open_launched(&open_fs, b, false, &open_f);    // false: it won't be written to
    if (r != FS_OK) {
        say(ROW_FILE_SIZE, C_LRED, "  %s", r == FS_EIO ? "THE CARD DIDN'T START" : "IT WON'T OPEN");
        snprintf(note, sizeof note, "Can't open %.21s", name);
        leave_note(note);
        return;
    }
    // YOUR CODE: read the file. Sector k of it, 512 bytes, is at
    // fs_lba(&open_fs, k) on the card (0 past its end). Here the first one
    // is read. For a big file, read a few sectors a pass from the loop, so
    // the loop keeps turning (examples/open-file).
    const bool read = open_f.size == 0 || cvsd_read(fs_lba(&open_fs, 0), open_sector);
    say(ROW_FILE_SIZE, read ? C_WHITE : C_LRED, "  %lu BYTES%s", (unsigned long)open_f.size, read ? "" : ", A READ FAILED");
    snprintf(note, sizeof note, "Opened %.25s", name);
    leave_note(note);
}
//@end

@STEP_HEADER@
static void step(void) {
    player_step();
//@if usb
    usb_step();
//@end
//@if pads
    pads_step();
//@end
//@if keyboard
    keyboard_step();
//@end
//@if mouse
    mouse_step();
//@end
//@if midi-in
    midi_in_step();
//@end
//@if midi-out
    midi_out_step();
//@end
    // YOUR CODE: your plugin's step
}
//@if text

// ---- the screen -------------------------------------------------------------------------
static cvs_t screen;
static uint8_t list[CVS_LIST_MAX];

// The whole screen, every time: only what changed reaches the console. Two
// colors at most in any 8x1 run of pixels (text on its background is fine).
static void draw(canvas_t *c, bool all, void *ctx) {
    (void)all;
    (void)ctx;
    // YOUR CODE: your screen. This one is the rows of text.
    for (int r = 0; r < ROWS; r++) cvs_line(c, 16, 8 * r, 8 * COLS, text[r], ink[r], C_BLACK, 1);
}
//@end
//@if frames

// ---- the picture ------------------------------------------------------------------------
// The frame kernel sets up Graphics II: 768 names at $1800 say which tile
// is at each 8x8 place, and each third of the screen has its own 256 tiles:
// patterns at $0000, colors at $2000, 8 bytes a tile. Here tiles 32-95 are
// the font, in each third, and a place's name is its character.
//
// want[] is the video memory the plugin wants and shown[] what the console
// will hold once the lists sent so far have run. Each frame's list carries
// as many of the bytes that differ as fit in its time (examples/frames).
#define NAMES    0x1800
#define SPRITES  0x1B00         // (the sprites' tables, to $1FFF: not written)
#define COLORS   0x2000
#define VRAM_END 0x3800
static uint8_t want[VRAM_END], shown[VRAM_END];

static void picture_start(void) {
    for (int third = 0; third < 3; third++)
        for (int ch = 32; ch < 96; ch++)
            for (int r = 0; r < 8; r++) {
                const int i = third * 0x800 + ch * 8 + r;
                want[i] = font8[ch - 32][r];
                want[COLORS + i] = 0xF0 | C_BLACK;      // white on black
                shown[i] = (uint8_t)~want[i];           // what the console has there isn't known: send it
                shown[COLORS + i] = (uint8_t)~want[COLORS + i];
            }
    memset(shown + NAMES, 0xFF, 768);
}

// The whole picture, every frame
static void picture(void) {
    // YOUR CODE: your picture. This one is the rows of text.
    for (int r = 0; r < ROWS; r++) {
        const int n = (int)strlen(text[r]);
        for (int col = 0; col < 32; col++) {
            const int i = col - 2;                      // two places each side: the TV cuts the edges
            const char ch = i >= 0 && i < n ? text[r][i] : ' ';
            want[NAMES + 32 * r + col] = (uint8_t)(ch >= 32 && ch < 96 ? ch : '?');
        }
    }
}

// What each command costs in Z80 cycles (gbkernel.asm's header)
#define FRAME_T     (262 * 228)
#define BLANK_T     (70 * 228)
#define BLANK_LIMIT (BLANK_T - 2000)    // the first part must end inside the blank...
#define LIST_LIMIT  (FRAME_T - 4000)    // ...and the list well inside the frame
#define RUN_T       106
#define PSG_T(n)    (75 + 52 * (n))     // with the ~29 T the sound chip holds the Z80 for at each byte
#define END_T       54
static uint8_t list[CVF_LIST_MAX];
static size_t len;
static uint32_t cost, cursor;

// The bytes of want[] that differ from shown[], as runs, until `limit`
// cycles are used up at per_byte cycles a byte. Everything in a list must
// be safe to run twice: with nothing new, the console runs the last again.
static void diff(uint32_t limit, uint32_t per_byte) {
    for (uint32_t scanned = 0; scanned < VRAM_END;) {
        if (cursor == SPRITES) cursor = COLORS;
        const uint32_t i = cursor, end = i < NAMES ? NAMES : i < SPRITES ? SPRITES : VRAM_END;
        if (want[i] == shown[i]) {
            cursor = (cursor + 1) % VRAM_END;
            scanned++;
            continue;
        }
        if (cost + RUN_T + per_byte > limit) return;
        // a run from here: up to 255 bytes, to the table's end, while the
        // time lasts, stopping at 4 bytes in a row that are right already
        uint32_t n = 0, same = 0;
        while (n < 255 && i + n < end && cost + RUN_T + per_byte * (n + 1) <= limit) {
            same = want[i + n] == shown[i + n] ? same + 1 : 0;
            if (same == 4) break;
            n++;
        }
        n -= same == 4 ? 3 : same;                      // not the right ones at its end
        list[len++] = 1;
        list[len++] = (uint8_t)i;
        list[len++] = (uint8_t)(i >> 8 | 0x40);
        list[len++] = (uint8_t)n;
        memcpy(list + len, want + i, n);
        memcpy(shown + i, want + i, n);                 // it will be so, once this list has run
        len += n;
        cost += RUN_T + per_byte * n;
        cursor = (i + n) % VRAM_END;
        scanned += n;
    }
}

// ---- the sound ----------------------------------------------------------------------------
// The sound chip's ten bytes: tone low, tone high and volume for voices 0
// to 2, then the noise's volume (a volume's low 4 bits: 0 loudest, 15 off).
// Every list carries all of them, changed or not. Now and then the console
// reads a byte from the cart wrong, and a register written only once would
// keep the wrong value, a drone, until RESET.
static uint8_t psg[10] = {0x80, 0x00, 0x9F, 0xA0, 0x00, 0xBF, 0xC0, 0x00, 0xDF, 0xFF};   // all off

static void sound(void) {
    // YOUR CODE: your sound, into psg[]. Make it about as loud as an
    // ordinary ColecoVision cartridge (docs/testing.md, "How loud").
    list[len++] = 2;
    list[len++] = sizeof psg;
    memcpy(list + len, psg, sizeof psg);
    len += sizeof psg;
    cost += PSG_T(sizeof psg);
}
//@end

// ---- the start, and the loop ------------------------------------------------------------
static void rows_start(void) {
    say(ROW_TITLE, C_LYELLOW, "%s", NAME);
    say(ROW_BY, C_GRAY, "BY %s", BY);
//@if midi-out
    say(ROW_MIDI_OUT, C_GRAY, "MIDI OUT: KEYPAD 1-8 PLAY");
//@end
    say(ROW_RESET, C_GRAY, "RESET: BACK TO THE MENU");
}

int main(void) {
//@if text
    // First thing: the console is parked, waiting for a cartridge to appear.
    static const cvk_config_t cfg = {rom_image, addr_line, data_bit, handoff_bin, HANDOFF_BIN_LEN};
    cvk_start(&cfg);
    cvs_init(&screen);
//@end
//@if frames
    // First thing: the console is parked, waiting for a cartridge to appear.
    static const cvf_config_t cfg = {gbkernel_bin, GBKERNEL_BIN_LEN, handoff_bin, HANDOFF_BIN_LEN};
    cvf_start(&cfg);
    cvd_start();
    picture_start();
//@end
    cvin_init(&in);
    leave_note(NAME);
    rows_start();
//@if psram
    psram_start();
//@end
//@if saves
    save_start();
//@end
//@if opens
    open_start();
//@end
//@if usb
    cvusb_start();              // after the kernel's start: @USB_START@
//@end

    // YOUR CODE: set up your plugin here

//@if text
    uint32_t t_step = 0;
//@end
//@if frames
    bool pending = false;       // a list is made and not taken yet
    uint32_t seen = 0;
//@end
    for (;;) {
        const uint32_t t = time_us_32();
//@if text
        // Every pass: is the console still on, has RESET been pressed? On
        // RESET this doesn't return: the cart goes back to the menu.
        cvk_task(t);
//@end
//@if frames
        // Every pass: is the console still on, has RESET been pressed? On
        // RESET this doesn't return: the cart goes back to the menu.
        cvf_task(t);
//@end
//@if usb
        cvusb_task();           // every pass: the USB port's work (it never waits)
//@end
//@if keyboard
        cvkey_t k;
        while (cvusb_key(&k)) key_in(&k);
//@end
//@if midi-in
        uint8_t m[3];
        int mn;
        while ((mn = cvusb_midi_read(m)) > 0) midi_in(m, mn);
//@end
//@if text
        // What the console says, once a TV frame: how far it got with the
        // screen (HELLO, ACK), and the controller.
        int kind;
        uint8_t bits;
        while (cvk_report(t, &kind, &bits)) {
            if (kind == REPORT_KIND_HELLO || kind == REPORT_KIND_ACK)
                cvs_report(&screen, kind == REPORT_KIND_HELLO, bits & 31, t);
            else
                cvin_report(&in, kind, bits);
        }
        // The step and the screen, 50 times a second
        if (t - t_step < 20000) continue;
        t_step = t;
        step();
        const size_t n = cvs_update(&screen, draw, NULL, list, sizeof list, t);
        if (n) cvk_list(list, n, screen.out_full, screen.seq);
//@end
//@if frames
        // Never drop a list: shown[] already counts on it. Offer it until it is taken.
        if (pending && cvf_publish(list, len)) pending = false;
        if (pending || cvf_frames() == seen) continue;
        seen = cvf_frames();                            // a TV frame has started: make the list for the next
        uint8_t joy, fire, key;
        cvf_input(&joy, &fire, &key);                   // the controller, as the console read it this frame
        cvin_frame(&in, joy, fire, key);
        step();
        picture();
        len = 0;
        cost = 480;                                     // the kernel's own start of a frame
        sound();                                        // the whole sound chip, every list
        diff(BLANK_LIMIT, 23);                          // the blank part: 23 cycles a byte
        list[len++] = 0;
        cost += END_T;
        diff(LIST_LIMIT, 31);                           // while the picture is drawn: 31
        list[len++] = 0;
        pending = true;
//@end
    }
}
"""


def rows(p):
    groups = [["TITLE", "BY"], ["STICK", "KEYPAD"]]
    if p.has(*USB_FEATURES):
        groups.append(["USB"])
    if p.has("pads"):
        groups.append(["PAD", "", "", "", "", ""])
    kb = (["KEY", "TYPED"] if p.has("keyboard") else []) + (["MOUSE"] if p.has("mouse") else [])
    if kb:
        groups.append(kb)
    midi = (["MIDI_IN"] if p.has("midi-in") else []) + (["MIDI_OUT"] if p.has("midi-out") else [])
    if midi:
        groups.append(midi)
    mem = (["PSRAM"] if p.has("psram") else []) + (["SAVE"] if p.has("saves") else [])
    if mem:
        groups.append(mem)
    if p.opens:
        groups.append(["FILE", "FILE_SIZE"])
    used = sum(len(g) for g in groups)
    gaps = 21 - used                # rows 1-21 (a TV cuts the top and bottom ones), 22 RESET's
    out, row = [], 1
    for i, g in enumerate(groups):
        if i and gaps > 0:
            row += 1
            gaps -= 1
        for name in g:
            if name:
                out.append(f"ROW_{name} = {row},")
            row += 1
    out.append("ROW_RESET = 22,")
    lines, line = [], "   "
    for item in out:
        if len(line) + 1 + len(item) > 96:
            lines.append(line)
            line = "   "
        line += " " + item
    lines.append(line)
    return "\n".join(lines)


def english(items):
    return items[0] if len(items) == 1 else ", ".join(items[:-1]) + " and " + items[-1]


def main_c(p):
    flags = set(p.features) | {p.screen}
    if p.opens:
        flags.add("opens")
    if p.has(*USB_FEATURES):
        flags.add("usb")
    if p.has(*HOST_FEATURES) and p.has("midi-out"):
        flags.add("ask")
    phrases = {"pads": "USB game pads", "keyboard": "a USB keyboard", "mouse": "a USB mouse", "midi-in": "MIDI in",
               "midi-out": "MIDI out", "psram": "the PSRAM", "saves": "a save file on the card"}
    things = [phrases[k] for k in p.features]
    if p.opens:
        things.append("the file the menu opens it with (" + " ".join(p.opens) + ")")
    feats = english(things) if things else "no features: the console's controller and the screen"
    usb = [{"PADS": "pads", "KEYBOARD": "keyboard", "MOUSE": "mouse", "MIDI_IN": "MIDI in", "MIDI_OUT": "MIDI out"}[w]
           for w in p.words if w not in ("PSRAM", "CARD")]
    host, dev = p.has(*HOST_FEATURES), p.has("midi-out")
    if host and dev:
        start = "a MIDI device first; a host if no computer takes it in 1.5 s"
    elif dev:
        start = "a MIDI device, for a computer"
    else:
        start = "a host, for what is plugged into the hub"
    header = (f"SPDX-License-Identifier: {p.license}\nCopyright (c) {datetime.date.today().year} {p.by}\n\n" +
              wrap(f"{p.name}: {p.about}", 76) + "\n\n" +
              wrap(f"Made by tools/new-plugin.py, with {feats}. It builds and runs as it is, with each feature on "
                   "the TV, working. YOUR CODE marks where yours goes; take out what you don't want to show. "
                   "docs/features.md has the features, sdk/firmware/revc/sdk/README.md the SDK.", 76))
    values = {
        "HEADER": "\n".join(("// " + line).rstrip() for line in header.split("\n")),
        "PLAYER_TEXT": "the console's controller and USB pad 1" if p.has("pads") else "the console's controller",
        "USB_TEXT": english(usb) if usb else "",
        "NAME_C": c_str(p.name),
        "BY_C": c_str(p.by),
        "NOTE_NAME_C": c_str(p.note_name).replace("%", "%%"),
        "SHOWER": "draw()" if p.screen == "text" else "picture()",
        "ROWS": rows(p),
        "MIDI_IN_FROM": ("From a MIDI keyboard on the hub, or from the computer when the cart\n// is its MIDI device."
                         if dev else "From a MIDI keyboard on the hub."),
        "MIDI_OUT_TO": ("To the computer when the cart is its MIDI device, or to a MIDI\n// keyboard on the hub."
                        if p.has("midi-in") else "To the computer: the cart is its MIDI device."),
        "SAVE_FOLDER": p.save_folder,
        "SAVE_FILE": p.save_file,
        "SAVE_MAGIC": p.save_magic,
        "STEP_HEADER": ("// ---- the step: " + ("50 times a second " if p.screen == "text" else "every TV frame ")).ljust(92, "-"),
        "USB_START": start,
        "OPENS_TEXT": " ".join(p.opens),
    }
    return render(MAIN_C, flags, values)


def cmakelists(p):
    kernel = ("parrotcart_screen({t})           # text and pictures, drawn for you: the screen kernel"
              if p.screen == "text" else
              "parrotcart_frames({t})           # a whole picture every TV frame: the frame kernel").format(t=p.target)
    feats = f"cvcart_features({p.target} {' '.join(p.words)})" if p.words else f"# cvcart_features({p.target} PADS ...)"
    extra = []
    if p.home:
        extra.append(f"HOME {p.home}")
    if p.opens:
        extra.append("OPENS " + " ".join(p.opens) + " FILE_ICON src/file_icon.txt")
    extra = ("\n              " + " ".join(extra)) if extra else ""
    return f"""# {p.name}: {p.about}
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/{p.file}
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Made by tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project({p.target} C CXX ASM)
parrotcart_init()

add_executable({p.target} src/main.c)
{kernel}

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
{feats}

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin({p.target} ID {p.id} NAME "{p.name}" VERSION {p.version} BY "{p.by}"
              ICON src/icon.txt README README.md FILE {p.file}{extra})
"""


def readme(p):
    md = lambda s: s.replace("*", "\\*").replace("_", "\\_")
    controls = ["Stick, fire buttons and keypad: shown on the TV and counted as you press them"]
    if p.has("pads"):
        controls.append("A USB game pad: works like the console's controller. The d-pad or left stick is "
                        "the stick, B and A are the fire buttons, and X and Y are the keypad's \\* and #. "
                        "Its buttons and sticks show on the TV.")
    if p.has("keyboard"):
        controls.append("A USB keyboard: what you type shows on the TV")
    if p.has("mouse"):
        controls.append("A USB mouse: its position, buttons and wheel show on the TV")
    if p.has("midi-out"):
        controls.append("Keypad 1-8: send MIDI notes, C to C")
    controls.append("RESET: back to the cart's menu")
    needs = []
    if p.has(*HOST_FEATURES):
        things = english([t for k, t in [("pads", "game pads"), ("keyboard", "a keyboard"), ("mouse", "a mouse"),
                                          ("midi-in", "a USB MIDI keyboard")] if p.has(k)])
        needs.append(f"For {things}, you need a powered USB hub between them and the cart, because the cart "
                     "can't power them.")
    if p.has("midi-out"):
        needs.append(f"For MIDI out, you need a computer. Plug the cart into it before you start {md(p.name)}, "
                     f"and it shows up as a MIDI port called {md(p.name)}. On a Mac, allow the accessory to "
                     "connect the first time.")
        if p.has(*HOST_FEATURES):
            needs.append("The USB port works with either a computer or a hub. If no computer connects within "
                         "a second and a half, the cart uses the hub instead.")
    if p.has("saves"):
        if p.home:
            needs.append(f"Saves go in `{p.home}/{p.save_file}`. Make the folder `{p.home}` on the card first.")
        else:
            needs.append(f"Saves go in `/PLUGINS/{p.save_file}`, next to the plugin.")
    if p.opens:
        needs.append("It opens " + english([f"`{e}`" for e in p.opens]) + " files from the card. Press A on one "
                     "in the card's folders to start it with that file.")
    if p.home and not p.has("saves"):
        needs.append(f"Its files go in `{p.home}` on the card.")
    credits = [f"Parrot Cart plugin template and SDK, by the Parrot Cart contributors, MIT: <{TEMPLATE_URL}>"]
    if p.has(*USB_FEATURES):
        credits.append("TinyUSB, by Ha Thach and contributors, MIT: <https://github.com/hathach/tinyusb>")
    credits.append("Raspberry Pi Pico SDK, BSD 3-Clause: <https://github.com/raspberrypi/pico-sdk>")
    out = [f"# {md(p.name)}", "", p.about, "", "## Controls", ""]
    out += [f"- {c}" for c in controls]
    if needs:
        out += ["", "## What it needs", ""] + [f"- {n}" for n in needs]
    out += ["", "## Credits", ""] + [f"- {c}" for c in credits]
    out += ["", "Source: put your repository's address here.", "", "## License", "",
            f"{p.license}. See LICENSE. THIRD-PARTY.md lists the parts that came from other projects.", "",
            "## Building", "",
            f"Your code is in `src/main.c`, where it says YOUR CODE. To build `build/{p.file}`:", "",
            "```sh", "cmake -B build -G Ninja \\", "  -DPICO_SDK_PATH=$HOME/pico-sdk", "ninja -C build", "```", "",
            "- docs/getting-started.md: installing the tools, and using the card",
            "- docs/features.md: the features and how to use them",
            f"- docs/testing.md: `test/run.sh {' '.join(p.ci_tests())}` tries it on a model of the console",
            "- docs/simulator.md: `sim/run.sh src` plays it in a window, with sound and controls",
            "- docs/release-checklist.md: what to check before you tag a release", ""]
    return "\n".join(out)


def license_text(p):
    year = datetime.date.today().year
    if p.license in LICENSES:
        return license_file(p.license, year, p.by)
    return (f"{p.license}\n\nCopyright (c) {year} {p.by}\n\n"
            f"Paste the full text of the {p.license} license here, from\n"
            f"https://spdx.org/licenses/{p.license}.html\n")


def third_party(old, p, examples):
    # the first paragraph says what is whose; the template's own license goes at the end
    _, _, rest = old.partition("\n\n")
    _, _, rest = rest.partition("\n\n")
    head = "# What in this repository came from elsewhere"
    parts = "build files, `lib/`, `test/`, `tools/`, `docs/`" + (", `examples/`" if examples else "") + " and workflows"
    intro = (f"The plugin's code is {p.license} (`LICENSE`). The plugin template it started from (its "
             f"{parts}) and the copy of the "
             "cart's SDK in `sdk/` are by the Parrot Cart contributors, under the MIT license below. "
             "These parts came from other projects and keep their original licenses:")
    rest = re.sub(r"\n## The plugin template's license\n.*", "", rest, flags=re.S).rstrip("\n")
    mit = license_file("MIT", 2026, TEMPLATE_HOLDER)
    return f"{head}\n\n{wrap(intro)}\n\n{rest}\n\n## The plugin template's license\n\n```\n{mit}```\n"


def wrap(s, width=74):
    lines, line = [], ""
    for w in s.split():
        if line and len(line) + 1 + len(w) > width:
            lines.append(line)
            line = w
        else:
            line = f"{line} {w}" if line else w
    return "\n".join(lines + [line])


def shq(s):
    return "'" + s.replace("'", "'\\''") + "'"


def ere(s):
    return re.sub(r"([.^$*+?()\[\]{}|\\])", r"\\\1", s)


def plugin_tests(p):
    """The plugin's tests, for test/run.sh and sim/test.sh alike: (name, ms,
    script, what the log must have) each. The card's test is written apart:
    an image in test/run.sh, a folder in sim/test.sh."""
    t = p.test
    want = [f'note "{ere(p.note_name)}: 3 presses"', "back to the menu"]
    if p.screen == "frames":
        want.append("psg 80 00 9F A0 00 BF C0 00 DF FF")          # the whole sound chip, in the lists
    if p.has("midi-out"):
        want += ["the port is a MIDI device", "midi 90 43 64", "midi 80 43 00"]          # keypad 5: G
    tests = [(t, 3000, "500:joy=up+fire 900:joy= 1200:key=5 1500:key= 2000:joy=fire2 2300:joy=", want)]
    if p.has(*HOST_FEATURES):
        ev = []
        if p.has("midi-out"):
            ev.append("0:computer=0")                   # no computer: the port is a host at 1.5 s
        if p.has("pads"):
            ev.append("2000:pad=south+l 2100:lstick=1500,-900 2200:rstick=-2048,2047 2400:pad=east+up")
        if p.has("keyboard"):
            ev.append("2600:keydown=0B 2650:keyup=0B 2700:mods=02 2750:keydown=1E 2800:keyup=1E 2850:mods=0 "
                      "2900:keydown=0C 2950:keyup=0C 3000:keydown=2A 3050:keyup=2A")
        if p.has("mouse"):
            ev.append("3100:mouse=40,20,1,1 3200:mouse=-10,5,0")
        if p.has("midi-in"):
            ev.append("3300:midiin=90,3C,64 3400:midiin=B0,07,64")
        want = ["usb: the port is a host", "back to the menu"]
        if p.has("midi-in") and p.has("midi-out"):
            ev.append("3500:key=3 3600:key=")           # MIDI out to the MIDI keyboard on the hub
            want.append(r"midi 90 40 64 \(to the MIDI device\)")
        ev.append("3800:snap=usb")
        tests.append((t + "-usb", 4000, " ".join(ev), want))
    return tests


OPEN_NOTE = r'note "Opened A first file\.txt"'       # what the plugin's -open test must leave


def test_block(p):
    """test/run.sh's part for the plugin"""
    t = p.test
    out = ["# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----"]
    for name, ms, script, want in plugin_tests(p):
        out += [f"if wanted {name}; then",
                f'    PC_END={ms} PC_SCRIPT="{script}" \\',
                f'        run {name} "$kit/src" ' + " ".join(shq(w) for w in want), "fi"]
    if p.has("saves"):
        path = f"{p.save_folder}/{p.save_file}"
        folder = f" {shq(p.home)}" if p.home else ""
        out += [f"if wanted {t}-card; then",
                f"    PC_END=1500 run {t}-nocard \"$kit/src\" 'back to the menu'",
                f"    card \"MS-DOS FAT32:MBRSPUD:fat32\"{folder}",
                "    for n in 1 2; do",
                f"        PC_CARD=$out/card-fat32.cdr PC_END=2500 run \"{t}-card-$n\" \"$kit/src\" 'back to the menu'",
                "    done",
                f"    card_check fat32 \"MS-DOS FAT32\" 2 {shq(path)} {shq(p.save_magic)}",
                "fi"]
    if p.opens:
        # (the models don't look at the extension: the card's text file will do)
        out += [f"if wanted {t}-open; then",
                "    card \"MS-DOS FAT32:MBRSPUD:fat32\"",
                "    PC_CARD=$out/card-fat32.cdr PC_OPEN=\"/A first file.txt\" PC_END=2500 \\",
                f"        run {t}-open \"$kit/src\" {shq(OPEN_NOTE)} 'back to the menu'",
                "fi"]
    out.append("# ---- the examples ----")
    return "\n".join(out) + "\n"


def sim_test_block(p):
    """sim/test.sh's part for the plugin: the same tests in the simulator,
    headless, and the card as a folder"""
    t = p.test
    out = ["# ---- the plugin in src/ (tools/new-plugin.py rewrites this part) ----"]
    for name, ms, script, want in plugin_tests(p):
        out += [f"if wanted {name}; then",
                f'    run {name} "$kit/src" --ms {ms} --script "{script}"',
                "    expect " + " ".join(shq(w) for w in want), "fi"]
    if p.has("saves"):
        path = f"{p.save_folder}/{p.save_file}"
        out += [f"if wanted {t}-card; then",
                f"    run {t}-nocard \"$kit/src\" --ms 1500",
                "    expect 'back to the menu'",
                '    tree "$out/card"' + (f'; mkdir -p "$out/card"{shq(p.home)}' if p.home else ""),
                "    for n in 1 2; do",
                f"        run \"{t}-card-folder-$n\" \"$kit/src\" --card \"$out/card\" --ms 2500",
                "        expect 'back to the menu'",
                "    done",
                f"    save_check \"$out/card\"{shq(path)} {shq(p.save_magic)} 2",
                "fi"]
    if p.opens:
        out += [f"if wanted {t}-open; then",
                '    tree "$out/card"',
                f"    run {t}-open-folder \"$kit/src\" --card \"$out/card\" --open \"/A first file.txt\" --ms 2500",
                f"    expect {shq(OPEN_NOTE)} 'back to the menu'",
                f"    same_as {t}-open picture",
                "fi"]
    out.append("# ---- the examples ----")
    return "\n".join(out) + "\n"


# ---- writing it --------------------------------------------------------------------------
SKIP = {".git", "build", "__pycache__", ".DS_Store", "settings.local.json", "CLAUDE.local.md"}   # and not your own Claude settings


def copy_kit(dest, examples):
    for dirpath, dirnames, filenames in os.walk(KIT):
        rel = os.path.relpath(dirpath, KIT)
        dirnames[:] = [d for d in dirnames if d not in SKIP and not d.startswith("build-")
                       and not (rel == "." and d == "examples" and not examples)]
        os.makedirs(os.path.join(dest, rel), exist_ok=True)
        for f in filenames:
            if f in SKIP or (rel == "screenshots" and f.startswith("screenshot-")):
                continue
            shutil.copy2(os.path.join(dirpath, f), os.path.join(dest, rel, f))


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)


def is_hello(root):
    main = os.path.join(root, "src", "main.c")
    return not os.path.exists(main) or "The smallest useful Parrot Cart plugin" in open(main).read(4000)


def reshape(root, p, examples):
    # src/: the plugin's main.c and icons, in place of what was there
    src = os.path.join(root, "src")
    for f in ("main.c", "icon.txt", "file_icon.txt"):
        if os.path.exists(os.path.join(src, f)):
            os.remove(os.path.join(src, f))
    write(os.path.join(src, "main.c"), main_c(p))
    icon = open(p.icon).read() if os.path.isfile(p.icon) else "\n".join(ICONS[p.icon]) + "\n"
    write(os.path.join(src, "icon.txt"), icon)
    if p.opens:
        write(os.path.join(src, "file_icon.txt"), "\n".join(FILE_ICON) + "\n")
    write(os.path.join(root, "CMakeLists.txt"), cmakelists(p))
    write(os.path.join(root, "README.md"), readme(p))
    write(os.path.join(root, "LICENSE"), license_text(p))
    tp = os.path.join(root, "THIRD-PARTY.md")
    if os.path.exists(tp):
        write(tp, third_party(open(tp).read(), p, examples))
    # the template's screenshot isn't this plugin's (docs/screenshots.md makes yours)
    shots = os.path.join(root, "screenshots")
    if os.path.isdir(shots):
        for f in os.listdir(shots):
            if f.startswith("screenshot-"):
                os.remove(os.path.join(shots, f))
    # its tests, in the models and in the simulator, and the workflow's list of them
    old_tests = []
    for rel, block, last in ((("test", "run.sh"), test_block, r"\[ \$status = 0 \]"),
                             (("sim", "test.sh"), sim_test_block, r"\[ \$compared = 0 \]")):
        run = os.path.join(root, *rel)
        if not os.path.exists(run):
            continue
        s = open(run).read()
        m = re.search(r"# ---- the plugin in src/.*?\n(.*?)# ---- the examples ----\n", s, re.S)
        if m:
            old_tests += re.findall(r"^if wanted (\S+); then", m.group(1), re.M)
            s = s[:m.start()] + block(p) + s[m.end():]
            if not examples:
                e = re.search(r"(# ---- the examples ----\n)(.*?)(?=^" + last + ")", s, re.S | re.M)
                if e:
                    s = (s[:e.start()] + e.group(1) +
                         "# (left out with examples/: tools/new-plugin.py --examples keeps them)\n\n" + s[e.end():])
            write(run, s)
    wf = os.path.join(root, ".github", "workflows", "build.yml")
    if os.path.exists(wf):
        s = open(wf).read()

        def tests_line(m):
            words = [w for w in m.group(2).split() if w not in old_tests and (examples or w in p.ci_tests())]
            return m.group(1) + " ".join(p.ci_tests() + words)
        s = re.sub(r"(NO_MAME=1 test/run\.sh )([^\n]*)", tests_line, s)
        write(wf, s)


# ---- asking ------------------------------------------------------------------------------
def ask(question, check, default=None):
    while True:
        shown = f" [{default}]" if default not in (None, "") else ""
        try:
            got = input(f"{question}{shown}: ").strip()
        except EOFError:
            print()
            raise SystemExit(1)
        if not got and default is not None:
            got = default
        try:
            return check(got)
        except Bad as e:
            print(f"  {e}")


def one_of(*options):
    def check(s):
        s = s.strip().lower()[:1] if options == ("y", "n") else s.strip()
        if s not in options:
            raise Bad(" or ".join(options))
        return s
    return check


def interview(a):
    print("This makes a new Parrot Cart plugin. Press Enter to use the answer in [brackets].\n")
    a.name = ask("Plugin name, as the menu shows it", check_name, a.name)
    a.by = ask("Your name, as the menu shows it next to the plugin's", check_by, a.by)
    a.id = ask("Plugin ID, which stays the same in every version (docs/plugin-rules.md)", check_id,
               a.id or default_id(a.by, a.name))
    a.about = ask("What the plugin is, in one sentence", lambda s: s.strip(), a.about or
                  "A plugin for the Parrot Cart, the ColecoVision SD cartridge.")
    a.version = ask("First version", check_version, a.version)
    print("\nChoose how the plugin draws the screen:")
    for i, (k, what) in enumerate(SCREENS, 1):
        print(f"  {i}. {k:7} {what}")
    a.screen = ask("Which one", check_screen, a.screen)
    print("\nChoose the features you want (docs/features.md):")
    for i, (k, _, what) in enumerate(FEATURES, 1):
        print(f"  {i}. {k:9} {what}")
    a.features = ask("Which ones (numbers or names, or none)", check_features, ",".join(a.features) if a.features else "none")
    print()
    a.home = ask("A folder on the card for the plugin's files" + (" and saves" if "saves" in a.features else "") +
                 ", like /SWEEPER, or none", lambda s: "" if s.strip().lower() == "none" else check_home(s), a.home or "none")
    a.opens = ask("File types the menu opens with it (it reads them from the card), like .swp .lvl, or none",
                  lambda s: [] if s.strip().lower() == "none" else check_opens(s), " ".join(a.opens) if a.opens else "none")
    print("\nChoose an icon (8x8, docs/icons.md):")
    print("  " + "  ".join(f"{i} {k:8}" for i, k in enumerate(ICONS, 1)))
    for r in range(8):
        print("  " + "  ".join(f"  {ICONS[k][r]}" for k in ICONS).replace(".", " "))
    default_icon = "note" if {"midi-in", "midi-out"} & set(a.features) else "pad" if "pads" in a.features else "star"
    a.icon = ask("Which one, or a file you made", check_icon, a.icon or default_icon)
    print("\nChoose a license for your code, as an SPDX ID. These come with the full text:")
    print("  " + "\n  ".join(wrap(", ".join(LICENSES), 72).split("\n")))
    print("For any other license, you get a LICENSE file to paste its text into.")
    a.license = ask("Which one", check_license, a.license)
    if not a.here and not a.out:
        print("\nWhere should the plugin go?")
        print("  1. a new folder, with a copy of this kit made into the plugin")
        print("  2. here, turning this kit into the plugin in place of Hello")
        where = ask("Which one", one_of("1", "2"), "1")
        if where == "2":
            a.here = True
        else:
            a.out = ask("Which folder", lambda s: s, default_out(a.name))
    if a.examples is None:
        a.examples = ask("\nKeep examples/, the small plugins that each show one thing? y/n",
                         one_of("y", "n"), "n") == "y"
    p = Plugin(a)
    print(f"\n{p.name} {p.version} by {p.by} ({p.id}): {p.screen} screen, "
          f"features: {', '.join(p.features) or 'none'}, " + (f"opens {' '.join(p.opens)}, " if p.opens else "") +
          f"{p.license}, to {'this kit' if a.here else a.out}")
    if ask("Write the plugin? y/n", one_of("y", "n"), "y") != "y":
        raise SystemExit(1)
    return p


def default_out(name):
    return os.path.relpath(os.path.join(os.path.dirname(KIT), slug(name) or "plugin"))


def main():
    ap = argparse.ArgumentParser(
        description="Start a Parrot Cart plugin with the features you pick, already working and shown on "
                    "the TV. Without --name, it asks you one question at a time.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="features: " + ", ".join(FEATURE_KEYS) + " (or all, none)\n"
               "icons: " + ", ".join(ICONS) + ", or a file\n"
               "example:\n  tools/new-plugin.py --name \"Star Sweeper\" --by \"Ann\" "
               "--features pads,keyboard,midi-in --screen text")
    ap.add_argument("--name", help="the plugin's name as the menu shows it, up to 32 characters")
    ap.add_argument("--by", help="your name, as the menu shows it")
    ap.add_argument("--id", help="developer.pluginname (default: from --by and --name)")
    ap.add_argument("--about", help="what the plugin is, in one sentence (the README's first line)")
    ap.add_argument("--version", default="0.1.0", help="the first version (default 0.1.0)")
    ap.add_argument("--screen", default="text", help="text (the default) or frames")
    ap.add_argument("--features", default="", help="comma-separated (default: none)")
    ap.add_argument("--home", default="", help="the plugin's folder on the card, like /SWEEPER (default: none)")
    ap.add_argument("--opens", default="",
                    help="file types the menu opens with it, like '.swp .lvl'. It turns on CARD, and src/main.c "
                         "opens the file and shows its name and size (default: none)")
    ap.add_argument("--icon", help="an icon's name, or a file (default: one that suits the features)")
    ap.add_argument("--license", default="MIT",
                    help="an SPDX ID for your code (default MIT). The full text is built in for " + ", ".join(LICENSES) +
                         ". Any other license gets a LICENSE file to paste its text into")
    ap.add_argument("--out", help="a new folder for the plugin (default: next to this kit, named after the plugin)")
    ap.add_argument("--here", action="store_true", help="make this kit into the plugin, in place of Hello")
    ap.add_argument("--examples", action="store_true", default=None,
                    help="keep examples/, the small plugins that each show one thing (default: left out)")
    ap.add_argument("--force", action="store_true", help="write over src/main.c that isn't Hello, or into a folder that isn't empty")
    a = ap.parse_args()
    if a.here and a.out:
        ap.error("--here or --out, not both")
    try:
        a.screen = check_screen(a.screen)
        a.features = check_features(a.features)
        a.home = check_home(a.home)
        a.opens = check_opens(a.opens)
        a.license = check_license(a.license)
        a.version = check_version(a.version)
        if a.icon:
            a.icon = check_icon(a.icon)
        if a.name is None:
            if not sys.stdin.isatty():
                ap.error("give --name and --by, or run it in a terminal to answer questions")
            p = interview(a)
        else:
            a.name = check_name(a.name)
            if a.by is None:
                ap.error("--by needs your name, as the menu shows it")
            a.by = check_by(a.by)
            a.id = check_id(a.id) if a.id else None
            p = Plugin(a)
    except Bad as e:
        raise SystemExit(f"new-plugin.py: {e}")

    if a.here:
        root = KIT
        if not is_hello(root) and not a.force:
            raise SystemExit("new-plugin.py: src/main.c isn't the template's Hello, so it may be your code. "
                             "Use --force to write over it")
        examples = bool(a.examples)
        if not examples and os.path.isdir(os.path.join(root, "examples")):
            shutil.rmtree(os.path.join(root, "examples"))
    else:
        root = os.path.abspath(a.out or default_out(p.name))
        if os.path.commonpath([root, KIT]) == KIT:
            raise SystemExit(f"new-plugin.py: {root} is inside this kit. Please pick a folder outside it")
        if os.path.exists(root) and os.listdir(root) and not a.force:
            raise SystemExit(f"new-plugin.py: {root} isn't empty. Use --force to write into it")
        examples = bool(a.examples)
        copy_kit(root, examples)
    reshape(root, p, examples)

    where = os.path.relpath(root)
    print(f"{p.name}: {where}")
    print(f"  CMakeLists.txt   {p.id} {p.version}, {p.screen} screen, features: {' '.join(p.words) or 'none'}" +
          (f", opens {' '.join(p.opens)}" if p.opens else ""))
    print("  src/main.c       each feature on the TV, with YOUR CODE where your code goes")
    print(f"  README.md        controls, what it needs and credits (license {p.license})")
    print(f"  test/run.sh      tests for it: {' '.join(p.tests())} (and in sim/test.sh)")
    if p.license not in LICENSES:
        print(f"  LICENSE          paste the {p.license} text into it")
    print("  examples/        " + ("kept" if examples else "left out (--examples keeps it)"))
    print("Next steps:")
    if root != KIT:
        print(f"  cd {where}")
    print("  cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk && ninja -C build")
    print(f"  test/run.sh {' '.join(p.tests())}")
    print("  sim/run.sh src                     (in a window, see docs/simulator.md)")


if __name__ == "__main__":
    main()
