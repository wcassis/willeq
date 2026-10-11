#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace eqt {
namespace inventory {

// Equipment slot IDs (0-21)
//
// T494 (board B796) -- WHICH NUMBERING THIS IS. Everything in this block is
// the TITANIUM numbering: the slot ids the server has already translated to
// by the time a packet reaches inventory_item_parser.cpp, and the ids fable's
// inventory model, MoveItemIntent and the whole wire are indexed by.
//
// `inventory.slot_id` in the database -- and every number an eqemu log line,
// Spire or #summonitem prints -- is the SERVER (RoF2) numbering, which is
// NOT the same: server ammo is 22, general 1-8 are 23-30 and the cursor is
// 33, and server slot 21 (the power source) has no Titanium slot at all.
//
// USER-FACING SLOT NUMBERS ARE `inventory.slot_id`. The conversion lives in
// exactly two functions below -- parseUserSlot() on the way IN and
// describeSlot() on the way OUT. Nothing else in the tree converts.
enum EquipmentSlot : int16_t {
    SLOT_CHARM = 0,
    SLOT_EAR1 = 1,
    SLOT_HEAD = 2,
    SLOT_FACE = 3,
    SLOT_EAR2 = 4,
    SLOT_NECK = 5,
    SLOT_SHOULDERS = 6,
    SLOT_ARMS = 7,
    SLOT_BACK = 8,
    SLOT_WRIST1 = 9,
    SLOT_WRIST2 = 10,
    SLOT_RANGE = 11,
    SLOT_HANDS = 12,
    SLOT_PRIMARY = 13,
    SLOT_SECONDARY = 14,
    SLOT_RING1 = 15,
    SLOT_RING2 = 16,
    SLOT_CHEST = 17,
    SLOT_LEGS = 18,
    SLOT_FEET = 19,
    SLOT_WAIST = 20,
    SLOT_AMMO = 21
};

// Slot ranges
constexpr int16_t EQUIPMENT_BEGIN = 0;
constexpr int16_t EQUIPMENT_END = 21;
constexpr int16_t EQUIPMENT_COUNT = 22;

constexpr int16_t GENERAL_BEGIN = 22;
constexpr int16_t GENERAL_END = 29;
constexpr int16_t GENERAL_COUNT = 8;

constexpr int16_t CURSOR_SLOT = 30;
constexpr int16_t POSSESSIONS_COUNT = 31;
// SERVER_CURSOR_SLOT used to be the literal 33 here. It is now DERIVED from
// clientSlotToServer(CURSOR_SLOT), below the conversion functions -- the
// magic number was the last surviving fragment of the server's slot table in
// fable, and a fragment is how the table drifts.

// Bag slot ranges (each bag has 10 slots)
constexpr int16_t BAG_SLOT_COUNT = 10;
constexpr int16_t GENERAL_BAGS_BEGIN = 251;
constexpr int16_t GENERAL_BAGS_END = 330;  // 8 bags * 10 slots = 80 slots
constexpr int16_t CURSOR_BAG_BEGIN = 331;
constexpr int16_t CURSOR_BAG_END = 340;

// Bank slots
constexpr int16_t BANK_BEGIN = 2000;
constexpr int16_t BANK_END = 2015;
constexpr int16_t BANK_COUNT = 16;
constexpr int16_t BANK_BAGS_BEGIN = 2031;
constexpr int16_t BANK_BAGS_END = 2190;

// Shared bank slots
constexpr int16_t SHARED_BANK_BEGIN = 2500;
constexpr int16_t SHARED_BANK_END = 2501;
constexpr int16_t SHARED_BANK_COUNT = 2;
constexpr int16_t SHARED_BANK_BAGS_BEGIN = 2531;
constexpr int16_t SHARED_BANK_BAGS_END = 2550;

// Trade slots
constexpr int16_t TRADE_BEGIN = 3000;
constexpr int16_t TRADE_END = 3007;
constexpr int16_t TRADE_COUNT = 8;
constexpr int16_t TRADE_BAGS_BEGIN = 3031;
constexpr int16_t TRADE_BAGS_END = 3110;

// World/ground items
constexpr int16_t WORLD_BEGIN = 4000;
constexpr int16_t WORLD_END = 4009;
constexpr int16_t WORLD_COUNT = 10;

// Tradeskill world container slot (used when combining in a world object like a forge)
// This is the slot used in OP_TradeSkillCombine when using a world container
constexpr int16_t SLOT_TRADESKILL_EXPERIMENT_COMBINE = 1000;

// Invalid slot
constexpr int16_t SLOT_INVALID = -1;

// ============================================================================
// Titanium <-> server (RoF2) slot numbering -- T494, board B796
// ============================================================================
// Everything ABOVE is the TITANIUM numbering: it is what fable's inventory
// model is indexed by, because the server has already translated every slot
// id by the time a packet reaches inventory_item_parser.cpp.
//
// `inventory.slot_id` in the database, every eqemu log line, and everything
// `#summonitem`/Spire shows is the SERVER numbering (EQ::invslot, which
// common/emu_constants.h:155-156 aliases to RoF2::invslot). The two differ:
//
//     meaning        server (RoF2)   Titanium (this header)
//     waist               20               20
//     POWER SOURCE        21               -- (not sent to Titanium at all)
//     ammo                22               21
//     general 1-8         23-30            22-29
//     general 9-10        31-32            --
//     cursor              33               30
//     general bags        4010 + 200*bag + idx      251 + 10*bag + idx
//
// The conversions below are ported field for field from the server's own
// pair, /mnt/projects/akk-stack/code/common/patches/titanium.cpp:3576
// `ServerToTitaniumSlot()` and :3666 `TitaniumToServerSlot()`, with the
// constants from common/patches/{titanium,rof2}_limits.h. They are
// deliberately faithful, quirks included (the server maps bank 2016-2023 and
// general bags 9/10 to Titanium numbers Titanium does not have); callers that
// need a slot a Titanium client can actually address use parseUserSlot(),
// which round-trips through both and refuses anything that does not survive.
//
// T494's convention, stated once: USER-FACING SLOT NUMBERS ARE
// `inventory.slot_id`. parseUserSlot() is the only IN conversion and
// describeSlot() is the only OUT one; nothing else in the tree converts, and
// the wire/model stays Titanium end to end.
namespace serverslot {

// RoF2::invslot (rof2_limits.h:124-158) / EQ::invslot.
constexpr int16_t SLOT_WAIST = 20;
constexpr int16_t SLOT_POWER_SOURCE = 21;
constexpr int16_t SLOT_AMMO = 22;
constexpr int16_t GENERAL_BEGIN = 23;
constexpr int16_t GENERAL_END = 32;   // general1..general10
constexpr int16_t CURSOR = 33;
constexpr int16_t POSSESSIONS_COUNT = 34;

constexpr int16_t TRIBUTE_BEGIN = 400;
constexpr int16_t TRIBUTE_END = 404;
constexpr int16_t GUILD_TRIBUTE_BEGIN = 450;
constexpr int16_t GUILD_TRIBUTE_END = 451;
constexpr int16_t SLOT_TRADESKILL_EXPERIMENT_COMBINE = 1000;

constexpr int16_t BANK_BEGIN = 2000;
constexpr int16_t BANK_END = 2023;    // BANK_SIZE 24, not Titanium's 16
constexpr int16_t SHARED_BANK_BEGIN = 2500;
constexpr int16_t SHARED_BANK_END = 2501;
constexpr int16_t TRADE_BEGIN = 3000;
constexpr int16_t TRADE_END = 3007;
constexpr int16_t WORLD_BEGIN = 4000;
constexpr int16_t WORLD_END = 4009;

// EQ::invbag (emu_constants.h:215-235): SLOT_COUNT is 200 per container and
// the families are laid out end to end starting one past WORLD_END.
constexpr int16_t BAG_SLOT_COUNT = 200;
constexpr int16_t GENERAL_BAGS_BEGIN = 4010;   // WORLD_END + 1
constexpr int16_t GENERAL_BAGS_END = 6009;     // + 10 bags * 200
constexpr int16_t CURSOR_BAG_BEGIN = 6010;
constexpr int16_t CURSOR_BAG_END = 6209;
constexpr int16_t BANK_BAGS_BEGIN = 6210;
constexpr int16_t BANK_BAGS_END = 11009;       // + 24 * 200
constexpr int16_t SHARED_BANK_BAGS_BEGIN = 11010;
constexpr int16_t SHARED_BANK_BAGS_END = 11409;
constexpr int16_t TRADE_BAGS_BEGIN = 11410;
constexpr int16_t TRADE_BAGS_END = 13009;

}  // namespace serverslot

/**
 * Server (`inventory.slot_id`) -> this client's model slot.
 * Port of titanium.cpp:3576 ServerToTitaniumSlot(). SLOT_INVALID means the
 * server never serialises that slot to a Titanium client (slot 21, the power
 * source, is the one a user is most likely to type).
 */
constexpr int16_t serverSlotToClient(int16_t serverSlot) {
    if (serverSlot < 0) return SLOT_INVALID;
    if (serverSlot <= serverslot::SLOT_WAIST) return serverSlot;
    if (serverSlot == serverslot::SLOT_AMMO) return static_cast<int16_t>(serverSlot - 1);
    if (serverSlot >= serverslot::GENERAL_BEGIN && serverSlot <= serverslot::GENERAL_BEGIN + 7)
        return static_cast<int16_t>(serverSlot - 1);  // general1-8 only; Titanium has no 9/10
    if (serverSlot >= serverslot::CURSOR &&
        serverSlot <= serverslot::POSSESSIONS_COUNT + serverslot::SLOT_WAIST)
        return static_cast<int16_t>(serverSlot - 3);
    if (serverSlot == serverslot::POSSESSIONS_COUNT + serverslot::SLOT_AMMO)
        return static_cast<int16_t>(serverSlot - 4);
    if (serverSlot >= serverslot::GENERAL_BAGS_BEGIN && serverSlot <= serverslot::GENERAL_BAGS_END)
        return static_cast<int16_t>(
            serverSlot - (serverslot::GENERAL_BAGS_BEGIN - GENERAL_BAGS_BEGIN) -
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((serverSlot - serverslot::GENERAL_BAGS_BEGIN) / serverslot::BAG_SLOT_COUNT));
    if (serverSlot >= serverslot::CURSOR_BAG_BEGIN && serverSlot <= serverslot::CURSOR_BAG_END)
        return static_cast<int16_t>(serverSlot -
                                    (serverslot::CURSOR_BAG_BEGIN - CURSOR_BAG_BEGIN));
    if (serverSlot >= serverslot::TRIBUTE_BEGIN && serverSlot <= serverslot::TRIBUTE_END)
        return serverSlot;
    if (serverSlot >= serverslot::GUILD_TRIBUTE_BEGIN && serverSlot <= serverslot::GUILD_TRIBUTE_END)
        return serverSlot;
    if (serverSlot == serverslot::SLOT_TRADESKILL_EXPERIMENT_COMBINE) return serverSlot;
    if (serverSlot >= serverslot::BANK_BEGIN && serverSlot <= serverslot::BANK_END)
        return serverSlot;
    if (serverSlot >= serverslot::BANK_BAGS_BEGIN && serverSlot <= serverslot::BANK_BAGS_END)
        return static_cast<int16_t>(
            serverSlot - (serverslot::BANK_BAGS_BEGIN - BANK_BAGS_BEGIN) -
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((serverSlot - serverslot::BANK_BAGS_BEGIN) / serverslot::BAG_SLOT_COUNT));
    if (serverSlot >= serverslot::SHARED_BANK_BEGIN && serverSlot <= serverslot::SHARED_BANK_END)
        return serverSlot;
    if (serverSlot >= serverslot::SHARED_BANK_BAGS_BEGIN &&
        serverSlot <= serverslot::SHARED_BANK_BAGS_END)
        return static_cast<int16_t>(
            serverSlot - (serverslot::SHARED_BANK_BAGS_BEGIN - SHARED_BANK_BAGS_BEGIN) -
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((serverSlot - serverslot::SHARED_BANK_BAGS_BEGIN) / serverslot::BAG_SLOT_COUNT));
    if (serverSlot >= serverslot::TRADE_BEGIN && serverSlot <= serverslot::TRADE_END)
        return serverSlot;
    if (serverSlot >= serverslot::TRADE_BAGS_BEGIN && serverSlot <= serverslot::TRADE_BAGS_END)
        return static_cast<int16_t>(
            serverSlot - (serverslot::TRADE_BAGS_BEGIN - TRADE_BAGS_BEGIN) -
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((serverSlot - serverslot::TRADE_BAGS_BEGIN) / serverslot::BAG_SLOT_COUNT));
    if (serverSlot >= serverslot::WORLD_BEGIN && serverSlot <= serverslot::WORLD_END)
        return serverSlot;
    return SLOT_INVALID;
}

/**
 * This client's model slot -> server (`inventory.slot_id`).
 * Port of titanium.cpp:3666 TitaniumToServerSlot(). This is the number a user
 * should SEE, so describeSlot() is its only caller besides parseUserSlot()'s
 * round-trip guard.
 */
constexpr int16_t clientSlotToServer(int16_t clientSlot) {
    if (clientSlot < 0) return SLOT_INVALID;
    if (clientSlot <= SLOT_WAIST) return clientSlot;
    if (clientSlot == SLOT_AMMO) return static_cast<int16_t>(clientSlot + 1);
    if (clientSlot >= GENERAL_BEGIN && clientSlot <= GENERAL_END)
        return static_cast<int16_t>(clientSlot + 1);
    if (clientSlot >= CURSOR_SLOT && clientSlot <= POSSESSIONS_COUNT + SLOT_WAIST)
        return static_cast<int16_t>(clientSlot + 3);
    if (clientSlot == POSSESSIONS_COUNT + SLOT_AMMO)
        return static_cast<int16_t>(clientSlot + 4);
    if (clientSlot >= GENERAL_BAGS_BEGIN && clientSlot <= GENERAL_BAGS_END)
        return static_cast<int16_t>(
            clientSlot + (serverslot::GENERAL_BAGS_BEGIN - GENERAL_BAGS_BEGIN) +
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((clientSlot - GENERAL_BAGS_BEGIN) / BAG_SLOT_COUNT));
    if (clientSlot >= CURSOR_BAG_BEGIN && clientSlot <= CURSOR_BAG_END)
        return static_cast<int16_t>(clientSlot +
                                    (serverslot::CURSOR_BAG_BEGIN - CURSOR_BAG_BEGIN));
    if (clientSlot >= serverslot::TRIBUTE_BEGIN && clientSlot <= serverslot::TRIBUTE_END)
        return clientSlot;
    if (clientSlot == SLOT_TRADESKILL_EXPERIMENT_COMBINE) return clientSlot;
    if (clientSlot >= serverslot::GUILD_TRIBUTE_BEGIN && clientSlot <= serverslot::GUILD_TRIBUTE_END)
        return clientSlot;
    if (clientSlot >= BANK_BEGIN && clientSlot <= BANK_END) return clientSlot;
    if (clientSlot >= BANK_BAGS_BEGIN && clientSlot <= BANK_BAGS_END)
        return static_cast<int16_t>(
            clientSlot + (serverslot::BANK_BAGS_BEGIN - BANK_BAGS_BEGIN) +
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((clientSlot - BANK_BAGS_BEGIN) / BAG_SLOT_COUNT));
    if (clientSlot >= SHARED_BANK_BEGIN && clientSlot <= SHARED_BANK_END) return clientSlot;
    if (clientSlot >= SHARED_BANK_BAGS_BEGIN && clientSlot <= SHARED_BANK_BAGS_END)
        return static_cast<int16_t>(
            clientSlot + (serverslot::SHARED_BANK_BAGS_BEGIN - SHARED_BANK_BAGS_BEGIN) +
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((clientSlot - SHARED_BANK_BAGS_BEGIN) / BAG_SLOT_COUNT));
    if (clientSlot >= TRADE_BEGIN && clientSlot <= TRADE_END) return clientSlot;
    if (clientSlot >= TRADE_BAGS_BEGIN && clientSlot <= TRADE_BAGS_END)
        return static_cast<int16_t>(
            clientSlot + (serverslot::TRADE_BAGS_BEGIN - TRADE_BAGS_BEGIN) +
            (serverslot::BAG_SLOT_COUNT - BAG_SLOT_COUNT) *
                ((clientSlot - TRADE_BAGS_BEGIN) / BAG_SLOT_COUNT));
    if (clientSlot >= WORLD_BEGIN && clientSlot <= WORLD_END) return clientSlot;
    return SLOT_INVALID;
}

// The cursor's server slot, DERIVED rather than the literal 33 it used to be.
constexpr int16_t SERVER_CURSOR_SLOT = clientSlotToServer(CURSOR_SLOT);
static_assert(SERVER_CURSOR_SLOT == 33, "titanium.cpp:3666 maps cursor 30 -> 33");
static_assert(serverSlotToClient(33) == CURSOR_SLOT, "and back again");
static_assert(serverSlotToClient(21) == SLOT_INVALID,
              "RoF2 slotPowerSource is not sent to a Titanium client");
static_assert(serverSlotToClient(23) == GENERAL_BEGIN, "DB general 1 is model slot 22");
static_assert(clientSlotToServer(GENERAL_BEGIN) == 23, "and back again");

// ============================================================================
// CORPSE slots -- the second conversion pair (B810, #199)
// ============================================================================
// The corpse slots the loot window addresses use a DISTINCT pair on the
// server, titanium.cpp:3641 ServerToTitaniumCorpseSlot() / :3727
// TitaniumToServerCorpseSlot(), not the possessions pair (T494's port). The
// two share the SAME three arms over the corpse domain -- general1-8 shift
// by 1, the cursor-and-equipment range (server 33 .. POSSESSIONS_COUNT+
// slotWaist = 34+20 = 54) by 3, and the single POSSESSIONS_COUNT+slotAmmo
// (34+22 = 56) by 4 -- and agree with each other on every one of those
// values. What makes this a SEPARATE pair is the DOMAIN, not the arms: a
// corpse only ever holds what the looter could carry as possessions, so the
// corpse pair REFUSES the looter's own equipment (server 0-22, i.e.
// 0-20 identity and the 22->21 ammo shift the possessions pair performs),
// general 9/10 (server 31/32) and the single 55 -- none of which a corpse
// slot can be. T494 fixed the possessions/bank/trade/world/bag table and
// deliberately left this domain untouched (it is a class-B split, not a
// table split). The model side (titanium_limits.h: slotGeneral1 22 ..
// slotCursor 30, POSSESSIONS_COUNT 31) runs 22-29, 30-51 (+3) and 52 (+4).
//
//     meaning                       server (RoF2)     Titanium (model)
//     general 1-8                       23-30               22-29
//     corpse cursor/equipment range     33-54               30-51
//     POSSESSIONS_COUNT + slotAmmo      56                  52
//     (server 0-22, 31/32 and 55 have no model slot)
//
// WHY THE WIRE IS MODEL-NUMBERED (the same B796 rule): eqemu packs the loot
// window's contents into corpse slots (corpse.cpp MakeLootRequestPackets,
// loot_slot = CORPSE_BEGIN=slotGeneral1=23 .. CORPSE_END) and sends each via
// SendItemPacket -> ENCODE(OP_ItemPacket)'s ServerToTitaniumSlot()
// (titanium.cpp:1135) -- identical to the corpse IN over this domain. The
// client's OP_LootItem comes back through TitaniumToServerCorpseSlot()
// (titanium.cpp:2980). So fable's loot window, LootItemIntent,
// PendingLootSlots, the auto-loot queue and the sent packet are all
// MODEL-numbered, and the server accepts exactly that back. The only hops
// that need the corpse conversion are the USER boundary, B796's rule: a
// typed /loot <N> is a SERVER number (inventory.slot_id), converted on the
// way in by parseCorpseSlot(); anything that PRINTS a corpse slot prints
// corpseSlotToServer(), the same number the user can type back. The loot
// popup draws names only, so it prints no slots. Use this pair -- not the
// possessions pair -- for every corpse hop, because the corpse pair's
// refusal of server 0-22 is exactly what stops "/loot 22" (the looter's
// own ammo slot) from being silently re-read as a corpse slot.
/**
 * Model corpse slot -> server (`inventory.slot_id`).
 * Port of titanium.cpp:3727 TitaniumToServerCorpseSlot(). The number the
 * user should SEE for a corpse slot, so it is the only OUT conversion for
 * the corpse domain.
 */
constexpr int16_t corpseSlotToServer(int16_t modelSlot) {
    if (modelSlot < 0) return SLOT_INVALID;
    if (modelSlot >= GENERAL_BEGIN && modelSlot <= GENERAL_END)
        return static_cast<int16_t>(modelSlot + 1);  // general1-8
    if (modelSlot >= CURSOR_SLOT && modelSlot <= POSSESSIONS_COUNT + SLOT_WAIST)
        return static_cast<int16_t>(modelSlot + 3);  // the corpse cursor/equipment range
    if (modelSlot == POSSESSIONS_COUNT + SLOT_AMMO)
        return static_cast<int16_t>(modelSlot + 4);  // the single ammo corpse slot
    return SLOT_INVALID;
}

/**
 * Server (`inventory.slot_id`) corpse slot -> model slot.
 * Port of titanium.cpp:3641 ServerToTitaniumCorpseSlot(). SLOT_INVALID
 * means the number is not a slot a corpse can hold for a Titanium client.
 */
constexpr int16_t serverCorpseSlotToModel(int16_t serverSlot) {
    if (serverSlot < 0) return SLOT_INVALID;
    if (serverSlot >= serverslot::GENERAL_BEGIN &&
        serverSlot <= serverslot::GENERAL_BEGIN + 7)
        return static_cast<int16_t>(serverSlot - 1);  // general1-8 only
    if (serverSlot >= serverslot::CURSOR &&
        serverSlot <= serverslot::POSSESSIONS_COUNT + serverslot::SLOT_WAIST)
        return static_cast<int16_t>(serverSlot - 3);  // the RoF2 cursor/equipment range
    if (serverSlot == serverslot::POSSESSIONS_COUNT + serverslot::SLOT_AMMO)
        return static_cast<int16_t>(serverSlot - 4);  // the single ammo corpse slot
    return SLOT_INVALID;
}

static_assert(corpseSlotToServer(GENERAL_BEGIN) == 23, "corpse general 1 is server 23");
static_assert(corpseSlotToServer(GENERAL_END) == 30, "corpse general 8 is server 30");
static_assert(corpseSlotToServer(CURSOR_SLOT) == 33, "the model's own cursor prints as 33");
static_assert(corpseSlotToServer(POSSESSIONS_COUNT + SLOT_WAIST) == 54,
              "the +3 arm tops out at model 51 -> server 54");
static_assert(corpseSlotToServer(POSSESSIONS_COUNT + SLOT_AMMO) == 56,
              "the single ammo corpse slot is 52 -> 56, NOT 55");
static_assert(serverCorpseSlotToModel(23) == GENERAL_BEGIN, "and back again");
static_assert(serverCorpseSlotToModel(56) == POSSESSIONS_COUNT + SLOT_AMMO, "and the ammo slot back");
static_assert(serverCorpseSlotToModel(31) == SLOT_INVALID,
              "RoF2 general 9 has no Titanium slot");
static_assert(serverCorpseSlotToModel(22) == SLOT_INVALID,
              "the ammo slot is not a corpse slot -- the domain starts at general 1");
static_assert(serverCorpseSlotToModel(21) == SLOT_INVALID,
              "equipment is not a corpse slot");

// ============================================================================
// parseCorpseSlot -- the corpse-domain inbound conversion (B810, #199)
// ============================================================================
// The /loot <slot> hop: with a loot window open, a typed number is a SERVER
// number (inventory.slot_id, B796's rule) and a word is an item name. This
// parses AND converts over the corpse domain only -- the possessions
// parseUserSlot() is the wrong table here: it converts the looter's own
// equipment and ammo (server 0-22), which a corpse never holds, so it
// cannot refuse "/loot 22". Same three outcomes as parseUserSlot(): a typo
// is a usage error; a real server corpse slot this client cannot reach
// (general 9/10, or the equipment/ammo numbers a database reader might
// type) needs to say so, or the user re-types it and blames the client.
// (The enum lives HERE, above its two users, rather than beside
// parseUserSlot() at the bottom of the file.)
enum class UserSlotResult {
    Ok,
    NotANumber,       // not an integer, or out of int16_t range
    NotAddressable,   // a number, but no slot this client can reach
};

inline UserSlotResult parseCorpseSlot(const std::string& tok, int16_t& out) {
    out = SLOT_INVALID;
    if (tok.empty()) return UserSlotResult::NotANumber;
    long value = 0;
    try {
        size_t consumed = 0;
        value = std::stol(tok, &consumed);
        if (consumed != tok.size()) return UserSlotResult::NotANumber;
    } catch (const std::exception&) {
        return UserSlotResult::NotANumber;
    }
    if (value < std::numeric_limits<int16_t>::min() ||
        value > std::numeric_limits<int16_t>::max()) {
        return UserSlotResult::NotANumber;
    }
    const int16_t serverSlot = static_cast<int16_t>(value);
    const int16_t modelSlot = serverCorpseSlotToModel(serverSlot);
    if (modelSlot == SLOT_INVALID) return UserSlotResult::NotAddressable;
    // Round-trip guard, parseUserSlot()'s device: the corpse pair is
    // injective over its domain, so this passes for every addressable slot
    // and stays the safety net if the table ever drifts.
    if (corpseSlotToServer(modelSlot) != serverSlot) return UserSlotResult::NotAddressable;
    out = modelSlot;
    return UserSlotResult::Ok;
}

/** The refusal text for a corpse slot this client cannot reach, in one place. */
inline std::string describeUnaddressableCorpseSlot(const std::string& tok) {
    return "Slot " + tok +
           " is not a corpse slot this client can reach -- corpse slots are "
           "inventory.slot_id from 23 (general 1) to 56, and general 9/10 have "
           "no Titanium slot.";
}

// ============================================================================
// The ZONE-IN SNAPSHOT's slot set (T439, board B444)
// ============================================================================
// OnGameStateComplete() republishes the whole inventory as enriched
// InventorySlotChanged events so the renderer's panels start the session
// agreeing with the game state. That loop was `for (slot = 0; slot < 30;)`
// -- the 22 equipment slots plus the 8 general ones -- which was every slot
// with a CONSUMER until T436 gave the cursor (30) and the bank (2000..2007)
// one. A character who zoned in holding something on the cursor therefore
// drew no attachment, and an open bank window drew nothing, until the next
// MoveItem happened to republish the slot.
//
// The set is a named table rather than a second literal range so the census
// test and the loop cannot drift; a new panel that wants a slot family at
// zone-in adds it HERE and nowhere else.
//
// BANK_SNAPSHOT_COUNT is EIGHT, not BANK_COUNT's sixteen: the era's
// EQUI_BankWnd.xml declares BW_BankSlot0..7 and nothing consumes 2008..2015
// (T436). BANK_COUNT stays 16 because that is the WIRE's range.
constexpr int16_t BANK_SNAPSHOT_COUNT = 8;

inline std::vector<int16_t> ZoneInSnapshotSlots() {
    std::vector<int16_t> slots;
    slots.reserve(static_cast<size_t>(POSSESSIONS_COUNT) +
                  static_cast<size_t>(BANK_SNAPSHOT_COUNT));
    // 0..29: equipment + general. 30: the cursor. POSSESSIONS_COUNT is 31,
    // so this one range covers both.
    for (int16_t s = 0; s < POSSESSIONS_COUNT; ++s) slots.push_back(s);
    for (int16_t i = 0; i < BANK_SNAPSHOT_COUNT; ++i) {
        slots.push_back(static_cast<int16_t>(BANK_BEGIN + i));
    }
    return slots;
}

// ============================================================================
// Tradeskill Container Types (bagType values)
// These correspond to EQEmu's ObjectType namespace values
// Used to identify what type of tradeskill a container supports
// ============================================================================
enum TradeskillContainerType : uint8_t {
    CONTAINER_SMALL_BAG = 0,
    CONTAINER_LARGE_BAG = 1,
    CONTAINER_QUIVER = 2,
    CONTAINER_BELT_POUCH = 3,
    CONTAINER_WRIST_POUCH = 4,
    CONTAINER_BACKPACK = 5,
    CONTAINER_SMALL_CHEST = 6,
    CONTAINER_LARGE_CHEST = 7,
    CONTAINER_BANDOLIER = 8,
    CONTAINER_MEDICINE = 9,
    CONTAINER_TINKERING = 10,
    CONTAINER_LEXICON = 11,
    CONTAINER_POISON_MAKING = 12,
    CONTAINER_QUEST = 13,
    CONTAINER_MIXING_BOWL = 14,
    CONTAINER_BAKING = 15,
    CONTAINER_TAILORING = 16,
    CONTAINER_BLACKSMITHING = 17,
    CONTAINER_FLETCHING = 18,
    CONTAINER_BREWING = 19,
    CONTAINER_JEWELRY_MAKING = 20,
    CONTAINER_POTTERY = 21,
    CONTAINER_KILN = 22,
    CONTAINER_KEY_MAKER = 23,
    CONTAINER_RESEARCH_WIZ = 24,
    CONTAINER_RESEARCH_MAG = 25,
    CONTAINER_RESEARCH_NEC = 26,
    CONTAINER_RESEARCH_ENC = 27,
    CONTAINER_UNKNOWN = 28,
    CONTAINER_RESEARCH_PRACTICE = 29,
    CONTAINER_ALCHEMY = 30,
    CONTAINER_HIGH_ELF_FORGE = 31,
    CONTAINER_DARK_ELF_FORGE = 32,
    CONTAINER_OGRE_FORGE = 33,
    CONTAINER_DWARF_FORGE = 34,
    CONTAINER_GNOME_FORGE = 35,
    CONTAINER_BARBARIAN_FORGE = 36,
    CONTAINER_IKSAR_FORGE = 37,
    CONTAINER_HUMAN_FORGE_1 = 38,
    CONTAINER_HUMAN_FORGE_2 = 39,
    CONTAINER_HALFLING_TAILORING_1 = 40,
    CONTAINER_HALFLING_TAILORING_2 = 41,
    CONTAINER_ERUDITE_TAILORING = 42,
    CONTAINER_WOOD_ELF_TAILORING = 43,
    CONTAINER_WOOD_ELF_FLETCHING = 44,
    CONTAINER_IKSAR_POTTERY = 45,
    CONTAINER_FISHING = 46,
    CONTAINER_TROLL_FORGE = 47,
    CONTAINER_WOOD_ELF_FORGE = 48,
    CONTAINER_HALFLING_FORGE = 49,
    CONTAINER_ERUDITE_FORGE = 50,
    CONTAINER_MERCHANT = 51,
    CONTAINER_FROGLOK_FORGE = 52,
    CONTAINER_AUGMENTER = 53,
    CONTAINER_CHURN = 54,
    CONTAINER_TRANSFORMATION_MOLD = 55,
    CONTAINER_DETRANSFORMATION_MOLD = 56,
    CONTAINER_UNATTUNER = 57,
    CONTAINER_TRADESKILL_BAG = 58,
    CONTAINER_COLLECTIBLE_BAG = 59,
    CONTAINER_NO_DEPOSIT = 60
};

// Check if a container type is a tradeskill container (not a regular bag)
// Tradeskill containers start at type 10 (tinkering) and go up
// Regular bag types are 0-9
inline bool isTradeskillContainerType(uint8_t containerType) {
    // Regular bags are types 0-9
    // Tradeskill containers start at 10 (tinkering)
    // But some special types like quest(13), merchant(51), etc. are not tradeskill
    switch (containerType) {
        case CONTAINER_TINKERING:
        case CONTAINER_LEXICON:
        case CONTAINER_POISON_MAKING:
        case CONTAINER_MIXING_BOWL:
        case CONTAINER_BAKING:
        case CONTAINER_TAILORING:
        case CONTAINER_BLACKSMITHING:
        case CONTAINER_FLETCHING:
        case CONTAINER_BREWING:
        case CONTAINER_JEWELRY_MAKING:
        case CONTAINER_POTTERY:
        case CONTAINER_KILN:
        case CONTAINER_KEY_MAKER:
        case CONTAINER_RESEARCH_WIZ:
        case CONTAINER_RESEARCH_MAG:
        case CONTAINER_RESEARCH_NEC:
        case CONTAINER_RESEARCH_ENC:
        case CONTAINER_RESEARCH_PRACTICE:
        case CONTAINER_ALCHEMY:
        case CONTAINER_HIGH_ELF_FORGE:
        case CONTAINER_DARK_ELF_FORGE:
        case CONTAINER_OGRE_FORGE:
        case CONTAINER_DWARF_FORGE:
        case CONTAINER_GNOME_FORGE:
        case CONTAINER_BARBARIAN_FORGE:
        case CONTAINER_IKSAR_FORGE:
        case CONTAINER_HUMAN_FORGE_1:
        case CONTAINER_HUMAN_FORGE_2:
        case CONTAINER_HALFLING_TAILORING_1:
        case CONTAINER_HALFLING_TAILORING_2:
        case CONTAINER_ERUDITE_TAILORING:
        case CONTAINER_WOOD_ELF_TAILORING:
        case CONTAINER_WOOD_ELF_FLETCHING:
        case CONTAINER_IKSAR_POTTERY:
        case CONTAINER_FISHING:
        case CONTAINER_TROLL_FORGE:
        case CONTAINER_WOOD_ELF_FORGE:
        case CONTAINER_HALFLING_FORGE:
        case CONTAINER_ERUDITE_FORGE:
        case CONTAINER_FROGLOK_FORGE:
        case CONTAINER_CHURN:
            return true;
        default:
            return false;
    }
}

// Check if a slot is a world container item slot (4000-4009)
inline bool isWorldContainerItemSlot(int16_t slotId) {
    return slotId >= WORLD_BEGIN && slotId <= WORLD_END;
}

// Check if a slot is any world container slot (combine slot 1000 or item slots 4000-4009)
inline bool isWorldContainerSlot(int16_t slotId) {
    return slotId == SLOT_TRADESKILL_EXPERIMENT_COMBINE || isWorldContainerItemSlot(slotId);
}

// Get the tradeskill name for a container type
inline const char* getTradeskillContainerName(uint8_t containerType) {
    switch (containerType) {
        case CONTAINER_SMALL_BAG: return "Small Bag";
        case CONTAINER_LARGE_BAG: return "Large Bag";
        case CONTAINER_QUIVER: return "Quiver";
        case CONTAINER_BELT_POUCH: return "Belt Pouch";
        case CONTAINER_WRIST_POUCH: return "Wrist Pouch";
        case CONTAINER_BACKPACK: return "Backpack";
        case CONTAINER_SMALL_CHEST: return "Small Chest";
        case CONTAINER_LARGE_CHEST: return "Large Chest";
        case CONTAINER_BANDOLIER: return "Bandolier";
        case CONTAINER_MEDICINE: return "Medicine Bag";
        case CONTAINER_TINKERING: return "Tinkering";
        case CONTAINER_LEXICON: return "Lexicon";
        case CONTAINER_POISON_MAKING: return "Mortar and Pestle";
        case CONTAINER_QUEST: return "Quest Container";
        case CONTAINER_MIXING_BOWL: return "Mixing Bowl";
        case CONTAINER_BAKING: return "Oven";
        case CONTAINER_TAILORING: return "Loom";
        case CONTAINER_BLACKSMITHING: return "Forge";
        case CONTAINER_FLETCHING: return "Fletching Kit";
        case CONTAINER_BREWING: return "Brew Barrel";
        case CONTAINER_JEWELRY_MAKING: return "Jeweler's Kit";
        case CONTAINER_POTTERY: return "Pottery Wheel";
        case CONTAINER_KILN: return "Kiln";
        case CONTAINER_KEY_MAKER: return "Key Maker";
        case CONTAINER_RESEARCH_WIZ: return "Wizard Lexicon";
        case CONTAINER_RESEARCH_MAG: return "Mage Lexicon";
        case CONTAINER_RESEARCH_NEC: return "Necromancer Lexicon";
        case CONTAINER_RESEARCH_ENC: return "Enchanter Lexicon";
        case CONTAINER_RESEARCH_PRACTICE: return "Practice Lexicon";
        case CONTAINER_ALCHEMY: return "Alchemy Table";
        case CONTAINER_HIGH_ELF_FORGE: return "High Elf Forge";
        case CONTAINER_DARK_ELF_FORGE: return "Dark Elf Forge";
        case CONTAINER_OGRE_FORGE: return "Ogre Forge";
        case CONTAINER_DWARF_FORGE: return "Dwarf Forge";
        case CONTAINER_GNOME_FORGE: return "Gnome Forge";
        case CONTAINER_BARBARIAN_FORGE: return "Barbarian Forge";
        case CONTAINER_IKSAR_FORGE: return "Iksar Forge";
        case CONTAINER_HUMAN_FORGE_1: return "Human Forge";
        case CONTAINER_HUMAN_FORGE_2: return "Human Forge";
        case CONTAINER_HALFLING_TAILORING_1: return "Halfling Loom";
        case CONTAINER_HALFLING_TAILORING_2: return "Halfling Loom";
        case CONTAINER_ERUDITE_TAILORING: return "Erudite Loom";
        case CONTAINER_WOOD_ELF_TAILORING: return "Wood Elf Loom";
        case CONTAINER_WOOD_ELF_FLETCHING: return "Wood Elf Fletching Kit";
        case CONTAINER_IKSAR_POTTERY: return "Iksar Pottery Wheel";
        case CONTAINER_FISHING: return "Tackle Box";
        case CONTAINER_TROLL_FORGE: return "Troll Forge";
        case CONTAINER_WOOD_ELF_FORGE: return "Wood Elf Forge";
        case CONTAINER_HALFLING_FORGE: return "Halfling Forge";
        case CONTAINER_ERUDITE_FORGE: return "Erudite Forge";
        case CONTAINER_MERCHANT: return "Merchant";
        case CONTAINER_FROGLOK_FORGE: return "Froglok Forge";
        case CONTAINER_AUGMENTER: return "Augmenter";
        case CONTAINER_CHURN: return "Churn";
        case CONTAINER_TRANSFORMATION_MOLD: return "Transformation Mold";
        case CONTAINER_DETRANSFORMATION_MOLD: return "Detransformation Mold";
        case CONTAINER_UNATTUNER: return "Unattuner";
        case CONTAINER_TRADESKILL_BAG: return "Tradeskill Bag";
        case CONTAINER_COLLECTIBLE_BAG: return "Collectible Bag";
        case CONTAINER_NO_DEPOSIT: return "No Deposit";
        default: return "Unknown Container";
    }
}

// Item sizes
enum ItemSize : uint8_t {
    SIZE_TINY = 0,
    SIZE_SMALL = 1,
    SIZE_MEDIUM = 2,
    SIZE_LARGE = 3,
    SIZE_GIANT = 4,
    SIZE_GIGANTIC = 5
};

// Item flags (bitmask)
enum ItemFlags : uint32_t {
    ITEM_FLAG_NONE = 0,
    ITEM_FLAG_MAGIC = (1 << 0),
    ITEM_FLAG_LORE = (1 << 1),
    ITEM_FLAG_NODROP = (1 << 2),
    ITEM_FLAG_NORENT = (1 << 3),
    ITEM_FLAG_ARTIFACT = (1 << 4),
    ITEM_FLAG_SUMMONED = (1 << 5),
    ITEM_FLAG_NODESTROY = (1 << 6),
    ITEM_FLAG_STACKABLE = (1 << 7),
    ITEM_FLAG_EXPENDABLE = (1 << 8),
    ITEM_FLAG_QUESTITEM = (1 << 9)
};

// Equipment slot bitmask (for item.slots field)
enum EquipSlotBit : uint32_t {
    EQUIPSLOT_CHARM = (1 << 0),
    EQUIPSLOT_EAR = (1 << 1),      // Both ears
    EQUIPSLOT_HEAD = (1 << 2),
    EQUIPSLOT_FACE = (1 << 3),
    EQUIPSLOT_NECK = (1 << 5),
    EQUIPSLOT_SHOULDERS = (1 << 6),
    EQUIPSLOT_ARMS = (1 << 7),
    EQUIPSLOT_BACK = (1 << 8),
    EQUIPSLOT_WRIST = (1 << 9),    // Both wrists
    EQUIPSLOT_RANGE = (1 << 11),
    EQUIPSLOT_HANDS = (1 << 12),
    EQUIPSLOT_PRIMARY = (1 << 13),
    EQUIPSLOT_SECONDARY = (1 << 14),
    EQUIPSLOT_FINGER = (1 << 15),  // Both fingers
    EQUIPSLOT_CHEST = (1 << 17),
    EQUIPSLOT_LEGS = (1 << 18),
    EQUIPSLOT_FEET = (1 << 19),
    EQUIPSLOT_WAIST = (1 << 20),
    EQUIPSLOT_AMMO = (1 << 21)
};

// Class bitmask values
enum ClassBit : uint32_t {
    CLASS_WARRIOR = (1 << 0),
    CLASS_CLERIC = (1 << 1),
    CLASS_PALADIN = (1 << 2),
    CLASS_RANGER = (1 << 3),
    CLASS_SHADOWKNIGHT = (1 << 4),
    CLASS_DRUID = (1 << 5),
    CLASS_MONK = (1 << 6),
    CLASS_BARD = (1 << 7),
    CLASS_ROGUE = (1 << 8),
    CLASS_SHAMAN = (1 << 9),
    CLASS_NECROMANCER = (1 << 10),
    CLASS_WIZARD = (1 << 11),
    CLASS_MAGICIAN = (1 << 12),
    CLASS_ENCHANTER = (1 << 13),
    CLASS_BEASTLORD = (1 << 14),
    CLASS_BERSERKER = (1 << 15),
    CLASS_ALL = 0xFFFF
};

// Race bitmask values
enum RaceBit : uint32_t {
    RACE_HUMAN = (1 << 0),
    RACE_BARBARIAN = (1 << 1),
    RACE_ERUDITE = (1 << 2),
    RACE_WOOD_ELF = (1 << 3),
    RACE_HIGH_ELF = (1 << 4),
    RACE_DARK_ELF = (1 << 5),
    RACE_HALF_ELF = (1 << 6),
    RACE_DWARF = (1 << 7),
    RACE_TROLL = (1 << 8),
    RACE_OGRE = (1 << 9),
    RACE_HALFLING = (1 << 10),
    RACE_GNOME = (1 << 11),
    RACE_IKSAR = (1 << 12),
    RACE_VAH_SHIR = (1 << 13),
    RACE_FROGLOK = (1 << 14),
    RACE_DRAKKIN = (1 << 15),
    RACE_ALL = 0xFFFF
};

// Weapon skill types (for skillType field)
enum WeaponSkillType : uint8_t {
    SKILL_1H_SLASHING = 0,      // 1HS - 1-hand slashing
    SKILL_2H_SLASHING = 1,      // 2HS - 2-hand slashing
    SKILL_PIERCING = 2,         // 1HP - piercing
    SKILL_1H_BLUNT = 3,         // 1HB - 1-hand blunt
    SKILL_2H_BLUNT = 4,         // 2HB - 2-hand blunt
    SKILL_ARCHERY = 5,          // BOW - archery
    SKILL_THROWING = 6,         // THR - throwing
    SKILL_HAND_TO_HAND = 7,     // H2H - hand to hand
    SKILL_2H_PIERCING = 35,     // 2HP - 2-hand piercing
    SKILL_NONE = 255            // No weapon skill
};

// Get weapon type abbreviation string
inline const char* getWeaponTypeAbbrev(uint8_t skillType) {
    switch (skillType) {
        case SKILL_1H_SLASHING:  return "1HS";
        case SKILL_2H_SLASHING:  return "2HS";
        case SKILL_PIERCING:     return "1HP";
        case SKILL_1H_BLUNT:     return "1HB";
        case SKILL_2H_BLUNT:     return "2HB";
        case SKILL_ARCHERY:      return "BOW";
        case SKILL_THROWING:     return "THR";
        case SKILL_HAND_TO_HAND: return "H2H";
        case SKILL_2H_PIERCING:  return "2HP";
        default:                 return nullptr;
    }
}

// Augment type bitmask
enum AugTypeBit : uint32_t {
    AUG_TYPE_NONE = 0x00000000,
    AUG_TYPE_GENERAL_SINGLE_STAT = 0x00000001,
    AUG_TYPE_GENERAL_MULTIPLE_STAT = 0x00000002,
    AUG_TYPE_GENERAL_SPELL_EFFECT = 0x00000004,
    AUG_TYPE_WEAPON_GENERAL = 0x00000008,
    AUG_TYPE_WEAPON_ELEM_DAMAGE = 0x00000010,
    AUG_TYPE_WEAPON_BASE_DAMAGE = 0x00000020,
    AUG_TYPE_GENERAL_GROUP = 0x00000040,
    AUG_TYPE_GENERAL_RAID = 0x00000080,
    AUG_TYPE_ORNAMENTATION = 0x00080000,
    AUG_TYPE_SPECIAL_ORNAMENTATION = 0x00100000
};

// Maximum augment sockets per item
constexpr int MAX_AUGMENT_SLOTS = 5;

// Slot calculation utilities
inline int16_t calcBagSlotId(int16_t generalSlot, int16_t bagIndex) {
    if (generalSlot < GENERAL_BEGIN || generalSlot > GENERAL_END) {
        return SLOT_INVALID;
    }
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return GENERAL_BAGS_BEGIN + (generalSlot - GENERAL_BEGIN) * BAG_SLOT_COUNT + bagIndex;
}

inline int16_t calcCursorBagSlotId(int16_t bagIndex) {
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return CURSOR_BAG_BEGIN + bagIndex;
}

inline bool isEquipmentSlot(int16_t slotId) {
    return slotId >= EQUIPMENT_BEGIN && slotId <= EQUIPMENT_END;
}

inline bool isGeneralSlot(int16_t slotId) {
    return slotId >= GENERAL_BEGIN && slotId <= GENERAL_END;
}

inline bool isBagSlot(int16_t slotId) {
    return (slotId >= GENERAL_BAGS_BEGIN && slotId <= GENERAL_BAGS_END) ||
           (slotId >= CURSOR_BAG_BEGIN && slotId <= CURSOR_BAG_END);
}

inline bool isCursorSlot(int16_t slotId) {
    return slotId == CURSOR_SLOT || slotId == SERVER_CURSOR_SLOT;
}

inline bool isTradeSlot(int16_t slotId) {
    return slotId >= TRADE_BEGIN && slotId <= TRADE_END;
}

inline bool isTradeBagSlot(int16_t slotId) {
    return slotId >= TRADE_BAGS_BEGIN && slotId <= TRADE_BAGS_END;
}

// Get the parent general slot for a bag slot
inline int16_t getParentGeneralSlot(int16_t bagSlot) {
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return GENERAL_BEGIN + (bagSlot - GENERAL_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return CURSOR_SLOT;
    }
    return SLOT_INVALID;
}

// Get the index within a bag (0-9)
inline int16_t getBagIndex(int16_t bagSlot) {
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return (bagSlot - GENERAL_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return bagSlot - CURSOR_BAG_BEGIN;
    }
    return SLOT_INVALID;
}

// ============================================================================
// Bank slot utilities
// ============================================================================

// Check if slot is a main bank slot (2000-2015)
inline bool isBankSlot(int16_t slotId) {
    return slotId >= BANK_BEGIN && slotId <= BANK_END;
}

// Check if slot is a shared bank slot (2500-2501)
inline bool isSharedBankSlot(int16_t slotId) {
    return slotId >= SHARED_BANK_BEGIN && slotId <= SHARED_BANK_END;
}

// Check if slot is any bank slot (main or shared)
inline bool isAnyBankSlot(int16_t slotId) {
    return isBankSlot(slotId) || isSharedBankSlot(slotId);
}

// Check if slot is inside a bank bag (2031-2190)
inline bool isBankBagSlot(int16_t slotId) {
    return slotId >= BANK_BAGS_BEGIN && slotId <= BANK_BAGS_END;
}

// Check if slot is inside a shared bank bag (2531-2550)
inline bool isSharedBankBagSlot(int16_t slotId) {
    return slotId >= SHARED_BANK_BAGS_BEGIN && slotId <= SHARED_BANK_BAGS_END;
}

// Check if slot is inside any bank bag (main or shared)
inline bool isAnyBankBagSlot(int16_t slotId) {
    return isBankBagSlot(slotId) || isSharedBankBagSlot(slotId);
}

// Calculate bag slot ID for an item inside a bank container
// bankSlot: the bank slot containing the bag (2000-2015)
// bagIndex: index within the bag (0-9)
// Returns: the calculated bag slot ID (2031-2190), or SLOT_INVALID if invalid
inline int16_t calcBankBagSlotId(int16_t bankSlot, int16_t bagIndex) {
    if (bankSlot < BANK_BEGIN || bankSlot > BANK_END) {
        return SLOT_INVALID;
    }
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return BANK_BAGS_BEGIN + (bankSlot - BANK_BEGIN) * BAG_SLOT_COUNT + bagIndex;
}

// Calculate bag slot ID for an item inside a shared bank container
// sharedBankSlot: the shared bank slot containing the bag (2500-2501)
// bagIndex: index within the bag (0-9)
// Returns: the calculated bag slot ID (2531-2550), or SLOT_INVALID if invalid
inline int16_t calcSharedBankBagSlotId(int16_t sharedBankSlot, int16_t bagIndex) {
    if (sharedBankSlot < SHARED_BANK_BEGIN || sharedBankSlot > SHARED_BANK_END) {
        return SLOT_INVALID;
    }
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }
    return SHARED_BANK_BAGS_BEGIN + (sharedBankSlot - SHARED_BANK_BEGIN) * BAG_SLOT_COUNT + bagIndex;
}

