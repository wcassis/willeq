# Architecture Decisions

Standing decisions for WillEQ. Sessions and agents must treat these as settled —
do not re-litigate or contradict them without explicit direction from Will.
Newest entries first. Each entry: decision, rationale, implications.

---

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
- Slot assignments are recorded in the active batch plan (`refactor/F_file_split.md`).

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
