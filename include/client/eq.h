#pragma once

#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_recorder.h"
#include "common/packet_structs.h"
#include "client/eq_profile_appearance.h"

// T171: SaveInspectMessage()/LoadInspectMessage() and the shared config-node
// helpers take Json::Value by pointer/reference only, so a forward
// declaration keeps jsoncpp out of this header (it is included by ~everything).
namespace Json { class Value; }
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include "client/string_database.h"
#include "client/door_state_manager.h"
#include "client/zone_load_snapshot.h"
#include "client/raid_update.h"
#include "client/hotbar/hotbar_slot_type.h"  // T239: ActivateHotbarCommandSlot
#include <openssl/des.h>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>
#include <array>
#include <optional>
#include <functional>

// Forward declarations
class IPathfinder;
class HCMap;
class CombatManager;
class TradeManager;

namespace eqt {
    struct WorldObject;
}

namespace EQ {
    class SpellManager;
    class BuffManager;
    class SkillManager;
}

namespace EQT {
    class ZoneLines;
    // Trade packet structures (forward declarations)
    struct TradeRequest_Struct;
    struct TradeRequestAck_Struct;
    struct TradeCoins_Struct;
    struct TradeAcceptClick_Struct;
    struct CancelTrade_Struct;
}

namespace eqt {
namespace bridge {
    class GameStateBridge;
    class IrrlichtBridge;
}
// U07c3: WindowManager deleted
class HotbarModel;
}

#ifdef WITH_AUDIO
namespace EQT {
namespace Audio {
    class AudioManager;
    class ZoneAudioManager;
}
}
#endif

#ifdef EQT_HAS_GRAPHICS
namespace EQT {
namespace Graphics {
    class IrrlichtRenderer;
    enum class ConstrainedRenderingPreset;
    enum class ZoneLoadStep : uint8_t;
}
}
#include "client/graphics/constrained_renderer_config.h"
// D20e3: forward declaration for the renderer's own progress-screen type
// (owned by Application, signalled via a pointer). BspTree/PfsArchive/
// WldLoader moved out below — P28: they are zone-load data types with no
// renderer dependency (see zone_bsp.h/pfs.h/wld_loader.h), used by base
// zone loading (LoadZoneSources in eq_zone.cpp) regardless of graphics.
namespace EQT { namespace Graphics { struct LoadingStatus; } }
#endif

// P28: these are plain game-state/data types (inventory manager, chat command
// registry, hotbar button model, zone BSP/archive/WLD loading, the movement
// intent struct) with no renderer dependency — they were misclassified
// under EQT_HAS_GRAPHICS by 55b101e. Un-guarded so the headless
// (EQT_GRAPHICS=OFF) build can see them.
namespace EQT { namespace Graphics { struct BspTree; class PfsArchive; class WldLoader; } }
namespace eqt { namespace events { struct PlayerPositionChanged; } }
namespace eqt {
namespace inventory {
    class InventoryManager;
    struct ItemInstance;   // T276: ResolveCarriedItem() returns one
}
namespace ui {
    class CommandRegistry;

    // T156: eqt::ui::HotbarButtonType (Spell=0, Skill=1, Item=2, Command=3)
    // and PendingHotbarButton were deleted here. HotbarButtonType was the
    // second of three disagreeing hotbar numberings; its only remaining user
    // was PendingHotbarButton, whose four EverQuest accessors had zero callers
    // anywhere in src/, include/ or tests/ (the reachability pattern again).
    // The one surviving numbering is eqt::HotbarSlotType, in
    // include/client/hotbar/hotbar_slot_type.h.
}
}

// The free-standing enumerations, opcode tables and small structs that used to
// live here now sit in the three headers below, included at their original
// position so declaration order is unchanged.
#include "client/eq_opcodes.h"
#include "client/eq_enums.h"
#include "client/eq_types.h"
#include "client/guild/guild_component.h"  // K01: ClientContext + GuildComponent
#include "client/chat/chat_component.h"    // K03: ChatComponent

