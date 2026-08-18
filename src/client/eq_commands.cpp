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
// unconditionally now that RegisterEquipmentCommands()'s /equip is not
// graphics-gated either (SendMoveItem is plain network logic).
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
	d.doorCount = m_door_state_manager.getDoorCount();
	d.doorEstimateBytes = m_door_state_manager.getDoorCount() * sizeof(EQT::DoorState);
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
	RegisterEquipmentCommands();
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

// T66: mirrors InventoryManager::isValidSlot() (inventory_manager.cpp) --
// that method is private (an internal helper for the UI's own drag/drop
// path), so this reproduces its exact OR-chain over the same public
// inventory_constants.h predicates rather than reaching into
// InventoryManager's internals or widening its access for one caller.
// Any recognized slot category (equipment/general/bag/cursor/bank/shared
// bank/bank-bag/shared-bank-bag/trade/trade-bag) passes; this does NOT
// mean the move is a legal equip (item type, class, etc.) -- that
// judgment stays server-side, per the plan's scope fence.
bool IsRecognizedInventorySlot(int16_t slot) {
	using namespace eqt::inventory;
	return isEquipmentSlot(slot) ||
	       isGeneralSlot(slot) ||
	       isBagSlot(slot) ||
	       isCursorSlot(slot) ||
	       isBankSlot(slot) ||
	       isSharedBankSlot(slot) ||
	       isBankBagSlot(slot) ||
	       isSharedBankBagSlot(slot) ||
	       isTradeSlot(slot) ||
	       isTradeBagSlot(slot);
}

// Friendlier reply text: names the slot when it's a canonical equipment
// slot (getSlotName(), inventory_constants.h), otherwise just the number --
// getSlotName() itself returns "Unknown" for anything outside 0-21, which
// would be a worse message than just showing the slot id.
std::string DescribeEquipSlot(int16_t slot) {
	if (eqt::inventory::isEquipmentSlot(slot)) {
		return fmt::format("{} ({})", eqt::inventory::getSlotName(slot), slot);
	}
	return fmt::format("slot {}", slot);
}

// T77 -- pre-flight destination legality, reused by both /equip forms.
// Only meaningful when toSlot is an equipment slot: canEquipItem()'s whole
// domain is "does this item belong in this equip slot"; moving into
// general/bag/bank/cursor has no equip-legality question and always passes
// here (the server remains the sole arbiter of everything else, per T66's
// "no swap semantics" rule -- this is pre-flight to avoid sending a doomed
// packet, not a claim to know better than the server). Reuses
// ItemInstance::canEquipInSlot() (the paired-slot-aware bitmask check) and
// InventoryManager::canEquipItem() (adds class/race/level restrictions) --
// no new legality logic is written here, per the plan's explicit fence
// against a third copy of a predicate this session has already paid twice
// to converge (T57's slot geometry, T60's bone attachment). Returns an
// empty string on success, else a caller-actionable reason naming the item
// and the slot -- never eqemu's opaque "The selected slot was invalid",
// which is what cost T75 a whole session to diagnose.
std::string CheckEquipDestination(const eqt::inventory::InventoryManager* inv,
                                   const eqt::inventory::ItemInstance* item,
                                   int16_t toSlot) {
	if (!item || !eqt::inventory::isEquipmentSlot(toSlot)) {
		return {};
	}
	if (!item->canEquipInSlot(toSlot)) {
		return fmt::format("{} cannot go in {} -- wrong slot type for this item.",
			item->name, DescribeEquipSlot(toSlot));
	}
	if (inv && !inv->canEquipItem(item, toSlot)) {
		return fmt::format(
			"{} cannot go in {} -- your character does not meet its class/race/level requirements.",
			item->name, DescribeEquipSlot(toSlot));
	}
	return {};
}

