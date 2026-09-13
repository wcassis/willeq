# T459 — Orange Pi live diagnosis: no cursor, inventory fps halving, camera jerk, missing spell gems

Class A, **eqemu lock + Pi ownership**, slot3. Measurement unit: it produces
evidence and board entries, and fixes only what the evidence makes a one-line,
test-pinned change (say so if you do). Fixes that need design belong to
follow-up units the orchestrator plans from your Review.

## What Will reported (2026-09-13, Orange Pi One, `review/pi/README.md` recipe)

Binary `main/build-arm-noble/bin/willeq` (built 2026-09-08 01:27 UTC from
`8401a33f`; the only later commit touches no source, so it IS the tip's code),
`--drm --renderer gles2 --constrained orangepi -r 800 600`, account
`willeq_test` / `Testcaster` (Wizard). Config: `review/pi/willeq_pi.json`
(host now 172.16.200.99).

1. No mouse cursor visible in-game.
2. Opening the inventory halves fps.
3. **Default (Follow) camera**, **moving forward on flat ground**: the camera
   jerks rapidly between two different perspectives.
4. The spell gem bar is missing THE WHOLE SESSION; the character had spells
   memorized.
5. Held items are misoriented in the hands (NOT this unit — needs oracle
   goldens after T461's camera work).

## Orchestrator's code triage (leads, NOT findings — confirm or refute each)

1. **Cursor.** `renderer_init.cpp` creates the software cursor only under
   `--drm`; `renderFrameTail` draws it only while `owner_.softwareCursorTexture_`
   is non-null. `ConstrainedTextureCache::clear()` (zone unload,
   `renderer_zone_lifecycle.cpp`) notifies eviction listeners for every
   texture not `protectTexture()`'d — only the UI atlas and item icon sheets
   are protected — and the cursor's guard (`renderer_zone_merge.h`) nulls
   the pointer; nothing reloads it. So: visible at the loading screen / before
   the first zone unload, gone after. Alternatives: an LRU eviction under the
   orangepi budget; no evdev mouse found (`CIrrDeviceFB.cpp` logs it). **T460
   is fixing this mechanism in parallel on x86** — your job is the live
   before/after-zone-in observation and the log lines, so T460's fix is proven
   against the real symptom.
2. **Inventory fps.** Paperdoll re-renders its RT (~158x254, own scene
   manager, 3 point lights) when dirty + every 200 ms (a hardcoded 16 ms/frame
   counter, not wall time). The static inventory popup (`static_panels.cpp`)
   is redrawn from scratch every frame; the pre-static-UI InventoryWindow was
   cached to an RT (`43c56a70`, measured ~13 ms/frame on Mali-400 for 70+
   uncached draws) and U07c2 (`0f25cb57`) deleted that cache. Will: do not
   chase old numbers — ACCOUNT FOR THE CURRENT HIT.
3. **Camera jerk.** Four writers of the Follow camera (physics, zoom, lifts,
   bridge `PlayerMoved` → `setPlayerPosition` → `setFollowPosition` with dt 0).
   Suspected: `movement_update.cpp` publishes `HeadingDeg()` (0–360) where
   every other publisher sends 0–512 (T460 checks this in code); self
   `OP_ClientUpdate` corrections alternating with local integration; BSP
   camera pull-in with a one-frame-late region. ⚠ The 90° player/NPC heading
   offset is deliberate — never "harmonise" it.
4. **Spell gems.** `renderSpellGemPanel` has no Show gate and draws 8 slots at
   x=764, y=268–552 for 800x600. Suspected: DRM mode fallback (the display has
   no 800x600 mode → `CIrrDeviceFB.cpp` uses the preferred mode while the UI
   lays out for 800x600), or the panel covered.

## Tasks

0. Lock (`eqemu_lock_take T459`), `up.sh --launcher`, check the Pi
   (`ssh orangepi`: uptime, free, `ls /dev/input/by-id`, attached display's
   modes from `/sys/class/drm/*/modes`). Select Testcaster's current pose from
   the db before moving it; restore what you change. Force daylight.
1. **Input.** Find how prior Pi units drove movement/UI (`scripts/pi/`,
   `--path-file benchmarks/paths/*.path`, slash commands such as
   `/inventory`, uinput). If there is no way to inject a mouse move or
   keyboard forward-run on the Pi, say so and use the nearest faithful
   substitute (path-file forward run); STOP only if no substitute exists.
2. **Cursor:** screenshots (`--dump-frames-dir` to Pi-local /tmp, bounded)
   before zone-in and after; the log lines for mouse detection, texture cache
   clear, cursor eviction. State which lead is confirmed.
3. **Spell gems:** full-frame screenshot in world; log `DRM:` mode lines;
   actual framebuffer size vs 800x600; memorized gems in the game state (log).
   Also take the same shot on x86 `build-gles2` at 800x600 for contrast.
4. **Inventory fps:** with the existing `frameTimings_` sections (find how they
   are reported — a debug overlay, log, or `--frame-timings`-style flag), a
   standing-still run of N seconds inventory CLOSED vs OPEN, same pose and
   heading, ≥3 repeats each. Report fps plus the per-section split
   (staticUI, paperdoll RT update, everything else). If the tree cannot
   separate the paperdoll from the popup, add the smallest timing split on
   your branch (commit it) rather than guessing.
5. **Camera jerk:** a forward run in Follow on flat ground, video via
   `--dump-frames-dir` + `scripts/ci/encode_capture.sh` (capture fps is not
   performance), plus DEBUG/TRACE logs of every camera writer with frame
   number, position and heading. Identify the two alternating states and the
   writer of each. Also repeat on x86 `build-gles2` — does it reproduce off
   the Pi?
6. Board each confirmed defect as its own entry (B505–B519 are yours), with
   the measured evidence and the refuted leads named.

## Proof / constraints

- Pi protocol (EXECUTOR_PROTOCOL "Pi"): Pi-local /tmp for I/O, ≤12
  invocations, leave it pristine. eqemu protocol: lock, world DOWN at the end,
  lock released, character pose restored.
- No golden moves. If you commit source (a timing split or a trace line), the
  standard verification bar applies to it; a docs/board-only branch needs the
  doc nets (`ctest -R 'Doc|Board|Comment'`).
- Media + README: `/mnt/projects/fable/review/T459/` (`pi/`, `x86/`).
- File territory: `refactor/BOARD.md` (your B range), this plan,
  `review/T459/`, and — only if needed — timing/trace lines in
  `src/client/graphics/frame/`. Do NOT touch the cursor/texture-cache code
  (T460) or `renderer_camera.cpp`/hotkeys/slash commands (T461).

STOP: the Pi or the display is unreachable; the server cannot reach zone-in.

## Review

(executor fills)
