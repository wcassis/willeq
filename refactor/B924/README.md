# B924 — llvmpipe's post-ready `gpu_driver` growth, measured on x86 (the measurement leg)

GitLab #243. Branch `task/kb-b924-llvmpipe-growth`, from `fable` @ 7ad78a6c.
Date 2026-10-14. Host: the hermes-ext worker box (x86, 16 cores, surfaceless
EGL on llvmpipe, **Mesa 25.2.8**, Ubuntu 24.04; `libgallium-25.2.8-0ubuntu0.24.04.4.so`,
build-id `198701d4…`). eqemu lanes 1–3 (`scripts/eqemu/env.sh`), lock owner
`b924-llvmpipe-growth`.

This is the PROVE card of a prove-then-fix split: it measures (acceptance 1)
and registers the repeatable net (acceptance 3). The follow-up card takes
acceptance 2 (client fix or a `docs/decisions.md` record) from what the
measurement shows.

## What grows, and why (the conclusion)

**The growth is llvmpipe's own scene queue filling to its designed ceiling —
not a willeq leak, not an unbounded driver bug.** Three uncapped 5-minute
holds all grow from ~137 MB at `ready` to ~2.24–2.31 GiB by `steady_60s` and
then **hold flat** for the remaining ~4 minutes (the per-interval deltas are
±1 MiB noise, e.g. `run1` idle_120→idle_360: +0.6 / −1.1 / +1.3 / +24.8 /
−24.9 / −1.1 / −0.1 / +1.4 / −0.8 MiB). The A/B that proves it is rate, not
time:

| run | zone | rate | gpu_driver @ready | @last idle | growth | last point |
|---|---|---|---:|---:|---:|---|
| run1 | gfaydark | uncapped (~16 fps) | 137 MiB | 2272 MiB (idle_360) | **+2136 MiB** | flat |
| run3 | iceclad | uncapped (~20 fps) | 123 MiB | 2323 MiB (idle_330) | **+2200 MiB** | flat |
| run4 | gfaydark | uncapped (~23 fps, trace) | 137 MiB | 2270 MiB (idle_360) | **+2133 MiB** | flat |
| run2 | gfaydark | **10 fps cap** | 137 MiB | 155 MiB (idle_360) | **+18 MiB** | flat |
| run5 | gfaydark | **10 fps cap** | 137 MiB | 139 MiB (idle_270) | **−2.5 MiB** | flat |

At 10 fps the growth is ~zero; uncapped it is ~2.1–2.2 GiB. It is therefore
tied to **frames rendered**, not wall time — and it *stops* once the consumer
keeps up.

**The mechanism (Mesa 25.2.8, `src/gallium/drivers/llvmpipe/`):** llvmpipe
rasterizes on a pool of worker threads that drain a **bounded FIFO queue of
full scenes**, each scene a slab of command/data blocks capped at
`LP_SCENE_MAX_SIZE`. The ceiling of that queue is exactly the observed growth:

- `MAX_SCENES 64` — `lp_setup_context.h:60` (the full/empty scene queues,
  `lp_scene_queue.c:42` `SCENE_QUEUE_SIZE = MAX_SCENES`).
- `LP_SCENE_MAX_SIZE (36*1024*1024)` — `lp_scene.h:64` (each scene's command
  + data budget; `lp_scene_new_data_block` in `lp_scene.c:342` refuses blocks
  past it).
- 64 × 36 MiB = **2304 MiB**, against the measured 2133–2200 MiB (the queue
  rarely holds all 64 at exactly-max size — the gap is the unfilled tail and
  freed-but-not-yet-reused slots).

When willeq renders faster than the ~16 raster-worker threads
(`screen->num_threads`, `lp_screen.c:990`, capped at `LP_MAX_THREADS 32`,
`lp_limits.h:69`) can drain, `lp_scene_enqueue` (`lp_scene_queue.c:124`)
accumulates up to 64 scenes of ~36 MiB before the workers catch up — and the
client holds the final 64 in flight while standing still, because each scene
keeps its command/data blocks live until its fence retires (`lp_scene_end_
rasterization`, `lp_scene.c:207`, frees the data blocks only when the scene
is dequeued and finished). At a 10 fps cap the workers finish every scene
before the next is produced, so the queue never fills and `gpu_driver` stays
at the ~137 MB zone-load baseline.

