#include "client/graphics/ui/inventory_manager.h"
#include "common/net/packet.h"
#include <iostream>
#include "common/logging.h"
#include <sstream>
#include <charconv>
#include <algorithm>
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
    LOG_INFO(MOD_UI, "TitaniumItemParser BEFORE QUOTE (first 80 chars): '{}' (quote at pos {})", instancePreview, firstQuote);

    auto instanceFields = splitByPipe(instanceSection);

    if (!parseInstanceData(instanceFields, *item, slotId)) {
        LOG_DEBUG(MOD_UI, "Failed to parse instance data");
        return nullptr;
    }

    // Parse static data (between quotes)
    std::string_view staticSection(data.data() + firstQuote + 1, secondQuote - firstQuote - 1);

    // Debug: log raw static section (first 100 chars)
    std::string rawPreview(staticSection.substr(0, std::min(size_t(100), staticSection.size())));
    LOG_INFO(MOD_UI, "TitaniumItemParser RAW STATIC (first 100 chars): '{}'", rawPreview);

    if (!parseStaticData(staticSection, *item)) {
        LOG_DEBUG(MOD_UI, "Failed to parse static data");
        return nullptr;
    }

    // Parse sub-items (bag contents) if this is a container and caller wants them
    if (subItems && item->isContainer() && secondQuote + 1 < data.size()) {
        parseSubItems(std::string_view(data), secondQuote + 1, slotId, *subItems);
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
    LOG_INFO(MOD_UI, "TitaniumItemParser INSTANCE: slot={} stackCount={} count={} price={} charges={} attuned={} [fields: 0='{}' 1='{}' 2='{}' 3='{}' 4='{}' 5='{}' 6='{}' 7='{}' 8='{}' 9='{}' 10='{}']",
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
    LOG_INFO(MOD_UI, "TitaniumItemParser STATIC: '{}' itemId={} noRent={} noDrop={} (field[6]='{}' field[7]='{}')",
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

    // Book (100-102)
    item.bookType = static_cast<uint32_t>(toInt(fields[100]));
    // fields[101] = BookType (alternative/legacy?)
    if (fields.size() > 102 && !fields[102].empty()) {
        item.bookText = std::string(fields[102]);
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
    LOG_INFO(MOD_UI, "TitaniumItemParser FINAL: '{}' noDrop={} (field[7]={}, field[132]/NoTransfer={}, field[124]/Attuneable='{}')",
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

                if (subItem) {
                    LOG_DEBUG(MOD_UI, "TitaniumItemParser Parsed bag content: {} in bag slot {} (index {}) qty={} stackable={}",
                              subItem->name, bagSlotId, slotIndex, subItem->quantity, subItem->stackable);
                    subItems[bagSlotId] = std::move(subItem);
                }
            }
        }

        // Move to next sub-item
        pos = subItemEnd + 1;  // Skip the pipe
        slotIndex++;
    }
}

// ============================================================================
// InventoryManager Implementation
// ============================================================================

InventoryManager::InventoryManager() = default;
InventoryManager::~InventoryManager() = default;

void InventoryManager::setPlayerInfo(uint32_t race, uint32_t classId, uint8_t level) {
    playerRace_ = race;
    playerClass_ = classId;
    playerLevel_ = level;
}

const ItemInstance* InventoryManager::getItem(int16_t slotId) const {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
    }
    auto it = items_.find(slotId);
    if (it != items_.end()) {
        return it->second.get();
    }
    return nullptr;
}

ItemInstance* InventoryManager::getItemMutable(int16_t slotId) {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
    }
    auto it = items_.find(slotId);
    if (it != items_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool InventoryManager::hasItem(int16_t slotId) const {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        return !cursorQueue_.empty();
    }
    return items_.find(slotId) != items_.end();
}

const ItemInstance* InventoryManager::getItemById(uint32_t itemId) const {
    // First check the ID cache
    auto it = itemCacheById_.find(itemId);
    if (it != itemCacheById_.end()) {
        return it->second.get();
    }

    // Fall back to searching inventory slots (slower but covers any edge cases)
    for (const auto& [slotId, item] : items_) {
        if (item && item->itemId == itemId) {
            return item.get();
        }
    }

    // Check cursor queue
    for (const auto& item : cursorQueue_) {
        if (item && item->itemId == itemId) {
            return item.get();
        }
    }

    return nullptr;
}

int16_t InventoryManager::findItemSlotByItemId(uint32_t itemId) const {
    // Search equipment slots first (0-21) as these are typically clickable items
    for (int16_t slot = 0; slot < EQUIPMENT_COUNT; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    // Search general inventory slots (22-29)
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    // Search bag contents (251-330)
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; slot++) {
        auto it = items_.find(slot);
        if (it != items_.end() && it->second && it->second->itemId == itemId) {
            return slot;
        }
    }

    return SLOT_INVALID;
}

bool InventoryManager::addQuantityToExistingStack(uint32_t itemId, int32_t quantity) {
    // Search inventory slots for a stackable item with this ID
    for (auto& [slotId, item] : items_) {
        if (item && item->itemId == itemId && item->stackable) {
            int32_t newQuantity = item->quantity + quantity;
            // Clamp to stack size if defined
            if (item->stackSize > 0) {
                newQuantity = std::min(newQuantity, static_cast<int32_t>(item->stackSize));
            }
            item->quantity = newQuantity;
            LOG_DEBUG(MOD_UI, "Updated stack quantity for item {} in slot {}: {} -> {}",
                     item->name, slotId, item->quantity - quantity, item->quantity);
            return true;
        }
    }
    return false;
}

void InventoryManager::cacheItemById(const ItemInstance& item) {
    if (item.itemId == 0) {
        return;  // Don't cache items with no ID
    }

    // Check if already cached
    if (itemCacheById_.find(item.itemId) != itemCacheById_.end()) {
        return;  // Already have this item cached
    }

    // Evict old entries if cache is full
    if (itemCacheById_.size() >= MAX_ITEM_CACHE_SIZE) {
        // Simple eviction: remove the first entry (oldest by map order)
        itemCacheById_.erase(itemCacheById_.begin());
    }

    // Cache a copy of the item
    itemCacheById_[item.itemId] = std::make_unique<ItemInstance>(item);
}

