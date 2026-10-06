// command_processor_friends.cpp - the friends and ignore family (B897,
// GitLab #213): /friends (/friend), /ignore and /friendswho.
//
// Every body here is a thin caller of the free functions in
// include/client/state/social_lists.h -- the SAME ones the Friends window's
// SocialListIntent handler runs (bridge_component_intents.cpp) -- so a slash
// command and a button click edit the same model and print the same oracle
// strings. The edit lands in state::GameState::social() (the ONE instance,
// T227); persistence and the window refresh happen once per tick in
// BridgeComponent::SyncSocialLists(), never here.

#include "client/action/command_processor.h"
#include "client/state/game_state.h"
#include "client/state/social_lists.h"

namespace eqt {
namespace action {

namespace {

ActionResult Emit(const eqt::state::SocialOutcome& outcome,
                  const std::function<void(const std::string&)>& display) {
    for (const auto& line : outcome.lines) display(line);
    return ActionResult::Success();
}

}  // namespace

ActionResult CommandProcessor::cmdFriends(const std::string& args) {
    const auto outcome = eqt::state::RunFriendsCommand(
        m_state.social(), args, m_state.player().name());
    return Emit(outcome, [this](const std::string& s) { displayMessage(s); });
}

ActionResult CommandProcessor::cmdIgnore(const std::string& args) {
    const auto outcome = eqt::state::RunIgnoreCommand(
        m_state.social(), args, m_state.player().name());
    return Emit(outcome, [this](const std::string& s) { displayMessage(s); });
}

ActionResult CommandProcessor::cmdFriendsWho(const std::string& args) {
    // No name resolution: the server takes NAMES for this opcode and reports
    // on characters in other zones, so an entity-list lookup would be wrong.
    // No argument asks about the stored friends list (B897); an argument is
    // still sent verbatim, as before.
    const auto outcome = eqt::state::RunFriendsWho(m_state.social(), args);
    for (const auto& line : outcome.lines) displayMessage(line);
    if (outcome.whoList.empty()) return ActionResult::Success();
    return m_dispatcher.friendsWho(outcome.whoList);
}

}  // namespace action
}  // namespace eqt
