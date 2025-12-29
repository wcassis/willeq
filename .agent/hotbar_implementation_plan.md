# Hotbar Window Implementation Plan

## Overview

Implement a hotbar window that displays as a single row of buttons without a titlebar. The hotbar supports drag+drop from spellbar, inventory, and equipment using a Ctrl+click pickup system, with Ctrl+number activation.

**Scope Decisions:**
- **Skills**: Deferred for later - focus on spells and items first
- **Custom Emotes**: Right-click on empty hotbar button opens a text input dialog

## Architecture Summary

### New Files to Create
- `include/client/graphics/ui/hotbar_window.h` - HotbarWindow class definition
- `src/client/graphics/ui/hotbar_window.cpp` - HotbarWindow implementation
- `include/client/graphics/ui/hotbar_cursor.h` - Hotbar cursor state management

### Files to Modify
- `include/client/graphics/ui/window_manager.h` - Add hotbar window ownership
- `src/client/graphics/ui/window_manager.cpp` - Initialize and route input to hotbar
- `include/client/graphics/ui/ui_settings.h` - Add HotbarSettings struct
- `src/client/graphics/ui/ui_settings.cpp` - Load/save hotbar settings
- `src/client/graphics/ui/spell_gem_panel.cpp` - Add Ctrl+click pickup
- `src/client/graphics/ui/inventory_window.cpp` - Add Ctrl+click pickup
- `src/client/graphics/irrlicht_renderer.cpp` - Add Ctrl+1-10 keybinding capture
- `CMakeLists.txt` - Add new source files

---

## Step 1: Define Hotbar Data Structures

### HotbarButtonType Enum
```cpp
enum class HotbarButtonType {
    Empty,      // No action assigned
    Spell,      // Spell from spellbook (by spell_id)
    Item,       // Item use (by item_id)
    Emote       // Custom emote string (right-click to set)
};
```

### HotbarButton Struct
```cpp
struct HotbarButton {
    HotbarButtonType type = HotbarButtonType::Empty;
    uint32_t id = 0;              // spell_id, item_id, or skill_id
    std::string emoteText;        // For emote type only
    uint32_t iconId = 0;          // Cached icon ID for display
    irr::core::recti bounds;      // Button bounds (relative to content area)
    bool hovered = false;
    bool pressed = false;
};
```

---

## Step 2: Create HotbarCursor System

### Purpose
Manage a separate cursor state for hotbar drag operations, distinct from the existing inventory cursor.

### File: `include/client/graphics/ui/hotbar_cursor.h`

```cpp
class HotbarCursor {
public:
    bool hasItem() const;
    void setItem(HotbarButtonType type, uint32_t id, const std::string& emoteText, uint32_t iconId);
    void clear();

    HotbarButtonType getType() const;
    uint32_t getId() const;
    const std::string& getEmoteText() const;
    uint32_t getIconId() const;

    // For rendering cursor icon
    void render(IVideoDriver* driver, IGUIEnvironment* gui,
                ItemIconLoader* iconLoader, int mouseX, int mouseY);

private:
    bool hasItem_ = false;
    HotbarButtonType type_ = HotbarButtonType::Empty;
    uint32_t id_ = 0;
    std::string emoteText_;
    uint32_t iconId_ = 0;
};
```

### Integration Points
- WindowManager owns the HotbarCursor instance
- Rendered at mouse position when active
- Cleared when clicking outside hotbar

---

## Step 3: Create HotbarWindow Class

### File: `include/client/graphics/ui/hotbar_window.h`

