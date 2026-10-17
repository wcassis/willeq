#include "client/eq.h"
#include "client/core/memory_ledger.h"  // T504: per-load ledger points
#include "client/movement/movement_component.h"  // T346: ApplyPositionState
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/raycast_mesh.h"  // B951: owner rows for the collision map arrays
#include "client/combat.h"
#include "client/corpse_predicate.h"  // T185: player-corpse rename on the death path
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/position_frames.h"  // T332: the wire<->render z conversion
#include "client/zone/zone_display_name.h"  // B1088 (#408): the loading screen's zone name
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// P28: inventory/chat/hotbar/command-registry are plain data-model headers
// with no renderer dependency (used unconditionally below: m_inventory_manager,
// m_hotbar_model, m_command_registry are complete-type-required here). Only
// hotkey_manager.h (pulls in <irrlicht.h>) and json_config.h/loading_thread.h
// (renderer-only helpers) stay gated.
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"
#include "client/chat/chat_message_buffer.h"
#include "client/hotbar/hotbar_model.h"
#include "client/graphics/ui/command_registry.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#endif

#include "client/bridge/game_state_bridge.h"
#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/loading_thread.h"
#endif
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#include "client/audio/zone_audio_manager.h"
#include "client/audio/sfx_manager.h"
#include "client/audio/sound_assets.h"
#include "client/audio/door_sounds.h"
#include "client/audio/water_sounds.h"
#endif

#include "common/net/posix_udp_transport.h"
#include "client/animation_constants.h"
#include <array>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <map>
#include <tuple>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


// Zone packet processors
void eqt::zone::ZoneComponent::ZoneProcessNewZone(const EQ::Net::Packet &p)
{
	// NewZone packet contains zone information
	// NewZone_Struct: char_name[64], zone_short_name[32], zone_long_name[278], ...
	// zone_id at offset 684, total struct size 700 bytes
	if (p.Length() >= 96) {  // Need at least 64 + 32 bytes for zone name
		// Skip opcode (2 bytes) and char_name (64 bytes) to get to zone_short_name
		// T288: parse into locals and let the single m_game_state.world()
		// .setZone() call below be the ONE write. Writing the name here via
		// setZoneName() would fire ZoneChanged BEFORE the safe coordinates
		// below are parsed, so the event would carry the previous zone's
		// arrival position.
		std::string zone_name = p.GetCString(66);  // 2 (opcode) + 64 (char_name)
		uint16_t zone_id = 0;

		// Extract zone_id if packet is large enough (offset 684 in struct, 686 in packet)
		if (p.Length() >= 688) {  // 2 (opcode) + 684 + 2 (zone_id)
			zone_id = p.GetUInt16(686);
		}

		// B1088 (#408): the server's long name (NewZone_Struct struct
		// offset 96 = packet offset 98, right after zone_short_name) is
		// AUTHORITATIVE for this zone and is the only source that knows the
		// ids the era table omits (tutorialb). noteNewZone stores it and
		// re-derives the loading screen's stored name when this IS the zone
		// being entered (logged only when the line changes). An empty field
		// leaves the era table's answer in place. Bounded GetCString: the
		// scan stops at the packet end, and the struct's 278-char field is
		// NUL-terminated in practice.
		if (p.Length() >= 99 && zone_id != 0) {
			const std::string longName = p.GetCString(98);
			ZoneDisplayNames::instance().noteNewZone(zone_id, longName);
		}

		// Extract zone environment data (sky type, fog colors)
		// Offsets are +2 for opcode prefix
		if (p.Length() >= 477) {  // Need sky field at offset 474 (+2 = 476)
			// Zone type at offset 374 (0=outdoor, 1=dungeon, etc.)
			m_zone_type = p.GetUInt8(376);

			// Sky type at offset 474
			m_zone_sky_type = p.GetUInt8(476);

			// Fog colors at offsets 375-386
			for (int i = 0; i < 4; i++) {
				m_zone_fog_red[i] = p.GetUInt8(377 + i);
				m_zone_fog_green[i] = p.GetUInt8(381 + i);
				m_zone_fog_blue[i] = p.GetUInt8(385 + i);
			}

			// Fog clip distances at offsets 388-419
			if (p.Length() >= 422) {
				for (int i = 0; i < 4; i++) {
					m_zone_fog_minclip[i] = p.GetFloat(390 + i * 4);
					m_zone_fog_maxclip[i] = p.GetFloat(406 + i * 4);
				}
			}

			if (EverQuest::GetDebugLevel() >= 3) {
				LOG_DEBUG(MOD_MAIN, "Zone environment: ztype={}, sky={}, fog RGB=({},{},{}), clip={}-{}",
					m_zone_type, m_zone_sky_type,
					m_zone_fog_red[0], m_zone_fog_green[0], m_zone_fog_blue[0],
					m_zone_fog_minclip[0], m_zone_fog_maxclip[0]);
			}
		}

		// Extract safe spawn coordinates (struct offsets 492/496/500, +2 for opcode)
		if (p.Length() >= 504) {
			float safe_server_y = p.GetFloat(494);  // safe_y in struct = server Y
			float safe_server_x = p.GetFloat(498);  // safe_x in struct = server X
			float safe_z = p.GetFloat(502);         // safe_z
			// Swap X/Y from server INVERSEXY format to client coords
			m_safe_x = safe_server_y;
			m_safe_y = safe_server_x;
			m_safe_z = safe_z;
			LOG_DEBUG(MOD_ZONE, "Zone safe coordinates: server=({:.2f},{:.2f},{:.2f}) -> client=({:.2f},{:.2f},{:.2f})",
			          safe_server_x, safe_server_y, safe_z, m_safe_x, m_safe_y, m_safe_z);
		}

		// Phase 7.3: Also update WorldState
		// T28: pass the confirmed arrival position (same m_safe_x/y/z the bridge
		// publish below uses) so the EventBus ZoneChanged payload matches the
		// bridge's, instead of reading the unrelated pending-zone fields (which
		// are usually still zero here -- see refactor/deferred.md "Dead parallel
		// state copies..." entry). Heading is 0.0f to match the bridge publish
		// immediately below; this client does not yet track arrival heading.
		ctx_.game_state.world().setZone(zone_name, zone_id,
			m_safe_x, m_safe_y, m_safe_z, 0.0f);

		// Publish ZoneChanged event to bridge
		if (ctx_.bridge) {
			eqt::state::ZoneChangedData data;
			data.zoneName = ctx_.self.CurrentZoneName();
			data.zoneId = ctx_.self.CurrentZoneId();
			data.x = m_safe_x;
			data.y = m_safe_y;
			data.z = m_safe_z;
			data.heading = 0.0f;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneChanged, std::move(data)));
		}

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data for: {} (zone_id={})",
				ctx_.self.CurrentZoneName(), ctx_.self.CurrentZoneId());
		}

		// T504 (load-step audit): the game thread's own zone loads were ONE
		// unmeasured interval between login_world and zone_load_start
		// (+16.5 MiB of client heap in qeynos2). One ledger point per load, so
		// each has a row: navmesh, collision map, zone lines, zone sources.
		eqt::memory::recordPoint("game_zone_begin", ctx_.self.CurrentZoneName());

		// B941 (GitLab #259): each load is a named `game_state` owner row
		// (WILLEQ_LEDGER_OWNERS=game_state prints them); everything the game
		// thread allocates outside these four stays in the catch-all row.
		// Load pathfinder for this zone
		{
			eqt::memory::StartupScope owner("gs.navmesh");
			ctx_.Movement().LoadPathfinder(ctx_.self.CurrentZoneName());
		}
		eqt::memory::recordPoint("game_pathfinder", ctx_.self.CurrentZoneName());

		// Load zone map for terrain height calculations. B951 (GitLab #269):
		// the raycast mesh's final arrays get their own gs.collision_map.*
		// rows (the hooks open/close a sub-owner exactly like StartupScope).
		{
			eqt::memory::StartupScope owner("gs.collision_map");
			setRaycastMeshOwnerHooks(
				[](const char* name) -> unsigned {
					const unsigned prev = eqt::memory::detail::t_sublabel;
					eqt::memory::detail::t_sublabel = eqt::memory::startupSublabelIndex(name);
					return prev;
				},
				[](unsigned prev) { eqt::memory::detail::t_sublabel = static_cast<uint8_t>(prev); });
			LoadZoneMap(ctx_.self.CurrentZoneName());
		}
		eqt::memory::recordPoint("game_zone_map", ctx_.self.CurrentZoneName());

		// Load zone lines for zone transitions
		{
			eqt::memory::StartupScope owner("gs.zone_lines");
			LoadZoneLines(ctx_.self.CurrentZoneName());
		}

		// Open zone S3D archive, parse WLD, extract BSP tree
		{
			eqt::memory::StartupScope owner("gs.zone_sources");
			LoadZoneSources(ctx_.self.CurrentZoneName());
		}
		eqt::memory::recordPoint("game_zone_sources", ctx_.self.CurrentZoneName());

		// Update zone music and sound emitters
