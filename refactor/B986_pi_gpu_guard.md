# B986 — Pi unnamed-GPU guard investigation

## Superseded 2026-10-05 (B988, issue #307)

Will, 2026-10-05, verbatim: **"make the nobocache flag opt-in (off by default)
and fix the stale docs"**.

B986 merged into `fable` with B975 (MR !73, `ee538d2c`, 2026-10-05) and made
every DRM client append `nobocache` to `LIMA_DEBUG`. A same-binary Orange Pi
A/B the same day measured what that costs
(`/mnt/projects/fable/review/CODEX-AUDIT-1005/README.md`: the pre-B986 ARM
binary `e209d3ee` with and without `--env LIMA_DEBUG=nobocache`, plus fable
`ee538d2c` as shipped; 800x600, orangepi preset, shipped LLVM-less Mesa,
character standing at the safe point; ms from a `/frametiming` tour, memory at
`steady_60s`):

| zone | leg | scene draw ms | total ms | owned MiB | gpu_buffers MiB | unnamed MiB |
|---|---|---|---|---|---|---|
| gfaydark | old, cache on | 7.7 | 28.8 | 81.6 | 22.6 | 7.5 |
| gfaydark | old, nobocache | 15.5 | 28.9 | 78.2 | 19.2 | 4.1 |
| gfaydark | fable shipped | 14.9 | 28.6 | 79.2 | 19.7 | 4.4 |
| qeynos2 | old, cache on | 3.3 | 7.8 | 66.2 | 23.2 | 5.7 |
| qeynos2 | old, nobocache | 6.0 | 12.7 | 64.7 | 21.3 | 3.7 |
| qeynos2 | fable shipped | 6.0 | 12.5 | 63.9 | 21.1 | 3.5 |

Standing still the cache costs 1.5–3.5 MiB of owned memory and buys a 2x
cheaper scene draw. Codex's own crowd tours point the same way, though they
are NOT a single-variable A/B (client `c778d23d` cache on,
`refactor/B975/evidence-20261003/pi-crowd.md`, vs B975 candidate `850b679c`
under `nobocache`, `refactor/B975/evidence-run281/pi-crowd.md`; idle FPS from
the client lines, owned at `steady_60s`):

| zone / variant | idle FPS, cache on -> nobocache | owned MiB, cache on -> nobocache |
|---|---|---|
| gfaydark / imp0 (normal population) | 41 -> 24 | 90.11 -> 79.89 |
| gfaydark / imp200far (200-NPC crowd, batch) | 26 -> 11 | 146.59 -> 126.64 |
| gfaydark / tagnode200far (crowd, per-node) | 16 -> 8 | 145.02 -> 128.63 |
| qeynos2 / imp200far | 31 -> 24 | 123.53 -> 114.42 |
| qeynos2 / tagnode200far | 20 -> 13 | 131.23 -> 117.48 |

So `nobocache` saved 9–20 MiB of owned memory in the 200-NPC crowds and cost
up to half the frame rate.

**New default (B988):** a DRM client leaves `LIMA_DEBUG` untouched, so Lima's
BO cache is ON. **Opt-in:** `WILLEQ_LIMA_NOBOCACHE=1` restores the policy below
exactly (append `nobocache`, keep the caller's other flags, idempotent, `all`
respected, before `createDeviceEx`). A caller's own `LIMA_DEBUG=nobocache`
still works without the switch. Renderer init logs one `[GL] Lima buffer
policy:` INFO line naming the effective policy. `nametag_drawcost` calls the
same `configureLimaBufferPolicy()`, so it measures the client's default (cache
on) unless the switch is set. Code: `include/client/graphics/lima_buffer_policy.h`;
tests: `tests/test_lima_buffer_policy.cpp`.

