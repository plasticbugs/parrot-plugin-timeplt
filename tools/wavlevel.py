#!/usr/bin/env python3
"""wavlevel.py FILE.wav ... -- how loud a recording is, to compare a plugin's
sound with an ordinary ColecoVision cartridge's (docs/testing.md, "How loud").

For each file it prints, in dBFS (0 is the WAV's full scale):

    rms      the level of the whole file, its silent seconds left out
    median   the median of its seconds' levels, silent ones left out
    loudest  the loudest second
    peak     the highest sample

Each second is measured from its own average, so a resting level (a
DAC's, say) doesn't count as sound. A second below -60 dBFS is silent.

Record both sounds the same way, so the numbers compare: a cartridge with
MAME's -wavwrite, and the plugin's lists with MAME_WAV=plugin.wav
tools/mame-replay.sh. The simulator's --wav has its own mixer, so don't
compare it with MAME's. The WAV is 16-bit PCM, mono or stereo (the
channels are averaged).

    python3 tools/wavlevel.py reference.wav plugin.wav
"""
import math
import struct
import sys
import wave

SILENT_DB = -60.0


def samples(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: not 16-bit PCM")
        ch, rate = w.getnchannels(), w.getframerate()
        data = w.readframes(w.getnframes())
    v = struct.unpack(f"<{len(data) // 2}h", data)
    if ch > 1:
        v = [sum(v[i:i + ch]) / ch for i in range(0, len(v) - ch + 1, ch)]
    return rate, v


def db(x):
    return 20 * math.log10(x / 32768) if x > 0 else float("-inf")


def level(path):
    rate, v = samples(path)
    if not v:
        return None
    seconds, loud_sq, loud_n, top = [], 0.0, 0, 0.0
    for i in range(0, len(v), rate):
        part = v[i:i + rate]
        mean = sum(part) / len(part)            # (each second's resting level, a DAC's say, isn't loudness)
        part = [s - mean for s in part]
        top = max(top, max(abs(s) for s in part))
        sq = sum(s * s for s in part)
        r = db(math.sqrt(sq / len(part)))
        if r > SILENT_DB:
            seconds.append(r)
            loud_sq += sq
            loud_n += len(part)
    peak = db(top)
    if not seconds:
        return {"rms": float("-inf"), "median": float("-inf"), "loudest": float("-inf"), "peak": peak, "s": 0}
    seconds.sort()
    return {"rms": db(math.sqrt(loud_sq / loud_n)), "median": seconds[len(seconds) // 2],
            "loudest": seconds[-1], "peak": peak, "s": len(seconds)}


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        r = level(path)
        if r is None:
            print(f"{path}: empty")
            continue
        print(f"{path}: rms {r['rms']:.1f}  median {r['median']:.1f}  loudest {r['loudest']:.1f}  "
              f"peak {r['peak']:.1f} dBFS  ({r['s']} seconds with sound)")


if __name__ == "__main__":
    main()
