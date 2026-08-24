# T206 — First valid Pi EQE1 measurement: x86 halves, the Pi does not

With the UI-atlas crash fixed (T205), spawn-driven archives merged (T199), the
composite atlas out of the preset (T198) and regenerated assets (T198), the Pi
completes an EQE1 zone load for the first time — `PI_EXIT=0`.

## Result

Cold befallen zone-in, Phase 5/15 → Phase 15/15:

| | S3D baseline (T186) | **EQE1** | change |
|---|---|---|---|
| **Pi** | ~37.7s | **39.9s / 39.1s** | **~+4% (slightly worse)** |
| **x86** | ~12.1s | **5.0s** | **−59%** |

Two Pi runs, one x86 run, all `EXIT=0`.

## Reading it

**On x86 the flat-pack + ETC1 pipeline is a large win** — the zone-in more than
halves. That is the case Will expected from EQE1: fewer file opens, no per-
texture decode.

**On the Pi it is not a win at all**, and is consistently a little worse. Two
runs agree to within a second, so this is not noise at the scale of the
difference.

That asymmetry is the finding. The Pi is the platform EQE1 was built for, and
it is the one platform where it currently buys nothing. Worth noting the Pi's
absolute number barely moved across the whole arc — ~37.7s (S3D, T186) to
~39.5s (EQE1) — while x86 went 12.1s → 5.0s.

## What this does NOT explain, and should be measured next

Why the Pi fails to benefit. The per-step `Progressive: seq_` instrumentation
(T187) is the tool: re-run the T187 split under EQE1 and compare against its
S3D numbers, where `seq_entities` was 64% of the Pi's load. If entity work is
still dominant and unchanged, the flat-pack/ETC1 win simply does not touch the
Pi's bottleneck — which would make ETC1's value on the Pi a memory story rather
than a load-time one, consistent with T195's finding that its cost there was
address space.

**Do not re-measure the total again first** — the per-step split is what
distinguishes "EQE1 does nothing here" from "EQE1 helps but something else got
worse".

## Configuration

Pi: `--drm --renderer gles2 --constrained orangepi --eqe1-path
/mnt/projects/EverQuest_ETC1_regen -r 800 600`, ARM binary built from `3edeb0dd`
(T199 + T205), preset deployed from the repo with the atlas key removed.
x86: same flags minus `--drm`. Both characters placed in befallen via DB while
disconnected.
