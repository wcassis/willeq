# B961 (GitLab #279): the scanout framebuffer and the cgroup kernel bytes on the client's own ledger

The issue's two byte classes lived on no ledger row:

1. **The scanout colour buffers.** The device's GBM surface allocates its
   colour buffers on the DISPLAY device (CMA on the Orange Pi's sun4i-drm;
   the Lima render driver imports them by PRIME). They are not shmem, not in
   RssAnon, and not in the process's memcg, so the `gpu_buffers` row and
   every per-process kernel source were blind to them. The rigs read them as
   root from `/sys/kernel/debug/dri/display-engine/framebuffer`
   (`scripts/pi/proc_mem_snapshot.py` `gpu.scanout_fb_bytes`,
   `rendered_zone_summarize.py` `scanout_fb_bytes`).
2. **The process cgroup's `memory.stat kernel` bytes** (slab, page tables,
   kernel stacks). Printed beside the row (`[memgpu] memcg_kernel=`) since
   B955 but on no ledger row and in no owned figure.

Measured by B955 in the issue body: 2 x 3,686,400 B = 7.03 MiB scanout at
1280x720 (qeynos2/gfaydark/growthplane); 1.11-1.93 MiB memcg kernel at
steady_60s.

## The fix

### The client-side source for the scanout (item 1)

The device knows every `gbm_bo` it has locked, and a bo's backing store is
`stride x height` — so the device itself is the client-side source; no root,
no debugfs.

- `docker/irrlicht-drm/scanout_buffer_tally.h` — the tally: one fixed table
  (8 slots; the GBM pool is three), a CAS spinlock, and a published
  `(bufs, bytes)` pair the ledger reads from another thread. Header-only;
  `tests/test_scanout_buffer_tally.cpp` exercises it on fake bos.
- Both device forks (`docker/irrlicht-drm/CIrrDeviceFB.cpp`,
  `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp`): `scanoutTally_` member;
  `registerBo(stride, height)` at the first lock (`getOrCreateFb`, the one
  place a bo meets the DRM framebuffer); the `gbm_bo_set_user_data` destroy
  callback is the single removal point; constructor publishes the device
  pointer before the first lock, destructor clears the tally (and publishes
  `{0, 0}`) and the pointer after the surface is gone.
- **Resolution: the B955 publish idiom.** The core owns the tally
  (two atomics, `{0, 0}`-initialised) in
  `src/client/core/gpu_buffer_census.cpp`; the device's TUs declare the
  core's `eqt::memory::publishScanoutTally(bufs, bytes)` (a strong
  device->core reference -- the direction that binds across the two static
  archives, `libwilleq_core.a` linked before `libIrrlicht.a`) and call it
  from the register/unregister callbacks and, with `{0, 0}`, from the
  destructor. The core's read is a pure atomic load. A binary without the
  device (tests, narrow tools) never publishes, so it reads the
  `{0, 0}` default: the true answer of "no GBM buffers in this process".
  (First draft: a weak DEFINITION of the getter -- the linker resolved it to
  the core's own `{0,0}` body before it ever looked in the later
  `libIrrlicht.a`; second draft: a WEAKREF, which also did not bind across
  the archives. Both are dead ends with static archives in this order.)

### The two rows (item 2)

Reported, in one derivation site (`b961Rows()` in the header, used by both
emitters). **Neither is added to `owned`** — the counting question below is
Will's ruling, and this MR changes no budget, ratchet, owned-formula constant
or gate value.

`[memgpu]` line (inserted after `fdinfo_purgeable=`, BEFORE the B978 owned
fields, which stay last for the pre-B961 parsers — `scripts/ci/
owned_memory.py` `owned_from_memgpu`, `scripts/pi/rendered_zone_summarize.py`):

```
scanout_fb=<B> scanout_fb_bufs=<n> memcg_kernel=<B> memcg_kernel_seen=<0|1>
```

Ledger JSON (`gpuBuffersToJson`): `scanoutFbBytes`, `scanoutFbBufs`,
`memcgKernelBytes` (was already there since B955), `memcgKernelSeen`.