#ifdef WITH_AUDIO
		// Phase 13: Load zone sound emitters FIRST (unloadZone() stops music)
		if (ctx_.zone_audio_manager && !ctx_.eq_client_path.empty()) {
			ctx_.zone_audio_manager->loadZone(ctx_.self.CurrentZoneName(), ctx_.eq_client_path);
		}
		// THEN start zone music (after loadZone to avoid being stopped by unloadZone)
		if (ctx_.audio_manager) {
			ctx_.audio_manager->onZoneChange(ctx_.self.CurrentZoneName());
		}
#endif
	} else {
		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "Received new zone data");
		}
	}
	
	m_new_zone_received = true;

	// NOTE: Zone graphics are now loaded in LoadZoneGraphics() which is called from
	// OnGameStateComplete() when we receive our first ClientUpdate. This ensures
	// the progress bar shows during the entire loading process.

	// After NewZone, send all parallel requests
	// 1. AA table request
	if (!m_aa_table_sent) {
		ZoneSendSendAATable();
	}
	
	// 2. Update AA
	if (!m_update_aa_sent) {
		ZoneSendUpdateAA();
	}
	
	// 3. Send tributes request
	if (!m_tributes_sent) {
		ZoneSendSendTributes();
	}
	
	// 4. Request guild tributes
	if (!m_guild_tributes_sent) {
		ZoneSendRequestGuildTributes();
	}

	// T228: NewZone is one of the five inputs to the zone-request phase gate
	// but, until now, the only handler that did not re-run the check. If the
	// AA-table / tribute-info / guild-tribute responses have all already
	// arrived (server causality makes that unusual, not impossible), this is
	// the last chance to send ReqClientSpawn. Idempotent: the check is
	// guarded by m_req_client_spawn_sent and its other inputs only ever go
	// false -> true, so calling it here with the counters still at zero is
	// a no-op (T04 pinned the gap, tests/test_zone_connection_flow_handshake.cpp).
	CheckZoneRequestPhaseComplete();
}

