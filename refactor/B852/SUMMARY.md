# B852 sweep summary — 118-zone offline content sweep

Run: 2026-09-20, x86 dev VM (this host), llvmpipe surfaceless EGL, `build/bin/gles2_zone_benchmark`
from `task/kb-b852-zone-content-sweep` @ 3fbe3a48 (origin/fable tip at run time).
Invocation per zone: the gate's memory-coverage form (`--eqe1-path/--objects/--entities 5/
--test-races 1,2,3,4,5 --occluder-budget 16`, `--frames 5 --warmup 1 --res 800 600`,
`--log-level=INFO`), one process per zone, 300 s timeout + 120 s silence watchdog.
Clean definition: exit 0, zero FATAL/ERROR, `Zone loaded in` present, zero
placeholder models, peak RssAnon < 256 MB (docs/decisions.md T293 envelope).

## Counts per expansion [measured]

| expansion | rows | clean | failed | died on the 4 GiB cap |
|---|---|---|---|---|
| classic | 72 | 70 | 2 (hole, soldunga) | 0 |
| kunark  | 28 | 28 | 0 | 0 |
| velious | 18 | 18 | 0 | 0 |
| **total** | **118** | **116** | **2** | **0** |

Every Kunark and Velious zone loads clean — including sebilis, citymist,
frozenshadow, iceclad, velketor, necropolis and thurgadina, which the CI gate
has never opened. Both failures are classic zones.

## Failed zones by name [measured]

| zone | expansion | status | why | issue |
|---|---|---|---|---|
| ~~**soldunga**~~ | classic | ~~failed, exit 1~~ **closed by B100 (2026-09-21)** | ~~FATAL: `Texture 'gnome_stronghold_floor_transition.bmp' not found in any EQE1 flat pack` — the pack stores the name truncated to 31 chars (`gnome_stronghold_floor_transiti`); the 33-char lookup never matches.~~ B100 shipped the EQE1 v3 name table (builder/loader/verifier); the pack is now v3 and soldunga loads clean with `--eqe1-path` (see `refactor/BOARD.md` B100 strike, 2026-09-21). | **#100** |
| **hole** | classic | failed, exit 0 | Loads and renders fine, but peak RssAnon 273,870,848 B = 261.2 MB, 5.2 MB over the 256 MB Pi ceiling (standalone re-run: 261.1 MB, stable). Next-highest zone is timorous at 247.8 MB. | **#101** |

No zone died on the 4 GiB cap and no zone hit the timeout.

## Heaviest zones (peak RssAnon, all under the ceiling except hole)

| zone | expansion | peak RssAnon |
|---|---|---|
| hole | classic | 261.2 MB (**over ceiling**) |
| timorous | kunark | 247.8 MB |
| growthplane | velious | 236.0 MB |
| iceclad | velious | 228.8 MB |

## Artifacts

- Committed 118-row results TSV: `refactor/B852/results_118.tsv`
- Bulk per-zone logs + run log: `/home/user/kb-evidence/b852-zone-content-sweep/full/`
  (`logs/<zone>.log`, `results.tsv`, `../full_run.log`)
- The 3-zone ctest smoke (qeynos2 classic / sebilis kunark / iceclad velious)
  is green on this tree: 12.3 s, all three clean.
