// Vendor/merchant, money, trade, book, GM-training and pet-command wire
// layouts.
//
// Wire layouts, extracted verbatim from the umbrella header by
// refactor/J14_packet_structs_split.md. The RULES for this file family live
// in the umbrella, include/common/packet_structs.h -- read them before
// touching a struct or a compile-time size check here.
//
// This header opens and closes its own one-byte packing region; no include
// sits inside it.

#ifndef EQT_PACKETS_MERCHANT_H
#define EQT_PACKETS_MERCHANT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "common/packets/common.h"

namespace EQT {

#pragma pack(push, 1)

// ============================================================================
// Vendor/Merchant Structures
// ============================================================================

// Merchant open request (client -> server)
struct Merchant_Click_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/ uint32_t unknown08;   // Unknown (always 0)
/*0012*/ uint32_t unknown12;   // Unknown (always 0)
/*0016*/
};
static_assert(sizeof(Merchant_Click_Struct) == 16, "Merchant_Click_Struct must be 16 bytes");

// Merchant purchase action
enum MerchantAction : uint32_t {
    MERCHANT_BUY = 2,
    MERCHANT_SELL = 3
};

// Merchant buy/sell request (client <-> server)
//
// !! DO NOT "FIX" THE NAMES OF THIS STRUCT AND `Merchant_Sell_Struct` BELOW !!
//
// Our two names are INVERTED relative to eqemu's, and that is deliberate --
// both of our wire paths are correct, because eqemu's own naming is
// counterintuitive:
//   * `Handle_OP_ShopPlayerBuy`  (zone/client_packet.cpp:14026) expects
//     eqemu's `Merchant_Sell_Struct`     -- 24 bytes  -> our `Merchant_Purchase_Struct`
//   * `Handle_OP_ShopPlayerSell` (zone/client_packet.cpp:14274) expects
//     eqemu's `Merchant_Purchase_Struct` -- 16 bytes  -> our `Merchant_Sell_Struct`
// Verified T145: `eq_merchant.cpp` sends our 24-byte struct on
// HC_OP_ShopPlayerBuy and our 16-byte struct on HC_OP_ShopPlayerSell, which
// is what the server expects on each. Renaming these to "agree with eqemu"
// breaks BOTH merchant directions at once.
struct Merchant_Purchase_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/ uint32_t itemslot;    // Item slot in merchant inventory (0-based)
/*0012*/ uint32_t unknown12;   // Always 0
/*0016*/ uint32_t quantity;    // Quantity to buy (1 for single, 20 for stack)
/*0020*/ uint32_t action;      // MerchantAction enum (2 = buy, 3 = sell)
/*0024*/
};
static_assert(sizeof(Merchant_Purchase_Struct) == 24, "Merchant_Purchase_Struct must be 24 bytes");

// Merchant session end (client -> server)
struct Merchant_End_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t player_id;   // Player entity ID
/*0008*/
};
static_assert(sizeof(Merchant_End_Struct) == 8, "Merchant_End_Struct must be 8 bytes");

// Merchant sell request (client -> server)
// Note: Titanium uses a simpler 16-byte structure for sells (no player_id, unknown12, action)
struct Merchant_Sell_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t itemslot;    // Player inventory slot (Titanium format)
/*0008*/ uint32_t quantity;    // Quantity to sell
/*0012*/ uint32_t unknown12;   // Unknown value (client sends timestamp-like value)
/*0016*/
};
static_assert(sizeof(Merchant_Sell_Struct) == 16, "Merchant_Sell_Struct must be 16 bytes");

// Merchant sell response (server -> client)
struct Merchant_Sell_Response_Struct {
/*0000*/ uint32_t npc_id;      // NPC spawn ID
/*0004*/ uint32_t itemslot;    // Player inventory slot (Titanium format)
/*0008*/ uint32_t quantity;    // Quantity sold
/*0012*/ uint32_t price;       // Total price in copper
/*0016*/
};
static_assert(sizeof(Merchant_Sell_Response_Struct) == 16, "Merchant_Sell_Response_Struct must be 16 bytes");

// Money update (server -> client) - sent after sell transactions
struct MoneyUpdate_Struct {
/*0000*/ int32_t platinum;
/*0004*/ int32_t gold;
/*0008*/ int32_t silver;
/*0012*/ int32_t copper;
/*0016*/
};
static_assert(sizeof(MoneyUpdate_Struct) == 16, "MoneyUpdate_Struct must be 16 bytes");

