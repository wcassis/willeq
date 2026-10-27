#!/usr/bin/env python3
"""B1163 (#482) fix leg 2: the closing time of an eqgame missile object, from
the traced update (refactor/B1163/README.md (A), "The motion model").

Every constant is read from /mnt/projects/EverQuestP1999/eqgame.exe .rdata or
is an immediate in review/B1141/ghidra/out/eqgame.dis:

  pass gate   00445580 cmp ecx, 0x21 / 00445583 jb   -> a pass when >= 33 time() units elapsed
  dt          004c0033 fmul dword ptr [0x6410b8]     -> dt = 0.02 x elapsed (clamp 3.0, 004c0049)
  hit test    00476743 fcomp dword ptr [0x6410b4]    -> removed when the stored XY distance < 2.0
  refresh     004767d3 jbe (unsigned)                -> the distance is re-stored every pass
  thrust      004c04c1 fld dword ptr [0x6431d8]      -> 0.8 x word
  damping     004c04cd fld dword ptr [0x643f7c]      -> 0.625 (0.85 when [m+0x62] == 0, 004c0762)
  initial     00476eef mov [eax + 0xff4], 0x44fa0000 -> stored distance 2000.0 before the first pass

Per pass k (order of the loop 00445587 / 0044558f / 0044559a):
  1. FUN_004766aa: if k >= 2 and the distance stored at pass k-1 < 2.0 -> removed at pass k;
     else store the current XY distance (before this pass's physics).
  2. FUN_004bfe0f: v = damp x v + 0.8 x word ; s += dt x v.
The caster is standing (v0 = 0). The heading is the bearing to the target
(within 32 units the heading is snapped every pass, 00476939 / 0047694c).

The arithmetic is float32 (as the client's fstp stores and as
spell_b1163_missile_rules.h MissilePassesToHit computes).
"""
import sys

import numpy as np

F = np.float32


def passes_to_hit(gap, word, damp, pass_units, thrust=0.8, hit=2.0, max_passes=1000):
    v = F(0.0)
    s = F(0.0)
    stored = F(2000.0)
    dt = F(F(0.02) * F(pass_units))
    for k in range(1, max_passes):
        if k >= 2 and stored < F(hit):
            return k
        stored = F(F(gap) - s)
        v = F(F(damp) * v + F(thrust) * F(word))
        s = F(s + dt * v)
    return None


def main():
    print("gap\tword\tdamp\tpass_units\tpasses\tflight_s(ms reading)\tterminal_u_per_s")
    for gap in (9.05, 7.55):
        for word, damp in ((1.5, 0.625), (1.0, 0.85)):
            for pu in (33, 34, 40, 50, 67, 100):
                k = passes_to_hit(gap, word, damp, pu)
                term = 0.8 * word / (1.0 - damp) * 20.0
                print(f"{gap}\t{word}\t{damp}\t{pu}\t{k}\t{k * pu / 1000.0:.3f}\t{term:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
