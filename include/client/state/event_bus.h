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

// Forward declarations for event data structures
struct PlayerMovedData;
struct PlayerStatsChangedData;
struct EntitySpawnedData;
struct EntityDespawnedData;
struct EntityMovedData;
struct EntityStatsChangedData;
struct DoorStateChangedData;
struct DoorSpawnedData;
struct ZoneChangedData;
struct ZoneLoadingData;
struct ChatMessageData;
struct CombatEventData;
struct GroupChangedData;
struct GroupMemberUpdatedData;
struct TimeOfDayChangedData;
struct PetCreatedData;
struct PetRemovedData;
struct PetStatsChangedData;
struct PetButtonStateChangedData;
struct WindowOpenedData;
struct WindowClosedData;

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

    // Window events (vendor, bank, trainer, tradeskill)
    VendorWindowOpened,
    VendorWindowClosed,
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

    // Spell events
    SpellGemChanged,
    CastingStateChanged,
    SpellMemorizing,
};

// Event data structures

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

struct ChatMessageData {
    std::string sender;
    std::string message;
    uint32_t channelType;
    std::string channelName;
};

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

struct TimeOfDayChangedData {
    uint8_t hour;
    uint8_t minute;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

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

struct WindowOpenedData {
    uint16_t npcId;
    std::string npcName;
    float sellRate;  // For vendor window
};

struct WindowClosedData {
    uint16_t npcId;
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

// Variant type for all event data
using EventData = std::variant<
    PlayerMovedData,
    PlayerStatsChangedData,
    EntitySpawnedData,
    EntityDespawnedData,
    EntityMovedData,
    EntityStatsChangedData,
    DoorSpawnedData,
    DoorStateChangedData,
    ZoneChangedData,
    ZoneLoadingData,
    ChatMessageData,
    CombatEventData,
    GroupChangedData,
    GroupMemberUpdatedData,
    TimeOfDayChangedData,
    PetCreatedData,
    PetRemovedData,
    PetStatsChangedData,
    PetButtonStateChangedData,
    WindowOpenedData,
    WindowClosedData,
    TradeskillContainerOpenedEvent,
    TradeskillContainerClosedEvent,
    InventorySlotChangedData,
    CursorItemChangedData,
    EquipmentStatsChangedData,
    SpellGemChangedData,
    CastingStateChangedData,
    SpellMemorizingData
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
