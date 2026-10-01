# B978 — GPU buffers count toward the 64 MB target; the orangepi texture-cache eviction ceiling 51 -> 21 MiB

GitLab #296 (class::A, lane::pi). Branch `task/B978-owned-gpu-buffers-texmem`
from `gitlab/fable` @ `d3375b65`. Worktree `/mnt/projects/fable/wt/audit-owned2`.

Two rulings from Will (chat 2026-10-01), one unit:

1. **"1) yes"** to the question "does gpu_buffers count toward the 64 MB
   target": owned = RssAnon − gpu_driver + gpu_buffers on the Pi (gpu_buffers
   added only when the row is OUTSIDE RssAnon; on x86/llvmpipe the buffers are
   inside `gpu_driver` already — no double count). 256 MB failing bound stays
   on whole RssAnon; the `gpu_driver` exclusion stands.
2. **"3) bless"**: ratchet `textureMemoryBytes` (the texture cache's real
   EVICTION ceiling, orangepi preset; 51 MiB from B943) by the in-tree rule on
   the shipped EQE1 path.

## STATE AT HAND-OFF 2026-10-01

Handed off mid-unit on the coordinator's order (owner near his usage limit).
This commit is a **WIP**: code edits are complete for both halves, but
**nothing has been compiled or run yet** — the first `cmake --build` of this
worktree was still running when the hand-off order came.

### Locks / stacks touched

- **Pi lock: never taken.** **eqemu lock: never taken.** No stack brought up.
  Nothing to release.
- Worktree build tree `/mnt/projects/fable/wt/audit-owned2/build` was
  configured (RelWithDebInfo, GLES2, tests ON, its own
  `third_party/irrlicht-gles2-x86`, bootstrapped OK) and a background
  `cmake --build build -j24` was in flight (log
  `/tmp/claude-1000/-mnt-projects-fable/465ebf9d-87a5-4061-a6c6-8da5eb953e7a/scratchpad/b978/build.log`,
  exit file `.../b978/build.exit`). Treat its result as untrusted: if it was
  killed, run the exit-143 truncation scan from `refactor/EXECUTOR_PROTOCOL.md`
  before the next build.
- `data/eq_client` and `data/eqe1_client` symlinks created in the worktree
  (untracked, not committed).

### DONE (code edited, not yet built/tested)

**(1) owned arithmetic** — new label text `owned=RssAnon-gpu_driver+gpu_buffers`:

- `include/client/core/memory_ledger.h`: `Snapshot::ownedRssAnonBytes` (new,
  = max(0, RssAnon − gpu_driver)), `ownedBytes` now = ownedRssAnon + gpu_buffers
  (outside RssAnon only); `ownedPlusGpuBuffersBytes` REMOVED (it equals
  `ownedBytes` now). GapRow comment: `gpu_buffers` is a gap bucket on the Pi.
- `src/client/core/memory_ledger.cpp`: `finishTargetGap()` new arithmetic;
  `gapToTarget()` pushes a `gpu_buffers` row when outside RssAnon; the
  `[memledger]` line now prints `gpu_driver= owned_rss_anon= gpu_buffers=
  gpu_buffers_src= gpu_buffers_outside_rss_anon= owned= over_target=
  over_target_rss_anon=` (no more `owned_plus_gpu_buffers=`); report text
  `owned(RssAnon-gpu_driver+gpu_buffers) X MiB` + a line `owned =
  RssAnon-gpu_driver X MiB + gpu_buffers Y MiB (source, inside/outside)`; JSON
  `ownedRssAnonBytes` + `ownedBytes` (no more `ownedPlusGpuBuffersBytes`).
