// EverQuest inventory/trade/loot/vendor/bank/trainer/recipe cluster.
//
// Split out of eq.cpp (Batch F, unit F05): CharInventory/MoveItem/DeleteItem
// processing, ScribeSpellFromScroll, the trade request/coins/accept senders,
// ReadBook, TradePartnerItem, the loot/vendor/bank/trade-window/tradeskill/
// trainer Setup*Callbacks and their packet handlers, GM training, and the
// Phase 4 tradeskill recipe senders/handlers. Moved verbatim, no behavior
// change. See refactor/F05_merchant.md.

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

void EverQuest::ZoneProcessCharInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "Received character inventory packet, size={}", p.Length());

	if (m_inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Processing with inventory manager");
		m_inventory_manager->processCharInventory(p);
	} else {
		LOG_ERROR(MOD_INVENTORY, "No inventory manager!");
	}

	// Update stats after inventory changes
	UpdateInventoryStats();
}

// P28: the rest of this file (ZoneProcessMoveItem through the tradeskill
// recipe handlers just above the #endif that used to close here) is plain
// "Player mode" bridge-driven inventory/trade/loot/vendor/bank/trainer logic
// with no Irrlicht/renderer usage anywhere in it — un-guarded to match the
// unconditional eq.h declarations (55b101e had folded it under
// EQT_HAS_GRAPHICS alongside the genuinely renderer-only code elsewhere).
void EverQuest::ZoneProcessMoveItem(const EQ::Net::Packet &p)
{
	// Server response to our MoveItem request
	// Format: MoveItem_Struct { from_slot, to_slot, number_in_stack }
	LOG_DEBUG(MOD_INVENTORY, "Received MoveItem response");

	if (m_inventory_manager) {
		m_inventory_manager->processMoveItemResponse(p);
	}

	// Update stats after item movement (may affect equipped items)
	UpdateInventoryStats();

	// Publish InventorySlotChanged for both slots involved in the move (D18a: bridge-only)
	// T221: the source-side occupancy write precedes the bridge guard.
	if (p.Length() >= 14) {
		int16_t fromSlot = static_cast<int16_t>(p.GetUInt32(2));
		int16_t toSlot = static_cast<int16_t>(p.GetUInt32(6));

		auto publishSlot = [this](int16_t slot) {
			eqt::state::InventorySlotChangedData data;
			data.slotId = slot;
			const auto* item = m_inventory_manager ? m_inventory_manager->getItem(slot) : nullptr;
			data.hasItem = (item != nullptr && item->itemId != 0);
			SyncInventorySlotToGameState(slot, data.hasItem);
			if (!m_bridge) {
				return;
			}
			data.itemId = data.hasItem ? item->itemId : 0;
			data.itemName = data.hasItem ? item->name : "";
			data.iconId = data.hasItem ? item->icon : 0;
			data.quantity = data.hasItem ? item->quantity : 0;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
		};

		publishSlot(fromSlot);
		publishSlot(toSlot);
	}
}

