# T496 — the Pi's own shrink queue, then the largest category at the root

Class A/P. **Will's standing decision, 2026-09-17:** "the goal is willeq's RSS
footprint between 32 and 64 MB" (256 MB is an interim ceiling). T491 gave 100%
attribution **on x86** and printed the shrink queue; T490 proved **on the Pi** that
the gap is the pre-zone baseline. This unit makes the queue a **Pi** measurement
and then attacks its head. Leg A needs the **LOCAL eqemu lane + the Pi**; Leg B is
offline. Board entries start at **B816**. Requires fable to carry T490+T491+T495
(the chain) — rebase onto that tip first.

## What exists (read first)

- `include/client/core/memory_ledger.h` / `src/client/core/memory_ledger.cpp`:
  18 categories, `MemoryScope`, `snapshot/recordPoint/toLogLine`, `gapToTarget()`,
  the smaps census (`readMappings`/`mappingsToLogLines`, owner rows
  `[heap]`/`[stack]`/`[anon]`/`so:`/`file:`). [measured] Emission points wired:
  `application.cpp:805,811` (`startup` + `[memmap]`), `bridge_component.cpp:905-911`
  (`login_world`, `ready` + `[memmap]`), seven zone-load points,
  `player_component.cpp:118` (`camp`), `steady_60s`, and `/pmem`
  (`commands_component.cpp:129`, `irrlicht_bridge.cpp:1094`). **Nothing new has to
  be written to take the Pi measurement.**
- `EQT_MEMORY_LEDGER` ON by default (`CMakeLists.txt:29`); `scripts/build-arm-noble.sh`
  builds `willeq` and `gles2_zone_benchmark` for armhf. [measured]
- Pi lane: `EQEMU_HOST=local` + `scripts/eqemu/env.sh`; Pi config host 10.0.30.23
  (local copy `review/T490/pi/willeq_pi_local.json`), Pi 10.0.30.192 armv7l 483 MB;
  `review/T490/scripts/pi_sweep.sh` (settle → update → READ BACK `character_data`,
  refuse on mismatch — B802) driving `review/T490/scripts/pi_zone_run.sh` (0.5 s
  RssAnon/VmSwap sampler, watchdog, dmesg OOM capture); deploy shape in
  `review/AUDIT-0914/scripts/run_pi.sh`. [measured]
- Starting numbers: Pi nro `00_start` 192,284 kB anon → `13_Cleanup` 190,548 kB
  (a loaded zone costs ~0 anon; the ~126 MB gap to 64 MB is the pre-zone baseline,
  B801) [measured]; x86 queue `entity_models` 64.56 / `startup` 49.73 /
  `textures_driver` 30.50 / `objects` 28.91 / `irrlicht_scene` 27.95 / `zone_mesh`
  20.26 / `malloc_slack` 17.43 / `textures_decoded` 13.04 MiB [measured, x86];
  every driver-coupled row is llvmpipe and [assumed] for Mali (B812). x86 startup
  heaptrack heads: `createSoftwareCursor()` 8.70 MiB, `initLoadingScreen()` 7.47,
  `ParticleManager::init()` 4.19, `decodeBMP()` 4.19 — ~9.5 MiB of the 88.85 MiB x86
  startup peak is in willeq frames; the x86 trace does NOT explain the Pi's 190 MB.

## Tasks

### Leg A — settle B812: the Pi's shrink queue (lock; FIRST; no source changes)
1. Cross-build at the tip (`scripts/build-arm-noble.sh`), deploy per `run_pi.sh`.
   A measurement leg that edits the thing it measures is not a measurement.
2. One `willeq` run per gate zone (qeynos2, gfaydark, befallen) + iceclad,
   `--drm --constrained orangepi -r 1280 720 --load-mem-audit`, through
   `pi_sweep.sh`/`pi_zone_run.sh` (B802 read-back guard, watchdog, `=== Entering
   <zone> ===` re-check). **Every row names the zone actually entered.** iceclad is
   expected to OOM at step 4 (B800); its `startup`/`ready` lines are still the point.
3. Per run: `[memledger]` at `startup`, `login_world`, `ready`, `steady_60s`;
   `[memmap]` at `startup` and `ready` (top 16 owners); one `/pmem` at `ready`;
   one `gles2_zone_benchmark --zone <Z> --memory-json` per zone as the headless
   cross-check.
4. Output `review/T496/pi/table_categories.tsv`: per zone, per bucket live and
   peak, `unattributed`, `heap_uncharged`, `malloc_slack`, `unaccounted`, non-heap
   anon by owner, `over_target` vs 64 MiB, **sorted largest-first** (`gapToTarget()`
   order), plus the same for `startup` alone. Every cell [measured]; nothing from
   x86 in it.
