// EverQuest chat output primitives -- now three forwarders.
//
// Split out of eq.cpp (Batch F, unit F06) as the whole chat and text-message
// cluster. K03 (Batch K, refactor/K_component_extraction.md) moved that
// cluster into eqt::chat::ChatComponent (src/client/chat/chat_component.cpp)
// and left exactly these three behind.
//
// K13 moved the BODIES into eqt::ChatOutput (src/client/chat_output.cpp), the
// context service the twelve components reach as ctx_.out. What is left here
// is the sanctioned forwarder set: the callers OUTSIDE the EverQuest family
// (the action layer, spell_manager, skill_manager, inventory_manager,
// application_commands) keep the EverQuest receiver they always had. That is
// the plan's explicit exception to §Mechanism 5's no-forwarder rule; K14
// decides whether the surface goes away entirely.

#include "client/eq.h"

void EverQuest::AddChatSystemMessage(const std::string &text)
{
	m_chat_output.System(text);
}

void EverQuest::AddChatCombatMessage(const std::string &text, bool is_self)
{
	m_chat_output.Combat(text, is_self);
}

void EverQuest::AddChatMissMessage(const std::string &text)
{
	m_chat_output.Miss(text);
}

// T156: the "Hotbar Button Management" block (AddPendingHotbarButton and
// friends) lived here and had zero callers in the whole tree. Deleted with
// eqt::ui::HotbarButtonType/PendingHotbarButton — see include/client/eq.h.
