// EverQuest assist/boat/split/LFG/shielding/discipline/banker, save/popup/
// duel/bindwound, cosmetic misc social, and camera/mp3/sound handlers.
//
// Clusters 9-12 of eq_player_info.cpp, split off by Batch J unit J07. Moved
// verbatim, no behavior change. See refactor/J07_eq_player_info_split.md and
// refactor/F07_player_info.md.

#include "client/eq.h"
#include "client/inspect_answer.h"
#include "client/bind_wound_outcome.h"
#include "client/who_format.h"
#include "client/who_response.h"
#include "client/zone_lines.h"
#include "client/combat.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/spell/buff_cancel_request.h"
#include "client/bridge/game_state_bridge.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/sound_assets.h"
#endif

#include "client/world_object.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#endif

#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

// ============================================================================
// Cluster 9: Assist/Boat/Split/LFG/Shielding/EnvDamage/Discipline/Banker
// ============================================================================

void EverQuest::SendAssist(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Assist);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Assist request for target ID: {}", target_id);
	}
}

void EverQuest::SendAssistGroup(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_AssistGroup);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Group assist request for target ID: {}", target_id);
	}
}

// ============================================================================
// Phase 3: Travel System
// ============================================================================

// T168: SendBoardBoat / SendLeaveBoat / SendControlBoat were DELETED here.
//
// All three were zero-caller senders, and all three were dead on arrival, not
// merely unwired -- the arc's standing lesson is that a sender nothing ever
// called is UNVERIFIED, not "correct but unreached":
//
//   * SendBoardBoat sent a 4-byte entity id. eqemu's Handle_OP_BoardBoat
//     (zone/client_packet.cpp:4133) requires `5 < size <= 64` and resolves the
//     body as a NAME via entity_list.GetMob(boatname). A 4-byte body is
//     rejected before anything else happens, so boarding could never succeed.
//   * SendLeaveBoat guarded on m_is_on_boat, which only SendBoardBoat ever
//     set, so it could never be reached -- and eqemu's Handle_OP_LeaveBoat
//     ignores the body entirely, so the id it carried was meaningless.
//   * SendControlBoat is the T151/T154 HAZARD: Handle_OP_ControlBoat
//     (:5421) resolves the id through entity_list.GetMob() and any hit that is
//     not a controllable boat writes RecordPlayerEventLog(POSSIBLE_HACK)
//     AGAINST OUR OWN ACCOUNT. Its only id source was the broken boarder.
//
// Keeping them was the trap T147 names: three functions that look like a
// half-built feature but cannot work, with a comment block warning the reader
// off the only one with a body worth reading. The opcodes, ControlBoat_Struct
// and the findings above are kept -- see the HC_OP_BoardBoat annotation in
// eq.h and refactor/T168_sender_triage.md. Boats need a ride/entity model
// first; steering is OP_ClientUpdate with a foreign spawn_id, not a boat
// opcode at all.

// ============================================================================
// Phase 3: Group Split
// ============================================================================

void EverQuest::SendSplit(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper)
{
	if (!m_game_state.group().inGroup()) {
		AddChatSystemMessage("You are not in a group.");
		return;
	}

	// Check if we have enough money
	uint64_t total_copper = copper + silver * 10 + gold * 100 + platinum * 1000;
	uint64_t my_copper = m_game_state.player().copper() + m_game_state.player().silver() * 10 + m_game_state.player().gold() * 100 + m_game_state.player().platinum() * 1000;

	if (total_copper > my_copper) {
		AddChatSystemMessage("You don't have that much money.");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Split);

	EQT::Split_Struct split;
	split.platinum = platinum;
	split.gold = gold;
	split.silver = silver;
	split.copper = copper;
	packet.PutData(2, &split, sizeof(split));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Splitting {}p {}g {}s {}c with group", platinum, gold, silver, copper);
		AddChatSystemMessage(fmt::format("You split {}p {}g {}s {}c with your group.",
			platinum, gold, silver, copper));
	}
}

// ============================================================================
// Phase 3: LFG System
// ============================================================================

void EverQuest::SendLFGCommand(bool lfg_on)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_LFGCommand);
	packet.PutUInt32(2, lfg_on ? 1 : 0);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		m_is_lfg = lfg_on;
		LOG_INFO(MOD_MAIN, "LFG status: {}", lfg_on ? "ON" : "OFF");
		AddChatSystemMessage(fmt::format("You are {} looking for a group.", lfg_on ? "now" : "no longer"));
	}
}

