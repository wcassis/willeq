// eqt::merchant::MerchantComponent -- inventory item movement, scroll/book
// and trade-request cluster.
//
// Split out of eq.cpp (Batch F, unit F05), then split again (Batch J, unit
// J27): CharInventory/MoveItem/DeleteItem processing, the inventory
// callbacks, ScribeSpellFromScroll, the trade request/coins/accept/cancel
// senders, ReadBook and the incoming TradePartnerItem packet. Moved
// verbatim, no behavior change. See refactor/F05_merchant.md and
// refactor/J27_eq_merchant_split.md.
//
// K05 (Batch K, refactor/K_component_extraction.md) moved it again, from
// src/client/eq_merchant.cpp, and rebased the bodies from EverQuest onto
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
#include "client/commands/commands_component.h"

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

void eqt::merchant::MerchantComponent::ZoneProcessCharInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "Received character inventory packet, size={}", p.Length());

	if (ctx_.inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Processing with inventory manager");
		ctx_.inventory_manager->processCharInventory(p);
	} else {
		LOG_ERROR(MOD_INVENTORY, "No inventory manager!");
	}

	// Update stats after inventory changes
	ctx_.Bridge().UpdateInventoryStats();
}

// P28: the rest of this file (ZoneProcessMoveItem through the tradeskill
// recipe handlers just above the #endif that used to close here) is plain
// "Player mode" bridge-driven inventory/trade/loot/vendor/bank/trainer logic
// with no Irrlicht/renderer usage anywhere in it — un-guarded to match the
// unconditional eq.h declarations (55b101e had folded it under
// EQT_HAS_GRAPHICS alongside the genuinely renderer-only code elsewhere).
void eqt::merchant::MerchantComponent::ZoneProcessMoveItem(const EQ::Net::Packet &p)
{
	// Server response to our MoveItem request
	// Format: MoveItem_Struct { from_slot, to_slot, number_in_stack }
	LOG_DEBUG(MOD_INVENTORY, "Received MoveItem response");

	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->processMoveItemResponse(p);
	}

	// Update stats after item movement (may affect equipped items)
	ctx_.Bridge().UpdateInventoryStats();

	// Publish InventorySlotChanged for both slots involved in the move (D18a: bridge-only)
	// T221: the source-side occupancy write precedes the bridge guard.
	if (p.Length() >= 14) {
		int16_t fromSlot = static_cast<int16_t>(p.GetUInt32(2));
		int16_t toSlot = static_cast<int16_t>(p.GetUInt32(6));

		auto publishSlot = [this](int16_t slot) {
			eqt::state::InventorySlotChangedData data;
			data.slotId = slot;
			const auto* item = ctx_.inventory_manager ? ctx_.inventory_manager->getItem(slot) : nullptr;
			data.hasItem = (item != nullptr && item->itemId != 0);
			SyncInventorySlotToGameState(slot, data.hasItem);
			if (!ctx_.bridge) {
				return;
			}
			data.itemId = data.hasItem ? item->itemId : 0;
			data.itemName = data.hasItem ? item->name : "";
			data.iconId = data.hasItem ? item->icon : 0;
			data.quantity = data.hasItem ? item->quantity : 0;
			data.bagSlots = data.hasItem ? item->bagSlots : 0;  // B442b
			data.scrollSpellId = data.hasItem ? item->getScrollSpellId() : 0;  // B1076
			ctx_.bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
		};

		publishSlot(fromSlot);
		publishSlot(toSlot);
	}
}

