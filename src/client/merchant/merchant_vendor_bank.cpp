// eqt::merchant::MerchantComponent -- vendor-window, bank, GM trainer and
// tradeskill-recipe cluster.
//
// Split out of eq_merchant.cpp (Batch J, unit J27), which was itself split
// out of eq.cpp (Batch F, unit F05): the vendor UI packet and the vendor
// open/buy/sell/close senders, the bank window, the trainer callbacks with
// GM training, and the Phase 4 tradeskill recipe senders/handlers. Moved
// verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.
//
// K05 (Batch K, refactor/K_component_extraction.md) moved it again, from
// src/client/eq_merchant_vendor_bank.cpp, and rebased the bodies from EverQuest onto
// eqt::merchant::MerchantComponent. Still verbatim -- only the substitutions
// Mechanism 4 allows (core members -> ctx_..., the loot/vendor/scribe members
// that stay on EverQuest -> owner_.m_..., the accessors and output primitives
// that stay behind -> owner_.Foo(), and EverQuest's statics s_debug_level,
// DumpPacket and FormatPrice qualified as EverQuest:: -- friendship grants
// access, not scope). See the K05 Review.
//
// T341 (board B038): the "stay on EverQuest" clauses above are HISTORY, not
// the current shape. This family now reaches EverQuest not at all -- the
// `owner_` handle and the `friend class` line are both gone; see the T341
// note in this family's component header.

#include "client/merchant/merchant_component.h"

#include "client/eq.h"
#include "client/combat.h"
#include "client/trade_manager.h"
#include "client/spell/spell_manager.h"
#include "client/skill/skill_manager.h"
#include "common/logging.h"
#include "common/packet_structs.h"
#include "common/name_utils.h"
#include "client/bridge/game_state_bridge.h"

// P28: renderer-free inventory data model, used unconditionally below
// (m_inventory_manager->processCharInventory).
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/item_instance.h"

#ifdef WITH_AUDIO
#include "client/audio/audio_manager.h"
#endif

#include <cstring>
#include <cctype>
#include <algorithm>
#include <vector>

void eqt::merchant::MerchantComponent::ZoneProcessVendorItemToUI(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI called, packet length={}", p.Length());

	// Parse the serialized item data for the vendor window using TitaniumItemParser
	if (p.Length() < 10) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI: Packet too short, ignoring");
		return;
	}

	// Skip the first 2 bytes (opcode header) and 4 bytes (item packet type = 100)
	const char* data = (const char*)p.Data() + 2 + 4;
	size_t data_len = p.Length() - 2 - 4;

	if (data_len == 0) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessVendorItemToUI: No item data");
		return;
	}

	// Use TitaniumItemParser to properly parse the item data
	int16_t slotId = eqt::inventory::SLOT_INVALID;
	auto item = eqt::inventory::TitaniumItemParser::parseItem(data, data_len, slotId);

	if (!item) {
		LOG_WARN(MOD_INVENTORY, "Failed to parse vendor item");
		return;
	}

	// slotId from parser is the vendor slot number
	uint32_t vendorSlot = static_cast<uint32_t>(slotId);
	LOG_DEBUG(MOD_INVENTORY, "Creating vendor item: slot={} name='{}'", vendorSlot, item->name);

	// Track vendor item in game state (D18a: game-side vendor item tracking)
	eqt::commands::CommandsComponent::GameVendorItem gvi;
	gvi.itemId = item->itemId;
	gvi.name = item->name;
	gvi.price = item->price;
	gvi.quantity = item->quantity;
	ctx_.Commands().VendorItems()[vendorSlot] = std::move(gvi);

	// Publish VendorItemAdded (D18a: bridge is sole path to renderer)
	if (ctx_.bridge) {
		eqt::state::VendorItemAddedData vdata;
		vdata.vendorSlot = static_cast<uint16_t>(vendorSlot);
		vdata.itemId = item->itemId;
		vdata.itemName = item->name;
		vdata.price = item->price;
		vdata.quantity = item->quantity;
		// T436 (board B429): the icon the merchant window's icon column
		// draws. Parsed into the ItemInstance above and dropped until now.
		vdata.iconId = item->icon;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorItemAdded, std::move(vdata)));
	}
}