// T77 -- name resolution for /equip's item-name form. Case-insensitive
// substring match against ItemInstance::name (say-what-you-chose is the
// caller's job once a single match comes back). Searches only the slot
// ranges InventoryManager already exposes accessors for -- equipment
// (getEquipmentItems()), general 22-29 (getGeneralItems()), general-bag
// contents (via the already-exposed getBagContentsSlots(), no new bag
// traversal invented), and the cursor -- deliberately NOT bank/shared-bank/
// trade: this is command-layer convenience for the "equip something you're
// carrying" case the plan describes, not a general inventory search.
std::vector<std::pair<int16_t, const eqt::inventory::ItemInstance*>>
FindItemsByNameSubstring(eqt::inventory::InventoryManager* inv, const std::string& needle) {
	std::vector<std::pair<int16_t, const eqt::inventory::ItemInstance*>> matches;
	if (!inv || needle.empty()) {
		return matches;
	}
	std::string lowerNeedle = needle;
	std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), ::tolower);

	auto tryMatch = [&](int16_t slot, const eqt::inventory::ItemInstance* item) {
		if (!item) return;
		std::string lowerName = item->name;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
		if (lowerName.find(lowerNeedle) != std::string::npos) {
			matches.emplace_back(slot, item);
		}
	};

	for (const auto& [slot, item] : inv->getEquipmentItems()) {
		tryMatch(slot, item);
	}
	for (const auto& [slot, item] : inv->getGeneralItems()) {
		tryMatch(slot, item);
		if (item && item->isContainer()) {
			for (int16_t bagSlot : inv->getBagContentsSlots(slot)) {
				tryMatch(bagSlot, inv->getItem(bagSlot));
			}
		}
	}
	if (inv->hasCursorItem()) {
		tryMatch(eqt::inventory::CURSOR_SLOT, inv->getCursorItem());
	}
	return matches;
}

// T77 -- resolves the destination equipment slot from the item's own
// `slots` bitmask via ItemInstance::canEquipInSlot() (T57's canonical slot
// geometry -- not re-derived here). Iterating every equipment slot and
// asking canEquipInSlot() is what makes paired slots (ears, wrists,
// fingers) fall out for free: both members of a pair independently satisfy
// the bitmask check, so both show up as candidates with no special-casing
// here. Prefers an empty candidate so the name form does not silently
// clobber an already-worn item the caller didn't name; if every candidate
// is occupied, falls back to the first candidate (still a legal
// destination category) and flags it so the caller can say "would
// replace" rather than picking silently.
struct EquipDestinationResult {
	int16_t slot = eqt::inventory::SLOT_INVALID;  // SLOT_INVALID = item fits no equip slot at all
	bool allCandidatesOccupied = false;
};

