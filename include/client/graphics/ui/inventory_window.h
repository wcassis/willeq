#pragma once

#include "window_base.h"
#include "item_slot.h"
#include "inventory_constants.h"
#include "inventory_manager.h"
#include "ui_settings.h"
#include "character_model_view.h"
#include <array>
#include <functional>
#include <memory>

// Forward declarations
namespace EQT {
namespace Graphics {
class RaceModelLoader;
class EquipmentModelLoader;
struct EntityAppearance;
}
}

namespace eqt {
namespace ui {

// Callback types
using BagClickCallback = std::function<void(int16_t generalSlot)>;
using SlotClickCallback = std::function<void(int16_t slotId, bool shift, bool ctrl)>;
using SlotHoverCallback = std::function<void(int16_t slotId, int mouseX, int mouseY)>;
using DestroyClickCallback = std::function<void()>;
using IconLookupCallback = std::function<irr::video::ITexture*(uint32_t iconId)>;

class InventoryWindow : public WindowBase {
public:
    InventoryWindow(inventory::InventoryManager* manager);

    // Input handling
    bool handleMouseDown(int x, int y, bool leftButton, bool shift, bool ctrl) override;
    bool handleMouseUp(int x, int y, bool leftButton) override;
    bool handleMouseMove(int x, int y) override;

    // Callbacks
    void setBagClickCallback(BagClickCallback callback) { bagClickCallback_ = callback; }
    void setSlotClickCallback(SlotClickCallback callback) { slotClickCallback_ = callback; }
    void setSlotHoverCallback(SlotHoverCallback callback) { slotHoverCallback_ = callback; }
    void setDestroyClickCallback(DestroyClickCallback callback) { destroyClickCallback_ = callback; }
    void setIconLookupCallback(IconLookupCallback callback) { iconLookupCallback_ = callback; }

    // Get slot at position
    int16_t getSlotAtPosition(int x, int y) const;

    // Update highlighting
    void setHighlightedSlot(int16_t slotId);
    void setInvalidDropSlot(int16_t slotId);
    void clearHighlights();

    // Character info (for left panel display)
    void setCharacterName(const std::wstring& name) { characterName_ = name; }
    void setCharacterLevel(int level) { characterLevel_ = level; }
    void setCharacterClass(const std::wstring& className) { characterClass_ = className; }
    void setCharacterDeity(const std::wstring& deity) { characterDeity_ = deity; }
    void setHP(int current, int max) { currentHP_ = current; maxHP_ = max; }
    void setMana(int current, int max) { currentMana_ = current; maxMana_ = max; }
    void setStamina(int current, int max) { currentStamina_ = current; maxStamina_ = max; }
    void setAC(int ac) { ac_ = ac; }
    void setATK(int atk) { atk_ = atk; }
    void setExpProgress(float progress) { expProgress_ = progress; }
    void setStats(int str, int sta, int agi, int dex, int wis, int intel, int cha) {
        stats_[0] = str; stats_[1] = sta; stats_[2] = agi; stats_[3] = dex;
        stats_[4] = wis; stats_[5] = intel; stats_[6] = cha;
    }
    void setResists(int poison, int magic, int disease, int fire, int cold) {
        resists_[0] = poison; resists_[1] = magic; resists_[2] = disease;
        resists_[3] = fire; resists_[4] = cold;
    }
    void setHaste(int haste) { haste_ = haste; }
    void setSpellDmg(int spellDmg) { spellDmg_ = spellDmg; }
    void setHealAmt(int healAmt) { healAmt_ = healAmt; }
    void setRegenHP(int regen) { regenHP_ = regen; }
    void setRegenMana(int regen) { regenMana_ = regen; }
    void setWeight(float current, float max) { weight_ = current; maxWeight_ = max; }
    void setCurrency(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
        platinum_ = platinum; gold_ = gold; silver_ = silver; copper_ = copper;
    }