void EverQuest::ZoneProcessDeleteItem(const EQ::Net::Packet &p)
{
	// Server response to our DeleteItem request
	LOG_DEBUG(MOD_INVENTORY, "Received DeleteItem response");

	if (m_inventory_manager) {
		m_inventory_manager->processDeleteItemResponse(p);
	}

	// Publish InventorySlotChanged for the deleted slot (D18a: bridge-only)
	// T221: the source-side occupancy write precedes the bridge guard.
	if (p.Length() >= 6) {
		int16_t slot = static_cast<int16_t>(p.GetUInt32(2));
		SyncInventorySlotToGameState(slot, false);
		if (!m_bridge) {
			return;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		data.hasItem = false;
		data.itemId = 0;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	}
}

// T221: source-side twin of GameStateSyncBridge's InventorySlotChanged
// occupancy projection (T42) -- same slot classification, same header
// (inventory_constants.h). Not inside InventoryManager (D18a: it has no
// GameState access). Called at every InventorySlotChanged publish site with
// the event's (slot, hasItem), before each bridge guard.
void EverQuest::SyncInventorySlotToGameState(int16_t slot, bool hasItem)
{
	if (eqt::inventory::isEquipmentSlot(slot)) {
		m_game_state.inventory().setEquipmentOccupied(slot, hasItem);
	} else if (eqt::inventory::isGeneralSlot(slot)) {
		m_game_state.inventory().setGeneralOccupied(slot - eqt::inventory::GENERAL_BEGIN, hasItem);
	} else if (eqt::inventory::isBankSlot(slot)) {
		m_game_state.inventory().setBankOccupied(slot - eqt::inventory::BANK_BEGIN, hasItem);
	}
}

void EverQuest::SetupInventoryCallbacks()
{
	/* D20b1: InventoryManager pushes MoveItemIntent/DeleteItemIntent/EquipmentChangedIntent via bridge */
}

void EverQuest::SendMoveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity)
{
	LOG_DEBUG(MOD_INVENTORY, "SendMoveItem called: fromSlot={} toSlot={} qty={}", fromSlot, toSlot, quantity);

	if (!m_zone_connection) {
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

	DumpPacket("C->S", HC_OP_MoveItem, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendDeleteItem(int16_t slot)
{
	if (!m_zone_connection) {
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

	DumpPacket("C->S", HC_OP_DeleteItem, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::ScribeSpellFromScroll(uint32_t spellId, uint16_t bookSlot, int16_t sourceSlot)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_SPELL, "No zone connection, cannot scribe spell");
		return;
	}

	if (!m_spell_manager) {
		LOG_WARN(MOD_SPELL, "No spell manager, cannot scribe spell");
		return;
	}

	// Check if spell is already scribed
	if (m_spell_manager->hasSpellScribed(spellId)) {
		AddChatSystemMessage("You already have this spell scribed in your spellbook.");
		LOG_DEBUG(MOD_SPELL, "Spell {} already scribed, not sending packets", spellId);
		return;
	}

	// Check level/class requirements
	const EQ::SpellData* spellData = m_spell_manager->getSpell(spellId);
	if (spellData) {
		uint8_t reqLevel = spellData->getClassLevel(static_cast<EQ::PlayerClass>(m_class));
		if (reqLevel == 255) {
			AddChatSystemMessage("Your class cannot use this spell.");
			LOG_DEBUG(MOD_SPELL, "Class {} cannot use spell {}", m_class, spellId);
			return;
		}
		if (m_level < reqLevel) {
			AddChatSystemMessage(fmt::format("You must be level {} to scribe this spell.", reqLevel));
			LOG_DEBUG(MOD_SPELL, "Level {} too low to scribe spell {} (requires {})",
				m_level, spellId, reqLevel);
			return;
		}
	}

	LOG_INFO(MOD_SPELL, "Scribing spell {} to spellbook slot {} (scroll already on cursor)",
		spellId, bookSlot);

	// Note: The scroll was already moved to cursor slot when Ctrl+clicked
	// (InventoryManager::setSpellForScribe sends MoveItem to cursor)
	// We only need to send OP_MemorizeSpell here

	// Send OP_MemorizeSpell with scribing action
	// MemorizeSpell_Struct:
	// - slot (4 bytes): spellbook slot (0-399) when scribing
	// - spell_id (4 bytes): spell ID
	// - scribing (4 bytes): action type (0=scribe from scroll, 1=memorize, 2=forget, 3=spellbar)
	// - reduction (4 bytes): reuse timer reduction
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
		DumpPacket("C->S", HC_OP_MemorizeSpell, memPacket);
		m_zone_connection->QueuePacket(memPacket);
	}

	// Store pending scribe state for confirmation handling
	m_pending_scribe_spell_id = spellId;
	m_pending_scribe_book_slot = bookSlot;
	m_pending_scribe_source_slot = sourceSlot;

	AddChatSystemMessage(fmt::format("Scribing {}...",
		spellData ? spellData->name : "spell"));
}

// Trade send functions

void EverQuest::SendTradeRequest(const EQT::TradeRequest_Struct& req)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequest");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequest_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequest);
	memcpy(packet.Data() + 2, &req, sizeof(EQT::TradeRequest_Struct));

	DumpPacket("C->S", HC_OP_TradeRequest, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendTradeRequestAck(const EQT::TradeRequestAck_Struct& ack)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeRequestAck");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeRequestAck_Struct));
	packet.PutUInt16(0, HC_OP_TradeRequestAck);
	memcpy(packet.Data() + 2, &ack, sizeof(EQT::TradeRequestAck_Struct));

	DumpPacket("C->S", HC_OP_TradeRequestAck, packet);
	m_zone_connection->QueuePacket(packet);
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

void EverQuest::SendMoveCoin(const EQT::MoveCoin_Struct& move)
{
	LOG_DEBUG(MOD_MAIN, "SendMoveCoin called: from_slot={} to_slot={} cointype1={} cointype2={} amount={}",
		move.from_slot, move.to_slot, move.cointype1, move.cointype2, move.amount);

	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending MoveCoin");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::MoveCoin_Struct));
	packet.PutUInt16(0, HC_OP_MoveCoin);
	memcpy(packet.Data() + 2, &move, sizeof(EQT::MoveCoin_Struct));

	LOG_DEBUG(MOD_MAIN, "Queueing MoveCoin packet, size={}", packet.Length());
	DumpPacket("C->S", HC_OP_MoveCoin, packet);
	m_zone_connection->QueuePacket(packet);
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
void EverQuest::RequestBankCoinMove(uint8_t coinType, int32_t amount, bool fromBank)
{
	if (!IsBankWindowOpen()) {
		AddChatSystemMessage("You must have a bank window open to move coins.");
		return;
	}

	if (m_bridge) {
		m_bridge->pushIntent(eqt::events::BankCurrencyMoveIntent{coinType, amount, fromBank});
	} else {
		AddChatSystemMessage("Cannot move coins - no bridge attached.");
	}
}

void EverQuest::RequestBankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount)
{
	if (!IsBankWindowOpen()) {
		AddChatSystemMessage("You must have a bank window open to convert coins.");
		return;
	}

	if (m_bridge) {
		m_bridge->pushIntent(eqt::events::BankCurrencyConvertIntent{fromCoinType, toCoinType, amount});
	} else {
		AddChatSystemMessage("Cannot convert coins - no bridge attached.");
	}
}

void EverQuest::SendTradeAcceptClick(const EQT::TradeAcceptClick_Struct& accept)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending TradeAcceptClick");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::TradeAcceptClick_Struct));
	packet.PutUInt16(0, HC_OP_TradeAcceptClick);
	memcpy(packet.Data() + 2, &accept, sizeof(EQT::TradeAcceptClick_Struct));

	DumpPacket("C->S", HC_OP_TradeAcceptClick, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::SendCancelTrade(const EQT::CancelTrade_Struct& cancel)
{
	if (!m_zone_connection) {
		LOG_WARN(MOD_MAIN, "No zone connection, not sending CancelTrade");
		return;
	}

	EQ::Net::DynamicPacket packet;
	packet.Resize(2 + sizeof(EQT::CancelTrade_Struct));
	packet.PutUInt16(0, HC_OP_CancelTrade);
	memcpy(packet.Data() + 2, &cancel, sizeof(EQT::CancelTrade_Struct));

	DumpPacket("C->S", HC_OP_CancelTrade, packet);
	m_zone_connection->QueuePacket(packet);
}

// Book/Note reading methods

void EverQuest::SendReadBookRequest(uint8_t window, uint8_t type, const std::string& filename)
{
	if (!m_zone_connection) {
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

	DumpPacket("C->S", HC_OP_ReadBook, packet);
	m_zone_connection->QueuePacket(packet);
}

void EverQuest::ZoneProcessReadBook(const EQ::Net::Packet& p)
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
	if (m_bridge) {
		eqt::state::NoteWindowOpenedData data;
		data.text = bookText;
		data.type = type;
		m_bridge->pushEvent(eqt::state::GameEvent(
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

void EverQuest::SurfaceBookText(const std::string& text)
{
	if (text.empty()) {
		AddChatSystemMessage("The page is blank.");
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
			AddChatSystemMessage(line);
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
		AddChatSystemMessage("The page is blank.");
	}
}

void EverQuest::RequestReadBook(const std::string& filename, uint8_t type)
{
	// Send read book request with new window flag (0xFF)
	SendReadBookRequest(0xFF, type, filename);
}

bool EverQuest::ZoneProcessTradePartnerItem(const EQ::Net::Packet& p)
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
	if (m_trade_manager) {
		std::string partnerName = m_trade_manager->getPartnerName();
		std::string itemName = item->name;
		AddChatSystemMessage(fmt::format("{} has offered you a {}.",
			EQT::toDisplayName(partnerName), itemName));
	}

	// Pass to trade manager
	if (m_trade_manager) {
		m_trade_manager->handlePartnerItemPacket(slotId, std::move(item));
	}

	return true;  // Handled as trade item
}

void EverQuest::SetupTradeManagerCallbacks()
{
	if (!m_trade_manager) {
		return;
	}

	// Set up network callbacks
	m_trade_manager->setSendTradeRequest([this](const EQT::TradeRequest_Struct& req) {
		SendTradeRequest(req);
	});

	m_trade_manager->setSendTradeRequestAck([this](const EQT::TradeRequestAck_Struct& ack) {
		SendTradeRequestAck(ack);
	});

	m_trade_manager->setSendMoveCoin([this](const EQT::MoveCoin_Struct& move) {
		SendMoveCoin(move);
	});

	m_trade_manager->setSendTradeAcceptClick([this](const EQT::TradeAcceptClick_Struct& accept) {
		SendTradeAcceptClick(accept);
	});

	m_trade_manager->setSendCancelTrade([this](const EQT::CancelTrade_Struct& cancel) {
		SendCancelTrade(cancel);
	});

	// Set up UI callbacks (via bridge events)
	//
	// T103: each lambda below now ALSO writes the same data directly into
	// m_game_state.trade() (EverQuest's OWN GameState instance), same-thread,
	// synchronous -- mirroring how SyncEntityToGameState() already writes
	// m_game_state.entities() directly (eq_player_info.cpp:261) rather than
	// only through the cross-thread bridge. This is what EqActionHandler
	// reads (m_eq.GetGameState().trade()); the bridge events these lambdas
	// already pushed are, separately, what GameStateSyncBridge now also
	// consumes into Application::m_gameState.trade() (see
	// game_state_sync_bridge.cpp) for any render-side consumer. Two
	// instances, two mechanisms, one source of truth -- same shape T81
	// established for entities.
	m_trade_manager->setOnRequestReceived([this](uint32_t spawnId, const std::string& name) {
		m_game_state.trade().setPendingRequest(spawnId, name);
		if (m_bridge) {
			eqt::state::TradeRequestReceivedData tdata;
			tdata.spawnId = spawnId;
			tdata.name = name;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeRequestReceived, std::move(tdata)));
		}
	});

	m_trade_manager->setOnStateChanged([this](TradeState state) {
		// Update inventory manager NPC trade flag for item validation
		if (m_inventory_manager) {
			if (state == TradeState::Active) {
				m_inventory_manager->setNpcTrade(m_trade_manager->isNpcTrade());
			} else if (state == TradeState::None || state == TradeState::Completed) {
				m_inventory_manager->setNpcTrade(false);
			}
		}

		if (state == TradeState::Active) {
			// Publish TradeStarted
			m_game_state.trade().startTrade(
				static_cast<uint16_t>(m_trade_manager->getPartnerSpawnId()),
				m_trade_manager->getPartnerName(),
				m_trade_manager->isNpcTrade());
			if (m_bridge) {
				eqt::state::TradeStartedData tdata;
				tdata.partnerId = m_trade_manager->getPartnerSpawnId();
				tdata.partnerName = m_trade_manager->getPartnerName();
				tdata.isNpc = m_trade_manager->isNpcTrade();
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeStarted, std::move(tdata)));
			}
		} else if (state == TradeState::None) {
			// Only close on None state (cancel) - Completed is handled by m_on_completed
			m_game_state.trade().reset();
			if (m_bridge) {
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeCancelled, eqt::state::TradeCancelledData{}));
			}
		}
	});

	m_trade_manager->setOnItemUpdated([this](bool isOwn, int slot) {
		if (!isOwn) {
			// Partner item updated - publish to bridge and the projection.
			// Own items are displayed from inventory trade slots (not
			// tracked here -- see trade_state.h's class comment; this isOwn
			// branch is never actually taken in production, since nothing
			// calls TradeManager::addItemToTrade()).
			const auto* item = m_trade_manager->getPartnerItem(slot);
			uint32_t itemId = item ? item->itemId : 0;
			std::string itemName = item ? item->name : "";
			m_game_state.trade().setPartnerItem(slot, itemId, itemName);
			if (m_bridge) {
				eqt::state::TradeItemUpdatedData tdata;
				tdata.who = 1;  // partner
				tdata.slot = static_cast<uint8_t>(slot);
				tdata.itemId = itemId;
				tdata.itemName = itemName;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::TradeItemUpdated, std::move(tdata)));
			}
		}
	});

	m_trade_manager->setOnMoneyUpdated([this](bool isOwn) {
		const auto& srcMoney = isOwn ? m_trade_manager->getOwnMoney() : m_trade_manager->getPartnerMoney();
		eqt::state::TradeMoney projMoney;
		projMoney.platinum = srcMoney.platinum;
		projMoney.gold = srcMoney.gold;
		projMoney.silver = srcMoney.silver;
		projMoney.copper = srcMoney.copper;
		m_game_state.trade().setMoney(isOwn, projMoney);
		if (m_bridge) {
			eqt::state::TradeMoneyUpdatedData tdata;
			tdata.who = isOwn ? 0 : 1;
			tdata.platinum = srcMoney.platinum;
			tdata.gold = srcMoney.gold;
			tdata.silver = srcMoney.silver;
			tdata.copper = srcMoney.copper;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeMoneyUpdated, std::move(tdata)));
		}
	});

	m_trade_manager->setOnAcceptStateChanged([this](bool ownAccepted, bool partnerAccepted) {
		m_game_state.trade().setAcceptState(ownAccepted, partnerAccepted);
		if (m_bridge) {
			eqt::state::TradeAcceptStateChangedData tdata;
			tdata.ownAccepted = ownAccepted;
			tdata.partnerAccepted = partnerAccepted;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeAcceptStateChanged, std::move(tdata)));
		}
	});

	m_trade_manager->setOnCompleted([this]() {
		// Generate chat messages for what was exchanged
		std::string partnerName = EQT::toDisplayName(m_trade_manager->getPartnerName());
		const auto& partnerMoney = m_trade_manager->getPartnerMoney();
		const auto& ownMoney = m_trade_manager->getOwnMoney();

		// Report money received from partner
		if (partnerMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You receive {} platinum from {}.", partnerMoney.platinum, partnerName));
		}
		if (partnerMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You receive {} gold from {}.", partnerMoney.gold, partnerName));
		}
		if (partnerMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You receive {} silver from {}.", partnerMoney.silver, partnerName));
		}
		if (partnerMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You receive {} copper from {}.", partnerMoney.copper, partnerName));
		}

		// Report items received from partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getPartnerItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You receive {} from {}.", item->name, partnerName));
			}
		}

		// Report money given to partner
		if (ownMoney.platinum > 0) {
			AddChatSystemMessage(fmt::format("You give {} platinum to {}.", ownMoney.platinum, partnerName));
		}
		if (ownMoney.gold > 0) {
			AddChatSystemMessage(fmt::format("You give {} gold to {}.", ownMoney.gold, partnerName));
		}
		if (ownMoney.silver > 0) {
			AddChatSystemMessage(fmt::format("You give {} silver to {}.", ownMoney.silver, partnerName));
		}
		if (ownMoney.copper > 0) {
			AddChatSystemMessage(fmt::format("You give {} copper to {}.", ownMoney.copper, partnerName));
		}

		// Report items given to partner
		for (int i = 0; i < 8; i++) {
			const auto* item = m_trade_manager->getOwnItem(i);
			if (item) {
				AddChatSystemMessage(fmt::format("You give {} to {}.", item->name, partnerName));
			}
		}

		// Publish TradeCompleted event to bridge. Read from the source
		// TradeManager above (still populated -- TradeManager::
		// resetTradeState() hasn't run yet at this point in
		// handleFinishTrade()'s sequence), then clear the projection now
		// that the summary has been read out of it.
		m_game_state.trade().reset();
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeCompleted, eqt::state::TradeCompletedData{}));
		}
	});

	m_trade_manager->setOnCancelled([this](TradeEndReason reason, const std::string& requesterName) {
		// T222: exactly one chat line per end reason -- declineTrade()
		// no longer prints its own.
		if (reason == TradeEndReason::DeclinedLocal) {
			AddChatSystemMessage(fmt::format("Declined trade request from {}.",
				EQT::toDisplayName(requesterName)));
		} else {
			AddChatSystemMessage("Trade cancelled.");
		}

		// Publish TradeCancelled event to bridge
		m_game_state.trade().reset();
		if (m_bridge) {
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::TradeCancelled, eqt::state::TradeCancelledData{}));
		}
	});
}

