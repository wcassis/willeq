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

## Review

Evidence: `/mnt/projects/fable/review/T293/` (README.md names every file).
The derivation is recorded permanently in `docs/decisions.md` under
**"2026-08-26 — per-subsystem memory budgets (T293)"** — that section, not
this one, is the audit trail the plan's Scope item 5 asked for; it carries the
envelope table, the asset-scale table, the full budget table and the residual
gaps. This section records what was done, what was found, and the deviations.

### Scope item 1 — the categories as they exist now

Thirteen measured categories, taken from what `zone_bench_memory.cpp` actually
emits at `43e3430e` (not from older docs, per T69's warning). Before this unit,
**two** had a configured ceiling; **eleven** were baseline-only. After it,
**ten** have a ceiling and are asserted; three remain reported-only.

| category | before T293 | after T293 |
|---|---|---|
| `textureCacheBytesUsed` | (a) budgeted, 64 MiB | (a) unchanged |
| `framebufferBytes` | (a) budgeted, 10 MiB — **but the assertion never fired**, see the defect below | (a) budgeted **and now actually asserted** |
| `gpuTextureBytes` | (b) baseline only | **(a) 64 MiB** |
| `zoneGeometryBytes` | (b) | **(a) 16 MiB** |
| `placeableObjectsBytes` | (b) | **(a) 24 MiB** |
| `s3dZoneSourceBytes` | (b) | **(a) 24 MiB** |
| `vboEboBytes` | (b) | **(a) 40 MiB** |
| `characterModelsBytes` | (b) | **(a) 40 MiB** |
| `eqe1MappedBytes` | (b) | **(a) 256 MiB — ADDRESS SPACE, not RAM** |
| `totalEstimate` | (b) | **(a) 128 MiB** (`totalMemoryBudgetBytes`, which had shipped since long before T69 with nothing asserting against it) |
| `eqe1ArchiveCount`, `textureCount` | (b) counts, not bytes | (b) unchanged — a byte budget is the meaningful ceiling; the counts stay regression guards |
| `processRssBytes`, `untrackedBytes` | (c) reported only | (c) unchanged — T69 Step 4's ruling stands, and the Pi's split is unmeasured (see below) |

### Scope item 2 — the derivation

Envelope, asset-scale factors, headroom rule and the per-category table are in
`docs/decisions.md`. Two things worth restating here because they are the parts
a reader is most likely to challenge:

- **The rule is `ceil_8MiB(worst_measured × asset_scale_from_disk × 1.25)`**,
  where `asset_scale_from_disk` is the ratio of the largest asset of that kind
  across all 310 zones to the largest among the three zones the gate measures.
  That is the "extrapolate out from the assets on disk" half of the ruling made
  concrete: 2.34x for zone geometry (ponightmare 9.72 MB vs qeynos2 4.16 MB),
  3.56x for placeable objects (dulak_obj 10.60 vs gfaydark_obj 2.98), 4.28x for
  character models (griegsend_chr 10.45 vs qeynos2_chr 2.44). The 1.25 is
  explicit slack for what neither measurement nor the directory listing covers
  (composite-entity archives, on-demand equipment models).
- **Two rows deliberately do not use that rule**, because a disk ratio would
  be meaningless for them, and both alternatives are *measurements*, not
  guesses:
  - **VBO/EBO is derived by identity.** It measured as exactly the GPU mirror
    of zone geometry + placeable objects (gfaydark 10.22 MiB vs 4.76 + 5.36 =
    10.12 MiB), so its budget is the sum of those two budgets, 16 + 24 = 40 MiB.
    A test pins that relationship so the three cannot drift apart.
  - **Character models is derived from a measured race-count sweep**, because
    the category is overwhelmingly fixed cost, and a flat multiple of the
    measured value would have budgeted the fixed part twice. Five runs on
    qeynos2 with 1/2/4/8/12 resident races: 24,831,127 → 26,910,544 bytes, i.e.
    **23.68 MiB fixed plus 189 KB per additional resident race**. Budget =
    23.68 + 40 races × 0.18 + 8.0 MiB for the largest `_chr` on disk = 38.5 →
    40 MiB. Forty resident races is ~3x the 14 distinct models T289 measured
    live in befallen on the Pi, which is T289's own stated caveat ("13 MB is
    not a budget that survives being multiplied") taken seriously.

**No category was too jittery to bound.** T69 (9 runs, 3 zones) and T80 (11
runs, 3 zones) both measured every one of these fields bit-identical run to
run; nothing in this unit's runs contradicted that, so no category needed the
"reported-not-asserted" treatment, and no tolerance was invented — the budget
assertions are inequalities against a config value and do not need one at all.

### Scope item 3 — the framebuffer verdict at 1280x720

**The shipped 10 MiB budget holds the stated maximum resolution.** The plan's
arithmetic assumed 32-bit colour; the `orangepi` preset is 16-bit.

    16-bit (as shipped): (2*2 + 4) =  8 B/px * 921,600 =  7,372,800 B =  7.03 MiB   FITS (67% of 10 MiB)
    32-bit:              (2*4 + 4) = 12 B/px * 921,600 = 11,059,200 B = 10.55 MiB   OVER by 573,440 B

So the plan's "~11 MB against a configured 10 MiB" is right *for 32-bit colour*
and the answer would then be that the budget cannot hold 1280x720 — but that is
not what ships. **The budget was NOT raised.** 16-bit is the shipped
configuration and Mali 400 has no reason to want 32. What was done instead is
pin both halves so the reason 10 MiB suffices cannot be lost:
`OrangePi_FramebufferBudgetHolds1280x720` asserts the 7,372,800-byte figure,
that `validateResolution(1280,720)` passes, and that `clampResolution()` leaves
1280x720 untouched; `OrangePi_FramebufferBudgetWouldNotHold1280x720At32Bit`
asserts the 11,059,200-byte figure, the exact 573,440-byte overrun, that
`validateResolution()` fails, and that the silent alternative would have been
`calculateMaxResolution()` → 1072x808 and a 1280x720 request narrowed to
1072x720. A future `colorDepthBits` edit now fails a test instead of quietly
shrinking the display.

### Scope item 4 — wiring, and a real defect found doing it

Budgets flow from one place. `ConstrainedRendererConfig` gains seven fields
(`gpuTextureMemoryBytes`, `zoneGeometryMemoryBytes`,
`placeableObjectsMemoryBytes`, `s3dZoneSourceMemoryBytes`, `vboEboMemoryBytes`,
`characterModelsMemoryBytes`, `eqe1MappedAddressSpaceBytes`); `fromPreset()`
sets them for `OrangePi` only; `loadJsonOverrides()` parses all seven (P39 and
P42 both caught preset keys that shipped in the JSON with **no parse behind
them** — silently un-tunable and misleading to anyone reading the JSON as the
source of truth, so a test actively proves these seven resolve from the real
shipped file); `config/constrained_presets.json` mirrors them;
`zone_bench_memory.cpp` reads them off the *same* config object the loader
already built (`ConstrainedTextureCache::getConfig()`) and emits them next to
each usage number, so CI holds no second copy of the numbers.

**Defect found: T69's framebuffer budget assertion had never once fired.** The
budget check lived inside the loop that exact-matches usage against the blessed
baseline, and that loop `continue`s when `used_key not in expected_mem`. The
committed `tests/goldens/manifest.json` carries only the three pre-T69
texture-cache fields in each zone's `memory` block (T69 and T80 both shipped
unblessed, by instruction), so `framebufferBytes` was always absent and the
whole pair — budget check included — was skipped every run. Wiring eight more
budgets into that same loop would have shipped eight more dead assertions.

