# B1044 macOS graphics parity — implementation evidence

Evidence root: `/home/user/kb-evidence/b1044-macos-fix/`. The 15 captured
Darwin PNGs, benchmark CSV/JSON, logs and SHA256SUMS are in `goldens_mac/`.
They are proposed evidence, **not blessed goldens**. The executable used for
the capture is the M4 mini's `~/willeq-probe/build-gles2/bin/gles2_zone_benchmark`
from the successful `~/b1044-build.log` run (exit file `~/b1044-build.exit`
was `0`); the source was rsynced from this branch. `capture.log` is the gate's
stdout. `frame-comparison.csv` is recomputed by `analyze_capture.py` from the
real PNGs, CSVs and the committed x86 manifest. Every number below cites one
of those files; no Mac golden or budget was changed.

## Per-step status

| step | status and evidence |
|---|---|
| 1 Cocoa window and input | **Blocked for proof**: `ssh willeq@m4mini.goldfarm.local 'who; launchctl print gui/$(id -u) ...'` reported `will console` and `NO_GUI` for `willeq`. The on-screen window/input proof cannot run as this user. No `will` login was used. |
| 2 GLES2 tree/tests | ANGLE-backed EGL/GLES2 hwcaps discovery and `model_viewer` linkage are patched. The full Mac build is **red** on `test_placed_object_record`'s pinned ABI layout (`160` vs `168`; `mac-census-final.log`). That pin is bless-only, so it was not changed. The partial suite returned exit 8: 19 failed/not-run of 5627, with 167 skips (`mac-ctest-partial.log`); it is not a full-suite PASS. |
| 3 frameworks | Obsolete OpenGL, Carbon and IOKit framework links removed; Cocoa and ANGLE EGL/GLESv2 remain in `CMakeLists.txt`. |
| 4 headers | The five constrained texture-cache TUs now include GLES2 headers on every platform. Linux CI and ARM job are not yet verified. |
| 5 etc2comp | CMake FetchContent patches etc2comp's minimum version locally; `scripts/build-gles2-darwin.sh` no longer passes the global policy-minimum flag. The M4 configure and benchmark build returned 0 (`~/b1044-build.log`, `.exit`). |
| 6 source list/stamp/log | Both host build scripts source `scripts/ci/irrlicht_gles2_files.sh`; the stamp includes Darwin inputs. Darwin EGL messages say ANGLE/Metal. |
| 7 texture cache limit | No code or manifest change: `tools/hwcaps/zone_bench_loader.cpp:299` constructs `ConstrainedRenderingPreset::OrangePi`, whose `textureMemoryBytes` is 21 MiB (`src/client/graphics/constrained_renderer_config.cpp:294`), then `ConstrainedTextureCache::getMemoryLimit()` supplies the JSON (`tools/hwcaps/zone_bench_memory.cpp:148`). The five Mac coverage JSONs each report `22020096`. The x86 manifest's golden-image memory blocks still say `67108864` (e.g. `tests/goldens/manifest.json` qeynos2 memory), a stale **bless value**, not a host-memory probe. `FOLLOW-UP:` Will's manifest bless must decide that value; this card does not edit it. |
| 8 memory | Darwin's malloc-zone backend compiles ON and all five benchmark JSONs report `ledgerEnabled=true`; `task_info(TASK_VM_INFO)` reports point-in-time `physFootprintBytes`. The ledger does **not** reconcile with footprint (see ruling request). |
| 9 visual gate | All five zones and five memory-coverage legs ran. Capture exit **1**, not PASS: timorous frame 0 had draws `579` vs manifest `586` and polys `11114` vs `11258` (`capture.log` and `frame-comparison.csv`). The other 14 frames' draws/state/polys match; the images are captured, not blessed. |

## Five-zone measurements

Each metric fraction is the number of designated frames with all three
draw/state/poly fields equal to the x86 manifest. Diff percent and PSNR are
against the x86 PNGs, **not** a Mac acceptance threshold. These are derived
by `summarize_capture.py` from `frame-comparison.csv`, which is itself derived
from the attached PNGs/CSVs. `physFootprintBytes` is the golden-image run's
point-in-time report; see each `<zone>_mem.json`.

| zone | metrics equal | pixel diff % range | PSNR dB range | physFootprintBytes |
|---|---:|---:|---:|---:|
| qeynos2 | 3/3 | 14.733–40.790 | 50.412–54.723 | 170886008 |
| gfaydark | 3/3 | 22.010–23.920 | 50.144–51.787 | 169460552 |
| befallen | 3/3 | 21.615–32.136 | 49.714–58.883 | 167756664 |
| iceclad | 3/3 | 13.797–22.741 | 47.791–53.936 | 249693240 |
| timorous | 2/3 | 33.543–49.285 | 31.277–55.550 | 259687408 |

The memory-coverage legs also produced `<zone>_coverage_mem.json`; no leg
was skipped. Those files report `physFootprintBytes` respectively as
`177816608`, `193922080`, `187712544`, `267077024`, and `285672792`
for qeynos2, gfaydark, befallen, iceclad, and timorous. The gate stdout
`capture.log` prints all live per-category budget checks. Its final FAIL must
remain a FAIL despite the successful image capture.

## Ruling requested

Which macOS quantity should stand for Linux `RssAnon - gpu_driver` when
judging the 64 MiB owned-memory goal? Apple exposes `task_vm_info.phys_footprint`
and `resident_size`, not Linux's `RssAnon`; the point-in-time former was
measured above. Apple's [task VM fields](https://developer.apple.com/documentation/kernel/task_vm_info_data_t)
and [Metal memory analysis](https://developer.apple.com/documentation/xcode/analyzing-the-memory-usage-of-your-metal-app)
describe footprint as dirty, compressed/swapped process memory, with Metal
resources separately visible. **Inference:** using `phys_footprint` verbatim
as `RssAnon - gpu_driver` could count driver/GPU bytes differently from Linux.
No substitution or target/budget change is made here.

The malloc-zone hook charges `malloc_size()`'s allocation-class bytes for
allocations that reach the executable's wrappers, but Darwin's two-level
binding and allocations made directly by ANGLE/libSystem need coverage proof.
For example, qeynos2's golden-image JSON reports ledger `5408144` against
`physFootprintBytes=170886008`, and its coverage JSON reports ledger
`11195760` against `177816608` (the two JSON files); neither delta is
labelled wholly "unattributed" because `phys_footprint` includes non-heap
and possibly GPU memory. The Mac JSON still has `rssAnonBytes=0`, so the
Linux-only heap/RssAnon accounting assertions cannot establish 100% coverage
on Darwin. A ruling and a Mac-specific reconciliation mechanism are required.

## Not verified / left open

- Step 1 Cocoa device, screenshot and NSEvent logs: no GUI login for `willeq`.
- Full Mac graphics suite: test-tree build red on the bless-only ABI pin.
  The keep-going partial ctest is also red: 19 failed/not-run of 5627,
  167 skipped. `mac-ctest-partial.log` names all cases, including collision,
  BSP budget, first-person segfault, bag, name-tag, sound-font, visual gate,
  and zone-content-smoke failures; the skipped cases are not claimed green.
- Linux CI at this branch head and the ARM build-arm job: not yet run.
- timorous frame 0's geometry metric mismatch: genuine gate failure, not
  waived by the other frames or a process exit 0.
- A Mac memory figure equivalent to Linux owned anonymous memory, and full
  ledger reconciliation: ruling requested above; no budget changed.
- Mac images are evidence for Will's later bless; no golden or manifest row
  is committed here.