void eqt::merchant::MerchantComponent::ZoneProcessDeleteItem(const EQ::Net::Packet &p)
{
	// Server-side deletion of one unit (B1140: never an echo of our own destroy)
	LOG_DEBUG(MOD_INVENTORY, "Received DeleteItem");

	if (ctx_.inventory_manager) {
		ctx_.inventory_manager->processDeleteItemResponse(p);
	}

	// Publish InventorySlotChanged for the deleted slot (D18a: bridge-only)
	// T221: the source-side occupancy write precedes the bridge guard.
	// B1140: a stack that loses one unit is still there -- publish the slot's
	// real state after processDeleteItemResponse(), not "empty".
	if (p.Length() >= 6) {
		int16_t slot = static_cast<int16_t>(p.GetUInt32(2));
		const auto* item = ctx_.inventory_manager ? ctx_.inventory_manager->getItem(slot) : nullptr;
		const bool hasItem = item != nullptr && item->itemId != 0;
		SyncInventorySlotToGameState(slot, hasItem);
		if (!ctx_.bridge) {
			return;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		data.hasItem = hasItem;
		data.itemId = hasItem ? item->itemId : 0;
		data.itemName = hasItem ? item->name : "";
		data.iconId = hasItem ? item->icon : 0;
		data.quantity = hasItem ? item->quantity : 0;
		data.bagSlots = hasItem ? item->bagSlots : 0;
		data.scrollSpellId = hasItem ? item->getScrollSpellId() : 0;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	}
}

// T221: source-side twin of GameStateSyncBridge's InventorySlotChanged
// occupancy projection (T42) -- same slot classification, same header
// (inventory_constants.h). Not inside InventoryManager (D18a: it has no
// GameState access). Called at every InventorySlotChanged publish site with
// the event's (slot, hasItem), before each bridge guard.
void eqt::merchant::MerchantComponent::SyncInventorySlotToGameState(int16_t slot, bool hasItem)
{
	if (eqt::inventory::isEquipmentSlot(slot)) {
		ctx_.game_state.inventory().setEquipmentOccupied(slot, hasItem);
	} else if (eqt::inventory::isGeneralSlot(slot)) {
		ctx_.game_state.inventory().setGeneralOccupied(slot - eqt::inventory::GENERAL_BEGIN, hasItem);
	} else if (eqt::inventory::isBankSlot(slot)) {
		ctx_.game_state.inventory().setBankOccupied(slot - eqt::inventory::BANK_BEGIN, hasItem);
	}
	// B899: a change to an open bag's own general slot closes its window
	// (CommandsComponent::OnInventorySlotChanged). This is the one choke point
	// every InventorySlotChanged publisher already calls.
	if (ctx_.commands_ && eqt::inventory::isGeneralSlot(slot)) {
		ctx_.Commands().OnInventorySlotChanged(slot);
	}
}

void eqt::merchant::MerchantComponent::SetupInventoryCallbacks()
{
	/* D20b1: InventoryManager pushes MoveItemIntent/DeleteItemIntent/EquipmentChangedIntent via bridge */
}

void eqt::merchant::MerchantComponent::SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity)
{
	LOG_DEBUG(MOD_INVENTORY, "SendMoveItem called: fromSlot={} toSlot={} qty={}", fromSlot, toSlot, quantity);

	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_INVENTORY, "No zone connection, not sending MoveItem");
		return;
	}

	// MoveItem_Struct is 12 bytes: from_slot(4), to_slot(4), number_in_stack(4)
	// Packet format: opcode(2) + data(12) = 14 bytes
	//
	// When quantity is 0, the entire item/stack is moved.
	// When quantity > 0, only that many items are split from the stack.
	EQ::Net::DynamicPacket packet;
	packet.Resize(14);

	packet.PutUInt16(0, HC_OP_MoveItem);                   // opcode
	packet.PutUInt32(2, static_cast<uint32_t>(fromSlot));  // from_slot
	packet.PutUInt32(6, static_cast<uint32_t>(toSlot));    // to_slot
	packet.PutUInt32(10, quantity);                        // number_in_stack (0 = move entire item)

	LOG_DEBUG(MOD_INVENTORY, "Sending MoveItem packet: {} -> {} (qty: {})", fromSlot, toSlot, quantity);

	eqt::strings::DumpPacket("C->S", HC_OP_MoveItem, packet);
	ctx_.net.SafeQueueZone(packet);
}

