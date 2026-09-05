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

#include "client/hotbar/hotbar_slot_type.h"  // T156

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

/** Render target info panel (HP bar + name). Only renders if target exists. */
void renderTargetInfo(UIRenderer& ui, const UILayout& layout,
                      const TargetInfoData& target);

// Cached chat panel state
struct ChatPanelState {
    eqt::ui::ChatMessageBuffer* messageBuffer = nullptr;
    int scrollOffset = 0;      // Lines scrolled up from bottom (0 = at bottom)
    bool showTimestamps = false;
    bool inputActive = false;  // Whether chat input field is focused
    bool mouseHover = false;   // Whether mouse is over chat panel
    std::string inputText;     // Current input field text
    int cursorPos = 0;
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

// Popup types for center screen area
enum class PopupType : uint8_t {
    None = 0,
    Inventory,
    Spellbook,
    Vendor,
    Bank,
    Loot,
    Trade,
    Skills,
    Options
};

// A single inventory slot's visible state
struct SlotDisplayInfo {
    bool hasItem = false;
    std::string itemName;       // Short name for display
    uint32_t iconId = 0;        // Item icon ID (for future icon atlas)
    int32_t quantity = 1;       // Stack count
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
};

/** Render inventory popup (equipment + general slots). Only when activePopup == Inventory. */
void renderInventoryPopup(UIRenderer& ui, const UILayout& layout,
                          const InventoryPanelState& inv);

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
    uint32_t memorizeTotalMs = 0;
    std::chrono::steady_clock::time_point lastUpdateTime;
};

struct SpellGemPanelState {
    static constexpr int GEM_COUNT = 8;
    SpellGemState gems[8];
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
struct CastingBarState {
    bool isCasting = false;
    std::string spellName;
    uint32_t castTimeMs = 0;
    std::chrono::steady_clock::time_point castStartTime;

    float getProgress() const {
        if (!isCasting || castTimeMs == 0) return 0.0f;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - castStartTime).count();
        return std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(castTimeMs));
    }
};

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

    int pageCount() const {
        return spells.empty() ? 1 : static_cast<int>((spells.size() + SPELLS_PER_PAGE - 1) / SPELLS_PER_PAGE);
    }
};

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