void EverQuest::ZoneProcessLootItemToUI(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI called, packet length={}", p.Length());

	// Parse the serialized item data for the loot window
	// The packet contains pipe-delimited item data
	if (p.Length() < 10) {
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootItemToUI: Packet too short, ignoring");
		return;
	}

	// Skip the first 2 bytes (opcode is already removed, but there's a header)
	const char* data = (const char*)p.Data() + 2;
	int data_len = p.Length() - 2;

	// Skip the first 4 bytes of the data (unknown header)
	if (data_len > 4) {
		data += 4;
		data_len -= 4;
	}

	std::string item_data(data, data_len);

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Item data length={}", item_data.length());
		LOG_TRACE(MOD_INVENTORY, "Item data preview: {}", item_data.substr(0, std::min(size_t(200), item_data.length())));
	}

	// Parse the pipe-delimited fields to extract item info
	std::vector<std::string> fields;
	std::stringstream ss(item_data);
	std::string field;
	while (std::getline(ss, field, '|')) {
		fields.push_back(field);
	}

	if (s_debug_level >= 3) {
		LOG_TRACE(MOD_INVENTORY, "Parsed {} fields", fields.size());
		for (size_t i = 0; i < fields.size() && i < 20; i++) {
			LOG_TRACE(MOD_INVENTORY, "  field[{}] = '{}'", i, fields[i].substr(0, 50));
		}
	}

	// The format is: 1|0|slot|0|1|0|icon|0|0|0|0|"0|item_name|lore_name|model|item_id|...
	// field[2] = slot number
	// field[6] = icon ID
	// field[12] = item name
	if (fields.size() > 15) {
		try {
			int16_t slot_num = static_cast<int16_t>(std::stoi(fields[2]));  // Slot number in loot window
			std::string item_name = fields[12];         // Item name (was incorrectly field[11])

			LOG_DEBUG(MOD_INVENTORY, "Creating loot item: slot={} name='{}'", slot_num, item_name);

			// Parse item data and publish via bridge (D18a: bridge is sole path to renderer)
			uint32_t itemId = 0;
			uint32_t icon = 0;

			// Parse icon from static data field[11] (absolute field[22])
			try {
				if (fields.size() > 22) {
					icon = static_cast<uint32_t>(std::stoi(fields[22]));
					LOG_TRACE(MOD_INVENTORY, "  icon={} (from field[22])", icon);
				}
			} catch (...) {
				LOG_DEBUG(MOD_INVENTORY, "  icon parsing failed for field[22]='{}'",
					(fields.size() > 22 ? fields[22] : "N/A"));
			}

			// Parse item ID from static data field[4] (absolute field[15])
			try {
				itemId = static_cast<uint32_t>(std::stoi(fields[15]));
				LOG_TRACE(MOD_INVENTORY, "  itemId={} (from field[15])", itemId);
			} catch (...) {}

			// Track loot item in game state (D18a: game-side loot item tracking)
			m_loot_items[slot_num] = GameLootItem{itemId, item_name};

			// T268: feed the auto-loot queue too (this replaces the deleted
			// headless-only ZoneProcessLootItem). CheckAutoLoot() is a
			// non-blocking nudge; UpdateLootState() decides on the game tick.
			if (m_combat_manager) {
				m_combat_manager->AddLootItem(static_cast<uint32_t>(slot_num));
				m_combat_manager->CheckAutoLoot();
			}

			// Publish LootItemAdded
			if (m_bridge) {
				eqt::state::LootItemAddedData ldata;
				ldata.corpseId = m_player_looting_corpse_id;
				ldata.slot = static_cast<uint8_t>(slot_num);
				ldata.itemId = itemId;
				ldata.itemName = item_name;
				m_bridge->pushEvent(eqt::state::GameEvent(
					eqt::state::GameEventType::LootItemAdded, std::move(ldata)));
			}
		} catch (const std::exception& e) {
			LOG_WARN(MOD_INVENTORY, "Failed to parse loot item data: {}", e.what());
		}
	} else {
		LOG_DEBUG(MOD_INVENTORY, "Not enough fields ({}), need > 15", fields.size());
	}
}