void eqt::merchant::MerchantComponent::SendDeleteItem(int16_t slot)
{
	if (!ctx_.zone_connection) {
		return;
	}

	// DeleteItem_Struct is 12 bytes: from_slot(4), to_slot(4), number_in_stack(4)
	// to_slot is 0xFFFFFFFF for deletion
	// Packet format: opcode(2) + data(12) = 14 bytes
	EQ::Net::DynamicPacket packet;
	packet.Resize(14);

	packet.PutUInt16(0, HC_OP_DeleteItem);                 // opcode
	packet.PutUInt32(2, static_cast<uint32_t>(slot));      // from_slot
	packet.PutUInt32(6, 0xFFFFFFFF);                       // to_slot = -1 (delete)
	packet.PutUInt32(10, 0);                               // number_in_stack

	LOG_DEBUG(MOD_INVENTORY, "Sending DeleteItem: slot {}", slot);

	eqt::strings::DumpPacket("C->S", HC_OP_DeleteItem, packet);
	ctx_.net.SafeQueueZone(packet);
}

// T276 -- the ONE place a scribe refusal becomes a chat line, shared by the
// pre-flight in EverQuest::RequestScribeItem() (which must refuse BEFORE the
// scroll is moved onto the cursor) and by the last-line guard in
// ScribeSpellFromScroll() below, so the two can never drift (the T244
// castRefusalMessage() idiom). Empty string means "no refusal".
//
// The two class/level strings are the official ones eqemu itself sends from
// Client::OPMemorizeSpell (zone/client_process.cpp): SPELL_LEVEL_TO_LOW
// (eqstr_us.txt 12048, "You will have to achieve level %1 before you can
// scribe the %2.") and CANNOT_USE_ITEM (181, "Your race, class, or deity
// cannot use this item."). The already-scribed refusal has no official
// string -- the stock client never offers the gesture for a spell you know,
// and eqemu would happily overwrite the book slot AND eat the scroll.
std::string eqt::merchant::MerchantComponent::ScribeRefusalMessage(uint32_t spellId)
{
	if (!ctx_.spell_manager) {
		return "Cannot scribe - spell system not available.";
	}

	if (ctx_.spell_manager->hasSpellScribed(spellId)) {
		return "You already have this spell scribed in your spellbook.";
	}

	const EQ::SpellData* spellData = ctx_.spell_manager->getSpell(spellId);
	if (!spellData) {
		return fmt::format("Spell {} does not exist or is invalid.", spellId);
	}

	// GetClass()/GetLevel() read m_game_state.player() -- the authoritative
	// copy. T276: this used the raw m_game_state.player().classId()/m_game_state.player().level() members, a second copy
	// written only by the PlayerProfile handler.
	const uint8_t reqLevel = spellData->getClassLevel(static_cast<EQ::PlayerClass>(ctx_.self.GetClass()));
	if (reqLevel == 255) {
		return ctx_.strings.GetStringMessage(181);
	}
	if (ctx_.self.GetLevel() < reqLevel) {
		return ctx_.strings.GetFormattedStringMessage(12048, {std::to_string(reqLevel), spellData->name});
	}

	return "";
}

