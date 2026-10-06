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
// T341: the `owner_` TRANSITION handle and the
// `friend class eqt::merchant::MerchantComponent` line are BOTH GONE. K13
// took this family to zero owner_.m_ reaches; its last two handle uses were
// the PUBLIC string lookups, now ctx_.strings, and its last EverQuest-scoped
// private name was s_debug_level, now the public EverQuest::GetDebugLevel()
// (the K13 (jj) substitution). Both axes of the K13 (ii) friend test are
// empty. The loot cluster lives in eqt::bridge::BridgeComponent (K11) and the
// m_pending_scribe_* trio in eqt::zone::ZoneComponent (K09), reached through
// ctx_.Bridge()/ctx_.Zone().
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

namespace eqt { namespace state { struct TradeskillRecipeReceivedData; } }

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
	// T341: the EverQuest& transition handle is GONE -- this family reaches
	// zero EverQuest private members and names no EverQuest-scoped private
	// name (the two-axis rule, K13 correction (ii) / B039). It is the
	// second component to pass both axes, after eqt::gm::GmComponent.
	explicit MerchantComponent(ClientContext& ctx) : ctx_(ctx) {}

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
	// B898 (#214), merchant_trainer.cpp. Build TrainerWindowOpenedData from
	// the kept caps + the current skill values, practices and coin, and push
	// it. Called by ZoneProcessGMTraining (open) and by the OP_SkillUpdate arm
	// while a session is open (refresh) -- the trainer window changes only
	// when the server has said something. No-op with no session.
	void PublishTrainerWindow();
	// B898 (#214), merchant_trainer.cpp. The refusals the client can make
	// from the session the server opened, shared by /train <skill|id> and the
	// trainer window's Train button (the TrainSkillIntent consumer): returns
	// the message, or "" when the server should be asked. Each mirrors a
	// SILENT server refusal (no session, no practices, not taught here,
	// already at this trainer's cap) and none is stricter than the server.
	std::string TrainRefusal(uint8_t skillId) const;

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
	// T419 (board B335): the tradeskill recipe list's boundary marker. The
	// server never says a reply run has started, so the two senders that ask
	// for a list publish this as they go out.
	void PublishRecipeListCleared(uint32_t object_type, const std::string& query,
	                              uint32_t mintrivial, uint32_t maxtrivial,
	                              bool is_favorites);

	ClientContext& ctx_;

	// Trainer state, moved out of EverQuest: referenced by this family only.
	// T280: the per-skill CAPS the server sent in its OP_GMTraining reply
	// (GMTrainee_Struct::skills[], one entry per skill id). They were read
	// once to build the TrainerWindowOpened event and then thrown away, so
	// /train had nothing to check a request against. Kept for the life of
	// the trainer session; cleared by CloseTrainerWindow().
	std::vector<uint32_t> m_trainer_skill_caps;
};

/**
 * Decode ONE OP_RecipeReply body into the bridge payload (T419, board B335).
 *
 * A free function, and not a private method, so a test can drive the real
 * wire bytes through the real decode with no ClientContext, no bridge and no
 * server -- the decode is the half B335 was missing and the half worth
 * pinning. `data`/`length` are the packet MINUS its two opcode bytes.
 * Returns false (and leaves `out` alone) for anything shorter than the
 * struct's 84 bytes, which is the same gate ZoneProcessRecipeReply applies.
 */
bool DecodeRecipeReply(const void* data, size_t length,
                       eqt::state::TradeskillRecipeReceivedData& out);

}  // namespace merchant
}  // namespace eqt