```cpp
class HotbarWindow : public WindowBase {
public:
    static constexpr int MAX_BUTTONS = 10;

    HotbarWindow();

    // Configuration
    void setButtonCount(int count);  // 1-10 buttons
    int getButtonCount() const;

    // Button management
    void setButton(int index, HotbarButtonType type, uint32_t id,
                   const std::string& emoteText = "");
    void clearButton(int index);
    const HotbarButton& getButton(int index) const;
    void swapButtons(int indexA, int indexB);

    // Activation
    void activateButton(int index);

    // Callbacks
    using ActivateCallback = std::function<void(int index, const HotbarButton&)>;
    using PickupCallback = std::function<void(int index, const HotbarButton&)>;
    void setActivateCallback(ActivateCallback cb);
    void setPickupCallback(PickupCallback cb);

    // Icon loader reference
    void setIconLoader(ItemIconLoader* loader);

    // WindowBase overrides
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

protected:
    void renderContent(IVideoDriver* driver, IGUIEnvironment* gui) override;

private:
    void initializeLayout();
    void updateWindowSize();
    int getButtonAtPosition(int relX, int relY) const;  // Returns -1 or 0-9
    void drawButton(IVideoDriver* driver, IGUIEnvironment* gui,
                    const HotbarButton& button, int index);

    std::array<HotbarButton, MAX_BUTTONS> buttons_;
    int buttonCount_ = 10;
    int buttonSize_ = 32;
    int buttonSpacing_ = 2;
    int padding_ = 4;

    ItemIconLoader* iconLoader_ = nullptr;
    ActivateCallback activateCallback_;
    PickupCallback pickupCallback_;
};
```

### Key Implementation Details

1. **No Titlebar**: Call `setShowTitleBar(false)` in constructor
2. **Dynamic Width**: `updateWindowSize()` calculates width based on button count
3. **Button Rendering**: Draw icon from iconLoader, number label in corner

---

## Step 4: Add UISettings for Hotbar

### File: `include/client/graphics/ui/ui_settings.h`

Add new struct:
```cpp
struct HotbarSettings {
    WindowSettings window;
    int buttonSize = 32;
    int buttonSpacing = 2;
    int padding = 4;
    int buttonCount = 10;

    // Per-button saved data (10 buttons max)
    struct ButtonData {
        int type = 0;           // HotbarButtonType as int
        uint32_t id = 0;
        std::string emoteText;
    };
    std::array<ButtonData, 10> buttons;
};
```

Add accessor in UISettings class:
```cpp
const HotbarSettings& hotbar() const { return hotbarSettings_; }
HotbarSettings& hotbar() { return hotbarSettings_; }
```

---

## Step 5: Integrate with WindowManager

### Changes to `window_manager.h`

```cpp
// Add includes
#include "hotbar_window.h"
#include "hotbar_cursor.h"

// Add members
private:
    std::unique_ptr<HotbarWindow> hotbarWindow_;
    HotbarCursor hotbarCursor_;

public:
    HotbarWindow* getHotbarWindow();
    void toggleHotbar();

    // Hotbar cursor operations
    bool hasHotbarCursor() const;
    void setHotbarCursor(HotbarButtonType type, uint32_t id,
                         const std::string& emoteText, uint32_t iconId);
    void clearHotbarCursor();
    const HotbarCursor& getHotbarCursor() const;
```

### Changes to `window_manager.cpp`

1. **Initialize in init()**:
   ```cpp
   hotbarWindow_ = std::make_unique<HotbarWindow>();
   hotbarWindow_->setIconLoader(iconLoader_);
   hotbarWindow_->setActivateCallback([this](int idx, const HotbarButton& btn) {
       handleHotbarActivation(idx, btn);
   });
   hotbarWindow_->setPickupCallback([this](int idx, const HotbarButton& btn) {
       handleHotbarPickup(idx, btn);
   });
   loadHotbarFromSettings();
   ```

2. **Route input in handleMouseDown()**:
   - Check hotbar window first when hotbar cursor is active
   - Clear cursor if clicking outside hotbar with cursor active

3. **Render cursor** in render():
   ```cpp
   if (hotbarCursor_.hasItem()) {
       hotbarCursor_.render(driver, gui, iconLoader_, mouseX_, mouseY_);
   }
   ```

