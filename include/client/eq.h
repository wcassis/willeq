#pragma once

#include "common/logging.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_recorder.h"
#include "common/packet_structs.h"
#include "client/player/profile_appearance.h"

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
#include "client/zone/zone_connect_watchdog.h"  // B1072
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
    class IZoneLoadSurface;  // K16 (B006): what InitGraphics takes now
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
    struct ItemInstance;
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
#include "client/gm/gm_component.h"  // K02: GmComponent
#include "client/chat/chat_component.h"    // K03: ChatComponent
#include "client/group/group_component.h"  // K04: GroupComponent (group/raid/pet)
#include "client/merchant/merchant_component.h"  // K05: MerchantComponent
#include "client/entities/entities_component.h"  // K06: EntitiesComponent
#include "client/movement/movement_component.h"  // K07: MovementComponent
#include "client/login_world/login_world_component.h"  // K10: LoginWorldComponent
#include "client/player/player_component.h"  // K08: PlayerComponent
#include "client/zone/zone_component.h"  // K09: ZoneComponent
#include "client/bridge/bridge_component.h"  // K11: BridgeComponent
#include "client/commands/commands_component.h"  // K12: CommandsComponent (commands + combat_actions)

class EverQuest
{
public:
	EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character);
	~EverQuest();

	// Poll-based network tick: drains recv, processes protocol, resends.
	// Call from main loop instead of EQ::EventLoop::Get().Process().
	void TickNetwork();

	// Replay/test seam (Batch R): dispatches a decoded application packet
	// through the same switch ZoneOnPacketRecv uses, without requiring a
	// live m_zone_connection. `opcode` is the application opcode (HC_OP_*)
	// and `data`/`size` is the payload excluding the 2-byte opcode prefix,
	// matching include/common/net/session_log.h's RecordHeader layout.
	// Any handler-triggered sends no-op safely (see m_replay_suppressed_sends).
	// B760 (#45): `reliable` = false replays the packet as an UNRELIABLE one
	// (delivered outside the reliable stream's sequence order).
	void ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size, bool reliable = true);

	// K13: the suppressed-send counter moved to eqt::PacketSender -- read it
	// as eq.Net().ReplaySuppressedSendCount().

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
	// no headless test can construct. T366 (B038 cluster 1) moved the bypass
	// flag and its setter to eqt::PacketSender, where the full rationale now
	// lives: a test arms it with eq.Net().Test_SetBypassSendGuardForSeam(true).

	// T88 test seam: SendPositionUpdate() silently no-ops (by design) when
	// the player's spawn id is still 0 -- normally only ever set from a
	// decoded PlayerProfile/ZoneSpawns entity id. Lets a test drive Move()/
	// MoveToWithPath() without first replaying a full PlayerProfile.
	// Test-only; not called from any production path.
	void Test_SetMySpawnIdForSeam(uint16_t spawn_id) { m_player_facade.SetMySpawnId(spawn_id); }
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

	// T268 test seam. K13 moved the capture itself into eqt::PacketSender
	// (it lives inside the body it captures); these three keep the EverQuest
	// receiver the 30+ existing test call sites use.
	using TestQueuedPacket = eqt::PacketSender::TestQueuedPacket;
	void Test_SetCaptureQueuedPacketsForSeam(bool capture) { m_packet_sender.Test_SetCaptureQueuedPacketsForSeam(capture); }
	const std::vector<TestQueuedPacket>& Test_GetQueuedPackets() const { return m_packet_sender.Test_GetQueuedPackets(); }
	void Test_ClearQueuedPackets() { m_packet_sender.Test_ClearQueuedPackets(); }

	// T322 test seam: the deferred logout disconnect (H03). The zone-out
	// handshake seams that stood here moved with their flags into
	// eqt::zone::ZoneComponent -- reach them through Zone().
	bool Test_IsPendingLogoutDisconnect() const { return m_pending_logout_disconnect; }

	// B1072 (#392) + B1079 (#399): the bounded zone-in (src/client/core/zone_connect.cpp).
	// Called every game tick from MovementComponent::UpdateMovement() through
	// ctx_.lifecycle.TickZoneConnect; public with an explicit clock so a test
	// can step the deadline (tests/test_zone_connect_timeout.cpp).
	void TickZoneConnect(std::chrono::steady_clock::time_point now);
	const eqt::zone::ZoneConnectWatchdog& ZoneConnectWatchdog() const { return m_zone_connect_watchdog; }
	// How many zone UDP sessions ConnectToZone/the retry opened this run
	// (the zone_connect_timeout scenario and the unit test read it).
	int ZoneConnectOpenCount() const { return m_zone_connect_opens; }
	// B1079: the last application opcode the ZONE sent (ZoneOnPacketRecv), or
	// -1 until one arrives -- the escalation's "last opcode seen" (rendered
	// by the LastZoneOpcodeName() free function beside TickZoneConnect in
	// zone_connect.cpp).
	int LastZoneOpcode() const { return m_last_zone_opcode; }
	// B1079 (#399): the per-phase deadline table (phase_index order), in
	// milliseconds -- the one place that builds it, so the two arm sites in
	// ConnectToZone and the unit test read the SAME ten rows. Defined in
	// zone_connect.cpp straight off the phase_bound constants.
	static const int* ZonePhaseBoundsMs();
	// Diagnostic lever for the scenario lane (zone_connect_timeout): the next
	// `attempts` zone connects go to port `port` on the ZoneServerInfo host
	// instead of the port world named -- a dead port, so the bound is the only
	// way out. 0 attempts = off (the default; nothing else sets it).
	void SimulateUnreachableZoneConnects(int attempts, uint16_t port) {
		m_sim_dead_zone_attempts = attempts;
		m_sim_dead_zone_port = port;
	}
	// B1079 (#399): the scenario lane's "zone answered then went silent" lever
	// (zone_connect_timeout's silent arm, the 2026-10-15 XU4 shape). Armed
	// alongside a dead-port connect (SimulateUnreachableZoneConnects), the
	// next ConnectToZone's handshake is overridden to ZONE_RECEIVING_PROFILE --
	// the phase ZoneProcessPlayerProfile sets once the zone has answered the
	// session and OP_ZoneEntry. Since the phase advances past ZONE_CONNECTING
	// ONLY on a PlayerProfile (zone_entry.cpp), that pin is the client-side
	// shape of "the zone answered the session, then went silent" (the dead
	// port supplies the silence). The bound must then walk that phase to its
	// own deadline and escalate WITHOUT a connect retry (the zone already saw
	// our OP_ZoneEntry -- a re-connect would duplicate it), landing on
	// character select. It is a one-shot flag for the next ConnectToZone: the
	// silent arm does exactly one Enter World (it is a verdict, not a
	// recovery), so the pin never needs clearing. Nothing else sets it.
	void SimulateZoneSilentAfterHandshake() { m_sim_zone_silent = true; }

	static void SetDebugLevel(int level) { s_debug_level = level; ::SetDebugLevel(level); }
	static int GetDebugLevel() { return s_debug_level; }

	// Public chat interface. K03 (Batch K) moved everything else here into
	// eqt::chat::ChatComponent; these three are output primitives with 30+
	// callers across every family and stay put until K-final.
	void AddChatSystemMessage(const std::string &text);  // Add system message to chat window
	void AddChatCombatMessage(const std::string &text, bool is_self = false);  // Add combat message (is_self for player damage)
	void AddChatMissMessage(const std::string &text);  // Add miss message (filterable separately)

	// K12 (Batch K): the commands + combat_actions family is
	// eqt::commands::CommandsComponent; callers go through Commands()
	// (T143's GetCommandRegistry() moved there too).

	// Public movement interface
	void SetHeading(float heading);
	void SetPosition(float x, float y, float z);
	void SetMoving(bool moving);
	glm::vec3 GetPosition() const;
	// K13: the zone collision map, the BSP tree and the fog/sky/zone-type
	// block moved into eqt::zone::ZoneComponent (K11 measured the shape and
	// left the move here) -- read the map as eq.Zone().GetZoneMap().
	float GetSize() const { return m_game_state.player().size(); }  // player model size (6 until the spawn says otherwise)
	// T311: the renderer-measured origin-to-feet distance of OUR model; 0 until
	// the model loads. Only for render-frame <-> wire-frame conversion --
	// include/client/position_frames.h.
	float GetPlayerCollisionZOffset() const { return m_game_state.player().collisionZOffset(); }
	// K12 (Batch K): IsStunned() (T247: true while an instantaneous OP_Stun
	// is in force; buff-borne stuns are BuffManager's) moved to
	// eqt::commands::CommandsComponent -- see Commands().
	float GetHeading() const;
	bool IsMoving() const;
	bool IsFullyZonedIn() const { return m_player_facade.IsFullyZonedIn(); }
	bool IsZoneChangeApproved() const { return m_game_state.world().zoneChangeApproved(); }

	// Loading phase tracking (covers entire pre-gameplay process).
	// T341 (B038 cluster 2): the state machine and its store are
	// eqt::bridge::BridgeComponent's -- it signals the loading thread and
	// publishes ZoneLoading. These six FORWARD: Application drives the
	// loading screen through them and ~90 test sites read this receiver.
	LoadingPhase GetLoadingPhase() const { return m_bridge_component->GetLoadingPhase(); }
	void SetLoadingPhase(LoadingPhase phase, const char* statusText = nullptr) { m_bridge_component->SetLoadingPhase(phase, statusText); }
	float GetLoadingProgress() const { return m_bridge_component->GetLoadingProgress(); }
	// GetLoadingStatusText() kept NO forwarder: zero callers here (Mechanism 5).
	bool IsGameStateReady() const { return m_bridge_component->IsGameStateReady(); }
	bool IsGraphicsReady() const { return m_bridge_component->IsGraphicsReady(); }
	void OnGameStateComplete();  // Called when game state setup finishes, triggers graphics loading
	void OnGraphicsComplete();   // Called when graphics loading finishes, enables gameplay
	void DumpEntityAppearance(uint16_t spawn_id) const;
	void DumpEntityAppearance(const std::string& name) const;

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
	// B1183 (#502): the non-const overload -- a writer into the local player's
	// own stand byte (the client's cast path, spell_manager.cpp beginCastFromGem).
	std::map<uint16_t, Entity>& GetEntities() { return m_entities; }
	uint16_t GetEntityID() const { return m_player_facade.GetEntityID(); }

	// D20c4: Quit signaling — Application checks this flag
	bool IsQuitRequested() const { return m_quit_requested; }
	void RequestQuit() { m_quit_requested = true; }
	// T341: m_last_tell_sender is eqt::chat::ChatComponent's (its only
	// writer); forwarder for the action layer (T18) and the tests.
	const std::string& GetLastTellSender() const { return m_chat->GetLastTellSender(); }
	void QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet);
	// K13: m_world_objects moved into eqt::entities::EntitiesComponent --
	// read it as eq.Entities().GetWorldObjects().
	uint32_t GetActiveTradeskillObjectId() const { return m_active_tradeskill_object_id; }

	// Group queries (Phase 7.5: read from GameState)
	bool IsInGroup() const;
	bool IsGroupLeader() const;
	int GetGroupMemberCount() const;
	const std::string& GetGroupLeaderName() const;
	const std::string& GetMyName() const { return m_character; }
	const std::string& GetMyLastName() const { return m_game_state.player().lastName(); }

	// Read-only observers of login/world state (T09). Trivial inline
	// returns of existing members -- no new storage, no logic.
	// T341: both are eqt::login_world::LoginWorldComponent's (its
	// ZoneServerInfo handler is their only writer); forwarders.
	const std::string& GetZoneServerHost() const { return m_login_world->GetZoneServerHost(); }
	uint16_t GetZoneServerPort() const { return m_login_world->GetZoneServerPort(); }

	// Pending group invite handling (Phase 7.5: read from GameState)
	bool HasPendingGroupInvite() const;
	const std::string& GetPendingInviterName() const;

	// Pet queries
	// T40: GetPetHpPercent()/GetPetButtonState() removed -- their only
	// caller was Application::syncPetState(), which is now replaced by
	// event-fed pet state via GameStateSyncBridge (PetStatsChanged /
	// PetButtonStateChanged, both published in eq_bridge.cpp /
	// eq_entities.cpp). K04 moved GetPetName()/GetPetLevel()/GetPetEntity()
	// and SendPetCommand() into GroupComponent; DismissPet() had zero callers
	// and was deleted.
	bool HasPet() const { return m_player_facade.HasPet(); }
	uint16_t GetPetSpawnId() const { return m_game_state.pet().petSpawnId(); }

	// Movement state and behavior methods

	// K13: m_water_state moved into eqt::movement::MovementComponent (its sole
	// writer). GetWaterState(), IsSwimming(), SetSwimUp(), SetSwimDown(),
	// m_swim_up and m_swim_down each had exactly ONE occurrence tree-wide --
	// their own definition -- and were DELETED (Mechanism 5, K10 correction
	// (s)); see refactor/BOARD.md.

	void StartUpdateLoop();
	void StopUpdateLoop();
	bool IsCamping() const;  // Phase 7.8: reads from GameState
	// K01 (Batch K): the guild family is eqt::guild::GuildComponent; callers go
	// through Guild(), there are no forwarders. HasPendingGuildInvite() had
	// zero callers tree-wide and was deleted rather than moved (§Mechanism 5).
	eqt::guild::GuildComponent& Guild() { return *m_guild; }
	// K02 (Batch K): the gm family is eqt::gm::GmComponent; callers go through
	// Gm(), there are no forwarders.
	eqt::gm::GmComponent& Gm() { return *m_gm; }
	// K03 (Batch K): the chat family is eqt::chat::ChatComponent; callers go
	// through Chat(), there are no forwarders.
	eqt::chat::ChatComponent& Chat() { return *m_chat; }
	// K04 (Batch K): the group family -- group, raid and pet -- is
	// eqt::group::GroupComponent. Same shape: no forwarders.
	eqt::group::GroupComponent& Group() { return *m_group; }
	// K05 (Batch K): the merchant family (inventory, scribe, book, trade, loot,
	// vendor, bank, trainer, recipes) is eqt::merchant::MerchantComponent;
	// callers go through Merchant(), there are no forwarders.
	eqt::merchant::MerchantComponent& Merchant() { return *m_merchant; }
	// K06 (Batch K): the entities family is eqt::entities::EntitiesComponent;
	// callers go through Entities(). m_entities (the spawn-id -> Entity map)
	// stays a core member, ctx_.entities -- this owns the handlers, not the map.
	eqt::entities::EntitiesComponent& Entities() { return *m_entities_component; }
	// K07 (Batch K): the movement family is eqt::movement::MovementComponent;
	// callers go through Movement().
	eqt::movement::MovementComponent& Movement() { return *m_movement; }
	// B892 (GitLab #208): the self row's AT_FLYMODE value, 0 before the row
	// exists (or after CleanupZone cleared the map). The input the buff
	// apply/fade hooks feed to Movement().SyncLevitation() -- the
	// predicate's OR half, which the official client derives from its own
	// flymode (GM #levitate and the like reach self even though the cast
	// path's AT_FLYMODE does not).
	uint8_t SelfFlymode() const {
		auto it = m_entities.find(m_player_facade.MySpawnId());
		return it == m_entities.end() ? 0 : it->second.flymode;
	}
	// K10 (Batch K): the login_world family is eqt::login_world::LoginWorldComponent;
	// callers go through LoginWorld().
	eqt::login_world::LoginWorldComponent& LoginWorld() { return *m_login_world; }
	// K08 (Batch K): the player family is eqt::player::PlayerComponent. The
	// player-state FACADE (stats, position, flags, group accessors) stayed on
	// EverQuest -- see src/client/eq_player_info.cpp and the K08 Review.
	eqt::player::PlayerComponent& Player() { return *m_player; }
	// K09 (Batch K): the zone family -- handshake, entry, the zone-stream
	// opcode switch and the zone lifecycle -- is eqt::zone::ZoneComponent.
	eqt::zone::ZoneComponent& Zone() { return *m_zone; }
	// K11 (Batch K): the bridge family -- graphics lifecycle, the intent and
	// event pumps, the loot window, config/hotbar/inspect persistence and
	// audio -- is eqt::bridge::BridgeComponent. The five lifecycle entry
	// points Application drives (setBridge, InitGraphics, ShutdownGraphics,
	// GameTick, PostRenderTick) stay on EverQuest as forwarders; everything
	// else goes through Bridge().
	eqt::bridge::BridgeComponent& Bridge() { return *m_bridge_component; }
	// K12 (Batch K): the commands + combat_actions family -- the game-registry
	// slash-command driver, its surviving Request*() wrappers, and the
	// Consider/Action/Damage/MoneyOnCorpse/Stun combat packet handlers -- is
	// eqt::commands::CommandsComponent; callers go through Commands().
	eqt::commands::CommandsComponent& Commands() { return *m_commands; }
	eqt::ClientContext& Context() { return m_ctx; }
	// K13 (Batch K): the three CONTEXT SERVICES. Components reach them as
	// ctx_.out / ctx_.self / ctx_.net; these three accessors are for
	// everything else (chiefly the tests that read the replay counter).
	eqt::ChatOutput& Out() { return m_chat_output; }
	eqt::PlayerFacade& Self() { return m_player_facade; }
	eqt::PacketSender& Net() { return m_packet_sender; }
	// K14 (Batch K): the fourth context service -- the eqstr lookup surface
	// (GetStringMessage/GetFormattedStringMessage/LoadStringFiles), over
	// EverQuest's own m_string_db. Components reach it as ctx_.strings; this
	// accessor is for everything else (the action layer, spell_cast_checks,
	// the UI, tests).
	eqt::StringDb& Strings() { return m_strings; }
	// K05 (Batch K): the Phase 4 tradeskill senders are
	// eqt::merchant::MerchantComponent's; callers go through Merchant().
	// K02 (Batch K): the GM command senders are eqt::gm::GmComponent's; callers
	// go through Gm().
	// Phase 4: Petitions
	void SendPetition(const std::string& text);
	void SetSneak(bool sneak);

	// State getters (Phase 7.8: read from GameState)
	bool IsAFK() const;
	bool IsAnonymous() const;
	bool IsRoleplay() const;

	// Additional public methods
	uint16_t GetMySpawnID() const { return m_player_facade.MySpawnId(); }
	uint16_t GetCurrentTargetId() const { return m_game_state.combat().targetId(); }

	// T133: populated from the player's own PlayerProfile packet
	// (ZoneProcessPlayerProfile, eq_zone_entry.cpp). 0 means "not in a guild" -- every
	// SendGuild*() sender in eq_guild.cpp guards on that, correctly.
	// T341: m_guild_id is eqt::guild::GuildComponent's; forwarder (~14 sites).
	uint32_t GetGuildID() const { return m_guild->GetGuildID(); }

	// T317: the player's own guild rank, in the TITANIUM encoding the wire and
	// the profile use -- 0 = member, 1 = officer, 2 = leader, 9
	// (GUILD_RANK_NONE_TI) = none/unknown. Parsed from PlayerProfile offset
	// 13055, one byte after `gm` at 13054, in ZoneProcessPlayerProfile: the
	// same once-per-login shape as m_guild_id and m_gm_mode. The server folds
	// its own 0..8 rank scale into these three values in
	// common/patches/titanium.cpp:1494 -- a Titanium client cannot see any
	// finer rank than that. See client/guild_packet_bodies.h for what the
	// guild opcodes actually do with a rank byte.
	// T341: m_guild_rank is eqt::guild::GuildComponent's; forwarder (7 sites).
	uint8_t GetGuildRank() const { return m_guild->GetGuildRank(); }

	// T167: the player's own appearance, from the SAME parse site as
	// m_guild_id and m_gm_mode above. CHECK .valid BEFORE USE -- zero is a
	// legal value for every field, so it is the only thing separating an
	// unknown appearance from a real all-zero one.
	//
	// Unlike GetGMMode() this is NOT deliberately stale: it is re-set after a
	// /appearance send, because eqemu applies the whole body and Save()s, so
	// what we sent IS what the profile holds. See client/player/profile_appearance.h.
	// T341: m_profile_appearance is eqt::zone::ZoneComponent's
	// (ZoneProcessPlayerProfile is its only writer); forwarders.
	const EQT::ProfileAppearance& GetProfileAppearance() const { return m_zone->GetProfileAppearance(); }
	void SetProfileAppearance(const EQT::ProfileAppearance& appearance) { m_zone->SetProfileAppearance(appearance); }

	// Game state access (new state management system)
	eqt::state::GameState& GetGameState() { return m_game_state; }
	const eqt::state::GameState& GetGameState() const { return m_game_state; }

	// K11 (Batch K): one of the FIVE sanctioned lifecycle forwarders. The
	// body is eqt::bridge::BridgeComponent::setBridge(); this forwards so
	// Application can wire the bridge without knowing a component exists.
	// The bridge PUMPS (ProcessBridgeIntents/ProcessBridgeEvents) are not
	// lifecycle and go through Bridge().
	void setBridge(eqt::bridge::GameStateBridge* bridge);

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

	// Friend classes for system access.
	//
	// The friend test is TWO-AXIS (K13 correction (ii), board B039): a
	// component needs friendship if it reaches an EverQuest PRIVATE member
	// through the handle (`owner_.m_x`) OR names an EverQuest-scoped private
	// name by QUALIFICATION (`EverQuest::s_debug_level`, a private static or
	// nested type) -- the second is invisible to any `owner_.` census.
	//
	// T341 censused both axes for all twelve components and removed EIGHT
	// lines (guild, chat, merchant, entities, login_world, player, commands,
	// bridge; gm went at K13). The 134 `EverQuest::s_debug_level`
	// qualifications inside the families became the PUBLIC
	// `EverQuest::GetDebugLevel()` -- same static, the K13 (jj) substitution
	// -- which is what emptied axis 2.
	//
	// T366 (B038 CLOSED) removed the last three (group, movement, zone) and
	// with them the last four `EverQuest& owner_` handles: the T88 send-guard
	// bypass moved to eqt::PacketSender, movement's function-static throttle
	// tables went back to keying on `this`, ZoneOnStatusChangeReconnectDisabled
	// moved into eqt::zone::ZoneComponent, and EQ::castRefusalMessage now takes
	// the ClientContext. NO component names EverQuest any more; the only friends
	// left are the two managers below, which are not components.
	friend class CombatManager;
	friend class TradeManager;

	// String database (all modes). K14: LoadStringFiles moved to eqt::StringDb
	// -- see Strings(). T341: GetStringDatabase() DELETED, zero callers
	// tree-wide (Mechanism 5); its two readers go through Strings().Db().

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
	void PublishFullStateSnapshot(); // U00: Push all current state through bridge for new UI
	// U07b: Standalone hotbar model
	eqt::HotbarModel* GetHotbarModel() { return m_hotbar_model.get(); }
	// Phase 7.3: Zone accessors read from GameState
	const std::string& GetCurrentZoneName() const { return m_game_state.world().zoneName(); }
	void GetTimeOfDay(uint8_t& hour, uint8_t& minute) const { hour = m_game_state.world().timeHour(); minute = m_game_state.world().timeMinute(); }

