#pragma once

#include <functional>
#include <vector>
#include <variant>
#include <string>
#include <cstdint>
#include <mutex>
#include <glm/glm.hpp>

namespace eqt {
namespace state {

// Event types enum
enum class GameEventType {
    // Player events
    PlayerMoved,
    PlayerStatsChanged,
    PlayerPositionStateChanged,
    PlayerMovementModeChanged,

    // Entity events
    EntitySpawned,
    EntityDespawned,
    EntityMoved,
    EntityStatsChanged,
    EntityAppearanceChanged,
    EntityLightChanged,
    EntityAnimationEvent,
    EntityPoseStateChanged,
    EntityDeathAnimation,
    CorpseDecayStarted,
    CombatAnimation,

    // Door events
    DoorSpawned,
    DoorStateChanged,

    // Zone events
    ZoneChanged,
    ZoneLoading,
    ZoneLoaded,

    // Chat events
    ChatMessage,
    SystemMessage,

    // Combat events
    CombatEvent,
    TargetChanged,
    DamageEvent,
    SpellCastStarted,
    SpellCastComplete,

    // Group events
    GroupChanged,
    GroupMemberUpdated,
    GroupInviteReceived,

    // Time events
    TimeOfDayChanged,

    // Pet events
    PetCreated,
    PetRemoved,
    PetStatsChanged,
    PetButtonStateChanged,
    PetWindowOpened,
    PetWindowClosed,

    // Window events (vendor, bank, trainer, tradeskill)
    VendorWindowOpened,
    VendorWindowClosed,
    VendorItemAdded,
    BankWindowOpened,
    BankWindowClosed,
    TrainerWindowOpened,
    TrainerWindowClosed,
    TradeskillContainerOpened,
    TradeskillContainerClosed,

    // Inventory events
    InventorySlotChanged,
    CursorItemChanged,
    EquipmentStatsChanged,
    CurrencyChanged,
    BankCurrencyChanged,

    // Loot events
    LootWindowOpened,
    LootWindowClosed,
    LootItemAdded,
    LootItemRemoved,

    // Trade events
    TradeStarted,
    TradeItemUpdated,
    TradeAcceptStateChanged,
    TradeCancelled,
    TradeCompleted,

    // Spell events
    SpellGemChanged,
    CastingStateChanged,
    SpellMemorizing,
    BuffUpdated,
    BuffRemoved,
    VisionChanged,

    // Skill events
    SkillValueChanged,
    SkillsRefreshed,

    // World/environment events
    WeatherChanged,
    SwimmingStateChanged,

    // Zone lifecycle events
    CollisionMapChanged,
    ZoneLineBoundingBoxes,

