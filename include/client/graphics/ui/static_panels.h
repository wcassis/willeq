/*
 * Static UI panels — simple render functions, not a class hierarchy.
 *
 * U03d: Each panel is a free function that takes a UIRenderer + data.
 * Data comes from cached bridge events, not from EverQuest pointers.
 */

#pragma once

#include <irrlicht.h>
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

#include "common/packets/common.h"  // T483: the gem counts

#include "client/hotbar/hotbar_slot_type.h"  // T156
#include "client/graphics/ui/text_entry.h"  // T421: ChatPanelState derives from it
#include "client/graphics/ui/chat_wrap_cache.h"  // T462: ChatPanelState::wrapCache

namespace eqt { namespace ui { class ChatMessageBuffer; } }
// T156: applyHotbarSlotAssigned() below takes this by const reference; the
// definition lives in static_panels.cpp, which includes client/state/event_bus.h.
// Forward-declared here so every graphics TU does not pay for that header.
namespace eqt { namespace state { struct HotbarSlotAssignedData; } }
// T290: the four loot bridge arms apply their payloads through the free
// functions declared below; same forward-declare trick as
// HotbarSlotAssignedData so graphics TUs do not pay for event_bus.h.
namespace eqt { namespace state {
struct LootWindowOpenedData;
struct LootWindowClosedData;
struct LootItemAddedData;
struct LootItemRemovedData;
// T393 (board B201): the purse. The CurrencyChanged arm was a LOG_TRACE that
// discarded its payload, so no coin count ever reached a panel.
struct CurrencyChangedData;
// T413 (board B296): the casting bar and the spellbook's memorisation gauge
// apply their payloads through the free functions declared below.
struct CastingStateChangedData;
struct SpellMemorizingData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;
struct UILayout;

// Cached player stats (from PlayerStatsChanged bridge event)
struct PlayerStatsData {
    uint32_t curHP = 0, maxHP = 1;
    uint32_t curMana = 0, maxMana = 1;
    uint32_t curEndurance = 0, maxEndurance = 1;
    uint8_t level = 1;
    std::string name;
};

// Cached target info (from TargetChanged bridge event)
struct TargetInfoData {
    uint16_t spawnId = 0;  // 0 = no target
    std::string name;
    uint8_t level = 0;
    uint8_t hpPercent = 100;
};

/** Render player status panel (HP/mana/stamina bars + name). */
void renderPlayerStatus(UIRenderer& ui, const UILayout& layout,
                        const PlayerStatsData& stats);

/**
 * T416 (board B316): the health-percent label, as one shared body.
 *
 * The target panel, every group member row and the pet panel each formatted
 * it as `fmt::format("{}%%", pct)` -- and fmt is not printf: `%` is an
 * ordinary character to it, so all three drew **"100%%"** on screen, doubled
 * sign and all. Caught in T416's panel captures
 * (review/T416/crops/group_3way.png). One body, pinned by a test, like
 * coinRowText() below.
 */
std::string percentText(uint8_t percent);

/** Render target info panel (HP bar + name). Only renders if target exists. */
void renderTargetInfo(UIRenderer& ui, const UILayout& layout,
                      const TargetInfoData& target);

// Cached chat panel state
// T421 (board B365): the three input members moved to TextEntryState, which
// this now DERIVES from -- `inputText`, `cursorPos` and `inputActive` keep
// their names and every chat call site is untouched. They live there because
// the tradeskill window's three editboxes are the same widget, and fable has
// exactly one text-entry mechanism (include/client/graphics/ui/text_entry.h).
struct ChatPanelState : TextEntryState {
    eqt::ui::ChatMessageBuffer* messageBuffer = nullptr;
    int scrollOffset = 0;      // Lines scrolled up from bottom (0 = at bottom)
    bool showTimestamps = false;
    bool mouseHover = false;   // Whether mouse is over chat panel