// T276 -- the scribe request itself (T264 F5). Reached ONLY through
// ScribeSpellIntent, whose producer is /scribe (EverQuest::RequestScribeItem
// in eq_commands.cpp).
//
// PRECONDITION, and it is the whole reason this function used to be dead:
// eqemu's Client::OPMemorizeSpell reads the scroll off the CURSOR
// (m_inv[EQ::invslot::slotCursor]) and requires that item's Scroll.Effect to
// equal spell_id. The PRODUCER puts it there --
// InventoryManager::setSpellForScribe() -> pickupItem() pushes a
// MoveItemIntent, which the same ProcessBridgeIntents() drain turns into
// OP_MoveItem BEFORE it reaches this arm, because intents are drained FIFO
// and /scribe pushes the move first. Doing the pickup HERE instead would
// queue the MoveItemIntent for the NEXT drain and the server would see
// OP_MemorizeSpell against an empty cursor. The comment this function
// carried before T276 asserted the precondition was already met -- and
// setSpellForScribe() had zero callers anywhere, so it never was.
void eqt::merchant::MerchantComponent::ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_SPELL, "No zone connection, cannot scribe spell");
		return;
	}

	if (!ctx_.spell_manager) {
		LOG_WARN(MOD_SPELL, "No spell manager, cannot scribe spell");
		return;
	}

	if (std::string refusal = ScribeRefusalMessage(spellId); !refusal.empty()) {
		ctx_.out.System(refusal);
		LOG_DEBUG(MOD_SPELL, "Refusing to scribe spell {}: {}", spellId, refusal);
		return;
	}

	if (bookSlot >= EQ::MAX_SPELLBOOK_SLOTS) {
		ctx_.out.System("Your spellbook is full.");
		return;
	}

	// Last-line guard mirroring eqemu's own: the cursor must hold THIS
	// scroll. Nothing but a producer bug can trip it, which is exactly why
	// it is here -- the pre-T276 code sent the packet regardless.
	if (ctx_.inventory_manager) {
		const eqt::inventory::ItemInstance* onCursor = ctx_.inventory_manager->getCursorItem();
		if (!onCursor || onCursor->getScrollSpellId() != spellId) {
			ctx_.out.System("The scroll is not on your cursor - cannot scribe.");
			LOG_WARN(MOD_SPELL, "Scribe aborted: cursor does not hold spell {}'s scroll", spellId);
			return;
		}
	}

	const EQ::SpellData* spellData = ctx_.spell_manager->getSpell(spellId);

	LOG_INFO(MOD_SPELL, "Scribing spell {} to spellbook slot {} (scroll from slot {})",
		spellId, bookSlot, sourceSlot);

	// eqemu MemorizeSpell_Struct (common/eq_packet_structs.h:415, and the
	// identical titanium_structs.h:407):
	//   uint32 slot      -- spellbook slot (0-399) when scribing
	//   uint32 spell_id
	//   uint32 scribing  -- Client::memSpellScribing = 0 (zone/client.h:102)
	//   uint32 reduction
	// Client::OPMemorizeSpell rejects any size != 16.
	{
		EQ::Net::DynamicPacket memPacket;
		memPacket.Resize(18);  // opcode(2) + struct(16)

		constexpr uint32_t SCRIBING_FROM_SCROLL = 0;

		memPacket.PutUInt16(0, HC_OP_MemorizeSpell);
		memPacket.PutUInt32(2, bookSlot);    // spellbook slot (0-399)
		memPacket.PutUInt32(6, spellId);     // spell ID
		memPacket.PutUInt32(10, SCRIBING_FROM_SCROLL);  // scribing action = 0
		memPacket.PutUInt32(14, 0);          // reduction

		LOG_DEBUG(MOD_SPELL, "Sending MemorizeSpell: spell={} book_slot={} scribing=0 (scribe from scroll)",
			spellId, bookSlot);
		eqt::strings::DumpPacket("C->S", HC_OP_MemorizeSpell, memPacket);
		ctx_.net.SafeQueueZone(memPacket);
	}

	// Store pending scribe state for confirmation handling (eq.cpp's
	// HC_OP_MemorizeSpell case matches on it).
	ctx_.Zone().PendingScribeSpellId() = spellId;
	ctx_.Zone().PendingScribeBookSlot() = bookSlot;
	ctx_.Zone().PendingScribeSourceSlot() = sourceSlot;

	ctx_.out.System(fmt::format("Scribing {}...",
		spellData ? spellData->name : "spell"));
}

// Trade send functions

void eqt::merchant::MerchantComponent::SendTradeRequest(const EQT::TradeRequest_Struct& req)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequest");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequest_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequest);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &req, sizeof(EQT::TradeRequest_Struct));

	eqt::strings::DumpPacket("C->S", HC_OP_TradeRequest, packet);
	ctx_.net.SafeQueueZone(packet);
}

void eqt::merchant::MerchantComponent::SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequestAck");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequestAck_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequestAck);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &ack, sizeof(EQT::TradeRequestAck_Struct));

	eqt::strings::DumpPacket("C->S", HC_OP_TradeRequestAck, packet);
	ctx_.net.SafeQueueZone(packet);
}

// T168: SendTradeCoins was DELETED here. It sent on an opcode eqemu never
// accepts: there is no ConnectedOpcodes[OP_TradeCoins] entry anywhere in the
// server. OP_TradeCoins is SERVER->CLIENT -- eqemu builds it inside
// Handle_OP_MoveCoin (zone/client_process.cpp:1598) to tell the OTHER trader
// that coins were added. The client->server route for putting coins into a
// trade is OP_MoveCoin, i.e. SendMoveCoin() below, which is wired and used.
//
// The struct and the inbound parse (eq.cpp's HC_OP_TradeCoins arm ->
// TradeManager::handleTradeCoins) are correct and are kept.

