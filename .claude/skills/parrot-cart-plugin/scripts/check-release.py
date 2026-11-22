#!/usr/bin/env python3
"""check-release.py [PLUGIN_DIR] [--cvp FILE.CVP] -- is this plugin ready to
release and list on the Parrot Cart plugins site?

Run it in the plugin's folder (a copy of the starter kit). It reads
CMakeLists.txt's cvcart_plugin() line, README.md, the license files, the
screenshots, the icons and the git tags, and looks through the files for
games, ROMs and BIOS images, which must never be in a plugin's repository.
It also checks the timing: a frame plugin's lists served by the two-sample
bus loop, and no change to the 150 MHz clock that loop is timed for. With
--cvp it also checks a built .CVP against CMakeLists.txt.

Each line is "ok", "warn" (worth fixing) or "FAIL" (fix it before the
release). It exits 1 if anything failed. Python 3.8 or newer, standard
library only."""
import argparse
import os
import re
import struct
import subprocess
import sys

results = []


def ok(msg):
    results.append(("ok", msg))


def warn(msg):
    results.append(("warn", msg))


def fail(msg):
    results.append(("FAIL", msg))


# ---- CMakeLists.txt -----------------------------------------------------------------
def cmake_call(text):
    """the cvcart_plugin(...) call's words, comments removed"""
    text = re.sub(r"#[^\n]*", "", text)
    m = re.search(r"\bcvcart_plugin\s*\(", text)
    if not m:
        return None
    depth, i = 1, m.end()
    while i < len(text) and depth:
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    body = text[m.end():i - 1]
    return re.findall(r'"[^"]*"|\S+', body)


KEYS = {"ID", "NAME", "VERSION", "BY", "HOME", "OPENS", "USES", "ICON", "FILE_ICON", "README", "FILE",
        "HEAP", "XIP"}


def cmake_fields(words):
    f, key = {}, None
    for w in words[1:]:
        if w in KEYS:
            key = w
            f.setdefault(key, [])
        elif key:
            f[key].append(w.strip('"'))
    return {k: " ".join(v) for k, v in f.items()}, words[0]


def version_tuple(v):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", v or "")
    return tuple(int(x) for x in m.groups()) if m else None


def check_cmake(root):
    p = os.path.join(root, "CMakeLists.txt")
    if not os.path.exists(p):
        fail("no CMakeLists.txt: run this in the plugin's folder")
        return None, None
    words = cmake_call(open(p, encoding="utf-8").read())
    if not words:
        fail("CMakeLists.txt has no cvcart_plugin() call")
        return None, None
    f, target = cmake_fields(words)
    pid = f.get("ID", "")
    if not re.fullmatch(r"[a-z0-9_-]+(\.[a-z0-9_.-]+)+", pid) or len(pid) > 32:
        fail(f"ID {pid!r}: it's developer.pluginname, lower case, digits, . - _, up to 32 characters")
    elif pid.startswith("yourname.") or pid == "yourname.hello":
        fail(f"ID {pid}: still the template's. Use your own developer name, for good (docs/plugin-rules.md)")
    elif pid.startswith("parrotcart."):
        warn(f"ID {pid}: parrotcart. is for the cart's own plugins. Use your own developer name")
    else:
        ok(f"ID {pid}")
    name = f.get("NAME", "")
    if not name or len(name) > 32:
        fail(f"NAME {name!r}: 1 to 32 characters")
    elif name == "Hello" and pid.endswith(".hello"):
        warn("NAME is still Hello")
    by = f.get("BY", "")
    if not by:
        warn("no BY: the menu shows a developer name next to plugins with the same name")
    elif by == "Your Name":
        fail('BY is still "Your Name"')
    v = f.get("VERSION", "")
    if not version_tuple(v):
        fail(f"VERSION {v!r}: three numbers, like 1.0.3")
    else:
        ok(f"VERSION {v}")
    for k in ("ICON", "FILE_ICON"):
        if k in f:
            check_icon(os.path.join(root, f[k]), k)
    if "ICON" not in f:
        warn("no ICON: the menu shows its plug icon (docs/icons.md)")
    check_opens(f.get("OPENS", "").split())
    if "OPENS" in f and "FILE_ICON" not in f:
        warn("OPENS without FILE_ICON: its files get the plain file icon")
    if "README" not in f:
        warn("no README in cvcart_plugin(): the cart's details page will have no text")
    return f, target


