#pragma once

// Batch K (refactor/K_component_extraction.md), unit K08.
//
// The player family -- everything the player character DOES and everything the
// server tells it about itself: emotes and the social flags, the camp/logout
// timer, resurrection, /who, /inspect, corpse drag and consent, duels, the
// cosmetic and misc senders, the buff/cast/mana handlers, the death and
// bind/stamina/level handlers, and the tribute/AA/exp zone-in handlers.
//
// WHAT DID NOT MOVE: the PLAYER-STATE FACADE. GetLevel()...GetMaxWeight(),
// GetPosition()/SetPosition()/GetHeading()/SetHeading()/IsMoving()/SetMoving(),
// IsAFK()/IsAnonymous()/IsRoleplay()/IsCamping(), the group accessors and
// Test_GetGameStatePlayerPosition() are still EverQuest's, in
// src/client/eq_player_info.cpp. They are not a family -- they are the thin
// read surface over m_game_state that every OTHER family uses, the same shape
// as the three AddChat* primitives K03 left behind. K13 decides whether they
// become a ClientContext service.
//
// Every body in src/client/player/*.cpp is the body that used to live in
// src/client/eq_player_{info,misc,social,state,buffs}.cpp, moved verbatim; the
// only edits inside a body are the substitutions Mechanism 4 allows (m_x ->
// ctx_.x / owner_.m_x, Foo() -> owner_.Foo() or owner_.<Family>().Foo() for an
// already-extracted family, EverQuest statics and nested types qualified).
//
// T341: the `owner_` TRANSITION handle and the
// `friend class eqt::player::PlayerComponent` line are BOTH GONE. The send
// path and the player-state reads became ctx_.net/ctx_.self at K13,
// m_inspect_message went to eqt::bridge::BridgeComponent at K11, and T341
// turned the last four reaches into ctx_.lifecycle (RequestQuit,
// m_pending_logout_disconnect), ctx_.Bridge() (the loading phase) and
// ctx_.strings.Db() (the /who eqstr hook). Both axes of the K13 (ii) friend
// test are empty.

#include "client/client_context.h"
#include "client/player/camp_timer.h"  // B1005
#include "client/player/profile_appearance.h"
#include "client/tracking/tracking_controller.h"  // B895
#include "common/packet_structs.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace EQ {
namespace Net {
class Packet;
}
}

class EverQuest;

namespace eqt {
namespace player {

class PlayerComponent {
public:
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit PlayerComponent(ClientContext& ctx) : ctx_(ctx), m_tracking(ctx) {}

	// B895 (GitLab #211): the Tracking skill's list, settings and target.
	eqt::tracking::TrackingController& Tracking() { return m_tracking; }