// Item packet type constants (first 4 bytes of OP_ItemPacket)
enum ItemPacketType : uint32_t {
    ITEM_PACKET_MERCHANT = 100,     // Item from merchant inventory
    ITEM_PACKET_TRADE_VIEW = 101,   // Item in partner's trade window (slot 0-3)
    ITEM_PACKET_INVENTORY = 103,    // Item in player inventory
    // T282: eqemu zone/object.cpp Object::HandleClick sends the contents of
    // a WORLD tradeskill container (a forge/oven on the ground) with this
    // type, one packet per occupied index, and the serialized item's slot
    // field is the container's own BAG INDEX 0-9 -- not 4000+index. The
    // client maps it into WORLD_BEGIN..WORLD_END on arrival.
    ITEM_PACKET_WORLD_CONTAINER = 107
};

/*
 * Trade packet structures
 * Used for player-to-player trading
 */

// OP_TradeRequest - Initiate a trade request
// Size: 8 bytes
struct TradeRequest_Struct {
    uint32_t target_spawn_id;  // Who to trade with
    uint32_t from_spawn_id;    // Who is requesting the trade
};

// OP_TradeRequestAck - Accept a trade request
// Size: 8 bytes
// NOTE: Server uses same struct as TradeRequest_Struct - first field is recipient
struct TradeRequestAck_Struct {
    uint32_t target_spawn_id;  // Who initiated the trade (packet forwarded to them)
    uint32_t from_spawn_id;    // Who is accepting
};

// Coin type constants for MoveCoin_Struct
static const int32_t COINTYPE_CP = 0;  // Copper
static const int32_t COINTYPE_SP = 1;  // Silver
static const int32_t COINTYPE_GP = 2;  // Gold
static const int32_t COINTYPE_PP = 3;  // Platinum

// Coin slot constants for MoveCoin_Struct
static const int32_t COINSLOT_CURSOR = 0;
static const int32_t COINSLOT_INVENTORY = 1;
static const int32_t COINSLOT_BANK = 2;
static const int32_t COINSLOT_TRADE = 3;
static const int32_t COINSLOT_SHAREDBANK = 4;

// OP_MoveCoin - Move coins between locations (cursor, inventory, trade, etc.)
// Size: 20 bytes
struct MoveCoin_Struct {
    int32_t from_slot;    // Source: 0=cursor, 1=inventory, 2=bank, 3=trade, 4=shared bank
    int32_t to_slot;      // Destination: same as above
    int32_t cointype1;    // Source coin type: 0=copper, 1=silver, 2=gold, 3=platinum
    int32_t cointype2;    // Destination coin type (usually same as cointype1)
    int32_t amount;       // Amount of coins to move
};

// OP_TradeCoins - coins were added to a trade (Server->Client notification)
// Size: 12 bytes
//
// INBOUND ONLY. eqemu has no handler for this opcode; it BUILDS it inside
// Handle_OP_MoveCoin (zone/client_process.cpp:1598) and sends it to the other
// trader. To put coins into a trade, send OP_MoveCoin. T168 deleted the
// SendTradeCoins() that sent on this opcode -- nothing could ever have
// received it.
//
// Ground truth: eqemu TradeCoin_Struct (common/eq_packet_structs.h:1691) --
// trader(4), slot(1), unknown5(2), unknown7(1), amount(4).
struct TradeCoins_Struct {
    uint32_t spawn_id;         // eqemu `trader`: whose coins these are
    uint8_t  slot;             // Coin type: 0=copper, 1=silver, 2=gold, 3=platinum
    uint8_t  unknown[3];       // eqemu writes unknown5=0x4fD2, unknown7=0 here.
                               // NOT a client-side magic value: an older
                               // comment called these "magic bytes ... for
                               // server to accept", which described this
                               // server-built constant as something we had to
                               // send. There is no server accept path at all.
    uint32_t amount;           // Amount of this coin type
};
static_assert(sizeof(TradeCoins_Struct) == 12, "TradeCoins_Struct must be 12 bytes");

// OP_TradeAcceptClick - Click accept button in trade
// Size: 8 bytes
// Byte 4 (`accepted`) is UNSETTLED: the 8-byte size is certain, but whether
// that byte is a meaningful accept flag or eqemu's `unknown4` garbage has not
// been observed on the wire. Recorded in refactor/deferred.md (T147).
struct TradeAcceptClick_Struct {
    uint32_t spawn_id;         // Player clicking accept
    uint8_t  accepted;         // 1 = accepted, 0 = not accepted
    uint8_t  unknown[3];       // Possibly trade ID or padding
};

