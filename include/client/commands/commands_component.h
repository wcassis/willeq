#pragma once

// Batch K (refactor/K_component_extraction.md), unit K12.
//
// The commands + combat_actions family, lifted out of class EverQuest: the
// game-registry slash-command driver and its surviving Request*() thin
// wrappers (src/client/commands/{commands_component,commands_movement,
// commands_spells,commands_train}.cpp, was eq_commands{,_movement,_spells,
// _train}.cpp), plus the Consider/Action/Damage/MoneyOnCorpse/Stun combat
// packet handlers (src/client/commands/combat_actions.cpp, was
// eq_combat_actions.cpp) -- small enough to ride along as this component's
// "combat actions" section, per the batch plan. Every body is the body that
// used to live in those files, moved verbatim; the only edits inside a body
// are the substitutions Sec.Mechanism 4 allows (m_x -> ctx_.x / owner_.m_x,
// Foo() -> owner_.Foo() / owner_.<Family>().Foo() for an already-extracted
// family, an EverQuest static -> EverQuest::Static).
//
// T341: the `owner_` TRANSITION handle and the
// `friend class eqt::commands::CommandsComponent` line are BOTH GONE. K13
// took this family to zero owner_.m_ reaches; its last two handle uses were
// the PUBLIC string lookups, now ctx_.strings, and its last EverQuest-scoped
// private name was s_debug_level, now the public EverQuest::GetDebugLevel()
// (the K13 (jj) substitution). Both axes of the K13 (ii) friend test are
// empty. Sibling families are reached as ctx_.Merchant()/Movement()/
// LoginWorld()/Entities().
//
// What DID move as state: m_stunned_until (single-family, combat) and
// m_vendor_items/GameVendorItem (shared with Merchant since K05, which left
// it on EverQuest with an explicit "read by eq_commands.cpp, K12" note --
// this is the last family using it, so Sec.Mechanism 3's rule moves it here;
// Merchant's three writers (merchant.cpp, merchant_trade.cpp,
// merchant_vendor_bank.cpp) reach it through the new VendorItems() accessor,
// same shape as K09's ZoneComponent::MovementHistory()).
//
// runLoadDiagnostics() has ZERO CALLERS tree-wide (confirmed dead since
// T105/T108, per the comments in renderer_slash_commands.cpp and
// commands_spells.cpp) -- it looks like a Sec.Mechanism 5 delete, and a
// first pass deleted it, but it is the SOLE PUBLISHER of the
// DiagnosticsSceneDump GameEventType (irrlicht_bridge.cpp is still a live
// consumer): deleting it failed
// ReachabilitySweep.EveryEventIsPublishedAndConsumed. Sec.Mechanism 5 is
// about callers of a MOVED METHOD; it has no rule for a dead method that is
// the last publisher of a still-consumed event, and deleting one changes
// observable behaviour (an event stops firing) in a unit whose charter is
// "no behaviour change." KEPT, private, moved verbatim -- see the K12
// Review. runPmemDiagnostics() looked like the same zero-caller case at
// first glance -- the plan listed both as candidates -- but has ONE real
// in-family caller (the /pmem command handler in commands_spells.cpp), so
// it was never in question.

#include "client/client_context.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace eqt {
namespace ui {
class CommandRegistry;
}
namespace inventory {
struct ItemInstance;
}
}

namespace eqt {
namespace commands {

class CommandsComponent {
public:
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit CommandsComponent(ClientContext& ctx) : ctx_(ctx) {}

	// Driver, called once from EverQuest's ctor (eq.cpp), same shape as
	// MerchantComponent's SetupInventoryCallbacks()/SetupTradeManagerCallbacks().
	void RegisterCommands();

	// T143: read-only access to this registry's catalog. Was
	// EverQuest::GetCommandRegistry(); the one external caller
	// (application_commands.cpp) reaches it via Commands().
	const eqt::ui::CommandRegistry* GetCommandRegistry() const { return ctx_.command_registry.get(); }