- `include/client/core/gpu_buffer_census.h` / `src/client/core/gpu_buffer_census.cpp`:
  `[memgpu]` line ends `... owned_rss_anon=<RssAnon-gpu_driver> owned=<target figure>`
  (was `owned=<RssAnon-gpu_driver> owned_plus_gpu_buffers=`); JSON
  `ownedRssAnonBytes`/`ownedBytes`; the functions take `ownedRssAnonBytes`.
  Caller `tools/hwcaps/zone_bench_gpu_tally.h` and `memory_ledger.cpp`'s
  `recordPoint` pass `s.ownedRssAnonBytes`.
- NEW `scripts/ci/owned_memory.py`: the ONE Python definition
  (`OWNED_LABEL`, `owned()`, `owned_rss_anon()`, `gpu_buffers_outside()`,
  `owned_from_memgpu()` — reads BOTH the pre-B978 `[memgpu]` shape and the new
  one, `owned_from_ledger_json()`).
- `scripts/ci/visual_gate.py` `[rss-target]`: `owned=RssAnon-gpu_driver+gpu_buffers
  X MiB (RssAnon, gpu_driver, gpu_buffers ... inside gpu_driver, not added
  again | outside RssAnon, added)`; `gpu_buffers` is a shrink bucket on the Pi.
