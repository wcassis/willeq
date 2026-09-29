# B854 / #102: the RENDERED Kunark + Velious sweep on the Orange Pi

**Date**: 2026-09-29 (UTC). Main sweep 03:42:25 to 05:30:41. Eligibility leg 05:36:57 to 05:49:46.
**Branch**: `task/B854-rendered-pi-sweep` (from fable 3c6ea8b3).
**Why**: B854's sweep (`SUMMARY.md`) ran `--scenario zone_in`, which is headless, so no graphics were ever
initialised. This sweep runs the RENDERED client on the Pi, on the orangepi preset, with the memory ledger.

## Verdict, plainly

- **Zone-in rendered on the Pi: 45 of 46 zones** (a verified ready point in the right zone, a render
  proven, and steady_60s measured). **39/46 passed on the first attempt.** The one zone never measured is
  **charasis**: the server kills the character at its safe point within 1 to 8 s of `ready`, in all four
  attempts.
- **No OOM kill in any run** (dmesg diff per zone; 57 client runs across both legs).
- **Against the 64 MB design yardstick: FAIL for 44 of 45 zones** (peak RssAnon). Only nurga peaks under
  64 MB (60.3). At steady_60s, 41/45 are over 64 MB.
- **Against the 256 MB failing bound: PASS for 45/45.** The worst peak RssAnon is 159.2 MB (growthplane) and
  the worst VmHWM is 218.7 MB. VmHWM is total resident memory, including file-backed driver libraries.
- **The four heavy zones**, all rendered and all under 256 MB:

  | zone | result | peak RssAnon | steady RssAnon |
  |---|---|---|---|
  | timorous | pass, first attempt | 138.5 MB | 131.0 MB |
  | iceclad | pass, first attempt | 115.3 MB | 108.8 MB |
  | growthplane | failed the first attempt (a server kick mid-load, then a client FATAL); passed on retry | 159.2 MB | 152.2 MB |
  | emeraldjungle | pass, first attempt | 137.4 MB | 121.7 MB |

  iceclad stalled at Phase 9/15 in the headless B854 sweep. Here, with CAP_NET_ADMIN, it passes.

## The binary

| | |
|---|---|
| source commit | fable **3c6ea8b3**, which includes MRs !39/!41. The published build dir used is `ci-arm/0effd2fbb7c17739adcc05f7c3dc98f5a16289aa`: fable moved to 0effd2fb (a docs + script merge, B874) while the rig was being built. |
| sha256 | **6888f621fef9cc1cb95ca4b3d45167f7d9111b2962e73cf8cf1364afb8682526**. This is byte-identical to `ci-arm/3c6ea8b3.../bin/willeq`, and to the Pi copy that was run. |
| Pi copy | `/home/user/pi-rendered-sweep/willeq`, `cap_net_admin,cap_net_raw=ep` |
| per-leg identity | `refactor/B854/rendered/binary_identity_{full,eligibility}.txt` |

fable moved again during the sweep, to 2f18b55e (B911/B912/B914), which is a DIFFERENT binary. The
eligibility leg was therefore re-run with `--pi-dir` pinned to the same sha256. The unpinned first attempt
was aborted before it wrote a row (`review/B854-rendered/aborted-eligibility-unpinned-binary/`).

## How the client was driven, and how rendering was proven

- **Launch.** `willeq -c client.json --drm --renderer gles2 --constrained orangepi -r 1280 720
  --eqe1-path /mnt/projects/EverQuest_ETC1 --log-level=INFO --script tour.txt --dump-frames-dir <d>
  --dump-every 100 --dump-max-frames 3`. It runs ON THE PI under `scripts/pi/rendered_zone_run.py`, with
  `WILLEQ_DATA_ROOT` set to the ci-arm build dir.
- **Why `--script`.** `--scenario` would force the headless lane. `tour.txt` is `/loc`, `wait 240`, `/loc`,
  `/q`. The client holds a command line until it is fully connected.
