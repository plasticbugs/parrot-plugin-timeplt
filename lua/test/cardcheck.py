#!/usr/bin/env python3
"""cardcheck.py LUA_FOLDER ERROR.lua

What lua/test/run.sh's card test leaves in the card's /LUA folder, read on
this computer: the files files.lua made, and error.lua as the editor saved
it. Prints ok, or what is wrong.
"""
import os
import sys

folder, original = sys.argv[1], open(sys.argv[2], "rb").read()
wrong = []
d = open(os.path.join(folder, "DATA.BIN"), "rb").read()
if len(d) != 1000 or d[510:516] != b"ACROSS" or sum(d) != sum(b"ACROSS"):
    wrong.append("DATA.BIN")
r = open(os.path.join(folder, "RUNS.SAV"), "rb").read()
if len(r) != 4008 or r[:4] != b"LSV1" or r[4:8] != bytes([1, 0, 0, 0]) or r[8:9] != b"3":
    wrong.append("RUNS.SAV")
e = open(os.path.join(folder, "error.lua"), "rb").read()
if e != b"x\n" + original:
    wrong.append(f"error.lua ({len(e)} bytes, {len(original) + 2} wanted)")
left = [n for n in os.listdir(folder) if n.upper().startswith("EDITSAV")]
if left:
    wrong.append("the editor's spare file is still there: " + " ".join(left))
print("ok" if not wrong else "; ".join(wrong))
