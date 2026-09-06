#include "client/eq.h"
#include "client/eq_strings.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/corpse_predicate.h"  // T185: player-corpse rename on the death path
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
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
#include <iostream>
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



// Static member definition
int EverQuest::s_debug_level = 0;

EverQuest::EverQuest(const std::string &host, int port, const std::string &user, const std::string &pass, const std::string &server, const std::string &character)
	// K01 (Batch K, refactor/K_component_extraction.md): m_ctx bundles the
	// fifteen shared-service members every extracted component receives by
	// reference. Reference members must be bound here; nothing is read
	// through them during construction.
	: m_chat_output{m_bridge},
	  m_player_facade{m_game_state, m_update_running, m_entities},
	  m_packet_sender{m_game_state, m_zone_connection, m_zone_connection_manager,
	                  m_session_recorder, m_main_thread_id},
	  // K14: the fourth context service, over EverQuest's own m_string_db
	  // (declared well above this point, so it is already constructed).
	  m_strings{m_string_db},
	  // T341 (B038 cluster 3): the fifth context service. The seven calls are
	  // bound to EverQuest's own bodies; the three flags are bound by
	  // reference under their own names. Nothing is invoked during
	  // construction.
	  m_lifecycle{[this] { RequestQuit(); },
	              [this] { TickNetwork(); },
	              [this] { StartUpdateLoop(); },
	              [this] { StopUpdateLoop(); },
	              [this] { OnGameStateComplete(); },
	              [this] { ConnectToZone(); },
	              [this](bool sneak) { SetSneak(sneak); },
	              m_update_running,
	              m_pending_logout_disconnect,
	              m_zone_session_established},
	  m_ctx{m_game_state,
	        m_bridge,
	        m_zone_connection,
	        m_entities,
	        m_character,
	        m_combat_manager,
#ifdef WITH_AUDIO
	        m_audio_manager,
#endif
	        m_trade_manager,
	        m_spell_manager,
	        m_inventory_manager,
	        m_skill_manager,
	        m_zone_connection_manager,
	        m_command_registry,
	        m_buff_manager,
	        m_active_tradeskill_object_id,
	        // K13: the three context services, plus the four shared members
	        // promoted out of the owner_.m_* residue. Declaration order in
	        // eqt::ClientContext, not this list, is what binds them.
	        m_chat_output,
	        m_player_facade,
	        m_packet_sender,
	        m_strings,
	        m_lifecycle,
	        m_hotbar_model,
	        m_eq_client_path,
	        m_session_recorder
#ifdef WITH_AUDIO
	        , m_zone_audio_manager
#endif
	        }
{
	// K01: the guild family. Constructed first so nothing below can reach a
	// null m_guild.
	m_guild = std::make_unique<eqt::guild::GuildComponent>(m_ctx);
	// K02: the gm family, same shape.
	m_gm = std::make_unique<eqt::gm::GmComponent>(m_ctx);
	// K03: the chat family, same shape.
	m_chat = std::make_unique<eqt::chat::ChatComponent>(m_ctx);
	// K04: the group family (group, raid, pet). Same rule.
	m_group = std::make_unique<eqt::group::GroupComponent>(m_ctx);
	// K05: the merchant family, same shape.
	m_merchant = std::make_unique<eqt::merchant::MerchantComponent>(m_ctx);
	// K06: the entities family, same shape.
	m_entities_component = std::make_unique<eqt::entities::EntitiesComponent>(m_ctx);
	// K07: the movement family. Same rule.
	m_movement = std::make_unique<eqt::movement::MovementComponent>(m_ctx);
	// K10: the login_world family. It owns the login/world connection managers,
	// so it must exist before SetCredentials below.
	m_login_world = std::make_unique<eqt::login_world::LoginWorldComponent>(m_ctx);
	// K08: the player family. The player-state facade stays on EverQuest.
	m_player = std::make_unique<eqt::player::PlayerComponent>(m_ctx);
	// K09: the zone family -- handshake, entry, DispatchZonePacket, lifecycle.
	m_zone = std::make_unique<eqt::zone::ZoneComponent>(m_ctx);
	// K11: the bridge family -- graphics lifecycle, the intent/event pumps,
	// the loot window, config persistence and audio.
	m_bridge_component = std::make_unique<eqt::bridge::BridgeComponent>(m_ctx);
	// K12: the commands + combat_actions family. Constructed here (with the
	// other components) even though m_command_registry itself is not
	// allocated until RegisterCommands() is called below -- ctx_.command_registry
	// is a reference to the member, not the pointee, so this is safe.
	m_commands = std::make_unique<eqt::commands::CommandsComponent>(m_ctx);

	// K13: every component now exists, so the context can carry a pointer to
	// each one. A component reaches a sibling as ctx_.Zone()/ctx_.Bridge()/...
	// from here on; nothing above this line may.
	m_ctx.SetComponents(m_guild.get(), m_gm.get(), m_chat.get(), m_group.get(),
	                    m_merchant.get(), m_entities_component.get(), m_movement.get(),
	                    m_login_world.get(), m_player.get(), m_zone.get(),
	                    m_bridge_component.get(), m_commands.get());

	// T379 (BOARD B146): the login and world streams reach eqt::PacketSender
	// the same way the zone stream does, but they can only be BOUND here --
	// LoginWorldComponent owns both connections and is constructed in this
	// body, long after m_packet_sender's initialiser list ran. The component
	// is heap-allocated and never moved, so these two addresses are stable for
	// the life of the client.
	m_packet_sender.BindLoginWorldStreams(&m_login_world->LoginConnection(),
	                                      &m_login_world->WorldConnection());

	m_login_world->SetCredentials(host, port, user, pass, server);
	m_character = character;
	m_main_thread_id = std::this_thread::get_id();
	
	// Initialize combat manager
	m_combat_manager = std::make_unique<CombatManager>(this);

	// Initialize trade manager
	m_trade_manager = std::make_unique<TradeManager>();
	m_merchant->SetupTradeManagerCallbacks();

	// Initialize spell manager
	m_spell_manager = std::make_unique<EQ::SpellManager>(this);

	// Initialize skill manager
	m_skill_manager = std::make_unique<EQ::SkillManager>(this);

	// D20b2: Skill activation feedback (game-state handler, not renderer)
	m_skill_manager->setOnSkillActivated([this](uint8_t skill_id, bool success, const std::string& message) {
		const char* skill_name = EQ::getSkillName(skill_id);
		if (success) {
			AddChatSystemMessage(fmt::format("You use {}!", skill_name));
			// Calculate haste-adjusted cooldown
			int32_t totalHaste = 0;
			if (m_inventory_manager) {
				auto equipStats = m_inventory_manager->calculateEquipmentStats();
				totalHaste += equipStats.haste;
			}
			if (m_buff_manager) {
				totalHaste += m_buff_manager->getPlayerStatMod(EQ::SpellEffect::AttackSpeed);
			}
			uint32_t adjustedCooldown = EQ::getAdjustedSkillRecastTime(skill_id, totalHaste);
			// Publish SkillActivationFeedback to bridge (for UI cooldown)
			if (m_bridge && adjustedCooldown > 0) {
				eqt::state::SkillActivationFeedbackData data;
				data.skillId = skill_id;
				data.success = true;
				data.cooldownMs = adjustedCooldown;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::SkillActivationFeedback, std::move(data)));
			}
		} else {
			AddChatSystemMessage(fmt::format("Cannot use {}: {}", skill_name, message));
		}
	});

	// D20b2: Skill-up notification (game-state handler, not renderer)
	m_skill_manager->setOnSkillUpdate([this](uint8_t skill_id, uint32_t old_value, uint32_t new_value) {
		if (new_value > old_value) {
			const char* skill_name = EQ::getSkillName(skill_id);
			AddChatSystemMessage(fmt::format("You have become better at {}! ({})", skill_name, new_value));
		}
	});

	// U07b: Create standalone hotbar model
	m_hotbar_model = std::make_unique<eqt::HotbarModel>();
	m_hotbar_model->setNameResolver([this](eqt::HotbarSlotType type, uint32_t id) -> std::string {
		if (type == eqt::HotbarSlotType::Spell && m_spell_manager) {
			const auto* spell = m_spell_manager->getSpell(id);
			return spell ? spell->name : "";
		}
		if (type == eqt::HotbarSlotType::Skill) {
			return EQ::getSkillName(static_cast<uint8_t>(id));
		}
		return "";
	});

	// S04: Create inventory manager at startup (was lazy-init in ConnectToZone/InitGraphics)
	// T41: this construction was still folded under EQT_HAS_GRAPHICS even
	// though P28 already declassified the *declaration* in eq.h ("plain
	// game state and network logic with no renderer dependency") and T29's
	// own comment right below names InventoryManager alongside
	// CommandRegistry as one of the members that guard misclassification
	// affected. Left guarded here, EQT_GRAPHICS=OFF builds never
	// constructed it at all -- not "polled the wrong way", genuinely null
	// for the process lifetime -- which would have made PublishFullStateSnapshot's
	// inventory block (un-guarded below) silently publish nothing. Same
	// declassification as T29's fix just below, applied to the member T29's
	// comment named but didn't itself touch.
	m_inventory_manager = std::make_unique<eqt::inventory::InventoryManager>();
	m_merchant->SetupInventoryCallbacks();
	// T259: TradeManager::setInventoryManager() had NO caller, so
	// TradeManager::getOwnItem() (the "You give X to Y" lines) always saw a
	// null manager and the trade-end clearing of the trade slots could not
	// reach the inventory model at all.
	if (m_trade_manager) {
		m_trade_manager->setInventoryManager(m_inventory_manager.get());
	}
	LOG_DEBUG(MOD_INVENTORY, "Inventory manager initialized");

	// T29: command registry creation was folded under EQT_HAS_GRAPHICS
	// alongside the inventory manager above, which meant a headless build
	// never registered a single slash command -- not even non-graphics ones
	// like /combat, /assist, /target. RegisterCommands() and its per-domain
	// helpers (eq_commands*.cpp) are themselves un-guarded now (see their
	// T29 notes); only the individual commands that genuinely need graphics
	// stay guarded at their own handler bodies. Create the registry
	// unconditionally so headless builds get the same vocabulary.
	m_command_registry = std::make_unique<eqt::ui::CommandRegistry>();
	m_commands->RegisterCommands();
	LOG_DEBUG(MOD_MAIN, "Command registry initialized");

	// Connection is deferred to ConnectToLogin() so that graphics initialization
	// (which can take several seconds on slow devices like Orange Pi) completes first.
	// This prevents the login server from timing out while waiting for a response.
}

