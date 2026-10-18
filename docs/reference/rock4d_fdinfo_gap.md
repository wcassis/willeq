# The lane-3 (ROCK 4D) gpu_buffers gap: the scanout framebuffers (B1035, #354)

Captured 2026-10-18 on the published aarch64 build `bfcf50ea573a0cee598acd44fec1b47f4d9f9f67`,
`PI_LANE=3` (ROCK 4D, kernel 7.1.8-edge-rockchip64, Mesa 26.0.8 system, Panfrost,
Mali-G52), gate zone `qeynos2`, the client in its OWN cgroup
(`user.slice/.../willeq-run-*.scope`, `cgroup.procs` = 1 process), the rendered
sweep's `steady_60s` window. Raw captures (per-second, one directory per second
of client life, 59 client-seconds): `/home/user/kb-evidence/kb-b1035-lane3-fdinfo-gap/raw/rawNNN/`;
the table below is `raw030` (2026-10-18 22:02:23Z, steady). The in-tree fixtures
under `tests/data/b955/rock4d_*` are byte copies of the `raw030` files.

## The mechanism

The `gpu_buffers` row is the panfrost render node's `drm-resident-memory`. The
exclusive cgroup's `memory.stat shmem` is HIGHER by the process's SCANOUT
FRAMEBUFFERS: they are shmem-backed DRM objects charged to the process cgroup
as shmem, but

- they are **not** in any node's `drm-resident-memory`: the panfrost render
  node counts them as `drm-shared-memory` (exactly 3 x 3,170,304 = 9,510,912 B
  at steady), and the rockchip **card** node (`/dev/dri/card0`) publishes **NO
  memory key at all** -- its fdinfo is `pos`/`flags`/`mnt_id`/`ino` only. The
  issue's hypothesis ("count under the rockchip-drm node's fdinfo") is the part
  that does not hold: that node's `show_fdinfo` never printed memory stats in
  this kernel, so no second node's fdinfo can recover them;
- they are not in the process's `smaps` (Shmem: 0 -- the process maps them for
  page-flipping through the GBM lock, but the kernel does not count the
  mappings as resident shmem here; the charge is on the GEM object).

The framebuffers are `imported=no` in the card node's debugfs `framebuffer`
dump (`allocated by = eq-render`), i.e. the client's own GBM colour buffers
(3 x 1024x768 XR24, pitch 4096, `size=3170304` each) bound as display
framebuffers -- the client-side tally is `scanout_fb=9,437,184` (stride x
height) on the `[memgpu]` line.

## The four-source table (steady_60s, raw030)

| source | file (raw030) | value |
|---|---|---|
| (a) panfrost render node fdinfo | `fdinfo_7_renderD128.raw` | resident 30,810,112; shared 9,510,912; total 172,441,600; purgeable 1,835,008 |
| (b) rockchip card node fdinfo | `fdinfo_4_card0.raw` (identical for fds 4,5,6) | no `drm-*` memory key at all |
| (c) exclusive memcg | `memory.stat.raw` | shmem 39,841,792; anon 93,126,656; file 39,841,792 (`memory.current` 136,572,928) |
| (d) smaps | `smaps.raw` | Shmem across all mappings: 0 |

Reconciliation: resident (30,810,112) + shared (9,510,912) = 40,321,024 vs
memcg shmem 39,841,792 -- within the 1 s capture window (the render node's
`total`/`resident` move a few hundred KiB second to second; the `raw030`
neighbourhood straddles a 73 KiB resident step). The gap
39,841,792 − 30,810,112 = 9,031,680 brackets the scanout tally 9,437,184.

At raw002 (22:01:41Z) one framebuffer is already bound (shared 3,170,304) and
the gap is 1,724,416; the run's first `[memgpu]` line (the startup point,
22:01:42.3Z) still reads `scanout_fb=0`. By the login_world point (22:01:43.6Z)
all three framebuffers are bound (`scanout_fb=9,437,184`) and the gap has
opened to ~8 MB (raw003 and later).

## What the ledger does (the fix)

`decideGpuBuffers()` (B1035): when a resident figure exists (B1032) the row is
it -- UNLESS the cgroup is EXCLUSIVE and its shmem is higher, in which case the
shmem (the higher measurement of the same bytes; it is the next-best source in
the ranking) becomes the row and the delta is named
`fdinfo_scanout_gap=<B>` on the `[memgpu]` line (and
`fdinfoScanoutGapBytes` in the JSON), after `memcg_kernel_seen=` and before the
B978 `owned=` fields (which stay last for the pre-B961 parsers). The `owned=`
arithmetic is unchanged in meaning (B978: `gpu_buffers` counts toward the
64 MB target only when outside RssAnon -- still true here, the shmem bytes
were never in RssAnon).

Lanes unaffected: the Lima lanes (Orange Pi, Rock64) publish no `drm-*` key
and take the shmem as the row already (the `else if (cgroupExclusive)` branch);
a Panfrost run whose resident figure leads keeps the resident row and reports
`fdinfo_scanout_gap=0`.

The `scanout_fb=`/`scanout_fb_bufs=` pair (B961) carries the device's own
tally of the same bytes either way; its header comment's "not in the
process's memcg" claim was the Orange-Pi (CMA) fact and is now scoped to the
Lima lanes (B1035).

After-fix table: the fix leg re-measures the lane-3 `[memgpu]` steady line at
the fix head once published (FOLLOW-UP on the job, not a gate), against the
same table's run.
