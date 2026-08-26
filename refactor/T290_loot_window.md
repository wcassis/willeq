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

---

## Review

Executed in `/mnt/projects/fable/wt/slot2`, branch `task/T290-loot-window`,
off fable `b36c609c`. Evidence: `/mnt/projects/fable/review/T290/`
(README + 12 screenshots from two live sessions + the DB before/after/
restored triple + red/green logs).

### How much of the old window was reusable

The old `LootWindow` was 163 + 867 = **1,030 lines**. What came back is
**~230 lines of `src/client/graphics/ui/loot_panel.cpp` + ~90 lines of
`static_panels.h`**. The split, honestly:

| old part | fate |
|---|---|
| open/close/add/remove state machine | **PORTED**, semantics intact (see the two behaviour changes below) |
| "display row -> CORPSE SLOT" indirection (`getCorpseSlotFromDisplayIndex`) | **PORTED** — this is the load-bearing part, and it is the same lesson T277 relearned on the buff bar |
| scrolled visible window over a growing list (`scrollOffset_`, `getMaxScrollOffset`) | **PORTED** |
| Loot All / Close buttons + their hit-test | **PORTED** |
| `map<slot, unique_ptr<ItemInstance>>` ordering | **REWRITTEN** as an explicit sorted-insert `vector<LootItemDisplay>` — the renderer has no `ItemInstance`, only what the bridge event carries |
| 4x2 icon grid + `ItemSlot` rendering | **REWRITTEN** as full-width name rows. Deviation, flagged: the static UI has no item-icon atlas (the inventory popup draws three-character abbreviations), and a grid of 3-char abbreviations is a worse loot window than a list of real item names. The screenshots show why. |
| scrollbar with draggable thumb + up/down arrow buttons | **DROPPED**. Needs sprites and a drag state machine the static UI has none of; replaced by the "N-M of T" indicator `renderSkillsPopup()` already uses. Scroll input itself is not wired (no wheel plumbing in `StaticUIInput`) — flagged as the one piece of the old window not restored. |
| `WindowBase` shell: title-bar dragging, per-window callbacks, z-order | **DROPPED** — that whole class is gone (U07c); the static UI is anchored regions. |
| `canLootAll()` / `canLootItem()` / `getLootAllError()` (~250 lines of inventory pre-flight: stack space, bag sizes, lore conflicts, cursor) | **DROPPED**. It needed a renderer-side `InventoryManager`, which no longer exists, and it duplicated a judgement the server makes anyway — the live runs show the server's refusal arriving in chat one hop later, in the player's own words ("You may not loot an item while you have an item on your cursor"). Restoring it would mean rebuilding a renderer-side inventory model to re-derive an answer we already get. |
| `DestroyAll` button | **DROPPED** — T268 established Titanium has no destroy-loot packet; `DestroyAllLootIntent` was deleted then. |

Two deliberate behaviour changes from the old window, both because the old
one was wrong about who owns the state:

1. **The panel never closes itself.** `close()` used to flip `isOpen_` and
   then call back. Here a Close click / ESC pushes `CloseLootIntent` and the
   panel stays up until `LootWindowClosed` arrives. `CombatManager` only
   leaves `COMBAT_STATE_LOOTING` on the real close (T268), so a local close
   would desync it. Same "ask, never assume" rule T277 applied to buffs.
2. **An emptied corpse does not auto-close the panel.** `removeItem()` used
   to call `close()` when the map emptied — which would now fire a close the
   server did not ask for, mid-`/lootall`.

### The 20% that was actually missing

