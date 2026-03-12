# UI and Text Rendering Overhaul

## Problem Statement

### UI Performance

The current UI system has 21+ window types derived from `WindowBase`, each making individual `draw2DRectangle`, `draw2DImage`, and `font->draw()` calls per element. Draw call counts scale with open windows:

| Scenario | Draw Calls/Frame |
|----------|-----------------|
| Minimal HUD (chat + hotbar + gems + status) | ~85 |
| With inventory | ~86 (RTT cached) |
| With inventory + 1 bag | ~150-180 (bags NOT cached) |
| All windows + 8 bags + tooltips | **2,000-3,000+** |

Hotbar: 40-60 uncached draw calls every frame. Each bag window: 60-90 uncached draw calls. Buff window: 15-80 uncached calls. These all go through Irrlicht's `IVideoDriver` virtual dispatch, with per-call state management overhead.

Some windows have RTT (render-to-texture) caching: InventoryWindow, ChatWindow, SpellGemPanel. When cached, they cost 1 draw call per frame. But HotbarWindow, BuffWindow, BagWindow, LootWindow, VendorWindow, TradeWindow, and all tooltips render fully every frame.

### Text Rendering

Irrlicht's text path: `Font->draw()` → `CGUISpriteBank::draw2DSpriteBatch()` → `driver->draw2DImageBatch()`. Each `Font->draw()` call:
1. Computes string dimensions by iterating all characters (`getDimension()`)
2. Looks up each character via `CharacterMap.find(c)` (hashmap)
3. Groups sprites by texture
4. Issues one `draw2DImageBatch` per texture

With 40 entity name tags at ~10 characters each, that's 40 separate draw calls, 40 texture binds, 40 shader state sets — even though every name tag uses the same font texture. No cross-string batching.

Chat window adds ~10 more `Font->draw()` calls per frame. UI labels, tooltips, and debug text add more. Total: ~53+ font draw calls per frame in a busy scene.

### Input Lag Despite High FPS

Input lag is caused by two issues in `src/client/application.cpp`, not by Irrlicht:

1. **Unconditional 1ms sleep** (~line 462): `std::this_thread::sleep_for(1ms)` executes every main loop iteration regardless of pending input.
2. **Input gated to frame timer** (~line 438): `if (deltaTime >= 1.0f / 60.0f)` — input processing waits up to 16ms for the frame timing window.

Combined worst case: 17-20ms input latency before the code sees a keypress. This is independent of render FPS or Irrlicht.

### UI Complexity

The current UI mirrors original EQ: every window is draggable, resizable, has z-order management, overlap handling, and per-window lock/unlock state. `WindowBase` implements drag logic, title bar rendering, close callbacks, hover detection, unlock highlighting, and settings persistence. `WindowManager` maintains 28+ window render entries with individual timing.

This complexity serves no gameplay purpose on a constrained display (800x600 Orange Pi) where windows can't meaningfully overlap without becoming unusable.

## Target Architecture: Static Layout UI

Replace the current draggable window system with a fixed-layout UI inspired by vanilla WoW. Windows have predetermined screen positions. Persistent HUD elements (HP/mana bars, hotbar, chat, buffs, spell gems) are always visible at fixed positions. Content windows (inventory, spellbook, vendor, etc.) open as center-screen popups, one at a time or in designated slots (e.g., left panel + right panel).

### Layout

```
┌──────────────────────────────────────────────────┐
│ [Player Status]                    [Target Info]  │
│  HP ████████░░                      HP ████░░░░░  │
│  Mana ██████░░░                                   │
│  Stam ████████░                                   │
│                                                   │
│               ┌──────────────┐                    │
│               │              │                    │
│               │ Center Popup │                    │
│               │ (Inventory,  │                    │
│               │  Spellbook,  │                    │
│               │  Vendor...)  │                    │
│               │              │                    │
│               └──────────────┘                    │
│                                                   │
│ [Buffs: icon icon icon icon...]                   │
│ ┌────────────────────┐                            │
│ │ Chat               │        [Casting Bar]       │
│ │ ...                │                            │
│ │ ...                │                            │
│ └────────────────────┘                            │
│ [Hotbar: 1 2 3 4 5 6 7 8 9 0]    [Spell Gems]   │
└──────────────────────────────────────────────────┘
```

