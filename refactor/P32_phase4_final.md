# P32 — Desktop-GL elimination Phase 4 (final): entry-point removal + wind extraction + deletion

**Slot: slot1. x86 only — no Pi, no eqemu. Sole agent this dispatch.**

Executes Phase 4 under Will's 2026-07-28 scope ruling (docs/decisions.md
addendum — read it, plus P31's Review reachability table, which is the
ground truth this unit builds on). Order matters: remove the entry
points FIRST, so the deletions in step 3 become provably dead.

## Work (in order)

1. **`--renderer` → gles2-only.** `application.cpp` (~353-357 per P31):
   `software` and `opengl` values are removed; unknown/removed values
   fail fast with a clear message ("only gles2 is supported; software/
   opengl were removed — docs/decisions.md 2026-07-28"). Remove
   `RenderingBackend::Software`/`::OpenGL` enum values and every call
   site; collapse single-case switches cleanly.
2. **Decouple presets from renderer selection.** Per Will: presets
   define budgets/features ONLY and apply after renderer selection.
   Find the coupling that made `--constrained voodoo1|voodoo2` reach
   EDT_BURNINGSVIDEO (P31: CLI override wins, but the preset side must
   also stop implying a renderer) and remove it — every preset,
   including voodoo1/voodoo2/tnt, must configure its budgets/features
   on GLES2. Preset SEMANTICS (budgets, feature toggles) are untouched.
   Prove with a smoke run: `--renderer gles2 --constrained voodoo1`
   starts, applies the voodoo1 budget (cite the log line), renders.
3. **Extract the live wind logic out of AnimatedTreeManager.** P31
   mapped the live GLES2 consumers: tree classification (`isTreeObject`)
   used by the wind material pass (renderer_zone_stream.cpp:426-432,
   572-577), per-frame wind update + weather multiplier
   (renderer_frame.cpp:1068-1070,1158), wind snapshot/tree shadows/
   diagnostics (renderer_sim_bridge.cpp, renderer_diagnostics.cpp).
   Move that into a new, appropriately-named TU (e.g.
   `tree_wind_controller.{h,cpp}` under src/client/graphics/) —
   BEHAVIOR-PRESERVING: same classification results, same wind
   parameters, same update cadence. Then delete
   `animated_tree_manager.{h,cpp}` (the CPU-instancing machinery and
   the P20-era dormant mirror go with it), its construction/wiring,
   and the `loadZoneStep11_Env()` EDT_BURNINGSVIDEO branch.
4. **Delete the now-dead paths**: all remaining EDT_BURNINGSVIDEO
   client branches, the EDT_OPENGL loading-screen branch and the
   EDT_SOFTWARE retry fallback (irrlicht_renderer.cpp:611-616 —
   post-step-1 nothing can request them), the ARM `--no-gles2` Lima
   support: `scripts/build-arm-noble.sh`'s flag + the CMakeLists
   "Desktop GL mode via Lima" branch (~610-613) + its doc mentions.
   Driver-fork INTERNAL switch cases (docker/irrlicht-drm*/) remain
   out of scope, as in P31.
5. Docs: update the P31-rewritten renderer-tier table and roadmap —
   the elimination is now COMPLETE; `--help`/README text for
   `--renderer`; decisions.md gets a one-line "Phase 4 executed" note
   appended (do not rewrite entries).

## Rules

- Wind extraction is the risk center: goldens must be untouched (the
  gate exercises tree/wind materials at fixed frames) and the wind
  path's runtime behavior identical — if extraction forces ANY
  behavior choice, STOP and report rather than pick silently.
- Preset semantics untouched; only the renderer coupling goes.
- Grep-proof at the end: `EDT_BURNINGSVIDEO`, `EDT_OPENGL`,
  `RenderingBackend::Software`, `RenderingBackend::OpenGL`,
  `AnimatedTreeManager` absent from src/ + include/ + scripts/
  (driver forks + historical docs + plan files excepted — list
  residual hits).
- STOP: reachability surprises beyond P31's map; extraction cascade
  beyond the named consumers; any golden movement.

## Verification

- Full current bar at the NEW baselines: GLES2-config tree build +
  ctest **1361/1362** (ALSA exception only), `build-gles2-x86.sh`
  clean, visual gate PASS goldens untouched, `build-arm-noble.sh`
  (WITHOUT the retired flag) clean, headless tree **939/940**,
  `check_file_sizes.sh` no new FAILs.
- The step-2 preset smoke evidence (voodoo1 budget applied on gles2).
- Negative test: `--renderer software` fails fast with the documented
  message (show it).
- Grep-proof table.

## Review

(executor fills in: per-step evidence, the extraction's consumer-by-
consumer mapping old→new, smoke + negative outputs, verification
numbers, grep table, deviations)