void InventoryManager::setItem(int16_t slotId, const ItemInstance& item) {
    // Cache the item by ID for item link lookups
    cacheItemById(item);

    auto newItem = std::make_unique<ItemInstance>(item);
    newItem->currentSlot = slotId;

    // Special handling for cursor slot - server uses 33, client uses 30
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        newItem->currentSlot = CURSOR_SLOT;  // Normalize to client cursor slot
        cursorQueue_.push_back(std::move(newItem));
        LOG_DEBUG(MOD_UI, "InventoryManager Item added to cursor queue: {} (queue size: {})", cursorQueue_.back()->name, cursorQueue_.size());
        // If this is the first item, set source to CURSOR_SLOT (came from server)
        if (cursorQueue_.size() == 1) {
            cursorSourceSlot_ = CURSOR_SLOT;
        }
    } else {
        items_[slotId] = std::move(newItem);
    }
}

void InventoryManager::setItem(int16_t slotId, std::unique_ptr<ItemInstance> item) {
    if (item) {
        // Cache the item by ID for item link lookups (before moving)
        cacheItemById(*item);

        item->currentSlot = slotId;

        // Special handling for cursor slot - server uses 33, client uses 30
        if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
            item->currentSlot = CURSOR_SLOT;  // Normalize to client cursor slot
            cursorQueue_.push_back(std::move(item));
            LOG_DEBUG(MOD_UI, "InventoryManager Item added to cursor queue: {} (queue size: {})", cursorQueue_.back()->name, cursorQueue_.size());
            // If this is the first item, set source to CURSOR_SLOT (came from server)
            if (cursorQueue_.size() == 1) {
                cursorSourceSlot_ = CURSOR_SLOT;
            }
        } else {
            // Check if we're adding to an existing stackable item
            auto existingIt = items_.find(slotId);
            if (existingIt != items_.end() && existingIt->second &&
                existingIt->second->stackable && item->stackable &&
                existingIt->second->itemId == item->itemId && item->itemId != 0) {
                // Same stackable item - add to existing quantity
                int32_t oldQty = existingIt->second->quantity;
                int32_t addQty = item->quantity;
                int32_t newQty = oldQty + addQty;
                // Clamp to stack size if defined
                if (existingIt->second->stackSize > 0) {
                    newQty = std::min(newQty, static_cast<int32_t>(existingIt->second->stackSize));
                }
                existingIt->second->quantity = newQty;
                LOG_DEBUG(MOD_UI, "InventoryManager Stack update: {} in slot {} qty {} + {} = {}",
                         existingIt->second->name, slotId, oldQty, addQty, newQty);
            } else {
                // New item or different item - replace
                items_[slotId] = std::move(item);
            }
        }
    }
}

void InventoryManager::removeItem(int16_t slotId) {
    // Handle cursor slot specially
    if (slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT) {
        if (!cursorQueue_.empty()) {
            cursorQueue_.pop_front();
            // If queue is now empty, reset source slot
            if (cursorQueue_.empty()) {
                cursorSourceSlot_ = SLOT_INVALID;
            } else {
                // Next item in queue becomes active, it came from server
                cursorSourceSlot_ = CURSOR_SLOT;
            }
        }
    } else {
        items_.erase(slotId);
    }
}

void InventoryManager::clearAll() {
    items_.clear();
    cursorQueue_.clear();
    cursorSourceSlot_ = SLOT_INVALID;
    itemCacheById_.clear();
}

