#!/usr/bin/env python3
"""script_lists.py PROGRAM.BIN PCODE -- seeds for Starblade's self-relative timed scripts (slave 0x5238: cmp.w (A5)+,D0 ; movea.l A5,A1 ;
adda.w (A5)+,A1 ; jsr (A1)): lists of (frame, offset) words where the routine is at the offset word's own address + offset. A run of
4+ entries (frame = a wait, < 0x1000, or 0xFFFF = the end) whose targets are 3/4 known code is taken as a list; its unknown targets are printed as seeds."""
import sys
rom = open(sys.argv[1], 'rb').read(); end = len(rom.rstrip(b'\0'))
ins = {int(l.split()[1], 16) for l in open(sys.argv[2]) if l.startswith('I ')}
def w(a): return int.from_bytes(rom[a:a + 2], 'big')
def tgt(a): o = w(a + 2); return a + 2 + (o - 0x10000 if o & 0x8000 else o)
seeds = set(); a = 0
while a < end - 8:
    run = []; k = a
    while k < end - 4 and (w(k) < 0x1000 or w(k) == 0xFFFF) and not tgt(k) & 1 and 0x400 <= tgt(k) < end:
        run.append(tgt(k)); k += 4
    known = sum(t in ins for t in run)
    if len(run) >= 4 and known * 4 >= len(run) * 3:
        seeds |= {t for t in run if t not in ins}; a = k
    else: a += 2
for s in sorted(seeds): print(f'{s:X}')
