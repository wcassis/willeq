// tools/model_viewer/spell_bar_ui.h -- the viewer's on-screen spell bar and casting
// progress bar, plus the SpellBarEntry/CastingState value types.
//
// T385 (2026-09-05, board B167): split out of tools/model_viewer_spell_bar.h,
// which was 1,965 lines -- over the ceiling that check_file_sizes.sh did not
// apply to tools/ until this unit. Declarations moved verbatim; the original
// header is now an umbrella that includes this one.

#pragma once

#include <irrlicht.h>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <unordered_map>
#include "client/graphics/eq/dds_decoder.h"
#include "model_viewer/spell_effect_db.h"
// Forward declaration for hand bone queries
namespace EQT {
namespace Graphics {
class EQAnimatedMeshSceneNode;
}
}

namespace EQT {

// ============================================================================
// Spell Bar Entry - represents a single spell in the bar
// ============================================================================
struct SpellBarEntry {
    std::string name;           // Display name (e.g., "Fire Bolt")
    ResistType resistType;      // Determines particle color
    SpellSchool school;         // Determines casting animation
    SpellCategory category;     // Determines completion effect style
    float castTime;             // Cast duration in seconds

    // Derived from category - uses authentic EQ casting animations:
    // t04: beneficial spells (buffs, shields, invis)
    // t05: heals, summoning, creating items
    // t06: offensive/detrimental spells (damage, CC)
    std::string getCastAnimation() const {
        switch (category) {
            // Offensive/detrimental -> t06
            case SpellCategory::DirectDamage:
            case SpellCategory::DoT:
            case SpellCategory::AEDamage:
            case SpellCategory::PBAE:
            case SpellCategory::Rain:
            case SpellCategory::Mez:
            case SpellCategory::Root:
            case SpellCategory::Fear:
                return "t06";

            // Heals, summoning, creating -> t05
            case SpellCategory::Heal:
            case SpellCategory::SummonPet:
            case SpellCategory::SummonItem:
                return "t05";

            // Beneficial (buffs, utility) -> t04
            case SpellCategory::Buff:
            case SpellCategory::Gate:
            case SpellCategory::Invisibility:
            default:
                return "t04";
        }
    }

    // Get color based on resist type
    irr::video::SColor getColor() const {
        switch (resistType) {
            case ResistType::Fire:       return irr::video::SColor(255, 255, 100, 0);    // Orange
            case ResistType::Cold:       return irr::video::SColor(255, 0, 200, 255);    // Cyan
            case ResistType::Magic:      return irr::video::SColor(255, 180, 100, 255);  // Purple
            case ResistType::Poison:     return irr::video::SColor(255, 0, 200, 0);      // Green
            case ResistType::Disease:    return irr::video::SColor(255, 139, 90, 43);    // Brown
            case ResistType::Chromatic:
            case ResistType::Prismatic:  return irr::video::SColor(255, 255, 200, 100);  // Gold/Rainbow
            case ResistType::Corruption: return irr::video::SColor(255, 100, 0, 150);    // Dark purple
            case ResistType::Physical:   return irr::video::SColor(255, 150, 150, 150);  // Grey
            case ResistType::None:
            default:                     return irr::video::SColor(255, 255, 255, 255);  // White
        }
    }

    // UI state
    irr::core::recti bounds;    // Screen bounds for hit testing
    bool isHovered = false;
};

// Callback when a spell is clicked
using SpellClickCallback = std::function<void(int spellIndex, const SpellBarEntry& spell)>;

// Callback when a spell cast completes
using CastCompleteCallback = std::function<void(const SpellBarEntry& spell)>;

// ============================================================================
// CastingState - tracks the current spell being cast
// ============================================================================
struct CastingState {
    bool isCasting = false;
    int spellIndex = -1;
    SpellBarEntry spell;        // Copy of the spell being cast
    float castTime = 0;         // Total cast time in seconds
    float elapsed = 0;          // Time elapsed since cast started
    std::string castAnim;       // Animation being played

    // Get progress as 0.0 to 1.0
    float getProgress() const {
        if (castTime <= 0) return 1.0f;
        return std::min(1.0f, elapsed / castTime);
    }

    // Check if cast is complete
    bool isComplete() const {
        return elapsed >= castTime;
    }

    // Reset to not casting
    void reset() {
        isCasting = false;
        spellIndex = -1;
        castTime = 0;
        elapsed = 0;
        castAnim.clear();
    }
};

// ============================================================================
// CastingBar - UI component showing cast progress
// ============================================================================
class CastingBar {
public:
    CastingBar() = default;
    ~CastingBar() = default;

