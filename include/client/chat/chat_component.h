#pragma once

// Batch K (refactor/K_component_extraction.md), unit K03.
//
// The chat family, lifted out of class EverQuest. Every body in
// src/client/chat/chat_component.cpp is the body that used to live in
// src/client/eq_chat.cpp, moved verbatim; the only edits inside a body are
// the substitutions §Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo(), an EverQuest static -> EverQuest::Static).
//
// What did NOT move: AddChatSystemMessage/AddChatCombatMessage/
// AddChatMissMessage. Those three are OUTPUT PRIMITIVES with 30+ callers
// across every family -- the same shape as QueuePacket -- so they stay on
// EverQuest (and in eq_chat.cpp) for now and K-final decides whether they
// become ClientContext services. That is why eq_chat.cpp was split rather
// than moved whole.
//
// `owner_` is the TRANSITION handle: the moved code still reaches EverQuest
// members and methods that have not been extracted yet (m_last_tell_sender,
// m_string_db, AddChatSystemMessage(), GetStringMessage()). EverQuest
// declares `friend class eqt::chat::ChatComponent` for exactly that.
// K-final removes both the handle and the friendship.

#include "client/client_context.h"
#include "client/eq_enums.h"

#include <functional>
#include <string>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace chat {

class ChatComponent {
public:
	ChatComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

	// Public chat interface. Were public on EverQuest; K03 deleted those
	// declarations and retargeted the callers.
	void SendChatMessage(const std::string &message, const std::string &channel_name, const std::string &target = "");
	// T243: /emote wire form -- OP_Emote + Emote_Struct, plus the local echo
	// the server deliberately does not send back to the actor.
	void ZoneSendEmote(const std::string &message);
	void ProcessChatInput(const std::string &input);  // Process chat window input (commands or messages)
	// T29: the first stage of the unified slash-command dispatch order
	// (registry 1: EverQuest::m_command_registry -- see docs/decisions.md
	// 2026-08-02 and refactor/T29_unified_command_channel.md). Tries only
	// this registry; does not touch chat/say fallback or emit an "unknown
	// command" message -- that's ProcessChatInput's job for the live chat
	// window, and Application::DispatchSlashCommand's job for stdin/script.
	// Returns true iff m_command_registry recognised and executed cmdLine
	// (cmdLine has NO leading '/'). Safe to call unconditionally (headless
	// or graphics) -- the registry itself is no longer graphics-gated.
	bool DispatchSlashCommand(const std::string &cmdLine);
	// T29: invoked by ProcessChatInput() when DispatchSlashCommand() (registry
	// 1) doesn't recognise a command typed in the live chat window -- lets
	// the SAME registries 2/3 that stdin/--script use (see
	// Application::dispatchToActionOrRenderer(), wired in by
	// Application::wireUnifiedCommandDispatch()) also be reached from the
	// chat window, without EverQuest needing to know about Application,
	// CommandProcessor, or the renderer bridge's ownership. cmdLine has no
	// leading '/'. Returns true if handled.
	void SetUnrecognizedCommandFallback(std::function<bool(const std::string&)> fallback) {
		m_unrecognized_command_fallback = std::move(fallback);
	}

	// Wire senders. Were private on EverQuest; only this family calls them.
	void ZoneSendChannelMessage(const std::string &message, ChatChannelType channel, const std::string &target = "");

	// Packet handlers. Were private on EverQuest; DispatchZonePacket
	// (eq_zone_dispatch.cpp) now calls m_chat->ZoneProcess*() directly.
	void ZoneProcessChannelMessage(const EQ::Net::Packet &p);
	void ZoneProcessEmote(const EQ::Net::Packet &p);      // OP_Emote text (T243)
	void ZoneProcessColoredText(const EQ::Net::Packet &p);
	void ZoneProcessFormattedMessage(const EQ::Net::Packet &p);
	void ZoneProcessSimpleMessage(const EQ::Net::Packet &p);

private:
	ClientContext& ctx_;
	EverQuest& owner_;

	// Chat state, moved out of EverQuest: referenced by this family only.
	// T29: see SetUnrecognizedCommandFallback() above.
	std::function<bool(const std::string&)> m_unrecognized_command_fallback;
};

}  // namespace chat
}  // namespace eqt