def check_opens(exts):
    """Any extension the cart can take is fine, familiar or not (.mx1, .lvl):
    up to 8, each a dot and 1 to 7 letters or digits, as the SDK's mkcvp.py
    builds them"""
    if not exts:
        return
    bad = [e for e in exts if not re.fullmatch(r"\.[A-Za-z0-9]{1,7}", e)]
    if bad:
        fail(f"OPENS {' '.join(bad)}: each is a dot and 1 to 7 letters or digits, like .nes")
    elif len(exts) > 8:
        fail(f"OPENS has {len(exts)} extensions: 8 at most")
    elif len({e.lower() for e in exts}) < len(exts):
        warn(f"OPENS {' '.join(exts)} lists one twice (case doesn't count)")
    else:
        ok(f"OPENS {' '.join(exts)}")


def check_icon(path, what):
    if not os.path.exists(path):
        fail(f"{what} {path}: no such file")
        return
    rows = [r.strip() for r in open(path, encoding="utf-8") if r.strip()]
    if len(rows) != 8 or any(not re.fullmatch(r"[#.]{8}", r) for r in rows):
        fail(f"{what} {os.path.basename(path)}: 8 lines of 8 characters, # or .")
    elif all(set(r) == {"."} for r in rows):
        warn(f"{what} {os.path.basename(path)} has no pixels on, so it counts as no icon")
    else:
        ok(f"{what} {os.path.basename(path)}")


# ---- git ------------------------------------------------------------------------------
def git(root, *a):
    try:
        r = subprocess.run(["git", "-C", root, *a], capture_output=True, text=True, timeout=30)
        return r.stdout if r.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


def check_tags(root, version):
    tags = git(root, "tag", "--list", "v*")
    if tags is None:
        warn("not a git repository: a release is a git tag on GitHub")
        return
    vs = sorted(t for t in (version_tuple(x[1:]) for x in tags.split()) if t)
    want = version_tuple(version)
    if not want:
        return
    if want in vs:
        fail(f"v{version} is already tagged: raise VERSION for a new release")
    elif vs and want < vs[-1]:
        fail(f"VERSION {version} is lower than the last release, v{'.'.join(map(str, vs[-1]))}")
    else:
        ok(f"the release's tag will be v{version}" + (f" (last: v{'.'.join(map(str, vs[-1]))})" if vs else ""))


# ---- README, license -------------------------------------------------------------------
def check_readme(root):
    p = os.path.join(root, "README.md")
    if not os.path.exists(p):
        fail("no README.md: it's the plugin's manual on the cart and the site")
        return
    t = open(p, encoding="utf-8").read()
    low = t.lower()
    if "put your repository's address here" in low:
        fail("README.md still says to put your repository's address there: link to your source")
    elif not re.search(r"https?://\S+", t):
        fail("README.md has no link: it needs a link to the plugin's source")
    if not re.search(r"^#+\s*credits", t, re.M | re.I):
        fail("README.md has no Credits heading: list every core, library, font, sound and picture "
             "that isn't yours, with its license and a link to its source")
    else:
        needs = [("pico-sdk", "the Raspberry Pi Pico SDK"), ("parrot", "the Parrot Cart SDK")]
        for word, what in needs:
            if word not in low.replace(" ", "-") and word not in low:
                warn(f"README.md's credits don't seem to mention {what}, which every plugin contains")
        ok("README.md has credits")
    if not re.search(r"^#+\s*licen[cs]e", t, re.M | re.I):
        warn("README.md has no License heading")
    if "## making your own plugin" in low:
        warn("README.md still has the template's 'Making your own plugin' section")
    if "hello is the smallest plugin" in low:
        fail("README.md is still the template's")
    conv = os.path.join(root, "sdk", "firmware", "revc", "sdk", "cvreadme.py")
    if os.path.exists(conv):
        try:
            out = subprocess.run([sys.executable, conv, p], capture_output=True, timeout=30)
            if out.returncode == 0:
                n = len(out.stdout)
                lines = out.stdout.count(b"\n")
                (ok if n <= 16384 else warn)(f"the cart's README text: {lines} lines, {n} bytes"
                                             + ("" if n <= 16384 else ", over 16,384, so the cart cuts it"))
        except (OSError, subprocess.SubprocessError):
            pass


