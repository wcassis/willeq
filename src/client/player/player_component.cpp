// EverQuest emote/AFK/anon/roleplay/camp/logout, rezz/who/inspect, and
// corpse/consent handlers.
//
// Clusters 6-8 of eq_player_info.cpp, split off by Batch J unit J07. Moved
// verbatim, no behavior change. See refactor/J07_eq_player_info_split.md and
// refactor/F07_player_info.md.

#include "client/player/player_component.h"
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

#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>

// ============================================================================
// Cluster 6: Emote/AFK/Anon/RP/Camp/Logout
// ============================================================================

void eqt::player::PlayerComponent::PerformEmote(uint32_t animation)
{
	if (!ctx_.self.IsFullyZonedIn()) {
		return;
	}

	// Send the emote animation using the proper Animation packet
	ctx_.Entities().SendAnimation(static_cast<uint8_t>(animation));
	
	// Some emotes should stop movement
	if (animation == ANIM_KNEEL || animation == ANIM_DEATH) {
		if (ctx_.game_state.player().isMoving()) {
			ctx_.Movement().StopMovement();
		}
	}
	
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Performing emote animation {}", animation);
	}
}

void eqt::player::PlayerComponent::SetAFK(bool afk)
{
	if (ctx_.game_state.player().isAFK() == afk) {
		return;
	}

	ctx_.game_state.player().setAFK(afk);  // Phase 7.8 / T286: only store
	ctx_.Entities().SendSpawnAppearance(AT_AFK, afk ? 1 : 0);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "AFK status: {}", afk ? "ON" : "OFF");
	}
}

void eqt::player::PlayerComponent::SetAnonymous(bool anon)
{
	if (ctx_.game_state.player().isAnonymous() == anon) {
		return;
	}

	ctx_.game_state.player().setAnonymous(anon);  // Phase 7.8 / T286: only store
	ctx_.Entities().SendSpawnAppearance(AT_ANONYMOUS, anon ? 1 : 0);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Anonymous status: {}", anon ? "ON" : "OFF");
	}
}

void eqt::player::PlayerComponent::SetRoleplay(bool rp)
{
	if (ctx_.game_state.player().isRoleplay() == rp) {
		return;
	}

	ctx_.game_state.player().setRoleplay(rp);  // Phase 7.8 / T286: only store
	ctx_.Entities().SendSpawnAppearance(AT_ANONYMOUS, rp ? 2 : 0);  // 2 = roleplay

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Roleplay status: {}", rp ? "ON" : "OFF");
	}
}

void eqt::player::PlayerComponent::StartCampTimer()
{
	if (ctx_.game_state.player().isCamping()) {
		return;  // Already camping
	}

	// Phase 7.8 / T286: PlayerState is the only store, start time included
	ctx_.game_state.player().setCamping(true);
	ctx_.game_state.player().setCampStartTime(std::chrono::steady_clock::now());

	// Send OP_Camp to server to notify we're camping
	SendCamp();

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer started, will log out in {} seconds", CAMP_TIMER_SECONDS);
	}
}

void eqt::player::PlayerComponent::CancelCamp()
{
	if (!ctx_.game_state.player().isCamping()) {
		return;
	}

	ctx_.game_state.player().setCamping(false);  // Phase 7.8 / T286: only store
	ctx_.out.System("You are no longer camping.");

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Camp timer cancelled");
	}
}

void eqt::player::PlayerComponent::UpdateCampTimer()
{
	if (!ctx_.game_state.player().isCamping()) {
		return;
	}

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - ctx_.game_state.player().campStartTime()).count();

	if (elapsed >= CAMP_TIMER_SECONDS) {
		// Camp timer complete, send logout to server
		ctx_.game_state.player().setCamping(false);  // Phase 7.8 / T286: only store
		ctx_.out.System("You have camped.");

		// Send OP_Logout to server - server will respond with OP_LogoutReply
		SendLogout();

		if (EverQuest::GetDebugLevel() >= 1) {
			LOG_DEBUG(MOD_MAIN, "Camp timer complete, sending logout request");
		}
	}
}

void eqt::player::PlayerComponent::SendCamp()
{
	// OP_Camp is an empty packet -- body-only convention: QueuePacket(opcode,
	// pkt) prepends the opcode itself (eq.cpp), so writing it into the body
	// here too made this a 4-byte packet where the server expects 2.
	EQ::Net::DynamicPacket packet;
	ctx_.net.Queue(HC_OP_Camp, &packet);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Camp to server");
	}
}