- `scripts/pi/rendered_zone_summarize.py`: owned per ledger point = RssAnon −
  gpu_driver + gpu_buffers net of dumped PNG frames (2 s sample peak uses
  ready's gpu_driver/gpu_buffers); column `owned_plus_gpu_buffers_steady`
  REPLACED by `owned_rss_anon_steady` (same position); new END columns
  `owned_rss_anon_peak gpu_buffers_at_owned_peak`; details.json points carry
  `gpu_buffers_added`.
- `scripts/pi/rendered_sweep_report.py` (pre-B978 rows left out of the owned
  line with a note), `scripts/pi/collision_ab_table.py`,
  `scripts/client_ledger_table.py`, `scripts/pi/mesa_ab_table.py`
  (pre-B978 details.json flagged), `scripts/b964/crowd_table.py`.
- Tests: `MemoryLedger.TargetGapExcludesGpuDriver` rewritten (RssAnon 100,
  gpu_driver 19, gpu_buffers 22 outside → owned_rss_anon 81, owned 103, gap 39;
  x86 inside → owned 81, gap 17; log/report/JSON/gapToTarget pinned);
  `ReportsCarryTheTargetAndTheCeiling` updated;
  `tests/test_gpu_buffer_census.cpp`: new `CapturedPiRowCountsTowardTheTarget`
  (tests/data/b955 kmscube fixtures → owned), log/JSON expectations updated.
  `scripts/ci/test_visual_gate_ledger.py`: 7b (x86 no double count), 7c (Pi
  100/19/22 → 103, gap 39), 7d (`owned_from_memgpu` on BOTH
  `tests/data/b962/*_memgpu.txt` fixtures: qeynos2 steady 48,537,616 +
  23,433,216 = 71,970,832; legacy 47,755,144 + 38,735,872 = 86,491,016) —
  **this Python test was run and PASSES** (`19 checks OK`).

**(2) ceiling** — `textureMemoryBytes` orangepi **51 -> 21 MiB = 22,020,096 B**:

- Derivation re-verified from `refactor/B927/category_census.tsv` (117 zone
  rows per category, EQE1 path): worst `estimate:gpuTextureBytes`
  **16,797,104 (qeynos)**, worst `estimate:textureCacheBytesUsed` 16,250,840
  (qeynos); B943 `census_output.tsv` agrees. Larger = 16,797,104 × 1.25 =
  20,996,380 B = 20.02 MiB → **21 MiB**. (The dispatch said "118-zone"; the
  TSV has 117 zones per category.)
- Edited: `config/constrained_presets.json` (53477376 → 22020096),
  `src/client/graphics/constrained_renderer_config.cpp` (value + comment),
  `include/client/graphics/constrained_renderer_config.h` (comment),
  `tests/test_constrained_renderer.cpp` (three pins 51 → 21).

### ⚠ Finding that blocks the gate (needs the orchestrator / Will)

`textureMemoryBytes` is NOT only the eviction ceiling: the gate's
`textureCacheBytesUsed` budget IS this knob (`textureCacheBytesLimit` =
`getMemoryLimit()`, `tools/hwcaps/zone_bench_memory.cpp:134`), so that budget
moves 51 -> 21 too (the dispatch said it would stay at 51 — it cannot; only
`gpuTextureMemoryBytes` 38 MiB is a separate knob). Usage stays under it by
eviction. BUT `scripts/ci/visual_gate.py` ALSO exact-matches
`textureCacheBytesUsed` (and `textureCount`) against the blessed per-zone
baseline in `tests/goldens/manifest.json` (`MEMORY_BUDGET_PAIRS_ALSO_BASELINED`).
qeynos2's baseline is **42,226,984** (limit recorded 67,108,864) — with a
21 MiB ceiling the golden run evicts and will report ~21-22 MB, so the gate
WILL FAIL "qeynos2: texture cache bytes used mismatch" until that one baseline
is re-applied. The other four zones' baselines (11.2 / 2.9 / 3.9 / 3.2 MB) are
under 21 MiB and should not move. The dispatch forbids touching
`tests/goldens/` (B967 is regenerating goldens concurrently), so the value must
be applied BY HAND at merge from this unit's standalone gate run (precedent:
the T406/T411/T408 memory baselines in the manifest's memoryCoverage note) —
Will's "3) bless" arguably covers it; confirm before applying. Whether
`textureCount` (126 in qeynos2) is the entry or resident count is unverified
(B944 printed "entries 126, resident 62" at 20 MiB).

### REMAINS (numbered, exact steps)

1. Finish/redo the build: `cmake --build /mnt/projects/fable/wt/audit-owned2/build -j24 > LOG 2>&1; echo EXIT=$?`
   (background + `scripts/wait_for.sh --exit-file`). Fix compile errors (none
   expected; the edited C++ is `memory_ledger.{h,cpp}`, `gpu_buffer_census.{h,cpp}`,
   `zone_bench_gpu_tally.h`, three tests).
2. Targeted tests: `ctest --test-dir .../build -R "MemoryLedger|GpuBufferCensus|ConstrainedRenderer|VisualGateLedger" --output-on-failure`.
3. Grep for anything else reading the removed names:
   `git grep -n "ownedPlusGpuBuffersBytes\|owned_plus_gpu_buffers"` — only the
   b962 fixtures, `owned_memory.py`'s back-compat branch and the test 7d should remain.
4. Full suite `ctest --test-dir .../build -j6 --output-on-failure`; hygiene
   `ctest -L hygiene` (DocClaims/CommentClaims may want rows for new numbers;
   `ZeroCallers` may want an exemption for `ownedRssAnonBytes`? it is a field, probably not).
5. `bash scripts/build-gles2-x86.sh` (after step 1 finishes — never concurrently),
   then the local gate: `bash scripts/ci/visual_gate.sh` (all five zones). Expect
   image goldens **0 PIXEL MISMATCH** (B944 proved it at 20 MiB) and the single
   memory failure above (qeynos2 textureCacheBytesUsed). Record the new
   qeynos2 `textureCacheBytesUsed`/`textureCount` and the `[texture-cache]`
   stdout line (entries / resident / evictions / restores / restore failures)
   for every zone, before (fable `d3375b65` build-gles2, e.g. `/mnt/projects/fable/main/build-gles2`
   if at that tip, or `--texture-memory-bytes 53477376`) and after. Frame time:
   the gate's p95 process-CPU ms per zone (printed `[cpu]` line) before/after.
6. ARM: `bash scripts/build-arm-noble.sh` locally (compile check), then push and
   let CI `build-arm` publish `ci-arm/<sha>/` (`glab ci trigger build-arm -b task/B978-owned-gpu-buffers-texmem`
   if manual on task branches).
7. Pi (owner `b978`): `source scripts/pi/env.sh; pi_lock_take b978`;
   `source scripts/eqemu/env.sh; EQEMU_HOST=local eqemu_lock_take b978`;
   rendered client qeynos2 + gfaydark via `scripts/pi_rendered_zone_sweep.sh`
   (own cgroup default; `--zones qeynos2,gfaydark`, the CI binary
   `ci-arm/<sha>/bin/willeq-nollvm`; read the script header for the exact
   flags, and `docs/ci.md` "The ARM runner and Pi runs"), once with the B978
   binary and once with the fable `d3375b65` binary (or `WILLEQ_TEXTURE_MEMORY_BYTES=53477376`
   on the B978 binary — B944's test knob, LOG_WARN when set). Measure from
   client.log: `ConstrainedTextureCache` eviction/restore counts (the
   `unloadZone` line names them; also `[texture-cache]`), WARN counts for
   placeholder / pinned / restore-fail (must be 0), RssFile (snap_*.json),
   gpu_buffers, owned (new summarizer columns), fps (median/min) and frame-time
   hitches (>33 ms frames around restores; `/frametiming` blocks). Restore,
   `down.sh`, `eqemu_lock_release`, `pi_lock_release b978` ALWAYS.
8. Re-summarize the committed Pi runs with the new columns WITHOUT touching
   their dirs (summarize writes `details.json` into `--run`): build a symlink
   farm per run (client.log, samples.tsv, summary.json, snap_*.json, frames/)
   under `/mnt/projects/fable/review/B978/resummarized/<sweep>/<zone>` and run
   `python3 scripts/pi/rendered_zone_summarize.py --run <farm> --zone Z --attempt first --tsv <out.tsv> --expansion E --s3d-bytes N`
   for each row of `review/B962/{pi_final,pi_fixed,pi_legacy,pi_census_*,pi_before_fable}/results.tsv`
   and `review/B955/pi_sweep_{1,2}/results.tsv` (qeynos2 / gfaydark /
   growthplane only — no wider post-B955 Pi sweep exists, the 45-zone
   Kunark/Velious sweep predates gpu_buffers and cannot be re-derived); then
   `python3 scripts/b964/crowd_table.py review/B964/pi/crowd800_v2 ...` for the
   crowd runs. Put the per-zone owned-vs-64 table (owned_rss_anon, gpu_buffers
   net, owned, owned − 64) in this README and in `/mnt/projects/fable/review/B978/README.md`.
9. Docs: `docs/decisions.md` — two dated entries at the END (newest last):
   "2026-10-01 — GPU buffers count toward the 64 MB target (Will; B978)"
   quoting **"1) yes"** to "does gpu_buffers count toward the 64 MB target",
   and "2026-10-01 — textureMemoryBytes 51 -> 21 MiB (Will, "3) bless"; B978)"
   with the derivation table; strike B933's parenthetical in the 2026-09-29 entry
   (`owned=RssAnon-gpu_driver` → note superseded by B978). Root `CLAUDE.md`
   "Memory accounting" bullets (owned=RssAnon−gpu_driver+gpu_buffers; drop
   "`owned_plus_gpu_buffers` is printed beside `owned`, never asserted"; keep
   ≤ 60,000 chars — `FileSizeCeiling`). `src/client/graphics/CLAUDE.md`
   memory-ledger gpu_buffers bullet ("The 64 MB target stays on `owned`;
   `owned_plus_gpu_buffers` is printed for Will's ruling" → new rule) and the
   texture-cache paragraph (budget vs ceiling are the same knob for the cache,
   `gpuTextureMemoryBytes` is the other). `docs/ci.md` (~line 210 mentions
   `ownedBytes`/gpu_buffers). `refactor/BOARD.md` (~line 1049-1057: "Pi
   behaviour under 51 MiB is unverified" → B978).
10. Commit (trailer per dispatch), merge `gitlab/fable` if it moved, push,
    un-draft the MR (`Closes #296`), issue label `status::review`.