	// Social, camp/logout, resurrection, /who, /inspect, corpse, consent
	// (was eq_player_social.cpp).
	void SetAFK(bool afk);
	void SetAnonymous(bool anon);
	void SetRoleplay(bool rp);
	// B1005 (GitLab #324): /camp. Bodies: player_camp.cpp; the rule:
	// camp_timer.h. StartCampTimer is the /camp command (a second one while
	// counting abandons); CancelCamp is the stand that abandons it.
	void StartCampTimer(CampMode mode = CampMode::CharacterSelect);
	void CancelCamp();
	void UpdateCampTimer();
	void SendCamp(uint32_t mode = 0);   // OP_Camp, body u32 mode (camp.tsv [3])
	void SendLogout();                  // OP_Logout, empty
	void ZoneProcessLogoutReply(const EQ::Net::Packet &p);
	CampTimer& Camp() { return m_camp; }
	const CampTimer& Camp() const { return m_camp; }
	// Application: the run can draw Character Select (or the scenario lane
	// plays it), so /camp goes back there instead of exiting. (Line comments
	// only in this header: its banner names src/client/player/<star>.cpp, and
	// a block comment's close would pair with that for the hygiene scanners.)
	void SetCampReturnsToCharacterSelect(bool on) { m_camp_to_select = on; }
	bool CampReturnsToCharacterSelect() const { return m_camp_to_select; }
	// The deferred post-logout teardown asks once: go back to select?
	bool ConsumeReturnToCharacterSelect();
	void ZoneProcessRezzRequest(const EQ::Net::Packet &p);
	void ZoneProcessRezzComplete(const EQ::Net::Packet &p);
	void SendRezzAnswer(bool accept);  // Send OP_RezzAnswer (accept/decline)
	bool HasPendingRezz() const { return m_has_pending_rezz; }
	void SendWhoAllRequest(const std::string& name = "", int lvllow = -1, int lvlhigh = -1,
		int race = -1, int class_ = -1, bool gm = false);  // Send /who request
	void ZoneProcessWhoAllResponse(const EQ::Net::Packet &p);
	void SendInspectRequest(uint32_t target_id);  // Send /inspect request
	// T170: answers an inspect request aimed at us. A Titanium client MUST
	// do this -- eqemu only builds the answer itself for SoF+ clients and
	// bots. Called from ZoneProcessInspectRequest, never by a command.
	// Read the hazard note on the definition before touching it: the
	// answer's text field overwrites the character's stored inspect
	// message on the server.
	void SendInspectAnswer(uint32_t requester_id);
	void ZoneProcessInspectRequest(const EQ::Net::Packet &p);
	void ZoneProcessInspectAnswer(const EQ::Net::Packet &p);
	// Phase 3: Corpse Management
	void SendCorpseDrag(const std::string& corpse_name);
	void SendCorpseDrop();
	void SendConsiderCorpse(uint32_t corpse_id);
	bool IsDraggingCorpse() const { return m_is_dragging_corpse; }
	// Phase 3: Consent System
	void SendConsent(const std::string& player_name);
	void SendConsentDeny(const std::string& player_name);
	void ZoneProcessConsentResponse(const EQ::Net::Packet &p);
	void ZoneProcessDenyResponse(const EQ::Net::Packet &p);

