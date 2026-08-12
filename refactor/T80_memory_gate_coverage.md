# T80 — Three memory categories are baselined at zero and guard nothing

**Slot: first free. x86 only. No Pi. NO EQEMU.**

**Territory: `scripts/ci/visual_gate.py`, `scripts/ci/visual_gate.sh`,
`tools/hwcaps/gles2_zone_benchmark.cpp` (only if the memory snapshot needs
a field it does not already emit), and `benchmarks/` baselines.**

Do not change rendering. Do not change the existing golden-image runs —
see the constraint below, it is the crux of the unit. `refactor/deferred.md`
is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then **`refactor/T69_memory_enforcement_gate.md`'s
Review in full** — it built the gate and measured everything you are about
to extend.

## Why this unit exists

T69 wired `/pmem`'s per-subsystem accounting into the visual gate. Will
has **held off blessing it** for a specific reason: three of the thirteen
measured categories baseline at **zero**, because the gate's benchmark
invocation never exercises them.

- `eqe1MappedBytes` / `eqe1ArchiveCount` — the gate does not pass
  `--eqe1-path`.
- `placeableObjectsBytes` — the gate does not pass `--objects`.
- `characterModelsBytes` — the golden zones load no entities.

Blessing today would record "guarded" for three categories that guard
nothing. A character-model memory regression would sail straight through.
**The gate covers less than its field list implies**, and that gap is
exactly the kind of thing that reads as coverage a year from now.

## The constraint that shapes the whole unit

**Do not add these flags to the existing golden-image runs.**

Loading objects and entities changes what is rendered, so the golden
images would move — which would require Will to re-bless *images*, a
separate and much larger judgement, and would conflate two unrelated
blessings. It would also make the image goldens hostage to entity
placement.

**Add a separate memory-coverage run instead.** The image-comparison runs
stay exactly as they are; a new invocation exercises full asset loading
and supplies the memory baselines. The two want opposite things — image
goldens want minimal and stable, memory coverage wants maximal — and
separating them is correct on its own merits, not just as a workaround.

The flags exist already: `--eqe1-path`, `--objects`, `--entities N`,
`--test-races` (`gles2_zone_benchmark.cpp:342, 380, 421, 581`). Use them.

## The trap — re-measure determinism, do not inherit it

T69's **zero-tolerance exact-match** assertions were an empirically
measured choice: 9 runs across 3 zones, every baseline field
bit-identical. **But that was measured on a path that loaded no objects
and no entities.**

Entity placement, model loading order and object loading may not be
deterministic in the same way. **Re-run the determinism measurement for
the newly-exercised categories** — same discipline as T69's Step 3, repeat
runs, report the per-field spread — and set tolerance from what you
measure.

If a newly-covered category is genuinely jittery, **report it and do not
assert on it** rather than inventing a tolerance to make it fit. T69's
plan said an honest gap beats a false alarm, and that still holds: a gate
that fires spuriously gets waived, and this project already paid for that
lesson when T35 found the RDP "flake" waiver had masked a real
use-after-free.

## Do not bless

**Report the numbers; Will blesses in chat.** He does not run commands —
this is his stated workflow and I got it wrong once already.

So: produce the new baseline values, per zone and per category, in a form
he can read and approve. State plainly which categories now measure real
values, which remain zero and why, and what tolerance each will use. If
anything still cannot be exercised, say so rather than leaving a zero
looking like coverage.

## STOP conditions

- The golden images moving. That means the coverage run is not properly
  separated.
- Needing to change rendering behaviour to exercise a category.
- A newly-covered category proving non-deterministic in a way that needs a
  design decision — report rather than choosing a tolerance to paper over
  it.

## Verification

Full protocol bar, plus: proof the golden-image runs are byte-identical to
before (that is the separation working), the determinism data for the
newly-covered categories, and the proposed baseline table.

Also re-confirm T69's deliberate-failure proof still holds — breach a
budget and show the gate fails — since you are changing the code that
performs it.

## Review

(executor fills: how the coverage run is separated; what each newly-covered
category now measures; the determinism data and resulting tolerances; any
category still at zero and why; the proposed baseline table for Will;
proof the image runs are unchanged; deviations)

Board: update the "Memory categories with no configured budget" entry —
distinguish clearly between *no configured budget* (a Will decision, still
open) and *not exercised by the gate* (this unit's subject). They have been
conflated, and they are different problems with different fixes.