void InventoryManager::clearTradeSlots() {
    // Clear items in trade slots (3000-3007)
    for (int16_t slot = TRADE_BEGIN; slot <= TRADE_END; slot++) {
        items_.erase(slot);
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Cleared trade slots {}-{}", TRADE_BEGIN, TRADE_END);
}

void InventoryManager::clearBankSlots() {
    // Clear items in main bank slots (2000-2015) and bank bag slots (2031-2190)
    for (int16_t slot = BANK_BEGIN; slot <= BANK_END; slot++) {
        items_.erase(slot);
    }
    for (int16_t slot = BANK_BAGS_BEGIN; slot <= BANK_BAGS_END; slot++) {
        items_.erase(slot);
    }

    // Clear shared bank slots (2500-2501) and shared bank bag slots (2531-2550)
    for (int16_t slot = SHARED_BANK_BEGIN; slot <= SHARED_BANK_END; slot++) {
        items_.erase(slot);
    }
    for (int16_t slot = SHARED_BANK_BAGS_BEGIN; slot <= SHARED_BANK_BAGS_END; slot++) {
        items_.erase(slot);
    }

    LOG_DEBUG(MOD_UI, "InventoryManager Cleared bank slots");
}

void InventoryManager::clearWorldContainerSlots() {
    // Clear items in world container slots (4000-4009)
    for (int16_t slot = WORLD_BEGIN; slot <= WORLD_END; slot++) {
        items_.erase(slot);
    }
    LOG_DEBUG(MOD_UI, "InventoryManager Cleared world container slots {}-{}", WORLD_BEGIN, WORLD_END);
}

bool InventoryManager::pickupItem(int16_t slotId) {
    // Can't pick up if already holding something on cursor
    if (!cursorQueue_.empty()) {
        return false;
    }

    auto it = items_.find(slotId);
    if (it == items_.end()) {
        return false;  // No item at this slot
    }

    // Move item to cursor (push to front since this becomes active)
    LOG_DEBUG(MOD_UI, "InventoryManager Picked up '{}' from slot {}", it->second->name, slotId);
    cursorQueue_.push_front(std::move(it->second));
    cursorSourceSlot_ = slotId;
    items_.erase(it);

    // Notify server of pickup (move from slot to cursor)
    // quantity = 0 means move entire item/stack
    if (moveItemCallback_) {
        moveItemCallback_(slotId, CURSOR_SLOT, 0);
    }

    // Notify equipment change callback if picking up from equipment slot
    if (isEquipmentSlot(slotId) && equipmentChangedCallback_) {
        equipmentChangedCallback_(slotId);
    }

    return true;
}

bool InventoryManager::placeItem(int16_t targetSlot) {
    if (cursorQueue_.empty()) {
        return false;  // Nothing to place
    }

    // Get the front (active) cursor item
    auto& cursorItem = cursorQueue_.front();

    // Check if placement is valid
    if (!canPlaceItemInSlot(cursorItem.get(), targetSlot)) {
        return false;
    }

    auto it = items_.find(targetSlot);
    if (it != items_.end()) {
        // Slot is occupied - swap items
        auto targetItem = std::move(it->second);

        // Check if we can swap (target item must be able to go where cursor came from)
        if (cursorSourceSlot_ != SLOT_INVALID && cursorSourceSlot_ != CURSOR_SLOT &&
            !canPlaceItemInSlot(targetItem.get(), cursorSourceSlot_)) {
            // Can't swap - restore
            it->second = std::move(targetItem);
            return false;
        }

        // Save info about items being swapped (before we move them)
        uint32_t placedQuantity = cursorItem->quantity;
        bool cursorItemIsBag = cursorItem->isContainer();

        // Place cursor item in target slot
        cursorItem->currentSlot = targetSlot;
        items_[targetSlot] = std::move(cursorItem);

        // Pop the placed item from queue
        cursorQueue_.pop_front();

        // Relocate bag contents if the cursor item was a bag
        if (cursorItemIsBag && isGeneralSlot(cursorSourceSlot_) && isGeneralSlot(targetSlot)) {
            relocateBagContents(cursorSourceSlot_, targetSlot);
        }

        // Put target item on cursor (push to front - it becomes the new active item)
        cursorQueue_.push_front(std::move(targetItem));
        // cursorSourceSlot_ stays the same (original pickup location)

        // Note: If target item was a bag, its contents stay in targetSlot's bag range
        // until the user places the cursor item (the bag) somewhere else

        // Notify server of swap - use CURSOR_SLOT as source since item is on cursor
        // quantity = 0 means move entire item/stack
        LOG_DEBUG(MOD_UI, "InventoryManager Swapped items: cursor(from {}) <-> {}", cursorSourceSlot_, targetSlot);
        if (moveItemCallback_) {
            moveItemCallback_(CURSOR_SLOT, targetSlot, 0);
        }

        // Notify equipment change callback if target is an equipment slot
        if (isEquipmentSlot(targetSlot) && equipmentChangedCallback_) {
            equipmentChangedCallback_(targetSlot);
        }
    } else {
        // Slot is empty - just place
        LOG_DEBUG(MOD_UI, "InventoryManager Placed '{}' in slot {}", cursorItem->name, targetSlot);
        bool cursorItemIsBag = cursorItem->isContainer();
        int16_t sourceSlot = cursorSourceSlot_;

        cursorItem->currentSlot = targetSlot;
        items_[targetSlot] = std::move(cursorItem);

        // Pop the placed item from queue
        cursorQueue_.pop_front();

        // Relocate bag contents if we moved a bag
        if (cursorItemIsBag && isGeneralSlot(sourceSlot) && isGeneralSlot(targetSlot)) {
            relocateBagContents(sourceSlot, targetSlot);
        }

        // Notify server - use CURSOR_SLOT as source since item is on cursor
        // quantity = 0 means move entire item/stack
        if (moveItemCallback_) {
            moveItemCallback_(CURSOR_SLOT, targetSlot, 0);
        }

        // If queue still has items, send pop notification to server
        if (!cursorQueue_.empty()) {
            LOG_DEBUG(MOD_UI, "InventoryManager Cursor queue has {} more items, sending pop notification", cursorQueue_.size());
            if (moveItemCallback_) {
                // Send cursor-to-cursor move to pop the next item on server
                moveItemCallback_(CURSOR_SLOT, CURSOR_SLOT, 0);
            }
            cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
        } else {
            cursorSourceSlot_ = SLOT_INVALID;
        }

        // Notify equipment change callback if target is an equipment slot
        if (isEquipmentSlot(targetSlot) && equipmentChangedCallback_) {
            equipmentChangedCallback_(targetSlot);
        }
    }

    return true;
}

bool InventoryManager::pickupPartialStack(int16_t slotId, int32_t quantity) {
    // Can't pick up if already holding something on cursor
    if (!cursorQueue_.empty()) {
        return false;
    }

    auto it = items_.find(slotId);
    if (it == items_.end()) {
        return false;  // No item at this slot
    }

    auto& sourceItem = it->second;
    if (!sourceItem->stackable || sourceItem->quantity <= 1) {
        // Not stackable or only 1 item - just pick up the whole thing
        return pickupItem(slotId);
    }

    // Clamp quantity to available amount
    quantity = std::min(quantity, sourceItem->quantity);
    if (quantity <= 0) {
        return false;
    }

    if (quantity >= sourceItem->quantity) {
        // Picking up entire stack - use normal pickup
        return pickupItem(slotId);
    }

    // Split the stack: create a new item on cursor with requested quantity
    auto cursorItem = std::make_unique<ItemInstance>(*sourceItem);
    cursorItem->quantity = quantity;
    cursorItem->currentSlot = CURSOR_SLOT;

    // Reduce source stack quantity
    sourceItem->quantity -= quantity;

    // Move new item to cursor
    LOG_DEBUG(MOD_UI, "InventoryManager Picked up {} of '{}' from slot {} (leaving {})",
              quantity, sourceItem->name, slotId, sourceItem->quantity);
    cursorQueue_.push_front(std::move(cursorItem));
    cursorSourceSlot_ = slotId;

    // Notify server of partial pickup
    if (moveItemCallback_) {
        moveItemCallback_(slotId, CURSOR_SLOT, quantity);
    }

    return true;
}

bool InventoryManager::placeOnMatchingStack(int16_t targetSlot) {
    if (cursorQueue_.empty()) {
        return false;  // Nothing to place
    }

    auto& cursorItem = cursorQueue_.front();
    auto it = items_.find(targetSlot);
    if (it == items_.end()) {
        // No item at target - just place normally
        return placeItem(targetSlot);
    }

    auto& targetItem = it->second;

    // Check if items can stack
    if (!cursorItem->stackable || !targetItem->stackable ||
        cursorItem->itemId != targetItem->itemId) {
        // Can't stack - do normal swap
        return placeItem(targetSlot);
    }

    // Check stack size limits
    int32_t targetMax = targetItem->stackSize;  // stackSize of 0 means not stackable
    int32_t availableSpace = targetMax - targetItem->quantity;

    if (availableSpace <= 0) {
        // Target stack is full - do normal swap
        return placeItem(targetSlot);
    }

    // Calculate how many items to transfer
    int32_t toTransfer = std::min(cursorItem->quantity, availableSpace);

    // Update quantities
    targetItem->quantity += toTransfer;
    cursorItem->quantity -= toTransfer;

    LOG_DEBUG(MOD_UI, "InventoryManager Stacked {} items onto slot {} (now {} total)",
              toTransfer, targetSlot, targetItem->quantity);

    // Notify server of stack operation
    if (moveItemCallback_) {
        moveItemCallback_(CURSOR_SLOT, targetSlot, toTransfer);
    }

    // If cursor item is now empty, remove it from cursor
    if (cursorItem->quantity <= 0) {
        cursorQueue_.pop_front();
        if (cursorQueue_.empty()) {
            cursorSourceSlot_ = SLOT_INVALID;
        } else {
            cursorSourceSlot_ = CURSOR_SLOT;  // Next item from server queue
        }
    }

    return true;
}

bool InventoryManager::hasCursorItem() const {
    return !cursorQueue_.empty();
}

const ItemInstance* InventoryManager::getCursorItem() const {
    return cursorQueue_.empty() ? nullptr : cursorQueue_.front().get();
}

void InventoryManager::returnCursorItem() {
    if (cursorQueue_.empty()) {
        return;
    }

    // Can only return if the item was picked up locally (not from server queue)
    if (cursorSourceSlot_ != SLOT_INVALID && cursorSourceSlot_ != CURSOR_SLOT) {
        // Return to original slot
        auto& cursorItem = cursorQueue_.front();
        cursorItem->currentSlot = cursorSourceSlot_;
        items_[cursorSourceSlot_] = std::move(cursorItem);
        cursorQueue_.pop_front();

        // Update source slot for next item (if any)
        if (cursorQueue_.empty()) {
            cursorSourceSlot_ = SLOT_INVALID;
        } else {
            cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
        }
    }
    // If item is from server queue (cursorSourceSlot_ == CURSOR_SLOT), don't return it
}

void InventoryManager::popCursorItem() {
    if (cursorQueue_.empty()) {
        return;
    }

    cursorQueue_.pop_front();

    // If queue still has items, send pop notification to server
    if (!cursorQueue_.empty()) {
        LOG_DEBUG(MOD_UI, "InventoryManager popCursorItem: {} items remaining in cursor queue", cursorQueue_.size());
        if (moveItemCallback_) {
            // Send cursor-to-cursor move to pop the next item on server
            moveItemCallback_(CURSOR_SLOT, CURSOR_SLOT, 0);
        }
        cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
    } else {
        cursorSourceSlot_ = SLOT_INVALID;
    }
}

size_t InventoryManager::getCursorQueueSize() const {
    return cursorQueue_.size();
}

void InventoryManager::pickupMoney(CursorMoneyType type, uint32_t amount) {
    if (amount == 0 || type == CursorMoneyType::None) {
        return;
    }
    cursorMoneyType_ = type;
    cursorMoneyAmount_ = amount;
    LOG_DEBUG(MOD_UI, "InventoryManager picked up {} of money type {}",
              amount, static_cast<int>(type));
}

void InventoryManager::clearCursorMoney() {
    cursorMoneyType_ = CursorMoneyType::None;
    cursorMoneyAmount_ = 0;
}

void InventoryManager::returnCursorMoney() {
    // Money is returned by simply clearing it - the callback to update
    // player money should be handled by WindowManager
    clearCursorMoney();
}

void InventoryManager::setSpellForScribe(uint32_t spellId, int16_t sourceSlot) {
    scribeSpellId_ = spellId;
    scribeSourceSlot_ = sourceSlot;
    LOG_DEBUG(MOD_UI, "InventoryManager: Set spell {} from slot {} for scribing",
              spellId, sourceSlot);

    // Move the scroll to cursor slot - this sends MoveItem to server
    // The server expects the scroll to be on cursor when we send OP_MemorizeSpell
    if (!pickupItem(sourceSlot)) {
        LOG_WARN(MOD_UI, "InventoryManager: Failed to pick up scroll from slot {} for scribing",
                 sourceSlot);
    }
}

void InventoryManager::clearSpellForScribe() {
    if (scribeSpellId_ != 0) {
        LOG_DEBUG(MOD_UI, "InventoryManager: Cleared spell scribing state (was spell {})",
                  scribeSpellId_);
    }
    scribeSpellId_ = 0;
    scribeSourceSlot_ = SLOT_INVALID;
}

bool InventoryManager::swapItems(int16_t fromSlot, int16_t toSlot) {
    auto fromIt = items_.find(fromSlot);
    auto toIt = items_.find(toSlot);

    if (fromIt == items_.end()) {
        return false;  // No item at source
    }

    // Check if from item can go to target
    if (!canPlaceItemInSlot(fromIt->second.get(), toSlot)) {
        return false;
    }

    if (toIt != items_.end()) {
        // Check if target item can go to source
        if (!canPlaceItemInSlot(toIt->second.get(), fromSlot)) {
            return false;
        }

        // Swap
        std::swap(fromIt->second, toIt->second);
        fromIt->second->currentSlot = fromSlot;
        toIt->second->currentSlot = toSlot;
    } else {
        // Just move
        auto item = std::move(fromIt->second);
        item->currentSlot = toSlot;
        items_.erase(fromIt);
        items_[toSlot] = std::move(item);
    }

    // Notify server - quantity = 0 means move entire item/stack
    if (moveItemCallback_) {
        moveItemCallback_(fromSlot, toSlot, 0);
    }

    return true;
}

bool InventoryManager::canPlaceItemInSlot(const ItemInstance* item, int16_t targetSlot) const {
    if (!item) {
        return false;
    }

    // Check for lore item conflicts (can't have duplicate lore items)
    // Exclude cursor source slot since we're moving from there
    if (hasLoreConflict(item, cursorSourceSlot_)) {
        return false;
    }

    // Equipment slots have restrictions
    if (isEquipmentSlot(targetSlot)) {
        return canEquipItem(item, targetSlot);
    }

    // General inventory slots - anything goes
    if (isGeneralSlot(targetSlot)) {
        return true;
    }

    // Bag slots - check container constraints
    if (isBagSlot(targetSlot)) {
        return canPlaceInBag(item, targetSlot);
    }

    // Bank slots (main and shared, plus bag slots within)
    if (isBankSlot(targetSlot) || isSharedBankSlot(targetSlot) ||
        isBankBagSlot(targetSlot) || isSharedBankBagSlot(targetSlot)) {
        return canPlaceInBankSlot(item, targetSlot);
    }

    // Trade slots - NPC trades allow all items, player trades restrict NO_DROP
    if (isTradeSlot(targetSlot)) {
        // NPC trades (quests) allow all item types including NO_DROP
        if (isNpcTrade_) {
            return true;
        }
        return !item->noDrop;  // Only non-nodrop items can be traded to players
    }

    // Trade bag slots - check container constraints and tradeability
    if (isTradeBagSlot(targetSlot)) {
        // NPC trades allow all items in bags too
        if (isNpcTrade_) {
            if (item->isContainer()) {
                return false;  // Still can't put containers in containers
            }
            const ItemInstance* container = getContainerAtSlot(targetSlot);
            if (!container) {
                return false;
            }
            return item->canFitInContainer(container->bagSize);
        }
        if (item->noDrop || item->isContainer()) {
            return false;
        }
        const ItemInstance* container = getContainerAtSlot(targetSlot);
        if (!container) {
            return false;
        }
        return item->canFitInContainer(container->bagSize);
    }

    // World container slots (tradeskill containers like forges, looms)
    if (isWorldContainerItemSlot(targetSlot)) {
        return canPlaceInWorldContainer(item, targetSlot);
    }

    // Cursor slot
    if (isCursorSlot(targetSlot)) {
        return true;
    }

    return false;
}

bool InventoryManager::canEquipItem(const ItemInstance* item, int16_t equipSlot) const {
    if (!item) {
        return false;
    }

    // Check if item can go in this slot type
    if (!item->canEquipInSlot(equipSlot)) {
        LOG_DEBUG(MOD_UI, "canEquipItem: '{}' cannot go in slot {} (item slots bitmask=0x{:08X})",
                  item->name, equipSlot, item->slots);
        return false;
    }

    // Check player restrictions
    if (!meetsRestrictions(item)) {
        LOG_DEBUG(MOD_UI, "canEquipItem: '{}' fails restrictions (playerClass={}, playerRace={}, playerLevel={}, "
                  "itemClasses=0x{:X}, itemRaces=0x{:X}, itemReqLevel={})",
                  item->name, playerClass_, playerRace_, playerLevel_,
                  item->classes, item->races, item->reqLevel);
        return false;
    }

    return true;
}

bool InventoryManager::canPlaceInBag(const ItemInstance* item, int16_t bagSlot) const {
    if (!item) {
        return false;
    }

    // Can't put bags in bags
    if (item->isContainer()) {
        return false;
    }

    // Get the bag containing this slot
    const ItemInstance* bag = getBagAtSlot(bagSlot);
    if (!bag) {
        return false;  // No bag at this location
    }

    // Check size constraint
    if (!item->canFitInContainer(bag->bagSize)) {
        return false;
    }

    return true;
}

bool InventoryManager::canPlaceInWorldContainer(const ItemInstance* item, int16_t slot) const {
    if (!item) {
        return false;
    }

    // Can't put containers in world containers (no nested containers)
    if (item->isContainer()) {
        LOG_DEBUG(MOD_UI, "Cannot place container in world container slot {}", slot);
        return false;
    }

    // Can't put no-drop items in world containers (they're shared with the world)
    if (item->noDrop) {
        LOG_DEBUG(MOD_UI, "Cannot place no-drop item '{}' in world container slot {}", item->name, slot);
        return false;
    }

    // World containers don't have size restrictions like bags do
    // The server will validate if the item is appropriate for the tradeskill

    return true;
}

bool InventoryManager::meetsRestrictions(const ItemInstance* item) const {
    if (!item) {
        return false;
    }

    return item->isUsableBy(playerClass_, playerRace_, playerLevel_);
}

bool InventoryManager::hasLoreConflict(const ItemInstance* item, int16_t excludeSlot) const {
    if (!item || !item->loreItem) {
        return false;  // Not a lore item, no conflict
    }

    // Check all inventory slots for a matching lore item
    for (const auto& [slotId, existingItem] : items_) {
        if (slotId == excludeSlot) {
            continue;  // Skip the slot we're moving from
        }

        if (!existingItem || !existingItem->loreItem) {
            continue;
        }

        // Check for lore conflict:
        // - If both items have a lore group, they conflict if groups match
        // - If either has no lore group (0), they conflict if item IDs match
        if (item->loreGroup != 0 && existingItem->loreGroup != 0) {
            if (item->loreGroup == existingItem->loreGroup) {
                return true;  // Same lore group = conflict
            }
        } else {
            // No lore group, check by item ID
            if (item->itemId == existingItem->itemId) {
                return true;  // Same item ID = conflict
            }
        }
    }

    // Also check cursor item if we're placing into inventory
    if (!cursorQueue_.empty()) {
        const auto& cursorItem = cursorQueue_.front();
        if (cursorItem.get() != item && cursorItem->loreItem) {
            if (item->loreGroup != 0 && cursorItem->loreGroup != 0) {
                if (item->loreGroup == cursorItem->loreGroup) {
                    return true;
                }
            } else if (item->itemId == cursorItem->itemId) {
                return true;
            }
        }
    }

    return false;
}

bool InventoryManager::isBag(int16_t slotId) const {
    const ItemInstance* item = getItem(slotId);
    return item && item->isContainer();
}

int InventoryManager::getBagSize(int16_t slotId) const {
    const ItemInstance* item = getItem(slotId);
    if (item && item->isContainer()) {
        return item->bagSlots;
    }
    return 0;
}

std::vector<int16_t> InventoryManager::getBagContentsSlots(int16_t generalSlot) const {
    std::vector<int16_t> slots;

    if (!isGeneralSlot(generalSlot) && generalSlot != CURSOR_SLOT) {
        return slots;
    }

    const ItemInstance* bag = getItem(generalSlot);
    if (!bag || !bag->isContainer()) {
        return slots;
    }

    int bagSize = bag->bagSlots;
    for (int i = 0; i < bagSize; i++) {
        int16_t bagSlot;
        if (generalSlot == CURSOR_SLOT) {
            bagSlot = calcCursorBagSlotId(i);
        } else {
            bagSlot = calcBagSlotId(generalSlot, i);
        }
        if (bagSlot != SLOT_INVALID) {
            slots.push_back(bagSlot);
        }
    }

    return slots;
}

bool InventoryManager::isBagEmpty(int16_t generalSlot) const {
    auto slots = getBagContentsSlots(generalSlot);
    for (int16_t slot : slots) {
        if (hasItem(slot)) {
            return false;
        }
    }
    return true;
}

void InventoryManager::relocateBagContents(int16_t fromGeneralSlot, int16_t toGeneralSlot) {
    // Move bag contents from one general slot's bag range to another
    // This is done locally only - server handles its own bag content relocation

    if (!isGeneralSlot(fromGeneralSlot) || !isGeneralSlot(toGeneralSlot)) {
        return;
    }

    // Get the bag to determine how many slots it has
    const ItemInstance* bag = getItem(toGeneralSlot);  // Bag is already in new slot
    if (!bag || !bag->isContainer()) {
        return;
    }

    int bagSize = bag->bagSlots;
    LOG_DEBUG(MOD_UI, "InventoryManager Relocating {} bag slots from general slot {} to {}", bagSize, fromGeneralSlot, toGeneralSlot);

    // Move contents from old slot range to new slot range
    for (int i = 0; i < bagSize; i++) {
        int16_t oldBagSlot = calcBagSlotId(fromGeneralSlot, i);
        int16_t newBagSlot = calcBagSlotId(toGeneralSlot, i);

        auto it = items_.find(oldBagSlot);
        if (it != items_.end()) {
            LOG_DEBUG(MOD_UI, "InventoryManager Moving bag content '{}' from slot {} to {}", it->second->name, oldBagSlot, newBagSlot);
            it->second->currentSlot = newBagSlot;
            items_[newBagSlot] = std::move(it->second);
            items_.erase(it);
        }
    }
}

bool InventoryManager::isBankBag(int16_t slotId) const {
    // Check if slot is a bank slot (main or shared) containing a container
    if (!isBankSlot(slotId) && !isSharedBankSlot(slotId)) {
        return false;
    }
    const ItemInstance* item = getItem(slotId);
    return item && item->isContainer();
}

int InventoryManager::getBankBagSize(int16_t slotId) const {
    if (!isBankSlot(slotId) && !isSharedBankSlot(slotId)) {
        return 0;
    }
    const ItemInstance* item = getItem(slotId);
    if (item && item->isContainer()) {
        return item->bagSlots;
    }
    return 0;
}

std::vector<int16_t> InventoryManager::getBankBagContentsSlots(int16_t bankSlot) const {
    std::vector<int16_t> slots;

    // Must be a bank or shared bank slot
    if (!isBankSlot(bankSlot) && !isSharedBankSlot(bankSlot)) {
        return slots;
    }

    const ItemInstance* bag = getItem(bankSlot);
    if (!bag || !bag->isContainer()) {
        return slots;
    }

    int bagSize = bag->bagSlots;
    for (int i = 0; i < bagSize; i++) {
        int16_t bagSlot = calcContainerSlotId(bankSlot, i);
        if (bagSlot != SLOT_INVALID) {
            slots.push_back(bagSlot);
        }
    }

    return slots;
}

bool InventoryManager::isBankBagEmpty(int16_t bankSlot) const {
    auto slots = getBankBagContentsSlots(bankSlot);
    for (int16_t slot : slots) {
        if (hasItem(slot)) {
            return false;
        }
    }
    return true;
}

bool InventoryManager::canPlaceInBankSlot(const ItemInstance* item, int16_t targetSlot) const {
    if (!item) {
        return false;
    }

    // Check if this is a main bank slot
    if (isBankSlot(targetSlot)) {
        return true;  // Any item can go in main bank slots
    }

    // Check if this is a shared bank slot
    if (isSharedBankSlot(targetSlot)) {
        // Only non-nodrop items can go in shared bank (account-wide storage)
        return !item->noDrop;
    }

    // Check if this is a bank bag slot
    if (isBankBagSlot(targetSlot) || isSharedBankBagSlot(targetSlot)) {
        // Can't put bags in bags
        if (item->isContainer()) {
            return false;
        }

        // Get the parent bank slot to find the container
        const ItemInstance* container = getContainerAtSlot(targetSlot);
        if (!container) {
            return false;  // No container at this location
        }

        // Check size constraint
        if (!item->canFitInContainer(container->bagSize)) {
            return false;
        }

        // For shared bank bag slots, check nodrop
        if (isSharedBankBagSlot(targetSlot) && item->noDrop) {
            return false;
        }

        return true;
    }

    return false;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getBankItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    // Main bank slots (2000-2015)
    for (int16_t slot = BANK_BEGIN; slot <= BANK_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    // Bank bag contents (2031-2190)
    for (int16_t slot = BANK_BAGS_BEGIN; slot <= BANK_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getSharedBankItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    // Shared bank slots (2500-2501)
    for (int16_t slot = SHARED_BANK_BEGIN; slot <= SHARED_BANK_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    // Shared bank bag contents (2531-2550)
    for (int16_t slot = SHARED_BANK_BAGS_BEGIN; slot <= SHARED_BANK_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }

    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getEquipmentItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }
    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getGeneralItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            result.emplace_back(slot, item);
        }
    }
    return result;
}

std::vector<std::pair<int16_t, const ItemInstance*>> InventoryManager::getSellableItems() const {
    std::vector<std::pair<int16_t, const ItemInstance*>> result;

    LOG_DEBUG(MOD_UI, "getSellableItems: Checking inventory for sellable items (items_.size={})", items_.size());

    // Check general inventory slots (22-29) for non-container items
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  General slot {}: '{}' container={} noDrop={}",
                slot, item->name, item->isContainer(), item->noDrop);
            if (!item->isContainer() && !item->noDrop) {
                result.emplace_back(slot, item);
            }
        }
    }

    // Check bag contents (251-330)
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  Bag slot {}: '{}' noDrop={}", slot, item->name, item->noDrop);
            if (!item->noDrop) {
                result.emplace_back(slot, item);
            }
        }
    }

    LOG_DEBUG(MOD_UI, "getSellableItems: Found {} sellable items", result.size());
    return result;
}