#ifdef EQT_HAS_GRAPHICS
	// Graphics renderer methods — genuinely require the renderer/loading-thread
	// lifecycle and stay gated.
	bool InitGraphics(int width, int height,
	                  EQT::Graphics::IZoneLoadSurface* renderer,
	                  eqt::bridge::GameStateBridge* bridge);
	void ShutdownGraphics();
	// T341 (cluster 1): both bodies moved with the renderer-lifetime state
	// they write; forwarders for Application, their only caller.
	void SetConstrainedConfig(const EQT::Graphics::ConstrainedRendererConfig& config) { m_bridge_component->SetConstrainedConfig(config); }
	// D20e3: GetRenderer() removed — EverQuest no longer holds a renderer pointer
	// D20e2: Application owns loading status; EverQuest signals it via this pointer
	void SetLoadingStatus(EQT::Graphics::LoadingStatus* status) { m_bridge_component->SetLoadingStatus(status); }
	// D20e2: Flag for Application to detect re-zone loading requests
	bool ConsumeZoneLoadRequest() { bool v = m_zone_load_requested; m_zone_load_requested = false; return v; }
	// U07c3: SetWindowManager removed — old UI deleted
	// D20g: SetRDPServer removed — RDP audio setup moved to Application
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

	// K14 (Batch K, refactor/K14_directory_move.md): DumpPacket and eight
	// name/price lookups (GetOpcodeName, GetChatTypeName, GetClassName,
	// GetRaceName, GetDeityName, GetBodyTypeName, GetEquipSlotName,
	// GetNpcTypeName, FormatPrice) moved to eqt::strings
	// (include/client/eq_strings.h) as free functions;
	// GetStringMessage/GetFormattedStringMessage moved to eqt::StringDb --
	// see Strings().

	// K13: the PRIVATE half of the player-state facade (PosX/PosY/PosZ/
	// HeadingDeg/SetPosLocal/SetHeadingLocal/MySpawnId/SetMySpawnId/
	// CurrentZoneName/CurrentZoneId, and the T288 banner explaining that they
	// are accessors INTO m_game_state and never a second copy) moved to
	// eqt::PlayerFacade -- include/client/player_facade.h, which carries the
	// banner verbatim. No caller outside the friend components existed, so
	// nothing forwards.

	// Phase 7.5: Group sync helper


	// Variables
	std::string m_character;

	// Debug level
	static int s_debug_level;

	// T341: m_zone_server_host/m_zone_server_port moved into
	// eqt::login_world::LoginWorldComponent, the handler that writes them;
	// ConnectToZone() below reads them back through its getters.

	// Zone server connection
	void ConnectToZone();
	// B1072: open (or re-open, dropping the old manager) the zone UDP session.
	void OpenZoneConnection(const std::string& host, uint16_t port);
	// B1072: the bound ran out twice -- tear down and go back to character select.
	void AbandonZoneConnect();
	void ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);

	// K12 (Batch K): the combat packet handlers (ZoneProcessConsider/Action/
	// Damage/MoneyOnCorpse/Stun) moved to eqt::commands::CommandsComponent;
	// DispatchZonePacket now calls owner_.Commands().ZoneProcess*() directly.

	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_zone_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_zone_connection;

	// K13: SafeQueueZonePacket() and QueuePacket()'s body, plus the
	// suppressed-send counter, are eqt::PacketSender's -- reached as
	// ctx_.net.SafeQueueZone()/Queue() inside a component and eq.Net()
	// elsewhere. SafeQueueZonePacket had no caller outside the friend
	// components, so nothing forwards for it.

	// T366 (B038 cluster 1): the T88 send-guard bypass moved to
	// eqt::PacketSender -- eq.Net().Test_SetBypassSendGuardForSeam(true) arms
	// it, ctx_.net.TestBypassSendGuard() reads it. The guard bypass belongs to
	// the thing that sends.

	// T341: m_loading_phase/m_loading_status_text moved into
	// eqt::bridge::BridgeComponent with their state machine.

	// Zone state
	bool m_zone_session_established = false;

	// Entity tracking
	std::map<uint16_t, Entity> m_entities;
	// T288: the player's own spawn id lives in m_game_state.player() and
	// nowhere else -- MySpawnId()/SetMySpawnId() above are accessors INTO
	// that store, not a copy.
	// K12: m_stunned_until (T247) moved to eqt::commands::CommandsComponent.

	// Target tracking (server-authoritative — renderer is a consumer, not the source of truth)

	// Pet tracking
	// T286: pet spawn id and button states live in m_game_state.pet().

	// String database for EQ message lookups (works in all modes)
	EQT::StringDatabase m_string_db;

	// T219: door state lives in m_game_state.doors() (the write model) --
	// the former separate EQT::DoorStateManager member was a third copy that
	// neither GameState instance ever saw (refactor/T78_two_gamestate_analysis.md item 3).

	uint32_t m_active_tradeskill_object_id = 0;  // Currently open tradeskill container (0 = none)

	// Movement and position tracking. T288: position and heading live in
	// m_game_state.player() and nowhere else -- PosX()/PosY()/PosZ()/
	// HeadingDeg() and SetPosLocal()/SetHeadingLocal() above are accessors
	// INTO that store, not a copy.

	// Movement target
	// K07: m_last_position_update_time moved into MovementComponent.
	// K15 (B031): m_last_movement_update DELETED -- zero references tree-wide.

	// Follow target

	// Combat movement

	// Last slain entity (for SimpleMessage "You have slain %1!" substitution)

	// T288: zone identity lives in m_game_state.world() and nowhere else --
	// CurrentZoneName()/CurrentZoneId() above are accessors INTO that store.

	// "Update loop" flag. Set at the first ClientUpdate (StartUpdateLoop),
	// cleared by CleanupZone / the destructor (StopUpdateLoop). It is part of
	// IsFullyZonedIn() and gates OnGameStateComplete(). T245: the 500 ms
	// std::thread that used to run behind it is GONE -- CombatManager::Update()
	// is now ticked from UpdateMovement() on the game thread (eq_movement.cpp),
	// so every GameState writer is game-thread. Nothing else ran on it.
	std::atomic<bool> m_update_running{false};
	std::thread::id m_main_thread_id;

	// R01: Session recorder (null unless --record-session was passed)
	std::unique_ptr<EQT::Net::SessionRecorder> m_session_recorder;


	// H03: server confirmed our OP_Logout (ZoneProcessLogoutReply) — DisconnectFromZone()
	// must not run synchronously from inside that packet handler (it resets
	// m_zone_connection_manager, destroying the DaybreakConnectionManager whose Tick()
	// is still on the call stack — a use-after-free). Deferred the same way as
	// m_game_state.world().zoneChangeApproved(): set here, processed at the top of UpdateMovement().
	bool m_pending_logout_disconnect = false;

	// B1072 (#392) + B1079 (#399): the zone-IN bound over the WHOLE loading
	// span, per the per-phase table ZonePhaseBoundsMs() (zone_connect.cpp) --
	// the connect phase is ZoneComponent::kZoneConnectTimeoutMs with
	// kZoneConnectRetries retries, limited to ZONE_CONNECTING -- armed by
	// ConnectToZone().
	eqt::zone::ZoneConnectWatchdog m_zone_connect_watchdog;
	int m_zone_connect_opens = 0;
	// B1079: last zone-sent application opcode (see LastZoneOpcode()).
	int m_last_zone_opcode = -1;
	int m_sim_dead_zone_attempts = 0;
	uint16_t m_sim_dead_zone_port = 0;
	// B1079 (#399): the "zone answered then went silent" lever (see
	// SimulateZoneSilentAfterHandshake). When set, ConnectToZone overrides the
	// handshake's ZONE_CONNECTING with ZONE_RECEIVING_PROFILE (the zone
	// "answered" phase). Cleared by the escalation (AbandonZoneConnect).
	bool m_sim_zone_silent = false;

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

	// Skill manager
	std::unique_ptr<EQ::SkillManager> m_skill_manager;

	// Group state
	// T286: group membership, leader, member array/count and the pending
	// invite all live in m_game_state.group() and nowhere else.

	// Pending group invite


	// Movement and position state

	// T170/T171: what we send as our inspect message. eqemu never tells a
	// Titanium client its own stored message, so this client keeps its own --
	// see SendInspectAnswer() and SaveInspectMessage().
	std::string m_inspect_message;

	// Guild state. K01: the pending-invite trio moved into GuildComponent.
	// T341 (B038 cluster 4): m_guild_id/m_guild_rank followed them there --
	// GetGuildID()/GetGuildRank() above forward. K15 (B019): m_guild_name
	// DELETED -- zero references tree-wide. Restore only alongside BOTH a
	// wire source and a reader.


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
	bool m_quit_requested = false;  // D20c4: Application checks this flag
	// U07c3: m_hotbar_window_manager removed — old UI deleted
	std::unique_ptr<eqt::HotbarModel> m_hotbar_model;  // U07b: standalone hotbar
	// D20g: m_rdp_server removed — Application owns RDP setup

	// Inventory manager
	std::unique_ptr<eqt::inventory::InventoryManager> m_inventory_manager;

	// Command registry for chat commands. K12 (Batch K): RegisterCommands()
	// (the driver), the per-domain Register*Commands() methods and the
	// runPmemDiagnostics()/runLoadDiagnostics() diagnostics helpers moved to
	// eqt::commands::CommandsComponent; called via m_commands->RegisterCommands()
	// (eq.cpp).
	std::unique_ptr<eqt::ui::CommandRegistry> m_command_registry;

	// K12 (Batch K): the player-mode vendor state (GameVendorItem struct +
	// m_vendor_items) moved to eqt::commands::CommandsComponent -- it was
	// the last family still reading it (see MerchantComponent's file
	// banner); Merchant's writers reach it through Commands().VendorItems().

	// K14: FormatPrice (format copper as "5p 3g 2s 1c") moved to
	// eqt::strings::FormatPrice -- see include/client/eq_strings.h.

	// Player mode bank state

	// K05 (Batch K): the inventory, scribe, loot, vendor, bank, trainer and
	// trade packet handlers and wire senders are
	// eqt::merchant::MerchantComponent's. DispatchZonePacket calls
	// m_merchant->ZoneProcess*() directly; everything else goes via Merchant().

