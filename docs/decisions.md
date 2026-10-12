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

- ~~**Untracked RSS** (RSS minus every tallied category): 10–24% tracked on the
  golden-image path, i.e. **76–90% of RSS is untracked**. T69 Step 4 ruled it
  reported-not-gated and that stands.~~ **SUPERSEDED by the 2026-09-14 entry
  below (Will's 100%-accounting ruling, T491): untracked anonymous memory is
  measured at the allocator and GATED. The paragraph below describes only the
  T69/T293 per-subsystem ESTIMATES, which remain reported-not-gated.** The
  original text, for the estimate gap only: The x86 figure is inflated by llvmpipe
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
  page as T466 measured them (T471). F10/F11/L/Alt+C are Will's call
  (B582) — **applied 2026-10-10 (B582)**: F10 = ToggleAllUI, F11 and L unbound
  (netstat and Leadership window), the four volume keys removed.
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

## 2026-09-14 — 100% memory accounting, at the allocator, gated (Will; T491)

**Will's ruling, verbatim:** "the 32 MB figure and the need to track memory
usage meticulously is the reason I went down the ConstrainedCache road. I want
100% accounting of memory use and I want it controlled. We have extensive
perf/profiling and should be able to track it at all game lifecycle points."

**Decision.** Untracked anonymous memory is a **defect to drive to zero**, not
a number to report. This **supersedes** the "Untracked RSS ... T69 Step 4 ruled
it reported-not-gated and that stands" line in *What is deliberately still
unbudgeted* above, and the T69 sentence in `scripts/ci/visual_gate.py` that
called RSS "reported, not asserted". ~~Untracked RSS is reported, not gated.~~
It is now measured at the allocator and gated.

**What replaced the estimates.** `include/client/core/memory_ledger.h` /
`src/client/core/memory_ledger.cpp` (`eqt::memory`, `-DEQT_MEMORY_LEDGER=ON` by
default) replaces the allocator entry points and charges every block's whole
glibc chunk to the calling thread's `MemoryScope` category — eighteen
lifecycle/owner categories, not per-subsystem self-reports. The T69 `/pmem`
estimates stay for continuity and are explicitly labelled estimates.

**RssAnon is COVERED by four buckets, not partitioned, and that is measured,
not a hedge:** `mallinfo2`'s `arena` is what glibc has taken from the OS and its
free lists may hold pages that are **not resident**, so the heap buckets can sum
past the resident total. The buckets are

| bucket | definition | exact? | can attribution work shrink it? |
|---|---|---|---|
| attributed | sum of the eighteen categories | yes | it IS the attribution |
| `heap_uncharged` | `mallocInUse - ledgerTotal` — glibc's own internal allocations, which an in-executable interposer never sees | yes | no (would need `LD_PRELOAD` or a patched libc) |
| `malloc_slack` | `arena - inUse` — free lists and fragmentation | upper bound | no: no caller owns those bytes. It is a glibc tuning question (`M_ARENA_MAX`, `M_TRIM_THRESHOLD`) |
| `unaccounted` | `RssAnon - arena` — thread stacks, direct `mmap`, llvmpipe's JIT code pages | yes | no |

So the gate asserts the **unaccounted** share (`LEDGER_UNATTRIBUTED_MAX_PCT`),
the **slack** share separately and much more loosely (`LEDGER_SLACK_MAX_PCT`),
and each category against its `memoryLedgerBudgets` preset entry. Gating the
raw `RssAnon - ledger` gap, as T491's plan first framed it, would gate glibc's
free-list state, which no code change in this repo can move.

**Deviation from the T491 plan, flagged:** the plan said "assert unattributed ≤
a stated bound ... (allocator overhead/fragmentation reported separately via
`mallinfo2`)". Reported-separately and asserted-inside are incompatible, and the
measurement decided it: in a 450 kB test process the arena was 544 kB with 97 kB
in use, so `RssAnon - ledger` was 81% while attribution was essentially
complete. The bound is therefore taken over the residual after the measured
slack is removed. The name `unattributedBytes` is kept in the schema.

**Cost, measured:** ~45 ns per malloc/free pair on top of glibc's ~18 ns on
x86-64 (`MemoryLedger.HookOverheadPerAllocationIsSmall`, best of five ×400k
iterations). Under the plan's 2%-of-frame-time STOP for anything a frame does
tens of allocations of; the flag exists to compile it out regardless.

## 2026-09-17 — the RSS target is 32–64 MB; 256 MB is an interim ceiling (Will)

**Will's standing decision:** "the goal is willeq's RSS footprint between 32 and
64 MB. The 256 MB ceiling is now an interim bound, not the target."

**Decision.** The T293 envelope's **256 MB stays the FAILING bound** in CI
(`scripts/ci/visual_gate.py` `RSS_CEILING_BYTES`) and the **64 MB target is
printed, never asserted** (`RSS_TARGET_BYTES`) — every zone is over it today, so
gating it would make the gate uniformly red and say nothing the printed gap does
not. Every memory-ledger report at every lifecycle point carries both lines
(`target=`/`over_target=`/`ceiling=`/`over_ceiling=` on the `[memledger]` line,
`rssTargetBytes`/`overTargetBytes` in the JSON) plus the gap to 64 MB broken
down by bucket, largest first (`eqt::memory::gapToTarget`). The whole point is
that the next unit can be planned off the ledger's own output rather than a new
measurement: the buckets named at the top of that list, with their bytes, are
the shrink work.

Reference: the original client ran these assets in 32 MB total (Will's T490
ruling, 2026-09-14), so 32–64 MB is a restoration target, not an aspiration.

## 2026-09-17 - user-facing slot numbers are `inventory.slot_id` (T494, board B796)

**Class C, taken by the orchestrator while Will is away, and REVERSIBLE.**

Every slot number a user types or reads at fable's command boundary --
`/equip`, `/read`, `/scribe`, `/sell`, `/combine`, `/equip list`, and every
refusal message -- is the SERVER's (RoF2) numbering, the one stored in
`inventory.slot_id`: 0-20 worn, **21 is the power source and this client
cannot reach it**, 22 ammo, 23-30 general 1-8, 33 cursor, `4010 + 200*bag +
idx` inside a general bag, bank/shared-bank/trade/world unchanged.

Why that side of the fence. The only people who type a slot number are
developers and rigs, and both read them out of the database and the server
log, which print RoF2 slots. The official client never shows a slot number at
all, so no player convention is broken either way. Before T494 the boundary
used fable's own (Titanium) model numbering, which nothing else a user can
see prints, and `/equip 21 13` silently addressed DB slot 22 (board B796).

What did NOT change, and must not: fable's inventory model, `MoveItemIntent`,
`SendMoveItem` and the whole wire stay TITANIUM-numbered, because that is what
the server serialises to a Titanium client. The conversion exists at exactly
two seams, both in `include/client/inventory/inventory_constants.h`:
`parseUserSlot()` on the way in and `describeSlot()` on the way out. If a
third one ever appears, one of them is wrong.

**If Will prefers Titanium numbering at the user boundary**, the change is
small and local: delete the conversion from `parseUserSlot()` (keep its
addressability check), make `describeSlot()` print `slotId` again, and flip
the typed arguments in `tests/test_eq_commands_equip_*.cpp`,
`tests/test_inventory_slot_convention.cpp`, `tests/test_command_processor_world.cpp`,
`tests/test_read_book.cpp`, `tests/test_scribe_scroll.cpp`,
`tests/test_tradeskill_combine.cpp` and the llmbench timelines back. The
model-side expectations do not move either way.

## 2026-09-17 (addendum) — the PI's shrink queue is the planning list, not the x86 one (T496)

**Decision.** From T496 on, a memory-shrink unit is planned off
`review/T496/pi/table_categories.tsv` — the Orange Pi's own ledger output —
and **not** off the x86/llvmpipe queue T491 printed. The two disagree on which
buckets matter, by more than an order of magnitude, in both directions:

| bucket | x86 (T491, llvmpipe) | Orange Pi (T496, `ready`) |
|---|---:|---:|
| `textures_driver` | 30.50 MiB | 0.56–0.67 MiB |
| `irrlicht_scene` | 25.4–27.9 MiB | 1.11–1.45 MiB |
| `entity_models` | 64.56 MiB | 18.6–22.9 MiB |
| `startup` | 40.5–49.7 MiB | 26.82 MiB |
| **`audio`** | **not in the top ten** | **47.57 MiB — the largest bucket on the machine** |

The x86 queue's top three were Mesa's state, and the Pi's #1 was invisible on
x86 because llvmpipe's own allocations dwarfed it. This is B812's answer and
the reason it is struck.

**Corollary, and the reason the rule is written down:** the head of the Pi
queue turned out to be a LIFETIME defect, not a size one — two complete copies
of `synthus2.sf2`, one of them unreachable (B817). A queue taken on the wrong
machine would not have pointed at it.

The measurement is reproducible without a new tool:
`review/T496/scripts/pi_sweep.sh` + `pi_zone_run.sh` + `pi_table.py`. The one
thing the Pi rig needs that the x86 one does not is **`--log-module=MAIN:INFO`**:
`[memledger]`/`[memmap]` are `LOG_INFO` on `MOD_MAIN`, the Pi runs at the
default quiet level, and T490's Pi logs contain zero `[INFO]` lines — the
ledger was silent there and nobody had noticed.

## 2026-09-17 — memory: 100 % accounting is PRIMARY; x86 development; shared blocks (Will)

Three rulings from Will, recorded verbatim in substance by the orchestrator (T501 found the
second and third relayed into CLAUDE.md without a citation here):

1. **"For memory, 'inside target' is a secondary goal. The primary goal is 100 % accounting for
   all use. This is non-negotiable and must be completed."** Every resident byte — anonymous and
   file-backed, heap and non-heap, driver mappings, stacks, library data — is attributed to a named
   owner at every lifecycle point, on BOTH platforms. `unaccounted`, `unattributed` and
   `malloc_slack` are defects to be explained byte-for-byte, not residues.
2. **"If perf is better, the memory work can be done on x86. There's no requirement that all
   testing must be done on the Pi. However, having final memory usage numbers for both x86 and
   arm is a requirement."** Instrument, red-test and fix on x86; the Pi is the confirming run and
   the source of the ARM numbers; every memory unit's Review carries BOTH tables. Caveat measured
   by T496: driver-coupled buckets differ 1.5–50× between llvmpipe and Mali and in direction, so
   for those the accounting closes on both, never assumed from x86.
3. **Shared resources (B843): "in the case of shared resources occupying X amount of memory, just
   report X: category1, category2, … categoryN."** A shared block is reported ONCE with every
   claimant listed; it is never added into each claimant's total (that double-count is what made
   the Pi `[modelmem]` tally read 125 %). The ledger/tally sums to the resident total exactly once.

