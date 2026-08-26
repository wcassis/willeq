# T293 — derive per-category memory budgets (Will's ruling 2026-08-26)

Slot: slot2. x86 for the derivation; **NO Pi run** (T289 already measured the
envelope — reuse its data). NO eqemu unless a zone-in is needed for a category
number that T289/T287 did not capture. Class A/C-resolved. Territory:
`config/constrained_presets.json`, `include/client/graphics/constrained_renderer_config.h`
(new budget fields if needed), `scripts/ci/visual_gate.py`'s category
assertions, docs/decisions.md, tests. Do NOT touch `protectedRegions_` (T292
slot1) or the gate's CPU metric (T291 slot3) — coordinate: T291 is editing
`visual_gate` too, so keep to the MEMORY assertions and rebase carefully.

## The ruling
Will, 2026-08-26: **"For memory budgets, extrapolate out from the assets on
disk, 1280x720 max resolution, orange pi one (512mb ram, arm, 32bit, etc), and
come up with sensible defaults."** T69 explicitly refused to invent these
("Proposing numbers is Will's call, not this unit's; recording the absence is
the deliverable") — that block is now discharged. **Show the working; the
numbers matter less than the reasoning being auditable.**

## Inputs that already exist — do not re-measure what is measured
- **T289 (`review/T289/`)**: real Pi peaks — RSS 209–230 MB, VmPeak 571–575 MB,
  MemAvailable floor 55–66 MB, four runs, composites on and off.
- **T195**: the Pi's usable RAM is ~483 MB, and its binding constraint is the
  **32-bit address-space ceiling (~3 GB)**, not RAM. Both ceilings must be
  respected; say which binds each category.
- **T287/T284**: EQE1 mapped bytes per zone, composite sheet sizes.
- Assets on disk: `/mnt/projects/EverQuest_ETC1` is **4.8 GB across 1506
  packs** — the budget is emphatically NOT "fit the assets in RAM"; it is
  "bound the resident working set", and the report should say so.
- Current orangepi preset: `textureMemoryBytes` 64 MiB, `framebufferMemoryBytes`
  10 MiB, `maxTextureDimension` 256.

## Scope
1. **Enumerate the categories as they exist now** (T69's warning: older docs
   describe categories that no longer exist). For each, record measured usage
   from the evidence above, and which are already budgeted (a) vs merely
   baselined (b).
2. **Derive a budget per category** from the Orange Pi One envelope: 483 MB
   usable, minus the OS/other floor T289 observed, leaving the client's share;
   then split it by measured proportion with headroom. State the headroom rule
   you used and why. A category too jittery to bound (T69 Step 3) stays
   reported-not-asserted — say so rather than inventing a number.
3. **Check the framebuffer number against 1280x720 explicitly.** 1280x720x4 =
   3.69 MB per buffer; double-buffered plus a depth/stencil attachment is
   ~11 MB against a configured 10 MiB. **If the shipped budget cannot hold the
   stated maximum resolution, that is a finding — report it with the
   arithmetic** rather than quietly raising the number.
4. Wire the derived budgets into the preset(s) and turn the (b) categories that
   now have a budget into real `usage <= budget` assertions in the gate.
5. Record the numbers AND the derivation in `docs/decisions.md` so the next
   person can audit them.

## Verification
Full ctest (`-j6`); visual gate (the memory assertions are the point — if a
category now exceeds its derived budget, that is a REAL finding: report it,
do not raise the budget to make the gate pass); Review with the derivation
table; evidence review/T293/. Commit:
`T293: per-category memory budgets derived from the Orange Pi One envelope`.