void EverQuest::ZoneProcessLFGAppearance(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::LFG_Appearance_Struct)) {
		return;
	}

	const EQT::LFG_Appearance_Struct* lfg = reinterpret_cast<const EQT::LFG_Appearance_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update entity's LFG status
	auto it = m_entities.find(static_cast<uint16_t>(lfg->spawn_id));
	if (it != m_entities.end()) {
		// If we had an LFG flag on Entity, we'd update it here
		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "Entity {} LFG status: {}", lfg->spawn_id, lfg->lfg);
		}
	}

	// Update our own status if it's us
	if (lfg->spawn_id == MySpawnId()) {
		m_is_lfg = (lfg->lfg != 0);
	}
}

// ============================================================================
// Phase 3: Combat Abilities
// ============================================================================

void EverQuest::SendShielding(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Shielding);

	EQT::Shielding_Struct shield;
	shield.target_id = target_id;
	packet.PutData(2, &shield, sizeof(shield));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Shielding target ID: {}", target_id);
	}
}

// NOTE (T145): OP_ENVDAMAGE IS CLIENT->SERVER IN EQEMU. It is registered in
// `ConnectedOpcodes` (zone/client_packet.cpp:198) and the server never sends
// it, so this receive path cannot fire against an eqemu server no matter what
// size the struct is. The struct and this guard are now correct against
// eqemu's 31-byte EnvDamage2_Struct (the guard previously demanded 34 bytes
// for a 33-byte packet), but that only means the path would parse correctly
// IF something sent it. This client has no OP_EnvDamage sender at all.
void EverQuest::ZoneProcessEnvDamage(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::EnvDamage2_Struct)) {
		return;
	}

	const EQT::EnvDamage2_Struct* env = reinterpret_cast<const EQT::EnvDamage2_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// eqemu EQ::constants::EnvironmentalDamage (common/emu_constants.h:308)
	std::string damage_type;
	switch (env->dmgtype) {
		case 250: damage_type = "lava"; break;
		case 251: damage_type = "drowning"; break;
		case 252: damage_type = "falling"; break;
		case 253: damage_type = "trap"; break;
		default: damage_type = "environmental"; break;
	}

	if (env->id == MySpawnId()) {
		LOG_INFO(MOD_MAIN, "You took {} {} damage!", env->damage, damage_type);
		AddChatSystemMessage(fmt::format("You took {} points of {} damage.", env->damage, damage_type));
	}
}

// ============================================================================
// Phase 3: Discipline System
// ============================================================================

void EverQuest::ZoneProcessDisciplineUpdate(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Disciplines_Struct)) {
		return;
	}

	const EQT::Disciplines_Struct* disc = reinterpret_cast<const EQT::Disciplines_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update discipline list in game state if needed
	LOG_DEBUG(MOD_MAIN, "Received discipline update");

	// Count known disciplines
	int count = 0;
	for (uint32_t i = 0; i < EQT::MAX_PP_DISCIPLINES; i++) {
		if (disc->values[i] != 0) {
			count++;
		}
	}

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "You know {} disciplines", count);
	}
}

void EverQuest::ZoneProcessDisciplineTimer(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::DisciplineTimer_Struct)) {
		return;
	}

	const EQT::DisciplineTimer_Struct* timer = reinterpret_cast<const EQT::DisciplineTimer_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Discipline timer {}: {} seconds remaining",
			timer->timer_id, timer->timer_value);
	}
}

// ============================================================================
// Phase 3: Banking
// ============================================================================

void EverQuest::ZoneProcessBankerChange(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::BankerChange_Struct)) {
		return;
	}

	const EQT::BankerChange_Struct* bank = reinterpret_cast<const EQT::BankerChange_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Update bank currency (T286: PlayerState is the only store)
	m_game_state.player().setBankPlatinum(bank->platinum);
	m_game_state.player().setBankGold(bank->gold);
	m_game_state.player().setBankSilver(bank->silver);
	m_game_state.player().setBankCopper(bank->copper);

	LOG_DEBUG(MOD_MAIN, "Bank updated: {}p {}g {}s {}c",
		m_game_state.player().bankPlatinum(), m_game_state.player().bankGold(), m_game_state.player().bankSilver(), m_game_state.player().bankCopper());
}

// ============================================================================
// Cluster 10: Save/Popup/ClearObject/Duel/BindWound/Track
// ============================================================================

