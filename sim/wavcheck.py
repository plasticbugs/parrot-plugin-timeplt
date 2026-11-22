#!/usr/bin/env python3
"""wavcheck.py FILE.wav FROM-TO:HZ ... -- the simulator's sound, checked.

Each FROM-TO:HZ (milliseconds of console time, then a pitch) says the
sound between FROM and TO is one tone at HZ, within 1% (17 cents); HZ 0
says it is silent. It prints what it finds, and exits 1 if any differs.
For sim/test.sh: the WAV is --wav's, 16-bit mono.

    sim/wavcheck.py sound.wav 500-900:262 1000-1400:294 1500-1900:0
"""
import struct
import sys
import wave


def samples(path):
    with wave.open(path, "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit(f"{path}: not 16-bit mono")
        rate = w.getframerate()
        data = w.readframes(w.getnframes())
    return rate, struct.unpack(f"<{len(data) // 2}h", data)


def pitch(x, rate):
    """(Hz, loudness) of a stretch: rising zero crossings of the mean-free signal."""
    if not x:
        return 0.0, 0
    mean = sum(x) / len(x)
    y = [v - mean for v in x]
    loud = max(abs(v) for v in y)
    if loud < 200:
        return 0.0, loud
    hyst = loud / 4                         # (a crossing counts once the signal has gone well below)
    ups, armed = [], False
    for i, v in enumerate(y):
        if v < -hyst:
            armed = True
        elif armed and v > hyst:
            ups.append(i)
            armed = False
    if len(ups) < 3:
        return 0.0, loud
    return (len(ups) - 1) * rate / (ups[-1] - ups[0]), loud


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    rate, x = samples(sys.argv[1])
    bad = 0
    for spec in sys.argv[2:]:
        span, want = spec.split(":")
        a, b = (int(v) for v in span.split("-"))
        hz, loud = pitch(x[a * rate // 1000 : b * rate // 1000], rate)
        want = float(want)
        ok = hz == 0 if want == 0 else abs(hz - want) <= want * 0.01
        print(f"  {a}-{b} ms: {hz:.1f} Hz (level {int(loud)}), wanted {'silence' if want == 0 else f'{want:g} Hz'}: {'ok' if ok else 'WRONG'}")
        bad += not ok
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