void eqt::zone::ZoneComponent::ZoneProcessPlayerProfile(const EQ::Net::Packet &p)
{
	ctx_.Bridge().SetLoadingPhase(LoadingPhase::ZONE_RECEIVING_PROFILE);

	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "PlayerProfile received, size={} bytes", p.Length());
	}

	// PlayerProfile_Struct from titanium_structs.h
	// All offsets need +2 for the opcode
	const size_t base = 2;

	// Extract all player data from the profile
	if (p.Length() < base + 19588) {  // PlayerProfile_Struct is 19588 bytes
		LOG_WARN(MOD_MAIN, "PlayerProfile packet too small: {} bytes (expected at least {})",
			p.Length(), base + 19588);
		return;
	}
	
	// T346 step 2: a PlayerProfile is the authoritative "you are in a zone
	// now" statement, and it is what closes the death round-trip -- the bind
	// re-entry ends here. Releasing SendPositionUpdate's POS_DEAD gate at
	// this point (and only here) is what keeps that gate from being
	// permanent: nothing else in the client ever cleared POS_DEAD, so a
	// respawned character would have stopped transmitting its position for
	// the rest of the session. ApplyPositionState(), not SetPositionState():
	// the server is telling us, so there is nothing to tell it back (T344).
	if (ctx_.game_state.player().positionState() == eqt::state::PositionState::Dead) {
		LOG_DEBUG(MOD_ZONE, "PlayerProfile: clearing Dead position state (bind re-entry complete)");
		ctx_.Movement().ApplyPositionState(POS_STANDING);
	}

	// Basic attributes (offsets from struct)
	uint32_t gender = p.GetUInt32(base + 4);     // offset 4
	uint32_t race = p.GetUInt32(base + 8);       // offset 8
	uint32_t class_ = p.GetUInt32(base + 12);    // offset 12
	uint8_t level = p.GetUInt8(base + 20);       // offset 20
	uint32_t deity = p.GetUInt32(base + 124);    // offset 124

	// Bind points - array of 5 BindStruct at offset 24, each 20 bytes
	// BindStruct: zone_id (4), x (4), y (4), z (4), heading (4)
	// First bind point (index 0) is the main bind location
	uint32_t bind_zone_id = p.GetUInt32(base + 24);       // offset 24
	float bind_x = p.GetFloat(base + 28);                  // offset 28
	float bind_y = p.GetFloat(base + 32);                  // offset 32
	float bind_z = p.GetFloat(base + 36);                  // offset 36
	float bind_heading = p.GetFloat(base + 40);            // offset 40

	// Appearance. T167: these seven fields were parsed into locals here and
	// read by nothing; the result is now STORED, same once-per-profile shape
	// as m_guild_id (T133) and m_gm_mode (T148) below. Offsets and the full
	// rationale live in client/player/profile_appearance.h, where a test pins them.
	m_profile_appearance = EQT::ParseProfileAppearance(p, base);

	// Experience points
	uint32_t exp = p.GetUInt32(base + 2268);     // offset 2268
	uint32_t exp_aa = p.GetUInt32(base + 2288);  // offset 2288 (AA exp)

	// Stats - these are base stats without equipment
	uint32_t cur_hp = p.GetUInt32(base + 2232);  // offset 2232
	uint32_t mana = p.GetUInt32(base + 2228);    // offset 2228
	uint32_t endurance = p.GetUInt32(base + 6148); // offset 6148
	uint32_t STR = p.GetUInt32(base + 2236);     // offset 2236
	uint32_t STA = p.GetUInt32(base + 2240);     // offset 2240
	uint32_t CHA = p.GetUInt32(base + 2244);     // offset 2244
	uint32_t DEX = p.GetUInt32(base + 2248);     // offset 2248
	uint32_t INT = p.GetUInt32(base + 2252);     // offset 2252
	uint32_t AGI = p.GetUInt32(base + 2256);     // offset 2256
	uint32_t WIS = p.GetUInt32(base + 2260);     // offset 2260

	// Practice points (unspent training points)
	uint32_t practice_points = p.GetUInt32(base + 2224);  // offset 2224

	// Currency - player's on-person money
	uint32_t platinum = p.GetUInt32(base + 4428);  // offset 4428
	uint32_t gold = p.GetUInt32(base + 4432);      // offset 4432
	uint32_t silver = p.GetUInt32(base + 4436);    // offset 4436
	uint32_t copper = p.GetUInt32(base + 4440);    // offset 4440

	// Bank currency - money stored in bank
	// From titanium_structs.h: platinum_bank at 13136, gold_bank at 13140, etc.
	uint32_t bank_platinum = p.GetUInt32(base + 13136);  // offset 13136
	uint32_t bank_gold = p.GetUInt32(base + 13140);      // offset 13140
	uint32_t bank_silver = p.GetUInt32(base + 13144);    // offset 13144
	uint32_t bank_copper = p.GetUInt32(base + 13148);    // offset 13148

	// Character identity
	char name[64] = {0};
	char last_name[32] = {0};
	if (p.Length() >= base + 13036) {
		memcpy(name, static_cast<const char*>(p.Data()) + base + 12940, 64);      // offset 12940
		memcpy(last_name, static_cast<const char*>(p.Data()) + base + 13004, 32); // offset 13004
	}
	uint32_t guild_id = p.GetUInt32(base + 13036); // offset 13036
	// T148: PlayerProfile.gm, offset 13054 -- 18 bytes past guild_id, with
	// pvp@13052 and anon@13053 in between. eqemu's own header comments this
	// field `// 0=no, 1=yes (guessing!)`, so it was CAPTURED off the wire
	// before being trusted: three logins with `#set gm on|off` toggled
	// between them read 0 -> 1 -> 0 here, while anon@13053 tracked its own
	// independent toggle and pvp@13052 stayed constant, and birthday@13040
	// matched character_data exactly (region alignment proven against an
	// authority outside the client). See
	// review/T148/playerprofile_gm_offset_annotated.txt.
	uint8_t gm_mode = p.GetUInt8(base + 13054);    // offset 13054
	// T317: PlayerProfile.guildrank, offset 13055 -- the byte immediately
	// after gm, in the same run of single-byte flags T148 walked. Titanium
	// encoding (0 member / 1 officer / 2 leader / 9 none); the server folds
	// its internal 0..8 scale into it in common/patches/titanium.cpp:1494.
	// This is the client's ONLY source for its own guild rank, and until now
	// it was not read at all -- which is why the two guild senders in
	// eq_guild.cpp were writing a literal into their rank fields.
	uint8_t guild_rank = p.GetUInt8(base + 13055);  // offset 13055
	
	// Position data is stored differently - it's in bind points at offset 24
	// But the current position is at offset 13116-13128
	// Note: Server x/y are swapped relative to zone geometry
	float server_x = p.GetFloat(base + 13116);      // offset 13116
	float server_y = p.GetFloat(base + 13120);      // offset 13120
	float z = p.GetFloat(base + 13124);      // offset 13124
	float heading = p.GetFloat(base + 13128); // offset 13128

	// Update our position (swap x and y from server)
	// PlayerProfile heading is stored as (12-bit scaled value / 4)
	// Convert back: server_heading_deg = heading * 360 / 512, then client heading = 90 - server_heading_deg
	//
	// LOAD-BEARING — DO NOT "HARMONIZE" THIS WITH THE OTHER HEADING SITES.
	// The `90 -` flip below is applied to the PLAYER's own heading here, and
	// deliberately NOT to other entities (ZoneProcessNewSpawn/ZoneProcessZoneSpawns
	// in eq_entities.cpp assign server_heading directly, with their own extra
	// /4.0f). That asymmetry looks
	// exactly like the duplicated-then-drifted defect class this codebase keeps
	// hitting, and it is not: players and NPCs do not share the same default
	// rotation, and the model files themselves carry differing default
	// rotations. The divergence here is compensating for a real per-model
	// difference in the assets.
	//
	// This offset was hard won. Do not remove, unify, or refactor it into a
	// shared helper "for consistency" without first establishing, by
	// measurement, the per-entity-type and per-model default rotations and what
	// the server actually expects. A single client cannot detect an error here
	// because it is self-consistent — validating this requires a second client
	// observing the first (see docs/CLAUDE.md, Heading/Rotation Conventions).
	float server_heading_deg = heading * 360.0f / 512.0f;
	float new_heading = 90.0f - server_heading_deg;
	if (new_heading < 0.0f) new_heading += 360.0f;
	if (new_heading >= 360.0f) new_heading -= 360.0f;
	// T58: this is the packet that determines landing position on ordinary
	// zone-in, and (per T55's own flagged follow-up) it was the last forced-
	// relocation site still writing only the raw position/heading members
	// members -- the state layer's PlayerState was never updated here. Route
	// through SetPosition/SetHeading (same helper T55 wired at the other
	// forced-relocation sites) so both copies stay in sync; this preserves
	// the x/y swap and the 12-bit-scaled heading math above bit-for-bit, it
	// only adds the missing state write. This does NOT double-publish
	// PlayerMoved to the renderer: SetPosition/SetHeading fire PlayerMoved
	// on EverQuest::m_game_state's OWN state-layer EventBus, which has no
	// production listener and is never forwarded to the GameStateBridge the
	// hand-rolled push below feeds (verified -- see
	// refactor/T58_playerprofile_propagation.md Review's trace table). The
	// hand-rolled bridge push below is unchanged and remains the only thing
	// that reaches the renderer.
	// T332 (B049), MEASURED LIVE: this z is the WIRE frame -- groundZ +
	// 0.625*size, exactly like a spawn packet's -- and GameState's player z is
	// the RENDER frame (include/client/position_frames.h). It was stored raw
	// and, unlike the two self-SPAWN edges T311 fixed, was never marked as
	// owing the conversion. On a real graphical zone-in NEITHER spawn edge runs
	// (T332 instrumented a live login: no ZoneSpawns-self and no NewSpawn-self
	// branch is ever taken for our own character -- the spawn id comes from
	// here), so the flag stayed false for the whole session, the deferred
	// conversion never fired, and SendPositionUpdate added 0.625*size to a z
	// that already contained it. The server persists what we send, so it
	// COMPOUNDED 0.625 every login: 3.75 -> 4.38 -> 5.01 -> 5.63 against the
	// official client's steady 3.751 (T331's table).
	//
	// Same two lines as the spawn edges: convert if we can, and if we cannot
	// (no model yet -- the usual case here, this packet is the first thing that
	// arrives) say so, so onPlayerModelGeometryChanged converts it exactly once
	// when the mesh offset becomes measurable.
	ctx_.self.SetPosition(server_y, server_x,
	            eqt::wireZToRenderZ(z, ctx_.game_state.player().collisionZOffset(),
	                                ctx_.game_state.player().size()));
	ctx_.game_state.player().setZAwaitingFrameConversion(
		ctx_.game_state.player().collisionZOffset() <= 0.0f);
	ctx_.self.SetHeading(new_heading);

	// Debug: Zone-in position tracking
	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_INFO(MOD_ZONE, "[ZONE-IN] PlayerProfile position: server=({:.2f},{:.2f},{:.2f}) heading={:.2f} -> client=({:.2f},{:.2f},{:.2f}) heading={:.2f}",
		         server_x, server_y, z, heading, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg());
		// Additional logging in consistent format for position debugging
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] profile_heading={:.2f} -> server_heading_deg={:.2f} -> heading={:.2f}deg",
			heading, server_heading_deg, ctx_.self.HeadingDeg());
		LOG_DEBUG(MOD_MOVEMENT, "POS S->C PlayerProfile [SELF] server_pos=({:.2f},{:.2f},{:.2f}) -> client_pos=({:.2f},{:.2f},{:.2f})",
			server_x, server_y, z, ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ());
	}

	// Sync initial position to bridge (important for Player mode)
	if (ctx_.bridge) {
		float heading512 = ctx_.self.HeadingDeg() * 512.0f / 360.0f;
		if (EverQuest::GetDebugLevel() >= 1) {
			LOG_INFO(MOD_ZONE, "[ZONE-IN] Publishing PlayerMoved from PlayerProfile: ({:.2f},{:.2f},{:.2f}) heading={:.2f}deg -> {:.2f} (512 fmt)",
			         ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg(), heading512);
		}
		eqt::state::PlayerMovedData data;
		data.x = ctx_.self.PosX(); data.y = ctx_.self.PosY(); data.z = ctx_.self.PosZ();
		data.heading = heading512;
		data.dx = 0; data.dy = 0; data.dz = 0;
		data.isMoving = false;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerMoved, std::move(data)));
	}

	// Store character stats (T286: the PlayerState below is the only store
	// for everything it holds; only fields PlayerState does NOT carry --
	// exp, AA exp, guild id, GM mode -- remain EverQuest members)
	m_exp = exp;
	m_exp_aa = exp_aa;
	// T133: this `guild_id` local (parsed just above, offset 13036) was
	// already being written into self_entity.guild_id further down --
	// display-only, per-entity data -- but never into m_guild_id, the field
	// every SendGuild*() sender in eq_guild.cpp guards on. That left
	// guildinvite/remove/demote/leader and guildmotd's set-path permanently
	// dead on the wire (T132's finding). This is the player's own profile
	// packet -- unlike Spawn_Struct's guildID (offset 238, decoded per-
	// entity in eq_entities.cpp and correctly NOT propagated here, since a
	// spawn's guildID describes THAT entity, not necessarily the player --
	// see ZoneProcessSpawnAll/NewSpawn), this value unconditionally
	// describes the player, so no spawn-id matching or ordering hazard
	// applies. See refactor/T133_guild_id_population.md Review for the
	// live-log evidence this is the correct, and only necessary, hook.
	ctx_.Guild().SetGuildID(guild_id);
	// T317: same once-per-login shape as m_guild_id above.
	ctx_.Guild().SetGuildRank(guild_rank);
	// T148: the login-time GM-mode gate. Same shape as m_guild_id above --
	// parsed from the player's own profile packet, assigned once, no spawn-id
	// matching. This is the ONLY place m_gm_mode is written: the check is
	// deliberately performed once at login and never refreshed mid-session
	// (Will's decision, 2026-08-19; the AT_GM SpawnAppearance live-update
	// path was considered and declined). It means "GM mode was on at login",
	// not "this account has GM privileges" -- see EverQuest::GetGMMode().
	m_gm_mode = (gm_mode != 0);

	// B430: the five server-owned Options-window settings, from the same
	// profile (titanium_structs.h PlayerProfile_Struct: anon @13053,
	// autosplit @13256, group/raid/guild autoconsent @19572..19574). The
	// window shows these, never a locally saved guess. A short profile (a
	// truncated test packet) publishes nothing.
	if (p.Length() >= base + 19575) {
		const uint8_t anon = p.GetUInt8(base + 13053);
		ctx_.game_state.player().setAnonymous(anon == 1);
		ctx_.game_state.player().setRoleplay(anon == 2);
		ctx_.Bridge().PublishProfileGameOptions(
			anon, p.GetUInt32(base + 13256), p.GetUInt8(base + 19572),
			p.GetUInt8(base + 19573), p.GetUInt8(base + 19574));
	}

	// Phase 7.2 / T286: the single store for all of the above
	auto& player = ctx_.game_state.player();
	player.setName(name[0] ? name : ctx_.character);  // Use server name if available, else local
	player.setLastName(last_name);
	player.setLevel(level);
	player.setClass(class_);
	player.setRace(race);
	player.setGender(gender);
	player.setDeity(deity);
	player.setHP(cur_hp, cur_hp);
	player.setMana(mana, mana);
	player.setEndurance(endurance, endurance);
	player.setAttributes(STR, STA, CHA, DEX, INT, AGI, WIS);
	player.setCurrency(platinum, gold, silver, copper);
	player.setBankCurrency(bank_platinum, bank_gold, bank_silver, bank_copper);
	player.setPracticePoints(practice_points);

	// Initialize skill manager with player info first
	if (ctx_.skill_manager) {
		ctx_.skill_manager->initialize(static_cast<uint8_t>(class_), static_cast<uint8_t>(race), level);

		// Skills are stored as uint32_t[100] starting at offset 4460
		// Reference: EQEmu titanium_structs.h PlayerProfile_Struct
		const size_t skill_offset = 4460;
		if (p.Length() >= base + skill_offset + (EQT::MAX_PP_SKILL * 4)) {
			uint32_t skills[EQT::MAX_PP_SKILL];
			int nonzero_count = 0;
			for (size_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
				skills[i] = p.GetUInt32(base + skill_offset + (i * 4));
				if (skills[i] > 0) {
					nonzero_count++;
					if (EverQuest::GetDebugLevel() >= 2) {
						LOG_DEBUG(MOD_MAIN, "Skill[{}] = {}", i, skills[i]);
					}
				}
			}
			ctx_.skill_manager->updateAllSkills(skills, EQT::MAX_PP_SKILL);
			LOG_INFO(MOD_MAIN, "Loaded {} skills from PlayerProfile ({} non-zero)", EQT::MAX_PP_SKILL, nonzero_count);
			if (ctx_.bridge) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillsRefreshed, eqt::state::SkillsRefreshedData{}));
			}
		} else {
			LOG_WARN(MOD_MAIN, "PlayerProfile too small for skills: {} < {}",
				p.Length(), base + skill_offset + (EQT::MAX_PP_SKILL * 4));
		}
	}

	// T43: was #ifdef EQT_HAS_GRAPHICS. m_inventory_manager is constructed
	// unconditionally since T41 (per P28's declassification of the
	// declaration); setPlayerInfo() is plain inventory-manager state (race/
	// class/level for item restriction checks) with no renderer dependency.
	// Left guarded, a headless build's inventory manager never learned the
	// player's race/class/level, so item-restriction validation silently
	// used stale defaults for the whole session.
	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->setPlayerInfo(race, class_, level);
	}

	// Store bind point (swap x/y like we do for position -- server x/y are swapped)
	ctx_.game_state.player().setBindPoint(bind_zone_id, bind_y, bind_x, bind_z, bind_heading);

	// The entity ID appears to be at offset 14384 based on previous debugging
	uint32_t entity_id = 0;
	if (p.Length() > base + 14384) {
		entity_id = p.GetUInt32(base + 14384);
		uint16_t new_spawn_id = static_cast<uint16_t>(entity_id);
		if (ctx_.self.MySpawnId() != 0 && ctx_.self.MySpawnId() != new_spawn_id) {
			LOG_WARN(MOD_ZONE, "Spawn ID changed unexpectedly: {} -> {}", ctx_.self.MySpawnId(), new_spawn_id);
		}
		// T288: this is the ordinary zone-in source of the player's own
		// spawn id, and before T288 it was the ONE write site that did not
		// mirror to PlayerState -- so the state layer's spawnId() stayed 0
		// for the session and getNearestPlayer()'s "skip myself" argument
		// could never exclude the player.
		ctx_.self.SetMySpawnId(new_spawn_id);
	}
	
	// For size, we need to check if it's sent in the profile or if we get it from spawn
	// It's not in the standard PlayerProfile_Struct, so we'll use default
	float size = 6.0f;  // Default player size
	
	if (EverQuest::GetDebugLevel() >= 2) {
		LOG_DEBUG(MOD_MAIN, "=== CHARACTER DATA ===");
		LOG_DEBUG(MOD_MAIN, "Name: {} {} (Level {} {} {})",
			name, last_name, level, eqt::strings::GetClassName(class_), eqt::strings::GetRaceName(race));
		LOG_DEBUG(MOD_MAIN, "Gender: {} Race: {} Class: {} Deity: {}",
			gender, race, class_, deity);
		LOG_DEBUG(MOD_MAIN, "=== POSITION DATA ===");
		LOG_DEBUG(MOD_MAIN, "Server raw: x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			server_x, server_y, z, heading);
		LOG_DEBUG(MOD_MAIN, "Internal (swapped): x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			ctx_.self.PosX(), ctx_.self.PosY(), ctx_.self.PosZ(), ctx_.self.HeadingDeg());
		LOG_DEBUG(MOD_MAIN, "Size: {:.2f} (default, will be updated from spawn)", size);
		LOG_DEBUG(MOD_MAIN, "=== BIND POINT ===");
		LOG_DEBUG(MOD_MAIN, "Bind raw: zone={} x={:.2f} y={:.2f} z={:.2f} heading={:.2f}",
			bind_zone_id, bind_x, bind_y, bind_z, bind_heading);
		LOG_DEBUG(MOD_MAIN, "Bind internal: zone={} m_game_state.player().bindX()={:.2f} m_game_state.player().bindY()={:.2f} m_game_state.player().bindZ()={:.2f}",
			ctx_.game_state.player().bindZoneId(), ctx_.game_state.player().bindX(), ctx_.game_state.player().bindY(), ctx_.game_state.player().bindZ());
		LOG_DEBUG(MOD_MAIN, "=== STATS ===");
		LOG_DEBUG(MOD_MAIN, "HP: {} Mana: {} End: {}", cur_hp, mana, endurance);
		LOG_DEBUG(MOD_MAIN, "STR:{} STA:{} CHA:{} DEX:{} INT:{} AGI:{} WIS:{}",
			STR, STA, CHA, DEX, INT, AGI, WIS);
		LOG_DEBUG(MOD_MAIN, "Currency: {}pp {}gp {}sp {}cp", platinum, gold, silver, copper);
		LOG_DEBUG(MOD_MAIN, "Bank Currency: {}pp {}gp {}sp {}cp", bank_platinum, bank_gold, bank_silver, bank_copper);
		LOG_DEBUG(MOD_MAIN, "Practice points (training sessions): {}", practice_points);
		LOG_DEBUG(MOD_MAIN, "Entity ID: {} (from offset 14384)", entity_id);
		LOG_DEBUG(MOD_MAIN, "===========================");
	}
	
	// Add ourselves to the entity list
	Entity self_entity;
	self_entity.spawn_id = ctx_.self.MySpawnId();
	self_entity.name = std::string(name);
	// Use already-swapped client x and y, and transformed heading
	self_entity.x = ctx_.self.PosX();
	self_entity.y = ctx_.self.PosY();
	self_entity.z = ctx_.self.PosZ();
	self_entity.heading = ctx_.self.HeadingDeg();  // Use transformed heading (360 - raw_heading)
	self_entity.level = level;
	self_entity.class_id = static_cast<uint8_t>(class_);
	self_entity.race_id = static_cast<uint16_t>(race);
	self_entity.gender = static_cast<uint8_t>(gender);
	self_entity.guild_id = guild_id;
	self_entity.hp_percent = 100;  // We'll get actual HP from spawn or HP update packets
	self_entity.cur_mana = static_cast<uint16_t>(mana);
	self_entity.max_mana = static_cast<uint16_t>(mana);  // Will be updated from spawn
	self_entity.size = size;
	self_entity.animation = 0;
	self_entity.delta_x = 0;
	self_entity.delta_y = 0;
	self_entity.delta_z = 0;
	self_entity.delta_heading = 0;
	self_entity.last_update_time = std::time(nullptr);
	
	// Update our own size
	ctx_.game_state.player().setSize(size);
	
	// Store in entity list (sync to both local map and EntityManager)
	ctx_.entities[ctx_.self.MySpawnId()] = self_entity;
	ctx_.Player().SyncEntityToGameState(self_entity);

	if (EverQuest::GetDebugLevel() >= 1) {
		LOG_DEBUG(MOD_MAIN, "Added self to entity list: {} (ID: {})", self_entity.name, ctx_.self.MySpawnId());
	}

	// Extract spell gems and spellbook from PlayerProfile
	// Titanium offsets: spell_book at 2312 (400 x uint32), mem_spells at 4360 (EQT::SPELL_GEM_COUNT x uint32)
	if (ctx_.spell_manager) {
		constexpr size_t SPELLBOOK_OFFSET = 2312;
		constexpr size_t MEM_SPELLS_OFFSET = 4360;
		constexpr size_t SPELLBOOK_SIZE = 400;
		constexpr size_t kGems = EQT::SPELL_GEM_COUNT;  // T483: all nine are stored

		// Extract spellbook (400 spell IDs)
		if (p.Length() >= base + SPELLBOOK_OFFSET + SPELLBOOK_SIZE * 4) {
			std::array<uint32_t, 400> spellbook;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				spellbook[i] = p.GetUInt32(base + SPELLBOOK_OFFSET + i * 4);
			}
			ctx_.spell_manager->setSpellbook(spellbook.data(), SPELLBOOK_SIZE);

			// Count non-empty spellbook entries for debug
			size_t scribed_count = 0;
			for (size_t i = 0; i < SPELLBOOK_SIZE; ++i) {
				if (spellbook[i] != 0xFFFFFFFF && spellbook[i] != 0) {
					scribed_count++;
				}
			}
			if (EverQuest::GetDebugLevel() >= 1) {
				LOG_DEBUG(MOD_SPELL, "Loaded {} spells from spellbook", scribed_count);
			}
		}

		// Extract memorized spells: all nine Titanium gems (the ninth is stored, never shown)
		if (p.Length() >= base + MEM_SPELLS_OFFSET + kGems * 4) {
			std::array<uint32_t, kGems> mem_spells;
			for (size_t i = 0; i < kGems; ++i) {
				mem_spells[i] = p.GetUInt32(base + MEM_SPELLS_OFFSET + i * 4);
			}
			ctx_.spell_manager->setSpellGems(mem_spells.data(), kGems);

			if (EverQuest::GetDebugLevel() >= 1) {
				LOG_DEBUG(MOD_SPELL, "Memorized spells:");
				for (size_t i = 0; i < kGems; ++i) {
					if (mem_spells[i] != 0xFFFFFFFF && mem_spells[i] != 0) {
						const EQ::SpellData* spell = ctx_.spell_manager->getSpell(mem_spells[i]);
						std::string name = spell ? spell->name : "Unknown";
						LOG_DEBUG(MOD_SPELL, "  Gem {}: {} (ID {})", i + 1, name, mem_spells[i]);
					}
				}
			}
		}
	}

	// Extract player buffs from PlayerProfile
	// Titanium offsets: buffs at 5008, 25 SpellBuff_Struct entries (20 bytes each)
	if (ctx_.buff_manager) {
		constexpr size_t BUFF_OFFSET = 5008;
		constexpr size_t BUFF_COUNT = 25;
		constexpr size_t BUFF_STRUCT_SIZE = 20;

		if (p.Length() >= base + BUFF_OFFSET + BUFF_COUNT * BUFF_STRUCT_SIZE) {
			std::array<EQT::SpellBuff_Struct, 25> buffs;
			for (size_t i = 0; i < BUFF_COUNT; ++i) {
				size_t offset = base + BUFF_OFFSET + i * BUFF_STRUCT_SIZE;
				buffs[i].effect_type = p.GetUInt8(offset + 0);
				buffs[i].level = p.GetUInt8(offset + 1);
				buffs[i].bard_modifier = p.GetUInt8(offset + 2);
				buffs[i].unknown003 = p.GetUInt8(offset + 3);
				buffs[i].spellid = p.GetUInt32(offset + 4);
				buffs[i].duration = p.GetInt32(offset + 8);
				buffs[i].counters = p.GetUInt32(offset + 12);
				buffs[i].player_id = p.GetUInt32(offset + 16);
			}
			ctx_.buff_manager->setPlayerBuffs(buffs.data(), BUFF_COUNT);
			// T277: and tell the renderer (PublishPlayerBuffs,
			// eq_player_buffs.cpp). Without
			// this the buff bar starts every session empty -- eqemu never
			// sends OP_Buff for a buff that is merely still running.
			ctx_.Player().PublishPlayerBuffs(buffs.data(), BUFF_COUNT);

			if (EverQuest::GetDebugLevel() >= 1) {
				size_t active_count = 0;
				for (size_t i = 0; i < BUFF_COUNT; ++i) {
					if (buffs[i].spellid != 0 && buffs[i].spellid != 0xFFFFFFFF && buffs[i].effect_type != 0) {
						active_count++;
						if (ctx_.spell_manager) {
							const EQ::SpellData* spell = ctx_.spell_manager->getSpell(buffs[i].spellid);
							std::string name = spell ? spell->name : "Unknown";
							LOG_DEBUG(MOD_SPELL, "  Buff slot {}: {} (ID {}, duration={})",
								i, name, buffs[i].spellid, buffs[i].duration);
						}
					}
				}
				LOG_DEBUG(MOD_SPELL, "Loaded {} active buffs from profile", active_count);
			}
		}
	}

	// Calculate stats from equipment and update
	ctx_.Bridge().UpdateInventoryStats();

	// Publish CharacterInfoChanged event to bridge
	if (ctx_.bridge) {
		eqt::state::CharacterInfoChangedData data;
		data.name = ctx_.character;
		data.level = ctx_.game_state.player().level();
		data.className = eqt::strings::GetClassName(ctx_.game_state.player().classId());
		data.deity = eqt::strings::GetDeityName(ctx_.game_state.player().deity());
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CharacterInfoChanged, std::move(data)));
	}
}