Fixed by splitting the two checks: `assert_memory_budgets()` asserts
`usage <= budget` from the live benchmark output with **no baseline
dependency** (a budget is a config value, not a measurement, and is knowable
without a bless), while baseline exact-matching stays in the
`MEMORY_BASELINE_FIELDS` loop where it belongs. Consequence: **every budget
assertion in this unit is live today against the committed, unblessed
manifest** — nothing here is waiting on a bless. It also runs on the T80
memory-coverage invocation, which is the only run where placeable objects,
character models and EQE1 mapped bytes are nonzero and therefore the only run
where those three budgets mean anything. A budget of 0 is skipped and *named*
in the printed summary ("NO BUDGET CONFIGURED for ..."), so an unbudgeted
category stays visible instead of looking checked.

### Is anything over budget?

**No.** Utilisation, both benchmark paths, all three zones (from
`review/T293/visual_gate.log`):

| category | golden-image path (max) | coverage path (max) |
|---|---|---|
| texture cache | **63%** (qeynos2) | 14% |
| framebuffer | 37% @800x600 (**70%** @1280x720) | 37% |
| GPU textures | 47% (qeynos2) | 14% |
| zone geometry | 30% (gfaydark) | 30% |
| placeable objects | 0% (path loads none) | 22% (gfaydark) |
| S3D zone source | 31% (gfaydark) | 31% |
| VBO/EBO | 12% (gfaydark) | 26% (gfaydark) |
| character models | 0% (path loads none) | **61%** (befallen) |
| EQE1 mapped address space | 0% (path maps none) | **45%** (qeynos2) |
| tracked total | 34% (qeynos2) | **49%** (gfaydark) |

