#pragma once

// Event payloads: chat, combat, group, time of day, pet and windows.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

#include <functional>
#include <vector>
#include <variant>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

#include "client/hotbar/hotbar_slot_type.h"  // T156: HotbarSlotAssignedData carries the enum, not a raw uint8_t
#include "client/spell/spell_visual_vocabulary.h"  // T381: SpellCastVisual*Data carry the render vocabulary by value, not a SpellDatabase*

class HCMap;
class PathfinderNavmesh;

#include "client/state/event_types.h"

namespace eqt {
namespace state {

// --- Chat events ---

struct ChatMessageData {
    std::string sender;
    std::string message;
    uint32_t channelType;
    std::string channelName;
};

// --- Combat events ---

struct TargetChangedData {
    uint16_t spawnId = 0;     // 0 = target cleared
    std::string name;
    uint8_t level = 0;
    uint8_t hpPercent = 100;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    uint8_t classId = 0;
    uint8_t bodyType = 0;
    uint8_t npcType = 0;      // 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    uint8_t helm = 0;
    uint8_t showHelm = 0;
    uint8_t texture = 0;      // equip_chest2
    uint32_t equipment[9] = {0};
    uint32_t equipmentTint[9] = {0};
};

struct DamageEventData {
    uint16_t sourceId;
    uint16_t targetId;
    int32_t amount;
    uint8_t type;
    std::string spellName;
};

struct SpellCastStartedData {
    uint16_t casterId;
    uint32_t spellId;
    uint32_t castTimeMs;
    uint16_t targetId = 0;  // T44: added for the (T227-deleted) projection;
                             // SpellManager writes SpellState directly now.
};

struct SpellCastCompleteData {
    uint16_t casterId;
    uint32_t spellId;
    uint8_t result;  // 0=success, non-zero=failure reason
};

// --- Group events ---

struct GroupChangedData {
    bool inGroup;
    bool isLeader;
    std::string leaderName;
    int memberCount;
};

struct GroupMemberUpdatedData {
    int memberIndex;
    std::string name;
    uint16_t spawnId;
    uint8_t level;
    uint8_t classId;
    uint8_t hpPercent;
    uint8_t manaPercent;
    bool inZone;
};

struct GroupInviteReceivedData {
    std::string inviterName;
};

// --- Time events ---

struct TimeOfDayChangedData {
    uint8_t hour;
    uint8_t minute;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

// --- Pet events ---

struct PetCreatedData {
    uint16_t spawnId;
    std::string name;
    uint8_t level;
};

struct PetRemovedData {
    uint16_t spawnId;
    std::string name;
};

struct PetStatsChangedData {
    uint16_t spawnId;
    uint8_t hpPercent;
    uint8_t manaPercent;
};

struct PetButtonStateChangedData {
    uint8_t button;
    bool state;
};

// --- Window events ---

struct WindowOpenedData {
    uint16_t npcId;
    std::string npcName;
    float sellRate;  // For vendor window
};

struct TrainerSkillEntryData {
    uint8_t skillId;
    std::string name;
    uint32_t currentValue;
    uint32_t maxTrainable;
    uint32_t cost;  // In copper
};

struct TrainerWindowOpenedData {
    uint16_t npcId;
    std::string npcName;
    std::vector<TrainerSkillEntryData> skills;
    uint32_t platinum;
    uint32_t gold;
    uint32_t silver;
    uint32_t copper;
    uint32_t practicePoints;
};

struct WindowClosedData {
    uint16_t npcId;
};

struct VendorItemAddedData {
    uint16_t vendorSlot;
    uint32_t itemId;
    std::string itemName;
    int32_t price;
    int32_t quantity;
    // T436 (board B429): the merchant window's first <Columns> entry is a
    // 24px cell with an EMPTY <Heading> -- the icon cell -- and this is the
    // field that fills it. `ZoneProcessVendorItemToUI` parsed `icon` off the
    // wire into an ItemInstance and dropped it here, exactly as T430 found
    // the loot arm doing.
    uint32_t iconId = 0;
};

struct TradeskillContainerOpenedEvent {
    bool isWorldObject;           // true if world object (forge, etc.), false if inventory container
    uint32_t objectId;            // World object drop ID (if world object)
    int16_t inventorySlot;        // Inventory slot (if inventory container)
    std::string containerName;
    uint8_t containerType;
    uint8_t slotCount;
};

struct TradeskillContainerClosedEvent {
    bool wasWorldObject;
    uint32_t objectId;
    int16_t inventorySlot;
};

// T419 (board B335) -- the tradeskill recipe list.
//
// eqemu's Client::SendTradeskillSearchResults() (zone/tradeskills.cpp) sends
// ONE OP_RecipeReply per matching recipe and marks neither the start nor the
// end of the run. The only honest list boundary is therefore the client's own
// request, so the two opcodes that ask for a list -- OP_RecipesSearch and
// OP_RecipesFavorite -- publish Cleared as they go out, and every reply that
// arrives afterwards appends.
struct TradeskillRecipeListClearedData {
    uint32_t objectType;   // the container the request was issued against
    std::string query;     // the search text; empty for a favourites fetch
    uint32_t minTrivial;   // RecipesSearch_Struct::mintrivial, as sent
    uint32_t maxTrivial;   // RecipesSearch_Struct::maxtrivial, as sent
    bool isFavorites;
};

// One OP_RecipeReply. Every field is the server's, verbatim:
//
//   struct RecipeReply_Struct {          // common/eq_packet_structs.h:4313
//       uint32 object_type;
//       uint32 some_id;                  //same as in favorites
//       uint32 component_count;
//       uint32 recipe_id;
//       uint32 trivial;
//       char recipe_name[64];
//   /*84*/
//   };
//
// Note what is NOT on the wire: the recipe's tradeskill and its component
// NAMES. The server filters by tradeskill before it builds the packet and
// sends the component list only in reply to OP_RecipeDetails, so a row can
// show a name and a trivial and a component COUNT, and nothing else.
struct TradeskillRecipeReceivedData {
    uint32_t objectType;
    uint32_t someId;
    uint32_t componentCount;
    uint32_t recipeId;
    uint32_t trivial;
    std::string name;
};

} // namespace state
} // namespace eqt