class EverQuest
{
public:
	EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character);
	~EverQuest();

	// Initiate login server connection (call after graphics init to avoid timeout during slow startup)
	void ConnectToLogin();

	// Poll-based network tick: drains recv, processes protocol, resends.
	// Call from main loop instead of EQ::EventLoop::Get().Process().
	void TickNetwork();

	// Replay/test seam (Batch R): dispatches a decoded application packet
	// through the same switch ZoneOnPacketRecv uses, without requiring a
	// live m_zone_connection. `opcode` is the application opcode (HC_OP_*)
	// and `data`/`size` is the payload excluding the 2-byte opcode prefix,
	// matching include/common/net/session_log.h's RecordHeader layout.
	// Any handler-triggered sends no-op safely (see m_replay_suppressed_sends).
	void ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size);

	// T05: same replay/test seam as ReplayZonePacket, for the login and
	// world opcode switches.
	void ReplayLoginPacket(uint16_t opcode, const uint8_t* data, size_t size);
	void ReplayWorldPacket(uint16_t opcode, const uint8_t* data, size_t size);

	// Count of zone packet sends suppressed because m_zone_connection was
	// null (replay/test mode). Only incremented by the Zone packet senders
	// reachable from S->C handlers (see ReplayZonePacket).
	uint64_t GetReplaySuppressedSendCount() const { return m_replay_suppressed_sends; }

	// T52 test seam: m_game_state.world().pendingZoneX()/y/z/id have three producers
	// (ZoneProcessZoneChange, CheckZoneLine, and the bind/death path in
	// eq_player_state.cpp) and one consumer (WorldProcessZoneServerInfo), all private.
	// Minimal, behavior-preserving read-only accessors so tests can pin
	// each producer's coordinate convention without a live connection --
	// combined with ReplayZonePacket (for producer A) or a direct
	// UpdateMovement()/CheckZoneLine() drive (for producer B, via existing
	// public entry points), and GetPosition() for the consumer's effect.
	uint16_t GetPendingZoneId() const { return m_game_state.world().pendingZoneId(); }
	float GetPendingZoneX() const { return m_game_state.world().pendingZoneX(); }
	float GetPendingZoneY() const { return m_game_state.world().pendingZoneY(); }
	float GetPendingZoneZ() const { return m_game_state.world().pendingZoneZ(); }

	// T55 test seam: narrow, read-only reader for the state layer's player
	// position (m_game_state.player().x()/y()/z()), which otherwise has no
	// public accessor. T288 deleted the raw m_x/m_y/m_z members entirely, so
	// this and GetPosition() now read the SAME storage -- the tests that
	// compare the two are coherence assertions, and they are the ones that
	// fail if a future edit re-introduces a second copy. Deliberately does
	// not widen GameState's public surface beyond this one read. Not called
	// from any production path.
	glm::vec3 Test_GetGameStatePlayerPosition() const;

	// T52 test seam, continued: WorldProcessZoneServerInfo's consumer
	// branch is gated on `m_game_state.world().isZoneConnected() || m_zone_connection !=
	// nullptr`, which in real operation is only ever set true by a live
	// UDP connection-state callback (ZoneOnStatusChangeReconnectEnabled) --
	// not reachable through ReplayZonePacket/ReplayWorldPacket at all, and
	// (per this unit's live wire evidence) never true by the time
	// WorldProcessZoneServerInfo actually runs in the traced production
	// flows either (ProcessDeferredZoneChange always disconnects first).
	// This setter exists ONLY so a fast unit test can force that branch to
	// execute and pin its corrected (no second swap) behavior, rather than
	// leaving a fixed-but-untested dead branch. Test-only; not called from
	// any production path.
	void Test_SetZoneConnectedForSeam(bool connected) { m_game_state.world().setZoneConnected(connected); }

	// T88 test seam: SendPositionUpdate()'s top guard
	// (`!IsFullyZonedIn() || !m_zone_connection`) and MoveToWithPath()'s
	// "Not in zone yet" guard both require a live UDP zone connection that
	// no headless test can construct. Everything past those guards already
	// degrades gracefully with a null m_zone_connection --
	// SafeQueueZonePacket() and SendMovementHistory() both self-check and
	// no-op -- so this flag exists ONLY to skip those specific guards (and
	// SendPositionUpdate's send throttle, which is a single process-wide
	// static and would otherwise make test order/timing-dependent) so a
	// test can read back what the outbound C->S coordinate conversion
	// actually computed. Default false; production behavior is bit-for-bit
	// unchanged when unset. Not called from any production path.
	void Test_SetBypassSendGuardForSeam(bool bypass) { m_test_bypass_send_guard = bypass; }

	// T88 test seam: SendPositionUpdate() silently no-ops (by design) when
	// the player's spawn id is still 0 -- normally only ever set from a
	// decoded PlayerProfile/ZoneSpawns entity id. Lets a test drive Move()/
	// MoveToWithPath() without first replaying a full PlayerProfile.
	// Test-only; not called from any production path.
	void Test_SetMySpawnIdForSeam(uint16_t spawn_id) { SetMySpawnId(spawn_id); }
	// T311: player model size, normally set from the self spawn struct. The
	// wire z is 0.625 * size, so a test that does not vary size cannot pin the
	// measured size-linearity.
	void Test_SetPlayerSizeForSeam(float size) { m_game_state.player().setSize(size); }
	// T311: the deferred-conversion flag a self-spawn sets when the model has
	// not loaded yet. A headless test has no renderer to reach the real setter.
	void Test_SetZAwaitingFrameConversionForSeam(bool a) { m_game_state.player().setZAwaitingFrameConversion(a); }
	bool Test_GetZAwaitingFrameConversionForSeam() const { return m_game_state.player().zAwaitingFrameConversion(); }

	// T221 test seam: m_game_state.pet().petSpawnId() is normally set only by pet detection
	// in ZoneSpawns/NewSpawn/ClientUpdate; lets a test drive the MobHealth
	// pet branch directly. Test-only; not called from any production path.
	void Test_SetPetSpawnIdForSeam(uint16_t spawn_id) { m_game_state.pet().setPet(spawn_id, "", 0); }

	// T88 test seam: the last outbound client position update body
	// fields SendPositionUpdate() computed, captured unconditionally right
	// before the (possibly-suppressed) send -- this is the actual
	// production encoder output, not a re-derivation of the formula in the
	// test. Read-only; test-only.
	struct TestSentPositionUpdate {
		float x_pos = 0.0f;
		float y_pos = 0.0f;
		float z_pos = 0.0f;
		uint16_t heading_scaled = 0;
	};
	TestSentPositionUpdate Test_GetLastComputedPositionUpdate() const { return m_test_last_sent_update; }

	// T167 test seam, same shape as the T88 one above: the body
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

	// T217 test seam: the complete OP_GMHideMe packet (opcode + body) the
	// last SendGMHideMe() call built, captured before the connection gate.
	// Raw bytes rather than decoded fields so the test pins the wire layout
	// eqemu's Handle_OP_GMHideMe size-checks, not a re-derivation of it.
	struct TestSentGMHideMe {
		bool sent = false;
		std::vector<uint8_t> bytes;
	};
	const TestSentGMHideMe& Test_GetLastGMHideMe() const { return m_test_last_gm_hideme; }

	// T229 test seam, same shape: the complete OP_GMSummon packet (opcode +
	// 152-byte GMSummon_Struct) the last SendGMSummon() call built, captured
	// before the connection gate. Pins that the body carries the GM's own
	// position in SERVER coordinates as floats -- eqemu's OPGMSummon reads
	// them (MovePC to gms->x/y/z), so the zeros T148 shipped sent the target
	// to the origin.
	struct TestSentGMSummon {
		bool sent = false;
		std::vector<uint8_t> bytes;
	};
	const TestSentGMSummon& Test_GetLastGMSummon() const { return m_test_last_gm_summon; }

	// T170 test seam: the two id fields of the last inspect answer built. They
	// are the whole point -- they mean the OPPOSITE of the request struct's.
	struct TestSentInspectAnswer {
		bool sent = false;
		uint32_t target_id = 0;   // the requester (routing)
		uint32_t playerid = 0;    // the inspected player (us)
	};
	TestSentInspectAnswer Test_GetLastInspectAnswer() const { return m_test_last_inspect_answer; }

	// T231 test seam: every RequestZoneChange() call is counted here BEFORE
	// its live-connection gate, so a headless test can pin whether a server
	// packet made the client decide to reply with OP_ZoneChange at all
	// (same-zone: must not; cross-zone: must). Read-only; test-only.
	struct TestZoneChangeRequest {
		int attempts = 0;
		uint16_t zone_id = 0;
	};
	TestZoneChangeRequest Test_GetLastZoneChangeRequest() const { return m_test_last_zone_change_request; }

	// T268 test seam: every packet QueuePacket() was asked to send (opcode +
	// body bytes), captured before the connection gate so a headless test can
	// assert the exact wire sequence a loot action produces (OP_LootRequest,
	// N x OP_LootItem, OP_EndLootRequest) instead of re-deriving it. Off by
	// default (zero cost in production); Test_SetCaptureQueuedPacketsForSeam
	// (true) turns it on. Read-only; test-only.
	struct TestQueuedPacket {
		uint16_t opcode = 0;
		std::vector<uint8_t> bytes;  // body only (no opcode prefix)
	};
	void Test_SetCaptureQueuedPacketsForSeam(bool capture) { m_test_capture_queued_packets = capture; }
	const std::vector<TestQueuedPacket>& Test_GetQueuedPackets() const { return m_test_queued_packets; }
	void Test_ClearQueuedPackets() { m_test_queued_packets.clear(); }
	// Called from QueuePacket() BEFORE its connection gate; no-op unless armed.
	void Test_RecordQueuedPacket(uint16_t opcode, const void* body, size_t len) {
		if (!m_test_capture_queued_packets) {
			return;
		}
		const auto* d = static_cast<const uint8_t*>(body);
		m_test_queued_packets.push_back(TestQueuedPacket{opcode, std::vector<uint8_t>(d, d + len)});
	}

	// T88 test seam: forces immediate arrival at the current movement
	// target, exactly mirroring UpdateMovement()'s own "distance < 2.0f"
	// arrival branch (eq_movement.cpp: SetPosLocal(targetX, targetY, targetZ)
	// then StopMovement(), which sends the final
	// position) -- without depending on wall-clock-driven incremental
	// movement steps for targets outside that 2-unit snap radius. Test-only.
	void Test_SnapToMovementTargetForSeam();

	// T322 test seams: the zone-transition connection lifecycle. The
	// hand-off itself is gated on a live m_zone_connection (there is nobody
	// to hand off to without one), which no headless test can supply, so
	// Test_BeginGracefulZoneOutForSeam() lifts exactly that one gate and
	// nothing else -- the packets it queues, the flags it arms and the
	// OP_LogoutReply routing that follows are the production code paths.
	// Read-only otherwise; test-only. See tests/test_zone_out_handshake.cpp.
	// force_gate=false leaves the live-connection gate in place, so the
	// no-connection fallback can be pinned too.
	bool Test_BeginGracefulZoneOutForSeam(bool force_gate = true) {
		m_test_force_zone_out_handshake = force_gate;
		const bool armed = BeginGracefulZoneOut();
		m_test_force_zone_out_handshake = false;
		return armed;
	}
	void Test_DisconnectFromZoneForSeam() { DisconnectFromZone(); }
	bool Test_IsZoneOutHandshakePending() const { return m_zone_out_handshake_pending; }
	bool Test_IsZoneOutAcknowledged() const { return m_zone_out_acknowledged; }
	bool Test_IsPendingLogoutDisconnect() const { return m_pending_logout_disconnect; }
	bool Test_IsZoneToZoneInProgress() const { return m_zone_to_zone_in_progress; }
	void Test_SetZoneToZoneInProgressForSeam(bool v) { m_zone_to_zone_in_progress = v; }

	static void SetDebugLevel(int level) { s_debug_level = level; ::SetDebugLevel(level); }
	static int GetDebugLevel() { return s_debug_level; }

	// Public chat interface. K03 (Batch K) moved everything else here into
	// eqt::chat::ChatComponent; these three are output primitives with 30+
	// callers across every family and stay put until K-final.
	void AddChatSystemMessage(const std::string &text);  // Add system message to chat window
	void AddChatCombatMessage(const std::string &text, bool is_self = false);  // Add combat message (is_self for player damage)
	void AddChatMissMessage(const std::string &text);  // Add miss message (filterable separately)

	// T143: read-only access to registry 1's catalog (name/alias/category
	// introspection only -- NOT a way to execute commands; use
	// DispatchSlashCommand() for that). Added so Application, which is the
	// only object that also holds registry 2 (action::CommandProcessor),
	// can build a merged /help view across both. See
	// refactor/T143_registry_aware_help.md.
	const eqt::ui::CommandRegistry* GetCommandRegistry() const { return m_command_registry.get(); }

	// Public movement interface
	void Move(float x, float y, float z);
	void MoveToEntity(const std::string &name);
	void MoveToEntityWithinRange(const std::string &name, float stop_distance);
	void StartCombatMovement(const std::string &name, float stop_distance);
	void StartCombatMovement(uint16_t entity_id);
	void SetCombatStopDistance(float distance);  // Phase 7.6: also syncs to GameState
	void UpdateCombatMovement();
	void Follow(const std::string &name);
	void StopFollow();
	void Face(float x, float y, float z);
	void FaceEntity(const std::string &name);
	void SetHeading(float heading);
	void SetPosition(float x, float y, float z);
	void SetMoving(bool moving);
	void UpdateMovement();
	void SendPositionUpdate();
	glm::vec3 GetPosition() const;
	// T246: the zone's collision map (null until a zone's .map is loaded);
	// game-thread readers (SpellManager::checkLineOfSight) take a copy of
	// the shared_ptr so a zone change cannot free it mid-query (P35).
	std::shared_ptr<const HCMap> GetZoneMap() const { return m_zone_map; }
	float GetSize() const { return m_game_state.player().size(); }  // player model size (6 until the spawn says otherwise)
	// T311: the renderer-measured origin-to-feet distance of OUR model; 0 until
	// the model loads. Only for render-frame <-> wire-frame conversion --
	// include/client/position_frames.h.
	float GetPlayerCollisionZOffset() const { return m_game_state.player().collisionZOffset(); }
	// T247: true while an instantaneous OP_Stun (eqemu Stun_Struct, duration
	// in ms; 0 = UnStun) is in force. Buff-borne stuns are BuffManager's.
	bool IsStunned() const;
	float GetHeading() const;
	bool IsMoving() const;
	bool IsFullyZonedIn() const { return m_game_state.world().isZoneConnected() && m_game_state.world().isClientReady() && m_update_running; }
	bool IsZoneChangeApproved() const { return m_game_state.world().zoneChangeApproved(); }
	void SetZoningEnabled(bool enabled) { m_zoning_enabled = enabled; }
	bool IsZoningEnabled() const { return m_zoning_enabled; }

	// Loading phase tracking (covers entire pre-gameplay process)
	LoadingPhase GetLoadingPhase() const { return m_loading_phase; }
	void SetLoadingPhase(LoadingPhase phase, const char* statusText = nullptr);
	float GetLoadingProgress() const;
	const char* GetLoadingStatusText() const;
	bool IsGameStateReady() const { return static_cast<int>(m_loading_phase) >= static_cast<int>(LoadingPhase::ZONE_AWAITING_CONFIRM); }
	bool IsGraphicsReady() const { return m_loading_phase == LoadingPhase::COMPLETE; }
	void OnGameStateComplete();  // Called when game state setup finishes, triggers graphics loading
	void OnGraphicsComplete();   // Called when graphics loading finishes, enables gameplay
	void ListEntities(const std::string& search = "") const;
	void DumpEntityAppearance(uint16_t spawn_id) const;
	void DumpEntityAppearance(const std::string& name) const;
	void SetPathfinding(bool enabled) { m_use_pathfinding = enabled; }
	bool IsPathfindingEnabled() const { return m_use_pathfinding; }
	bool HasReachedDestination() const;
	void SetMoveSpeed(float speed);
	void SetNavmeshPath(const std::string& path) { m_navmesh_path = path; }
	void SetMapsPath(const std::string& path) { m_maps_path = path; }

	// R01: Session recorder — records decoded application packets to `path`
	// for later replay (--record-session CLI flag). No-op hooks elsewhere
	// check m_session_recorder before recording.
	void StartSessionRecording(const std::string& path);

	// Combat and entity access
	CombatManager* GetCombatManager() { return m_combat_manager.get(); }
	TradeManager* GetTradeManager() { return m_trade_manager.get(); }
	EQ::SpellManager* GetSpellManager() { return m_spell_manager.get(); }
	EQ::BuffManager* GetBuffManager() { return m_buff_manager.get(); }
	EQ::SkillManager* GetSkillManager() { return m_skill_manager.get(); }
	// T71: needed for /equip's local-state sync fix to be testable without a
	// live server (getItem()/hasCursorItem() must see seeded test items) --
	// same accessor-getter pattern as the managers above, not a new seam.
	eqt::inventory::InventoryManager* GetInventoryManager() { return m_inventory_manager.get(); }

	const std::map<uint16_t, Entity>& GetEntities() const { return m_entities; }
	uint16_t GetEntityID() const { return MySpawnId(); }

	// D20c4: Quit signaling — Application checks this flag
	bool IsQuitRequested() const { return m_quit_requested; }
	void RequestQuit() { m_quit_requested = true; }
	const std::string& GetLastTellSender() const { return m_last_tell_sender; }
	void QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet);

	// Door interaction
	void SendClickDoor(uint8_t door_id, uint32_t item_id = 0);

	// World object / Tradeskill interaction
	void SendClickObject(uint32_t drop_id);
	void SendTradeSkillCombine(int16_t container_slot);
	void SendCloseContainer(uint32_t drop_id);

	// Skill-specific packet sends
	void SendApplyPoison(uint32_t inventory_slot);
	const std::map<uint32_t, eqt::WorldObject>& GetWorldObjects() const { return m_world_objects; }
	const eqt::WorldObject* GetWorldObject(uint32_t drop_id) const;
	uint32_t GetActiveTradeskillObjectId() const { return m_active_tradeskill_object_id; }
	void ClearWorldObjects();

	// Group queries (Phase 7.5: read from GameState)
	bool IsInGroup() const;
	bool IsGroupLeader() const;
	int GetGroupMemberCount() const;
	const GroupMember* GetGroupMember(int index) const;
	const std::string& GetGroupLeaderName() const;
	const std::string& GetMyName() const { return m_character; }
	const std::string& GetMyLastName() const { return m_game_state.player().lastName(); }

	// Read-only observers of login/world state (T09). Trivial inline
	// returns of existing members -- no new storage, no logic.
	uint32_t GetAccountId() const { return m_dbid; }
	const std::string& GetSessionKey() const { return m_key; }
	bool IsWorldReady() const { return m_world_ready; }
	bool HasSentEnterWorld() const { return m_enter_world_sent; }
	const std::string& GetZoneServerHost() const { return m_zone_server_host; }
	uint16_t GetZoneServerPort() const { return m_zone_server_port; }
	const std::map<uint32_t, WorldServer>& GetWorldServers() const { return m_world_servers; }
	int GetCharacterSelectIndex() const { return m_character_select_index; }
	// T14: unlike the T09 observers above, this one is backed by new
	// storage — occupied slots only, rebuilt on every OP_SendCharInfo.
	const std::vector<CharacterSelectEntry>& GetCharacterSelectList() const { return m_character_select_list; }
	const std::string& GetChatServerHost() const { return m_ucs_host; }
	uint16_t GetChatServerPort() const { return m_ucs_port; }
	const std::string& GetMailKey() const { return m_mail_key; }
	char GetChatConnectionType() const { return m_ucs_connection_type; }

	// Group actions (send packets to server)
	void SendGroupInvite(const std::string& target_name);
	void SendGroupFollow(const std::string& inviter_name);  // Accept invite
	void SendGroupDecline(const std::string& inviter_name); // Decline invite
	void SendGroupDisband();
	void SendLeaveGroup();

	// Pending group invite handling (Phase 7.5: read from GameState)
	bool HasPendingGroupInvite() const;
	const std::string& GetPendingInviterName() const;
	void AcceptGroupInvite();
	void DeclineGroupInvite();

	// Raid actions

	// Pet queries
	// T40: GetPetHpPercent()/GetPetButtonState() removed -- their only
	// caller was Application::syncPetState(), which is now replaced by
	// event-fed pet state via GameStateSyncBridge (PetStatsChanged /
	// PetButtonStateChanged, both published in eq_bridge.cpp /
	// eq_entities.cpp). GetPetSpawnId()/GetPetName()/GetPetLevel()/
	// GetPetEntity() stay -- they still have live callers outside the
	// deleted poll (eq_action_handler.cpp, spell_manager.cpp, eq.cpp).
	bool HasPet() const { return m_game_state.pet().petSpawnId() != 0; }
	uint16_t GetPetSpawnId() const { return m_game_state.pet().petSpawnId(); }
	const Entity* GetPetEntity() const;
	std::string GetPetName() const;
	uint8_t GetPetLevel() const;

	// Pet commands
	void SendPetCommand(EQT::PetCommand command, uint16_t target_id = 0);
	void DismissPet();

	// Movement state and behavior methods
	void SetMovementMode(MovementMode mode);
	void SetPositionState(PositionState state);
	void SendSpawnAppearance(uint16_t type, uint32_t value);
	void SendAnimation(uint8_t animation_id, uint8_t animation_speed = 10);
	void SendJump();
	void SendMovementHistory();
	void Jump();
	void UpdateJump();

	// Swimming methods
	void UpdateWaterState();           // Check BSP region and update water state
	void OnEnterWater();               // Called when entering water
	void OnExitWater();                // Called when exiting water
	float GetSwimSpeed() const;        // Get swim speed based on skill
	WaterState GetWaterState() const { return m_water_state; }
	bool IsSwimming() const { return m_water_state != WaterState::NotInWater; }
	void SetSwimUp(bool up) { m_swim_up = up; }
	void SetSwimDown(bool down) { m_swim_down = down; }

	void StartUpdateLoop();
	void StopUpdateLoop();
	void PerformEmote(uint32_t animation);
	void SetAFK(bool afk);
	void SetAnonymous(bool anon);
	void SetRoleplay(bool rp);
	void StartCampTimer();
	void CancelCamp();
	void UpdateCampTimer();
	bool IsCamping() const;  // Phase 7.8: reads from GameState
	void SendCamp();         // Send OP_Camp to server
	void SendLogout();       // Send OP_Logout to server
	void SendRezzAnswer(bool accept);  // Send OP_RezzAnswer (accept/decline)
	bool HasPendingRezz() const { return m_has_pending_rezz; }
	void SendWhoAllRequest(const std::string& name = "", int lvllow = -1, int lvlhigh = -1,
		int race = -1, int class_ = -1, bool gm = false);  // Send /who request
	void SendInspectRequest(uint32_t target_id);  // Send /inspect request
	// T170: answers an inspect request aimed at us. A Titanium client MUST
	// do this -- eqemu only builds the answer itself for SoF+ clients and
	// bots. Called from ZoneProcessInspectRequest, never by a command.
	// Read the hazard note on the definition before touching it: the
	// answer's text field overwrites the character's stored inspect
	// message on the server.
	void SendInspectAnswer(uint32_t requester_id);

	// T171: the inspect message this client sends when answering an inspect.
	// Truncated to EQT::INSPECT_MESSAGE_MAX_LEN on set -- the server stores
	// only 256 bytes and a longer value would be stored unterminated.
	//
	// Setting it does NOT push anything to the server: there is no opcode for
	// that on Titanium (OP_InspectMessageUpdate is SoF+ and is not even mapped
	// in patch_Titanium.conf). It reaches the server the next time somebody
	// inspects us, which is also the moment it replaces whatever the server
	// held -- the same last-writer-wins the real client has.
	const std::string& GetInspectMessage() const { return m_inspect_message; }
	void SetInspectMessage(const std::string& message);
	// K01 (Batch K): the guild family is eqt::guild::GuildComponent; callers go
	// through Guild(), there are no forwarders. HasPendingGuildInvite() had
	// zero callers tree-wide and was deleted rather than moved (§Mechanism 5).
	eqt::guild::GuildComponent& Guild() { return *m_guild; }
	// K03 (Batch K): the chat family is eqt::chat::ChatComponent; callers go
	// through Chat(), there are no forwarders.
	eqt::chat::ChatComponent& Chat() { return *m_chat; }
	eqt::ClientContext& Context() { return m_ctx; }
	// Phase 3: Corpse Management
	void SendCorpseDrag(const std::string& corpse_name);
	void SendCorpseDrop();
	void SendConsiderCorpse(uint32_t corpse_id);
	bool IsDraggingCorpse() const { return m_is_dragging_corpse; }
	// Phase 3: Consent System
	void SendConsent(const std::string& player_name);
	void SendConsentDeny(const std::string& player_name);
	// Phase 3: Combat Targeting
	void SendAssist(uint32_t target_id);
	void SendAssistGroup(uint32_t target_id);
	// Phase 3: Travel System
	// Take (true) or release (false) control of the boat we are on.
	// OP_ControlBoat carries no heading and no control type -- see T145.
	// Phase 3: Group Split
	void SendSplit(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper);
	// Phase 3: LFG System
	void SendLFGCommand(bool lfg_on);
	bool IsLFG() const { return m_is_lfg; }
	// Phase 3: Combat Abilities
	void SendShielding(uint32_t target_id);
	// Phase 3: Misc
	void SendSave();
	// Phase 4: Dueling
	void SendDuelRequest(uint32_t target_id);
	void SendDuelAccept(uint32_t target_id);
	void SendDuelDecline(uint32_t target_id);
	bool IsInDuel() const { return m_is_dueling; }
	bool HasPendingDuel() const { return m_has_pending_duel; }
	// T102: accessor for EqActionHandler::duelAccept()/duelDecline() -- the
	// duel_cmd/duelaccept_cmd/dueldecline_cmd handlers being migrated read
	// m_duel_initiator_id directly (same TU); the action layer can't (no
	// friendship), so it needs a plain public getter, same shape as
	// GetCombatManager()/GetTradeManager() above.
	uint32_t GetPendingDuelInitiatorId() const { return m_duel_initiator_id; }
	// Phase 4: Skills
	void SendBindWound(uint32_t target_id);
	void SendTrackTarget(uint32_t target_id);

	// T179: raid commands. Sends OP_RaidCommand (Titanium OP_RaidInvite, 0x5891).
	void SendRaidCommand(uint32_t action, const std::string& player_name,
	                     const std::string& leader_name, uint32_t parameter = 0);
	// Raid state, maintained from the decoded OP_RaidUpdate stream.
	bool IsInRaid() const { return m_in_raid; }
	const std::string& GetRaidLeader() const { return m_raid_leader; }
	const std::string& GetPendingRaidInviter() const { return m_pending_raid_inviter; }
	void ClearPendingRaidInvite() { m_pending_raid_inviter.clear(); }
	// T180: the member list, assembled from the action-0 stream -- Titanium's
	// ENCODE drops eqemu's roster packet, so this is the only roster there is.
	const std::vector<EQT::RaidMemberInfo>& GetRaidMembers() const {
		return m_raid_roster.Members();
	}
	// T182: raid lock state, from actions 17/18. Re-established on zone-in --
	// eqemu's rejoin path sends SendRaidLockTo only when the raid is locked.
	bool IsRaidLocked() const { return m_raid_locked; }
	// Phase 4: Tradeskills
	void SendRecipesFavorite(uint32_t object_type, const std::vector<uint32_t>& favorites);
	void SendRecipesSearch(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial);
	void SendRecipeDetails(uint32_t recipe_id);
	void SendRecipeAutoCombine(uint32_t object_type, uint32_t recipe_id);
	// Phase 4: Cosmetic
	void SendSurname(const std::string& surname);
	void SendFaceChange(uint8_t haircolor, uint8_t beardcolor, uint8_t eyecolor1, uint8_t eyecolor2,
		uint8_t hairstyle, uint8_t beard, uint8_t face);
	// Phase 4: Misc
	void SendRandom(uint32_t low, uint32_t high);
	void SendFindPerson(uint32_t npc_id);
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
	// Phase 4: GM Commands
	void SendGMZoneRequest(const std::string& charname, uint16_t zone_id);
	void SendGMSummon(const std::string& charname);
	void SendGMGoto(const std::string& charname);
	void SendGMFind(const std::string& charname);
	void SendGMKick(const std::string& charname);
	void SendGMKill(const std::string& charname);
	void SendGMHideMe(bool hide);
	void SendGMEmoteZone(const std::string& text);
	void SendGMLastName(const std::string& charname, const std::string& lastname);
	// Phase 4: Petitions
	void SendPetition(const std::string& text);
	void SetSneak(bool sneak);
	float GetMovementSpeed() const;

	// State getters (Phase 7.8: read from GameState)
	bool IsAFK() const;
	bool IsAnonymous() const;
	bool IsRoleplay() const;

	// Additional public methods
	uint16_t GetMySpawnID() const { return MySpawnId(); }
	uint16_t GetCurrentTargetId() const { return m_game_state.combat().targetId(); }

	// T133: populated from the player's own PlayerProfile packet
	// (ZoneProcessPlayerProfile, eq_zone_entry.cpp). 0 means "not in a guild" -- every
	// SendGuild*() sender in eq_guild.cpp guards on that, correctly.
	uint32_t GetGuildID() const { return m_guild_id; }

	// T317: the player's own guild rank, in the TITANIUM encoding the wire and
	// the profile use -- 0 = member, 1 = officer, 2 = leader, 9
	// (GUILD_RANK_NONE_TI) = none/unknown. Parsed from PlayerProfile offset
	// 13055, one byte after `gm` at 13054, in ZoneProcessPlayerProfile: the
	// same once-per-login shape as m_guild_id and m_gm_mode. The server folds
	// its own 0..8 rank scale into these three values in
	// common/patches/titanium.cpp:1494 -- a Titanium client cannot see any
	// finer rank than that. See client/guild_packet_bodies.h for what the
	// guild opcodes actually do with a rank byte.
	uint8_t GetGuildRank() const { return m_guild_rank; }

	// T148: "GM mode was ON at login", NOT "this account has GM privileges".
	//
	// Populated once, from PlayerProfile.gm at offset 13054, in
	// ZoneProcessPlayerProfile (eq_zone_entry.cpp) -- the SAME parse site and the same
	// once-per-login shape as m_guild_id above. Its only server-side writer
	// is Client::SetGM(bool) (eqemu zone/client.cpp:2659), i.e. the `#gm
	// on|off` toggle, which Save()s so the value persists in the character
	// profile.
	//
	// Account status -- what actually authorises GM commands SERVER-side, via
	// Client::Admin() -- never reaches the client. There is no account-status
	// field anywhere in the Titanium protocol. So this flag is the only
	// GM-shaped signal a Titanium client can have, and it is a mode, not a
	// permission. Name any user-facing string accordingly.
	//
	// DELIBERATELY STALE, by Will's decision of 2026-08-19: the check happens
	// ONCE AT LOGIN. A GM who toggles `#gm` mid-session must log out and back
	// in for the client to notice. The AT_GM live-update path (an AT_GM case
	// in ZoneProcessSpawnAppearance, mirroring what T133 did for AT_GUILD_ID)
	// was considered and EXPLICITLY DECLINED -- do not "fix" this by adding
	// one. See refactor/T148_gm_status_gate.md.
	//
	// Offset 13054 is not taken on faith: eqemu's own header comments it
	// `// 0=no, 1=yes (guessing!)`. It was captured off the wire across three
	// logins with the flag toggled between them, with the neighbouring
	// pvp@13052/anon@13053 bytes and birthday@13040 used as controls --
	// review/T148/playerprofile_gm_offset_annotated.txt.
	bool GetGMMode() const { return m_gm_mode; }

	// T167: the player's own appearance, from the SAME parse site as
	// m_guild_id and m_gm_mode above. CHECK .valid BEFORE USE -- zero is a
	// legal value for every field, so it is the only thing separating an
	// unknown appearance from a real all-zero one.
	//
	// Unlike GetGMMode() this is NOT deliberately stale: it is re-set after a
	// /appearance send, because eqemu applies the whole body and Save()s, so
	// what we sent IS what the profile holds. See eq_profile_appearance.h.
	const EQT::ProfileAppearance& GetProfileAppearance() const { return m_profile_appearance; }
	void SetProfileAppearance(const EQT::ProfileAppearance& appearance) { m_profile_appearance = appearance; }

	// Game state access (new state management system)
	eqt::state::GameState& GetGameState() { return m_game_state; }
	const eqt::state::GameState& GetGameState() const { return m_game_state; }

	// Bridge for cross-thread game state ↔ renderer communication
	// T46: moved out of the header (was an inline one-liner setting only
	// m_bridge) so it can also wire the four managers' own bridge pointers
	// via wireBridgeToSubsystems() -- previously that wiring happened only
	// in InitGraphics(), so headless runs (which call only setBridge()) left
	// InventoryManager/SpellManager/SkillManager/HotbarModel's bridge
	// pointers null for the whole process. See wireBridgeToSubsystems().
	void setBridge(eqt::bridge::GameStateBridge* bridge);
	void ProcessBridgeIntents();  // Drain and process intents from the renderer (D14+)
	void ProcessBridgeEvents();   // Drain and apply events to the renderer (D14+)

	// Keyboard control methods
	void StartMoveForward();
	void StartMoveBackward();
	void StartTurnLeft();
	void StartTurnRight();
	void StopMoveForward();
	void StopMoveBackward();
	void StopTurnLeft();
	void StopTurnRight();
	void UpdateKeyboardMovement();

	// Character stat getters (Phase 7.2 - read from GameState)
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

	// Keyboard state

	// Friend classes for system access
	friend class CombatManager;
	friend class TradeManager;
	friend class eqt::guild::GuildComponent;  // K01 transition; K-final removes it
	friend class eqt::chat::ChatComponent;    // K03 transition; K-final removes it

	// String database for EQ message lookups (works in all modes)
	bool LoadStringFiles(const std::string& eqClientPath);
	const EQT::StringDatabase& GetStringDatabase() const { return m_string_db; }

	// S04: Pre-allocate spell system at startup (spell DB, buff manager, spell effects, spell type processor)
	// Must be called after SetEQClientPath(). Returns false if spell DB fails to load.
	bool InitializeSpellSystem(const std::string& eqClientPath);

	// Open the WilleqDatabase (SQLite) and set as global instance.
	// Not FATAL if missing — falls back to legacy file loading.
	void OpenDatabase(const std::string& dbPath);

	// P28: game-thread tick, config-path accessors, hotbar persistence, and
	// state-publishing are plain game logic — called unconditionally from
	// Application::updateGameState()/OnGameStateComplete() et al regardless
	// of EQT_HAS_GRAPHICS. Un-guarded (55b101e had folded these into the
	// graphics block along with the genuinely renderer-only methods below).
	// D21a: Split tick — game thread vs render thread
	void GameTick(float deltaTime);        // Game thread: spell/buff updates + intent processing
	// (PreRenderTick -- the render-thread event drain -- was deleted in T220;
	// Application drives GameStateSyncBridge's two-stage drain directly.)
	void PostRenderTick(float deltaTime);  // Game thread: audio sync
	void SetEQClientPath(const std::string& path);
	void SetRegionMapsPath(const std::string& path) { m_region_maps_path = path; }
	const std::string& GetEQClientPath() const { return m_eq_client_path; }
	void SetConfigPath(const std::string& path) { m_config_path = path; }
	const std::string& GetConfigPath() const { return m_config_path; }
	void SaveHotbarConfig();  // Save hotbar assignments to config file
	void LoadHotbarConfig();  // Load hotbar assignments from config file
	void SaveInspectMessage();  // T171: persist the inspect message
	void LoadInspectMessage();  // T171: restore it at startup
	void PublishFullStateSnapshot(); // U00: Push all current state through bridge for new UI
	// U07b: Standalone hotbar model
	eqt::HotbarModel* GetHotbarModel() { return m_hotbar_model.get(); }
	// Phase 7.3: Zone accessors read from GameState
	const std::string& GetCurrentZoneName() const { return m_game_state.world().zoneName(); }
	void GetTimeOfDay(uint8_t& hour, uint8_t& minute) const { hour = m_game_state.world().timeHour(); minute = m_game_state.world().timeMinute(); }
	void OnGraphicsMovement(const eqt::events::PlayerPositionChanged& update);  // S2: takes the movement intent directly (name predates this being a plain bridge-intent handler)
	void UpdateInventoryStats();  // Update inventory window with current stats (base + equipment)