public:
	// K11 (Batch K): the loot window -- its three senders, its item map and
	// its slot lookup -- is eqt::bridge::BridgeComponent's; callers go
	// through Bridge(). This one read stays because it reads GameState, not
	// the moved map. SaveEntityDataToFile() and GetLootWindowCorpseId() had
	// zero callers tree-wide and were deleted (§Mechanism 5).
	bool IsLootWindowOpen() const { return m_game_state.player().lootingCorpseId() != 0; }

	// Vendor window state (Player mode with graphics). K05: the vendor open,
	// buy, sell and close senders are on Merchant().
	bool IsVendorWindowOpen() const { return m_player_facade.IsVendorWindowOpen(); }
	uint16_t GetVendorNpcId() const { return m_player_facade.GetVendorNpcId(); }

	// T136: minimal read accessor for the migrated /vendorlist command --
	// see refactor/T136_migrate_merchant_family.md ("the state gap") for why
	// this read was exposed instead of building a vendor state domain
	// (T103's GameStateSyncBridge precedent doesn't apply: this serves
	// exactly one display command, not a cross-cutting need). K12: the
	// item-map twin, GetVendorItems(), moved to
	// eqt::commands::CommandsComponent::VendorItems() -- see Commands().
	const std::string& GetVendorName() const { return m_game_state.player().vendorName(); }
	// K14: FormatVendorPrice was a one-line alias for FormatPrice with a
	// single caller, inlined there (§Mechanism 5).

	// K12 (Batch K): RequestVendorToggle/Buy/Sell and RequestEquip moved to
	// eqt::commands::CommandsComponent -- see Commands().

	// Bank window state (Player mode with graphics). K05: Open/CloseBankWindow
	// are on Merchant().
	bool IsBankWindowOpen() const { return m_player_facade.IsBankWindowOpen(); }
	uint16_t GetBankerNpcId() const { return m_game_state.player().bankerNpcId(); }

	// Skill trainer window state (Player mode with graphics). K05:
	// RequestTrainerWindow/TrainSkill/CloseTrainerWindow are on Merchant().
	bool IsTrainerWindowOpen() const { return m_player_facade.IsTrainerWindowOpen(); }
	uint16_t GetTrainerNpcId() const { return m_game_state.player().trainerNpcId(); }

	// K12 (Batch K): RequestTrainSkill/RequestCloseTrainer/ListTrainableSkills
	// (T280), RequestWarp/RequestTimeOfDay/RequestBankToggle/RequestTraining
	// (T140), RequestReadItem/RequestScribeItem/ResolveCarriedItem (T273/
	// T276) and RequestTradeskillCombine/RequestCloseTradeskillContainer
	// (T278) all moved to eqt::commands::CommandsComponent -- see Commands().
	// ListTrainableSkills() and ResolveCarriedItem() are private there
	// (in-family callers only).