void eqt::merchant::MerchantComponent::RequestOpenVendor(uint16_t npcId)
{
	if (ctx_.game_state.player().vendorNpcId() != 0) {
		LOG_DEBUG(MOD_INVENTORY, "Already in vendor session with NPC {}", ctx_.game_state.player().vendorNpcId());
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Requesting vendor open for NPC {}", npcId);

	// Send Merchant_Click_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Click_Struct req;
	memset(&req, 0, sizeof(req));
	req.npc_id = npcId;
	req.player_id = ctx_.self.MySpawnId();
	pkt.PutData(0, &req, sizeof(req));
	ctx_.net.Queue(HC_OP_ShopRequest, &pkt);
}

void eqt::merchant::MerchantComponent::BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (ctx_.game_state.player().vendorNpcId() == 0 || ctx_.game_state.player().vendorNpcId() != npcId) {
		LOG_WARN(MOD_INVENTORY, "BuyFromVendor: Not in vendor session with NPC {}", npcId);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Buying from vendor {}: slot={} quantity={}", npcId, itemSlot, quantity);

	// Send Merchant_Purchase_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Purchase_Struct buy;
	buy.npc_id = npcId;
	buy.player_id = ctx_.self.MySpawnId();
	buy.itemslot = itemSlot;
	buy.unknown12 = 0;
	buy.quantity = quantity;
	buy.action = EQT::MERCHANT_BUY;
	pkt.PutData(0, &buy, sizeof(buy));
	ctx_.net.Queue(HC_OP_ShopPlayerBuy, &pkt);
}

void eqt::merchant::MerchantComponent::SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (ctx_.game_state.player().vendorNpcId() == 0 || ctx_.game_state.player().vendorNpcId() != npcId) {
		LOG_WARN(MOD_INVENTORY, "SellToVendor: Not in vendor session with NPC {}", npcId);
		ctx_.out.System("You are not interacting with a vendor.");
		return;
	}

	// Validate the item exists and can be sold
	if (ctx_.inventory_manager) {
		const auto* item = ctx_.inventory_manager->getItem(static_cast<int16_t>(itemSlot));
		if (!item) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: No item at slot {}", itemSlot);
			ctx_.out.System("That item is no longer in your inventory.");
			return;
		}
		if (item->noDrop) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: Item '{}' is NO_DROP", item->name);
			ctx_.out.System(fmt::format("You cannot sell {}.", item->name));
			return;
		}
		if (item->stackable && static_cast<int32_t>(quantity) > item->quantity) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: Requested quantity {} exceeds stack size {}",
				quantity, item->quantity);
			quantity = static_cast<uint32_t>(item->quantity);
		}
	}

	LOG_DEBUG(MOD_INVENTORY, "Selling to vendor {}: slot={} quantity={}", npcId, itemSlot, quantity);

	// Send Merchant_Sell_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Sell_Struct sell;
	sell.npc_id = npcId;
	sell.itemslot = itemSlot;
	sell.quantity = quantity;
	sell.unknown12 = 0;  // Server ignores this field
	pkt.PutData(0, &sell, sizeof(sell));
	ctx_.net.Queue(HC_OP_ShopPlayerSell, &pkt);
}

