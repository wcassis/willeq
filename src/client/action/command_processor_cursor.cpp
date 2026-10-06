// B442 (GitLab #151): /pickup, /putdown, /cursor and /coin -- the scriptable
// half of the click-to-cursor gesture. Raw arguments pass straight through
// (the cmdEquip idiom); parsing and every refusal live in
// CommandsComponent::RequestCursorCommand() -> eqt::inventory::
// ParseCursorCommand(), which pushes the SAME CursorGestureIntent a slot
// click pushes.
#include "client/action/command_processor.h"

namespace eqt {
namespace action {

ActionResult CommandProcessor::cmdPickup(const std::string& args) {
    return m_dispatcher.cursorCommand("pickup", args);
}

ActionResult CommandProcessor::cmdPutdown(const std::string& args) {
    return m_dispatcher.cursorCommand("putdown", args);
}

ActionResult CommandProcessor::cmdCursor(const std::string& args) {
    return m_dispatcher.cursorCommand("cursor", args);
}

ActionResult CommandProcessor::cmdCoin(const std::string& args) {
    return m_dispatcher.cursorCommand("coin", args);
}

}  // namespace action
}  // namespace eqt