#ifdef EQT_HAS_GRAPHICS
	// Graphics renderer methods — genuinely require the renderer/loading-thread
	// lifecycle and stay gated.
	bool InitGraphics(int width, int height,
	                  EQT::Graphics::IrrlichtRenderer* renderer,
	                  eqt::bridge::GameStateBridge* bridge);
	void ShutdownGraphics();
	eqt::ZoneLoadSnapshot CreateZoneLoadSnapshot() const;  // D20c3: snapshot for loading thread
	void SetConstrainedPreset(EQT::Graphics::ConstrainedRenderingPreset preset) { m_constrained_preset = preset; }
	EQT::Graphics::ConstrainedRenderingPreset GetConstrainedPreset() const { return m_constrained_preset; }
	void SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config);
	std::optional<EQT::Graphics::ConstrainedRendererConfig> GetConstrainedConfig() const { return m_constrained_config; }
	// D20e3: GetRenderer() removed — EverQuest no longer holds a renderer pointer
	void SetPlayerGraphicsEntityPending(bool v) { m_player_graphics_entity_pending = v; }
	// D20e2: Application owns loading status; EverQuest signals it via this pointer
	void SetLoadingStatus(EQT::Graphics::LoadingStatus* status) { m_loading_status_ptr = status; }
	// D20e2: Flag for Application to detect re-zone loading requests
	bool ConsumeZoneLoadRequest() { bool v = m_zone_load_requested; m_zone_load_requested = false; return v; }
	// U07c3: SetWindowManager removed — old UI deleted
	// D20g: SetRDPServer removed — RDP audio setup moved to Application
	// D20f2: Typed BSP tree for water detection / zone lines
	// P17/T260: installs the renderer's tree from a completed zone load.
	// builtForZone is the zone that load was FOR (Application passes the
	// consumed snapshot's zoneName). T260: a tree built for any zone other
	// than the current one is REJECTED, not installed -- that is the
	// graphicsCompleteReady finalization of the previous load landing after
	// a fast re-zone's NewZone (P15's "SIZE_MAX with zone=<source>" FATAL was
	// the previous zone's tree paired with the new zone's position). The
	// tree LoadZoneSources() built for the current zone stays in place.
	// P17 tagged with the zone name at call time, which is exactly
	// the value that is wrong in that race.
	void SetZoneBspTree(std::shared_ptr<EQT::Graphics::BspTree> bsp, const std::string& builtForZone);
	// D20e2: LoadZoneGraphics moved to Application