void eqt::merchant::MerchantComponent::SendMoveCoin(const EQT::MoveCoin_Struct& move)
{
	LOG_DEBUG(MOD_MAIN, "SendMoveCoin called: from_slot={} to_slot={} cointype1={} cointype2={} amount={}",
		move.from_slot, move.to_slot, move.cointype1, move.cointype2, move.amount);

	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending MoveCoin");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::MoveCoin_Struct));
	packet.PutUInt16(0, HC_OP_MoveCoin);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &move, sizeof(EQT::MoveCoin_Struct));

	LOG_DEBUG(MOD_MAIN, "Queueing MoveCoin packet, size={}", packet.Length());
	eqt::strings::DumpPacket("C->S", HC_OP_MoveCoin, packet);
	ctx_.net.SafeQueueZone(packet);
}

// T269: /bank deposit|withdraw -> the kept BankCurrencyMoveIntent (T264
// F3), the same shape a bank-window coin gesture would push. Same recipe as
// RequestVendorBuy (eq_commands.cpp): the intent sits in the bridge's queue
// and is drained by ProcessBridgeIntents() on the next GameTick(), whose arm
// (eq_bridge.cpp) builds OP_MoveCoin and updates the local projection. The
// coin-pool check is done by CommandProcessor::cmdBank against GameState
// BEFORE this is reached and again by the arm; this wrapper owns only the
// window-open guard (eqemu's OPMoveCoin refuses slot 2 silently -- with a
// POSSIBLE_HACK player-event log -- when no banker is within
// USE_NPC_RANGE2, and our proxy for "a banker is in range" is the window
// BankerInteractIntent's distance check opened).
void eqt::merchant::MerchantComponent::RequestBankCoinMove(uint8_t coinType, int32_t amount, bool fromBank)
{
	if (!ctx_.self.IsBankWindowOpen()) {
		ctx_.out.System("You must have a bank window open to move coins.");
		return;
	}

	if (ctx_.bridge) {
		ctx_.bridge->pushIntent(eqt::events::BankCurrencyMoveIntent{coinType, amount, fromBank});
	} else {
		ctx_.out.System("Cannot move coins - no bridge attached.");
	}
}

void eqt::merchant::MerchantComponent::RequestBankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount)
{
	if (!ctx_.self.IsBankWindowOpen()) {
		ctx_.out.System("You must have a bank window open to convert coins.");
		return;
	}

	if (ctx_.bridge) {
		ctx_.bridge->pushIntent(eqt::events::BankCurrencyConvertIntent{fromCoinType, toCoinType, amount});
	} else {
		ctx_.out.System("Cannot convert coins - no bridge attached.");
	}
}

// B441 (#26): `/bank change`, the slash half of the bank window's Change
// button -- the same intent, the same arm (BridgeComponent::onBankMakeChangeIntent).
void eqt::merchant::MerchantComponent::RequestBankMakeChange()
{
	if (!ctx_.self.IsBankWindowOpen()) {
		ctx_.out.System("You must have a bank window open to change coins.");
		return;
	}
	if (ctx_.bridge) {
		ctx_.bridge->pushIntent(eqt::events::BankMakeChangeIntent{});
	} else {
		ctx_.out.System("Cannot change coins - no bridge attached.");
	}
}

void eqt::merchant::MerchantComponent::SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeAcceptClick");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeAcceptClick_Struct));
	packet.PutUInt16(0, HC_OP_TradeAcceptClick);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &accept, sizeof(EQT::TradeAcceptClick_Struct));

	eqt::strings::DumpPacket("C->S", HC_OP_TradeAcceptClick, packet);
	ctx_.net.SafeQueueZone(packet);
}

void eqt::merchant::MerchantComponent::SendCancelTrade(const EQT::CancelTrade_Struct& cancel)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending CancelTrade");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::CancelTrade_Struct));
	packet.PutUInt16(0, HC_OP_CancelTrade);
	memcpy(static_cast<uint8_t*>(packet.Data()) + 2, &cancel, sizeof(EQT::CancelTrade_Struct));

	eqt::strings::DumpPacket("C->S", HC_OP_CancelTrade, packet);
	ctx_.net.SafeQueueZone(packet);
}

