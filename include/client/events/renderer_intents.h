#pragma once

#include <cstdint>
#include <string>
#include <variant>

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

// --- Targeting ---

struct TargetIntent {
    uint16_t spawnId;  // 0 = clear target
};

// --- Combat ---

struct ToggleAutoAttackIntent {};

struct AttackIntent {
    uint16_t targetId;
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

struct DestroyAllLootIntent {
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

struct ReadItemIntent {
    std::string bookText;
    uint8_t type;  // 0=book, 1=note
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

struct CloseVendorIntent {
    uint16_t npcId;
};

// --- Bank ---

struct BankerInteractIntent {
    uint16_t npcId;  // Validation stays on game thread
};

struct BankCurrencyMoveIntent {
    uint8_t coinType;   // 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t amount;
    bool fromBank;      // true = bank→inventory, false = inventory→bank
};

struct BankCurrencyConvertIntent {
    uint8_t fromCoinType;  // 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t amount;
};

struct CloseBankIntent {};

// --- Trade ---

struct TradeRequestIntent {
    uint16_t targetId;
};

struct TradeAcceptIntent {};

struct TradeCancelIntent {};

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

struct ForgetSpellIntent {
    uint8_t gemSlot;
};

struct ScribeSpellIntent {
    uint32_t spellId;
    uint8_t bookSlot;
    int16_t sourceSlot;
};

struct SpellbookStateIntent {
    bool isOpen;  // Sends appearance animation
};

struct InterruptSpellIntent {};

// --- Buffs ---

struct BuffCancelIntent {
    uint8_t slot;
};

// --- Skills ---

struct SkillActivateIntent {
    uint32_t skillId;
};

// --- Pet ---

struct PetCommandIntent {
    uint8_t command;     // PetCommand enum value
    uint16_t targetId;   // For attack command
};

// --- Group ---

struct GroupInviteIntent {
    std::string targetName;
};

struct DisbandIntent {};

struct DeclineInviteIntent {};

// --- General ---

struct RequestCampIntent {};

struct RequestQuitIntent {};

struct SlashCommandIntent {
    std::string fullCommand;  // Game-affecting commands that cross the bridge
};

struct RequestMemoryReport {};

struct RequestSceneDump {};

struct HotbarChangedIntent {};

// ============================================================================
// Variant type for all renderer intents
// ============================================================================

using RendererIntent = std::variant<
    // Movement
    PlayerPositionChanged,
    // Targeting
    TargetIntent,
    // Combat
    ToggleAutoAttackIntent,
    AttackIntent,
    // Chat
    ChatSubmitIntent,
    // Interaction
    DoorInteractIntent,
    LootCorpseIntent,
    LootItemIntent,
    LootAllIntent,
    DestroyAllLootIntent,
    CloseLootIntent,
    WorldObjectInteractIntent,
    ZoningEnabledIntent,
    ReadItemIntent,
    // Vendor
    VendorToggleIntent,
    VendorBuyIntent,
    VendorSellIntent,
    CloseVendorIntent,
    // Bank
    BankerInteractIntent,
    BankCurrencyMoveIntent,
    BankCurrencyConvertIntent,
    CloseBankIntent,
    // Trade
    TradeRequestIntent,
    TradeAcceptIntent,
    TradeCancelIntent,
    // Trainer
    TrainerToggleIntent,
    // Spells
    CastSpellIntent,
    MemorizeSpellIntent,
    ForgetSpellIntent,
    ScribeSpellIntent,
    SpellbookStateIntent,
    InterruptSpellIntent,
    // Buffs
    BuffCancelIntent,
    // Skills
    SkillActivateIntent,
    // Pet
    PetCommandIntent,
    // Group
    GroupInviteIntent,
    DisbandIntent,
    DeclineInviteIntent,
    // General
    RequestCampIntent,
    RequestQuitIntent,
    SlashCommandIntent,
    RequestMemoryReport,
    RequestSceneDump,
    HotbarChangedIntent
>;

} // namespace events
} // namespace eqt