    // T462 (board B505): the wrapped lines, kept across frames and brought up
    // to date only when the buffer, the width or the timestamp setting
    // changes (chat_wrap_cache.h). renderChatPanel() is its only writer.
    ChatWrapCache wrapCache;
    // Test seam: messages renderChatPanel() has formatted and word-wrapped,
    // cumulative. One per NEW message, not one per message per frame.
    uint64_t messagesWrapped = 0;
};

/** Render chat panel (message history + input field). */
void renderChatPanel(UIRenderer& ui, const UILayout& layout,
                     ChatPanelState& state);

/**
 * T313 -- how many wrapped lines the chat message area shows at once.
 *
 * The scroll input needs a page size, and renderChatPanel() derives the
 * same number from `layout.chatMessages` and the font's line height. The
 * font is only reachable through a live TextBatch, so the default here is
 * renderChatPanel()'s own `lineH <= 0` fallback; a caller that has a
 * TextBatch should pass its getLineHeight(). Getting it slightly wrong
 * costs at most a line of overshoot, because renderChatPanel() re-clamps
 * `scrollOffset` against the REAL wrapped-line count every frame -- that
 * clamp, not this helper, is the authority.
 */
int chatVisibleLines(const UILayout& layout, int lineHeight = 14);

// Popup types for center screen area.
//
// T439 (board B434 CLOSED): `Options` was here and is DELETED. T434 built the
// Options window as an INDEPENDENT window with its own OptionsPanelState and
// the oracle's own placement -- EQUI_OptionsWindow.xml is `RelativePosition
// false` at <Location>(90,47)</Location> -- and not as one of these mutually
// exclusive centre popups, so the enumerator never gained a producer or a
// consumer. It is deleted rather than pressed into a role the oracle does not
// give it. The same is true of the merchant window, which IS a centre popup
// but is tracked by MerchantPanelState::isOpen rather than by `Vendor` here.
// T442 (board B450) shrank this to the set fable actually drives. It once
// carried Spellbook, Bank, Loot, Trade and Skills as well, and nothing in the
// tree named any of them: each of those windows exists and is drawn, but each
// tracks its own visibility on its own panel state (BankPanelState::isOpen,
// LootPanelState, SkillsPopupState, ...), exactly as T434's Options window
// and T436's bank and merchant windows do. Adding an enumerator here without
// a producer AND a consumer is the B434/B450 defect again.
enum class PopupType : uint8_t {
    None = 0,
    Inventory,
    Vendor
};

// A single inventory slot's visible state
struct SlotDisplayInfo {
    bool hasItem = false;
    std::string itemName;       // Short name for display
    uint32_t iconId = 0;        // Item icon ID (for future icon atlas)
    int32_t quantity = 1;       // Stack count
    // B442b (#151 rows 19-21): the bag size, 0 = not a container. A RIGHT
    // click on a general slot holding a bag toggles its window.
    uint8_t bagSlots = 0;
};

// Cached inventory state for rendering
struct InventoryPanelState {
    PopupType activePopup = PopupType::None;
    // Equipment slots (22 total: 0-21)
    static constexpr int EQUIP_SLOTS = 22;
    SlotDisplayInfo equipSlots[22];
    // General inventory (8 slots)
    static constexpr int GENERAL_SLOTS = 8;
    SlotDisplayInfo generalSlots[8];
    // Hovered slot (-1 = none)
    int hoveredSlot = -1;
    // T60: the character paperdoll's rendered texture (owned by
    // IrrlichtRenderer's CharacterModelView, lazily constructed — see
    // InventoryLayout::paperdollRect() for where it's drawn). Null if the
    // doll hasn't been constructed yet (e.g. RTT unavailable) or has no
    // texture this frame; renderInventoryPopup() must handle null.
    irr::video::ITexture* paperdollTexture = nullptr;

    // T393 (board B201): the player's purse, in the oracle's own
    // denomination order (IW_Money0..3 = platinum/gold/silver/copper).
    // Fed by applyCurrencyChanged() below from the CurrencyChanged bridge
    // event; drawn by renderInventoryPopup() into
    // InventoryLayout::coinRect(). Absolute counts, not deltas -- every
    // one of the five publishers sends the player's whole purse.
    int32_t coins[4] = {0, 0, 0, 0};