void EverQuest::StartSessionRecording(const std::string& path) {
	m_session_recorder = std::make_unique<EQT::Net::SessionRecorder>();
	if (m_session_recorder->open(path, m_character, m_player_facade.CurrentZoneName())) {
		LOG_INFO(MOD_MAIN, "Session recording started: {}", path);
	} else {
		LOG_ERROR(MOD_MAIN, "Failed to start session recording to '{}'", path);
		m_session_recorder.reset();
	}
}

void EverQuest::OpenDatabase(const std::string& dbPath) {
	m_willeq_db = std::make_unique<EQT::WilleqDatabase>();
	if (m_willeq_db->open(dbPath)) {
		EQT::WilleqDatabase::setGlobalInstance(m_willeq_db.get());
		LOG_INFO(MOD_MAIN, "WilleqDatabase opened: {} (cache: {} bytes)",
				 dbPath, m_willeq_db->getMemoryUsage());
	} else {
		LOG_WARN(MOD_MAIN, "WilleqDatabase not found at {} — using legacy file loading", dbPath);
		m_willeq_db.reset();
	}
}

bool EverQuest::InitializeSpellSystem(const std::string& eqClientPath)
{
	// S04: Initialize spell database from SQLite
	if (!m_spell_manager->initialize()) {
		return false;
	}
	LOG_DEBUG(MOD_SPELL, "Spell database loaded from SQLite");

	// S04: Create buff manager (depends on spell DB)
	m_buff_manager = std::make_unique<EQ::BuffManager>(&m_spell_manager->getDatabase());
	LOG_DEBUG(MOD_SPELL, "Buff manager initialized");

	// D20b2 / T339 (board B055): vision buffs publish on LANDING as well as on
	// fade. Both hooks share ONE body, and that body's recompute is the single
	// BuffManager::computeVisionType() -- so a landing and a fade can never
	// disagree about how vision is derived from the buff list. The event
	// carries the ABSOLUTE buff vision; the racial base is the consumer's
	// (PlayerCameraComponent's) floor, applied there and never here.
	//
	// This is a game-state handler, not a renderer one.
	auto publishVisionForBuffChange = [this](uint16_t entity_id, uint32_t spell_id,
	                                         uint32_t exclude_spell_id) {
		if (entity_id != 0) return;  // Only handle player buffs
		if (!m_spell_manager || !m_buff_manager || !m_bridge) return;

		const EQ::SpellData* spell = m_spell_manager->getSpell(spell_id);
		if (!spell) return;

		bool hasVisionEffect = spell->hasEffect(EQ::SpellEffect::UltraVision) ||
		                       spell->hasEffect(EQ::SpellEffect::InfraVision);
		if (!hasVisionEffect) return;

		uint8_t finalVisionType = m_buff_manager->computeVisionType(exclude_spell_id);
		LOG_DEBUG(MOD_SPELL, "Vision buff changed (spell {}), recomputed vision = {}",
		          spell_id, finalVisionType);

		eqt::state::VisionChangedData vdata;
		vdata.visionType = finalVisionType;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VisionChanged, std::move(vdata)));
	};

	// T339: the landing hook. applyBuff()/setPlayerBuff() fire this AFTER the
	// buff is in the list, so nothing is excluded.
	m_buff_manager->setBuffApplyCallback(
		[publishVisionForBuffChange](uint16_t entity_id, uint32_t spell_id) {
			publishVisionForBuffChange(entity_id, spell_id,
			                           EQ::BuffManager::NO_EXCLUDED_SPELL);
		});

	// The fade hook. Three of the four fade paths notify BEFORE erasing, so the
	// departing spell must be excluded or it would count itself as still up.
	//
	// T425 (board B298, spell half): the same hook is where `spell_fades`
	// belongs. It is the sibling of `cast_on_you` / `cast_on_other`, the two
	// spells_us.txt messages ZoneProcessAction already prints to chat on a
	// LANDING (spell_packet_handlers.cpp); `spell_fades` is the one the
	// official client prints when the buff WEARS OFF, and it had no reader at
	// all -- a loader writing a string nobody read, which is exactly what
	// B298 catalogued. It is NOT a spell-display-window line: the window's
	// vocabulary is eqstr_us.txt's numbered formats, and this text is a
	// complete sentence shipped per spell in the spell row itself.
	// Player buffs only (entity_id 0): the fade text is written in the
	// second person ("Your strength fades") and the client has no
	// third-person form of it.
	m_buff_manager->setBuffFadeCallback(
		[this, publishVisionForBuffChange](uint16_t entity_id, uint32_t spell_id) {
			publishVisionForBuffChange(entity_id, spell_id, spell_id);
			if (entity_id != 0 || !m_spell_manager) return;
			const EQ::SpellData* faded = m_spell_manager->getSpell(spell_id);
			if (faded && !faded->spell_fades.empty()) {
				AddChatSystemMessage(faded->spell_fades);
			}
		});

	// ========================================================================
	// TOMBSTONE: the client-side spell RESOLUTION stack used to be built here.
	// Both layers of it are gone. DO NOT REBUILD EITHER ONE.
	// ========================================================================
	//
	// T324 (2026-09-02, commit ef2c3080) deleted the TARGET-RESOLUTION layer:
	// EQ::SpellTypeProcessor (include/client/spell/spell_type_processor.h,
	// src/client/spell/spell_type_processor.cpp) and its rain-spell instance
	// struct, together with the accessor on this class. It had ZERO callers:
	// the accessor was never used, its per-frame tick was never run, and its
	// entry point was never reached. It resolved single/self/PBAE/targeted-AE/
	// group/rain target sets locally.
	//
	// T327 (2026-09-02, commit 25bc9eaa) deleted the EFFECT-APPLICATION
	// layer below it:
	// EQ::SpellEffects (include/client/spell/spell_effects.h,
	// src/client/spell/spell_effects.cpp, ~1,120 lines), the EffectResult
	// struct, the accessor on this class and the member that used to be
	// constructed on this line. Its only external consumer was the class T324
	// removed, so it had zero callers as of that commit and no entry point
	// from the server-packet path at all: it published no bridge event, pushed
	// no intent, implemented no action-handler virtual, and reached this class
	// at exactly two lines -- one entity-id read and one system chat message.
	// All 49 of its per-effect handlers were logging stubs that applied
	// nothing: the heal handler did not heal, the teleport handler did not
	// teleport. The BuffManager it was handed was never dereferenced once.
	//
	// WHY, for both: the server is authoritative for spell resolution. eqemu
	// picks the targets and reports each landing individually (action, damage,
	// buff and appearance packets), and this client displays what it is told.
	// That is the same conclusion T318 reached for shrink/grow, where the
	// server already broadcasts every size change.
	//
	// WARNING if client-side effect calculation is ever wanted: the deleted
	// value-scaling table was WRONG against the server, and nothing ever
	// caught it because nothing ever called it. Every linear formula was
	// shifted by one slot against eqemu, formulas 107/108/122 are tick-based
	// decay rather than level scaling, the sign handling for spells whose max
	// lies below their base (snares, damage) was absent, and two thirds of the
	// table was missing. Transcribe it from eqemu's
	// Mob::CalcSpellEffectValue_formula (zone/spell_effects.cpp). Do NOT
	// recover it from this file's git history. Board item 45 in
	// refactor/deferred.md carries the full comparison.
	//
	// What did NOT go with either deletion, and who keeps it alive:
	//   - EQ::SpellManager and EQ::BuffManager are both LIVE (gem click, /cast,
	//     the buff window and its click-off path).
	//   - EQ::SpellDatabase, EQ::SpellEffectSlot, EQ::ResistType and the
	//     EQ::SpellEffect enum are all reached through those two and through
	//     the particle system.
	//   - the body-type targeting rule lives in
	//     include/client/spell/spell_body_type_target.h and is used by
	//     EQ::SpellManager.
	//
	// See board items 33 and 44 in refactor/deferred.md,
	// refactor/T324_delete_spell_type_processor.md,
	// refactor/T327_delete_spell_effects.md, and the Review of
	// refactor/T314_spell_targeting_correctness.md, which transcribes eqemu's
	// group-targeting rule in full should it ever be needed again.

	return true;
}