void eqt::merchant::MerchantComponent::CloseVendorWindow()
{
	if (ctx_.game_state.player().vendorNpcId() == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing vendor window for NPC {}", ctx_.game_state.player().vendorNpcId());

	// Send Merchant_End_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_End_Struct end;
	end.npc_id = ctx_.game_state.player().vendorNpcId();
	end.player_id = ctx_.self.MySpawnId();
	pkt.PutData(0, &end, sizeof(end));
	ctx_.net.Queue(HC_OP_ShopEnd, &pkt);

	// T286: capture before clearing the single store (see ShopEndConfirm).
	const uint16_t closedVendorNpcId = ctx_.game_state.player().vendorNpcId();

	// T227: source write (see ZoneProcessShopRequest).
	ctx_.game_state.player().clearVendor();

	// Publish VendorWindowClosed
	if (ctx_.bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = closedVendorNpcId;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorWindowClosed, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Stop vendor/bank music (context-based, will restore zone music or auto-attack music)
	if (ctx_.audio_manager) {
		ctx_.audio_manager->stopVendorBankMusic();
	}
#endif

	ctx_.Commands().VendorItems().clear();
}

void eqt::merchant::MerchantComponent::OpenBankWindow(uint16_t bankerNpcId)
{
	if (ctx_.game_state.player().bankerNpcId() != 0) {
		// Already have bank open, close it first
		CloseBankWindow();
	}

	// Use a dummy NPC ID if none provided (for /bank command testing)
	// In real use, this would be the banker NPC's spawn ID
	const uint16_t bankerId = (bankerNpcId != 0) ? bankerNpcId : 1;

	LOG_DEBUG(MOD_INVENTORY, "Opening bank window (banker NPC: {})", bankerId);

	// T227: source write, ABOVE the bridge guard (see ZoneProcessShopRequest).
	// T286: and the ONLY write -- EverQuest keeps no banker copy.
	ctx_.game_state.player().setBanker(bankerId);

	// Publish BankWindowOpened
	if (ctx_.bridge) {
		eqt::state::WindowOpenedData data;
		data.npcId = ctx_.game_state.player().bankerNpcId();
		auto it = ctx_.entities.find(ctx_.game_state.player().bankerNpcId());
		data.npcName = (it != ctx_.entities.end()) ? EQT::toDisplayName(it->second.name) : "Banker";
		data.sellRate = 0.0f;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::BankWindowOpened, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Start vendor/bank music (context-based, will restore on close)
	if (ctx_.audio_manager) {
		ctx_.audio_manager->startVendorBankMusic();
	}
#endif

	ctx_.out.System("Bank window opened");
}

void eqt::merchant::MerchantComponent::CloseBankWindow()
{
	if (ctx_.game_state.player().bankerNpcId() == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing bank window (banker NPC: {})", ctx_.game_state.player().bankerNpcId());

	// T286: capture before clearing the single store (see ShopEndConfirm).
	const uint16_t closedBankerNpcId = ctx_.game_state.player().bankerNpcId();

	// T227: source write (see ZoneProcessShopRequest).
	ctx_.game_state.player().clearBanker();

	// Publish BankWindowClosed (carrying the banker we were AT)
	if (ctx_.bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = closedBankerNpcId;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::BankWindowClosed, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Stop vendor/bank music (context-based, will restore zone music or auto-attack music)
	if (ctx_.audio_manager) {
		ctx_.audio_manager->stopVendorBankMusic();
	}
#endif

	ctx_.out.System("Bank window closed");
}
// ============================================================================
// Skill Trainer Window Methods
// ============================================================================

void eqt::merchant::MerchantComponent::SetupTrainerCallbacks()
{
	/* D20b1: WindowManager pushes TrainSkillIntent/CloseTrainerIntent via bridge */
}

// T280: eqemu's training price, in copper, for raising a skill that is
// currently at `currentValue` (Client::OPGMTrainSkill, zone/client_process.cpp).
// Verbatim, including the integer division and the free first ten points.
uint32_t eqt::merchant::MerchantComponent::TrainingCost(uint32_t currentValue)
{
	if (currentValue <= 10) {
		return 0;
	}
	const uint32_t adjusted = currentValue - 10;
	return (adjusted * adjusted * adjusted) / 100;
}

// T280: the open trainer session's cap for one skill (0 = not taught here,
// or no session).
uint32_t eqt::merchant::MerchantComponent::GetTrainerSkillCap(uint8_t skillId) const
{
	if (skillId >= m_trainer_skill_caps.size()) {
		return 0;
	}
	return m_trainer_skill_caps[skillId];
}

void eqt::merchant::MerchantComponent::ZoneProcessGMTraining(const EQ::Net::Packet& p)
{
	// Server response to our training request
	// GMTrainee_Struct: npcid(4), playerid(4), skills[100](400), unknown408[40](40) = 448 bytes
	if (p.Length() < sizeof(EQT::GMTrainee_Struct) + 2) {
		LOG_WARN(MOD_MAIN, "GMTraining packet too short: {} bytes", p.Length());
		return;
	}

	const auto* trainee = reinterpret_cast<const EQT::GMTrainee_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	uint32_t npc_id = trainee->npcid;
	uint32_t player_id = trainee->playerid;

	LOG_DEBUG(MOD_MAIN, "GMTraining response: npc_id={} player_id={}", npc_id, player_id);

	// Store trainer state
	const uint16_t trainerNpcId = static_cast<uint16_t>(npc_id);

	// Get trainer name from entity if available
	auto it = ctx_.entities.find(trainerNpcId);
	const std::string trainerName = (it != ctx_.entities.end())
		? EQT::toDisplayName(it->second.name)
		: std::string("Trainer");

	// T227: source write, ABOVE the bridge guard (see ZoneProcessShopRequest).
	// T286: and the ONLY write -- EverQuest keeps no trainer copy.
	ctx_.game_state.player().setTrainer(trainerNpcId, trainerName);

	// Build list of trainable skills and publish to bridge
	eqt::state::TrainerWindowOpenedData trainerData;
	trainerData.npcId = ctx_.game_state.player().trainerNpcId();
	trainerData.npcName = ctx_.game_state.player().trainerName();
	trainerData.platinum = ctx_.self.GetPlatinum();
	trainerData.gold = ctx_.self.GetGold();
	trainerData.silver = ctx_.self.GetSilver();
	trainerData.copper = ctx_.self.GetCopper();
	trainerData.practicePoints = ctx_.self.GetPracticePoints();

	// T280: keep the caps for the life of the session. /train has to answer
	// "does this trainer teach that, and am I already at its cap" before it
	// puts anything on the wire, and this reply is the only place the
	// answer ever arrives.
	m_trainer_skill_caps.assign(trainee->skills, trainee->skills + EQT::MAX_PP_SKILL);

	for (uint8_t skill_id = 0; skill_id < EQT::MAX_PP_SKILL; ++skill_id) {
		uint32_t max_trainable = trainee->skills[skill_id];

		// Skip skills with max_trainable = 0 (not trainable at this trainer)
		if (max_trainable == 0) {
			continue;
		}

		// Get current skill value
		uint32_t current_value = 0;
		if (ctx_.skill_manager) {
			current_value = ctx_.skill_manager->getSkillValue(skill_id);
		}

		// Skip skills already at or above max
		if (current_value >= max_trainable) {
			continue;
		}

		// Get skill name
		const char* skill_name = EQ::getSkillName(skill_id);

		// Training cost, in COPPER. T280 corrected this: the old
		// "(current_value + 1) * 10" was invented, and disagreed with the
		// server at every skill value. eqemu's Client::OPGMTrainSkill
		// (zone/client_process.cpp) is the authority and computes
		//     AdjustedSkillLevel = skilllevel - 10;
		//     if (AdjustedSkillLevel > 0)
		//         Cost = AdjustedSkillLevel^3 / 100;
		// off the CURRENT (pre-train) value -- so the first ten points of
		// any skill are free, and the price then grows cubically. Cost is
		// what the server DEDUCTS; it is not sent back on Titanium (see
		// TrainingCost() below), so this figure is the only one the player
		// ever sees and it had better match.
		uint32_t cost = TrainingCost(current_value);

		eqt::state::TrainerSkillEntryData skill;
		skill.skillId = skill_id;
		skill.name = skill_name ? skill_name : "Unknown";
		skill.currentValue = current_value;
		skill.maxTrainable = max_trainable;
		skill.cost = cost;
		trainerData.skills.push_back(std::move(skill));

		if (EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_MAIN, "  Skill {}: {} cur={} max={} cost={}",
				skill_id, skill_name, current_value, max_trainable, cost);
		}
	}

	LOG_INFO(MOD_MAIN, "Trainer window opened for {} (id={}) with {} trainable skills",
		ctx_.game_state.player().trainerName(), ctx_.game_state.player().trainerNpcId(), trainerData.skills.size());

	// Publish TrainerWindowOpened event to bridge
	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TrainerWindowOpened, std::move(trainerData)));
	}
}

void eqt::merchant::MerchantComponent::RequestTrainerWindow(uint16_t npcId)
{
	if (ctx_.game_state.player().trainerNpcId() != 0) {
		LOG_DEBUG(MOD_MAIN, "Already in trainer session with NPC {}", ctx_.game_state.player().trainerNpcId());
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Requesting trainer window for NPC {}", npcId);

	// Send GMTrainee_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainee_Struct req;
	memset(&req, 0, sizeof(req));
	req.npcid = npcId;
	req.playerid = ctx_.self.MySpawnId();
	pkt.PutData(0, &req, sizeof(req));
	ctx_.net.Queue(HC_OP_GMTraining, &pkt);
}

void eqt::merchant::MerchantComponent::TrainSkill(uint8_t skillId)
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		LOG_WARN(MOD_MAIN, "TrainSkill: Not in trainer session");
		ctx_.out.System("You are not interacting with a trainer.");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Training skill {} with trainer {}", skillId, ctx_.game_state.player().trainerNpcId());

	// Send GMSkillChange_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMSkillChange_Struct train;
	memset(&train, 0, sizeof(train));
	train.npcid = static_cast<uint16_t>(ctx_.game_state.player().trainerNpcId());
	train.skillbank = 0;  // 0 = normal skills, 1 = languages
	train.skill_id = skillId;
	pkt.PutData(0, &train, sizeof(train));
	ctx_.net.Queue(HC_OP_GMTrainSkill, &pkt);
}

void eqt::merchant::MerchantComponent::CloseTrainerWindow()
{
	if (ctx_.game_state.player().trainerNpcId() == 0) {
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Closing trainer window for NPC {}", ctx_.game_state.player().trainerNpcId());

	// Send GMTrainEnd_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainEnd_Struct end;
	end.npcid = ctx_.game_state.player().trainerNpcId();
	end.playerid = ctx_.self.MySpawnId();
	pkt.PutData(0, &end, sizeof(end));
	ctx_.net.Queue(HC_OP_GMEndTraining, &pkt);

	// T286: capture before clearing the single store (see ShopEndConfirm).
	const uint16_t closedTrainerNpcId = ctx_.game_state.player().trainerNpcId();

	// T227: source write (see ZoneProcessShopRequest).
	ctx_.game_state.player().clearTrainer();

	// Publish TrainerWindowClosed event to bridge (carrying the trainer we
	// were AT)
	if (ctx_.bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = closedTrainerNpcId;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TrainerWindowClosed, std::move(data)));
	}

	m_trainer_skill_caps.clear();  // T280
}
// ============================================================================
// Phase 4: Tradeskill Recipes
// ============================================================================

