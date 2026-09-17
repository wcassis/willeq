# Architecture Decisions

Standing decisions for WillEQ. Sessions and agents must treat these as settled —
do not re-litigate or contradict them without explicit direction from Will.
Newest entries first. Each entry: decision, rationale, implications.

---

## 2026-09-05 — The ceiling covers `tools/` and `benchmarks/` too (T385)

**Decision:** `scripts/refactor/check_file_sizes.sh --all` scans `src/`,
`include/`, `tests/`, `tools/` **and** `benchmarks/`. The M00 rule is unchanged
in shape; it now applies to every C++ file in the tree, which is what M00 said
it did.

**Rationale:** M00 said "tree-wide" and `--all` scanned three directories.
`tools/` was never checked, and it held **six** files over the ceiling —
`tools/hwcaps/zone_bench_loader.cpp` 3,280, `tools/model_viewer.cpp` 3,246,
`tools/hwcaps/gles2_zone_benchmark.cpp` 3,060, `tools/model_viewer_spell_bar.h`
1,965, `tools/entity_composite_builder.cpp` 1,861,
`tools/hwcaps/gles2_fog_volume_benchmark.cpp` 1,711 — three of them over the
3,000-line hard cap, while `FileSizeCeiling` reported a clean tree. `tools/`
holds production-API callers (board B166: `tools/model_viewer.cpp` was the one
caller of `setSpellDatabase()`), so it is not a second-class directory. <!-- RETIRED-OK: the historical fact that made the case for scanning tools/; T381 deleted the method -->

**Implications:** a scanned directory that does not exist is skipped, so
`benchmarks/` (no C++ today) costs nothing and is covered the day it grows
some. `tools/retired/` is scanned like everything else. The allowlist is still
the one reviewable exception mechanism and is still empty — a shader table or a
hand-written parser is not "generated data" and does not earn an entry.

## 2026-09-04 — The 1,500-line ceiling is ENFORCED, and the allowlist is empty (M00)

**Decision:** 1,500 lines is the enforced ceiling for every `*.cpp`, `*.h` and
`*.hpp` under `src/`, `include/` and `tests/`. Under
`scripts/refactor/check_file_sizes.sh --all` — which ctest runs as
`FileSizeCeiling` — a file over 1,500 that is not allowlisted is a **FAIL**,
no longer a WARN. The 3,000-line hard cap stays as a second line of defence.

**Rationale:** the soft limit existed because two single-class headers could
not meet it. Both now do: `include/client/eq.h` fell 2,025 -> 899 under
`refactor/K_component_extraction.md` and
`include/client/graphics/irrlicht_renderer.h` fell 1,914 -> 1,343 under
`refactor/L_renderer_component_extraction.md`. With Batches J and G also
complete, all 866 files in the tree are under the ceiling, so nothing is left
for a warning to carry — a WARN nobody has to act on decays into noise.

**Implications:** `scripts/refactor/file_size_allowlist.txt` is now EMPTY (kept
present, comment-only, so `--all` still parses it). A future single-class file
that genuinely must exceed 1,500 gets an allowlist entry with a reason and a
no-grow maximum, exactly as before — the K00 ratchet is unchanged, and the
entry is the deliberate, reviewable act that permits the exception. The
diff/`--files` modes still print WARN over 1,500 (they do not consult the
allowlist); the tree-wide `--all` run is what turns it into a failure, so an
executor who sees that WARN should expect ctest to fail.

## 2026-09-03 — File-size ceiling and doc citations are enforced tree-wide by ctest (K00)

**Decision:** `scripts/refactor/check_file_sizes.sh --all` and
`scripts/refactor/check_doc_citations.py` run as ctest tests
(`FileSizeCeiling`, `DocCitations`, label `hygiene`) against the whole tree,
not just files changed vs a base revision.

**Rationale:** the diff-only mode enforced the ceiling only for the unit
that introduced a file; nothing re-checked it once merged, and no
mechanism caught a live doc's citation going stale as the tree moved
under it.

**Implications:** `include/client/eq.h` and
`include/client/graphics/irrlicht_renderer.h` are allowlisted in
`scripts/refactor/file_size_allowlist.txt` at their measured line counts
(single-class files pending `refactor/K_component_extraction.md`) under a
no-grow ratchet — each K unit that shrinks one lowers its allowlisted
maximum, never raises it. No other file may exceed 1,500 lines without a
new allowlist entry and a reason. Every live doc (root and subdirectory
`CLAUDE.md`, `docs/*.md`, `refactor/BOARD.md`,
`refactor/EXECUTOR_PROTOCOL.md`, `refactor/K_*.md`) must cite only
resolvable paths and in-bounds line numbers.

## 2026-08-25 — Loading time is SECONDARY to correctness; there is no maximum load time

**Decision (Will):** the only bar for zone loading is that it **completes
successfully with the appropriate models loaded**. Pi zone-in duration
(~37–40s, T186/T208) is not a problem, a target, or a work item on its own.

**Implications:** do not raise load time in status reports or plans;
investigate it only if Will asks, or if slowness is evidence of a
correctness defect (redundant work indicating a caching bug — T209
`d6a96103` checked exactly that for `seq_entities` and refuted it: ~80% is
irreducible first-time construction of distinct models).

## 2026-08-25 — EQE1 is ALL-OR-NOTHING: no S3D fallback, no partial load

**Decision (Will):** every file used to build the EQE1 archives is on disk,
so the EQE1 set MUST contain every required asset. A missing texture is a
defect in the asset build (or in `config/eqe1_archives.json` coverage —
T198 `09d6ef03`), not a case to degrade around. The `exit(1)` at
`constrained_texture_cache.cpp` on a missing EQE1 texture is **correct
behaviour**.

**Implications:** lazy/partial opening is incompatible by construction —
the archive set must be right BEFORE the first lookup, which is why the
whole-directory table-of-contents index (T196 → T199 `3edeb0dd`, T207
`ed14c4de`) is the shape that works and the per-filename lazy open (T194,
reverted `edbbb90c`) could not. Do not propose S3D fallback again.

## 2026-08-25 — `enableCompositeEntities` is OFF on the orangepi preset

**Decision (Will, T198 `28a59bf4`):** the composite atlas is removed from
the `orangepi` preset because the Pi cannot handle the memory/address-space
pressure (T195 `0157fdc7`, T197 `09483f60`). The struct default is already
false. Not a knob to revisit for load-time reasons (see above). EQE1's two
real wins are flat-pack file I/O and ETC1 compression; the composite atlas
is not sold — most textures are UV-wrapped and cannot benefit.

## 2026-08-25 — The skills window is settled by SCREENSHOT, not by opinion

**Decision (Will):** run the official Titanium client under Wine on the
eq-oracle rig, open the skills window, screenshot it, and display whatever
it shows. The screenshot is authoritative over both the invented `9 / 50`
and T159's lifetime cap `9 / 525`. This is an OWED TASK, not an open
question — never ask Will to pick a cap number.

## 2026-08-25 — EQE1 archive rule: when an open archive is legitimate (T214)

**Decision (Will, T214 `e70e3347`):** an archive is legitimately open iff
- **R1** it is the CURRENT ZONE'S OWN archive (`{zone}`, `{zone}_obj`,
  `{zone}_chr`), or
- **R2** a lookup needed it and nothing already open could serve it.

Only R2 archives can be waste, and only by serving nothing. Zero lookups on
an R1 archive is NORMAL (a zone whose NPCs all use `global_chr` models
never touches `{zone}_chr`) — do not read the EQE1-USAGE report's idle
zone-own archives as waste. The reporter now classifies before counting.
Persistent client packs (`willeq.eqe1`, opened once at init) are retained
across `unloadZone()` (`3e4e8a95`).