void EverQuest::TickNetwork()
{
	if (m_login_world->LoginManager()) {
		m_login_world->LoginManager()->Tick();
	}
	if (m_login_world->WorldManager()) {
		m_login_world->WorldManager()->Tick();
	}
	if (m_zone_connection_manager) {
		m_zone_connection_manager->Tick();
	}
}

EverQuest::~EverQuest()
{
	LOG_DEBUG(MOD_MAIN, "EverQuest destructor called, outputting performance report...");

	// Output final performance metrics
	std::string perfReport = EQT::PerformanceMetrics::instance().generateReport();
	if (!perfReport.empty()) {
		LOG_INFO(MOD_MAIN, "{}", perfReport);
	} else {
		LOG_WARN(MOD_MAIN, "Performance report is empty");
	}
	std::cout.flush();  // Ensure performance report is flushed before cleanup

	// Shutdown audio before other cleanup
#ifdef WITH_AUDIO
	m_bridge_component->ShutdownAudio();
#endif

	// Stop the update loop before destroying
	StopUpdateLoop();
	
	// Clean up connections properly to avoid pure virtual method calls
	if (m_zone_connection) {
		m_zone_connection.reset();
	}
	if (m_zone_connection_manager) {
		m_zone_connection_manager.reset();
	}
	if (m_login_world->LoginConnection()) {
		m_login_world->LoginConnection().reset();
	}
	if (m_login_world->LoginManager()) {
		m_login_world->LoginManager().reset();
	}
	
	// Clear movement history
	m_zone->MovementHistory().clear();
}

