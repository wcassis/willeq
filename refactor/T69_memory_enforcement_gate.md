# T69 — Connect the memory accounting to the gate that can fail a build

**Slot: t1. x86 only. No Pi (see "Pi is out of scope"). NO EQEMU.**

**Territory: `tools/hwcaps/gles2_zone_benchmark.cpp` (the memory-JSON
emitter), `scripts/ci/visual_gate.py`, `src/client/graphics/renderer_diagnostics.cpp`
(read-only unless a getter must be exposed — see Step 1), and
`benchmarks/` baselines.**

Do not change any memory *behaviour* — no new caches, no budget
enforcement at runtime, no eviction changes. This unit measures and
asserts; it does not manage. `refactor/deferred.md` is append-only.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `scripts/ci/visual_gate.py`
in full (it is short, and its existing memory assertion is the pattern you
are extending).

## Why this unit exists

Will's standing goal: WillEQ must run on the widest possible hardware,
which means tight control of memory — and his ruling on how to get there:
**per-subsystem budgets that are defined and enforced. No global cap, no
cross-asset coordinator.**

The accounting already exists and is good. `getMemoryReport()`
(`src/client/graphics/renderer_diagnostics.cpp:620-900`) accumulates a
`totalEstimate` across ~15 categories — texture cache, EQE1 flat packs,
zone geometry, placeable objects, entities, character models, equipment
models, S3D zone source, GPU textures, HCMap/collision, framebuffer,
VBO/EBO buffers, SFX and sound-buffer caches — and compares it against
actual RSS.

**The enforcement also already exists — for exactly one subsystem.**
`scripts/ci/visual_gate.py:296-306` asserts `textureCacheBytesUsed`
against a blessed value *and* against its limit, and fails the gate on
either. That is precisely the right shape.

What is missing is the wire between them. The benchmark emits **four
fields** (`gles2_zone_benchmark.cpp:2552`): zone, texture-cache used,
texture-cache limit, texture count. Everything `/pmem` knows is invisible
to CI, so memory growth in any other subsystem is caught by nobody.

This unit connects the two. It is deliberately not a new mechanism.

## Step 1 — emit what /pmem already knows

Extend the benchmark's memory JSON to carry the full per-subsystem
breakdown plus `totalEstimate` and RSS.

Prefer **reusing `getMemoryReport()`'s accounting** over recomputing it in
the benchmark — two independent tallies of the same thing is the drift
this project keeps finding (T57's slot geometry, T60's bone logic). If the
report only produces formatted strings, that is a real obstacle: expose a
structured accessor alongside it rather than parsing text, and say so.
Parsing formatted log lines in CI is fragile and would be a poor
foundation for a gate.

**Note T67 just removed `ConstrainedMeshCache`**, so the mesh-cache
category is gone from the report. Take the categories as they exist now,
not as older docs describe them.

## Step 2 — assert, with the right kind of assertion per metric

Extend `visual_gate.py` following its existing texture-cache assertion.
**Two different kinds of check are needed and conflating them would be
wrong:**

**(a) Where a real budget exists — enforce it.** `ConstrainedRendererConfig`
defines `textureMemoryBytes` and `framebufferMemoryBytes`. For these,
assert *usage ≤ budget*. That is enforcing a defined budget, which is what
Will asked for, and it is meaningful independent of any baseline.

**(b) Where no budget exists — guard against regression.** Most categories
(entities, character models, S3D source, EQE1, HCMap…) have no configured
ceiling at all. For these, assert against a **blessed baseline with a
tolerance**. This is regression detection, not budget enforcement, and the
Review must say plainly which categories fall in this bucket — that list
is itself a finding: it enumerates where the project has no defined budget
today.

Do not invent budgets for category (b). Proposing numbers is Will's call,
not this unit's; recording the absence is the deliverable.

## Step 3 — the failure mode that would make this worthless

**A gate that fires spuriously gets waived, and a waived gate is worse
than no gate.** This project already learned that the expensive way: T35
found the `RDPServerFrameTest` "intermittent flake" waiver had been
masking a real use-after-free for months.

So: **establish which metrics are actually deterministic before choosing
tolerances.** Run the same zone several times and report the spread per
category. The texture cache is asserted *exactly* today (`!=`) and that
works because it is deterministic. Others may not be — allocator
behaviour, texture upload ordering, and page-cache effects can all move
numbers run to run.

Pick tolerance per metric from measured spread, not from a guess. A
category too jittery to gate meaningfully should be **reported but not
asserted**, with that stated openly — an honest gap beats a false alarm.

## Step 4 — the untracked-RSS gap: report it, do not hard-gate it yet

`totalEstimate` vs RSS is the most interesting single number here — it
measures memory the project does not know it is using, which is exactly
the failure mode Will described (iterative development accreting
unaccounted allocations).

**But it is the noisiest.** EQE1 flat packs are mmap/page-cache backed,
allocator arenas fluctuate, and RSS includes things no subsystem owns.

So: **emit it, record it in the baseline, print it prominently in the gate
output, and do not fail the build on it in this unit.** If Step 3's
repeat-run data shows it is stable enough to gate, say so with the numbers
and recommend a ceiling — but do not enable it on a hunch. Getting this
one wrong is how the whole gate loses credibility.

## Pi is out of scope

Ceilings measured on x86/llvmpipe do not transfer to Mali400, and the Pi
runs through a separate tier-2 harness. Note on the board that per-device
budgets are the natural follow-up; do not attempt them here.

## Step 5 — blessing

`visual_gate.sh --bless` already regenerates goldens and the manifest.
Memory baselines must ride the **same** mechanism, so there is one blessing
path rather than two. Blessing memory must be as deliberate as blessing an
image: state in the Review exactly what a re-bless would overwrite.

## STOP conditions

- `getMemoryReport()` proving impossible to reuse without a structural
  change larger than this unit — report rather than duplicating the
  accounting.
- Any category whose measured spread makes a meaningful tolerance
  impossible — report it, do not gate it.
- Any temptation to change runtime memory behaviour. Measure and assert
  only.

## Verification

Full protocol bar, plus: the per-category determinism data from Step 3
(repeat runs, spread per metric); the blessed baseline; proof the gate
**fails** when a ceiling is exceeded — deliberately breach one and show
the failure, because a gate never observed failing is not known to work;
and confirmation the existing texture-cache assertion still behaves.

## Review

(executor fills: where the structured accounting came from and whether a
getter was added; the per-category table — budget-enforced vs
baseline-guarded vs reported-only, with the measured spread that decided
each; the untracked-RSS numbers and the recommendation; proof of a
deliberate failure; what a re-bless overwrites; deviations)

Board (`refactor/deferred.md`, append, never delete): record the
categories that have **no defined budget** — that list is the concrete
next question for Will, since his ruling was that budgets should be
defined and enforced, and this unit can only enforce the ones that exist.
Note the Pi follow-up and, if applicable, the untracked-RSS gating
recommendation.