## 2026-08-25 — Recorded rulings from the T150–T214 arcs (so they are not re-litigated)

- **Raid leadership gate (T183 `9ee03a23`):** `/raid remove` is GATED
  client-side on leadership, matching the real Titanium client rather than
  the server's absent policy. The gate is OUR policy and the code says so.
- **T152 live crash reproduction — SKIPPED at Will's call (2026-08-22);**
  written up for upstream instead.
- **GM decision (2026-08-19):** `m_gm_mode` gates our GM commands; it is not
  and cannot mirror server-side `Client::Admin()` (account status never
  reaches the client). Do not "upgrade" it into one (T148).
- **Blessing rule:** Will blesses in chat, never by running commands.
  Numbers being "ready" is not the same as being blessed.

## 2026-07-22 — Workspace layout: namespaced root with executor slots

**Decision:** The canonical `fable` checkout lives at `/mnt/projects/fable/main`
(a linked worktree of the main repository at `/mnt/projects/claude/willeq`).
A compatibility symlink exists at the old path `/mnt/projects/willeq-fable`.
Refactor executor agents work in persistent reusable worktree slots
`/mnt/projects/fable/wt/slot1..3`, each with its own long-lived `build/`
directory (no ccache on this host — warm incremental builds are the cache).

**Rules:**
- Task branches are namespaced `task/F##-<name>` (e.g. `task/F01-commands`).
  Legacy branches (~50 exist) are out of scope and must not be touched.
- Executors never merge, never touch `main/`, never reach into sibling slots.
- The orchestrator serializes merges: rebase task branch on `fable`,
  fast-forward merge in `/mnt/projects/fable/main`, delete task branch, release slot.
- Slot assignments are recorded in each unit's plan (`refactor/T<nn>_*.md`); `refactor/F_file_split.md` was the Batch F plan and is complete (T215 correction).

## 2026-07-22 — `fable` is the active integration branch

**Decision:** Batch F (and subsequent work) lands on `fable`. `main` stays behind
until Will chooses to sync it. Task branches are cut from and merged back to `fable`.

## 2026-07-22 — `renderer` branch is reference-only, not a merge candidate

**Decision:** The `renderer` branch (69 commits of Irrlicht-removal work, last
commit 2026-03-26, checked out at `/mnt/projects/willeq-renderer`) will never be
merged. It diverged two-way from `fable` (~13k lines rewritten there vs ~2.8k
lines of composite/LOD work added here). Treat it as a **reference
implementation** for the future module-by-module Irrlicht re-port.

**Rationale:** Four months of two-way divergence in `src/client/graphics/` makes
a merge worse than a re-port. A split codebase (post Batch F) makes per-module
porting dramatically easier.

**Implication:** Any doc or memory claiming "Irrlicht removal is in progress"
refers to that stale branch. On `fable`, Irrlicht is fully present (~2,900
`irr::` references) and removal has not started.

## 2026-07-22 — Irrlicht removal strategy: incremental in-place

**Decision:** Of the two mutually exclusive proposals, the **incremental
in-place removal** (`docs/future/custom_renderer.md`, 7 chunks) is the chosen
strategy. The clean-room C99 rewrite proposal is rejected and archived at
`docs/archive/c99_handoff.md`.

**Sequencing:** Irrlicht re-port starts only after Batch F (file splitting)
completes, using the `renderer` branch as reference.

## 2026-07-24 — Zone lighting budget: COUNT mode stays production default (P05→P07d investigation closed)

**Decision:** The adaptive per-pixel zone lighting budget remains COUNT
mode (nearest-N-regions-by-draw-order keep per-pixel, default budget 20).
The P05 outskirts anomaly is understood, bounded, and ACCEPTED as the cost
of correct near lighting.

**The full evidence chain (refactor/P05..P07d reviews):**
- P05: sparse-region poses pay full per-pixel fill (2.2-2.6x) because a
  region-count budget never engages there. Cost at the worst poses:
  ~18ms/50fps at 720p — above the 30fps KPI; not the KPI bottleneck.
- P06 (area mode): statistically all-lightweight (the camera-containing
  region's near-plane fallback exhausts any k<1 budget at region 1);
  visually unlights near geometry — per-vertex light interpolates to ~0
  on giant polys (black walls).
- P07/P07b (area+floor): fixes the underfoot case, passed all timing
  criteria on the Pi, but P07c's golden regeneration exposed near-field
  blackouts of frame-dominant structures (the building you look AT can be
  region #2-3 by distance). Part of area-mode's measured savings was
  unlighting visible geometry. Flip REJECTED at orchestrator review.
- P07d (radius mode, "lossless by construction"): the player light's
  effective range is R=712 EQ units — it reaches essentially the whole
  zone, so truly-lossless demotion demotes ~nothing (0 regions at all 15
  reference views; radius ≈ full-PP cost). Genuinely free savings DO NOT
  EXIST at the current light attenuation.
- Bonus finding: count-mode itself is visibly different from full
  per-pixel at 7/13 reference views (up to 69% of pixels) — and it is the
  config Will has played and accepted. "Pixel-identical to full-PP" was
  never the real quality bar; "looks right in practice" is, and that bar
  is Will's eyes on contact sheets.

**Implications:**
- Per-pixel lighting cost can only be cut by visibly changing lighting
  somewhere. Further lighting micro-optimization is poor ROI; the 30fps
  KPI problem is draw-call/CPU-bound (befallen p95: 368 draws, 26.5ms
  CPU). Draw-call reduction (P08) is the funded thread.
- Area/floor/radius modes remain in the tree as flag-gated experiments
  (config-selectable, default count). The one future lever: radius-mode
  with margin<1 (perceptual rather than 1/255 threshold) — would need a
  visual-knee study; only revisit if lighting cost resurfaces after the
  draw-call work.
- task/P07c-default-flip stays parked unmerged (its contact sheets
  document the rejection). Its harness-alignment idea survives as a
  deferred item: the visual gate and Pi tier-2 baseline currently measure
  FULL per-pixel, not production count-mode config — align them (with a
  count-mode golden re-bless) so KPI tracking reflects what ships.

**Addendum (2026-07-25, Q03):** P07d's "radius mode demotes ~nothing"
finding was a property of the BENCHMARK FIXTURE, not the game — the bench
lamp (fixed atten, R=712) reached the whole zone, while the production
player light is a lightstone (level-dependent radius 20-100; Greater
Lightstone = level 9, effective R=336 with the ×3 shader color boost the
first Q03 pass missed). The harness now mirrors the real light
(Q01/Q02/Q03, goldens blessed by Will 2026-07-25). Under the honest lamp,
radius mode IS provably lossless while demoting real regions, and count
mode is provably lossy at >20-region views — the count-stays-default
decision above still stands on TIMING alone (radius measured 1-17% slower
in Q03), not on "savings don't exist." The flip remains an open candidate
in refactor/completed/deferred_archive_20260903.md pending re-measurement under the final fixture.

## 2026-07-24 — Texture format: KEEP ETC1 (Will's decision, P03 data)

**Decision:** ETC1 flatpack (EQE1 v1) remains the production texture path.
Measured fairly (P03): ETC1 wins steady-state p95 (-3.5 to -12.4% vs RGBA),
GPU texture memory (8x), pack bytes, and cold load (NFS-transfer-bound).
The suspected CMA leak was overturned (page-cache borrowing of movable CMA
pages, fully reclaimable). Quality cost (29.5dB vs lossless) accepted.
RGB565/4444 v2 payloads remain implemented and measured as the fallback
(one config flag away) if ETC1's complexity costs (alpha companions,
unrolled textures) ever tip the balance.

**Companion decisions from the same review:**
- The real instability is the v1 loader's whole-file heap loading (139MB of
  never-referenced global archives on a 494MB board) — fix funded (P04:
  mmap/streaming + referenced-archives-only + full sequential-loader memory
  audit; /pmem has shown large UNTRACKED RSS historically).