#endif

private:
	// Game state (new state management system)
	// This contains the central game state that can be shared across modes and renderers.
	// During the transition period, both m_game_state and the legacy member variables
	// exist. New code should use m_game_state; existing code is gradually migrated.
	eqt::state::GameState m_game_state;

	// Bridge for cross-thread game state → renderer communication (Phase 2+)
	eqt::bridge::GameStateBridge* m_bridge = nullptr;

	// T46: assigns m_bridge and propagates the same pointer (including
	// null) to the four managers' own bridge pointers
	// (InventoryManager/SpellManager/SkillManager/HotbarModel), each of
	// which is a separate member from m_bridge. Called from both
	// setBridge() (the headless path) and InitGraphics() (the graphics
	// path), so the managers get wired whichever path sets the bridge.
	// Plain pointer assignment underneath, so calling it twice with the
	// same pointer (graphics mode: InitGraphics() after Application's own
	// setBridge()-equivalent construction) is a harmless no-op.
	void wireBridgeToSubsystems(eqt::bridge::GameStateBridge* bridge);

	// D20f2: Typed BSP tree for water detection, zone lines, collision
	std::shared_ptr<EQT::Graphics::BspTree> m_zone_bsp_tree;
	// P17: zone name m_zone_bsp_tree was built for (set by LoadZoneSources()
	// and SetZoneBspTree()). Empty whenever m_zone_bsp_tree is null. Used by
	// CreateZoneLoadSnapshot() to detect a stale tree that outlived a
	// CleanupZone() reset. T260: LoadZoneSources() did not set it, so every
	// zone-in since P17 skipped the region lookup with a spurious "stale"
	// WARN (captured for '') -- see refactor/T260_bsp_region_size_max.md.
	std::string m_zone_bsp_tree_zone_name;

	// Zone S3D/WLD sources — opened/parsed once at game state layer, shared with sequential loader
	std::shared_ptr<EQT::Graphics::PfsArchive> m_zone_archive;
	std::shared_ptr<EQT::Graphics::WldLoader> m_zone_wld;

	// Utility functions
	static void DumpPacket(const std::string &prefix, uint16_t opcode, const EQ::Net::Packet &p);
	static void DumpPacket(const std::string &prefix, uint16_t opcode, const uint8_t *data, size_t size);
	static std::string GetOpcodeName(uint16_t opcode);
public:
	// T244: public because EqActionHandler (not a friend) renders cast
	// refusals from official eqstr ids -- see client/spell/cast_refusal.h.
	std::string GetStringMessage(uint32_t string_id);
	// T251: public for the same reason -- castRefusalMessage() substitutes
	// the missing component's name into 433 "You are missing %1.".
	std::string GetFormattedStringMessage(uint32_t string_id, const std::vector<std::string>& args);
private:
	static std::string GetChatTypeName(uint32_t chat_type);
public:
	// T180: public because /raid list renders class names from EqActionHandler,
	// which is not a friend. A pure lookup table over a wire enum -- no state,
	// nothing to encapsulate. The alternative was pulling the graphics-side
	// EQT::Graphics::getClassName into a non-graphics TU, i.e. a new dependency
	// direction for a switch statement.
	static std::string GetClassName(uint32_t class_id);
	// T271: public for the same reason as GetClassName above -- it is a pure
	// race_models lookup with no state, and tests must be able to hold it to
	// the audited rows (it used to carry a shifted hardcoded table that was
	// printed to the player).
	static std::string GetRaceName(uint32_t race_id);
