#!/usr/bin/env python3
"""cmpdump.py A.dump B.dump [A.pix B.pix] -- compare tp_host's and MAME's frames.

Prints how many frames matched in each part (colour RAM, video RAM, RAM,
sprite RAM, the CPUs' registers, the sound CPU's RAM, the picture) and the
first frame and offset where each part differs. Exit status 1 if any does.
"""
import struct, sys

PARTS = [('colour RAM', 0x400), ('video RAM', 0x400), ('RAM A800-AFFF', 0x800), ('sprite RAM', 0x200),
         ('main CPU registers', 32), ('sound CPU registers', 32), ('sound CPU RAM', 0x400)]
REC = 8 + sum(n for _, n in PARTS)

def frames(name):
    d = open(name, 'rb').read()
    for i in range(0, len(d) - REC + 1, REC):
        assert d[i:i + 4] == b'TPFR', 'bad record at %d' % i
        yield int.from_bytes(d[i + 4:i + 8], 'little'), d[i + 8:i + REC]

def pix(name):
    d = open(name, 'rb').read()
    i, out = 0, {}
    while i + 8 <= len(d):
        n, w, h = struct.unpack_from('<IHH', d, i)
        out[n] = (w, h, d[i + 8:i + 8 + w * h * 4])
        i += 8 + w * h * 4
    return out

def main():
    a, b = dict(frames(sys.argv[1])), dict(frames(sys.argv[2]))
    common = sorted(set(a) & set(b))
    if not common:
        print('no frames in common'); sys.exit(1)
    print('frames compared: %d (%d..%d)' % (len(common), common[0], common[-1]))
    bad = 0
    off = 0
    for name, n in PARTS:
        diff = [f for f in common if a[f][off:off + n] != b[f][off:off + n]]
        line = '  %-20s %d/%d frames identical' % (name, len(common) - len(diff), len(common))
        if diff:
            f = diff[0]
            x, y = a[f][off:off + n], b[f][off:off + n]
            j = next(k for k in range(n) if x[k] != y[k])
            line += ', first difference frame %d offset %04X (%02X vs %02X)' % (f, j, x[j], y[j])
            bad = 1
        print(line)
        off += n
    if len(sys.argv) > 4:
        pa, pb = pix(sys.argv[3]), pix(sys.argv[4])
        both = sorted(set(pa) & set(pb))
        same, first = 0, None
        for f in both:
            if pa[f] == pb[f]:
                same += 1
            elif first is None:
                w, h, x = pa[f]
                y = pb[f][2]
                if (w, h) != pb[f][:2]:
                    first = '%d: sizes %dx%d vs %dx%d' % (f, w, h, pb[f][0], pb[f][1])
                else:
                    k = next(i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]) // 4
                    first = '%d at (%d, %d)' % (f, k % w, k // w)
        print('  %-20s %d/%d identical%s' % ('pictures', same, len(both), '' if first is None else ', first difference frame ' + first))
        if same != len(both):
            bad = 1
    sys.exit(bad)

main()