void EverQuest::SendSave()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Save);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Character save requested");
		AddChatSystemMessage("Saving character...");
	}
}

// T278: this used to read the first four payload bytes as a world-object
// drop_id and erase m_world_objects[that id]. OP_ClearObject carries no such
// field. eqemu's ClearObject_Struct is { uint8 Clear; uint8 Unknown001[7]; }
// and every one of its three senders sets Clear = 1 -- so the "object id"
// this read was always the literal 1, and any ground spawn that happened to
// have drop_id 1 was silently deleted from the client's world-object map.
//
// What the packet actually means is "the WORLD CONTAINER's contents are
// gone". eqemu queues it from exactly three places, all with an open world
// container: Object::Close() (the container was closed), and the
// world-container branches of Object::HandleCombine() and
// Object::HandleAugmentation() after they Clear() the container and delete
// its row. It is NOT a close notification -- after a combine the window
// stays open with empty slots, which is why the active container id is left
// alone here.
//
// So the correct response is to empty the world container slots
// (4000-4009). InventoryManager::clearWorldContainerSlots() already existed
// for this and had ZERO callers; this is its caller.
void EverQuest::ZoneProcessClearObject(const EQ::Net::Packet &p)
{
	(void)p;  // ClearObject_Struct carries only a "did something" flag

	LOG_DEBUG(MOD_MAIN, "ClearObject: emptying world container slots (active container {})",
		m_active_tradeskill_object_id);

	if (!m_inventory_manager) {
		return;
	}
	m_inventory_manager->clearWorldContainerSlots();

	// The UI holds its own copy of each slot, so an emptied slot has to be
	// announced the same way a deleted item is (D18a: bridge-only).
	for (int16_t slot = eqt::inventory::WORLD_BEGIN; slot <= eqt::inventory::WORLD_END; slot++) {
		SyncInventorySlotToGameState(slot, false);
		if (!m_bridge) {
			continue;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		data.hasItem = false;
		data.itemId = 0;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	}
}

// ============================================================================
// Phase 4: Dueling System
// ============================================================================

void EverQuest::SendDuelRequest(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RequestDuel);

	EQT::Duel_Struct duel;
	duel.duel_initiator = MySpawnId();
	duel.duel_target = target_id;
	packet.PutData(2, &duel, sizeof(duel));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		m_has_pending_duel = true;
		m_duel_target_id = target_id;
		LOG_INFO(MOD_MAIN, "Sent duel request to target ID: {}", target_id);
		AddChatSystemMessage("You have challenged someone to a duel.");
	}
}

void EverQuest::SendDuelAccept(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_DuelAccept);

	EQT::Duel_Struct duel;
	duel.duel_initiator = target_id;
	duel.duel_target = MySpawnId();
	packet.PutData(2, &duel, sizeof(duel));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		m_is_dueling = true;
		m_has_pending_duel = false;
		m_duel_target_id = target_id;
		LOG_INFO(MOD_MAIN, "Accepted duel from: {}", target_id);
		AddChatSystemMessage("You have accepted the duel!");
	}
}

void EverQuest::SendDuelDecline(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_DuelDecline);

	EQT::DuelResponse_Struct response;
	response.target_id = MySpawnId();
	response.entity_id = target_id;
	response.unknown = 0;
	packet.PutData(2, &response, sizeof(response));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		m_has_pending_duel = false;
		m_duel_initiator_id = 0;
		LOG_INFO(MOD_MAIN, "Declined duel from: {}", target_id);
		AddChatSystemMessage("You have declined the duel.");
	}
}

void EverQuest::ZoneProcessDuelRequest(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Duel_Struct)) {
		return;
	}

	const EQT::Duel_Struct* duel = reinterpret_cast<const EQT::Duel_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Someone challenged us to a duel
	if (duel->duel_target == MySpawnId()) {
		m_has_pending_duel = true;
		m_duel_initiator_id = duel->duel_initiator;

		// Find challenger name
		std::string challenger = "Someone";
		auto it = m_entities.find(static_cast<uint16_t>(duel->duel_initiator));
		if (it != m_entities.end()) {
			challenger = it->second.name;
		}

		LOG_INFO(MOD_MAIN, "{} has challenged you to a duel!", challenger);
		AddChatSystemMessage(fmt::format("{} has challenged you to a duel! Type /duelaccept or /dueldecline.", challenger));
	}
}

// ============================================================================
// Phase 4: Skills
// ============================================================================

