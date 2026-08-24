# T200 — Baseline hangs too: T199 is exonerated

T199's verification hung at `[Phase 11/15]`, and I could not say whether that
was my change or the regenerated assets, because T198's baseline run against
that tree died earlier on the `fishe0301` FATAL and never reached Phase 11.
This establishes the missing baseline.

## Three controlled runs, all BASELINE (`bc54e527`, no T199 in the binary)

| run | tree | preset | zone | result |
|---|---|---|---|---|
| 1 | **regen** | orangepi | qeynos2 | **hang @ Phase 11/15** |
| 2 | **live** | orangepi | qeynos2 | **hang @ Phase 11/15** |
| 3 | **live** | **none** | qeynos2 | **hang @ Phase 11/15** |

Every one: process **alive**, log frozen, no `FATAL`, no `Ready`, last line the
same `mergedraws` entry.

## What this settles

- **T199 is not the cause.** The hang reproduces with none of its code present.
- **The regenerated assets are not the cause.** The live tree hangs identically.
- **The `orangepi` preset is not the cause.** Dropping it changes nothing.

T199's branch is therefore blocked only by a pre-existing fault, not by its own
change — and its actual result stands: 15 spawned races resolved to 3 queued
archives, and the `fishe0301` FATAL gone.

## What is NOT settled

**What does cause it.** The only known-good data point is T193: baseline, live
tree, no preset, **befallen** — reached `Ready` in 13.25s. Every hanging run
above is **qeynos2**, so the zone is the remaining uncontrolled variable and the
obvious next test. But T199's own hang was in *befallen* with the regen tree and
the preset, so a single-variable story does not yet cover every observation.

Isolating this needs a matrix over {zone} × {tree} × {preset} rather than
another guess.

## Method note

Moving a character between zones for these runs must be done with the client
**fully disconnected** (`UPDATE character_data SET zone_id=…`), per T190 — a
live session overwrites the row on save, which is why an earlier attempt "did
not stick".
