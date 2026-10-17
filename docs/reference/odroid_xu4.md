# ODROID XU4 as a willeq client (B1045, #365)

Will turned the XU4 on 2026-10-13 as an **interactive play-test box** (his words:
not set up for automated work; no `PI_LANE`, no lock, no board cards). This page
records the device, how willeq runs on it, and the GPU clock defect that made the
first session run at 12 fps. The fix and the tools live in `scripts/sbc/xu4/`.

## Play-test defects (Will, 2026-10-13)

| Issue | Observation | Code change | Device status |
|---|---|---|---|
| #369 / B1049 | Held keys did not repeat in chat. | evdev repeat value 2 now emits another key-down with a character. | Awaiting Will's XU4 play session after merge. |
| #370 / B1050 | Camera pans tore; UI windows sometimes appeared partially drawn or disappeared. | GBM's displaced scanout buffer is retained until the DRM flip event; the synchronous SetCrtc path releases after completion. | Awaiting Will's XU4 play session after merge. |

## Power-cycle caveat (Will, 2026-10-13)

A soft power cycle does not take effect unless the HDMI cable is fully removed:
the monitor back-feeds +5 V on HDMI pin 18, so the PMIC never sees a cold start.
This also explains why `systemctl reboot` strands the board. The hardware
remedy is a pin-18-isolated cable/adapter or a monitor/switch that does not
source 5 V. This is Will's report, not a measurement made for this card.

## The device

