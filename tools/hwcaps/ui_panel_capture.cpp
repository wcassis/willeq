// ui_panel_capture -- T416 (board B316): every static UI panel, drawn through
// a REAL UIAtlas, one PNG per panel.
//
// Why this exists. T411 made the three alpha-BLEND shaders sample the ETC1
// `_alpha` companion, so `UI2D_FS` stopped reading `texture2D(uTexture, uv).a
// == 1.0` for the atlas and started honouring the atlas's real alpha --
// 77.1% of its texels are not opaque and 64.0% are fully transparent. Before
// that every sprite, bar, border and coin decal the static UI drew was
// painted as a filled rectangle. No visual-gate golden photographs any UI
// (the gate's three zones capture geometry only), and the sibling tool
// `ui_inventory_capture` only ever renders the inventory popup plus one
// combined atlas frame, so most of the UI has never been looked at on the
// path the client actually uses.
//
// What it draws. Each panel alone, on a CHECKERBOARD background, at 800x600:
// the checker is what makes a now-transparent region legible as transparency
// rather than as "dark". It never self-assesses -- it writes pixels and says
// what they are.
//
// Usage:
//   ui_panel_capture <output_dir> [--eqe1-path <dir-or-file>] [--opaque-alpha]
//
//   --eqe1-path   directory holding willeq.eqe1, OR the pack file itself
//                 (so a reference pack such as
//                 review/T411/willeq.eqe1.pre-T411 can be named directly).
//   --opaque-alpha  clears the atlas texture's ETC1 alpha companion after
//                 load, which makes UI2D_FS read alpha 1.0 for an ETC1
//                 entry -- exactly the pre-T411 behaviour. Only meaningful
//                 against a pack whose `ui_atlas` is ETC1 (the pre-T411
//                 one); the post-T411 pack carries `ui_atlas` alpha IN BAND,
//                 where unit 0 supplies it and unbinding unit 1 changes
//                 nothing. Pairing this flag with the pre-T411 pack is the
//                 "before" half of T416's before/after.

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/options_panel.h"
// T436 (board B429).
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/trainer_panel.h"  // B898
#include "client/graphics/ui/quantity_window.h"   // B442
#include "client/graphics/ui/inventory_layout.h"  // B442
// B895 (GitLab #211).
#include "client/graphics/ui/tracking_panel.h"
#include "client/tracking/tracking_state.h"
#include "client/con_tier.h"
#include "client/display_eqstr.h"
#include "client/inventory/item_display_text.h"
#include "client/spell/spell_display_text.h"
#include "client/spell/spell_data.h"
#include "client/inventory/item_instance.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/text_batch.h"
#include "client/chat/chat_message_buffer.h"
#include "client/data_paths.h"
#include "client/string_database.h"
#include "client/willeq_database.h"

#include <irrlicht.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <sys/stat.h>

using namespace EQT::Graphics;

// Defined in the patched GLES2 driver (docker/irrlicht-drm): records the
// ETC1 alpha companion on a texture. Passing nullptr clears the pairing.
extern void gles2SetAlphaCompanion(void* mainTexture, void* companionTexture);

