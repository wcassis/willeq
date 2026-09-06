// The player-state FACADE, as a context service.
//
// K13 (Batch K, refactor/K_component_extraction.md): every body below came
// from src/client/eq_player_info.cpp VERBATIM. They were EverQuest's, left
// there by K08 with the note "K13 decides whether the facade becomes a
// ClientContext service" -- this is that decision. Not one code line changed:
// the member they read is still spelled m_game_state, because PlayerFacade
// holds a reference to EverQuest's m_game_state under the same name. Only the
// class-qualifier on each definition is different.
//
// EverQuest::GetLevel()/GetPosition()/... still exist, as one-line forwarders
// (src/client/eq_player_info.cpp), for the callers outside the EverQuest
// family. See include/client/player_facade.h.

#include "client/player_facade.h"

#include <glm/glm.hpp>

// ============================================================================
// Cluster 2: Player stat getters
// ============================================================================

uint8_t eqt::PlayerFacade::GetLevel() const
{
	return m_game_state.player().level();
}

uint32_t eqt::PlayerFacade::GetClass() const
{
	return m_game_state.player().classId();
}

uint32_t eqt::PlayerFacade::GetRace() const
{
	return m_game_state.player().race();
}

uint32_t eqt::PlayerFacade::GetGender() const
{
	return m_game_state.player().gender();
}

uint32_t eqt::PlayerFacade::GetSTR() const
{
	return m_game_state.player().STR();
}

uint32_t eqt::PlayerFacade::GetSTA() const
{
	return m_game_state.player().STA();
}

uint32_t eqt::PlayerFacade::GetDEX() const
{
	return m_game_state.player().DEX();
}

uint32_t eqt::PlayerFacade::GetAGI() const
{
	return m_game_state.player().AGI();
}

uint32_t eqt::PlayerFacade::GetINT() const
{
	return m_game_state.player().INT();
}

uint32_t eqt::PlayerFacade::GetWIS() const
{
	return m_game_state.player().WIS();
}

uint32_t eqt::PlayerFacade::GetCHA() const
{
	return m_game_state.player().CHA();
}

uint32_t eqt::PlayerFacade::GetCurrentHP() const
{
	return m_game_state.player().curHP();
}

uint32_t eqt::PlayerFacade::GetMaxHP() const
{
	return m_game_state.player().maxHP();
}

uint32_t eqt::PlayerFacade::GetCurrentMana() const
{
	return m_game_state.player().curMana();
}

uint32_t eqt::PlayerFacade::GetMaxMana() const
{
	return m_game_state.player().maxMana();
}

uint32_t eqt::PlayerFacade::GetCurrentEndurance() const
{
	return m_game_state.player().curEndurance();
}

uint32_t eqt::PlayerFacade::GetMaxEndurance() const
{
	return m_game_state.player().maxEndurance();
}

uint32_t eqt::PlayerFacade::GetDeity() const
{
	return m_game_state.player().deity();
}

uint32_t eqt::PlayerFacade::GetPlatinum() const
{
	return m_game_state.player().platinum();
}

uint32_t eqt::PlayerFacade::GetGold() const
{
	return m_game_state.player().gold();
}

uint32_t eqt::PlayerFacade::GetSilver() const
{
	return m_game_state.player().silver();
}

uint32_t eqt::PlayerFacade::GetCopper() const
{
	return m_game_state.player().copper();
}

uint32_t eqt::PlayerFacade::GetBankPlatinum() const
{
	return m_game_state.player().bankPlatinum();
}

uint32_t eqt::PlayerFacade::GetBankGold() const
{
	return m_game_state.player().bankGold();
}

uint32_t eqt::PlayerFacade::GetBankSilver() const
{
	return m_game_state.player().bankSilver();
}

uint32_t eqt::PlayerFacade::GetBankCopper() const
{
	return m_game_state.player().bankCopper();
}

uint32_t eqt::PlayerFacade::GetPracticePoints() const
{
	return m_game_state.player().practicePoints();
}

float eqt::PlayerFacade::GetWeight() const
{
	return m_game_state.player().weight();
}

float eqt::PlayerFacade::GetMaxWeight() const
{
	return m_game_state.player().maxWeight();
}
// ============================================================================
// Movement State Accessors (Phase 7.1 - use embedded m_game_state)
// ============================================================================

glm::vec3 eqt::PlayerFacade::Test_GetGameStatePlayerPosition() const
{
	// T55 test seam: read the state layer's own copy directly (see eq.h).
	const auto& player = m_game_state.player();
	return glm::vec3(player.x(), player.y(), player.z());
}

glm::vec3 eqt::PlayerFacade::GetPosition() const
{
	// T288: reads the one store (m_game_state.player()), the same storage
	// Test_GetGameStatePlayerPosition() reads.
	return m_game_state.player().position();
}

float eqt::PlayerFacade::GetHeading() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().heading();
}

bool eqt::PlayerFacade::IsMoving() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().isMoving();
}

void eqt::PlayerFacade::SetHeading(float heading)
{
	// T288: one store. setHeading() fires PlayerMoved on the state bus, as
	// it always has -- this is the discrete-relocation path, not the
	// per-tick integrator path (SetHeadingLocal()).
	m_game_state.player().setHeading(heading);
}

void eqt::PlayerFacade::SetPosition(float x, float y, float z)
{
	// T288: one store. setPosition() fires PlayerMoved on the state bus, as
	// it always has -- this is the discrete-relocation path, not the
	// per-tick integrator path (SetPosLocal()).
	m_game_state.player().setPosition(x, y, z);
	// T404 (board B251): and the self row in the entity map, which every
	// range check and positioned sound reads. See player_facade.h.
	MirrorSelfEntityPosition(x, y, z);
}

void eqt::PlayerFacade::SetMoving(bool moving)
{
	m_game_state.player().setMoving(moving);  // T286: only store
}

// ============================================================================
// Behavioral Flag Accessors (Phase 7.8 - read from PlayerState)
// ============================================================================

bool eqt::PlayerFacade::IsAFK() const
{
	return m_game_state.player().isAFK();
}

bool eqt::PlayerFacade::IsAnonymous() const
{
	return m_game_state.player().isAnonymous();
}

bool eqt::PlayerFacade::IsRoleplay() const
{
	return m_game_state.player().isRoleplay();
}

bool eqt::PlayerFacade::IsCamping() const
{
	return m_game_state.player().isCamping();
}

// ============================================================================
// Group Accessors and Sync Helper (Phase 7.5 - read from GroupState)
// ============================================================================

bool eqt::PlayerFacade::IsInGroup() const
{
	return m_game_state.group().inGroup();
}

bool eqt::PlayerFacade::IsGroupLeader() const
{
	return m_game_state.group().isLeader();
}

int eqt::PlayerFacade::GetGroupMemberCount() const
{
	return m_game_state.group().memberCount();
}

const std::string& eqt::PlayerFacade::GetGroupLeaderName() const
{
	return m_game_state.group().leaderName();
}

bool eqt::PlayerFacade::HasPendingGroupInvite() const
{
	return m_game_state.group().hasPendingInvite();
}

const std::string& eqt::PlayerFacade::GetPendingInviterName() const
{
	return m_game_state.group().pendingInviterName();
}

// T286: SyncGroupMemberToGameState() is DELETED. It existed only to copy
// EverQuest's own GroupMember array into GroupState's after every write;
// with one array and one struct there is nothing left to convert.

