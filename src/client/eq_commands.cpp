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
	RegisterVendorCommands();
	RegisterEquipmentCommands();
}

// ── T48: vendor slash-command argument parsing helpers ─────────────────────
// Same idiom as action::CommandProcessor::cmdLoot() (T34): std::stol with a
// consumed-length check so trailing garbage ("5abc") is rejected instead of
// silently truncated, plus an explicit range check instead of relying on
// stol's own (wider, signed) overflow behavior.
namespace {

bool ParseVendorSlot(const std::string& tok, uint32_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < 0 || v > 0xFFFF || consumed != tok.size()) return false;
		out = static_cast<uint32_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

bool ParseVendorQuantity(const std::string& tok, uint8_t& out) {
	if (tok.empty()) return false;
	try {
		size_t consumed = 0;
		long v = std::stol(tok, &consumed);
		if (v < 1 || v > 0xFF || consumed != tok.size()) return false;
		out = static_cast<uint8_t>(v);
		return true;
	} catch (const std::exception&) {
		return false;
	}
}

// T66: /equip slot argument parsing. Same consumed-length idiom as
// ParseVendorSlot above, but signed/int16_t range since inventory slot ids
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

	// === Resurrection Commands ===

	Command rezaccept_cmd;
	rezaccept_cmd.name = "rezaccept";
	rezaccept_cmd.aliases = {"acceptrez"};
	rezaccept_cmd.usage = "/rezaccept";
	rezaccept_cmd.description = "Accept pending resurrection offer";
	rezaccept_cmd.category = "Utility";
	rezaccept_cmd.handler = [this](const std::string& args) {
		SendRezzAnswer(true);
	};
	m_command_registry->registerCommand(rezaccept_cmd);

	Command rezdecline_cmd;
	rezdecline_cmd.name = "rezdecline";
	rezdecline_cmd.aliases = {"declinerez"};
	rezdecline_cmd.usage = "/rezdecline";
	rezdecline_cmd.description = "Decline pending resurrection offer";
	rezdecline_cmd.category = "Utility";
	rezdecline_cmd.handler = [this](const std::string& args) {
		SendRezzAnswer(false);
	};
	m_command_registry->registerCommand(rezdecline_cmd);

	// T102: who (who_cmd -- the Social section's real server /who
	// request) and inspect MIGRATED to action::CommandProcessor
	// (cmdWho/cmdInspect). See refactor/T102_migrate_combat_social.md for
	// why who_cmd was chosen over Movement's who (eq_commands_movement.cpp,
	// deleted, not migrated).

	// === Guild Commands ===