| | |
|---|---|
| board | Hardkernel ODROID XU4, `10.0.30.127`, ssh alias `xu4` (user `user`, passwordless sudo) |
| SoC | Exynos 5422: 4x Cortex-A15 @ 2.0 GHz + 4x Cortex-A7 @ 1.4 GHz, 2 GB LPDDR3, 1 GB zram swap |
| GPU | Mali-T628 MP6 **r0p1** (Panfrost reports it as `Mali-T620`, id 0x620 major 0 minor 1), two core groups; Panfrost powers the first (4 of 6 cores) |
| OS | Armbian 26.8.3 noble (Ubuntu 24.04 armhf), kernel `6.6.151-current-odroidxu4`, Mesa 25.2.8 (the Pi's version), GLES 2.0 |
| display | exynos-drm on card0, HDMI 1920x1200/1080p; Panfrost is card1 / renderD129 |
| storage | 29 GB eMMC root, `/tmp` tmpfs 995 MB, NFS `/mnt/projects` (plain NFSv3 mount works from the 10.0.30.x subnet) |
| input | USB keyboard + mouse on evdev (the DRM device reads them directly) |

The armhf CI publish runs unmodified: `/mnt/projects/fable/ci-arm/by-branch/fable/bin/willeq` (the system-Mesa
binary, not the `-nollvm` Lima prefix). On the box: `~/willeq/play.sh` (cd to the publish dir,
`WILLEQ_DATA_ROOT`, `WILLEQ_CACHE_DIR=/tmp/willeq-cache`, `--drm --renderer gles2 --constrained orangepi
-r 1280 720`), `~/willeq/willeq` a setcap'd copy (NFS and tmpfs refuse `setcap`), `~/willeq/update.sh` to
refresh it, `~/willeq/willeq.json` with `maps_path`/`navmesh_path` under `/mnt/projects/akk-stack/server/maps`
(without `maps_path` the client logs `Unable to open map file ./maps/<zone>.map`). There is no `--constrained`
preset for this GPU; `orangepi` is used as-is on 2 GB.

## The defect: the GPU ran at 24 MHz

**Symptom.** qeynos at 1280x720 ran at ~12 fps. The offline benchmark scaled almost linearly with pixel count
and the CPU sat 97 % idle at its lowest clock, so the GPU was the wait.

**Measurement chain** (every step is a tool in `scripts/sbc/xu4/` or a one-liner on the box):

1. `gpu_job_trace.sh` (kernel `dma_fence` tracepoints): every fragment job 60.7 ms, every vertex/tiler job
   6.5 ms, **independent of resolution** (320x240 = 1280x720) and of the devfreq GPU clock (177 MHz = 600 MHz,
   the clock tree showing `g3d` really at 177). glmark2 `build` offscreen: 15 fps at any size.
2. One job IRQ per job; a kernel thread polling `JOB_INT_RAWSTAT` every 100 us never beat the IRQ, so the
   completion was not late, the GPU was busy. Sampling the activity registers during fragment jobs: shader cores
   and L2 active 99.9 % of the time, tiler idle, MMU walker idle.
3. `GPU_CYCLE_COUNT` against wall time: **24.00 MHz** at both devfreq settings. The GPU was clocked from the
   crystal.
4. `g3d_mux.py`: CMU_TOP `SRC_TOP5` bit 16 (`mout_user_aclk_g3d`) = 0 = `fin_pll`, while the common clock
   framework reported the mux on `mout_sw_aclk_g3d` (VPLL -> `dout_aclk_g3d`, 177-600 MHz). The GPU regulator,
   VPLL lock and the other two muxes were all as the framework believed.

**Mechanism.** `drivers/clk/samsung/clk-exynos5-subcmu.c` describes the G3D domain (`exynos5x_g3d_subcmu`)
with the suspend register `{ SRC_TOP5, 0, BIT(16) }`: park the user mux on `fin_pll` while the power domain is
off, restore the saved value on resume. The park happens at boot (`exynos5_subcmus_init`) and the hardware forces
the mux to `fin_pll` on every G3D power-domain cycle (measured: a hand-set bit reverts after one GPU runtime
suspend/resume). The restore lives in the sub-CMU platform device's `runtime_resume`, and that device stays
runtime-suspended while Panfrost powers the domain up and down, so nothing ever restores the mux. The clock
framework's cached parent never changes, so `clk_summary`, devfreq and `/sys/kernel/debug/clk` all look right.
This affects any mainline Exynos 5420/5422 Panfrost user; it is upstream-report-worthy and not yet reported.

**Refuted on the way** (each built as a module variant and measured, fragment job unchanged at 60.7 ms):
job affinity masked to the powered core group (upstream names every core in `shader_present` while powering
four, but it is not the cause), powering both core groups (43 ms, with job faults), dropping the per-job
`START/END_FLUSH_CLEAN_INVALIDATE`, every `AS_MEMATTR` variant from the T628 patch series, Mesa linear/no-AFBC.
The T628 series' "r0 not supported" note was the wrong lead: r0p1 renders correctly once clocked.

**Fix.** `panfrost-xu4-g3d-mux.patch`: at the top of `panfrost_gpu_power_on()` (probe, runtime resume and job
reset all pass through it), on an `exynos5420`/`5800`/`5422` machine, `ioremap` `SRC_TOP5` once and set bit 16
if clear, logging the first arm and every re-select. Measured after the fix:

| | before | after |
|---|---|---|
| glmark2 `build`, offscreen 320x240 | 15 fps | 279 fps |
| fragment job (`panfrost-js-0`) | 60.7 ms | 2.8 ms |
| vertex job (`panfrost-js-1`) | 6.5 ms | 0.3 ms |
| glmark2 `build` / `texture` / `shading`, 1280x720 | 27 / 29 / 16 fps | 477 / 520 / 299 fps |
| `gles2_zone_benchmark` qeynos2 1280x720, 200 frames | 10.3 fps, p50 102 ms | 89 fps, p50 10.0 ms |
| same, 3000 frames (sustained) | | 91 fps, p50 10.1 ms, GPU 45 C, fan on, no throttle |
| live `--scenario zone_in` 1280x720 | | PASS, zoned to qeynos in 3.7 s |

Evidence: `review/B1045/README.md`.

## Play-test defects under investigation

Will reported corrupted NPC textures and all-black NPC silhouettes on this box
(B1051, issue #371). The first headless investigation did not capture an
affected NPC: synthetic benchmark entities used diagnostic coloring, and the
live Testchar camera faced a nearby wall in qeynos. A later scripted qeynos2
heading sweep captured one distant named NPC, but not the reported group or a
classifiable defect. Take 2 ran Will's `play.sh` at his saved qeynos location:
the baseline showed one normally textured bridge NPC, while the subsequent
1024x768 and per-frame turn captures produced no classifiable affected NPC.
An image-only script counted dark and high-variance *proxies* in every frame,
but UI panels, windows, water, and wood also triggered them; they are not an
NPC defect count. Therefore the result remains **NOT REPRODUCED**, and no
Panfrost mechanism, one-knob result, or preset change is established. See
`refactor/B1051/README.md` for the four run rows, raw evidence, and limitations.

**Resolved 2026-10-15 as a CLIENT defect, not a Panfrost/Mesa one** (issue
#371, MR !173). The live reproduction scripted `/moveto` + `/face` on
Guard_Treitan (race 71, qeynos2) against eqemu lane 2 with the x86 llvmpipe
client: with `--eqe1-path` (ETC1) the multi-buffer character rendered one
flat mottled body ("smear") while single-buffer zone ETC1 rendered correctly;
with `--eqe1-path` removed (raw RGBA) the same character rendered with
distinct per-part textures. `refactor/B1051/STEP_A_x86_capability.md` proves
llvmpipe accepts ETC1 uploads and decodes them correctly, so the driver is
cleared on both sides of the equation. The confirmed mechanism is
client-side: in an EQE1-authoritative run the character S3D's raw texture
bytes are released at parse time, so `ZoneMeshBuilder::getOrLoadTexture()` —
the on-demand per-buffer path for multi-buffer characters — found nothing in
its own maps and fell to the textureless diffuse fallback; the per-part GPU
textures existed in the `ConstrainedTextureCache` but only after the render
thread's upload loop *promoted* the queued entry, which lane-2 run 912c
measured 80-94 ms AFTER the mesh build (first race-71 guard mesh build
11:02:47.871 / Guard_Drath's own 11:02:47.885 vs the first qcf ETC1 upload
11:02:47.965; the continuation's run re-measured the same gap at 154 ms,
`refactor/B1051/README.md` "The client fix"). The fix is the
constrained-cache fallback plus the synchronous promotion of the queued
entry (`bf2a8968`, `ad7295e1`, MR !173), pinned by
`tests/test_b1051_mesh_texture_fallback.cpp`. The earlier companion-alpha
material gate hypothesis (take 3) was refuted live and reverted
(`eaf15dc0`) — it was the wrong mechanism; the smear buffers drew mat=256
SOLID. The x86 lane-2 after-stills (ETC1 == RGBA, distinct parts,
`/home/user/kb-evidence/b1051-followup/`) land with the XU4 after-still leg
of the same MR.

**2026-10-17 saved-pose recheck:** `play.sh` and the published MR !173 armhf
binary were captured separately from the same stationary Testchar position,
`qeynos2` `(272.4, 197.6, 3.6)`, heading `471.50` in the zone-in log.
`/target` and `/face` could not find either Lieutenant Arathur or Guard Dunix
in either run, so the after-still acceptance for those NPCs remains OPEN.
The fixed pose showed a close wood-textured figure in both runs, not a
classifiable affected guard. The image-only dark/noise TSV totals are proxies,
not NPC defect counts. `refactor/B1051/README.md` "Take-2 resumption" names
the stills, commands, log lines and cleanup checks. No new Mesa mechanism or
`xu4` preset follows from this non-reproduction.

## Kernel and module state on the box

Armbian 26.05 (6.6.129) shipped Panfrost but had this defect; its headers are not packaged. Armbian 26.8.3
(6.6.151) has headers but **no Panfrost at all** (`CONFIG_DRM_PANFROST` unset, no `gpu-sched`): glmark2 on it
reports `llvmpipe`. The box therefore runs 6.6.151 with self-built modules: `scripts/sbc/xu4/build-panfrost-modules.sh`
fetches the upstream stable Panfrost and GPU-scheduler sources at the running kernel's version, applies the
patch, builds both against the Armbian headers (the headers' `Module.symvers` lacks `gpu-sched`, so the fresh
scheduler build supplies it) and installs them under `/lib/modules/<ver>/extra` with the `modules-load.d` entry
and the boot service. A kernel package upgrade drops `extra/`: re-run the script.

Cold boot verified 2026-10-13 after a power cycle: the modules load from `modules-load.d`, the hook logs
"armed" at probe, the GPU runtime-suspends while idle, and the first GPU use logs the re-select
(`G3D mux: fin_pll -> mout_sw_aclk_g3d`); glmark2 `build` 288 fps.

Traps: `systemctl reboot` twice left the board unreachable until Will power-cycled it (the USB-ethernet
XU4 does not come back from a soft reboot reliably); after a few module reloads the GPU's `runtime_usage`
count sits at 3-4 and it stops runtime-suspending, which is why a hand-set mux bit appeared to survive
during the investigation -- do not take that as evidence the restore is unnecessary.
