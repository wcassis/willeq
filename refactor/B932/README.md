# B932 — an LLVM-less Mesa on the Orange Pi, measured (issue #250)

Will, 2026-09-29: "64 MB excludes driver memory; try the LLVM-less Mesa build"
and "memory first, frame rate second" (`docs/decisions.md`, both 2026-09-29).
The unit builds Mesa without LLVM and measures it on the Pi against the stock
Ubuntu Mesa. The measurements are kmscube, the rendered client in five zones,
and fixed-pose frames.

## Result

- **File-backed memory falls by 25-30 MiB in every zone.** libLLVM (30.6-32.3
  MiB resident) disappears. libgallium drops from 6.6-7.2 MiB (Ubuntu's
  17-driver megadriver) to 4.8-5.0 MiB (Lima only). The libraries that only
  libLLVM or Ubuntu's other drivers and platforms pull in (libicuuc,
  libicudata, libxml2, libedit, libtinfo, libelf, libffi, libsensors,
  libdrm_amdgpu, libwayland-client, the xcb set, libxshmfence) stop being
  mapped.
- **RssAnon falls by 6.7 MiB at startup, in every run** (28.5 → 21.8). That is
  the driver libraries' copy-on-write pages: libLLVM 5.1 MiB, libgallium 1.0 →
  0.3 MiB. Later in a run the run-to-run noise (12.4 MiB between the two stock
  runs of growthplane) hides it, and the per-zone medians at
  steady_60s show -0.9 to -6.7 MiB.
- **The hypothesis about the ~19 MiB `gpu_driver` heap is wrong: it does not
  move.** With a warm shader cache it is 17.8 MiB at startup and 18.4-19.2 MiB
  at steady_60s on both sides, within ±0.1 MiB per zone. The driver heap is
  Lima/gallium's own, not LLVM's.
- **Frames are pixel-identical.** 72 of 72 fixed-pose comparisons match
  exactly: 4 zones × 3 poses, 2 runs per side, all pairs, cross-build
  included, max abs diff 0.
- **fps is unchanged.** The median is identical in every zone and run.
- **Nothing the client uses was lost.** kmscube's report
  (`review/B932/kmscube/extensions_{stock,nollvm}.txt`, diffed mechanically):
  the same `Mali400` renderer, GLSL ES 1.0.16 and **71/71 GLES2 extensions
  identical**, and the 33/33 EGL display extensions identical. Only the EGL
  *client* extensions differ, and only by the five platforms that were not
  built (`EGL_{EXT,KHR}_platform_{x11,wayland}`, `EGL_EXT_platform_xcb`).
  `--drm` uses GBM. No run on either side logged a FATAL or took a watchdog
  kill, and all 20 client runs pass the sweep's verdict.

### kmscube floor (3 runs per side, medians, MiB; `/proc/<pid>/status` + smaps 12 s after launch)

| | VmRSS | RssAnon | RssFile | libLLVM Rss (anon) | libgallium Rss (anon) | [heap] |
|---|---|---|---|---|---|---|
| stock Mesa 25.2.8 | 55.9 | 13.8 | 42.0 | 34.1 (5.1) | 7.1 (1.0) | 5.8 |
| LLVM-less 25.2.8 | 17.3 | 7.2 | 10.1 | absent | 4.6 (0.3) | 5.5 |
| B − A | **−38.6** | −6.6 | −31.9 | | | −0.3 |

### Rendered client (2 runs per side per zone, medians over the runs, MiB)

`WILLEQ_BASELINE=1 --drm --renderer gles2 --constrained orangepi -r 1280 720 --eqe1-path ...`,
fable `0542a43c` CI ARM build, idle at the zone's safe point, level 60,
SIGTERM 5 s after `steady_60s`. **owned = RssAnon − gpu_driver**, from the same
`[memledger]` line. "owned max" is the maximum over every ledger point of the
run.