    // B442b (#152, merchant.tsv row 3): the inventory item a left click
    // SELECTED for sale while a merchant window is open (the oracle frames it
    // in yellow; the cursor is never used). -1 = none. Written by
    // StaticUIInput beside MerchantPanelState's own copy of the selection.
    int selectedSlot = -1;
    // B442b (#151 row 34): IW_Destroy, hovered.
    bool destroyHovered = false;
};

/** Render inventory popup (equipment + general slots). Only when activePopup == Inventory. */
void renderInventoryPopup(UIRenderer& ui, const UILayout& layout,
                          const InventoryPanelState& inv);

/**
 * T393 (board B201): the CurrencyChanged bridge arm, as a free function.
 *
 * Same idiom as applyLootWindowOpened() and applyHotbarSlotAssigned() -- the
 * arm in IrrlichtBridge was a LOG_TRACE that discarded its payload, and a
 * free function lets a test drive the real publisher's data struct through
 * the real applier with no Irrlicht device.
 */
void applyCurrencyChanged(InventoryPanelState& state,
                          const eqt::state::CurrencyChangedData& data);

/**
 * The text the popup prints in coin row `denom` (0=platinum .. 3=copper):
 * the count, decimal, no separators -- the oracle's IW_Money buttons carry a
 * bare number as their <Text> (the shipped placeholder is "9999"). Exposed so
 * the static-panel tests can assert the drawn string without a device;
 * renderInventoryPopup() formats coin text nowhere else.
 */
std::string coinRowText(const InventoryPanelState& state, int denom);

// Cached hotbar slot state
struct HotbarSlotState {
    // T156: THE hotbar numbering — client/hotbar/hotbar_slot_type.h.
    eqt::HotbarSlotType type = eqt::HotbarSlotType::Empty;
    uint32_t id = 0;          // T156: spell/skill/item id — was dropped here,
                              // which is why HotbarActivateIntent had nothing
                              // to carry and the Skill arm was unreachable.
    std::string emoteText;    // T156: for Emote slots
    std::string name;         // Spell/skill/item name (abbreviated for display)
    uint32_t iconId = 0;      // Icon ID (for future icon atlas)
    // Cooldown tracking
    std::chrono::steady_clock::time_point cooldownEndTime;
    uint32_t cooldownDurationMs = 0;

