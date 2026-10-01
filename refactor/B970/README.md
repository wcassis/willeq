# B970 -- vendor the ARM Irrlicht/libpng archives

GitLab #288 (class B). Branch `task/B970-vendor-arm-archives`, cut from `fable` `788c6b82`.
No budget, golden or gate changed.

## Defect

`docker/Dockerfile.arm-noble` wgot `irrlicht-1.8.5.zip` and `libpng-1.6.37.tar.xz` from
downloads.sourceforge.net inside a Docker layer. That layer re-runs whenever `docker/irrlicht-drm/*`
changes, and on 2026-10-01 SourceForge answered HTTP 522 for ~3 h (build-arm jobs 1896/1916/1926 failed).

## Change

- Store: `/mnt/projects/fable/ci-arm/vendor/` (NFS, next to B935's `ci-arm/mesa/`), never in git.
- Pins: `docker/vendor.sha256`.
  - `effb7beed3985099ce2315a959c639b4973aac8210f61e354475a84105944f3d  irrlicht-1.8.5.zip`
  - `505e70834d35383537b6491e7ae8641f1a4bed1876dbfe361201fc80868d88ca  libpng-1.6.37.tar.xz`
  Both equal a fresh SourceForge download taken 2026-10-01 (the bytes the old Dockerfile fetched), and
  `/mnt/projects/irrlicht-1.8.5.zip` has the same Irrlicht hash.
- `scripts/ci/vendor_fetch.sh --ensure|--check`: verifies the store against the pins, downloads only what is
  missing or wrong (SourceForge, then `sources.buildroot.net`, then for libpng `ftp.osuosl.org`; each candidate
  must hash to the pin; buildroot and osuosl were verified to serve the identical bytes), prints the store path.
  A pin that cannot be satisfied exits 1 naming the file and hash.
- `Dockerfile.arm-noble`: `COPY --from=vendor` the two archives, `COPY docker/vendor.sha256` and
  `sha256sum -c` them, all BEFORE the `docker/irrlicht-drm/*` COPYs; the Irrlicht RUN extracts from `/tmp/vendor`.
- `scripts/build-arm-noble.sh` runs `vendor_fetch.sh --ensure` and passes `--build-context vendor=<store>`.
  `.gitlab-ci.yml` `build-arm` runs `vendor_fetch.sh --ensure` as its first script line.
- x86: `scripts/build-gles2-x86.sh` does not download anything (it copies `/mnt/projects/irrlicht-1.8.5`); no change.
- Not changed (not in CI, still wget from SourceForge): `docker/Dockerfile`, `Dockerfile.arm64-noble`,
  `Dockerfile.arm-cross`. The other wgets in `Dockerfile.arm-noble` (fmt, jsoncpp, glm, cereal, recast,
  openal, libsndfile, fluidsynth) are GitHub and are outside this card.

## Evidence

Offline proof. The image was built from the new Dockerfile with `--add-host` mapping downloads.sourceforge.net,
sourceforge.net, sources.buildroot.net and ftp.osuosl.org to 127.0.0.1 (the only hosts the archives could come
from), `--build-context vendor=ci-arm/vendor`. The build log shows `irrlicht-1.8.5.zip: OK` /
`libpng-1.6.37.tar.xz: OK` (the in-image pin check) and the Irrlicht RUN step passed. (`--network none` for the
whole build is not possible: the later openal/libsndfile/fluidsynth layers rebuild after the changed layer and
wget GitHub.) The failure path: with an empty store and `https_proxy` pointing at a dead port,
`vendor_fetch.sh --ensure` tries every source and exits 1 with
`FAIL: pin 'irrlicht-1.8.5.zip' (effb7bee...) is not in the store`, once per pin.

Identity. Same source tree (this branch's code), same day, both images built with `--no-cache` apt: the OLD
Dockerfile (network wget) vs the NEW one. `libIrrlicht.a` is byte-identical (sha256 a5ab03f1...), the
unstripped `bin/willeq` is byte-identical (sha256 c85985ed...), and so is the stripped one (84ccd086...). An
earlier old-Dockerfile image built from stale layer cache differed in every object because its gcc was
13.3.0-6ubuntu2~24.04 against the current ~24.04.1 (toolchain drift, not the archives).

CI. `glab ci trigger build-arm` on the branch: job 1998 succeeded (2,037 s; the first build with the new layers
re-ran the image), `vendor_fetch.sh --ensure` printed `ok ... (store)` for both archives, and
`/mnt/projects/fable/ci-arm/61c1f083.../` was published normally (bin/willeq, bin/willeq-nollvm, config, data,
mesa, BUILD_INFO). Its `bin/willeq` (sha256 52409c9a..., 11,804,036 B) is NOT byte-identical to the local
stripped build (11,803,996 B): the only section difference is `.comment`, which in the CI binary carries an extra
`GCC: (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0` entry, i.e. objects left in the runner's persistent
`build-arm-noble/cache` from an earlier toolchain are linked in. Not related to the archives.

## Not verified
- A refresh of a pin (a new Irrlicht/libpng) was not exercised; the `--ensure` download path was exercised for
  real once (SourceForge, empty store) but the buildroot/osuosl fallbacks were only verified by hand-downloading
  the same hashes, not by forcing SourceForge down inside the script.
- The CI job always had SourceForge reachable (it was back by 09:00); the offline proof is the `--add-host` build,
  not a live outage.
- `build-arm` on the `fable` branch pipeline (automatic job) was not run; only the manual trigger on this branch.
- `Dockerfile`, `Dockerfile.arm64-noble`, `Dockerfile.arm-cross` still fetch from SourceForge.
- The CI binary differs from a clean local build by the stale-cache `.comment` entry described above; a clean CI
  cache was not tried.
