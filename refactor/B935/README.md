# B935 — the LLVM-less Mesa ships as a prefix with the client (issue #253)

Will, 2026-09-30, in chat: **"ship the prefix"** (`docs/decisions.md`). Plan:
`refactor/B935_ship_mesa_prefix.md`. How to use it: `docs/ci.md`, "The shipped
Mesa prefix".

## What ships

| piece | where | what |
|---|---|---|
| names | `scripts/pi/mesa_nollvm.env` | version `25.2.8`, Pi install path `/home/user/mesa-nollvm-25.2.8`, store `/mnt/projects/fable/ci-arm/mesa/25.2.8/` |
| pin | `scripts/pi/mesa_nollvm.sha256` | SHA256 of `mesa-nollvm-25.2.8-armhf.tar.zst` |
| artifact | the store (NFS, never git, never pruned) | the tarball, its `.sha256`, `BUILD_INFO`, `meson-options.txt`, `prefix/` (extracted + `.willeq-mesa-pin`) |
| per build | `ci-arm/<sha>/` | `bin/willeq` (as before, system Mesa), **`bin/willeq-nollvm`** (DT_RPATH=`/home/user/mesa-nollvm-25.2.8/lib`), `mesa` → the store dir, Mesa lines in `BUILD_INFO`, `willeq-nollvm.dynamic.txt` (its RPATH + NEEDED) |
| on the Pi | `/home/user/mesa-nollvm-25.2.8/` (ext4) | installed by `scripts/pi/deploy_mesa_prefix.sh`, pin checked on both ends |

The tarball is deterministic: sorted names, mtime 0, owner 0, `go-w`,
single-threaded `zstd -19`. **Two clean builds from scratch (separate output
dirs, full container rebuild of Mesa each time) produced the same SHA256**,
`cc4d9beb…14bf`. The second run was a plain build, not `--write-pin`, so the
pin check itself passed it. `--ensure` on a populated store took 0.06 s and
built nothing.

Contents (17 files, 2.35 MB compressed): `lib/libgallium-25.2.8.so`,
`lib/libEGL_mesa.so.0.0.0`, `lib/libgbm.so.1.0.0`, `lib/gbm/dri_gbm.so`, the
glvnd vendor json, drirc, pkgconfig and headers. `DT_NEEDED` of the four
`.so`: libdrm, libexpat, libz, libzstd, libstdc++, libm, libgcc_s, libc. No
libLLVM (the build script fails if any file names it).

## The selection mechanism, and why not `$ORIGIN`

The brief preferred an `$ORIGIN`-relative rpath baked at link time. It cannot
work, for two independent reasons:

1. **Secure-execution mode drops it.** The client is
   `setcap cap_net_raw,cap_net_admin+ep` (B751), so it runs with
   `AT_SECURE=1`. glibc then drops an `$ORIGIN` rpath entry unless it expands
   into a trusted system directory (`_dl_dst_substitute` +
   `is_trusted_path_normalize`). Measured on dev-u24, glibc
   2.39-0ubuntu8.9: a two-file test program with `DT_RPATH=$ORIGIN/lib` loads
   its library without capabilities (`AT_SECURE=0`) and fails `libfoo.so:
   cannot open shared object file` once it is setcap'd. The same program with
   an absolute DT_RPATH loads it both ways (`AT_SECURE=1 foo=prefix-lib`).
   The Pi result is in the table below.
2. **The prefix is not relocatable.** libgbm carries
   `/home/user/mesa-nollvm-25.2.8/lib/gbm` (it `dlopen`s `dri_gbm.so` from
   there by absolute path) and libgallium carries `.../share/drirc.d` and
   `.../etc/drirc`, all compiled in from meson's `--prefix` (`strings`
   on the build). A prefix beside the binary at any other path would load
   libgbm and then fail to find its backend.

So the mechanism is B932's: an **absolute DT_RPATH** (not RUNPATH, so glvnd's
`dlopen("libEGL_mesa.so.0")` from the system `libEGL.so.1` searches it too),
written by `patchelf --force-rpath` on a copy at publish time. Link-time
baking was not used either: it would have changed what `bin/willeq` loads for
every existing consumer (every A side, the documented NFS recipe), and the
opt-out would then have needed an rpath-less copy made by the same patchelf.

## Rig changes

- `scripts/pi_rendered_zone_sweep.sh`: `WILLEQ_MESA` (or `--mesa`), default
  `nollvm`. It stages `bin/willeq-nollvm` and deploys the prefix; `system`
  stages `bin/willeq`. It always snapshots at `startup` (plus any
  `--snap-points`) and passes `--expect-mesa` to the summarizer. A
  `mesa_mismatch` row aborts the sweep with exit 67. A build dir without
  `bin/willeq-nollvm` is refused under the default (exit 68).
  `binary_identity.txt` records the staged file, the Mesa mode, the pin and
  the deploy line.