- Per-pixel lighting STAYS ENABLED as a feature; headroom comes from
  dynamic LOD, draw-call reduction, and loader/memory fixes — not from
  defaulting to lightweight (Will: >45fps WITH per-pixel mid-city qeynos2;
  the outskirts render SLOWER despite simpler scenes — anomaly under
  investigation, P05).
- Draw-call reduction/minimization remains a critical strategic thread
  (it motivated composite entities and is not finished).

## 2026-07-23 — Batch F complete; per-step decomposition deferred to Batch G

**Status:** Batch F finished 2026-07-23 — all 25 units, 40 new domain TUs,
every move verifier-proven behavior-preserving. Final layout:
`docs/module_map.md`. Follow-up (Batch G) candidates in `refactor/completed/deferred_archive_20260903.md`:
per-step helper decomposition of `loadZoneSequential` (needs header changes,
outside move-only rules), splitting `eq_commands.cpp` (2,568 lines), and the
Step-2 decoupling residue.

## 2026-07-22 — Batch F: mechanical TU splits before any deeper refactor

**Decision:** `eq.cpp` (19,676 lines) and `irrlicht_renderer.cpp` (16,394
lines) are split into domain translation units **without changing any class
interface or behavior**. Classes stay intact; only method definitions move to
new `.cpp` files. Every unit is verified by build + ctest + the move-only
verifier (`scripts/refactor/verify_move_only.py`).

**Pattern rule for later class extractions:** follow the `TradeManager`
callback-injection pattern (no `EverQuest*` back-pointer). Do **not** replicate
the `CombatManager`/`SpellManager` `friend class` + back-pointer pattern.

**File-size ceiling:** new/split files target ≤1,500 lines;
`scripts/refactor/check_file_sizes.sh` warns >1,500 and fails >3,000 for
changed files. Legacy files are exempt until their split unit lands.

## 2026-07-22 — Documentation truth tiers

**Decision:** Docs are split into two tiers.
- **Load-bearing (kept accurate):** all `CLAUDE.md` files, `docs/decisions.md`,
  `docs/module_map.md`, `refactor/BOARD.md`,
  `docs/reference/arch_design_orig.md` / `arch_design_update.md`. (The former
  `progress.md` and `deferred.md` trackers under `refactor/` were archived by
  K00, 2026-09-03, superseded by `refactor/BOARD.md` — see
  `refactor/completed/progress_archive_20260903.md` and
  `refactor/completed/deferred_archive_20260903.md`.)
- **Historical (banner, never fixed):** `refactor/completed/*`, the rest of
  `docs/reference/*`, `docs/archive/*`. These carry a header marking them as
  point-in-time snapshots; superseded findings inside them are expected.

Open work items deferred out of "done" batches live in
`refactor/completed/deferred_archive_20260903.md` at the time of this
decision (2026-07-22); superseded by `refactor/BOARD.md` (K00, 2026-09-03),
not inside completed batch files.

## 2026-07-28 — GLES2 is the only 3D renderer

**Decision (Will):** The custom GLES2 backend (`COpenGLES2Driver`, six GLSL ES
programs) is the sole 3D renderer going forward. The desktop-GL path (system
libirrlicht, X11/GLX) is to be eliminated, not maintained.

- x86 rendering runs the same GLES2 driver via Mesa EGL: surfaceless/llvmpipe
  for CI, goldens, and benchmarks (H01), and an X11-window EGL surface for
  interactive dev use (P25). The Pi keeps DRM/KMS.
- The full test suite and all retained tools must build against the patched
  GLES2 Irrlicht; desktop-GL-only tools are ported or retired (survey: P24,
  plan doc `refactor/desktop_gl_elimination_plan.md`).
- Consequences: desktop GL stops being a diagnostic cross-check baseline
  (goldens/visual gate carry that role); anything still referencing
  `EDT_BURNINGSVIDEO`/desktop driver types is dead code to remove during the
  elimination, not to preserve.

**Elimination progress:** Phase 0 (P26, tool link fixes), Phase 2+3 (P30,
`build`/`build-gles2` become GLES2-config trees, system-Irrlicht CMake
fallback removed, mandatory-Irrlicht configure gate added) executed
2026-07-24..28. Phase 4 (delete `EDT_BURNINGSVIDEO`/`AnimatedTreeManager`/
`RenderingBackend::Software`/`RenderingBackend::OpenGL`) attempted 2026-07-28
(P31, `refactor/P31_phase4_5_dead_code_docs.md`) but **not executed** —
verification found these are not provably dead:
- `RenderingBackend::Software` (→ `EDT_BURNINGSVIDEO`) is directly reachable
  today via the documented `--renderer software` / `--constrained
  voodoo1|voodoo2` flags on the exact GLES2-config `willeq` binary the
  verification bar builds, and `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp`'s
  `createDriver()` compiles `createBurningVideoDriver()` unconditionally (no
  `#ifdef` gate) — so it very likely still constructs a working device.
