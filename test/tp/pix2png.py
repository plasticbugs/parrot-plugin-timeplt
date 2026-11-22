#!/usr/bin/env python3
"""pix2png.py FILE.pix OUTDIR [FRAME...] -- a .pix file's pictures (tp_host's
or tpdump.lua's: u32 frame, u16 width, u16 height, then BGRA) as PNGs,
turned as MAME shows them (ROT90), named FRAME.png. No FRAME: every one."""
import os, struct, sys, zlib

def write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(tag, d):
        c = tag + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

def main():
    d = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    want = set(int(a) for a in sys.argv[3:])
    os.makedirs(out, exist_ok=True)
    i = 0
    while i + 8 <= len(d):
        n, w, h = struct.unpack_from('<IHH', d, i)
        px = d[i + 8:i + 8 + w * h * 4]
        i += 8 + w * h * 4
        if want and n not in want:
            continue
        # turned clockwise: (x', y') = (h - 1 - y, x)
        rgb = bytearray(w * h * 3)
        for y in range(h):
            for x in range(w):
                s = (y * w + x) * 4
                o = (x * h + (h - 1 - y)) * 3
                rgb[o] = px[s + 2]; rgb[o + 1] = px[s + 1]; rgb[o + 2] = px[s]
        write_png(os.path.join(out, '%d.png' % n), h, w, rgb)

main()