`src/client/bridge/irrlicht_bridge.cpp:455-467` — the four
payload-discarding `LOG_TRACE` arms. They now call the four `applyLoot*()`
free functions (bodies in `loot_panel.cpp`, so tests drive them without an
Irrlicht device — `applyHotbarSlotAssigned`'s precedent), and
`LootWindowOpened` also closes the other three center popups.

### The three parked intents: DELISTED, one body each

`LootItemIntent` / `LootAllIntent` / `CloseLootIntent` are off
`kBlockedIntents` in `tests/test_reachability_sweep.cpp`. The popup pushes
the SAME intents onto the SAME `eq_bridge.cpp` consumer arms the `/loot`
family reaches (`LootItemFromCorpse`, `CombatManager::LootAll`,
`CloseLootWindow`) — no second path was created. Sweep 5/5 green after the
delisting.

### Bonus fix, flagged as a deviation: `StaticUIInput::processKey()` had no caller

Wiring the loot ESC arm exposed that `processKey()` — the static UI's whole
ESC-dismisses-a-popup handler — was reached ONLY from
`tests/test_static_ui_panels.cpp`. Nothing in production called it: the T48
implemented-but-unreached class, instance N. `renderer_frame.cpp`'s escape
handling now calls it before clear-target, so ESC dismisses the top popup
first (real-client behaviour) and falls through to clearing the target when
no popup is open. This is outside the plan's literal scope; it was the only
way to give the loot ESC arm a production caller rather than adding a tenth
unreachable one.

### Red/green (measured)

RED, targeted at the load-bearing behaviour: the click producer was changed
to push `LootItemIntent{corpseId, idx}` — the row index instead of the
corpse slot, i.e. exactly the bug `getCorpseSlotFromDisplayIndex()` existed
to prevent. 27/29 pass, **2 fail**:
`ClickingARowPushesLootItemIntentCarryingTheCorpseSlot` and
`ScrolledClickStillCarriesTheRightCorpseSlot`. Reverted → **29/29**.
(At fable tip the file does not compile at all — `LootPanelState`,
`renderLootPopup`, `hitTestLootRow` and the appliers do not exist.)

### Verification

| step | result |
|---|---|
| `cmake --build build -j24` | PASS |
| `ctest -j6` | **3203/3204** — sole failure `AudioManagerTest.PreloadCommonSounds` (documented ALSA host quirk) |
| `scripts/build-gles2-x86.sh` | PASS |
| Visual gate | **PASS**, goldens untouched (`git status` clean of `goldens/`) |
| `scripts/build-arm-noble.sh` | PASS |
| `check_file_sizes.sh fable` | 9 ok, 2 warn, **0 fail** — both WARNs (`irrlicht_renderer.h`, `renderer_frame.cpp`) pre-existing; the new code went into a new TU |
| Live (eqemu, x86, `--eqe1-path`) | PASS — see below |

**Golden STOP not triggered.** The panel draws only while the game says a
corpse is open, so it cannot appear in a golden zone view. No re-bless.

### Live

Two sessions, both `EXIT=0`. Run 2 is the clean one:

- panel opens with the corpse's real name and seven real item names;
- `/loot Blood Axe` removes exactly that row and the list re-packs to six;
- `/lootall` drains the six and the panel closes on the server's event;
- re-open on a second corpse repopulates; `/lootclose` closes.

DB proof: Blood Axe equipped in slot 13, the loot-all haul stacked into
general slots 25/26/27, then everything restored — `db_restored.txt` is an
EMPTY diff against `db_before.txt`.

**Run 1 documents a trap, not a defect.** Every loot was refused with "You
may not loot an item while you have an item on your cursor": Testchar had a
stale `Wooden Fishing Tackle` on the cursor (`inventory.slot_id = 33`) left
by an earlier session. The panel was right — nothing was looted, so nothing
left the list. Worth knowing before the next live loot unit: **check
`slot_id = 33` before blaming the client.** (Also, T268's own note holds:
auto-loot is ON by default and empties the corpse before you can see it;
both tours set `/autoloot off` first.)

Not captured live: a mouse click on a row. `--script` drives slash commands
only and there is no click-injection path. `LootClickTest` covers it with
the real `StaticUIInput` and the real intent queue instead.

### Files

New: `src/client/graphics/ui/loot_panel.cpp`, `tests/test_loot_panel.cpp`.
Touched: `static_panels.h`, `static_ui_input.{h,cpp}`, `irrlicht_bridge.cpp`,
`irrlicht_renderer.h`, `renderer_frame.cpp`, `CMakeLists.txt`,
`tests/CMakeLists.txt`, `test_reachability_sweep.cpp`, the four existing
`StaticUIInput` test callers (new parameter), and
`include/client/graphics/ui/CLAUDE.md`.
