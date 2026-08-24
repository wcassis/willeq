# T193 — Re-measured with `--eqe1-path`: the Pi cannot load the zone at all

T192 established that every earlier measurement ran with `eqe1Path=''` — the
non-EQE1 asset path — so T186's zone-in numbers were taken on the wrong
pipeline for the Pi, whose whole reason for ETC1/EQE1 is memory. Re-run with
`--eqe1-path /mnt/projects/EverQuest_ETC1`, same script, same zone.

## Result

| befallen zone-in (Phase 5 → Phase 15) | without EQE1 (T186) | with EQE1 |
|---|---|---|
| **Pi** | ~37.7s | **FAILS — `std::bad_alloc`** |
| **x86** | ~12.1s | **13.25s** |

**The Pi cannot complete a zone load with EQE1 enabled.** It dies during the
sequential load:

```
loadZoneSequential: [48%] Opening EQE1 textures...
EQE1: loaded 664 composite entity archives from '/mnt/projects/EverQuest_ETC1'
[FATAL] LoadingThread: zone loading failed: std::bad_alloc
[FATAL] checkLoadingComplete: loading thread failed — shutting down
```

The Pi has 483MB total, ~380-400MB available. The load opens **664 composite
entity archives** — the whole directory — as one eager step at 48%, before any
entity needs one.

**On x86 EQE1 is slightly SLOWER, not faster** (13.25s vs 12.1s;
`seq_entities` 5594ms vs ~5400ms). ETC1's benefit is memory, not speed, so that
is not itself surprising — but it means EQE1 buys nothing on the machine where
it works and breaks the machine it was built for.

## What this means for the earlier numbers

- **T186's Pi 37.7s and x86 12.1s stand as measurements of the non-EQE1 path**,
  which is what the client actually runs today by default (no preset sets
  `eqe1Path`; it is CLI-only).
- There is **no EQE1 Pi baseline to compare against**, because the Pi cannot
  produce one until the eager archive load is fixed.
- T187's per-step split (`seq_entities` = 64%) likewise describes the non-EQE1
  path.

## The defect

`EQE1: loaded 664 composite entity archives` is eager and unconditional: every
composite archive in the directory is opened during zone load regardless of
which races the zone actually contains. On x86 that is affordable; on the
target device it is fatal.

Befallen needs a handful of races (T187 counted 12-13 distinct race/gender
pairs loaded, and T105 measured 6-9 as actually needed). Opening 664 archives
to serve ~12 is the shape of the problem.

**Not fixed here.** The obvious direction is to open composite archives on
demand — the same deferral `preloadModelData` already applies to per-part
textures — or to filter the directory scan by the zone's actual race set.

## Method

Pi ran the T186-era binary (`/tmp/willeq_t186`, fable `4b6d63be`), unchanged in
this area since, so the only variable is `--eqe1-path`. Both machines re-zone
with `#zone` first (T190). x86 numbers from `t193_x86.log`, Pi from
`t193_pi.log`; both in `review/T193/`.

---

## Attempt at the fix (T194) — reverted; the naming assumption was wrong

**Design tried:** stop opening the composite directory eagerly. Index it by
filename at zone load (no `open()`, no mmap), and open one archive on demand
when a texture lookup misses — targeted, because a composite sheet's name looked
like its filename (`ske_hs_def_0_0` → `ske_hs_def_0_0.eqe1`). This mirrored what
P04 already did for global archives.

**On x86 it did exactly what it was designed to do:**

| | before | after |
|---|---|---|
| archives opened at zone load | 664 | **10** (on demand) |
| composite meshes built | 2 | **2** (unchanged) |
| archives indexed (filenames only) | — | 1506 |

**And it broke the zone load anyway:**

```
[FATAL] Texture 'fishe0101.bmp' not found in any EQE1 flat pack.
```

**A composite archive is not named after the textures it contains.**
`fis_hs_def_0_0.eqe1` holds `fishe0101.bmp` and others — the T193 log said so in
passing and I read past it:

```
EQE1Loader: mmap'd '.../fis_hs_def_0_0.eqe1' (2 textures, 1 alpha, 0 raw, ...)
```

So texture-name → filename is **not 1:1**, and a targeted open can never find
per-part textures. The eager load was doing double duty: it was also the only
thing that made every packed texture findable. Reverted; `fable` is unchanged.

## The correct shape of the fix

The index has to be built from each archive's **table of contents**, not its
filename: for every `.eqe1`, read its TOC and record `texture name → archive
path`, then open the archive on first reference. If `EQE1Loader::open()` can be
made to read a TOC without retaining the payload (or if the mapping can be
dropped after indexing), this costs one pass over 1506 files at zone load and
keeps only referenced archives resident.

**Open question that decides the design:** what does `EQE1Loader::open()`
actually retain? If it only mmaps, the resident cost should be page-cache and
not heap — in which case the Pi's `std::bad_alloc` comes from something else in
that step and this whole approach is aimed at the wrong thing. **Measure the
Pi's RSS across "[48%] Opening EQE1 textures" before designing further.**