void eqt::player::PlayerComponent::SendLogout()
{
	// OP_Logout is an empty packet -- body-only convention, see SendCamp().
	EQ::Net::DynamicPacket packet;
	ctx_.net.Queue(HC_OP_Logout, &packet);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_Logout to server");
	}
}

void eqt::player::PlayerComponent::ZoneProcessLogoutReply(const EQ::Net::Packet &p)
{
	// T322: on a zone change the source zone answers our graceful zone-out
	// (OP_SaveOnZoneReq + OP_DeleteSpawn) with this same OP_LogoutReply --
	// eqemu Handle_OP_DeleteSpawn queues it before calling Disconnect().
	// That is an acknowledgement that we may now tear the session down, NOT
	// a logout: quitting the game here would end the session mid-transition.
	// The flag is only ever set by BeginGracefulZoneOut(), so when no
	// zone-out is in flight -- camp/SendLogout, and the world-side path --
	// the behaviour below is exactly what it always was.
	if (ctx_.Zone().ZoneOutHandshakePending()) {
		LOG_DEBUG(MOD_ZONE, "Source zone acknowledged graceful zone-out");
		ctx_.Zone().ZoneOutAcknowledged() = true;
		return;
	}

	// Server confirmed logout - now we can safely disconnect
	LOG_INFO(MOD_MAIN, "Logout confirmed by server");

	// T322: a logout is not a zone transition; make sure a stale latch can
	// never make the next world login claim to be one.
	ctx_.Zone().ZoneToZoneInProgress() = false;

	// D20c4: Signal quit via flag — Application checks and exits
	ctx_.lifecycle.RequestQuit();

	// H03: Don't call DisconnectFromZone() here — this handler runs from inside
	// DaybreakConnectionManager::Tick()'s packet-processing loop (Tick -> ProcessPacket
	// -> ZoneOnPacketRecv -> DispatchZonePacket -> here). DisconnectFromZone() resets
	// m_zone_connection_manager, destroying the very object whose Tick() is still on
	// the call stack; the next loop iteration then dereferences a dangling m_transport
	// (use-after-free crash — reproduced via GM fast-camp, which triggers an
	// almost-immediate OP_LogoutReply). Defer to UpdateMovement(), which — like the
	// existing m_game_state.world().zoneChangeApproved() deferral — runs outside any callback context.
	ctx_.lifecycle.m_pending_logout_disconnect = true;
}

// ============================================================================
// Cluster 7: Rezz / WhoAll / Inspect
// ============================================================================

void eqt::player::PlayerComponent::ZoneProcessRezzRequest(const EQ::Net::Packet &p)
{
	// Server is offering a resurrection
	if (p.Length() < 2 + sizeof(EQT::Resurrect_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_RezzRequest packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::Resurrect_Struct* rez = reinterpret_cast<const EQT::Resurrect_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Store the pending rez offer
	m_has_pending_rezz = true;
	m_pending_rezz = *rez;

	// Display the rez offer in chat
	std::string rezzer_name(rez->rezzer_name, strnlen(rez->rezzer_name, 64));
	std::string corpse_name(rez->corpse_name, strnlen(rez->corpse_name, 64));

	LOG_INFO(MOD_MAIN, "Resurrection offer from {} for corpse '{}' (spell {})",
		rezzer_name, corpse_name, rez->spellid);

	ctx_.out.System(fmt::format("{} is offering to resurrect you.", rezzer_name));
	ctx_.out.System("Type /accept to accept or /decline to decline the resurrection.");

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Rez location: zone={}, instance={}, pos=({}, {}, {})",
			rez->zone_id, rez->instance_id, rez->x, rez->y, rez->z);
	}
}

void eqt::player::PlayerComponent::ZoneProcessRezzComplete(const EQ::Net::Packet &p)
{
	// Resurrection is complete
	m_has_pending_rezz = false;
	memset(&m_pending_rezz, 0, sizeof(m_pending_rezz));

	LOG_INFO(MOD_MAIN, "Resurrection complete");
	ctx_.out.System("You have been resurrected!");

	// The server will handle the zone change if needed via OP_ZoneChange
}