private:
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

	static std::string GetDeityName(uint32_t deity_id);
	static std::string GetBodyTypeName(uint8_t bodytype);
	static std::string GetEquipSlotName(int slot);
	static std::string GetNpcTypeName(uint8_t npc_type);

	// Phase 7.4: Entity sync helpers
	void SyncEntityToGameState(const Entity& entity);
	void RemoveEntityFromGameState(uint16_t spawnId);

	// Phase 7.5: Group sync helper

	// T221: source-side twins of the two transforming READ-model projections
	// (EntityAppearanceChanged partial merge; InventorySlotChanged occupancy).
	void ApplyAppearanceToGameState(const eqt::state::EntityAppearanceChangedData& data);
	void SyncInventorySlotToGameState(int16_t slot, bool hasItem);

	// Login
	void LoginOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void LoginOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void LoginOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by LoginOnPacketRecv and the ReplayLoginPacket test seam.
	void DispatchLoginPacket(const EQ::Net::Packet &p);

	void LoginSendSessionReady();
	void LoginSendLogin();
	void LoginSendServerRequest();
	void LoginSendPlayRequest(uint32_t id);
	void LoginProcessLoginResponse(const EQ::Net::Packet &p);
	void LoginProcessServerPacketList(const EQ::Net::Packet &p);
	void LoginProcessServerPlayResponse(const EQ::Net::Packet &p);

	void LoginDisableReconnect();

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_login_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_login_connection;
	std::map<uint32_t, WorldServer> m_world_servers;

	// World
	void ConnectToWorld(const std::string& world_address);

	void WorldOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void WorldOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void WorldOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void WorldOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by WorldOnPacketRecv and the ReplayWorldPacket test seam.
	void DispatchWorldPacket(const EQ::Net::Packet &p);

	void WorldSendSessionReady();
	void WorldSendClientAuth();
	void WorldSendEnterWorld(const std::string &character);
	void WorldSendApproveWorld();
	void WorldSendWorldClientCRC();
	void WorldSendWorldClientReady();
	void WorldSendWorldComplete();

	void WorldProcessCharacterSelect(const EQ::Net::Packet &p);
	void WorldProcessGuildsList(const EQ::Net::Packet &p);
	void WorldProcessLogServer(const EQ::Net::Packet &p);
	void WorldProcessApproveWorld(const EQ::Net::Packet &p);
	void WorldProcessEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessPostEnterWorld(const EQ::Net::Packet &p);
	void WorldProcessExpansionInfo(const EQ::Net::Packet &p);
	void WorldProcessMOTD(const EQ::Net::Packet &p);
	void WorldProcessSetChatServer(const EQ::Net::Packet &p);
	void WorldProcessZoneServerInfo(const EQ::Net::Packet &p);

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_world_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_world_connection;

	// Variables
	std::string m_host;
	int m_port;
	std::string m_user;
	std::string m_pass;
	std::string m_server;
	std::string m_character;

	// T179/T180: raid state, derived from ZoneProcessRaidUpdate.
	bool m_in_raid = false;
	std::string m_raid_leader;
	std::string m_pending_raid_inviter;
	EQT::RaidRoster m_raid_roster;
	bool m_raid_locked = false;

	std::string m_key;
	uint32_t m_dbid;

	// Debug level
	static int s_debug_level;

	// Sequence numbers for packets
	uint32_t m_login_sequence = 2;

	// World server state
	bool m_world_ready = false;
	bool m_enter_world_sent = false;
	std::string m_zone_server_host;
	uint16_t m_zone_server_port = 0;

	// World server state for reconnection
	std::string m_world_server_host;

	// Zone server connection
	void ConnectToZone();
	void ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void ZoneOnStatusChangeReconnectDisabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);
	// Opcode switch shared by ZoneOnPacketRecv and the ReplayZonePacket test seam.
	void DispatchZonePacket(const EQ::Net::Packet &p);

	void ZoneSendStreamIdentify();
	void ZoneSendAckPacket();
	void ZoneSendSessionReady();
	void ZoneSendZoneEntry();
	void ZoneSendReqNewZone();
	void ZoneSendSendAATable();
	void ZoneSendUpdateAA();
	void ZoneSendSendTributes();
	void ZoneSendRequestGuildTributes();
	void ZoneSendSpawnAppearance();
	void ZoneSendReqClientSpawn();
	void ZoneSendSendExpZonein();
	void ZoneSendSetServerFilter();
	void ZoneSendClientReady();

	void ZoneProcessNewZone(const EQ::Net::Packet &p);
	void ZoneProcessPlayerProfile(const EQ::Net::Packet &p);
	void ZoneProcessCharInventory(const EQ::Net::Packet &p);
	void ZoneProcessZoneSpawns(const EQ::Net::Packet &p);
	void ZoneProcessTimeOfDay(const EQ::Net::Packet &p);
	void ZoneProcessTributeUpdate(const EQ::Net::Packet &p);
	void ZoneProcessTributeTimer(const EQ::Net::Packet &p);
	void ZoneProcessWeather(const EQ::Net::Packet &p);
	void ZoneProcessSendAATable(const EQ::Net::Packet &p);
	void ZoneProcessRespondAA(const EQ::Net::Packet &p);
	void ZoneProcessTributeInfo(const EQ::Net::Packet &p);
	void ZoneProcessSendGuildTributes(const EQ::Net::Packet &p);
	void ZoneProcessSpawnDoor(const EQ::Net::Packet &p);
	void ZoneProcessGroundSpawn(const EQ::Net::Packet &p);
	void ZoneProcessClickObjectAction(const EQ::Net::Packet &p);
	void ZoneProcessTradeSkillCombine(const EQ::Net::Packet &p);
	void ZoneProcessApplyPoison(const EQ::Net::Packet &p);
	void ZoneProcessTrack(const EQ::Net::Packet &p);
	void ZoneProcessBindWound(const EQ::Net::Packet &p);
	void ZoneProcessSendZonepoints(const EQ::Net::Packet &p);
	void ZoneProcessSendAAStats(const EQ::Net::Packet &p);
	void ZoneProcessSendExpZonein(const EQ::Net::Packet &p);
	void ZoneProcessWorldObjectsSent(const EQ::Net::Packet &p);
	void ZoneProcessSpawnAppearance(const EQ::Net::Packet &p);
	void ZoneProcessAnimation(const EQ::Net::Packet &p);  // OP_Animation (T243 rename)
	void ZoneProcessExpUpdate(const EQ::Net::Packet &p);
	void ZoneProcessRaidUpdate(const EQ::Net::Packet &p);
	void ZoneProcessGuildMOTD(const EQ::Net::Packet &p);
	void ZoneProcessNewSpawn(const EQ::Net::Packet &p);
	void ZoneProcessClientUpdate(const EQ::Net::Packet &p);
	void ZoneProcessDeleteSpawn(const EQ::Net::Packet &p);
	void ZoneProcessMobHealth(const EQ::Net::Packet &p);
	void ZoneProcessHPUpdate(const EQ::Net::Packet &p);
	void ZoneProcessWearChange(const EQ::Net::Packet &p);
	void UpdatePlayerAppearanceFromInventory();
	void ZoneProcessIllusion(const EQ::Net::Packet &p);
	void ZoneProcessMoveDoor(const EQ::Net::Packet &p);
	void ZoneProcessCompletedTasks(const EQ::Net::Packet &p);
	void ZoneProcessDzCompass(const EQ::Net::Packet &p);
	void ZoneProcessDzExpeditionLockoutTimers(const EQ::Net::Packet &p);
	void ZoneProcessBeginCast(const EQ::Net::Packet &p);
	void ZoneProcessManaChange(const EQ::Net::Packet &p);
	void ZoneProcessBuff(const EQ::Net::Packet &p);
	void ZoneProcessPlayerStateAdd(const EQ::Net::Packet &p);
	void ZoneProcessDeath(const EQ::Net::Packet &p);
	void ZoneProcessPlayerStateRemove(const EQ::Net::Packet &p);
	void ZoneProcessStamina(const EQ::Net::Packet &p);
	void ZoneProcessZonePlayerToBind(const EQ::Net::Packet &p);
	void ZoneProcessLevelUpdate(const EQ::Net::Packet &p);
	void ZoneProcessZoneChange(const EQ::Net::Packet &p);
	void ZoneProcessRequestClientZoneChange(const EQ::Net::Packet &p);
	void ZoneProcessLogoutReply(const EQ::Net::Packet &p);
	void ZoneProcessRezzRequest(const EQ::Net::Packet &p);
	void ZoneProcessRezzComplete(const EQ::Net::Packet &p);
	void ZoneProcessWhoAllResponse(const EQ::Net::Packet &p);
	void ZoneProcessInspectRequest(const EQ::Net::Packet &p);
	// T171: shared resolution of the per-client config node. See the
	// definitions in eq_bridge.cpp for why read and write are separate.
	static Json::Value* ResolveClientConfigForWrite(Json::Value& root);
	static const Json::Value* ResolveClientConfigForRead(const Json::Value& root);
	void ZoneProcessInspectAnswer(const EQ::Net::Packet &p);
	// K01: the four guild packet handlers are GuildComponent's; DispatchZonePacket
	// calls m_guild->ZoneProcessGuild*() directly.
	// Phase 3 packet handlers
	void ZoneProcessConsentResponse(const EQ::Net::Packet &p);
	void ZoneProcessDenyResponse(const EQ::Net::Packet &p);
	void ZoneProcessEnvDamage(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineUpdate(const EQ::Net::Packet &p);
	void ZoneProcessDisciplineTimer(const EQ::Net::Packet &p);
	void ZoneProcessBankerChange(const EQ::Net::Packet &p);
	void ZoneProcessClearObject(const EQ::Net::Packet &p);
	void ZoneProcessLFGAppearance(const EQ::Net::Packet &p);
	// Phase 4 packet handlers
	void ZoneProcessDuelRequest(const EQ::Net::Packet &p);
	void ZoneProcessRecipeReply(const EQ::Net::Packet &p);
	void ZoneProcessRecipeAutoCombine(const EQ::Net::Packet &p);
	void ZoneProcessRandomReply(const EQ::Net::Packet &p);
	void ZoneProcessFindPersonReply(const EQ::Net::Packet &p);
	void ZoneProcessCameraEffect(const EQ::Net::Packet &p);
	void ZoneProcessPlayMP3(const EQ::Net::Packet &p);
	void ZoneProcessSound(const EQ::Net::Packet &p);
	void ZoneProcessGMZoneRequest(const EQ::Net::Packet &p);
	void ZoneProcessGMFind(const EQ::Net::Packet &p);
	void ZoneProcessGMSummon(const EQ::Net::Packet &p);
	// Pet packet handlers
	void ZoneProcessPetBuffWindow(const EQ::Net::Packet &p);

	// Zone transition methods
	// T322: zone_reason is the value echoed into OP_ZoneChange's zone_reason
	// field. The official Titanium client echoes back the `type` byte the
	// server handed it in OP_RequestClientZoneChange (always 0x01 in eqemu,
	// zoning.cpp 983/1022); for a client-initiated zone-line crossing there
	// is nothing to echo and 1 (ZoneUnsolicited) is the right default. It is
	// a log-only memo field server-side -- matched for wire fidelity, not
	// because any server logic reads it.
	void RequestZoneChange(uint16_t zone_id, float x, float y, float z, float heading, uint32_t zone_reason = 1);
	// T322: the graceful source-zone hand-off the official client performs
	// before its zone-server session goes away. Returns false if it could
	// not be sent (no zone connection), in which case the caller must fall
	// straight through to the hard teardown.
	bool BeginGracefulZoneOut();
	// T231: same-zone OP_RequestClientZoneChange -- apply the destination
	// locally (state + renderer + one forced OP_ClientUpdate), no zone cycle.
	void RelocateWithinZone(float x, float y, float z, float server_heading);
	void CleanupZone();  // Cleanup current zone state for zone transition
	void DisconnectFromZone();  // Disconnect from current zone server
	void ProcessDeferredZoneChange();  // Process zone change outside of callback context

	// Combat packet handlers
	void ZoneProcessConsider(const EQ::Net::Packet &p);
	void ZoneProcessAction(const EQ::Net::Packet &p);
	void ZoneProcessDamage(const EQ::Net::Packet &p);
	void ZoneProcessStun(uint32_t duration_ms);  // T247
	void ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p);

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_zone_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_zone_connection;

	// Safe packet sending helper
	bool SafeQueueZonePacket(EQ::Net::Packet &p, int stream = 0, bool reliable = true);

	// Batch R: incremented whenever a Zone packet sender no-ops because
	// m_zone_connection is null (replay/test mode has no live connection).
	uint64_t m_replay_suppressed_sends = 0;

	// T88 test seam (see Test_SetBypassSendGuardForSeam / Test_GetLastComputedPositionUpdate).
	bool m_test_bypass_send_guard = false;
	TestSentPositionUpdate m_test_last_sent_update;

	// Loading phase tracking (covers entire pre-gameplay process)
	LoadingPhase m_loading_phase = LoadingPhase::DISCONNECTED;
	const char* m_loading_status_text = "";

	// Zone state
	bool m_zone_session_established = false;
	bool m_zone_entry_sent = false;
	bool m_weather_received = false;
	bool m_req_new_zone_sent = false;

	// Time of day (from server)
	bool m_new_zone_received = false;
	bool m_aa_table_sent = false;
	bool m_update_aa_sent = false;
	bool m_tributes_sent = false;
	bool m_guild_tributes_sent = false;
	bool m_req_client_spawn_sent = false;
	bool m_spawn_appearance_sent = false;
	bool m_exp_zonein_sent = false;
	bool m_send_exp_zonein_received = false;
	bool m_server_filter_sent = false;
	bool m_client_spawned = false;
	uint32_t m_zone_sequence = 2;
	int m_aa_table_count = 0;
	int m_tribute_count = 0;
	int m_guild_tribute_count = 0;

	// Entity tracking
	std::map<uint16_t, Entity> m_entities;
	// T288: the player's own spawn id lives in m_game_state.player() and
	// nowhere else -- MySpawnId()/SetMySpawnId() above are accessors INTO
	// that store, not a copy.
	// T247: expiry of the current OP_Stun; epoch (the default) = not stunned.
	std::chrono::steady_clock::time_point m_stunned_until{};
	bool m_player_graphics_entity_pending = false;  // True when player entity needs to be created on renderer after zoning

	// Target tracking (server-authoritative — renderer is a consumer, not the source of truth)

	// Pet tracking
	// T286: pet spawn id and button states live in m_game_state.pet().

	// String database for EQ message lookups (works in all modes)
	EQT::StringDatabase m_string_db;

	// T219: door state lives in m_game_state.doors() (the write model) --
	// the former separate EQT::DoorStateManager member was a third copy that
	// neither GameState instance ever saw (refactor/T78_two_gamestate_analysis.md item 3).
	std::set<uint8_t> m_pending_door_clicks;  // Doors clicked by user, awaiting server response

	// World object tracking (forges, looms, groundspawns, etc.)
	std::map<uint32_t, eqt::WorldObject> m_world_objects;
	uint32_t m_active_tradeskill_object_id = 0;  // Currently open tradeskill container (0 = none)

	int m_character_select_index = -1;
	std::vector<CharacterSelectEntry> m_character_select_list;

	// Character stats. T286: level/class/race/gender/deity, HP/mana/
	// endurance, the seven attributes, currency, bank currency, weight and
	// the bind point all live in m_game_state.player() and nowhere else.
	// Only what PlayerState does NOT carry stays here.
	uint32_t m_exp = 0;          // Current experience points
	uint32_t m_exp_aa = 0;       // Current AA experience points

	// Movement and position tracking. T288: position and heading live in
	// m_game_state.player() and nowhere else -- PosX()/PosY()/PosZ()/
	// HeadingDeg() and SetPosLocal()/SetHeadingLocal() above are accessors
	// INTO that store, not a copy.

	// Movement target
	uint32_t m_last_movement_update = 0;
	std::chrono::steady_clock::time_point m_last_position_update_time;

	// Follow target

	// Combat movement

	// Last slain entity (for SimpleMessage "You have slain %1!" substitution)

	// Pathfinding
	std::unique_ptr<IPathfinder> m_pathfinder;
	// T288: zone identity lives in m_game_state.world() and nowhere else --
	// CurrentZoneName()/CurrentZoneId() above are accessors INTO that store.

	// Zone environment data from NewZone packet
	uint8_t m_zone_sky_type = 0;           // Sky type for rendering (0-255)
	uint8_t m_zone_type = 0;               // Zone type (0=outdoor, 1=dungeon, etc.)
	uint8_t m_zone_fog_red[4] = {0};       // Fog colors for 4 fog ranges
	uint8_t m_zone_fog_green[4] = {0};
	uint8_t m_zone_fog_blue[4] = {0};
	float m_zone_fog_minclip[4] = {0};     // Fog min clip distances
	float m_zone_fog_maxclip[4] = {0};     // Fog max clip distances
	float m_safe_x = 0, m_safe_y = 0, m_safe_z = 0;  // Zone safe coordinates (client coords)

	std::vector<glm::vec3> m_current_path;
	size_t m_current_path_index = 0;

	// "Update loop" flag. Set at the first ClientUpdate (StartUpdateLoop),
	// cleared by CleanupZone / the destructor (StopUpdateLoop). It is part of
	// IsFullyZonedIn() and gates OnGameStateComplete(). T245: the 500 ms
	// std::thread that used to run behind it is GONE -- CombatManager::Update()
	// is now ticked from UpdateMovement() on the game thread (eq_movement.cpp),
	// so every GameState writer is game-thread. Nothing else ran on it.
	std::atomic<bool> m_update_running{false};
	std::thread::id m_main_thread_id;
	bool m_use_pathfinding = true;
	std::string m_navmesh_path;

	// R01: Session recorder (null unless --record-session was passed)
	std::unique_ptr<EQT::Net::SessionRecorder> m_session_recorder;

	// Map for Z-height calculations. shared_ptr<const HCMap> (P35): the
	// renderer/SimulationWorker boundary holds copies of this same
	// shared_ptr (via CollisionMapChangedData / SimulationZoneData::hcMap),
	// so a zone-change reset() here only drops the game thread's
	// reference -- the object survives until every renderer-side holder
	// (collisionMap_, SimulationWorker's zoneData_.hcMap) also releases
	// it, closing the P34-adjacent CRITICAL UAF where the game thread
	// freed the old HCMap out from under the SimulationWorker thread
	// mid-zone-transition. HCMap is immutable zone data (all read paths
	// are const methods) so const-ness costs nothing on the game-thread
	// side.
	std::shared_ptr<const HCMap> m_zone_map;

	// Zone lines for zone transitions
	std::unique_ptr<EQT::ZoneLines> m_zone_lines;

	// Zone line detection state
	bool m_zoning_enabled = false;                                   // Whether zone line detection triggers zoning (Ctrl+Z toggles)

	// H03: server confirmed our OP_Logout (ZoneProcessLogoutReply) — DisconnectFromZone()
	// must not run synchronously from inside that packet handler (it resets
	// m_zone_connection_manager, destroying the DaybreakConnectionManager whose Tick()
	// is still on the call stack — a use-after-free). Deferred the same way as
	// m_game_state.world().zoneChangeApproved(): set here, processed at the top of UpdateMovement().
	bool m_pending_logout_disconnect = false;

	// T322: graceful zone-out hand-off state. The official Titanium client
	// does NOT close its source-zone socket on a zone change: it sends
	// OP_SaveOnZoneReq then OP_DeleteSpawn(own entity id) and lets the SERVER
	// close the session (eqemu Handle_OP_DeleteSpawn -> Disconnect()). That
	// difference decides whether the group survives -- see the block comment
	// on BeginGracefulZoneOut() in eq_zone.cpp.
	//
	// m_zone_out_handshake_pending: hand-off sent, waiting for the source
	// zone's OP_LogoutReply. While set, ZoneProcessLogoutReply() treats that
	// reply as a zone-out acknowledgement and NOT as a logout -- the logout
	// path (SendLogout/camp, and the world-side disconnect) is untouched
	// whenever this flag is clear.
	bool m_zone_out_handshake_pending = false;
	bool m_zone_out_acknowledged = false;
	std::chrono::steady_clock::time_point m_zone_out_handshake_deadline{};
	// Bound on how long the transition waits for the acknowledgement. A lost
	// or never-sent reply must not wedge zoning, so the teardown runs anyway.
	static constexpr int kZoneOutHandshakeTimeoutMs = 2000;
	// T322 test seam (see Test_BeginGracefulZoneOutForSeam): lifts the
	// live-connection gate in BeginGracefulZoneOut() only. Never set in
	// production.
	bool m_test_force_zone_out_handshake = false;

	// T322: "this world reconnect is a ZONE TRANSITION, not a fresh login."
	// It cannot live in WorldState: CleanupZone() -> resetZoneState() clears
	// every zone flag, and it runs (inside DisconnectFromZone) BEFORE the
	// world reconnect that has to read it -- which is why the zoning byte in
	// OP_SendLoginInfo was always 0. Set in ProcessDeferredZoneChange(),
	// cleared once the destination zone session is up (or on a logout).
	bool m_zone_to_zone_in_progress = false;

	// Combat manager
	std::unique_ptr<CombatManager> m_combat_manager;

	// Trade manager
	std::unique_ptr<TradeManager> m_trade_manager;

	// Central data database (SQLite)
	std::unique_ptr<EQT::WilleqDatabase> m_willeq_db;

	// Spell manager
	std::unique_ptr<EQ::SpellManager> m_spell_manager;

	// Buff manager
	std::unique_ptr<EQ::BuffManager> m_buff_manager;

	// Pending spell scribe state (for handling server confirmation)
	uint32_t m_pending_scribe_spell_id = 0;
	uint16_t m_pending_scribe_book_slot = 0;
	int16_t m_pending_scribe_source_slot = -1;

	// Skill manager
	std::unique_ptr<EQ::SkillManager> m_skill_manager;

	// Group state
	// T286: group membership, leader, member array/count and the pending
	// invite all live in m_game_state.group() and nowhere else.

	// Pending group invite

	// Chat state - for reply to tell
	std::string m_last_tell_sender;

	// Group helper methods
	void ClearGroup();
	int FindGroupMemberByName(const std::string& name) const;
	void UpdateGroupMemberFromEntity(int index);

	// Group packet handlers
	void ZoneProcessGroupInvite(const EQ::Net::Packet& p);
	void ZoneProcessGroupFollow(const EQ::Net::Packet& p);
	void ZoneProcessGroupUpdate(const EQ::Net::Packet& p);
	// T320: groupActJoin carries a GroupJoin_Struct, not a GroupUpdate_Struct
	// -- both are 452 bytes, so only the action can tell them apart.
	void ZoneProcessGroupJoin(const EQ::Net::Packet& p);
	void FillGroupMemberFromEntities(GroupMember& member) const;
	void ZoneProcessGroupDisband(const EQ::Net::Packet& p);
	void ZoneProcessGroupCancelInvite(const EQ::Net::Packet& p);

	// Movement history for anti-cheat
	struct MovementHistoryEntry {
		float x, y, z;
		uint8_t type;
		uint32_t timestamp;
	};
	std::deque<MovementHistoryEntry> m_movement_history;
	uint32_t m_last_movement_history_send = 0;
	std::string m_maps_path;

	// Movement and position state

	// Swimming state
	WaterState m_water_state = WaterState::NotInWater;
	float m_water_surface_z = 0.0f;  // Z coordinate of water surface when swimming
	bool m_swim_up = false;          // Currently pressing swim up key
	bool m_swim_down = false;        // Currently pressing swim down key

	// Camp timer
	static constexpr int CAMP_TIMER_SECONDS = 30;

	// Pending resurrection offer
	bool m_has_pending_rezz = false;
	// Phase 3: Corpse drag state
	bool m_is_dragging_corpse = false;
	std::string m_dragged_corpse_name;
	// Phase 3: Boat state
	// Phase 3: LFG state
	bool m_is_lfg = false;
	// Phase 4: Duel state
	bool m_is_dueling = false;
	bool m_has_pending_duel = false;
	uint32_t m_duel_target_id = 0;
	uint32_t m_duel_initiator_id = 0;
	EQT::Resurrect_Struct m_pending_rezz;

	// T148: PlayerProfile.gm @13054 -- "GM mode was on at login". See
	// GetGMMode() for the full contract (login-time only, deliberately
	// stale, NOT an account-privilege flag).
	bool m_gm_mode = false;

	// T167: the seven PlayerProfile appearance fields. See
	// GetProfileAppearance().
	EQT::ProfileAppearance m_profile_appearance;

	// T167 test seam -- see Test_GetLastFaceChange().
	TestSentFaceChange m_test_last_face_change;

	// T217 test seam -- see Test_GetLastGMHideMe().
	TestSentGMHideMe m_test_last_gm_hideme;

	// T229 test seam -- see Test_GetLastGMSummon().
	TestSentGMSummon m_test_last_gm_summon;

	// T170 test seam -- see Test_GetLastInspectAnswer().
	TestSentInspectAnswer m_test_last_inspect_answer;

	// T231 test seam -- see Test_GetLastZoneChangeRequest().
	TestZoneChangeRequest m_test_last_zone_change_request;

	// T268 test seam -- see Test_GetQueuedPackets().
	bool m_test_capture_queued_packets = false;
	std::vector<TestQueuedPacket> m_test_queued_packets;

	// T231: one-shot flag read by SendPositionUpdate() -- bypass the 250 ms
	// throttle and restart delta tracking at the current position. Set only
	// by RelocateWithinZone().
	bool m_force_next_position_update = false;

	// T170/T171: what we send as our inspect message. eqemu never tells a
	// Titanium client its own stored message, so this client keeps its own --
	// see SendInspectAnswer() and SaveInspectMessage().
	std::string m_inspect_message;

	// Guild state. K01: the pending-invite trio moved into GuildComponent.
	// m_guild_id/m_guild_rank STAY -- written by the zone family
	// (eq_zone_entry.cpp, eq_entities.cpp), read by the public getters.
	// m_guild_name has ZERO references tree-wide (K01; BOARD B-item).
	uint32_t m_guild_id = 0;
	// T317: Titanium rank encoding (0 member / 1 officer / 2 leader / 9 none).
	// 9 == GUILD_RANK_NONE_TI is the correct "not in a guild" default.
	uint8_t m_guild_rank = 9;
	std::string m_guild_name;

	// Movement methods
	void MoveTo(float x, float y, float z);
	void MoveToWithPath(float x, float y, float z);
	void StopMovement();
	float CalculateHeading(float x1, float y1, float x2, float y2);
	float CalculateDistance(float x1, float y1, float z1, float x2, float y2, float z2) const;
	float CalculateDistance2D(float x1, float y1, float x2, float y2) const;

	// Entity helper methods
	Entity* FindEntityByName(const std::string& name);

	// Pathfinding methods
	void LoadPathfinder(const std::string& zone_name);
	bool FindPath(float start_x, float start_y, float start_z, float end_x, float end_y, float end_z);
	void FollowPath();

	// Map methods
	void LoadZoneMap(const std::string& zone_name);
	float GetBestZ(float x, float y, float z);
	void FixZ();

	// Zone source data (S3D/WLD) — opens archive and parses WLD once for game state + graphics
	void LoadZoneSources(const std::string& zone_name);

	// Zone line methods
	void LoadZoneLines(const std::string& zone_name);
	void CheckZoneLine();  // Check if player is in a zone line and handle transition

	// Zone connection flow helpers
	void CheckZoneRequestPhaseComplete();

	// UCS
	std::string m_ucs_host;
	uint16_t m_ucs_port = 0;
	std::string m_mail_key;
	// T13: the connection-type character the world server concatenates onto
	// the mail key. Titanium sends 'B' on OP_SetChatServer (chat) and 'b' on
	// OP_SetChatServer2 (mail) — see emu_versions.h ucsTitaniumChat/Mail. A
	// UCS implementation needs it to tell the two connections apart, so it is
	// stored rather than discarded. 0 = nothing parsed yet.
	char m_ucs_connection_type = 0;

	// World server channel message routing
	void WorldSendChannelMessage(const std::string &channel, const std::string &message, const std::string &target = "");
	void WorldProcessChannelMessage(const EQ::Net::Packet &p);

	// P28: the bulk of this block (eq client/config paths, quit flag, hotbar
	// model, inventory manager, command registry, "Player mode" loot/vendor/
	// bank/trainer state + packet handlers, spawn/pet callbacks) is plain
	// game state and network logic with no renderer dependency — it was
	// folded under EQT_HAS_GRAPHICS by 55b101e alongside the genuinely
	// renderer-only members that remain guarded just below (D20b5 raw
	// pointers, constrained-rendering config, loading-thread signaling,
	// zone-load-request handshake).
	// D20e3: m_renderer removed — Application owns the renderer
	std::string m_eq_client_path;
	std::string m_region_maps_path = "data/region_maps";
	std::string m_config_path;  // Path to per-character config file
	bool m_quit_requested = false;  // D20c4: Application checks this flag
	// U07c3: m_hotbar_window_manager removed — old UI deleted
	std::unique_ptr<eqt::HotbarModel> m_hotbar_model;  // U07b: standalone hotbar
	// D20g: m_rdp_server removed — Application owns RDP setup

	// Inventory manager
	std::unique_ptr<eqt::inventory::InventoryManager> m_inventory_manager;

	// Command registry for chat commands
	std::unique_ptr<eqt::ui::CommandRegistry> m_command_registry;
	void RegisterCommands();  // Driver: calls per-domain registration methods below, in order (G02)
	// G02: per-domain command registration, split out of the former monolithic
	// RegisterCommands() body. Order between methods (and within each) matches
	// the original registration order exactly — see eq_commands.cpp driver.
	void RegisterSocialCommands();    // Chat/group/trade/guild/corpse/duel/misc social commands (eq_commands.cpp)
	void RegisterMovementCommands();  // Movement/combat/utility-debug/sky commands (eq_commands_movement.cpp)
	void RegisterSpellCommands();     // Spell/skill/hotkey/detail/audio commands (eq_commands_spells.cpp)
	// T136: RegisterVendorCommands() (T48: /vendorlist, /buy, /sell) removed
	// -- migrated to action::CommandProcessor. See RequestVendorToggle()/
	// RequestVendorBuy()/RequestVendorSell() below.
	// T141: RegisterEquipmentCommands() (T66: /equip) removed -- migrated to
	// action::CommandProcessor. See RequestEquip() below.

	// Manual zone load diagnostics helpers
	void runPmemDiagnostics(const std::string& label);
	void runLoadDiagnostics(const std::string& label);

	// Loot window state (game-side tracking, D18a). T268: the ONE loot
	// window -- set by RequestLootCorpse for every open path (/loot,
	// shift+click, auto-hunt) and read by the item-packet dispatch; the
	// loot-all sequencing lives in CombatManager's LootSubState machine
	// (the m_loot_all_* loop that duplicated it is gone).
	std::vector<int16_t> m_pending_loot_slots;  // Corpse slots waiting for server confirmation
	uint16_t m_loot_complete_corpse_id = 0;  // Corpse ID for which looting was completed (ready for deletion)
	struct GameLootItem {
		uint32_t itemId = 0;
		std::string name;
	};
	std::map<int16_t, GameLootItem> m_loot_items;  // slot -> item data (game-side)

	// Player mode vendor state (game-side tracking, D18a)
	struct GameVendorItem {
		uint32_t itemId = 0;
		std::string name;
		int32_t price = 0;        // Price in copper
		int32_t quantity = -1;    // -1 = unlimited
	};
	std::map<uint32_t, GameVendorItem> m_vendor_items;  // slot -> item data (game-side)

	// Format copper amount as currency string (e.g. "5p 3g 2s 1c")
	static std::string FormatPrice(int32_t copperAmount);

	// Player mode bank state

	// Player mode trainer state
	// T280: the per-skill CAPS the server sent in its OP_GMTraining reply
	// (GMTrainee_Struct::skills[], one entry per skill id). They were read
	// once to build the TrainerWindowOpened event and then thrown away, so
	// /train had nothing to check a request against. Kept for the life of
	// the trainer session; cleared by CloseTrainerWindow().
	std::vector<uint32_t> m_trainer_skill_caps;

	// Graphics callbacks (LoadZoneGraphics is public, others are private)
	void OnSpawnAddedGraphics(const Entity& entity);  // Only creates entity if loading is complete
	void OnSpawnRemovedGraphics(uint16_t spawn_id);
	void OnSpawnMovedGraphics(uint16_t spawn_id, float x, float y, float z, float heading,
	                          float dx, float dy, float dz, int32_t animation);

	// Pet graphics callbacks
	void OnPetCreated(const Entity& pet);
	void OnPetRemoved();
	void OnPetButtonStateChanged(EQT::PetButton button, bool state);

	// Inventory packet handlers
	void ZoneProcessMoveItem(const EQ::Net::Packet& p);
	void ZoneProcessDeleteItem(const EQ::Net::Packet& p);
	void SetupInventoryCallbacks();
	void SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity);
	void SendDeleteItem(int16_t slot);

	// Spell scribing from scroll
	void ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot);
	// T276: the one scribe-refusal string table, shared by /scribe's
	// pre-flight and ScribeSpellFromScroll()'s guard (eq_merchant.cpp).
	// Empty return = no refusal.
	std::string ScribeRefusalMessage(uint32_t spellId);

	// Loot packet handlers (Player mode)
	void ZoneProcessLootItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessLootedItemToInventory(const EQ::Net::Packet& p);
	void SetupLootCallbacks();

	// Vendor packet handlers (Player mode)
	void ZoneProcessShopRequest(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerBuy(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerSell(const EQ::Net::Packet& p);
	void ZoneProcessShopEndConfirm(const EQ::Net::Packet& p);
	void ZoneProcessVendorItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessMoneyUpdate(const EQ::Net::Packet& p);
	void SetupVendorCallbacks();
	void SetupBankCallbacks();

	// Skill trainer packet handlers and send functions
	void ZoneProcessGMTraining(const EQ::Net::Packet& p);
	void SetupTrainerCallbacks();

	// Trade packet handlers and send functions
	void SetupTradeManagerCallbacks();
	void SetupTradeWindowCallbacks();
	bool ZoneProcessTradePartnerItem(const EQ::Net::Packet& p);
	void SendTradeRequest(const EQT::TradeRequest_Struct& req);
	void SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack);
	void SendMoveCoin(const EQT::MoveCoin_Struct& move);

	// T269 bank-coin helpers (eq_bridge.cpp, next to the Bank* intent arms).
	// coinType is the wire scale (EQT::COINTYPE_*: 0=cp 1=sp 2=gp 3=pp).
	// BankCoinGet/BankCoinSet read and write the counter for one
	// denomination in one pool (bank=true -> the bank pool, false -> the
	// on-hand pool). T286: the counters live only in
	// m_game_state.player(), so this is a get/set pair rather than the
	// pointer-into-a-member it used to be.
	uint32_t BankCoinGet(uint8_t coinType, bool bank) const;
	void BankCoinSet(uint8_t coinType, bool bank, uint32_t value);
	static const char* BankCoinName(uint8_t coinType);
	static int64_t BankCoinCoppers(uint8_t coinType);
	// Publishes CurrencyChanged (only when the on-hand pool moved) +
	// BankCurrencyChanged. T286: no mirroring step -- the counters are
	// GameState's own.
	void PublishBankCoinProjection(bool inventoryChanged);
	void SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept);
	void SendCancelTrade(const EQT::CancelTrade_Struct& cancel);

	// Tradeskill container callbacks
	void SetupTradeskillCallbacks();

	// Book/Note reading packet handlers
	void ZoneProcessReadBook(const EQ::Net::Packet& p);
	void SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename);
	// T273: split the server's book text into chat lines. Public entry is
	// ZoneProcessReadBook; exposed here only so the handler stays readable.
	void SurfaceBookText(const std::string& text);