EquipmentStats InventoryManager::calculateEquipmentStats() const {
    EquipmentStats stats;

    // Sum stats from all equipped items
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (!item) continue;

        stats.ac += item->ac;
        stats.atk += item->attack;
        stats.hp += item->hp;
        stats.mana += item->mana;
        stats.endurance += item->endurance;

        stats.str += item->str;
        stats.sta += item->sta;
        stats.agi += item->agi;
        stats.dex += item->dex;
        stats.wis += item->wis;
        stats.int_ += item->int_;
        stats.cha += item->cha;

        stats.magicResist += item->magicResist;
        stats.fireResist += item->fireResist;
        stats.coldResist += item->coldResist;
        stats.diseaseResist += item->diseaseResist;
        stats.poisonResist += item->poisonResist;

        stats.hpRegen += item->hpRegen;
        stats.manaRegen += item->manaRegen;

        // Haste: take highest value (doesn't stack)
        if (item->haste > stats.haste) {
            stats.haste = item->haste;
        }

        stats.weight += item->weight;
    }

    return stats;
}

float InventoryManager::calculateTotalWeight() const {
    float totalWeight = 0.0f;

    // Iterate over all items
    for (const auto& [slot, item] : items_) {
        if (!item) continue;

        float itemWeight = item->weight;

        // Apply bag weight reduction if this item is in a bag
        if (isBagSlot(slot)) {
            int16_t parentSlot = getParentGeneralSlot(slot);
            const ItemInstance* bag = getItem(parentSlot);
            if (bag && bag->bagWR > 0) {
                // Weight reduction is a percentage
                float reduction = static_cast<float>(bag->bagWR) / 100.0f;
                itemWeight *= (1.0f - reduction);
            }
        }

        totalWeight += itemWeight * item->quantity;
    }

    return totalWeight;
}

