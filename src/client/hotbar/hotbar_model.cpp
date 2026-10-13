/*
 * HotbarModel implementation.
 * U07b: Standalone hotbar data model with JSON + bridge events.
 */

#include "client/hotbar/hotbar_model.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/logging.h"

namespace eqt {

static HotbarSlot emptySlot;

static bool valid(HotbarAddress address) {
    return address.page >= 0 && address.page < HotbarModel::PAGE_COUNT &&
           address.slot >= 0 && address.slot < HotbarModel::SLOT_COUNT;
}

const HotbarSlot& HotbarModel::getSlot(HotbarAddress address) const {
    if (!valid(address)) return emptySlot;
    return slots_[address.page][address.slot];
}

void HotbarModel::setCurrentPage(int page) {
    if (page < 0 || page >= PAGE_COUNT || page == currentPage_) return;
    currentPage_ = page;
    publishAllSlots();
    onChanged();
}

void HotbarModel::reset() {
    slots_ = {};
    currentPage_ = 0;
}

void HotbarModel::assignSlot(HotbarAddress address, HotbarSlotType type, uint32_t id,
                              const std::string& emoteText, uint32_t iconId) {
    if (!valid(address)) return;

    auto& slot = slots_[address.page][address.slot];
    slot.type = type;
    slot.id = id;
    slot.emoteText = emoteText;
    slot.iconId = iconId;

    // Resolve display name
    // T239: Emote and Macro slots are named by their command text; the
    // resolver only knows spells and skills (installed by EverQuest's ctor,
    // eq.cpp) and returns "" for them.
    if (type == HotbarSlotType::Emote || type == HotbarSlotType::Macro) {
        slot.name = emoteText.empty() ? hotbarSlotTypeName(type) : emoteText.substr(0, 10);
    } else if (nameResolver_ && type != HotbarSlotType::Empty) {
        slot.name = nameResolver_(type, id);
    } else {
        slot.name.clear();
    }

    publishSlot(address);
    onChanged();
}

void HotbarModel::clearSlot(HotbarAddress address) {
    if (!valid(address)) return;
    slots_[address.page][address.slot] = {};
    publishSlot(address);
    onChanged();
}

void HotbarModel::loadFromJson(const Json::Value& data) {
    if (!data.isObject()) return;
    reset();
    const auto& pages = data["pages"];
    if (pages.isArray()) {
        const int page = data.get("currentPage", 0).asInt();
        if (page >= 0 && page < PAGE_COUNT) currentPage_ = page;
    }
    for (int p = 0; p < PAGE_COUNT; ++p) {
        // The old ten-button shape is imported as page zero.
        if (pages.isArray() && p >= static_cast<int>(pages.size())) break;
        const auto& buttons = pages.isArray() ? pages[p] : data["buttons"];
        if (!buttons.isArray()) {
            if (!pages.isArray()) break;
            continue;
        }
        for (Json::ArrayIndex i = 0; i < buttons.size() && i < SLOT_COUNT; ++i) {
            const auto& btn = buttons[i];
            int type = btn.get("type", 0).asInt();
            HotbarSlotType slotType = HotbarSlotType::Empty;
            // T156/T95: validated conversion. A config written by an older
            // build (or by hand) can hold anything; an out-of-range value
            // clears the slot rather than becoming a bogus enumerator.
            if (type < 0 || !hotbarSlotTypeFromRaw(static_cast<uint32_t>(type), slotType)) {
                LOG_WARN(MOD_CONFIG, "HotbarModel: slot {} has unknown type {}; clearing", i, type);
                slots_[p][i] = {};
                continue;
            }
            if (slotType != HotbarSlotType::Empty) {
                uint32_t id = btn.get("id", 0).asUInt();
                std::string emoteText = btn.get("emoteText", "").asString();
                uint32_t iconId = btn.get("iconId", 0).asUInt();

                slots_[p][i].type = slotType;
                slots_[p][i].id = id;
                slots_[p][i].emoteText = emoteText;
                slots_[p][i].iconId = iconId;

                if (slotType == HotbarSlotType::Emote || slotType == HotbarSlotType::Macro) {
                    slots_[p][i].name = emoteText.empty() ? hotbarSlotTypeName(slotType) : emoteText.substr(0, 10);
                } else if (nameResolver_) {
                    slots_[p][i].name = nameResolver_(slotType, id);
                }
            } else {
                slots_[p][i] = {};
            }
        }
        if (!pages.isArray()) break;
    }
    LOG_DEBUG(MOD_CONFIG, "HotbarModel: loaded hotbar from JSON");
}

Json::Value HotbarModel::saveToJson() const {
    Json::Value hotbar;
    Json::Value pages(Json::arrayValue);
    for (int p = 0; p < PAGE_COUNT; ++p) {
        Json::Value buttons(Json::arrayValue);
        for (int i = 0; i < SLOT_COUNT; ++i) {
            const auto& slot = slots_[p][i];
            Json::Value btn;
            btn["type"] = static_cast<int>(slot.type);
            btn["id"] = slot.id;
            btn["emoteText"] = slot.emoteText;
            btn["iconId"] = slot.iconId;
            buttons.append(btn);
        }
        pages.append(buttons);
    }
    hotbar["pages"] = pages;
    hotbar["currentPage"] = currentPage_;
    return hotbar;
}

void HotbarModel::publishAllSlots() {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        publishSlot({currentPage_, i});
    }
}

void HotbarModel::publishSlot(HotbarAddress address) {
    if (!bridge_ || !valid(address) || address.page != currentPage_) return;

    const auto& slot = slots_[address.page][address.slot];
    state::HotbarSlotAssignedData data;
    data.page = address.page;
    data.index = address.slot;
    data.type = slot.type;
    data.id = slot.id;
    data.name = slot.name;
    data.iconId = slot.iconId;
    data.emoteText = slot.emoteText;
    bridge_->pushEvent(state::GameEvent(
        state::GameEventType::HotbarSlotAssigned, std::move(data)));
}

void HotbarModel::onChanged() {
    if (saveCallback_) {
        saveCallback_();
    }
}

} // namespace eqt
