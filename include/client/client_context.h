#pragma once

// Batch K (refactor/K_component_extraction.md), unit K01.
//
// ClientContext is the bundle of shared services every extracted
// <Family>Component receives by reference. Its membership is the measured
// "core" of class EverQuest: the fifteen m_* data members referenced by five
// or more of the eq_*.cpp TU families (see the batch doc's
// "Data-member coupling" table).
//
// Each member is a REFERENCE to the EverQuest member of the SAME type -- a
// std::unique_ptr<X> member becomes std::unique_ptr<X>&, not X&. Nothing is
// dereferenced here: a component sees exactly what the code it was moved out
// of saw, so a moved body's `m_spell_manager->Foo()` becomes
// `ctx_.spell_manager->Foo()` and nothing else changes.
//
// EverQuest owns one of these (EverQuest::m_ctx), built in its constructor's
// initialiser list from its own members. It is not copyable or assignable by
// construction (reference members), which is what we want: there is exactly
// one per EverQuest.

#include "client/eq_types.h"   // Entity, and (transitively) eqt::state::GameState

#include <cstdint>
#include <map>
#include <memory>
#include <string>

class CombatManager;
class TradeManager;

namespace EQ {
class SpellManager;
class BuffManager;
class SkillManager;
namespace Net {
class DaybreakConnection;
class DaybreakConnectionManager;
}
}

namespace eqt {
namespace bridge {
class GameStateBridge;
}
namespace inventory {
class InventoryManager;
}
namespace ui {
class CommandRegistry;
}
}

#ifdef WITH_AUDIO
namespace EQT {
namespace Audio {
class AudioManager;
}
}
#endif

namespace eqt {

struct ClientContext {
	// 13 families
	eqt::state::GameState& game_state;
	// 12 families. A raw pointer member on EverQuest, reassigned by
	// setBridge(), so this is a reference TO THE POINTER -- a component must
	// see a later setBridge() the same way an EverQuest method does.
	eqt::bridge::GameStateBridge*& bridge;
	// 11 families
	std::shared_ptr<EQ::Net::DaybreakConnection>& zone_connection;
	std::map<uint16_t, Entity>& entities;
	std::string& character;
	// 9 families
	std::unique_ptr<CombatManager>& combat_manager;
#ifdef WITH_AUDIO
	// 8 families. EverQuest's m_audio_manager is itself #ifdef WITH_AUDIO, so
	// this member cannot be unconditional (K01 correction 1).
	std::unique_ptr<EQT::Audio::AudioManager>& audio_manager;
#endif
	// 7 families
	std::unique_ptr<TradeManager>& trade_manager;
	std::unique_ptr<EQ::SpellManager>& spell_manager;
	std::unique_ptr<eqt::inventory::InventoryManager>& inventory_manager;
	// 6 families
	std::unique_ptr<EQ::SkillManager>& skill_manager;
	// 5 families
	std::unique_ptr<EQ::Net::DaybreakConnectionManager>& zone_connection_manager;
	std::unique_ptr<eqt::ui::CommandRegistry>& command_registry;
	std::unique_ptr<EQ::BuffManager>& buff_manager;
	uint32_t& active_tradeskill_object_id;
};

}  // namespace eqt
