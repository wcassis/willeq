// B442 (GitLab #151): CommandsComponent::RequestCursorCommand -- /pickup,
// /putdown, /cursor and /coin. A new TU beside commands_component.cpp (which
// is near the size ceiling). Parsing is eqt::inventory::ParseCursorCommand()
// (cursor_gesture.cpp, slot numbers through parseUserSlot()); the move itself
// is the same CursorGestureIntent a slot click pushes, drained by
// BridgeComponent::onCursorGestureIntent() -> RunCursorGesture(), which
// prints the outcome because the intent carries echo = true.
#include "client/eq.h"
#include "client/commands/commands_component.h"
#include "client/bridge/game_state_bridge.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_manager.h"

void eqt::commands::CommandsComponent::RequestCursorCommand(const std::string& verb,
                                                            const std::string& args)
{
	if (verb == "cursor") {
		ctx_.out.System(ctx_.inventory_manager
			? eqt::inventory::DescribeCursor(*ctx_.inventory_manager)
			: std::string("Inventory not available."));
		return;
	}
	const eqt::inventory::CursorCommandParse parsed =
		eqt::inventory::ParseCursorCommand(verb, args);
	if (!parsed.ok) {
		ctx_.out.System(parsed.error);
		return;
	}
	if (!ctx_.bridge) {
		ctx_.out.System("Cannot move items - no bridge attached.");
		return;
	}
	ctx_.bridge->pushIntent(parsed.intent);
}