// T341 (B038 cluster 2): the loading-phase STATE MACHINE -- SetLoadingPhase,
// GetLoadingProgress and GetLoadingStatusText, over m_loading_phase and
// m_loading_status_text -- moved verbatim into
// eqt::bridge::BridgeComponent (src/client/bridge/bridge_component.cpp).
// EverQuest keeps six one-line forwarders in eq.h for Application and the
// tests; see the T341 Review.

void EverQuest::OnGameStateComplete() {
	LOG_INFO(MOD_MAIN, "[LOADING] Game state setup complete. Spawn ID: {}, Zone: {}",
	         m_player_facade.MySpawnId(), m_player_facade.CurrentZoneName());

#ifdef EQT_HAS_GRAPHICS
	if (m_bridge_component->IsGraphicsInitialized()) {
		SetLoadingPhase(LoadingPhase::GRAPHICS_LOADING_ZONE, "Entering world...");

		// Signal Application to create snapshot and start zone loading.
		// Application's game thread loop picks this up, creates the snapshot
		// (with full zone data), then starts the loading thread.
		// PublishFullStateSnapshot() is NOT called here -- graphics mode
		// publishes it later from OnGraphicsComplete(), once, after zone
		// graphics finish loading (T41: do not add a second call site here,
		// that would double-publish for every graphics-mode zone-in).
		m_zone_load_requested = true;
		LOG_INFO(MOD_GRAPHICS, "OnGameStateComplete: zone load requested");
	} else {
		// No graphics mode - we're done
		SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
		if (m_bridge) {
			eqt::state::ZoneLoadedData data;
			data.zoneName = m_player_facade.CurrentZoneName();
			data.zoneId = m_player_facade.CurrentZoneId();
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::ZoneLoaded, std::move(data)));
		}
		// T41: OnGameStateComplete() is the correct GAME-lifecycle
		// zone-in/state-ready signal (eq_zone.cpp's re-zone comment calls it
		// "the single signal point ... which only fires once the new zone's
		// NewZone packet has actually been received"). Graphics mode reaches
		// PublishFullStateSnapshot() via OnGraphicsComplete() instead (a
		// GRAPHICS lifecycle hook that only exists because zone graphics
		// take time to build); this is the equivalent trigger for the run
		// that has no graphics phase to wait on, so the snapshot fires
		// exactly once per zone-in here too instead of never firing at all.
		PublishFullStateSnapshot();
	}