void eqt::player::PlayerComponent::SendRezzAnswer(bool accept)
{
	if (!m_has_pending_rezz) {
		ctx_.out.System("You don't have a pending resurrection offer.");
		return;
	}

	// Copy the pending rez struct and set the action
	EQT::Resurrect_Struct rez = m_pending_rezz;
	rez.action = accept ? 1 : 0;

	// Build and send the packet. T134: this used to build its own
	// opcode-prefixed packet (Resize(2+struct); PutUInt16(0, opcode);
	// memcpy at +2) and THEN call EverQuest::QueuePacket(opcode, &packet),
	// which itself prepends another 2-byte opcode -- the same
	// double-opcode-prefix bug T133 found and fixed in eq_guild.cpp's
	// senders (confirmed via the eqemu zone log's "Wrong size" complaint).
	// Fixed by building a body-only packet and calling QueuePacket once.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Resurrect_Struct));
	packet.PutData(0, &rez, sizeof(EQT::Resurrect_Struct));
	ctx_.net.Queue(HC_OP_RezzAnswer, &packet);

	if (accept) {
		LOG_INFO(MOD_MAIN, "Accepted resurrection offer");
		ctx_.out.System("You have accepted the resurrection.");
	} else {
		LOG_INFO(MOD_MAIN, "Declined resurrection offer");
		ctx_.out.System("You have declined the resurrection.");
		// Clear pending rez on decline
		m_has_pending_rezz = false;
		memset(&m_pending_rezz, 0, sizeof(m_pending_rezz));
	}
}

void eqt::player::PlayerComponent::SendWhoAllRequest(const std::string& name, int lvllow, int lvlhigh,
	int race, int class_, bool gm)
{
	EQT::Who_All_Struct who;
	memset(&who, 0, sizeof(who));

	// Copy name filter (up to 63 chars + null)
	if (!name.empty()) {
		strncpy(who.whom, name.c_str(), 63);
		who.whom[63] = '\0';
	}

	// Set filters (-1 means no filter, use 0xFFFFFFFF)
	who.wrace = (race < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(race);
	who.wclass = (class_ < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(class_);
	who.lvllow = (lvllow < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(lvllow);
	who.lvlhigh = (lvlhigh < 0) ? 0xFFFFFFFF : static_cast<uint32_t>(lvlhigh);
	who.gmlookup = gm ? 1 : 0xFFFFFFFF;

	// Build and send the packet
	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Who_All_Struct));
	packet.PutData(0, &who, sizeof(EQT::Who_All_Struct));
	ctx_.net.Queue(HC_OP_WhoAllRequest, &packet);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_WhoAllRequest: name='{}', race={}, class={}, lvl={}-{}",
			name, race, class_, lvllow, lvlhigh);
	}
}

