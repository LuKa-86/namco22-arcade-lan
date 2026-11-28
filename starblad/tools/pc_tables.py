#!/usr/bin/env python3
"""pc_tables.py PROGRAM.BIN -- seeds for Starblade's PC-relative word jump tables, which Ghidra's SeedTables does not follow:

    move.w (d8a,PC,Dn.w),Dn      303B/323B/.. 0xxx      (a word OFFSET from the table)
    jsr|jmp (d8b,PC,Dn.w)        4EBB/4EFB  0xxx      (to table base + offset)

The table is the words from the move's target; each word + the jsr's base is an entry. The table ends where the first entry's
code begins (or at a word that is odd / points outside the program). Prints one hex address a line (tools/regen.sh seeds)."""
import sys
rom = open(sys.argv[1], 'rb').read()
end = len(rom.rstrip(b'\0')) if rom.rstrip(b'\0') else len(rom)
def w(a): return int.from_bytes(rom[a:a + 2], 'big')
def pcix(a):
    """the base of a (d8,PC,Dn.w*1) operand whose extension word is at a, or None"""
    ext = w(a)
    if ext & 0x8F00 != 0x0000: return None              # data register, word index, scale 1, brief format
    d8 = ext & 0xFF; d8 -= 0x100 if d8 & 0x80 else 0
    return a + d8
seeds = set()
for a in range(0, end - 8, 2):
    op = w(a)
    if op & 0xF1FF != 0x303B: continue                   # move.w (d8,PC,Xn),Dn
    t1 = pcix(a + 2)
    if t1 is None: continue
    for b in (a + 4, a + 6):                             # the jsr/jmp right after (or after one instruction-sized gap)
        if w(b) in (0x4EBB, 0x4EFB):
            t2 = pcix(b + 2)
            if t2 is None: continue
            lo, k = None, t1
            while k < end and (lo is None or k < lo) and k - t1 < 0x400:
                t = t2 + (w(k) if w(k) < 0x8000 else w(k) - 0x10000)
                if t & 1 or not (0 < t < end): break
                seeds.add(t); lo = t if lo is None else min(lo, t)
                k += 2
            break
for s in sorted(seeds): print(f'{s:X}')

# TIMED SCRIPT PLAYERS: cmp.w (An)+,D0 ; bne ; move.w (An)+,D0 ; move.l An,(d16,Ax) ; jsr (d8,PC,D0.w) -- the list a script pointer
# walks holds (frame, offset) pairs and the jsr's base is where the game's own list for it starts (Starblade 0x5DDE, 0xA280): every
# odd word is a routine at base + offset. Printed as seeds, plus "LO-HI data" for the list (it ends where the first routine begins).
for a in range(0, end - 8, 2):
    if w(a) not in (0x4EBB,) or w(a + 2) & 0x8F00: continue
    if not (0x3018 <= w(a - 6) <= 0x301F and (w(a - 4) & 0xFFC0) == 0x2140 or (w(a - 6) & 0xF1F8) == 0x3018 and (w(a - 4) & 0xF1F8) in (0x2148, 0x2348, 0x2548, 0x2748, 0x2948, 0x2B48, 0x2D48)): continue
    t2 = pcix(a + 2); lo = None; k = t2; tg = []
    while k + 4 <= end and (lo is None or k < lo) and k - t2 < 0x400:
        t = t2 + w(k + 2)
        if w(k + 2) & 1 or w(k + 2) >= 0x8000 or not (0 < t < end) or t < k + 4: break
        tg.append(t); lo = t if lo is None else min(lo, t); k += 4
    if tg:
        for t in tg: print(f'{t:X}')
        print(f'{t2:X}-{k - 1:X} data (script list of the jsr at {a:X})')
