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

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace eqt {

// Hotbar button types (matches old HotbarButtonType)
enum class HotbarSlotType : uint8_t {
    Empty = 0,
    Spell = 1,
    Skill = 2,
    Item = 3,
    Emote = 4,
    Macro = 5,
    Combat = 6
};

struct HotbarSlot {
    HotbarSlotType type = HotbarSlotType::Empty;
    uint32_t id = 0;              // Spell/skill/item ID
    std::string emoteText;        // For emote type
    uint32_t iconId = 0;          // Cached icon ID
    std::string name;             // Display name (resolved from spell/skill DB)
};

class HotbarModel {
public:
    static constexpr int SLOT_COUNT = 10;

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
    const HotbarSlot& getSlot(int index) const;
    int getSlotCount() const { return SLOT_COUNT; }

    // --- Slot mutation (publishes events) ---
    void assignSlot(int index, HotbarSlotType type, uint32_t id,
                    const std::string& emoteText = "", uint32_t iconId = 0);
    void clearSlot(int index);

    // --- JSON serialization ---
    void loadFromJson(const Json::Value& data);
    Json::Value saveToJson() const;

    // --- Publish all slots to bridge (for snapshot) ---
    void publishAllSlots();

private:
    void publishSlot(int index);
    void onChanged();

    std::array<HotbarSlot, SLOT_COUNT> slots_;
    bridge::GameStateBridge* bridge_ = nullptr;
    NameResolver nameResolver_;
    SaveCallback saveCallback_;
};

} // namespace eqt