namespace {

// A 16px checkerboard, drawn IMMEDIATELY (before any UIRenderer quad is
// flushed) so every UI pixel with alpha < 255 shows it through.
void drawChecker(irr::video::IVideoDriver* driver, int w, int h) {
    const irr::video::SColor a(255, 96, 96, 104);
    const irr::video::SColor b(255, 56, 56, 64);
    constexpr int kCell = 16;
    for (int y = 0; y < h; y += kCell) {
        for (int x = 0; x < w; x += kCell) {
            const bool even = ((x / kCell) + (y / kCell)) % 2 == 0;
            driver->draw2DRectangle(even ? a : b,
                irr::core::rect<irr::s32>(x, y, x + kCell, y + kCell));
        }
    }
}

// T429 (board B390): the display composers fetch their formats from the
// shipped eqstr table, so this tool has to open `data/willeq.db` the way the
// client does. If the db is not there the composers draw nothing and the two
// display captures come out empty -- which is visible, not silent.
EQT::StringDatabase& eqStrings() {
    static EQT::WilleqDatabase db;
    static EQT::StringDatabase strings;
    static bool once = [] {
        if (db.open(eqt::ResolveDataPath("data/willeq.db"))) {
            EQT::WilleqDatabase::setGlobalInstance(&db);
            strings.initFromDatabase();
        } else {
            std::fprintf(stderr, "ui_panel_capture: could not open data/willeq.db -- "
                                 "the item/spell display captures will be empty\n");
        }
        return true;
    }();
    (void)once;
    return strings;
}

void addChat(eqt::ui::ChatMessageBuffer& buf, const char* text, uint32_t color) {
    eqt::ui::ChatMessage msg;
    msg.text = text;
    msg.color = color;
    msg.channel = eqt::ui::ChatChannel::Say;
    buf.addMessage(std::move(msg));
}

// ---- panel states, populated once and shared by every capture -------------

struct PanelStates {
    PlayerStatsData stats;
    TargetInfoData target;
    eqt::ui::ChatMessageBuffer chatBuffer;
    ChatPanelState chat;
    InventoryPanelState inventory;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    BuffBarState buffs;
    CastingBarState casting;
    GroupPanelState group;
    PetPanelState pet;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    LootPanelState loot;
    TradeskillPanelState tradeskill;   // T419 (boards B335/B321)
    ItemDisplayPanelState itemDisplay; // T421 (board B298, item half)
    // T425 (board B298, spell half): the SAME window shape holding a SPELL.
    // The oracle has no second display window, so this is a second STATE,
    // not a second panel.
    ItemDisplayPanelState spellDisplay;
    // T434: the Options window. Captured once PER TAB -- the tabs are
    // what a reviewer has to be able to see, and only one page draws.
    OptionsPanelState options;
    // T436 (board B429): the merchant window, the bank window and the item
    // held on the cursor. Real dragitem icon ids, T430's lesson.
    MerchantPanelState merchant;
    BankPanelState bank;
    CursorAttachmentState cursor;
    TrainerPanelState trainer;  // B898 (#214)
    // B895: the Tracking Window, with con-coloured rows through the real
    // list rules and combobox text from the shipped eqstr table.
    TrackingPanelState tracking;
    XPBarState xp;