## 2026-09-17 (addendum) — 100% accounting means the LEDGER's own overhead has a name (T500)

**Will's standing decision, restated at T500:** *"for memory, 100% ACCOUNTING
of all use is the PRIMARY goal and non-negotiable; the 32–64 MB target is
secondary."* Three rules follow, and T500 is the unit that had to learn each
one.

**1. A subsystem's own byte tally reconciles against `liveRequestedBytes`, not
against `liveBytes`.** The ledger charges the whole glibc chunk: the caller's
bytes, plus glibc's size word, plus its 16-byte rounding, plus the ledger's
OWN 16-byte `LedgerHeader`. That is real resident memory and `liveBytes` stays
the number checked against RssAnon — but no logical tally can ever reach it.
MEASURED on x86 over twelve race models: **5,786,646 B across 185,396 live
blocks, 31.2 B each, 11 % of `entity_models`.** `CategoryStats` now carries
`liveRequestedBytes`, `liveBlocks` and `blockOverheadBytes`, so the gap is a
named bucket instead of a residual. It cost no header bytes: the cached chunk
size was replaced by the requested size and the chunk is recomputed from
glibc's own chunk word (a 24-byte header would break `max_align_t`, and a
32-byte one would double the overhead being measured).

**2. A truncated census is not accounting.** `recordMappings()` at `startup`
and `ready` prints every owner row, not the top 16. `RssShmem` is read and on
the `[memledger]` line beside `RssFile` and `VmRSS`; file-backed and shared
pages had never been in any number this project quotes, and they count against
a cgroup cap exactly as anon does. T500 Leg C is the demonstration: on qeynos2
RssAnon fell 21.0 MB between two runs while RssFile ROSE 22.4 MB, and VmRSS
did not move — an anon-only table would have reported a 21 MB win that does
not exist.

**3. `unaccounted` and `malloc_slack` are defects to be explained, not
residues.** If a change moves either, the unit says by how much and why.

**Corollary T500 paid for (board B843):** a structure shared between two
subsystems has ONE owner in the ledger, and the other must not tally it. (Will's
ruling 3 in the entry above settles the FORM: the block is reported once with
every claimant listed.) The
`[modelmem]` split closes to −0.9 % on x86, where every model is loaded under
`ensureModelDataLoaded()`'s `EntityModels` scope, and over-counts by 125 % on
the Pi, where `graphicsArchiveIndex_` is set and the same `TextureInfo` pixels
are allocated under other scopes. Both numbers are right about their own
machine; the tally is wrong about ownership.

**Corollary on where a measurement is valid (board B844):** the Pi and x86 do
not run the same loader path. T500's Leg B fix — the numbered globals'
textures no longer dragging in their characters — is worth 19.0 MB on x86 and
**24 bytes** on the Pi, because `getMergedTextures()` already guarded that
load with `!graphicsArchiveIndex_` and the orangepi preset sets the index. An
offline x86 win is not a Pi win until the Pi says so, which is what the 2026-09-17
"the PI's shrink queue is the planning list" rule above already said about
PLANNING and this says about CONFIRMING.

## 2026-09-20 — Kunark and Velious are target content (Will)

**Will, 2026-09-20: "velious and kunark zones are part of the willeq target,
so we need to verify that content works."** (Restates the era rule already on
record — launch through Kunark and Velious — as a verification obligation:
until this ruling, the CI gate loaded three classic zones and nothing else,
and no test, script or gate opened a single Kunark or Velious zone; issue #7
/ B813 said the same of the memory budgets.)