### Rendering Strategy: UI Atlas + Batched Quads

**Single UI atlas texture**: Pre-pack all UI sprites (slot backgrounds, button frames, bar fills, border pieces, buff icons, item icons) into one or a few atlas textures at load time.

**Per-frame rendering**:
1. Collect all UI quads into a single vertex buffer (position, UV, color per vertex)
2. Sort by texture (UI atlas, font atlas, item icon atlas)
3. One `glDrawElements` call per texture — typically 2-5 total for the entire UI
4. Text: all text quads go into one vertex buffer with the font texture, one draw call

Compare: 85-3,000 draw calls today → **2-5 draw calls** with batched atlas rendering.

### What This Eliminates

- All drag/resize/z-order logic in `WindowBase` (~200 lines per window)
- `WindowManager` render ordering and overlap management
- Per-window RTT caching (unnecessary when the whole UI is a few batched draw calls)
- Per-element `draw2DRectangle` / `draw2DImage` / `draw2DRectangleOutline` virtual dispatch
- Per-window dirty tracking and cache invalidation

## Work Chunks

### Chunk 1: Fix Input Lag (Quick Win)

**Goal**: Eliminate the 17-20ms input latency caused by the main loop sleep and frame gating.

**Scope**:
- `src/client/application.cpp` ~line 462: Make the 1ms sleep conditional on input queue depth. If input events are pending, skip the sleep.
- `src/client/application.cpp` ~line 438: Decouple input polling from the 60 FPS frame timer. Poll input every loop iteration (or at a higher frequency gate like 500Hz). Keep game state updates and rendering at 60Hz.
- Verify with manual testing: key response should feel immediate at any FPS.

**Risk**: Low. The sleep exists to prevent CPU spinning; making it conditional preserves that while eliminating input delay. Input polling is read-only (no side effects beyond queuing events).

**Files**: `src/client/application.cpp`

### Chunk 2: Bitmap Font Atlas + Batched Text Renderer

**Goal**: Replace Irrlicht's per-string `Font->draw()` with a single-draw-call batched text renderer.

**Scope**:
- Create `BitmapFont` class: loads a font texture atlas (pre-baked PNG or generated at startup from TTF/bitmap) with per-glyph UV rects and advance widths
- Create `TextBatch` class: accumulates text quads across the frame
  - `addText(string, screenX, screenY, color, scale)` — appends quads to a vertex buffer
  - `addText3D(string, worldPos, color, scale, camera)` — projects world position to screen, then appends quads (for name tags)
  - `flush()` — binds font texture, uploads vertex buffer, single `glDrawElements`, clears buffer
- Replace all `font->draw()` calls in UI windows, HUD, chat, name tags with `textBatch.addText()`
- Replace `CTextSceneNode` for entity name tags: compute screen position from world position + camera projection, add to text batch. Remove name tag scene nodes from the scene graph entirely.

**What this replaces**:
- `CGUIFont::draw()` — per-string character iteration, sprite bank lookup, dimension computation
- `CGUISpriteBank::draw2DSpriteBatch()` — per-texture sprite grouping
- `CTextSceneNode::render()` — per-entity scene node rendering for name tags
- All `ITextSceneNode` creation/destruction in `entity_renderer.cpp`

**Performance impact**: ~53 font draw calls/frame → 1. All name tags, chat lines, UI labels, tooltips batched into a single vertex buffer and drawn in one call.

**Key files**:
- New: `include/client/graphics/text_batch.h`, `src/client/graphics/text_batch.cpp`
- Modified: `src/client/graphics/entity_renderer.cpp` (remove `addTextSceneNode`, add `textBatch.addText3D`)
- Modified: `src/client/graphics/ui/chat_window.cpp` (replace `font->draw` with `textBatch.addText`)
- Modified: all UI window files that call `font->draw()` or `drawText()`
- Modified: `src/client/graphics/irrlicht_renderer.cpp` (HUD text, FPS display, debug overlays)