#else
	// No graphics mode - we're done
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = m_player_facade.CurrentZoneName();
		data.zoneId = m_player_facade.CurrentZoneId();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoaded, std::move(data)));
	}
	// T41: see the twin branch above (EQT_HAS_GRAPHICS defined, graphics not
	// initialized) -- same reasoning, this is the EQT_GRAPHICS=OFF configure's
	// copy of the same headless path.
	PublishFullStateSnapshot();
#endif
}

void EverQuest::OnGraphicsComplete() {
	SetLoadingPhase(LoadingPhase::COMPLETE, "Ready!");
	LOG_INFO(MOD_MAIN, "[LOADING] Graphics loading complete. Game ready!");

	// ZoneLoaded bridge event drives setZoneReady(true) + hideLoadingScreen() via bridge handler
	if (m_bridge) {
		eqt::state::ZoneLoadedData data;
		data.zoneName = m_player_facade.CurrentZoneName();
		data.zoneId = m_player_facade.CurrentZoneId();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ZoneLoaded, std::move(data)));
	}

	// U00: Publish full state snapshot so new UI panels have data on first frame
	PublishFullStateSnapshot();
}

void EverQuest::PublishFullStateSnapshot() {
	if (!m_bridge) return;

	LOG_INFO(MOD_MAIN, "Publishing full state snapshot to bridge");

	// --- Player stats ---
	{
		eqt::state::PlayerStatsChangedData data;
		data.curHP = m_game_state.player().curHP();
		data.maxHP = m_game_state.player().maxHP();
		data.curMana = m_game_state.player().curMana();
		data.maxMana = m_game_state.player().maxMana();
		data.curEndurance = m_game_state.player().curEndurance();
		data.maxEndurance = m_game_state.player().maxEndurance();
		data.level = m_game_state.player().level();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PlayerStatsChanged, std::move(data)));
	}

	// --- Character info ---
	{
		eqt::state::CharacterInfoChangedData data;
		data.name = m_character;
		data.level = m_game_state.player().level();
		data.className = eqt::strings::GetClassName(m_game_state.player().classId());
		data.deity = eqt::strings::GetDeityName(m_game_state.player().deity());
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CharacterInfoChanged, std::move(data)));
	}

	// --- XP progress ---
	{
		// EQ XP is 0-330 for current level (330 = level up)
		float progress = (m_zone->Exp() > 0) ? static_cast<float>(m_zone->Exp() % 330) / 330.0f : 0.0f;
		eqt::state::ExpProgressChangedData data;
		data.progress = progress;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::ExpProgressChanged, std::move(data)));
	}

	// --- Currency ---
	{
		eqt::state::CurrencyChangedData data;
		data.platinum = m_game_state.player().platinum();
		data.gold = m_game_state.player().gold();
		data.silver = m_game_state.player().silver();
		data.copper = m_game_state.player().copper();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(data)));
	}

	// --- Spell gems ---
	if (m_spell_manager) {
		for (uint8_t i = 0; i < EQ::MAX_SPELL_GEMS; ++i) {
			uint32_t spellId = m_spell_manager->getMemorizedSpell(i);
			eqt::state::SpellGemChangedData data;
			data.gemSlot = i;
			data.spellId = spellId;
			data.gemState = static_cast<uint8_t>(m_spell_manager->getGemState(i));
			data.cooldownRemainingMs = m_spell_manager->getGemCooldownRemaining(i);
			data.cooldownTotalMs = 0;
			if (spellId != EQ::SPELL_UNKNOWN) {
				const auto* spell = m_spell_manager->getSpell(spellId);
				data.spellName = spell ? spell->name : "";
				data.iconId = spell ? spell->gem_icon : 0;
			}
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::SpellGemChanged, std::move(data)));
		}
	}

	// --- Spellbook snapshot ---
	if (m_spell_manager) {
		eqt::state::SpellbookSnapshotData sbdata;
		for (uint16_t slot = 0; slot < EQ::MAX_SPELLBOOK_SLOTS; ++slot) {
			uint32_t spellId = m_spell_manager->getSpellbookSpell(slot);
			if (spellId == 0 || spellId == EQ::SPELL_UNKNOWN) continue;
			const auto* spell = m_spell_manager->getSpell(spellId);
			eqt::state::SpellbookEntry entry;
			entry.slot = slot;
			entry.spellId = spellId;
			entry.name = spell ? spell->name : "";
			entry.iconId = spell ? spell->spell_icon : 0;
			entry.level = (spell && m_game_state.player().classId() > 0) ? spell->class_levels[m_game_state.player().classId() - 1] : 0;
			sbdata.spells.push_back(std::move(entry));
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SpellbookSnapshot, std::move(sbdata)));
	}

	// --- Skills snapshot ---
	if (m_skill_manager) {
		eqt::state::SkillsSnapshotData skdata;
		auto allSkills = m_skill_manager->getAllSkills();
		for (const auto* skill : allSkills) {
			if (!skill || skill->current_value == 0) continue;
			eqt::state::SkillDisplayEntry entry;
			entry.skillId = skill->skill_id;
			entry.name = skill->name;
			entry.value = skill->current_value;
			entry.maxValue = skill->max_value;
			skdata.skills.push_back(std::move(entry));
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::SkillsSnapshot, std::move(skdata)));
	}

	// --- Buffs ---
	if (m_buff_manager) {
		const auto& buffs = m_buff_manager->getPlayerBuffs();
		for (size_t i = 0; i < buffs.size(); ++i) {
			const auto& buff = buffs[i];
			if (buff.spell_id == 0 || buff.spell_id == EQ::SPELL_UNKNOWN) continue;
			eqt::state::BuffUpdatedData data;
			data.slot = (buff.slot >= 0) ? static_cast<uint8_t>(buff.slot) : static_cast<uint8_t>(i);
			data.spellId = buff.spell_id;
			data.ticksLeft = buff.remaining_seconds / 6;  // Convert seconds to ticks (6s per tick)
			data.casterName = "";  // Caster name not stored in ActiveBuff
			if (m_spell_manager) {
				const auto* spell = m_spell_manager->getSpell(buff.spell_id);
				data.spellName = spell ? spell->name : "";
				data.iconId = spell ? spell->spell_icon : 0;
			}
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::BuffUpdated, std::move(data)));
		}
	}

	// --- Inventory ---
	// T41: was #ifdef EQT_HAS_GRAPHICS. m_inventory_manager (per P28's
	// comment at the top of this file) is a renderer-free data model used
	// unconditionally elsewhere in this TU; nothing in this block touches
	// the renderer. Un-guarding it is what makes the headless configure's
	// snapshot (see OnGameStateComplete() above) actually publish inventory
	// data instead of silently compiling out.
	if (m_inventory_manager) {
		// Equipment slots (0-21) + general inventory (22-29)
		for (int16_t slot = 0; slot < 30; ++slot) {
			const auto* item = m_inventory_manager->getItem(slot);
			eqt::state::InventorySlotChangedData data;
			data.slotId = slot;
			data.hasItem = (item != nullptr);
			m_merchant->SyncInventorySlotToGameState(slot, data.hasItem);  // T221: zone-in parity
			data.itemId = item ? item->itemId : 0;
			data.itemName = item ? item->name : "";
			data.iconId = item ? item->icon : 0;
			data.quantity = item ? item->quantity : 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
		}
	}

	// --- Group ---
	if (m_game_state.group().inGroup()) {
		const auto& group = m_game_state.group();
		eqt::state::GroupChangedData data;
		data.inGroup = true;
		data.isLeader = group.isLeader();
		data.leaderName = group.leaderName();
		data.memberCount = 0;
		for (int i = 0; i < 5; ++i) {
			const GroupMember* gm = group.getMember(i);
			if (gm && !gm->name.empty()) data.memberCount++;
		}
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::GroupChanged, std::move(data)));

		for (int i = 0; i < 5; ++i) {
			const GroupMember* gm = group.getMember(i);
			if (!gm || gm->name.empty()) continue;
			eqt::state::GroupMemberUpdatedData mdata;
			mdata.memberIndex = i;
			mdata.name = gm->name;
			mdata.level = gm->level;
			mdata.classId = gm->classId;
			mdata.hpPercent = gm->hpPercent;
			mdata.inZone = gm->inZone;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::GroupMemberUpdated, std::move(mdata)));
		}
	}

	// --- Pet ---
	if (m_game_state.pet().petSpawnId() != 0) {
		eqt::state::PetCreatedData data;
		data.spawnId = m_game_state.pet().petSpawnId();
		data.name = m_group->GetPetName();
		data.level = m_group->GetPetLevel();
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::PetCreated, std::move(data)));
	}

	LOG_INFO(MOD_MAIN, "State snapshot published");
}