public:
	// Save entity data to JSON file for debugging
	void SaveEntityDataToFile(const std::string& filename);

	// Loot window methods -- the single senders (T268): OP_LootRequest,
	// OP_LootItem, OP_EndLootRequest. corpseId 0 means "the open window".
	void RequestLootCorpse(uint16_t corpseId);
	// autoLoot=true: item goes straight to inventory (eqemu
	// AutoPutLootInInventory); false: to the cursor, as the UI gesture does.
	// Returns false (nothing sent) when no window is open.
	bool LootItemFromCorpse(uint16_t corpseId, int16_t slot, bool autoLoot = false);
	void CloseLootWindow(uint16_t corpseId);
	bool IsLootWindowOpen() const { return m_game_state.player().lootingCorpseId() != 0; }
	uint16_t GetLootWindowCorpseId() const { return m_game_state.player().lootingCorpseId(); }
	const std::map<int16_t, GameLootItem>& GetLootItems() const { return m_loot_items; }
	// Case-insensitive match against the open window's item names: an exact
	// match wins, else the lowest slot whose name contains `name`. Returns
	// -1 when nothing matches (or no window is open).
	int16_t FindLootSlotByName(const std::string& name) const;

	// Vendor window methods (Player mode with graphics)
	void RequestOpenVendor(uint16_t npcId);
	void BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void CloseVendorWindow();
	bool IsVendorWindowOpen() const { return m_game_state.player().vendorNpcId() != 0; }
	uint16_t GetVendorNpcId() const { return m_game_state.player().vendorNpcId(); }

	// T136: minimal read accessors for the migrated /vendorlist command --
	// see refactor/T136_migrate_merchant_family.md ("the state gap") for why
	// these two reads were exposed instead of building a vendor state
	// domain (T103's GameStateSyncBridge precedent doesn't apply: this
	// serves exactly one display command, not a cross-cutting need).
	// GameVendorItem is private, but its members are public (default for a
	// struct) -- callers iterate via structured bindings without ever
	// naming the type, same idiom already used throughout eq_commands.cpp.
	const std::string& GetVendorName() const { return m_game_state.player().vendorName(); }
	const std::map<uint32_t, GameVendorItem>& GetVendorItems() const { return m_vendor_items; }
	static std::string FormatVendorPrice(int32_t copperAmount) { return FormatPrice(copperAmount); }

	// T136: /vendor, /buy, /sell push the SAME eqt::events::Vendor*Intent
	// the renderer's vendor-window gesture pushes (T48's max-fidelity
	// choice, preserved). action::EqActionHandler has no access to the
	// private m_bridge, so these thin wrappers carry the exact guard bodies
	// eq_commands.cpp's old RegisterVendorCommands() lambdas had --
	// stateful guards stay in the layer owning the state (T134); only
	// usage-string/numeric-token parsing moved up to
	// action::CommandProcessor::cmdBuy/cmdSell. Defined in eq_commands.cpp.
	void RequestVendorToggle();
	void RequestVendorBuy(uint32_t slot, uint8_t quantity);
	void RequestVendorSell(uint32_t slot, uint8_t quantity);

	// T141: /equip's thin orchestration entry point -- form dispatch +
	// read-only report formatting only; all legality/mutation/bridge-push
	// logic lives in InventoryManager::moveItem() and friends
	// (include/client/inventory/inventory_manager.h). Defined in
	// eq_commands.cpp. See refactor/T141_equip_extraction.md.
	void RequestEquip(const std::string& args);

	// T230: fire an inventory item's click effect (OP_CastSpell, slot 10)
	// from an INVENTORY slot. The one sender for both the mouse hotbar path
	// (HotbarActivateIntent, Item) and the keyboard hotbar path
	// (EqActionHandler::useItem). Every refusal is reported via
	// AddChatSystemMessage; returns true only when the packet was queued.
	// Pushes HotbarCooldownStarted for each hotbar slot holding the item.
	// Defined in eq_bridge.cpp.
	bool UseInventoryItem(int16_t slot);

	// T239: fire a hotbar slot whose content is a COMMAND rather than a
	// spell/skill/item: Emote and Macro carry command text (run through
	// ProcessChatInput, the live chat window's unified dispatch entry);
	// Combat toggles auto-attack (CombatManager::ToggleAutoAttack). The one
	// body for both the mouse hotbar path (HotbarActivateIntent) and the
	// keyboard hotbar path (EqActionHandler::activateHotbarCommandSlot <-
	// ActionDispatcher <- InputActionBridge). Refusals are reported via
	// AddChatSystemMessage; returns true only when something was executed.
	// Any other slot type is not this function's business and returns false.
	// Defined in eq_bridge.cpp.
	bool ActivateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text);

	// Bank window methods (Player mode with graphics)
	void OpenBankWindow(uint16_t bankerNpcId = 0);
	void CloseBankWindow();
	bool IsBankWindowOpen() const { return m_game_state.player().bankerNpcId() != 0; }
	uint16_t GetBankerNpcId() const { return m_game_state.player().bankerNpcId(); }

	// Skill trainer window methods (Player mode with graphics)
	void RequestTrainerWindow(uint16_t npcId);
	void TrainSkill(uint8_t skillId);
	void CloseTrainerWindow();
	bool IsTrainerWindowOpen() const { return m_game_state.player().trainerNpcId() != 0; }
	uint16_t GetTrainerNpcId() const { return m_game_state.player().trainerNpcId(); }

	// T280: /train <skill|id> and /trainclose (T264 F8). RequestTrainSkill()
	// resolves the argument against the caps the OPEN trainer session sent,
	// refuses locally (message, no intent, nothing on the wire) for the
	// things the client can actually know -- no session, no training
	// points, an unknown/ambiguous skill name, a skill this trainer does
	// not teach, a skill already at this trainer's cap -- and otherwise
	// pushes the kept TrainSkillIntent, the intent T264 listed as BLOCKED.
	// It writes NOTHING locally: the new skill value arrives as the
	// server's OP_SkillUpdate. `list` in place of a skill name prints the
	// session's trainable skills. RequestCloseTrainer() pushes the kept
	// CloseTrainerIntent. Both are defined in eq_commands_train.cpp.
	void RequestTrainSkill(const std::string& args);
	void RequestCloseTrainer();
	// T280: /train list -- the trainer session's offerings, with the price.
	void ListTrainableSkills();
	// T280: the trainer session's cap for one skill id (0 = this trainer
	// does not teach it, or there is no session).
	uint32_t GetTrainerSkillCap(uint8_t skillId) const;
	// T280: eqemu's own training price in copper for a skill currently at
	// `currentValue` (Client::OPGMTrainSkill). Static: it is arithmetic, and
	// the trainer window and /train both quote it.
	static uint32_t TrainingCost(uint32_t currentValue);

	// T140: four thin wrappers, same rationale as T136's
	// RequestVendorToggle/Buy/Sell -- action::EqActionHandler has no access
	// to EverQuest's private m_bridge/m_combat_manager/m_entities, so each
	// carries its old eq_commands_*.cpp lambda body verbatim (guard logic
	// included, unchanged) rather than exposing those members directly.
	// Defined in eq_commands_movement.cpp (RequestWarp/RequestTimeOfDay,
	// where the old lambdas lived) and eq_commands_spells.cpp
	// (RequestBankToggle/RequestTraining, ditto).
	void RequestWarp(const std::string& args);
	void RequestTimeOfDay(const std::string& args);
	void RequestBankToggle();
	void RequestTraining();

	// T269: /bank deposit|withdraw|convert. Push the kept
	// BankCurrencyMoveIntent / BankCurrencyConvertIntent (the same shape a
	// bank-window coin gesture would push) for ProcessBridgeIntents() to
	// turn into OP_MoveCoin. Refuse (message, no intent) when the bank
	// window is not open. Defined in eq_merchant.cpp next to SendMoveCoin.
	void RequestBankCoinMove(uint8_t coinType, int32_t amount, bool fromBank);
	void RequestBankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount);

	// Book/Note reading methods (Player mode with graphics)
	void RequestReadBook(const std::string& filename, uint8_t type = 0);

	// T273: /read <slot|item name>. Resolves the carried item, refuses
	// (message, no intent) when it is not a book/note, and otherwise pushes
	// the kept ReadItemIntent -- the same intent a book-item double-click
	// gesture would push -- for ProcessBridgeIntents() to turn into
	// RequestReadBook() -> OP_ReadBook. Defined in eq_commands.cpp next to
	// RequestEquip, whose item-name resolution it mirrors.
	void RequestReadItem(const std::string& args);

	// T276: /scribe <slot|item name>. Resolves the carried spell scroll,
	// refuses locally (message, no intent, nothing on the wire) for a
	// non-scroll / already-known spell / wrong class / too low a level /
	// a full spellbook, and otherwise pushes the kept ScribeSpellIntent --
	// the intent T264 listed as BLOCKED (F5). Defined in eq_commands.cpp
	// next to RequestReadItem, whose item resolution it mirrors.
	void RequestScribeItem(const std::string& args);

	// T276: shared slot|name resolution for /read and /scribe (T273's
	// copy of /equip's rule, now one function). Returns nullptr after
	// having already sent the refusal chat line; on success writes the
	// resolved inventory slot to slotOut.
	const eqt::inventory::ItemInstance* ResolveCarriedItem(
		const std::string& args, const char* verb, int16_t& slotOut);
	// T277: click off a buff. RequestCancelBuff() resolves a user-facing
	// argument -- a 1-based buff slot number, or a spell-name substring --
	// against BuffManager's player buffs. SendBuffCancelRequest() is the
	// single wire hop it and the buff-bar click (BuffCancelIntent) share;
	// it removes nothing locally, it only asks. Defined in eq_player_buffs.cpp
	// beside ZoneProcessBuff(), which handles the server's answer.
	void RequestCancelBuff(const std::string& args);
	void SendBuffCancelRequest(uint8_t slot);

	// T277: publish ONE player buff slot to the renderer. Shared by
	// ZoneProcessBuff()'s apply branch and the PlayerProfile's zone-in
	// buffs, which previously reached BuffManager and stopped there.
	void PublishPlayerBuffUpdated(uint8_t slot, const EQT::SpellBuff_Struct& buff);
	void PublishPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count);
	// T278: /combine and /closecontainer. Push the kept
	// TradeskillCombineIntent / TradeskillCloseIntent -- the same intents a
	// forge-window Combine button and its close box would push -- for
	// ProcessBridgeIntents() to turn into SendTradeSkillCombine() /
	// SendCloseContainer(). containerSlot == SLOT_INVALID means the world
	// container the server has told us is open (refused, with a message and
	// no intent, when there is none); any other value is a carried
	// container's own inventory slot. Defined in eq_commands.cpp.
	void RequestTradeskillCombine(int16_t containerSlot);
	void RequestCloseTradeskillContainer();

