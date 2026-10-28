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
//                    [--size WxH] [--panel <capture-name>]
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
//   --size          output resolution (default 800x600).
//   --panel         capture one named panel (or a prefix ending in *) instead
//                   of the full set.

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/hotbar_panel.h"  // B1025-H2: renderHotbar's home
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
#include "client/graphics/ui/confirmation_dialog.h"  // B442b
#include "client/graphics/ui/inventory_layout.h"  // B442
// B895 (GitLab #211).
#include "client/graphics/ui/tracking_panel.h"
#include "client/tracking/tracking_state.h"
#include "client/con_tier.h"
#include "client/display_eqstr.h"
// B899 (#215 / #151 Leg B).
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/friends_panel.h"   // B897
#include "client/graphics/ui/trade_panel.h"     // B896
#include "client/graphics/ui/item_tooltip.h"    // B1004
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/ui/tip_window.h"      // B834
#include <fstream>
#include <sstream>
#include "client/graphics/ui/char_create_panel.h"   // B888
#include "client/login_world/char_create_data.h"     // B888
#include "client/graphics/ui/char_select_panel.h"   // B998
#include "client/login_world/character_select.h"    // B998
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

// B1004: one tooltip frame over the given windows, past the hover dwell, drawn
// as renderStaticUI() draws it -- its own layer on top.
irr::s32 captureTextWidth(const std::string& t, const void* ctx) {
    return ctx ? static_cast<const TextBatch*>(ctx)->getTextWidth(t) : 0;
}
void tooltipCapture(UIRenderer& u, const UILayout& l, const InventoryPanelState* inv,
                    const BankPanelState* bank, const ContainerWindowsState* bags,
                    int mx, int my, bool alt) {
    ItemTooltipScene scene;
    scene.layout = &l;
    scene.inventory = inv;
    scene.bank = bank;
    scene.containers = bags;
    scene.mouseX = mx;
    scene.mouseY = my;
    scene.altHeld = alt;
    const TextBatch* tb = u.getTextBatch();
    const irr::s32 lh = tb ? tb->getLineHeight() : 14;
    ItemTooltipState st;
    updateItemTooltips(st, scene, 0, &captureTextWidth, tb, lh);
    const ItemTooltipFrame f = updateItemTooltips(st, scene, ItemTooltip::kHoverDelayMs, &captureTextWidth, tb, lh);
    renderItemTooltips(u.layer(), f);
}

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

// B888: the creation screen's eqstr text and its era data.
EqstrResolver charCreateEqstr() {
    return [](uint32_t id) { return eqStrings().getString(id); };
}

// 0 = page 0 (Human male Warrior, AGI +25), 1 = page 1 (AGNOSTIC, QEYNOS,
// "Cinewbie"), 2 = page 0 with nothing spent and Next pressed (eqstr 1577).
CharCreateScreenState charCreateCaptureState(int which) {
    static eqt::charcreate::CharCreateData data = [] {
        eqt::charcreate::CharCreateData d;
        if (!d.loadShipped()) std::fprintf(stderr, "ui_panel_capture: %s\n", d.error().c_str());
        return d;
    }();
    CharCreateScreenState s;
    openCharCreateWith(s, data, eqt::charcreate::Race::Human, eqt::charcreate::Gender::Male,
                       eqt::charcreate::Class::Warrior);
    if (which == 2) {
        pressNext(s);
        return s;
    }
    statMax(s, eqt::charcreate::AGI);
    if (which == 1) {
        pressNext(s);
        s.nameEntry.inputText = "Cinewbie";
    }
    return s;
}