// Zone server connection functions
void EverQuest::ConnectToZone()
{
	LOG_DEBUG(MOD_ZONE, "Connecting to zone server at {}:{}", m_login_world->GetZoneServerHost(), m_login_world->GetZoneServerPort());

	SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);

	// S04: inventory_manager, spell DB and buff_manager are all
	// pre-allocated in EverQuest constructor / Application::initialize()

	EQ::Net::DaybreakConnectionManagerOptions zone_opts;
	auto transport = std::make_unique<EQ::Net::PosixUdpTransport>();
	m_zone_connection_manager.reset(new EQ::Net::DaybreakConnectionManager(zone_opts, std::move(transport)));
	m_zone_connection_manager->OnNewConnection(std::bind(&EverQuest::ZoneOnNewConnection, this, std::placeholders::_1));
	m_zone_connection_manager->OnConnectionStateChange(std::bind(&EverQuest::ZoneOnStatusChangeReconnectEnabled, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_zone_connection_manager->OnPacketRecv(std::bind(&EverQuest::ZoneOnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));
	m_zone_connection_manager->Connect(m_login_world->GetZoneServerHost(), m_login_world->GetZoneServerPort());
}

void EverQuest::ZoneOnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_zone_connection = connection;
	LOG_DEBUG(MOD_ZONE, "Zone connection created");
}

void EverQuest::ZoneOnStatusChangeReconnectEnabled(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LOG_DEBUG(MOD_ZONE, "Zone connected");
		// T286: this write had NO WorldState counterpart, so world()
		// .isZoneConnected() never became true on the reconnect path.
		m_game_state.world().setZoneConnected(true);
		// T322: the destination zone session is up, so the zone-to-zone
		// transition is over -- the next world login is a fresh one unless
		// ProcessDeferredZoneChange latches it again.
		m_zone->ZoneToZoneInProgress() = false;
		// Send stream identify immediately after connection
		m_zone->ZoneSendStreamIdentify();
		m_zone_session_established = true;

		// Send ack packet and zone entry immediately after session established
		m_zone->ZoneSendAckPacket();
		m_zone->ZoneSendZoneEntry();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LOG_INFO(MOD_ZONE, "Zone connection lost, reconnecting.");
		m_game_state.world().setZoneConnected(false);  // T286: only store
		m_zone_session_established = false;
		m_zone->ZoneEntrySent() = false;
		m_zone_connection.reset();
		SetLoadingPhase(LoadingPhase::ZONE_CONNECTING);
		m_zone_connection_manager->Connect(m_login_world->GetZoneServerHost(), m_login_world->GetZoneServerPort());
	}
}

