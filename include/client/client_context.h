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
// K13: the three context SERVICES. They are held by reference like every other
// ClientContext member, so a component reaches them as ctx_.out / ctx_.self /
// ctx_.net without knowing EverQuest exists.
#include "client/chat_output.h"
#include "client/packet_sender.h"
#include "client/player_facade.h"

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
class HotbarModel;
// K13: the twelve sibling components. Forward declarations only -- the
// accessors below bind a reference to *pointer, which needs no complete type,
// and every TU that actually CALLS one already includes eq.h (which includes
// all twelve component headers).
namespace guild { class GuildComponent; }
namespace gm { class GmComponent; }
namespace chat { class ChatComponent; }
namespace group { class GroupComponent; }
namespace merchant { class MerchantComponent; }
namespace entities { class EntitiesComponent; }
namespace movement { class MovementComponent; }
namespace login_world { class LoginWorldComponent; }
namespace player { class PlayerComponent; }
namespace zone { class ZoneComponent; }
namespace bridge { class BridgeComponent; }
namespace commands { class CommandsComponent; }
}

#ifdef WITH_AUDIO
namespace EQT {
namespace Audio {
class AudioManager;
class ZoneAudioManager;
}
}
#endif

namespace EQT {
namespace Net {
class SessionRecorder;
}
}

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

	// ------------------------------------------------------------------
	// K13 (Batch K, refactor/K13_context_services.md): the three CONTEXT
	// SERVICES. Each is an EverQuest member declared just above m_ctx, bound
	// here by reference exactly like the fourteen core members above.
	//
	// out  -- the three AddChat* output primitives (was 303 owner_. uses)
	// self -- the player-state facade over GameState (was 534)
	// net  -- QueuePacket/SafeQueueZonePacket + the replay counter (was 53)
	eqt::ChatOutput& out;
	eqt::PlayerFacade& self;
	eqt::PacketSender& net;

	// K13: four more shared members promoted out of the owner_.m_* residue.
	// Each was reached through the transition handle by two or three families
	// and belongs to none of them: they are ctor-wired subsystem handles and
	// process-wide config, which is what ClientContext is for. (K11 recorded
	// this decision for hotbar_model in as many words -- "it belongs in
	// ClientContext at K13, not in one family".)
	std::unique_ptr<eqt::HotbarModel>& hotbar_model;
	std::string& eq_client_path;
	std::unique_ptr<EQT::Net::SessionRecorder>& session_recorder;
#ifdef WITH_AUDIO
	std::unique_ptr<EQT::Audio::ZoneAudioManager>& zone_audio_manager;
#endif

	// ------------------------------------------------------------------
	// K13: the twelve SIBLING COMPONENTS.
	//
	// These are raw pointers, not references, and they are NULL until
	// EverQuest's constructor has built all twelve and called
	// SetComponents(). That late binding is deliberate: the components are
	// constructed in dependency order in the ctor BODY (m_ctx itself is built
	// in the initialiser list, before any of them exist), so a reference
	// member could not be bound. Nothing reads a component pointer during
	// construction.
	//
	// A component reaches a sibling as ctx_.Zone(), ctx_.Bridge(), ... --
	// the same spelling the transition handle used (owner_.Zone()), with the
	// receiver changed. 338 owner_. uses became these.
	void SetComponents(guild::GuildComponent* g, gm::GmComponent* gm_,
	                   chat::ChatComponent* c, group::GroupComponent* gr,
	                   merchant::MerchantComponent* m, entities::EntitiesComponent* e,
	                   movement::MovementComponent* mv, login_world::LoginWorldComponent* lw,
	                   player::PlayerComponent* p, zone::ZoneComponent* z,
	                   bridge::BridgeComponent* b, commands::CommandsComponent* cm) {
		guild_ = g; gm_component_ = gm_; chat_ = c; group_ = gr; merchant_ = m;
		entities_component_ = e; movement_ = mv; login_world_ = lw; player_ = p;
		zone_ = z; bridge_component_ = b; commands_ = cm;
	}

	guild::GuildComponent& Guild() const { return *guild_; }
	gm::GmComponent& Gm() const { return *gm_component_; }
	chat::ChatComponent& Chat() const { return *chat_; }
	group::GroupComponent& Group() const { return *group_; }
	merchant::MerchantComponent& Merchant() const { return *merchant_; }
	entities::EntitiesComponent& Entities() const { return *entities_component_; }
	movement::MovementComponent& Movement() const { return *movement_; }
	login_world::LoginWorldComponent& LoginWorld() const { return *login_world_; }
	player::PlayerComponent& Player() const { return *player_; }
	zone::ZoneComponent& Zone() const { return *zone_; }
	bridge::BridgeComponent& Bridge() const { return *bridge_component_; }
	commands::CommandsComponent& Commands() const { return *commands_; }

	// Public only because ClientContext is an AGGREGATE -- EverQuest builds it
	// with brace initialisation in its ctor initialiser list, and an aggregate
	// may not have private data members. Set by SetComponents() and read
	// through the accessors above; nothing else touches them.
	guild::GuildComponent* guild_ = nullptr;
	gm::GmComponent* gm_component_ = nullptr;
	chat::ChatComponent* chat_ = nullptr;
	group::GroupComponent* group_ = nullptr;
	merchant::MerchantComponent* merchant_ = nullptr;
	entities::EntitiesComponent* entities_component_ = nullptr;
	movement::MovementComponent* movement_ = nullptr;
	login_world::LoginWorldComponent* login_world_ = nullptr;
	player::PlayerComponent* player_ = nullptr;
	zone::ZoneComponent* zone_ = nullptr;
	bridge::BridgeComponent* bridge_component_ = nullptr;
	commands::CommandsComponent* commands_ = nullptr;
};

}  // namespace eqt
