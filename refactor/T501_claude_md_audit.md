# T501 — audit the root CLAUDE.md and rewrite it under a size cap

Branch `task/T501-claude-md-audit`, cut from `fable` at `5e6f9afe`, worked in
`/mnt/projects/fable/wt/t6`. OFFLINE unit: no eqemu lane, no Orange Pi, no VM,
no client launch.

## Why

`CLAUDE.md` was 66,660 bytes / 1,037 lines. The hermes kanban workers on
hermes-ext read it as a context file with a 62,914-byte cap (6 % of a 256K-token
window at 4 bytes/token) and silently drop the MIDDLE of anything longer, so the
derived-asset recipes, the scenario-lane exit codes and the whole Key Patterns
section were being lost without an error anywhere. Will: "I'm sure there's stale
data if nothing else" — and in this project a stale doc is a first-class defect.

## Method

1. **Audit, claim by claim.** Every checkable statement in the file got a row in
   `review/T501/claude_md_audit.tsv`: line span, category (path / count / flag /
   port / test / lane / board / rule / arch / workspace / build), the claim as
   the file states it, a verdict in {current, stale, wrong, unverifiable}, and
   the file:line or command output that proves it. 147 rows.

   The existing hygiene checkers were used as INPUTS, not as the audit:
   `check_doc_citations.py` (1,521 citations, 0 fail), `check_doc_claims.py`
   (54 retired names) and `check_doc_counts.py` (17 netted counts over 33 sites)
   were all green on the OLD file, which is exactly why they cannot be the
   audit — they only check what they were taught. Everything past them was
   re-measured by hand: `wc -l` on every file whose length the doc quotes,
   `check_file_sizes.sh --all` re-run, `sqlite3` over `data/willeq.db`, the
   `EQE1` header of the installed pack, `grep -c "TEST("` over the scenario
   tests, `git worktree list`, `nproc`, the BMP bit-depth field at offset 28,
   the registered scenario table, and every B-number in the file cross-checked
   against `refactor/BOARD.md`'s struck/live state.

2. **Rewrite.** Every `stale` and `wrong` row fixed. Narrative that belongs in
   `refactor/completed/` or `docs/decisions.md` deleted and replaced with a
   link — in particular the unit-by-unit history of the file-size batches, the
   three database-table drops, the pack's alpha arithmetic and the UI-preferences
   feature. **Every RULE kept**, none softened: process requirements, the merge
   golden step, the file-size ceiling, lane discipline, the verification bar, the
   "do not" imperatives (do not add a third slot conversion, do not put an item
   id in `equipment[]`, do not inline another restore, never quote 8-10 cells/s
   as a measurement, never draw a performance conclusion from a capture video).
   Reading order for a new agent preserved and made explicit at the top.

   Three rule blocks were ADDED, all of them things the file did not say and a
   worker needed: the eqemu lane's host (`scripts/eqemu/env.sh`, the VM at
   172.16.200.99, `EQEMU_HOST=local` for the local lane), the memory-accounting
   decisions, and the file's own size ceiling.

3. **Pin.** `scripts/refactor/check_file_sizes.sh` grew a `check_doc_ceilings`
   function driven by `DOC_BYTE_CEILINGS="CLAUDE.md:60000"`, enforced under
   `--all` only — which is what the `FileSizeCeiling` ctest test runs. It also
   runs on the zero-C++-files early-exit path, because a check that silently
   checked nothing is indistinguishable from a pass (the T37 rule this script
   already follows).

## Result

| | before | after |
|---|---:|---:|
| `CLAUDE.md` | 66,660 bytes / 1,037 lines | 55,319 bytes / 961 lines |

Audit verdicts: **129 current, 12 stale, 3 wrong, 3 unverifiable** over 147 rows.

The ten most consequential:

1. "the largest file anywhere under `src/`/`include/`/`tests/`/`tools/`/
   `benchmarks/` is 1,193" and "T453 emptied the band above 1,200 entirely" —
   **wrong**: `tests/test_constrained_renderer.cpp` is 1,279 lines today. Both
   sentences replaced by the measuring command.
2. "`cmake --build build -j24` for this 24-core host" — **wrong**: `nproc` = 40.
   Corrected, and the absence of ccache stated, because the very next line said
   "Build (no -j flag!)" and contradicted it.
3. "executor worktree slots at `wt/slot1..3` plus `t1`, `t2`" — **stale**: there
   are eleven, `t3`/`t5`/`t6`/`t7`/`t8` all on `task/T###` branches. Replaced by
   `git worktree list`.
4. The scenario table listed **three** scenarios; `scenario_registry.cpp`
   registers **six** (T494 added `equip_observed` / `equip_actor` /
   `equip_actor_return`). The table now points at `kScenarios` as the live list.
5. "`test_scenario_state_machines.cpp` runs all twenty cases" — **stale**: 29.
   Replaced by "every case".
6. "`PathfinderNavMesh`" — **wrong**: the class is `PathfinderNavmesh`
   (`include/client/pathfinder_nav_mesh.h:9`); the doc's spelling occurs zero
   times in the tree.
7. `check_file_sizes.sh --all` "reports 1,196 files" — **stale**: 1,248. Removed.
8. `include/client/eq.h` "899 lines" (891) and
   `include/client/graphics/irrlicht_renderer.h` "1,191" (1,184) — **stale**.
   Both removed rather than re-pinned; the ceiling is the rule, not the number.