	// Assist/split/LFG/shielding/discipline/banker, save, duels, bind wound,
	// track, cosmetics, random/find-person/rewind/yell/report/friends-who,
	// camera/mp3/sound (was eq_player_misc.cpp).
	// Phase 3: Combat Targeting
	void SendAssist(uint32_t target_id);
	void SendAssistGroup(uint32_t target_id);
	// Phase 3: Group Split
	void SendSplit(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);
	// Phase 3: LFG System
	void SendLFGCommand(bool lfg_on);
	bool IsLFG() const { return m_is_lfg; }
	void ZoneProcessLFGAppearance(const EQ::Net::Packet &p);
	// Phase 3: Combat Abilities
	void SendShielding(uint32_t target_id);
	void ZoneProcessEnvDamage(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineUpdate(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineTimer(const EQ::Net::Packet &p);
	void ZoneProcessBankerChange(const EQ::Net::Packet &p);
	// Phase 3: Misc
	// T349 (B059): a 192-byte body, not a bare opcode. `announce` is false on
	// the death path -- the official client sends OP_Save as the first packet
	// of its self-death burst and prints nothing while doing it.
	void SendSave(bool announce = true);
	void ZoneProcessClearObject(const EQ::Net::Packet &p);
	// Phase 4: Dueling
	void SendDuelRequest(uint32_t target_id);
	void SendDuelAccept(uint32_t target_id);
	void SendDuelDecline(uint32_t target_id);
	void ZoneProcessDuelRequest(const EQ::Net::Packet &p);
	bool IsInDuel() const { return m_is_dueling; }
	bool HasPendingDuel() const { return m_has_pending_duel; }
	// T102: accessor for EqActionHandler::duelAccept()/duelDecline() -- the
	// duel_cmd/duelaccept_cmd/dueldecline_cmd handlers being migrated read
	// m_duel_initiator_id directly (same TU); the action layer can't (no
	// friendship), so it needs a plain public getter, same shape as
	// GetCombatManager()/GetTradeManager() on EverQuest.
	uint32_t GetPendingDuelInitiatorId() const { return m_duel_initiator_id; }
	// Phase 4: Skills
	void SendBindWound(uint32_t target_id);
	void SendTrackTarget(uint32_t target_id);
	// Phase 4: Cosmetic
	void SendSurname(const std::string& surname);
	void SendFaceChange(uint8_t haircolor, uint8_t beardcolor, uint8_t eyecolor1, uint8_t eyecolor2,
		uint8_t hairstyle, uint8_t beard, uint8_t face);
	// Phase 4: Misc
	void SendRandom(uint32_t low, uint32_t high);
	void ZoneProcessRandomReply(const EQ::Net::Packet &p);
	void SendFindPerson(uint32_t npc_id);
	void ZoneProcessFindPersonReply(const EQ::Net::Packet &p);
	void SendRewind();
	void SendYellForHelp();
	void SendReport(const std::string& report_text);
	// T150: takes the comma-delimited friends list the server expects.
	// eqemu's Handle_OP_FriendsWho (zone/client_packet.cpp) casts the WHOLE
	// application payload to `char*` and passes it to Client::FriendsWho(),
	// which strchr()s it for ','; there is no encoder/struct for this opcode
	// (common/patches has none), so any binary prefix corrupts the first
	// name. The former zero-argument form wrote a uint32 spawn id ahead of
	// the string and could never match a character.
	void SendFriendsWho(const std::string& friends_list);
	void ZoneProcessCameraEffect(const EQ::Net::Packet &p);
	void ZoneProcessPlayMP3(const EQ::Net::Packet &p);
	void ZoneProcessSound(const EQ::Net::Packet &p);

	// Player state the server pushes, plus the GameState mutator
	// (was eq_player_state.cpp).
	void SetCombatStopDistance(float distance);  // Phase 7.6: also syncs to GameState
	void ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p);
	void ZoneProcessDeath(const EQ::Net::Packet &p);
	void ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p);
	void ZoneProcessStamina(const EQ::Net::Packet &p);
	void ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p);
	void ZoneProcessLevelUpdate(const EQ::Net::Packet &p);
	void SendTradeSkillCombine(int16_t container_slot);
	void ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p);

