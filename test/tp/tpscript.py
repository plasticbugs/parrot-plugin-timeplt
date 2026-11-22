#!/usr/bin/env python3
"""tpscript.py NAME [SEED] > FILE -- input scripts for tp_host and tpdump.lua.

Each line is "frame port value": from that frame on, the port reads that
value, 1 for pressed (in0: 0x01 coin, 0x08 one player; in1: 0x01 left,
0x02 right, 0x04 up, 0x08 down, 0x10 fire).

  attract   no inputs
  game      a coin at 300, one player at 360, then the plane turns and
            fires to a fixed pattern (from SEED) until its lives are gone
  cart      the game's, for cart_host: "frame joy|key|fire HEX" lines, the
            console's joystick nibble (low = held: 1 up, 2 right, 4 down,
            8 left), keypad code (0xA is *, the coin; 0xD is 1, one player;
            0xF none) and fire (1 held)
"""
import random, sys

LEFT, RIGHT, UP, DOWN, FIRE = 0x01, 0x02, 0x04, 0x08, 0x10
DIRS = [UP, UP | RIGHT, RIGHT, DOWN | RIGHT, DOWN, DOWN | LEFT, LEFT, UP | LEFT]

def game(seed=1, end=20000):
    ev = [(300, 'in0', 0x01), (306, 'in0', 0x00), (360, 'in0', 0x08), (366, 'in0', 0x00)]
    rnd = random.Random(seed)
    f, d = 480, 0
    while f < end:
        # a direction held for a while, fire pressed and let go (the game
        # fires on each press), now and then a sweep round
        d = (d + rnd.choice([-1, 1, 1, 2, 3])) % 8
        hold = rnd.randint(20, 90)
        for k in range(0, hold, 8):
            ev.append((f + k, 'in1', DIRS[d] | FIRE))
            ev.append((f + k + 4, 'in1', DIRS[d]))
        f += hold
    ev.append((f, 'in1', 0))
    return ev

# the console's joystick nibble for a stick direction (low = held)
def joy(v):
    n = 0x0F
    if v & UP: n &= ~0x1
    if v & RIGHT: n &= ~0x2
    if v & DOWN: n &= ~0x4
    if v & LEFT: n &= ~0x8
    return n

def cart(seed=1):
    ev = []
    for f, port, v in game(seed):
        if port == 'in0':
            key = {0x01: 0x0A, 0x08: 0x0D, 0x00: 0x0F}[v]
            ev.append((f, 'key', key))
        else:
            ev.append((f, 'joy', joy(v)))
            ev.append((f, 'fire', 1 if v & FIRE else 0))
    return ev

def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'attract'
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    ev = {'attract': lambda: [], 'game': lambda: game(seed), 'cart': lambda: cart(seed)}[name]()
    for f, port, v in sorted(ev, key=lambda e: e[0]):
        print('%d %s %02X' % (f, port, v))

main()