	// T136: vendor/buy/sell -- were public on EverQuest; callers go through
	// Commands() (eq_action_handler_extended.cpp).
	void RequestVendorToggle();
	void RequestVendorBuy(uint32_t slot, uint8_t quantity);
	void RequestVendorSell(uint32_t slot, uint8_t quantity);

	// T141: /equip's thin orchestration entry point.
	void RequestEquip(const std::string& args);

	// T273/T276: /read and /scribe.
	void RequestReadItem(const std::string& args);
	void RequestScribeItem(const std::string& args);

	// B442 (#151): /pickup /putdown /cursor /coin (commands_cursor.cpp).
	void RequestCursorCommand(const std::string& verb, const std::string& args);

	// T278: /combine and /closecontainer.
	void RequestTradeskillCombine(int16_t containerSlot);
	void RequestCloseTradeskillContainer();

	// T140: warp/time-of-day thin wrappers.
	void RequestWarp(const std::string& args);
	// T362 (B070): the entity form of /warp, already resolved to a spawn id
	// by ActionDispatcher::warp() with the ranked nearest-first lookup.
	void RequestWarpToEntity(uint16_t spawn_id);
	void RequestTimeOfDay(const std::string& args);

	// T140: bank/train thin wrappers.
	void RequestBankToggle();
	void RequestTraining();

	// T280: /train <skill|id> and /trainclose.
	void RequestTrainSkill(const std::string& args);
	void RequestCloseTrainer();

	// T247: true while an instantaneous OP_Stun is in force. Buff-borne
	// stuns are BuffManager's -- see combat_actions.cpp's file banner.
	bool IsStunned() const;

	// Combat packet handlers. Were private on EverQuest; DispatchZonePacket
	// (zone_dispatch.cpp) now calls owner_.Commands().ZoneProcess*() directly,
	// same shape as GmComponent's ZoneProcessGM* -- see the K02 Review.
	void ZoneProcessConsider(const EQ::Net::Packet &p);
	void ZoneProcessAction(const EQ::Net::Packet &p);
	void ZoneProcessDamage(const EQ::Net::Packet &p);
	void ZoneProcessStun(uint32_t duration_ms);  // T247
	void ZoneProcessMoneyOnCorpse(const EQ::Net::Packet &p);

	// K05 (Batch K): the vendor window's slot->item map. Was EverQuest's
	// m_vendor_items/GameVendorItem, left there by MerchantComponent (K05)
	// with an explicit note that K12 is the last family using it. Merchant's
	// three writers reach this through the same accessor, matching K09's
	// ZoneComponent::MovementHistory() shape.
	struct GameVendorItem {
		uint32_t itemId = 0;
		std::string name;
		int32_t price = 0;        // Price in copper
		int32_t quantity = -1;    // -1 = unlimited
	};
	std::map<uint32_t, GameVendorItem>& VendorItems() { return m_vendor_items; }

private:
	// G02: per-domain command registration, called only from RegisterCommands()
	// above (in-family).
	void RegisterSocialCommands();
	void RegisterMovementCommands();
	void RegisterSpellCommands();

	// Manual zone load diagnostics helpers. runPmemDiagnostics() has an
	// in-family caller (the /pmem command handler, commands_spells.cpp);
	// runLoadDiagnostics() has ZERO callers anywhere but is kept -- it is the
	// sole publisher of DiagnosticsSceneDump. See the file banner above.
	void runPmemDiagnostics(const std::string& label);
	void runLoadDiagnostics(const std::string& label);

	// T276: shared slot|name resolution for /read and /scribe. In-family
	// callers only (RequestReadItem/RequestScribeItem above).
	const eqt::inventory::ItemInstance* ResolveCarriedItem(
		const std::string& args, const char* verb, int16_t& slotOut);

	// T280: /train list. In-family caller only (RequestTrainSkill above).
	void ListTrainableSkills();

	ClientContext& ctx_;

	// T247: expiry of the current OP_Stun; epoch (the default) = not stunned.
	std::chrono::steady_clock::time_point m_stunned_until{};

	std::map<uint32_t, GameVendorItem> m_vendor_items;  // slot -> item data (game-side)
};

}  // namespace commands
}  // namespace eqt