`memcg_kernel_seen` distinguishes "the cgroup's memory.stat had a kernel=
line" from "the source was absent" (no cgroup v2 memory controller): an
absent source reads 0 and `seen=0`, not a guessed 0.

## Regression coverage (item 3)

- `tests/test_gpu_buffer_census.cpp`:
  - `B961RowsReportTheTwoUnrowedByteClasses` — the Pi's steady_60s numbers
    (2 x 3,686,400 B) ride the log line in order (after `fdinfo_purgeable`,
    before `owned_rss_anon`), the row values are REPORT not COUNT
    (`owned` unchanged), and the JSON carries both fields.
  - `B961KernelAbsentIsReportedAbsent` — no `kernel=` line: 0 bytes,
    `kernelSeen=false`.
  - `B961PublishRoundTripAndEmptyDefault` — the publish round-trip: what
    `publishScanoutTally` last stored is what `readScanoutTally` returns,
    and a deviceless binary (this test binary) reads the `{0, 0}` default.
- `tests/test_scanout_buffer_tally.cpp` — register/accumulate, no double
  count on re-lock, size update, destroy removes only its own bo, clear,
  null bo, the published pair.
- x86: rows present, values whatever the local DRM/none path yields — the
  dev-VM path has no GBM device, so `scanout_fb=0 scanout_fb_bufs=0`;
  `memcg_kernel` is whatever the dev-VM's own cgroup memory controller
  reports. No Pi needed for x86 green.

## x86 vs Pi (item 4)

| row | x86 (surfaceless/surfaceless-fallback) | Pi (DRM/GBM/EGL, 1280x720) |
|---|---|---|
| `scanout_fb` | 0 (no GBM device -- never published) | ~7.03 MiB (2 x 3,686,400 B), cross-checked against the rigs' root read `gpu.scanout_fb_bytes` |
| `scanout_fb_bufs` | 0 | 2 (the GBM pool locks two at steady state; three at most after a flip-event loss) |
| `memcg_kernel` | the dev-VM cgroup's kernel (the row is whatever the source says) | 1.11-1.93 MiB band (issue body, steady_60s) |
| `memcg_kernel_seen` | 1 when the dev-VM cgroup has a memory controller | 1 |

Pi-leg measurements: <TO BE FILLED from the rendered-zone sweep: scanout_fb
vs the rigs' gpu.scanout_fb_bytes, memcg_kernel vs the band, file:line of
each>

## Left open for Will — THE COUNTING QUESTION

Do `scanout_fb_bytes` and `memcg_kernel_bytes` count toward the 64 MiB owned
target (Will's ruling: owned = RssAnon − gpu_driver + gpu_buffers, judged on
willeq's own memory), or are they listed-but-excluded like `gpu_driver`?

**The mechanisms.**
- `scanout_fb`: CMA on the DISPLAY device. Not in RssAnon (the pages belong
  to the kernel's CMA allocator, not to the process's address space), not
  shmem, not in the process memcg. But the BYTES ARE WILLEQ'S: the process
  asked the display device for exactly these buffers, owns them through its
  GBM surface, and the buffer count is bounded by the surface (three).
- `memcg_kernel`: kernel memory (slab, page tables, kernel stacks) the
  kernel charges to the process's own cgroup. Not in RssAnon (kernel memory
  is not a process's), but it exists because this process exists — it is
  willeq's share of the kernel, charged to willeq's cgroup.

**The prediction each choice makes for the Pi owned figure** (steady_60s,
1280x720; the issue's measured rows):
- **Exclude both (the status quo this MR ships):** owned is unchanged.
  Both rows are reported beside it, the way `gpu_driver` is listed but
  excluded.
- **Count both:** owned rises by scanout (7.03 MiB) + memcg kernel
  (1.11-1.93 MiB) = +8.14 to +8.96 MiB on top of the current owned figure.
- **Count scanout only:** +7.03 MiB.
- **Count memcg kernel only:** +1.11 to +1.93 MiB.

A one-line ruling ("count both" / "exclude both" / either one) settles it;
the follow-up MR moves the row into the owned formula and, if counting,
decides the gate's budget treatment.
