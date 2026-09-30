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

## Pi verification

PENDING.

## Not verified

PENDING.