const ItemInstance* InventoryManager::getBagAtSlot(int16_t bagSlot) const {
    int16_t parentSlot = getParentGeneralSlot(bagSlot);
    if (parentSlot == SLOT_INVALID) {
        return nullptr;
    }
    return getItem(parentSlot);
}

bool InventoryManager::isValidSlot(int16_t slotId) const {
    return isEquipmentSlot(slotId) ||
           isGeneralSlot(slotId) ||
           isBagSlot(slotId) ||
           isCursorSlot(slotId) ||
           isBankSlot(slotId) ||
           isSharedBankSlot(slotId) ||
           isBankBagSlot(slotId) ||
           isSharedBankBagSlot(slotId) ||
           isTradeSlot(slotId) ||
           isTradeBagSlot(slotId);
}

const ItemInstance* InventoryManager::getContainerAtSlot(int16_t bagSlot) const {
    // Use the universal helper to get the parent container slot
    int16_t parentSlot = getParentContainerSlot(bagSlot);
    if (parentSlot == SLOT_INVALID) {
        return nullptr;
    }
    return getItem(parentSlot);
}

bool InventoryManager::destroyItem(int16_t slotId) {
    if (!hasItem(slotId)) {
        return false;
    }

    const ItemInstance* item = getItem(slotId);
    if (item && item->noDestroy) {
        return false;  // Can't destroy this item
    }

    // If it's a bag, check if empty
    if (item && item->isContainer()) {
        if (!isBagEmpty(slotId)) {
            return false;  // Can't destroy non-empty bag
        }
    }

    removeItem(slotId);

    // Notify server
    if (deleteItemCallback_) {
        deleteItemCallback_(slotId);
    }

    return true;
}