**Font atlas options**:
- Simplest: ship a pre-baked PNG atlas + JSON metrics file (glyph rects, advances). Generate offline with a tool like BMFont or msdf-atlas-gen.
- Runtime: render ASCII 32-126 into a texture at startup using stb_truetype.h (header-only, ~4K lines, public domain). This avoids shipping font atlas assets.
- EQ original fonts: extract from Titanium client files if available, or use a bitmap font that matches the aesthetic.

### Chunk 3: UI Atlas Texture

**Goal**: Pack all UI visual elements (slot backgrounds, button frames, bar fills, borders, icons) into a single atlas texture.

**Scope**:
- Define UI sprite sheet: slot background, slot border (normal/hover/selected), button frame (normal/pressed/disabled), bar fill (HP green, mana blue, stamina yellow, XP purple, casting blue), scroll indicators, checkbox states, tab backgrounds
- Simple approach: 256×256 or 512×256 atlas with rectangular regions, hand-placed
- Create `UIAtlas` class with named sprite regions: `UIAtlas::slotBackground()` returns UV rect
- Generate at startup from constants (solid color rects, gradient bars) — no external assets needed for basic UI chrome

**This is a prerequisite for Chunk 4** (batched UI rendering) but can be developed and tested independently.

**Key files**: New `include/client/graphics/ui/ui_atlas.h`, `src/client/graphics/ui/ui_atlas.cpp`

### Chunk 4: Static Layout UI Framework

**Goal**: Replace `WindowBase` + `WindowManager` with a fixed-layout UI system that renders via batched quads.

**Scope**:
- Define `UILayout` with fixed screen regions per element type:
  - `playerStatus`: top-left (HP/mana/stamina bars + level/name)
  - `targetInfo`: top-right or top-center (target HP bar + name)
  - `chatPanel`: bottom-left (message history + input field)
  - `hotbar`: bottom-center (10 slots)
  - `spellGems`: bottom-right (8 gem slots)
  - `buffBar`: above chat or below player status (buff icons in a row)
  - `castingBar`: center-bottom or above hotbar
  - `centerPopup`: center screen (inventory, spellbook, vendor, trade, loot, bank, skills, options)
  - `groupPanel`: left side below player status (group member HP bars)
  - `petPanel`: below group panel
- `UIRenderer` class:
  - `beginFrame()` — clear quad buffer
  - `drawRect(region, uvRect, color)` — append quad to buffer (uses UI atlas UVs)
  - `drawBar(region, fillPercent, fillColor, bgColor)` — health/mana bar helper
  - `drawSlot(region, itemTextureId, stackCount)` — item slot with icon + text overlay
  - `endFrame()` — sort quads by texture, upload, draw (1 call per texture)
- Each UI panel is a simple function, not a class hierarchy:
  - `renderPlayerStatus(uiRenderer, textBatch, playerState)` — draws HP/mana/stam bars + text
  - `renderHotbar(uiRenderer, textBatch, hotbarState)` — draws 10 slots with icons + cooldowns + key labels
  - `renderChat(uiRenderer, textBatch, chatState)` — draws message lines + input field
  - etc.
- Center popup state machine: `enum PopupType { None, Inventory, Spellbook, Vendor, ... }`. Only one popup at a time (or left+right split for vendor+inventory). ESC or dedicated key closes popup.
- Input handling: fixed regions mean hit-testing is trivial (is mouse in hotbar rect? which slot index?). No overlap resolution needed.

**Migration path**: Implement new UI panels one at a time alongside existing `WindowManager`. Toggle with a `/newui` command. Once all panels are ported, remove `WindowBase`/`WindowManager` and all 40+ window classes.

**Key files**:
- New: `include/client/graphics/ui/ui_renderer.h`, `src/client/graphics/ui/ui_renderer.cpp`
- New: `include/client/graphics/ui/ui_layout.h` (screen region definitions)
- New: `src/client/graphics/ui/static_panels.cpp` (all panel render functions)
- Eventually remove: `include/client/graphics/ui/window_base.h`, `include/client/graphics/ui/window_manager.h`, all 40+ window `.h/.cpp` files

### Chunk 5: Chat Panel

