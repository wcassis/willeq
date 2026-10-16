// B1025-H5 (#345): the scenario's hotbar controls, each driving the SAME user
// path a player does -- never a model call. Three distinct routes, which is
// the card's point ("through the user path ... activate each by the keyboard
// route and by the click route"):
//
//   * assign  -- HotbarAssignIntent, the render-thread hold gesture's intent
//     (BridgeComponent::onHotbarAssignIntent -> HotbarModel::assignSlot).
//   * click   -- HotbarActivateIntent for the slot's content
//     (onHotbarActivateIntent reads the model's slot and dispatches by type).
//   * key     -- a hotbar keypress the game thread's InputActionBridge drains
//     (processHotbarRequests, input_action_bridge.cpp:292) and dispatches by
//     the slot's type. Injected into AutomatedMode's NullInputHandler, the
//     handler that path consumes.
//   * page    -- HotbarPageChangedIntent -> HotbarModel::setCurrentPage.
//
// Everything runs on the GAME THREAD (the automation callback), so the
// intents pushed here are drained by the next GameTick's ProcessBridgeIntents
// (bridge) / processInput (key) -- the scenario then polls the server-visible
// result, as every other lane does.

#include "client/scenario/scenario_world_app.h"

#include "client/application.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/events/renderer_intents.h"
#include "client/hotbar/hotbar_model.h"
#include "client/hotbar/hotbar_slot_type.h"
#include "client/mode/automated_mode.h"

namespace eqt {
namespace scenario {

bool ApplicationScenarioWorld::AssignHotbarSlot(int page, int slot, int type,
                                                uint32_t id, const std::string& text) {
    if (!bridge_) return false;
    if (type < 0 || type > static_cast<int>(eqt::HotbarSlotType::Combat)) return false;
    eqt::events::HotbarAssignIntent i;
    i.page = page;
    i.slot = slot;
    i.type = static_cast<eqt::HotbarSlotType>(static_cast<uint8_t>(type));
    i.id = id;
    i.text = text;
    bridge_->pushIntent(std::move(i));
    return true;
}

bool ApplicationScenarioWorld::ActivateHotbarSlot(int page, int slot) {
    // The click route reads the slot's OWN content and fires it -- exactly
    // what static_ui_input.cpp does for a slot press. A slot the model cannot
    // read is "the world cannot click".
    auto* model = client_.GetHotbarModel();
    if (!model || !bridge_) return false;
    const eqt::HotbarSlot& s = model->getSlot({page, slot});
    eqt::events::HotbarActivateIntent i;
    i.index = slot;
    i.type = s.type;
    i.id = s.id;
    i.emoteText = s.emoteText;
    bridge_->pushIntent(std::move(i));
    return true;
}

bool ApplicationScenarioWorld::PressHotbarKey(int slot) {
    // The key route: a keypress the game thread drains through
    // InputActionBridge::processHotbarRequests (reads the CURRENT page's slot).
    // AutomatedMode owns the NullInputHandler that path consumes; injecting
    // here is the programmatic form of the H-4 key binding firing.
    auto* gm = app_.getGameMode();
    if (!gm || gm->getMode() != mode::OperatingMode::Automated) return false;
    static_cast<mode::AutomatedMode*>(gm)->input().injectHotbar(
        static_cast<uint8_t>(slot));
    return true;
}

bool ApplicationScenarioWorld::SetHotbarPage(int page) {
    if (!bridge_) return false;
    bridge_->pushIntent(eqt::events::HotbarPageChangedIntent{page});
    return true;
}

HotbarSlotRead ApplicationScenarioWorld::HotbarSlotAt(int page, int slot) const {
    HotbarSlotRead out;
    auto* model = client_.GetHotbarModel();
    if (!model) return out;
    const eqt::HotbarSlot& s = model->getSlot({page, slot});
    out.type = static_cast<int>(s.type);
    out.id = s.id;
    out.text = s.emoteText;
    return out;
}

}  // namespace scenario
}  // namespace eqt
