# B1036 — Rock64 Lima GEM owner census (#355)

The owner census is under
`/home/user/kb-evidence/kb-b1036-lane2-gem-census/`. The complete six-column
ready/`steady_60s` owner table is `owner_table.md`; each cell comes from
`census_retry/zones/<zone>/gem_owners.tsv` and the corresponding `client.log`
`[memgpu]` point. `summarize_census.py` regenerates the table from those raw
files. The original connected-display result is `baseline/results.tsv`, and
the diagnostic sweep result is `census_retry/results.tsv`. No numbers below
are a claimed passing sweep: all three diagnostic rows failed the existing
`gpu_unnamed_over_bound` check.

## Capture and attribution

The aarch64 shim was compiled by `docker/Dockerfile.arm64-noble` from
`scripts/pi/lima_bo_census.c` and shipped as `bin/lima_bo_census.so` with the
`31dd7b6baeb32333bc79d527738afa2b433880cd` ARM build. The merged head
(`6573a1507393cba8928da866c3ec9830091c57fb`, MR !245's CI-run pipeline)
rebuilds and republishes it at the merged sha; the capture evidence below is
from the pre-merge head and does not change in the merge. The three-zone
rendered client ran on lane 2 with `--constrained orangepi`, system Mesa
25.2.8, an own cgroup, and the B962 `ready` and `steady_60s` points.
`census_retry/sweep.log` records both locks, the staged binary checksum,
snapshot and restore; `census_retry/binary_identity.txt` is the identity
record. The diagnostic shim requires an uncapped client, so the rig raised
`net.core.rmem_max` for the run and restored it in cleanup.

The original connected-display baseline requested 1280×720 and reported
17.23 MiB unnamed in qeynos2 (`baseline/results.tsv`). The KVM was
disconnected for this census: `pi_hdmi_force_on` offered 1024×768, despite
the rig requesting 1280×720 (`census_retry2_job/job.log` and
`census_retry/zones/*/client.log`). This is an explicit resolution deviation;
the before/after figures are not a controlled same-mode comparison.

| Zone | Original unnamed | Census unnamed | Scanout in census | Census unnamed minus scanout and extra EGL colour |
|---|---:|---:|---:|---:|
| qeynos2 | 17.23 MiB | 19.36 MiB | 9.00 MiB | 6.36 MiB |
| gfaydark | not captured in original baseline | 19.67 MiB | 9.00 MiB | 6.67 MiB |
| growthplane | not captured in original baseline | 18.49 MiB | 9.00 MiB | 5.49 MiB |

The last column is an attribution, **not a changed gate result**. The full
owner table shows three 1024×768 `egl:device` regular BOs totalling 7.00 MiB
at both points in every zone; the logical depth/stencil tally names only
3.00 MiB. The 4.00 MiB difference is the EGL window-surface colour BOs.
`gem_owner_table.py` now labels the whole owner as colour plus depth/stencil;
the earlier depth-only label was false on this lane. The `lima` draw-time
and Mesa stream-uploader rows are the remaining driver owners, explained by
facts 3, 5 and 13 of `docs/reference/lima_cost_sheet.md`.

The old shim leaves four 4 MiB BOs as `untagged (scope textures_driver)` at
both poses. The same run's `client.log` records four 4,194,304-byte impostor
page allocations; `ImpostorCache::prepare()` eagerly creates those pages and
`ensurePage()` calls `glTexImage2D`. The current source gives that allocation
the `tex:impostor-page` tag and folds it into an explicit owner row. This is a
diagnostic naming change; the renderer's `logical_textures` already includes
the wrapped page textures at `steady_60s`, so subtracting the 16 MiB a second
time would be wrong. The `untagged (scope irrlicht_scene)` row is the raw-GL
draw path B962 already recorded, not the Rock64-only gap.

## Mechanism and checked prediction

The Rock64's display scanout is charged to the process's `memcg_shmem`
`gpu_buffers`, unlike the Orange Pi's CMA-backed scanout (B961, documented in
`include/client/core/gpu_buffer_census.h`). This is inferred from the device
measurements, not from a claim that the two kernels have identical GEM code.
At `ready`, the Rock64's `gpu_buffers` minus every regular Lima BO is
9.06 MiB in qeynos2 and gfaydark, against 9.00 MiB of reported scanout.
At `steady_60s`, the residual minus scanout and the rig's PNG frame dumps is
0.07 MiB in both; growthplane's remainder is 0.75 MiB. Thus the predicted
scanout-sized cgroup residual is present at both poses and in three zones.
The original connected-display run had 7.03 MiB of scanout; the forced-mode
run has 9.00 MiB, and its qeynos2 unnamed figure moved from 17.23 to
19.36 MiB. The 1.97 MiB scanout difference numerically tracks the
2.13 MiB unnamed difference, but these were not same-mode/same-head runs.
The other newly named 4 MiB of EGL colour BOs
are present at both poses in all three zones; subtracting both owners puts
each diagnostic row below 10 MiB without moving the bound. The precise
kernel allocation path for scanout is not proven by this userspace census.

## RULING REQUEST: class C

Approve a platform-aware ledger decomposition that counts scanout as a
`gpu_buffers` constituent on Rock64 only when its cgroup charge is proven,
and names EGL colour BOs separately from default-framebuffer depth/stencil.
Do not add Rock64 scanout to `owned` a second time, and do not subtract it
from the Pi's `gpu_buffers`: its scanout lives outside that row. The current
10 MiB unnamed bound, preset, budgets and visual goldens stay unchanged.
The owner census establishes the two missing names and their sizes but cannot
make a portable automatic inside/outside decision for future kernel/display
configurations, so the failed sweep is preserved pending this ruling.

## Not verified

- The three-zone diagnostic capture used the pre-tag `31dd7b6b` binary. A
  one-zone published-head capture (`census_final_tag/zones/qeynos2/gem_owners.tsv`)
  confirms `tex:impostor-page` at both poses, but the tagged binary has not
  repeated the full three-zone owner table.
- The connected 1280×720 baseline lacks a tagged owner dump. Its 17.23 MiB
  attribution is inferred from the same lane's forced-mode owner table and
  the separately measured scanout-size change, not a byte-identical replay.
- No code has made `gpu_unnamed_over_bound` pass. The table's post-attribution
  column is a checked prediction, not an assertion from the current gate.