    PanelStates() {
        stats.name = "Testchar";
        stats.level = 12;
        stats.curHP = 137; stats.maxHP = 220;
        stats.curMana = 90; stats.maxMana = 180;
        stats.curEndurance = 74; stats.maxEndurance = 100;

        target.spawnId = 42;
        target.name = "a large rat";
        target.level = 9;
        target.hpPercent = 63;

        addChat(chatBuffer, "Welcome to EverQuest!", 0xFFFFFF00u);
        addChat(chatBuffer, "You say, 'Hail, a large rat'", 0xFFC8C8C8u);
        addChat(chatBuffer, "a large rat bites YOU for 7 points of damage.", 0xFFFF6464u);
        addChat(chatBuffer, "You have gained experience!", 0xFF78DCFFu);
        chatBuffer.processPending();
        chat.messageBuffer = &chatBuffer;
        chat.inputActive = true;          // so the chat panel BACKGROUND draws
        chat.inputText = "/say hail";
        chat.cursorPos = 9;

        inventory.activePopup = PopupType::Inventory;
        for (int i = 0; i < InventoryPanelState::EQUIP_SLOTS; ++i) {
            inventory.equipSlots[i].hasItem = (i % 3) != 0;
            inventory.equipSlots[i].itemName = "Item";
            // T430 (board B376): real dragitem icon ids, so the capture shows
            // the art rather than the three-letter placeholder it replaced.
            inventory.equipSlots[i].iconId = 500 + static_cast<uint32_t>(i) * 7;
        }
        for (int i = 0; i < InventoryPanelState::GENERAL_SLOTS; ++i) {
            inventory.generalSlots[i].hasItem = true;
            inventory.generalSlots[i].itemName = "Bag";
            inventory.generalSlots[i].iconId = 640 + static_cast<uint32_t>(i);
            inventory.generalSlots[i].quantity = i + 1;
        }
        inventory.hoveredSlot = 4;

        // B895: a mixed-con list as a level-12 Ranger sees it, through the
        // real VisibleRows() and the shared con table.
        {
            using namespace eqt::tracking;
            struct S { const char* name; int level; float dist; bool pc; };
            static const S kSpawns[] = {
                {"Guard Hezlan", 30, 120.f, false}, {"a gnoll pup", 2, 40.f, false},
                {"a large rat", 7, 75.f, false},    {"a black wolf", 11, 90.f, false},
                {"Fippy Darkpaw", 14, 210.f, false}, {"a decaying skeleton", 1, 33.f, false},
                {"Oraclerogue", 10, 15.f, true},     {"a gnoll", 13, 160.f, false},
                {"an orc pawn", 16, 280.f, false},   {"a moss snake", 9, 55.f, false},
            };
            std::vector<TrackEntry> entries;
            uint32_t order = 0;
            for (const auto& sp : kSpawns) {
                TrackEntry e;
                e.spawnId = static_cast<uint16_t>(400 + order);
                e.name = sp.name; e.level = static_cast<uint8_t>(sp.level);
                e.distance = sp.dist; e.isPlayer = sp.pc; e.serverOrder = order++;
                e.con = eqt::ConTierForLevels(12, sp.level);
                entries.push_back(e);
            }
            // One filter unchecked, and the rows computed WITH it, so the
            // picture is self-consistent: no yellow NPC row while Y is off.
            TrackingSettings settings;
            settings.filters[static_cast<size_t>(TrackFilter::Yellow)] = false;
            for (const auto& e : VisibleRows(entries, settings)) {
                eqt::state::TrackingRowData r;
                r.spawnId = e.spawnId; r.name = e.name; r.isPlayer = e.isPlayer;
                const auto c = eqt::ConTierRgb(e.con);
                r.r = c.r; r.g = c.g; r.b = c.b;
                tracking.rows.push_back(r);
            }
            for (int i = 0; i < kSortCount; ++i) {
                tracking.sortOptions.push_back(eqt::EqStrLine(
                    eqStrings(), SortOptionStringId(static_cast<TrackSort>(i))));
            }
            for (int i = 0; i < kPlayersCount; ++i) {
                tracking.playersOptions.push_back(eqt::EqStrLine(
                    eqStrings(), PlayersOptionStringId(static_cast<TrackPlayers>(i))));
            }
            tracking.filters = settings.filters;
            tracking.isOpen = true;
            tracking.selectedSpawnId = 403;
            tracking.hoveredRow = 1;
        }

        // T436 (board B429): a merchant with real stock, a bank with real
        // contents, and an item on the cursor. The icon ids are real
        // dragitem ids so the captures show the art rather than a fallback.
        {
            eqt::state::WindowOpenedData mo;
            mo.npcId = 77; mo.npcName = "Bob the Merchant"; mo.sellRate = 1.0f;
            applyVendorWindowOpened(merchant, mo);
            static const char* kNames[] = {
                "Rusty Short Sword", "Cloth Cap", "Water Flask",
                "Bandages", "Small Lantern", "Iron Ration",
                "Leather Gloves", "Fishing Pole", "Cracked Staff",
                "Bronze Dagger", "Tattered Cloak", "Wooden Shield"};
            for (uint16_t i = 0; i < 12; ++i) {
                eqt::state::VendorItemAddedData v;
                v.vendorSlot = i;
                v.itemId = 1000u + i;
                v.itemName = kNames[i];
                v.price = 7 + static_cast<int32_t>(i) * 431;
                v.quantity = (i % 3 == 0) ? 20 : 1;
                v.iconId = 512u + i * 3u;
                applyVendorItemAdded(merchant, v);
            }
            merchant.selectedRow = 2;
            merchant.hoveredRow = 5;

            eqt::state::WindowOpenedData bo;
            bo.npcId = 55; bo.npcName = "Banker Bruce"; bo.sellRate = 0.0f;
            applyBankWindowOpened(bank, bo);
            for (int i = 0; i < BankPanelState::SLOT_COUNT; ++i) {
                eqt::state::InventorySlotChangedData d;
                d.slotId = static_cast<int16_t>(2000 + i);
                d.hasItem = (i != 5);
                d.itemId = 2000u + static_cast<uint32_t>(i);
                d.itemName = "Backpack";
                d.iconId = 700u + static_cast<uint32_t>(i) * 5u;
                d.quantity = static_cast<uint32_t>(i) + 1u;
                applyBankSlotChanged(bank, d);
            }
            bank.hoveredSlot = 1;
            eqt::state::BankCurrencyChangedData bc;
            bc.platinum = 1420; bc.gold = 6; bc.silver = 3; bc.copper = 91;
            applyBankCurrencyChanged(bank, bc);

            eqt::state::InventorySlotChangedData cd;
            cd.slotId = 30;   // eqt::inventory::CURSOR_SLOT
            cd.hasItem = true;
            cd.itemId = 4242;
            cd.itemName = "Bread Cake";
            cd.iconId = 918;
            cd.quantity = 4;
            applyCursorSlotChanged(cursor, cd);
        }
        inventory.coins[0] = 12; inventory.coins[1] = 5;
        inventory.coins[2] = 8;  inventory.coins[3] = 47;

        for (int i = 0; i < HotbarPanelState::SLOT_COUNT; ++i) {
            hotbar.slots[i].type = eqt::HotbarSlotType::Spell;
            hotbar.slots[i].id = 100 + i;
            hotbar.slots[i].name = "Spel";
        }
        hotbar.hoveredSlot = 3;

        gems.gems[0].spellId = 278; gems.gems[0].gemState = 1; gems.gems[0].spellName = "Frost Bolt";
        gems.gems[1].spellId = 279; gems.gems[1].gemState = 1; gems.gems[1].spellName = "Frost Rift";
        gems.gems[2].spellId = 280; gems.gems[2].gemState = 3; gems.gems[2].spellName = "Shock";
        gems.memorizeTargetGem = 5;

        buffs.isOpen = true;
        for (int i = 0; i < 5; ++i) {
            buffs.buffs[i].spellId = 500 + i;
            buffs.buffs[i].spellName = "Buff";
            buffs.buffs[i].ticksLeft = 60 - i * 7;
            buffs.buffs[i].updateTime = std::chrono::steady_clock::now();
        }

        casting.isCasting = true;
        casting.spellName = "Minor Healing";
        casting.totalMs = 2000;
        casting.remainingMs = 900;

        group.isOpen = true;
        group.inGroup = true;
        group.isLeader = true;
        group.leaderName = "Testchar";
        group.memberCount = 3;
        const char* names[3] = {"Aldric", "Brynn", "Cadoc"};
        const uint8_t hp[3] = {100, 71, 34};
        for (int i = 0; i < 3; ++i) {
            group.members[i].name = names[i];
            group.members[i].hpPercent = hp[i];
            group.members[i].manaPercent = 80;
            group.members[i].inZone = (i != 2);
        }

        pet.isOpen = true;
        pet.hasPet = true;
        pet.name = "Gynok";
        pet.level = 10;
        pet.hpPercent = 88;
        pet.buttonStates[0] = true;
        pet.buttonStates[3] = true;

        spellbook.isOpen = true;
        for (int i = 0; i < 12; ++i) {
            SpellbookDisplayEntry e;
            e.slot = static_cast<uint16_t>(i);
            e.spellId = 300 + i;
            e.level = static_cast<uint8_t>(1 + i);
            e.name = "Spell Of Testing";
            spellbook.spells.push_back(e);
        }
        spellbook.hoveredSlot = 2;
        spellbook.isMemorizing = true;
        spellbook.memorizeGemSlot = 3;
        spellbook.memorizeProgressMs = 4000;
        spellbook.memorizeTotalMs = 10000;

        skills.isOpen = true;
        const char* skillNames[10] = {"1H Slashing", "Offense", "Defense", "Dodge",
                                      "Parry", "Swimming", "Sense Heading", "Meditate",
                                      "Bind Wound", "Alcohol Tolerance"};
        for (int i = 0; i < 10; ++i) {
            SkillDisplayItem s;
            s.skillId = static_cast<uint8_t>(i);
            s.name = skillNames[i];
            s.value = 20 + i * 13;
            s.maxValue = 200;
            skills.skills.push_back(s);
        }
        skills.hoveredRow = 3;

        loot.isOpen = true;
        loot.corpseId = 7;
        loot.corpseName = "a large rat's corpse";
        const char* lootNames[4] = {"Rat Ears", "Rusty Dagger", "Bone Chips", "Small Bag"};
        for (int i = 0; i < 4; ++i) {
            LootItemDisplay it;
            it.slot = static_cast<uint8_t>(i * 3);
            it.itemId = 1000 + i;
            it.itemName = lootNames[i];
            it.iconId = 500 + static_cast<uint32_t>(i) * 11;  // T430
            loot.items.push_back(it);
        }
        loot.hoveredRow = 1;

        // T419 (board B335): the tradeskill window WITH its recipe list, so
        // the capture shows the left side B335 said was missing.
        eqt::state::TradeskillContainerOpenedEvent forge;
        forge.isWorldObject = true;
        forge.objectId = 4242;
        forge.inventorySlot = -1;
        forge.containerName = "Forge";
        forge.containerType = 15;
        forge.slotCount = 10;
        applyTradeskillContainerOpened(tradeskill, forge);
        for (int i = 0; i < 4; ++i) {
            tradeskill.slots[i].hasItem = true;
            tradeskill.slots[i].itemName = "Ore";
            tradeskill.slots[i].iconId = 700 + static_cast<uint32_t>(i);  // T430
            tradeskill.slots[i].quantity = i + 1;
        }
        eqt::state::TradeskillRecipeListClearedData search;
        search.objectType = 15;
        search.query = "bracer";
        search.minTrivial = 1;
        search.maxTrivial = 200;
        search.isFavorites = false;
        applyTradeskillRecipeListCleared(tradeskill, search);
        const char* recipeNames[9] = {
            "Banded Bracer", "Bronze Bracer", "Cured Silk Bracer",
            "Fine Steel Bracer", "Iron Bracer", "Leather Bracer",
            "Rawhide Bracer", "Small Bronze Bracer", "Split Paw Hide Bracer"};
        const uint32_t trivials[9] = {122, 68, 90, 135, 44, 21, 33, 68, 51};
        for (int i = 0; i < 9; ++i) {
            eqt::state::TradeskillRecipeReceivedData r;
            r.objectType = 15;
            r.someId = 0;
            r.componentCount = 3;
            r.recipeId = 2000 + i;
            r.trivial = trivials[i];
            r.name = recipeNames[i];
            applyTradeskillRecipeReceived(tradeskill, r);
        }
        tradeskill.selectedRecipe = 2;
        tradeskill.hoveredRecipe = 4;
        // T421 (board B365): COMBW_SearchTextEdit has the caret, so the
        // capture shows the focused-field frame and the block cursor.
        tradeskill.focusedField = TradeskillField::SearchText;
        tradeskill.searchEntry.inputActive = true;

        // T421 (board B298, item half): the item display window, filled by
        // the REAL composer over a REAL ItemInstance -- no hand-typed lines.
        {
            eqt::inventory::ItemInstance it;
            it.itemId = 4231;
            it.name = "Bladed Thulian Claws";
            it.icon = 640;
            it.magic = true;
            it.loreItem = true;
            it.noDrop = true;
            it.slots = eqt::inventory::EQUIPSLOT_PRIMARY;
            it.weight = 4.5f;
            it.size = eqt::inventory::SIZE_MEDIUM;
            it.ac = 12;  it.damage = 19;  it.delay = 21;  it.skillType = 1;
            it.str = 5;  it.sta = 4;  it.dex = 2;  it.agi = 3;
            it.hp = 35;  it.mana = 40;
            it.fireResist = 8;  it.coldResist = 9;  it.magicResist = 10;
            it.attack = 14;  it.haste = 16;  it.accuracy = 17;
            it.damageShield = 18;  it.dotShield = 19;  it.spellShield = 20;
            it.strikethrough = 21;  it.stunResist = 22;  it.shielding = 23;
            it.hpRegen = 25;  it.manaRegen = 26;  it.enduranceRegen = 24;
            it.recLevel = 45;  it.reqLevel = 40;
            it.maxCharges = 3;  it.charges = 2;
            it.clickEffect.effectId = 300;
            it.clickEffect.name = "Shield of Words";
            it.clickEffect.castTime = 2500;
            it.lore = "This weapon hums faintly.";
            eqt::state::ItemDisplayShownData d;
            d.itemId = it.itemId;
            d.name = it.name;
            d.iconId = it.icon;
            d.lines = eqt::inventory::BuildItemDisplayText(eqStrings(), it);
            applyItemDisplayShown(itemDisplay, d);
        }

        // T425 (board B298, spell half): a spell in that same window.
        {
            EQ::SpellData sp;
            sp.id = 15;
            sp.name = "Word of Health";
            sp.mana_cost = 200;
            sp.cast_time_ms = 4500;
            sp.recast_time_ms = 9000;
            sp.duration_formula = static_cast<uint8_t>(EQ::DurationFormula::Fixed50);
            sp.range = 100.0f;
            sp.casting_skill = EQ::CastingSkill::Alteration;
            sp.spell_icon = 331;
            for (auto& lvl : sp.class_levels) lvl = 255;
            sp.class_levels[1] = 44;  // Cleric
            eqt::state::ItemDisplayShownData d;
            d.itemId = sp.id;
            d.name = sp.name;
            d.iconId = sp.spell_icon;
            d.lines = eqt::spell::BuildSpellDisplayText(eqStrings(), sp, 2, 50);
            applyItemDisplayShown(spellDisplay, d);
        }

        xp.progress = 0.62f;

        // B898 (#214): a warrior guildmaster's reply, with more rows than the
        // list shows so the scroll bar's thumb is visible. Costs are eqemu's
        // (cur-10)^3/100, computed the way the game thread computes them.
        {
            eqt::state::TrainerWindowOpenedData t;
            t.npcId = 90; t.npcName = "Guildmaster Rough";
            t.platinum = 12; t.gold = 3; t.silver = 4; t.copper = 5;
            t.practicePoints = 5;
            static const struct { uint8_t id; const char* name; uint32_t cur; } kSkills[] = {
                {0, "1H Blunt", 22}, {1, "1H Slashing", 51}, {2, "2H Blunt", 0},
                {3, "2H Slashing", 14}, {7, "Archery", 3}, {10, "Bash", 30},
                {15, "Defense", 48}, {19, "Disarm", 0}, {21, "Dodge", 35},
                {22, "Double Attack", 0}, {33, "Offense", 55}, {34, "Parry", 12},
                {36, "Piercing", 9}, {50, "Swimming", 40}};
            for (const auto& s : kSkills) {
                const uint32_t adj = s.cur > 10 ? s.cur - 10 : 0;
                t.skills.push_back({s.id, s.name, s.cur, 200, adj * adj * adj / 100});
            }
            applyTrainerWindowOpened(trainer, t);
            trainer.selectedRow = 1;
            trainer.hoveredRow = 5;
        }
    }
};

struct PanelCapture {
    const char* name;
    std::function<void(UIRenderer&, const UILayout&, PanelStates&)> draw;
};

std::vector<PanelCapture> buildCaptureList() {
    return {
        {"player_status", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPlayerStatus(u, l, s.stats); }},
        {"target_info", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTargetInfo(u, l, s.target); }},
        {"chat", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderChatPanel(u, l, s.chat); }},
        {"hotbar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderHotbar(u, l, s.hotbar); }},
        {"spell_gems", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSpellGemPanel(u, l, s.gems); }},
        {"buff_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderBuffBar(u, l, s.buffs); }},
        {"casting_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderCastingBar(u, l, s.casting); }},
        {"group", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderGroupPanel(u, l, s.group); }},
        {"group_nogroup", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            GroupPanelState g = s.group; g.inGroup = false;
            renderGroupPanel(u, l, g); }},
        {"pet", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPetPanel(u, l, s.pet); }},
        {"xp_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderXPBar(u, l, s.xp); }},
        {"inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u, l, s.inventory); }},
        {"spellbook", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSpellbookPopup(u, l, s.spellbook); }},
        {"skills", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSkillsPopup(u, l, s.skills); }},
        {"loot", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderLootPopup(u, l, s.loot); }},
        // T419 (board B335): the recipe list on the window's left side.
        {"tradeskill", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTradeskillPopup(u, l, s.tradeskill); }},
        // T421 (board B298, item half): the item display window.
        {"item_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderItemDisplayPopup(u, l, s.itemDisplay); }},
        // T425 (board B298, spell half): the same window with a SPELL in it.
        {"spell_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderItemDisplayPopup(u, l, s.spellDisplay); }},
        // T434: one capture per tab of the Options window. `options.isOpen`
        // is set here rather than in the state block so the seven lambdas
        // differ ONLY in activeTab.
        {"options_general", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 0;
            renderOptionsWindow(u, l, s.options); }},
        {"options_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 1;
            renderOptionsWindow(u, l, s.options); }},
        {"options_mouse", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 2;
            renderOptionsWindow(u, l, s.options); }},
        {"options_keys", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 3;
            renderOptionsWindow(u, l, s.options); }},
        {"options_filters", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 4;
            renderOptionsWindow(u, l, s.options); }},
        {"options_colors", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 5;
            renderOptionsWindow(u, l, s.options); }},
        {"options_mail", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 6;
            renderOptionsWindow(u, l, s.options); }},
        // T436 (board B429): the three windows B429 said did not exist.
        // `merchant_sell` is the SAME window with an item on the cursor --
        // the oracle gives MW_Buy_Button and MW_Sell_Button one rect, so
        // only one can show, and that is the state that picks Sell.
        {"merchant", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            CursorAttachmentState empty;
            renderMerchantPopup(u, l, s.merchant, empty); }},
        {"merchant_sell", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderMerchantPopup(u, l, s.merchant, s.cursor); }},
        {"bank", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderBankWindow(u, l, s.bank); }},
        // B898 (#214): the skill trainer window, and the same window scrolled.
        {"trainer", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.trainer.scrollOffset = 0;
            renderTrainerWindow(u, l, s.trainer, s.xp.progress); }},
        {"trainer_scrolled", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.trainer.scrollOffset = s.trainer.maxScrollOffset();
            renderTrainerWindow(u, l, s.trainer, s.xp.progress); }},
        // B895: the Tracking Window, and the same window with its sort
        // combobox dropped down.
        {"tracking", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.tracking.openCombo = TrackingCombo::None;
            renderTrackingWindow(u, l, s.tracking); }},
        {"tracking_sort_open", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.tracking.openCombo = TrackingCombo::Sort;
            renderTrackingWindow(u, l, s.tracking); }},
        {"cursor_item", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            // Placed off-centre so the 50x50 box and its (5,5) inset are
            // both visible against the empty frame.
            s.cursor.mouseX = l.screenW / 2;
            s.cursor.mouseY = l.screenH / 2;
            renderCursorAttachment(u, l, s.cursor); }},
        // B442 (#151): click-to-cursor. `inventory_pickup` is the popup the
        // moment after a left click lifted general slot 3's item: that slot
        // is empty and the item rides the pointer, which sits over the empty
        // slot it is about to be put down in. `quantity` is the QuantityWnd a
        // plain click on a stack of 20 opens, slider at 7.
        {"inventory_pickup", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            InventoryPanelState inv = s.inventory;
            inv.generalSlots[2].hasItem = false;
            inv.hoveredSlot = 22 + 5;
            inv.generalSlots[5].hasItem = false;
            renderInventoryPopup(u, l, inv);
            CursorAttachmentState held;
            held.hasItem = true;
            held.itemName = s.inventory.generalSlots[2].itemName;
            held.iconId = s.inventory.generalSlots[2].iconId;
            const auto target = InventoryLayout::slotRect(l, 22 + 5);
            held.mouseX = target.UpperLeftCorner.X + 8;
            held.mouseY = target.UpperLeftCorner.Y + 8;
            renderCursorAttachment(u, l, held); }},
        {"quantity", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            QuantityWindowState q;
            q.openForStack(23, "Arrow", 20);
            q.value = 7;
            renderQuantityWindow(u, l, q); }},
        // Everything the HUD draws at once -- the screen a player sees.
        {"hud_all", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPlayerStatus(u, l, s.stats);
            renderTargetInfo(u, l, s.target);
            renderGroupPanel(u, l, s.group);
            renderPetPanel(u, l, s.pet);
            renderBuffBar(u, l, s.buffs);
            renderChatPanel(u, l, s.chat);
            renderCastingBar(u, l, s.casting);
            renderHotbar(u, l, s.hotbar);
            renderSpellGemPanel(u, l, s.gems);
            renderXPBar(u, l, s.xp); }},
    };
}