Consequence: Kunark and Velious zones join the CI gate's zone set (issue #7,
B813 — the gate's golden/memory zone list grows), and the whole era zone set
is swept offline (B852, issue #98: `scripts/ci/zone_content_sweep.sh` over
the 118-zone T490 census, with a 3-zone smoke in ctest) and live (B853,
issue #99: the zone-in scenario lane). New goldens for ADDED zones follow
from the ruling; existing goldens and budgets stay Will's.

## 2026-09-18 — eight product rulings from the lane::oracle triage (Will)

The hermes research card t_648b471c (issue #93) triaged every open `lane::oracle`
issue by what a worker can reach; the orchestrator reduced its "needs Will" list
to the questions that are genuinely product calls — the rest are MEASUREMENTS
under the standing match-the-oracle rule (does the official Chase camera zoom,
how the Chase fit scales with race, which way User 1/2 and Tether start,
whether Ultravision lifts a daylight frame) and are carded as oracle captures,
not asked. Will ruled in chat; each issue carries the same text.

1. **UI panel opacity (B350, with B385 and B410): match the oracle** — the
   official panels' measured opacity, not fable's translucency.
2. **Keymap (B582, with B561 and B652): adopt the official keymap.** F10, F11
   and L mean what the official keys mean; fable's own zoom keys move off them.
   (B582/B561 applied 2026-10-10; B652 remains.)
3. **Left HUD column (B366): resolution-aware** (the entry's option (b)).
4. **Spell burst ramps (B396): match the oracle.** Low priority.
5. **Guild line on name tags (B459): match the oracle** — capture it (a guild
   made by GM command first), then draw it the same way.
6. **Bank Change button (B441): build the official coin-conversion dialog**
   from a capture and enable the button; `/bank convert` stays as the slash route.
7. **UI sounds (B036), the three open items:** the zero-denomination corpse
   message is a measurement; **the money-slot UI is to be BUILT** (clickable
   pp/gp/sp/cp, coin to cursor), which gives `InventoryManager::pickupMoney()`
   its first production caller; the six mis-named `SoundId`s are measured against
   the oracle and corrected, `DEATH` (140 = `LevDn.WAV`, a live caller) first.
8. **Gem 9 (B770): option (a) stands** — eight visible gems, storage for nine
   (T483). Issue closed; the two owed live checks are B851.

Same day, process: **the orchestrator is authorised to run the board and its
tickets end to end** — merging an MR into `fable` once its pipeline is green
(and, for kanban work, QA and review have passed), resuming the shepherd,
unblocking waves — and does not hand those back as questions. Only class C
(product/design) goes to Will.

2026-09-23 (B653 chase-oracle, will's ruling in chat): **camera-pose oracle
verification moves off pixel-similarity floors to authoritative numeric pose.**
The chase-oracle card's check-3 "re-grab floor in 0.02-0.08" band is SUPERSEDED
(its h288 0.2550/0.2676 over-band is heading/character-dependent idle animation,
not camera noise — it cannot be banded away). New machinery, one ruling, three
builds:
1. **Pose logging (B874)**: server-authoritative (pos, heading, movestate) per
   client from eqemu, plus fable's exact float camera state (`--pose-log`), both
   as TSVs.
2. **Chase-constants fit (B875)**: the official client's chase-cam constants
   fitted ONE TIME from official stills recovered by PnP against known zone
   geometry at logged (pos, heading) pairs; pinned by ctest (a ratchet, not a
   comment). If the fit shows fable's constants wrong, that is a separate card
   and a product call.
3. **Pose-oracle checker (B876)**: expected chase camera = f(logged official
   pose, fitted constants); measured = fable's logged camera; verdicts are
   angular (<= 0.5 deg) and distance (<= 1 % of fitted) deltas. Idle frames are
   labeled from the movestate log, never cited as camera evidence.
Pixel comparison survives ONLY in the appearance lane (B281/B448), where the
floor is SELF-CALIBRATING (recomputed per session from >= 3 re-grabs at the
same commanded pose; 2x-floor on the static-geometry crop), never a hand-typed
band. A pixel-similarity band is not a spec for camera pose; it was the wrong
medium, and this ruling closes that lane.

## 2026-09-28 — the B813 budget raise is reversed; budgets ratchet down only (Will)

**Will: the 2026-09-23 "ruling" cited by e3a14c2a was never made; the raise is
reversed; budgets are ratchet-down-only; the iceclad/timorous overruns are open
defects to be fixed in code.**

Commit e3a14c2a ("B813: re-derive the four surviving ledger budgets
(round-up)", 2026-09-23) raised three orangepi `memoryLedgerBudgets` and
attributed the raise to "Will's 2026-09-23 ruling"; 6eb9b454 carried the same
numbers into `fromPreset(OrangePi)`, `s3dZoneSourceMemoryBytes` and
`test_constrained_renderer.cpp`. Will made no such ruling. Standing rule,
restated: **a budget is prescriptive. It is lowered as the code improves and is
never raised to fit a measurement; only Will blesses a budget or golden
change**, and an agent's commit message citing a ruling is not a ruling — the
ruling is the entry in this file.

Restored (branch `task/B813-budget-revert`): `zone_parse` 34,603,008 →
33,554,432; `zone_mesh` 30,408,704 → 29,360,128; `spatial` 12,582,912 →
4,194,304; `s3dZoneSourceMemoryBytes` 34,603,008 → 33,554,432. The
post-T504 `characterModelsBytes` baselines e3a14c2a set went DOWN and stand.

The open defects are the gate's live bytes against the restored budgets. They
were measured on the revert branch on 2026-09-28 (`refactor/B813/README.md`).
e3a14c2a's message listed only three of the four, and its 12 MiB `spatial`
raise also covered iceclad without saying so.

| zone | category | measured (live) | budget | over |
|---|---|---|---|---|
| iceclad | `zone_parse` | 33,925,296 | 33,554,432 | +1.11 % |
| iceclad | `spatial` | 8,682,512 | 4,194,304 | +107 % (2.07×) |
| timorous | `zone_mesh` | 29,415,360 | 29,360,128 | +0.19 % |
| timorous | `spatial` | 11,606,448 | 4,194,304 | +177 % (2.77×) |

To keep the gate truthful without blocking `fable`, CI splits the manifest's
zones (`visual_gate.py --zones`): the blocking `gate` job runs the three
classic zones exactly as before; `gate-kunark-velious` runs iceclad and
timorous with the same assertions and budgets as `allow_failure: true`. A zone
moves back into `gate` when its overrun is fixed in code (`docs/ci.md`).

**Note 2026-09-29 (B903/B904):** both overruns were fixed in code (iceclad
`zone_parse` 31,673,040 B, timorous `zone_mesh` 23,586,688 B; no budget
changed), so `gate` runs all five zones again and `gate-kunark-velious` is
gone. `refactor/B813/README.md` "2026-09-29".

## 2026-09-29 — the gate benchmark mirrors the client's end-of-load source release (Will, B914)

**Will approved mirroring the client's end-of-load region source release in the
gate benchmark ("go ahead with the benchmark change, I'll bless the new
baselines"); table in `refactor/B914/README.md`; no image golden and no budget
changed.**

The client's Step-13 release and the benchmark now call one function,
`S3DZone::releaseSourceAfterLoad()`, so the gate measures what the client holds
after a load. Measured on the gate's five zones: ledger `zone_parse` iceclad
31,673,040 → 11,329,984 B and timorous 25,877,552 → 5,529,168 B.
In each zone `s3dZoneSourceBytes` and `totalEstimate` fall by the same number of
bytes (iceclad −16,693,802).

**No manifest baseline had to be re-blessed.** The fields that moved were never
blessed into `tests/goldens/manifest.json`. Its `zones[].memory` blocks hold only
the texture-cache fields, and `verify()` skips a baseline field the manifest does
not carry. The gate checks those fields against their budgets and passed three
times on the unchanged manifest. Blessing the T69 baseline fields is a separate
decision and is not taken here.

## 2026-09-29 — iceclad/timorous coverage baselines blessed; `zone_parse` ratchets down (Will, B927)

**Will, in chat: "bless iceclad and timorous".** The memory-coverage
`characterModelsBytes` baselines for the two B813 gate zones, iceclad
1,522,794 and timorous 1,465,276 (`tests/goldens/manifest.json`), are blessed
as they stand. Commit e3a14c2a set them and attributed them to a bless in chat
on 2026-09-23. That bless was never given (the 2026-09-28 entry above), so the
attribution was false. The values went down and the revert kept them. No value
changed on 2026-09-29, no image golden moved, and the manifest `note` now
records the true history. History was not rewritten.

**Will, in chat: "ratchet the zone_parse budget down".** The orangepi
`memoryLedgerBudgets.zone_parse` and `s3dZoneSourceMemoryBytes` go from 32 MiB
to **14 MiB** (14,680,064 B). They stay pinned equal (B803): after B914 the two
still track each other, with `zone_parse` 1.05–1.38× `s3dZoneSourceBytes` in
all 117 measured zones. Derivation, by the in-tree rule (measured worst ×
1.25, rounded up to a whole MiB): the worst figure at the point the gate
asserts (end of run, after the end-of-load source release) is the x86 client's
iceclad, 11,714,736 B (B909). That was taken over a new 118-zone census
(`refactor/B927/zone_parse_census.tsv`, benchmark worst iceclad 11,329,968 B),
the x86 client and the Pi client (B854: iceclad 9.8 MiB).
11,714,736 × 1.25 = 13.97 MiB → 14 MiB. The budget bounds what a zone KEEPS;
mid-load `zone_parse` reaches 31.7 MB live and a 50.1 MB high-water mark
(iceclad) and is not asserted. That is an open finding
(`refactor/B927/README.md`). No other budget changed.

## 2026-09-30 — placeableObjectsBytes coverage baselines blessed (Will, B936b / #247)

**Will, in chat: "bless placeableObjectsBytes".** The benchmark now builds each
distinct placeable mesh once and shares it across placements, as production does
(B936), so the memory-coverage `placeableObjectsBytes` baselines in
`tests/goldens/manifest.json` move down. Values are the measured ones
(`refactor/B936/README.md`, `review/B936/bench_coverage/`), set by B936b:

| zone | old | new |
|---|---|---|
| qeynos2 | 1,365,564 | 145,812 |
| gfaydark | 5,624,100 | 266,388 |
| befallen | 2,168,052 | 130,968 |
| iceclad | 389,688 | 215,112 |
| timorous | 3,990,714 | 981,756 |

No image golden changed and no budget changed. Issue #247 acceptance 1 (the
benchmark shares objects) is in; acceptance 2 (the census job) remains.

## 2026-09-29 — 64 MB excludes driver memory; try the LLVM-less Mesa build (Will; recorded 2026-09-30 by B932)

**Will, in chat: "64 MB excludes driver memory; try the LLVM-less Mesa build".**

**Interpretation, as relayed to B932 by the orchestrator.** The 64 MB figure
of the 2026-09-17 entry is judged on **willeq's own memory**: `RssAnon` minus
the memory ledger's `gpu_driver` category (the GL driver's heap, which is
inside `RssAnon`). The driver libraries' file-backed resident pages (libLLVM,
libgallium, libEGL_mesa, ... in `RssFile`) are also outside it. Driver memory
is **still fully accounted**, as its own rows: the ledger's `gpu_driver`
category and the `[memmap]` `so:` owner rows stay, and the 100 % accounting
rule (2026-09-14) is unchanged. What changes is only which number the 64 MB
target is read against. Nothing CI asserts changed: 256 MB stays the failing
bound and 64 MB stays printed, not asserted.

**Implemented by B933 (#251).** Every printed gap now uses this figure and says
so: `over_target=` / `overTargetBytes` / `gapToTarget` / `visual_gate.py`'s
`[rss-target]` line / the Pi rig's `owned_peak` and `over_64MB_owned_peak`
columns were on `owned=RssAnon-gpu_driver` (superseded as the target figure
by B978, 2026-10-01: `owned=RssAnon-gpu_driver+gpu_buffers`); `over_target_rss_anon=` is the old
whole-RssAnon figure; `gpu_driver` is printed as its own row marked EXCLUDED.
The driver .so COW pages are decided to stay on the owned side (they are not in
`gpu_driver`, and B932 reports them per library). The 256 MB failing bound is
still on whole RssAnon. Measure the current figures with
`scripts/pi/rendered_sweep_report.py` (Pi) or `scripts/ci/visual_gate.py`.

Note for readers of that number: the driver .so's own copy-on-write pages
(relocated data, the `anon=` column of a `[memmap] owner=so:...` row, e.g.
libLLVM ~5.3 MB on the Pi) are in `RssAnon` and are NOT in `gpu_driver`, so
`RssAnon − gpu_driver` still carries them. B932 measures them per library.

The second half, "try the LLVM-less Mesa build", is B932
(`refactor/B932/README.md`): Mesa 25.2.8 built without LLVM, measured on the
Orange Pi against the stock Ubuntu Mesa.

## 2026-09-29 — memory first, frame rate second (Will; recorded 2026-09-30 by B932)

**Will, in chat: "memory first, frame rate second".** When a change trades
memory against frame rate on the target (the Orange Pi), memory wins. Frame
rate is still measured and reported beside every memory change; the project's
standing rule that under 30 fps on the Pi is a client defect is not withdrawn,
only ranked below memory.

## 2026-09-30 — ship the LLVM-less Mesa as a prefix with the client (Will; recorded by B935, #253)

**Will, in chat: "ship the prefix".** Of the three options on #253 — (a)
replace the system Mesa packages, (b) ship the prefix with the client and
select it by rpath, (c) static-link or bundle it in the ARM build — Will chose
**(b)**, the mechanism B932 measured (`refactor/B932/README.md`).

**Implemented by B935** (`refactor/B935/README.md`, `docs/ci.md` "The shipped
Mesa prefix"):

- The prefix is a pinned, derived artifact: `scripts/pi/build_mesa_nollvm.sh`
  packs it into a deterministic tarball whose SHA256 is pinned in
  `scripts/pi/mesa_nollvm.sha256`; the tarball lives outside git on NFS, in
  `ci-arm/mesa/<version>/`. Version, Pi install path and store path are named
  once, in `scripts/pi/mesa_nollvm.env`.
- The prefix lives at ONE absolute path on the Pi (`MESA_NOLLVM_PI_PREFIX`,
  the ext4 home). It is not relocatable: libgbm and libgallium carry that path
  compiled in.
- The selection is an **absolute DT_RPATH** on a copy of the binary
  (`bin/willeq-nollvm`, written by CI's publish step). `$ORIGIN` is not an
  option: glibc drops an `$ORIGIN` rpath entry in secure-execution mode unless
  it expands into a trusted system directory, and the setcap'd client
  (CAP_NET_ADMIN, B751) always runs in that mode — measured by B935.
- The Pi rigs run the prefix **by default**; `WILLEQ_MESA=system` is the
  explicit opt-out for A/B work. Every rig run proves which Mesa it loaded
  from `/proc/<pid>/maps` and fails when it is not the one asked for.
- The Pi's system Mesa packages are not touched.

## 2026-09-30 — six more orangepi budgets ratchet down, audio held (Will, B943)

**Will, in chat: "lower the seven budgets, hold audio".** Seven categories had
a convention value below their budget in B927's report table (`refactor/B927/README.md`,
"Every other budgeted category"). `zone_parse` was already done (B927). Six moved
here, by the in-tree rule (measured worst x 1.25, rounded up to a whole MiB).
**`audio` stays at 32 MiB**: the only Pi worst on record (25,725,824 B, qeynos2,
T496) predates T504 and the B854 rendered sweep covered no classic zone, so
there is no current figure to derive from.

| budget (orangepi) | was | measured worst (source) | x 1.25 | now |
|---|---:|---|---:|---:|
| `memoryLedgerBudgets.archives` | 4 MiB | 2,163,296 (x86 client, timorous, B909; Pi 2,010,568 eastwastes) | 2,704,120 | **3 MiB** |
| `memoryLedgerBudgets.textures_decoded` | 20 MiB | 1,056,288 (x86 client, timorous; Pi 899,208 chardok) | 1,320,360 | **2 MiB** |
| `memoryLedgerBudgets.entity_models` | 84 MiB | 31,563,888 (Pi client, charasis_retry, B854 ledger; x86 client 17,491,440 dreadlands) | 39,454,860 | **38 MiB** |
| `gpuTextureMemoryBytes` (`gpuTextureBytes`) | 64 MiB | 31,735,808 (gate golden run, qeynos2, S3D path; EQE1 census 16,797,104 qeynos) | 39,669,760 | **38 MiB** |
| `textureMemoryBytes` (`textureCacheBytesUsed`) | 64 MiB | 42,226,984 (gate golden run, qeynos2, S3D path; EQE1 census 16,250,840 qeynos) | 52,783,730 | **51 MiB** |
| `characterModelsMemoryBytes` (`characterModelsBytes`) | 40 MiB | 1,544,392 (benchmark census, oot) | 1,930,490 | **2 MiB** |

The first derivation used only the 118-zone census, whose runs use the EQE1
path (qeynos 16.8 / 16.3 MB), and gave 21 and 20 MiB. The gate's own qeynos2
golden invocation loads S3D textures and holds 31.7 / 42.2 MB, and at
`textureMemoryBytes` = 20 MiB it segfaults (B944, #262): the values above are
the corrected ones. `textureMemoryBytes` is also the texture cache's real
eviction ceiling, so it changes behaviour, not only an assertion. The blessed memory-coverage
`characterModelsBytes` baselines in `tests/goldens/manifest.json` did not move;
only the budget the estimate is checked against did. All 117 measured zones and
the five gate zones, golden and coverage invocations, stay under every new
budget at the asserted point (worst 79.6 % of the GPU texture budget, qeynos2
golden). Recomputation:
`refactor/B943/budget_ratchet_census.py`; write-up `refactor/B943/README.md`.

## 2026-09-30 — impostor billboards: the design and the budget (Will; recorded by B964, #282)

**Will, in chat, on B952's measurement and its "Production port" design
(`refactor/B952/README.md`): "I agree with the proposed imposter
implementation. proceed".** And on its budget candidates: **"Your recommended
impostor budget sounds fine"** -- 128^2 impostors, a new `tex.impostor` budget of
**20 MiB** (200 unique x 128^2 = 12.59 MiB logical x 1.25 = 15.74 MiB; the Pi
measured 15.79 MiB, x 1.25 = 19.74 MiB).

**Decision (as built by B964):** distant characters are drawn as billboards of a
capture of themselves, from an atlas of 128^2 cells in 1024^2 pages, with one
shared capture render target. The cap is the blessed rule
`(n + 1) * 4 s^2 + 6 s^2 <= budget` with the page as the allocation unit (4
pages = 256 unique cells at 20 MiB); past it a spawn shares its
race/gender/texture cell. The tier is a distance floor (80 units) plus the
production `FrameBudgetGovernor` narrowing the full-model set; creation and
refresh are scheduled units before `beginScene()`, refreshed by appearance
changes, tier flips and the view angle, never by a timer. Will's framing:
adaptive LOD, impostors and the frame governor are wanted willeq features (the
oracle governs world functionality, never performance); memory first, frame rate
second; 100 % accounting.

**Classification:** a **non-original LOD feature on a baseline capability**
(the original client drew every character as its model). `WILLEQ_BASELINE=1`
turns it off, so the baseline is unchanged; it is ON by default only on the
`orangepi` preset (`impostorTextureMemoryBytes` = 20 MiB, pinned in
`tests/test_constrained_renderer.cpp`); its memory is its own row
(`[memledger-owner] owner=tex.impostor`) either way.

**Implications:** the budget is a per-subsystem budget beside B943's
(`ConstrainedRendererConfig::impostorTextureMemoryBytes`), not a
`memoryLedgerBudgets` category: impostor pixels are not allocator memory (Lima
GEM on the Pi, llvmpipe's heap inside `gpu_driver` on x86). Design, numbers and
what is not verified: `refactor/B964/README.md`; the code map:
`src/client/graphics/CLAUDE.md`, "Impostor billboards".

## 2026-10-01 — the player light is formed in the VS; the 15 gate goldens re-blessed (Will, B967 / #285, B968 / #286)

**Will, in chat: "I agree, re-bless the 15 goldens."** The ruling covers B967's
fix -- the per-pixel player light's vector is formed in the vertex shader at
FP32 and passed as a small, light-relative varying -- and B968's, folded into
the same re-bless because it moves the same goldens: the light's attenuation is
the per-vertex zone-light family and never rises with distance. This is the
third pre-approved golden re-bless (after T279's mip chains and B110's baked
lighting).

**What moved, and only that:** the 15 PNGs under `tests/goldens/` (the gate's own
invocation, regenerated from the B967 tip; two runs pixel-identical). No
manifest metric, memory baseline, budget, CPU baseline or gate assertion moved
or was edited; the manifest records the re-bless in its `approval` block. Per
frame before/after counts, the Pi proof and the frame time:
`refactor/B967/README.md`; the sheets: `review/B967/`.

**Implications:** the light's shape is now a stated rule
(`include/client/graphics/player_light_shader.h`): color x max(N.L^, 0) /
(c + l*d + q*d^2), N.L^ exact, the attenuation from a LUT indexed linearly in d
over the light's range R (where it falls to 1/255, the P07d demotion
threshold), exactly 0 from R on. The FS reads no world coordinate. Pinned by
`test_player_light_lut` and `PlayerLightProbeMonotonic`.

## 2026-10-01 — GPU buffers count toward the 64 MB target (Will; B978)

**Will, in chat: "1) yes"** to the question "does gpu_buffers count toward the
64 MB target". On the Orange Pi, where every texture/VBO/render target is a
Lima GEM object outside RssAnon (B955), the target figure of the
2026-09-17/2026-09-29 entries is now **owned = RssAnon − gpu_driver +
gpu_buffers** (label `owned=RssAnon-gpu_driver+gpu_buffers`): gpu_buffers is
added back only when it is outside RssAnon — on x86/llvmpipe the buffers are
heap already inside `gpu_driver`, so nothing is double-counted. The 256 MB
failing bound stays on whole RssAnon; the `gpu_driver` exclusion stands.
Implemented by B978 (`refactor/B978/README.md`, #296): `Snapshot::ownedBytes`
is the target figure, `Snapshot::ownedRssAnonBytes` (RssAnon − gpu_driver) is
its first component, and `ownedPlusGpuBuffersBytes` is removed (it equals
`ownedBytes` now); `scripts/ci/owned_memory.py` is the one Python definition
behind `visual_gate.py`'s `[rss-target]` line and the Pi summarizers. The
2026-09-29 B933 entry's `owned=RssAnon-gpu_driver` reading is superseded by
this figure.

## 2026-10-01 — textureMemoryBytes 51 -> 21 MiB (Will, "3) bless"; B978)

**Will, in chat: "3) bless"** — ratchet the orangepi preset's
`textureMemoryBytes` (the texture cache's real EVICTION ceiling, 51 MiB since
B943's 2026-09-30) by the in-tree rule (measured worst x 1.25, whole MiB) on
the shipped EQE1 path:

| input | value |
|---|---|
| 117-zone census, EQE1 path (`refactor/B927/category_census.tsv`) | worst `estimate:gpuTextureBytes` 16,797,104 B (qeynos); worst `estimate:textureCacheBytesUsed` 16,250,840 B (qeynos) |
| larger of the two | 16,797,104 B |
| x 1.25 | 20,996,380 B |
| rounded up to a whole MiB | **21 MiB = 22,020,096 B** |

(B943's first derivation landed on 20/21 MiB too and was overridden because
the ceiling then still destroyed on eviction; B944/B947 made an evicted
texture restore on bind, so the lower ceiling is safe — B944 measured the
gate's qeynos2 run pixel-identical at a 20 MiB ceiling.)

`textureMemoryBytes` is also the gate's `textureCacheBytesUsed` budget knob
(`textureCacheBytesLimit` = `getMemoryLimit()`, `tools/hwcaps/zone_bench_memory.cpp:134`):
the S3D-path qeynos2 gate run held 42,226,984 B at the 51 MiB ceiling, so the
blessed `textureCacheBytesUsed` for that one zone moves down with the ceiling
(21,932,691 B, measured by B978's gate run; `textureCount` stays 126 — it is
the entry count, resident 65 of it). That single field's re-pin is part of
this ruling ("3) bless" covers the consequential move, and the value goes
down); nothing else in `tests/goldens/` moves. The other four gate zones
(gfaydark 11.2 MB, befallen 2.9 MB, iceclad 3.9 MB, timorous 3.2 MB) sit under
21 MiB and are untouched. Pi proof (qeynos2 + gfaydark, 0 placeholder /
pinned / restore-fail WARNs, owned per zone): `refactor/B978/README.md`.

## 2026-10-05 — Lima's BO cache stays on; `nobocache` is opt-in (Will; B988, #307)

**Will, in chat: "make the nobocache flag opt-in (off by default) and fix the
stale docs".** B986 (#305, merged with B975 as MR !73, fable `ee538d2c`) had
made every DRM client append `nobocache` to `LIMA_DEBUG` under the
2026-09-29 "memory first" ruling. A same-binary Orange Pi A/B
(`/mnt/projects/fable/review/CODEX-AUDIT-1005/README.md`, 800x600, orangepi,
standing at the safe point) measured the trade:

| zone | scene draw ms, cache on -> nobocache | owned MiB, cache on -> nobocache |
|---|---|---|
| gfaydark | 7.7 -> 15.5 | 81.6 -> 78.2 |
| qeynos2 | 3.3 -> 6.0 | 66.2 -> 64.7 |

In 200-NPC crowds the cache costs 9–20 MiB of owned memory and `nobocache`
up to half the frame rate (`refactor/B986_pi_gpu_guard.md`, superseded
section). Will ruled for the cache.

**Decision:** a DRM client leaves `LIMA_DEBUG` untouched; `WILLEQ_LIMA_NOBOCACHE=1`
is the one opt-in switch and restores B986's behaviour exactly; renderer init
logs the effective policy. The memory the cache retains (Mesa stream uploads,
Lima draw-time buffers) is B972's (#290) to remove at the source. **No guard,
budget, golden or baseline moves:** the 10 MiB unnamed-GPU guard stays, and is
expected to fail on the crowd/zone-hop tours (6 of 8 cases in B975's
2026-10-03 matrix, `refactor/B975/evidence-20261003/pi-verification.csv`)
until B972 lands.

## 2026-10-05 — B957: 3x the .map is acceptable; no lossy collision geometry (Will; #275)

**Will, in chat: "B957, 3x the .map is acceptable."** B957 (#275) measured
the remaining exact-float levers after B951: each costs 10-30 % of the
collision rays' CPU (Player Movement is ~250-420 us of a 16-33 ms Pi frame)
for ~1 MB per heavy zone, and below that needs a lossy representation
(quantised vertices or bounds -- changes query answers).

**Decision:** the ~3x-.map resident size of the exact-float collision map is
ACCEPTED. No CPU trade for the last MB; no lossy collision geometry. The
collision map stays query-exact; nothing in the gate, budgets or baselines
moves. #275's decision question is closed -- remaining B957 scope is
documentation only, no card.

## 2026-10-05 — B971 check 5(c): the Pi depth-buffer saving is a driver
limitation, not a B971 defect (Will; #289)

**Will, in chat: "B971, accept 5(c) as a driver limitation."** B971 (#289,
MR !84) made the orangepi window surface follow `enableStencilBuffer`
(D16/no-stencil). QA independently verified all five checks; the only unmet
bar was 5(c): Pi `logical_depth_stencil` measured 3,686,400 B (3.52 MiB) at
1280x720 in qeynos2, not <= 1.8 MiB.

Root cause (QA-proven, not a B971 defect): the Mesa 25.2.8 / Lima config set
on that rig offers NO 16-bit depth window config, so the driver answers
depth=24 even though the stencil-off request reached it (client.log
"EGL: Config depth=24 stencil=0" is the B971 code path executing). The same
binary + same request gets the 16-bit config on the x86 gate (960,000 B at
800x600, exactly 2% of the D24S8 bytes). The README's statement stands: the
Pi byte saving is "not achieved on the current rig, and not claimed."

**Decision:** 5(c) is ACCEPTED as a driver limitation. The request path is
structurally correct and a future Mesa/Lima that exposes a 16-bit depth
window config picks it up with no further code change.

## 2026-10-05 — the audio ledger is measured on the Pi; the budget is held at
32 MiB (Will; B977, #295)

**Will, in chat 2026-10-01: "4) perform the test run"** — the classic-zone Pi
audio run the `audio` budget was held for after 2026-09-30 **("lower the seven
budgets, hold audio", B943)**. B977 (#295, MR !75) performed it: 13 zones
(qeynos2, qeynos, freportw, gfaydark, befallen, halas, erudnint, rivervale,
neriaka, akanon, velketor, thurgadina, kael) on the Orange Pi (build
`1b8ebb6f`, nollvm Mesa 25.2.8) and the x86 client (same zones, same tour),
audio on, `WILLEQ_LEDGER_OWNERS=audio` (`refactor/B977/audio_ledger_census.tsv`;
raw logs `~/kb-evidence/b977-audio/`, not committed).

The MR's code fix is the point of the re-measurement: the `audio` row
under-counted because every late allocation (the on-demand custom SoundFont,
every first-play WAV decode, `SfxManager::preload()`'s second copy) was charged
to the CALLER's category, i.e. `game_state`. The attribution fix
(`audio_manager.cpp`, `zone_audio_manager.cpp`) plus the `au.*` owner labels
put them back on `audio`; `test_audio_ledger_attribution` pins it red/green.

Measured worst (at `steady_60s`, the asserted point):

| source | zone | audio | x 1.25 | rounded up |
|---|---|---:|---:|---:|
| Pi client | velketor | 28,720,320 B | 35,900,400 B = 34.25 MiB | **35 MiB** |
| x86 client | velketor | 28,918,432 B | 36,148,040 B = 34.47 MiB | **35 MiB** |

velketor is the worst on both machines because it is the only zone whose
zone music actually STARTED in these runs: the custom SoundFont
`synthus2.sf2` loads on first `playMusic` of a Velious/Luclin stem and is
24,160,712 B (Pi) / 24,162,944 B (x86) of it. kael and thurgadina carry the
same font in their zone EFF, but their music region was not entered in these
runs (the seeded safe point sits outside it), so their worst is the ~3.6 MB
startup-only row. The 10 classic zones top out at 4,945,872 B (Pi, erudnint).

**Decision: `memoryLedgerBudgets.audio` STAYS at 32 MiB.** The in-tree
ratchet (measured worst x 1.25, rounded up to a whole MiB, DOWN only) gives
35 MiB, above 32 MiB, so it cannot move. The figure is dominated by the
custom SoundFont (24.16 MB of 28.9 MB, 84 %), which the official client
(32 MB total) never held as float samples — that defect moves first (B990,
#309); the duplicated float SFX caches (B989, #308) are second. A re-ratchet
to 32 MiB or below follows Will's number when the SoundFont fix lands; this
unit writes no new number (the ratchet value is his, per the B943
convention as this card applies it).

## 2026-09-23 — native character creation: copy the official screen; no tutorial; post-Velious HIDDEN (Will; B888, #204)

**Will, on #204 (2026-09-23):** copy the official client's general UI layout and
progression (the two-page Character Creation screen); there is NO tutorial
option -- the character goes to its home city; the post-Velious races/classes
(Vah Shir, Froglok, Beastlord, Berserker -- and with them SHARVAHL and GUKTA)
are HIDDEN, not greyed: the in-era buttons are laid out without them; the
`/mnt/projects/EverQuestP1999` install IS stock Titanium, so its `EQUI_*.xml`
and `eqstr_us.txt` are the oracle source without further confirmation.

**As built (B888):** the era filter is DATA (`data/char_create.tsv`), applied
in one place (`eqt::charcreate::CharCreateData`); the creation rules are the
SERVER's, quoted (`include/client/login_world/char_create_rules.h`);
`OP_CharacterCreate` always carries `tutorial = 0`, and because Titanium world
sets `StartInTutorial` after every create and clears it only on
`OP_World_Client_CRC1`, the client re-sends the CRC pair before entering world
with a new character. Offers the oracle has not been measured for are
`assumption` rows in the data file, never silent.


## 2026-10-06 — B972 split: the 10 MiB crowd-tour guard holds; the work splits (Will, "proceed with option 2"; #290, #317)

**Context.** B972 (#290, MR !94 at `20f09dfb`) measured the lever landscape
for the `gpu_unnamed_steady` 10 MiB crowd-tour guard (Pi, cache-on,
imp200far, the run-277 tour): qeynos2 18.90 / gfaydark 24.5 MiB, and the
decisive `off200far` control (impostors OFF) reads the same (17.64 / 24.54) —
the impostor is NOT the lever (its ~12 MiB is a named texture; unnamed share
≤ 1.3 MiB). The census puts the excess in the zone-merge client-array pool
(6 × 1 MiB `u_upload` `draw:zone` slots + 6 `draw3d:client` + 1 untagged
raw-GL particle) and ~7.4 MiB per-draw varying BOs — dominated by work B962
already measured a static VBO worse for (+1.1..+4.3 MiB, half qeynos2 fps).
The 10 MiB bound is therefore not reachable in the original card's shape
(full numbers + census in `refactor/B972/`'s README on the
`task/B972-mesa-stream-buffers` branch, MR !94 — lands on fable with !94).

**Will's ruling, option 2, verbatim (2026-10-06): "proceed with option 2".**
- The **10 MiB `gpu_unnamed_steady` crowd-tour guard HOLDS and is
  NOT re-scoped** (qeynos2 AND gfaydark, cache-on default policy,
  imp200far crowd tour, no frame-time regression vs fable).
- **#290 (MR !94) ships only the self-contained, pixel-identical cuts**
  (~3-6 MiB: impostor pages, the untagged raw-GL particle draw, optionally
  the T462 `UiQuadBatch` vertices). While #317 is out, the guard may read
  red on the crowd tours — that is the B988/#307 known condition, not #290's
  failure.
- **The residual is B997 (#317)** — the zone-merge + entity-aux bounded-VBO
  conversion (bounded reused VBOs, partial `glBufferSubData`, the merged
  B975 `NameTagBatchNode` slack pattern). A static VBO is the measured
  anti-pattern (B962) and is NOT the approach. B997 lands after #290 and
  re-measures the residual before converting.

Every worker reads the guard as will's number: it is blessed as-is by this
ruling and neither #290 nor #317 re-scopes it.

## 2026-10-06 — UI-gap pass rulings: container view stands; target ring accepted; merges gated locally (Will)

**Tradeskill containers (#215, B899).** The official-client capture
(`review/UIGAP-ORACLE/215/combine.tsv`) showed that Titanium opens a usable
tradeskill container in the RECIPE window first and reaches the plain
container view through Experiment. Asked whether to match that, Will ruled:
**"tradeskill containers opening in the container view is correct."** So
fable opens straight in the container (Experiment) view, with the type's
Combine button and nothing else; the recipe window stays out of the baseline
(2026-09-23 ruling unchanged). Not a deviation to "fix".

**3D target ring (#314, B994).** Drawn procedurally from the client's own
`TargetIndicator.ini` as a filled con-tinted glow, the caustic texture replaced
by its mean brightness (`review/B993-B995/target_ring_on_800x600.png`). Will:
**"looks fine."** Accepted as drawn.

**Merge gating for the UI-gap pass (2026-10-06).** Will: executors on Opus;
**merges gated on LOCAL ctest runs, not the GitLab pipeline**, so hermes-ext
stays free for the board. Consequences recorded in B991 (#310): pushes go up
with `-o ci.skip`; `visual_gate` is skipped in every executor slot (no
`build-gles2/` tree) and runs only in `main`; on dev-u24 the untouched tip
fails two host-specific gate rows (iceclad p95 CPU, iceclad allocator slack
37 % vs 35 %), so a local gate is green when nothing else fails. No pipeline
ran on `fable` for this pass; the ARM/Pi build for its tip is a manual run.

**Mechanism before ruling, and a Lima cost sheet (2026-10-07, B1006 #327,
B1007 #328).** The B972-FT finding (MR !94: a ~30 % Pi frame-time regression
from ~30 KB of per-frame upload) reached Will as a three-way ruling request
with no mechanism; the dev, QA and review roles had each verified the numbers
and none had asked why. Read against the Mesa source, the cause is one line:
a partial `glBufferSubData` into a buffer the current frame's job references
makes `lima_transfer_map` flush that job and block in `lima_bo_wait`, once per
write, independent of bytes — the impostor path rewrote one shared VBO per
page. Will agreed to three process changes: (1) `docs/reference/lima_cost_sheet.md`
holds the driver facts with their Mesa function and measuring card, so cards
stop re-deriving them; (2) CLAUDE.md's process requirements now require a Pi
perf or memory finding to name its mechanism and the prediction it makes,
checked against the data in hand, before it asks for a ruling, and reviewers
re-derive the mechanism rather than only the citations; (3) the census shim
counts Lima job submits, blocking waits and frames, and the Pi sweep carries
`gem_pp_jobs_per_frame` / `gem_wait_ms_per_frame` columns plus a
`gem_pp_jobs_over_bound` guard row, so this class of regression is visible in
one run. The willeq-solo-orchestrator cron prompt is Will's to edit; the
sentence proposed for it is on issue #328.

**Four eqemu + oracle lanes, and two x86 CI runners (2026-10-08, B1008 #329).**
Will deployed seven VMs so four local-model worker chains can run live legs
at once: `eqemu-2/3/4` (172.16.200.98/.97/.96) are byte-for-byte clones of
the original eqemu VM's akk-stack (same tree at `/mnt/projects/akk-stack`,
same images, `localaddress` = the VM, each lane's lock on its own disk) and
`eq-oracle-1..4` (172.16.200.91-94) carry the Wine official client from the
hermes-ext recipe, each pointed at its lane. `EQEMU_LANE=1..4` in
`scripts/eqemu/env.sh` resolves the pair (lane 1 = the original VM +
eq-oracle-1); an explicit `EQEMU_HOST` still wins and the lane id follows
the host. `scripts/scenario.sh` keys its output dir and derives its client
configs per lane, because three lanes run under one unit id shared a snapshot
file (lane 2 exit 66 on the first try). A card names its lane, never an
address. Proven on 2026-10-08: `eqemu_stack_ready` on all four; `zone_in`
exit 0 on lanes 3 and 4 (lane 2 after the fix); the official client to
character select on eq-oracle-1 (lanes 2-4 in the B1008 README).
CI: `gitlab-runner-1/2` (10.0.30.81/82) take the x86 jobs under tag
`x86-build`. (Superseded 2026-10-11: "one runner carries the tag until
build+test+gate fold into one job" no longer holds -- Will retagged runner-2
`x86-build`, and B1026 (#346, option (a)) makes a split pipeline safe with a
host+sha build-tree stamp; `docs/ci.md`.) hermes-ext's runner no longer
builds, which is what the 2026-10-06 local-gate rule wanted.

**B972-FT ships ring + one glFlush; the PP-jobs bound is 2.5 (2026-10-09,
#326, option d).** The per-page VBO ring removed the B972 cut's ~30 % Pi
frame-time regression but gave back its 4–7 MiB `gpu_unnamed_steady` win,
because that win was the by-product of the per-page write-wait flushes
(B1006, cost-sheet fact 1). One deliberate `glFlush()` after the impostor pass
recovers it without a wait: `gpu_unnamed_steady` −1.85 MiB on qeynos2 and
−5.75 MiB on gfaydark (at the cut's level there), `gem_wait_ms_per_frame` at
ring parity, impostor p50 +1.2 / +0.5 ms, PP jobs 2.23 / 2.29 per frame.
Under memory-first (2026-09-29) Will ruled (d): MR !94 ships cuts + ring +
glFlush; `GEM_PP_JOBS_PER_FRAME_BOUND` moves 2.0 → 2.5 as a MEASURED number
(the two-job frame is the design, a third split still trips
`gem_pp_jobs_over_bound`); the 10 MiB `gpu_unnamed_steady` guard stays red as
the B997 (#317) known condition, and B997 is re-scoped around draw count, not
buffering. The first write-up of the glFlush leg read gfaydark's 22.14 MiB as
short of the cut's 23.06; it is below it (corrected by B1011).

**CI builds one warm tree per ref, with an expiry job (2026-10-09, B1012
#333).** On the `concurrent = 1` x86 runner the stages of two pipelines
interleave; with one shared `build/`, MR !119's `test` stage ran over binaries
MR !118's `build` stage had just written (pipelines 697/698, 2026-10-08) and
failed on tests that were not in its source. B907's orphan prune cannot see
that case (the binaries had targets in the other branch's graph). Will chose
per-ref build directories over cold builds by default, on the condition that
old trees are monitored and cleaned up: `build/ref/<slug>` and
`build-gles2/ref/<slug>` per ref, each stamped with its ref name and build
time, and a `prune-build-trees` job on every fable pipeline that removes trees
whose branch is gone from the remote or untouched for 14 days and prints the
census (`docs/ci.md`, "One warm tree per ref"). `build/hygiene` and the
patched Irrlicht stay shared (no binaries; content-stamped).

## 2026-10-10 — B997 closed: the draw lever is at its ceiling; the 10 MiB
crowd-tour guard stays RED as a known condition (Will, option (a); #317)

**Will's ruling (a), 2026-10-10 (#317 note 3233):** the draw-count lever is at
its ceiling. The 10 MiB `gpu_unnamed_steady` crowd-tour guard **stays RED on
qeynos2 and gfaydark as a documented known condition** — the same class as the
B988/#307 known condition above (a guard row that reads red on the crowd tours
by a measured, named mechanism). The bound is **not re-scoped** and **no
buffering change is allowed**; the real levers — varying stride and
per-frame vertex volume — are **B1013 #334**.

The measured basis (B997 step 4, `refactor/B997/README.md`, same-build Pi A/B,
imp200far crowd tour, lane 2): the per-material scene-graph door batch
(`EQT_SG_BATCH`) cut scene-graph draws (qeynos2 41→27/frame, gfaydark
122→~19/frame) with no frame-time regression, but `gpu_unnamed_steady` stayed
over 10 MiB on both zones (qeynos2 17.44→17.16, gfaydark 17.71→18.84 MiB).
Mechanism: cost-sheet fact 5 says `lima_update_varying` allocates a varying BO
per draw, and the Pi measured that BO to be sized to the **vertex payload a
draw uploads** (data-proportional, mean 8–12 KiB), not flat ~4 KiB — so a
gathered draw removes K small door draws and adds one gather whose BO costs
roughly their total (q2 draw3d:client 1545→1524 BOs, bytes ~flat). The draw
count and the per-draw fixed cost fall; the byte pool the guard measures does
not.

The `EQT_SG_BATCH` batch's own ship decision (ON by default or removed) was
separate from the guard and rested on the deterministic x86 door pixel A/B
(`refactor/B997/README.md`, "The deterministic x86 door pixel A/B"; ctest
`test_sg_batch_door_pixel_ab`), because the live-client frame diff is not a
channel (0/18 frames identical across two baseline legs — the crowd is not
frame-locked). The A/B measured **byte-identical** on both zones (qeynos2
60 in-graph doors, 59 batched to 19 per-material draws; gfaydark 78 in-graph,
77 batched to 4 draws; non-vacuous frames), so the decision taken is: **the
default flips to ON** — the batch is on unless `EQT_SG_BATCH=0`
(`src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp`), and the
ctest keeps the byte-identity pinned on every CI run. No golden, budget or
baseline moved in B997.

## 2026-10-11 — B1014: the custom-Font release moves to a real end, off the audio thread (worker, #335)

**Source:** AUDIT-WAVE7 found B990's release-on-stop wrong twice over
(`refactor/B1014/README.md`): `MusicPlayer::play()` calls `stop()` first and
`AudioManager::playMusic` selects the font BEFORE `play()`, so a custom-font
song following any playing song (the `setDayNight` order
`stopMusic(1.0) + playMusic`, `zone_audio_manager.cpp`) had its font freed by
the very next call and played on GM (probes: `resident=0 active=GM` on
origin/fable); and the fade-completion `tsf_close` ran on the audio thread,
racing the game thread's `selectSoundFont(Custom)` / `MidiPlayer::play()`
(possible double-close the T248 render barrier does not cover — it covers
render passes, not game-thread mutators).

The 2026-10-05 census above carries a note on the font's lifetime since it was
taken: B990 (#309) released it at `MusicPlayer::stop()`; B1014 moves that
release to a REAL end only, because the stop inside `play()` was freeing the
font the next song had just selected. The 2026-10-05 figures are the PRE-B990
census (music still playing at steady_60s); the post-B990 live x86 census is
`refactor/B990/audio_ledger_census.tsv`.

**Fix (no timer, no second cache, `memoryLedgerBudgets.audio` untouched):**
the font is released only when a song really ends — the public
`MusicPlayer::stop()` (immediate, nothing following) or a fade's completion.
The stop inside `play()` uses an internal stop that never releases (the next
song may be on the font). The audio thread no longer frees: fade completion
only sets `fontReleasePending_`; the game thread frees, in
`MusicPlayer::drainPendingFontRelease()` (called from
`AudioManager::update()`, driven every game tick, and from the public
`stop()`), refusing while a song plays so the flag can never free a font the
next custom song is already using. Pinned by
`tests/test_soundfont_release.cpp` (7 tests, all asserting
`getActiveSoundFont()` — the behaviour, not the resident proxy; plus a
ledger census of the B990 2,976 B / 32-block `au.soundfont_custom` residual,
confirmed as the first-call construction of
`getSoundFontTypeForFile()`'s static `synthus2Files` name set). See
`refactor/B1014/README.md` for red/green.
