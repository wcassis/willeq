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

EVIDENCE_PLACEHOLDER

## Not verified
NOTVERIFIED_PLACEHOLDER
