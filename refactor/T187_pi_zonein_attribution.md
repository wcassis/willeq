# T187 — Where the Pi's 37s actually goes

T186 established the number has not moved (Pi ~37.7s cold befallen zone-in,
x86 ~12.1s). This attributes it. **No new instrumentation was needed** — the
renderer already times every sequential load step via `logAssetBuildTime`,
which logs at WARN above 10ms, so the answer was already sitting in T186's
captured logs.

## The breakdown

Cold befallen zone-in, per step, both Pi runs and the x86 pair:

| step | Pi run 1 | Pi run 2 | x86 | Pi/x86 |
|---|---|---|---|---|
| `seq_s3d_parse` | 0.61s | 0.52s | 0.70s | 0.8x |
| `seq_bsp_compute` | 0.52s | 0.54s | 0.37s | 1.4x |
| `seq_bsp_install` | 0.01s | 0.01s | — | — |
| `seq_regions` (#3596) | 3.12s | 3.16s | 0.25s | 12.6x |
| `seq_assets` | 4.92s | 3.91s | 0.92s | 4.6x |
| `seq_objects` (#346) | 0.70s | 0.78s | 0.26s | 2.8x |
| `seq_doors` | 0.03s | 0.03s | — | — |
| **`seq_entities`** | **24.53s** | **23.60s** | **5.41s** | **4.4x** |
| `seq_collision` | 0.16s | 0.14s | 0.02s | 7x |
| `seq_sky` | 0.38s | 0.40s | 0.04s | 9.5x |
| `seq_env` + `seq_simulation` | 0.52s | 0.52s | — | — |
| **sum** | **~35.5s** | **~33.6s** | **~8.0s** | |

The remaining ~3s to the Phase 5 → Phase 15 total is the pre-graphics
connection phases.

## The answer

**`seq_entities` is 64% of the Pi's cold zone-in.** Nothing else is close:
`seq_assets` (~4s) and `seq_regions` (~3.1s) are the only other steps above a
second, and together they are half of what entities costs.

**And the Pi is disproportionately bad at exactly this step:** 4.4x x86 on
entities against 3.0x overall. Whatever entity prep does, it is the work the
Pi handles worst, not merely the largest pile of it.

(`seq_regions` at 12.6x is a worse *ratio*, but it is 3.1s — a fix there buys
seconds; a fix in entities buys tens of seconds.)

## Why the gate did not deliver

T109's load-distance gate **is** active — `deferEntityForLoadGate` runs in
Step 8 and the `EntityPrepWorker` starts for the deferred set — but:

**12 distinct (race, gender) pairs are still preloaded.** T105 measured
**6-9** as actually needed at the 300-unit render distance, against 16-20
loaded ungated. So the gate cut roughly the top half of the overloading and
stopped well short of the floor. That is consistent with T186's headline: it
did something, just not enough to move the total.

The question for whoever takes this on is not "is the gate wired" — it is
**why 12 and not 6-9**, given the same zone and the same render distance
T105 measured against.

## A separate, platform-independent finding

The composite-texture atlas path falls back to the expensive multi-buffer
build **6 times per zone-in**, preceded by:

```
[WARN] RaceModelLoader: Composite texture not yet uploaded for race 367
       — falling back to multi-buffer
```

The composite sheet is *found* (12 "composite sheet hit" lines, per-part
textures deferred), then at mesh-build time it is not uploaded yet, so the
build takes the multi-buffer path — and the per-part textures that were
deferred have to be loaded after all. The optimization pays its setup cost and
then misses its window.

**The counts are IDENTICAL on Pi and x86** (6 fallbacks, 2 "not yet uploaded",
12 sheet hits), so this is an ordering bug, not a platform limitation, and it
does not explain the Pi's disproportion. It is worth its own unit on both
platforms. Note also `SEQ Step8: uploadAllPending uploaded 0 entity textures`
at the end of the step — nothing was pending by then.

## Recommended next step

Attack `seq_entities`, and start with the gate: establish why 12 race/gender
pairs survive it when T105 measured 6-9 as sufficient. The composite-upload
ordering bug is real but is a second, smaller, cross-platform unit.

**Do not re-measure the total first** — T186 did that, and the per-step
instrumentation above is already sufficient to tell whether a change helped.

## Method note

Evidence is T186's logs (`review/T186/`), read at INFO — `logAssetBuildTime`
emits WARN above 10ms, and every zone-load step clears that bar, so no DEBUG
run and no code change was required. The Pi's clock runs at a different UTC
offset from the VM's; compare deltas, never absolute times across the two.