- `AnimatedTreeManager` is not solely a software-path relic: it is
  unconditionally constructed in `IrrlichtRenderer::InitGraphics` and its
  wind-shader classification (`isTreeObject`) and `getWindController()` are
  used on the live GLES2 path (`renderer_zone_stream.cpp`,
  `renderer_frame.cpp`, `renderer_sim_bridge.cpp` — weather multiplier, tree
  shadows, diagnostics). Only its CPU-instancing branch
  (`beginInitialize`/`initializeNextBatch`/`assignBspRegions`, gated by a
  runtime `driver_->getDriverType() == EDT_BURNINGSVIDEO` check in
  `renderer_zone_load.cpp`'s `loadZoneStep11_Env()`) is unreachable in the
  GLES2-config tree — deleting the whole class would break the live wind
  path, i.e. exactly the "forces cascading signature changes" STOP condition.
- `RenderingBackend::OpenGL` (→ `EDT_OPENGL`) is a dead end specifically on
  x86 (the patched GLES2 Irrlicht disables `_IRR_COMPILE_WITH_OPENGL_`,
  `scripts/build-gles2-x86.sh`), but is the live, compiled driver for the
  still-buildable ARM `--no-gles2` configuration (`scripts/build-arm-noble.sh
  --no-gles2`, CMakeLists.txt's "Desktop GL mode via Lima" branch,
  unaffected by P30) — CLAUDE.md's own "Legacy platforms" note documents this
  build as present, if superseded, not removed.

Per this unit's own ambiguous-reachability rule (KEEP + flag, same
discipline as P30's `APPLE` branches), none of Phase 4's deletions were
made. Phase 4 remains open pending a decision (Will's, not an executor's) on
whether to also retire `--renderer software|opengl`, `--constrained
voodoo1|voodoo2|tnt`, and the ARM `--no-gles2` Lima build — the actual
precondition for the code deletions to be safe. See `src/client/graphics/
CLAUDE.md`'s Renderer Tiers table (updated by P31 with this finding) for the
per-path detail.

Phase 4 item 4 (docker/Dockerfile) and Phase 5 (doc sweep) were executed by
P31 independent of the above: `docker/Dockerfile` still had a live
in-repo/documented workflow (`docker/docker-compose.yml`, `docker/README.md`)
so it was updated (not retired) to bootstrap the same patched GLES2 Irrlicht
`scripts/build-gles2-x86.sh`/`docker/Dockerfile.arm-noble` use, verified by a
full local `docker build` through all three stages (builder/runtime/vnc) plus
running the resulting binary's `--help`.

### 2026-07-28 addendum — Phase 4 scope ruling (Will)

- `--renderer` is limited to `gles2` only (for now); the `software` and
  `opengl` options are removed, along with `RenderingBackend::Software`
  / `::OpenGL` and the EDT_BURNINGSVIDEO/EDT_OPENGL client paths.
- Constrained presets are RENDERER-INDEPENDENT by definition: they set
  memory budgets/features and are applied after renderer selection.
  voodoo1/voodoo2/tnt etc. stay, but any preset→renderer coupling is
  removed — every preset runs on GLES2.
- The ARM `--no-gles2` Lima desktop-GL build is retired.

### 2026-07-28 — Phase 4 executed (P32)

Per the scope ruling above, P32 (`refactor/P32_phase4_final.md`) executed
Phase 4 in full: `--renderer` is gles2-only (removed values fail CLI parsing
fast); `RenderingBackend::Software`/`::OpenGL` and every call site deleted
(`EDT_BURNINGSVIDEO`/`EDT_OPENGL` client branches, the `EDT_SOFTWARE` device-
creation retry fallback); constrained presets decoupled from renderer
selection (voodoo1/voodoo2/tnt keep their budgets/features, no longer set
`renderingBackend`); `AnimatedTreeManager` (the CPU-instancing wrapper)
deleted — its live GLES2 consumers (tree classification, wind config,
weather multiplier) now use `TreeWindController`/`TreeIdentifier` directly
(both were already separate TUs; `AnimatedTreeManager` was just a facade
over them plus dead CPU-instancing machinery); the ARM `--no-gles2` Lima
build retired (`scripts/build-arm-noble.sh` flag removed, CMakeLists.txt's
"Desktop GL mode via Lima" `ELSE()` branch removed). The desktop-GL
elimination (`refactor/desktop_gl_elimination_plan.md`) is now COMPLETE.
See `refactor/P32_phase4_final.md`'s Review for full verification evidence,
the extraction's consumer-by-consumer mapping, and one notable finding
preserved (not fixed): `TreeWindController::update()` — the only thing that
advances wind time/weather-multiplier transition — was already unreachable
on the live GLES2 path before this unit (gated on a CPU-instancing tree
count that only the deleted software path ever populated), so the wind
weather multiplier has always been frozen at 1.0 in practice; this unit
preserves that exact behavior rather than silently "fixing" it.

### 2026-07-30 — Lighting model: per-pixel STAYS (Will's decision, P41 characterization)

Will reviewed P41's evidence (Pi A/B at e5 + e205 on the canonical
befallen replay; x86 stills at the three Q03-blessed golden poses,
worst case f1 doorway = 89.9% of pixels differ with per-vertex banding
on large flat polys) and decided: **keep per-pixel adaptive lighting as
shipped.** The lightweight flip's saving is ~1.3ms GPU/frame absolute
(−15.1% total at realistic e5 load, −6.4% at e205 stress); the 30fps
KPI is already met with the current stack, so the flip's re-bless cost
buys headroom the project doesn't currently need at the price of a
visual change to personally-tuned Q03 territory. The per-tier-knob
option was not pursued. Lightweight mode remains flag-gated in tree
for future re-evaluation if the Pi tier ever needs the headroom;
evidence preserved in refactor/perf_truth_audit.md ("2026-07-30 — P41")
and benchmarks/audits/p41_lighting/. Q03's blessed visual state is
unchanged and remains the quality bar.

### 2026-08-02 — Automation is a first-class requirement; slash commands are THE command channel (Will's ruling)

**Standing policy: anything that stands in the way of fully automated
development and testing is a defect and gets fixed.** Not worked around,
not documented as a known limitation — fixed. This is now a standing
consideration when scoping any unit, and grounds for reopening past
"acceptable limitation" calls.

**The direction (Will, 2026-08-02):** "The slash command syntax (e.g.
/moveto, /attack, etc) works well for commands and that kind of flow
should be something that can be piped to stdin/stdout. Then, scripted
automation can use commands to trigger actions and I can also execute
the same commands in a real session."

So: **one slash-command vocabulary, reachable over stdin/stdout, and it
is the same path a live session uses.** Automation must not have a
parallel, weaker dialect — if a command works when typed in the chat
window, it must work piped to stdin, and vice versa. The value is that a
script and a human exercise identical code, so automated verification is
evidence about the real thing.

**What triggered it (T26, 2026-08-02):** T26 replaced the blocking
auto-loot sleeps but could NOT complete its live kill→loot verification,
because `CombatManager::Enable()` (`/combat on`) lives in a registry that
only live chat-window keyboard input can reach. The unit's most
important evidence leg was blocked by a tooling gap, not by the code
under test. Under this ruling that gap is the bug.

**Ground truth at the time of the ruling — FOUR disjoint command
vocabularies** (verified by orchestrator, fable `a22d634`):
1. `EverQuest::m_command_registry` — the real slash vocabulary
   (`/combat`, `/assist`, `/duel`, …, registered across
   `eq_commands*.cpp`). Reached ONLY via `EverQuest::ProcessChatInput()`
   (`EverQuest::DispatchSlashCommand`, `eq_chat.cpp`), fed from the
   chat-window intent (`ChatInputIntent` -> `ProcessChatInput`,
   `src/client/bridge/bridge_component_intents.cpp`). **Also wrapped entirely in
   `#ifdef EQT_HAS_GRAPHICS`** — so in a headless-compiled build, slash
   commands do not exist at all.
2. `action::CommandProcessor` — a second slash vocabulary, the only one
   `--script` dispatches to (`Application::DispatchSlashCommand`,
   `application_commands.cpp`).
3. Renderer debug registry (`/sort`, `/mergedraws`, `/screenshot`, …) —
   reached via the `GameEventType::RendererCommand` bridge queue.
   `--script` forwards its unrecognized commands here as a fallback.
4. `ConsoleInputHandler` (`ConsoleInputHandler::processCommand`,
   `console_input_handler.cpp`) — a bespoke
   NON-slash word vocabulary (`say`, `tell`, `quit`) on stdin, behind an
   interactive raw-mode/keyboard-mode toggle built for a human at a tty,
   not for a pipe.

`--script` therefore reaches 2 and 3 but not 1 — the exact hole T26 fell
in — and the one existing stdin path (4) speaks a different language
than every other path.

**Landing constraints carried forward** (each already cost someone a
debugging session, do not rediscover them):
- Renderer commands MUST stay on the `RendererCommand` bridge queue,
  drained on the render thread. Calling `processSlashCommand()` directly
  off the game thread runs GL without the EGL context current — P10 hit
  this and got deterministic-but-garbage screenshots rather than a
  crash, because Mesa does not fail loudly.
- The stdin reader must not assume a tty. Piped stdin has no raw mode,
  and `ConsoleInputHandler`'s mode toggle would consume the first line as
  a mode switch.
- Only one component may read stdin (`Application::processInput` in
  `application_commands.cpp` already documents a DRM-mode duplicate-read
  hazard).
- Automation needs to know when a command has been *applied*, not just
  accepted — stdout has to carry a result, or scripts go back to sleeping
  and guessing, which is the class of bug T26 just removed.

## 2026-08-25 — Two `state::GameState` instances: projection model is the contract (Will)

**Decision:** `EverQuest::m_game_state` is the WRITE model (packet handlers
mutate it directly). `Application::m_gameState` is a READ model for the
action/command layer, populated ONLY by `GameStateSyncBridge`'s event
projection. **Every new action-layer read of `m_state.<domain>()` must have a
corresponding projection case in `GameStateSyncBridge`, added in the same
unit, with a test that fails when the case is absent.** T78's analysis
(`refactor/T78_two_gamestate_analysis.md`) found five silently-broken command
families from missing projections; T81/T99/T103/T104 closed four this way.
T218 (player flags, heading) and T219 (doors — which also retires the third
door instance `EverQuest::m_door_state_manager`) close the rest.