// Get the parent bank slot for a bank bag slot
// bagSlot: a slot inside a bank bag (2031-2190)
// Returns: the parent bank slot (2000-2015), or SLOT_INVALID if not a bank bag slot
inline int16_t getParentBankSlot(int16_t bagSlot) {
    if (bagSlot >= BANK_BAGS_BEGIN && bagSlot <= BANK_BAGS_END) {
        return BANK_BEGIN + (bagSlot - BANK_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }
    return SLOT_INVALID;
}

// Get the parent shared bank slot for a shared bank bag slot
// bagSlot: a slot inside a shared bank bag (2531-2550)
// Returns: the parent shared bank slot (2500-2501), or SLOT_INVALID if not a shared bank bag slot
inline int16_t getParentSharedBankSlot(int16_t bagSlot) {
    if (bagSlot >= SHARED_BANK_BAGS_BEGIN && bagSlot <= SHARED_BANK_BAGS_END) {
        return SHARED_BANK_BEGIN + (bagSlot - SHARED_BANK_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }
    return SLOT_INVALID;
}

// Get the parent slot for any bank bag slot (main or shared)
// Returns the parent bank/shared bank slot, or SLOT_INVALID if not a bank bag slot
inline int16_t getParentBankSlotAny(int16_t bagSlot) {
    int16_t parent = getParentBankSlot(bagSlot);
    if (parent != SLOT_INVALID) {
        return parent;
    }
    return getParentSharedBankSlot(bagSlot);
}

// Get the index within a bank bag (0-9)
// bagSlot: a slot inside a bank bag (2031-2190)
// Returns: the index within the bag (0-9), or SLOT_INVALID if not a bank bag slot
inline int16_t getBankBagIndex(int16_t bagSlot) {
    if (bagSlot >= BANK_BAGS_BEGIN && bagSlot <= BANK_BAGS_END) {
        return (bagSlot - BANK_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }
    return SLOT_INVALID;
}

// Get the index within a shared bank bag (0-9)
// bagSlot: a slot inside a shared bank bag (2531-2550)
// Returns: the index within the bag (0-9), or SLOT_INVALID if not a shared bank bag slot
inline int16_t getSharedBankBagIndex(int16_t bagSlot) {
    if (bagSlot >= SHARED_BANK_BAGS_BEGIN && bagSlot <= SHARED_BANK_BAGS_END) {
        return (bagSlot - SHARED_BANK_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }
    return SLOT_INVALID;
}

// Get the index within any bank bag (main or shared)
// Returns the bag index (0-9), or SLOT_INVALID if not a bank bag slot
inline int16_t getBankBagIndexAny(int16_t bagSlot) {
    int16_t index = getBankBagIndex(bagSlot);
    if (index != SLOT_INVALID) {
        return index;
    }
    return getSharedBankBagIndex(bagSlot);
}

// ============================================================================
// Universal container slot utilities
// ============================================================================

// Calculate the slot ID for an item inside any container type
// parentSlot: the slot containing the container (general, bank, shared bank, or cursor)
// bagIndex: index within the container (0-9)
// Returns: the calculated slot ID, or SLOT_INVALID if invalid parent or index
inline int16_t calcContainerSlotId(int16_t parentSlot, int16_t bagIndex) {
    // Validate bag index
    if (bagIndex < 0 || bagIndex >= BAG_SLOT_COUNT) {
        return SLOT_INVALID;
    }

    // General inventory slots (22-29)
    if (parentSlot >= GENERAL_BEGIN && parentSlot <= GENERAL_END) {
        return GENERAL_BAGS_BEGIN + (parentSlot - GENERAL_BEGIN) * BAG_SLOT_COUNT + bagIndex;
    }

    // Cursor slot (30)
    if (parentSlot == CURSOR_SLOT) {
        return CURSOR_BAG_BEGIN + bagIndex;
    }

    // Bank slots (2000-2015)
    if (parentSlot >= BANK_BEGIN && parentSlot <= BANK_END) {
        return BANK_BAGS_BEGIN + (parentSlot - BANK_BEGIN) * BAG_SLOT_COUNT + bagIndex;
    }

    // Shared bank slots (2500-2501)
    if (parentSlot >= SHARED_BANK_BEGIN && parentSlot <= SHARED_BANK_END) {
        return SHARED_BANK_BAGS_BEGIN + (parentSlot - SHARED_BANK_BEGIN) * BAG_SLOT_COUNT + bagIndex;
    }

    // Trade slots (3000-3007)
    if (parentSlot >= TRADE_BEGIN && parentSlot <= TRADE_END) {
        return TRADE_BAGS_BEGIN + (parentSlot - TRADE_BEGIN) * BAG_SLOT_COUNT + bagIndex;
    }

    return SLOT_INVALID;
}

// Get the parent container slot for any bag slot
// bagSlot: a slot inside any container
// Returns: the parent container slot, or SLOT_INVALID if not a bag slot
inline int16_t getParentContainerSlot(int16_t bagSlot) {
    // General bag slots (251-330)
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return GENERAL_BEGIN + (bagSlot - GENERAL_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }

    // Cursor bag slots (331-340)
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return CURSOR_SLOT;
    }

    // Bank bag slots (2031-2190)
    if (bagSlot >= BANK_BAGS_BEGIN && bagSlot <= BANK_BAGS_END) {
        return BANK_BEGIN + (bagSlot - BANK_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }

    // Shared bank bag slots (2531-2550)
    if (bagSlot >= SHARED_BANK_BAGS_BEGIN && bagSlot <= SHARED_BANK_BAGS_END) {
        return SHARED_BANK_BEGIN + (bagSlot - SHARED_BANK_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }

    // Trade bag slots (3031-3110)
    if (bagSlot >= TRADE_BAGS_BEGIN && bagSlot <= TRADE_BAGS_END) {
        return TRADE_BEGIN + (bagSlot - TRADE_BAGS_BEGIN) / BAG_SLOT_COUNT;
    }

    return SLOT_INVALID;
}

// Get the index within any container (0-9)
// bagSlot: a slot inside any container
// Returns: the index within the container (0-9), or SLOT_INVALID if not a bag slot
inline int16_t getContainerIndex(int16_t bagSlot) {
    // General bag slots (251-330)
    if (bagSlot >= GENERAL_BAGS_BEGIN && bagSlot <= GENERAL_BAGS_END) {
        return (bagSlot - GENERAL_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }

    // Cursor bag slots (331-340)
    if (bagSlot >= CURSOR_BAG_BEGIN && bagSlot <= CURSOR_BAG_END) {
        return bagSlot - CURSOR_BAG_BEGIN;
    }

    // Bank bag slots (2031-2190)
    if (bagSlot >= BANK_BAGS_BEGIN && bagSlot <= BANK_BAGS_END) {
        return (bagSlot - BANK_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }

    // Shared bank bag slots (2531-2550)
    if (bagSlot >= SHARED_BANK_BAGS_BEGIN && bagSlot <= SHARED_BANK_BAGS_END) {
        return (bagSlot - SHARED_BANK_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }

    // Trade bag slots (3031-3110)
    if (bagSlot >= TRADE_BAGS_BEGIN && bagSlot <= TRADE_BAGS_END) {
        return (bagSlot - TRADE_BAGS_BEGIN) % BAG_SLOT_COUNT;
    }

    return SLOT_INVALID;
}

// Check if a slot is inside any container (bag slot of any type)
inline bool isContainerSlot(int16_t slotId) {
    return (slotId >= GENERAL_BAGS_BEGIN && slotId <= GENERAL_BAGS_END) ||
           (slotId >= CURSOR_BAG_BEGIN && slotId <= CURSOR_BAG_END) ||
           (slotId >= BANK_BAGS_BEGIN && slotId <= BANK_BAGS_END) ||
           (slotId >= SHARED_BANK_BAGS_BEGIN && slotId <= SHARED_BANK_BAGS_END) ||
           (slotId >= TRADE_BAGS_BEGIN && slotId <= TRADE_BAGS_END);
}

// Check if a slot can contain items (is a container slot like general, bank, cursor, trade)
inline bool isContainerParentSlot(int16_t slotId) {
    return isGeneralSlot(slotId) ||
           isCursorSlot(slotId) ||
           isBankSlot(slotId) ||
           isSharedBankSlot(slotId) ||
           isTradeSlot(slotId);
}

// Get human-readable slot name
inline const char* getSlotName(int16_t slotId) {
    switch (slotId) {
        case SLOT_CHARM: return "Charm";
        case SLOT_EAR1: return "Ear";
        case SLOT_HEAD: return "Head";
        case SLOT_FACE: return "Face";
        case SLOT_EAR2: return "Ear";
        case SLOT_NECK: return "Neck";
        case SLOT_SHOULDERS: return "Shoulders";
        case SLOT_ARMS: return "Arms";
        case SLOT_BACK: return "Back";
        case SLOT_WRIST1: return "Wrist";
        case SLOT_WRIST2: return "Wrist";
        case SLOT_RANGE: return "Range";
        case SLOT_HANDS: return "Hands";
        case SLOT_PRIMARY: return "Primary";
        case SLOT_SECONDARY: return "Secondary";
        case SLOT_RING1: return "Fingers";
        case SLOT_RING2: return "Fingers";
        case SLOT_CHEST: return "Chest";
        case SLOT_LEGS: return "Legs";
        case SLOT_FEET: return "Feet";
        case SLOT_WAIST: return "Waist";
        case SLOT_AMMO: return "Ammo";
        default: return "Unknown";
    }
}

// T141 -- friendlier reply text: names the slot when it's a canonical
// equipment slot (getSlotName() above), otherwise just the number --
// getSlotName() itself returns "Unknown" for anything outside 0-21, which
// would be a worse message than just showing the slot id. Moved here
// (from eq_commands.cpp's file-local DescribeEquipSlot()) so both
// InventoryManager's own messages and EverQuest's command-layer messages
// share one formatter instead of two copies.
// T494 (board B796): the number PRINTED is the server's (`inventory.slot_id`),
// because that is the number the reader can type back in -- see parseUserSlot()
// below, which is this function's exact inverse. The argument is still a MODEL
// slot: every caller passes the slot it is holding, untouched.
inline std::string describeSlot(int16_t slotId) {
    const int16_t shown = clientSlotToServer(slotId);
    const std::string number = std::to_string(shown == SLOT_INVALID ? slotId : shown);
    if (isEquipmentSlot(slotId)) {
        return std::string(getSlotName(slotId)) + " (" + number + ")";
    }
    // T282: 4000-4009 read as bare numbers in every refusal message about a
    // world tradeskill container; name them by their 1-based container index.
    if (isWorldContainerItemSlot(slotId)) {
        return "tradeskill container slot " + std::to_string(slotId - WORLD_BEGIN + 1) +
               " (" + number + ")";
    }
    return "slot " + number;
}

// ============================================================================
// parseUserSlot -- the ONE inbound conversion (T494, board B796)
// ============================================================================
// Every command that takes a slot NUMBER from a user (/equip, /read, /scribe,
// /sell, /combine, and the same via --script and the llmbench timelines) goes
// through here. It parses AND converts, so a command handler never sees a
// server number and never has to remember which numbering it is holding.
//
// Three outcomes, because the refusals are not interchangeable: a typo is a
// usage error, but a real server slot this client is not sent (21, the power
// source, is the one a reader of the DB will type) needs to say so, or the
// user re-types it and blames the client. UserSlotResult is defined above,
// beside parseCorpseSlot() -- the first user of the three-outcome shape.
inline UserSlotResult parseUserSlot(const std::string& tok, int16_t& out) {
    out = SLOT_INVALID;
    if (tok.empty()) return UserSlotResult::NotANumber;
    long value = 0;
    try {
        size_t consumed = 0;
        value = std::stol(tok, &consumed);
        if (consumed != tok.size()) return UserSlotResult::NotANumber;
    } catch (const std::exception&) {
        return UserSlotResult::NotANumber;
    }
    if (value < std::numeric_limits<int16_t>::min() ||
        value > std::numeric_limits<int16_t>::max()) {
        return UserSlotResult::NotANumber;
    }
    const int16_t serverSlot = static_cast<int16_t>(value);
    const int16_t clientSlot = serverSlotToClient(serverSlot);
    if (clientSlot == SLOT_INVALID) return UserSlotResult::NotAddressable;
    // The ROUND-TRIP guard. The server's own table is not injective over its
    // whole domain -- it maps bank 2016-2023 and general bags 9/10 onto
    // Titanium numbers Titanium does not have, and inside a container it is
    // only right for the first ten of the server's two hundred slots. Rather
    // than silently pass one of those through as some other slot, require the
    // number to survive both directions. This is also what makes the bag half
    // of the table safe: 4010 round-trips, 4025 and 5610 do not.
    if (clientSlotToServer(clientSlot) != serverSlot) return UserSlotResult::NotAddressable;
    out = clientSlot;
    return UserSlotResult::Ok;
}

/** The refusal text for UserSlotResult::NotAddressable, in one place. */
inline std::string describeUnaddressableSlot(const std::string& tok) {
    return "Slot " + tok +
           " is not a slot this client can reach -- the server does not send it to a "
           "Titanium client. Slot numbers are inventory.slot_id; try /equip list.";
}

// Get size name
inline const char* getSizeName(uint8_t size) {
    switch (size) {
        case SIZE_TINY: return "TINY";
        case SIZE_SMALL: return "SMALL";
        case SIZE_MEDIUM: return "MEDIUM";
        case SIZE_LARGE: return "LARGE";
        case SIZE_GIANT: return "GIANT";
        case SIZE_GIGANTIC: return "GIGANTIC";
        default: return "UNKNOWN";
    }
}

// Class abbreviations for display
inline const char* getClassAbbrev(int classId) {
    switch (classId) {
        case 0: return "WAR";
        case 1: return "CLR";
        case 2: return "PAL";
        case 3: return "RNG";
        case 4: return "SHD";
        case 5: return "DRU";
        case 6: return "MNK";
        case 7: return "BRD";
        case 8: return "ROG";
        case 9: return "SHM";
        case 10: return "NEC";
        case 11: return "WIZ";
        case 12: return "MAG";
        case 13: return "ENC";
        case 14: return "BST";
        case 15: return "BER";
        default: return "???";
    }
}

// Build class restriction string from bitmask
std::string buildClassString(uint32_t classMask);

// Build race restriction string from bitmask
std::string buildRaceString(uint32_t raceMask);

// Build equippable slots string from bitmask
std::string buildSlotsString(uint32_t slotMask);

} // namespace inventory
} // namespace eqt
