# Batch U — UI and Text Rendering Overhaul

Full design rationale and analysis: `docs/future/ui_fixes.md`

## Summary

Replace the current draggable window system (21+ WindowBase subclasses, 85-3000
draw calls/frame) with a static-layout UI using batched quads and a bitmap font
atlas. Target: 2-5 draw calls for the entire UI.

## Units

### U01: Bitmap Font Atlas + Batched Text Renderer

Replace Irrlicht's per-string `Font->draw()` with a single-draw-call batched text
renderer. ~53 font draw calls/frame → 1.

**New files**:
- `include/client/graphics/text_batch.h`
- `src/client/graphics/text_batch.cpp`

**Scope**:
- `BitmapFont` class: loads font texture atlas with per-glyph UV rects and advance widths
- `TextBatch` class: accumulates text quads, single `glDrawElements` flush
  - `addText(string, x, y, color, scale)` — screen-space text
  - `addText3D(string, worldPos, color, scale, camera)` — world-to-screen projected text
  - `flush()` — bind font texture, upload VBO, draw, clear
- Replace all `font->draw()` calls in UI, HUD, chat, entity name tags
- Replace `CTextSceneNode` name tags with `textBatch.addText3D()`, remove from scene graph

**Font atlas source**: Generate ASCII 32-126 at startup from Irrlicht's built-in
bitmap font, or ship a pre-baked PNG + JSON metrics file.

### U02: UI Atlas Texture

Pack all UI visual elements into a single atlas texture. Prerequisite for U03.

**New files**:
- `include/client/graphics/ui/ui_atlas.h`
- `src/client/graphics/ui/ui_atlas.cpp`

**Scope**:
- Define UI sprite sheet (256×256 or 512×256): slot backgrounds, button frames,
  bar fills (HP/mana/stamina/XP/casting), borders, checkbox states, tab backgrounds
- `UIAtlas` class with named sprite regions returning UV rects
- Generate at startup from constants (solid color rects, gradient bars) — no external assets

### U03: Static Layout UI Framework

Core framework replacing WindowBase + WindowManager with fixed-layout batched rendering.

**New files**:
- `include/client/graphics/ui/ui_renderer.h`
- `src/client/graphics/ui/ui_renderer.cpp`
- `include/client/graphics/ui/ui_layout.h`
- `src/client/graphics/ui/static_panels.h`
- `src/client/graphics/ui/static_panels.cpp`

**Scope**:
- `UILayout`: fixed screen regions (playerStatus, targetInfo, chatPanel, hotbar,
  spellGems, buffBar, castingBar, centerPopup, groupPanel, petPanel)
- `UIRenderer` class: `beginFrame()`, `drawRect()`, `drawBar()`, `drawSlot()`, `endFrame()`
  - Collects quads into VBO, sorts by texture, 1 draw call per texture
- Panel render functions (not class hierarchy):
  - `renderPlayerStatus()`, `renderTargetInfo()`, `renderHotbar()`, etc.
- Center popup state machine: `enum PopupType { None, Inventory, Spellbook, Vendor, ... }`
- Input hit-testing via fixed regions (no overlap resolution)
- Toggle with `/newui` command — old WindowManager remains functional during migration
- Resolution scaling via anchor points (works at 800×600 and higher)

### U04: Chat Panel

Implement chat as a static-layout panel with efficient text rendering.

**Scope**:
- Fixed position (bottom-left), fixed size
- Message buffer: ring buffer of pre-wrapped lines (wrap on add, not render)
- Scroll: up/down keys or mouse wheel, minimal scroll indicator
- Input field: single line, cursor, basic text editing
- Channel tabs: fixed tab bar (All, Combat, Group, etc.)
- Render: background quad (UI atlas) + text lines (TextBatch) + input field
- Preserve clickable item/spell links

### U05: Inventory and Item Slots

Implement inventory, bags, bank, loot, vendor, trade as center-screen popups.

**Scope**:
- Equipment slots (22) in fixed positions + general inventory (8)
- Bag popup: grid of slots (4/6/8/10), adjacent to inventory
- All slots use `drawSlot()`: background + item icon + stack count
- Item icon atlas: pack icons into atlas pages, one draw call for all slots
- Hover highlight, click pick up/put down, tooltip via TextBatch
- Character model view: separate render target, blit into popup area

### U06: Remaining Panels

Port all remaining HUD elements and popup windows.

**Scope** (each is a simple render function):
- Hotbar: 10 slots, bottom-center, icon + cooldown + key label
- Buff bar: row of icons + duration text
- Spell gems: 8 slots
- Player status: HP/mana/stamina bars + text
- Target info: target HP bar + name
- Casting bar: progress bar + spell name
- Group panel: 5 member names + HP bars
- Pet panel: pet name + HP bar + command buttons
- Spellbook popup: spell list with memorize action
- Skills popup: skill list with values

### U07: Remove Old UI System

Delete WindowBase, WindowManager, and all 40+ window class files.

**Scope**:
- Remove `window_base.h`, `window_manager.h`, all derived window headers
- Remove all `*_window.cpp` files
- Remove UISettings drag/position persistence
- Remove RTT caching infrastructure
- Update IrrlichtRenderer to use UIRenderer + TextBatch
- Clean up CMakeLists.txt

**Dependency**: All previous units must be complete and tested.

## Execution Order

```
U01 (Text batch)         — standalone
U02 (UI atlas)           — standalone, can parallel with U01
    ↓
U03 (Static layout)      — depends on U01 + U02
    ↓
U04 (Chat panel)         — depends on U03
U05 (Inventory/slots)    — depends on U03, can parallel with U04
U06 (Remaining panels)   — depends on U03, can parallel with U04+U05
    ↓
U07 (Remove old UI)      — depends on all above
```

## Notes

- Input lag fix (original Chunk 1) deferred — needs testing after D21b threading changes
- Layout uses anchor-point coordinates for resolution independence
- Center popup pattern: one popup at a time, ESC to close
- `/newui` toggle allows incremental migration alongside old WindowManager