    bool isOnCooldown() const {
        return cooldownDurationMs > 0 && std::chrono::steady_clock::now() < cooldownEndTime;
    }
    float getCooldownProgress() const {
        if (cooldownDurationMs == 0) return 1.0f;
        auto now = std::chrono::steady_clock::now();
        if (now >= cooldownEndTime) return 1.0f;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(cooldownEndTime - now).count();
        return 1.0f - static_cast<float>(remaining) / static_cast<float>(cooldownDurationMs);
    }
};

struct HotbarPanelState {
    static constexpr int SLOT_COUNT = 10;
    HotbarSlotState slots[10];
    int hoveredSlot = -1;
};

/** Render hotbar (10 slots, bottom-center). */
void renderHotbar(UIRenderer& ui, const UILayout& layout,
                  const HotbarPanelState& state);

/**
 * T156: apply a HotbarSlotAssigned bridge event to the cached panel state.
 *
 * This is the single hop between HotbarModel's published event and the state
 * StaticUIInput reads when building a HotbarActivateIntent. It lives here, as a
 * free function, rather than inline in IrrlichtBridge's event switch, so tests
 * can drive the REAL producer (HotbarModel) all the way through to the REAL
 * consumer input path without an Irrlicht device and without hand-building the
 * intermediate state — the fake-substitution trap T93 catalogued.
 * Out-of-range indices are ignored.
 */
void applyHotbarSlotAssigned(HotbarPanelState& state,
                             const eqt::state::HotbarSlotAssignedData& data);

// Cached spell gem state
struct SpellGemState {
    uint32_t spellId = 0;       // 0 or SPELL_UNKNOWN = empty
    uint8_t gemState = 0;       // GemState enum: 0=Empty, 1=Ready, 2=Casting, 3=Refresh, 4=Memorize
    std::string spellName;
    uint32_t iconId = 0;
    uint32_t cooldownRemainingMs = 0;
    uint32_t cooldownTotalMs = 0;
    std::chrono::steady_clock::time_point lastUpdateTime;
};

struct SpellGemPanelState {
    // T483: EQT::VISIBLE_SPELL_GEM_COUNT -- the official skin's eight
    // (include/common/packets/common.h). The stored ninth gem is never shown.
    SpellGemState gems[EQT::VISIBLE_SPELL_GEM_COUNT];
    int hoveredGem = -1;
    // T272: destination gem for the next spellbook memorize click, chosen by
    // clicking a gem WHILE THE SPELLBOOK IS OPEN. -1 means "no explicit
    // choice" -- resolveMemorizeGem() then picks the first empty gem, which
    // is what a plain click on a spell does.
    int memorizeTargetGem = -1;
};

/**
 * T272: resolve which gem a spellbook memorize click should target.
 *
 * Returns state.memorizeTargetGem when the user has explicitly picked one
 * (a gem click while the spellbook is open), otherwise the first empty gem,
 * otherwise -1 ("all eight gems are full and you did not say which to
 * overwrite" -- the click is then a no-op, not a silent clobber of gem 1).
 *
 * A free function rather than a method so tests can drive the same body the
 * click handler uses without an Irrlicht device.
 */
int resolveMemorizeGem(const SpellGemPanelState& state);

/** Render spell gem panel (8 gem slots, bottom-right). */
void renderSpellGemPanel(UIRenderer& ui, const UILayout& layout,
                         const SpellGemPanelState& state);

// Cached buff state
struct BuffSlotState {
    uint32_t spellId = 0;       // 0 = empty
    std::string spellName;
    uint32_t iconId = 0;
    uint32_t ticksLeft = 0;     // Ticks remaining (6s per tick)
    std::chrono::steady_clock::time_point updateTime;  // When ticksLeft was set
};

struct BuffBarState {
    // T368 (B101): the Alt+B key's toggle. Default OPEN because the official
    // client's defaults.ini has no `Show=0` under [BuffWindow] (it writes that
    // key only for a window whose default is closed -- SelectorWindow,
    // HotButtonWnd3/4), so the buff window ships shown.
    bool isOpen = true;
    static constexpr int MAX_BUFFS = 25;
    BuffSlotState buffs[25];
};

/**
 * T368 (B101): does the buff bar draw this frame?
 *
 * Composes the Alt+B toggle with the auto-driven content gate the bar has
 * always had: an empty bar draws nothing at all (no background, no icons --
 * see hitTestBuffBar, which returns -1 for exactly that reason), and a
 * closed bar draws nothing whatever it holds. A free predicate so the
 * renderer and a test read the same body without an Irrlicht device.
 */
bool buffBarIsVisible(const BuffBarState& state);

/** Render buff bar (icon row above chat). */
void renderBuffBar(UIRenderer& ui, const UILayout& layout,
                   const BuffBarState& state);

/**
 * Hit-test a point against the buff bar and return the BUFF SLOT under it,
 * or -1 (T277).
 *
 * Not the visual column index: renderBuffBar() PACKS the bar, skipping empty
 * slots, so the third icon on screen can be slot 7. This walks the slots in
 * exactly the same order with exactly the same geometry, so the icon the
 * cursor is over and the slot the click cancels can never disagree. Any
 * change to the bar's ICON/PAD/overflow rule has to be made in both bodies;
 * the test pins them against each other.
 *
 * A free function, like resolveMemorizeGem(), so tests drive the same body
 * the click handler uses without an Irrlicht device.
 */
int hitTestBuffBar(const UILayout& layout, const BuffBarState& state,
                   int mouseX, int mouseY);

// Cached casting bar state
//
// The oracle is EQUI_CastingWindow.xml's <Gauge item="Casting_Gauge">
// (EQType 7, 116x10 at (3,19) inside the 140x60 "Casting Time" screen): one
// horizontal fill plus the spell name above it. fable's bar is that gauge.
struct CastingBarState {
    bool isCasting = false;
    std::string spellName;
    uint32_t castTimeMs = 0;
    std::chrono::steady_clock::time_point castStartTime;

    // T413 (board B296): the AUTHORITATIVE remaining/total the game thread
    // computes and publishes on CastingStateChanged. Before this the bar had
    // only its own render-thread stopwatch, started when
    // SpellCastVisualStarted arrived, so every source of skew between the two
    // threads (a stall, a long frame, a zone load, a cast the server shortened)
    // showed up as a gauge that disagreed with the cast it was drawing --
    // and the two fields the game thread had already computed for exactly this
    // were read by nobody. totalMs == 0 means "no authoritative sample yet",
    // and the stopwatch is used, exactly as before.
    uint32_t remainingMs = 0;
    uint32_t totalMs = 0;

    float getProgress() const {
        if (!isCasting) return 0.0f;
        if (totalMs > 0) {
            const uint32_t done = (remainingMs >= totalMs) ? 0u : (totalMs - remainingMs);
            return std::min(1.0f, static_cast<float>(done) / static_cast<float>(totalMs));
        }
        if (castTimeMs == 0) return 0.0f;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - castStartTime).count();
        return std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(castTimeMs));
    }
};

