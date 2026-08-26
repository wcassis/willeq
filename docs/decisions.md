# Architecture Decisions

Standing decisions for WillEQ. Sessions and agents must treat these as settled —
do not re-litigate or contradict them without explicit direction from Will.
Newest entries first. Each entry: decision, rationale, implications.

---

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
in refactor/deferred.md pending re-measurement under the final fixture.

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
`docs/module_map.md`. Follow-up (Batch G) candidates in `refactor/deferred.md`:
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
  `docs/module_map.md`, `refactor/progress.md`, `refactor/deferred.md`,
  `docs/reference/arch_design_orig.md` / `arch_design_update.md`.
- **Historical (banner, never fixed):** `refactor/completed/*`, the rest of
  `docs/reference/*`, `docs/archive/*`. These carry a header marking them as
  point-in-time snapshots; superseded findings inside them are expected.

Open work items deferred out of "done" batches live in `refactor/deferred.md`,
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
   (`eq_chat.cpp:276`), fed from the chat-window intent
   (`eq_bridge.cpp:947`). **Also wrapped entirely in
   `#ifdef EQT_HAS_GRAPHICS`** — so in a headless-compiled build, slash
   commands do not exist at all.
2. `action::CommandProcessor` — a second slash vocabulary, the only one
   `--script` dispatches to (`application.cpp:901`).
3. Renderer debug registry (`/sort`, `/mergedraws`, `/screenshot`, …) —
   reached via the `GameEventType::RendererCommand` bridge queue.
   `--script` forwards its unrecognized commands here as a fallback.
4. `ConsoleInputHandler` (`console_input_handler.cpp:276`) — a bespoke
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
- Only one component may read stdin (`application.cpp:808,818` already
  documents a DRM-mode duplicate-read hazard).
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
