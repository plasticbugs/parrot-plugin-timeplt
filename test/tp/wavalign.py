#!/usr/bin/env python3
"""wavalign.py REF.wav IN.wav OUT.wav [STRETCH] -- IN lined up with REF.

The replay (tools/mame-replay.sh) starts the lists a little after MAME's
start; MAME's timeplt starts at once. And MAME's ColecoVision runs a list
in less than the 60,036 T the kit's costs give it (its replay counts no
list outlasting its TV frame), so there the lists go a TV frame each,
59,736 T, and the replay is 0.5% fast: STRETCH (60036/59736 for the
lists) makes IN that much longer first, as the console plays them. Both
are made mono 16-bit at their own rate, and the lead is the lag at which
their envelopes (each 10 ms piece's spread about its mean) agree best,
within 3 s. OUT is IN shifted by it, so the two line up to a few ms
(test/tp/sndcmp.c finds the rest, within 50 ms).
"""
import struct
import sys
import wave


def read(name):
    with wave.open(name, 'rb') as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        data = w.readframes(n)
    if width != 2:
        sys.exit(f'wavalign: {name} is not 16-bit')
    s = struct.unpack(f'<{n * ch}h', data)
    if ch > 1:
        s = [sum(s[i:i + ch]) // ch for i in range(0, len(s), ch)]
    return list(s), rate


def envelope(s, rate):
    piece = max(1, rate // 100)
    env = []
    for i in range(0, len(s) - piece, piece):
        p = s[i:i + piece]
        m = sum(p) / piece
        env.append(sum(abs(v - m) for v in p) / piece)
    m = sum(env) / max(1, len(env))
    return [v - m for v in env]


def best_lag(a, b, most=300):           # b[k + lag] best like a[k]
    best, at = None, 0
    for lag in range(-most, most + 1):
        lo, hi = max(0, -lag), min(len(a), len(b) - lag)
        c = sum(a[k] * b[k + lag] for k in range(lo, hi))
        if best is None or c > best:
            best, at = c, lag
    return at


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    ref, rref = read(sys.argv[1])
    s, rate = read(sys.argv[2])
    stretch = 1.0
    if len(sys.argv) > 4:               # a number, or A/B
        num, _, den = sys.argv[4].partition('/')
        stretch = float(num) / float(den or 1)
    if stretch != 1.0:                  # linear, sample by sample
        n = int(len(s) * stretch) - 1
        s = [int(s[int(t)] + (s[int(t) + 1] - s[int(t)]) * (t - int(t))) for t in (k / stretch for k in range(n))]
    lead = best_lag(envelope(ref, rref), envelope(s, rate)) * (rate // 100)
    out = s[lead:] if lead >= 0 else [s[0]] * -lead + s
    with wave.open(sys.argv[3], 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack(f'<{len(out)}h', *out))
    print(f'wavalign: {sys.argv[2]} from {lead / rate:.3f} s')


main()
