#!/usr/bin/env python3
"""lua-embed.py OUT.h BASE_DIR MAIN.lua [FILE ...]

A Lua app's files as a C header, for the Lua runtime to build into the
plugin (lua/main.c includes it). cmake/parrotcart.cmake's parrotcart_lua()
runs this. Each file is known by its path under BASE_DIR, with forward
slashes, and the first one is the script the plugin starts.
"""
import os
import sys


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out, base, files = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3:]
    lines = ["// made by tools/lua-embed.py: don't edit", ""]
    table = []
    seen = set()
    for i, path in enumerate(files):
        full = os.path.abspath(path)
        name = os.path.relpath(full, base).replace(os.sep, "/")
        if name.startswith(".."):
            sys.exit(f"lua-embed.py: {path} isn't under {base}")
        if name in seen:
            sys.exit(f"lua-embed.py: {name} twice")
        seen.add(name)
        data = open(full, "rb").read()
        if name.endswith(".lua") and data.startswith(b"\x1bLua"):
            sys.exit(f"lua-embed.py: {path} is compiled Lua. The cart runs source files")
        body = ",".join(str(b) for b in data) or "0"
        lines.append(f"static const unsigned char cvlua_app_{i}[] = {{{body}}};")
        table.append(f'    {{"{name}", cvlua_app_{i}, {len(data)}}},')
    lines += ["", "static const cvlua_file_t cvlua_app[] = {"] + table + ["};", f"#define CVLUA_APP_COUNT {len(files)}", ""]
    text = "\n".join(lines)
    try:
        if open(out).read() == text:
            return
    except OSError:
        pass
    with open(out, "w") as f:
        f.write(text)


if __name__ == "__main__":
    main()