| zone | RssFile steady A → B | RssAnon steady A → B | gpu_driver steady A → B | owned steady A → B | owned max A → B | owned max − 64 MiB, worst run A / B | peak RssAnon A → B | VmHWM A → B | driver .so Rss A → B | fps median A / B |
|---|---|---|---|---|---|---|---|---|---|---|
| growthplane | 55.5 → 30.6 (−25.0) | 151.1 → 150.2 (−0.9) | 18.4 → 18.4 | 132.7 → 131.9 | 132.9 → 132.4 | +75.4 / +69.1 | 157.8 → 157.3 | 216.3 → 187.8 | 38.0 → 5.6 | 20 / 20 |
| timorous | 56.3 → 26.4 (−29.9) | 129.7 → 123.8 (−5.9) | 19.0 → 19.0 | 110.7 → 104.8 | 116.1 → 108.6 | +53.3 / +44.9 | 140.0 → 133.9 | 201.9 → 168.2 | 39.3 → 5.6 | 30 / 30 |
| iceclad | 57.0 → 28.6 (−28.4) | 104.3 → 97.6 (−6.7) | 18.6 → 18.6 | 85.6 → 79.0 | 92.2 → 85.5 | +28.2 / +21.7 | 111.0 → 104.2 | 174.4 → 136.8 | 41.1 → 5.6 | 30 / 30 |
| gfaydark | 64.9 → 36.4 (−28.6) | 90.3 → 85.3 (−5.0) | 19.1 → 19.0 | 71.2 → 66.3 | 72.0 → 67.0 | +8.7 / +4.9 | 95.4 → 88.9 | 164.2 → 130.6 | 39.7 → 5.6 | 30 / 30 |
| qeynos2 | 65.8 → 37.6 (−28.2) | 68.0 → 63.8 (−4.2) | 19.2 → 19.2 | 48.8 → 44.6 | 48.8 → 44.7 | −12.5 / −19.1 | 78.6 → 74.4 | 145.0 → 111.9 | 41.5 → 5.6 | 60 / 60 |

At startup, before any zone, every run reads RssAnon 28.4-28.5 (A) against
21.7-21.9 (B), RssFile 53.3-58.4 against 24.2-25.6, and `gpu_driver` 17.8 on
both sides.

Under the 2026-09-29 reading of 64 MB, qeynos2 is under target on both sides.
gfaydark is 4.9 MiB over with the LLVM-less Mesa (8.7 with stock). The Kunark
and Velious zones stay 22-69 MiB over, and that gap is willeq's own memory,
not the driver's.

The per-run rows (RssAnon/RssFile/gpu_driver at startup/ready/steady, VmHWM,
fps min, which libgallium was mapped) and the per-library table are in
`refactor/B932/ab_tables.md`. The fps minimum over a run is 1-43 and neither
side is consistently lower (for example, growthplane A 1/19 against B 19/19,
qeynos2 A 16/43 against B 42/27).

### Frames (`refactor/B932/frame_diff.md`)

The live client's dumped frames cannot be compared across legs. The server's
game clock moves between legs, so iceclad rendered at night in one leg and in
daylight in the next (cross-run SSIM 0.25, `review/B932/ab/`). The picture
check therefore uses the visual gate's own offline renderer on the Pi:
`scripts/pi/mesa_frame_ab.sh` runs `gles2_zone_benchmark --drm` (the 2026-09-19
ARM build in `main/build-arm-noble/bin`, sha256 `fddd6b05…`, stripped, the
same binary on both sides). It uses the manifest's `productionConfig` and each
zone's three `--camera-pose` entries, with the EQE1 pack, in qeynos2,
gfaydark, iceclad and timorous. Every run's maps were saved (`maps_<zone>.txt`,
libgallium path + libLLVM count). Frames are non-blank (luma stddev 11-65,
2,700-10,700 distinct colours at 160×90).

| comparison | pairs | pixel-identical | max abs diff |
|---|---|---|---|
| stock run 1 vs stock run 2 | 12 | 12 | 0 |
| no-LLVM run 1 vs no-LLVM run 2 | 12 | 12 | 0 |
| stock vs no-LLVM (4 pairings) | 48 | 48 | 0 |

## The build

`scripts/pi/build_mesa_nollvm.sh`, in the container
`docker/Dockerfile.mesa-nollvm-armhf` on dev-u24 (never on the Pi, never on
the Inspur box). It uses the **exact source the Pi runs**: Ubuntu's source
package `mesa_25.2.8-0ubuntu0.24.04.1` from the Launchpad librarian, with the
.dsc's SHA256 sums checked. noble-updates had already moved to
`0ubuntu0.24.04.2` on 2026-09-30, so `apt-get source` could not fetch it. The
upstream tarball and the Ubuntu patches are the same as Ubuntu's build, with
Ubuntu's `dpkg-buildflags` for armhf (`-g -O2 -fstack-protector-strong ...`),
`buildtype=plain`, stripped. Cross gcc 13.3 (Noble), meson 1.7.2 pinned.

The configured option set, exactly (`refactor/B932/meson_args.txt`; the full
`meson configure` dump is `~/b932-mesa-nollvm/meson-options.txt` on dev-u24):

