# ODROID XU4: Panfrost at full clock (B1045, #365)

The ODROID XU4 (Exynos 5422, Mali-T628 MP6) is Will's interactive willeq
play-test box, not a lane. Under mainline Panfrost its GPU runs at **24 MHz**
unless something re-selects the PLL path after every GPU power-on; the full
story and the measurements are in `docs/reference/odroid_xu4.md`. This
directory holds the fix and the tools that found it.

| file | what |
|---|---|
| `panfrost-xu4-g3d-mux.patch` | the fix: `panfrost_gpu_power_on()` sets CMU_TOP `SRC_TOP5` bit 16 (`mout_user_aclk_g3d` = `mout_sw_aclk_g3d`) on every power-on, Exynos 5420-family only |
| `build-panfrost-modules.sh` | fetches the upstream stable Panfrost + GPU-scheduler sources for the running kernel, applies the patch, builds both out of tree against the Armbian headers, installs under `/lib/modules/<ver>/extra` plus the boot files below |
| `Makefile.panfrost`, `Makefile.gpu-sched` | the out-of-tree kbuild wrappers the build script drops into the source dirs |
| `xu4-panfrost.conf` | `/etc/modules-load.d/`: `drm_shmem_helper`, `gpu-sched`, `panfrost` (Armbian 26.8.3's kernel has no Panfrost of its own) |
| `xu4-g3d-clock.sh` + `.service` | belt-and-braces: after Panfrost probes, set the same bit from userspace via `/dev/mem` and log the before/after word. The module hook is what keeps the GPU fast; this proves the register at boot. |
| `g3d_mux.py` | read (or `set_user_g3d_sw`) the G3D clock registers from `/dev/mem`: the three muxes, the divider, VPLL lock/M/P/S |
| `gpu_job_trace.sh` | `gpu_job_trace.sh <label> <glmark2-es2-drm args>`: per-job-slot GPU time from the kernel's `dma_fence` tracepoints. `panfrost-js-0` is the fragment slot, `js-1` vertex/tiler. 60 ms per fragment job at any resolution is the 24 MHz signature; 2.8 ms is healthy |

Rebuild after a kernel upgrade: `bash scripts/sbc/xu4/build-panfrost-modules.sh` on the board (the sources
are pinned to `uname -r`'s upstream version). Verify: `sudo dmesg | grep "G3D mux"` shows "armed" at probe and
`glmark2-es2-drm --off-screen -b build` is in the hundreds of fps, not 15.