void EverQuest::ZoneProcessLootedItemToInventory(const EQ::Net::Packet &p)
{
	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory called, pending_slots={}", m_pending_loot_slots.size());

	// This is called when an item is being looted from a corpse to inventory
	// The item packet format is the same as ZoneProcessLootItemToUI

	// T42: processItemPacket() is the same choke point the generic eq.cpp
	// ItemPacket fallback uses for vendor-buy/bank-window placement; publish
	// InventorySlotChanged here too (not one of T41's three named paths, but
	// the same gap -- a looted item landing in inventory never told the UI
	// either) using its returned slot rather than duplicating parse logic.
	auto publishLootedSlot = [this](int16_t slot) {
		if (slot == eqt::inventory::SLOT_INVALID) {
			return;
		}
		eqt::state::InventorySlotChangedData data;
		data.slotId = slot;
		const auto* item = m_inventory_manager ? m_inventory_manager->getItem(slot) : nullptr;
		data.hasItem = (item != nullptr && item->itemId != 0);
		SyncInventorySlotToGameState(slot, data.hasItem);  // T221
		if (!m_bridge) {
			return;
		}
		data.itemId = data.hasItem ? item->itemId : 0;
		data.itemName = data.hasItem ? item->name : "";
		data.iconId = data.hasItem ? item->icon : 0;
		data.quantity = data.hasItem ? item->quantity : 0;
		LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (loot-to-inventory): slot={} hasItem={} itemId={} name='{}' qty={}",
			data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
	};

	if (m_pending_loot_slots.empty()) {
		// No pending loot - shouldn't happen, but handle gracefully
		LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: No pending loot slots! Treating as inventory item.");
		if (m_inventory_manager) {
			publishLootedSlot(m_inventory_manager->processItemPacket(p));
		}
		return;
	}

	// Get the slot we're expecting to receive
	int16_t expected_slot = m_pending_loot_slots.front();
	m_pending_loot_slots.erase(m_pending_loot_slots.begin());

	LOG_DEBUG(MOD_INVENTORY, "ZoneProcessLootedItemToInventory: Processing looted item for corpse slot {}, remaining pending={}",
		expected_slot, m_pending_loot_slots.size());

	// Add item to player's inventory
	if (m_inventory_manager) {
		LOG_TRACE(MOD_INVENTORY, "Adding item to inventory via processItemPacket");
		publishLootedSlot(m_inventory_manager->processItemPacket(p));
	}

	// Remove from game-side loot tracking and publish event
	m_loot_items.erase(expected_slot);

	if (m_bridge) {
		eqt::state::LootItemRemovedData data;
		data.corpseId = m_player_looting_corpse_id;
		data.slot = static_cast<uint8_t>(expected_slot);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::LootItemRemoved, std::move(data)));
	}
	LOG_TRACE(MOD_INVENTORY, "Removed item from loot window slot {}", expected_slot);

	// T268: keep the auto-loot queue in step (a hand-taken slot must not be
	// re-requested by a later /lootall). Loot-all sequencing itself is
	// CombatManager's LootingItems state -- the confirmation-driven
	// m_loot_all_* loop that used to continue here was the drifted duplicate.
	if (m_combat_manager) {
		m_combat_manager->RemoveLootItem(static_cast<uint32_t>(expected_slot));
	}
}