	Command guildinvite_cmd;
	guildinvite_cmd.name = "guildinvite";
	guildinvite_cmd.aliases = {"ginvite"};
	guildinvite_cmd.usage = "/guildinvite <name>";
	guildinvite_cmd.description = "Invite player to guild";
	guildinvite_cmd.category = "Guild";
	guildinvite_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildinvite <name>");
			return;
		}
		SendGuildInvite(args);
	};
	m_command_registry->registerCommand(guildinvite_cmd);

	Command guildaccept_cmd;
	guildaccept_cmd.name = "guildaccept";
	guildaccept_cmd.aliases = {"acceptguild"};
	guildaccept_cmd.usage = "/guildaccept";
	guildaccept_cmd.description = "Accept pending guild invite";
	guildaccept_cmd.category = "Guild";
	guildaccept_cmd.handler = [this](const std::string& args) {
		SendGuildInviteAccept(true);
	};
	m_command_registry->registerCommand(guildaccept_cmd);

	Command guilddecline_cmd;
	guilddecline_cmd.name = "guilddecline";
	guilddecline_cmd.aliases = {"declineguild"};
	guilddecline_cmd.usage = "/guilddecline";
	guilddecline_cmd.description = "Decline pending guild invite";
	guilddecline_cmd.category = "Guild";
	guilddecline_cmd.handler = [this](const std::string& args) {
		SendGuildInviteAccept(false);
	};
	m_command_registry->registerCommand(guilddecline_cmd);

	Command guildremove_cmd;
	guildremove_cmd.name = "guildremove";
	guildremove_cmd.aliases = {"gremove", "gkick"};
	guildremove_cmd.usage = "/guildremove <name>";
	guildremove_cmd.description = "Remove member from guild";
	guildremove_cmd.category = "Guild";
	guildremove_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildremove <name>");
			return;
		}
		SendGuildRemove(args);
	};
	m_command_registry->registerCommand(guildremove_cmd);

	Command guilddemote_cmd;
	guilddemote_cmd.name = "guilddemote";
	guilddemote_cmd.aliases = {"gdemote"};
	guilddemote_cmd.usage = "/guilddemote <name>";
	guilddemote_cmd.description = "Demote guild member";
	guilddemote_cmd.category = "Guild";
	guilddemote_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guilddemote <name>");
			return;
		}
		SendGuildDemote(args);
	};
	m_command_registry->registerCommand(guilddemote_cmd);

	Command guildleader_cmd;
	guildleader_cmd.name = "guildleader";
	guildleader_cmd.aliases = {"gleader"};
	guildleader_cmd.usage = "/guildleader <name>";
	guildleader_cmd.description = "Transfer guild leadership";
	guildleader_cmd.category = "Guild";
	guildleader_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /guildleader <name>");
			return;
		}
		SendGuildLeader(args);
	};
	m_command_registry->registerCommand(guildleader_cmd);

	Command guildmotd_cmd;
	guildmotd_cmd.name = "guildmotd";
	guildmotd_cmd.aliases = {"gmotd"};
	guildmotd_cmd.usage = "/guildmotd [message]";
	guildmotd_cmd.description = "View or set guild MOTD";
	guildmotd_cmd.category = "Guild";
	guildmotd_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			SendGetGuildMOTD();
		} else {
			SendSetGuildMOTD(args);
		}
	};
	m_command_registry->registerCommand(guildmotd_cmd);

	// === Phase 3: Corpse Commands ===

	Command corpsedrag_cmd;
	corpsedrag_cmd.name = "corpsedrag";
	corpsedrag_cmd.aliases = {"drag"};
	corpsedrag_cmd.usage = "/corpsedrag <corpse_name>";
	corpsedrag_cmd.description = "Start dragging a corpse";
	corpsedrag_cmd.category = "Corpse";
	corpsedrag_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /corpsedrag <corpse_name>");
			return;
		}
		SendCorpseDrag(args);
	};
	m_command_registry->registerCommand(corpsedrag_cmd);

	Command corpsedrop_cmd;
	corpsedrop_cmd.name = "corpsedrop";
	corpsedrop_cmd.aliases = {"drop"};
	corpsedrop_cmd.usage = "/corpsedrop";
	corpsedrop_cmd.description = "Drop the corpse you are dragging";
	corpsedrop_cmd.category = "Corpse";
	corpsedrop_cmd.handler = [this](const std::string& args) {
		SendCorpseDrop();
	};
	m_command_registry->registerCommand(corpsedrop_cmd);

	Command consent_cmd;
	consent_cmd.name = "consent";
	consent_cmd.usage = "/consent <player_name>";
	consent_cmd.description = "Grant permission to drag your corpse";
	consent_cmd.category = "Corpse";
	consent_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /consent <player_name>");
			return;
		}
		SendConsent(args);
	};
	m_command_registry->registerCommand(consent_cmd);

	Command deny_cmd;
	deny_cmd.name = "deny";
	deny_cmd.usage = "/deny <player_name>";
	deny_cmd.description = "Revoke permission to drag your corpse";
	deny_cmd.category = "Corpse";
	deny_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /deny <player_name>");
			return;
		}
		SendConsentDeny(args);
	};
	m_command_registry->registerCommand(deny_cmd);

	// === Phase 3: Combat Targeting Commands ===

	// T102: assist MIGRATED to action::CommandProcessor (cmdAssist).

	// === Phase 3: Group Commands ===

	Command split_cmd;
	split_cmd.name = "split";
	split_cmd.usage = "/split <platinum> [gold] [silver] [copper]";
	split_cmd.description = "Split money with your group";
	split_cmd.category = "Group";
	split_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /split <platinum> [gold] [silver] [copper]");
			return;
		}
		uint32_t p = 0, g = 0, s = 0, c = 0;
		std::istringstream iss(args);
		iss >> p >> g >> s >> c;
		SendSplit(p, g, s, c);
	};
	m_command_registry->registerCommand(split_cmd);

	// === Phase 3: LFG Commands ===

	// T102: lfg MIGRATED to action::CommandProcessor (cmdLFG).

	// === Phase 3: Combat Ability Commands ===

	// T102: shield MIGRATED to action::CommandProcessor (cmdShield).
	// See refactor/T102_migrate_combat_social.md.

	// === Phase 3: Misc Commands ===

	Command save_cmd;
	save_cmd.name = "save";
	save_cmd.usage = "/save";
	save_cmd.description = "Request character save";
	save_cmd.category = "Utility";
	save_cmd.handler = [this](const std::string& args) {
		SendSave();
	};
	m_command_registry->registerCommand(save_cmd);

	// T102: duel/duelaccept/dueldecline MIGRATED to
	// action::CommandProcessor (cmdDuel/cmdDuelAccept/cmdDuelDecline).

	// === Phase 4: Skill Commands ===

	Command bindwound_cmd;
	bindwound_cmd.name = "bandage";
	bindwound_cmd.aliases = {"bindwound"};
	bindwound_cmd.usage = "/bandage";
	bindwound_cmd.description = "Use bind wound skill on target";
	bindwound_cmd.category = "Skills";
	bindwound_cmd.handler = [this](const std::string& args) {
		// T28: repointed from the never-populated state-layer CombatState
		// target to the authoritative CombatManager (see /assist above).
		uint32_t target_id = (m_combat_manager && m_combat_manager->HasTarget())
			? m_combat_manager->GetTargetId() : 0;
		if (target_id == 0) {
			target_id = m_my_spawn_id;  // Self-target
		}
		SendBindWound(target_id);
	};
	m_command_registry->registerCommand(bindwound_cmd);

	// === Phase 4: Cosmetic Commands ===

	// T102: surname MIGRATED to action::CommandProcessor (cmdSurname).

	// === Phase 4: Misc Commands ===

	// T102: random MIGRATED to action::CommandProcessor (cmdRandom).

	Command rewind_cmd;
	rewind_cmd.name = "rewind";
	rewind_cmd.usage = "/rewind";
	rewind_cmd.description = "Attempt to return to a safe position";
	rewind_cmd.category = "Movement";
	rewind_cmd.handler = [this](const std::string& args) {
		SendRewind();
	};
	m_command_registry->registerCommand(rewind_cmd);

	// T102: yell MIGRATED to action::CommandProcessor (cmdYell).

	Command bug_cmd;
	bug_cmd.name = "bug";
	bug_cmd.aliases = {"report"};
	bug_cmd.usage = "/bug <description>";
	bug_cmd.description = "Submit a bug report";
	bug_cmd.category = "Utility";
	bug_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /bug <description>");
			return;
		}
		SendReport(args);
	};
	m_command_registry->registerCommand(bug_cmd);

	Command petition_cmd;
	petition_cmd.name = "petition";
	petition_cmd.usage = "/petition <text>";
	petition_cmd.description = "Submit a petition";
	petition_cmd.category = "Utility";
	petition_cmd.handler = [this](const std::string& args) {
		if (args.empty()) {
			AddChatSystemMessage("Usage: /petition <text>");
			return;
		}
		SendPetition(args);
	};
	m_command_registry->registerCommand(petition_cmd);
}