/**
 * T413: apply a CastingStateChanged bridge event to the cached bar state.
 *
 * Same one-hop free-function shape as applyHotbarSlotAssigned() above, for
 * the same reason: a test can drive the real producer's data struct into the
 * real consumer state with no Irrlicht device. A `isCasting == false` payload
 * clears the bar, which is what the game thread means by it.
 */
void applyCastingStateChanged(CastingBarState& state,
                              const eqt::state::CastingStateChangedData& data);

/** Render casting bar (center-bottom, only when casting). */
void renderCastingBar(UIRenderer& ui, const UILayout& layout,
                      const CastingBarState& state);

// Cached group member state
struct GroupMemberDisplay {
    std::string name;
    uint8_t hpPercent = 100;
    uint8_t manaPercent = 100;
    bool inZone = false;
};

struct GroupPanelState {
    // T368 (B101): the Alt+P key's toggle (T382/B170: the oracle's own key),
    // default OPEN (see the note on
    // BuffBarState::isOpen; a real character's UI_*.ini persists
    // `[GroupWindow] Show=1`).
    bool isOpen = true;
    bool inGroup = false;
    bool isLeader = false;
    std::string leaderName;
    int memberCount = 0;
    static constexpr int MAX_MEMBERS = 5;
    GroupMemberDisplay members[5];
};

/**
 * T368 (B101): does the group panel draw this frame?
 *
 * The toggle alone -- an OPEN group window with no group still draws (as its
 * bare title bar, see groupPanelFrameRect), which is why this does not consult
 * `inGroup`. Joining a group does not reopen a window you closed.
 *
 * T382 (B170): VERIFIED against the oracle's pixels. T377 screenshotted an
 * official client with the group window open and no group
 * (`review/T377/oracle/`): it is shown, and it paints only its title bar.
 */
bool groupPanelIsVisible(const GroupPanelState& state);

/**
 * T382 (B170): the rect renderGroupPanel paints.
 *
 * MEASURED at the oracle (T377, `review/T377/oracle/`): with NO group the
 * official client paints ONLY a title strip reading `Group` -- no member rows,
 * no empty body, no reserved height. With a group it paints the full panel.
 * The collapsed height is `BAR_HEIGHT + MARGIN`, the title band the panel's
 * own layout already reserves before the first member row, so the two shapes
 * share an origin and a width.
 *
 * Exposed for the static-panel tests: the shape is assertable with no
 * Irrlicht device.
 */
irr::core::rect<irr::s32> groupPanelFrameRect(const UILayout& layout,
                                              const GroupPanelState& state);

/** Render group panel (left side below player status). */
void renderGroupPanel(UIRenderer& ui, const UILayout& layout,
                      const GroupPanelState& state);

// Cached pet state
struct PetPanelState {
    // T368 (B101): the P key's toggle, default OPEN. A real character's
    // UI_*.ini persists `[PetInfoWindow] Show=0`, so the official pet window
    // is a user-toggled flag independent of whether a pet exists.
    bool isOpen = true;
    bool hasPet = false;
    std::string name;
    uint8_t level = 0;
    uint8_t hpPercent = 100;
    static constexpr int BUTTON_COUNT = 10;
    bool buttonStates[10] = {};  // PetButton toggle states
};

/**
 * T368 (B101): does the pet panel draw this frame?
 *
 * Composes the P toggle with the auto-driven `hasPet` gate: with no pet there
 * is no name, no health and no button row, and unlike the group panel the
 * panel has no title of its own to draw, so an empty frame would be a blank
 * box. Closed hides it whether or not a pet exists.
 */
bool petPanelIsVisible(const PetPanelState& state);

/** Render pet panel (below group panel). */
void renderPetPanel(UIRenderer& ui, const UILayout& layout,
                    const PetPanelState& state);

// Cached spellbook entry for display
struct SpellbookDisplayEntry {
    uint16_t slot = 0;
    uint32_t spellId = 0;
    std::string name;
    uint32_t iconId = 0;
    uint8_t level = 0;
};

// Spellbook popup state (UI presentation — page navigation is client-side)
struct SpellbookPopupState {
    bool isOpen = false;
    std::vector<SpellbookDisplayEntry> spells;  // All scribed spells
    int currentPage = 0;                        // 0-based page index
    static constexpr int SPELLS_PER_PAGE = 8;
    int hoveredSlot = -1;                       // 0-7 within current page

