// The chat output primitives, as a context service.
//
// K13 (Batch K, refactor/K_component_extraction.md): the three bodies below
// came from src/client/eq_chat.cpp VERBATIM -- they were
// EverQuest::AddChatSystemMessage / AddChatCombatMessage / AddChatMissMessage,
// left there by K03 as "output primitives with 30+ callers across every
// family ... they stay on EverQuest until K-final decides whether they become
// ClientContext services". This is that decision. Not one code line changed:
// the member they read is still spelled m_bridge, because ChatOutput holds a
// reference to EverQuest's m_bridge under the same name.
//
// EverQuest::AddChat*() still exist, as one-line forwarders, for the callers
// outside the EverQuest family (see include/client/chat_output.h).

#include "client/chat_output.h"

#include "common/logging.h"
#include "client/bridge/game_state_bridge.h"
// P28: a plain data-model header (chat channel enum/buffer) with no renderer
// dependency — used unconditionally below (ChatChannel). Un-guarded to match
// its declassified eq.h forward decl.
#include "client/chat/chat_message_buffer.h"

void eqt::ChatOutput::System(const std::string &text)
{
	// Publish SystemMessage event to bridge
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::System);
		data.channelName = "system";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SystemMessage, std::move(data)));
	}
	// Also print to console
	LOG_INFO(MOD_MAIN, "{}", text);
}

void eqt::ChatOutput::Combat(const std::string &text, bool is_self)
{
	// Publish ChatMessage event to bridge (combat channel)
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(is_self ? eqt::ui::ChatChannel::CombatSelf : eqt::ui::ChatChannel::Combat);
		data.channelName = is_self ? "combat_self" : "combat";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
	// Also print to console
	LOG_DEBUG(MOD_COMBAT, "{}", text);
}

void eqt::ChatOutput::Miss(const std::string &text)
{
	// D20e1: Bridge already handles ChatMessage → chat window display.
	// Publish ChatMessage event to bridge (combat miss channel)
	if (m_bridge) {
		eqt::state::ChatMessageData data;
		data.message = text;
		data.channelType = static_cast<uint32_t>(eqt::ui::ChatChannel::CombatMiss);
		data.channelName = "combat_miss";
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ChatMessage, std::move(data)));
	}
	LOG_DEBUG(MOD_COMBAT, "{}", text);
}