    void initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
        m_driver = driver;
        m_gui = gui;
    }

    void setScreenSize(int width, int height) {
        m_screenWidth = width;
        m_screenHeight = height;
        updateLayout();
    }

    // Start a new cast
    void startCast(const SpellBarEntry& spell) {
        m_state.isCasting = true;
        m_state.spell = spell;
        m_state.castTime = spell.castTime;
        m_state.elapsed = 0;
        m_state.castAnim = spell.getCastAnimation();
        m_visible = true;
    }

    // Update cast progress (call each frame)
    // Returns true if cast just completed this frame
    bool update(float deltaSeconds) {
        if (!m_state.isCasting) return false;

        m_state.elapsed += deltaSeconds;

        if (m_state.isComplete()) {
            return true;  // Cast completed
        }
        return false;
    }

    // Complete the current cast (called after completion effect)
    void completeCast() {
        m_state.reset();
        m_fadeTime = FADE_DURATION;  // Start fade out
    }

    // Cancel/interrupt the current cast
    void cancelCast() {
        m_state.reset();
        m_interrupted = true;
        m_fadeTime = FADE_DURATION;
    }

    // Render the casting bar
    void render() {
        if (!m_driver) return;

        // Handle fade out
        if (!m_state.isCasting && m_fadeTime > 0) {
            m_fadeTime -= 0.016f;  // Assume ~60fps
            if (m_fadeTime <= 0) {
                m_visible = false;
                m_interrupted = false;
                return;
            }
        }

        if (!m_visible) return;

        float alpha = m_state.isCasting ? 1.0f : (m_fadeTime / FADE_DURATION);
        renderBar(alpha);
    }

    // Check if currently casting
    bool isCasting() const { return m_state.isCasting; }

    // Get current casting state
    const CastingState& getState() const { return m_state; }

    // Get the spell being cast
    const SpellBarEntry* getCurrentSpell() const {
        return m_state.isCasting ? &m_state.spell : nullptr;
    }

private:
    void updateLayout() {
        // Position bar at bottom center of screen
        m_barWidth = 300;
        m_barHeight = 20;
        m_barX = (m_screenWidth - m_barWidth) / 2;
        m_barY = m_screenHeight - 100;  // 100 pixels from bottom
    }

    void renderBar(float alpha) {
        irr::u32 a = static_cast<irr::u32>(alpha * 255);

        // Background
        irr::core::recti bgRect(m_barX - 2, m_barY - 2,
                                 m_barX + m_barWidth + 2, m_barY + m_barHeight + 2);
        m_driver->draw2DRectangle(irr::video::SColor(a * 200 / 255, 20, 20, 30), bgRect);

        // Border
        m_driver->draw2DRectangleOutline(bgRect, irr::video::SColor(a, 80, 80, 100));

        // Progress fill
        float progress = m_state.getProgress();
        int fillWidth = static_cast<int>(m_barWidth * progress);

        irr::video::SColor fillColor = m_interrupted ?
            irr::video::SColor(a, 200, 50, 50) :  // Red if interrupted
            m_state.spell.getColor();
        fillColor.setAlpha(a);

        irr::core::recti fillRect(m_barX, m_barY,
                                   m_barX + fillWidth, m_barY + m_barHeight);
        m_driver->draw2DRectangle(fillColor, fillRect);

        // Empty portion (darker)
        if (fillWidth < m_barWidth) {
            irr::core::recti emptyRect(m_barX + fillWidth, m_barY,
                                        m_barX + m_barWidth, m_barY + m_barHeight);
            m_driver->draw2DRectangle(irr::video::SColor(a * 150 / 255, 30, 30, 40), emptyRect);
        }

        // Spell name and time
        if (m_gui) {
            irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
            if (font) {
                // Spell name above bar
                std::wstring nameStr(m_state.spell.name.begin(), m_state.spell.name.end());
                if (m_interrupted) {
                    nameStr += L" - INTERRUPTED";
                }
                irr::core::recti nameRect(m_barX, m_barY - 16, m_barX + m_barWidth, m_barY);
                font->draw(nameStr.c_str(), nameRect, irr::video::SColor(a, 255, 255, 255));

                // Time remaining
                float remaining = std::max(0.0f, m_state.castTime - m_state.elapsed);
                int tenths = static_cast<int>(remaining * 10) % 10;
                int seconds = static_cast<int>(remaining);
                std::wstring timeStr = std::to_wstring(seconds) + L"." + std::to_wstring(tenths) + L"s";
                irr::core::recti timeRect(m_barX + m_barWidth - 40, m_barY + 3,
                                          m_barX + m_barWidth - 2, m_barY + m_barHeight);
                font->draw(timeStr.c_str(), timeRect, irr::video::SColor(a, 200, 200, 200));
            }
        }
    }