bool InventoryManager::destroyCursorItem() {
    if (cursorQueue_.empty()) {
        return false;
    }

    const auto& cursorItem = cursorQueue_.front();
    if (cursorItem->noDestroy) {
        return false;
    }

    LOG_DEBUG(MOD_UI, "InventoryManager destroyCursorItem: {} (cursorSourceSlot={})",
              cursorItem->name, cursorSourceSlot_);

    // If it's a bag, it should be empty (bags on cursor can't have contents)
    cursorQueue_.pop_front();

    // Update source slot for next item (if any)
    if (cursorQueue_.empty()) {
        cursorSourceSlot_ = SLOT_INVALID;
    } else {
        // If more items in queue, send pop notification
        if (moveItemCallback_) {
            moveItemCallback_(CURSOR_SLOT, CURSOR_SLOT, 0);
        }
        cursorSourceSlot_ = CURSOR_SLOT;  // Next item is from server queue
    }

    // Notify server - item is on CURSOR_SLOT, not the original source slot
    if (deleteItemCallback_) {
        deleteItemCallback_(CURSOR_SLOT);
    }

    return true;
}

void InventoryManager::processCharInventory(const EQ::Net::Packet& packet) {
    // OP_CharInventory contains multiple serialized items concatenated together
    // Each item is null-terminated
    LOG_DEBUG(MOD_UI, "InventoryManager Received CharInventory packet, size: {}", packet.Length());

    const char* data = reinterpret_cast<const char*>(packet.Data());
    size_t totalLen = packet.Length();
    size_t offset = 0;
    int itemCount = 0;
    int bagContentCount = 0;

    while (offset < totalLen) {
        // Find the end of this item (null terminator)
        size_t itemStart = offset;
        while (offset < totalLen && data[offset] != '\0') {
            offset++;
        }

        size_t itemLen = offset - itemStart;
        if (itemLen > 0) {
            // Parse this item and its bag contents
            int16_t slotId = SLOT_INVALID;
            std::map<int16_t, std::unique_ptr<ItemInstance>> bagContents;
            auto item = TitaniumItemParser::parseItem(data + itemStart, itemLen, slotId, &bagContents);

            if (item && slotId != SLOT_INVALID) {
                LOG_DEBUG(MOD_UI, "InventoryManager Parsed item: {} in slot {}", item->name, slotId);
                setItem(slotId, std::move(item));
                itemCount++;

                // Add any bag contents
                for (auto& [bagSlotId, bagItem] : bagContents) {
                    setItem(bagSlotId, std::move(bagItem));
                    bagContentCount++;
                }
            }
        }

        // Skip the null terminator
        offset++;
    }

    LOG_DEBUG(MOD_UI, "InventoryManager Loaded {} items and {} bag contents from CharInventory", itemCount, bagContentCount);
    // Note: Items at CURSOR_SLOT are automatically handled by setItem() which adds to cursorQueue_
}