private:
#ifdef EQT_HAS_GRAPHICS
	// T341 (cluster 1): m_graphics_initialized, m_constrained_preset,
	// m_constrained_config and m_loading_status_ptr moved into
	// eqt::bridge::BridgeComponent, their only reader and writer (16 owner_.
	// uses). m_zone_load_requested stays: the re-zone handshake flag consumed
	// only from the graphics render loop, via ConsumeZoneLoadRequest() above.
	bool m_zone_load_requested = false;  // Set by re-zone, consumed by Application
#endif

#ifdef WITH_AUDIO
	// Audio manager for sound effects and music
	std::unique_ptr<EQT::Audio::AudioManager> m_audio_manager;
	// Zone audio manager for positioned sound emitters
	std::unique_ptr<EQT::Audio::ZoneAudioManager> m_zone_audio_manager;
	// D20g: SetupRDPAudio moved to Application
	// K11 (Batch K): the audio lifecycle, the day/night flag, the audio
	// config block and the sound helpers are
	// eqt::bridge::BridgeComponent's; callers go through Bridge().
	// GetZoneAudioManager(), IsDaytime(), SetUse3DAudio() and the four
	// positional/typed Play* helpers had zero callers tree-wide and were
	// deleted (§Mechanism 5).

public:
	EQT::Audio::AudioManager* GetAudioManager() { return m_audio_manager.get(); }
