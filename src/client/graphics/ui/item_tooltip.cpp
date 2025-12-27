#include "client/graphics/ui/item_tooltip.h"
#include <sstream>
#include <iomanip>

namespace eqt {
namespace ui {

ItemTooltip::ItemTooltip()
{
    // Initialize layout constants from UISettings
    const auto& tooltipSettings = UISettings::instance().itemTooltip();
    TOOLTIP_MIN_WIDTH = tooltipSettings.minWidth;
    TOOLTIP_MAX_WIDTH = tooltipSettings.maxWidth;
    LINE_HEIGHT = tooltipSettings.lineHeight;
    PADDING = tooltipSettings.padding;
    SECTION_SPACING = tooltipSettings.sectionSpacing;
    MOUSE_OFFSET = tooltipSettings.mouseOffset;
}

void ItemTooltip::setItem(const inventory::ItemInstance* item, int mouseX, int mouseY) {
    if (item_ != item) {
        item_ = item;
        visible_ = false;
        hoverStartTime_ = 0;
        hoverX_ = mouseX;
        hoverY_ = mouseY;
        lines_.clear();

        if (item_) {
            buildTooltipContent();
        }
    }
}

void ItemTooltip::clear() {
    item_ = nullptr;
    visible_ = false;
    hoverStartTime_ = 0;
    lines_.clear();
}

void ItemTooltip::update(uint32_t currentTimeMs, int mouseX, int mouseY) {
    if (!item_) {
        visible_ = false;
        return;
    }

    // Check if mouse moved significantly
    int dx = mouseX - hoverX_;
    int dy = mouseY - hoverY_;
    if (dx * dx + dy * dy > 100) {  // 10 pixel threshold
        // Mouse moved, reset timer
        hoverStartTime_ = currentTimeMs;
        hoverX_ = mouseX;
        hoverY_ = mouseY;
        visible_ = false;
    }

    // Check if hover delay has passed
    if (!visible_) {
        if (hoverStartTime_ == 0) {
            hoverStartTime_ = currentTimeMs;
        }

        if (currentTimeMs - hoverStartTime_ >= hoverDelayMs_) {
            visible_ = true;
        }
    }
}

void ItemTooltip::render(irr::video::IVideoDriver* driver,
                        irr::gui::IGUIEnvironment* gui,
                        int screenWidth, int screenHeight) {
    if (!visible_ || !item_ || lines_.empty()) {
        return;
    }

    // Position tooltip
    positionTooltip(hoverX_, hoverY_, screenWidth, screenHeight);

    // Render background and content
    renderBackground(driver);
    renderContent(driver, gui);
}

void ItemTooltip::buildTooltipContent() {
    lines_.clear();

    if (!item_) {
        return;
    }

    // Item name
    std::wstring name(item_->name.begin(), item_->name.end());
    addLine(name, item_->magic ? getMagic() : getTitle());

    // Item flags (Magic, No Trade, Lore, etc.)
    std::wstring flags = buildFlagsString();
    if (!flags.empty()) {
        addLine(flags, getFlags());
    }

    // Class restrictions
    std::wstring classStr = buildClassString();
    addLine(L"Class: " + classStr, getRestrictions());

    // Race restrictions
    std::wstring raceStr = buildRaceString();
    addLine(L"Race: " + raceStr, getRestrictions());

    // Equippable slots
    if (item_->slots != 0) {
        std::wstring slotsStr = buildSlotsString();
        addLine(slotsStr, getRestrictions());
    }

    addBlankLine();

    // Size, Weight, Level requirements
    {
        std::wostringstream ss;
        ss << L"Size: " << inventory::getSizeName(item_->size);
        std::wstring sizeStr = ss.str();

        ss.str(L"");
        if (item_->ac != 0) {
            ss << L"AC: " << item_->ac;
        }
        addLine(sizeStr, ss.str(), getStats(), getStatsValue());
    }

    {
        std::wostringstream ss;
        ss << L"Weight: " << std::fixed << std::setprecision(1) << item_->weight;
        std::wstring weightStr = ss.str();

        ss.str(L"");
        if (item_->hp != 0) {
            ss << L"HP: " << item_->hp;
        }
        addLine(weightStr, ss.str(), getStats(), getStatsValue());
    }

    if (item_->recLevel > 0 || item_->reqLevel > 0) {
        std::wostringstream ss;
        if (item_->reqLevel > 0) {
            ss << L"Req Level: " << (int)item_->reqLevel;
        } else {
            ss << L"Rec Level: " << (int)item_->recLevel;
        }
        std::wstring levelStr = ss.str();

        ss.str(L"");
        if (item_->mana != 0) {
            ss << L"Mana: " << item_->mana;
        }
        addLine(levelStr, ss.str(), getStats(), getStatsValue());
    } else if (item_->mana != 0) {
        std::wostringstream ss;
        ss << L"Mana: " << item_->mana;
        addLine(L"", ss.str(), getStats(), getStatsValue());
    }

    // Endurance (show alongside other resource stats)
    if (item_->endurance != 0) {
        std::wostringstream ss;
        ss << L"Endurance: " << item_->endurance;
        addLine(ss.str(), getStatsValue());
    }

    // Weapon stats
    if (item_->damage > 0 || item_->delay > 0) {
        addBlankLine();

        // Show weapon type if available
        const char* weaponType = inventory::getWeaponTypeAbbrev(item_->skillType);
        if (weaponType) {
            std::wostringstream ss;
            ss << L"Skill: " << weaponType;
            addLine(ss.str(), getStats());
        }

        std::wostringstream ss;
        ss << L"Damage: " << item_->damage << L"  Delay: " << item_->delay;
        addLine(ss.str(), getStatsValue());

        if (item_->delay > 0) {
            float ratio = (float)item_->damage / (float)item_->delay;
            ss.str(L"");
            ss << L"Ratio: " << std::fixed << std::setprecision(2) << ratio;
            addLine(ss.str(), getStats());
        }

        // Range for ranged weapons
        if (item_->range > 0) {
            ss.str(L"");
            ss << L"Range: " << item_->range;
            addLine(ss.str(), getStats());
        }

        // Attack bonus
        if (item_->attack != 0) {
            ss.str(L"");
            ss << L"Attack: " << std::showpos << item_->attack << std::noshowpos;
            addLine(ss.str(), getStatsValue());
        }

        // Accuracy bonus
        if (item_->accuracy != 0) {
            ss.str(L"");
            ss << L"Accuracy: " << std::showpos << item_->accuracy << std::noshowpos;
            addLine(ss.str(), getStatsValue());
        }
    }

    // Stats
    if (item_->hasStats()) {
        addBlankLine();
        addStatLine(L"Strength", item_->str, L"Magic", item_->magicResist);
        addStatLine(L"Stamina", item_->sta, L"Fire", item_->fireResist);
        addStatLine(L"Wisdom", item_->wis, L"Cold", item_->coldResist);
        addStatLine(L"Agility", item_->agi, L"Disease", item_->diseaseResist);
        addStatLine(L"Dexterity", item_->dex, L"Poison", item_->poisonResist);
        addStatLine(L"Intelligence", item_->int_, L"", 0);
        addStatLine(L"Charisma", item_->cha, L"", 0);
    } else if (item_->hasResists()) {
        // Just resistances
        addBlankLine();
        if (item_->magicResist != 0) addStatLine(L"", 0, L"Magic", item_->magicResist);
        if (item_->fireResist != 0) addStatLine(L"", 0, L"Fire", item_->fireResist);
        if (item_->coldResist != 0) addStatLine(L"", 0, L"Cold", item_->coldResist);
        if (item_->diseaseResist != 0) addStatLine(L"", 0, L"Disease", item_->diseaseResist);
        if (item_->poisonResist != 0) addStatLine(L"", 0, L"Poison", item_->poisonResist);
    }

    // Heroic stats
    if (item_->heroicStr != 0 || item_->heroicSta != 0 || item_->heroicAgi != 0 ||
        item_->heroicDex != 0 || item_->heroicWis != 0 || item_->heroicInt != 0 ||
        item_->heroicCha != 0) {
        addBlankLine();
        if (item_->heroicStr != 0) {
            std::wostringstream ss;
            ss << L"Heroic STR: " << std::showpos << item_->heroicStr << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));  // Gold color
        }
        if (item_->heroicSta != 0) {
            std::wostringstream ss;
            ss << L"Heroic STA: " << std::showpos << item_->heroicSta << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
        if (item_->heroicAgi != 0) {
            std::wostringstream ss;
            ss << L"Heroic AGI: " << std::showpos << item_->heroicAgi << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
        if (item_->heroicDex != 0) {
            std::wostringstream ss;
            ss << L"Heroic DEX: " << std::showpos << item_->heroicDex << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
        if (item_->heroicWis != 0) {
            std::wostringstream ss;
            ss << L"Heroic WIS: " << std::showpos << item_->heroicWis << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
        if (item_->heroicInt != 0) {
            std::wostringstream ss;
            ss << L"Heroic INT: " << std::showpos << item_->heroicInt << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
        if (item_->heroicCha != 0) {
            std::wostringstream ss;
            ss << L"Heroic CHA: " << std::showpos << item_->heroicCha << std::noshowpos;
            addLine(ss.str(), irr::video::SColor(255, 255, 215, 0));
        }
    }

    // Special stats (haste, regen, etc.)
    if (item_->haste != 0) {
        std::wostringstream ss;
        ss << L"Haste: " << item_->haste << L"%";
        addLine(ss.str(), getStatsValue());
    }
    if (item_->hpRegen != 0) {
        std::wostringstream ss;
        ss << L"HP Regen: " << item_->hpRegen;
        addLine(ss.str(), getStatsValue());
    }
    if (item_->manaRegen != 0) {
        std::wostringstream ss;
        ss << L"Mana Regen: " << item_->manaRegen;
        addLine(ss.str(), getStatsValue());
    }
    if (item_->enduranceRegen != 0) {
        std::wostringstream ss;
        ss << L"Endurance Regen: " << item_->enduranceRegen;
        addLine(ss.str(), getStatsValue());
    }

    // Additional defensive stats
    if (item_->damageShield != 0) {
        std::wostringstream ss;
        ss << L"Damage Shield: " << item_->damageShield;
        addLine(ss.str(), getStatsValue());
    }
    if (item_->spellShield != 0) {
        std::wostringstream ss;
        ss << L"Spell Shield: " << item_->spellShield << L"%";
        addLine(ss.str(), getStatsValue());
    }
    if (item_->shielding != 0) {
        std::wostringstream ss;
        ss << L"Shielding: " << item_->shielding << L"%";
        addLine(ss.str(), getStatsValue());
    }
    if (item_->dotShield != 0) {
        std::wostringstream ss;
        ss << L"DoT Shielding: " << item_->dotShield << L"%";
        addLine(ss.str(), getStatsValue());
    }
    if (item_->avoidance != 0) {
        std::wostringstream ss;
        ss << L"Avoidance: " << item_->avoidance;
        addLine(ss.str(), getStatsValue());
    }
    if (item_->strikethrough != 0) {
        std::wostringstream ss;
        ss << L"Strikethrough: " << item_->strikethrough << L"%";
        addLine(ss.str(), getStatsValue());
    }
    if (item_->stunResist != 0) {
        std::wostringstream ss;
        ss << L"Stun Resist: " << item_->stunResist << L"%";
        addLine(ss.str(), getStatsValue());
    }

    // Container info
    if (item_->isContainer()) {
        addBlankLine();
        std::wostringstream ss;
        ss << L"Container: " << (int)item_->bagSlots << L" slots";
        addLine(ss.str(), getStats());

        ss.str(L"");
        ss << L"Size Capacity: " << inventory::getSizeName(item_->bagSize);
        addLine(ss.str(), getStats());

        if (item_->bagWR > 0) {
            ss.str(L"");
            ss << L"Weight Reduction: " << (int)item_->bagWR << L"%";
            addLine(ss.str(), getStats());
        }
    }

    // Augment slots
    int augCount = item_->countAugmentSlots();
    if (augCount > 0) {
        addBlankLine();
        for (int i = 0; i < inventory::MAX_AUGMENT_SLOTS; i++) {
            const auto& slot = item_->augmentSlots[i];
            if (slot.visible && slot.type != 0) {
                std::wostringstream ss;
                ss << L"Slot " << (i + 1) << L", " << buildAugmentTypeString(slot.type);
                if (slot.augmentId != 0) {
                    ss << L": [Augment]";  // Would show augment name here
                } else {
                    ss << L": empty";
                }
                addLine(ss.str(), getStats());
            }
        }
    }

    // Effects
    if (item_->hasEffects()) {
        addBlankLine();

        if (item_->clickEffect.effectId != 0) {
            std::wstring effectName(item_->clickEffect.name.begin(), item_->clickEffect.name.end());
            addLine(L"Effect: " + effectName, getEffect());
            if (!item_->clickEffect.description.empty()) {
                std::wstring desc(item_->clickEffect.description.begin(),
                                 item_->clickEffect.description.end());
                addLine(desc, getEffectDesc());
            }
        }

        if (item_->wornEffect.effectId != 0) {
            std::wstring effectName(item_->wornEffect.name.begin(), item_->wornEffect.name.end());
            addLine(L"Worn Effect: " + effectName, getEffect());
        }

        if (item_->focusEffect.effectId != 0) {
            std::wstring effectName(item_->focusEffect.name.begin(), item_->focusEffect.name.end());
            addLine(L"Focus Effect: " + effectName, getEffect());
            if (!item_->focusEffect.description.empty()) {
                std::wstring desc(item_->focusEffect.description.begin(),
                                 item_->focusEffect.description.end());
                addLine(desc, getEffectDesc());
            }
        }

        if (item_->procEffect.effectId != 0) {
            std::wstring effectName(item_->procEffect.name.begin(), item_->procEffect.name.end());
            addLine(L"Combat Effect: " + effectName, getEffect());
        }
    }

    // Lore text
    if (!item_->lore.empty() && item_->lore != item_->name) {
        addBlankLine();
        std::wstring lore(item_->lore.begin(), item_->lore.end());
        addLine(lore, getLore());
    }
}

void ItemTooltip::positionTooltip(int mouseX, int mouseY,
                                  int screenWidth, int screenHeight) {
    // Calculate tooltip size based on content
    int contentHeight = lines_.size() * LINE_HEIGHT + PADDING * 2;
    int tooltipWidth = TOOLTIP_MIN_WIDTH;
    int tooltipHeight = contentHeight;

    // Start position: below and to the right of cursor
    int x = mouseX + MOUSE_OFFSET;
    int y = mouseY + MOUSE_OFFSET;

    // Adjust if going off screen
    if (x + tooltipWidth > screenWidth) {
        x = mouseX - tooltipWidth - MOUSE_OFFSET;
    }
    if (y + tooltipHeight > screenHeight) {
        y = mouseY - tooltipHeight - MOUSE_OFFSET;
    }

    // Ensure not off left/top edge
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    bounds_ = irr::core::recti(x, y, x + tooltipWidth, y + tooltipHeight);
}

void ItemTooltip::renderBackground(irr::video::IVideoDriver* driver) {
    // Draw background
    driver->draw2DRectangle(getBackground(), bounds_);

    // Draw border
    // Top
    driver->draw2DRectangle(getBorder(),
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.UpperLeftCorner.Y + 1));
    // Left
    driver->draw2DRectangle(getBorder(),
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.UpperLeftCorner.X + 1,
                        bounds_.LowerRightCorner.Y));
    // Bottom
    driver->draw2DRectangle(getBorder(),
        irr::core::recti(bounds_.UpperLeftCorner.X,
                        bounds_.LowerRightCorner.Y - 1,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
    // Right
    driver->draw2DRectangle(getBorder(),
        irr::core::recti(bounds_.LowerRightCorner.X - 1,
                        bounds_.UpperLeftCorner.Y,
                        bounds_.LowerRightCorner.X,
                        bounds_.LowerRightCorner.Y));
}

void ItemTooltip::renderContent(irr::video::IVideoDriver* driver,
                               irr::gui::IGUIEnvironment* gui) {
    irr::gui::IGUIFont* font = gui->getBuiltInFont();
    if (!font) {
        return;
    }

    int y = bounds_.UpperLeftCorner.Y + PADDING;
    int leftX = bounds_.UpperLeftCorner.X + PADDING;
    int rightX = bounds_.LowerRightCorner.X - PADDING;
    int midX = (leftX + rightX) / 2;

    for (const auto& line : lines_) {
        if (line.text.empty() && line.text2.empty()) {
            // Blank line
            y += LINE_HEIGHT / 2;
            continue;
        }

        if (line.isTwoColumn) {
            // Two column layout
            if (!line.text.empty()) {
                font->draw(line.text.c_str(),
                          irr::core::recti(leftX, y, midX, y + LINE_HEIGHT),
                          line.color);
            }
            if (!line.text2.empty()) {
                font->draw(line.text2.c_str(),
                          irr::core::recti(midX, y, rightX, y + LINE_HEIGHT),
                          line.color2);
            }
        } else {
            // Single column
            font->draw(line.text.c_str(),
                      irr::core::recti(leftX, y, rightX, y + LINE_HEIGHT),
                      line.color);
        }

        y += LINE_HEIGHT;
    }
}

void ItemTooltip::addLine(const std::wstring& text, irr::video::SColor color) {
    TooltipLine line;
    line.text = text;
    line.color = color;
    line.isTwoColumn = false;
    lines_.push_back(line);
}

void ItemTooltip::addLine(const std::wstring& label, const std::wstring& value,
                         irr::video::SColor labelColor, irr::video::SColor valueColor) {
    TooltipLine line;
    line.text = label;
    line.color = labelColor;
    line.text2 = value;
    line.color2 = valueColor;
    line.isTwoColumn = true;
    lines_.push_back(line);
}

void ItemTooltip::addStatLine(const std::wstring& stat1Name, int stat1Value,
                             const std::wstring& stat2Name, int stat2Value) {
    std::wostringstream ss1, ss2;

    if (!stat1Name.empty() && stat1Value != 0) {
        ss1 << stat1Name << L": " << stat1Value;
    }

    if (!stat2Name.empty() && stat2Value != 0) {
        ss2 << stat2Name << L": " << stat2Value;
    }

    if (!ss1.str().empty() || !ss2.str().empty()) {
        addLine(ss1.str(), ss2.str(), getStats(), getStatsValue());
    }
}

void ItemTooltip::addBlankLine() {
    TooltipLine line;
    line.isTwoColumn = false;
    lines_.push_back(line);
}

std::wstring ItemTooltip::buildFlagsString() const {
    if (!item_) return L"";

    std::wstring result;
    if (item_->magic) {
        result += L"Magic";
    }
    if (item_->noDrop) {
        if (!result.empty()) result += L", ";
        result += L"No Trade";
    }
    if (item_->loreItem) {
        if (!result.empty()) result += L", ";
        result += L"Lore";
    }
    if (item_->noRent) {
        if (!result.empty()) result += L", ";
        result += L"No Rent";
    }
    if (item_->artifact) {
        if (!result.empty()) result += L", ";
        result += L"Artifact";
    }
    if (item_->summoned) {
        if (!result.empty()) result += L", ";
        result += L"Summoned";
    }
    if (item_->questItem) {
        if (!result.empty()) result += L", ";
        result += L"Quest";
    }
    return result;
}

std::wstring ItemTooltip::buildClassString() const {
    if (!item_) return L"ALL";

    std::string str = inventory::buildClassString(item_->classes);
    return std::wstring(str.begin(), str.end());
}

std::wstring ItemTooltip::buildRaceString() const {
    if (!item_) return L"ALL";

    std::string str = inventory::buildRaceString(item_->races);
    return std::wstring(str.begin(), str.end());
}

std::wstring ItemTooltip::buildSlotsString() const {
    if (!item_) return L"";

    std::string str = inventory::buildSlotsString(item_->slots);
    return std::wstring(str.begin(), str.end());
}

std::wstring ItemTooltip::buildAugmentTypeString(uint32_t augType) const {
    // Common augment type descriptions
    if (augType & inventory::AUG_TYPE_GENERAL_SINGLE_STAT) return L"type 1 (General: Single Stat)";
    if (augType & inventory::AUG_TYPE_GENERAL_MULTIPLE_STAT) return L"type 2 (General: Multiple Stat)";
    if (augType & inventory::AUG_TYPE_GENERAL_SPELL_EFFECT) return L"type 3 (General: Spell Effect)";
    if (augType & inventory::AUG_TYPE_WEAPON_GENERAL) return L"type 4 (Weapon: General)";
    if (augType & inventory::AUG_TYPE_WEAPON_ELEM_DAMAGE) return L"type 5 (Weapon: Elem Damage)";
    if (augType & inventory::AUG_TYPE_WEAPON_BASE_DAMAGE) return L"type 6 (Weapon: Base Damage)";
    if (augType & inventory::AUG_TYPE_GENERAL_GROUP) return L"type 7 (General: Group)";
    if (augType & inventory::AUG_TYPE_GENERAL_RAID) return L"type 8 (General: Raid)";
    if (augType & inventory::AUG_TYPE_ORNAMENTATION) return L"type 20 (Ornamentation)";
    if (augType & inventory::AUG_TYPE_SPECIAL_ORNAMENTATION) return L"type 21 (Special Ornamentation)";

    std::wostringstream ss;
    ss << L"type " << augType;
    return ss.str();
}

} // namespace ui
} // namespace eqt
