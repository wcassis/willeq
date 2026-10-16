/*
 * HotbarModel — standalone hotbar data model.
 *
 * U07b/B1025-H1: Owns ten pages of slot data, imports legacy JSON, publishes
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
    std::string sourceCode;       // Verbatim official [HotButtons] value, when loaded
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
    using SpellAtGem = std::function<uint32_t(uint8_t)>;
    void setSpellAtGem(SpellAtGem reader) { spellAtGem_ = std::move(reader); }
    // B1077 (GitLab #397): a Spell slot's drawn icon is its spell's gem_icon
    // (the Gems family of the spell-icon atlas), the same column the gem bar
    // reads. A slot whose id resolves to nothing (or whose callback is not
    // installed) stays 0: 0 draws the atlas's first cell, exactly as an
    // unmemorised gem does.
    using SpellIconFor = std::function<uint32_t(uint32_t)>;
    void setSpellIconFor(SpellIconFor resolver) { spellIconFor_ = std::move(resolver); }
    int findSpellGem(uint32_t spellId) const;
    uint32_t spellAtGem(uint8_t gem) const { return spellAtGem_ ? spellAtGem_(gem) : 0; }

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
    // production slot writer is loadFromJson() (LoadHotbarConfig at zone-in);
    // nothing in a command or the UI assigns or clears a slot, so the
    // per-character save callback (setSaveCallback, registered in
    // BridgeComponent::wireBridgeToSubsystems) has no production assignment
    // trigger yet, i.e. hotbar
    // slot assignment is a missing feature, not a live path. They stay
    // because their tests drive assignSlot/clearSlot as the REAL producer of
    // the HotbarSlotAssigned bridge event, not a hand-built fixture (T156's
    // skill-reachability leg, the T15 input-action-bridge routing, T230's
    // hotbar item route) -- the tests are the net, so the functions live
    // with them.
    void assignSlot(HotbarAddress address, HotbarSlotType type, uint32_t id,
                    const std::string& emoteText = "", uint32_t iconId = 0);
    void clearSlot(HotbarAddress address);

    // --- Legacy JSON import ---
    void loadFromJson(const Json::Value& data);

    // --- Publish all slots to bridge (for snapshot) ---
    void publishAllSlots();

private:
    void publishSlot(HotbarAddress address);
    void onChanged();

    std::array<std::array<HotbarSlot, SLOT_COUNT>, PAGE_COUNT> slots_;
    int currentPage_ = 0;
    bridge::GameStateBridge* bridge_ = nullptr;
    NameResolver nameResolver_;
    SpellAtGem spellAtGem_;
    SpellIconFor spellIconFor_;  // B1077
    SaveCallback saveCallback_;
};

} // namespace eqt