private:
#ifdef EQT_HAS_GRAPHICS
	// Genuinely renderer-lifetime-only state: raw pointers Application wires
	// up around InitGraphics/ShutdownGraphics, the constrained-rendering
	// (NxNxN) startup preset/config, the loading-thread progress signal, and
	// the re-zone handshake flag consumed only from the graphics render loop.
	bool m_graphics_initialized = false;
	EQT::Graphics::ConstrainedRenderingPreset m_constrained_preset = EQT::Graphics::ConstrainedRenderingPreset::OrangePi;  // Constrained rendering preset (startup-only)
	std::optional<EQT::Graphics::ConstrainedRendererConfig> m_constrained_config;  // Custom constrained config (from NxNxN spec)
	// D20e2: Loading thread owned by Application; EverQuest holds pointer to signal it
	EQT::Graphics::LoadingStatus* m_loading_status_ptr = nullptr;
	bool m_zone_load_requested = false;  // Set by re-zone, consumed by Application
#endif

#ifdef WITH_AUDIO
	// Audio manager for sound effects and music
	std::unique_ptr<EQT::Audio::AudioManager> m_audio_manager;
	// Zone audio manager for positioned sound emitters
	std::unique_ptr<EQT::Audio::ZoneAudioManager> m_zone_audio_manager;
	void InitializeAudio();
	void ShutdownAudio();
	// D20g: SetupRDPAudio moved to Application

	// Day/night state for audio (calculated from game time)
	bool m_is_daytime = true;
	void UpdateDayNightState();

	// Audio configuration (applied during InitializeAudio)
	bool m_audio_config_enabled = true;
	float m_audio_config_master_volume = 1.0f;
	float m_audio_config_music_volume = 0.7f;
	float m_audio_config_effects_volume = 1.0f;
	std::string m_audio_config_soundfont;
	std::string m_audio_config_vendor_music = "gl.xmi";
	bool m_audio_config_use_3d_audio = true;

