#include "client/inventory/inventory_manager.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "common/net/packet.h"
#include "common/packet_structs.h"  // T282: EQT::ITEM_PACKET_WORLD_CONTAINER
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace eqt {
namespace inventory {

// ============================================================================
// TitaniumItemParser Implementation
// ============================================================================

int32_t TitaniumItemParser::toInt(std::string_view str, int32_t defaultVal) {
    if (str.empty()) return defaultVal;

    int32_t result = defaultVal;
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);
    if (ec != std::errc()) {
        return defaultVal;
    }
    return result;
}

float TitaniumItemParser::toFloat(std::string_view str, float defaultVal) {
    if (str.empty()) return defaultVal;

    // std::from_chars for float might not be available in all compilers
    // Use a simple manual approach
    try {
        std::string s(str);
        return std::stof(s);
    } catch (...) {
        return defaultVal;
    }
}

std::vector<std::string_view> TitaniumItemParser::splitByPipe(std::string_view str) {
    std::vector<std::string_view> result;
    size_t start = 0;
    size_t pos = 0;

    while (pos < str.size()) {
        if (str[pos] == '|') {
            result.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }
        pos++;
    }
    // Add the last segment
    if (start <= str.size()) {
        result.push_back(str.substr(start));
    }

    return result;
}

std::unique_ptr<ItemInstance> TitaniumItemParser::parseItem(const char* data, size_t length, int16_t& slotId,
                                                            std::map<int16_t, std::unique_ptr<ItemInstance>>* subItems) {
    if (!data || length == 0) {
        return nullptr;
    }
    std::string str(data, length);
    return parseItem(str, slotId, subItems);
}

std::unique_ptr<ItemInstance> TitaniumItemParser::parseItem(const std::string& data, int16_t& slotId,
                                                            std::map<int16_t, std::unique_ptr<ItemInstance>>* subItems) {
    if (data.empty()) {
        return nullptr;
    }

    auto item = std::make_unique<ItemInstance>();
    slotId = SLOT_INVALID;

    // The format is:
    // [leading protection/quotes for subitems]
    // instance_field_0|instance_field_1|...|instance_field_10|"static_data"|subitem0|subitem1|...|subitem9[trailing]

    // Find the quoted section which contains static item data
    size_t firstQuote = data.find('"');
    if (firstQuote == std::string::npos) {
        LOG_DEBUG(MOD_UI, "No quoted section found in item data");
        return nullptr;
    }

    // Find the matching closing quote
    // The quote pattern is: "static_data"
    // But there can be escaped quotes inside, and sub-items can have quotes too
    // The static data section ends with " followed by | or end of string
    size_t secondQuote = firstQuote + 1;
    while (secondQuote < data.size()) {
        if (data[secondQuote] == '"') {
            // Check if next char is | or end - that's our closing quote
            if (secondQuote + 1 >= data.size() || data[secondQuote + 1] == '|') {
                break;
            }
        }
        secondQuote++;
    }

    if (secondQuote >= data.size()) {
        LOG_DEBUG(MOD_UI, "No closing quote found in item data");
        return nullptr;
    }

    // Parse instance data (before first quote)
    std::string_view instanceSection(data.data(), firstQuote);

    // Debug: log what's before the quote
    std::string instancePreview(instanceSection.substr(0, std::min(size_t(80), instanceSection.size())));
    LOG_DEBUG(MOD_UI, "TitaniumItemParser BEFORE QUOTE (first 80 chars): '{}' (quote at pos {})", instancePreview, firstQuote);

    auto instanceFields = splitByPipe(instanceSection);

    if (!parseInstanceData(instanceFields, *item, slotId)) {
        LOG_DEBUG(MOD_UI, "Failed to parse instance data");
        return nullptr;
    }

    // Parse static data (between quotes)
    std::string_view staticSection(data.data() + firstQuote + 1, secondQuote - firstQuote - 1);

    // Debug: log raw static section (first 100 chars)
    std::string rawPreview(staticSection.substr(0, std::min(size_t(100), staticSection.size())));
    LOG_DEBUG(MOD_UI, "TitaniumItemParser RAW STATIC (first 100 chars): '{}'", rawPreview);

    if (!parseStaticData(staticSection, *item)) {
        LOG_DEBUG(MOD_UI, "Failed to parse static data");
        return nullptr;
    }

    // Parse sub-items (bag contents) if this is a container and caller wants them
    if (subItems && item->isContainer() && secondQuote + 1 < data.size()) {
        // T255: the server serialises a cursor item at its own cursor slot
        // (33); calcContainerSlotId() knows only the client cursor (30), so
        // every cursor bag's contents used to collapse onto SLOT_INVALID.
        int16_t parentSlot = (slotId == SERVER_CURSOR_SLOT) ? CURSOR_SLOT : slotId;
        parseSubItems(std::string_view(data), secondQuote + 1, parentSlot, *subItems);
    }

    return item;
}

bool TitaniumItemParser::parseInstanceData(const std::vector<std::string_view>& fields,
                                           ItemInstance& item, int16_t& slotId) {
    // Instance data fields (from titanium.cpp SerializeItem):
    // 0: stack count (0 if not stackable)
    // 1: unknown (0)
    // 2: slot id / merchant slot
    // 3: price
    // 4: count / merchant count
    // 5: experience (for scaling items)
    // 6: serial number / merchant serial
    // 7: recast timestamp
    // 8: charge count
    // 9: attuned (0/1)
    // 10: unknown (0)

    if (fields.size() < 11) {
        // Not enough instance fields
        return false;
    }

    int32_t stackCount = toInt(fields[0]);
    slotId = static_cast<int16_t>(toInt(fields[2]));
    item.price = toInt(fields[3]);
    int32_t count = toInt(fields[4]);
    item.quantity = std::max(1, stackCount > 0 ? stackCount : count);
    item.charges = toInt(fields[8]);
    int32_t attuned = toInt(fields[9]);

    // Debug: log ALL instance data fields for debugging NO_DROP issue
    LOG_DEBUG(MOD_UI, "TitaniumItemParser INSTANCE: slot={} stackCount={} count={} price={} charges={} attuned={} [fields: 0='{}' 1='{}' 2='{}' 3='{}' 4='{}' 5='{}' 6='{}' 7='{}' 8='{}' 9='{}' 10='{}']",
        slotId, stackCount, count, item.price, item.charges, attuned,
        std::string(fields[0]), std::string(fields[1]), std::string(fields[2]),
        std::string(fields[3]), std::string(fields[4]), std::string(fields[5]),
        std::string(fields[6]), std::string(fields[7]), std::string(fields[8]),
        std::string(fields[9]), fields.size() > 10 ? std::string(fields[10]) : "N/A");

    return true;
}

bool TitaniumItemParser::parseStaticData(std::string_view quotedData, ItemInstance& item) {
    auto fields = splitByPipe(quotedData);

    // Static item data fields (from titanium.cpp SerializeItem):
    // This is a very long list - see the serialize function for all 159+ fields
    // Key fields we care about:

    if (fields.size() < 135) {
        LOG_DEBUG(MOD_UI, "TitaniumItemParser Not enough static fields: {}", fields.size());
        return false;
    }

    // Basic info
    item.itemType = static_cast<uint8_t>(toInt(fields[0]));  // ItemClass
    item.name = std::string(fields[1]);
    item.lore = std::string(fields[2]);
    // fields[3] = IDFile
    item.itemId = static_cast<uint32_t>(toInt(fields[4]));
    item.weight = static_cast<float>(toInt(fields[5])) / 10.0f;  // Weight is in tenths

    // Flags - Debug dump first 15 fields to verify structure
    LOG_DEBUG(MOD_UI, "TitaniumItemParser FIELDS '{}': [0]={} [1]={} [2]={} [3]={} [4]={} [5]={} [6]={} [7]={} [8]={} [9]={} [10]={} [11]={} [12]={} [13]={} [14]={}",
        std::string(fields[1]),  // name
        std::string(fields[0]), std::string(fields[1]), std::string(fields[2]), std::string(fields[3]),
        std::string(fields[4]), std::string(fields[5]), std::string(fields[6]), std::string(fields[7]),
        std::string(fields[8]), std::string(fields[9]), std::string(fields[10]), std::string(fields[11]),
        std::string(fields[12]), std::string(fields[13]), std::string(fields[14]));

    // EQEmu convention: 0 = cannot drop/trade (IS nodrop), non-zero = can drop/trade (NOT nodrop)
    item.noRent = toInt(fields[6]) == 0;
    item.noDrop = toInt(fields[7]) == 0;
    item.size = static_cast<uint8_t>(toInt(fields[8]));
    item.slots = static_cast<uint32_t>(toInt(fields[9]));  // Equipment slots bitmask
    // fields[10] = vendor price (already have from instance)
    item.icon = static_cast<uint32_t>(toInt(fields[11]));

    // Log item with emphasis on noDrop field for debugging
    LOG_DEBUG(MOD_UI, "TitaniumItemParser STATIC: '{}' itemId={} noRent={} noDrop={} (field[6]='{}' field[7]='{}')",
        item.name, item.itemId, item.noRent, item.noDrop, std::string(fields[6]), std::string(fields[7]));
    // fields[12-13] = 0, 0
    // fields[14] = BenefitFlag
    // fields[15] = Tradeskills

    // Resistances (16-20)
    item.coldResist = toInt(fields[16]);
    item.diseaseResist = toInt(fields[17]);
    item.poisonResist = toInt(fields[18]);
    item.magicResist = toInt(fields[19]);
    item.fireResist = toInt(fields[20]);

    // Stats (21-27)
    item.str = toInt(fields[21]);
    item.sta = toInt(fields[22]);
    item.agi = toInt(fields[23]);
    item.dex = toInt(fields[24]);
    item.cha = toInt(fields[25]);
    item.int_ = toInt(fields[26]);
    item.wis = toInt(fields[27]);

    // HP/Mana/AC/Deity (28-31)
    item.hp = toInt(fields[28]);
    item.mana = toInt(fields[29]);
    item.ac = toInt(fields[30]);
    item.deity = static_cast<uint32_t>(toInt(fields[31]));

    // Skill mod (32-34)
    // fields[32-34] = SkillModValue, SkillModMax, SkillModType

    // Bane damage (35-37)
    // fields[35-37] = BaneDmgRace, BaneDmgAmt, BaneDmgBody

    // Combat stats (38-44)
    item.magic = toInt(fields[38]) != 0;
    // fields[39] = CastTime_
    item.reqLevel = static_cast<uint8_t>(toInt(fields[40]));
    // fields[41-42] = BardType, BardValue
    // fields[43] = Light
    item.delay = toInt(fields[44]);

    // Rec level/skill (45-46)
    item.recLevel = static_cast<uint8_t>(toInt(fields[45]));
    // fields[46] = RecSkill

    // Elemental (47-48)
    // fields[47-48] = ElemDmgType, ElemDmgAmt

    // Range/Damage (49-50)
    item.range = toInt(fields[49]);
    item.damage = toInt(fields[50]);

    // Color/Class/Race (51-53)
    item.color = static_cast<uint32_t>(toInt(fields[51]));
    item.classes = static_cast<uint32_t>(toInt(fields[52]));
    item.races = static_cast<uint32_t>(toInt(fields[53]));
    // fields[54] = 0

    // Max charges / item type / material (55-57)
    item.maxCharges = toInt(fields[55]);
    item.skillType = static_cast<uint8_t>(toInt(fields[56]));  // Weapon skill type (1HS, 2HB, etc.)
    item.material = static_cast<uint8_t>(toInt(fields[57]));
    // fields[58] = SellRate

    // More combat stats (59-72)
    // fields[59-61] = 0, CastTime_, 0
    // fields[62] = ProcRate
    // fields[63-72] = CombatEffects, Shielding, StunResist, StrikeThrough, etc.
    item.shielding = toInt(fields[64]);
    item.stunResist = toInt(fields[65]);
    item.strikethrough = toInt(fields[66]);
    // fields[67] = ExtraDmgSkill
    // fields[68] = ExtraDmgAmt
    item.spellShield = toInt(fields[69]);
    item.avoidance = toInt(fields[70]);
    item.accuracy = toInt(fields[71]);

    // Charm/Faction (72-81)
    // fields[72] = CharmFileID
    // fields[73-80] = FactionMod/Amt 1-4
    // fields[81] = CharmFile

    // Augments (82-92)
    // fields[82] = AugType
    // fields[83-92] = AugSlotType/AugSlotVisible for 5 augs
    for (int i = 0; i < 5 && i < MAX_AUGMENT_SLOTS; i++) {
        item.augmentSlots[i].type = static_cast<uint32_t>(toInt(fields[83 + i * 2]));
        item.augmentSlots[i].visible = toInt(fields[84 + i * 2]) != 0;
    }

    // LDoN (93-95)
    // fields[93-95] = LDoNTheme, LDoNPrice, LDoNSold

    // Bag info (96-99)
    item.bagType = static_cast<uint8_t>(toInt(fields[96]));
    item.bagSlots = static_cast<uint8_t>(toInt(fields[97]));
    item.bagSize = static_cast<uint8_t>(toInt(fields[98]));
    item.bagWR = static_cast<uint8_t>(toInt(fields[99]));

    // Debug: log container info
    if (item.bagSlots > 0 || item.itemType == 1) {  // itemType 1 = container
        LOG_DEBUG(MOD_UI, "TitaniumItemParser Container '{}': bagType={} bagSlots={} bagSize={} (raw fields: [96]='{}' [97]='{}' [98]='{}')",
                  item.name, (int)item.bagType, (int)item.bagSlots, (int)item.bagSize,
                  std::string(fields[96]), std::string(fields[97]), std::string(fields[98]));
    }

    // Book (100-102) -- eqemu titanium.cpp SerializeItem emits, in order,
    // ItemData::Book (0=not a book, 1=book), ItemData::BookType (0=scroll,
    // 1=book, 2=item info) and ItemData::Filename (the `books`.`name` key
    // OP_ReadBook asks the server for). T273: field 100 used to be read as
    // the book TYPE and field 101 dropped, so /read would have sent the
    // is-a-book flag as the display type.
    item.bookFlag = static_cast<uint8_t>(toInt(fields[100]));
    item.bookType = static_cast<uint32_t>(toInt(fields[101]));
    if (fields.size() > 102 && !fields[102].empty()) {
        item.bookFilename = std::string(fields[102]);
    }

    // More flags (103-108)
    // fields[103] = BaneDmgRaceAmt
    // fields[104] = AugRestrict
    item.loreGroup = static_cast<uint32_t>(toInt(fields[105]));
    item.loreItem = toInt(fields[106]) != 0;  // PendingLoreFlag
    item.artifact = toInt(fields[107]) != 0;
    item.summoned = toInt(fields[108]) != 0;

    // Regeneration and special stats (109-121)
    // fields[109] = Favor
    // fields[110] = FVNoDrop
    item.endurance = toInt(fields[111]);
    item.dotShield = toInt(fields[112]);
    item.attack = toInt(fields[113]);
    item.hpRegen = toInt(fields[114]);
    item.manaRegen = toInt(fields[115]);
    item.enduranceRegen = toInt(fields[116]);
    item.haste = toInt(fields[117]);
    item.damageShield = toInt(fields[118]);
    // fields[119-121] = RecastDelay, RecastType, GuildFavor

    // fields[122] = AugDistiller
    // fields[123-128] = various unknowns, Attuneable, NoPet, PointType

    // Stack info (129-133) - from titanium.cpp SerializeItem:
    // fields[129] = PotionBelt
    // fields[130] = PotionBeltSlots
    // fields[131] = StackSize
    // fields[132] = NoTransfer
    // fields[133] = Stackable
    item.stackSize = toInt(fields[131]);
    bool noDropFromField7 = item.noDrop;  // Already set from field[7]
    int32_t noTransferField132 = toInt(fields[132]);
    item.noDrop = item.noDrop || (noTransferField132 != 0);  // NoTransfer
    item.stackable = toInt(fields[133]) != 0;

    // Log NoTransfer field and final noDrop status
    LOG_DEBUG(MOD_UI, "TitaniumItemParser FINAL: '{}' noDrop={} (field[7]={}, field[132]/NoTransfer={}, field[124]/Attuneable='{}')",
        item.name, item.noDrop, noDropFromField7 ? 1 : 0, noTransferField132,
        fields.size() > 124 ? std::string(fields[124]) : "N/A");

    // Effects (134-158) - Click, Proc, Worn, Focus, Scroll (5 fields each)
    if (fields.size() >= 154) {
        item.clickEffect.effectId = toInt(fields[134]);
        item.clickEffect.type = toInt(fields[135]);
        // fields[136-137] = Level2, Level
        // fields[138] = Click name (0)

        item.procEffect.effectId = toInt(fields[139]);
        item.procEffect.type = toInt(fields[140]);

        item.wornEffect.effectId = toInt(fields[144]);
        item.wornEffect.type = toInt(fields[145]);

        item.focusEffect.effectId = toInt(fields[149]);
        item.focusEffect.type = toInt(fields[150]);
    }

    // Scroll effect (154-158)
    if (fields.size() >= 159) {
        item.scrollEffect.effectId = toInt(fields[154]);
        item.scrollEffect.type = toInt(fields[155]);
        LOG_DEBUG(MOD_UI, "TitaniumItemParser Scroll effect for '{}': effectId={} type={}",
            item.name, item.scrollEffect.effectId, item.scrollEffect.type);
    }

    // T425 Leg B (board B375): `expendable`, which had NO WRITER at all and
    // so could never light eqstr 3001 "EXPENDABLE     Charges: %1" that
    // BuildItemDisplayText() has printed since T421.
    //
    // It is not a field on the wire -- it is DERIVED, and eqemu's own
    // derivation is the definition (/mnt/projects/akk-stack/code/common/item_instance.h):
    //
    //     inline bool IsExpendable() const {
    //         return m_item->Click.Type == item::ItemEffectExpendable
    //             || m_item->ItemType == item::ItemTypePotion; }
    //
    // Both inputs are serialized by Titanium: Click.Type is field 135 (read
    // just above as clickEffect.type) and ItemType is field 0 (read at the
    // top as item.itemType). The two enumerator values come from
    // /mnt/projects/akk-stack/code/common/item_data.h: ItemEffect{Combat,Click,Worn,Expendable,...} makes
    // Expendable 3, and the ItemType table makes Potion 21.
    constexpr int kItemEffectExpendable = 3;   // EQ::item::ItemEffectExpendable
    constexpr uint8_t kItemTypePotion   = 21;  // EQ::item::ItemTypePotion
    item.expendable = (item.clickEffect.type == kItemEffectExpendable)
                   || (item.itemType == kItemTypePotion);

    return true;
}

void TitaniumItemParser::parseSubItems(std::string_view data, size_t afterQuotePos, int16_t parentSlot,
                                       std::map<int16_t, std::unique_ptr<ItemInstance>>& subItems) {
    // Sub-items are after the closing quote of the parent item's static data
    // Format: |subitem0|subitem1|...|subitem9
    // Each sub-item at depth 1 is wrapped in quotes: "instance|\"static\""
    // Empty slots are just empty strings between pipes

    if (afterQuotePos >= data.size()) {
        return;
    }

    // Skip initial pipe if present
    size_t pos = afterQuotePos;
    if (pos < data.size() && data[pos] == '|') {
        pos++;
    }

    int slotIndex = 0;
    const int maxSlots = 10;  // Max bag size

    while (pos < data.size() && slotIndex < maxSlots) {
        // Find the end of this sub-item section (next pipe at depth 0)
        size_t subItemStart = pos;
        size_t subItemEnd = pos;
        int quoteDepth = 0;

        while (subItemEnd < data.size()) {
            char c = data[subItemEnd];
            if (c == '"' && (subItemEnd == 0 || data[subItemEnd - 1] != '\\')) {
                quoteDepth = 1 - quoteDepth;  // Toggle between 0 and 1
            } else if (c == '|' && quoteDepth == 0) {
                break;
            }
            subItemEnd++;
        }

        // Extract sub-item data
        std::string_view subItemData = data.substr(subItemStart, subItemEnd - subItemStart);

        // Parse non-empty sub-items
        if (!subItemData.empty() && subItemData.find('"') != std::string_view::npos) {
            // Sub-items at depth 1 have format: "instance_data|\"static_data\""
            // Strip the outer quotes
            size_t firstQuote = subItemData.find('"');
            size_t lastQuote = subItemData.rfind('"');

            if (firstQuote != std::string_view::npos && lastQuote > firstQuote) {
                std::string_view innerData = subItemData.substr(firstQuote + 1, lastQuote - firstQuote - 1);

                // Unescape the inner quotes (\" -> ")
                std::string unescaped;
                unescaped.reserve(innerData.size());
                for (size_t i = 0; i < innerData.size(); i++) {
                    if (i + 1 < innerData.size() && innerData[i] == '\\' && innerData[i + 1] == '"') {
                        unescaped += '"';
                        i++;  // Skip the backslash
                    } else {
                        unescaped += innerData[i];
                    }
                }

                // Calculate the bag slot ID for this sub-item
                // Uses calcContainerSlotId which handles general, bank, shared bank, cursor, and trade bags
                int16_t bagSlotId = calcContainerSlotId(parentSlot, slotIndex);

                // Parse the sub-item (don't recurse into sub-sub-items)
                int16_t parsedSlot = SLOT_INVALID;
                auto subItem = parseItem(unescaped, parsedSlot, nullptr);

                if (subItem && bagSlotId != SLOT_INVALID) {
                    LOG_DEBUG(MOD_UI, "TitaniumItemParser Parsed bag content: {} in bag slot {} (index {}) qty={} stackable={}",
                              subItem->name, bagSlotId, slotIndex, subItem->quantity, subItem->stackable);
                    subItems[bagSlotId] = std::move(subItem);
                } else if (subItem) {
                    LOG_WARN(MOD_UI, "TitaniumItemParser Bag content '{}' under parent slot {} (index {}) has no client slot -- dropped",
                             subItem->name, parentSlot, slotIndex);
                }
            }
        }

        // Move to next sub-item
        pos = subItemEnd + 1;  // Skip the pipe
        slotIndex++;
    }
}

} // namespace inventory
} // namespace eqt