// T48: vendor buy/sell were previously reachable ONLY through the renderer's
// vendor-window UI gesture -- VendorBuyIntent/VendorSellIntent existed as
// types with working handlers in ProcessBridgeIntents() (eq_bridge.cpp:
// 1069-1074, dispatching to BuyFromVendor()/SellToVendor()), but nothing
// anywhere constructed either intent (refactor/deferred.md "PATTERN: real
// features reachable only through a UI gesture, invisible to automation";
// refactor/T48_vendor_reachability.md). T42 had to add temporary scratch
// hooks calling BuyFromVendor()/SellToVendor() directly just to run its live
// acceptance, then revert them.
//
// These three commands close that gap. /vendorlist surfaces m_vendor_items
// (populated in memory by ZoneProcessVendorItemToUI, eq_merchant.cpp) so a
// script can discover what's for sale without guessing slot numbers -- T42's
// exact blocker. /buy and /sell push the SAME VendorBuyIntent/VendorSellIntent
// the vendor-window gesture pushes (maximum-fidelity choice; see the
// commentary on buy_cmd's handler below for exactly which hops differ from
// the gesture's). All three default to the currently open vendor
// (GetVendorNpcId()/IsVendorWindowOpen()) rather than making the caller pass
// an npc id, per the plan.
void EverQuest::RegisterVendorCommands()
{
	using namespace eqt::ui;

	// /vendor was added alongside /vendorlist, /buy, /sell once scoping this
	// unit's mandated live acceptance ("open a vendor, list its inventory,
	// buy an item, sell it back -- all via piped commands, no scratch
	// hooks") surfaced that VendorToggleIntent -- the intent the renderer's
	// right-click-a-merchant gesture pushes to OPEN the window in the first
	// place (renderer_frame.cpp:618) -- has exactly the same problem as
	// VendorBuyIntent/VendorSellIntent: its handler exists in
	// ProcessBridgeIntents() (eq_bridge.cpp:866, validating target/NPC-type/
	// class==merchant then calling RequestOpenVendor()), but nothing ever
	// constructed it either. Without a command-channel way to OPEN a vendor,
	// the plan's own mandated live acceptance would have been impossible to
	// run without a scratch hook -- precisely what this unit exists to
	// avoid. This command pushes the SAME VendorToggleIntent{} the gesture
	// pushes (same fidelity choice as /buy and /sell below), so it is a
	// direct, in-spirit extension of the T48 pattern, not a new feature:
	// the actual open/close logic (target/NPC-type/class validation,
	// RequestOpenVendor()/CloseVendorWindow()) is untouched, unmoved,
	// pre-existing code.
	Command vendor_cmd;
	vendor_cmd.name = "vendor";
	vendor_cmd.usage = "/vendor";
	vendor_cmd.description = "Open (or close) a vendor window with your current target";
	vendor_cmd.category = "Commerce";
	vendor_cmd.handler = [this](const std::string& /*args*/) {
		if (!IsVendorWindowOpen() && (!m_combat_manager || !m_combat_manager->HasTarget())) {
			AddChatSystemMessage("You don't have a target - target a merchant first.");
			return;
		}
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorToggleIntent{});
		} else {
			AddChatSystemMessage("Cannot open vendor - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(vendor_cmd);

	Command vendorlist_cmd;
	vendorlist_cmd.name = "vendorlist";
	vendorlist_cmd.usage = "/vendorlist";
	vendorlist_cmd.description = "List the open vendor's inventory (slot, name, price, quantity)";
	vendorlist_cmd.category = "Commerce";
	vendorlist_cmd.handler = [this](const std::string& /*args*/) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (m_vendor_items.empty()) {
			AddChatSystemMessage(fmt::format("{}'s inventory is empty (or still loading).", m_vendor_name));
			return;
		}
		AddChatSystemMessage(fmt::format("{}'s inventory ({} item{}):",
			m_vendor_name, m_vendor_items.size(), m_vendor_items.size() == 1 ? "" : "s"));
		for (const auto& [slot, item] : m_vendor_items) {
			std::string qtyStr = (item.quantity < 0) ? "unlimited" : std::to_string(item.quantity);
			AddChatSystemMessage(fmt::format("  [slot {}] {} - {} (qty: {})",
				slot, item.name, FormatPrice(item.price), qtyStr));
		}
	};
	m_command_registry->registerCommand(vendorlist_cmd);

	Command buy_cmd;
	buy_cmd.name = "buy";
	buy_cmd.usage = "/buy <slot> [quantity]";
	buy_cmd.description = "Buy an item from the open vendor (see /vendorlist)";
	buy_cmd.category = "Commerce";
	buy_cmd.handler = [this](const std::string& args) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /buy <slot> [quantity] - see /vendorlist");
			return;
		}

		std::istringstream iss(args);
		std::string slotTok, qtyTok;
		iss >> slotTok;
		iss >> qtyTok;  // optional; empty if not present

		uint32_t slot = 0;
		if (!ParseVendorSlot(slotTok, slot)) {
			AddChatSystemMessage("Invalid slot - see /vendorlist");
			return;
		}

		uint8_t quantity = 1;  // default, per the plan ("optional quantity")
		if (!qtyTok.empty() && !ParseVendorQuantity(qtyTok, quantity)) {
			AddChatSystemMessage("Invalid quantity (must be 1-255)");
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
		// on the next GameTick(), which (T45) now runs in headless mode too.
		// The ONE hop that differs: WHO calls pushIntent() and on which
		// thread. The gesture's caller is renderer code on the render
		// thread, crossing to the game thread via the bridge. Here the
		// caller is EverQuest itself (m_command_registry's handler, which
		// piped-command dispatch invokes on the GAME thread -- see
		// Application::DispatchSlashCommand()/dispatchToActionOrRenderer()'s
		// registry-1 stage), so push and the later drain both happen on the
		// game thread. GameStateBridge::pushIntent()'s mutex makes this safe
		// either way; it is an honest difference in the calling thread, not
		// a gap in what gets dispatched or how.
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorBuyIntent{
				GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
		} else {
			AddChatSystemMessage("Cannot buy - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(buy_cmd);

	Command sell_cmd;
	sell_cmd.name = "sell";
	sell_cmd.usage = "/sell <slot> [quantity]";
	sell_cmd.description = "Sell an item from your inventory to the open vendor";
	sell_cmd.category = "Commerce";
	sell_cmd.handler = [this](const std::string& args) {
		if (!IsVendorWindowOpen()) {
			AddChatSystemMessage("You are not at a vendor.");
			return;
		}
		if (args.empty()) {
			AddChatSystemMessage("Usage: /sell <slot> [quantity] - <slot> is your inventory slot");
			return;
		}

		std::istringstream iss(args);
		std::string slotTok, qtyTok;
		iss >> slotTok;
		iss >> qtyTok;

		uint32_t slot = 0;
		if (!ParseVendorSlot(slotTok, slot)) {
			AddChatSystemMessage("Invalid slot");
			return;
		}

		uint8_t quantity = 1;
		if (!qtyTok.empty() && !ParseVendorQuantity(qtyTok, quantity)) {
			AddChatSystemMessage("Invalid quantity (must be 1-255)");
			return;
		}

		// Same fidelity note as /buy above: pushes the SAME VendorSellIntent
		// the vendor-window gesture pushes, drained by the same
		// ProcessBridgeIntents() -> SellToVendor() dispatch. Item-in-slot /
		// NO_DROP / stack-size validation stays entirely inside SellToVendor()
		// (eq_merchant.cpp) -- unchanged, per the plan's fence against
		// touching that function -- so this handler does not duplicate it.
		if (m_bridge) {
			m_bridge->pushIntent(eqt::events::VendorSellIntent{
				GetVendorNpcId(), static_cast<uint16_t>(slot), quantity});
		} else {
			AddChatSystemMessage("Cannot sell - no bridge attached.");
		}
	};
	m_command_registry->registerCommand(sell_cmd);
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
