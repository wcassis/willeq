#pragma once

// Batch K (refactor/K_component_extraction.md), unit K13.
//
// PlayerFacade is the second CONTEXT SERVICE: the thin read/write surface over
// eqt::state::GameState that every family uses, lifted off class EverQuest.
//
// These are the bodies K08 deliberately left behind in
// src/client/eq_player_info.cpp -- "they are not a family, they are the read
// surface every family uses" -- plus the private position/spawn-id/zone-identity
// accessor block that stood in include/client/eq.h behind the T288 banner, plus
// IsFullyZonedIn() and the vendor/bank/trainer window predicates. Measured at
// this unit's tip they carry 534 of the 1,425 owner_. uses in the twelve
// components.
//
// The service owns NOTHING and adds no storage. It holds a reference to
// EverQuest's m_game_state -- under the same name, so every body below is
// byte-identical to the body it was moved from -- and one reference to
// EverQuest's m_update_running, which is the third term of IsFullyZonedIn()
// and the only thing in this file that is not GameState. (T288's rule still
// holds and is now enforced one level down: there is exactly ONE store, and
// this class is an accessor into it, never a copy.)
//
// EverQuest keeps the public half of this surface as forwarders for the
// NON-component callers -- combat, the spell/skill/inventory managers, the
// action layer and the UI read GetPosition()/GetLevel()/... on EverQuest and
// always have. That is the plan's explicit exception to §Mechanism 5's
// no-forwarder rule. The PRIVATE half (PosX/PosY/PosZ/HeadingDeg/SetPosLocal/
// SetHeadingLocal/MySpawnId/SetMySpawnId/CurrentZoneName/CurrentZoneId) had no
// callers outside the friend components at all, so it is GONE from eq.h.

#include "client/state/game_state.h"

#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace eqt {

class PlayerFacade {
public:
	PlayerFacade(eqt::state::GameState& game_state, std::atomic<bool>& update_running)
		: m_game_state(game_state), m_update_running(update_running) {}

	// --- Cluster 2: player stat getters (K08's facade, verbatim) ---
	uint8_t GetLevel() const;
	uint32_t GetClass() const;
	uint32_t GetRace() const;
	uint32_t GetGender() const;
	uint32_t GetSTR() const;
	uint32_t GetSTA() const;
	uint32_t GetDEX() const;
	uint32_t GetAGI() const;
	uint32_t GetINT() const;
	uint32_t GetWIS() const;
	uint32_t GetCHA() const;
	uint32_t GetCurrentHP() const;
	uint32_t GetMaxHP() const;
	uint32_t GetCurrentMana() const;
	uint32_t GetMaxMana() const;
	uint32_t GetCurrentEndurance() const;
	uint32_t GetMaxEndurance() const;
	uint32_t GetDeity() const;
	uint32_t GetPlatinum() const;
	uint32_t GetGold() const;
	uint32_t GetSilver() const;
	uint32_t GetCopper() const;
	uint32_t GetBankPlatinum() const;
	uint32_t GetBankGold() const;
	uint32_t GetBankSilver() const;
	uint32_t GetBankCopper() const;
	uint32_t GetPracticePoints() const;
	float GetWeight() const;
	float GetMaxWeight() const;

	// --- Movement state accessors ---
	glm::vec3 Test_GetGameStatePlayerPosition() const;
	glm::vec3 GetPosition() const;
	float GetHeading() const;
	bool IsMoving() const;
	void SetHeading(float heading);
	void SetPosition(float x, float y, float z);
	void SetMoving(bool moving);

	// --- Behavioural flags ---
	bool IsAFK() const;
	bool IsAnonymous() const;
	bool IsRoleplay() const;
	bool IsCamping() const;

	// --- Group accessors ---
	bool IsInGroup() const;
	bool IsGroupLeader() const;
	int GetGroupMemberCount() const;
	const std::string& GetGroupLeaderName() const;
	bool HasPendingGroupInvite() const;
	const std::string& GetPendingInviterName() const;

	// ------------------------------------------------------------------
	// T288: ONE store for position/heading, spawn id and zone identity.
	//
	// EverQuest used to keep its own position/heading, spawn id and zone
	// name/id alongside the state layer's
	// PlayerState/WorldState copies, and hand-mirror them at each wire
	// site. Some sites mirrored, some did not: the self-ClientUpdate
	// branch, the whole movement integrator and the PlayerProfile
	// entity-id write all updated only EverQuest's copy, so every
	// state-layer reader (GetHeading(), clickNearestDoor(),
	// getNearestPlayer(), GameState::playerPosition()) saw the zone-in
	// value for the rest of the session. T286 catalogued this class; these
	// seven were the last of it.
	//
	// The members are GONE. What follows are accessors INTO
	// m_game_state -- adding a member back here is the regression
	// tests/test_duplicate_state_stores.cpp exists to catch.
	//
	// Coordinate order: PosX()/PosY()/PosZ() are CLIENT coordinates
	// (INVERSEXY already applied -- client x is the server's y). Every
	// wire site that reads them for an outbound packet swaps back; see
	// docs/CLAUDE.md's coordinate section, and do not "simplify" a swap
	// away. HeadingDeg() is the PLAYER's heading in degrees, carrying the
	// deliberate `90 -` flip that NPC headings do not share.
	float PosX() const { return m_game_state.player().x(); }
	float PosY() const { return m_game_state.player().y(); }
	float PosZ() const { return m_game_state.player().z(); }
	float HeadingDeg() const { return m_game_state.player().heading(); }

	// Movement-step writes: same store, no PlayerMoved event -- exactly the
	// event behaviour the raw members had (see
	// PlayerState::setPositionNoEvent). SetPosition()/SetHeading() remain
	// the event-firing relocation path.
	void SetPosLocal(float x, float y, float z) {
		m_game_state.player().setPositionNoEvent(x, y, z);
	}
	void SetHeadingLocal(float heading) {
		m_game_state.player().setHeadingNoEvent(heading);
	}

	uint16_t MySpawnId() const { return m_game_state.player().spawnId(); }
	void SetMySpawnId(uint16_t id) { m_game_state.player().setSpawnId(id); }

	const std::string& CurrentZoneName() const { return m_game_state.world().zoneName(); }
	uint16_t CurrentZoneId() const { return m_game_state.world().zoneId(); }
	// ------------------------------------------------------------------

	uint16_t GetEntityID() const { return MySpawnId(); }
	bool HasPet() const { return m_game_state.pet().petSpawnId() != 0; }

	// The zone-in gate. m_update_running is set at the first ClientUpdate
	// (EverQuest::StartUpdateLoop) and cleared by CleanupZone/~EverQuest.
	bool IsFullyZonedIn() const { return m_game_state.world().isZoneConnected() && m_game_state.world().isClientReady() && m_update_running; }

	// Player-mode window predicates -- pure GameState reads, exactly as they
	// stood inline in eq.h.
	bool IsVendorWindowOpen() const { return m_game_state.player().vendorNpcId() != 0; }
	uint16_t GetVendorNpcId() const { return m_game_state.player().vendorNpcId(); }
	bool IsBankWindowOpen() const { return m_game_state.player().bankerNpcId() != 0; }
	bool IsTrainerWindowOpen() const { return m_game_state.player().trainerNpcId() != 0; }

private:
	eqt::state::GameState& m_game_state;
	std::atomic<bool>& m_update_running;
};

}  // namespace eqt
