// EverQuest slash-command registration.
//
// Split out of eq.cpp (Batch F, unit F01): originally held RegisterCommands()
// — the ~2,300-line cluster of lambda-based /slash command registrations —
// plus the two manual zone-load diagnostics helpers it depends on
// (runPmemDiagnostics, runLoadDiagnostics). Moved verbatim, no behavior
// change. See refactor/F01_commands.md.
//
// Batch G (unit G02): RegisterCommands() split into a short driver plus
// per-domain Register*Commands() methods across 3 TUs (this file holds the
// driver, diagnostics helpers, and RegisterSocialCommands(); the remaining
// domains are in eq_commands_movement.cpp and eq_commands_spells.cpp). Lambda
// bodies moved verbatim; registration order preserved exactly. See
// refactor/G02_commands_split.md.

#include "client/eq.h"
#include "client/willeq_database.h"
#include "client/pathfinder_interface.h"
#ifdef EQT_HAS_NAVMESH
#include "client/pathfinder_nav_mesh.h"
#endif
#include "client/hc_map.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/world_object.h"
#include "client/spell/spell_manager.h"
#include "client/spell/buff_manager.h"
#include "client/spell/spell_effects.h"
#include "client/spell/spell_type_processor.h"
#include "client/skill/skill_manager.h"
#include "client/zone_lines.h"
#include "client/formatted_message.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"

// T29: plain data-model headers, no renderer dependency (P28 declassified
// them; see eq_chat.cpp's identical note) -- needed unconditionally now
// that RegisterCommands()/RegisterSocialCommands() are no longer
// graphics-only (below).
#include "client/chat/chat_message_buffer.h"
#include "client/graphics/ui/command_registry.h"
#include "client/hotbar/hotbar_model.h"
// T66: inventory_manager.h/inventory_constants.h -- same "plain data-model
// header" status as the T29 group above (verified against eq.cpp's own
// unconditional include of both): no graphics dependency, needed
// unconditionally for RequestEquip() (T141: the former
// RegisterEquipmentCommands()'s /equip handler, still defined in this file)
// since SendMoveItem is plain network logic, not graphics-gated.
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/loading_thread.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/weather_effects_controller.h"
#include "client/graphics/weather_config_loader.h"
#include "client/graphics/weather_quality_preset.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/inventory/item_instance.h"
#include "client/input/hotkey_manager.h"
#include "common/util/json_config.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"
#include "client/graphics/detail/seasonal_controller.h"
#include "client/graphics/environment/spell_effects_config.h"
#endif

#include "client/bridge/game_state_bridge.h"
#include "client/bridge/irrlicht_bridge.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_callback_probe.h"
#include "client/audio/midi_voice_probe.h"
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
#include <cctype>
#include <iomanip>
#include <limits>
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
#include <malloc.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// T29: un-guarded from EQT_HAS_GRAPHICS -- see the identical note in
// eq_commands_movement.cpp. This file has no direct graphics-API usage at
// all (verified by grep for IrrlichtRenderer/LoadingThread/Weather*/
// Detail*/Season*/Wld*/HotkeyManager); every renderer-only command already
// routed through the RendererCommand bridge queue via the RegisterMovement/
// RegisterSpells rendererCmd() helpers in their own files.

// ── Manual zone load diagnostics helpers ────────────────────────────────────

void EverQuest::runPmemDiagnostics(const std::string& label) {
	if (!m_bridge) return;

	eqt::state::DiagnosticsMemoryReportData d;
	d.label = label;

#ifdef __linux__
	{
		FILE* f = fopen("/proc/self/statm", "r");
		if (f) {
			unsigned long vm = 0, rss = 0;
			if (fscanf(f, "%lu %lu", &vm, &rss) == 2) {
				long ps = sysconf(_SC_PAGESIZE);
				d.processRssBytes = static_cast<size_t>(rss) * ps;
				d.processVmBytes = static_cast<size_t>(vm) * ps;
			}
			fclose(f);
		}
	}

	{
		FILE* f = fopen("/proc/self/smaps_rollup", "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				unsigned long kb = 0;
				if (sscanf(line, "Shared_Clean: %lu kB", &kb) == 1 ||
				    sscanf(line, "Shared_Dirty: %lu kB", &kb) == 1) {
					d.sharedLibBytes += static_cast<size_t>(kb) * 1024;
				} else if (sscanf(line, "Anonymous: %lu kB", &kb) == 1) {
					d.anonBytes = static_cast<size_t>(kb) * 1024;
				}
			}
			fclose(f);
		}
		f = fopen("/proc/self/status", "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof(line), f)) {
				unsigned long kb = 0;
				if (sscanf(line, "VmStk: %lu kB", &kb) == 1) {
					d.stackBytes = static_cast<size_t>(kb) * 1024;
					break;
				}
			}
			fclose(f);
		}
	}

	// T127: glibc allocator bookkeeping (arena committed vs. actually in
	// use) -- see the field comments in event_bus.h. mallinfo2() is a plain
	// aggregate query, no code-change/instrumentation needed, same
	// zero-distortion property as the rest of this diagnostic.
	{
		struct mallinfo2 mi = mallinfo2();
		d.mallocArenaBytes = static_cast<size_t>(mi.arena) + static_cast<size_t>(mi.hblkhd);
		d.mallocInUseBytes = static_cast<size_t>(mi.uordblks);
	}
#endif