void eqt::player::PlayerComponent::ZoneProcessWhoAllResponse(const EQ::Net::Packet &p)
{
	// 64-byte WhoAllReturnHeader followed by `playercount` packed
	// variable-length entries (layout documented in client/who_response.h).
	// T224: before this the entries were never parsed and /who printed only
	// the count line.
	if (p.Length() < 2 + sizeof(EQT::WhoAllReturnHeader)) {
		LOG_WARN(MOD_MAIN, "OP_WhoAllResponse packet too small: {} bytes", p.Length());
		return;
	}

	const uint8_t* data = static_cast<const uint8_t*>(p.Data()) + 2;
	const size_t data_len = p.Length() - 2;
	const EQT::WhoAllReturnHeader* header = reinterpret_cast<const EQT::WhoAllReturnHeader*>(data);

	const uint32_t player_count = header->playercount;
	LOG_INFO(MOD_MAIN, "Who response: {} player(s)", player_count);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Who header: id={}, playerineqstring={}, playersinzonestring={}, playercount={}",
			header->id, header->playerineqstring, header->playersinzonestring, header->playercount);
	}

	// String-table hooks: the client's eqstr db first, the built-in copies
	// of the who strings when it has no entry (unloaded db, headless tests).
	EQT::WhoFormatContext ctx;
	ctx.eqstr = [this](uint32_t id) {
		std::string s = ctx_.strings.Db().getString(id);
		return s.empty() ? EQT::whoDefaultString(id) : s;
	};
	ctx.className = [](uint32_t c) { return eqt::strings::GetClassName(c); };
	ctx.raceName = [](uint32_t r) { return eqt::strings::GetRaceName(r); };
	ctx.zoneName = [](uint32_t z) {
		return EQT::ZoneLines::zoneShortNameFromId(static_cast<uint16_t>(z));
	};

	// Line 1: "Players in EverQuest:" (5001). World's friends-who sends
	// 0xFFFFFFFF here and an empty rule line -- both are then skipped.
	if (header->playerineqstring != 0 && header->playerineqstring != EQT::kWhoStringNone) {
		std::string title = ctx.eqstr(header->playerineqstring);
		if (!title.empty()) {
			ctx_.out.System(title);
		}
	}

	// Line 2: the rule ("---------------------------").
	if (header->line[0] != '\0') {
		ctx_.out.System(std::string(header->line, strnlen(header->line, sizeof(header->line))));
	}

	// Entries.
	std::vector<EQT::WhoAllEntry> entries;
	const uint8_t* body = data + sizeof(EQT::WhoAllReturnHeader);
	const size_t body_len = data_len - sizeof(EQT::WhoAllReturnHeader);
	if (!EQT::parseWhoAllEntries(body, body_len, player_count, entries)) {
		LOG_WARN(MOD_MAIN, "OP_WhoAllResponse: header claims {} entries, only {} parsed from {} bytes",
			player_count, entries.size(), body_len);
	}
	for (const auto& e : entries) {
		// T256 live leg: world's SendWhoAll counts an anon player whose admin
		// level equals the viewer's (a GM /who-ing itself while /anon) but
		// then skips writing that entry, so the count says N and the packet
		// carries 49 zeroed bytes in its place. A zero format string is never
		// a real entry (5022..5025 on the wire); rendering it produced a
		// phantom "[0 Class0] (Race0)" line.
		if (e.formatString == 0) {
			continue;
		}
		ctx_.out.System(EQT::formatWhoAllEntry(e, ctx));
	}

	// Last line: the count string (5028 / 5036 / 5029). T178: eqemu's string
	// carries a `%1` placeholder for the count; fill it rather than appending
	// a raw ": N". Fall back to appending only when the string had no
	// placeholder, so an unexpected string never drops the number.
	std::string count_text;
	if (header->playersinzonestring != 0 && header->playersinzonestring != EQT::kWhoStringNone) {
		count_text = ctx.eqstr(header->playersinzonestring);
	}
	if (count_text.empty()) {
		count_text = EQT::whoDefaultString(player_count == 0 ? EQT::kWhoStrNoPlayers
		                                  : player_count == 1 ? EQT::kWhoStrOnePlayer
		                                                      : EQT::kWhoStrManyPlayers);
	}
	bool substituted = false;
	std::string filled = EQT::substituteWhoCount(count_text, player_count, substituted);
	if (substituted || player_count == 0) {
		ctx_.out.System(filled);
	} else {
		ctx_.out.System(fmt::format("{}: {}", filled, player_count));
	}
}

void eqt::player::PlayerComponent::SendInspectRequest(uint32_t target_id)
{
	// Build the inspect request
	EQT::Inspect_Struct inspect;
	inspect.TargetID = target_id;
	inspect.PlayerID = ctx_.self.MySpawnId();

	// Build and send the packet
	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::Inspect_Struct));
	packet.PutData(0, &inspect, sizeof(EQT::Inspect_Struct));
	ctx_.net.Queue(HC_OP_InspectRequest, &packet);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "Sent OP_InspectRequest for target {}", target_id);
	}
}

// Answers an inspect request aimed at us.
//
// ⚠ THIS WRITES TO THE CHARACTER'S SERVER-SIDE INSPECT MESSAGE, and there is
// no way to avoid it. Handle_OP_InspectAnswer (zone/client_packet.cpp:9045-
// 9049) memcpy()s our `text` field over the character's inspect message and
// calls SaveCharacterInspectMessage() unconditionally, on every answer. The
// packet is length-checked EXACT, so the field cannot be omitted.
//
// We send it EMPTY, because this client genuinely does not know the value:
// eqemu loads the inspect message at zone-in (client_packet.cpp:1367) and
// NEVER sends it to a Titanium client -- the real client owns the text, in its
// own inspect window. So a character who set an inspect message elsewhere will
// have it cleared the first time a WillEQ session is inspected.
//
// That is a real, if small, loss, and it is accepted deliberately: the
// alternative is not answering at all, which leaves inspect completely broken
// between two WillEQ clients. The fix is a client-side inspect message the
// user can set and this sender can echo -- recorded as the follow-up in
// refactor/T170_inspect_answer.md, NOT done here, because a setter with no way
// to reach it is the trap T168 just finished removing.
void eqt::player::PlayerComponent::SendInspectAnswer(uint32_t requester_id)
{
	EQT::InspectResponse_Struct answer =  // non-const: PutData takes void*
		EQT::MakeInspectAnswer(requester_id, ctx_.self.MySpawnId(), ctx_.Bridge().GetInspectMessage());

	// T170 test seam, same shape as T88's and T167's: capture the body before
	// the connection gate so a test can assert on the real encoder output.
	m_test_last_inspect_answer.sent = true;
	m_test_last_inspect_answer.target_id = answer.TargetID;
	m_test_last_inspect_answer.playerid = answer.playerid;

	// Body-only: QueuePacket(opcode, pkt) prepends the opcode itself.
	EQ::Net::DynamicPacket packet;
	packet.Resize(sizeof(EQT::InspectResponse_Struct));
	packet.PutData(0, &answer, sizeof(EQT::InspectResponse_Struct));
	ctx_.net.Queue(HC_OP_InspectAnswer, &packet);

	LOG_DEBUG(MOD_MAIN, "Answered inspect request from entity {}", requester_id);
}

