#pragma once

// Batch K (refactor/K_component_extraction.md), unit K05.
//
// The merchant family, lifted out of class EverQuest: inventory item
// movement, spell-scroll scribing, book/note reading, player-to-player
// trade, loot transfer, vendor, bank, skill trainer and tradeskill recipes.
// Every body in src/client/merchant/{merchant,merchant_trade,
// merchant_vendor_bank}.cpp is the body that used to live in
// src/client/eq_merchant{,_trade,_vendor_bank}.cpp, moved verbatim; the only
// edits inside a body are the substitutions §Mechanism 4 allows
// (m_x -> ctx_.x / owner_.m_x, Foo() -> owner_.Foo(), an EverQuest static ->
// EverQuest::Static).
//
// `owner_` is the TRANSITION handle: the moved code still reaches EverQuest
// members and methods that have not been extracted yet -- the loot cluster
// (m_loot_items, m_pending_loot_slots, whose owner K11 decides), the
// m_pending_scribe_* trio (written here,
// read by the OP_MemorizeSpell arm of DispatchZonePacket, which is K09's), and
// the output/accessor primitives AddChatSystemMessage(), QueuePacket(),
// UpdateInventoryStats(), GetLevel()/GetClass()/GetPlatinum()/... EverQuest
// declares `friend class eqt::merchant::MerchantComponent` for exactly that.
// K-final removes both the handle and the friendship.
//
// What DID move as state: m_trainer_skill_caps (T280). It is referenced by
// this family only -- the one hit elsewhere is a comment in
// action/action_dispatcher_guild_misc.cpp -- and its reader
// GetTrainerSkillCap() moved with it.

#include "client/client_context.h"

#include <cstdint>
#include <string>
#include <vector>

class EverQuest;

namespace EQ {
namespace Net {
class Packet;
}
}

namespace EQT {
struct TradeRequest_Struct;
struct TradeRequestAck_Struct;
struct MoveCoin_Struct;
struct TradeAcceptClick_Struct;
struct CancelTrade_Struct;
}

namespace eqt {
namespace merchant {

class MerchantComponent {
public:
	MerchantComponent(ClientContext& ctx, EverQuest& owner) : ctx_(ctx), owner_(owner) {}

	// ---- Inventory packet handlers and senders (were private on EverQuest)
	void ZoneProcessCharInventory(const EQ::Net::Packet &p);
	void ZoneProcessMoveItem(const EQ::Net::Packet& p);
	void ZoneProcessDeleteItem(const EQ::Net::Packet& p);
	void SetupInventoryCallbacks();
	void SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity);
	void SendDeleteItem(int16_t slot);
	// T221: source-side twin of the InventorySlotChanged occupancy projection.
	void SyncInventorySlotToGameState(int16_t slot, bool hasItem);

	// ---- Spell scribing from scroll
	void ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot);
	// T276: the one scribe-refusal string table, shared by /scribe's
	// pre-flight and ScribeSpellFromScroll()'s guard. Empty return = no refusal.
	std::string ScribeRefusalMessage(uint32_t spellId);

	// ---- Loot packet handlers (Player mode)
	void ZoneProcessLootItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessLootedItemToInventory(const EQ::Net::Packet& p);
	void SetupLootCallbacks();

	// ---- Vendor packet handlers (Player mode)
	void ZoneProcessShopRequest(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerBuy(const EQ::Net::Packet& p);
	void ZoneProcessShopPlayerSell(const EQ::Net::Packet& p);
	void ZoneProcessShopEndConfirm(const EQ::Net::Packet& p);
	void ZoneProcessVendorItemToUI(const EQ::Net::Packet& p);
	void ZoneProcessMoneyUpdate(const EQ::Net::Packet& p);
	void SetupVendorCallbacks();
	void SetupBankCallbacks();

	// ---- Skill trainer packet handlers and send functions
	void ZoneProcessGMTraining(const EQ::Net::Packet& p);
	void SetupTrainerCallbacks();

	// ---- Trade packet handlers and send functions
	void SetupTradeManagerCallbacks();
	void SetupTradeWindowCallbacks();
	bool ZoneProcessTradePartnerItem(const EQ::Net::Packet& p);
	void SendTradeRequest(const EQT::TradeRequest_Struct& req);
	void SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack);
	void SendMoveCoin(const EQT::MoveCoin_Struct& move);
	void SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept);
	void SendCancelTrade(const EQT::CancelTrade_Struct& cancel);

	// ---- Tradeskill container callbacks
	void SetupTradeskillCallbacks();

	// ---- Book/Note reading packet handlers
	void ZoneProcessReadBook(const EQ::Net::Packet& p);
	void SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename);
	// T273: split the server's book text into chat lines. Public entry is
	// ZoneProcessReadBook; exposed here only so the handler stays readable.
	void SurfaceBookText(const std::string& text);

	// ---- Vendor window methods (Player mode with graphics)
	void RequestOpenVendor(uint16_t npcId);
	void BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity);
	void CloseVendorWindow();

	// ---- Bank window methods (Player mode with graphics)
	void OpenBankWindow(uint16_t bankerNpcId = 0);
	void CloseBankWindow();

	// ---- Skill trainer window methods (Player mode with graphics)
	void RequestTrainerWindow(uint16_t npcId);
	void TrainSkill(uint8_t skillId);
	void CloseTrainerWindow();
	// T280: the trainer session's cap for one skill id (0 = this trainer
	// does not teach it, or there is no session).
	uint32_t GetTrainerSkillCap(uint8_t skillId) const;
	// T280: eqemu's own training price in copper for a skill currently at
	// `currentValue` (Client::OPGMTrainSkill). Static: it is arithmetic, and
	// the trainer window and /train both quote it.
	static uint32_t TrainingCost(uint32_t currentValue);

	// ---- T269: /bank deposit|withdraw|convert. Push the kept
	// BankCurrencyMoveIntent / BankCurrencyConvertIntent (the same shape a
	// bank-window coin gesture would push) for ProcessBridgeIntents() to turn
	// into OP_MoveCoin. Refuse (message, no intent) when the bank window is
	// not open.
	void RequestBankCoinMove(uint8_t coinType, int32_t amount, bool fromBank);
	void RequestBankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount);

	// ---- Book/Note reading methods (Player mode with graphics)
	void RequestReadBook(const std::string& filename, uint8_t type = 0);

	// ---- Phase 4: Tradeskills (recipe senders and their replies)
	void SendRecipesFavorite(uint32_t object_type, const std::vector<uint32_t>& favorites);
	void SendRecipesSearch(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial);
	void SendRecipeDetails(uint32_t recipe_id);
	void SendRecipeAutoCombine(uint32_t object_type, uint32_t recipe_id);
	void ZoneProcessRecipeReply(const EQ::Net::Packet &p);
	void ZoneProcessRecipeAutoCombine(const EQ::Net::Packet &p);

private:
	ClientContext& ctx_;
	EverQuest& owner_;

	// Trainer state, moved out of EverQuest: referenced by this family only.
	// T280: the per-skill CAPS the server sent in its OP_GMTraining reply
	// (GMTrainee_Struct::skills[], one entry per skill id). They were read
	// once to build the TrainerWindowOpened event and then thrown away, so
	// /train had nothing to check a request against. Kept for the life of
	// the trainer session; cleared by CloseTrainerWindow().
	std::vector<uint32_t> m_trainer_skill_caps;
};

}  // namespace merchant
}  // namespace eqt