#ifdef WITH_AUDIO
	if (m_audio_manager && m_audio_manager->isInitialized()) {
		d.audioAvailable = true;
		d.soundBufferCacheBytes = m_audio_manager->getSoundBufferCacheBytes();
		d.soundBufferCacheMaxBytes = m_audio_manager->getSoundBufferCacheMaxBytes();
		d.soundFontEstimateBytes = m_audio_manager->getSoundFontMemoryEstimate();
		d.musicDecodedBytes = m_audio_manager->getMusicDecodedBytes();
		d.audioPfsArchiveBytes = m_audio_manager->getPfsArchiveCacheBytes();
		if (auto* sfx = m_audio_manager->getSfxManager()) {
			d.sfxCacheBytes = sfx->getCacheSize();
		}
	}
	if (m_zone_audio_manager) {
		d.zoneEmitterCount = m_zone_audio_manager->getEmitterCount();
		d.activeEmitterCount = m_zone_audio_manager->getActiveEmitterCount();
	}
	// T130: real-time audio callback timing (see event_bus.h field comments).
	// Read here on the game thread from atomics the audio thread only ever
	// writes -- same lock-free single-reader-of-record pattern as every
	// other /pmem external-input field.
	{
		using EQT::Audio::AudioCallbackProbe;
		d.audioCallbackCount = AudioCallbackProbe::callbackCount.load(std::memory_order_relaxed);
		d.audioLateCallbackCount = AudioCallbackProbe::lateCallbackCount.load(std::memory_order_relaxed);
		d.audioWorstCallbackDeltaUs = AudioCallbackProbe::worstDeltaNs.load(std::memory_order_relaxed) / 1000;
	}
	// T131: peak concurrent TSF voices / dropped-note count (see event_bus.h
	// field comments). Same lock-free single-reader-of-record pattern.
	{
		using EQT::Audio::MidiVoiceProbe;
		d.audioPeakVoices = MidiVoiceProbe::peakVoicesSeen.load(std::memory_order_relaxed);
		d.audioNoteOnFailures = MidiVoiceProbe::noteOnFailures.load(std::memory_order_relaxed);
	}
#endif

	auto addConn = [&](const std::string& name, std::shared_ptr<EQ::Net::DaybreakConnection> conn) {
		if (!conn) return;
		auto stats = conn->GetStats();
		eqt::state::DiagnosticsMemoryReportData::ConnectionInfo ci;
		ci.name = name;
		ci.recvBytes = stats.recv_bytes;
		ci.sentBytes = stats.sent_bytes;
		ci.avgPing = conn->GetRollingPing();
		d.connections.push_back(std::move(ci));
	};
	addConn("Zone", m_zone_connection);
	addConn("World", m_world_connection);
	addConn("Login", m_login_connection);

	d.entityCount = m_entities.size();
	d.entityEstimateBytes = m_entities.size() * sizeof(Entity);
	d.doorCount = m_game_state.doors().getDoorCount();
	d.doorEstimateBytes = m_game_state.doors().getDoorCount() * sizeof(EQT::DoorState);
	if (m_spell_manager) {
		d.spellDbCount = m_spell_manager->getDatabase().getSpellCount();
		d.spellDbEstimateBytes = d.spellDbCount * 512;
	}

	m_bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::DiagnosticsMemoryReport, std::move(d)));
}

void EverQuest::runLoadDiagnostics(const std::string& label) {
	LOG_INFO(MOD_MAIN, "");
	LOG_INFO(MOD_MAIN, "╔══════════════════════════════════════════════════════════════");
	LOG_INFO(MOD_MAIN, "║ LOAD DIAGNOSTICS: {}", label);
	LOG_INFO(MOD_MAIN, "╚══════════════════════════════════════════════════════════════");

	// Scene dump via bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::DiagnosticsSceneDump,
			eqt::state::DiagnosticsSceneDumpData{label}));
	}

	// Memory report via bridge
	runPmemDiagnostics(label);

	LOG_INFO(MOD_MAIN, "══════════════════════════════════════════════════════════════");
	LOG_INFO(MOD_MAIN, "");
}

void EverQuest::RegisterCommands()
{
	// G02: driver — calls per-domain registration methods in the exact
	// order their commands appeared in the original monolithic method.
	// This preserves observable behavior for order-sensitive registrations
	// (e.g. the duplicate "who" command name — see refactor/G02_commands_split.md).
	RegisterSocialCommands();
	RegisterMovementCommands();
	RegisterSpellCommands();
	// T136: RegisterVendorCommands() removed -- vendor/vendorlist/buy/sell
	// MIGRATED to action::CommandProcessor. See the T136 block below (near
	// RequestVendorToggle()) and refactor/T136_migrate_merchant_family.md.
	// T141: RegisterEquipmentCommands() removed -- equip MIGRATED to
	// action::CommandProcessor (cmdEquip), the last of the game-registry
	// migration programme. See refactor/T141_equip_extraction.md.
}

namespace {

// T66: /equip slot argument parsing. Same consumed-length idiom T48's
// (now-removed, T136) ParseVendorSlot used, but signed/int16_t range since inventory slot ids
// (include/client/inventory/inventory_constants.h) are the same type
// MoveItemIntent::fromSlot/toSlot and SendMoveItem() use -- no separate
// slot numbering invented here (T57's ruling: that enum/header is canonical).
bool ParseEquipSlot(const std::string& tok, int16_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < std::numeric_limits<int16_t>::min() || v > std::numeric_limits<int16_t>::max() ||
		    consumed != tok.size()) {
			return false;
		}
		out = static_cast<int16_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

// T141 -- IsRecognizedInventorySlot()/DescribeEquipSlot()/
// CheckEquipDestination()/FindItemsByNameSubstring()/
// EquipDestinationResult/ResolveEquipDestination() (the ~150-line block
// that used to live here) MOVED to InventoryManager
// (include/client/inventory/inventory_manager.h,
// src/client/inventory/inventory_manager.cpp) as
// checkEquipDestination()/resolveEquipDestination()/
// findItemsByNameSubstring()/moveItem(), plus a shared
// eqt::inventory::describeSlot() (inventory_constants.h). See
// refactor/T141_equip_extraction.md's Review for the full argument: this
// eliminates IsRecognizedInventorySlot's whole reason for existing (it
// existed only to mirror InventoryManager::isValidSlot(), which is
// private -- moveItem() is now a member and calls isValidSlot() directly,
// so there is exactly one copy of that predicate, not two) and lets
// moveItem() push its own bridge intents via InventoryManager's existing
// bridge_ member (the same one pickupItem()/placeItem() already use),
// which is why the handler below no longer touches m_bridge at all.

}  // namespace

