# B986 — Pi unnamed-GPU guard investigation

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

B975 remains blocked for a different, measured reason: its standalone timing
tool omitted this production policy. Applying it makes submission alone
exceed B975's CPU bar; the tool now uses the same policy as the client.
See `refactor/B975/README.md`. The memory fix and its thresholds remain intact;
neither this integration nor a green pipeline is B975 performance acceptance.