// T149 FINDING (source-verified against eqemu, not yet on the wire): this
// sender used to write a VARIABLE-length body of 8 + 4*favorites.size()
// bytes. eqemu's Handle_OP_RecipesFavorite (zone/client_packet.cpp) begins
// with a hard equality gate --
//     if (app->size != sizeof(TradeskillFavorites_Struct)) { LogError(...); return; }
// -- and TradeskillFavorites_Struct is { uint32 object_type; uint32 some_id;
// uint32 favorite_recipes[500]; } == 2008 bytes, fixed. So every packet this
// function has ever been capable of producing would have been dropped by the
// server with a log line and no reply. It had zero production callers, so
// nothing ever noticed. The body is now always the full 2008 bytes, zero-
// padded; eqemu skips the zero entries when building its id list.
//
// The other three recipe senders were checked the same way and are correct:
// OP_RecipesSearch is gated on == sizeof(RecipesSearch_Struct) (80, matches),
// OP_RecipeAutoCombine on == sizeof(RecipeAutoCombine_Struct) (20, matches),
// OP_RecipeDetails only on >= sizeof(uint32) (we send 4).
static constexpr size_t kTradeskillFavoritesMaxRecipes = 500;
static constexpr size_t kTradeskillFavoritesBodySize =
	sizeof(uint32_t) * 2 + sizeof(uint32_t) * kTradeskillFavoritesMaxRecipes;  // 2008