Nothing above 63%. The three highest are the ones to watch, and they are the
three with the least derived headroom by construction: the texture cache and
the tracked total are pre-existing numbers this unit did not change, and
character models was derived from a marginal-cost measurement rather than a
multiple, which is exactly why it sits highest.

### Proof the gate fails when breached

A gate never observed failing is not known to work (T35's lesson). Following
T69's and T80's precedent, `review/T293/breach_test.py` imports the **real**
`visual_gate.py` and exercises `assert_memory_budgets()` against a live
benchmark memory JSON. It first asserts the committed manifest has **no**
baseline for any of these fields — i.e. that what follows is proving the
baseline-independence the defect fix bought. Results (`breach_test.log`):

1. clean numbers, no baseline in the manifest → **0 failures** (and the
   `[budget]` utilisation line prints all ten categories).
2. `characterModelsBytes` + 1 → `character models bytes used 41943041 exceeds
   budget 41943040`.
3. `eqe1MappedBytes` + 4096 → `EQE1 mapped address space bytes used 268439552
   exceeds budget 268435456`. This is the T195 failure mode made catchable:
   the eager directory scan mapped ~2,900 MB.
4. `totalEstimate` + 1 → `tracked total bytes used 134217729 exceeds budget
   134217728`.
5. budget set to 0 → skipped and reported as "NO BUDGET CONFIGURED", **not**
   asserted as `usage <= 0`.

Each breach produced exactly one failure — no double-reporting, which is why
`MEMORY_BUDGET_PAIRS_ALSO_BASELINED` restricts the (a') exact-match loop to the
two pairs `MEMORY_BASELINE_FIELDS` does not already cover.

### Gaps deliberately left open

- **Untracked RSS is 76–90% of RSS and stays ungated.** T69 Step 4 ruled it
  reported-not-gated; nothing here changes that. More importantly the x86
  figure is inflated by llvmpipe and does **not** transfer to the Pi, and
  **T289 did not dump `/pmem`**, so the Pi's tracked/untracked split is
  unmeasured. That is the one place the envelope arithmetic cannot be closed
  end to end: the 256 MB client RSS ceiling is derived and defensible, and the
  per-category budgets are derived and defensible, but the bridge between them
  needs a `/pmem` dump on a live Pi zone-in. **No number was invented to paper
  over it.** That measurement is the natural follow-up and costs one Pi
  session.
- **Categories no benchmark constructs** (entity per-instance game state,
  equipment models, HCMap/collision, audio, network, door/spell DB) remain
  unbudgeted, unchanged from T69/T80. A budget on a number nothing measures
  would be decoration.
- **Non-OrangePi presets** stay at 0 = no budget configured, pinned by a test.

### Deviations

1. **The plan's framebuffer arithmetic assumed 32-bit colour.** The shipped
   preset is 16-bit, so the answer is "fits" rather than the "cannot hold the
   stated maximum resolution" finding the plan anticipated. Both cases are
   reported with the arithmetic and pinned by tests; the budget was not
   changed either way. Flagged rather than silently absorbed because the plan
   asked a specific question and got a different answer.
2. **The defect fix in `visual_gate.py` is slightly beyond "add assertions".**
   Restructuring the budget check out of the baseline-coupled loop changes
   behaviour for the two pre-existing pairs (the framebuffer one starts firing;
   the texture-cache one is unaffected because it *is* blessed). It was not
   optional: wiring eight budgets into a loop that skips unblessed fields would
   have shipped eight dead assertions and called the unit done.
3. **`totalEstimate <= totalMemoryBudgetBytes` is an aggregate assertion.**
   Will's standing ruling is "per-subsystem budgets, no global cap, no
   cross-asset coordinator". That ruling is about **runtime behaviour** —
   nothing here evicts, coordinates or allocates. This is a CI assertion on a
   number the preset has carried since before T69 with nothing checking it, and
   it is what makes the per-category ceilings safe to leave summing to 282 MiB.
   Called out explicitly rather than assumed benign; trivially removable if Will
   reads his ruling as covering CI too.
4. **A pre-existing asset gap surfaced, not fixed.** Extending the race sweep
   past 12 races aborts: `Texture 'avihe0005.bmp' not found in any EQE1 flat
   pack` (races 13–16, the aviak set). That is the EQE1 all-or-nothing rule
   working as designed on an incomplete pack set — out of this unit's
   territory, and the sweep's derivation only needed races 1–12. Recorded here
   so it is not rediscovered.
5. **Rebased onto `fable` `8a6f555e` (T290/T291/T292 merged) mid-unit**, as
   the dispatch anticipated. The rebase was clean -- no conflict with T291's
   `visual_gate.py` CPU-metric work, because this unit's edits are confined to
   the MEMORY assertions as instructed. Everything was re-verified on the
   rebased tree: full build, ctest **3208/3209** (sole failure the documented
   ALSA quirk), visual gate **PASS** with the new `[budget]` lines and goldens
   untouched, breach proof re-run and still passing, `check_file_sizes.sh`
   **5 files checked, 5 ok, 0 warn, 0 fail**. (Run before the rebase the script
   also listed T290/T291/T292's files, because it compares against whatever
   `fable` currently points at -- that produced a pre-existing FAIL on
   `tools/hwcaps/gles2_zone_benchmark.cpp` and a pre-existing WARN on
   `include/client/graphics/irrlicht_renderer.h`, neither of them this unit's
   and neither in its diff. After the rebase the comparison is correct and
   clean.)

   One incident worth recording: a 10-minute Bash timeout killed a `cmake
   --build` mid-link and left **ten test binaries silently truncated** (`file`
   reported `data`/`empty`, not ELF). ctest then failed at *discovery* with a
   shell-parse error on the corrupt executable rather than a test failure. If
   a ctest run ever dies with `Syntax error: word unexpected` naming a test
   binary, that is a truncated link, not a code defect: `find build/bin -type f
   -exec file {} + | grep -v 'ELF 64-bit'` finds them, delete and rebuild.
6. **Territory note.** The plan named `tools/hwcaps` only implicitly ("the gate
   assertions"); emitting the budgets required `zone_bench_memory.{h,cpp}` (+46
   lines, no new tally — the values are read off the existing config object).
   `tests/test_constrained_renderer.cpp` was extended rather than a new TU
   created: +134 lines onto an existing 1035-line file, still `ok` at 1169.
   `visual_gate.py` edits are confined to the MEMORY assertions, per the
   coordination note about T291.