**Consequence, stated plainly.** With the cache on, Codex's 2026-10-03 Pi
matrix exceeded the 10 MiB unnamed-GPU guard in **6 of 8** crowd/zone-hop
cases (`refactor/B975/evidence-20261003/pi-verification.csv`: both 200-NPC
crowds in qeynos2 and gfaydark with either renderer, and gfaydark's
normal-population pair at 12.24 / 11.80 MiB). The guard is expected to read
red again on those tours until **B972 (#290)** removes the retained Mesa
stream-upload / Lima draw-time buffers at their source. **The guard number
(`GPU_UNNAMED_BOUND`, 10 MiB) is NOT changed.**

The text below is the original B986 record, kept as written.

---

Assigned contract: kanban `t_d79d91ae`, GitLab issue #305. Stacked branch
`task/B986-pi-gpu-guard`, based on B975 at
`b0bb555ee8d2cdc8f25cb16202a8237e2d237eeb`. No merge is authorized.

## Scope and verification contract

Attribute the existing unnamed-GPU guard failures using the real Pi GEM
census before selecting a fix. Reproduce Qeynos2 and Greater Faydark at
800x600, normal population and 200-NPC far crowds, with both batched and
per-node name tags. Preserve the existing memory, ready, steady and no-OOM
criteria and require the final visit to the requested zone.

Baseline client source: `c778d23dab9024a55c983c62bf61169453f2a2bb`,
pipeline 483. The immutable capture harness is separately pinned at
`b0bb555ee8d2cdc8f25cb16202a8237e2d237eeb` under
`/home/user/src/codex-executor/rig/willeq-b0bb555e`.
Run evidence: `/home/user/kb-evidence/b986-pi-gpu-guard/run-277/`.

Investigation territory: GEM census tools, GPU storage/tally implementation,
and allocation sites identified by the census, with focused regression tests.
Do not change unrelated name-tag behaviour. Narrow the implementation scope
from measured allocation evidence before modifying production code.

Use CI for client builds, explicitly transfer the selected ARM publication,
and retain client/harness/shim hashes. Captures use the detached executor
guardian and existing Pi/EQEmu locks. Preserve raw failures. Restore character
snapshots byte-identically with the world down, stop the stack and release
only owned locks. Never edit a running script.

## Stop conditions

Missing required services or credentials, unexplained attribution, a required
acceptance failure, or a needed threshold/golden/baseline change prevents
completion. No threshold, golden or baseline change is authorized. A failed
check is not excused by a successful process exit or green CI.

## Delivery

Document allocation cause and regression coverage. All eight Pi matrix entries
must pass; hygiene, build, test, gate and build-arm must pass at the exact
pushed HEAD. Retain raw evidence, identity, restoration comparisons and cleanup
proof. Publish a draft MR targeting `task/B975-name-tag-batch`; never merge.
B975 must independently rerun acceptance after integrating the reviewed fix.

## Investigation status

## Allocation cause and selected fix

The Greater Faydark 200-NPC far crowd reproduces the guard failure with the
baseline client. Its final-visit `steady_60s` census and the otherwise identical
`LIMA_DEBUG=nobocache` diagnostic identify Mesa/Lima's retained freed BOs as the
excess. Raw evidence remains under the run-277 and run-278 directories in
`/home/user/kb-evidence/b986-pi-gpu-guard/`. The diagnostic source and shim
hashes are retained separately from the client and harness hashes.

| final-visit measurement | default cache (run-277) | no cache (run-278) |
| --- | ---: | ---: |
| unnamed GEM, bytes | 26024977 | 6872161 |
| regular GEM BO bytes | 51351552 | 31948800 |
| stream uploader BOs, bytes | 12582912 | 1048576 |
| draw-time BOs, bytes | 10018816 | 741376 |
| logical texture storage, bytes | 23563320 | 23493488 |

Sources: each run's `census-gfaydark-imp200far[-nobocache]/capture/results.tsv`,
`capture/zones/gfaydark/gem_owners.tsv`, and `client.log`; compact folds in
`run-278/baseline-owner-table.txt` and `nobocache-owner-table.txt`.
The census tags identify the BO's original allocation, not its later reuse
inside Mesa. The switch comparison, not a tag alone, establishes retention.
Heap maximum sizes are never counted as committed pages. The residual between
regular BOs and cgroup shmem remains explicitly reported.

Mesa 25.2.8 `src/gallium/drivers/lima/lima_bo.c` retains cacheable BOs after
their refcount reaches zero. Stale eviction happens on cache insertion after
more than six seconds idle, and reuse may retain oversized buffers. The
`nobocache` flag makes new BOs noncacheable and releases them on last unref.
The versioned source copies are in the evidence directories. This is not a
texture-tally adjustment or a B972 assumption.

`configureLimaBufferPolicy()` adds `nobocache` to `LIMA_DEBUG` for DRM clients
before `createDeviceEx` (before GBM/EGL can initialize Lima). Non-DRM clients
are unchanged, other debug flags are retained, and no driver binary, threshold,
golden or baseline changes. `test_lima_buffer_policy` checks the environment
policy, idempotence and initialization order. Real-Pi acceptance must exercise
the compiled default without an external `LIMA_DEBUG` override or census shim.

The diagnostic trades throughput for memory (captured fps medians: 26.0 vs
16.5). Those shim-instrumented runs are attribution evidence, not the final
performance comparison. Will's memory-first ruling remains in force.

## Recovery and verification status

Run-277's interrupted no-cache attempt ended before the snapshot/character
mutation phase. The unchanged harness's control flow, missing snapshot files,
and startup-to-cleanup trace prove that distinction. A phase-aware executor
cleanup verifier with regression coverage enabled `executor job-recover`;
the original exit 130 and owner-lost failure remain preserved. See
`run-278/recovery/README.md`; no snapshot was invented and the pinned rig was
not edited. The fresh diagnostic restored its snapshot byte-identically,
stopped the stack/client and released both locks, verified by its guardian.

The parent task's run-278 handoff records passing CI and real-Pi acceptance
at `fd8b002b2a3ecb4f1b7d1a896cad20b9812f4d5d`. B975 run 279 reviewed that fix
and independently repeated the complete Pi matrix with passing memory,
final-visit and no-OOM checks and byte-identical snapshot restoration.
Evidence: `~/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/`
(`matrix-verification.json`, `pi-verification.csv`, and raw `matrix/capture/`).
This supersedes the pending status from the initial investigation commit.

*(2026-10-05: no longer current -- B975 later met its CPU bar under this
policy and merged with B986 as MR !73, `ee538d2c`; B988 then made the policy
opt-in. The paragraph is kept as written.)*
B975 remains blocked for a different, measured reason: its standalone timing
tool omitted this production policy. Applying it makes submission alone
exceed B975's CPU bar; the tool now uses the same policy as the client.
See `refactor/B975/README.md`. The memory fix and its thresholds remain intact;
neither this integration nor a green pipeline is B975 performance acceptance.