---

## Step 6: Add Ctrl+Click to Source Windows

### SpellGemPanel Changes (`spell_gem_panel.cpp`)

In `handleMouseDown()`:
```cpp
if (leftButton && ctrl && !shift) {
    int gemIndex = getGemAtPosition(x, y);
    if (gemIndex >= 0) {
        uint32_t spellId = spellMgr_->getMemorizedSpell(gemIndex);
        if (spellId != SPELL_UNKNOWN) {
            const SpellData* spell = spellMgr_->getSpell(spellId);
            if (spell && hotbarPickupCallback_) {
                hotbarPickupCallback_(HotbarButtonType::Spell, spellId,
                                     "", spell->gem_icon);
            }
        }
        return true;
    }
}
```

Add callback:
```cpp
using HotbarPickupCallback = std::function<void(HotbarButtonType, uint32_t,
                                                 const std::string&, uint32_t)>;
void setHotbarPickupCallback(HotbarPickupCallback cb);
```

### InventoryWindow Changes (`inventory_window.cpp`)

Similar pattern for Ctrl+click on equipment slots and inventory items:
```cpp
if (leftButton && ctrl && !shift) {
    int16_t slotId = getSlotAtPosition(relX, relY);
    if (slotId != SLOT_INVALID) {
        const ItemInstance* item = manager_->getItem(slotId);
        if (item && hotbarPickupCallback_) {
            hotbarPickupCallback_(HotbarButtonType::Item, item->itemId,
                                 "", item->icon);
        }
        return true;
    }
}
```

---

## Step 7: Add Ctrl+1-10 Keybindings

### IrrlichtRenderer Changes (`irrlicht_renderer.cpp`)

In `RendererEventReceiver::OnEvent()`, add after line ~115:
```cpp
// Hotbar shortcuts (Ctrl+1-0 keys)
if (event.KeyInput.Control && !event.KeyInput.Shift) {
    if (event.KeyInput.Key >= irr::KEY_KEY_1 && event.KeyInput.Key <= irr::KEY_KEY_9) {
        hotbarActivationRequest_ = static_cast<int8_t>(event.KeyInput.Key - irr::KEY_KEY_1);
    } else if (event.KeyInput.Key == irr::KEY_KEY_0) {
        hotbarActivationRequest_ = 9;  // 10th button
    }
}
```

Add member variable:
```cpp
int8_t hotbarActivationRequest_ = -1;
int8_t getHotbarActivationRequest() {
    int8_t val = hotbarActivationRequest_;
    hotbarActivationRequest_ = -1;
    return val;
}
```

In `processFrame()`, add handler:
```cpp
int8_t hotbarRequest = eventReceiver_->getHotbarActivationRequest();
if (hotbarRequest >= 0 && rendererMode_ == RendererMode::Player) {
    if (!windowManager_ || !windowManager_->isChatInputFocused()) {
        if (windowManager_ && windowManager_->getHotbarWindow()) {
            windowManager_->getHotbarWindow()->activateButton(hotbarRequest);
        }
    }
}
```

---

## Step 8: Implement Button Activation Logic

### In WindowManager or EverQuest class

```cpp
void WindowManager::handleHotbarActivation(int index, const HotbarButton& button) {
    switch (button.type) {
        case HotbarButtonType::Spell:
            if (spellCastCallback_) {
                spellCastCallback_(button.id);
            }
            break;

        case HotbarButtonType::Item:
            if (itemUseCallback_) {
                itemUseCallback_(button.id);
            }
            break;

        case HotbarButtonType::Emote:
            if (emoteCallback_) {
                emoteCallback_(button.emoteText);
            }
            break;

        default:
            break;
    }
}
```

---

## Step 8b: Implement Right-Click Emote Dialog

### In HotbarWindow::handleMouseDown()

