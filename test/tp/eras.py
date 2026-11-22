#!/usr/bin/env python3
"""eras.py FILE.dump -- the frames where the sky's colour (the
era: 1 is 1910, 2 1940, 3 1970, 4 1982, 5 2001) changed, from a dump ('-'
where the play area isn't mostly one era's sky)."""
import collections, sys

YEARS = {1: '1910', 2: '1940', 3: '1970', 4: '1982', 5: '2001'}
REC = 8 + 0x400 + 0x400 + 0x800 + 0x200 + 64 + 0x400

def main():
    d = open(sys.argv[1], 'rb').read()
    seq = []
    for i in range(0, len(d) - REC + 1, REC):
        n = int.from_bytes(d[i + 4:i + 8], 'little')
        c, v = d[i + 8:i + 8 + 0x400], d[i + 8 + 0x400:i + 8 + 0x800]
        cnt = collections.Counter()
        for ty in range(2, 30):
            for tx in range(4, 31):
                k = ty * 32 + tx
                cnt[c[k] & 0x1f] += 1
        col, num = cnt.most_common(1)[0]
        era = col if 1 <= col <= 5 and num > 600 else None
        seq.append((n, era))
    # a change that lasts 30 frames or more (the attract mode flashes)
    last, i = 'x', 0
    while i < len(seq):
        n, era = seq[i]
        j = i
        while j < len(seq) and seq[j][1] == era:
            j += 1
        if j - i >= 30 and era != last:
            print('%d: %s' % (n, 'era %d (%s)' % (era, YEARS[era]) if era else '-'))
            last = era
        i = j

main()