void EverQuest::SetupLootCallbacks()
{
	/* D20a: intents handle loot actions */
}

void EverQuest::SetupVendorCallbacks()
{
	/* D20a: intents handle vendor actions */
}

void EverQuest::SetupBankCallbacks()
{
	/* D20a: intents handle bank actions */
}

void EverQuest::SetupTradeWindowCallbacks()
{
	// D20e3: Trade window init moved to InitGraphics (needs renderer)
	// This stub remains for the call in InitGraphics; actual init is done there.
}

void EverQuest::SetupTradeskillCallbacks()
{
	/* D20b1: WindowManager pushes TradeskillCombineIntent/TradeskillCloseIntent via bridge */
}

void EverQuest::ZoneProcessShopRequest(const EQ::Net::Packet &p)
{
	// Server response to our shop request
	// Wire layout: npc_id(4), player_id(4), action(4), sell_rate(4) -- eqemu's
	// Merchant_Click_Struct (npcid/playerid/command/rate), used for OP_ShopRequest
	// in both directions. T147 deleted the dead EQT::Merchant_Open_Struct that
	// duplicated this; EQT::Merchant_Click_Struct is the one to use.
	if (p.Length() < 18) {
		LOG_WARN(MOD_INVENTORY, "ShopRequest packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t action = p.GetUInt32(10);
	float sell_rate = 1.0f;
	// Read sell_rate as float from bytes 14-17
	if (p.Length() >= 18) {
		uint32_t sell_rate_bits = p.GetUInt32(14);
		memcpy(&sell_rate, &sell_rate_bits, sizeof(float));
	}

	LOG_DEBUG(MOD_INVENTORY, "ShopRequest response: npc_id={} action={} sell_rate={:.4f}", npc_id, action, sell_rate);

	if (action == 1) {
		// Success - vendor is open
		m_vendor_npc_id = static_cast<uint16_t>(npc_id);
		m_vendor_sell_rate = sell_rate;

		// Get vendor name from entity if available (converted to display format)
		auto it = m_entities.find(m_vendor_npc_id);
		if (it != m_entities.end()) {
			m_vendor_name = EQT::toDisplayName(it->second.name);
		} else {
			m_vendor_name = "Merchant";
		}

		// T227: source write, ABOVE the bridge guard. Until stage 3 only the
		// GameStateSyncBridge projection wrote player().vendor; the projection
		// is gone, so the handler that owns the data writes it.
		m_game_state.player().setVendor(m_vendor_npc_id, m_vendor_sell_rate, m_vendor_name);

		// Publish VendorWindowOpened (D18a: bridge is sole path to renderer)
		if (m_bridge) {
			eqt::state::WindowOpenedData data;
			data.npcId = m_vendor_npc_id;
			data.npcName = m_vendor_name;
			data.sellRate = m_vendor_sell_rate;
			m_bridge->pushEvent(eqt::state::GameEvent(
				eqt::state::GameEventType::VendorWindowOpened, std::move(data)));
		}

#ifdef WITH_AUDIO
		// Start vendor/bank music (context-based, will restore on close)
		if (m_audio_manager) {
			m_audio_manager->startVendorBankMusic();
		}
#endif

		LOG_INFO(MOD_INVENTORY, "Opened vendor window for {} (id={})", m_vendor_name, m_vendor_npc_id);
	} else {
		LOG_WARN(MOD_INVENTORY, "Vendor open failed: action={}", action);
	}
}

void EverQuest::ZoneProcessShopPlayerBuy(const EQ::Net::Packet &p)
{
	// Server confirmation of purchase
	// Merchant_Purchase_Struct: npc_id(4), player_id(4), itemslot(4), unknown12(4), quantity(4), action(4)
	if (p.Length() < 26) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerBuy packet too short: {} bytes", p.Length());
		return;
	}

	uint32_t npc_id = p.GetUInt32(2);
	uint32_t player_id = p.GetUInt32(6);
	uint32_t itemslot = p.GetUInt32(10);
	uint32_t quantity = p.GetUInt32(18);
	uint32_t action = p.GetUInt32(22);

	LOG_DEBUG(MOD_INVENTORY, "ShopPlayerBuy: npc_id={} player_id={} slot={} qty={} action={}",
		npc_id, player_id, itemslot, quantity, action);

	// Display purchase confirmation and update money (D18a: uses game-side vendor items)
	auto vendorIt = m_vendor_items.find(itemslot);
	if (vendorIt != m_vendor_items.end()) {
		int32_t totalPrice = vendorIt->second.price * static_cast<int32_t>(quantity);
		std::string priceStr = FormatPrice(totalPrice);
		if (quantity > 1) {
			AddChatSystemMessage(fmt::format("You purchased {} x{} for {}", vendorIt->second.name, quantity, priceStr));
		} else {
			AddChatSystemMessage(fmt::format("You purchased {} for {}", vendorIt->second.name, priceStr));
		}

		// Deduct money from local currency (server confirms purchase = money spent)
		int64_t totalCopper = static_cast<int64_t>(m_platinum) * 1000 +
		                      static_cast<int64_t>(m_gold) * 100 +
		                      static_cast<int64_t>(m_silver) * 10 +
		                      static_cast<int64_t>(m_copper);
		totalCopper -= totalPrice;
		if (totalCopper < 0) totalCopper = 0;

		// Convert back to currency (update both local and GameState)
		m_platinum = static_cast<uint32_t>(totalCopper / 1000);
		totalCopper %= 1000;
		m_gold = static_cast<uint32_t>(totalCopper / 100);
		totalCopper %= 100;
		m_silver = static_cast<uint32_t>(totalCopper / 10);
		m_copper = static_cast<uint32_t>(totalCopper % 10);
		m_game_state.player().setCurrency(m_platinum, m_gold, m_silver, m_copper);

		LOG_DEBUG(MOD_INVENTORY, "Updated money: {}pp {}gp {}sp {}cp", m_platinum, m_gold, m_silver, m_copper);
	}

	// ItemPacket with type 103 (inventory) handles the actual item placement
	// and stack quantity updates via InventoryManager::setItem()
}

void EverQuest::ZoneProcessShopPlayerSell(const EQ::Net::Packet &p)
{
	// Server confirmation of sale
	// Merchant_Sell_Response_Struct: npc_id(4), itemslot(4), quantity(4), price(4)
	if (p.Length() < sizeof(EQT::Merchant_Sell_Response_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "ShopPlayerSell packet too short: {} bytes", p.Length());
		return;
	}

	auto* resp = reinterpret_cast<const EQT::Merchant_Sell_Response_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	LOG_INFO(MOD_INVENTORY, "Sold {} items from slot {} for {} copper",
		resp->quantity, resp->itemslot, resp->price);

	// Display sale confirmation and update inventory (D18a: no renderer dependency)
	{
		std::string priceStr = FormatPrice(static_cast<int32_t>(resp->price));
		if (resp->quantity > 1) {
			AddChatSystemMessage(fmt::format("You sold {} items for {}", resp->quantity, priceStr));
		} else {
			AddChatSystemMessage(fmt::format("You sold item for {}", priceStr));
		}

		// Remove item from local inventory
		int16_t invSlot = static_cast<int16_t>(resp->itemslot);
		if (m_inventory_manager) {
			const auto* item = m_inventory_manager->getItem(invSlot);
			if (item) {
				if (item->stackable && item->quantity > static_cast<int32_t>(resp->quantity)) {
					auto* mutableItem = m_inventory_manager->getItemMutable(invSlot);
					if (mutableItem) {
						mutableItem->quantity -= static_cast<int32_t>(resp->quantity);
						LOG_DEBUG(MOD_INVENTORY, "Reduced stack at slot {} to {} items",
							invSlot, mutableItem->quantity);
					}
				} else {
					m_inventory_manager->removeItem(invSlot);
					LOG_DEBUG(MOD_INVENTORY, "Removed item from slot {}", invSlot);
				}

				// Publish InventorySlotChanged (T42: vendor sell was a direct
				// mutation -- removeItem()/quantity decrement above -- with no
				// publish anywhere after it; the UI never learned the slot
				// changed). Re-read the slot post-mutation so a partial-stack
				// sell reports the remaining quantity and a full sell reports
				// empty, in one place regardless of which branch ran above.
				{
					eqt::state::InventorySlotChangedData data;
					data.slotId = invSlot;
					const auto* postItem = m_inventory_manager->getItem(invSlot);
					data.hasItem = (postItem != nullptr && postItem->itemId != 0);
					SyncInventorySlotToGameState(invSlot, data.hasItem);  // T221
					if (m_bridge) {
						data.itemId = data.hasItem ? postItem->itemId : 0;
						data.itemName = data.hasItem ? postItem->name : "";
						data.iconId = data.hasItem ? postItem->icon : 0;
						data.quantity = data.hasItem ? postItem->quantity : 0;
						LOG_DEBUG(MOD_INVENTORY, "Publishing InventorySlotChanged (vendor sell): slot={} hasItem={} itemId={} name='{}' qty={}",
							data.slotId, data.hasItem, data.itemId, data.itemName, data.quantity);
						m_bridge->pushEvent(eqt::state::GameEvent(
							eqt::state::GameEventType::InventorySlotChanged, std::move(data)));
					}
				}
			}
		}
	}
}

void EverQuest::ZoneProcessMoneyUpdate(const EQ::Net::Packet &p)
{
	// Money update after transaction
	// MoneyUpdate_Struct: platinum(4), gold(4), silver(4), copper(4)
	if (p.Length() < sizeof(EQT::MoneyUpdate_Struct) + 2) {
		LOG_WARN(MOD_INVENTORY, "MoneyUpdate packet too short: {} bytes", p.Length());
		return;
	}

	auto* money = reinterpret_cast<const EQT::MoneyUpdate_Struct*>(
		static_cast<const char*>(p.Data()) + 2);

	// Update player money (both local and GameState)
	m_platinum = static_cast<uint32_t>(money->platinum);
	m_gold = static_cast<uint32_t>(money->gold);
	m_silver = static_cast<uint32_t>(money->silver);
	m_copper = static_cast<uint32_t>(money->copper);
	m_game_state.player().setCurrency(m_platinum, m_gold, m_silver, m_copper);

	LOG_DEBUG(MOD_INVENTORY, "Money updated: {}pp {}gp {}sp {}cp",
		m_platinum, m_gold, m_silver, m_copper);

	// Publish CurrencyChanged (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::CurrencyChangedData data;
		data.platinum = static_cast<int32_t>(m_platinum);
		data.gold = static_cast<int32_t>(m_gold);
		data.silver = static_cast<int32_t>(m_silver);
		data.copper = static_cast<int32_t>(m_copper);
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::CurrencyChanged, std::move(data)));
	}
}