**Goal**: Implement the chat panel as a static-layout element with efficient text rendering.

**Scope**:
- Fixed position (bottom-left), fixed size
- Message buffer: ring buffer of wrapped lines (pre-wrap on message add, not on render)
- Scroll: up/down keys or mouse wheel, no scrollbar widget needed (or minimal: just an indicator of scroll position)
- Input field: single line at bottom, cursor blinking, basic text editing (type, backspace, enter to send)
- Channel tabs: fixed tab bar above message area (All, Combat, Group, etc.)
- Render: background quad (UI atlas) + text lines via `TextBatch` + input field text + cursor quad
- Link detection: preserve clickable links for item/spell links (EQ chat protocol)

**Current chat window complexity to eliminate**:
- `wrapText()` called per message on every dirty check — O(n²) with repeated `find()` calls. Replace with pre-wrapping on message arrival.
- Dual RTT caching (`messageAreaRT_` + `messageTextRT_`) with 500ms throttle. Replace with direct text batch rendering (1 draw call for all chat text).
- Scrollbar widget rendering (3 `draw2DRectangleOutline` + resize grip). Replace with minimal scroll indicator or nothing.
- Tab switching forces full re-render. With text batch, re-render is trivial (iterate visible messages, add to batch).

**Key files**:
- Current: `src/client/graphics/ui/chat_window.cpp` (856 lines)
- New: integrated into `static_panels.cpp` chat panel function

### Chunk 6: Inventory and Item Slots

**Goal**: Implement inventory, bags, bank, loot, vendor, and trade as center-screen popups with batched slot rendering.

**Scope**:
- `renderInventoryPopup(uiRenderer, textBatch, inventoryState)`:
  - Equipment slots (22) in fixed positions (matching EQ paper doll layout)
  - General inventory slots (8) in a row below
  - Stat summary text on the side
  - Character model view (separate render target, blit into popup area)
- `renderBagPopup(uiRenderer, textBatch, bagState)`:
  - Grid of slots (variable size per bag: 4/6/8/10)
  - Opens adjacent to inventory, not overlapping
- All item slots use the same `drawSlot()` helper: background quad + item icon quad + optional stack count text
- Hover: highlight slot border color. Click: pick up / put down item (cursor item state).
- Tooltip: render near cursor position using `TextBatch` for stat text. One tooltip visible at a time.

**Item icon rendering**: Item icons are textures loaded from EQ client files. Options:
- Individual textures (current approach): each icon is a separate texture, each slot is a separate draw call
- Icon atlas: pack all item icons into atlas pages at load time. All slots share one texture, one draw call for all slots. This is the preferred approach.