**Follow-on, agreed in principle, NOT started:** converge on a single
instance (T78 Option A) once T218/T219 land, with the projection tests as the
regression net. Strategy to be discussed with Will before any refactor work.

## 2026-08-25 — ONE `state::GameState` (Option A complete: T220 → T221 → T227)

**Decision (Will, A1):** `EverQuest::m_game_state` is the only instance. The
action/command layer, modes, `InputActionBridge` and `ConsoleRenderer` all
borrow `m_eqClient->GetGameState()`. `GameStateSyncBridge` is a pure
forwarding decorator: `stagePendingEvents()` on the game thread,
`forwardStagedEvents()` on the render thread — it holds NO state.
**Rule that replaces the 2026-08-25 projection rule:** every state a
handler learns from the wire is written into `m_game_state` at the source,
before any bridge publish, and pinned by `tests/test_game_state_source_writes.cpp`
(run with and without a bridge). Off-thread writers are the remaining
hazard class: `CombatManager::Update()` on `m_update_thread` can reach
`SetTarget`/`ClearTarget` when `/combat on` (T227 STOP note, on the board).
**Closed T245 (2026-08-26): `m_update_thread` is deleted; `Update()` is
ticked from `UpdateMovement()` on the game thread and paces itself to
500 ms. EverQuest now runs NO thread of its own -- every `m_game_state`
writer is the game thread. No mutex was added.**

## 2026-08-26 — EQE1 mip chains are completed and the alpha companion is fixed; goldens re-blessed (Will)