void EverQuest::ZoneProcessShopEndConfirm(const EQ::Net::Packet &p)
{
	// Server confirms vendor session ended
	LOG_DEBUG(MOD_INVENTORY, "ShopEndConfirm received, closing vendor window");

	// T227: source write (see ZoneProcessShopRequest).
	m_game_state.player().clearVendor();

	// Publish VendorWindowClosed (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = m_vendor_npc_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorWindowClosed, std::move(data)));
	}

	// Clear vendor state
	m_vendor_npc_id = 0;
	m_vendor_sell_rate = 1.0f;
	m_vendor_name.clear();
	m_vendor_items.clear();
}

void EverQuest::ZoneProcessVendorItemToUI(const EQ::Net::Packet &p)
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
	GameVendorItem gvi;
	gvi.itemId = item->itemId;
	gvi.name = item->name;
	gvi.price = item->price;
	gvi.quantity = item->quantity;
	m_vendor_items[vendorSlot] = std::move(gvi);

	// Publish VendorItemAdded (D18a: bridge is sole path to renderer)
	if (m_bridge) {
		eqt::state::VendorItemAddedData vdata;
		vdata.vendorSlot = static_cast<uint16_t>(vendorSlot);
		vdata.itemId = item->itemId;
		vdata.itemName = item->name;
		vdata.price = item->price;
		vdata.quantity = item->quantity;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorItemAdded, std::move(vdata)));
	}
}