5. Board **B816** with the table; **strike B812** with the measured
   `startup`/`textures_driver`/`irrlicht_scene`/non-heap numbers; Mali budgets for
   those three in the orangepi preset ONLY if stable across the four runs, else say
   why not. Close B812's second half: hook cost on ARM from the benchmark's
   `frame_allocs` × the per-pair delta.

### Leg B — the head of the Pi queue, at the root (offline, red first)
Target = whatever Leg A names first. Deliverable: a headless red test asserting
that category's bytes under a stated bound, failing at this tip on x86, with the Pi
run as confirmation.
- **If `entity_models`** (B814): `loadedModels_` is shared per race [measured] but
  `variantModels_` (race,gender,variant) may retain a full geometry+skeleton copy
  per texture variant [assumed — header comment says so; the byte split does not
  exist]. Third term: `otherChrCaches_`/`characterModelBytes`. Red: split
  `MemoryStats` per variant; assert `bytes(N variants) − bytes(1) ≤ B` (texture
  bindings only). Root fix: share geometry/skeleton across variants. No pixel changes.
- **If `startup`** (likelier — the Pi baseline is 190 MB): candidates confirmable
  from Leg A's `[memmap]` + `startup` line without new code: `createSoftwareCursor()`
  8.70 MiB and `initLoadingScreen()` 7.47 (one-shot decode staging retained past
  upload?), `ParticleManager::init()` 4.19, `decodeBMP()`/`processTextureData()`
  staging, the orangepi texture cache (44,116 kB by the Pi's own audit line
  [measured]), the Irrlicht device/DRM surface (shows as `so:`/`[anon]`, not in the
  ledger). Red: after startup, `startup` live ≤ bound, or a named staging buffer is
  zero once resident. Root fix: free staging after upload / bound the atlas — never
  "shrink the image".
- Green on the same red; re-measure that category in one confirming Pi run with the
  Leg A recipe verbatim; move the x86 gate's per-category budget down (T293 headroom).

### Leg C — record
`docs/decisions.md` (the Pi queue replaces the x86 queue as the planning list);
BOARD B816 (queue / B812 struck), **B817** (root cause + its red), **B818** deferred
items; `src/client/graphics/CLAUDE.md` + `docs/module_map.md` if Leg B touches them.

## Proof
`cmake --build` exit 0; full ctest green incl. the new test; `build-gles2-x86.sh`;
`build-arm-noble.sh`; `visual_gate.py` PASS with goldens untouched;
`check_file_sizes.sh`. Plus `review/T496/pi/table_categories.tsv` with every row's
actual zone, and a before/after Pi number for the category attacked.

## STOP (report, do not proceed)
1. Anything needing a golden re-bless (adding iceclad/Velious to the gate = Will's
   call, open half of B813).
2. Anything that changes what is drawn (texture size, variants, cursor/loading-screen
   resolution, particle sets). Only lifetime/sharing/staging changes are in scope.
3. Leg A's four runs disagree by > ~10% on `startup` live bytes.
4. The head of the Pi queue is NON-HEAP (Mali/DRM mappings): report the `[memmap]`
   owner rows and stop — a driver/surface decision.

## Rules
- LOCAL lane only; lock `eqemu_lock_take T496`; release on every path; down.sh.
  Never the VM, never hermes-ext. No Testchar. Private scratch `scratchpad/T496/`.
- No ccache; -j8; `review/` beside the checkout; visual_gate flaky under load —
  rerun alone on a quiet host.
- Territory: `review/T496/**`, `refactor/T496_*.md`, BOARD, decisions.md; Leg B:
  ONE of `race_model_loader.{cpp,h}` or `renderer_init.cpp`/`irrlicht_renderer.cpp`
  + its test + `config/constrained_presets.json` if a budget moves. NOT the gate's
  zone set, NOT goldens, NOT `zone_bsp*`/PVS (T490), NOT the ledger's categories.

## Review (executor, 2026-09-17)

Every claim below is tagged **[measured]** or **[assumed]**.

## 0. What did not work / deviations, first