void EverQuest::SendBindWound(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_BindWound);

	EQT::BindWound_Struct bw;
	bw.to = static_cast<uint16_t>(target_id);
	bw.unknown2 = 0;
	bw.type = 0;
	bw.unknown6 = 0;
	packet.PutData(2, &bw, sizeof(bw));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Bind wound on target: {}", target_id);
	}
}

void EverQuest::SendTrackTarget(uint32_t target_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_TrackTarget);
	packet.PutUInt32(2, target_id);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Track target: {}", target_id);
	}
}

// ============================================================================
// Cluster 11: Surname/FaceChange/Random/FindPerson + social misc
// ============================================================================

void EverQuest::SendSurname(const std::string& surname)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Surname);

	EQT::Surname_Struct sn;
	memset(&sn, 0, sizeof(sn));
	strncpy(sn.name, m_character.c_str(), 63);
	strncpy(sn.lastname, surname.c_str(), 31);
	packet.PutData(2, &sn, sizeof(sn));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Set surname to: {}", surname);
		AddChatSystemMessage(fmt::format("Requesting surname: {}", surname));
	}
}

void EverQuest::SendFaceChange(uint8_t haircolor, uint8_t beardcolor, uint8_t eyecolor1, uint8_t eyecolor2,
	uint8_t hairstyle, uint8_t beard, uint8_t face)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FaceChange);

	EQT::FaceChange_Struct fc;
	fc.haircolor = haircolor;
	fc.beardcolor = beardcolor;
	fc.eyecolor1 = eyecolor1;
	fc.eyecolor2 = eyecolor2;
	fc.hairstyle = hairstyle;
	fc.beard = beard;
	fc.face = face;
	packet.PutData(2, &fc, sizeof(fc));

	// T167 test seam, mirroring SendPositionUpdate()'s: capture the body we
	// built BEFORE the connection gate, so a test can assert on the real
	// encoder output without a live connection. See Test_GetLastFaceChange().
	m_test_last_face_change.sent = true;
	m_test_last_face_change.body.haircolor = haircolor;
	m_test_last_face_change.body.beardcolor = beardcolor;
	m_test_last_face_change.body.eyecolor1 = eyecolor1;
	m_test_last_face_change.body.eyecolor2 = eyecolor2;
	m_test_last_face_change.body.hairstyle = hairstyle;
	m_test_last_face_change.body.beard = beard;
	m_test_last_face_change.body.face = face;
	m_test_last_face_change.body.valid = true;

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Face change requested");
	}
}

// ============================================================================
// Phase 4: Misc
// ============================================================================

void EverQuest::SendRandom(uint32_t low, uint32_t high)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RandomReq);

	EQT::RandomReq_Struct rnd;
	rnd.low = low;
	rnd.high = high;
	packet.PutData(2, &rnd, sizeof(rnd));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Random {} to {}", low, high);
	}
}

void EverQuest::ZoneProcessRandomReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::RandomReply_Struct)) {
		return;
	}

	const EQT::RandomReply_Struct* rnd = reinterpret_cast<const EQT::RandomReply_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string name(rnd->name, strnlen(rnd->name, 64));
	LOG_INFO(MOD_MAIN, "{} rolled {} ({} to {})", name, rnd->result, rnd->low, rnd->high);
	AddChatSystemMessage(fmt::format("**A Magic Die is rolled by {}. It could have been {} to {}. It is {}.",
		name, rnd->low, rnd->high, rnd->result));
}

void EverQuest::SendFindPerson(uint32_t npc_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FindPersonRequest);

	EQT::FindPersonRequest_Struct fp;
	fp.npc_id = npc_id;
	fp.client_pos.x = PosX();
	fp.client_pos.y = PosY();
	fp.client_pos.z = PosZ();
	packet.PutData(2, &fp, sizeof(fp));

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Find person request for NPC: {}", npc_id);
	}
}

void EverQuest::ZoneProcessFindPersonReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::FindPerson_Point)) {
		return;
	}

	const EQT::FindPerson_Point* dest = reinterpret_cast<const EQT::FindPerson_Point*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	LOG_INFO(MOD_MAIN, "Find person result: destination at ({}, {}, {})", dest->x, dest->y, dest->z);
	AddChatSystemMessage(fmt::format("Path destination: {:.1f}, {:.1f}, {:.1f}", dest->x, dest->y, dest->z));
}

void EverQuest::SendRewind()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Rewind);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Rewind requested");
		AddChatSystemMessage("Attempting to rewind to a safe position...");
	}
}

