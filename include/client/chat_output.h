#pragma once

// Batch K (refactor/K_component_extraction.md), unit K13.
//
// ChatOutput is the first of the three CONTEXT SERVICES: the shared output
// primitive every family reaches for, lifted off class EverQuest so a
// component no longer needs the EverQuest& transition handle to print.
//
// The three bodies (AddChatSystemMessage / AddChatCombatMessage /
// AddChatMissMessage) are the ones K03 deliberately left behind in
// src/client/eq_chat.cpp -- "output primitives with 30+ callers across every
// family". Measured at this unit's tip they carry 303 of the 1,425 owner_.
// uses in the twelve components, by far the largest single group. They move
// here verbatim; the only thing that changed is the receiver.
//
// The service owns NOTHING. It holds a reference to EverQuest's bridge
// POINTER member (m_bridge is reassigned by setBridge(), so a reference to
// the pointer is required for the same reason ClientContext::bridge is one),
// and that is its entire state.
//
// EverQuest keeps AddChatSystemMessage/AddChatCombatMessage/AddChatMissMessage
// as forwarders for the NON-component callers (the action layer,
// spell_manager, skill_manager, inventory_manager, application_commands) --
// the plan's explicit exception to §Mechanism 5's no-forwarder rule, because
// those callers are not EverQuest-family code and retargeting them is K14's
// question, not this unit's.

#include <string>

namespace eqt {
namespace bridge {
class GameStateBridge;
}

class ChatOutput {
public:
	explicit ChatOutput(eqt::bridge::GameStateBridge*& bridge) : m_bridge(bridge) {}

	// Add system message to chat window.
	void System(const std::string &text);
	// Add combat message (is_self for player damage).
	void Combat(const std::string &text, bool is_self = false);
	// Add miss message (filterable separately).
	void Miss(const std::string &text);

private:
	// Reference TO EverQuest's bridge pointer, not to the bridge.
	eqt::bridge::GameStateBridge*& m_bridge;
};

}  // namespace eqt