void EverQuest::RegisterSocialCommands()
{
	using namespace eqt::ui;

	// === Chat Commands ===
	//
	// T100: say/shout/ooc/auction/gsay/gu/tell/emote/reply migrated OUT of
	// this registry and INTO action::CommandProcessor -- see
	// refactor/T100_migrate_chat_family.md. This registry is tried BEFORE
	// action::CommandProcessor (Application::DispatchSlashCommand's stage 1
	// vs dispatchToActionOrRenderer's stage 2), so leaving these
	// registrations here while also adding them to CommandProcessor would
	// have made the migration invisible -- removal from here is the other
	// half of the same change as the addition there, not a separate step.
	// `shout` in particular used to exist in BOTH registries at once (this
	// one via primary name "shout"/alias "sho", CommandProcessor's via the
	// escaping alias "sh" only, since T98); T100 reconciled that duplicate
	// by folding "sho" into CommandProcessor's alias list and deleting this
	// registration, rather than leaving shout split across two registries.

	// === Group Commands ===
	//
	// T99: invite/disband/decline (and the Social follow_cmd's group-invite
	// -accept branch, folded in below) migrated OUT of this registry and INTO
	// action::CommandProcessor -- see refactor/T99_action_layer_unification_
	// pilot.md. This registry is tried BEFORE action::CommandProcessor
	// (Application::DispatchSlashCommand's stage 1 vs
	// dispatchToActionOrRenderer's stage 2), so leaving these registrations
	// here while also adding them to CommandProcessor would have made the
	// migration invisible -- removal from here is the other half of the same
	// change as the addition there, not a separate step.
	//
	// The Social-section follow_cmd that lived here (name "follow", alias
	// "fol") was ALREADY dead by the time this unit ran: T97 found
	// eq_commands_movement.cpp registers its own later "follow" (no
	// invite-accept branch) that always won the same-registry collision, so
	// no reachable spelling of /follow could ever accept a pending invite.
	// Movement's "follow"/"stopfollow" (actual entity-following, a different
	// feature) are untouched -- they were never part of the Group family.
	// The pending-invite-accept behaviour this dead follow_cmd used to gate
	// on m_has_pending_invite now lives at action::CommandProcessor's
	// explicit /groupaccept (alias /gaccept) -- see that file's cmdGroupAccept
	// for why an explicit spelling was chosen over reviving bare /follow.

	// T102: trade/accept/tradedecline/canceltrade MIGRATED to
	// action::CommandProcessor (cmdTrade/cmdTradeAccept/cmdTradeDecline/
	// cmdCancelTrade) -- see refactor/T102_migrate_combat_social.md.

	// T134: Resurrection (rezaccept/rezdecline) MIGRATED to
	// action::CommandProcessor (cmdRezAccept/cmdRezDecline). Net-new
	// plumbing, not a reconcile-and-delete -- action::CommandProcessor had
	// zero registrations for this family before this unit. See
	// refactor/T134_migrate_corpse_rez.md.

	// T102: who (who_cmd -- the Social section's real server /who
	// request) and inspect MIGRATED to action::CommandProcessor
	// (cmdWho/cmdInspect). See refactor/T102_migrate_combat_social.md for
	// why who_cmd was chosen over Movement's who (eq_commands_movement.cpp,
	// deleted, not migrated).

	// T132: Guild (guildinvite/guildaccept/guilddecline/guildremove/
	// guilddemote/guildleader/guildmotd) MIGRATED to
	// action::CommandProcessor (cmdGuildInvite/cmdGuildAccept/
	// cmdGuildDecline/cmdGuildRemove/cmdGuildDemote/cmdGuildLeader/
	// cmdGuildMotd). Net-new plumbing, not a reconcile-and-delete --
	// action::CommandProcessor had zero guild registrations before this
	// unit. See refactor/T132_migrate_guild_family.md.

	// T134: Corpse (corpsedrag/corpsedrop/consent/deny) MIGRATED to
	// action::CommandProcessor (cmdCorpseDrag/cmdCorpseDrop/cmdConsent/
	// cmdConsentDeny). Net-new plumbing, not a reconcile-and-delete -- see
	// refactor/T134_migrate_corpse_rez.md.

	// T135: Utility (split/save/bandage/rewind/bug/petition) MIGRATED to
	// action::CommandProcessor (cmdSplit/cmdSave/cmdBandage/cmdRewind/
	// cmdBug/cmdPetition). Net-new plumbing, not a reconcile-and-delete --
	// these six do not share a scenario, unlike Guild/Corpse-Rez, but share
	// a unit. See refactor/T135_migrate_utility_family.md.

	// T136: vendor/vendorlist/buy/sell (of the five-command Merchant family)
	// MIGRATED to action::CommandProcessor (cmdVendor/cmdVendorList/cmdBuy/
	// cmdSell). Net-new plumbing over three new EverQuest methods
	// (RequestVendorToggle/RequestVendorBuy/RequestVendorSell, near the top
	// of this file) -- there were no pre-existing senders, unlike every
	// prior family. equip is DELIBERATELY NOT migrated here: it already
	// carries category "Inventory", not "Commerce", in this very registry,
	// and its mechanism (InventoryManager, three input forms, cursor
	// staging, pre-flight legality) shares nothing with vendor/buy/sell's
	// bridge-intent shape. See refactor/T136_migrate_merchant_family.md's
	// Review for the full argument and refactor/deferred.md for the
	// follow-up unit this defers /equip to. This leaves eq_commands.cpp's
	// registry holding only /equip (below).

	// === Phase 3: Combat Targeting Commands ===

	// T102: assist MIGRATED to action::CommandProcessor (cmdAssist).

	// === Phase 3: Group Commands ===

	// T135: split MIGRATED to action::CommandProcessor (cmdSplit).

	// === Phase 3: LFG Commands ===

	// T102: lfg MIGRATED to action::CommandProcessor (cmdLFG).

	// === Phase 3: Combat Ability Commands ===

	// T102: shield MIGRATED to action::CommandProcessor (cmdShield).
	// See refactor/T102_migrate_combat_social.md.

	// === Phase 3: Misc Commands ===

	// T135: save MIGRATED to action::CommandProcessor (cmdSave).

	// T102: duel/duelaccept/dueldecline MIGRATED to
	// action::CommandProcessor (cmdDuel/cmdDuelAccept/cmdDuelDecline).

	// === Phase 4: Skill Commands ===

	// T135: bandage MIGRATED to action::CommandProcessor (cmdBandage).

	// === Phase 4: Cosmetic Commands ===

	// T102: surname MIGRATED to action::CommandProcessor (cmdSurname).

	// === Phase 4: Misc Commands ===

	// T102: random MIGRATED to action::CommandProcessor (cmdRandom).

	// T135: rewind MIGRATED to action::CommandProcessor (cmdRewind).

	// T102: yell MIGRATED to action::CommandProcessor (cmdYell).

	// T135: bug MIGRATED to action::CommandProcessor (cmdBug).

	// T135: petition MIGRATED to action::CommandProcessor (cmdPetition).
}