    irr::video::IVideoDriver* m_driver = nullptr;
    irr::gui::IGUIEnvironment* m_gui = nullptr;

    CastingState m_state;
    bool m_visible = false;
    bool m_interrupted = false;
    float m_fadeTime = 0;

    int m_screenWidth = 800;
    int m_screenHeight = 600;
    int m_barX = 250;
    int m_barY = 500;
    int m_barWidth = 300;
    int m_barHeight = 20;

    static constexpr float FADE_DURATION = 0.5f;
};

// ============================================================================
// SpellBar - UI component for spell casting visualization
// ============================================================================
class SpellBar {
public:
    SpellBar();
    ~SpellBar() = default;

    // Initialize with Irrlicht components
    void initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui);

    // Set screen dimensions for layout
    void setScreenSize(int width, int height);

    // Populate with demo spells
    void initializeDefaultSpells();

    // Rendering
    void render();

    // Input handling - returns true if event was consumed
    bool handleMouseMove(int x, int y);
    bool handleMouseClick(int x, int y, bool leftButton);

    // Keyboard shortcuts (1-0 keys)
    bool handleKeyPress(int keyCode);

    // Set callback for spell clicks
    void setSpellClickCallback(SpellClickCallback callback) {
        m_clickCallback = std::move(callback);
    }

    // Get spell at index (for casting)
    const SpellBarEntry* getSpell(int index) const {
        if (index >= 0 && index < static_cast<int>(m_spells.size())) {
            return &m_spells[index];
        }
        return nullptr;
    }

    int getSpellCount() const { return static_cast<int>(m_spells.size()); }

    // Visibility
    void show() { m_visible = true; }
    void hide() { m_visible = false; }
    void toggle() { m_visible = !m_visible; }
    bool isVisible() const { return m_visible; }

private:
    void updateLayout();
    void renderGem(const SpellBarEntry& spell, int index);
    void renderTooltip(const SpellBarEntry& spell);

    irr::video::IVideoDriver* m_driver = nullptr;
    irr::gui::IGUIEnvironment* m_gui = nullptr;

    std::vector<SpellBarEntry> m_spells;
    SpellClickCallback m_clickCallback;

    // Layout
    int m_screenWidth = 800;
    int m_screenHeight = 600;
    int m_barX = 10;          // Left edge position
    int m_barY = 100;         // Starting Y position

    // Gem dimensions
    static constexpr int GEM_SIZE = 40;
    static constexpr int GEM_SPACING = 4;
    static constexpr int GEM_BORDER = 2;

    // State
    bool m_visible = true;
    int m_hoveredIndex = -1;
    int m_mouseX = 0;
    int m_mouseY = 0;
};

// ============================================================================
// Implementation
// ============================================================================

inline SpellBar::SpellBar() {}

inline void SpellBar::initialize(irr::video::IVideoDriver* driver, irr::gui::IGUIEnvironment* gui) {
    m_driver = driver;
    m_gui = gui;
    initializeDefaultSpells();
    updateLayout();
}

inline void SpellBar::setScreenSize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;
    updateLayout();
}

inline void SpellBar::initializeDefaultSpells() {
    m_spells.clear();

    // Representative spells covering different resist types and categories
    // Based on the plan document

    m_spells.push_back({"Fire Bolt", ResistType::Fire, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 2.0f});

    m_spells.push_back({"Frost Shock", ResistType::Cold, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 1.5f});

    m_spells.push_back({"Lightning Bolt", ResistType::Magic, SpellSchool::Evocation,
                        SpellCategory::DirectDamage, 2.5f});

    m_spells.push_back({"Poison Bolt", ResistType::Poison, SpellSchool::Conjuration,
                        SpellCategory::DoT, 2.0f});

    m_spells.push_back({"Plague", ResistType::Disease, SpellSchool::Conjuration,
                        SpellCategory::DoT, 3.0f});

    m_spells.push_back({"Chromatic Flash", ResistType::Chromatic, SpellSchool::Evocation,
                        SpellCategory::AEDamage, 3.5f});

    m_spells.push_back({"Greater Heal", ResistType::None, SpellSchool::Alteration,
                        SpellCategory::Heal, 4.0f});

    m_spells.push_back({"Spirit Armor", ResistType::None, SpellSchool::Abjuration,
                        SpellCategory::Buff, 3.0f});

    m_spells.push_back({"Summon Pet", ResistType::None, SpellSchool::Conjuration,
                        SpellCategory::SummonPet, 8.0f});

    m_spells.push_back({"Root", ResistType::Magic, SpellSchool::Alteration,
                        SpellCategory::Root, 1.5f});

    updateLayout();
}

