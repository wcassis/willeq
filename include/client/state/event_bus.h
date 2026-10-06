#pragma once

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

// T451: the payload structs and the GameEventType enum were split out of this
// header (it was 1,269 lines) by family. This header includes all of them, so
// every existing `#include "client/state/event_bus.h"` keeps compiling
// unchanged and stays the one include a consumer needs.
#include "client/state/event_types.h"
#include "client/state/event_payloads_entity.h"
#include "client/state/event_payloads_social.h"
#include "client/state/event_payloads_inventory.h"
#include "client/state/event_payloads_spell.h"
#include "client/state/event_payloads_ui.h"
#include "client/state/event_payloads_tracking.h"  // B895

namespace eqt {
namespace state {

// ============================================================================
// Variant type for all event data
// ============================================================================

using EventData = std::variant<
    // Player
    PlayerMovedData,
    PlayerStatsChangedData,
    PlayerPositionStateChangedData,
    PlayerMovementModeChangedData,
    // Entity
    EntitySpawnedData,
    EntityDespawnedData,
    EntityMovedData,
    EntityStatsChangedData,
    EntityAppearanceChangedData,
    EntitySizeChangedData,
    EntityLightChangedData,
    EntityAnimationEventData,
    EntityWeaponSkillsChangedData,
    EntityPoseStateChangedData,
    EntityDeathAnimationData,
    CorpseDecayStartedData,
    CombatAnimationData,
    CombatSkillAnimationData,
    ReceivedDamageAnimationData,
    PlayerSpawnIdSetData,
    NetworkReadyData,
    // Door
    DoorSpawnedData,
    DoorStateChangedData,
    // Zone
    ZoneChangedData,
    ZoneLoadingData,
    ZoneLoadedData,
    // Chat
    ChatMessageData,
    // Combat
    TargetChangedData,
    DamageEventData,
    SpellCastStartedData,
    SpellCastCompleteData,
    // Group
    GroupChangedData,
    GroupMemberUpdatedData,
    GroupInviteReceivedData,
    // Time
    TimeOfDayChangedData,
    // Pet
    PetCreatedData,
    PetRemovedData,
    PetStatsChangedData,
    PetButtonStateChangedData,
    // Window
    WindowOpenedData,
    TrainerWindowOpenedData,
    WindowClosedData,
    VendorItemAddedData,
    TradeskillContainerOpenedEvent,
    TradeskillContainerClosedEvent,
    TradeskillRecipeListClearedData,
    TradeskillRecipeReceivedData,
    // Inventory
    InventorySlotChangedData,
    ItemDisplayShownData,
    CursorItemChangedData,
    EquipmentStatsChangedData,
    CurrencyChangedData,
    BankCurrencyChangedData,
    // Audio
    VolumesChangedData,
    GameOptionChangedData,  // B430
    // Loot
    LootWindowOpenedData,
    LootWindowClosedData,
    LootItemAddedData,
    LootItemRemovedData,
    // Trade
    TradeRequestReceivedData,
    TradeStartedData,
    TradeItemUpdatedData,
    TradeMoneyUpdatedData,
    TradeAcceptStateChangedData,
    TradeCancelledData,
    TradeCompletedData,
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
    SkillsSnapshotData,
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
    NoteWindowOpenedData,
    SpellScribeCompletedData,
    SpellbookSnapshotData,
    HotbarCooldownStartedData,
    HotbarSlotAssignedData,
    SkillActivationFeedbackData,
    RendererCommandData,
    // D20e1
    SpellCastVisualStartedData,
    SpellCastVisualCompleteData,
    SpellCastVisualInterruptedData,
    SpellMemorizeVisualStartedData,
    SpellMemorizeVisualCompleteData,
    ZoneUnloadingData,
    NavmeshChangedData,
    DiagnosticsMemoryReportData,
    DiagnosticsSceneDumpData,
    // B895: the Tracking Window
    TrackingListChangedData
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