// T136: vendor/vendorlist/buy/sell MIGRATED to action::CommandProcessor
// (cmdVendor/cmdVendorList/cmdBuy/cmdSell). Unlike every other family, there
// were no EverQuest::Send*() senders to wrap -- these three EverQuest
// methods (RequestVendorToggle/RequestVendorBuy/RequestVendorSell) ARE the
// net-new senders this unit adds, carrying the exact guard bodies the old
// vendor_cmd/buy_cmd/sell_cmd lambdas had verbatim (stateful guards stay in
// the layer owning the state, T134's rule), so action::EqActionHandler --
// which has no access to the private m_bridge -- has something to call.
// /buy and /sell still push the SAME VendorBuyIntent/VendorSellIntent the
// vendor-window gesture pushes (T48's maximum-fidelity choice, preserved
// unchanged); /vendor still pushes the same VendorToggleIntent, whose
// target/NPC-type/class==merchant validation remains exactly where it was,
// inside ProcessBridgeIntents() (eq_bridge.cpp) -- untouched, since that
// logic reads m_entities/m_combat_manager directly and has no public
// accessor to duplicate it through.
//
// /vendorlist MIGRATED too, but as a pure state read + print --
// EqActionHandler::vendorList() calls the two new GetVendorItems()/
// GetVendorName() accessors (eq.h) directly rather than routing through a
// new EverQuest method, per the plan's explicit design question 1: see this
// plan's Review for the full argument.
//
// Argument PARSING (numeric slot/quantity tokens) moved up to
// action::CommandProcessor::cmdBuy/cmdSell -- a formatting concern, not a
// guard, same precedent as cmdSplit/cmdWho's own token parsing. The old
// ParseVendorSlot()/ParseVendorQuantity() free functions below are UNUSED
// now (equip's ParseEquipSlot is a separate, still-used, function) --
// removed rather than left as dead code.
//
// Registrations removed from eq_commands.cpp's m_command_registry in the
// same commit as the CommandProcessor registrations (RegisterCommands()
// above no longer calls RegisterVendorCommands() -- see the removed call
// and this plan's Board for what that means for the registry).

void EverQuest::RequestVendorToggle()
{
	if (!IsVendorWindowOpen() && (!m_combat_manager || !m_combat_manager->HasTarget())) {
		AddChatSystemMessage("You don't have a target - target a merchant first.");
		return;
	}
	if (m_bridge) {
		m_bridge->pushIntent(eqt::events::VendorToggleIntent{});
	} else {
		AddChatSystemMessage("Cannot open vendor - no bridge attached.");
	}
}

