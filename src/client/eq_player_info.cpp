// EverQuest's PLAYER-STATE FACADE: the thin accessors over m_game_state that
// the whole tree reads -- stats, currency, weight, position/heading/moving,
// the behavioural flags, and the group accessors.
//
// K08 (Batch K, refactor/K_component_extraction.md) left these on EverQuest
// deliberately. Every other body that used to live in eq_player_{info,misc,
// social,state,buffs}.cpp moved into eqt::player::PlayerComponent
// (src/client/player/); these did not, for the same reason K03 left the three
// AddChat* primitives behind: they are not a family, they are the read surface
// every family uses (GetPosition() alone has callers in combat, spells, skills
// and the action handlers). K13 decides whether the facade becomes a
// ClientContext service.
//
// The position/heading/moving/flag accessors were carried here verbatim from
// eq_player_state.cpp, whose remainder became src/client/player/player_state.cpp.
//
// Clusters 1-3, split out of eq.cpp by Batch F unit F07 and narrowed to these
// clusters by Batch J unit J07. Moved verbatim, no behavior change. See
// refactor/J07_eq_player_info_split.md and refactor/F07_player_info.md.

#include "client/eq.h"

#include <glm/glm.hpp>

// ============================================================================
// Cluster 2: Player stat getters
// ============================================================================

uint8_t EverQuest::GetLevel() const
{
	return m_game_state.player().level();
}

uint32_t EverQuest::GetClass() const
{
	return m_game_state.player().classId();
}

uint32_t EverQuest::GetRace() const
{
	return m_game_state.player().race();
}

uint32_t EverQuest::GetGender() const
{
	return m_game_state.player().gender();
}

uint32_t EverQuest::GetSTR() const
{
	return m_game_state.player().STR();
}

uint32_t EverQuest::GetSTA() const
{
	return m_game_state.player().STA();
}

uint32_t EverQuest::GetDEX() const
{
	return m_game_state.player().DEX();
}

uint32_t EverQuest::GetAGI() const
{
	return m_game_state.player().AGI();
}

uint32_t EverQuest::GetINT() const
{
	return m_game_state.player().INT();
}

uint32_t EverQuest::GetWIS() const
{
	return m_game_state.player().WIS();
}

uint32_t EverQuest::GetCHA() const
{
	return m_game_state.player().CHA();
}

uint32_t EverQuest::GetCurrentHP() const
{
	return m_game_state.player().curHP();
}

uint32_t EverQuest::GetMaxHP() const
{
	return m_game_state.player().maxHP();
}

uint32_t EverQuest::GetCurrentMana() const
{
	return m_game_state.player().curMana();
}

uint32_t EverQuest::GetMaxMana() const
{
	return m_game_state.player().maxMana();
}

uint32_t EverQuest::GetCurrentEndurance() const
{
	return m_game_state.player().curEndurance();
}

uint32_t EverQuest::GetMaxEndurance() const
{
	return m_game_state.player().maxEndurance();
}

uint32_t EverQuest::GetDeity() const
{
	return m_game_state.player().deity();
}

uint32_t EverQuest::GetPlatinum() const
{
	return m_game_state.player().platinum();
}

uint32_t EverQuest::GetGold() const
{
	return m_game_state.player().gold();
}

uint32_t EverQuest::GetSilver() const
{
	return m_game_state.player().silver();
}

uint32_t EverQuest::GetCopper() const
{
	return m_game_state.player().copper();
}

uint32_t EverQuest::GetBankPlatinum() const
{
	return m_game_state.player().bankPlatinum();
}

uint32_t EverQuest::GetBankGold() const
{
	return m_game_state.player().bankGold();
}

uint32_t EverQuest::GetBankSilver() const
{
	return m_game_state.player().bankSilver();
}

uint32_t EverQuest::GetBankCopper() const
{
	return m_game_state.player().bankCopper();
}

uint32_t EverQuest::GetPracticePoints() const
{
	return m_game_state.player().practicePoints();
}

float EverQuest::GetWeight() const
{
	return m_game_state.player().weight();
}

float EverQuest::GetMaxWeight() const
{
	return m_game_state.player().maxWeight();
}
// ============================================================================
// Movement State Accessors (Phase 7.1 - use embedded m_game_state)
// ============================================================================

glm::vec3 EverQuest::Test_GetGameStatePlayerPosition() const
{
	// T55 test seam: read the state layer's own copy directly (see eq.h).
	const auto& player = m_game_state.player();
	return glm::vec3(player.x(), player.y(), player.z());
}

glm::vec3 EverQuest::GetPosition() const
{
	// T288: reads the one store (m_game_state.player()), the same storage
	// Test_GetGameStatePlayerPosition() reads.
	return m_game_state.player().position();
}

float EverQuest::GetHeading() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().heading();
}

bool EverQuest::IsMoving() const
{
	// Read from GameState (single source of truth)
	return m_game_state.player().isMoving();
}

void EverQuest::SetHeading(float heading)
{
	// T288: one store. setHeading() fires PlayerMoved on the state bus, as
	// it always has -- this is the discrete-relocation path, not the
	// per-tick integrator path (SetHeadingLocal()).
	m_game_state.player().setHeading(heading);
}

void EverQuest::SetPosition(float x, float y, float z)
{
	// T288: one store. setPosition() fires PlayerMoved on the state bus, as
	// it always has -- this is the discrete-relocation path, not the
	// per-tick integrator path (SetPosLocal()).
	m_game_state.player().setPosition(x, y, z);
}

void EverQuest::SetMoving(bool moving)
{
	m_game_state.player().setMoving(moving);  // T286: only store
}

// ============================================================================
// Behavioral Flag Accessors (Phase 7.8 - read from PlayerState)
// ============================================================================

bool EverQuest::IsAFK() const
{
	return m_game_state.player().isAFK();
}

bool EverQuest::IsAnonymous() const
{
	return m_game_state.player().isAnonymous();
}

bool EverQuest::IsRoleplay() const
{
	return m_game_state.player().isRoleplay();
}

bool EverQuest::IsCamping() const
{
	return m_game_state.player().isCamping();
}

// ============================================================================
// Group Accessors and Sync Helper (Phase 7.5 - read from GroupState)
// ============================================================================

bool EverQuest::IsInGroup() const
{
	return m_game_state.group().inGroup();
}

bool EverQuest::IsGroupLeader() const
{
	return m_game_state.group().isLeader();
}

int EverQuest::GetGroupMemberCount() const
{
	return m_game_state.group().memberCount();
}

const std::string& EverQuest::GetGroupLeaderName() const
{
	return m_game_state.group().leaderName();
}

bool EverQuest::HasPendingGroupInvite() const
{
	return m_game_state.group().hasPendingInvite();
}

const std::string& EverQuest::GetPendingInviterName() const
{
	return m_game_state.group().pendingInviterName();
}

// T286: SyncGroupMemberToGameState() is DELETED. It existed only to copy
// EverQuest's own GroupMember array into GroupState's after every write;
// with one array and one struct there is nothing left to convert.