def check_license(root):
    if not os.path.exists(os.path.join(root, "LICENSE")):
        fail("no LICENSE file")
    else:
        ok("LICENSE")
    if not os.path.exists(os.path.join(root, "THIRD-PARTY.md")):
        warn("no THIRD-PARTY.md: keep the template's, which covers the SDK's parts")
    if not os.path.exists(os.path.join(root, "RELEASE_NOTES.md")):
        warn("no RELEASE_NOTES.md: the release and the plugins site show it (optional)")


# ---- screenshots -------------------------------------------------------------------------
def png_size(path):
    with open(path, "rb") as f:
        h = f.read(24)
    if len(h) < 24 or h[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", h[16:24])


def check_screenshots(root):
    d = os.path.join(root, "screenshots")
    shots = sorted(n for n in os.listdir(d) if re.fullmatch(r"screenshot-[123]\.png", n)) if os.path.isdir(d) else []
    if not shots:
        warn("no screenshots/screenshot-1.png: the plugins site shows up to three. "
             "Make them with the simulator's --shot (256x192)")
        return
    for n in shots:
        p = os.path.join(d, n)
        size = png_size(p)
        if size != (256, 192):
            fail(f"screenshots/{n}: {size[0]}x{size[1] if size else ''} pixels, it must be exactly 256x192"
                 if size else f"screenshots/{n}: not a PNG")
        elif os.path.getsize(p) > 100 * 1024:
            fail(f"screenshots/{n}: {os.path.getsize(p) // 1024} KB, it must be under 100 KB")
        else:
            ok(f"screenshots/{n}")


# ---- games and ROMs ----------------------------------------------------------------------
ROM_EXT = {".nes", ".fds", ".gb", ".gbc", ".gba", ".sms", ".gg", ".sg", ".col", ".rom", ".pce", ".a26",
           ".vec", ".int", ".gen", ".smc", ".sfc", ".lfl", ".sou", ".sav", ".state", ".zip", ".7z",
           ".rar", ".cvp", ".uf2", ".elf"}
SKIP_DIRS = {".git", "build", "__pycache__", "node_modules"}


def repo_files(root):
    listed = git(root, "ls-files", "-co", "--exclude-standard")
    if listed is not None:
        return [l for l in listed.splitlines() if l]
    out = []
    for dp, dns, fns in os.walk(root):
        dns[:] = [d for d in dns if d not in SKIP_DIRS and not d.startswith("build-")]
        out += [os.path.relpath(os.path.join(dp, f), root) for f in fns]
    return out


def rom_magic(data):
    if data[:4] == b"NES\x1a":
        return "an iNES ROM header"
    if len(data) >= 8192 and data[:2] in (b"\xaa\x55", b"\x55\xaa"):
        return "a ColecoVision ROM header"
    if len(data) > 0x150 and data[0x104:0x108] == b"\xce\xed\x66\x66":
        return "the Game Boy logo"
    for at in (0x1FF0, 0x3FF0, 0x7FF0):            # where the Master System's header goes
        if data[at:at + 8] == b"TMR " + b"SEGA":
            return "a Sega header"
    return None


def check_roms(root):
    bad = 0
    for rel in repo_files(root):
        if rel.startswith(("sdk/", "lib/", "sim/third_party/")):     # the SDK's own files
            continue
        p = os.path.join(root, rel)
        ext = os.path.splitext(rel)[1].lower()
        if ext in ROM_EXT:
            fail(f"{rel}: a {ext} file. Games, saves, ROMs and built plugins don't go in the repository "
                 "(a release attaches the .CVP)")
            bad += 1
            continue
        try:
            if os.path.getsize(p) < 16 * 1024 * 1024:
                with open(p, "rb") as f:
                    why = rom_magic(f.read(0x8000))
                if why:
                    fail(f"{rel}: it starts like a game ({why}). Never commit games or BIOS images")
                    bad += 1
        except OSError:
            pass
    if not bad:
        ok("no games, ROMs or saves among the files")


# ---- the timing ----------------------------------------------------------------------------
CLOCK = re.compile(r"\b(set_sys_clock_\w+|clock_configure\w*|vreg_set_voltage)\s*\(")


def check_timing(root):
    """core 1's bus loop: the two-sample one for frame lists, and the 150 MHz it's timed for"""
    p = os.path.join(root, "CMakeLists.txt")
    text = re.sub(r"#[^\n]*", "", open(p, encoding="utf-8").read()) if os.path.exists(p) else ""
    frames = re.search(r"\b(parrotcart_frames|cvframes_use)\s*\(", text)
    if re.search(r"\bcvframes_use\s*\(", text) and not re.search(r"\b(parrotcart_frames|cvframes_serve2)\s*\(", text):
        fail("cvframes_use() without cvframes_serve2(): core 1 would serve the lists with the old loop, which "
             "can hand the console a wrong byte. Use parrotcart_frames(), which has the two-sample loop")
    elif frames:
        ok("the frame lists are served by the two-sample bus loop")
    bad = []
    for rel in repo_files(root):
        if rel.startswith(("sdk/", "sim/", "test/", "lua/", "lib/")) or not rel.endswith((".c", ".cpp", ".cc", ".h")):
            continue
        try:
            if CLOCK.search(open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()):
                bad.append(rel)
        except OSError:
            pass
    if bad:
        fail(f"{', '.join(bad[:3])}: changes the clock. The SDK's bus loop is timed for 150 MHz, and a "
             "200 MHz build crashed the console")


# ---- a built .CVP ------------------------------------------------------------------------
def cstr(b):
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def check_cvp(path, f):
    try:
        h = open(path, "rb").read(512)
    except OSError as e:
        fail(f"{path}: {e.strerror}")
        return
    if h[:8] != b"CVPLUG\0\0":
        fail(f"{path}: not a .CVP")
        return
    hdr, api, size = struct.unpack_from("<III", h, 8)
    pid, name, ver = cstr(h[52:84]), cstr(h[84:116]), cstr(h[116:132])
    exts = [e for e in (cstr(h[196 + 8 * i:204 + 8 * i]) for i in range(8)) if e]
    if f:
        for what, got, want in (("ID", pid, f.get("ID")), ("NAME", name, f.get("NAME")), ("VERSION", ver, f.get("VERSION"))):
            if want is not None and got != want:
                fail(f"{os.path.basename(path)}: {what} {got!r}, but CMakeLists.txt says {want!r}. Build it again")
        want = [e.lower() for e in f.get("OPENS", "").split()]
        if sorted(exts) != sorted(want):
            fail(f"{os.path.basename(path)}: OPENS {' '.join(exts) or 'nothing'}, but CMakeLists.txt says "
                 f"{' '.join(want) or 'nothing'}. Build it again")
    if size > 2093056:
        fail(f"{os.path.basename(path)}: image {size} bytes, over the 2,093,056 a plugin can be")
    elif size > 1044480:
        warn(f"{os.path.basename(path)}: image {size} bytes takes two flash slots, so it needs an up-to-date cart menu")
    ok(f"{os.path.basename(path)}: {name} {ver} ({pid}), API {api}, image {size} bytes")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("dir", nargs="?", default=".")
    ap.add_argument("--cvp", help="a built .CVP to check against CMakeLists.txt")
    a = ap.parse_args()
    root = os.path.abspath(a.dir)
    f, _ = check_cmake(root)
    if f and f.get("VERSION"):
        check_tags(root, f["VERSION"])
    check_readme(root)
    check_license(root)
    check_screenshots(root)
    check_roms(root)
    check_timing(root)
    if a.cvp:
        check_cvp(a.cvp, f)
    for kind, msg in results:
        print(f"{kind:4}  {msg}")
    n = sum(1 for k, _ in results if k == "FAIL")
    print("ready to release" if not n else f"{n} to fix before the release")
    sys.exit(1 if n else 0)


if __name__ == "__main__":
    main()