void InventoryManager::processItemPacket(const EQ::Net::Packet& packet) {
    // OP_ItemPacket has a 4-byte header (ItemPacketType) followed by serialized item
    LOG_DEBUG(MOD_UI, "InventoryManager Received ItemPacket, size: {}", packet.Length());

    if (packet.Length() < 5) {
        LOG_DEBUG(MOD_UI, "ItemPacket too short");
        return;
    }

    // First 4 bytes are the packet type
    uint32_t packetType = packet.GetUInt32(0);

    // Rest is the serialized item (null-terminated)
    const char* itemData = reinterpret_cast<const char*>(packet.Data()) + 4;
    size_t itemLen = packet.Length() - 4;

    // Remove trailing null if present
    while (itemLen > 0 && itemData[itemLen - 1] == '\0') {
        itemLen--;
    }

    if (itemLen == 0) {
        LOG_DEBUG(MOD_UI, "ItemPacket has no item data");
        return;
    }

    int16_t slotId = SLOT_INVALID;
    auto item = TitaniumItemParser::parseItem(itemData, itemLen, slotId);

    if (item && slotId != SLOT_INVALID) {
        LOG_DEBUG(MOD_UI, "InventoryManager Parsed item from ItemPacket: {} in slot {} (type: {})", item->name, slotId, packetType);
        setItem(slotId, std::move(item));
    } else {
        LOG_DEBUG(MOD_UI, "Failed to parse item from ItemPacket");
    }
}