	// Tasks/DZ, spell cast, mana, buffs and pet buffs, poison, bind wound,
	// track (was eq_player_buffs.cpp).
	void ZoneProcessCompletedTasks(const EQ::Net::Packet &p);
	void ZoneProcessDzCompass(const EQ::Net::Packet &p);
	void ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p);
	void ZoneProcessBeginCast(const EQ::Net::Packet &p);
	// B1170 (#489): OP_SomeItemPacketMaybe (0x4033), the missile object.
	void ZoneProcessSomeItemPacketMaybe(const EQ::Net::Packet &p);
	void ZoneProcessManaChange(const EQ::Net::Packet &p);
	void ZoneProcessBuff(const EQ::Net::Packet &p);
	// T277: click off a buff. RequestCancelBuff() resolves a user-facing
	// argument -- a 1-based buff slot number, or a spell-name substring --
	// against BuffManager's player buffs. SendBuffCancelRequest() is the
	// single wire hop it and the buff-bar click (BuffCancelIntent) share;
	// it removes nothing locally, it only asks. Defined in player_buffs.cpp
	// beside ZoneProcessBuff(), which handles the server's answer.
	void RequestCancelBuff(const std::string& args);
	void SendBuffCancelRequest(uint8_t slot);
	// T277: publish ONE player buff slot to the renderer. Shared by
	// ZoneProcessBuff()'s apply branch and the PlayerProfile's zone-in
	// buffs, which previously reached BuffManager and stopped there.
	void PublishPlayerBuffUpdated(uint8_t slot, const EQT::SpellBuff_Struct& buff);
	void PublishPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count);
	// T348 (board B055): THE BUFF-LANDING PRODUCER. eqemu never sends a
	// Titanium client an "applied" OP_Buff, so ZoneProcessBuff()'s apply
	// branch is dead on the wire; a buff landing on us arrives as the SECOND
	// OP_Action of the pair (effect_flag == 4). Called from
	// CommandsComponent::ZoneProcessAction. See player_buffs.cpp.
	void NoteBuffLandedOnPlayer(uint32_t spell_id, uint16_t caster_id,
	                            uint8_t caster_level);
	void ZoneProcessPetBuffWindow(const EQ::Net::Packet &p);
	// Skill-specific packet sends
	void SendApplyPoison(uint32_t inventory_slot);
	void ZoneProcessApplyPoison(const EQ::Net::Packet &p);
	void ZoneProcessBindWound(const EQ::Net::Packet &p);
	void ZoneProcessTrack(const EQ::Net::Packet &p);

	// Tribute/AA/exp zone-in handlers and the GameState entity-sync helpers
	// (was the mover half of eq_player_info.cpp).
	void ZoneProcessTributeUpdate(const EQ::Net::Packet &p);
	void ZoneProcessTributeTimer(const EQ::Net::Packet &p);
	void ZoneProcessSendAATable(const EQ::Net::Packet &p);
	void ZoneProcessRespondAA(const EQ::Net::Packet &p);
	void ZoneProcessTributeInfo(const EQ::Net::Packet &p);
	void ZoneProcessSendGuildTributes(const EQ::Net::Packet &p);
	void ZoneProcessSendAAStats(const EQ::Net::Packet &p);
	void ZoneProcessSendExpZonein(const EQ::Net::Packet &p);
	void ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p);
	// Phase 7.4: Entity sync helpers
	void SyncEntityToGameState(const Entity& entity);
	void RemoveEntityFromGameState(uint16_t spawnId);

	// T167 test seam, same shape as the T88 one on MovementComponent: the body
	// SendFaceChange() actually built, captured unconditionally right before
	// the connection-gated send. Without this, the merge in
	// EqActionHandler::setAppearance() can only be tested by re-implementing
	// it in the test -- which proves nothing. `sent` starts false so a test
	// can assert that NOTHING was sent. Read-only; test-only.
	struct TestSentFaceChange {
		bool sent = false;
		EQT::ProfileAppearance body;
	};
	TestSentFaceChange Test_GetLastFaceChange() const { return m_test_last_face_change; }

	// T170 test seam: the two id fields of the last inspect answer built. They
	// are the whole point -- they mean the OPPOSITE of the request struct's.
	struct TestSentInspectAnswer {
		bool sent = false;
		uint32_t target_id = 0;   // the requester (routing)
		uint32_t playerid = 0;    // the inspected player (us)
	};
	TestSentInspectAnswer Test_GetLastInspectAnswer() const { return m_test_last_inspect_answer; }

private:
	ClientContext& ctx_;

	// B1005: the camp state machine and where a camp lands.
	CampTimer m_camp;
	bool m_camp_to_select = false;
	bool m_pending_return_to_select = false;

	// Pending resurrection offer
	bool m_has_pending_rezz = false;
	// Phase 3: Corpse drag state
	bool m_is_dragging_corpse = false;
	std::string m_dragged_corpse_name;
	// Phase 3: LFG state
	bool m_is_lfg = false;
	// Phase 4: Duel state
	bool m_is_dueling = false;
	bool m_has_pending_duel = false;
	// B344 deleted m_duel_target_id: /duel recorded it, the reply path
	// never consulted it.
	uint32_t m_duel_initiator_id = 0;
	EQT::Resurrect_Struct m_pending_rezz;

	// T167 test seam -- see Test_GetLastFaceChange().
	TestSentFaceChange m_test_last_face_change;

	// T170 test seam -- see Test_GetLastInspectAnswer().
	TestSentInspectAnswer m_test_last_inspect_answer;

	// B895: declared after ctx_, which it binds.
	eqt::tracking::TrackingController m_tracking;
};

}  // namespace player
}  // namespace eqt