public:
	EQT::Audio::AudioManager* GetAudioManager() { return m_audio_manager.get(); }
	EQT::Audio::ZoneAudioManager* GetZoneAudioManager() { return m_zone_audio_manager.get(); }
	bool IsDaytime() const { return m_is_daytime; }

	// Audio configuration setters (call before InitGraphics/InitializeAudio)
	void SetAudioEnabled(bool enabled) { m_audio_config_enabled = enabled; }
	void SetMasterVolume(float volume) { m_audio_config_master_volume = volume; }
	void SetMusicVolume(float volume) { m_audio_config_music_volume = volume; }
	void SetEffectsVolume(float volume) { m_audio_config_effects_volume = volume; }
	void SetSoundFont(const std::string& path) { m_audio_config_soundfont = path; }
	void SetVendorMusic(const std::string& filename) { m_audio_config_vendor_music = filename; }
	void SetUse3DAudio(bool enabled) { m_audio_config_use_3d_audio = enabled; }

	// Sound effect helpers (client-triggered)
	void PlaySound(uint32_t soundId);
	void PlaySoundAt(uint32_t soundId, float x, float y, float z);
	void PlayCombatSound(bool hit, float x, float y, float z);
	void PlaySpellSound(uint32_t spellId);
	void PlayUISound(uint32_t soundId);
#endif
private:
	// K01: declared last -- every member m_ctx references is declared above.
	eqt::ClientContext m_ctx;
	std::unique_ptr<eqt::guild::GuildComponent> m_guild;
	std::unique_ptr<eqt::chat::ChatComponent> m_chat;
};