**Per-slot draw calls**: Current = 6-9 per slot. New (with atlas) = 0 additional draw calls (all quads batched into the frame's UI vertex buffer). For 30 inventory slots + 10 bag slots: **240-360 draw calls → 0 additional** (folded into the 2-5 total UI draw calls).

**Key files**:
- Current: `src/client/graphics/ui/inventory_window.cpp`, `src/client/graphics/ui/bag_window.cpp`, `src/client/graphics/ui/item_slot.cpp`, `src/client/graphics/ui/item_tooltip.cpp`, `src/client/graphics/ui/loot_window.cpp`, `src/client/graphics/ui/vendor_window.cpp`, `src/client/graphics/ui/trade_window.cpp`, `src/client/graphics/ui/bank_window.cpp`
- New: integrated into `static_panels.cpp` popup functions

### Chunk 7: Remaining Panels (Hotbar, Buffs, Spells, Status, Group, Pet, Casting)

**Goal**: Port all remaining HUD elements and popup windows to the static layout.

**Scope**:
- Hotbar: 10 fixed slots, bottom-center. Icon + cooldown overlay (darkened sweep or countdown number) + key label. Currently 40-60 uncached draw calls/frame → 0 additional (batched).
- Buff bar: row of small icons with duration text. Currently 15-80 uncached draw calls → 0 additional.
- Spell gems: 8 gem slots, right side. Already RTT cached (1 call), but simplify to batched quads.
- Player status: HP/mana/stamina bars + numeric text. Simple bars + text batch.
- Target info: same as player status but for current target.
- Casting bar: progress bar + spell name text. Only visible during casting.
- Group panel: list of 5 member names + HP bars.
- Pet panel: pet name + HP bar + command buttons.
- Spellbook popup: list of known spells with memorize action.
- Skills popup: list of skills with values.
- Options popup: settings toggles.

Each panel is a simple render function. No class hierarchy, no inheritance, no virtual dispatch.

**Key files**:
- Current: `src/client/graphics/ui/hotbar_window.cpp`, `src/client/graphics/ui/buff_window.cpp`, `src/client/graphics/ui/spell_gem_panel.cpp`, `src/client/graphics/ui/player_status_window.cpp`, `src/client/graphics/ui/group_window.cpp`, `src/client/graphics/ui/pet_window.cpp`, `src/client/graphics/ui/spellbook_window.cpp`, `src/client/graphics/ui/skills_window.cpp`, `src/client/graphics/ui/options_window.cpp`
- New: all integrated into `static_panels.cpp`

### Chunk 8: Remove Old UI System

**Goal**: Delete `WindowBase`, `WindowManager`, and all 40+ window class files once all panels are ported.

**Scope**:
- Remove `include/client/graphics/ui/window_base.h` and all derived window headers
- Remove `include/client/graphics/ui/window_manager.h`
- Remove all `src/client/graphics/ui/*_window.cpp` files
- Remove `UISettings` drag/position persistence (positions are now constants)
- Remove RTT caching infrastructure (no longer needed)
- Update `IrrlichtRenderer` to use the new `UIRenderer` + `TextBatch` instead of `WindowManager`
- Clean up CMakeLists.txt to remove old UI source files

**Dependency**: All previous UI chunks must be complete and tested.

**Key files**: ~50+ files removed from `include/client/graphics/ui/` and `src/client/graphics/ui/`

## Execution Order

```
Chunk 1 (Input lag fix)      — immediate, standalone, hours of work
    ↓
Chunk 2 (Text batch)         — standalone, enables name tag + chat improvements
    ↓
Chunk 3 (UI atlas)           — standalone, can parallel with Chunk 2
    ↓
Chunk 4 (Static layout)      — depends on Chunks 2+3, core framework
    ↓
Chunk 5 (Chat panel)         — depends on Chunk 4
Chunk 6 (Inventory/slots)    — depends on Chunk 4, can parallel with Chunk 5
Chunk 7 (Remaining panels)   — depends on Chunk 4, can parallel with 5+6
    ↓
Chunk 8 (Remove old UI)      — depends on all above
```

Chunks 5, 6, and 7 can be done in any order and in parallel. Each adds panels to the new system while the old system remains functional for un-ported windows.

## Relationship to Custom Renderer Plan

The UI/text work is **independent of and can precede** the Irrlicht removal plan in `docs/future/custom_renderer.md`. The batched text renderer and static UI can work with the current Irrlicht-based renderer (using `glDrawElements` directly for UI quads while Irrlicht handles 3D scene rendering).

However, Chunk 2 (text batch) directly enables **Custom Renderer Chunk 5** (entity rendering without scene nodes) by replacing `CTextSceneNode` name tags with batched text. And the UI atlas/batched quad approach in Chunks 3-4 naturally feeds into the custom renderer's direct render list architecture.

Recommended sequence: do UI Chunks 1-4 first, then start Custom Renderer Chunks 1-4, then finish both plans in parallel.

## Notes

- The static layout must handle resolution scaling. Define layout in normalized coordinates (0-1) or relative to anchor points (top-left, bottom-center, etc.) so it works at 800×600 (Orange Pi) and higher resolutions.
- Consider gamepad/controller input from the start. Fixed layout with slot-based navigation is much more gamepad-friendly than draggable windows.
- The center popup pattern (one popup at a time, ESC to close) matches how EQ was actually played — most players rarely had more than one window open. The draggable multi-window system was rarely useful and often frustrating.
- Chat input needs keyboard focus management: when the chat input is active, key presses go to chat, not to hotbar/movement. This is simpler with a static layout (explicit focus state) than with the current z-order-based system.