void eqt::merchant::MerchantComponent::SendRecipesFavorite(uint32_t object_type, const std::vector<uint32_t>& favorites)
{
	// Fixed-size, zero-padded body -- see the note above.
	std::vector<uint8_t> body(kTradeskillFavoritesBodySize, 0);
	auto put32 = [&body](size_t offset, uint32_t value) {
		memcpy(body.data() + offset, &value, sizeof(value));
	};
	put32(0, object_type);
	put32(4, 0);  // some_id: 0 == world combiner (an inventory container
	              // would carry its item id here; see T149's Review)

	size_t count = std::min(favorites.size(), kTradeskillFavoritesMaxRecipes);
	for (size_t i = 0; i < count; i++) {
		put32(8 + i * 4, favorites[i]);
	}

	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RecipesFavorite);
	packet.PutData(2, body.data(), body.size());

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_DEBUG(MOD_MAIN, "Sent {} favorite recipes (object_type={})", count, object_type);
		PublishRecipeListCleared(object_type, std::string(), 0, 0, true);
	}
}

void eqt::merchant::MerchantComponent::SendRecipesSearch(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RecipesSearch);

	EQT::RecipesSearch_Struct search;
	memset(&search, 0, sizeof(search));
	search.object_type = object_type;
	search.some_id = 0;
	search.mintrivial = mintrivial;
	search.maxtrivial = maxtrivial;
	strncpy(search.query, query.c_str(), 55);
	search.unknown4 = 0x00030000;
	search.unknown5 = 0x0012DD4C;
	packet.PutData(2, &search, sizeof(search));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_DEBUG(MOD_MAIN, "Recipe search: '{}'", query);
		PublishRecipeListCleared(object_type, query, mintrivial, maxtrivial, false);
	}
}