// B998 (#318): the Character Select screen. `which`: 0 = the recorded
// account (data/captures/session.sleq's three characters, Testchar
// selected), 1 = an empty account, 2 = the delete prompt over 0 for
// Testmelee, 3 = "Create a New Character" pressed (the hosted creation page).
// The rows and the eqstr table are composed by the same functions the game
// thread uses (login_world/character_select.h).
CharSelectScreenState charSelectCaptureState(int which) {
    static eqt::charcreate::CharCreateData data = [] {
        eqt::charcreate::CharCreateData d;
        d.loadShipped();
        return d;
    }();
    CharSelectScreenState s;
    auto table = std::make_shared<eqt::login_world::EqstrTable>();
    for (uint32_t id : eqt::login_world::CharacterSelectEqstrIds(data)) (*table)[id] = eqStrings().getString(id);
    eqt::login_world::CharacterSelectView view;
    view.strings = table;
    if (which != 1) {
        std::vector<CharacterSelectEntry> list = {
            {0, "Testcaster", 1, 12, 6, 25}, {1, "Testchar", 1, 1, 1, 36}, {2, "Testmelee", 1, 1, 6, 25}};
        view.rows = eqt::login_world::BuildCharacterSelectRows(list, eqt::login_world::LoadEraZoneLongNames());
    }
    applyCharacterSelectView(s, view);
    if (which == 0) s.selected = 1;
    if (which == 2) {
        s.selected = 2;
        pressDelete(s);
    }
    if (which == 3) pressCharButton(s, 3, data, 1);
    return s;
}

void addChat(eqt::ui::ChatMessageBuffer& buf, const char* text, uint32_t color) {
    eqt::ui::ChatMessage msg;
    msg.text = text;
    msg.color = color;
    msg.channel = eqt::ui::ChatChannel::Say;
    buf.addMessage(std::move(msg));
}