void eqt::zone::ZoneComponent::ZoneProcessAnimation(const EQ::Net::Packet &p)
{
	// OP_Animation packet format (T243: was ZoneProcessEmote):
	// Offset 0-1: Opcode
	// Offset 2-3: spawn_id (uint16)
	// Offset 4: animation_speed (uint8)
	// Offset 5: animation_id (uint8)
	if (p.Length() < 6) {
		return;
	}

	uint16_t spawn_id = p.GetUInt16(2);
	uint8_t anim_speed = p.GetUInt8(4);
	uint8_t anim_id = p.GetUInt8(5);

	if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(spawn_id)) {
		LOG_DEBUG(MOD_ENTITY, "[EMOTE] spawn_id={}, speed={}, anim_id={}", spawn_id, anim_speed, anim_id);
	}

	// Update entity animation state
	auto it = ctx_.entities.find(spawn_id);
	if (it != ctx_.entities.end()) {
		it->second.animation = anim_id;
	}

	// Publish animation events via bridge (D17b: bridge is sole path to renderer)
	if (ctx_.bridge) {
		// Map animation ID to EQ model animation code
		std::string animCode;
		bool loop = false;
		bool playThrough = true;  // Most emotes/attacks should play through

		// Get entity's weapon skill types for weapon-based attack animations
		// Read from game state (entity struct) instead of querying the renderer (D17a)
		uint8_t primaryWeaponSkill = 255;
		uint8_t secondaryWeaponSkill = 255;
		auto weaponIt = ctx_.entities.find(spawn_id);
		if (weaponIt != ctx_.entities.end()) {
			primaryWeaponSkill = weaponIt->second.primary_weapon_skill;
			secondaryWeaponSkill = weaponIt->second.secondary_weapon_skill;
		}

		// Map common animation IDs to EQ model animation codes
		// Combat animations (these are the key ones for NPC combat)
		switch (anim_id) {
		// Combat attack animations - use weapon-based animation
		case 1:  // Primary hand attack
		case 5:  // Attack
		case 6:  // Attack2
			// Use weapon skill type to determine correct animation
			animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			break;
		case 2:  // Secondary hand attack
			// Use secondary weapon skill type
			animCode = EQ::getWeaponAttackAnimation(secondaryWeaponSkill, true, false);
			break;

		// Combat skill animations (use correct animation codes from reference)
		// For the PLAYER, queue these into the combat buffer so they're
		// processed with proper priority alongside weapon attacks
		case 10: // Round kick
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_ROUND_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued round kick animation for player");
				break;
			}
			animCode = EQ::ANIM_ROUND_KICK;  // c11
			break;
		case 11: // ANIM_KICK
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued kick animation for player");
				break;
			}
			animCode = EQ::ANIM_KICK;  // c01 - basic kick
			break;
		case 12: // ANIM_BASH
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_BASH}));
				LOG_DEBUG(MOD_COMBAT, "Queued bash animation for player");
				break;
			}
			animCode = EQ::ANIM_BASH;  // c07 - Shield bash
			break;
		case 13: // Tiger Claw / Eagle Strike (rapid strikes)
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_TIGER_STRIKE}));
				LOG_DEBUG(MOD_COMBAT, "Queued tiger claw/eagle strike animation for player");
				break;
			}
			animCode = EQ::ANIM_TIGER_STRIKE;  // t08
			break;
		case 14: // Flying kick
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_FLYING_KICK}));
				LOG_DEBUG(MOD_COMBAT, "Queued flying kick animation for player");
				break;
			}
			animCode = EQ::ANIM_FLYING_KICK;  // t07
			break;
		case 15: // Dragon Punch
			if (spawn_id == ctx_.self.MySpawnId()) {
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::CombatSkillAnimation,
					eqt::state::CombatSkillAnimationData{spawn_id, EQ::ANIM_DRAGON_PUNCH}));
				LOG_DEBUG(MOD_COMBAT, "Queued dragon punch animation for player");
				break;
			}
			animCode = EQ::ANIM_DRAGON_PUNCH;  // t09
			break;

		// Damage/hit reaction
		case 3:  // Damage from front
		case 4:  // Damage from back
			// Server sends damage emote when entity takes melee damage
			// For the PLAYER, we need to integrate this with the combat buffer system
			// so that attack animations take priority over damage reactions when the
			// player is both attacking and being attacked simultaneously
			if (spawn_id == ctx_.self.MySpawnId()) {
				// Queue received damage into player's combat buffer instead of playing immediately
				// This allows the buffer processing to decide attack vs damage priority
				ctx_.bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::ReceivedDamageAnimation,
					eqt::state::ReceivedDamageAnimationData{spawn_id}));
				LOG_DEBUG(MOD_COMBAT, "Queued received damage animation for player (will process with combat buffer)");
				break;  // Don't play immediately - buffer will handle it
			}
			animCode = EQ::ANIM_DAMAGE_MINOR;  // d01
			break;

		// Death
		case 16: // ANIM_DEATH
			animCode = EQ::ANIM_DEATH;  // d05
			break;

		// Social/Emote animations (s01-s28)
		// EQ Titanium animation IDs mapped to pre-Luclin animation codes
		case 18: // ANIM_CHEER
			animCode = EQ::ANIM_EMOTE_CHEER;  // s01 - Cheer
			break;
		case 19: // ANIM_KNEEL
			animCode = EQ::ANIM_CROUCHING;  // l08 - Crouching
			loop = true;
			playThrough = false;
			break;
		case 20: // ANIM_JUMP
			animCode = EQ::ANIM_FALLING;  // l05 - Falling/jump
			break;
		case 21: // ANIM_CRY / ANIM_MOURN
			animCode = EQ::ANIM_EMOTE_MOURN;  // s02 - Mourn/Disappointed
			break;
		case 23: // ANIM_RUDE
			animCode = EQ::ANIM_EMOTE_RUDE;  // s04 - Rude
			break;
		case 24: // ANIM_YAWN
			animCode = EQ::ANIM_EMOTE_YAWN;  // s05 - Yawn
			break;
		case 26: // ANIM_NOD
			animCode = EQ::ANIM_EMOTE_NOD;  // s06 - Nod
			break;
		case 27: // ANIM_AMAZED
			animCode = EQ::ANIM_EMOTE_AMAZED;  // s07 - Amazed
			break;
		case 28: // ANIM_PLEAD
			animCode = EQ::ANIM_EMOTE_PLEAD;  // s08 - Plead
			break;
		case 29: // ANIM_WAVE
			animCode = EQ::ANIM_EMOTE_WAVE;  // s03 - Wave
			break;
		case 30: // ANIM_CLAP
			animCode = EQ::ANIM_EMOTE_CLAP;  // s09 - Clap
			break;
		case 31: // ANIM_DISTRESS
			animCode = EQ::ANIM_EMOTE_DISTRESS;  // s10 - Distress/Hungry
			break;
		case 32: // ANIM_BLUSH
			animCode = EQ::ANIM_EMOTE_BLUSH;  // s11 - Blush
			break;
		case 33: // ANIM_CHUCKLE
			animCode = EQ::ANIM_EMOTE_CHUCKLE;  // s12 - Chuckle
			break;
		case 34: // ANIM_COUGH / ANIM_BURP
			animCode = EQ::ANIM_EMOTE_BURP;  // s13 - Burp/Cough
			break;
		case 35: // ANIM_DUCK
			animCode = EQ::ANIM_EMOTE_DUCK;  // s14 - Duck
			break;
		case 36: // ANIM_PUZZLE / ANIM_LOOK_AROUND
			animCode = EQ::ANIM_EMOTE_PUZZLE;  // s15 - Look Around/Puzzle
			break;
		case 58: // ANIM_DANCE
			animCode = EQ::ANIM_EMOTE_DANCE;  // s16 - Dance
			loop = true;
			playThrough = false;
			break;
		case 59: // ANIM_BLINK
			animCode = EQ::ANIM_EMOTE_BLINK;  // s17 - Blink
			break;
		case 60: // ANIM_GLARE
			animCode = EQ::ANIM_EMOTE_GLARE;  // s18 - Glare
			break;
		case 61: // ANIM_DROOL
			animCode = EQ::ANIM_EMOTE_DROOL;  // s19 - Drool
			break;
		case 62: // ANIM_KNEEL_EMOTE
			animCode = EQ::ANIM_EMOTE_KNEEL;  // s20 - Kneel (emote)
			break;
		case 63: // ANIM_LAUGH
			animCode = EQ::ANIM_EMOTE_LAUGH;  // s21 - Laugh
			break;
		case 64: // ANIM_POINT
			animCode = EQ::ANIM_EMOTE_POINT;  // s22 - Point
			break;
		case 65: // ANIM_SHRUG / ANIM_PONDER
			animCode = EQ::ANIM_EMOTE_SHRUG;  // s23 - Ponder/Shrug
			break;
		case 66: // ANIM_READY
			animCode = EQ::ANIM_EMOTE_READY;  // s24 - Ready
			break;
		case 67: // ANIM_SALUTE
			animCode = EQ::ANIM_EMOTE_SALUTE;  // s25 - Salute
			break;
		case 68: // ANIM_SHIVER
			animCode = EQ::ANIM_EMOTE_SHIVER;  // s26 - Shiver
			break;
		case 69: // ANIM_TAP_FOOT
			animCode = EQ::ANIM_EMOTE_TAP_FOOT;  // s27 - Tap Foot
			break;
		case 70: // ANIM_BOW
			animCode = EQ::ANIM_EMOTE_BOW;  // s28 - Bow
			break;

		// Bard instrument animations
		case 43: // ANIM_STRINGED_INSTRUMENT
			animCode = EQ::ANIM_STRINGED_INST;  // t02 - Stringed Instrument
			loop = true;
			playThrough = false;
			break;
		case 44: // ANIM_WIND_INSTRUMENT
			animCode = EQ::ANIM_WIND_INST;  // t03 - Wind Instrument
			loop = true;
			playThrough = false;
			break;

		// Casting animations (when triggered via emote packet)
		case 42: // ANIM_CAST
			animCode = EQ::ANIM_CAST_PULLBACK;  // t04 - Cast Pull Back
			break;

		// Special animations
		case 105: // ANIM_LOOT
			animCode = EQ::ANIM_POSE_KNEEL;  // p05 - kneeling/looting
			break;

		default:
			// Unknown animation - use weapon-based attack for combat-range IDs
			if (anim_id >= 1 && anim_id <= 15) {
				animCode = EQ::getWeaponAttackAnimation(primaryWeaponSkill, false, false);
			}
			break;
		}

		if (!animCode.empty()) {
			eqt::state::EntityAnimationEventData data;
			data.spawnId = spawn_id;
			data.animCode = anim_id;
			data.animName = animCode;
			data.loop = loop;
			data.playThrough = playThrough;
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::EntityAnimationEvent, std::move(data)));
			if (EverQuest::GetDebugLevel() >= 2 || IsTrackedTarget(spawn_id)) {
				LOG_DEBUG(MOD_ENTITY, "[EMOTE] Set animation '{}' on spawn_id={} (anim_id={}, weaponSkill={})",
					animCode, spawn_id, anim_id, primaryWeaponSkill);
			}
		}
	}
}







