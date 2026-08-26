# T287 — live composite zone-in on x86, then the Pi measurement (T284 hand-back; bears on decision #3)

Slot: whichever is free. OWNS eqemu (noclobber) and, for part 2 ONLY if
Will says so, the Pi. Class B (measurement). Territory: none in src/ unless
part 1 fails; measurement scripts + review/T287/.

## Part 1 — close T284's x86 gap (do this now, no permission needed)
`loadZoneStep0_EQE1` is only reachable through a real zone load. With
composites ON on x86 (`--eqe1-path`, composites enabled in the preset used
for the run — NOT the orangepi preset), zone in and record from
`EQT_EQE1_REQUEST_LOG_DIR`: packs opened, bytes mapped, and that the
composite sheets came in via `late-open`/index rather than a scan. Compare
against T284's benchmark numbers (1 pack, 174,856 B for a Human). Confirm
entities render (screenshot, 0 magenta) and nothing regressed vs T191/T192's
composite-adoption findings.

## Part 2 — the Pi run: DO NOT START WITHOUT WILL'S GO
Decision #3 (composites OFF on orangepi) was justified by address-space
exhaustion, which T284 removed. Whether to re-enable is Will's call. If he
says go: P02 protocol via `scripts/pi/pi_common.py`, ARM_BUILD_BIN_DIR
pointed at THIS slot, `<=12` invocations, drop_caches not reboot, leave the
Pi pristine. Measure with composites ON: VmPeak, VmRSS, packs opened, bytes
mapped, zone-in time, and whether it completes at all (T193's `std::bad_alloc`
is the failure to watch for). Report numbers only — the preset change, if
any, is a separate unit after Will reads them.

## Verification
Part 1: Review + evidence review/T287/ with the request-log TSVs and the
screenshot. Commit (docs/measurement only unless part 1 finds a defect):
`T287: composite zone-in verified live on x86` (+ ` and measured on the Pi`
if part 2 ran).

---

## Review (executor, slot2, 2026-08-26, off fable `663e576c`)

**Part 1 only. Part 2 was NOT started** — the dispatch was explicit that the Pi
needs Will's go and it has not been given. No Pi invocation, no ARM staging,
the `orangepi` preset is byte-identical to `fable`.

Evidence: `/mnt/projects/fable/review/T287/` (README.md indexes it).
**No source changed** — `git status` is clean against `fable`; this unit is
measurement only.

### What this closes

T284's own flagged deviation:

> **Production's `loadZoneStep0_EQE1()` is not exercised on x86.** … a live
> composite zone-in on x86 would close it.

It is now exercised. Real client, real eqemu, real login zone-in to befallen,
composites ON via `--composite-entities` (the CLI flag that sets
`enableCompositeEntities` — **no preset read or edited**), composite dir ==
`--eqe1-path` == the shipped ETC1 tree, i.e. exactly the layout whose old bulk
scan mapped all 1506 packs / 4.44 GiB.

### Packs opened and bytes mapped

Seven composite sheet packs, **1,049,368 B total**, one per composite-eligible
race/gender in the zone:

| pack | bytes | lookups | `how` | race |
|---|---|---|---|---|
| `dam_hs_def_0_0` | 174,856 | 1 | `late-open` | 6 g0 |
| `gnf_hs_def_0_0` | 174,856 | 1 | `late-open` | 12 g1 |
| `gnm_hs_def_0_0` | 174,856 | 1 | `late-open` | 12 g0 |
| `hom_hs_def_0_0` | 87,496 | 1 | `late-open` | 11 g0 |
| `hum_hs_def_0_0` | 174,856 | 1 | `late-open` | 1 g0 |
| `rat_hs_def_0_0` | 87,464 | 1 | `late-open` | 36 g2 |
| `ske_hs_def_0_0` | 174,984 | 1 | `late-open` | 367 g2 |

**Matches T284's benchmark exactly.** T284 reports *1 pack, 174,856 B for a
Human*. Live, the Human's sheet is `hum_hs_def_0_0`, opened as one pack of
**exactly 174,856 B**. Seven packs rather than one only because befallen has
seven composite races; the per-race figure is identical.

### How the sheets were resolved: index, not scan