void eqt::merchant::MerchantComponent::SendRecipeDetails(uint32_t recipe_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RecipeDetails);
	packet.PutUInt32(2, recipe_id);

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_DEBUG(MOD_MAIN, "Request recipe details: {}", recipe_id);
	}
}

void eqt::merchant::MerchantComponent::SendRecipeAutoCombine(uint32_t object_type, uint32_t recipe_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RecipeAutoCombine);

	EQT::RecipeAutoCombine_Struct combine;
	combine.object_type = object_type;
	combine.some_id = 0;
	combine.unknown1 = 0;
	combine.recipe_id = recipe_id;
	combine.reply_code = 0xe16493;  // Magic request value
	packet.PutData(2, &combine, sizeof(combine));

	if (ctx_.zone_connection && ctx_.game_state.world().isZoneConnected()) {
		ctx_.net.SafeQueueZone(packet);
		LOG_DEBUG(MOD_MAIN, "Auto-combine recipe: {}", recipe_id);
	}
}

// T419 (board B335). The decode, as a free function so a test can drive the
// real wire bytes through it. Every field is RecipeReply_Struct's, verbatim --
// the packet carries no tradeskill and no component names (see the struct
// quote in src/client/graphics/ui/tradeskill_recipe_list.cpp).
bool eqt::merchant::DecodeRecipeReply(const void* data, size_t length,
                                      eqt::state::TradeskillRecipeReceivedData& out)
{
	if (data == nullptr || length < sizeof(EQT::RecipeReply_Struct)) {
		return false;
	}
	const EQT::RecipeReply_Struct* recipe =
		reinterpret_cast<const EQT::RecipeReply_Struct*>(data);
	out.objectType = recipe->object_type;
	out.someId = recipe->some_id;
	out.componentCount = recipe->component_count;
	out.recipeId = recipe->recipe_id;
	out.trivial = recipe->trivial;
	// recipe_name is strn0cpy'd by the server and need not be terminated if
	// a name is exactly 64 characters.
	out.name.assign(recipe->recipe_name,
	                strnlen(recipe->recipe_name, sizeof(recipe->recipe_name)));
	return true;
}

void eqt::merchant::MerchantComponent::ZoneProcessRecipeReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2) {
		return;
	}

	// Until T419 this whole body was the LOG_DEBUG below: the server's recipe
	// list arrived and was thrown away, so the tradeskill window's entire
	// left side had nothing to draw.
	eqt::state::TradeskillRecipeReceivedData data;
	if (!eqt::merchant::DecodeRecipeReply(
	        static_cast<const uint8_t*>(p.Data()) + 2, p.Length() - 2, data)) {
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Recipe: {} (ID: {}, Trivial: {})", data.name, data.recipeId, data.trivial);

	if (ctx_.bridge) {
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TradeskillRecipeReceived, std::move(data)));
	}
}

// The list boundary. eqemu's Client::SendTradeskillSearchResults() queues one
// OP_RecipeReply per matching row and marks neither the start nor the end of
// the run, so the only thing that can say "a new list starts here" is the
// request that provoked it -- which is this client.
void eqt::merchant::MerchantComponent::PublishRecipeListCleared(
	uint32_t object_type, const std::string& query,
	uint32_t mintrivial, uint32_t maxtrivial, bool is_favorites)
{
	if (!ctx_.bridge) {
		return;
	}
	eqt::state::TradeskillRecipeListClearedData data;
	data.objectType = object_type;
	data.query = query;
	data.minTrivial = mintrivial;
	data.maxTrivial = maxtrivial;
	data.isFavorites = is_favorites;
	ctx_.bridge->pushEvent(eqt::state::GameEvent(
		eqt::state::GameEventType::TradeskillRecipeListCleared, std::move(data)));
}

void eqt::merchant::MerchantComponent::ZoneProcessRecipeAutoCombine(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::RecipeAutoCombine_Struct)) {
		return;
	}

	const EQT::RecipeAutoCombine_Struct* combine = reinterpret_cast<const EQT::RecipeAutoCombine_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (combine->reply_code == 0) {
		LOG_INFO(MOD_MAIN, "Auto-combine successful for recipe {}", combine->recipe_id);
		ctx_.out.System("Tradeskill combine successful!");
	} else {
		LOG_INFO(MOD_MAIN, "Auto-combine failed for recipe {}", combine->recipe_id);
		ctx_.out.System("Tradeskill combine failed.");
	}
}