void EverQuest::RequestOpenVendor(uint16_t npcId)
{
	if (m_vendor_npc_id != 0) {
		LOG_DEBUG(MOD_INVENTORY, "Already in vendor session with NPC {}", m_vendor_npc_id);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Requesting vendor open for NPC {}", npcId);

	// Send Merchant_Click_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Click_Struct req;
	memset(&req, 0, sizeof(req));
	req.npc_id = npcId;
	req.player_id = m_my_spawn_id;
	pkt.PutData(0, &req, sizeof(req));
	QueuePacket(HC_OP_ShopRequest, &pkt);
}

void EverQuest::BuyFromVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (m_vendor_npc_id == 0 || m_vendor_npc_id != npcId) {
		LOG_WARN(MOD_INVENTORY, "BuyFromVendor: Not in vendor session with NPC {}", npcId);
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Buying from vendor {}: slot={} quantity={}", npcId, itemSlot, quantity);

	// Send Merchant_Purchase_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_Purchase_Struct buy;
	buy.npc_id = npcId;
	buy.player_id = m_my_spawn_id;
	buy.itemslot = itemSlot;
	buy.unknown12 = 0;
	buy.quantity = quantity;
	buy.action = EQT::MERCHANT_BUY;
	pkt.PutData(0, &buy, sizeof(buy));
	QueuePacket(HC_OP_ShopPlayerBuy, &pkt);
}

void EverQuest::SellToVendor(uint16_t npcId, uint32_t itemSlot, uint32_t quantity)
{
	if (m_vendor_npc_id == 0 || m_vendor_npc_id != npcId) {
		LOG_WARN(MOD_INVENTORY, "SellToVendor: Not in vendor session with NPC {}", npcId);
		AddChatSystemMessage("You are not interacting with a vendor.");
		return;
	}

	// Validate the item exists and can be sold
	if (m_inventory_manager) {
		const auto* item = m_inventory_manager->getItem(static_cast<int16_t>(itemSlot));
		if (!item) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: No item at slot {}", itemSlot);
			AddChatSystemMessage("That item is no longer in your inventory.");
			return;
		}
		if (item->noDrop) {
			LOG_WARN(MOD_INVENTORY, "SellToVendor: Item '{}' is NO_DROP", item->name);
			AddChatSystemMessage(fmt::format("You cannot sell {}.", item->name));
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
	QueuePacket(HC_OP_ShopPlayerSell, &pkt);
}

void EverQuest::CloseVendorWindow()
{
	if (m_vendor_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing vendor window for NPC {}", m_vendor_npc_id);

	// Send Merchant_End_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::Merchant_End_Struct end;
	end.npc_id = m_vendor_npc_id;
	end.player_id = m_my_spawn_id;
	pkt.PutData(0, &end, sizeof(end));
	QueuePacket(HC_OP_ShopEnd, &pkt);

	// T227: source write (see ZoneProcessShopRequest).
	m_game_state.player().clearVendor();

	// Publish VendorWindowClosed
	if (m_bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = m_vendor_npc_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::VendorWindowClosed, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Stop vendor/bank music (context-based, will restore zone music or auto-attack music)
	if (m_audio_manager) {
		m_audio_manager->stopVendorBankMusic();
	}
#endif

	// Clear vendor state
	m_vendor_npc_id = 0;
	m_vendor_sell_rate = 1.0f;
	m_vendor_name.clear();
	m_vendor_items.clear();
}

void EverQuest::OpenBankWindow(uint16_t bankerNpcId)
{
	if (m_banker_npc_id != 0) {
		// Already have bank open, close it first
		CloseBankWindow();
	}

	// Use a dummy NPC ID if none provided (for /bank command testing)
	// In real use, this would be the banker NPC's spawn ID
	m_banker_npc_id = (bankerNpcId != 0) ? bankerNpcId : 1;

	LOG_DEBUG(MOD_INVENTORY, "Opening bank window (banker NPC: {})", m_banker_npc_id);

	// T227: source write, ABOVE the bridge guard (see ZoneProcessShopRequest).
	m_game_state.player().setBanker(m_banker_npc_id);

	// Publish BankWindowOpened
	if (m_bridge) {
		eqt::state::WindowOpenedData data;
		data.npcId = m_banker_npc_id;
		auto it = m_entities.find(m_banker_npc_id);
		data.npcName = (it != m_entities.end()) ? EQT::toDisplayName(it->second.name) : "Banker";
		data.sellRate = 0.0f;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::BankWindowOpened, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Start vendor/bank music (context-based, will restore on close)
	if (m_audio_manager) {
		m_audio_manager->startVendorBankMusic();
	}
#endif

	AddChatSystemMessage("Bank window opened");
}

void EverQuest::CloseBankWindow()
{
	if (m_banker_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_INVENTORY, "Closing bank window (banker NPC: {})", m_banker_npc_id);

	// T227: source write (see ZoneProcessShopRequest).
	m_game_state.player().clearBanker();

	// Publish BankWindowClosed (before clearing m_banker_npc_id)
	if (m_bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = m_banker_npc_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::BankWindowClosed, std::move(data)));
	}

#ifdef WITH_AUDIO
	// Stop vendor/bank music (context-based, will restore zone music or auto-attack music)
	if (m_audio_manager) {
		m_audio_manager->stopVendorBankMusic();
	}
#endif

	// Clear bank state
	m_banker_npc_id = 0;

	AddChatSystemMessage("Bank window closed");
}
// ============================================================================
// Skill Trainer Window Methods
// ============================================================================

void EverQuest::SetupTrainerCallbacks()
{
	/* D20b1: WindowManager pushes TrainSkillIntent/CloseTrainerIntent via bridge */
}

void EverQuest::ZoneProcessGMTraining(const EQ::Net::Packet& p)
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
	m_trainer_npc_id = static_cast<uint16_t>(npc_id);

	// Get trainer name from entity if available
	auto it = m_entities.find(m_trainer_npc_id);
	if (it != m_entities.end()) {
		m_trainer_name = EQT::toDisplayName(it->second.name);
	} else {
		m_trainer_name = "Trainer";
	}

	// T227: source write, ABOVE the bridge guard (see ZoneProcessShopRequest).
	m_game_state.player().setTrainer(m_trainer_npc_id, m_trainer_name);

	// Build list of trainable skills and publish to bridge
	eqt::state::TrainerWindowOpenedData trainerData;
	trainerData.npcId = m_trainer_npc_id;
	trainerData.npcName = m_trainer_name;
	trainerData.platinum = GetPlatinum();
	trainerData.gold = GetGold();
	trainerData.silver = GetSilver();
	trainerData.copper = GetCopper();
	trainerData.practicePoints = GetPracticePoints();

	for (uint8_t skill_id = 0; skill_id < EQT::MAX_PP_SKILL; ++skill_id) {
		uint32_t max_trainable = trainee->skills[skill_id];

		// Skip skills with max_trainable = 0 (not trainable at this trainer)
		if (max_trainable == 0) {
			continue;
		}

		// Get current skill value
		uint32_t current_value = 0;
		if (m_skill_manager) {
			current_value = m_skill_manager->getSkillValue(skill_id);
		}

		// Skip skills already at or above max
		if (current_value >= max_trainable) {
			continue;
		}

		// Get skill name
		const char* skill_name = EQ::getSkillName(skill_id);

		// Calculate training cost
		// EQ training cost formula: (current_value + 1) * 10 copper per point
		uint32_t cost = (current_value + 1) * 10;

		eqt::state::TrainerSkillEntryData skill;
		skill.skillId = skill_id;
		skill.name = skill_name ? skill_name : "Unknown";
		skill.currentValue = current_value;
		skill.maxTrainable = max_trainable;
		skill.cost = cost;
		trainerData.skills.push_back(std::move(skill));

		if (s_debug_level >= 2) {
			LOG_DEBUG(MOD_MAIN, "  Skill {}: {} cur={} max={} cost={}",
				skill_id, skill_name, current_value, max_trainable, cost);
		}
	}

	LOG_INFO(MOD_MAIN, "Trainer window opened for {} (id={}) with {} trainable skills",
		m_trainer_name, m_trainer_npc_id, trainerData.skills.size());

	// Publish TrainerWindowOpened event to bridge
	if (m_bridge) {
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TrainerWindowOpened, std::move(trainerData)));
	}
}