```
--prefix /home/user/mesa-nollvm --libdir lib --buildtype plain -Db_ndebug=true
-Dllvm=disabled -Dgallium-drivers=lima -Dvulkan-drivers= -Dvulkan-layers= -Dplatforms=
-Degl=enabled -Dgbm=enabled -Dglvnd=enabled
-Dgles1=disabled -Dgles2=enabled -Dopengl=false -Dglx=disabled
-Dgallium-va=disabled -Dgallium-vdpau=disabled -Dgallium-rusticl=false
-Dteflon=false -Dvalgrind=disabled -Dlmsensors=disabled -Dlibunwind=disabled
-Dgallium-extra-hud=false -Dbuild-tests=false -Dtools= -Dvideo-codecs=
-Dzstd=enabled -Dshader-cache=enabled -Dxmlconfig=enabled -Dexpat=enabled
```

Mesa 25.2 has no `kmsro` value for `gallium-drivers`. The renderonly display
glue for sun4i-drm is built with `lima` automatically. `VERSION` is left at
upstream's `25.2.8` (Ubuntu writes the full Debian version into it), so the
megadriver is `libgallium-25.2.8.so` and cannot be mistaken in maps for the
system's `libgallium-25.2.8-0ubuntu0.24.04.1.so`.

The build installs `lib/libgallium-25.2.8.so` (10.1 MB stripped, against
Ubuntu's 26.4 MB), `lib/libEGL_mesa.so.0`, `lib/libgbm.so.1`,
`lib/gbm/dri_gbm.so`, the glvnd vendor json and drirc. `DT_NEEDED` of the
megadriver: libdrm, libexpat, libz, libzstd, libstdc++, libm, libgcc_s,
libc. The script fails if any installed file mentions libLLVM.

**What this A/B changes is "LLVM off + only Lima built"**, not LLVM alone.
Ubuntu's megadriver also carries 16 other gallium drivers. Part of the
libgallium drop (7.1 → 4.6-5.0 MiB resident) may come from the driver set
rather than from LLVM. libLLVM's own 30.6-34.1 MiB resident + 5.1 MiB COW is
the dominant term either way. Not separated (see "Not verified").

## How the build is selected, and the proof

Nothing on the Pi was replaced: the system Mesa packages are untouched, no
apt-remove, no apt install. The prefix lives at `/home/user/mesa-nollvm` (ext4
home) and stays there.

**Mechanism: a DT_RPATH on a copy of the binary** (`scripts/pi/mesa_nollvm_select.sh`,
`patchelf --force-rpath --set-rpath /home/user/mesa-nollvm/lib`). No
environment variable is involved. The reasons:

- The rendered client is `setcap cap_net_raw,cap_net_admin+ep` (B751's rcvbuf).
  A capability binary runs in secure-execution mode and `ld.so` ignores
  `LD_LIBRARY_PATH` there. An absolute DT_RPATH without `$ORIGIN` is honoured.
  All 20 client runs, both sides, report
  `rcvbuf=16777216 (SO_RCVBUFFORCE, CAP_NET_ADMIN)`, so the capability was
  kept on the B side.
- glibc searches the **executable's** DT_RPATH for every load whose requester
  has no DT_RUNPATH. That covers glvnd's `dlopen("libEGL_mesa.so.0")` from the
  system `libEGL.so.1` (the system vendor json is used as is), the binary's
  own `libgbm.so.1`, and `libgallium-25.2.8.so` as needed by `libEGL_mesa` and
  by `dri_gbm.so`. libgbm loads `dri_gbm.so` by the absolute
  `$prefix/lib/gbm` path compiled into it. A DT_RUNPATH would have covered
  only the binary's direct `DT_NEEDED` entries.

**Proof from `/proc/<pid>/maps`**, never inferred:

- kmscube: `review/B932/kmscube/{stock,nollvm}/run*.json` (`shared_objects`).
  In all 3 stock runs, libLLVM plus the `/usr/lib/...` libgallium, libEGL_mesa,
  libgbm and dri_gbm are mapped. In all 3 no-LLVM runs, libLLVM is absent and
  all four come from `/home/user/mesa-nollvm/lib`.
- Rendered client: `snap_{startup,ready,steady_60s}.json` in every run dir
  (`rendered_zone_run.py --snap-points`, taken under `sudo -n` because a
  capability process's smaps is root-owned). At all 3 points of all 10 A runs,
  libLLVM is mapped with the system libgallium. At all 3 points of all 10 B
  runs, libLLVM is absent with `/home/user/mesa-nollvm/lib/libgallium-25.2.8.so`
  (`ab_tables.json`, `proof_points`).
- Benchmark frames: `review/B932/frames/{A1,A2,B1,B2}/maps_<zone>.txt`.

## Cold shader cache (finding, B934 → #252)

The single-zone smoke run was the first ever run of the new build, and it read
`gpu_driver` +2.8 MiB against stock. That is the shader cache, not the build.
With `~/.cache/mesa_shader_cache` removed before each run (then put back),
**both** Mesas read 20.6 / 21.7-21.8 / 21.7-22.0 MiB at startup / ready /
steady (`review/B932/coldcache/`), against 17.8 / 18.9 / 19.1 warm. All A/B
numbers above are warm on both sides: the smoke run had warmed the LLVM-less
build's cache before the A/B started.

## Findings filed

- **B933 → #251**: the printed 64 MB gap (`over_target`, `overTargetBytes`,
  `gapToTarget`, `visual_gate.py`) is still taken on the whole RssAnon, with
  `gpu_driver` inside, against the 2026-09-29 ruling. The driver .so COW pages
  (6.1 MiB with stock Mesa, 0.3 MiB without LLVM) are in RssAnon and not in
  `gpu_driver`, so which side of the line they belong on needs deciding too.
- **B934 → #252**: a cold Mesa shader cache adds about 2.8 MiB to `gpu_driver`
  for the session. The rigs neither record nor pin it.
- **B935 → #253** (class C): how the Pi gets the LLVM-less Mesa (system
  packages, a shipped prefix, or bundled). Nothing ships it today.

## Reproduce

```bash
# build (dev-u24, docker) and deploy the prefix
bash scripts/pi/build_mesa_nollvm.sh                     # -> ~/b932-mesa-nollvm/
rsync -a --delete ~/b932-mesa-nollvm/mesa-nollvm/ user@10.0.30.192:/home/user/mesa-nollvm/
# kmscube floor (Pi lock held; kmscube.nollvm = mesa_nollvm_select.sh on a copy of /usr/bin/kmscube)
ssh user@10.0.30.192 bash /mnt/projects/fable/wt/<slot>/scripts/pi/kmscube_floor.sh <kmscube> <out-dir> 3
# rendered A/B (takes and releases both locks per leg, alternates A B / B A)
EQEMU_HOST=local bash scripts/pi/mesa_ab.sh --a /mnt/projects/fable/ci-arm/<sha> --rounds 2 --out <dir>
python3 scripts/pi/mesa_ab_table.py --ab-dir <dir> --json <dir>/ab_tables.json
# fixed-pose frames (Pi lock held), then the pairwise diff
ssh user@10.0.30.192 bash .../scripts/pi/mesa_frame_ab.sh <gles2_zone_benchmark copy> <out>/A1
python3 scripts/pi/mesa_ab_table.py --frame-dirs A1=<out>/A1 A2=<out>/A2 B1=<out>/B1 B2=<out>/B2
```

Raw data: `/mnt/projects/fable/review/B932/` (`kmscube/`, `smoke/`, `ab/` with
the per-leg sweep dirs, `frames/`, `coldcache/`).

Rig changes made by this unit, all opt-in, with defaults unchanged:
`rendered_zone_run.py --snap-points`, `pi_rendered_zone_sweep.sh --snap-points`,
and `proc_mem_snapshot.py` (new).

## Not verified

- **LLVM alone vs the driver set.** The B build removes LLVM and 16 unused
  gallium drivers together. No LLVM-less build with Ubuntu's full non-LLVM
  driver list was measured.
- **Moving, or with the uinput drive.** All client runs are idle at the safe
  point. B926's moving fps and the collision path were not re-run on B.
  Frame-rate parity is shown idle (the rendered runs) and on the benchmark's
  three fixed poses per zone.
- **Longer sessions, zoning between zones, the other 40 Kunark/Velious
  zones.** Only 5 zones × 2 runs per side, about 60-90 s after ready.
- **Driver behaviour beyond the extension lists** and what willeq, kmscube
  and gles2_zone_benchmark exercise. There is no software fallback on the Pi
  in the B build: llvmpipe and softpipe were not built, and the stock build's
  llvmpipe is not `--drm`'s path. A GLES2 feature Lima reports but draws
  through gallium's LLVM `draw` module on stock would show as a frame
  difference, and none was found. Code paths the benchmark poses and idle
  scenes do not reach were not checked.
- **Why growthplane idles at 20 fps** here (B926 measured 26.9-28.0) is the
  same on both sides and was not investigated. It is not a Mesa difference.
- The x86 dev-host numbers are unaffected, since this is Pi-only. CI asserts
  nothing new, and no budget, golden, baseline or gate changed.
