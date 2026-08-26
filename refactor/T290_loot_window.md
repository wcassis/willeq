# T290 — restore the loot window (Will: REQUIRED; it was ~80% working before the static-UI migration)

Slot: slot2. x86. OWNS eqemu (noclobber) for a live loot with a real corpse.
Class A, feature restoration. Territory: `src/client/graphics/ui/static_panels.*`,
`static_ui_input.*`, the loot bridge arms in `irrlicht_bridge.cpp`, a new
`LootPanelState`, the three parked loot intents, tests, goldens if the panel
lands in one. Do NOT touch the Pi/composite path (T289 slot1) or the visual-gate
metric (T291 slot3).

## The prior art — RECOVER IT, do not redesign
Will: *"This has been implemented previously before the static ui changes. The
previous loot window was 80% working."* Confirmed in history:
- `include/client/graphics/ui/loot_window.h` (163 lines)
- `src/client/graphics/ui/loot_window.cpp` (867 lines)
both deleted by **U07c2 `0f25cb57`** ("Delete WindowManager + all old UI window
files"). Recover with `git show 0f25cb57^:<path>`.
- `refactor/D06b_publish_loot_vendor.md` (also on that parent) documents the
  publish path the old window consumed.
Read all three FIRST and port the working parts. The old code targets the
deleted `WindowManager`; the layout, hit-testing, item-row rendering and state
machine are the parts worth keeping.

## What T283 established is missing (the 20%)
- `PopupType::Loot` is a bare enumerator; none of `static_panels.cpp`'s thirteen
  render functions is a loot popup.
- `static_ui_input.cpp` knows only `Inventory` plus a generic Escape arm.
- **There is no renderer-side corpse-contents state at all** — it went with the
  `getWindowManager()` removal.
- The four loot game events reach `IrrlichtBridge` only to `LOG_TRACE`
  (`irrlicht_bridge.cpp:455-467`), **discarding their payloads** — so the
  renderer does not know what is on a corpse. Those four arms are the real work.

## Scope
1. `LootPanelState` fed by four real bridge arms (replacing the LOG_TRACE stubs).
2. Port the old window's render + layout + hit-test onto the static-panel
   pattern (T272's spellbook click and T277's buff-bar hit-test are the two
   worked examples — note T277's lesson: a PACKED list means on-screen row ≠
   slot index).
3. Wire the three parked intents to the SAME handler bodies `/loot`, `/lootall`,
   `/lootclose` already use (T230/T239 one-body rule) — no second path.
   Delist them in `tests/test_reachability_sweep.cpp`.
4. Tests: panel state from the bridge arms; a click at an item's rect pushes the
   right intent; red/green measured.
5. Live: kill something (T247's auto-defence recipe), corpse opens the panel
   with real items, click one to take it, loot-all, close. DB proof + screenshot.
   ⚠ auto-loot is ON by default and will empty the corpse first (T268).

## Golden re-bless
If the panel appears in a golden view, that IS a re-bless — STOP and report
rather than blessing on your own; the only pre-approved re-bless was T279's.

## Verification
Full ctest (`-j6`); visual gate; Review; evidence review/T290/. Commit:
`T290: the loot window is back`.
