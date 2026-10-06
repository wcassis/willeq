// UIGAP-live (#151 #213 #214 #215 #211): the observables the six wave-1 UI
// units' live legs need, over the real client. See scenario.h for the
// contract of each; this TU is only the reads.
//
// Numbering: every slot a scenario names is `inventory.slot_id` (the server's
// numbering), converted here through serverSlotToClient() -- the same table
// parseUserSlot() uses for /pickup and /putdown -- so a scenario, the slash
// commands it types and the database it is checked against speak one
// numbering.

#include "client/scenario/scenario_world_app.h"

#include "client/application.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/entities/entities_component.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/skill/skill_manager.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"
#include "client/world_object.h"

#include <cmath>
#include <limits>

namespace eqt {
namespace scenario {

void ScenarioTranscript::Record(const eqt::state::GameEvent& event) {
    using eqt::state::GameEventType;
    std::lock_guard<std::mutex> lock(mutex);
    ++eventCounts[static_cast<int>(event.type)];
    switch (event.type) {
        case GameEventType::ChatMessage:
        case GameEventType::SystemMessage:
            if (const auto* d = std::get_if<eqt::state::ChatMessageData>(&event.data)) {
                chatLines.push_back(d->channelName + "|" + d->sender + "|" + d->message);
            }
            break;
        case GameEventType::SkillValueChanged:
            if (const auto* d = std::get_if<eqt::state::SkillValueChangedData>(&event.data)) {
                lastSkillValue[d->skillId] = d->value;
            }
            break;
        case GameEventType::TrackingListChanged:
            if (const auto* d = std::get_if<eqt::state::TrackingListChangedData>(&event.data)) {
                trackingRows = static_cast<long long>(d->rows.size());
            }
            break;
        default:
            break;
    }
}

ApplicationScenarioWorld::ApplicationScenarioWorld(Application& app, EverQuest& client,
                                                   eqt::bridge::GameStateBridge* bridge)
    : app_(app), client_(client), bridge_(bridge),
      transcript_(std::make_shared<ScenarioTranscript>()) {
    if (bridge_) {
        // The observer holds the transcript, not the world: the bridge is
        // destroyed before the world (application.h member order), and this
        // way neither can dangle into the other.
        std::shared_ptr<ScenarioTranscript> t = transcript_;
        bridge_->setEventObserver([t](const eqt::state::GameEvent& e) { t->Record(e); });
    }
}

namespace {

const eqt::inventory::ItemInstance* ItemAtServerSlot(EverQuest& client, int serverSlot) {
    auto* inv = client.GetInventoryManager();
    if (!inv) return nullptr;
    if (serverSlot < std::numeric_limits<int16_t>::min() ||
        serverSlot > std::numeric_limits<int16_t>::max()) {
        return nullptr;
    }
    const int16_t model = eqt::inventory::serverSlotToClient(static_cast<int16_t>(serverSlot));
    if (model == eqt::inventory::SLOT_INVALID) return nullptr;
    if (model == eqt::inventory::CURSOR_SLOT) return inv->getCursorItem();
    return inv->getItem(model);
}

bool Addressable(int serverSlot) {
    if (serverSlot < 0 || serverSlot > std::numeric_limits<int16_t>::max()) return false;
    return eqt::inventory::serverSlotToClient(static_cast<int16_t>(serverSlot)) !=
           eqt::inventory::SLOT_INVALID;
}

}  // namespace

long long ApplicationScenarioWorld::ClientItemAt(int serverSlot) const {
    if (!client_.GetInventoryManager() || !Addressable(serverSlot)) return -1;
    const auto* item = ItemAtServerSlot(client_, serverSlot);
    return item ? static_cast<long long>(item->itemId) : 0;
}

long long ApplicationScenarioWorld::ClientChargesAt(int serverSlot) const {
    if (!client_.GetInventoryManager() || !Addressable(serverSlot)) return -1;
    const auto* item = ItemAtServerSlot(client_, serverSlot);
    if (!item) return 0;
    return item->stackable ? static_cast<long long>(item->quantity)
                           : static_cast<long long>(item->charges);
}

long long ApplicationScenarioWorld::CursorQueueSize() const {
    auto* inv = client_.GetInventoryManager();
    return inv ? static_cast<long long>(inv->getCursorQueueSize()) : -1;
}

long long ApplicationScenarioWorld::SkillValue(int skillId) const {
    auto* skills = client_.GetSkillManager();
    if (!skills || skillId < 0 || skillId > 255) return -1;
    return static_cast<long long>(skills->getSkillValue(static_cast<uint8_t>(skillId)));
}

long long ApplicationScenarioWorld::PracticePoints() const {
    return static_cast<long long>(client_.GetPracticePoints());
}

size_t ApplicationScenarioWorld::ChatLineCount() const {
    std::lock_guard<std::mutex> lock(transcript_->mutex);
    return transcript_->chatLines.size();
}

std::string ApplicationScenarioWorld::ChatLine(size_t index) const {
    std::lock_guard<std::mutex> lock(transcript_->mutex);
    return index < transcript_->chatLines.size() ? transcript_->chatLines[index] : std::string();
}

long long ApplicationScenarioWorld::EventCount(int gameEventType) const {
    if (!bridge_) return -1;
    std::lock_guard<std::mutex> lock(transcript_->mutex);
    auto it = transcript_->eventCounts.find(gameEventType);
    return it == transcript_->eventCounts.end() ? 0 : it->second;
}

long long ApplicationScenarioWorld::LastSkillUpdateValue(int skillId) const {
    std::lock_guard<std::mutex> lock(transcript_->mutex);
    auto it = transcript_->lastSkillValue.find(skillId);
    return it == transcript_->lastSkillValue.end() ? -1 : it->second;
}

long long ApplicationScenarioWorld::TrackingRowCount() const {
    std::lock_guard<std::mutex> lock(transcript_->mutex);
    return transcript_->trackingRows;
}

long long ApplicationScenarioWorld::NearestWorldObjectId(int objectType) const {
    const float px = client_.GetGameState().player().x();
    const float py = client_.GetGameState().player().y();
    long long best = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (const auto& [id, obj] : client_.Entities().GetWorldObjects()) {
        if (static_cast<int>(obj.object_type) != objectType) continue;
        const float d = std::hypot(obj.x - px, obj.y - py);
        if (d < bestDist) {
            bestDist = d;
            best = static_cast<long long>(obj.drop_id);
        }
    }
    return best;
}

int ApplicationScenarioWorld::ClickSlot(int serverSlot, bool rightButton, bool shift, bool ctrl) {
    auto* inv = client_.GetInventoryManager();
    if (!inv || !bridge_ || !Addressable(serverSlot)) return -1;
    const int16_t model = eqt::inventory::serverSlotToClient(static_cast<int16_t>(serverSlot));
    // What the clicked window's panel would show for the slot, and what the
    // renderer knows about the cursor -- read off the model the panels are
    // republished from (B442: the panels never hold state the model lacks).
    eqt::inventory::SlotSnapshot slot;
    const auto* item = ItemAtServerSlot(client_, serverSlot);
    slot.occupied = item != nullptr;
    slot.quantity = (item && item->stackable) ? item->quantity : 0;
    eqt::inventory::CursorSnapshot cursor;
    cursor.hasItem = inv->hasCursorItem();
    eqt::inventory::ClickModifiers mods;
    mods.shift = shift;
    mods.ctrl = ctrl;
    eqt::inventory::CursorGesture gesture;
    gesture.setBridge(bridge_);
    const auto action = gesture.click(
        model, rightButton ? eqt::inventory::ClickButton::Right : eqt::inventory::ClickButton::Left,
        mods, slot, cursor);
    return static_cast<int>(action);
}

}  // namespace scenario
}  // namespace eqt