inline void SpellBar::updateLayout() {
    // Position spell bar vertically centered on left side
    int totalHeight = static_cast<int>(m_spells.size()) * (GEM_SIZE + GEM_SPACING) - GEM_SPACING;
    m_barY = (m_screenHeight - totalHeight) / 2;

    // Update bounds for each spell
    for (size_t i = 0; i < m_spells.size(); ++i) {
        int y = m_barY + static_cast<int>(i) * (GEM_SIZE + GEM_SPACING);
        m_spells[i].bounds = irr::core::recti(
            m_barX, y,
            m_barX + GEM_SIZE, y + GEM_SIZE
        );
    }
}

inline void SpellBar::render() {
    if (!m_visible || !m_driver) return;

    // Draw semi-transparent background panel
    int panelPadding = 6;
    int totalHeight = static_cast<int>(m_spells.size()) * (GEM_SIZE + GEM_SPACING) - GEM_SPACING;
    irr::core::recti panelRect(
        m_barX - panelPadding,
        m_barY - panelPadding,
        m_barX + GEM_SIZE + panelPadding,
        m_barY + totalHeight + panelPadding
    );
    m_driver->draw2DRectangle(irr::video::SColor(160, 20, 20, 30), panelRect);

    // Draw border around panel
    m_driver->draw2DRectangleOutline(panelRect, irr::video::SColor(200, 80, 80, 100));

    // Render each gem
    for (size_t i = 0; i < m_spells.size(); ++i) {
        renderGem(m_spells[i], static_cast<int>(i));
    }

    // Render tooltip for hovered spell
    if (m_hoveredIndex >= 0 && m_hoveredIndex < static_cast<int>(m_spells.size())) {
        renderTooltip(m_spells[m_hoveredIndex]);
    }
}

inline void SpellBar::renderGem(const SpellBarEntry& spell, int index) {
    const irr::core::recti& bounds = spell.bounds;

    // Gem background (darker)
    irr::video::SColor bgColor(220, 30, 30, 40);
    if (spell.isHovered) {
        bgColor = irr::video::SColor(240, 50, 50, 70);
    }
    m_driver->draw2DRectangle(bgColor, bounds);

    // Gem colored inner area (based on resist type)
    irr::video::SColor gemColor = spell.getColor();
    if (spell.isHovered) {
        // Brighten on hover
        gemColor.setAlpha(255);
    } else {
        gemColor.setAlpha(200);
    }

    irr::core::recti innerRect(
        bounds.UpperLeftCorner.X + GEM_BORDER,
        bounds.UpperLeftCorner.Y + GEM_BORDER,
        bounds.LowerRightCorner.X - GEM_BORDER,
        bounds.LowerRightCorner.Y - GEM_BORDER
    );
    m_driver->draw2DRectangle(gemColor, innerRect);

    // Border
    irr::video::SColor borderColor = spell.isHovered ?
        irr::video::SColor(255, 255, 255, 200) :
        irr::video::SColor(200, 100, 100, 120);
    m_driver->draw2DRectangleOutline(bounds, borderColor);

    // Draw slot number in corner
    if (m_gui) {
        irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
        if (font) {
            std::wstring numStr = std::to_wstring((index + 1) % 10);  // 1-9, 0
            irr::core::recti numRect(
                bounds.UpperLeftCorner.X + 2,
                bounds.UpperLeftCorner.Y + 1,
                bounds.UpperLeftCorner.X + 12,
                bounds.UpperLeftCorner.Y + 12
            );
            font->draw(numStr.c_str(), numRect, irr::video::SColor(200, 200, 200, 200));
        }
    }
}