void eqt::player::PlayerComponent::ZoneProcessInspectRequest(const EQ::Net::Packet &p)
{
	// Someone is inspecting us
	if (p.Length() < 2 + sizeof(EQT::Inspect_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_InspectRequest packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::Inspect_Struct* inspect = reinterpret_cast<const EQT::Inspect_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// Find the inspector's name
	std::string inspector_name = "Someone";
	auto it = ctx_.entities.find(static_cast<uint16_t>(inspect->PlayerID));
	if (it != ctx_.entities.end()) {
		inspector_name = it->second.name;
	}

	LOG_INFO(MOD_MAIN, "{} is inspecting you", inspector_name);
	ctx_.out.System(fmt::format("{} is inspecting you.", inspector_name));

	// T170: ANSWER IT. This is the half that was missing. eqemu forwards the
	// request to us precisely because a Titanium client is expected to reply
	// (Handle_OP_InspectRequest, client_packet.cpp:9084-9087 -- only SoF+
	// clients and bots get a server-built answer). Without this reply the
	// inspector's window stays empty forever, and no single-client test could
	// ever have shown it: the request and the empty window are on two
	// different machines.
	SendInspectAnswer(inspect->PlayerID);
}

void eqt::player::PlayerComponent::ZoneProcessInspectAnswer(const EQ::Net::Packet &p)
{
	// Received inspection data about someone
	if (p.Length() < 2 + sizeof(EQT::InspectResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_InspectAnswer packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::InspectResponse_Struct* response = reinterpret_cast<const EQT::InspectResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	// T170: the inspected player is `playerid`, NOT TargetID. In an ANSWER,
	// TargetID is the ROUTING field -- the requester, i.e. US, since we only
	// receive answers we asked for. Reading it here named the inspecting
	// player (ourselves) as the inspected one. eqemu's own builder settles it:
	// Client::ProcessInspectRequest, zone/client.cpp:6590-6591.
	std::string target_name = "Unknown";
	auto it = ctx_.entities.find(static_cast<uint16_t>(response->playerid));
	if (it != ctx_.entities.end()) {
		target_name = it->second.name;
	}

	LOG_INFO(MOD_MAIN, "Received inspect data for {}", target_name);
	ctx_.out.System(fmt::format("Inspecting {}:", target_name));

	// Display equipped items
	for (int i = 0; i < 22; i++) {
		if (response->itemnames[i][0] != '\0') {
			std::string item_name(response->itemnames[i], strnlen(response->itemnames[i], 64));
			ctx_.out.System(fmt::format("  Slot {}: {}", i, item_name));
		}
	}

	// Display inspect message if present
	if (response->text[0] != '\0') {
		std::string message(response->text, strnlen(response->text, 288));
		ctx_.out.System(fmt::format("Message: {}", message));
	}
}

// ============================================================================
// Cluster 8: Corpse / Consent
// ============================================================================

void eqt::player::PlayerComponent::SendCorpseDrag(const std::string& corpse_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_CorpseDrag);

	EQT::CorpseDrag_Struct drag;
	memset(&drag, 0, sizeof(drag));
	strncpy(drag.CorpseName, corpse_name.c_str(), 63);
	strncpy(drag.DraggerName, ctx_.character.c_str(), 63);
	packet.PutData(2, &drag, sizeof(drag));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.zone_connection->QueuePacket(packet);
		m_is_dragging_corpse = true;
		m_dragged_corpse_name = corpse_name;
		LOG_INFO(MOD_MAIN, "Started dragging corpse: {}", corpse_name);
		ctx_.out.System(fmt::format("You begin to drag {}.", corpse_name));
	}
}

void eqt::player::PlayerComponent::SendCorpseDrop()
{
	if (!m_is_dragging_corpse) {
		ctx_.out.System("You are not dragging a corpse.");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_CorpseDrop);

	EQT::CorpseDrag_Struct drop;
	memset(&drop, 0, sizeof(drop));
	strncpy(drop.CorpseName, m_dragged_corpse_name.c_str(), 63);
	strncpy(drop.DraggerName, ctx_.character.c_str(), 63);
	packet.PutData(2, &drop, sizeof(drop));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Dropped corpse: {}", m_dragged_corpse_name);
		ctx_.out.System(fmt::format("You stop dragging {}.", m_dragged_corpse_name));
		m_is_dragging_corpse = false;
		m_dragged_corpse_name.clear();
	}
}

void eqt::player::PlayerComponent::SendConsiderCorpse(uint32_t corpse_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_ConsiderCorpse);

	// T153: this used to write a bare 4-byte corpse id, so the packet was
	// dropped before the handler ever ran. eqemu's Handle_OP_ConsiderCorpse
	// (zone/client_packet.cpp:5245) gates on
	// `app->size != sizeof(Consider_Struct)` -- 28 bytes -- and returns via a
	// LogDebug with no error path, so the failure was silent on both ends.
	// titanium.cpp has no ENCODE/DECODE for OP_Consider, and
	// Titanium::structs::Consider_Struct (common/patches/titanium_structs.h:1160)
	// is byte-identical to the base common/eq_packet_structs.h:1351 layout, so
	// the 28-byte body goes raw. The handler reads only `targetid`; every other
	// field is server-filled on the reply, which is why the live
	// CombatManager::ConsiderTarget() path sends them zeroed too -- this is the
	// same body, not an invented one.
	EQT::Consider_Struct con;
	memset(&con, 0, sizeof(con));
	con.playerid = ctx_.self.GetEntityID();
	con.targetid = corpse_id;
	packet.PutData(2, &con, sizeof(con));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Considering corpse ID: {}", corpse_id);
	}
}