std::string resolvePackPath(const std::string& eqe1Path) {
    struct stat st;
    if (stat(eqe1Path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return eqe1Path;
    return eqe1Path + "/willeq.eqe1";
}

} // namespace

int main(int argc, char** argv) {
    std::string outputDir = "ui_panel_capture";
    std::string eqe1Path = "/mnt/projects/EverQuest_ETC1";
    bool opaqueAlpha = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eqe1-path") && i + 1 < argc) {
            eqe1Path = argv[++i];
        } else if (!strcmp(argv[i], "--opaque-alpha")) {
            opaqueAlpha = true;
        } else if (argv[i][0] != '-') {
            outputDir = argv[i];
        } else {
            fprintf(stderr, "ERROR: unknown argument '%s'\n", argv[i]);
            return 1;
        }
    }
    mkdir(outputDir.c_str(), 0755);

    const std::string packPath = resolvePackPath(eqe1Path);
    {
        FILE* probe = fopen(packPath.c_str(), "rb");
        if (!probe) {
            fprintf(stderr, "ERROR: %s not readable\n", packPath.c_str());
            return 1;
        }
        fclose(probe);
    }

    constexpr irr::u32 kW = 800, kH = 600;
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
    params.Fullscreen = true;
    eqt::ConfigureGles2ShaderCache();  // B959: cache dir via ResolveCachePathForWrite
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: failed to create Irrlicht device\n");
        return 1;
    }
    irr::video::IVideoDriver* driver = device->getVideoDriver();

    bool ok = true;
    {
        EQE1Loader loader;
        if (!loader.open(packPath)) {
            fprintf(stderr, "ERROR: failed to open %s\n", packPath.c_str());
            device->drop();
            return 1;
        }

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        ConstrainedTextureCache textureCache(config);
        textureCache.setDriver(driver);
        textureCache.addEQE1Loader(&loader);

        UIAtlas atlas;
        if (!atlas.load(driver, &textureCache)) {
            fprintf(stderr, "ERROR: UIAtlas::load() failed against %s\n", packPath.c_str());
            device->drop();
            return 1;
        }
        if (opaqueAlpha) {
            gles2SetAlphaCompanion(atlas.getTexture(), nullptr);
            printf("--opaque-alpha: cleared the ui_atlas ETC1 alpha companion "
                   "(pre-T411 emulation; a no-op if this pack's ui_atlas is RGBA8888)\n");
        }

        TextBatch textBatch;
        textBatch.init(device->getGUIEnvironment());

        UIRenderer ui;
        ui.init(driver, &atlas, &textBatch);

        UILayout layout;
        layout.computeLayout(static_cast<irr::s32>(kW), static_cast<irr::s32>(kH));

        PanelStates states;
        const auto captures = buildCaptureList();

        // Prime the driver, then a warm-up content frame: the surfaceless-EGL
        // /llvmpipe backend drops immediate-mode quads on the first content
        // frame after the prime (documented in ui_inventory_capture.cpp).
        driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver->endScene();

        for (const auto& cap : captures) {
            for (int pass = 0; pass < 2; ++pass) {
                driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
                drawChecker(driver, static_cast<int>(kW), static_cast<int>(kH));
                ui.beginFrame();
                cap.draw(ui, layout, states);
                ui.endFrame();
                driver->endScene();
            }

            irr::video::IImage* img = driver->createScreenShot();
            if (!img) {
                fprintf(stderr, "ERROR: createScreenShot() returned null for %s\n", cap.name);
                ok = false;
                continue;
            }
            const std::string path = outputDir + "/" + cap.name + "_800x600.png";
            const bool wrote = driver->writeImageToFile(img, path.c_str());
            img->drop();
            if (!wrote) {
                fprintf(stderr, "ERROR: writeImageToFile failed for %s\n", path.c_str());
                ok = false;
                continue;
            }
            printf("Wrote %s\n", path.c_str());
        }
    }

    device->drop();
    return ok ? 0 : 1;
}