**Decision:** (1) Fix the ETC1 **alpha companion** texture path (it carries
`GL_NEAREST_MIPMAP_NEAREST` over the same truncated chain that made the UI
atlas sample black in T275, and the driver never sets unit 1's filters).
(2) Fix the **builder** so every EQE1 entry ships a *complete* mip chain
(down to 1x1), rather than living with non-mipmap filters forever.
**Re-blessing the visual goldens is approved** as part of this — whatever
the alpha companion samples today is baked into them.

**Rationale:** T275 (`ad51cc3f`) proved an incomplete chain plus a mipmap
MIN_FILTER samples opaque black on GLES2, and that the 3D path only escapes
it because the driver rewrites unit 0's filters per material bind. Leaving
the alpha companion in that state is the same defect waiting for a driver
that does not. `verify_eqe1` has been reporting `mipLevels=7 (expected 9)`
for every entry of every pack all along.

**Consequence:** all EQE1 packs are regenerated; `data/eqe1_texture_index.json`
is rebuilt if entry offsets move; the goldens are re-blessed once, in the
same unit, with before/after evidence.

## 2026-08-26 — Will's rulings on the standing class-C list

- **Pi composite run: GO** (T289 = T287 Part 2). Measurement only; re-enabling
  composites on the orangepi remains a separate decision after the numbers.
- **Loot window: REQUIRED.** It existed and was ~80% working before the static-UI
  migration. The old `loot_window.h`/`loot_window.cpp` (deleted in U07c2
  `0f25cb57`) were ported to the static-panel architecture as `LootPanelState` /
  `src/client/graphics/ui/loot_panel.cpp` (T290) — see
  `refactor/completed/D06b_publish_loot_vendor.md` for the publish-path design
  and `include/client/graphics/ui/CLAUDE.md` for the shipped shape.
- **`/bug`: DO NOT WIRE.** The command shape question is closed as won't-do.
- **T180/T181 leftovers: KEEP** both (`EverQuest::GetClassName` public,
  `/raid looter add|remove`).
- **Upstream eqemu reports: DEFER to the absolute end of the project.** We are
  not tracking upstream, so some may already be fixed there. Do not file now.
- **`docs/plans` / `docs/future`: PARKED.** Not a source of work.
- **visual_gate: switch the metric to process CPU time (`getrusage`)** and
  re-bless `p95CpuMsBaseline`. The wall-clock `cpuDrawMs` is defeated by load
  from a sibling worktree build.
- **`protectedRegions_`: REMOVE** — if it was built for progressive terrain
  loading, it and the dead progressive paths go. Verify that is what it was for
  before deleting.
- **Memory budgets: derive them.** Extrapolate sensible per-category defaults
  from the assets actually on disk, a 1280x720 maximum resolution, and the
  Orange Pi One's real envelope (512 MB RAM, 32-bit ARM). Do not ask for numbers.

## 2026-08-26 — per-subsystem memory budgets, derived (T293)

Discharges the block T69 deliberately left open ("Proposing numbers is Will's
call, not this unit's; recording the absence is the deliverable") against
Will's ruling immediately above. **Settled — do not re-litigate the numbers;
re-derive them if the inputs change.** Plan and full working:
`refactor/T293_memory_budgets.md`. Evidence: `/mnt/projects/fable/review/T293/`.

### The Orange Pi One envelope (measured, not assumed)

| quantity | value | source |
|---|---|---|
| physical RAM | 512 MB | board |
| usable RAM after kernel + CMA | ~483 MB | T195 |
| OS/services baseline, client not running | ~151 MB (483 − 332 MemAvailable) | T289 rep1 pre-run meminfo |
| client peak RSS, four completed runs | 209–230 MB | T289 |
| MemAvailable floor, four completed runs | 55–66 MB | T289 |
| client VmPeak, four completed runs | 571–575 MB | T289 |
| 32-bit ARM user address space | ~3,072 MB | T195 |

**Client RSS ceiling = 483 − 151 (OS) − 48 (MemAvailable reserve, under the
54.9 MB minimum four completed runs actually survived) = 284 MB → adopt
256 MB**, rounded down: a round number, 11% above the worst measured peak,
and 28 MB inside the raw arithmetic so the rounding errs safe.

**The address space, not the RAM, is the binding ceiling for anything mmapped**
(T195: the eager EQE1 directory scan threw `std::bad_alloc` at ~2,900 MB mapped
with 260 MB of RAM still free). VmPeak budget: 1,024 MB — 1.8x measured, 3x
inside the hard limit.

### Headroom rule

    budget = ceil_8MiB( worst_measured_of_the_3_golden_zones
                        × asset_scale_factor_from_disk
                        × 1.25 slack )

`asset_scale_factor` is the ratio of the **largest on-disk asset of that kind
across all 310 zones** to the largest among the three zones the gate actually
measures — the "extrapolate from the assets on disk" half of the ruling:

| asset | largest on disk | largest golden | factor |
|---|---|---|---|
| `{zone}.s3d` | ponightmare 9.72 MB | qeynos2 4.16 MB | **2.34x** |
| `{zone}_obj.s3d` | dulak 10.60 MB | gfaydark 2.98 MB | **3.56x** |
| `{zone}_chr.s3d` | griegsend 10.45 MB | qeynos2 2.44 MB | **4.28x** |
| all three summed | powar 21.20 MB | qeynos2 8.76 MB | 2.42x |

Two rows use a different, stated basis because they have no on-disk analogue
(VBO/EBO, character models) — see the table below.

### The budgets

Measured column = worst of six x86 runs (3 golden zones × {golden-image S3D
path, T80 memory-coverage EQE1 path}), at fable `43e3430e`.

| category | measured worst | budget | ratio | basis |
|---|---|---|---|---|
| framebuffer | 3.66 MiB @800x600; **7.03 MiB @1280x720** | **10 MiB** (unchanged) | 1.42x | pure arithmetic at the stated max resolution — see the verdict below |
| texture cache | 40.27 MiB (qeynos2/S3D) | **64 MiB** (unchanged) | 1.59x | pre-existing; a hard cache ceiling, enforced by eviction |
| GPU textures | 30.27 MiB (qeynos2/S3D) | **64 MiB** | 2.11x | 2x measured, which lands exactly on the texture-cache ceiling — the cache is its dominant feeder |
| zone geometry | 4.76 MiB (gfaydark) | **16 MiB** | 3.36x | 4.76 × 2.34 × 1.25 = 13.95 → 16 |
| placeable objects | 5.36 MiB (gfaydark) | **24 MiB** | 4.48x | 5.36 × 3.56 × 1.25 = 23.87 → 24 |
| S3D zone source | 7.52 MiB (gfaydark) | **24 MiB** | 3.19x | 7.52 × 2.34 × 1.25 = 22.00 → 24 |
| VBO/EBO | 10.22 MiB (gfaydark) | **40 MiB** | 3.91x | **derived by identity**: measured to be the GPU mirror of zone geometry + placeable objects (10.22 vs 4.76 + 5.36 = 10.12), so 16 + 24 |
| character models | 24.54 MiB (qeynos2) | **40 MiB** | 1.63x | **derived from a measured race sweep**, not a flat multiple: 23.68 MiB fixed with one race resident + 189 KB per additional race (n=1..12) → 23.68 + 40 races × 0.18 + 8.0 for the largest `_chr` on disk = 38.5 → 40. 40 resident races is ~3x the 14 T289 measured live in befallen |
| **EQE1 mapped (ADDRESS SPACE)** | 114.39 MiB (qeynos2) | **256 MiB** | 2.24x | worst possible zone set from disk: powar 41.24 (zone packs) + 132.57 (the 33 globals in `config/eqe1_archives.json`) = 173.81 × 1.25 for composite late-opens = 217 → 256. **8.3% of the address-space ceiling** |
| tracked total | 62.24 MiB (gfaydark/coverage) | **128 MiB** (unchanged) | 2.06x | `totalMemoryBudgetBytes`, shipped since long before T69 with nothing asserting against it. Now asserted **in CI only** — not runtime enforcement, not a cross-asset coordinator |

**The assets on disk are 4,545 MB across 1,506 EQE1 packs** (4.8 GB
for the whole directory including the `.bmp`/`.json` sidecars). The budget is not
"fit the assets in RAM"; it is "bound the resident/mapped working set".

**The per-category budgets sum to 282 MiB, more than the 256 MB RSS ceiling.
That is deliberate.** They are per-subsystem *ceilings*, not reservations: no
single zone is the worst case for every category at once (qeynos2 maxes
textures and character models, gfaydark maxes geometry, objects and VBO/EBO),
and the two largest — texture cache and GPU textures — bound substantially the
same textures. The simultaneous total is bounded separately, and measured at
62.24 MiB, 49% of the 128 MiB tracked-total ceiling.

### The 1280x720 framebuffer verdict

    16-bit colour (as shipped): (2×2 + 4) =  8 B/px × 921,600 =  7,372,800 B =  7.03 MiB   FITS (67% of 10 MiB)
    32-bit colour:              (2×4 + 4) = 12 B/px × 921,600 = 11,059,200 B = 10.55 MiB   DOES NOT FIT (over by 573,440 B)

**The shipped 10 MiB budget holds the stated maximum resolution — but only
because this preset is 16-bit.** At 32 bits `calculateMaxResolution()` yields
1072x808 and `clampResolution()` would silently narrow a 1280x720 request to
1072x720. The budget is **not** raised to cover the 32-bit case: 16-bit is the
shipped configuration and Mali 400 has no reason to want 32. Both halves are
pinned by tests so a future `colorDepthBits` edit fails loudly instead of
silently clamping the display.

### What is deliberately still unbudgeted

- **Untracked RSS** (RSS minus every tallied category): 10–24% tracked on the
  golden-image path, i.e. **76–90% of RSS is untracked**. T69 Step 4 ruled it
  reported-not-gated and that stands. The x86 figure is inflated by llvmpipe
  and does **not** transfer to the Pi, and T289 did not dump `/pmem`, so the
  Pi's tracked/untracked split is **unmeasured**. That is the one place the
  envelope arithmetic cannot be closed end-to-end today; a `/pmem` dump on a
  live Pi zone-in is the cheap next measurement.
- **Categories no benchmark constructs**: entity per-instance game state,
  equipment models, HCMap/collision, audio, network, door/spell DB. Unchanged
  from T69/T80 — a budget on a number nothing measures would be decoration.
- **Non-OrangePi presets.** Voodoo1/2/TNT keep 0 = "no budget configured"; the
  gate skips and says so rather than asserting `usage <= 0`.

## 2026-08-27 — Composite entities stay OFF on the orangepi (Will, after T289's measurement)

**Decision:** `enableCompositeEntities` remains **false** for the `orangepi`
preset. Unchanged from decision #3 — but the *reason* has changed and the old
one must not be cited again.

**What changed:** T284 deleted the eager scan that mapped the whole composite
directory (1506 packs / 4.68 GB = **1.49x the entire 32-bit ARM user address
space**), which is what actually threw `std::bad_alloc` in T195. T289 measured
the result on the Pi: VmPeak 3,128 MB -> 575 MB (18.4% of the ceiling), RSS
+13.4 MB with composites ON, four clean runs, no allocation failure.

**So the address-space argument is dead, and composites are OFF by choice, not
by necessity.** The remaining cost is resident: +13.4 MB on a 483 MB board for
one zone's seven composite races, and RSS now scales with the number of
distinct resident composite races — which does not survive multiplication in a
race-dense zone. Will chose not to spend that budget.

**Do not "fix" this by re-enabling it, and do not repeat the T195 rationale.**
J21 (2026-09-02) struck the superseded wording from
`src/client/graphics/CLAUDE.md`, `renderer_zone_load.cpp` and
`tests/test_eqe1_composite_open.cpp`; those three now point here.

## 2026-08-27 (later) — REVERSED: first person is the player model + eye camera + a clamped down-pitch (Will)

**Will explicitly approved reversing the viewmodel decision below.** T299
measured the mechanism in BOTH the classic client and Titanium: **first person
is the ORDINARY player model, camera at the eyes, with a near clip plane large
enough to swallow the body.** Nothing of the player renders at rest at ANY
pitch — including aimed straight down at feet 6.75 units below the eye — yet
the SAME leg reappears the instant a kick extends it, and the main-hand blade
appears on every swing, always cut cleanly with the hand and elbow never drawn.
**Geometry that reappears was CLIPPED, not omitted.** The weapon also enters
22.9 degrees off-axis and rotates about an off-screen pivot, entering from
different edges in different swings: world-space animation, not a screen-locked
viewmodel.

The entry below rested on T76's "no camera lever can fix it", which was
measured against a camera now known to be wrong on two counts (a 12-degree
down-pitch and a backward standoff, both introduced for in-world arms) and
against a near plane of 1.0 that was never the lever under discussion. **The
rejected alternative it names — "do NOT lower the global near clip plane" — is
not what T300 does: the near plane is RAISED, in first person only, and the
world's 1.0 plane is restored on leaving first person.**

**The near plane is NOT the lever — T300 Leg A refuted that too.** The official
client clips nothing (geometry draws at point-blank range), and a size sweep at
5x showed zero player geometry while ground 34 units below still drew, which
kills both a fixed and a size-derived plane. **The body is hidden by a clamped
down-pitch**: it hangs directly below the eye, and pitch is clamped so the
frustum's lower edge never passes the nadir, while a swing rotates the arm and
weapon up across a frame edge into view.

Implemented by T300 Leg B: the viewmodel pass, T74's arm-bone geometry filter
and T76's standoff are DELETED; the camera is at the player's XY with pitch
3.65 degrees down and hFOV 62.8 degrees (both T298's measured values); first
person uses the world's near plane like every other mode; down-pitch is clamped
at 90 - vFOV/2, which is scale- and race-invariant and needs no size term. See
`refactor/T300_leg_b_implementation.md` and the (now archived) FIRST PERSON
section of `refactor/completed/deferred_archive_20260903.md`. **Will judges
the live result (Leg C); no headless test can — nothing in the suite
rasterizes.**

## 2026-08-27 — First person gets a real viewmodel pass (Will) — SUPERSEDED, SEE ABOVE

**Decision:** Implement a **viewmodel pass** for first person, as T76
recommended. Do NOT lower the global near clip plane — that was the rejected
alternative (it buys the arms at the cost of depth precision across the whole
scene).

**Why the current approach cannot be rescued by camera tuning:** T76 measured
that the arm-filtered player geometry **straddles the near plane in every
pose** — idle Z=[-1.06, 1.11], 1H-slash Z=[-1.02, 2.18], hand-to-hand
Z=[-0.04, 2.15] in camera space. Negative Z is *behind the camera*. Pullbacks
of 0/0.2/0.3/0.5/1.0 were all tested and none clears it, because the mesh's
own depth span (~2-3 units) exceeds what any camera position can resolve.
**The geometry is in the wrong place, not seen through the wrong lens.**

**So the viewmodel is a separate pass with its own transform**, in the
classic sense: the arm/weapon geometry is posed in VIEW space at a designed
offset in front of the camera, rendered after the world with the depth buffer
cleared, using its own projection. It is not the in-world player model viewed
from inside — that is what has failed four times (T71, T74, T75, T76).

**Standing facts this must respect:** the T74 arm index filter (triangles
whose vertices are all bound to arm bones) is the geometry source and works
headlessly; animation already reaches the first-person path (locomotion and
combat both); the bone-attached weapon nodes are the held items. **Will judges
the visual result — no unit may self-declare it acceptable** (protocol rule,
and T76's README calling an unrecognizable mass "recognizable" is exactly the
failure this guards against).

## 2026-08-27 — CRC validation bypass and UCS: Will's rulings

- **CRC validation bypass: NOT a work item.** `EverQuest::ConnectToZone`
  (`eq.cpp`) sets
  `zone_opts.skip_crc_validation = true` with a `// TODO: Remove after CRC bug
  is fixed`. Will has ruled this out of scope; leave it and do not re-raise it.
- **UCS/chat server: DEFERRED, CONDITIONALLY.** `ConnectToUCS(...)` is
  commented out in the live `WorldProcessSetChatServer` handler
  (`eq_login_world.cpp`, `WorldProcessSetChatServer`; the implementation
  itself now lives in `eq_ucs.cpp`, J02). Will: *"ignore the UCS/chat server for now **as
  long as zone-wide and cross-zone communication is working**."*
  **SOURCE ANALYSIS (T297 `48800a8f`) SAYS THE DEFERRAL IS SAFE, LIVE PROOF
  STILL OWED.** From `/mnt/projects/akk-stack/code`: the ONLY branch reaching
  `UCSConnection::Instance()->SendMessage` is `chan_num == ChatChannel_UCSRelay`
  (20) at `/mnt/projects/akk-stack/code/world/zoneserver.cpp:423` — an Underfoot-and-later relay a Titanium
  client never sends. **No channel this client can speak depends on UCS.**
  Tell, guild, ooc and auction are dispatched by WORLD itself; group has its
  own `ServerOP_OOZGroupMessage` path; say/shout/emote are zone-local BY
  DESIGN (that is correct EverQuest behaviour, not a failure). UCS on a
  Titanium server covers only player-created channels (`/join`, `/1`) and
  mail — neither implemented. So the commented-out `ConnectToUCS(...)` should
  cost nothing.
  **The condition is therefore predicted to hold, but not yet measured** — every two-client chat probe in
  the record (T222 `/reply`, T243 `/emote`, T239 macro) put both clients in
  the SAME zone. T291 is verifying it (two clients in DIFFERENT zones, across
  say/shout/ooc/auction/tell/guild/group). **If a channel turns out to be
  broken cross-zone, the deferral does not cover it and it comes back as a
  defect** — the ruling was conditional, not blanket.

## 2026-09-03 — Will's rulings on the three open board calls

- **B006, `InitGraphics` narrowing:** the two-method interface is approved.
  `EverQuest::InitGraphics` (via `BridgeComponent::InitGraphics`) takes an
  abstract type with exactly `initLoadingScreen(const RendererConfig&)` and
  `initDeferredGraphics()`; `IrrlichtRenderer` implements it. Game logic
  then names no renderer class. Unit K16, after L05.
- **B036, UI sounds:** wanted — bag open/close, coin pickup by denomination,
  possibly tradeskill combine. Measure the official client's sounds first
  (match-the-oracle), then wire. A feature unit, not a structural one.
- **B040, the placeables memory gate:** stays open, low priority.

## 2026-09-05 - three rulings from Will

- **B058 `/skills`:** rename `/newskills` to `/skills` and remove `/newskills` <!-- RETIRED-OK: the ruling that retired the name -->
  outright (no alias). One body survives: the fuller one (closes the spellbook
  and inventory popups and echoes to chat). The bare `skills` stdin keyword
  keeps toggling the same window. Unit T363.
- **B060 racial vision table:** Will's expectation is Troll = Ultravision,
  Ogre = Infravision, but the table is settled by the OFFICIAL CLIENT, not by
  ruling — T361 captures the no-buff night floor per race under Wine.
- **B020 per-category memory budgets:** Will asked for a guess from the Orange
  Pi as the normal platform. No guess is needed: T293 (2026-08-26) already
  derived and configured all eight budgets in the orangepi preset from the
  on-disk assets, 1280x720 and the Pi envelope. The board row was STALE and is
  struck; the one open figure (untracked RSS) is B021.

## 2026-09-05 - racial vision table settled by the official client (T361)

Measured on the Titanium client under Wine at forced night with a cast
Ultravision positive control per sweep: **Dark Elf alone is Ultravision**;
Wood Elf, High Elf, Half Elf, Dwarf, **Troll, Ogre**, Halfling, Gnome, Iksar and
Vah Shir are Infravision; Human, Barbarian, Erudite are Normal. Will's
expectation (Troll = Ultravision) was refuted by the measurement, as he asked.
Froglok measures a FOURTH level between Infravision and Ultravision that
`VisionType` could not express. **T428 settled it and closed B060**: `VisionType`
now has four enumerators and `FroglokVision` is APPENDED as 3, not slotted into
brightness order, because the enumerator values are wire values
(`BuffManager::computeVisionType()` returns a literal 2 for Ultravision and
`SimulationInput::visionType` carries the enum as a raw `uint8_t`); brightness
order is `visionRank()` instead, so an Ultravision buff on a Froglok is not a
silent no-op. Measured x2.6803 / x1.9478 / x2.1753 per channel. The sentence
this replaces said "left on Normal, needs Will's call" and was stale (T444).
Ultravision on the official client is a ~1.55x whole-frame ambient raise at
night, sky unchanged; Infravision ~1.25x and red. Fable now imposes a
per-channel floor under the global ambient (`vision_ambient.cpp`), gains solved
against the oracle.

**Two corrections since (do not read the sentence this replaces).** (a) B080's
"fable's night is ~35x darker than the oracle's on near geometry" is
**REFUTED** — T367 measured both clients at a lamp-free spot at forced 22:00
and fable reads 0.005502 linear grey against the oracle's 0.004245, i.e. fable
is **1.30x brighter**. The 35x came from cropping two frames that did not show
the same geometry. What is real is B080's *lead*: the missing night light is a
lamp term, and the open residual is the baked per-vertex zone colours fable
skips (**B110**, Will's call). (b) T377 re-measured the vision ratios
(oracle 1.5612 / 1.2555 whole-frame) and T383 re-solved the gains **per
channel** rather than as one scalar: Ultravision gain 0.1601 x tint
(0.695, 0.698, 1.607), Infravision gain 0.1248 x tint (0.823, 0.759, 1.419),
and the zone-light Infravision shift dropped 0.3 -> 0.0508. All six channel
residuals land inside 0.11%. That fit is a model extrapolation from T361's
exponent, not a closed loop — the live re-measure is still owed (B171).
**(c) T428 re-solved all of it live and those T383 numbers are SUPERSEDED — do
not quote them** (T444 re-measured the tree). T405's floors had been solved
against the old night base and against a half-sky crop, so T428 moved the base
first (night ambient `(0.08, 0.08, 0.15)` -> `(0.26244, 0.31147, 0.24003)`, the
whole day/night curve lifted into `include/client/graphics/vision_ambient.h`)
and then re-solved the floors on top of it: the gains in the tree are
`kUltravisionAmbientGain` **0.50650**, `kFroglokVisionAmbientGain` **0.49566**
and `kInfravisionAmbientGain` **0.41469**, each with its own tint triple beside
it, not T383's 0.1601/0.1248. T428's own Ultravision oracle ratios are
x2.7995 / x1.9517 / x2.4946, where T405 had read x2.2531 / x1.4399 / x1.7687.
Read the header's constants, never this paragraph's history.

## 2026-09-06 - Will's rulings on the open class-C items

- **B240 BLESSED**: the visual gate's `eqe1MappedBytes` memory baselines move
  to what the shared pack measures once T406's alpha payloads have landed
  (T398 coins, T400 particle atlas, T406 the other textures). Applied by the
  orchestrator at the T406 merge with the final numbers; no pixel golden moved
  in any of those units.
- **B110 APPROVED as its own unit**: load the zone's baked per-vertex colours
  and match the zone lighting equation to the oracle; the pixel goldens WILL
  move and a re-bless with the contact sheet is approved for that unit only.
- **B219**: shrink fable's paperdoll to the official window's proportion
  (`IW_CharacterView` 85x168 in 428x460); the coin column stays where the
  oracle puts it.
- **B060**: add a FOURTH vision level for Froglok (its measured night floor
  sits between Infravision and Ultravision); its floor is solved by live A/B
  like the other two.
- **B138 / B139**: Will wants the live-server scenario lane; build it NEW
  (both surveys' recommendation), plugging into the automated mode's unreached
  callback. **Built by T410** (`src/client/scenario/`, `--scenario`,
  `scripts/scenario.sh`) -- see CLAUDE.md's "The scenario lane" for the
  exit-code taxonomy, which is now the contract every live verification
  reports through.
- **B142**: `/bug` stays on the player-report channel; no structured OP_Bug body.
- B022 is closed by T389's oracle answer; B162's reader fix is reclassified B.

## 2026-09-06 - spell sprites and weather darkening (Will)

- **B300**: draw the REAL official sprite files (`SpellEffects/spel*.tga`) —
  "I want willeq's effects to be as close to the original as possible." The
  particle atlas grows to carry them (a texture-loading atlas, not the 16-tile
  procedural sheet); approximation by tile is retired once the files draw.
- **B231**: capture the official client's ambient change under forced rain at
  the same spot and mimic the curve; fable's `maxDarkening = 0.6` is replaced
  by the measured value. Reclassified B, lock unit.

## 2026-09-07 - Will's directions on the remaining features

- **Spell effect measurement**: use TWO characters duelling each other; they
  can hold a set distance for the tests that need it (projectiles B227, the
  observer pairs). The rig for every future spell capture.
- **UI persistence (B275)**: persist per character whatever is user-changeable
  — measured: the 15 window open/closed states, the camera mode; keybinds are
  global (`config/hotkeys.json`). Window positions are static by design (XML-
  derived), nothing to persist there.
  **Implemented T432.** The census of user-toggleable windows in the tree came
  to **seven**, not fifteen: `[InventoryWindow]`, `[SpellBookWnd]`,
  `[SkillsWindow]`, `[GroupWindow]`, `[PetInfoWindow]`, `[BuffWindow]`,
  `[CombatAbilityWnd]`, plus `[Camera] Mode=`. The remaining panel `isOpen`
  flags are opened by the server or by clicking a container (loot, tradeskill,
  item display), and `RA::ToggleOptions` reached an empty arm — fable had no
  options window. The file is `<cache dir>/ui/UI_<Char>_<server>.ini`, the
  official client's own filename, section names and `Show=0/1` key.
  **T434 built that window** (`EQUI_OptionsWindow.xml`, 400x452, seven tabs)
  and added an eighth section, `[OptionsWindow] Show=`, plus an `[Options]`
  section carrying the three controls fable can wire — `Music`,
  `SoundVolume` and `ClipPlane`, the oracle's own key names on the oracle's
  own 0..20 slider scale. The key moved from a bare `O` to **Alt+O**, which is
  what the official client's Options > Keys > UI page shows. Everything on
  that window with no backing setting in fable is drawn greyed and is not
  hit-testable (boards B430–B434).
- **Item icons (B376) are critical** — the official `dragitem*.tga` sheets are
  the source.
- **Era rule**: the target is the original game from launch through Kunark and
  Velious. Tradeskill favourites and recipe search (B377) did not exist then:
  LOW priority; do not build post-Velious features.
- **First-person eye placement (B018)**: no judgement needed — capture the
  official client's first-person frame at the same spot and match it, like
  every other render parameter. *(Carried out by T431, 2026-09-07: skull
  fraction 0.65 → 0.45, Human male eye now 6.02 above its own feet against the
  oracle's frames. The same sweep on a second race says one global fraction
  cannot fit every race — board B470.)*

## 2026-09-07 - doors, lifts, elevators, switches and traps (Will)

Fable has never implemented lifts, elevators, click-activated switches
that drive other objects, or triggered traps (blades, saws, spikes that
damage a player in range). Greater Faydark's lifts to Kelethin are the
first test. The server sends most of these as "doors" (some possibly as
placeables); their name identifies their function. Requirements: animate
them as the official client does, treat lifts/elevators as walkable
moving surfaces, and apply trap behaviour. **The official client's
handling of doors and objects is the baseline to emulate.** Era rule
applies (launch → Velious).

## 2026-09-13 - the camera set (Will; recorded 2026-09-14 by T486)

Ruled for T461 (`refactor/T461_camera_modes_orbit_f9.md`, "Will's ruling"),
applied by T461/T464/T471/T474:

- **Match the official client's camera set** (First Person, Overhead, Chase,
  User Defined 1, User Defined 2, Tether -- names and order from
  `eqstr_us.txt` 9500-9505). **No Free mode**: the official client has no
  free-fly camera, and fable's (whose WASD fly routine had no caller) is
  deleted, not kept as an extra.
- **F9 cycles the modes in the official order and REPLACES Ctrl+F5** (no
  alias). The other camera keys follow the official Options > Keys > Camera
  page as T466 measured them (T471); F10/F11/L/Alt+C are still Will's call
  (B582).
- **Include the official orbit camera**: the user cameras orbit the player and
  zoom; the wheel and the zoom keys edit the active rig's distance.
- **`/camera` is the exact-placement command**: azimuth, pitch, distance and
  look-at height set absolutely, nudged by an exact step, and read back
  (`/camera get`). Its purpose is oracle work: put the official client in one
  of its fixed cameras, measure it, and set fable's to match.
- Every mode's geometry is MEASURED against the official client, never
  argued (`match-the-oracle-dont-infer-it`): Overhead and FOV by T466, Chase
  by T466/T474 (still open on the scoring crop, B721), the login camera is
  First Person (T471). User 1/2 and Tether hold a world azimuth (B580).