- **Ending the run.** The runner sends SIGTERM (the client's clean requestQuit path) 5 s after the ledger's
  `steady_60s` line. A fixed script wait could not do this: the held command fires at loading Phase 11,
  which is 5 to 25 s before `ready` in these runs. smoke-2 is where this was found.
- **Rendering proof, per zone.** All of the following held for all 45 verified zones:
  - the ledger's `zone_mesh`, `textures_decoded`+`textures_driver` and `irrlicht_scene` are NON-ZERO at
    `ready`. The smallest were 1.5, 0.35 and 0.33 MB.
  - 3 frames were dumped at 1280x720, and every one is a real non-blank image: more than 32 colours at
    160x90 and a luma stddev above 4. That is 135/135 frames.
  - at least 1,050 rendered frames per zone. This is counted from the client's own `RENDERER STATS` blocks
    (one every 150 frames) and from the frame-dump counters: 93,300 frames in total.
  - frames were inspected by eye for timorous (a beach and sea under a sky) and dalnir (a wall plus the
    UI). Frames from a static scene come out byte-identical; that is expected, not a fault.
- **Memory.**
  - The peak RssAnon is the max of the 20 ms `/proc/<pid>/status` samples and every `[memledger]` point.
  - Steady is the `steady_60s` point.
  - The category tables are the ledger's own lines at `ready` and `steady_60s`.
- **FPS** is the client's `FPS:` line (every 150 frames). Median per zone: 20 to 60. Median of medians: 30.
  The per-zone minimum (9 to 10 in many zones) includes the frames around a frame dump (glReadPixels +
  PNG encode), so it is not a performance figure.
- **Composite entities are OFF.** `config/constrained_presets.json`'s orangepi block has no
  `enableCompositeEntities`, so the C++ default `false` applies
  (`include/client/graphics/constrained_renderer_config.h`), and no `--composite-entities` was passed.

## Capability / rcvbuf (B751, #73)

- **Finding: the CI-published binary carries NO file capability.** `getcap` prints nothing, and it cannot
  carry one, because `setcap` does not work on NFS.
- **The documented "copy to a local filesystem" recipe fails if the copy goes to `/tmp`.** On the Pi,
  `/tmp` is tmpfs mounted `nosuid`, and the kernel ignores file capabilities there.
- **What works.** Copy the binary to the Pi's ext4 home and run
  `sudo setcap cap_net_raw,cap_net_admin+ep` on it (T504's `pi_run.sh` did the same). The sweep does this
  itself.
- **Result.** Every one of the 57 client runs logged
  `rcvbuf=16777216 bytes (SO_RCVBUFFORCE, CAP_NET_ADMIN; asked 8388608)`. The Pi's `net.core.rmem_max` is
  1,048,576 and was NOT changed.
- **Corollary.** A run of the published binary as-is (B854's) gets the 2 MiB rmem_max-capped buffer. That
  is the likely difference behind B854's headless iceclad stall. This was not re-measured without the
  capability.

## Failing zones and their signatures

| zone | attempts | signature | whose |
|---|---|---|---|
| **charasis** | full sweep first and retry, then eligibility first and retry: 0/4 | The server kills the character at the safe point (0,0,4) and sends it to its bind (qeynos). The zone log shows `ZonePC ... zone_mode [3]`, which is `ZoneToBindPoint`, "Death Only" (zone/client.h). At level 49 this happened BEFORE `ready`, and the client then hit the FATAL below. At level 60 it happened 1 to 8 s AFTER `ready`, and the client zoned to qeynos cleanly. The rendered ready in charasis was reached (peak 98 to 103 MB before the move), but steady was never measurable. | rig/server content: the safe point is lethal |
| growthplane, templeveeshan (first attempt) | pass on retry | The server moved the character to qeynos during the initial zone load: a `ZoneToZone` from the zone, reason not captured because the retry's zone boot overwrote that zone's log. **The client then FAILED the follow-on load:** `[FATAL] Failed to load zone S3D archive: .s3d` (an EMPTY zone name; growthplane) or `... qeynos.s3d (Failed to open archive: qeynos.s3d)` (templeveeshan). It quit with **exit code 0**. | **CLIENT defect** (the FATAL), triggered by a server kick |
| veeshan (full sweep, both attempts) | pass at level 60 | Server min-level kick: `CanEnterZone Character [Testchar] does not meet minimum level requirement (49 < 55)`. The server re-derives level from exp: Testchar's row says 20, but the zone logs `SetLevel ... to [49]`. The same client FATAL on `qeynos.s3d` followed, with exit 0. | rig (character) + **CLIENT defect** |
| charasis (full sweep first attempt) | see above | The same client FATAL on `qeynos.s3d` after a death mid-load, exit 0. | **CLIENT defect** |
| sleeper (first attempt) | pass on retry | The world sent the character to qeynos at login. The client loaded qeynos cleanly, so sleeper itself was not tested on that attempt. | server |
| burningwood (full sweep, both attempts) | pass in the eligibility leg | **A rig artifact.** Both times it ran straight after a veeshan kick. The static qeynos process held the kicked character LINKDEAD and wrote it back over burningwood's seed. Fixed: the rig now also stops the zone the client actually landed in. | rig (fixed) |
| chardok (full sweep first attempt) | pass in the eligibility leg (level 60) | Ready in chardok, then killed 34 s later (`zone_mode [3]`) and moved to qeynos, where steady_60s was measured. The first summarizer counted that as a pass. That criterion is fixed (`moved_after_ready`: steady and peak must belong to the swept zone) and the rows were rebuilt from the stored logs. Because of this, the main sweep's retry list did not include chardok; the eligibility leg covered it. | rig/server content |

**Client defects in one line.** When the server moves the character to another zone while the initial zone
load is still in progress, the renderer's follow-on zone load fails:
- it gets a bare, unresolved archive name (`qeynos.s3d`, which loads fine at login) or an empty one (`.s3d`);
- it logs FATAL and quits;
- the process exits 0.

This was seen 5 times (growthplane, templeveeshan, veeshan x2, charasis). A move AFTER `ready` (chardok,
charasis at level 60) is handled cleanly.

## Other findings

- **Where the bytes are, per the ledger at `ready`.**
  - `gpu_driver` (18.4 to 20.2 MB, the driver's heap) is the largest category in 30/45 zones, and in the
    top two in 43/45.
  - `game_state` is the largest in 11. It reaches 51.2 MB in emeraldjungle, 38.8 in overthere, 35.6 in
    wakening and 33.4 in timorous.
  - The per-zone top five are in the category table below.
- **Live `spatial` is over its orangepi budget in 25/45 zones.** The budget is 4 MiB
  (`memoryLedgerBudgets.spatial` = 4,194,304). Worst at `ready`: growthplane 42.1 MB, trakanon 29.0,
  overthere 18.0, emeraldjungle 14.6, skyfire 13.3.
  - The CI gate asserts the budgets over five zones only, in the offline benchmark.
  - These are the live client's figures. No other budgeted category (archives, zone_parse, zone_mesh,
    textures_decoded, entity_models, objects, audio, unattributed) was over at `ready` in any zone.
  - Computed from each run's `details.json`.
- **Pseudo-VRAM / driver, as its own rows (below):**
  - the ledger's `gpu_driver` is 18.4 to 20.2 MB, and it is inside RssAnon;
  - the driver .so file-backed RSS (libLLVM + libgallium + EGL/GLES/drm) is 29 to 42 MB, inside VmHWM;
  - the system-wide MemAvailable drop from launch to steady is 79 to 191 MB. That includes page cache and
    any GEM buffers.

  The GEM/CMA buffers are NOT separately measured: CmaFree was about 0 before any run, because CMA was
  already full of movable pages.
- **A latent set -u bug** in the zone-placement helper (`local short="$1" bootlog=...${short}...`) worked
  only because `scenario_zone_sweep.sh`'s caller leaked its own `short` by dynamic scope. It is now fixed in
  the shared lib.
- **Lock hygiene.** `eqemu_lock_release` removes the lock without checking the owner; `pi_lock_release`
  does check. Nothing went wrong tonight, but a late cleanup from one run could drop another run's lock.

## Exact commands

```bash
# the sweep (both locks, lane up, snapshot, 46 zones heavy-first, one retry each, restore, down, release)
bash scripts/pi_rendered_zone_sweep.sh --out /mnt/projects/fable/review/B854-rendered/sweep-full
# the eligibility leg (level 60 + matching exp; binary pinned)
bash scripts/pi_rendered_zone_sweep.sh --zones veeshan,burningwood,chardok,charasis \
    --char-level 60 --char-exp 620000000 \
    --pi-dir /mnt/projects/fable/ci-arm/0effd2fbb7c17739adcc05f7c3dc98f5a16289aa \
    --out /mnt/projects/fable/review/B854-rendered/sweep-eligibility
# re-derive the rows from the stored logs (after the moved_after_ready criterion was added)
python3 scripts/pi/rendered_sweep_rebuild.py /mnt/projects/fable/review/B854-rendered/sweep-full
# the tables below
python3 scripts/pi/rendered_sweep_report.py /mnt/projects/fable/review/B854-rendered/sweep-full \
    /mnt/projects/fable/review/B854-rendered/sweep-eligibility
```

Defaults: `EQEMU_HOST=local` (the client connects to 10.0.30.23; the VM lane is refused), `--cap 300`
(overall per zone), `--stall 180` (no log line), `--hold 240` (the script's fallback `/q`), `-r 1280 720`.
The rig lines added after the main sweep (`--char-level/--char-exp`, the landed-zone kill) and the
summarizer's `moved_after_ready` check are in this branch. The main sweep ran without them. Its rows were
re-derived with the final summarizer from the stored logs, and that changed only chardok.

TSVs: `refactor/B854/rendered/`. Logs, frames and samples: `/mnt/projects/fable/review/B854-rendered/`
(its README).

## What was NOT verified

- **charasis**: the rendered steady state was never measured (the server kills the character).
- **The zones kicked on their first attempt**: the mid-load kick of growthplane and templeveeshan at
  level 49 (which is ≥ their min_level 46) is not explained. That zone log was overwritten by the retry.
- **Swap** use during a run was not recorded. The Pi had about 30 MB of 241 MB of swap in use before the
  sweep.
- **FPS as performance**: the dump frames perturb the min, and the cap/vsync behaviour was not examined.
- **No run without CAP_NET_ADMIN** was made, so the "published binary as-is" failure rate on rendered
  zone-in is not measured here.
- **Lighter test characters / other races**: only Testchar (race from the local lane's DB) was used.
- **Visual correctness**: frames are proven non-blank, not judged. Will judges.

<!-- generated by scripts/pi/rendered_sweep_report.py from the TSVs; do not hand-edit below -->

## Totals

- first attempt: 39/46 pass
- retry (failed zones only, once): 3/6 pass
- OOM kills (dmesg): first 0, retry 0
- zones with a verified own-zone ready row (any attempt/leg): 45/46; of those passing: 45
- peak RssAnon (verified rows) (n=45): min 60.3, median 98.0, max 159.2 MB; over 64 MB: 44/45; over 256 MB: 0/45
- RssAnon at ready (verified rows) (n=45): min 49.8, median 88.8, max 151.9 MB; over 64 MB: 41/45; over 256 MB: 0/45
- RssAnon at steady_60s (verified rows) (n=45): min 49.8, median 89.4, max 152.2 MB; over 64 MB: 41/45; over 256 MB: 0/45
- VmHWM, peak total RSS incl. file-backed (verified rows) (n=45): min 120.2, median 160.4, max 218.7 MB; over 64 MB: 45/45; over 256 MB: 0/45
- client-reported FPS (median of each zone's post-ready samples): min 20, median 30, max 60
- ledger gpu_driver at ready (n=45): min 18.4, median 18.9, max 20.2 MB [driver heap charged by the ledger, INSIDE RssAnon]
- driver .so RSS (libLLVM/libgallium/EGL/GLES/drm) at ready (n=45): min 29.2, median 38.3, max 42.0 MB [file-backed, INSIDE VmHWM, not RssAnon]
- system MemAvailable drop, launch -> steady (ready if no steady) (n=45): min 78.6, median 124.4, max 190.7 MB [system-wide: process RSS + page cache + kernel/GEM buffers the process RSS never shows]

Worst five by peak RssAnon: growthplane 159.2 MB (steady 152.2), overthere 148.2 MB (steady 138.4), trakanon 147.4 MB (steady 140.3), timorous 138.5 MB (steady 131.0), emeraldjungle 137.4 MB (steady 121.7)

### First attempt (run order: heaviest four first, then s3d_bytes descending)

| zone | exp | verdict | phase | t→ready s | fps med/min | frames (dumped/non-blank) | zone_mesh / textures / irrlicht_scene @ready MB | RssAnon peak MB | @ready | @steady | VmHWM MB | gpu_driver @ready MB | driver .so RSS MB | sys MemAvailable drop @steady MB | OOM | signature |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| timorous | k | **pass** | 15/15 | 37.0 | 30/10 | ≥1650 (3/3) | 16.8 / 0.5 / 1.3 | 138.5 | 130.9 | 131.0 | 200.2 | 19.1 | 36.5 | 183.0 | n | - |
| iceclad | v | **pass** | 15/15 | 38.0 | 30/9 | ≥1650 (3/3) | 10.8 / 0.4 / 1.8 | 115.3 | 108.7 | 108.8 | 177.0 | 18.7 | 38.9 | 169.8 | n | - |
| growthplane | v | **fail** | 15/15 | 31.5 | -/- | ≥0 (0/0) | 6.5 / 0.4 / 1.0 | 149.3 | 143.4 | 0.0 | 213.2 | 18.5 | 37.8 | 181.6 | n | wrong_zone (qeynos) |
| emeraldjungle | k | **pass** | 15/15 | 27.9 | 30/10 | ≥1650 (3/3) | 5.0 / 0.4 / 0.7 | 137.4 | 127.2 | 121.7 | 197.4 | 18.6 | 34.9 | 149.1 | n | - |
| stonebrunt | k | **pass** | 15/15 | 28.5 | 60/42 | ≥3450 (3/3) | 6.1 / 0.4 / 0.7 | 103.0 | 95.0 | 95.0 | 172.4 | 19.0 | 38.8 | 137.7 | n | - |
| frozenshadow | v | **pass** | 15/15 | 27.6 | 30/10 | ≥1650 (3/3) | 5.0 / 0.8 / 0.9 | 101.6 | 95.1 | 94.5 | 163.1 | 19.7 | 38.4 | 134.1 | n | - |
| thurgadina | v | **pass** | 15/15 | 27.8 | 30/10 | ≥1650 (3/3) | 5.1 / 0.8 / 1.1 | 96.4 | 89.4 | 89.4 | 162.5 | 20.2 | 38.3 | 132.2 | n | - |
| warslikswood | k | **pass** | 15/15 | 31.4 | 30.0/9 | ≥1800 (3/3) | 9.2 / 0.7 / 0.8 | 130.9 | 122.5 | 108.4 | 195.2 | 19.5 | 38.6 | 156.1 | n | - |
| thurgadinb | v | **pass** | 15/15 | 22.7 | 20/19 | ≥1050 (3/3) | 3.9 / 0.6 / 0.6 | 88.5 | 78.6 | 78.0 | 149.4 | 19.6 | 37.1 | 124.3 | n | - |
| greatdivide | v | **pass** | 15/15 | 24.5 | 60.0/41 | ≥3600 (3/3) | 6.7 / 0.4 / 0.7 | 95.0 | 88.0 | 88.0 | 156.8 | 18.7 | 37.1 | 123.6 | n | - |
| lakeofillomen | k | **pass** | 15/15 | 30.7 | 57.0/30 | ≥3000 (3/3) | 9.3 / 0.7 / 0.9 | 127.6 | 120.5 | 120.5 | 189.1 | 19.6 | 35.0 | 151.7 | n | - |
| skyshrine | v | **pass** | 15/15 | 32.8 | 60/42 | ≥3450 (3/3) | 5.9 / 0.6 / 1.9 | 99.4 | 92.3 | 92.4 | 156.9 | 19.8 | 37.1 | 134.2 | n | - |
| templeveeshan | v | **fail** | 15/15 | 20.1 | -/- | ≥0 (0/0) | 3.7 / 0.4 / 1.6 | 77.5 | 76.4 | 0.0 | 140.5 | 18.6 | 37.3 | 108.4 | n | wrong_zone (qeynos) |
| sleeper | v | **fail** | 15/15 | 24.2 | 60.0/42 | ≥3300 (3/3) | 4.5 / 0.8 / 0.9 | 97.6 | 91.4 | 0.0 | 164.1 | 20.1 | 37.6 | 156.7 | n | wrong_zone (qeynos) |
| cabeast | k | **pass** | 15/15 | 23.8 | 30.0/10 | ≥1800 (3/3) | 4.4 / 0.7 / 1.0 | 87.2 | 80.2 | 80.2 | 146.6 | 19.4 | 36.2 | 112.3 | n | - |
| cabwest | k | **pass** | 15/15 | 20.4 | 60.0/40 | ≥3600 (3/3) | 4.4 / 0.7 / 0.8 | 83.2 | 72.6 | 72.7 | 145.9 | 19.0 | 37.1 | 101.3 | n | - |
| dreadlands | k | **pass** | 15/15 | 23.8 | 30/10 | ≥1650 (3/3) | 10.9 / 0.5 / 0.6 | 114.5 | 107.4 | 107.4 | 174.9 | 19.1 | 35.8 | 165.9 | n | - |
| sirens | v | **pass** | 15/15 | 18.0 | 30/10 | ≥1650 (3/3) | 3.3 / 0.4 / 0.7 | 68.9 | 65.3 | 65.3 | 129.6 | 18.5 | 37.1 | 98.5 | n | - |
| kael | v | **pass** | 15/15 | 22.0 | 30/10 | ≥1650 (3/3) | 4.7 / 0.4 / 1.3 | 77.1 | 76.1 | 76.1 | 140.1 | 18.7 | 35.5 | 102.2 | n | - |
| overthere | k | **pass** | 15/15 | 32.6 | 51/20 | ≥3150 (3/3) | 6.4 / 0.5 / 1.1 | 148.2 | 141.1 | 138.4 | 202.4 | 19.4 | 29.2 | 170.9 | n | - |
| citymist | k | **pass** | 15/15 | 16.0 | 30.0/9 | ≥1800 (3/3) | 3.0 / 0.4 / 0.6 | 73.8 | 63.2 | 63.2 | 133.6 | 18.9 | 36.5 | 96.7 | n | - |
| trakanon | k | **pass** | 15/15 | 26.3 | 40/22 | ≥2250 (3/3) | 7.9 / 0.4 / 0.9 | 147.4 | 140.3 | 140.3 | 204.0 | 18.9 | 37.2 | 187.7 | n | - |
| skyfire | k | **pass** | 15/15 | 29.6 | 30/10 | ≥1650 (3/3) | 10.1 / 0.4 / 0.9 | 122.3 | 115.6 | 115.6 | 182.4 | 18.6 | 35.6 | 142.7 | n | - |
| westwastes | v | **pass** | 15/15 | 22.8 | 60/20 | ≥3150 (3/3) | 6.1 / 0.4 / 0.7 | 97.0 | 90.9 | 84.4 | 160.4 | 19.0 | 36.1 | 124.4 | n | - |
| frontiermtns | k | **pass** | 15/15 | 25.4 | 30/9 | ≥1650 (3/3) | 6.9 / 0.5 / 0.6 | 105.5 | 94.1 | 94.2 | 162.5 | 19.2 | 29.6 | 128.1 | n | - |
| firiona | k | **pass** | 15/15 | 31.1 | 30/9 | ≥1650 (3/3) | 7.0 / 0.5 / 0.8 | 119.1 | 108.5 | 108.5 | 174.1 | 19.5 | 33.5 | 147.3 | n | - |
| swampofnohope | k | **pass** | 15/15 | 28.7 | 20.0/19 | ≥1200 (3/3) | 6.8 / 0.7 / 0.5 | 114.9 | 108.7 | 104.7 | 178.5 | 19.3 | 37.5 | 153.6 | n | - |
| eastwastes | v | **pass** | 15/15 | 26.9 | 30/9 | ≥1650 (3/3) | 7.7 / 0.4 / 0.8 | 101.2 | 94.2 | 94.2 | 164.1 | 18.6 | 37.2 | 137.0 | n | - |
| veeshan | k | **fail** | 15/15 | 21.8 | -/- | ≥0 (0/0) | 4.2 / 0.5 / 1.2 | 78.1 | 76.8 | 0.0 | 141.1 | 19.1 | 37.3 | 112.4 | n | wrong_zone (qeynos) |
| burningwood | k | **fail** | 15/15 | 23.5 | 60.0/23 | ≥3300 (3/3) | 4.5 / 0.8 / 0.9 | 98.0 | 91.8 | 0.0 | 153.1 | 20.1 | 30.3 | 151.3 | n | wrong_zone (qeynos) |
| fieldofbone | k | **pass** | 15/15 | 27.1 | 30/10 | ≥1650 (3/3) | 5.7 / 0.8 / 0.5 | 100.8 | 97.3 | 97.3 | 165.7 | 19.3 | 39.0 | 138.1 | n | - |
| cobaltscar | v | **pass** | 15/15 | 14.8 | 20/20 | ≥1050 (3/3) | 2.3 / 0.4 / 0.3 | 75.5 | 68.4 | 68.4 | 139.6 | 18.7 | 39.4 | 107.3 | n | - |
| sebilis | k | **pass** | 15/15 | 19.7 | 30/9 | ≥1650 (3/3) | 4.1 / 0.5 / 0.8 | 80.4 | 69.9 | 69.9 | 144.1 | 19.2 | 40.2 | 91.5 | n | - |
| chardok | k | **fail** | 15/15 | 21.0 | 60.0/31 | ≥5400 (3/3) | 3.3 / 0.5 / 1.0 | 78.9 | 68.4 | 0.0 | 164.5 | 18.8 | 40.2 | 116.0 | n | moved_after_ready (server sent the character to qeynos 34 s after ready) |
| velketor | v | **pass** | 15/15 | 23.6 | 30/30 | ≥1650 (3/3) | 4.0 / 0.4 / 0.7 | 100.2 | 70.3 | 93.1 | 165.1 | 18.6 | 40.1 | 123.0 | n | - |
| karnor | k | **pass** | 15/15 | 16.5 | 31/9 | ≥1650 (3/3) | 3.8 / 0.4 / 0.8 | 81.5 | 74.5 | 74.5 | 144.1 | 18.8 | 40.2 | 106.7 | n | - |
| crystal | v | **pass** | 15/15 | 15.4 | 30/9 | ≥1650 (3/3) | 3.6 / 0.6 / 0.6 | 75.1 | 64.6 | 64.6 | 141.3 | 18.6 | 40.6 | 96.0 | n | - |
| kurn | k | **pass** | 15/15 | 20.1 | 59.5/7 | ≥2400 (3/3) | 4.2 / 0.6 / 0.9 | 104.9 | 97.9 | 97.9 | 166.5 | 18.8 | 39.6 | 136.5 | n | - |
| droga | k | **pass** | 15/15 | 17.3 | 30.0/9 | ≥1800 (3/3) | 3.7 / 0.4 / 1.0 | 73.6 | 63.0 | 63.1 | 134.0 | 18.5 | 40.2 | 91.9 | n | - |
| necropolis | v | **pass** | 15/15 | 20.0 | 30/9 | ≥1650 (3/3) | 2.8 / 0.4 / 0.6 | 80.7 | 73.7 | 73.7 | 144.1 | 18.8 | 39.9 | 108.6 | n | - |
| warrens | k | **pass** | 15/15 | 18.4 | 60.0/14 | ≥3300 (3/3) | 3.3 / 0.5 / 0.5 | 79.5 | 72.4 | 72.4 | 145.8 | 18.6 | 40.1 | 105.6 | n | - |
| wakening | v | **pass** | 15/15 | 22.7 | 30/10 | ≥1650 (3/3) | 2.8 / 0.4 / 0.4 | 107.1 | 96.5 | 92.9 | 173.5 | 18.6 | 39.2 | 127.4 | n | - |
| charasis | k | **fail** | 15/15 | 19.8 | -/- | ≥0 (0/0) | 2.9 / 0.7 / 0.7 | 99.4 | 97.5 | 0.0 | 165.7 | 19.1 | 38.0 | 131.8 | n | wrong_zone (qeynos) |
| kaesora | k | **pass** | 15/15 | 17.5 | 60.0/41 | ≥3600 (3/3) | 3.0 / 0.6 / 0.7 | 85.8 | 78.8 | 78.8 | 146.8 | 18.8 | 39.4 | 106.9 | n | - |
| nurga | k | **pass** | 15/15 | 10.9 | 30.0/10 | ≥1800 (3/3) | 1.5 / 0.3 / 0.5 | 60.3 | 49.8 | 49.8 | 120.2 | 18.4 | 39.4 | 78.6 | n | - |
| dalnir | k | **pass** | 15/15 | 16.5 | 30.0/10 | ≥1800 (3/3) | 1.8 / 0.7 / 0.5 | 95.9 | 88.8 | 88.8 | 156.5 | 19.6 | 38.5 | 109.4 | n | - |

### Retry (each first-attempt failure, once, at the end of the sweep)

| zone | exp | verdict | phase | t→ready s | fps med/min | frames (dumped/non-blank) | zone_mesh / textures / irrlicht_scene @ready MB | RssAnon peak MB | @ready | @steady | VmHWM MB | gpu_driver @ready MB | driver .so RSS MB | sys MemAvailable drop @steady MB | OOM | signature |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| growthplane | v | **pass** | 15/15 | 29.9 | 20/2 | ≥1050 (3/3) | 6.5 / 0.4 / 1.0 | 159.2 | 151.9 | 152.2 | 218.7 | 18.5 | 33.1 | 190.7 | n | - |
| templeveeshan | v | **pass** | 15/15 | 20.9 | 30/30 | ≥1650 (3/3) | 3.7 / 0.4 / 1.6 | 99.6 | 73.2 | 96.1 | 163.9 | 18.6 | 40.5 | 130.5 | n | - |
| sleeper | v | **pass** | 15/15 | 17.9 | 30.0/30 | ≥1800 (3/3) | 2.2 / 0.4 / 0.7 | 69.1 | 62.1 | 62.1 | 135.0 | 18.7 | 40.7 | 94.5 | n | - |
| veeshan | k | **fail** | 15/15 | 20.1 | -/- | ≥0 (0/0) | 4.2 / 0.5 / 1.2 | 78.3 | 76.6 | 0.0 | 145.0 | 19.1 | 40.8 | 119.6 | n | wrong_zone (qeynos) |
| burningwood | k | **fail** | 15/15 | 24.8 | 60.0/43 | ≥3300 (3/3) | 4.5 / 0.8 / 0.9 | 97.7 | 91.5 | 0.0 | 169.9 | 20.1 | 38.7 | 151.0 | n | wrong_zone (qeynos) |
| charasis | k | **fail** | 15/15 | 21.7 | 60.0/42 | ≥3300 (3/3) | 2.0 / 0.7 / 0.8 | 110.0 | 100.4 | 0.0 | 179.0 | 19.1 | 72.7 | 153.3 | n | wrong_zone (qeynos) |

### sweep-eligibility: first attempt

| zone | exp | verdict | phase | t→ready s | fps med/min | frames (dumped/non-blank) | zone_mesh / textures / irrlicht_scene @ready MB | RssAnon peak MB | @ready | @steady | VmHWM MB | gpu_driver @ready MB | driver .so RSS MB | sys MemAvailable drop @steady MB | OOM | signature |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| veeshan | k | **pass** | 15/15 | 19.8 | 25.0/22 | ≥1500 (3/3) | 4.2 / 0.5 / 1.2 | 77.5 | 74.0 | 74.0 | 143.4 | 19.1 | 40.8 | 123.3 | n | - |
| burningwood | k | **pass** | 15/15 | 20.5 | 60/41 | ≥3450 (3/3) | 6.6 / 0.4 / 0.5 | 98.0 | 87.4 | 87.4 | 156.9 | 18.9 | 37.7 | 108.8 | n | - |
| chardok | k | **pass** | 15/15 | 21.7 | 60/42 | ≥3450 (3/3) | 3.3 / 0.5 / 1.0 | 79.4 | 68.8 | 68.8 | 144.1 | 18.8 | 42.0 | 103.1 | n | - |
| charasis | k | **fail** | 15/15 | 19.2 | -/- | ≥3750 (3/3) | 2.9 / 0.7 / 0.7 | 103.1 | 98.0 | 0.0 | 167.1 | 19.1 | 41.6 | 118.7 | n | moved_after_ready (server sent the character to qeynos 8 s after ready) |

### sweep-eligibility: retry

| zone | exp | verdict | phase | t→ready s | fps med/min | frames (dumped/non-blank) | zone_mesh / textures / irrlicht_scene @ready MB | RssAnon peak MB | @ready | @steady | VmHWM MB | gpu_driver @ready MB | driver .so RSS MB | sys MemAvailable drop @steady MB | OOM | signature |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| charasis | k | **fail** | 15/15 | 23.3 | -/- | ≥3450 (3/3) | 2.9 / 0.7 / 0.7 | 98.3 | 97.5 | 0.0 | 174.2 | 19.1 | 42.5 | 133.6 | n | moved_after_ready (server sent the character to qeynos 1 s after ready) |

### Top ledger categories

| zone | top 5 at ready | top 5 at steady_60s |
|---|---|---|
| burningwood (first) | gpu_driver(18.9M);game_state(16.6M);spatial(7.7M);zone_mesh(6.6M);startup(4.8M) | gpu_driver(19.0M);game_state(16.4M);spatial(7.7M);zone_mesh(6.6M);startup(4.8M) |
| cabeast (first) | gpu_driver(19.4M);entity_models(7.8M);game_state(5.6M);startup(4.8M);zone_mesh(4.4M) | gpu_driver(19.4M);entity_models(7.8M);game_state(5.5M);startup(4.8M);zone_mesh(4.4M) |
| cabwest (first) | gpu_driver(19.0M);entity_models(5.7M);startup(4.8M);zone_mesh(4.4M);game_state(4.3M) | gpu_driver(19.1M);entity_models(7.0M);startup(4.8M);zone_mesh(4.4M);game_state(4.2M) |
| chardok (first) | gpu_driver(18.8M);game_state(5.9M);entity_models(5.9M);startup(4.8M);spatial(3.4M) | gpu_driver(18.8M);entity_models(6.5M);game_state(5.7M);startup(4.8M);spatial(3.4M) |
| citymist (first) | gpu_driver(18.9M);game_state(5.1M);startup(4.8M);spatial(4.6M);objects(3.7M) | gpu_driver(18.9M);game_state(5.6M);startup(4.8M);spatial(4.6M);objects(3.7M) |
| cobaltscar (first) | gpu_driver(18.7M);game_state(8.8M);entity_models(7.5M);startup(4.8M);zone_mesh(2.3M) | gpu_driver(18.7M);game_state(9.4M);entity_models(7.5M);startup(4.8M);zone_mesh(2.1M) |
| crystal (first) | gpu_driver(18.6M);spatial(5.7M);startup(4.8M);game_state(4.6M);zone_mesh(3.6M) | gpu_driver(18.6M);spatial(5.7M);game_state(5.4M);startup(4.8M);zone_mesh(3.1M) |
| dalnir (first) | entity_models(26.0M);gpu_driver(19.6M);entity_instances(6.5M);startup(4.8M);game_state(4.4M) | entity_models(26.0M);gpu_driver(19.6M);entity_instances(6.5M);game_state(4.9M);startup(4.8M) |
| dreadlands (first) | game_state(19.4M);gpu_driver(19.1M);zone_mesh(10.9M);spatial(8.7M);startup(4.8M) | game_state(20.2M);gpu_driver(19.1M);zone_mesh(10.9M);spatial(8.7M);startup(4.8M) |
| droga (first) | gpu_driver(18.5M);game_state(6.0M);startup(4.8M);spatial(3.9M);zone_mesh(3.7M) | gpu_driver(18.5M);game_state(6.6M);startup(4.8M);spatial(3.9M);zone_mesh(3.7M) |
| eastwastes (first) | gpu_driver(18.6M);game_state(16.2M);zone_mesh(7.7M);startup(4.8M);spatial(4.3M) | gpu_driver(18.6M);game_state(16.3M);zone_mesh(7.7M);startup(4.8M);spatial(4.3M) |
| emeraldjungle (first) | game_state(51.2M);gpu_driver(18.6M);spatial(14.6M);objects(13.4M);zone_mesh(5.0M) | game_state(51.3M);gpu_driver(18.7M);spatial(14.6M);objects(13.4M);zone_mesh(5.0M) |
| fieldofbone (first) | gpu_driver(19.3M);game_state(18.3M);entity_models(5.9M);zone_mesh(5.7M);spatial(4.8M) | gpu_driver(19.4M);game_state(19.0M);entity_models(8.5M);zone_mesh(5.6M);spatial(4.8M) |
| firiona (first) | game_state(21.1M);gpu_driver(19.5M);spatial(8.8M);zone_mesh(7.0M);entity_models(5.4M) | game_state(20.9M);gpu_driver(19.5M);spatial(8.8M);entity_models(7.9M);zone_mesh(6.9M) |
| frontiermtns (first) | game_state(28.1M);gpu_driver(19.2M);spatial(10.3M);zone_mesh(6.9M);startup(4.8M) | game_state(27.9M);gpu_driver(19.2M);spatial(10.3M);zone_mesh(6.9M);startup(4.8M) |
| frozenshadow (first) | gpu_driver(19.7M);game_state(9.4M);entity_models(8.3M);spatial(6.5M);zone_mesh(5.0M) | gpu_driver(19.8M);game_state(9.3M);entity_models(8.3M);spatial(6.5M);startup(4.8M) |
| greatdivide (first) | gpu_driver(18.7M);game_state(17.3M);zone_mesh(6.7M);spatial(5.3M);startup(4.8M) | gpu_driver(18.7M);game_state(17.1M);zone_mesh(6.7M);spatial(5.3M);startup(4.8M) |
| growthplane (retry) | spatial(42.1M);objects(38.4M);game_state(25.1M);gpu_driver(18.5M);zone_mesh(6.5M) | spatial(42.1M);objects(38.4M);game_state(24.9M);gpu_driver(18.5M);zone_mesh(6.5M) |
| iceclad (first) | gpu_driver(18.7M);game_state(13.1M);zone_mesh(10.8M);zone_parse(9.8M);spatial(6.8M) | gpu_driver(18.7M);game_state(13.5M);zone_mesh(10.7M);zone_parse(9.8M);spatial(6.8M) |
| kael (first) | gpu_driver(18.7M);game_state(8.5M);startup(4.8M);zone_mesh(4.7M);entity_models(4.6M) | gpu_driver(18.7M);game_state(8.4M);startup(4.8M);zone_mesh(4.7M);entity_models(4.6M) |
| kaesora (first) | gpu_driver(18.8M);entity_models(14.6M);game_state(5.1M);startup(4.8M);entity_instances(3.3M) | gpu_driver(18.8M);entity_models(14.6M);game_state(5.5M);startup(4.8M);entity_instances(3.3M) |
| karnor (first) | gpu_driver(18.8M);entity_models(6.9M);startup(4.8M);game_state(4.0M);zone_mesh(3.8M) | gpu_driver(18.8M);entity_models(6.9M);startup(4.8M);game_state(3.9M);zone_mesh(3.5M) |
| kurn (first) | entity_instances(24.0M);gpu_driver(18.8M);entity_models(9.1M);game_state(5.6M);startup(4.8M) | entity_instances(24.0M);gpu_driver(18.8M);entity_models(9.1M);game_state(5.5M);startup(4.8M) |
| lakeofillomen (first) | gpu_driver(19.6M);game_state(18.3M);spatial(12.9M);entity_models(11.3M);zone_mesh(9.3M) | gpu_driver(20.0M);game_state(18.0M);entity_models(15.6M);spatial(12.9M);zone_mesh(9.2M) |
| necropolis (first) | gpu_driver(18.8M);game_state(9.7M);startup(4.8M);spatial(3.7M);zone_mesh(2.8M) | gpu_driver(18.8M);game_state(9.5M);startup(4.8M);spatial(3.7M);zone_mesh(2.8M) |
| nurga (first) | gpu_driver(18.4M);startup(4.8M);game_state(3.4M);entity_models(2.1M);archives(1.8M) | gpu_driver(18.4M);startup(4.8M);game_state(4.6M);entity_models(2.1M);archives(1.8M) |
| overthere (first) | game_state(38.8M);gpu_driver(19.4M);spatial(18.0M);objects(17.3M);zone_mesh(6.4M) | game_state(38.9M);gpu_driver(19.4M);spatial(18.0M);objects(17.3M);zone_mesh(6.4M) |
| sebilis (first) | gpu_driver(19.2M);game_state(6.4M);startup(4.8M);zone_mesh(4.1M);spatial(3.4M) | gpu_driver(19.2M);game_state(6.2M);startup(4.8M);zone_mesh(4.0M);spatial(3.4M) |
| sirens (first) | gpu_driver(18.5M);game_state(6.0M);startup(4.8M);zone_mesh(3.3M);spatial(3.1M) | gpu_driver(18.5M);game_state(6.5M);startup(4.8M);zone_mesh(3.3M);spatial(3.1M) |
| skyfire (first) | game_state(30.9M);gpu_driver(18.6M);spatial(13.3M);zone_mesh(10.1M);startup(4.8M) | game_state(31.4M);gpu_driver(18.7M);spatial(13.3M);zone_mesh(10.1M);entity_models(5.2M) |
| skyshrine (first) | gpu_driver(19.8M);game_state(13.7M);spatial(7.5M);zone_mesh(5.9M);entity_models(4.9M) | gpu_driver(19.8M);game_state(14.0M);spatial(7.5M);zone_mesh(5.8M);entity_models(5.2M) |
| sleeper (retry) | gpu_driver(18.7M);game_state(5.8M);startup(4.8M);zone_parse(2.3M);entity_models(2.2M) | gpu_driver(18.7M);game_state(5.7M);startup(4.8M);entity_models(3.5M);zone_parse(2.3M) |
| stonebrunt (first) | game_state(22.0M);gpu_driver(19.0M);zone_mesh(6.1M);startup(4.8M);spatial(4.7M) | game_state(22.9M);gpu_driver(19.0M);zone_mesh(6.1M);startup(4.8M);spatial(4.7M) |
| swampofnohope (first) | game_state(27.2M);gpu_driver(19.3M);zone_mesh(6.8M);entity_models(6.4M);spatial(5.8M) | game_state(27.1M);gpu_driver(19.3M);entity_models(6.4M);zone_mesh(5.9M);spatial(5.8M) |
| templeveeshan (retry) | gpu_driver(18.6M);startup(4.8M);game_state(4.6M);zone_mesh(3.7M);entity_models(3.5M) | game_state(27.8M);gpu_driver(18.6M);startup(4.8M);zone_mesh(3.6M);entity_models(3.5M) |
| thurgadina (first) | gpu_driver(20.2M);entity_models(7.0M);game_state(6.6M);spatial(5.8M);zone_mesh(5.1M) | gpu_driver(20.2M);entity_models(7.0M);game_state(6.5M);spatial(5.8M);zone_mesh(5.0M) |
| thurgadinb (first) | gpu_driver(19.6M);entity_models(9.7M);startup(4.8M);game_state(4.4M);equipment(4.2M) | gpu_driver(19.6M);entity_models(9.7M);startup(4.8M);game_state(4.6M);zone_mesh(3.3M) |
| timorous (first) | game_state(33.4M);gpu_driver(19.1M);zone_mesh(16.8M);spatial(12.3M);zone_parse(6.0M) | game_state(34.0M);gpu_driver(19.2M);zone_mesh(16.7M);spatial(12.3M);entity_models(10.5M) |
| trakanon (first) | spatial(29.0M);game_state(26.5M);objects(22.3M);gpu_driver(18.9M);zone_mesh(7.9M) | spatial(29.0M);game_state(27.3M);objects(22.3M);gpu_driver(18.9M);zone_mesh(7.9M) |
| veeshan (first) | gpu_driver(19.1M);game_state(7.1M);startup(4.8M);zone_mesh(4.2M);spatial(3.8M) | gpu_driver(19.1M);game_state(8.1M);startup(4.8M);zone_mesh(3.9M);spatial(3.8M) |
| velketor (first) | gpu_driver(18.6M);entity_models(5.1M);startup(4.8M);game_state(4.6M);zone_mesh(4.0M) | game_state(28.3M);gpu_driver(18.6M);entity_models(5.1M);startup(4.8M);zone_mesh(3.7M) |
| wakening (first) | game_state(35.6M);gpu_driver(18.6M);startup(4.8M);entity_models(4.6M);spatial(3.8M) | game_state(35.5M);gpu_driver(18.7M);entity_models(7.4M);startup(4.8M);spatial(3.8M) |
| warrens (first) | gpu_driver(18.6M);game_state(4.8M);startup(4.8M);entity_models(4.8M);zone_mesh(3.3M) | gpu_driver(18.6M);entity_models(6.6M);startup(4.8M);game_state(4.7M);entity_instances(3.0M) |
| warslikswood (first) | game_state(23.9M);gpu_driver(19.5M);spatial(11.8M);entity_models(10.7M);zone_mesh(9.2M) | game_state(24.7M);gpu_driver(19.5M);entity_models(13.2M);spatial(11.8M);zone_mesh(9.1M) |
| westwastes (first) | gpu_driver(19.0M);game_state(14.1M);zone_mesh(6.1M);startup(4.8M);spatial(4.7M) | gpu_driver(19.1M);game_state(14.0M);entity_models(6.1M);zone_mesh(6.1M);startup(4.8M) |