void EverQuest::SendYellForHelp()
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_YellForHelp);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Yelling for help!");
		AddChatSystemMessage("You yell for help!");
	}
}

void EverQuest::SendReport(const std::string& report_text)
{
	// Simplified report - just send the text
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Report);
	packet.PutUInt32(2, 0);  // category_id
	// For simplicity, we'll just send minimal data
	packet.PutString(6, report_text);

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Report submitted");
		AddChatSystemMessage("Your report has been submitted.");
	}
}

// T150: payload corrected against eqemu source while wiring the first caller.
// zone/client_packet.cpp's Handle_OP_FriendsWho does:
//     char *FriendsString = (char*)app->pBuffer;  FriendsWho(FriendsString);
// i.e. the ENTIRE application payload is one NUL-terminated, comma-delimited
// list of character names -- there is no struct and no Titanium encoder for
// this opcode (grep FriendsWho over common/patches finds only the opcode name).
// world/clientlist.cpp's ClientList::SendFriendsWho() then strchr()s that
// string for ',' and FindCharacter()s each name; the requester's spawn id and
// name are taken server-side from ServerFriendsWho_Struct::FromID/FromName
// (filled by the ZONE from GetID()/GetName()), never from the client payload.
// The previous form wrote a uint32 spawn id at offset 2 and the string at 6,
// so the first "name" the server parsed began with four raw binary bytes and
// could never match a character. It had zero callers, so nothing regressed.
void EverQuest::SendFriendsWho(const std::string& friends_list)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_FriendsWho);
	// PutCString, not PutString: the server strchr()s this buffer, so the
	// NUL terminator is load-bearing (Packet::PutString does not write one).
	packet.PutCString(2, friends_list.c_str());

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Friends who requested for: {}", friends_list);
	}
}

// ============================================================================
// Cluster 12: CameraEffect / PlayMP3 / Sound
// ============================================================================

void EverQuest::ZoneProcessCameraEffect(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::Camera_Struct)) {
		return;
	}

	const EQT::Camera_Struct* cam = reinterpret_cast<const EQT::Camera_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	LOG_DEBUG(MOD_MAIN, "Camera effect: duration={}ms, intensity={}", cam->duration, cam->intensity);
	// Could trigger camera shake in renderer if implemented
}

void EverQuest::ZoneProcessPlayMP3(const EQ::Net::Packet &p)
{
	if (p.Length() < 4) {
		return;
	}

	const char* filename = reinterpret_cast<const char*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string file(filename, strnlen(filename, p.Length() - 2));
	LOG_DEBUG(MOD_AUDIO, "Play MP3 request: {}", file);

#ifdef WITH_AUDIO
	if (m_audio_manager) {
		m_audio_manager->playMusic(file, false);  // Play once, don't loop
	}
#endif
}

void EverQuest::ZoneProcessSound(const EQ::Net::Packet &p)
{
	// Sound packet format (Titanium):
	// Offset 0-1: Opcode
	// Offset 2-5: Sound ID (uint32_t)
	// Offset 6-9: X position (float, optional)
	// Offset 10-13: Y position (float, optional)
	// Offset 14-17: Z position (float, optional)

	if (p.Length() < 6) {  // Minimum: opcode + sound ID
		LOG_DEBUG(MOD_AUDIO, "Sound packet too small: {} bytes", p.Length());
		return;
	}

	const uint8_t* data = static_cast<const uint8_t*>(p.Data());
	uint32_t soundId = *reinterpret_cast<const uint32_t*>(data + 2);

	// Check for position data
	bool hasPosition = (p.Length() >= 18);  // opcode + sound ID + 3 floats
	float x = 0, y = 0, z = 0;
	if (hasPosition) {
		x = *reinterpret_cast<const float*>(data + 6);
		y = *reinterpret_cast<const float*>(data + 10);
		z = *reinterpret_cast<const float*>(data + 14);
	}

	LOG_DEBUG(MOD_AUDIO, "Sound effect: id={} pos=({:.1f}, {:.1f}, {:.1f}) hasPos={}",
		soundId, x, y, z, hasPosition);

#ifdef WITH_AUDIO
	// Don't play sounds during zone loading
	if (m_audio_manager && m_loading_phase == LoadingPhase::COMPLETE) {
		if (hasPosition) {
			m_audio_manager->playSound(soundId, glm::vec3(x, y, z));
		} else {
			m_audio_manager->playSound(soundId);
		}
	}
#endif
}