void eqt::zone::ZoneComponent::ZoneProcessGuildMOTD(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_ZONE, "Received guild MOTD");

	// GuildMOTD is the last packet from SendZoneInPackets()
	// Now we send SetServerFilter and ClientReady
	if (!m_server_filter_sent) {
		ZoneSendSetServerFilter();
		m_server_filter_sent = true;
	}

	if (!ctx_.game_state.world().isClientReady()) {
		ctx_.Bridge().SetLoadingPhase(LoadingPhase::ZONE_PLAYER_READY, "Finalizing connection...");
		ZoneSendClientReady();
		ctx_.game_state.world().setClientReady(true);  // T286: only store

		// Note: We don't start the update loop here - we wait for our first ClientUpdate
		// which confirms the server has our position. StartUpdateLoop() is called in
		// ZoneProcessClientUpdate() when we receive our own spawn's position update.
		// That's also where we trigger OnGameStateComplete() for graphics loading.
		LOG_DEBUG(MOD_ZONE, "Sent ClientReady, waiting for position confirmation...");
	}
}





void eqt::zone::ZoneComponent::CheckZoneRequestPhaseComplete()
{
	// Check if we've received all expected responses from Zone Request phase
	// We need to have received:
	// 1. At least one AA table (m_aa_table_count > 0)
	// 2. RespondAA packet
	// 3. At least one tribute info (m_tribute_count > 0)
	// 4. At least one guild tribute (m_guild_tribute_count > 0)

	// For simplicity, we'll check if we've received at least one of each
	if (m_new_zone_received &&
	    m_aa_table_count > 0 &&
	    m_tribute_count > 0 &&
	    m_guild_tribute_count > 0 &&
	    !m_req_client_spawn_sent) {

		ctx_.Bridge().SetLoadingPhase(LoadingPhase::ZONE_REQUEST_PHASE);
		LOG_INFO(MOD_ZONE, "Zone Request phase complete, sending ReqClientSpawn");

		// Move to Player Spawn Request phase
		ZoneSendReqClientSpawn();
		m_req_client_spawn_sent = true;
	}
}



