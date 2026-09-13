# T462 — the static UI's per-frame cost on the Pi: the inventory popup (B506) and the chat re-wrap (B505)

Class A (Will's Pi report: "opening the inventory halves fps"), **eqemu lock +
Pi ownership for the before/after measurement**, slot3. Read T459's Review
(`refactor/T459_pi_live_diagnosis.md`) and `review/T459/README.md` first —
its numbers, its uinput recipe (B510) and its run scripts are your rig.

## Will's ruling (2026-09-13)

Do not chase the old pre-static-UI numbers; account for and reduce the perf
hit of the CURRENT static-UI inventory.

## What T459 measured (Pi, 800x600, DRM, vsync)

- Inventory closed → open: static UI 11.1 → 17.0 ms, wall 48.4 → 37.9 fps
  (Will's binary). The paperdoll is ~0.2 ms/frame amortised (re-render
  ~2.1 ms every ~12 frames + a one-off 243 ms build on first open) — NOT the
  cost. The ~6 ms is the popup's own draw calls.
- With vsync every frame over 16.7 ms is presented at 33.3 ms, which is why
  it reads as "halved".
- **B505:** the chat panel re-wraps its whole 1,000-message buffer every
  frame; static UI grew 11.9 → 22.0 ms over 5 minutes standing still with the
  inventory closed. This is a baseline cost Will pays everywhere and grows
  with session length.

## Tasks

1. **Account.** On x86 first (cheap, deterministic): count the popup's draw
   calls / text entries / state changes per frame (the T393 render pin
   `TheInventoryPopupEmitsTwoTextEntriesPerCoinRow` shows the tree can count
   entries headlessly). Profile where the 6 ms goes (text layout? per-slot
   icon lookups? 2D state switches that break batching? the alpha companion
   bind per draw?). Same for the chat panel's wrap path. Write the account
   into the Review before changing anything.
2. **B505 fix:** wrap on change only (new message, width change, scroll),
   cache wrapped lines; pre-fix failing test that the wrap runs O(changes)
   not O(frames) (a counter seam), plus a correctness test that the displayed
   lines are identical.
3. **B506 fix:** choose from the account — batching / removing redundant
   per-frame work first; a render-target cache of the popup redrawn only on
   change (the `43c56a70` shape U07c2 deleted) only if the account shows the
   draws themselves are the cost. If an RT: memory on the 483 MB board is
   part of the design (size it, count it against the constrained budgets),
   and the paperdoll RT composes into it without re-rendering it. Pixel
   identity against the uncached draw must be pinned by a test (the gate's
   GLES2 device), and hover/drag/tooltip state must invalidate correctly.
4. **Diagnostics from T459** (`renderer_frame.cpp` camera trace + cursor log,
   `overlays_hud_env.cpp` paperdoll timer): keep what is useful as a
   permanent, properly named timing section (`frameTimings_`) or TRACE line;
   delete the `T459`-named INFO lines. If the camera trace goes, restore the
   `getActualFollowDistance` row in `scripts/refactor/check_zero_callers.py`
   (T459 removed it).
5. **Prove on the Pi** with T459's rig: inventory closed vs open, ≥3 repeats,
   same pose; and a 5-minute standing run for B505. Before = fable tip binary,
   after = your branch. Report the per-section split and wall fps; note the
   vsync quantisation when interpreting.

## Proof

Standard verification bar; G-batch goldens for every TU touched (the
static-UI render function is statement-pinned — a reviewed edit updates the
golden IN PLACE per G-correction 32, and say so). Visual gate: no image golden
moves. eqemu + Pi protocols (lock, world down, pose restored, Pi pristine,
≤12 invocations). Media `review/T462/`. Board B540–B549; strike B505/B506 with
evidence.

File territory: `src/client/graphics/ui/` (static panels, chat), the T459
diagnostic lines, `frameTimings_`, tests. **Not** camera/hotkeys/slash
(T461 in slot1), not corpse/combat (T456 in slot2).

STOP: the only effective fix is an RT cache that costs more memory than the
orangepi budget has headroom for (report the numbers); an image golden moves.

## Review

(executor fills)