9. `tools/hwcaps/zone_bench_loader.cpp` "977 lines" and
   `gles2_zone_benchmark.cpp` "992" — **stale**: both 1,014. Removed.
10. The file named **no eqemu host and no memory rule at all**. eqemu has run on
    a VM since 2026-09-12 and Will's 100 %-accounting and 32–64 MB rulings are
    standing decisions; both are now in the file, with citations.

## Verification

- `ctest -L hygiene`: **11/11 passed** against the new tree (`FileSizeCeiling`,
  `DocCitations`, `DocClaims`, `DocCounts`, `CommentClaims`,
  `ItemModelsMatchServer`, `WriteOnlyFields`, `ZeroCallers`,
  `ZeroCallersSelfTest`, `VisualGateLedgerAssertions`, `MemoryCapHuntScript`).
- No C++ build was needed. The hygiene fragment's tests are python/script scans;
  the three that need an artefact (`TexturesReproducible`, `PackReproducible`,
  `DbReproducible`) are `IF(TARGET ...)`-guarded and simply do not register in a
  `-DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON` configure, which is what was used.
- The new ceiling shown RED and GREEN:
  `check_file_sizes.sh -C <old tree> --all` → `FAIL CLAUDE.md: 66660 bytes
  (> 60000 …)`, exit 1; `-C . --all` → `ok CLAUDE.md: 55319 bytes (ceiling
  60000)`, exit 0, 1,248 C++ files 0 warn 0 fail.
- `DocCounts` netted 17 counts over 30 sites after the rewrite (down from 33 —
  the three lost sites were restored as sentences, not deleted as rows; the
  checker's regexes are unchanged).

## Review

- [measured] `CLAUDE.md` 66,660 → 55,319 bytes, 1,037 → 961 lines (`wc -c -l`).
- [measured] 147 audit rows: 129 current, 12 stale, 3 wrong, 3 unverifiable.
- [measured] `nproc` = 40 on this host; `which ccache` → not found.
- [measured] largest C++ file in scope = `tests/test_constrained_renderer.cpp`,
  1,279 lines; `check_file_sizes.sh --all` = 1,248 files, 0 warn, 0 fail.
- [measured] `data/willeq.db`: ten user tables, 28,791 rows,
  `metadata.version` = 7, `input_digest` = `956f69a5686c98bb`.
- [measured] installed pack header: magic `EQE1`, version 2, `numEntries`
  = 0x0084 = 132; file 9,726,596 bytes.
- [measured] 17 `snd*.pfs` and 378 `*_chr.s3d` in the EQ tree.
- [measured] `scenario_registry.cpp` registers six scenarios;
  `test_scenario_state_machines.cpp` has 29 `TEST(` cases.
- [measured] exactly two `friend class` lines in `include/client/eq.h`.
- [measured] 2,128 drift rows / 3,661 json rows / 117,944 tsv rows.
- [measured] `ctest -L hygiene` 11/11 green; the ceiling RED on the old file
  (exit 1) and GREEN on the new (exit 0).
- [assumed] The brief named three memory rulings from `docs/decisions.md`. Only
  two are there: 100 % accounting (2026-09-14, T491) and 32–64 MB (2026-09-17).
  **"x86 development authorised, both platforms' final numbers required" and
  "a shared block is reported once with all its claimants" have NO entry in
  `docs/decisions.md`** (`grep` for "both platforms", "shared block",
  "claimant" → 0 hits). The x86/both-platforms half is at least anchored by the
  2026-09-17 T496 addendum, which rules that a shrink unit is planned off the
  Pi's ledger. Both were written into `CLAUDE.md` as rules, with an HTML comment
  recording that they were relayed rather than cited. **They should be recorded
  in `docs/decisions.md` by the orchestrator; until then that half of the block
  is an assumption, not a citation.**
- [assumed] The hermes context-file cap of 62,914 bytes is taken from the
  brief's arithmetic (6 % × 256K tokens × 4 bytes/token). Nothing in this
  checkout states it, and this unit had no hermes access to measure it. The
  60,000 ceiling is set below it with margin, so the assumption only affects how
  much headroom there is, not whether the ceiling works.
- [assumed] "Restore takes 17 s byte-identical, against 22 s (local) / 2 min
  (over ssh)" and "the Orange Pi's `/tmp` tmpfs is ~242 MB" were kept as T493's
  and T61's measurements. OFFLINE unit — neither could be re-taken.
- [measured, out of scope] `refactor/BOARD.md`'s **B797** and **B798** are still
  LIVE bullets although T497 fixed both (`eqemu_stack_ready` and
  `eq_container_pkill` are in `scripts/eqemu/env.sh` today). The brief says not
  to touch other entries, so they were left alone — but they are two unstruck
  entries for work that is done, and the orchestrator should strike them.

## STOP conditions honoured

- Sub-directory `CLAUDE.md` files were not touched.
- No rule was softened or removed; no new rule beyond the three the brief named
  and the lane-host fact.
- No other board entry was edited. `refactor/BOARD.md` gained exactly one
  section and one entry, struck in the same commit.
- Nothing was merged.
