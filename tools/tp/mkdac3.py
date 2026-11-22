#!/usr/bin/env python3
"""mkdac3.py LADDER OUT.h PREFIX -- the DAC table for a plugin's sound: three
of the SN76489's tones parked at a period of 1, their attenuators summed, as
measured on a real console (tools/tp/ladder.json, channels 0, 1 and 2). The
table is PREFIX_dac3 and its size PREFIXDAC3_LEVELS (Donkey Kong's: dk).

LEVELS levels from 0 to TOP (in ladder.json's units, 1.0 a channel at
attenuation 0). Movies' and Dragon's Lair's table is made the same way
(app/engine/src/audio.rs smooth_triples, dlair/tools/mkpsgtab.py): each
level's candidates are the triples within TOL steps of it (else its
nearest), and the table is the path through them that costs least, WEIGHT
(error / step)^2 a level plus the partial sums between neighbouring levels
(the console holds each sample's first and second write 52 T before the
next: written tone 0, 1, 2, they are off by d1 + d2 and by d2), squared, in
steps. Neighbouring levels then use triples that differ little, so a small
change in the sound makes small partial sums.

Each entry is tone 0's attenuation << 8 | tone 1's << 4 | tone 2's."""
import json
import sys

LEVELS, TOP, TOL, WEIGHT = 512, 2.40, 1.5, 0.5

amp = json.load(open(sys.argv[1]))["amp"]
name = sys.argv[3] if len(sys.argv) > 3 else "dk"
NAME = name.upper()
step = TOP / (LEVELS - 1)
triples = [(t >> 8, (t >> 4) & 15, t & 15) for t in range(4096)]
level = [amp[0][a] + amp[1][b] + amp[2][c] for a, b, c in triples]

cand = []
for i in range(LEVELS):
    want = i * step
    c = [t for t in range(4096) if abs(level[t] - want) <= TOL * step]
    if not c:
        c = [min(range(4096), key=lambda t: (abs(level[t] - want), t))]
    cand.append(c)


def err(t, i):
    e = (level[t] - i * step) / step
    return WEIGHT * e * e


def glitch(p, q):
    d1 = amp[1][triples[q][1]] - amp[1][triples[p][1]]
    d2 = amp[2][triples[q][2]] - amp[2][triples[p][2]]
    return ((d1 + d2) ** 2 + d2 ** 2) / (step * step)


cost = [err(t, 0) for t in cand[0]]
back = [None]
for i in range(1, LEVELS):
    nc, bk = [], []
    for q in cand[i]:
        best, bv = 0, float("inf")
        for k, p in enumerate(cand[i - 1]):
            v = cost[k] + glitch(p, q)
            if v < bv:
                bv, best = v, k
        nc.append(bv + err(q, i))
        bk.append(best)
    cost, back = nc, back + [bk]
k = min(range(len(cost)), key=lambda j: (cost[j], j))
path = [0] * LEVELS
for i in range(LEVELS - 1, -1, -1):
    path[i] = cand[i][k]
    if i:
        k = back[i][k]

worst = max(abs(level[t] - i * step) for i, t in enumerate(path))
with open(sys.argv[2], "w") as f:
    f.write("// %sdac3.h -- made by tools/tp/mkdac3.py from tools/tp/ladder.json: the DAC's %d levels,\n" % (name, LEVELS))
    f.write("// 0 to %.2f (a channel at attenuation 0 is 1.0), each as tone 0's attenuation << 8 |\n" % TOP)
    f.write("// tone 1's << 4 | tone 2's (all three parked at a period of 1). Off its level by\n")
    f.write("// %.4f at most.\n" % worst)
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write("#define %sDAC3_LEVELS %d\n#define %sDAC3_TOP %.2ff\n" % (NAME, LEVELS, NAME, TOP))
    f.write("static const uint16_t %s_dac3[%sDAC3_LEVELS] = {\n" % (name, NAME))
    for i in range(0, LEVELS, 12):
        f.write("    " + ", ".join("0x%03X" % (a << 8 | b << 4 | c) for a, b, c in (triples[t] for t in path[i:i + 12])) + ",\n")
    f.write("};\n")
print("%d levels, off by %.4f at most" % (LEVELS, worst))
