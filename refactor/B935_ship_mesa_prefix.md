# B935 — ship the LLVM-less Mesa as a prefix with the client (issue #253)

Will, 2026-09-30, in chat: **"ship the prefix"** — option (b) of #253 (system
package / shipped prefix / bundled), selected by rpath as B932 measured
(`refactor/B932/README.md`).

## Scope

1. **A reproducible prefix artifact.** `scripts/pi/build_mesa_nollvm.sh` packs
   the prefix into a deterministic `mesa-nollvm-<ver>-armhf.tar.zst` (sorted
   names, fixed mtime/owner, single-thread zstd) whose SHA256 is pinned in the
   repo (`scripts/pi/mesa_nollvm.sha256`). The tarball lives OUTSIDE git, on
   NFS beside the CI ARM builds: `/mnt/projects/fable/ci-arm/mesa/<ver>/`
   (tarball + `prefix/` extracted + BUILD_INFO). `--ensure` is idempotent: a
   store that already holds the pinned tarball is verified, never rebuilt. A
   build whose bytes do not match the pin is never installed into the store.
   The version, the Pi install path and the store path are named in ONE file,
   `scripts/pi/mesa_nollvm.env`.
2. **Publish.** `scripts/pi/publish_arm_build.sh` (the `build-arm` job) adds,
   per sha: `bin/willeq-nollvm` (the same stripped binary with
   `DT_RPATH=<pi prefix>/lib`, `patchelf --force-rpath`), a `mesa` symlink to
   the versioned store dir, and the Mesa identity in `BUILD_INFO`. `bin/willeq`
   is unchanged (system Mesa) — it is the opt-out and every A side.
3. **Selection by default + opt-out.** `scripts/pi_rendered_zone_sweep.sh` (and
   so `collision_ab.sh`, `mesa_ab.sh`) stages `bin/willeq-nollvm` and deploys
   the prefix to the Pi's ext4 home at the pinned path unless
   `WILLEQ_MESA=system`, which stages `bin/willeq`. A standalone
   `scripts/pi/deploy_mesa_prefix.sh` does the deploy for any other rig.
4. **Proof in every run.** The sweep always snapshots `/proc/<pid>/maps` at
   `startup` (plus any `--snap-points`); the summarizer prints which
   libgallium was mapped and whether libLLVM was, and a run whose loaded
   driver is not the requested one (or cannot be proven) fails with
   `mesa_mismatch`, which aborts the sweep with exit 67.
5. **Pi verification** (both locks): kmscube floor, rendered gfaydark +
   growthplane with the default, and the opt-out.
6. Docs: `docs/ci.md`, `docker/CLAUDE.md`, `docs/decisions.md` (dated, Will
   verbatim), `refactor/BOARD.md`, `refactor/B935/README.md`.

## Mechanism decision: `$ORIGIN` is not usable

The brief preferred an `$ORIGIN`-relative rpath baked by CMake. Two facts rule
it out:

- **glibc drops an `$ORIGIN` rpath entry in secure-execution mode unless the
  expanded path is a trusted system directory** (`_dl_dst_substitute`,
  `is_trusted_path_normalize`). willeq is `setcap cap_net_raw,cap_net_admin+ep`
  (B751), so it runs with `AT_SECURE=1`. Measured on dev-u24 (glibc
  2.39-0ubuntu8.9) with a two-file test: an `$ORIGIN/lib` DT_RPATH loads the
  library without caps and fails `cannot open shared object file` with caps;
  an absolute DT_RPATH loads it both ways. Re-measured on the Pi with willeq
  (README).
- **The prefix is not relocatable anyway.** libgbm carries
  `<prefix>/lib/gbm` (where it `dlopen`s `dri_gbm.so`) and libgallium carries
  `<prefix>/share/drirc.d` and `<prefix>/etc/drirc`, compiled in from
  meson's `--prefix`. The prefix must live at the path it was built for.

So: an absolute DT_RPATH (not RUNPATH, B932's reasons) to the pinned Pi path,
written on a copy at publish time.

## File territory

`scripts/pi/{build_mesa_nollvm.sh,mesa_nollvm_select.sh,mesa_nollvm.env,
mesa_nollvm.sha256,deploy_mesa_prefix.sh,publish_arm_build.sh,mesa_ab.sh,
rendered_zone_summarize.py,kmscube_floor.sh}`, `scripts/pi_rendered_zone_sweep.sh`,
`docker/Dockerfile.mesa-nollvm-armhf` (comments only), `docs/ci.md`,
`docker/CLAUDE.md`, `docs/decisions.md`, `refactor/BOARD.md`,
`refactor/B935/`. No C++; no budget, golden, baseline or gate change.

## STOP conditions

- The Pi or eqemu lock is held by someone else for longer than the rig waits.
- The rendered client with the prefix fails to render or to reach
  `steady_60s` where the system Mesa does.

## Verification bar

`ctest -L hygiene` green; the pipeline green; on the Pi: maps show the
shipped libgallium and no libLLVM with the default, the system libgallium
with `WILLEQ_MESA=system`, capability kept (`rcvbuf=... SO_RCVBUFFORCE`).

## Review

See `refactor/B935/README.md`.
