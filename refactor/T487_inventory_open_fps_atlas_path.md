# T487 — the open inventory still halves the frame rate on the atlas UI path at 1280x720 (B506 residue)

Class A (Will's 2026-09-13 report: "inventory halves fps"), **lock + Pi**, one slot.
Found by the 2026-09-14 audit (`review/AUDIT-0914/README.md` §inventory).
Read first: T459 Review (B506), T462 Review and `review/T462/README.md`
(the fallback-path fix, 60 fps closed AND open), `refactor/EXECUTOR_PROTOCOL.md`
Pi + eqemu sections, `feedback-watch-long-live-runs` (dead-client watchdog,
short stall deadlines).

## The measurement (tip 0daacef4 ARM, Will's flags + `--eqe1-path`, 1280x720, nro)

`review/AUDIT-0914/pi/raw/R3_tip_atlas_1280_inventory` (analysed with
`review/T462/scripts/analyze_ab_split.py <run> -4`): closed 44 fps / Total
Frame 17.7 ms; open 25.6 fps / 33.2 ms — exactly two vsync intervals. CPU
sections account for the difference only partly: Static UI 0.5 → 4.1 ms
(`UI Quad Flush` 179 us/2 draws → 3,425 us/**87 draws**, 178 commands), the
paperdoll 0.2 ms, scene draw unchanged (3.5 ms). ~12 ms of the open frame is
unaccounted CPU-side, i.e. GPU/vsync wait ("stall 63/90"). T462's 60/60 was
measured on the FALLBACK path (no atlas, 2 draws); the README recipe now
passes `--eqe1-path`, so this is the path Will runs.

## Tasks

1. **Isolate the GPU cost, by A/B on the Pi, one variable at a time**
   (use `review/AUDIT-0914/scripts/run_pi.sh` + `inventory_ab.sched`;
   `--frame-timing --log-level=INFO`; no frame dumps in timed windows):
   (a) atlas path, inventory open, paperdoll RTT suppressed (add a temporary
   env/flag seam if none exists — remove it before merge or keep it as a
   documented diagnostic); (b) icons suppressed (slot backgrounds only);
   (c) the 87 draws collapsed by sorting the popup's commands by texture
   within a layer (backgrounds, then icons, then frames — layering must be
   preserved: pin with the T462 byte-identical screenshot test at 1280x720
   as well as 800x600); (d) 800x600 for the fill-rate axis.
   Report a table: state × variant → wall fps, Total Frame, Static UI,
   Quad Flush draws.
2. Fix the dominant cost. Likely shapes: texture-run sorting inside a panel
   (c); paperdoll RTT re-render cadence (only on animation frame change, or a
   lower RTT resolution on the orangepi preset); avoiding an FBO switch per
   frame on Mali-400 (tile flush). Do not trade correctness: the popup must
   still look identical (screenshot pin) and the paperdoll must still
   animate as the oracle's does.
3. Pin: extend `tests/test_static_ui_frame_cost.cpp` so its pins also apply
   at 1280x720 (`pinsApply()` is 800x600-only today), and add a draw-count
   ceiling for the open inventory on the atlas path.
4. Board: amend B506 (T462 closed it on the fallback path only) with the
   numbers above; new items for what you find.

## Proof

Standard verification bar; Pi before/after A/B at 1280x720 on the atlas
path with the recipe in `review/pi/README.md` (`review/T487/` with README,
never self-assess); ARM; file sizes; goldens for touched TUs in place. Lock:
`eqemu_lock_take T487`, release at the end; Pi left pristine; watchdog the
client pid, stall deadline 5 min, never wait out a dead run.

File territory: `src/client/graphics/ui/ui_renderer*.cpp`, `ui_quad_batch*`,
`static_panels.cpp` inventory popup, paperdoll RTT (`overlays_hud_env.cpp`,
`character_model_view.cpp` — coordinate: T485 touches
`character_model_view.cpp`'s placeAttachment lambda only), the frame-cost
test, `review/T487/`. **Not** held-item transforms (T485), not T486's files.

STOP: the fix needs a change to the paperdoll's visual output; the dominant
cost is inside the Mali driver with no client lever (report the table).
