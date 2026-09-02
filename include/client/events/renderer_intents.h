#pragma once

#include <cstdint>
#include <string>
#include <variant>

// T156: the hotbar numbering, shared by value so HotbarActivateIntent cannot
// carry a raw uint8_t in some other scheme. Dependency-free header.
#include "client/hotbar/hotbar_slot_type.h"

namespace eqt {
namespace events {

// ============================================================================
// Renderer intent structs — posted by the renderer to the game thread via
// the GameStateBridge intent queue. Each struct represents a user action
// that requires game-state mutation or network communication.
//
// No renderer or EverQuest includes allowed in this file.
// ============================================================================

// --- Movement ---

struct PlayerPositionChanged {
    float x, y, z;
    float heading;
    float dx, dy, dz;  // Velocity/delta (animation derived on game thread from velocity + water state)
};

// T311: the player model's own measured origin-to-feet distance, published by
// the renderer whenever it changes (model load, race/size change).
//
// The game thread needs it to convert between the RENDER frame it receives from
// the renderer and the WIRE frame it must transmit -- see
// include/client/position_frames.h. It is a property of the SHIPPED MESH, so
// only the renderer can measure it (EntityRenderer::getPlayerCollisionZOffset,
// from collisionZOffsetFromBounds); it is pushed as a one-way intent rather than
// read back synchronously because EverQuest holds no renderer pointer by design
// (CLAUDE.md, "Key separation").
struct PlayerModelGeometryChanged {
    float collisionZOffset;  // |bbox.MinEdge.Y| * scale, world units; 0 = unknown
};

// --- Targeting ---

struct TargetIntent {
    uint16_t spawnId;  // 0 = clear target
};

// --- Chat ---

struct ChatSubmitIntent {
    std::string text;  // Full text including /channel prefix
};

// --- Interaction ---

struct DoorInteractIntent {
    uint8_t doorId;
};

struct LootCorpseIntent {
    uint16_t spawnId;
};

struct LootItemIntent {
    uint16_t corpseId;
    uint8_t slot;
};

struct LootAllIntent {
    uint16_t corpseId;
};

struct CloseLootIntent {
    uint16_t corpseId;
};

struct WorldObjectInteractIntent {
    uint32_t dropId;
};

struct ZoningEnabledIntent {
    bool enabled;
};

// T273: `filename` is the server-side book key (eqemu `books`.`name`, the
// item's Filename field) -- NOT the text, which only comes back in the
// server's OP_ReadBook reply. `type` is the item's BookType, echoed back by
// the server on Titanium: 0=scroll, 1=book, 2=item info.
struct ReadItemIntent {
    std::string filename;
    uint8_t type;
};

// --- Vendor ---

struct VendorToggleIntent {};  // NPC class/distance validation stays on game thread

struct VendorBuyIntent {
    uint16_t npcId;
    uint16_t slot;
    uint8_t quantity;
};

struct VendorSellIntent {
    uint16_t npcId;
    uint16_t slot;
    uint8_t quantity;
};

// --- Bank ---

struct BankerInteractIntent {
    uint16_t npcId;  // Validation stays on game thread
};

// T269: produced by EverQuest::RequestBankCoinMove() (/bank deposit|withdraw)
// -- the same intent a bank-window coin gesture would push. Consumed by the
// eq_bridge.cpp arm that builds OP_MoveCoin {inventory<->bank, cointype1==
// cointype2} (eqemu zone/client_process.cpp OPMoveCoin: slot 1 = inventory,
// slot 2 = bank; cointype 0=cp 1=sp 2=gp 3=pp).
struct BankCurrencyMoveIntent {
    uint8_t coinType;   // 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t amount;
    bool fromBank;      // true = bank→inventory, false = inventory→bank
};

// T269: produced by EverQuest::RequestBankCoinConvert() (/bank convert).
// OP_MoveCoin bank->bank with cointype1 != cointype2; the server converts at
// CoinTypeCoppers(from)/CoinTypeCoppers(to) (10:1 per step, either
// direction, truncating) -- so toCoinType is explicit rather than the old
// implied "fromCoinType + 1".
struct BankCurrencyConvertIntent {
    uint8_t fromCoinType;  // 0=copper, 1=silver, 2=gold, 3=platinum
    uint8_t toCoinType;    // same scale; must differ from fromCoinType
    int32_t amount;        // in fromCoinType coins
};

// --- Trainer ---

struct TrainerToggleIntent {};  // Validation stays on game thread

// --- Spells ---

struct CastSpellIntent {
    uint8_t gemSlot;
};

struct MemorizeSpellIntent {
    uint8_t gemSlot;
    uint32_t spellId;
};

struct ScribeSpellIntent {
    uint32_t spellId;
    // T276: was uint8_t. The Titanium spellbook is 400 slots
    // (EQ::MAX_SPELLBOOK_SLOTS, eqemu EQ::spells::SPELLBOOK_SIZE) and
    // EverQuest::ScribeSpellFromScroll()/SpellManager::scribeSpell() have
    // always taken uint16_t, so the intent was the one narrowing point on
    // the route -- book slots 256..399 would have wrapped to 0..143 and
    // scribed over an occupied slot. Never observed because nothing
    // produced the intent (T264 F5).
    uint16_t bookSlot;
    int16_t sourceSlot;
};

struct SpellbookStateIntent {
    bool isOpen;  // Sends appearance animation
};

// --- Buffs ---

struct BuffCancelIntent {
    uint8_t slot;
};

// --- Skills ---

struct SkillActivateIntent {
    uint32_t skillId;
};

// D20f4: BspTreeAvailableIntent removed — BSP tree set via typed setter

// --- Volume ---

struct MusicVolumeChangeIntent {
    float delta;  // +/- volume change
};

struct EffectsVolumeChangeIntent {
    float delta;  // +/- volume change
};

// --- Tradeskill ---

struct TradeskillCombineIntent {
    int16_t containerSlot;  // SLOT_TRADESKILL_EXPERIMENT_COMBINE for world containers
};

struct TradeskillCloseIntent {
    uint32_t dropId;  // World object drop ID (0 for inventory containers)
};

// --- Trainer ---

struct TrainSkillIntent {
    uint8_t skillId;
};

struct CloseTrainerIntent {};

// --- Hotbar ---

struct HotbarActivateIntent {
    int index = -1;      // Hotbar slot index (for cooldown feedback)
    // T156: was `uint8_t buttonType`, always value-initialised to 0 by the
    // only producer, so the Skill arm was unreachable. Now the model's own
    // enum, filled from the cached slot state.
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;     // spell_id, item_id, or skill_id
    std::string emoteText;  // For emote buttons
};

// --- Inventory ---

struct MoveItemIntent {
    int16_t fromSlot;
    int16_t toSlot;
    uint32_t quantity;  // 0 = move entire stack
};

struct DeleteItemIntent {
    int16_t slot;
};

struct EquipmentChangedIntent {
    int16_t slotId;
};

// ============================================================================
// Variant type for all renderer intents
// ============================================================================

using RendererIntent = std::variant<
    // Movement
    PlayerPositionChanged,
    PlayerModelGeometryChanged,
    // Targeting
    TargetIntent,
    // Chat
    ChatSubmitIntent,
    // Interaction
    DoorInteractIntent,
    LootCorpseIntent,
    LootItemIntent,
    LootAllIntent,
    CloseLootIntent,
    WorldObjectInteractIntent,
    ZoningEnabledIntent,
    ReadItemIntent,
    // Vendor
    VendorToggleIntent,
    VendorBuyIntent,
    VendorSellIntent,
    // Bank
    BankerInteractIntent,
    BankCurrencyMoveIntent,
    BankCurrencyConvertIntent,
    // Trainer
    TrainerToggleIntent,
    // Spells
    CastSpellIntent,
    MemorizeSpellIntent,
    ScribeSpellIntent,
    SpellbookStateIntent,
    // Buffs
    BuffCancelIntent,
    // Skills
    SkillActivateIntent,
    // Volume (D20c2)
    MusicVolumeChangeIntent,
    EffectsVolumeChangeIntent,
    // Hotbar (D20b2)
    HotbarActivateIntent,
    // Tradeskill
    TradeskillCombineIntent,
    TradeskillCloseIntent,
    // Trainer
    TrainSkillIntent,
    CloseTrainerIntent,
    // Inventory
    MoveItemIntent,
    DeleteItemIntent,
    EquipmentChangedIntent
>;

} // namespace events
} // namespace eqt
