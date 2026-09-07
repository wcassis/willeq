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
#include "client/eq_types.h"

#include <atomic>
#include <map>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace eqt {

class PlayerFacade {
public:
	// T404 (board B251): the entity map comes in by reference too. It is
	// NOT a second store of the player's position -- the position store is
	// m_game_state.player() and still only that (T288). The self row in the
	// entity map is a PROJECTION that every range check and every positioned
	// sound reads (SpellManager::checkRange takes the caster from
	// GetPosition() and the target from this map, so a SELF-targeted cast
	// compared the two), and before this unit only four discrete relocation
	// sites wrote it -- never the movement integrator, never the renderer's
	// PlayerPositionChanged intent, never a zone-in warp. T401 measured the
	// consequence twice: a melee sound emitted 1,020 units away at the spawn
	// pose and inaudible, and `distance=1019` on a self-targeted gem cast
	// after a #goto. SetPosLocal()/SetPosition() below are the only two
	// funnels through which the player's position ever changes, so the
	// projection is refreshed there and nowhere else.
	PlayerFacade(eqt::state::GameState& game_state, std::atomic<bool>& update_running,
	             std::map<uint16_t, Entity>& entities)
		: m_game_state(game_state), m_update_running(update_running),
		  m_entities(entities) {}

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
	// T438 (board B424): the two position writes carry their CALL SITE.
	// B424 -- "after a same-zone #goto the game-thread player z reports the
	// Kelethin platform height" -- survived two units because every offline
	// argument about WHICH handler wrote the z was circumstantial: the writers
	// were enumerated and excluded one by one from a log that never named one.
	// __builtin_FILE()/__builtin_LINE() defaults cost nothing at a call site
	// that does not pass them and make the next such question a single TRACE
	// run instead of a unit. TracePositionWrite() early-outs unless MOD_MOVEMENT
	// is at TRACE, so the shipped cost is one level compare per write.
	void SetPosition(float x, float y, float z,
	                 const char* from_file = __builtin_FILE(),
	                 int from_line = __builtin_LINE());
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
	void SetPosLocal(float x, float y, float z,
	                 const char* from_file = __builtin_FILE(),
	                 int from_line = __builtin_LINE()) {
		TracePositionWrite("SetPosLocal", x, y, z, from_file, from_line);
		m_game_state.player().setPositionNoEvent(x, y, z);
		MirrorSelfEntityPosition(x, y, z);  // T404 (B251)
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
	// T438: see SetPosition() above. Defined in player_facade.cpp so the
	// logging header does not have to be pulled into every includer.
	void TracePositionWrite(const char* what, float x, float y, float z,
	                        const char* from_file, int from_line) const;

	// T404 (board B251). POSITION ONLY, deliberately: the self row's HEADING
	// is not in the same convention as PlayerState's. ZoneProcessClientUpdate
	// stores `server_h` verbatim in the row while writing `90 - server_h`
	// into PlayerState (the player-only flip documented in docs/CLAUDE.md's
	// Heading/Rotation Conventions), so mirroring HeadingDeg() into the row
	// would corrupt it by 90 degrees plus a mirror. Do not "finish the job"
	// here without measuring which convention each reader wants.
	//
	// Cheap by construction: one map lookup on a map that holds a few
	// hundred entries, on the two calls that already wrote the whole
	// PlayerState position. A spawn id of 0 (pre-zone-in) finds nothing and
	// costs one failed lookup.
	void MirrorSelfEntityPosition(float x, float y, float z) {
		const uint16_t id = m_game_state.player().spawnId();
		if (id == 0) return;
		auto it = m_entities.find(id);
		if (it == m_entities.end()) return;
		it->second.x = x;
		it->second.y = y;
		it->second.z = z;
	}

	eqt::state::GameState& m_game_state;
	std::atomic<bool>& m_update_running;
	std::map<uint16_t, Entity>& m_entities;
};

}  // namespace eqt
