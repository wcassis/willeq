#pragma once

#include <irrlicht.h>
#include <array>
#include <functional>
#include <string>
#include "client/spell/spell_constants.h"
#include "client/graphics/ui/ui_settings.h"
#include "client/graphics/ui/hotbar_window.h"  // For HotbarButtonType

namespace EQ {
class SpellManager;
}

namespace eqt {
namespace ui {

class ItemIconLoader;

// Callback types for gem interactions
using GemCastCallback = std::function<void(uint8_t gem_slot)>;
using GemForgetCallback = std::function<void(uint8_t gem_slot)>;
using GemHoverCallback = std::function<void(uint8_t gem_slot, uint32_t spell_id, int mouseX, int mouseY)>;
using GemHoverEndCallback = std::function<void()>;
using SpellbookButtonCallback = std::function<void()>;
using MemorizeSpellCallback = std::function<void(uint32_t spell_id, uint8_t gem_slot)>;
using GetSpellCursorCallback = std::function<uint32_t()>;
using ClearSpellCursorCallback = std::function<void()>;

// Callback for Ctrl+click pickup to hotbar cursor
using SpellHotbarPickupCallback = std::function<void(HotbarButtonType type, uint32_t id,
                                                      const std::string& emoteText, uint32_t iconId)>;

// Individual gem slot layout data
struct GemSlotLayout {
    irr::core::recti bounds;       // Full gem bounds
    irr::core::recti iconBounds;   // Icon area within gem
    bool isHovered = false;
};

class SpellGemPanel {
public:
    SpellGemPanel(EQ::SpellManager* spellMgr, ItemIconLoader* iconLoader);
    ~SpellGemPanel() = default;

    // Position (typically right side of screen, vertical layout)
    void setPosition(int x, int y);
    int getX() const { return position_.X; }
    int getY() const { return position_.Y; }
    irr::core::recti getBounds() const;

    // Rendering
    void render(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false);
    bool handleMouseUp(int x, int y, bool leftButton);
    bool handleMouseMove(int x, int y);
    bool handleRightClick(int x, int y);

    // Keyboard casting (1-8 keys) - returns true if handled
    bool handleKeyPress(int keyCode);

    // Direct gem casting
    void castGem(uint8_t gemSlot);

    // Visibility
    void show() { visible_ = true; }
    void hide() { visible_ = false; }
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    // Hit testing for drag-drop
    bool containsPoint(int x, int y) const;
    int getGemAtPosition(int x, int y) const;

    // Callbacks
    void setGemCastCallback(GemCastCallback cb) { castCallback_ = std::move(cb); }
    void setGemForgetCallback(GemForgetCallback cb) { forgetCallback_ = std::move(cb); }
    void setGemHoverCallback(GemHoverCallback cb) { hoverCallback_ = std::move(cb); }
    void setGemHoverEndCallback(GemHoverEndCallback cb) { hoverEndCallback_ = std::move(cb); }
    void setSpellbookCallback(SpellbookButtonCallback cb) { spellbookCallback_ = std::move(cb); }
    void setHotbarPickupCallback(SpellHotbarPickupCallback cb) { hotbarPickupCallback_ = std::move(cb); }
    void setMemorizeCallback(MemorizeSpellCallback cb) { memorizeCallback_ = std::move(cb); }
    void setGetSpellCursorCallback(GetSpellCursorCallback cb) { getSpellCursorCallback_ = std::move(cb); }
    void setClearSpellCursorCallback(ClearSpellCursorCallback cb) { clearSpellCursorCallback_ = std::move(cb); }

private:
    void initializeLayout();
    void drawGem(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui,
                 uint8_t slot, const GemSlotLayout& gem);
    void drawCooldownOverlay(irr::video::IVideoDriver* driver,
                             const GemSlotLayout& gem, float progress);
    void drawMemorizeProgress(irr::video::IVideoDriver* driver,
                              const GemSlotLayout& gem, float progress);
    void drawCastingHighlight(irr::video::IVideoDriver* driver,
                              const GemSlotLayout& gem);
    void drawSpellbookButton(irr::video::IVideoDriver* driver);

    // Managers
    EQ::SpellManager* spellMgr_;
    ItemIconLoader* iconLoader_;

    // State
    bool visible_ = true;
    irr::core::position2di position_;

    // Layout constants - initialized from UISettings
    int GEM_WIDTH;
    int GEM_HEIGHT;
    int GEM_SPACING;
    int PANEL_PADDING;
    int SPELLBOOK_BUTTON_SIZE;
    int SPELLBOOK_BUTTON_MARGIN;

    // Gem slots
    std::array<GemSlotLayout, EQ::MAX_SPELL_GEMS> gems_;

    // Hover state
    int hoveredGem_ = -1;

    // Callbacks
    GemCastCallback castCallback_;
    GemForgetCallback forgetCallback_;
    GemHoverCallback hoverCallback_;
    GemHoverEndCallback hoverEndCallback_;
    SpellbookButtonCallback spellbookCallback_;
    SpellHotbarPickupCallback hotbarPickupCallback_;
    MemorizeSpellCallback memorizeCallback_;
    GetSpellCursorCallback getSpellCursorCallback_;
    ClearSpellCursorCallback clearSpellCursorCallback_;

    // Spellbook button
    irr::core::recti spellbookButtonBounds_;
    bool spellbookButtonHovered_ = false;
};

} // namespace ui
} // namespace eqt
