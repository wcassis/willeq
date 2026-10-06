#pragma once

// Event payloads: inventory, audio volumes, loot and trade.
//
// T451: split out of the 1,269-line include/client/state/event_bus.h.
// Every declaration is a contiguous line range of that file, moved verbatim;
// "client/state/event_bus.h" still includes all of them, so no include site
// changed.

#include <functional>
#include <vector>
#include <variant>
#include <string>
#include <cstdint>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

#include "client/game_options.h"  // B430: GameOptionChangedData
#include "client/hotbar/hotbar_slot_type.h"  // T156: HotbarSlotAssignedData carries the enum, not a raw uint8_t
#include "client/spell/spell_visual_vocabulary.h"  // T381: SpellCastVisual*Data carry the render vocabulary by value, not a SpellDatabase*

class HCMap;
class PathfinderNavmesh;

#include "client/state/event_types.h"

namespace eqt {
namespace state {

// --- Inventory events ---

// T421 (board B298, item half). The item display window's contents.
//
// The lines are COMPOSED GAME-SIDE, by BuildItemDisplayText()
// (include/client/inventory/item_display_text.h), rather than shipped as
// fifty numbered fields for the renderer to format. That is what the
// official client does too: EQUI_ItemDisplay.xml's whole body is one
// <STMLbox item="IDW_ItemDescription">, a free-text box the client fills,
// and the stat FORMATS live in the shipped string table eqstr_us.txt. A
// renderer-side formatter would put the string ids on the wrong side of the
// bridge and split the item's knowledge across two threads.
struct ItemDisplayShownData {
    uint32_t itemId = 0;
    std::string name;          // -> the Style_Titlebar row
    uint32_t iconId = 0;       // -> IDW_IconButton's 40x40 decal
    std::vector<std::string> lines;  // -> IDW_ItemDescription
};

struct InventorySlotChangedData {
    int16_t slotId;
    bool hasItem;
    uint32_t itemId;  // 0 if no item
    // S2: display fields carried in the event so the render-thread bridge no
    // longer reads the game-thread InventoryManager through a shared pointer
    std::string itemName;
    uint32_t iconId = 0;
    uint32_t quantity = 0;
    // B442b (#151 rows 19-21): the item's bag size (ItemInstance::bagSlots),
    // 0 for anything that is not a container -- so the render thread knows a
    // RIGHT click on this slot opens a bag window rather than the display.
    uint8_t bagSlots = 0;
};

// B442b (#151 row 29): the coin on the cursor. The official client draws the
// coin's decal with its count under it while it is held
// (review/UIGAP-ORACLE/151/run1.G33a_coin_quantity_set_3.png). `coinType` is
// EQT::COINTYPE_* (0 cp .. 3 pp); `amount` 0 means the cursor holds no coin.
struct CursorCoinChangedData {
    uint8_t coinType = 0;
    uint32_t amount = 0;
};

// B442b (#151 rows 31-35): a Yes/No prompt the game thread composed from
// eqstr. `purpose` is the asker's own token (eqt::inventory::ConfirmPurpose
// for the cursor gestures); the render thread echoes it back on Yes.
struct ConfirmationRequestedData {
    uint8_t purpose = 0;
    std::string text;
};

struct CursorItemChangedData {
    bool hasCursorItem;
    // T413 (board B296): `queueSize` lived here and no consumer ever read it.
    // It is not a rendering omission -- the official UI has no element for it:
    // EQUI_CursorAttachment.xml's whole screen is CA_Anim / CA_Anim2 /
    // CA_SpellGem, three 40x40 icon slots with no count label anywhere, and
    // the queue depth is simply never shown. The game-side depth stays on
    // InventoryState::cursorQueueSize() (which has a reader and a test); only
    // the renderer-facing copy is gone.
};

struct EquipmentStatsChangedData {
    int32_t ac;
    int32_t atk;
    int32_t hp;
    int32_t mana;
    float weight;
};

struct CurrencyChangedData {
    int32_t platinum;
    int32_t gold;
    int32_t silver;
    int32_t copper;
};

struct BankCurrencyChangedData {
    int32_t platinum;
    int32_t gold;
    int32_t silver;
    int32_t copper;
};

// --- Audio events ---

// T439 (board B432): the volume the audio manager ACTUALLY holds, expressed
// on the oracle's 0..20 step (eqt::audio::kVolumeSteps). Absolute, never a
// delta -- that was the whole defect: nothing published the result of a
// volume change back, so the renderer had to shadow what it last sent and a
// /musicvolume behind its back left the slider stale.
struct VolumesChangedData {
    int musicStep;   // 0..20
    int soundStep;   // 0..20
};

// B430: a server-owned Options-window setting the game thread now holds
// (eqt::GameOption, include/client/game_options.h) -- published from the
// player-profile read at zone-in and from every site that changes one
// (/anon, /roleplay, the Options window's own GameOptionIntent), so the
// window's checkboxes show what the SERVER was told, never a local guess.
struct GameOptionChangedData {
    eqt::GameOption option;
    int32_t value;
};

// --- Loot events ---

struct LootWindowOpenedData {
    uint16_t corpseId;
    std::string corpseName;
};

struct LootWindowClosedData {
    uint16_t corpseId;
};

struct LootItemAddedData {
    uint16_t corpseId;
    uint8_t slot;
    uint32_t itemId;
    std::string itemName;
    // T430 (board B376): the item's icon id, so the loot window can draw the
    // dragitem art the oracle draws in its LW_LootSlot cells. The producer
    // (ZoneProcessLootItem's field[22]) already PARSED this and threw it
    // away -- it logged the value and never put it on the event.
    uint32_t iconId = 0;
};

struct LootItemRemovedData {
    uint16_t corpseId;
    uint8_t slot;
};

// --- Trade events ---

struct TradeRequestReceivedData {
    uint32_t spawnId;
    std::string name;
};

struct TradeStartedData {
    uint16_t partnerId;
    std::string partnerName;
    bool isNpc;
};

struct TradeItemUpdatedData {
    uint8_t who;  // 0=self, 1=partner
    uint8_t slot;
    uint32_t itemId;
    std::string itemName;
};

struct TradeMoneyUpdatedData {
    uint8_t who;  // 0=self, 1=partner
    uint32_t platinum;
    uint32_t gold;
    uint32_t silver;
    uint32_t copper;
};

struct TradeAcceptStateChangedData {
    bool ownAccepted;
    bool partnerAccepted;
};

struct TradeCancelledData {};

struct TradeCompletedData {};

} // namespace state
} // namespace eqt
