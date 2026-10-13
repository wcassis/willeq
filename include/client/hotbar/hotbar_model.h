/*
 * HotbarModel — standalone hotbar data model.
 *
 * U07b: Owns hotbar slot data, reads/writes JSON config, publishes
 * HotbarSlotAssigned events. No dependency on WindowManager or old UI.
 */

#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <functional>
#include <json/json.h>
#include "client/hotbar/hotbar_slot_type.h"

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace eqt {

// T156: HotbarSlotType moved to client/hotbar/hotbar_slot_type.h so the
// renderer intents and the event bus can share the type, not a raw uint8_t.

struct HotbarSlot {
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;              // Spell/skill/item ID
    std::string emoteText;        // For emote type
    uint32_t iconId = 0;          // Cached icon ID
    std::string name;             // Display name (resolved from spell/skill DB)
};

// Stock Titanium exposes one hotbar with ten pages of ten buttons.
struct HotbarAddress {
    int page = 0;
    int slot = 0;
};

class HotbarModel {
public:
    static constexpr int SLOT_COUNT = 10;
    static constexpr int PAGE_COUNT = 10;

    HotbarModel() = default;
    ~HotbarModel() = default;

    /** Set the bridge for publishing events. */
    void setBridge(bridge::GameStateBridge* bridge) { bridge_ = bridge; }

    /** Callback to resolve spell/skill names from IDs. */
    using NameResolver = std::function<std::string(HotbarSlotType type, uint32_t id)>;
    void setNameResolver(NameResolver resolver) { nameResolver_ = std::move(resolver); }

    /** Callback for config save (triggered on any slot change). */
    using SaveCallback = std::function<void()>;
    void setSaveCallback(SaveCallback cb) { saveCallback_ = std::move(cb); }

    // --- Slot access ---
    const HotbarSlot& getSlot(HotbarAddress address) const;
    int getSlotCount() const { return SLOT_COUNT; }
    int currentPage() const { return currentPage_; }
    void setCurrentPage(int page);
    void reset();

    // --- Slot mutation (publishes events) ---
    // B603 (T458 class: test seam, kept): no production caller. The only
    // production slot writer is loadFromJson() (LoadHotbarConfig at startup);
    // nothing in a command or the UI assigns or clears a slot, so the
    // config-save callback (setSaveCallback, registered in
    // BridgeComponent::InitGraphics, bridge_component.cpp:145) fires in
    // production only from these two -- which today is never, i.e. hotbar
    // slot assignment is a missing feature, not a live path. They stay
    // because their tests drive assignSlot/clearSlot as the REAL producer of
    // the HotbarSlotAssigned bridge event, not a hand-built fixture (T156's
    // skill-reachability leg, the T15 input-action-bridge routing, T230's
    // hotbar item route) -- the tests are the net, so the functions live
    // with them.
    void assignSlot(HotbarAddress address, HotbarSlotType type, uint32_t id,
                    const std::string& emoteText = "", uint32_t iconId = 0);
    void clearSlot(HotbarAddress address);

    // --- JSON serialization ---
    void loadFromJson(const Json::Value& data);
    Json::Value saveToJson() const;

    // --- Publish all slots to bridge (for snapshot) ---
    void publishAllSlots();

private:
    void publishSlot(HotbarAddress address);
    void onChanged();

    std::array<std::array<HotbarSlot, SLOT_COUNT>, PAGE_COUNT> slots_;
    int currentPage_ = 0;
    bridge::GameStateBridge* bridge_ = nullptr;
    NameResolver nameResolver_;
    SaveCallback saveCallback_;
};

} // namespace eqt