```cpp
if (!leftButton && !ctrl) {  // Right-click
    int buttonIndex = getButtonAtPosition(relX, relY);
    if (buttonIndex >= 0) {
        if (buttons_[buttonIndex].type == HotbarButtonType::Empty) {
            // Open emote input dialog
            if (emoteDialogCallback_) {
                emoteDialogCallback_(buttonIndex);
            }
        }
        return true;
    }
}
```

### Emote Dialog Implementation

Use existing InputField pattern from ChatWindow:
- Modal text input dialog centered on screen
- OK/Cancel buttons
- On OK: call `setButton(index, HotbarButtonType::Emote, 0, inputText)`
- Uses a speech bubble or similar icon for emotes (icon ID TBD)

---

## Step 9: Handle Cursor Operations

### Hotbar Button Click with Cursor Active

In `HotbarWindow::handleMouseDown()`:
```cpp
if (leftButton && !ctrl && hasHotbarCursor()) {
    int buttonIndex = getButtonAtPosition(relX, relY);
    if (buttonIndex >= 0) {
        if (buttons_[buttonIndex].type != HotbarButtonType::Empty) {
            // Swap: put current button on cursor, place cursor on button
            HotbarButton oldButton = buttons_[buttonIndex];
            // Place cursor content
            setButtonFromCursor(buttonIndex);
            // Put old content on cursor
            setCursorFromButton(oldButton);
        } else {
            // Place cursor content on empty button
            setButtonFromCursor(buttonIndex);
            clearHotbarCursor();
        }
        return true;
    }
}
```

### Click Outside Hotbar

In `WindowManager::handleMouseDown()`:
```cpp
// If hotbar cursor active and click is outside all hotbar-related windows
if (hotbarCursor_.hasItem()) {
    bool clickedHotbar = hotbarWindow_ && hotbarWindow_->containsPoint(x, y);
    bool clickedSource = /* check spell panel, inventory, etc. */;

    if (!clickedHotbar && !clickedSource) {
        hotbarCursor_.clear();
        return true;  // Consume the click
    }
}
```

---

## Step 10: CMake Integration

### CMakeLists.txt

Add to UI sources list:
```cmake
src/client/graphics/ui/hotbar_window.cpp
```

---

## Implementation Order

1. **HotbarButton struct and HotbarButtonType enum** - Data structures
2. **HotbarCursor class** - Cursor state management
3. **HotbarSettings in UISettings** - Configuration persistence
4. **HotbarWindow class** - Window rendering and basic interaction
5. **WindowManager integration** - Ownership, routing, cursor management
6. **Ctrl+click on SpellGemPanel** - Pickup from spell gems
7. **Ctrl+click on InventoryWindow** - Pickup from items/equipment
8. **Ctrl+1-0 keybindings** - Keyboard activation (Ctrl+1-9 and Ctrl+0 for 10th)
9. **Button activation callbacks** - Wire up spell cast and item use
10. **Right-click emote dialog** - Text input for custom emotes
11. **Settings persistence** - Save/load hotbar button configuration

---

## Key Files Reference

| File | Purpose |
|------|---------|
| `include/client/graphics/ui/window_base.h` | Base class patterns |
| `include/client/graphics/ui/group_window.h` | Example of titlebar-less window |
| `include/client/graphics/ui/spell_gem_panel.h` | Spell display patterns |
| `include/client/graphics/ui/item_icon_loader.h` | Icon loading (< 500 = spells, >= 500 = items) |
| `src/client/graphics/irrlicht_renderer.cpp` | Keybinding capture (lines 115-150) |
| `src/client/graphics/ui/window_manager.cpp` | Input routing (lines 582-667) |

---

## Notes

- Skills integration deferred for later when skill system is more defined
- Multiple hotbars could be added later by extending to HotbarWindow array
- Hotbar position/size saved via existing UISettings persistence
- Right-click on occupied button could show context menu (Edit Emote / Clear) - implement if needed
- Emotes use a placeholder icon (speech bubble or similar from spell icons)