void EverQuest::RequestVendorBuy(uint32_t slot, uint8_t quantity)
{
	if (!IsVendorWindowOpen()) {
		AddChatSystemMessage("You are not at a vendor.");
		return;
	}
	if (m_vendor_items.find(slot) == m_vendor_items.end()) {
		AddChatSystemMessage(fmt::format("No item in vendor slot {} - see /vendorlist", slot));
		return;
	}

	// FIDELITY (maximum-fidelity choice, per the plan): this pushes the
	// exact same eqt::events::VendorBuyIntent the renderer's vendor-window
	// "buy" click pushes. From here every hop is IDENTICAL to the
	// gesture's: the intent sits in GameStateBridge's intent queue and is
	// drained by the same EverQuest::ProcessBridgeIntents() ->
	// BuyFromVendor(npcId, slot, quantity) dispatch (the VendorBuyIntent
	// arm of ProcessBridgeIntents, eq_bridge.cpp)
	// on the next GameTick(). The one hop that differs from the pre-T136
	// game-registry path: the caller is now action::EqActionHandler,
	// reached via action::CommandProcessor -- but both the OLD
	// m_command_registry path and the NEW action:: path dispatch on the
	// GAME thread (see this plan's Review, "Threading" section, for the
	// traced call chain proving this for both entry points), so this is
	// not a new cross-thread hazard.
	if (m_bridge) {
		m_bridge->pushIntent(eqt::events::VendorBuyIntent{
			GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
	} else {
		AddChatSystemMessage("Cannot buy - no bridge attached.");
	}
}

void EverQuest::RequestVendorSell(uint32_t slot, uint8_t quantity)
{
	if (!IsVendorWindowOpen()) {
		AddChatSystemMessage("You are not at a vendor.");
		return;
	}

	// Same fidelity note as RequestVendorBuy above: pushes the SAME
	// VendorSellIntent the vendor-window gesture pushes, drained by the
	// same ProcessBridgeIntents() -> SellToVendor() dispatch. Item-in-slot /
	// NO_DROP / stack-size validation stays entirely inside SellToVendor()
	// (eq_merchant.cpp) -- unchanged, per the plan's fence against touching
	// that function -- so this does not duplicate it.
	if (m_bridge) {
		m_bridge->pushIntent(eqt::events::VendorSellIntent{
			GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
	} else {
		AddChatSystemMessage("Cannot sell - no bridge attached.");
	}
}

// T66 -- instance 8 of "reachable only through a UI gesture, invisible to
// automation" (refactor/deferred.md): EverQuest::SendMoveItem(fromSlot,
// toSlot, quantity) (eq_merchant.cpp, opcode HC_OP_MoveItem=0x420f) is the
// real protocol path and was already fully implemented; nothing invoked it
// from a command. T63 had to write inventory.item_id directly into the DB
// and restart the client to get a character holding a weapon at all.
//
// /equip pushes the SAME MoveItemIntent{fromSlot, toSlot, quantity} the
// inventory UI's drag-and-drop gesture pushes (InventoryManager::moveItem,
// inventory_cursor.cpp),
// drained by the same ProcessBridgeIntents() -> SendMoveItem() dispatch --
// same maximum-fidelity choice T48/T50 made for vendor/bank. Slot ids use
// the canonical eqt::inventory::EquipmentSlot enum/ranges
// (inventory_constants.h, T57's ruling) -- no numbering invented here.
//
// Scope discipline (per the plan's fence): this is a thin wrapper. No
// inventory management, no swap semantics, no client-side prediction of the
// result -- the server is authoritative and the existing (untouched)
// ZoneProcessMoveItem path handles the reply. The server remains the sole
// arbiter of whether a move is ALLOWED; nothing here second-guesses a
// server rejection or suppresses the existing resync-on-failure path.
//
// T71 fixed /equip's local-state sync (this handler now keeps
// InventoryManager's own model, and therefore the rendered/paperdoll
// appearance, in sync with what was requested -- see that unit's Review for
// the full characterisation). T77 adds three things on top, all
// command-layer convenience over InventoryManager's EXISTING validation
// (InventoryManager::canEquipItem()/ItemInstance::canEquipInSlot() -- no
// second copy of that logic is written here, per the plan's explicit fence):
//   1. Pre-flight: before sending, confirm fromSlot actually holds an item
//      and, when toSlot is an equipment slot, that canEquipItem() allows it
//      -- so a doomed move (T75 hit "The selected slot was invalid" this
//      way) is rejected locally with a specific, actionable reason instead
//      of being sent at all.
//   2. `/equip <item name>` -- resolves fromSlot by a case-insensitive
//      substring search over carried items, and toSlot from the item's own
//      `slots` bitmask (ItemInstance::canEquipInSlot() over every equip
//      slot -- paired slots like ears/wrists/fingers fall out for free,
//      preferring an empty member of the pair). Reports what it resolved to
//      so a script can assert on it.
//   3. `/equip list` -- what's equipped and where, plus what's carried and
//      where it would go. T63, T71, and T75 each burned session time
//      discovering what a character was carrying before /equip could act
//      on it.
// The explicit `/equip <fromSlot> <toSlot>` form (numeric first token)
// keeps working exactly as T66/T71 left it -- existing session scripts use
// it, and T71 verified it live.
//
// T141 -- RegisterEquipmentCommands() (the game-registry registration
// wrapper that used to sit here) is REMOVED: /equip MIGRATED to
// action::CommandProcessor (cmdEquip -> action::EqActionHandler::equip() ->
// RequestEquip() below, same call target as before -- see
// refactor/T141_equip_extraction.md). This was the last row of the
// game-registry migration programme; eq_commands.cpp no longer registers
// any command into m_command_registry at all (RegisterSocialCommands()
// above was already all-migrated by earlier units, and now has no sibling
// still registering here either).
//
// T141 -- /equip's thin orchestration entry point (T136's Request* recipe).
// The ~150-line legality+execution block this handler used to contain
// moved to InventoryManager::moveItem()/checkEquipDestination()/
// resolveEquipDestination()/findItemsByNameSubstring() (inventory_manager.h/
// .cpp) -- see refactor/T141_equip_extraction.md's Review for the argument.
// What's left here is genuinely command-layer: which of the three input
// forms was typed (slot-pair / item-name / list), and formatting the
// resulting read-only report -- no legality judgment, no mutation, no
// bridge access. InventoryManager pushes its own MoveItemIntent/
// EquipmentChangedIntent via the bridge_ pointer EverQuest::setBridge()
// already wires it with (eq_bridge.cpp) -- the T136-era "EqActionHandler
// cannot push bridge intents" constraint that stopped three prior units on
// this very command does not apply to this shape: this method (and its
// caller, whichever registry it is reached through) never touches m_bridge.
void EverQuest::RequestEquip(const std::string& args)
{
	// T71/T77 -- shared execution for both slot-bearing forms: hands
	// concrete fromSlot/toSlot to InventoryManager::moveItem(), which does
	// all validation/pre-flight/execution/intent-pushing, and relays its
	// outcome message verbatim (moveItem() populates `message` on both
	// success and failure).
	auto doMove = [this](int16_t fromSlot, int16_t toSlot) {
		if (!m_inventory_manager) {
			AddChatSystemMessage(fmt::format("Nothing to move: {} is empty.", eqt::inventory::describeSlot(fromSlot)));
			return;
		}
		auto outcome = m_inventory_manager->moveItem(fromSlot, toSlot);
		AddChatSystemMessage(outcome.message);
	};

	// ---- form dispatch ----
	std::istringstream iss(args);
	std::string fromTok, toTok;
	iss >> fromTok;
	iss >> toTok;

	if (fromTok.empty()) {
		AddChatSystemMessage(
			"Usage: /equip <fromSlot> <toSlot> | /equip <item name> | /equip list "
			"(see include/client/inventory/inventory_constants.h for slot ids)");
		return;
	}

	std::string lowerFirst = fromTok;
	std::transform(lowerFirst.begin(), lowerFirst.end(), lowerFirst.begin(), ::tolower);

	// T77 -- /equip list: what's equipped, and what's carried and where
	// it would resolve to. T63, T71, and T75 each burned session time
	// discovering what a character was carrying before scripting
	// against it.
	if (lowerFirst == "list" && toTok.empty()) {
		if (!m_inventory_manager) {
			AddChatSystemMessage("Cannot list equipment - inventory not available.");
			return;
		}
		auto equipped = m_inventory_manager->getEquipmentItems();
		if (equipped.empty()) {
			AddChatSystemMessage("Nothing currently equipped.");
		} else {
			AddChatSystemMessage(fmt::format("Equipped ({} item{}):",
				equipped.size(), equipped.size() == 1 ? "" : "s"));
			for (const auto& [slot, item] : equipped) {
				AddChatSystemMessage(fmt::format("  {}: {}", eqt::inventory::describeSlot(slot), item->name));
			}
		}

		// Same reachable-by-name universe as
		// InventoryManager::findItemsByNameSubstring(): general slots +
		// general-bag contents + cursor.
		std::vector<std::pair<int16_t, const eqt::inventory::ItemInstance*>> carried;
		for (const auto& [slot, item] : m_inventory_manager->getGeneralItems()) {
			carried.emplace_back(slot, item);
			if (item && item->isContainer()) {
				for (int16_t bagSlot : m_inventory_manager->getBagContentsSlots(slot)) {
					const auto* bagItem = m_inventory_manager->getItem(bagSlot);
					if (bagItem) {
						carried.emplace_back(bagSlot, bagItem);
					}
				}
			}
		}
		if (m_inventory_manager->hasCursorItem()) {
			carried.emplace_back(eqt::inventory::CURSOR_SLOT, m_inventory_manager->getCursorItem());
		}

		bool printedHeader = false;
		for (const auto& [slot, item] : carried) {
			auto dest = m_inventory_manager->resolveEquipDestination(item);
			if (dest.slot == eqt::inventory::SLOT_INVALID) {
				continue;  // not equippable at all (a bag, a reagent, ...) -- not this list's business
			}
			if (!printedHeader) {
				AddChatSystemMessage("Carried, equippable via /equip <name>:");
				printedHeader = true;
			}
			AddChatSystemMessage(fmt::format("  {} ({}) -> {}{}",
				item->name, eqt::inventory::describeSlot(slot), eqt::inventory::describeSlot(dest.slot),
				dest.allCandidatesOccupied ? " (would replace)" : ""));
		}
		if (!printedHeader) {
			AddChatSystemMessage("Nothing else carried that could be equipped.");
		}
		return;
	}

	// A token that LOOKS like an attempted slot number (starts with a
	// digit or '-') is parsed strictly as the explicit
	// <fromSlot> <toSlot> form -- a typo'd number (e.g. "22abc") stays a
	// slot-form usage error rather than silently becoming an item-name
	// search for the literal text "22abc 13". Anything else is the
	// item-name form (below), since item names are free text and may be
	// multiple words.
	bool firstLooksNumeric = !fromTok.empty() &&
		(std::isdigit(static_cast<unsigned char>(fromTok[0])) || fromTok[0] == '-');
	if (firstLooksNumeric) {
		int16_t fromSlot = 0;
		int16_t toSlot = 0;
		if (toTok.empty() || !ParseEquipSlot(fromTok, fromSlot) || !ParseEquipSlot(toTok, toSlot)) {
			AddChatSystemMessage("Usage: /equip <fromSlot> <toSlot> (see include/client/inventory/inventory_constants.h for slot ids)");
			return;
		}
		doMove(fromSlot, toSlot);
		return;
	}

	// T77 -- item-name form.
	if (!m_inventory_manager) {
		AddChatSystemMessage("Cannot resolve item name - inventory not available.");
		return;
	}
	auto matches = m_inventory_manager->findItemsByNameSubstring(args);
	if (matches.empty()) {
		AddChatSystemMessage(fmt::format("No carried item matches '{}'.", args));
		return;
	}
	// Ambiguous only if DISTINCT item names matched -- duplicate stacks
	// of the identical item are interchangeable for equip purposes, so
	// multiple slots holding the same name is not ambiguity.
	std::string firstLowerName = matches.front().second->name;
	std::transform(firstLowerName.begin(), firstLowerName.end(), firstLowerName.begin(), ::tolower);
	bool ambiguous = false;
	for (const auto& [slot, item] : matches) {
		std::string n = item->name;
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		if (n != firstLowerName) {
			ambiguous = true;
			break;
		}
	}
	if (ambiguous) {
		std::string names;
		for (const auto& [slot, item] : matches) {
			if (!names.empty()) {
				names += ", ";
			}
			names += fmt::format("{} ({})", item->name, eqt::inventory::describeSlot(slot));
		}
		AddChatSystemMessage(fmt::format("'{}' matches multiple items: {} -- be more specific.", args, names));
		return;
	}

	int16_t fromSlot = matches.front().first;
	const eqt::inventory::ItemInstance* item = matches.front().second;
	auto dest = m_inventory_manager->resolveEquipDestination(item);
	if (dest.slot == eqt::inventory::SLOT_INVALID) {
		AddChatSystemMessage(fmt::format("{} cannot be equipped (fits no equipment slot).", item->name));
		return;
	}
	if (dest.allCandidatesOccupied) {
		AddChatSystemMessage(fmt::format("{} would replace what's already in {}.",
			item->name, eqt::inventory::describeSlot(dest.slot)));
	}
	AddChatSystemMessage(fmt::format("Resolved '{}' to {} ({}) -> {}.",
		args, item->name, eqt::inventory::describeSlot(fromSlot), eqt::inventory::describeSlot(dest.slot)));
	doMove(fromSlot, dest.slot);
}

// ============================================================================
// T273 -- /read <slot|item name>
// ============================================================================
//
// eqemu has always been able to serve a book: Handle_OP_ReadBook ->
// Client::ReadBook() -> content_db.GetBook(txtfile) -> OP_ReadBook reply.
// The client had the whole return leg (SendReadBookRequest, the reply
// handler, the NoteWindowOpened event, the ReadItemIntent consumer arm in
// eq_bridge.cpp) and NO producer -- T264 listed ReadItemIntent as a blocked
// intent for exactly that reason. This is the producer.
//
// Same shape as RequestEquip() above (T141/T77): the free-text argument is
// either a numeric inventory SLOT or an item-NAME substring, resolved
// against InventoryManager (general slots + bag contents + cursor +
// equipment). The intent it pushes is the SAME ReadItemIntent a book-item
// double-click gesture would push, drained by the same
// EverQuest::ProcessBridgeIntents() -> RequestReadBook() -> OP_ReadBook.
//
// What identifies a book on the wire is the item's Filename field (Titanium
// item field 102, eqemu ItemData::Filename), which is the `books`.`name`
// key the server looks the text up by -- NOT the item name and NOT any
// text carried in the item packet. An item with an empty Filename cannot be
// read, and asking for one would make the server silently return nothing
// (Client::ReadBook returns early on an empty text_file), so /read refuses
// locally and names the item, per T178.
// T276 -- ONE slot|name resolver for the carried-item commands. T273's
// /read copied the rule out of /equip; /scribe would have been the THIRD
// copy, so it is a function now and /read calls it too. Returns nullptr
// after having already sent the refusal (T178: every refusal names what was
// asked for). `verb` appears in the messages ("read"/"scribe").
const eqt::inventory::ItemInstance* EverQuest::ResolveCarriedItem(
	const std::string& args, const char* verb, int16_t& slotOut)
{
	slotOut = eqt::inventory::SLOT_INVALID;

	if (!m_inventory_manager) {
		AddChatSystemMessage(fmt::format("Cannot {} - inventory not available.", verb));
		return nullptr;
	}

	// A token that LOOKS like an attempted slot number is parsed strictly
	// as the slot form -- same rule as /equip, so a typo'd number stays a
	// slot error rather than becoming a search for the literal text.
	const bool numericForm = std::isdigit(static_cast<unsigned char>(args[0])) || args[0] == '-';

	if (numericForm) {
		int16_t parsed = 0;
		if (!ParseEquipSlot(args, parsed)) {
			AddChatSystemMessage(fmt::format("Invalid slot '{}' - use /{} <item name> instead.", args, verb));
			return nullptr;
		}
		const eqt::inventory::ItemInstance* item = m_inventory_manager->getItem(parsed);
		if (!item) {
			AddChatSystemMessage(fmt::format("Nothing in {}.", eqt::inventory::describeSlot(parsed)));
			return nullptr;
		}
		slotOut = parsed;
		return item;
	}

	auto matches = m_inventory_manager->findItemsByNameSubstring(args);
	if (matches.empty()) {
		AddChatSystemMessage(fmt::format("No carried item matches '{}'.", args));
		return nullptr;
	}
	// Ambiguous only if DISTINCT item names matched -- two copies of the
	// same note read identically, two copies of the same scroll scribe
	// identically. Same rule as /equip.
	std::string firstLowerName = matches.front().second->name;
	std::transform(firstLowerName.begin(), firstLowerName.end(), firstLowerName.begin(), ::tolower);
	bool ambiguous = false;
	for (const auto& [s, i] : matches) {
		std::string n = i->name;
		std::transform(n.begin(), n.end(), n.begin(), ::tolower);
		if (n != firstLowerName) {
			ambiguous = true;
			break;
		}
	}
	if (ambiguous) {
		std::string names;
		for (const auto& [s, i] : matches) {
			if (!names.empty()) {
				names += ", ";
			}
			names += fmt::format("{} ({})", i->name, eqt::inventory::describeSlot(s));
		}
		AddChatSystemMessage(fmt::format("'{}' matches multiple items: {} -- be more specific.", args, names));
		return nullptr;
	}
	slotOut = matches.front().first;
	return matches.front().second;
}

void EverQuest::RequestReadItem(const std::string& args)
{
	if (args.empty()) {
		AddChatSystemMessage("Usage: /read <slot> | /read <item name> (a book or note you are carrying)");
		return;
	}

	int16_t slot = eqt::inventory::SLOT_INVALID;
	const eqt::inventory::ItemInstance* item = ResolveCarriedItem(args, "read", slot);
	if (!item) {
		return;
	}

	if (item->bookFilename.empty()) {
		AddChatSystemMessage(fmt::format("{} ({}) is not a book or note.",
			item->name, eqt::inventory::describeSlot(slot)));
		return;
	}

	if (!m_bridge) {
		AddChatSystemMessage("Cannot read - no bridge attached.");
		return;
	}

	AddChatSystemMessage(fmt::format("Reading {} ({})...", item->name, eqt::inventory::describeSlot(slot)));
	m_bridge->pushIntent(eqt::events::ReadItemIntent{
		item->bookFilename, static_cast<uint8_t>(item->bookType)});
}

// ============================================================================
// T276 -- /scribe <slot|item name>   (T264 F5)
// ============================================================================
//
// The wire truth, read out of eqemu (zone/client_process.cpp
// Client::OPMemorizeSpell) rather than assumed:
//
//   * The SAME opcode as /mem (OP_MemorizeSpell, MemorizeSpell_Struct) with
//     `scribing` = memSpellScribing = 0 (zone/client.h:102), NOT 1. `slot`
//     is a SPELLBOOK slot (0..399), not a gem.
//   * The scroll must be on the CURSOR: the handler reads
//     m_inv[EQ::invslot::slotCursor] and demands the cursor item's
//     Scroll.Effect == spell_id. An item sitting in a general slot is
//     rejected with "Scribing a spell without an Item Instance on your
//     cursor?". This is why ScribeSpellFromScroll() moves it there.
//   * Class/level are validated server-side for every non-forget action
//     (SPELL_LEVEL_TO_LOW), and RuleB(Character, RestrictSpellScribing)
//     adds an IsEquipable race/class check (CANNOT_USE_ITEM).
//   * On success it calls ScribeSpell(spell_id, slot) -- which writes
//     character_spells and echoes OP_MemorizeSpell back with scribing=0 --
//     and then DELETES the scroll from the cursor.
//
// The client can know all of the class/level facts locally (SpellDatabase
// carries class_levels), so /scribe refuses without sending, per T178.
// What it cannot know is the server's RestrictSpellScribing rule; that one
// stays server-side.
//
// The destination book slot is NOT taken from the user: the stock client
// picks it, and a wrong slot silently overwrites a known spell. The first
// free slot comes from SpellManager::findFreeSpellbookSlot().
void EverQuest::RequestScribeItem(const std::string& args)
{
	if (args.empty()) {
		AddChatSystemMessage("Usage: /scribe <slot> | /scribe <item name> (a spell scroll you are carrying)");
		return;
	}

	int16_t slot = eqt::inventory::SLOT_INVALID;
	const eqt::inventory::ItemInstance* item = ResolveCarriedItem(args, "scribe", slot);
	if (!item) {
		return;
	}

	// Item field 154 (eqemu ItemData::Scroll.Effect, titanium.cpp
	// SerializeItem) is the spell the scroll teaches AND the value the
	// server compares against spell_id. An item without one cannot be
	// scribed no matter what its name says, so the id -- not
	// ItemInstance::isSpellScroll(), which also fires on a "Spell:" name
	// prefix alone -- is the gate.
	const uint32_t spellId = item->getScrollSpellId();
	if (spellId == 0) {
		AddChatSystemMessage(fmt::format("{} ({}) is not a spell scroll.",
			item->name, eqt::inventory::describeSlot(slot)));
		return;
	}

	if (!m_spell_manager) {
		AddChatSystemMessage("Cannot scribe - spell system not available.");
		return;
	}

	// Pre-flight the same refusals ScribeSpellFromScroll() guards with, so
	// nothing touches the cursor for a scribe that was never going to work.
	if (std::string refusal = ScribeRefusalMessage(spellId); !refusal.empty()) {
		AddChatSystemMessage(refusal);
		return;
	}

	const int32_t bookSlot = m_spell_manager->findFreeSpellbookSlot();
	if (bookSlot < 0) {
		AddChatSystemMessage("Your spellbook is full.");
		return;
	}

	if (!m_bridge) {
		AddChatSystemMessage("Cannot scribe - no bridge attached.");
		return;
	}

	// Put the scroll on the cursor FIRST. eqemu's Client::OPMemorizeSpell
	// reads it from there and compares its Scroll.Effect to spell_id, so the
	// OP_MoveItem has to reach the server ahead of the OP_MemorizeSpell.
	// setSpellForScribe() -> pickupItem() pushes MoveItemIntent, and intents
	// drain FIFO, so pushing the move here and ScribeSpellIntent immediately
	// after gives exactly that order in one ProcessBridgeIntents() pass.
	// (Doing the pickup inside the ScribeSpellIntent arm instead would defer
	// the move to the NEXT drain -- the packets would go out backwards.)
	if (slot != eqt::inventory::CURSOR_SLOT) {
		if (m_inventory_manager->hasCursorItem()) {
			AddChatSystemMessage("Your cursor is full - put down what you are holding first.");
			return;
		}
		m_inventory_manager->setSpellForScribe(spellId, slot);
	}

	m_bridge->pushIntent(eqt::events::ScribeSpellIntent{
		spellId, static_cast<uint16_t>(bookSlot), slot});
}

// ============================================================================
// T278 -- /combine and /closecontainer (T264 F7)
// ============================================================================
//
// What was actually missing. T264 listed TradeskillCombineIntent and
// TradeskillCloseIntent as blocked (consumer arm in eq_bridge.cpp, no
// producer), and the two senders they call -- SendTradeSkillCombine() and
// SendCloseContainer() -- were reachable only through those dead arms.
// /recipecombine is NOT the same thing: it is T149's AUTO-combine and sends
// OP_RecipeAutoCombine (SendRecipeAutoCombine), where the server picks the
// components for a recipe id. The MANUAL combine -- "I put these items in
// the forge, hit Combine" -- is OP_TradeSkillCombine and had no producer at
// all. Neither did the container close.
//
// Wire forms verified against eqemu rather than member names (T273's rule):
//   OP_TradeSkillCombine  -> Handle_OP_TradeSkillCombine, DECODE_LENGTH_EXACT
//     on titanium structs::NewCombine_Struct { int16 container_slot; int16
//     guildtribute_slot; } (4 bytes). Object::HandleCombine treats
//     container_slot == SLOT_TRADESKILL_EXPERIMENT_COMBINE (1000, identical
//     on both sides of TitaniumToServerSlot) as "the WORLD container the
//     server has me down as using" and reads the components out of it.
//   OP_ClickObjectAction  -> Handle_OP_ClickObjectAction, exact
//     ClickObjectAction_Struct (92 bytes, no Titanium translation), open==0
//     -> Object::Close(). Any other value of `open` is a server-side error.
//
// Both requests are gated on a container the SERVER has told us is open
// (m_active_tradeskill_object_id, written only by ZoneProcessClickObjectAction)
// -- the same gate the /recipe* family uses. Sending either one without that
// makes eqemu log an error and, for the combine, message the player that the
// server does not know about the container.
//
// BOTH container kinds go through this ONE sender, because container_slot is
// the only thing that distinguishes them on the wire:
//   world container (forge/oven/loom on the ground): container_slot = 1000,
//     and HandleCombine uses the object the server has us down as using.
//   inventory container (a carried Forge/Sewing Kit): container_slot = the
//     bag's own inventory slot, and HandleCombine reads its BagType itself.
// T149's "some_id" caveat does NOT apply here: some_id is a LOCAL in
// HandleCombine, computed server-side from the container it resolved. It is
// on the wire only for the /recipe* family (OP_RecipeAutoCombine et al),
// which is why those stay world-container-only.
//
// containerSlot == SLOT_INVALID selects the world form.
void EverQuest::RequestTradeskillCombine(int16_t containerSlot)
{
	if (containerSlot == eqt::inventory::SLOT_INVALID) {
		if (m_active_tradeskill_object_id == 0) {
			AddChatSystemMessage("You must have a tradeskill container open.");
			return;
		}
		containerSlot = eqt::inventory::SLOT_TRADESKILL_EXPERIMENT_COMBINE;
	} else {
		if (!m_inventory_manager) {
			AddChatSystemMessage("Cannot combine - inventory not available.");
			return;
		}
		const eqt::inventory::ItemInstance* item = m_inventory_manager->getItem(containerSlot);
		if (!item) {
			AddChatSystemMessage(fmt::format("Nothing in {}.",
				eqt::inventory::describeSlot(containerSlot)));
			return;
		}
		// Refuse locally rather than let eqemu answer "Server does not
		// recognize specified tradeskill container" (T178: name what was
		// asked for).
		if (!item->isContainer()) {
			AddChatSystemMessage(fmt::format("{} ({}) is not a container.",
				item->name, eqt::inventory::describeSlot(containerSlot)));
			return;
		}
	}

	if (!m_bridge) {
		AddChatSystemMessage("Cannot combine - no bridge attached.");
		return;
	}

	m_bridge->pushIntent(eqt::events::TradeskillCombineIntent{containerSlot});
}

void EverQuest::RequestCloseTradeskillContainer()
{
	if (m_active_tradeskill_object_id == 0) {
		AddChatSystemMessage("You do not have a tradeskill container open.");
		return;
	}

	if (!m_bridge) {
		AddChatSystemMessage("Cannot close the container - no bridge attached.");
		return;
	}

	m_bridge->pushIntent(eqt::events::TradeskillCloseIntent{m_active_tradeskill_object_id});
}