    // UI/misc events
    ExpProgressChanged,
    CharacterInfoChanged,
    WorldObjectSpawned,
    NoteWindowOpened,
};

// ============================================================================
// Event data structures
// ============================================================================

// --- Player events ---

struct PlayerMovedData {
    float x, y, z;
    float heading;
    float dx, dy, dz;  // Velocity
    bool isMoving;
};

struct PlayerStatsChangedData {
    uint32_t curHP, maxHP;
    uint32_t curMana, maxMana;
    uint32_t curEndurance, maxEndurance;
    uint8_t level;
};

// --- Entity events ---

struct EntitySpawnedData {
    uint16_t spawnId;
    std::string name;
    float x, y, z;
    float heading;
    uint16_t raceId;
    uint8_t classId;
    uint8_t level;
    uint8_t gender;
    uint8_t npcType;  // 0=player, 1=npc, 2=pc_corpse, 3=npc_corpse
    bool isCorpse;
};

struct EntityDespawnedData {
    uint16_t spawnId;
    std::string name;
};

struct EntityMovedData {
    uint16_t spawnId;
    float x, y, z;
    float heading;
    float dx, dy, dz;
    uint8_t animation;
};

struct EntityStatsChangedData {
    uint16_t spawnId;
    uint8_t hpPercent;
    uint16_t curMana;
    uint16_t maxMana;
};

struct EntityAppearanceChangedData {
    uint16_t spawnId;
    uint16_t raceId;
    uint8_t gender;
    uint16_t appearanceType;  // SpawnAppearanceType value
    uint32_t appearanceValue;
};

struct EntityLightChangedData {
    uint16_t spawnId;
    uint8_t lightLevel;
};

struct EntityAnimationEventData {
    uint16_t spawnId;
    uint8_t animCode;
    bool loop;
    bool playThrough;
};

struct EntityPoseStateChangedData {
    uint16_t spawnId;
    uint8_t poseState;
};

struct EntityDeathAnimationData {
    uint16_t spawnId;
};

struct CorpseDecayStartedData {
    uint16_t spawnId;
};

struct CombatAnimationData {
    uint16_t sourceId;
    uint16_t targetId;
    uint8_t damageType;
    int32_t damageAmount;
    uint8_t damagePercent;
};

// --- Door events ---

struct DoorSpawnedData {
    uint8_t doorId;
    std::string name;
    float x, y, z;
    float heading;
    uint8_t state;  // 0=closed, 1=open
};

struct DoorStateChangedData {
    uint8_t doorId;
    bool isOpen;
};

// --- Zone events ---

struct ZoneChangedData {
    std::string zoneName;
    uint16_t zoneId;
    float x, y, z;
    float heading;
};

struct ZoneLoadingData {
    std::string zoneName;
    uint16_t zoneId;
    float progress;  // 0.0 to 1.0
    std::string statusMessage;
};

// --- Chat events ---

struct ChatMessageData {
    std::string sender;
    std::string message;
    uint32_t channelType;
    std::string channelName;
};

// --- Combat events ---

struct CombatEventData {
    enum class Type {
        Hit,
        Miss,
        Dodge,
        Parry,
        Block,
        Riposte,
        CriticalHit,
        Death
    };
    Type type;
    uint16_t sourceId;
    uint16_t targetId;
    int32_t damage;
    std::string sourceName;
    std::string targetName;
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
    uint16_t targetId;
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

struct WindowClosedData {
    uint16_t npcId;
};

struct VendorItemAddedData {
    uint16_t vendorSlot;
    uint32_t itemId;
    std::string itemName;
    int32_t price;
    int32_t quantity;
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

// --- Inventory events ---

struct InventorySlotChangedData {
    int16_t slotId;
    bool hasItem;
    uint32_t itemId;  // 0 if no item
};

struct CursorItemChangedData {
    bool hasCursorItem;
    uint8_t queueSize;
};

struct EquipmentStatsChangedData {
    int32_t ac;
    int32_t atk;
    int32_t hp;
    int32_t mana;
    float weight;
};

struct CurrencyChangedData {
    int32_t platinum;
    int32_t gold;
    int32_t silver;
    int32_t copper;
};

struct BankCurrencyChangedData {
    int32_t platinum;
    int32_t gold;
    int32_t silver;
    int32_t copper;
};

// --- Loot events ---

struct LootWindowOpenedData {
    uint16_t corpseId;
    std::string corpseName;
};

struct LootWindowClosedData {
    uint16_t corpseId;
};

struct LootItemAddedData {
    uint16_t corpseId;
    uint8_t slot;
    uint32_t itemId;
    std::string itemName;
};

struct LootItemRemovedData {
    uint16_t corpseId;
    uint8_t slot;
};

// --- Trade events ---

struct TradeStartedData {
    uint16_t partnerId;
    std::string partnerName;
    bool isNpc;
};

struct TradeItemUpdatedData {
    uint8_t who;  // 0=self, 1=partner
    uint8_t slot;
    uint32_t itemId;
    std::string itemName;
};

struct TradeAcceptStateChangedData {
    bool ownAccepted;
    bool partnerAccepted;
};

// --- Spell events ---

struct SpellGemChangedData {
    uint8_t gemSlot;
    uint32_t spellId;
    uint8_t gemState;  // SpellGemState value
    uint32_t cooldownRemainingMs;
};

struct CastingStateChangedData {
    bool isCasting;
    uint32_t spellId;
    uint16_t targetId;
    uint32_t castTimeRemainingMs;
    uint32_t castTimeTotalMs;
};

struct SpellMemorizingData {
    bool isMemorizing;
    uint8_t gemSlot;
    uint32_t spellId;
    uint32_t progressMs;
    uint32_t totalMs;
};

struct BuffUpdatedData {
    uint8_t slot;
    uint32_t spellId;
    uint32_t ticksLeft;
    std::string casterName;
};

struct BuffRemovedData {
    uint8_t slot;
};

struct VisionChangedData {
    uint8_t visionType;  // 0=normal, 1=ultravision, 2=infravision
};

// --- Skill events ---

struct SkillValueChangedData {
    uint16_t skillId;
    uint16_t value;
};

struct SkillsRefreshedData {
    // Full skill refresh — consumer should re-query all skills
};

// --- World/environment events ---

struct WeatherChangedData {
    uint8_t type;       // 0=none, 1=rain, 2=snow
    uint8_t intensity;  // 0-255
};

struct SwimmingStateChangedData {
    bool isSwimming;
    float swimSpeed;
    bool isLevitating;
};

// --- Zone lifecycle events ---

struct CollisionMapChangedData {
    void* map;  // Opaque pointer — consumer casts to HCMap*
};

struct ZoneLineBoundingBoxesData {
    // Consumer should re-query zone lines from game state
};

// --- UI/misc events ---

struct ExpProgressChangedData {
    float progress;  // 0.0 to 1.0
};

struct CharacterInfoChangedData {
    std::string name;
    uint8_t level;
    std::string className;
    std::string deity;
};

struct WorldObjectSpawnedData {
    uint32_t dropId;
    float x, y, z;
    float heading;
    std::string modelName;
    uint8_t objectType;
};

struct NoteWindowOpenedData {
    std::string text;
    uint8_t type;  // 0=book, 1=note
};

// ============================================================================
// Variant type for all event data
// ============================================================================

using EventData = std::variant<
    // Player
    PlayerMovedData,
    PlayerStatsChangedData,
    // Entity
    EntitySpawnedData,
    EntityDespawnedData,
    EntityMovedData,
    EntityStatsChangedData,
    EntityAppearanceChangedData,
    EntityLightChangedData,
    EntityAnimationEventData,
    EntityPoseStateChangedData,
    EntityDeathAnimationData,
    CorpseDecayStartedData,
    CombatAnimationData,
    // Door
    DoorSpawnedData,
    DoorStateChangedData,
    // Zone
    ZoneChangedData,
    ZoneLoadingData,
    // Chat
    ChatMessageData,
    // Combat
    CombatEventData,
    DamageEventData,
    SpellCastStartedData,
    SpellCastCompleteData,
    // Group
    GroupChangedData,
    GroupMemberUpdatedData,
    // Time
    TimeOfDayChangedData,
    // Pet
    PetCreatedData,
    PetRemovedData,
    PetStatsChangedData,
    PetButtonStateChangedData,
    // Window
    WindowOpenedData,
    WindowClosedData,
    VendorItemAddedData,
    TradeskillContainerOpenedEvent,
    TradeskillContainerClosedEvent,
    // Inventory
    InventorySlotChangedData,
    CursorItemChangedData,
    EquipmentStatsChangedData,
    CurrencyChangedData,
    BankCurrencyChangedData,
    // Loot
    LootWindowOpenedData,
    LootWindowClosedData,
    LootItemAddedData,
    LootItemRemovedData,
    // Trade
    TradeStartedData,
    TradeItemUpdatedData,
    TradeAcceptStateChangedData,
    // Spell
    SpellGemChangedData,
    CastingStateChangedData,
    SpellMemorizingData,
    BuffUpdatedData,
    BuffRemovedData,
    VisionChangedData,
    // Skill
    SkillValueChangedData,
    SkillsRefreshedData,
    // World
    WeatherChangedData,
    SwimmingStateChangedData,
    // Zone lifecycle
    CollisionMapChangedData,
    ZoneLineBoundingBoxesData,
    // UI/misc
    ExpProgressChangedData,
    CharacterInfoChangedData,
    WorldObjectSpawnedData,
    NoteWindowOpenedData
>;

// Game event combining type and data
struct GameEvent {
    GameEventType type;
    EventData data;

