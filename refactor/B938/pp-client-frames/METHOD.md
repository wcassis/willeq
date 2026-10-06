# B938 acceptance-2 — per-pixel leg through the CLIENT's changed draw path

The CI visual gate runs `gles2_zone_benchmark`, which **never constructs the
client's `IrrlichtRenderer`** and links **none** of the TUs B938 changes
(`zone_bench_memory.cpp:4`: "This benchmark tool never constructs an
IrrlichtRenderer"). Its golden-image runs also pass no `--objects` (objects
unloaded; `loadObjects` defaults false), so its 0-pixel mismatch is a
benchmark-render net — it nets B936's mesh sharing and the zone geometry, **not
the client's object-draw path**. QA F1: that is not the per-pixel instrument for
this unit.

This leg is the real one: the **x86 rendered client** over a PVS zone *with*
objects, captured at a fixed pose through the client's own `--dump-frames-dir`
path, which drives exactly the manual PVS `drawZoneGeometrySorted()` — the
routine B938 changes (per-placement `IMeshSceneNode`'s `getMesh()` /
`getRelativeTransformation()` → `PlacedObjectRecord`'s `.mesh` / `.world`).

## Method

- **Zone:** gfaydark — a PVS zone with 2,829 placed objects (79 distinct
  geometries), only 3 of them vertex-animated. Seeded at the zone's own safe
  point (zoneid 54, 10 / −20 / 0, heading 0), the character never moves, so the
  pose is identical in every run (the zone's linkdead cache resumes it).
- **Lane:** the eqemu VM (`172.16.200.99`), one lock hold for the whole grid,
  one snapshot, restored byte-identically with the world down, lock released
  last. The rendered client ran under the x86 recipe
  (`--renderer gles2 --constrained orangepi -r 1280 720`, surfaceless EGL on
  llvmpipe), setcap'd for `CAP_NET_ADMIN` (the 1 MB UDP receive buffer).
- **Binaries:** base = fable **3c93eeff** (per-placement scene-node object draw;
  its `task/B945` tip is already merged into fable), head = **f6580c00**
  (B938 tip ed1e81ed + the fable merge). Each ran from **its own checkout**.
  `bin_sha256` for each run is in the raw `run_meta.txt` files.
- **Pose / determinism levers** (set by `--script` after zone-in, identical in
  both binaries): `/camera firstperson` — hides the local player's animated
  body (`entity_first_person_draw.cpp`), the biggest run-to-run variance source
  the player brings; `/fire` — the one particle-manager source. The character
  stands still.

## Why a control band, not "0 diff"

The client's frames are **not bit-deterministic across runs** — B936's own
"Not verified" says so (entities, sky, particles). gfaydark's safe point is in
a mob area (sylven bats, widow hatchlings respawn), so the combat log and a
passing mob differ between two runs of the *same* binary. A single base→head
number would be dominated by that. So the grid is 2×2:

- **A1, A2** = base binary, two runs (same-binary **control**).
- **B1, B2** = head binary, two runs (same-binary **control**).
- **A1→B1** = base→head (**the B938 comparison**).

B938's effect is only claimed against the band: a base→head pixel change counts
as a B938 change only if it exceeds the same-binary run-to-run noise.

The metric is the **3D world region** — the fixed UI rects (status, combat log,
hotbar, gems, target) are excluded, since they carry the run-to-run text. The
world region is 677,200 px of 1280×720.

## Result (per-frame table: `per_frame_table.md`, 40 common frames)

| pair | world-region differing px (sum over 40 frames) |
|---|---:|
| **A1→B1 (base→head, B938)** | **3,723** |
| A1→A2 (base, same binary) | 16,844 |
| B1→B2 (head, same binary) | 21,436 |

The base→head world-region diff is **4.5× smaller than the base same-binary
control and 5.7× smaller than the head same-binary control**, in *every single
frame* (see the table — no frame's B938 diff exceeds the controls). The full-frame
diff is dominated by sub-8-level llvmpipe FP16/ETC1 rounding (on the committed
frame 20: mean max-channel abs diff 0.809 base→head vs 1.91 / 2.50 for the
controls, with >16-level pixels at 0.01% / 0.70% / 0.79%). The 4–16 band on
frame 20: base→head has 18 pixels (2 in (4,8], 16 in (8,16]), vs 38 / 43 for the
two controls — the smallest of the three.

`heatmaps/diff_base_vs_head.png` (the B938 comparison) is almost entirely black,
with only tiny clusters where the 3 vertex-animated plants (the one thing that
moves, which B936 had too) and any passing mob land. The static placed-object
geometry — B938's actual subject — is pixel-identical.

**Verdict:** the client's object-draw path is frame-unchanged by B938; its
per-frame pixel change is below the client's own run-to-run noise floor. This is
the per-pixel acceptance-2 evidence the gate could not provide.

## Provenance

- Raw frames + per-run `run_meta.txt` / `command_line.txt` / `client.log`:
  `~/kb-evidence/b938-placement-records/pp-client-frames/{baseA1,baseA2,headB1,
  headB2}/` (164 frames total, ~700 KB each).
- Committed here: one steady frame from base and head (`frames/`), the three
  diff heatmaps (`heatmaps/`), the per-frame table, and the diff tooling
  (`pixel_diff.py`, `diff_heat.py`, `pp_table.py`) — reproducible from the raw
  set with those scripts.
- Capture rig: the `pp_orchestrator.sh` + `pixel_diff.py` + `diff_heat.py`
  + `pp_table.py` scripts in this directory's sibling `~/kb-evidence` evidence
  tree (a one-shot `pose_live_capture.sh`-style rig; not committed to the repo
  because it is a throwaway lane script, like `camera_match_sweep.sh`).
- x86: one run per (binary, side) leg, 2 legs per binary (the control band).
  The Pi leg is not part of acceptance 2 — it is the memory acceptance,
  measured separately (the README's Pi table).