void EverQuest::ZoneOnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet & p)
{
	auto opcode = p.GetUInt16(0);

	LOG_TRACE(MOD_ZONE, "[ZONE RECV] opcode=0x{:04x} len={}", opcode, p.Length());

	eqt::strings::DumpPacket("S->C", opcode, p);

	// R01: record real network traffic only (replayed packets enter via
	// ReplayZonePacket -> DispatchZonePacket and are never re-recorded)
	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ServerToClient,
			opcode, (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	m_zone->DispatchZonePacket(p);
}

// Batch R replay/test seam: build a synthetic app packet (2-byte opcode
// prefix + payload, matching session_log.h's on-disk layout) and run it
// through the same opcode switch ZoneOnPacketRecv uses.
void EverQuest::ReplayZonePacket(uint16_t opcode, const uint8_t* data, size_t size)
{
	EQ::Net::DynamicPacket p;
	p.Resize(2 + size);
	p.PutUInt16(0, opcode);
	if (size > 0 && data != nullptr) {
		memcpy(static_cast<char*>(p.Data()) + 2, data, size);
	}
	m_zone->DispatchZonePacket(p);
}

// K13: SafeQueueZonePacket's body is now eqt::PacketSender::SafeQueueZone
// (src/client/packet_sender.cpp), moved verbatim. It had no caller outside the
// friend components, so no forwarder stands here.


void EverQuest::StartUpdateLoop()
{
	// T245: no thread any more. This used to spawn a 500 ms std::thread whose
	// only remaining job (after P36 moved UpdateMovement() off it) was
	// CombatManager::Update() -- an off-game-thread writer of m_game_state
	// (combat().setTarget/clearTarget via CheckForAggro / ProcessCombatRound /
	// FindNextHuntTarget whenever /combat on; T227 STOP note, TSAN-proven in
	// review/T245/). Update() is now ticked from UpdateMovement() on the game
	// thread and paces itself at the same 500 ms (CombatManager::kUpdateIntervalMs).
	// The flag stays: IsFullyZonedIn() and OnGameStateComplete()'s once-only
	// gate read it, and the P36 self-join hazard (CleanupZone -> StopUpdateLoop
	// -> join() from the thread being joined) is gone with the thread.
	if (m_update_running) {
		return;  // Already running
	}
	m_update_running = true;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Update loop started (game-thread tick)");
	}
}

void EverQuest::StopUpdateLoop()
{
	if (!m_update_running) {
		return;
	}
	m_update_running = false;
	if (s_debug_level >= 2) {
		LOG_DEBUG(MOD_MAIN, "Update loop stopped");
	}
}






// ============================================================================
// Phase 3: Corpse Management
// ============================================================================


// ============================================================================
// Phase 3: Combat Targeting
// ============================================================================


// ============================================================================
// Phase 3: Misc
// ============================================================================



// ============================================================================
// Phase 4: Cosmetic
// ============================================================================




// ============================================================================
// Phase 4: Petitions
// ============================================================================

void EverQuest::SendPetition(const std::string& text)
{
	// T135: PutCString() not PutString() -- eqemu's Handle_OP_Petition reads
	// this as a NUL-terminated C string; PutString() wrote none, so the
	// server's read ran into adjacent heap memory (confirmed live via a
	// corrupted peq.petitions row). Pre-existing bug, not a T135 regression
	// -- see refactor/deferred.md's T135 entry.
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_Petition);
	packet.PutCString(2, text.c_str());

	if (m_zone_connection && m_game_state.world().isZoneConnected()) {
		m_packet_sender.SafeQueueZone(packet);
		LOG_INFO(MOD_MAIN, "Petition submitted");
		AddChatSystemMessage("Your petition has been submitted.");
	}
}

void EverQuest::SetSneak(bool sneak)
{
	if (m_game_state.player().isSneaking() == sneak) {
		return;
	}

	m_game_state.player().setSneaking(sneak);  // Phase 7.8 / T286: only store
	m_entities_component->SendSpawnAppearance(AT_SNEAK, sneak ? 1 : 0);

	if (s_debug_level >= 1) {
		LOG_DEBUG(MOD_MAIN, "Sneak status: {}", sneak ? "ON" : "OFF");
	}
}


// K13: one of the sanctioned forwarders. The body is
// eqt::PacketSender::Queue(); this stays because 17 NON-component call sites
// (combat.cpp 11, spell_manager.cpp 3, skill_manager.cpp 3) send through the
// EverQuest receiver and are not EverQuest-family code.
void EverQuest::QueuePacket(uint16_t opcode, EQ::Net::DynamicPacket* packet)
{
	m_packet_sender.Queue(opcode, packet);
}


// P28: SetEQClientPath is plain game config (EQ install path, string-file
// loading) with no renderer dependency — un-guarded to match its
// unconditional eq.h declaration. SetConstrainedConfig genuinely needs the
// renderer-only ConstrainedRendererConfig/ConstrainedRenderingPreset types
// and stays gated.
void EverQuest::SetEQClientPath(const std::string& path) {
	m_eq_client_path = path;

	// Load EQ string files for message lookups
	m_strings.LoadStringFiles(path);
}

// T341 (B038 cluster 1): SetConstrainedConfig's body moved verbatim into
// eqt::bridge::BridgeComponent with the renderer-lifetime state it writes;
// EverQuest keeps the forwarder in eq.h for Application::init().

// K11 (Batch K, refactor/K11_bridge_component.md): the FIVE sanctioned
// forwarders of this batch. Every other moved method is reached through
// Bridge(); these five are EverQuest's LIFECYCLE entry points -- Application
// wires the bridge, starts graphics, ticks the game and stops it through
// them, and must not have to know a component exists to do that. The bodies
// live in src/client/bridge/bridge_component*.cpp. K13 decides whether they
// survive.
void EverQuest::setBridge(eqt::bridge::GameStateBridge* bridge) {
	m_bridge_component->setBridge(bridge);
}

void EverQuest::GameTick(float deltaTime) {
	m_bridge_component->GameTick(deltaTime);
}

