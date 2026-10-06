// B998 (GitLab #318) -- the character-select screen's half of
// LoginWorldComponent: Enter World from the screen. Its own TU (M00). The
// list itself is T14's (WorldProcessCharacterSelect); creation and deletion
// are B888's (login_world_char_create.cpp) and the screen reaches them
// through the same RequestCreateCharacter / RequestDeleteCharacter /
// RequestRandomName bodies the world script uses.

#include "client/eq.h"
#include "client/login_world/login_world_component.h"

#include "common/logging.h"

namespace eqt {
namespace login_world {

bool LoginWorldComponent::EnterWorldFromSelect(const std::string& name) {
	if (m_enter_world_sent) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': OP_EnterWorld was already sent", name);
		return false;
	}
	if (m_create_status.stage != CharCreateStatus::Stage::Idle) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': a create/delete is in flight", name);
		return false;
	}
	if (!CharacterListHas(name)) {
		LOG_WARN(MOD_WORLD, "[CHARSELECT] Enter World '{}': not on this account's character list", name);
		return false;
	}
	// The selected character IS the configured one from here on, exactly as
	// after a create (EnterWorldAfterCreate): the zoning world pass
	// (WorldProcessPostEnterWorld) and every per-character file key off it.
	ctx_.character = name;
	for (const auto& e : m_character_select_list) {
		if (e.name == name) m_character_select_index = e.slot;
	}
	if (m_created_this_session) {
		// World set StartInTutorial on the create and clears it only on CRC1
		// (see m_created_this_session): never enter the tutorial by accident.
		WorldSendWorldClientCRC();
	}
	LOG_INFO(MOD_WORLD, "[CHARSELECT] Enter World: {}", name);
	WorldSendEnterWorld(name);
	return true;
}

}  // namespace login_world
}  // namespace eqt
