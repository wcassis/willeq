// EverQuest's PLAYER-STATE FACADE -- now 46 forwarders.
//
// K08 (Batch K, refactor/K_component_extraction.md) left these thin accessors
// over m_game_state on EverQuest when the rest of the player family moved into
// eqt::player::PlayerComponent, because they are not a family: they are the
// read surface every family uses.
//
// K13 moved the BODIES into eqt::PlayerFacade (src/client/player_facade.cpp),
// the context service the twelve components reach as ctx_.self. What is left
// here is the sanctioned forwarder set: the callers OUTSIDE the EverQuest
// family -- combat, the spell/skill/inventory managers, the action layer and
// the UI -- keep the EverQuest receiver they always had. That is the plan's
// explicit exception to §Mechanism 5's no-forwarder rule; K14 decides whether
// the surface goes away entirely.
//
// The PRIVATE half of the facade (PosX/PosY/PosZ/HeadingDeg/SetPosLocal/
// SetHeadingLocal/MySpawnId/SetMySpawnId/CurrentZoneName/CurrentZoneId, which
// stood inline in eq.h behind the T288 banner) had no caller outside the
// friend components, so it kept no forwarder at all and is gone from eq.h.

#include "client/eq.h"

#include <glm/glm.hpp>

uint8_t EverQuest::GetLevel() const
{
	return m_player_facade.GetLevel();
}

uint32_t EverQuest::GetClass() const
{
	return m_player_facade.GetClass();
}

uint32_t EverQuest::GetRace() const
{
	return m_player_facade.GetRace();
}

uint32_t EverQuest::GetGender() const
{
	return m_player_facade.GetGender();
}

uint32_t EverQuest::GetSTR() const
{
	return m_player_facade.GetSTR();
}

uint32_t EverQuest::GetSTA() const
{
	return m_player_facade.GetSTA();
}

uint32_t EverQuest::GetDEX() const
{
	return m_player_facade.GetDEX();
}

uint32_t EverQuest::GetAGI() const
{
	return m_player_facade.GetAGI();
}

uint32_t EverQuest::GetINT() const
{
	return m_player_facade.GetINT();
}

uint32_t EverQuest::GetWIS() const
{
	return m_player_facade.GetWIS();
}

uint32_t EverQuest::GetCHA() const
{
	return m_player_facade.GetCHA();
}

uint32_t EverQuest::GetCurrentHP() const
{
	return m_player_facade.GetCurrentHP();
}

uint32_t EverQuest::GetMaxHP() const
{
	return m_player_facade.GetMaxHP();
}

uint32_t EverQuest::GetCurrentMana() const
{
	return m_player_facade.GetCurrentMana();
}

uint32_t EverQuest::GetMaxMana() const
{
	return m_player_facade.GetMaxMana();
}

uint32_t EverQuest::GetCurrentEndurance() const
{
	return m_player_facade.GetCurrentEndurance();
}

uint32_t EverQuest::GetMaxEndurance() const
{
	return m_player_facade.GetMaxEndurance();
}

uint32_t EverQuest::GetDeity() const
{
	return m_player_facade.GetDeity();
}

uint32_t EverQuest::GetPlatinum() const
{
	return m_player_facade.GetPlatinum();
}

uint32_t EverQuest::GetGold() const
{
	return m_player_facade.GetGold();
}

uint32_t EverQuest::GetSilver() const
{
	return m_player_facade.GetSilver();
}

uint32_t EverQuest::GetCopper() const
{
	return m_player_facade.GetCopper();
}

uint32_t EverQuest::GetBankPlatinum() const
{
	return m_player_facade.GetBankPlatinum();
}

uint32_t EverQuest::GetBankGold() const
{
	return m_player_facade.GetBankGold();
}

uint32_t EverQuest::GetBankSilver() const
{
	return m_player_facade.GetBankSilver();
}

uint32_t EverQuest::GetBankCopper() const
{
	return m_player_facade.GetBankCopper();
}

uint32_t EverQuest::GetPracticePoints() const
{
	return m_player_facade.GetPracticePoints();
}

float EverQuest::GetWeight() const
{
	return m_player_facade.GetWeight();
}

float EverQuest::GetMaxWeight() const
{
	return m_player_facade.GetMaxWeight();
}

glm::vec3 EverQuest::Test_GetGameStatePlayerPosition() const
{
	return m_player_facade.Test_GetGameStatePlayerPosition();
}

glm::vec3 EverQuest::GetPosition() const
{
	return m_player_facade.GetPosition();
}

float EverQuest::GetHeading() const
{
	return m_player_facade.GetHeading();
}

bool EverQuest::IsMoving() const
{
	return m_player_facade.IsMoving();
}

void EverQuest::SetHeading(float heading)
{
	m_player_facade.SetHeading(heading);
}

void EverQuest::SetPosition(float x, float y, float z)
{
	m_player_facade.SetPosition(x, y, z);
}

void EverQuest::SetMoving(bool moving)
{
	m_player_facade.SetMoving(moving);
}

bool EverQuest::IsAFK() const
{
	return m_player_facade.IsAFK();
}

bool EverQuest::IsAnonymous() const
{
	return m_player_facade.IsAnonymous();
}

bool EverQuest::IsRoleplay() const
{
	return m_player_facade.IsRoleplay();
}

bool EverQuest::IsCamping() const
{
	return m_player_facade.IsCamping();
}

bool EverQuest::IsInGroup() const
{
	return m_player_facade.IsInGroup();
}

bool EverQuest::IsGroupLeader() const
{
	return m_player_facade.IsGroupLeader();
}

int EverQuest::GetGroupMemberCount() const
{
	return m_player_facade.GetGroupMemberCount();
}

const std::string& EverQuest::GetGroupLeaderName() const
{
	return m_player_facade.GetGroupLeaderName();
}

bool EverQuest::HasPendingGroupInvite() const
{
	return m_player_facade.HasPendingGroupInvite();
}

const std::string& EverQuest::GetPendingInviterName() const
{
	return m_player_facade.GetPendingInviterName();
}