void InventoryManager::processMoveItemResponse(const EQ::Net::Packet& packet) {
    // Server sends back MoveItem_Struct to confirm the move or indicate server-side changes
    // MoveItem_Struct: from_slot(4), to_slot(4), number_in_stack(4) = 12 bytes
    // Packet: opcode(2) + data(12) = 14 bytes
    LOG_DEBUG(MOD_UI, "InventoryManager Received MoveItem response, size: {}", packet.Length());

    if (packet.Length() < 14) {
        LOG_WARN(MOD_UI, "MoveItem response too short: {} bytes", packet.Length());
        return;
    }

    // Parse the MoveItem struct (after 2-byte opcode)
    int32_t fromSlot = static_cast<int32_t>(packet.GetUInt32(2));
    int32_t toSlot = static_cast<int32_t>(packet.GetUInt32(6));
    uint32_t quantity = packet.GetUInt32(10);

    LOG_DEBUG(MOD_UI, "InventoryManager MoveItem: from={} to={} qty={}", fromSlot, toSlot, quantity);

    // Check for item deletion (to_slot == -1 / 0xFFFFFFFF)
    // This happens when:
    // 1. A spell scroll is consumed after successful scribing
    // 2. Tradeskill combine consumes ingredients
    // 3. Server explicitly deletes an item
    if (toSlot == -1) {
        int16_t slot = static_cast<int16_t>(fromSlot);
        LOG_INFO(MOD_UI, "Item deleted from slot {} via MoveItem (to=-1)", slot);

        // Check if this deletion is related to our pending scribe
        if (isHoldingSpellForScribe() && getScribeSourceSlot() == slot) {
            LOG_DEBUG(MOD_UI, "Scroll consumed after scribing from slot {}", slot);
            clearSpellForScribe();
        }

        // Remove the item from inventory
        removeItem(slot);
    }
    // Normal move operations are handled client-side
    // Server will send OP_ItemPacket if it needs to correct our state
}

void InventoryManager::processDeleteItemResponse(const EQ::Net::Packet& packet) {
    // Server sends back DeleteItem_Struct to confirm deletion
    LOG_DEBUG(MOD_UI, "InventoryManager Received DeleteItem response, size: {}", packet.Length());

    // For now, we trust our client-side deletion
}

void InventoryManager::dumpInventory() const {
    LOG_DEBUG(MOD_UI, "=== Inventory Dump ===");

    LOG_DEBUG(MOD_UI, "Equipment:");
    for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            LOG_DEBUG(MOD_UI, "  [{}] {}: {} (ID: {})", slot, getSlotName(slot), item->name, item->itemId);
        }
    }

    LOG_DEBUG(MOD_UI, "General:");
    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; slot++) {
        const ItemInstance* item = getItem(slot);
        if (item) {
            if (item->isContainer()) {
                LOG_DEBUG(MOD_UI, "  [{}]: {} (Bag, {} slots)", slot, item->name, (int)item->bagSlots);
                // Show bag contents
                auto bagSlots = getBagContentsSlots(slot);
                for (int16_t bagSlot : bagSlots) {
                    const ItemInstance* bagItem = getItem(bagSlot);
                    if (bagItem) {
                        LOG_DEBUG(MOD_UI, "    [{}]: {}", bagSlot, bagItem->name);
                    }
                }
            } else {
                LOG_DEBUG(MOD_UI, "  [{}]: {}", slot, item->name);
            }
        }
    }

    if (!cursorQueue_.empty()) {
        LOG_DEBUG(MOD_UI, "Cursor Queue ({} items):", cursorQueue_.size());
        int idx = 0;
        for (const auto& item : cursorQueue_) {
            if (idx == 0) {
                LOG_DEBUG(MOD_UI, "  [{}]: {} (active, from slot {})", idx, item->name, cursorSourceSlot_);
            } else {
                LOG_DEBUG(MOD_UI, "  [{}]: {}", idx, item->name);
            }
            idx++;
        }
    }

    LOG_DEBUG(MOD_UI, "======================");
}

} // namespace inventory
} // namespace eqt
