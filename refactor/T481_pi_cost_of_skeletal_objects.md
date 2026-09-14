# T481 — measure the Orange Pi cost of T479's newly drawn skeletal objects before merge (B735)

Class A (correctness vs a 483 MB board), **eqemu lock + Pi**, slot2 (T479's
slot; its branch is the "after" build). Dispatch after T478 releases the lock.
Read T479's Review (`refactor/T479_skeleton_mesh_list_loaders.md` on
`task/T479-skeleton-mesh-list-loaders`) and `review/T479/README.md`, T459's and
T462's Pi rigs (`review/T459/`, `review/T462/` — uinput input, `WILLEQ_DATA_ROOT`,
`--log-level=INFO`, B541 traps), and the recipe `review/pi/README.md`
(1280x720, `--eqe1-path`).

## Why

T479 makes the client draw 95,666 object placements across 158 zones that it
silently dropped before: 80,768 tree placements (trees hung from bones)
and 14,897 skinned torches/braziers. That is correctness — the official client
draws them — and Will's standing rule is that load time is secondary. But memory is
not load time: the Pi has 483 MB, composite entities are OFF there by Will's
choice for memory, and a zone that newly places thousands of objects could exhaust
budgets or OOM. Measure before the merge, not after Will's next session.

## Tasks

1. **Pick zones by the census** (`review/T479/census/objects.csv`): the largest
   newly placed counts (warslikswood +4,433), a tree-heavy zone Will is likely to
   visit (gfaydark / its neighbours if present in the census), a torch zone
   (thedeep), and nro (Will's zone). Include qeynos2 for the gate number.
2. **Before vs after, same binary pair built for ARM** (before = fable tip,
   after = T479 branch rebased on it), per zone, ≥2 zone-ins each:
   zone-in completes (yes/no), `VmRSS`/`VmPeak`, the constrained cache budgets'
   high-water marks and evictions (log), `placeableObjectsBytes` / object counts
   (log), `dmesg` OOM lines, standing-still fps at the arrival pose and during a
   short forward run (T459's CAMERA trace not needed). Record Pi free memory
   before each run.
3. **Report the table and a verdict per zone:** fits / fits with evictions /
   fails. If any zone fails or evicts heavily, do NOT change budgets or drop
   objects — report, and name the lever candidates (per-object LOD/draw distance,
   instancing, budget) for the orchestrator to put to Will.
4. **Gate number:** the qeynos2 `placeableObjectsBytes` change (1,365,564 →
   1,389,324) is Will's to bless — do not update `tests/goldens/manifest.json`.

## Proof

Pi + eqemu protocols (lock, ≤12 Pi invocations, Pi pristine, poses restored,
world down). Media/logs `review/T481/`. Board B750–B759; narrow/strike B735. No
source change unless the Pi run exposes a crash (then STOP and report).

File territory: `review/T481/`, `refactor/BOARD.md`, the plan. Read-only on the
T479 branch.

STOP: a zone fails to load or the client is OOM-killed on the after build (report
with logs; no fix in this unit).

## Review

(executor fills)
