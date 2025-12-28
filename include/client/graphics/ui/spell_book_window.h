#pragma once

#include "window_base.h"
#include "ui_settings.h"
#include "client/spell/spell_constants.h"
#include "client/spell/spell_data.h"
#include <array>
#include <vector>
#include <functional>

namespace EQ {
class SpellManager;
}

namespace eqt {
namespace ui {

class ItemIconLoader;

// Callback types for spell book interactions
using SpellClickCallback = std::function<void(uint32_t spell_id, uint8_t gem_slot)>;
using SpellHoverCallback = std::function<void(uint32_t spell_id, int mouseX, int mouseY)>;
using SpellHoverEndCallback = std::function<void()>;
using SetSpellCursorCallback = std::function<void(uint32_t spell_id, irr::video::ITexture* icon)>;

// Individual spell slot in the spellbook
struct SpellSlot {
    irr::core::recti bounds;        // Screen-relative bounds
    uint32_t spell_id = EQ::SPELL_UNKNOWN;
    uint16_t book_slot = 0;         // Spellbook slot index
    bool is_hovered = false;
    bool is_empty = true;
};

class SpellBookWindow : public WindowBase {
public:
    SpellBookWindow(EQ::SpellManager* spell_mgr, ItemIconLoader* icon_loader);
    ~SpellBookWindow() = default;

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl = false) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Page navigation
    void nextPage();
    void prevPage();
    void goToPage(int page);
    int getCurrentPage() const { return currentPage_; }
    int getTotalPages() const;

    // Callbacks
    void setSpellClickCallback(SpellClickCallback cb) { spellClickCallback_ = std::move(cb); }
    void setSpellHoverCallback(SpellHoverCallback cb) { spellHoverCallback_ = std::move(cb); }
    void setSpellHoverEndCallback(SpellHoverEndCallback cb) { spellHoverEndCallback_ = std::move(cb); }
    void setSetSpellCursorCallback(SetSpellCursorCallback cb) { setSpellCursorCallback_ = std::move(cb); }

    // Set target gem slot for memorization (1-8, 0 = none)
    void setTargetGemSlot(uint8_t slot) { targetGemSlot_ = slot; }
    uint8_t getTargetGemSlot() const { return targetGemSlot_; }

    // Get spell at position (for drag-drop)
    uint32_t getSpellAtPosition(int x, int y) const;

    // Refresh spellbook contents from SpellManager
    void refresh();

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeLayout();
    void layoutPage();
    void renderSpellSlot(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui,
                        const SpellSlot& slot);
    void renderNavigationButtons(irr::video::IVideoDriver* driver,
                                 irr::gui::IGUIEnvironment* gui);
    void renderPageInfo(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui);
    void renderSpellTooltip(irr::video::IVideoDriver* driver,
                           irr::gui::IGUIEnvironment* gui,
                           const EQ::SpellData* spell,
                           int mouseX, int mouseY);
    int getSlotIndexAtPosition(int x, int y) const;

    // Managers
    EQ::SpellManager* spellMgr_;
    ItemIconLoader* iconLoader_;

    // Layout constants - initialized from UISettings
    // Keep array sizes as static constexpr
    static constexpr int SPELLS_PER_PAGE = 8;   // Spells per single page
    static constexpr int TOTAL_SLOTS = SPELLS_PER_PAGE * 2;  // Both pages

    // Initialized from UISettings
    int PAGE_WIDTH;       // Width of each page
    int PAGE_SPACING;     // Gap between pages (spine)
    int ICON_SIZE;        // Same as spell gem icons
    int ROW_HEIGHT;       // Icon + small padding
    int ROW_SPACING;
    int LEFT_PAGE_X;      // Left page start X
    int RIGHT_PAGE_X;     // Right page start X (computed)
    int SLOTS_START_Y;
    int NAME_OFFSET_X;    // Space after icon for name
    int NAME_MAX_WIDTH;   // Room for spell names within page
    int NAV_BUTTON_WIDTH;
    int NAV_BUTTON_HEIGHT;

    // Page state (each increment shows 2 pages worth of spells)
    int currentPage_ = 0;

    // Spell slots for current two-page spread (left page + right page)
    std::array<SpellSlot, TOTAL_SLOTS> spellSlots_;

    // Navigation button bounds (window-relative)
    irr::core::recti prevButtonBounds_;
    irr::core::recti nextButtonBounds_;

    // Button hover state
    bool prevButtonHovered_ = false;
    bool nextButtonHovered_ = false;

    // Currently hovered slot
    int hoveredSlotIndex_ = -1;

    // Mouse position for tooltip
    int mouseX_ = 0;
    int mouseY_ = 0;

    // Target gem slot for memorization (1-8, 0 = none selected)
    uint8_t targetGemSlot_ = 0;

    // Callbacks
    SpellClickCallback spellClickCallback_;
    SpellHoverCallback spellHoverCallback_;
    SpellHoverEndCallback spellHoverEndCallback_;
    SetSpellCursorCallback setSpellCursorCallback_;
};

} // namespace ui
} // namespace eqt
