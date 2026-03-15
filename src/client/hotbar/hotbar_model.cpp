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

const HotbarSlot& HotbarModel::getSlot(int index) const {
    if (index < 0 || index >= SLOT_COUNT) return emptySlot;
    return slots_[index];
}

void HotbarModel::assignSlot(int index, HotbarSlotType type, uint32_t id,
                              const std::string& emoteText, uint32_t iconId) {
    if (index < 0 || index >= SLOT_COUNT) return;

    auto& slot = slots_[index];
    slot.type = type;
    slot.id = id;
    slot.emoteText = emoteText;
    slot.iconId = iconId;

    // Resolve display name
    if (nameResolver_ && type != HotbarSlotType::Empty) {
        slot.name = nameResolver_(type, id);
    } else if (type == HotbarSlotType::Emote) {
        slot.name = emoteText.empty() ? "Emote" : emoteText.substr(0, 10);
    } else {
        slot.name.clear();
    }

    publishSlot(index);
    onChanged();
}

void HotbarModel::clearSlot(int index) {
    if (index < 0 || index >= SLOT_COUNT) return;
    slots_[index] = {};
    publishSlot(index);
    onChanged();
}

void HotbarModel::loadFromJson(const Json::Value& data) {
    if (!data.isObject()) return;

    if (data.isMember("buttons") && data["buttons"].isArray()) {
        const auto& buttons = data["buttons"];
        for (Json::ArrayIndex i = 0; i < buttons.size() && i < SLOT_COUNT; ++i) {
            const auto& btn = buttons[i];
            int type = btn.get("type", 0).asInt();
            if (type != 0) {
                auto slotType = static_cast<HotbarSlotType>(type);
                uint32_t id = btn.get("id", 0).asUInt();
                std::string emoteText = btn.get("emoteText", "").asString();
                uint32_t iconId = btn.get("iconId", 0).asUInt();

                slots_[i].type = slotType;
                slots_[i].id = id;
                slots_[i].emoteText = emoteText;
                slots_[i].iconId = iconId;

                if (nameResolver_) {
                    slots_[i].name = nameResolver_(slotType, id);
                } else if (slotType == HotbarSlotType::Emote) {
                    slots_[i].name = emoteText.empty() ? "Emote" : emoteText.substr(0, 10);
                }
            } else {
                slots_[i] = {};
            }
        }
    }

    LOG_DEBUG(MOD_CONFIG, "HotbarModel: loaded {} slots from JSON", SLOT_COUNT);
}

Json::Value HotbarModel::saveToJson() const {
    Json::Value hotbar;
    Json::Value buttons(Json::arrayValue);

    for (int i = 0; i < SLOT_COUNT; ++i) {
        const auto& slot = slots_[i];
        Json::Value btn;
        btn["type"] = static_cast<int>(slot.type);
        btn["id"] = slot.id;
        btn["emoteText"] = slot.emoteText;
        btn["iconId"] = slot.iconId;
        buttons.append(btn);
    }

    hotbar["buttons"] = buttons;
    return hotbar;
}

void HotbarModel::publishAllSlots() {
    for (int i = 0; i < SLOT_COUNT; ++i) {
        publishSlot(i);
    }
}

void HotbarModel::publishSlot(int index) {
    if (!bridge_ || index < 0 || index >= SLOT_COUNT) return;

    const auto& slot = slots_[index];
    state::HotbarSlotAssignedData data;
    data.index = index;
    data.type = static_cast<uint8_t>(slot.type);
    data.id = slot.id;
    data.name = slot.name;
    data.iconId = slot.iconId;
    bridge_->pushEvent(state::GameEvent(
        state::GameEventType::HotbarSlotAssigned, std::move(data)));
}

void HotbarModel::onChanged() {
    if (saveCallback_) {
        saveCallback_();
    }
}

} // namespace eqt