#endif
private:
	// K13: the three context services. Declared BEFORE m_ctx (which holds a
	// reference to each) and after every member they wrap.
	eqt::ChatOutput m_chat_output;
	eqt::PlayerFacade m_player_facade;
	eqt::PacketSender m_packet_sender;
	// K14: the fourth context service, over m_string_db (declared above).
	eqt::StringDb m_strings;
	// T341 (B038 cluster 3): the fifth context service -- bound calls to the
	// seven lifecycle entry points and references to the three lifecycle
	// flags. include/client/lifecycle_facade.h says why it binds rather than
	// holds.
	eqt::LifecycleFacade m_lifecycle;
	// K01: declared last -- every member m_ctx references is declared above.
	eqt::ClientContext m_ctx;
	std::unique_ptr<eqt::guild::GuildComponent> m_guild;
	std::unique_ptr<eqt::gm::GmComponent> m_gm;
	std::unique_ptr<eqt::chat::ChatComponent> m_chat;
	std::unique_ptr<eqt::group::GroupComponent> m_group;
	std::unique_ptr<eqt::merchant::MerchantComponent> m_merchant;
	std::unique_ptr<eqt::entities::EntitiesComponent> m_entities_component;
	std::unique_ptr<eqt::movement::MovementComponent> m_movement;
	std::unique_ptr<eqt::login_world::LoginWorldComponent> m_login_world;
	std::unique_ptr<eqt::player::PlayerComponent> m_player;
	std::unique_ptr<eqt::zone::ZoneComponent> m_zone;
	std::unique_ptr<eqt::bridge::BridgeComponent> m_bridge_component;
	std::unique_ptr<eqt::commands::CommandsComponent> m_commands;
};
