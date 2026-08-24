# T201 — The matrix: EQE1 has ONE pre-existing crash, everywhere

Ran {zone} × {tree} × {preset} on the **baseline** binary (`bc54e527`, no T199).
Eight cells. The result corrects both my classifier and three earlier reports.

## The matrix

| zone | tree | preset | result |
|---|---|---|---|
| qeynos2 | live | orangepi | **SEGFAULT** |
| qeynos2 | live | none | **SEGFAULT** |
| qeynos2 | regen | orangepi | **SEGFAULT** |
| qeynos2 | regen | none | **SEGFAULT** |
| befallen | live | orangepi | FATAL `fishe0301` — clean `exit(1)` |
| befallen | live | none | **SEGFAULT** |
| befallen | regen | orangepi | FATAL `fishe0301` — clean `exit(1)` |
| befallen | regen | none | **READY, then SEGFAULT** |

**Seven of eight runs segfaulted.** The two that did not are exactly the two
that hit the all-or-nothing `exit(1)` first — they died cleanly before reaching
the crash.

## What this settles

**There is ONE pre-existing crash in the EQE1 path, and it is independent of
everything I had been blaming:**

- not the zone — both zones crash
- not the tree — live and regenerated crash identically
- not the preset — with and without `orangepi` crash
- not T199 — this is the baseline binary

**And "hang" was my own misreading, three times over.** T196, T199 and T200 all
classified this as a hang because neither `Ready` nor `FATAL` appeared in the
log. A segfault writes neither. The shell reports it, and my classifier never
looked. T196's gdb backtrace was right all along:
`COpenGLES2Driver::draw2DImage` ← `UIRenderer::endFrame`.

**It also revises T193.** That run recorded `Ready` in 13.25s and I treated it
as a clean success — but `befallen + regen + none` here reaches `Ready` *and
then* segfaults, so T193 very likely crashed after Ready too and I never looked
past the phase line. **The "EQE1 works on x86" claim underpinning T193's
comparison is not safe.**

## What is actually blocked

EQE1 is unusable on any platform until this crash is fixed — not because of
address space (T195, which was the Pi-specific 32-bit issue and is separately
real), but because the UI draw path faults whenever EQE1 is enabled.

**Next:** debug the `draw2DImage` fault directly with the baseline binary and
`--eqe1-path`, which reproduces in 7 of 8 configurations — the easiest
reproduction this arc has had. The T196 backtrace (`review/T196/t197_gdb.log`)
is the starting point, and its suspicion that a UI texture resolves from EQE1
is now much stronger, since `willeq.eqe1` is known to hold the UI atlas (T198).

## Method note

Classify by process exit status, not by log contents. `timeout N cmd` returns
139 on SIGSEGV and 124 on timeout; a log missing both `Ready` and `FATAL` means
nothing on its own.