// ============================================================================
// Phase 3: Consent System
// ============================================================================

void eqt::player::PlayerComponent::SendConsent(const std::string& player_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Consent);
	packet.PutString(2, player_name);

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Granting corpse consent to: {}", player_name);
		ctx_.out.System(fmt::format("You have granted {} permission to drag your corpse.", player_name));
	}
}

void eqt::player::PlayerComponent::SendConsentDeny(const std::string& player_name)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_ConsentDeny);
	packet.PutString(2, player_name);

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.zone_connection->QueuePacket(packet);
		LOG_INFO(MOD_MAIN, "Denying corpse consent for: {}", player_name);
		ctx_.out.System(fmt::format("You have denied {} permission to drag your corpse.", player_name));
	}
}

void eqt::player::PlayerComponent::ZoneProcessConsentResponse(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::ConsentResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_ConsentResponse packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::ConsentResponse_Struct* consent = reinterpret_cast<const EQT::ConsentResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string grantname(consent->grantname, strnlen(consent->grantname, 64));
	std::string ownername(consent->ownername, strnlen(consent->ownername, 64));
	std::string zonename(consent->zonename, strnlen(consent->zonename, 32));

	LOG_INFO(MOD_MAIN, "Consent granted: {} can now loot {}'s corpse in {}",
		grantname, ownername, zonename);
	ctx_.out.System(fmt::format("{} has been given permission to drag your corpse.", grantname));
}

void eqt::player::PlayerComponent::ZoneProcessDenyResponse(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::ConsentResponse_Struct)) {
		LOG_WARN(MOD_MAIN, "OP_DenyResponse packet too small: {} bytes", p.Length());
		return;
	}

	const EQT::ConsentResponse_Struct* consent = reinterpret_cast<const EQT::ConsentResponse_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string grantname(consent->grantname, strnlen(consent->grantname, 64));

	LOG_INFO(MOD_MAIN, "Consent denied for: {}", grantname);
	ctx_.out.System(fmt::format("{}'s permission to drag your corpse has been revoked.", grantname));
}