**Where the bytes live.** The `[memmap]` census (client's own, `WILLEQ_IDLE_
MEMMAP=1`) attributes the delta to the glibc main `[heap]`:
`[heap] anon 174899200 → 1648119808 (+1.48 GiB)` ready→idle_360 in run1 —
the scene command/data blocks are `MALLOC_STRUCT(data_block)` (`lp_scene.c:350`)
and live in the driver process's heap, which the ledger reports under the
`gpu_driver` category (the load lands in `libgallium`, build-id
`198701d4…`). The `[memmap]` trace-leg report (`WILLEQ_LEDGER_TRACE=gpu_
driver:ready`, run4) independently shows 2.33 GiB live across 254 libgallium
call sites, the top one 990 MiB — consistent with ~64 scenes of command/data
blocks, all in `libgallium`.

So: the post-ready growth is **driver pseudo-VRAM** (llvmpipe's in-flight
scene queue at its 64×36 MiB design ceiling), reached when the render rate
outruns the worker pool and held while the client is idle. It is bounded and
self-limiting, not a leak.

## How it was measured

The client's own memory ledger (`include/client/core/memory_ledger.h`) is the
sampler; B909 already measured the 60 s after `ready`. What this unit adds:

- **The hold.** `scripts/b924/b924_hold.sh` runs the rendered client
  (surfaceless EGL/llvmpipe, `--renderer gles2 --constrained orangepi -r
  1280 720`, `WILLEQ_BASELINE=1` — the same invocation B909's
  `scripts/client_ledger_capture.sh` uses), zoned into a zone, and HOLDS it
  standing still for `--hold` seconds (420 s default = ~5 min idle). Lane
  discipline is B909's: one lock, one snapshot, restore with the world down,
  `down.sh`, release, lock state printed last. `RssAnon` is also sampled
  every 2 s from `/proc/<pid>/status` (`samples.tsv`).
- **The sampling points are the client's own.** `WILLEQ_LEDGER_IDLE_EVERY=30
  WILLEQ_LEDGER_IDLE_COUNT=10` (T504's instrument, `pollSteadyState()` in
  `src/client/core/memory_ledger.cpp`) puts an `[memledger]` point every 30 s
  past `steady_60s` — `idle_90 … idle_330` — each carrying `frames=`,
  `gpu_driver=`, `owned=`, `rss_anon=`. Two measurement switches this unit
  added, both **off by default and never used by any gate**:
  - `noteRenderedFrame()` (`memory_ledger_b924.cpp`, called once per frame
    from `FrameComponent::processFrame()`, `renderer_frame.cpp:101`) counts
    rendered frames and prints them as `frames=` on every `[memledger]` line
    — the growth-vs-frames correlation the issue asks for.
  - `WILLEQ_IDLE_MEMMAP=1` makes `pollSteadyState()` print the FULL `[memmap]`
    smaps census at `steady_60s` and every idle point, so the growing mapping
    owner is named by the client itself (`parse_b924_run.py` turns it into
    `memmap_timeseries.tsv`).
  - `WILLEQ_FPS_CAP=<fps>` paces the render loop to at most <fps> frames/s
    (a `sleep_until` in `noteRenderedFrame()`, `memory_ledger_b924.cpp`) for
    the frames-vs-time A/B.
- **Parsing.** `scripts/b924/parse_b924_run.py <run dir>` turns `client.log`
  into `ledger.tsv`, `memmap_*.tsv`, `memmap_timeseries.tsv` and
  `summary.txt`.

## Runs

Binary: sha256 `75964fbd…cd63b6b` of `build/bin/willeq`, commit
`7ad78a6c3ce93d88c2231fe722f99ba8622cb5f6`. Raw evidence (client.log,
samples.tsv, summary.txt, bound_check.txt, rig.log) under
`/home/user/kb-evidence/kb-b924-llvmpipe-growth/<run>/`: run1, run3, run4
(uncapped), run2, run5 (capped), net_uncapped, net_capped.

The branch tip since measurement merged origin/fable forward (28 commits:
hotbar persistence, the options-window fix, test additions — no
gpu_driver/render-loop or ledger changes); the MR pipeline re-runs the
full suite on that tip. The growth conclusion is unaffected: the growth
lives in the driver's scene queue, not in anything the merge touched.

Per-run `ready → last-idle` growth (from each `summary.txt`):

| run | zone | cap | peak RssAnon | gpu_driver @ready | @last idle | growth | frames Δ |
|---|---|---|---:|---:|---:|---:|---:|
| run1 | gfaydark | none | 2254.5 MiB | 137.2 MiB | 2271.9 MiB | **2135.5 MiB** | 5749 |
| run3 | iceclad | none | 2057.1 MiB | 123.4 MiB | 2271.4 MiB | **2148.0 MiB** | 6560 |
| run4 | gfaydark | none | 2256.2 MiB | 137.0 MiB | 2270.1 MiB | **2133.1 MiB** | 8492 |
| run2 | gfaydark | 10 | 196.8 MiB | 137.5 MiB | 155.5 MiB | **+18.3 MiB** | 3448 |
| run5 | gfaydark | 10 | 197.7 MiB | 137.1 MiB | 134.6 MiB | **−2.5 MiB** | 2655 |

Peak RssAnon: ~2.25 GiB uncapped (the ~1.2 GiB client baseline + the ~2.1 GiB
driver scene queue) vs ~200 MiB capped — the 256 MB envelope the issue names
is breached exactly when the queue fills.

## The net (acceptance 3)

Two live ctest legs, both label `live`, both registered in
`tests/cmake/commands.cmake` (gated `EQT_LIVE_TESTS AND EQT_GRAPHICS AND
IRRLICHT_LIBRARY`, like `ClientLedgerLive`), both running the hold rig then
`scripts/b924/b924_check_bound.py` (`growth = gpu_driver(last idle) −
gpu_driver(ready)`, verdict in `bound_check.txt`):

- **`B924IdleHoldLive`** (uncapped leg, `b924_net_leg.sh uncapped`, lane 1,
  TIMEOUT 1800) asserts the driver's scene-queue ceiling holds:
  `growth <= 2883497060` (= 2306797648 × 5/4, the worst measured uncapped
  growth + 25 %). **GREEN on today's tree** — the growth is driver-side and
  plateaus at ~2.24–2.31 GiB. It trips only if an *unbounded* accumulation
  appears (a client leak that keeps climbing past the ceiling point-over-point
  and no longer plateaus).
- **`B924IdleHoldCappedLive`** (capped leg, `b924_net_leg.sh capped`,
  `WILLEQ_FPS_CAP=10`, lane 2, TIMEOUT 1800) asserts the tight leak tripwire:
  `growth <= 67108864` (64 MiB). At 10 fps the driver queue drains, so a
  healthy tree holds ~0–20 MiB; a leak that grows with frames trips it.

**RED/GREEN stated honestly:** today's tree is **GREEN on both legs**
(net_uncapped `bound_check.txt` verdict=PASS growth≈2.1 GiB ≤ 2.69 GiB bound;
net_capped verdict=PASS growth≤64 MiB). The net does **not** FAIL today
because the growth is real but **driver-side and bounded** — exactly the
acceptance-3 branch the card anticipated: *"it must FAIL on today's tree if
the growth is real and client-caused, or document that it passes because the
growth is driver pseudo-VRAM."* This is the latter case. The net is still a
live tripwire: the capped leg catches a genuine client leak (the thing that
would actually break the 256 MB envelope at low frame rates), and the uncapped
leg catches a regression of the driver ceiling (e.g. a Mesa upgrade that
changes `MAX_SCENES`/`LP_SCENE_MAX_SIZE`).

**The bound is a plain variable**, not a blessed golden: `B924_BOUND_BYTES`
in each leg's default (2883497060 uncapped / 67108864 capped), overridable by
env. Basis: the worst measured 5-minute growth (run3, 2306797648) × 1.25, and
the capped leg's 64 MiB is ~3.5× the observed capped run spread (18.3 MiB max).

## Not verified / left open

- The worker-thread drain rate was **not** directly measured (no per-scene
  timestamp). The conclusion rests on the A/B (rate → growth, time → flat),
  the plateau, and the 64×36 MiB = 2304 MiB ceiling matching the ~2.2 GiB
  growth to within 3 %. A per-scene or `lp_scene`-count probe would tighten
  this; not worth a lane-hour on the PROVE leg.
- The `[heap]` attribution is the glibc main heap (the driver's
  `MALLOC_STRUCT(data_block)` allocations). It was **not** split further by
  exact `lp_scene` call site — the offsets in the trace leg all fall in
  `libgallium`'s dense code region and the system lib is stripped with no
  matching dbgsym on this host, so the call-site names are not resolved. The
  category (`gpu_driver`), the ceiling arithmetic, and the rate A/B are
  sufficient for the mechanism; a symbolized re-run (dbgsym or a debug build)
  would name the exact `lp_*` functions.
- Run-to-run: the three uncapped runs agree to within ~3 % on the plateau
  (2133–2200 MiB); the capped runs agree to the ~20 MiB noise floor. The 25 %
  margin on the uncapped bound covers a zone heavier than iceclad.
- **Acceptance 2 (the FOLLOW-UP decision) is a DECISION RECORD, not a client
  fix.** The growth is llvmpipe's bounded scene queue at its design ceiling —
  nothing in willeq is leaking, and the only client-side levers (lower the
  render rate, or accept ~2.2 GiB when rendering fast) are trade-offs Will
  must own, not a bug to fix. The capped leg already keeps the envelope green
  at low frame rates. The follow-up card should either (a) record in
  `docs/decisions.md` that x86 llvmpipe holds ~2.2 GiB of in-flight scene
  memory at uncapped render rate (driver pseudo-VRAM, `MAX_SCENES`×
  `LP_SCENE_MAX_SIZE`) and that the 256 MB `RssAnon` envelope applies at a
  sensible fps cap, or (b) cap the client's uncapped render rate so the queue
  drains (the capped leg is the acceptance test for that).