void EverQuest::PostRenderTick(float deltaTime) {
	m_bridge_component->PostRenderTick(deltaTime);
}

#ifdef EQT_HAS_GRAPHICS
bool EverQuest::InitGraphics(int width, int height,
                             EQT::Graphics::IZoneLoadSurface* renderer,
                             eqt::bridge::GameStateBridge* bridge) {
	return m_bridge_component->InitGraphics(width, height, renderer, bridge);
}

void EverQuest::ShutdownGraphics() {
	m_bridge_component->ShutdownGraphics();
}
#endif // EQT_HAS_GRAPHICS

// K14 (Batch K, refactor/K14_directory_move.md): DumpEntityAppearance moved
// here from eq_strings.cpp verbatim. The plan's eq_strings entry grouped it
// with DumpPacket as "debug helpers with s_debug_level reads", but the body
// reads m_entities -- EverQuest's own entity map -- and has no
// s_debug_level read at all, so it is not a pure lookup and does not belong
// in eqt::strings. It stays on EverQuest, next to the state it dumps; the
// five name-lookup calls inside it now reach eqt::strings (see this unit's
// Review, deviation 1).
void EverQuest::DumpEntityAppearance(const std::string& name) const {
	// Find entity by name (case-insensitive, underscore-tolerant)
	std::string search_lower = name;
	std::transform(search_lower.begin(), search_lower.end(), search_lower.begin(), ::tolower);
	std::replace(search_lower.begin(), search_lower.end(), ' ', '_');

	for (const auto& [id, entity] : m_entities) {
		std::string entity_name_lower = entity.name;
		std::transform(entity_name_lower.begin(), entity_name_lower.end(), entity_name_lower.begin(), ::tolower);
		if (entity_name_lower.find(search_lower) != std::string::npos) {
			DumpEntityAppearance(id);
			return;
		}
	}
	std::cout << fmt::format("Entity '{}' not found", name) << std::endl;
}

void EverQuest::DumpEntityAppearance(uint16_t spawn_id) const {
	auto it = m_entities.find(spawn_id);
	if (it == m_entities.end()) {
		std::cout << fmt::format("Entity with spawn_id {} not found", spawn_id) << std::endl;
		return;
	}

	const Entity& e = it->second;

	std::cout << "========================================" << std::endl;
	std::cout << fmt::format("Entity Appearance Dump: {} (ID: {})", e.name, e.spawn_id) << std::endl;
	std::cout << "========================================" << std::endl;

	// Basic info
	std::cout << fmt::format("Type:      {} ({})", eqt::strings::GetNpcTypeName(e.npc_type), e.npc_type) << std::endl;
	std::cout << fmt::format("Race:      {} (ID: {})", eqt::strings::GetRaceName(e.race_id), e.race_id) << std::endl;
	std::cout << fmt::format("Class:     {} (ID: {})", eqt::strings::GetClassName(e.class_id), e.class_id) << std::endl;
	std::cout << fmt::format("Gender:    {} ({})", e.gender == 0 ? "Male" : (e.gender == 1 ? "Female" : "Neutral"), e.gender) << std::endl;
	std::cout << fmt::format("Level:     {}", e.level) << std::endl;
	std::cout << fmt::format("Body Type: {} (ID: {})", eqt::strings::GetBodyTypeName(e.bodytype), e.bodytype) << std::endl;
	std::cout << fmt::format("Size:      {:.2f}", e.size) << std::endl;
	std::cout << fmt::format("Light:     {}", e.light) << std::endl;

	// Appearance details
	std::cout << std::endl << "--- Facial Features ---" << std::endl;
	std::cout << fmt::format("Face:       {}", e.face) << std::endl;
	std::cout << fmt::format("Hair Color: {}", e.haircolor) << std::endl;
	std::cout << fmt::format("Hair Style: {}", e.hairstyle) << std::endl;
	std::cout << fmt::format("Beard:      {}", e.beard) << std::endl;
	std::cout << fmt::format("Beard Color:{}", e.beardcolor) << std::endl;

	// Equipment textures
	std::cout << std::endl << "--- Equipment Textures (Material IDs) ---" << std::endl;
	std::cout << fmt::format("Helm Texture:   {} (Show: {})", e.helm, e.showhelm ? "Yes" : "No") << std::endl;
	std::cout << fmt::format("Chest Texture2: {} (equip_chest2 / mount color)", e.equip_chest2) << std::endl;

	for (int i = 0; i < 9; i++) {
		uint32_t mat = e.equipment[i];
		uint32_t tint = e.equipment_tint[i];

		// Parse tint components (BGRA format)
		uint8_t tint_blue = (tint >> 0) & 0xFF;
		uint8_t tint_green = (tint >> 8) & 0xFF;
		uint8_t tint_red = (tint >> 16) & 0xFF;
		uint8_t tint_use = (tint >> 24) & 0xFF;

		std::string slot_name = eqt::strings::GetEquipSlotName(i);

		if (mat != 0 || tint != 0) {
			std::cout << fmt::format("  [{:>9}] Material: {:>5}", slot_name, mat);
			if (tint != 0) {
				std::cout << fmt::format("  Tint: RGB({:>3},{:>3},{:>3}) Use: 0x{:02X}",
					tint_red, tint_green, tint_blue, tint_use);
			}
			std::cout << std::endl;
		} else {
			std::cout << fmt::format("  [{:>9}] (none)", slot_name) << std::endl;
		}
	}

	// Position
	std::cout << std::endl << "--- Position ---" << std::endl;
	std::cout << fmt::format("Location: ({:.2f}, {:.2f}, {:.2f}) Heading: {:.1f}",
		e.x, e.y, e.z, e.heading) << std::endl;
	std::cout << fmt::format("HP:       {}%", e.hp_percent) << std::endl;

	std::cout << "========================================" << std::endl;
}