inline void SpellBar::renderTooltip(const SpellBarEntry& spell) {
    if (!m_gui) return;

    irr::gui::IGUIFont* font = m_gui->getBuiltInFont();
    if (!font) return;

    // Build tooltip text
    std::wstring nameStr(spell.name.begin(), spell.name.end());

    // Get school name
    std::wstring schoolStr;
    switch (spell.school) {
        case SpellSchool::Abjuration:  schoolStr = L"Abjuration"; break;
        case SpellSchool::Alteration:  schoolStr = L"Alteration"; break;
        case SpellSchool::Conjuration: schoolStr = L"Conjuration"; break;
        case SpellSchool::Divination:  schoolStr = L"Divination"; break;
        case SpellSchool::Evocation:   schoolStr = L"Evocation"; break;
    }

    // Get resist type name
    std::wstring resistStr;
    switch (spell.resistType) {
        case ResistType::None:       resistStr = L"Unresistable"; break;
        case ResistType::Magic:      resistStr = L"Magic"; break;
        case ResistType::Fire:       resistStr = L"Fire"; break;
        case ResistType::Cold:       resistStr = L"Cold"; break;
        case ResistType::Poison:     resistStr = L"Poison"; break;
        case ResistType::Disease:    resistStr = L"Disease"; break;
        case ResistType::Chromatic:  resistStr = L"Chromatic"; break;
        case ResistType::Prismatic:  resistStr = L"Prismatic"; break;
        case ResistType::Physical:   resistStr = L"Physical"; break;
        case ResistType::Corruption: resistStr = L"Corruption"; break;
    }

    std::wstring castStr = L"Cast: " + std::to_wstring(static_cast<int>(spell.castTime * 10) / 10) + L"." +
                           std::to_wstring(static_cast<int>(spell.castTime * 10) % 10) + L"s";

    // Position tooltip to the right of the spell bar
    int tooltipX = m_barX + GEM_SIZE + 10;
    int tooltipY = spell.bounds.UpperLeftCorner.Y;
    int tooltipWidth = 150;
    int tooltipHeight = 52;

    // Background
    irr::core::recti tooltipRect(tooltipX, tooltipY, tooltipX + tooltipWidth, tooltipY + tooltipHeight);
    m_driver->draw2DRectangle(irr::video::SColor(230, 20, 20, 30), tooltipRect);
    m_driver->draw2DRectangleOutline(tooltipRect, irr::video::SColor(255, 100, 100, 120));

    // Text
    font->draw(nameStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 2, tooltipX + tooltipWidth - 4, tooltipY + 14),
               spell.getColor());

    font->draw((schoolStr + L" - " + resistStr).c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 16, tooltipX + tooltipWidth - 4, tooltipY + 28),
               irr::video::SColor(255, 180, 180, 180));

    font->draw(castStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 30, tooltipX + tooltipWidth - 4, tooltipY + 42),
               irr::video::SColor(255, 150, 150, 150));

    // Animation code
    std::string animCode = spell.getCastAnimation();
    std::wstring animStr = L"Anim: " + std::wstring(animCode.begin(), animCode.end());
    font->draw(animStr.c_str(),
               irr::core::recti(tooltipX + 4, tooltipY + 40, tooltipX + tooltipWidth - 4, tooltipY + 52),
               irr::video::SColor(255, 120, 120, 120));
}

inline bool SpellBar::handleMouseMove(int x, int y) {
    if (!m_visible) return false;

    m_mouseX = x;
    m_mouseY = y;

    int previousHovered = m_hoveredIndex;
    m_hoveredIndex = -1;

    for (size_t i = 0; i < m_spells.size(); ++i) {
        m_spells[i].isHovered = false;
        if (m_spells[i].bounds.isPointInside(irr::core::position2di(x, y))) {
            m_hoveredIndex = static_cast<int>(i);
            m_spells[i].isHovered = true;
        }
    }

    // Return true if we're over the spell bar (to prevent other input handling)
    return m_hoveredIndex >= 0 || previousHovered >= 0;
}

inline bool SpellBar::handleMouseClick(int x, int y, bool leftButton) {
    if (!m_visible || !leftButton) return false;

    for (size_t i = 0; i < m_spells.size(); ++i) {
        if (m_spells[i].bounds.isPointInside(irr::core::position2di(x, y))) {
            if (m_clickCallback) {
                m_clickCallback(static_cast<int>(i), m_spells[i]);
            }
            return true;
        }
    }

    return false;
}

inline bool SpellBar::handleKeyPress(int keyCode) {
    if (!m_visible) return false;

    // Handle 1-9, 0 keys (Irrlicht KEY_KEY_1 = 0x31, KEY_KEY_0 = 0x30)
    int spellIndex = -1;

    if (keyCode >= 0x31 && keyCode <= 0x39) {
        // Keys 1-9 -> spell indices 0-8
        spellIndex = keyCode - 0x31;
    } else if (keyCode == 0x30) {
        // Key 0 -> spell index 9
        spellIndex = 9;
    }

    if (spellIndex >= 0 && spellIndex < static_cast<int>(m_spells.size())) {
        if (m_clickCallback) {
            m_clickCallback(spellIndex, m_spells[spellIndex]);
        }
        return true;
    }

    return false;
}


} // namespace EQT