// Book/Note reading methods

void eqt::merchant::MerchantComponent::SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename)
{
	if (!ctx_.zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending ReadBook request");
		return;
	}

	// Packet format from server titanium_structs.h BookRequest_Struct:
	// Offset 0-1: opcode (2 bytes)
	// Offset 2: window (1 byte) - 0xFF = new window
	// Offset 3: type (1 byte) - 0=scroll, 1=book, 2=item info
	// Offset 4+: txtfile (null-terminated string)
	size_t packetSize = 2 + 2 + filename.length() + 1;  // opcode + window/type + filename + null

	EQ::Net::DynamicPacket packet;
	packet.Resize(packetSize);

	packet.PutUInt16(0, HC_OP_ReadBook);    // opcode
	packet.PutUInt8(2, window);              // window
	packet.PutUInt8(3, type);                // type
	// txtfile at offset 4
	memcpy((char*)packet.Data() + 4, filename.c_str(), filename.length() + 1);

	LOG_DEBUG(MOD_MAIN, "Sending ReadBook request: window={} type={} filename='{}'", window, type, filename);

	eqt::strings::DumpPacket("C->S", HC_OP_ReadBook, packet);
	ctx_.net.SafeQueueZone(packet);
}

void eqt::merchant::MerchantComponent::ZoneProcessReadBook(const EQ::Net::Packet& p)
{
	// Response packet format:
	// Offset 0-1: opcode (2 bytes)
	// Offset 2: window (1 byte)
	// Offset 3: type (1 byte)
	// Offset 4+: booktext (null-terminated string with '^' as newline)

	if (p.Length() < 4) {
		LOG_WARN(MOD_MAIN, "ReadBook response too short: {} bytes", p.Length());
		return;
	}

	uint8_t window = p.GetUInt8(2);
	uint8_t type = p.GetUInt8(3);

	// Extract the book text (starts at offset 4)
	std::string bookText;
	if (p.Length() > 4) {
		bookText = p.GetCString(4);
	}

	LOG_DEBUG(MOD_MAIN, "ReadBook response: window={} type={} textLen={}", window, type, bookText.length());
	LOG_TRACE(MOD_MAIN, "Book text: '{}'", bookText);

	// Publish NoteWindowOpened event to bridge
	if (ctx_.bridge) {
		eqt::state::NoteWindowOpenedData data;
		data.text = bookText;
		data.type = type;
		ctx_.bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::NoteWindowOpened, std::move(data)));
	}

	// T273 -- SURFACE the text. NoteWindowOpened's only consumer is
	// IrrlichtRenderer::showNoteWindow(), which LOG_INFOs and returns:
	// there is no book/note panel in the static UI, so without this the
	// text a player asked for never reaches them. It goes to the CHAT
	// window (SystemMessage), which exists in both graphics and headless
	// mode; when a book panel is built, showNoteWindow() is where it hooks
	// in and this can become that panel's fallback.
	//
	// Line breaks: eqemu's own struct comment says the text uses '`' as a
	// newline; P99-era content also uses '^'. Both are split on -- a book
	// rendered as one 500-character line is not readable in a chat window.
	SurfaceBookText(bookText);
}

void eqt::merchant::MerchantComponent::SurfaceBookText(const std::string& text)
{
	if (text.empty()) {
		ctx_.out.System("The page is blank.");
		return;
	}

	std::string line;
	bool any = false;
	auto flush = [&]() {
		// Trim trailing whitespace so a '`'-terminated paragraph does not
		// emit an empty chat line.
		while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
			line.pop_back();
		}
		if (!line.empty()) {
			ctx_.out.System(line);
			any = true;
		}
		line.clear();
	};
	for (char c : text) {
		if (c == '`' || c == '^' || c == '\n' || c == '\r') {
			flush();
		} else {
			line.push_back(c);
		}
	}
	flush();
	if (!any) {
		ctx_.out.System("The page is blank.");
	}
}

void eqt::merchant::MerchantComponent::RequestReadBook(const std::string& filename, uint8_t type)
{
	// Send read book request with new window flag (0xFF)
	SendReadBookRequest(0xFF, type, filename);
}