    // T413 (board B296): memorisation progress, from SpellMemorizingData.
    // The oracle puts this gauge in the SPELLBOOK, not on the gem — see
    // spellbookMemorizeGaugeRect() below for the quoted XML.
    bool isMemorizing = false;
    uint8_t memorizeGemSlot = 0;
    uint32_t memorizeProgressMs = 0;
    uint32_t memorizeTotalMs = 0;

    float memorizeProgress() const {
        if (!isMemorizing || memorizeTotalMs == 0) return 0.0f;
        if (memorizeProgressMs >= memorizeTotalMs) return 1.0f;
        return static_cast<float>(memorizeProgressMs) / static_cast<float>(memorizeTotalMs);
    }

    int pageCount() const {
        return spells.empty() ? 1 : static_cast<int>((spells.size() + SPELLS_PER_PAGE - 1) / SPELLS_PER_PAGE);
    }
};

/**
 * T413: apply a SpellMemorizing bridge event to the cached spellbook state.
 * `isMemorizing == false` clears the gauge.
 */
void applySpellMemorizing(SpellbookPopupState& state,
                          const eqt::state::SpellMemorizingData& data);

/**
 * T413: the memorisation gauge's rect, the oracle's own, scaled into the
 * centre popup exactly the way InventoryLayout::coinRect()/paperdollRect()
 * scale theirs (proportional, integer division).
 *
 * Quoted from /mnt/projects/EverQuestP1999/uifiles/default/EQUI_SpellBookWnd.xml:
 *
 *   <Gauge item="SBW_Memorize_Gauge">
 *     <Location><X>204</X><Y>312</Y></Location>
 *     <Size><CX>100</CX><CY>8</CY></Size>
 *     <EQType>9</EQType>
 *   </Gauge>
 *
 * inside  <Screen item="SpellBookWnd">  <Size><CX>510</CX><CY>360</CY></Size>.
 */
irr::core::rect<irr::s32> spellbookMemorizeGaugeRect(const UILayout& layout);

/** Render spellbook popup (center screen). */
void renderSpellbookPopup(UIRenderer& ui, const UILayout& layout,
                          const SpellbookPopupState& state);

// Cached skill display entry
struct SkillDisplayItem {
    uint8_t skillId = 0;
    std::string name;
    uint32_t value = 0;
    uint32_t maxValue = 0;
};

// Skills popup state
struct SkillsPopupState {
    bool isOpen = false;
    std::vector<SkillDisplayItem> skills;
    int scrollOffset = 0;  // Lines scrolled from top
    int hoveredRow = -1;

    // T313: row metrics, hoisted out of renderSkillsPopup()'s body (where
    // they were file-local constexprs) because StaticUIInput had already
    // grown its own copy of the same two numbers for the row hit test. One
    // source, three readers -- the buff bar's T277 rule.
    static constexpr int LINE_H = 18;
    static constexpr int ROW_PAD = 2;

    // Highest legal scrollOffset for a window `visibleRows` tall. Unlike
    // the chat panel and the loot popup, renderSkillsPopup() does NOT clamp
    // (it just starts at scrollOffset and stops at the end of the list), so
    // an unclamped writer would scroll the list clean off the panel. Every
    // T313 writer goes through this.
    int maxScrollOffset(int visibleRows) const {
        int over = static_cast<int>(skills.size()) - visibleRows;
        return over > 0 ? over : 0;
    }
};

/** Render skills popup (center screen). */
void renderSkillsPopup(UIRenderer& ui, const UILayout& layout,
                        const SkillsPopupState& state);

/**
 * T313 -- how many skill rows the popup shows at once, from the layout
 * alone. renderSkillsPopup()'s row loop, the row hit test and the scroll
 * clamp all ask this rather than recomputing contentH / (LINE_H + ROW_PAD).
 */
int skillsVisibleRows(const UILayout& layout);