- `scripts/pi/rendered_zone_summarize.py`: four columns appended at the end
  (`mesa_expected mesa_gallium llvm_mapped mesa_check`). The console line prints
  the libgallium and libLLVM facts. With `--expect-mesa`, `mesa_mismatch` is
  the first failure checked and `mesa_unproven` (no snapshot) the last.
  Checked against B932's own run dirs: gfaydark A under `--expect-mesa
  nollvm` → `mesa_mismatch (... mapped /usr/lib/arm-linux-gnueabihf/libgallium-25.2.8-0ubuntu0.24.04.1.so;
  libLLVM MAPPED)`; B under `nollvm` → ok; A under `system` → ok; B under
  `system` → mismatch.
- `scripts/pi/mesa_ab.sh`: A and B are now `--mesa system` / `--mesa nollvm`
  on the same published dir. It no longer builds its own B dir, and `--prefix`
  is gone.
- `scripts/pi/collision_ab.sh`: unchanged. It inherits the default, and
  `WILLEQ_MESA=system` passes through the environment.
- `scripts/pi/kmscube_floor.sh`: prints `libgallium=` / `libLLVM=` per run.
- `scripts/pi/deploy_mesa_prefix.sh` (new), `scripts/pi/mesa_nollvm_select.sh`
  (default prefix from the env file, builds the image if it is absent),
  `scripts/pi/publish_arm_build.sh` (the Mesa step; a Mesa failure still
  publishes the rest and exits 1).

## Pi verification (2026-09-30, CI build `ci-arm/df9801e7`)

The build is this branch's own `build-arm` job (pipeline 355, manual
trigger). It is the real publish path, not a hand copy. Its `bin/willeq` is
byte-identical to fable `0b84eef6`'s (sha256 `7112c483…`), so the client code
is the fable tip's. Raw data: `/mnt/projects/fable/review/B935/`.

### The rpath under secure mode, on the Pi (glibc 2.39-0ubuntu8.7)

`review/B935/origin/pi_rpath_probe.sh` → `RESULT.txt`. `--no-graphics`
against a closed port, and the snapshot is taken 8 s in. libgbm is a
DT_NEEDED of willeq, so which libgbm gets mapped shows which rpath won.

| binary | caps | AT_SECURE | libgbm mapped |
|---|---|---|---|
| `$ORIGIN/mesa/lib` copy | none | 0 | `/home/user/b935/origin/mesa/lib/libgbm.so.1.0.0` |
| `$ORIGIN/mesa/lib` copy | `cap_net_admin,cap_net_raw=ep` | 1 | `/usr/lib/arm-linux-gnueabihf/libgbm.so.1.0.0` (the `$ORIGIN` entry was dropped) |
| `bin/willeq-nollvm` (absolute) | same | 1 | `/home/user/mesa-nollvm-25.2.8/lib/libgbm.so.1.0.0` |
| `bin/willeq` (no rpath) | same | 1 | `/usr/lib/...` |

The same `bin/willeq-nollvm` run **before** the prefix was deployed mapped the
system libgbm without any error: a silent fallback. The rig's maps check
exists to catch that. Since **B946 → #264** the client itself says so: it
logs the `[gldriver]` line at GL init (the mapped libgallium/libLLVM and the
GL_RENDERER string, `refactor/B946/README.md`), and a nollvm binary whose
baked-in prefix is missing logs an ERROR naming it before continuing.

`deploy_mesa_prefix.sh` installed the prefix at
`/home/user/mesa-nollvm-25.2.8` (11 MB, pin checked on both ends). B932's
old `/home/user/mesa-nollvm` was left in place, untouched.

### kmscube floor (3 runs per side, medians, MiB; `review/B935/kmscube/`)

| | VmRSS | RssAnon | RssFile | libgallium mapped | libLLVM |
|---|---|---|---|---|---|
| system Mesa (`/usr/bin/kmscube` copy) | 51.7 | 13.8 | 37.9 | `/usr/lib/.../libgallium-25.2.8-0ubuntu0.24.04.1.so` | mapped, 3/3 |
| shipped prefix (DT_RPATH copy) | 17.1 | 7.2 | 9.9 | `/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so` | absent, 3/3 |
| B932 (stock → no-LLVM) | 55.9 → 17.3 | 13.8 → 7.2 | 42.0 → 10.1 | | |

### Rendered client, gfaydark + growthplane (1 run per side, MiB)

`EQEMU_HOST=local`. The rig is `review/B935/run_sweeps.sh`:
`pi_rendered_zone_sweep.sh` with B932's parameters (`WILLEQ_BASELINE=1`,
the idle tour, snapshots at startup/ready/steady_60s, level 60). The first
sweep used the default, the second `WILLEQ_MESA=system`. Both exited 0 and
all 4 rows pass. Tables: `review/B935/ab/ab_tables.md` (A = system,
B = prefix).

| zone | Mesa | libgallium (all 3 snapshots) | libLLVM | RssFile st/rdy/stdy | RssAnon st/rdy/stdy | gpu_driver st/rdy/stdy | owned stdy / max | owned max − 64 | VmHWM | driver .so Rss | fps med |
|---|---|---|---|---|---|---|---|---|---|---|---|
| gfaydark | system | `/usr/lib/…0ubuntu0.24.04.1.so` | mapped | 56.0/64.7/61.2 | 28.5/91.1/90.5 | 17.8/19.0/19.1 | 71.4 / 72.1 | +8.1 | 159.7 | 40.0 | 30 |
| gfaydark | **prefix (default)** | `/home/user/mesa-nollvm-25.2.8/lib/libgallium-25.2.8.so` | absent | 25.3/44.8/38.3 | 21.8/84.3/83.7 | 17.8/18.9/19.0 | 64.7 / 65.4 | **+1.4** | 129.2 | 6.1 | 30 |
| growthplane | system | `/usr/lib/…` | mapped | 57.2/60.9/59.4 | 28.4/92.4/92.4 | 17.8/18.4/18.5 | 73.9 / 74.4 | +10.4 | 163.2 | 39.3 | 20 |
| growthplane | **prefix (default)** | `/home/user/mesa-nollvm-25.2.8/lib/…` | absent | 26.0/31.8/31.9 | 26.6/94.6/94.6 | **20.6**/21.2/21.2 | 73.4 / 73.9 | +9.9 | 136.1 | 6.6 | 20 |

owned = RssAnon − gpu_driver. Against B932's gfaydark medians (A → B):
RssFile steady 64.9 → 36.4 (here 61.2 → 38.3), owned steady 71.2 → 66.3
(here 71.4 → 64.7), owned max − 64 MiB +8.7 → +4.9 (here +8.1 → +1.4),
driver .so 39.7 → 5.6 (here 40.0 → 6.1). growthplane: RssFile −25.0 in
B932, −27.5 here.

**growthplane's +2.7 MiB `gpu_driver` on the prefix side is the cold shader
cache (B934 → #252), not the build.** It was the first client run ever of the
prefix at its new path, and it ran first (the heaviest zone goes first).
Startup `gpu_driver` was 20.6, which is B932's cold-cache figure exactly
(20.6). gfaydark ran next, warm, and reads 17.8 on both sides. That same
+2.7 MiB is why growthplane's RssAnon is higher on the prefix side. One run
per side cannot separate it further (see "Not verified").

Every run logged `rcvbuf=16777216 bytes (SO_RCVBUFFORCE, CAP_NET_ADMIN)`
and `pi_getcap=... cap_net_admin,cap_net_raw=ep`: the capability was kept
with the rpath'd binary. `binary_identity.txt` records `staged=.../bin/willeq-nollvm`
or `staged=.../bin/willeq`, the Mesa mode, the pin and the deploy line.
Every row's `mesa_check` is `ok`: all 12 snapshots (2 zones × 2 sides × 3
points) show the expected libgallium.

**Locks:** the Pi lock (owner `b935`) was taken for the probe, the deploy and
kmscube, then released. The sweeps took and released the eqemu lock and the
Pi lock themselves. Both sweeps logged `locks released; eqemu lock now: '';
pi lock now: ''`, and `down.sh` brought the local stack down each time.

## Findings filed

- **B946 → #264**: a `willeq-nollvm` whose prefix directory is missing
  silently loaded the system Mesa and the client logged no GL driver
  identity. The rigs catch it and a hand run does not. **CLOSED** — the
  client now reports it: the `[gldriver]` line at GL init plus the
  missing-prefix ERROR (format, decisions and evidence:
  `refactor/B946/README.md`).

## Not verified

- **One run per side per zone.** The rendered comparison is a smoke-level
  A/B (2 zones × 1 run), not B932's 5 zones × 2 runs. The numbers agree
  with B932 in direction and roughly in size, but they are not a new
  measurement of the effect.
- **growthplane's `gpu_driver` / RssAnon** on the prefix side ran with a cold
  shader cache (reasoned from B934's figure, not re-measured warm here).
- **The mismatch path end to end.** `mesa_mismatch` / exit 67 was exercised
  by running the summarizer on B932's real run dirs with the wrong
  expectation (both directions). No live sweep was forced to load the wrong
  Mesa.
- **A fresh-store CI build.** CI ran `--ensure` against a store that already
  held the pinned tarball. The "store missing → build in CI" path was run by
  hand on dev-u24 (two clean builds, same sha), not inside the runner job.
- **Reproducibility across image rebuilds.** Both clean builds used the same
  `willeq-mesa-nollvm-armhf` image. If the image is rebuilt with newer
  Ubuntu packages, the tarball may differ. The pin then fails loudly (by
  design), and a re-pin is a deliberate commit.
- **`gles2_zone_benchmark` and other ARM tools** are not published with an
  rpath'd copy. `mesa_frame_ab.sh` still needs `mesa_nollvm_select.sh` by
  hand.
- **Moving/uinput runs, other zones, zoning between zones, long sessions**:
  the same gaps as B932.
- A hand run of `$D/bin/willeq-nollvm` straight from NFS, without setcap,
  was not run. It works by the same rpath (AT_SECURE=0 honours it too), but
  that is reasoned, not measured.