bool eqt::merchant::MerchantComponent::ZoneProcessTradePartnerItem(const EQ::Net::Packet& p)
{
	// Check if this is a partner trade item (ItemPacketTradeView = 101)
	// Partner items are sent with packet type 101 and slot 0-3 (relative)
	// Returns true if handled as trade item, false if should be processed normally

	if (p.Length() < 10) {
		return false;
	}

	// Get packet type (4 bytes after 2-byte opcode)
	uint32_t packetType = p.GetUInt32(2);

	// Only handle ItemPacketTradeView (101) packets
	if (packetType != EQT::ITEM_PACKET_TRADE_VIEW) {
		return false;
	}

	// Skip opcode (2 bytes) and packet type (4 bytes)
	const char* itemData = reinterpret_cast<const char*>(p.Data()) + 2 + 4;
	size_t itemLen = p.Length() - 2 - 4;

	// Remove trailing nulls
	while (itemLen > 0 && itemData[itemLen - 1] == '\0') {
		itemLen--;
	}

	if (itemLen == 0) {
		return false;
	}

	// Quick parse to get slot ID - it's the first pipe-delimited field
	// For trade view packets, slot is 0-3 (relative)
	int16_t slotId = eqt::inventory::SLOT_INVALID;
	std::string slotStr;
	for (size_t i = 0; i < itemLen && itemData[i] != '|'; i++) {
		slotStr += itemData[i];
	}
	if (!slotStr.empty()) {
		try {
			slotId = static_cast<int16_t>(std::stoi(slotStr));
		} catch (...) {
			return false;
		}
	}

	// T259: a partner's bag arrives as its own TradeView packet AND one
	// packet per content item at (3031 + idx) - 3000 = 31.. (eqemu
	// Trade::SendItemData, zone/trading.cpp). The bag packet itself
	// already carries the sub-items; the per-content packets are consumed
	// here so they never reach the inventory model as slot 31-110.
	if (slotId >= eqt::inventory::TRADE_BAGS_BEGIN - eqt::inventory::TRADE_BEGIN &&
	    slotId <= eqt::inventory::TRADE_BAGS_END - eqt::inventory::TRADE_BEGIN) {
		LOG_DEBUG(MOD_MAIN, "Trade partner bag content at relative slot {} (parent trade slot {}) -- consumed",
		          slotId, (slotId - (eqt::inventory::TRADE_BAGS_BEGIN - eqt::inventory::TRADE_BEGIN)) /
		                      eqt::inventory::BAG_SLOT_COUNT);
		return true;
	}

	// Validate slot is 0-7 (partner trade slots)
	if (slotId < 0 || slotId > 7) {
		LOG_WARN(MOD_MAIN, "Invalid partner trade slot: {}", slotId);
		return true;  // Still consumed - don't pass to inventory
	}

	// Parse the item. T259: no sub-item map -- the partner's bag contents
	// belong to the PARTNER's inventory; parsing them against our relative
	// slot 0-7 only produced one "has no client slot -- dropped" WARN per
	// content item (T255's parser guard), and nothing displayed them anyway
	// (TradeState carries partner items per slot only). Surfacing partner
	// bag contents in the trade window is a separate, unplanned feature.
	auto item = eqt::inventory::TitaniumItemParser::parseItem(itemData, itemLen, slotId, nullptr);

	if (!item) {
		LOG_WARN(MOD_MAIN, "Failed to parse trade partner item for slot {}", slotId);
		return true;  // Still consumed - don't pass to inventory
	}

	LOG_INFO(MOD_MAIN, "Received trade partner item: {} in slot {}", item->name, slotId);

	// Show chat message about the offered item
	if (ctx_.trade_manager) {
		std::string partnerName = ctx_.trade_manager->getPartnerName();
		std::string itemName = item->name;
		ctx_.out.System(fmt::format("{} has offered you a {}.",
			EQT::toDisplayName(partnerName), itemName));
	}

	// Pass to trade manager
	if (ctx_.trade_manager) {
		ctx_.trade_manager->handlePartnerItemPacket(slotId, std::move(item));
	}

	return true;  // Handled as trade item
}

