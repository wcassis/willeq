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

## STATE 2026-10-03 (kb-b978, hermes-ext) — built, tested, gated

`origin/fable` (55c9ac0c; B967 !74 merged, so the gate's goldens are the
re-blessed ones) was merged into this branch first. The C++ now compiles and
the targeted tests pass (113/113); the local gate reproduces the goldens on
the before side and, on the after side, moves ONLY qeynos2's
`textureCacheBytesUsed` (42,226,984 → 21,932,691), which is re-pinned per
Will's "3) bless". CI pipeline 465 is GREEN on `2590e552` (incl. the gate)
and the Pi A/B sweep (qeynos2 + gfaydark, 51 vs 21 MiB) is done — both pass
with 0 evictions / 0 restore-failures / 0 pinned (the ceiling is a no-op on
those two zones' 14.6 / 11.5 MiB steady footprints; the finding is in
REMAINS 7). The numbered REMAINS below carry the current status.

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

Status 2026-10-03 (kb-b978, hermes-ext): 1-5 and 8 done (see below); 6 via CI
`build-arm`; 7 and 10 pending. `origin/fable` (now 55c9ac0c, with B967 !74
merged) was merged into this branch first; the gate's goldens are the B967
re-bless.

1-4. DONE. Built here (Release, -j3): test targets + `JOBS=3
   bash scripts/build-gles2-x86.sh` (the `--irrlicht-only` bootstrap ran
   first; B959's `eqtSetShaderBinaryCacheDir` symbol present). Targeted ctest
   `MemoryLedger|GpuBufferCensus|ConstrainedRenderer`: 113/113 pass.
   `scripts/ci/test_visual_gate_ledger.py`: 19 checks OK. The full suite +
   hygiene run on CI (card: full suite on CI). The grep check passed: only the
   B955 historical README, this README, the docs/ci.md strike and
   `owned_memory.py`'s back-compat reader still name the removed field.

5. DONE (local gate, `scripts/ci/visual_gate.sh` + the before/after
   measurement in `scripts/b978/texture_cache_ab.py`, evidence
   `~/kb-evidence/b978-owned-gpu-texmem/gate/{before,after}_texture_cache.tsv`;
   the before side is `--texture-memory-bytes 53477376` on THIS branch, which
   is d3375b65's gate behaviour -- the before numbers reproduce the goldens
   exactly, qeynos2 42,226,984 / 126 entries / p95 3.666 ms):

   | zone     | before used | before p95 | after used  | after `[texture-cache]` (entries/resident/evict/restore/restfail/pinned) | after p95 |
   |---|---|---|---|---|---|
   | qeynos2  | 42,226,984 | 3.666 | 21,932,691 | 126 / 65 / 82 / 21 / 0 / 0 | 3.928 |
   | gfaydark | 11,184,800 | 4.540 | 11,184,800 | 32 / 32 / 0 / 0 / 0 / 0 | 4.643 |
   | befallen | 2,883,581 | 5.405 | 2,883,581 | 9 / 9 / 0 / 0 / 0 / 0 | 5.790 |
   | iceclad  | 3,866,619 | 12.991 | 3,866,619 | 15 / 15 / 0 / 0 / 0 / 0 | 11.269 |
   | timorous | 3,183,945 | 17.608 | 3,183,945 | 35 / 35 / 0 / 0 / 0 / 0 | 16.817 |

   All 30 golden frames are 0-pixel-diff on both sides. Only qeynos2 exceeds
   the 21 MiB ceiling. `textureCount` stays 126 (entry count; resident 65).
   `gpuTextureBytes`, `totalEstimate`, `framebufferBytes` and every other
   MEMORY_BASELINE_FIELDS value are unchanged. The gate's only memory failure
   is the in-scope qeynos2 `textureCacheBytesUsed`, re-pinned to 21,932,691
   (measured twice, identical) -- per Will's "3) bless", quoted in the card
   and in `docs/decisions.md`. Local gate run also tripped an iceclad p95
   CPU ceiling (host-load flake; 0 evictions there; CI retries 3x).

6. Local ARM compile skipped (no docker on this host) -- CI `build-arm`
   publishes `ci-arm/<sha>/` for the Pi leg.

7. DONE (Pi, owner `kb-b978`, VM lane `EQEMU_HOST=172.16.200.99`,
   `scripts/pi_rendered_zone_sweep.sh --zones qeynos2,gfaydark
   --allow-vm-lane --pi-dir /mnt/projects/fable/ci-arm/2590e552...`
   (the CI `build-arm` publish), run ON THE PI (hermes-ext has no NFS; the
   sweep's inline summarizer reads `--out` off the sweep host's disk), repo
   staged at `/home/user/kb-b978` from a `git bundle` of this branch; evidence
   rsynced to `~/kb-evidence/b978-owned-gpu-texmem/pi/{zones_a,zones_b}`.
   B side = plain (preset 21 MiB); A side = `--env
   WILLEQ_TEXTURE_MEMORY_BYTES=53477376` (the B944 knob; the A log carries
   `Constrained cache: WILLEQ_TEXTURE_MEMORY_BYTES=53477376 overrides the
   preset's textureMemoryBytes 22020096`). All four rows `pass` (first
   attempt A, re-summarized B after `apt-get install python3-pil` on the Pi
   — the summarizer's blank-frame check needs PIL; the original B rows were
   `render_unproven` only for the missing PIL, frames dumped fine), `mesa=
   nollvm:ok`, frames 3/3, `snapshot restored BYTE-IDENTICALLY`, both locks
   released.

   | zone     | side | verdict | fps_med | TMU used/budget | evict/rest/restorefail/pinned | owned_steady | owned−64 | frame p95 |
   |---|---|---|---|---|---|---|---|---|
   | qeynos2  | A 51 MiB | pass | 30 | 14.7 / 51.0 MB | 0 / 0 / 0 / 0 | 62.23 MiB | −1.77 | 33.9 |
   | qeynos2  | B 21 MiB | pass | 30 | 14.6 / 21.0 MB | 0 / 0 / 0 / 0 | 74.91 MiB | +10.91 | 34.0 |
   | gfaydark | A 51 MiB | pass | 30 | 11.5 / 51.0 MB | 0 / 0 / 0 / 0 | 86.08 MiB | +22.08 | 34.1 |
   | gfaydark | B 21 MiB | pass | 30 | 11.5 / 21.0 MB | 0 / 0 / 0 / 0 | 81.32 MiB | +17.32 | 34.0 |

   **Finding — the 51→21 MiB ceiling is a NO-OP on these two Pi zones.** Their
   steady TMU footprint (14.6 / 11.5 MiB) is under BOTH the 21 and 51 MiB
   ceilings, so 0 evictions / 0 restores / 0 restore-failures / 0 pinned / 0
   placeholder-or-restore WARN on every row. The ledger-attributed steady
   memory is ~equal across the ceiling (qeynos2 48.45 vs 48.22 MiB; gfaydark
   63.31 vs 61.11 MiB), confirming the ceiling does not move owned memory
   here; the ceiling only bites where the x86 gate's qeynos2 footprint
   (42 MiB) exceeds 21 (REMAINS 5). The `owned_steady` column (the new
   `owned = RssAnon − gpu_driver + gpu_buffers` target figure, "1) yes") is
   62–86 MiB, dominated by the ~18 MiB `gpu_driver` category plus the ~27 MiB
   Lima `gpu_buffers` (net of dumped-PNG shmem) that B978 now counts in. The
   A-vs-B spread in that column is the 2 s RssAnon-window sample variance on
   the Pi, not the ceiling (the per-category ledger live-sum is ~equal).
   Frame p95 is within ~0.3 ms A vs B, with one ~55–59 ms load spike on both
   sides (the zone-mesh load, not the texture cache).

8. DONE. The old runs were rsynced from the Pi NFS into
   `~/kb-evidence/b978-owned-gpu-texmem/ref/` (B962: pi_final, pi_fixed,
   pi_legacy, pi_census_{final,fixed,legacy}, pi_before_fable; B955:
   pi_sweep_{1,2}; B964: pi/crowd800_v2) and re-summarized with THIS branch's
   `scripts/pi/rendered_zone_summarize.py` on local symlink farms (the farms'
   `details.json` are new local files; the ref/ copies are untouched):
   `~/kb-evidence/b978-owned-gpu-texmem/resummarized/*.owned.tsv`. New
   per-sweep qeynos2 steady owned=RssAnon-gpu_driver+gpu_buffers (MiB):

   | sweep | owned | owned_rss_anon | gpu_buffers (net) |
   |---|---|---|---|
   | B962 pi_final | 59.99 | 39.00 | 20.99 |
   | B962 pi_fixed | 65.28 | 42.70 | 22.58 |
   | B962 pi_legacy | 79.23 | 43.78 | 35.45 |
   | B962 pi_census_final | 67.08 | 46.33 | 20.75 |
   | B962 pi_census_fixed | 69.27 | 46.74 | 22.53 |
   | B962 pi_census_legacy | 80.92 | 45.59 | 35.32 |
   | B962 pi_before_fable | 77.70 | 42.56 | 35.14 |
   | B955 pi_sweep_1 | 78.99 | 43.55 | 35.44 |
   | B955 pi_sweep_2 | 78.00 | 42.58 | 35.42 |

   Every row satisfies `owned_steady = owned_rss_anon_steady +
   gpu_buffers_net_steady`. gfaydark steady owned: B962 pi_final 77.03 MiB,
   B962 pi_census_final 82.84 MiB, B955 pi_sweep_1 90.72 MiB, B955 pi_sweep_2
   94.96 MiB. Caveat: the resumption farms flag `render_unproven` (the
   blank-frame check cannot read PNGs through the symlink farms); the
   MEMORY columns are taken straight from the runs' `[memledger]` /
   `[memgpu]` lines and are the point of the resumption. The crowd runs
   (`ref/crowd800_v2`) were NOT re-summarized: `scripts/b964/crowd_table.py`
   expects the B964 crowd rig's dir layout, not the sweep layout the
   committed crowd800_v2 has, and the crowd rows' owned figures predate the
   new rule -- flagged, not forced.

9. DONE (docs): `docs/decisions.md` — two dated entries at the END (newest
   last): "2026-10-01 — GPU buffers count toward the 64 MB target (Will;
   B978)" quoting **"1) yes"**, and "2026-10-01 — textureMemoryBytes 51 -> 21
   MiB (Will, "3) bless"; B978)" with the derivation table and the qeynos2
   `textureCacheBytesUsed` re-pin noted; B933's 2026-09-29 entry now says its
   `owned=RssAnon-gpu_driver` figure was superseded as the target figure.
   `docs/ci.md`'s B955 paragraph and `refactor/BOARD.md`'s B943 line are
   struck to the new rule. The two `CLAUDE.md` bullets (root "Memory
   accounting" + `src/client/graphics/CLAUDE.md`) were NOT updated: the
   agent-instruction-file write guard blocked them on this headless worker,
   and `DocClaims`/`FileSizeCeiling` pass with the stale text, so it is a
   docs-freshness gap flagged in the MR, not a CI block.

10. DONE except the MR close-out. Committed the x86 leg (1-5, 8, 9) at
    2590e552, pushed; CI pipeline 465 SUCCESS on `2590e5521bd0bf4a1560f3d115fbc37a24a76163`
    (hygiene 57 s, build 816 s, test 161 s, gate 274 s, build-arm 351 s — the
    gate's only memory delta is the in-scope qeynos2 `textureCacheBytesUsed`
    re-pin, 0 pixel mismatch; the iceclad p95 CPU flake did not recur on CI).
    The Pi leg (7) is done as above; this commit records it. Left: the
    CLAUDE.md doc-freshness gap (flagged in the MR), `glab mr update 76
    --ready` + `Closes #296` + #296 -> `status::review`.