EquipDestinationResult ResolveEquipDestination(const eqt::inventory::InventoryManager* inv,
                                                const eqt::inventory::ItemInstance* item) {
	EquipDestinationResult result;
	if (!item || !inv) {
		return result;
	}
	std::vector<int16_t> candidates;
	for (int16_t slot = eqt::inventory::EQUIPMENT_BEGIN; slot <= eqt::inventory::EQUIPMENT_END; ++slot) {
		if (item->canEquipInSlot(slot)) {
			candidates.push_back(slot);
		}
	}
	if (candidates.empty()) {
		return result;
	}
	for (int16_t slot : candidates) {
		if (!inv->getItem(slot)) {
			result.slot = slot;
			return result;
		}
	}
	result.slot = candidates.front();
	result.allCandidatesOccupied = true;
	return result;
}

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
	// BuyFromVendor(npcId, slot, quantity) dispatch (eq_bridge.cpp:1069)
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
// inventory UI's drag-and-drop gesture pushes (inventory_manager.cpp),
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
// The live end-to-end leg (does a real packet actually equip a real item on
// a real server, for ANY of the three forms above) is explicitly OUT of
// this unit's territory -- sibling T76 holds the eqemu lock this round. See
// this plan's Review for what is covered here (wiring + pre-flight,
// unit-tested against a populated InventoryManager) vs. what remains
// outstanding.
void EverQuest::RegisterEquipmentCommands()
{
	using namespace eqt::ui;

	Command equip_cmd;
	equip_cmd.name = "equip";
	equip_cmd.usage = "/equip <fromSlot> <toSlot> | /equip <item name> | /equip list";
	equip_cmd.description = "Move/equip an item by slot numbers or by name (e.g. /equip 22 13, /equip Rusty Mace, /equip list)";
	equip_cmd.category = "Inventory";
	equip_cmd.handler = [this](const std::string& args) {
		// T71/T77 -- shared execution for all three /equip forms: given
		// concrete fromSlot/toSlot, validates slot recognition, rejects a
		// no-op, pre-flights the move (source holds an item; if toSlot is an
		// equipment slot, canEquipItem() allows it -- see
		// CheckEquipDestination() above), then executes it via the same
		// InventoryManager local-model sync T71 added. No legality judgment
		// beyond what CheckEquipDestination() reuses from InventoryManager --
		// the server remains the sole arbiter of everything else.
		auto doMove = [this](int16_t fromSlot, int16_t toSlot) {
			if (!IsRecognizedInventorySlot(fromSlot)) {
				AddChatSystemMessage(fmt::format("Invalid source slot: {}", fromSlot));
				return;
			}
			if (!IsRecognizedInventorySlot(toSlot)) {
				AddChatSystemMessage(fmt::format("Invalid destination slot: {}", toSlot));
				return;
			}
			if (fromSlot == toSlot) {
				AddChatSystemMessage(fmt::format(
					"Source and destination are the same slot ({}) -- nothing to do.",
					DescribeEquipSlot(fromSlot)));
				return;
			}
			if (!m_bridge) {
				AddChatSystemMessage("Cannot move item - no bridge attached.");
				return;
			}

			// T77 -- pre-flight. Determine the source item first (same
			// cursor-vs-slot distinction the execution branches below make),
			// so both "is there anything to move" and "can it legally go
			// there" are checked -- and reported specifically -- before any
			// state is mutated or any packet queued.
			const eqt::inventory::ItemInstance* sourceItem = nullptr;
			if (eqt::inventory::isCursorSlot(fromSlot)) {
				if (m_inventory_manager && m_inventory_manager->hasCursorItem()) {
					sourceItem = m_inventory_manager->getCursorItem();
				}
			} else if (m_inventory_manager) {
				sourceItem = m_inventory_manager->getItem(fromSlot);
			}
			if (!sourceItem) {
				AddChatSystemMessage(fmt::format("Nothing to move: {} is empty.", DescribeEquipSlot(fromSlot)));
				return;
			}
			std::string legalityError = CheckEquipDestination(m_inventory_manager.get(), sourceItem, toSlot);
			if (!legalityError.empty()) {
				AddChatSystemMessage(legalityError);
				return;
			}

			// T71 -- Stage 1 fix (see refactor/T71_equip_fix_and_first_person.md
			// Review for the full characterisation of why each branch below
			// is shaped this way; unchanged by T77's pre-flight above, which
			// only decides whether execution reaches here at all).
			if (eqt::inventory::isCursorSlot(fromSlot) && !eqt::inventory::isCursorSlot(toSlot)) {
				// e.g. /equip 30 14 -- cursor -> a real slot.
				if (!m_inventory_manager->placeItem(toSlot)) {
					AddChatSystemMessage(fmt::format("Could not place cursor item in {}.", DescribeEquipSlot(toSlot)));
					return;
				}
			} else if (!eqt::inventory::isCursorSlot(fromSlot) && eqt::inventory::isCursorSlot(toSlot)) {
				// e.g. /equip 13 30 -- a real slot -> cursor.
				if (!m_inventory_manager->pickupItem(fromSlot)) {
					AddChatSystemMessage(fmt::format("Could not pick up item from {}.", DescribeEquipSlot(fromSlot)));
					return;
				}
			} else if (!eqt::inventory::isCursorSlot(fromSlot) && !eqt::inventory::isCursorSlot(toSlot)) {
				// e.g. /equip 13 14 -- direct slot<->slot swap, no cursor
				// staging (matches the server's own atomic SwapItem(), proven
				// live in T71). Pre-flight above already confirmed sourceItem
				// exists and (if applicable) the destination is legal.
				eqt::inventory::ItemInstance movedFromItem = *sourceItem;  // copy: setItem() below invalidates sourceItem
				const eqt::inventory::ItemInstance* toItemPtr = m_inventory_manager->getItem(toSlot);
				if (toItemPtr) {
					eqt::inventory::ItemInstance movedToItem = *toItemPtr;
					m_inventory_manager->setItem(toSlot, movedFromItem);
					m_inventory_manager->setItem(fromSlot, movedToItem);
				} else {
					m_inventory_manager->setItem(toSlot, movedFromItem);
					m_inventory_manager->removeItem(fromSlot);
				}
				m_bridge->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
				if (eqt::inventory::isEquipmentSlot(fromSlot)) {
					m_bridge->pushIntent(eqt::events::EquipmentChangedIntent{fromSlot});
				}
				if (eqt::inventory::isEquipmentSlot(toSlot)) {
					m_bridge->pushIntent(eqt::events::EquipmentChangedIntent{toSlot});
				}
			} else {
				// Both endpoints are cursor slots (e.g. /equip 30 33) --
				// nothing meaningful to do locally; fall through to the same
				// wire request as before for parity with pre-T71 behaviour.
				m_bridge->pushIntent(eqt::events::MoveItemIntent{fromSlot, toSlot, /*quantity=*/0});
			}

			AddChatSystemMessage(fmt::format("Moving item: {} -> {}",
				DescribeEquipSlot(fromSlot), DescribeEquipSlot(toSlot)));
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
					AddChatSystemMessage(fmt::format("  {}: {}", DescribeEquipSlot(slot), item->name));
				}
			}

			// Same reachable-by-name universe as FindItemsByNameSubstring()
			// above: general slots + general-bag contents + cursor.
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
				EquipDestinationResult dest = ResolveEquipDestination(m_inventory_manager.get(), item);
				if (dest.slot == eqt::inventory::SLOT_INVALID) {
					continue;  // not equippable at all (a bag, a reagent, ...) -- not this list's business
				}
				if (!printedHeader) {
					AddChatSystemMessage("Carried, equippable via /equip <name>:");
					printedHeader = true;
				}
				AddChatSystemMessage(fmt::format("  {} ({}) -> {}{}",
					item->name, DescribeEquipSlot(slot), DescribeEquipSlot(dest.slot),
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
		auto matches = FindItemsByNameSubstring(m_inventory_manager.get(), args);
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
				names += fmt::format("{} ({})", item->name, DescribeEquipSlot(slot));
			}
			AddChatSystemMessage(fmt::format("'{}' matches multiple items: {} -- be more specific.", args, names));
			return;
		}

		int16_t fromSlot = matches.front().first;
		const eqt::inventory::ItemInstance* item = matches.front().second;
		EquipDestinationResult dest = ResolveEquipDestination(m_inventory_manager.get(), item);
		if (dest.slot == eqt::inventory::SLOT_INVALID) {
			AddChatSystemMessage(fmt::format("{} cannot be equipped (fits no equipment slot).", item->name));
			return;
		}
		if (dest.allCandidatesOccupied) {
			AddChatSystemMessage(fmt::format("{} would replace what's already in {}.",
				item->name, DescribeEquipSlot(dest.slot)));
		}
		AddChatSystemMessage(fmt::format("Resolved '{}' to {} ({}) -> {}.",
			args, item->name, DescribeEquipSlot(fromSlot), DescribeEquipSlot(dest.slot)));
		doMove(fromSlot, dest.slot);
	};
	m_command_registry->registerCommand(equip_cmd);
}