1. **The plan's own candidate list carried a unit error.** It reads "the
   orangepi texture cache (44,116 kB by the Pi's own audit line [measured])".
   `src/client/graphics/load_mem_audit.cpp:48` prints that field with `%zu`
   from a `size_t` of **bytes**, and the same line reports
   `accounted_kb=43`. The startup texture cache is **43 kB**, not 43 MiB, and
   was never a shrink candidate. **[measured]**
2. **Leg B's target fell outside the plan's declared file territory.** The plan
   scoped Leg B to `race_model_loader.{cpp,h}` **or**
   `renderer_init.cpp`/`irrlicht_renderer.cpp`, anticipating `entity_models` or
   `startup`. Leg A named **`audio`**, a category the x86 queue does not list
   at all, so Leg B touched `src/client/audio/{audio_manager,midi_player,
   music_player}.cpp` instead. The plan's own instruction — "Target = whatever
   Leg A names first" — was followed over the file list. Three files, one
   lifetime change each, no behaviour change. **Flagged for the orchestrator.**
3. **`gfaydark` did not reach `ready` on the Pi** inside a 360 s watchdog: no
   OOM, `LoadZoneSources` finished in 403 ms, and the run died on the duration
   cap with only `startup` and `login_world` recorded. B751's shape on a zone
   that loads elsewhere. Boarded (B818.4), not chased. **[measured]**
4. **`iceclad` OOM-killed at `spatial_bsp`**, as B800 predicts. Its
   `startup`/`login_world` lines are intact and are the point. **[measured]**
5. **No `/pmem` was issued.** `/pmem` is a slash command and the Pi runs
   headless under DRM with no input rig. It is not needed: `/pmem` calls
   `toReportText()` over the same `snapshot()` that `recordPoint("ready")`
   already logs, and `recordMappings("ready", 16)` already emits the `[memmap]`
   census B812 asked for. Same bytes, no extra rig. **[measured]**
6. **The ledger was SILENT on the Pi until this unit.** `[memledger]` and
   `[memmap]` are `LOG_INFO` on `MOD_MAIN`, and the Pi runs at the default
   quiet level: `grep -c '\[INFO\]'` over every T490 Pi log returns **0**.
   `review/T496/scripts/pi_zone_run.sh` adds `--log-module=MAIN:INFO`, which
   overrides the global level per module (`logging.h:182`) without the global
   firehose on a 242 MB tmpfs. **[measured]**

## 1. Leg A — the Pi's shrink queue (B816), and B812 struck

Four runs, `--drm --renderer gles2 --constrained orangepi -r 1280 720
--load-mem-audit`, through `review/T496/scripts/pi_sweep.sh` (B802 read-back
guard) and `pi_zone_run.sh` (watchdog + stall deadline). **All four
`zone_actual` matched the request**; the table
(`review/T496/pi/table_categories.tsv`) is generated from the client's own
`[memledger]`/`[memmap]` lines and names the entered zone on every row.
**Nothing in it comes from x86.**

**At `ready`, largest-first (MiB live):**

| bucket | qeynos2 | befallen | x86 (T491) |
|---|---:|---:|---:|
| **`audio`** | **47.57** | **47.57** | *not in the top ten* |
| `malloc_slack` | 33.66 | 21.94 | 17.43 |
| `startup` | 26.82 | 26.82 | 40.5–49.7 |
| `zone_mesh` | 20.79 | 31.94 | 20.26 |
| `entity_instances` | 18.52 | 28.24 | — |
| `entity_models` | 22.87 | 18.59 | 64.56 |
| `game_state` | 7.23 | 8.62 | — |
| `spatial` | 6.28 | 5.53 | — |
| `zone_parse` | 5.80 | 7.00 | 8.52 |
| `objects` | 2.89 | 3.83 | 28.91 |
| `irrlicht_scene` | 1.45 | 1.11 | 27.95 |
| `textures_driver` | 0.67 | 0.56 | 30.50 |

**At `startup` alone** — identical on all four runs to **0.003 %**
(28,125,352 / 28,126,096 / 28,125,480 / 28,125,496 B for `startup`;
49,881,176 / 49,881,200 / 49,881,112 / 49,881,200 B for `audio`):

| bucket | live | peak |
|---|---:|---:|
| `malloc_slack` | **110.34 MiB** | — |
| `audio` | **47.57 MiB** | **171.29 MiB** |
| `startup` | **26.82 MiB** | 31.12 MiB |
| `unaccounted` | 3.42–3.46 MiB | — |
| everything else | < 0.1 MiB | |

RssAnon at `startup` **188.34 MiB**, `over_target` vs 64 MiB **124.34 MiB**.
**This is T490's B801 gap, now attributed.** **[measured]**

**Non-heap anon, from the client's own `[memmap]` census** (qeynos2, `ready`):
`[heap]` 136.4 MiB in 11 mappings; `[anon]` **48.2 MiB in 34 mappings**;
everything else — `libgallium` 0.64, `libLLVM` 0.58, `file:willeq` 0.14, and
~15 more libraries — under 0.7 MiB **combined**. The `[anon]` block is glibc
servicing large `malloc`s by `mmap`, **not a Mali/DRM mapping**: the ledger
charges every byte of it, and `ledger` (170.2 MB) exceeds `[heap]` anon
(136.4 MB) by exactly that shape. **STOP condition 4 does NOT fire.**
**[measured]**

**B812 is struck.** Its premise — that the x86 figures would flatter the Pi —
is **refuted in direction**: `textures_driver` and `irrlicht_scene` are
**40–50x SMALLER** on Mali, and `startup` is 1.5–1.9x smaller. Budgeting them
from x86 would have shipped ceilings far too loose. All three are stable across
the four runs, so they are **ready to budget** — I did **not** move
`config/constrained_presets.json`, because a budget change in a measurement leg
is the thing the plan's own first line forbids ("A measurement leg that edits
the thing it measures is not a measurement"). Boarded **B818.1**.

**B812's second half (hook cost on ARM) is NOT closed.** It needs
`[memledger] frame_allocs`, which only `gles2_zone_benchmark` emits, and the
four Leg A runs are `willeq`. Boarded **B818.2** with the one command that
closes it. **[measured that it is missing; the cost itself is [assumed] on
ARM]**

## 2. Leg B — the head of the queue, at the root (B817)

**`audio` is 47.57 MiB live at EVERY lifecycle point** — `startup`,
`login_world`, `ready`, `steady_60s`, on every zone, to within 3.6 kB. A
constant that large is a load, not a working set. Two defects, one cause:

1. **`~MidiPlayer` leaked outright.** It cleared `impl_->soundFont` and then
   closed only `gmSoundFont`/`customSoundFont`. `soundFont` aliases one of
   those **only on the dual-font path**; the single-font `init(path, rate)`
   overload loads into `soundFont` alone — so that font was never closed.
2. **`MusicPlayer::setMidiPlayer()` abandoned its fallback.** It overwrote
   `midiPlayer_` and left `ownedMidiPlayer_` alive holding a complete second
   copy of `synthus2.sf2`. `AudioManager::initialize()` called
   `initialize()` **before** `setMidiPlayer()`, so the shipping client always
   took that path.

**RED, offline on x86, in bytes** (`tests/test_music_player_soundfont_lifetime.cpp`,
3 cases, ledger-linked so the assertion is on `Category::Audio` live bytes):

```
SupersedingTheOwnedMidiPlayerReleasesItsSoundFont
  retained 24,160,112 vs bound 1,048,576          FAILED
AFallbackSoundFontIsNotSmall  (destructor half)
  retained 24,159,872 vs bound 1,048,576          FAILED
```

**GREEN after: 3/3.** The fix is lifetime only — `MusicPlayer` renders through
the shared `MidiPlayer` on either ordering, so no audio output changes.
`AudioManager` now wires the shared player **first**, so the duplicate is never
loaded at all and the transient leaves the startup peak too.

**Confirming Pi run, `qeynos2_T496fix`, Leg A recipe verbatim:**

| | before (`qeynos2_T496`) | after (`qeynos2_T496fix`) | delta |
|---|---:|---:|---:|
| `audio` live @ `startup` | 49,881,176 | **25,722,488** | **−24,158,688 (−23.04 MiB)** |
| `audio` live @ `ready` | 49,884,824 | **25,726,104** | **−24,158,720** |
| `audio` **peak** @ `startup` | 179,607,200 | **155,448,424** | **−24,158,776** |
| RssAnon @ `startup` | 197,492,736 | **174,211,072** | **−23,281,664 (−22.2 MiB)** |
| RssAnon @ `ready` | 195,428,352 | **181,243,904** | −14,184,448 |
| **peak RssAnon, whole run** | 212,828 kB | **186,640 kB** | **−26,188 kB (−25.6 MiB)** |
| `over_target` vs 64 MiB @ `startup` | 130,383,872 | **107,102,208** | −23,281,664 |

The Pi delta matches the x86 red/green to **1.4 kB**. `audio` is no longer the
largest bucket at `ready`. Every other category moved by < 0.01 %. **[measured]**

**No x86 gate budget was moved.** `audio` has never had a
`memoryLedgerBudgets` entry (the orangepi preset budgets eight categories and
`audio` is not one of them), so there was no per-category budget to lower;
setting a first one for it is a budget change and belongs with B818.1.

## 3. STOP conditions

| STOP | fired? |
|---|---|
| 1. golden re-bless | no — goldens untouched, gate zone set untouched |
| 2. changes what is drawn | no — lifetime/sharing only, no texture/variant/cursor/particle change |
| 3. four runs disagree > ~10 % on `startup` live bytes | **no — they agree to 0.003 %** |
| 4. head of the queue is NON-HEAP (Mali/DRM) | **no — `audio` is malloc'd and the ledger charges it; the Pi's non-heap anon is glibc's own mmap region** |

## 4. Verification bar

(filled below)

## 5. Lane state

LOCAL lane only. Lock taken as `T496` and **released**; world **DOWN**
(`down.sh`, ports 5998/9000 closed); Testcaster's `character_data` restored to
`zone_id=34, x=-41.8658, y=2723.33, z=-24.8438`, the row saved at
`review/T496/pi/testcaster_original_row.txt`. The VM at 172.16.200.99 and
hermes-ext were never touched. No Testchar. Pi left idle, no willeq running,
`/tmp/t496` removed by the run script.