// OP_CancelTrade - Cancel or reject a trade
// Size: 8 bytes
struct CancelTrade_Struct {
    uint32_t spawn_id;         // Player canceling/rejecting
    uint32_t action;           // Action code (0x07 observed for cancel)
};

// OP_ReadBook - Request to read a book/note item (C->S)
// Variable length - txtfile is null-terminated string
struct BookRequest_Struct {
    uint8_t window;            // Display window (0xFF = new window)
    uint8_t type;              // 0=scroll, 1=book, 2=item info
    char txtfile[1];           // Variable length - text file name from items table
};
static_assert(sizeof(BookRequest_Struct) == 3, "BookRequest_Struct must be 3 bytes");  // eqemu titanium_structs.h:2071 (fixed header; txtfile is variable-length)

// OP_ReadBook - Book/note text response (S->C)
// Variable length - booktext is null-terminated string
struct BookText_Struct {
    uint8_t window;            // Display window (0xFF = new window)
    uint8_t type;              // 0=scroll, 1=book, 2=item info
    char booktext[1];          // Variable length - text content (use '^' for newlines)
};
static_assert(sizeof(BookText_Struct) == 3, "BookText_Struct must be 3 bytes");  // eqemu titanium_structs.h:2063 (fixed header; booktext is variable-length)

/*
 * Skill Training packet structures
 * Used for GM/class trainer skill training
 */

// OP_GMTraining - Skill training request/response (bidirectional)
// Client sends to request training window, server responds with skill caps
// Size: 448 bytes
struct GMTrainee_Struct {
/*0000*/ uint32_t npcid;                   // Trainer NPC entity ID
/*0004*/ uint32_t playerid;                // Player entity ID
/*0008*/ uint32_t skills[MAX_PP_SKILL];    // Array of max trainable skill values (100 skills)
/*0408*/ uint8_t  unknown408[40];          // Unknown padding/data
/*0448*/
};
static_assert(sizeof(GMTrainee_Struct) == 448, "GMTrainee_Struct must be 448 bytes");

// OP_GMEndTraining - End training session (client -> server)
// Size: 8 bytes
struct GMTrainEnd_Struct {
/*0000*/ uint32_t npcid;                   // Trainer NPC entity ID
/*0004*/ uint32_t playerid;                // Player entity ID
/*0008*/
};
static_assert(sizeof(GMTrainEnd_Struct) == 8, "GMTrainEnd_Struct must be 8 bytes");

// OP_GMTrainSkill - Request to train a specific skill (client -> server)
// Size: 12 bytes
struct GMSkillChange_Struct {
/*0000*/ uint16_t npcid;                   // Trainer NPC entity ID (truncated to 16-bit)
/*0002*/ uint8_t  unknown1[2];             // Session identifier or padding
/*0004*/ uint16_t skillbank;               // 0 = normal skills, 1 = languages
/*0006*/ uint8_t  unknown2[2];             // Padding
/*0008*/ uint16_t skill_id;                // Skill ID to train
/*0010*/ uint8_t  unknown3[2];             // Padding
/*0012*/
};
static_assert(sizeof(GMSkillChange_Struct) == 12, "GMSkillChange_Struct must be 12 bytes");

// ============================================================================
// Pet Command Structures
// ============================================================================

// OP_PetCommands - Pet command sent from client to server
// Size: 8 bytes
// Used in: OP_PetCommands packet (client->server)
struct PetCommand_Struct {
/*0000*/ uint32_t command;     // Pet command ID (see pet_constants.h PetCommand enum)
/*0004*/ uint32_t target;      // Target spawn ID (for attack commands, 0 otherwise)
/*0008*/
};
static_assert(sizeof(PetCommand_Struct) == 8, "PetCommand_Struct must be 8 bytes");

// OP_PetCommandState - Pet button state update from server
// Size: 8 bytes
// Used in: OP_PetCommandState packet (server->client)
// Note: This opcode may be unmapped for Titanium (shows 0x0000 in some captures)
struct PetCommandState_Struct {
/*0000*/ uint32_t button_id;   // Pet button ID (see pet_constants.h PetButton enum)
/*0004*/ uint32_t state;       // Button state: 0 = off/unpressed, 1 = on/pressed
/*0008*/
};
static_assert(sizeof(PetCommandState_Struct) == 8, "PetCommandState_Struct must be 8 bytes");

// (T147: the file-level `#pragma pack(pop)` used to sit here, ~700 lines
// before EOF, leaving everything below it at natural alignment. It now sits
// at the bottom of the file so that every struct in this header is packed.
// See the banner comment at the top of the file.)


#pragma pack(pop)

} // namespace EQT

#endif // EQT_PACKETS_MERCHANT_H
