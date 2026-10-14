# B1044 macOS graphics parity (FIX, #364)

Evidence root: `/mnt/projects/fable/review/B1044/` (the mini sees it as
`~/projects/fable/review/B1044/`). `review/B1044/README.md` has every run,
command and file; this page is the summary that travels with the branch.

## Per-item status

| item | status |
|---|---|
| 1 Cocoa window + input | **met**. `docker/irrlicht-drm-x86/CIrrDeviceFB_Cocoa.mm` + `cocoa_keycode_map.h`; `--egl-window` (alias `--egl-x11`, which prints a hint on macOS). Proven at the console as `will` (run 4, `review/B1044/console_run4/out/`): window 1280x720, keys down/up 10/10, mouse down/up 4/4 incl. a right click, wheel 78, moves 85, reaching the client's hotkeys. Resize, Option and Command-Q were not exercised. |
| 2 GLES2 tree/tests | **builds; 2 of 5,633 fail** on the M4 (168 skipped). Classification below. |
| 3 frameworks | met (checkpoint); the Cocoa window's QuartzCore/CoreGraphics ride on the Darwin `EGL` target. |
| 4 GLES2 headers everywhere | met (checkpoint); x86 + ARM + gate green in MR pipeline 1258. |
| 5 etc2comp / CMake 4 | met (checkpoint). |
| 6 shared copy list / stamp / log | met; the list also carries the Cocoa files; the Darwin make passes `CXXFLAGS` the way the x86 script does (see ABI below). |
| 7 `textureCacheBytesLimit` | named: `ConstrainedRenderingPreset::OrangePi` (21 MiB) on every platform; the x86 manifest's 67,108,864 is a stale bless value. Under Will's ruling the Mac section carries its own row (22,020,096). |
| 8 memory on macOS | Will ruled 2026-10-14 (option 1, platform-scoped figures). The Darwin reading is `task_vm_info.phys_footprint` (`src/client/core/memory_ledger_darwin.h`), on every Darwin `[memledger]` line and JSON; Linux output unchanged. |
| 9 visual gate `--platform mac` | wired as a third set: `--capture` writes `manifest_mac.json`; verify reads the blessed `platforms.mac`. Capture staged in `review/B1044/goldens_mac/` (not committed); it is red on one ledger ratchet (qeynos2). |

## The 19 Mac ctest cases that failed or did not run at the checkpoint

| case(s) | class | mechanism and disposition |
|---|---|---|
| `test_placed_object_record` (not built) | Darwin ABI | libc++ `std::string` is 24 bytes, libstdc++'s 32; every other member lays out the same. Pinned per standard library (160/152 on libc++, 168/160 on libstdc++). Fixed. |
| `ObjectCollisionSetTest` x2 | Darwin build flags | the Darwin Irrlicht was built with the Makefile's `-O3` and clang's default FMA contraction; willeq TUs use `-ffp-contract=off`, so the selector's copy and the query-time transform differed by an ULP. Fixed: the Darwin make passes `CXXFLAGS=-ffp-contract=off`. |
| `ZoneBspRegionPool.SlabsAreFew...`, `ZoneBspPvsRealZones` x4 | test assumption | counted slab boundaries by address gaps, which only exist because glibc puts a chunk header between blocks; macOS malloc packs two 96 KiB slabs back to back. The tests now count owner changes (`owner_before`). Fixed. |
| `FirstPersonBodyDrawRule` x3, `FirstPersonWeaponAttachment` x2 (SEGFAULT) | Darwin build flags | `build-gles2-x86.sh` passes `CXXFLAGS` on the make line, which REPLACES the Makefile's `-fno-rtti`; the Darwin script did not, so its library had no RTTI and `dynamic_cast` over an Irrlicht node crashed. Fixed (same `CXXFLAGS` rule as x86). |
| `BagCommand` x2 | real bug (test) | `findClosed(h.events())` kept a pointer into a destroyed temporary vector: read-after-free on every platform; glibc left the bytes, macOS did not. Fixed. |
| `SoundFontRelease.SoundFontCustomScope...` | allocator accounting | the same 32 blocks cost 2,384 B under macOS `malloc_size()` classes and libc++ layouts (2,976 B on glibc). Pinned per platform. Fixed. |
| `zone_content_sweep_smoke` | Linux-only rig | GNU `grep -P`, `stdbuf`, `/proc` RssAnon. The zone-name check is portable now; the smoke SKIPs on macOS. |
| `visual_gate` | missing tool | Pillow / ImageMagick `compare` absent for the build user (Homebrew is owned by `will`). Open. |
| `NameTagBatchTest.BatchIsPixelIdentical...` | Darwin pixel difference | 615 px differ in the densest pose only (200 tags at 115-160 units; the 10- and 60-tag poses match). Mechanism not established. FOLLOW-UP card. |

## timorous frame 0

Mac draws/polys 579/11,114 vs x86 586/11,258 with `regions_vis` 142 vs 145 and
`regions_occluded` 59 vs 56; frames 1-2 and the other 12 frames equal. With
`--no-occlusion-cull` both platforms give 766 draws / 14,875 polys / 201
regions on frame 0 (`review/B1044/timorous_ab/`). So the difference is the CPU
software occlusion culler (`include/client/graphics/software_occlusion_culler.h`)
hiding three more regions on arm64. It is not render order, instancing or the
GPU driver. It survives the Irrlicht `-ffp-contract=off -O0` rebuild, so it is
in willeq's own culling arithmetic (candidates: libm trig in the camera setup,
compiler codegen). Frame 0's pixels are still close to x86 (PSNR 55.5 dB).
Pinning which value flips needs a card.

## Not verified

- Window resize, Option and Command-Q on the console; the window takes keys
  only after the first click (macOS 14+ cooperative activation).
- The Mac capture's qeynos2 ledger ratchet: unowned 56,192 B = 1.04 % of the
  ledger (bound 1 %), 52,448 B / 0.97 % at the checkpoint. Darwin has no
  `gpu_driver` caller classifier (Linux-only `dl_iterate_phdr`), so allocations
  made outside any scope stay unowned; not traced.
- `phys_footprint` vs the ledger: far apart (ledger 5-41 MiB vs footprint
  161-251 MiB); reconciling them is open.