// ---------------------------------------------------------------------------
// T290 -- the loot window, restored.
//
// The pre-static-UI LootWindow (include/client/graphics/ui/loot_window.h +
// .cpp, deleted by U07c2 0f25cb57) is the ancestor of everything below: the
// same open/close state machine, the same "display row -> CORPSE SLOT"
// indirection, the same Loot All / Close buttons, the same scrolled visible
// window over a growing item list. What changed is the shell -- a
// WindowBase subclass with callbacks became a plain state struct plus free
// functions, the static-panel idiom (T272's spellbook, T277's buff bar).
//
// Two rules carried over verbatim because they were the parts that were
// RIGHT:
//   1. The list is PACKED. A corpse's slots are not dense (slot 0..30 with
//      holes), so display row N is NOT corpse slot N -- exactly T277's
//      buff-bar lesson. Every row carries its own corpse slot and that is
//      what LootItemIntent sends.
//   2. The UI never closes itself. close() went through a callback so the
//      SERVER decided; here the click pushes CloseLootIntent and the panel
//      stays up until LootWindowClosed comes back. Optimistic local closes
//      desync the server's "this player is looting" state.
// ---------------------------------------------------------------------------

// One row of corpse contents (from a LootItemAdded bridge event).
struct LootItemDisplay {
    uint8_t slot = 0;        // CORPSE slot id -- what LootItemIntent carries
    uint32_t itemId = 0;
    std::string itemName;
    // T430 (board B376): the dragitem icon id. Zero means "no art" and the
    // row falls back to the name alone, exactly as it drew before T430.
    uint32_t iconId = 0;
};

// Renderer-side corpse contents. Fed exclusively by the four loot bridge
// arms via the apply*() functions below; there is no cross-thread read of
// the game-thread loot state (U05/S2's rule).
struct LootPanelState {
    bool isOpen = false;
    uint16_t corpseId = 0;
    std::string corpseName;
    std::vector<LootItemDisplay> items;  // packed, ascending corpse slot
    int scrollOffset = 0;                // index of the first visible row
    int hoveredRow = -1;                 // visible row index, -1 = none
    static constexpr int VISIBLE_ROWS = 8;

    int maxScrollOffset() const {
        int over = static_cast<int>(items.size()) - VISIBLE_ROWS;
        return over > 0 ? over : 0;
    }
};

// The loot popup's two buttons (the old window's DestroyAll is gone with
// DestroyAllLootIntent -- T268 established Titanium has no destroy-loot
// packet, so that button only ever closed the window).
enum class LootButton : uint8_t {
    None = 0,
    LootAll,
    Close
};

/** Render the loot popup (center screen). Only when state.isOpen. */
void renderLootPopup(UIRenderer& ui, const UILayout& layout,
                     const LootPanelState& state);

/**
 * Hit-test a point against the loot popup's item rows and return the INDEX
 * INTO state.items (not the corpse slot, not the on-screen row), or -1.
 *
 * The twin of renderLootPopup()'s row loop -- same geometry, same packing,
 * same scroll window -- so the row the cursor is over and the item the
 * click takes can never disagree (T277's rule). Callers turn the index into
 * a corpse slot via state.items[i].slot.
 */
int hitTestLootRow(const UILayout& layout, const LootPanelState& state,
                   int mouseX, int mouseY);

/** Hit-test the loot popup's buttons. Returns LootButton::None on a miss. */
LootButton hitTestLootButton(const UILayout& layout, const LootPanelState& state,
                             int mouseX, int mouseY);

/**
 * The four loot bridge arms, as free functions.
 *
 * IrrlichtBridge's arms were LOG_TRACE stubs that DISCARDED their payloads
 * (T283's audit, irrlicht_bridge.cpp:455-467) -- which is why no corpse
 * contents existed renderer-side at all. These are the bodies those arms
 * now call. Free functions, like applyHotbarSlotAssigned(), so tests drive
 * the real publisher through the real applier without an Irrlicht device.
 */
void applyLootWindowOpened(LootPanelState& state,
                           const eqt::state::LootWindowOpenedData& data);
void applyLootWindowClosed(LootPanelState& state,
                           const eqt::state::LootWindowClosedData& data);
void applyLootItemAdded(LootPanelState& state,
                        const eqt::state::LootItemAddedData& data);
void applyLootItemRemoved(LootPanelState& state,
                          const eqt::state::LootItemRemovedData& data);

// Cached XP progress
struct XPBarState {
    float progress = 0.0f;  // 0.0 to 1.0
};

/** Render XP bar (full width, bottom of screen). */
void renderXPBar(UIRenderer& ui, const UILayout& layout,
                 const XPBarState& state);

} // namespace Graphics
} // namespace EQT