Seven `EQE1: late-open … (1 owner(s); none in the candidate set) … (rule R2)`
lines, one per sheet — `preloadModelData` → `getOrLoad` miss → the step-0
resolver → `openArchiveContaining()` → `EQE1TextureIndex::resolve` → rule R2 →
one open. `served_by == name` in every TSV row, confirming the self-named-pack
property T284 depended on. `openCompositeSheetEQE1()`'s split-directory
fallback correctly did not fire (dir == eqe1Path).

Those seven are the **only** late-opens in the session (the request log's `how`
column: 7 `late-open`, 263 `-`). **0 NOT-FOUND, 0 FATAL** over 270 distinct
names.

### Composites ON maps LESS, not more (x86, mapped bytes)

| | composites ON | control (OFF) |
|---|---|---|
| archives open at zone-in | 17 | 11 |
| total mapped | **38,898 KB** | 42,169 KB |
| wasted opens (T214 rule) | 0 | 0 |
| distinct names, whole session | 270 | 309 |

Reconciles to the byte: `42,169 − 4,296 (beholder_chr, control-only) + 1,025
(the 7 sheets) = 38,898`. A sheet hit defers the race's per-part textures, so
`beholder_chr` was never requested at all and `global_chr` served 128 names
instead of 160. **This is a mapped-bytes observation on x86, not a Pi memory
claim** — decoded-sheet RSS is unmeasured and is a Pi question.

### Entities render; no T191/T192 regression

Four 800x600 captures (daylight forced with `#set time 11 0`, client confirmed
rendering after), **0 magenta pixels** in all four under a strict
`r>200 && g<60 && b>200` count. Real frames of a textured character, no
placeholder checkerboard. Will judges the pixels; no self-assessment here.

T192's retraction sets the bar for a correctly-configured composite run
(*2 composite builds, 0 "not yet uploaded"*). Reproduced live:
`buildCompositeMesh` **2** (race 367 SKE 1053v/13tex/1 buffer; race 36 RAT
904v/8tex/1 buffer), `not yet uploaded` **0**, `empty input data` **0**. The
two `composite path NOT taken` cases are `compositeAllowed=false` on a second
race-367 entity and on the player — the P37/P48 equipment/variant eligibility
rule, which the control run shows too. Not a regression.

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | exit 0 (tree already current; no source changed) |
| `ctest` | 3160/3162 under `-j8`; see note |
| visual gate | **PASS** standalone (`visual_gate_rerun.log`), goldens byte-untouched |
| `git status` | clean — no `src/`, `include/`, `tools/` file touched |

`check_file_sizes.sh`, `build-gles2-x86.sh` and `build-arm-noble.sh` were not
re-run: this unit adds no code, only `refactor/T287_*.md` and `review/T287/`.

### Deviations / notes

- **ctest ran at `-j8` and `visual_gate` failed its befallen p95-CPU ceiling by
  1.2 %** (40.641 ms vs 40.144 ms). Re-run alone it passes (exit 0). With zero
  source changes the gate cannot have regressed; the failure is host load I
  caused. Flagged, not waived silently. The other failure is
  `AudioManagerTest.PreloadCommonSounds`, the documented ALSA host quirk.
- **Zone-in time 7,230 ms (composites) vs 8,234 ms (control) is ONE SAMPLE
  EACH** on a shared VM under llvmpipe. Recorded, not claimed. No speedup is
  being asserted.
- Server hygiene: `diff db_before.txt db_final.txt` **EMPTY**. Two writes, both
  reverting this unit's own effects (two GM-spawned `a_fire_beetle` rows in
  `zone_state_spawns`; Testchar's `/camp`-rounded position). One residual
  difference in `zone_state_spawns` — spawn point 7083 `npc_id=0` → `36030` —
  is an ordinary roamer respawn the zone re-rolls on next boot, not a write of
  mine. Pre-existing `npc_id=1063` GM leftovers and Testchar's 01:29 corpse
  were left untouched. Lock taken atomically as `T287`, clean shutdown, lock
  deleted.
- **Part 2 remains open and needs Will's go.** T284 showed the address-space
  argument behind decision #3 no longer describes the code; this unit shows the
  index-driven open works in the real client. Neither is a Pi measurement, and
  neither re-enables composites anywhere.
