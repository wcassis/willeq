// EverQuest chat output primitives.
//
// Split out of eq.cpp (Batch F, unit F06) as the whole chat and text-message
// cluster. K03 (Batch K, refactor/K_component_extraction.md) moved that
// cluster into eqt::chat::ChatComponent (src/client/chat/chat_component.cpp)
// and left exactly these three behind: AddChatSystemMessage,
// AddChatCombatMessage and AddChatMissMessage are OUTPUT PRIMITIVES with 30+
// callers across every family -- the same shape as QueuePacket -- so they
// stay on EverQuest until K-final decides whether they become ClientContext
// services. See refactor/F06_chat.md and refactor/K03_chat_component.md.

#include "client/eq.h"
#include "common/logging.h"
#include "client/bridge/game_state_bridge.h"
// P28: a plain data-model header (chat channel enum/buffer) with no renderer
// dependency — used unconditionally below (ChatChannel). Un-guarded to match
// its declassified eq.h forward decl.
#include "client/chat/chat_message_buffer.h"

void EverQuest::AddChatSystemMessage(const std::string &text)
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

void EverQuest::AddChatCombatMessage(const std::string &text, bool is_self)
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

void EverQuest::AddChatMissMessage(const std::string &text)
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

// T156: the "Hotbar Button Management" block (AddPendingHotbarButton and
// friends) lived here and had zero callers in the whole tree. Deleted with
// eqt::ui::HotbarButtonType/PendingHotbarButton — see include/client/eq.h.