    // Character model view (3D preview in equipment area)
    void initModelView(irr::scene::ISceneManager* smgr,
                       irr::video::IVideoDriver* driver,
                       EQT::Graphics::RaceModelLoader* raceLoader,
                       EQT::Graphics::EquipmentModelLoader* equipLoader);
    void setPlayerAppearance(uint16_t raceId, uint8_t gender,
                             const EQT::Graphics::EntityAppearance& appearance);
    void updateModelView(float deltaTimeMs);
    bool hasModelView() const { return modelView_ != nullptr; }

protected:
    void renderContent(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui) override;

private:
    void initializeSlots();
    void renderCharacterInfo(irr::video::IVideoDriver* driver,
                            irr::gui::IGUIEnvironment* gui);
    void renderEquipmentSlots(irr::video::IVideoDriver* driver,
                             irr::gui::IGUIEnvironment* gui);
    void renderGeneralSlots(irr::video::IVideoDriver* driver,
                           irr::gui::IGUIEnvironment* gui);
    void renderButtons(irr::video::IVideoDriver* driver,
                      irr::gui::IGUIEnvironment* gui);
    void renderCurrency(irr::video::IVideoDriver* driver,
                       irr::gui::IGUIEnvironment* gui);
    void renderModelView(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui);

    // Inventory manager
    inventory::InventoryManager* manager_;

    // Equipment slots (22 slots: 0-21)
    std::array<ItemSlot, inventory::EQUIPMENT_COUNT> equipmentSlots_;

    // General inventory slots (8 slots: 22-29)
    std::array<ItemSlot, inventory::GENERAL_COUNT> generalSlots_;

    // Button bounds
    irr::core::recti destroyButtonBounds_;
    irr::core::recti doneButtonBounds_;

    // Button state
    bool destroyButtonHighlighted_ = false;
    bool doneButtonHighlighted_ = false;

    // Callbacks
    BagClickCallback bagClickCallback_;
    SlotClickCallback slotClickCallback_;
    SlotHoverCallback slotHoverCallback_;
    DestroyClickCallback destroyClickCallback_;
    IconLookupCallback iconLookupCallback_;

    // Highlighted slot
    int16_t highlightedSlot_ = inventory::SLOT_INVALID;
    int16_t invalidDropSlot_ = inventory::SLOT_INVALID;

    // Character info
    std::wstring characterName_;
    int characterLevel_ = 0;
    std::wstring characterClass_;
    std::wstring characterDeity_;
    int currentHP_ = 0;
    int maxHP_ = 0;
    int currentMana_ = 0;
    int maxMana_ = 0;
    int currentStamina_ = 0;
    int maxStamina_ = 0;
    int ac_ = 0;
    int atk_ = 0;
    float expProgress_ = 0.0f;  // 0.0 to 1.0
    int stats_[7] = {0};  // STR, STA, AGI, DEX, WIS, INT, CHA
    int resists_[5] = {0};  // Poison, Magic, Disease, Fire, Cold
    int haste_ = 0;
    int spellDmg_ = 0;
    int healAmt_ = 0;
    int regenHP_ = 0;
    int regenMana_ = 0;
    float weight_ = 0.0f;
    float maxWeight_ = 0.0f;

    // Currency
    uint32_t platinum_ = 0;
    uint32_t gold_ = 0;
    uint32_t silver_ = 0;
    uint32_t copper_ = 0;

    // Layout accessors - read from UISettings
    int getSlotSize() const { return UISettings::instance().inventory().slotSize; }
    int getSlotSpacing() const { return UISettings::instance().inventory().slotSpacing; }
    int getEquipmentStartX() const { return UISettings::instance().inventory().equipmentStartX; }
    int getEquipmentStartY() const { return UISettings::instance().inventory().equipmentStartY; }
    int getGeneralStartX() const { return UISettings::instance().inventory().generalStartX; }
    int getGeneralStartY() const { return UISettings::instance().inventory().generalStartY; }
    int getStatsWidth() const { return UISettings::instance().inventory().statsWidth; }

    // Legacy constants for initialization (read from UISettings at construction)
    int SLOT_SIZE;
    int SLOT_SPACING;
    int EQUIP_START_X;
    int EQUIP_START_Y;
    int GENERAL_START_X;
    int GENERAL_START_Y;
    int STATS_WIDTH;

    // Character model view (3D preview)
    std::unique_ptr<CharacterModelView> modelView_;
    irr::core::recti modelViewBounds_;  // Bounds relative to window content area
};

} // namespace ui
} // namespace eqt
