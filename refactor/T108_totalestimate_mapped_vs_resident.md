# T108 — `totalEstimate` counts mapped bytes as if they were resident

**Slot: slot1. Headless — the memory gate runs `gles2_zone_benchmark`, which
needs no server. Do not take the eqemu lock; T109 wants it.**

**Territory: `src/client/graphics/renderer_diagnostics.cpp`,
`scripts/ci/visual_gate.py`, `tests/goldens/` memory baselines, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T108/`.**

Read T105's, T106's and T107's Reviews first — they establish the numbers this
fix rests on.

## The defect

`renderer_diagnostics.cpp:646`:

```cpp
b.totalEstimate += b.eqe1MappedBytes;
```

`eqe1MappedBytes` is **mapped** — address space. T105 measured the EQE1
archives at **1.6-10.6% resident**; `lgequip.eqe1` is 23.6MB mapped and
**64KB resident**. So `totalEstimate` credits ~120MB as accounted-for when
roughly 110MB of it is not resident at all.

`untrackedBytes` (`:809`) is the residual against that inflated total, which
is why it read a comfortable "34-36%" when the true figure is **75-90% on
x86 and 32.8% on the Pi** (T106 corrected the arithmetic; T107 measured the
Pi).

**This is the blocker on blessing the memory-gate baselines.** Blessing them
as they stand would pin the error into the gate — and under the load-gate
design those budgets are meant to become a runtime control surface.

## The fix — decide between two shapes, and say why

**Option A: measure resident.** Read `/proc/self/smaps` for the EQE1 mappings
and sum `Rss`. Accurate, but costs a `/proc` parse at report time and needs
care about when it runs.

**Option B: stop counting it.** Remove `eqe1MappedBytes` from `totalEstimate`
and report it separately as an explicitly-mapped-not-resident figure.
Cheaper, honest, and makes `untrackedBytes` mean what it claims.

T105, T106 and T107 all used smaps parsing already; tooling exists in
`/mnt/projects/fable/review/T105/` and `T106/`. **Pick one, implement it, and
state the reasoning.** If you choose A, keep the mapped figure too — it is
useful, it just is not RSS.

## The baselines WILL move — that is the point

`scripts/ci/visual_gate.py`'s `MEMORY_BASELINE_FIELDS` asserts `totalEstimate`
and `eqe1MappedBytes` bit-identically across three golden zones. **This fix
changes `totalEstimate` by roughly 110MB.** The gate will fail until baselines
are regenerated.

That is expected and correct. **Regenerate them, and state the before/after
per zone explicitly** — the delta is the size of the error being removed, and
it is the number Will needs in order to bless.

Do **not** loosen the gate's zero-tolerance comparison to make it pass. The
comment at `visual_gate.py:95-98` is explicit that non-identical values are
"real news, not noise."

## Also fix, if it is genuinely small

T105 and T106 both found `/pmem`'s real RSS and untracked fields **unreachable
in a live client** — the populating code path has zero callers, so both units
had to work around it via the standalone benchmark. T106 additionally found
Audio counters that exist and are never reached for the same reason.

If wiring that up is small and clearly scoped, do it — a memory diagnostic
nobody can invoke live is not much of a diagnostic. **If it is not small, stop
and report**, and leave it in `deferred.md`.

## Tests

- A test pinning that `totalEstimate` does not include non-resident mapped
  bytes, whichever option you chose.
- **Prove it can fail in both directions** (the T95 standard): reintroduce the
  mapped-bytes credit, show red; restore, show green. Report that you did it.

## What you may not do

- Do not take the eqemu lock — T109 needs it and this unit does not.
- Do not change what is measured, only how it is accounted. No new categories.
- Do not loosen the gate's tolerance.
- **STOP and report** if correcting the arithmetic reveals a second
  double-count. Two errors compounding is a different finding from one.

## Verification

Full build and suite (current total 2247; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual gate must PASS
after baseline regeneration, with the **image** goldens untouched — only the
memory baselines should move. If an image golden moves, that is a real signal:
stop and say so.

## Review

(executor fills: which option and why; the before/after `totalEstimate` per
golden zone and the delta; whether `/pmem`'s live path was fixed or deferred;
proof the new test fails in both directions; whether any second double-count
surfaced; deviations)

## Board

Record that the memory baselines are now regenerated and correct, so the bless
can proceed on numbers that mean what they say.