void EverQuest::RequestTrainerWindow(uint16_t npcId)
{
	if (m_trainer_npc_id != 0) {
		LOG_DEBUG(MOD_MAIN, "Already in trainer session with NPC {}", m_trainer_npc_id);
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Requesting trainer window for NPC {}", npcId);

	// Send GMTrainee_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainee_Struct req;
	memset(&req, 0, sizeof(req));
	req.npcid = npcId;
	req.playerid = m_my_spawn_id;
	pkt.PutData(0, &req, sizeof(req));
	QueuePacket(HC_OP_GMTraining, &pkt);
}

void EverQuest::TrainSkill(uint8_t skillId)
{
	if (m_trainer_npc_id == 0) {
		LOG_WARN(MOD_MAIN, "TrainSkill: Not in trainer session");
		AddChatSystemMessage("You are not interacting with a trainer.");
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Training skill {} with trainer {}", skillId, m_trainer_npc_id);

	// Send GMSkillChange_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMSkillChange_Struct train;
	memset(&train, 0, sizeof(train));
	train.npcid = static_cast<uint16_t>(m_trainer_npc_id);
	train.skillbank = 0;  // 0 = normal skills, 1 = languages
	train.skill_id = skillId;
	pkt.PutData(0, &train, sizeof(train));
	QueuePacket(HC_OP_GMTrainSkill, &pkt);
}

void EverQuest::CloseTrainerWindow()
{
	if (m_trainer_npc_id == 0) {
		return;
	}

	LOG_DEBUG(MOD_MAIN, "Closing trainer window for NPC {}", m_trainer_npc_id);

	// Send GMTrainEnd_Struct
	EQ::Net::DynamicPacket pkt;
	EQT::GMTrainEnd_Struct end;
	end.npcid = m_trainer_npc_id;
	end.playerid = m_my_spawn_id;
	pkt.PutData(0, &end, sizeof(end));
	QueuePacket(HC_OP_GMEndTraining, &pkt);

	// T227: source write (see ZoneProcessShopRequest).
	m_game_state.player().clearTrainer();

	// Publish TrainerWindowClosed event to bridge (before clearing state)
	if (m_bridge) {
		eqt::state::WindowClosedData data;
		data.npcId = m_trainer_npc_id;
		m_bridge->pushEvent(eqt::state::GameEvent(
			eqt::state::GameEventType::TrainerWindowClosed, std::move(data)));
	}

	// Clear trainer state
	m_trainer_npc_id = 0;
	m_trainer_name.clear();
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

void EverQuest::SendRecipesFavorite(uint32_t object_type, const std::vector<uint32_t>& favorites)
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

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Sent {} favorite recipes (object_type={})", count, object_type);
	}
}

void EverQuest::SendRecipesSearch(uint32_t object_type, const std::string& query, uint32_t mintrivial, uint32_t maxtrivial)
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

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Recipe search: '{}'", query);
	}
}

void EverQuest::SendRecipeDetails(uint32_t recipe_id)
{
	EQ::Net::DynamicPacket packet;
	packet.PutUInt16(0, HC_OP_RecipeDetails);
	packet.PutUInt32(2, recipe_id);

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Request recipe details: {}", recipe_id);
	}
}

void EverQuest::SendRecipeAutoCombine(uint32_t object_type, uint32_t recipe_id)
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

	if (m_zone_connection && m_zone_connected) {
		m_zone_connection->QueuePacket(packet);
		LOG_DEBUG(MOD_MAIN, "Auto-combine recipe: {}", recipe_id);
	}
}

void EverQuest::ZoneProcessRecipeReply(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::RecipeReply_Struct)) {
		return;
	}

	const EQT::RecipeReply_Struct* recipe = reinterpret_cast<const EQT::RecipeReply_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	std::string name(recipe->recipe_name, strnlen(recipe->recipe_name, 64));
	LOG_DEBUG(MOD_MAIN, "Recipe: {} (ID: {}, Trivial: {})", name, recipe->recipe_id, recipe->trivial);
}

void EverQuest::ZoneProcessRecipeAutoCombine(const EQ::Net::Packet &p)
{
	if (p.Length() < 2 + sizeof(EQT::RecipeAutoCombine_Struct)) {
		return;
	}

	const EQT::RecipeAutoCombine_Struct* combine = reinterpret_cast<const EQT::RecipeAutoCombine_Struct*>(
		static_cast<const uint8_t*>(p.Data()) + 2);

	if (combine->reply_code == 0) {
		LOG_INFO(MOD_MAIN, "Auto-combine successful for recipe {}", combine->recipe_id);
		AddChatSystemMessage("Tradeskill combine successful!");
	} else {
		LOG_INFO(MOD_MAIN, "Auto-combine failed for recipe {}", combine->recipe_id);
		AddChatSystemMessage("Tradeskill combine failed.");
	}
}