    template<typename T>
    GameEvent(GameEventType t, T&& d) : type(t), data(std::forward<T>(d)) {}
};

// Event listener callback type
using EventListener = std::function<void(const GameEvent&)>;

// Listener handle for unsubscription
using ListenerHandle = size_t;

/**
 * EventBus - Central event distribution system for game state changes.
 *
 * Thread-safe event bus that allows components to subscribe to game events
 * and receive notifications when state changes occur. This decouples the
 * game state from the rendering and other systems that need to react to
 * state changes.
 *
 * Usage:
 *   EventBus bus;
 *   auto handle = bus.subscribe([](const GameEvent& event) {
 *       if (event.type == GameEventType::PlayerMoved) {
 *           auto& data = std::get<PlayerMovedData>(event.data);
 *           // Handle player movement
 *       }
 *   });
 *
 *   // Later, unsubscribe:
 *   bus.unsubscribe(handle);
 */
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // Non-copyable, non-movable (singleton-like usage)
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /**
     * Subscribe to all events.
     * @param listener Callback function to receive events
     * @return Handle for unsubscription
     */
    ListenerHandle subscribe(EventListener listener);

    /**
     * Subscribe to a specific event type.
     * @param type Event type to listen for
     * @param listener Callback function to receive events
     * @return Handle for unsubscription
     */
    ListenerHandle subscribe(GameEventType type, EventListener listener);

    /**
     * Unsubscribe a listener.
     * @param handle Handle returned from subscribe()
     */
    void unsubscribe(ListenerHandle handle);

    /**
     * Publish an event to all subscribed listeners.
     * @param event The event to publish
     */
    void publish(const GameEvent& event);

    /**
     * Convenience method to publish an event with type and data.
     * @param type Event type
     * @param data Event data
     */
    template<typename T>
    void publish(GameEventType type, T&& data) {
        publish(GameEvent(type, std::forward<T>(data)));
    }

    /**
     * Clear all listeners.
     */
    void clear();

private:
    struct ListenerEntry {
        ListenerHandle handle;
        EventListener listener;
        GameEventType filterType;
        bool hasFilter;
    };

    std::vector<ListenerEntry> m_listeners;
    ListenerHandle m_nextHandle = 1;
    mutable std::mutex m_mutex;
};

} // namespace state
} // namespace eqt