// B993: three chat lines on the channels whose [TextColors] rows differ most,
// drawn through `table` (a fresh panel each call, so nothing is cached).
void renderTextColorChat(UIRenderer& u, const UILayout& l, const eqt::ui::ChatTextColors& table) {
    eqt::ui::ChatMessageBuffer buf;
    const struct { eqt::ui::ChatChannel ch; const char* sender; const char* text; } kLines[] = {
        {eqt::ui::ChatChannel::Tell, "Oracle", "the Tell row, User_2"},
        {eqt::ui::ChatChannel::Group, "Oracle", "the Group row, User_3"},
        {eqt::ui::ChatChannel::Shout, "Oracle", "the Shout row, User_7"},
    };
    for (const auto& line : kLines) {
        eqt::ui::ChatMessage m;
        m.channel = line.ch;
        m.sender = line.sender;
        m.text = line.text;
        buf.addMessage(std::move(m));
    }
    ChatPanelState chat;
    chat.messageBuffer = &buf;
    chat.textColors = &table;
    renderChatPanel(u, l, chat);
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
    // B899 (#215): the era container windows -- a carried bag, a carried
    // tradeskill kit and a world forge, all through the REAL appliers.
    ContainerWindowsState containers;
    // B897: the Friends window, captured once per tab.
    FriendsPanelState friends;
    // B896 (#212): the trade window from BOTH sides of the ledger's row 4
    // (client A pressed Trade) and the NPC give window of row 9, all through
    // the real appliers with real dragitem icon ids.
    TradePanelState tradeA;
    TradePanelState tradeB;
    TradePanelState give;
    // B834 (#202): a context tip and the Tip of the Day, from the client's
    // own help/tips/ files when present (inline stand-ins otherwise).
    TipWindowState tipInventory;
    TipWindowState tipOfTheDay;
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
        {
            // B897: names from the oracle's own Obian_project1999.ini
            // [Friends] and Logjam_project1999.ini [Ignored], plus filler
            // so the VScroll thumb shows; a selection and a typed name.
            eqt::state::SocialListsChangedData sl;
            // In the model's order: sorted (UIGAP-ORACLE/213 friends.tsv rows 1, 11).
            sl.friends = {"aaodyn", "bilblow", "bortill", "caricature", "Drachtha",
                          "Gudknobadd", "hrakk", "khozmo", "shikaka", "Thrasix",
                          "vilurum", "Xenephon"};
            sl.ignored = {"ilvvqwgwqvvqgll", "Spammer"};
            applySocialListsChanged(friends, sl);
            friends.selected[0] = 2;
            friends.nameEntry[0].inputText = "Oraclecharlie";
            friends.nameEntry[1].inputText = "Pest";
        }
        {
            auto slurp = [](const std::string& path, std::string& out) {
                std::ifstream in(path, std::ios::binary);
                if (!in) return false;
                std::ostringstream ss;
                ss << in.rdbuf();
                out = ss.str();
                return true;
            };
            UILayout l;
            l.computeLayout(800, 600);
            const std::string dir = "/mnt/projects/EverQuestP1999/help/tips/";
            std::string html, totd;
            if (!slurp(dir + "ctxinventory.html", html)) {
                html = "<TITLE>Inventory Tip window</TITLE><BODY>Inventory Tips<br><br>"
                       "- (help/tips/ctxinventory.html not found)</BODY>";
            }
            if (!slurp(dir + "TOTD.txt", totd)) {
                totd = "###\n<c \"#00CC00\">\nGameplay Tip\n</c>\n(help/tips/TOTD.txt not found)\n";
            }
            openContextTip(tipInventory, "Inventory", parseTipHtml(html), l);
            auto entries = parseTipOfTheDay(totd);
            // The measured grab's entry ("Fire Beetle eyes ..."), if present.
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].paragraphs.size() > 2 &&
                    entries[i].paragraphs[2].find("Fire Beetle") != std::string::npos) {
                    tipOfTheDay.totdIndex = static_cast<int>(i);
                }
            }
            openTipOfTheDay(tipOfTheDay, entries, l);
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

        // B899 (#215 / #151 Leg B): the container windows, opened through the
        // same applier the bridge uses, with real dragitem icon ids.
        {
            auto open = [this](bool world, int16_t slot, const char* name,
                               uint8_t type, uint8_t count, uint32_t icon) {
                eqt::state::TradeskillContainerOpenedEvent e;
                e.isWorldObject = world;
                e.objectId = world ? 4242u : 0u;
                e.inventorySlot = slot;
                e.containerName = name;
                e.containerType = type;
                e.slotCount = count;
                e.iconId = icon;
                applyContainerOpened(containers, e);
            };
            auto fill = [this](int16_t slotId, const char* name, uint32_t icon,
                               uint32_t qty) {
                eqt::state::InventorySlotChangedData d;
                d.slotId = slotId;
                d.hasItem = true;
                d.itemId = 3000u + static_cast<uint32_t>(slotId);
                d.itemName = name;
                d.iconId = icon;
                d.quantity = qty;
                applyContainerSlotChanged(containers, d);
            };
            namespace inv = eqt::inventory;
            // A Backpack (bag type 5, eight slots) in general slot 1.
            open(false, inv::GENERAL_BEGIN, "Backpack", inv::CONTAINER_BACKPACK, 8, 565);
            fill(inv::calcBagSlotId(inv::GENERAL_BEGIN, 0), "Bread Cake", 537, 4);
            fill(inv::calcBagSlotId(inv::GENERAL_BEGIN, 3), "Water Flask", 550, 1);
            fill(inv::calcBagSlotId(inv::GENERAL_BEGIN, 6), "Bone Chips", 772, 12);
            // A Sewing Kit (bag type 16, ten slots) in general slot 2.
            open(false, inv::GENERAL_BEGIN + 1, "Sewing Kit", inv::CONTAINER_TAILORING, 10, 598);
            fill(inv::calcBagSlotId(inv::GENERAL_BEGIN + 1, 0), "Silk Thread", 1093, 2);
            fill(inv::calcBagSlotId(inv::GENERAL_BEGIN + 1, 1), "Spider Silk", 1094, 1);
            // A world forge.
            open(true, inv::SLOT_INVALID, "Forge", inv::CONTAINER_BLACKSMITHING, 10, 0);
            fill(inv::WORLD_BEGIN + 0, "Small Brick of Ore", 700, 1);
            fill(inv::WORLD_BEGIN + 1, "Water Flask", 550, 1);
            containers.windows[2].hoveredSlot = 1;
        }

        // B896 (#212): review/UIGAP-ORACLE/212/trade.tsv rows 1-4 and 9.
        {
            auto start = [](TradePanelState& s, const char* partner, bool npc, const char* me) {
                eqt::state::TradeStartedData d;
                d.partnerId = npc ? 332 : 329;
                d.partnerName = partner;
                d.isNpc = npc;
                applyTradeStarted(s, d, me);
            };
            auto own = [](TradePanelState& s, int i, const char* name, uint32_t icon) {
                eqt::state::InventorySlotChangedData d;
                d.slotId = static_cast<int16_t>(eqt::inventory::TRADE_BEGIN + i);
                d.hasItem = true;
                d.itemId = 1000u + static_cast<uint32_t>(i);
                d.itemName = name;
                d.iconId = icon;
                d.quantity = 1;
                applyTradeSlotChanged(s, d);
            };
            auto his = [](TradePanelState& s, int i, const char* name, uint32_t icon) {
                eqt::state::TradeItemUpdatedData d;
                d.who = 1;
                d.slot = static_cast<uint8_t>(i);
                d.itemId = 2000u + static_cast<uint32_t>(i);
                d.itemName = name;
                d.iconId = icon;
                d.quantity = 1;
                applyTradeItemUpdated(s, d);
            };
            auto coins = [](TradePanelState& s, uint8_t who, uint32_t pp) {
                eqt::state::TradeMoneyUpdatedData d{who, pp, 0, 0, 0};
                applyTradeMoneyUpdated(s, d);
            };
            // Client A (Testchar): its wolf skin in 3000, 1 pp of its own, B's
            // spell scroll in B's first slot, A has pressed Trade.
            start(tradeA, "Testbuddy", false, "Testchar");
            own(tradeA, 0, "Low Quality Wolf Skin", 783);
            coins(tradeA, 0, 1);
            his(tradeA, 0, "Spell: Minor Shielding", 504);
            applyTradeAcceptStateChanged(tradeA, {true, false});
            tradeA.hoveredOwnSlot = 4;
            // Client B (Testbuddy): the same moment from the other side.
            start(tradeB, "Testchar", false, "Testbuddy");
            own(tradeB, 0, "Spell: Minor Shielding", 504);
            his(tradeB, 0, "Low Quality Wolf Skin", 783);
            coins(tradeB, 1, 1);
            applyTradeAcceptStateChanged(tradeB, {false, true});
            // Row 9: the give window, the bottle auto-placed in slot 1.
            start(give, "Givedummy000", true, "Testchar");
            own(give, 0, "Bottle", 768);
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

// B1075: the always-on HUD, in renderStaticUI()'s order.
void drawCoOpenHud(UIRenderer& u, const UILayout& l, PanelStates& s) {
    renderPlayerStatus(u.layer(), l, s.stats);
    renderTargetInfo(u.layer(), l, s.target);
    renderChatPanel(u.layer(), l, s.chat);
    renderHotbar(u.layer(), l, s.hotbar);
    renderSpellGemPanel(u.layer(), l, s.gems);
    renderBuffBar(u.layer(), l, s.buffs);
    renderGroupPanel(u.layer(), l, s.group);
    renderPetPanel(u.layer(), l, s.pet);
    renderXPBar(u.layer(), l, s.xp);
}

std::vector<PanelCapture> buildCaptureList() {
    return {
        {"player_status", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPlayerStatus(u.layer(), l, s.stats); }},
        {"target_info", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTargetInfo(u.layer(), l, s.target); }},
        {"chat", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderChatPanel(u.layer(), l, s.chat); }},
        {"hotbar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderHotbar(u.layer(), l, s.hotbar); }},
        {"spell_gems", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSpellGemPanel(u.layer(), l, s.gems); }},
        {"buff_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderBuffBar(u.layer(), l, s.buffs); }},
        {"casting_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderCastingBar(u.layer(), l, s.casting); }},
        {"group", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderGroupPanel(u.layer(), l, s.group); }},
        {"group_nogroup", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            GroupPanelState g = s.group; g.inGroup = false;
            renderGroupPanel(u.layer(), l, g); }},
        {"pet", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPetPanel(u.layer(), l, s.pet); }},
        {"xp_bar", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderXPBar(u.layer(), l, s.xp); }},
        {"inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory); }},
        {"spellbook", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSpellbookPopup(u.layer(), l, s.spellbook); }},
        {"skills", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderSkillsPopup(u.layer(), l, s.skills); }},
        {"loot", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderLootPopup(u.layer(), l, s.loot); }},
        // T419 (board B335): the recipe list on the window's left side.
        {"tradeskill", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTradeskillPopup(u.layer(), l, s.tradeskill); }},
        // T421 (board B298, item half): the item display window.
        {"item_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderItemDisplayPopup(u.layer(), l, s.itemDisplay); }},
        // T425 (board B298, spell half): the same window with a SPELL in it.
        {"spell_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderItemDisplayPopup(u.layer(), l, s.spellDisplay); }},
        // T434: one capture per tab of the Options window. `options.isOpen`
        // is set here rather than in the state block so the seven lambdas
        // differ ONLY in activeTab.
        {"options_general", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 0;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_display", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 1;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_mouse", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 2;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_keys", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 3;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_filters", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 4;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_colors", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 5;
            renderOptionsWindow(u.layer(), l, s.options); }},
        {"options_mail", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.options.isOpen = true; s.options.activeTab = 6;
            renderOptionsWindow(u.layer(), l, s.options); }},
        // B993 (#313): the Colors page with Tell selected and the
        // EQUI_ColorPickerWnd open on it, CPW_TC1Button's green chosen.
        {"options_colors_picker", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            OptionsPanelState o = s.options;
            o.isOpen = true; o.activeTab = 5;
            o.colorSelectedRow = 2;
            openColorPicker(o.colorPicker, o.textColors, 2);
            o.colorPicker.working = ColorPickerLayout::swatchColor(1);
            renderOptionsWindow(u.layer(), l, o); }},
        // B995 (#315): the Display page with the Particles box on its Env
        // and Player sub-tabs (Env: density Medium, opacity 60 % -- B1175:
        // position 60 of the client's 0..100, the knob pixel of the old 12/20).
        {"options_display_particles_env", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            OptionsPanelState o = s.options;
            o.isOpen = true; o.activeTab = 1; o.particlesTab = 1;
            setOptionsValue(o, OptionsLayout::Setting::EnvParticleDensity, 2);
            setOptionsValue(o, OptionsLayout::Setting::EnvParticleOpacity, 60);
            renderOptionsWindow(u.layer(), l, o); }},
        {"options_display_particles_player", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            OptionsPanelState o = s.options;
            o.isOpen = true; o.activeTab = 1; o.particlesTab = 2;
            renderOptionsWindow(u.layer(), l, o); }},
        // B993: the same Tell / Group / Shout lines in the default table and
        // with the Tell row changed to (0,240,0) -- the chat panel's one
        // colour decision, resolveChatLineColor().
        {"chat_textcolors_default", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            static eqt::ui::ChatTextColors table = eqt::ui::defaultChatTextColors();
            renderTextColorChat(u.layer(), l, table); }},
        {"chat_textcolors_tell_green", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            static eqt::ui::ChatTextColors table = [] {
                auto t = eqt::ui::defaultChatTextColors();
                t[1] = ColorPickerLayout::swatchColor(1);
                return t;
            }();
            renderTextColorChat(u.layer(), l, table); }},
        // T436 (board B429): the three windows B429 said did not exist.
        // `merchant_sell` is the SAME window after a left click on one of the
        // player's items -- B442b (#152, merchant.tsv row 3): the last
        // selection was the player's, so the shared rect reads Sell and
        // MW_SelectedItem* describe that item. The cursor is not involved.
        {"merchant", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            CursorAttachmentState empty;
            renderMerchantPopup(u.layer(), l, s.merchant, empty); }},
        {"merchant_sell", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            MerchantPanelState m = s.merchant;
            m.selectPlayerItem(27, "Bottle", 1068, 5);
            CursorAttachmentState empty;
            renderMerchantPopup(u.layer(), l, m, empty); }},
        {"bank", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderBankWindow(u.layer(), l, s.bank); }},
        // B898 (#214): the skill trainer window, and the same window scrolled.
        {"trainer", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.trainer.scrollOffset = 0;
            renderTrainerWindow(u.layer(), l, s.trainer, s.xp.progress); }},
        {"trainer_scrolled", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.trainer.scrollOffset = s.trainer.maxScrollOffset();
            renderTrainerWindow(u.layer(), l, s.trainer, s.xp.progress); }},
        // B895: the Tracking Window, and the same window with its sort
        // combobox dropped down.
        {"tracking", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.tracking.openCombo = TrackingCombo::None;
            renderTrackingWindow(u.layer(), l, s.tracking); }},
        {"tracking_sort_open", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.tracking.openCombo = TrackingCombo::Sort;
            renderTrackingWindow(u.layer(), l, s.tracking); }},
        // B899 (#215): each container window alone, then all three open at
        // once: they share the oracle position, newest on top (measured).
        {"container_bag", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            ContainerWindowsState one;
            one.windows[1] = s.containers.windows[1];
            renderContainerWindows(u.layer(), l, one); }},
        {"container_sewing_kit", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            ContainerWindowsState one;
            one.windows[2] = s.containers.windows[2];
            renderContainerWindows(u.layer(), l, one); }},
        {"container_forge", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            ContainerWindowsState one;
            one.windows[0] = s.containers.windows[0];
            renderContainerWindows(u.layer(), l, one); }},
        {"container_three_open", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderContainerWindows(u.layer(), l, s.containers); }},
        // B992 (#312): every window in these captures is passed `u.layer()`,
        // as renderStaticUI() does, so a later window covers an earlier
        // one's text. Two overlapping windows, the lower one's text under the
        // upper one's panel. The Backpack (its "Backpack" label and its "4" /
        // "12" stack counts) drawn first, the Forge on top at the same place.
        {"stack_backpack_under_forge", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            ContainerWindowsState bag;
            bag.windows[1] = s.containers.windows[1];
            renderContainerWindows(u.layer(), l, bag);
            ContainerWindowsState forge;
            forge.windows[0] = s.containers.windows[0];
            renderContainerWindows(u.layer(), l, forge); }},
        // B897: both tabs of the Friends window (EQUI_FriendsWnd.xml).
        {"friends_friends", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.friends.isOpen = true; s.friends.activePage = 0;
            renderFriendsWindow(u.layer(), l, s.friends); }},
        {"friends_ignored", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            s.friends.isOpen = true; s.friends.activePage = 1;
            renderFriendsWindow(u.layer(), l, s.friends); }},
        // B896 (#212): both sides of trade.tsv row 4 and the give window.
        {"trade_client_a_own_accepted", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTradeWindow(u.layer(), l, s.tradeA); }},
        {"trade_client_b_partner_accepted", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTradeWindow(u.layer(), l, s.tradeB); }},
        {"trade_with_inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderTradeWindow(u.layer(), l, s.tradeA); }},
        {"give_npc", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTradeWindow(u.layer(), l, s.give); }},
        // B1075 (#395): the layout table's required co-open sets, each drawn
        // over the whole always-on HUD in the production draw order, so a
        // reviewer sees both windows AND what they sit beside.
        {"coopen_inventory_merchant", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderInventoryPopup(u.layer(), l, s.inventory);
            CursorAttachmentState empty;
            renderMerchantPopup(u.layer(), l, s.merchant, empty); }},
        {"coopen_inventory_bank", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderBankWindow(u.layer(), l, s.bank); }},
        {"coopen_inventory_trade", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderTradeWindow(u.layer(), l, s.tradeA); }},
        {"coopen_inventory_give", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderTradeWindow(u.layer(), l, s.give); }},
        {"coopen_inventory_loot", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderLootPopup(u.layer(), l, s.loot); }},
        {"coopen_spellbook_gems", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderSpellbookPopup(u.layer(), l, s.spellbook); }},
        {"coopen_skills_hud", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            drawCoOpenHud(u, l, s);
            renderSkillsPopup(u.layer(), l, s.skills); }},
        // B1004 (#323): the item-name tooltips (review/UIGAP-ORACLE/323/).
        // `tooltip_alt_bag`: Alt held, pointer on the Backpack's label -- every
        // item named at once, the right column half a slot lower.
        // `tooltip_alt_bank_from_inventory`: the bank open, Alt held with the
        // pointer on the INVENTORY (row 30) -- the bank's labels.
        // `tooltip_hover_inventory`: the hover tooltip over general slot 1
        // after the dwell, its bottom edge on the pointer.
        {"tooltip_alt_bag", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            ContainerWindowsState one;
            one.windows[1] = s.containers.windows[1];
            renderContainerWindows(u.layer(), l, one);
            const auto c = ContainerLayout::labelRect(l);
            tooltipCapture(u, l, nullptr, nullptr, &one,
                           (c.UpperLeftCorner.X + c.LowerRightCorner.X) / 2,
                           (c.UpperLeftCorner.Y + c.LowerRightCorner.Y) / 2, true); }},
        {"tooltip_alt_bank_from_inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            renderBankWindow(u.layer(), l, s.bank);
            const auto r = InventoryLayout::slotRect(l, 22 + 1);
            tooltipCapture(u, l, &s.inventory, &s.bank, nullptr, r.UpperLeftCorner.X - 40,
                           r.UpperLeftCorner.Y + 10, true); }},
        {"tooltip_hover_inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            const auto r = InventoryLayout::slotRect(l, 22 + 1);
            tooltipCapture(u, l, &s.inventory, nullptr, nullptr, r.UpperLeftCorner.X + 16,
                           r.UpperLeftCorner.Y + 16, false); }},
        // B834 (#202): EQUI_TipWnd.xml, both kinds.
        {"tip_inventory", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTipWindow(u.layer(), l, s.tipInventory); }},
        {"tip_of_the_day", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderTipWindow(u.layer(), l, s.tipOfTheDay); }},
        // B888 (#204): the two Character Creation pages, in the selections
        // the oracle frames were taken in (eq-oracle/logs/shots/cc_06_stats.png:
        // Human male Warrior, all 25 points in AGILITY; cc_08_deity_city.png:
        // AGNOSTIC, QEYNOS) so the pair can be laid side by side, plus page 0
        // holding eqstr 1577 (Next pressed with points left).
        {"charcreate_page0", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            auto s = charCreateCaptureState(0);
            renderCharCreateScreen(u, l.screenW, l.screenH, s, charCreateEqstr()); }},
        {"charcreate_facepick", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            auto s = charCreateCaptureState(0);
            openFacePick(s);
            renderCharCreateScreen(u, l.screenW, l.screenH, s, charCreateEqstr()); }},
        {"charcreate_page1", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            auto s = charCreateCaptureState(1);
            renderCharCreateScreen(u, l.screenW, l.screenH, s, charCreateEqstr()); }},
        {"charcreate_page0_points_left", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            auto s = charCreateCaptureState(2);
            renderCharCreateScreen(u, l.screenW, l.screenH, s, charCreateEqstr()); }},
        // B998 (#318): the Character Select screen -- with the recorded
        // account's characters, empty, the delete prompt, and the creation
        // page it opens (the pre-zone host draws exactly this call).
        {"charselect_characters", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            renderCharSelectScreen(u.layer(), l, charSelectCaptureState(0)); }},
        {"charselect_empty", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            renderCharSelectScreen(u.layer(), l, charSelectCaptureState(1)); }},
        {"charselect_delete_prompt", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            renderCharSelectScreen(u.layer(), l, charSelectCaptureState(2)); }},
        {"charselect_create_opened", [](UIRenderer& u, const UILayout& l, PanelStates&) {
            renderCharSelectScreen(u.layer(), l, charSelectCaptureState(3)); }},
        {"cursor_item", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            // Placed off-centre so the 50x50 box and its (5,5) inset are
            // both visible against the empty frame.
            s.cursor.mouseX = l.screenW / 2;
            s.cursor.mouseY = l.screenH / 2;
            renderCursorAttachment(u.layer(), l, s.cursor); }},
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
            renderInventoryPopup(u.layer(), l, inv);
            CursorAttachmentState held;
            held.hasItem = true;
            held.itemName = s.inventory.generalSlots[2].itemName;
            held.iconId = s.inventory.generalSlots[2].iconId;
            const auto target = InventoryLayout::slotRect(l, 22 + 5);
            held.mouseX = target.UpperLeftCorner.X + 8;
            held.mouseY = target.UpperLeftCorner.Y + 8;
            renderCursorAttachment(u.layer(), l, held); }},
        {"quantity", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            QuantityWindowState q;
            q.openForStack(23, "Arrow", 20);
            q.value = 7;
            renderQuantityWindow(u.layer(), l, q); }},
        // B442b (#151 remainder). `quantity_typed`: the box holds the
        // keyboard and "2" was typed (row 5). `cursor_coin`: 10 platinum on
        // the cursor over the purse, the decal + count strip (row 29).
        // `confirm_drop` / `confirm_destroy`: the confirmation dialog over the
        // inventory with an item held (rows 31, 34); the texts are eqstr 9057 /
        // 2043 with the oracle run's own items, typed here as fixture data
        // (the client composes them game-side from the string table).
        // `inventory_sell_selected`: the popup with slot 27 selected for sale
        // (the yellow frame, #152 row 3) and the Destroy button.
        {"quantity_typed", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            QuantityWindowState q;
            q.openForStack(26, "Low Quality Wolf Skin", 3);
            q.input.inputActive = true;
            q.input.inputText = "2";
            q.value = 2;
            renderQuantityWindow(u.layer(), l, q); }},
        {"cursor_coin", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            CursorAttachmentState coin;
            coin.coinType = 3;   // EQT::COINTYPE_PP
            coin.coinAmount = 10;
            const auto pp = InventoryLayout::coinRect(l, 0);
            coin.mouseX = pp.UpperLeftCorner.X + 20;
            coin.mouseY = pp.UpperLeftCorner.Y - 40;
            renderCursorAttachment(u.layer(), l, coin); }},
        {"confirm_drop", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            ConfirmationDialogState c;
            c.open(1, "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?");
            renderConfirmationDialog(u.layer(), l, c);
            CursorAttachmentState held = s.cursor;
            held.itemName = "Rusty Long Sword";
            held.iconId = 1001;
            held.mouseX = l.screenW / 2 + 160;
            held.mouseY = l.screenH / 2 + 120;
            renderCursorAttachment(u.layer(), l, held); }},
        {"confirm_destroy", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderInventoryPopup(u.layer(), l, s.inventory);
            ConfirmationDialogState c;
            c.open(2, "Are you absolutely sure you want to destroy your Low Quality Wolf Skin?");
            c.yesHovered = true;
            renderConfirmationDialog(u.layer(), l, c); }},
        {"inventory_sell_selected", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            InventoryPanelState inv = s.inventory;
            inv.selectedSlot = 27;
            inv.destroyHovered = true;
            renderInventoryPopup(u.layer(), l, inv); }},
        // Everything the HUD draws at once -- the screen a player sees.
        {"hud_all", [](UIRenderer& u, const UILayout& l, PanelStates& s) {
            renderPlayerStatus(u.layer(), l, s.stats);
            renderTargetInfo(u.layer(), l, s.target);
            renderGroupPanel(u.layer(), l, s.group);
            renderPetPanel(u.layer(), l, s.pet);
            renderBuffBar(u.layer(), l, s.buffs);
            renderChatPanel(u.layer(), l, s.chat);
            renderCastingBar(u.layer(), l, s.casting);
            renderHotbar(u.layer(), l, s.hotbar);
            renderSpellGemPanel(u.layer(), l, s.gems);
            renderXPBar(u.layer(), l, s.xp); }},
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
    irr::u32 captureW = 800, captureH = 600;
    std::string onlyPanel;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--eqe1-path") && i + 1 < argc) {
            eqe1Path = argv[++i];
        } else if (!strcmp(argv[i], "--opaque-alpha")) {
            opaqueAlpha = true;
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            unsigned w = 0, h = 0;
            if (sscanf(argv[++i], "%ux%u", &w, &h) != 2 || w < 400 || h < 452) {
                fprintf(stderr, "ERROR: --size expects WxH, at least 400x452\n");
                return 1;
            }
            captureW = w;
            captureH = h;
        } else if (!strcmp(argv[i], "--panel") && i + 1 < argc) {
            onlyPanel = argv[++i];
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

    const irr::u32 kW = captureW, kH = captureH;
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
            if (!onlyPanel.empty()) {
                const bool prefix = onlyPanel.back() == '*';
                const std::string name = prefix ? onlyPanel.substr(0, onlyPanel.size() - 1)
                                                : onlyPanel;
                if ((!prefix && cap.name != name) ||
                    (prefix && std::string(cap.name).compare(0, name.size(), name) != 0)) continue;
            }
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
            const std::string path = outputDir + "/" + cap.name + "_" +
                std::to_string(kW) + "x" + std::to_string(kH) + ".png";
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
