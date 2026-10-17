/*
 * UILayoutTable -- B1075 (#395): WHERE every static window sits, and which
 * windows must be able to be open at the same time.
 *
 * Will's ruling (docs/decisions.md 2026-10-15, "UI style: the oracle governs a
 * window's CONTENT, a fable layout table governs its PLACEMENT"; layout shape
 * confirmed on #395 note 5180): the ORACLE governs what a window contains and
 * how it behaves; THIS table governs its anchor, its orientation and the
 * REQUIRED CO-OPEN SETS. The layout copies the general World-of-Warcraft
 * arrangement: hotbar bottom-centre 1x10, chat bottom-left, the player frame
 * top-left with target, group and pet under it and the buffs beside it, and
 * the centre popups side by side in the middle band (the inventory on the
 * right, the window it is used WITH -- merchant, loot, bank, trade/give -- on
 * its left).
 *
 * Windows keep the size their own layout header gives them (the oracle's,
 * or the T415 scale onto the 400x350 popup); only the ANCHOR comes from here.
 *
 * This header is data only and depends on nothing in UILayout, so ui_layout.h
 * can include it and UILayout::computeLayout() can read it. The rect each row
 * resolves to on a given screen is `windowRectFor()` (ui_window_rects.h), and
 * tests/test_ui_layout_coopen.cpp asserts every set below at every resolution
 * in kSupportedResolutions.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace EQT {
namespace Graphics {

enum class UIWindow : uint8_t {
    // The always-on HUD.
    PlayerStatus,
    Target,
    Group,
    Pet,
    Buffs,
    Chat,
    Hotbar,
    SpellGems,
    CastingBar,
    XpBar,
    CombatAbilities,
    // The centre band.
    Inventory,
    Spellbook,
    Skills,
    Tradeskill,
    Merchant,
    Loot,
    Bank,
    Trade,
    Give,
    // Their own placement.
    Container,
    ItemDisplay,
    Options,
    Quantity,
    Confirmation,
    Trainer,
    Tracking,
    Tip,
    Friends,
    Count
};

/** The named anchors. Each row of kUILayoutTable names exactly one. */
enum class UIAnchor : uint8_t {
    TopLeft,              // the player frame, (MARGIN, MARGIN)
    UnderPlayerColumn,    // stacked under the player frame: target, group, pet
    BesidePlayerTop,      // the top strip right of the player frame (buffs)
    BottomLeft,           // chat
    BottomCentre,         // the hotbar: centred, clear of the chat, oracle's 30px bottom gap
    AboveHotbar,          // the casting bar, centred over the hotbar
    RightEdgeAboveHotbar, // the spell gem column (ends at the hotbar's top)
    FullWidthBottom,      // the XP bar
    LeftOfGems,           // combat abilities: left of the gem column, under the target's row
    CentreSlot,           // the one-at-a-time centre popups, centred in the middle band
    PairRight,            // the inventory: right half of the side-by-side pair
    PairLeft,             // the window used WITH the inventory: left half, right-aligned to it
    ScreenCentre,         // centred on the screen (dialogs and the oracle's Style_Sizable false windows)
    OracleAbsolute,       // the oracle's own RelativePosition false <Location>
    OracleDefaultsIni,    // defaults.ini's per-resolution position table (friends)
    OracleStacked,        // the oracle's (350,100), stacked per open bag (containers)
    RightEdgeScaled       // flush right at the measured y of 1024x768, scaled (tips)
};

struct UILayoutRow {
    UIWindow window;
    const char* name;
    UIAnchor anchor;
    // 'H' HUD (always-on), 'C' centre popup (one centre slot or the pair),
    // 'O' own placement. The 'C' rows are the ones the pre-B1075 rule made
    // mutually exclusive; closesOnOpen() below is what replaced it.
    char role;
};

inline constexpr UILayoutRow kUILayoutTable[] = {
    {UIWindow::PlayerStatus,    "player status",    UIAnchor::TopLeft,              'H'},
    {UIWindow::Target,          "target",           UIAnchor::UnderPlayerColumn,    'H'},
    {UIWindow::Group,           "group",            UIAnchor::UnderPlayerColumn,    'H'},
    {UIWindow::Pet,             "pet",              UIAnchor::UnderPlayerColumn,    'H'},
    {UIWindow::Buffs,           "buffs",            UIAnchor::BesidePlayerTop,      'H'},
    {UIWindow::Chat,            "chat",             UIAnchor::BottomLeft,           'H'},
    {UIWindow::Hotbar,          "hotbar",           UIAnchor::BottomCentre,         'H'},
    {UIWindow::SpellGems,       "spell gems",       UIAnchor::RightEdgeAboveHotbar, 'H'},
    {UIWindow::CastingBar,      "casting bar",      UIAnchor::AboveHotbar,          'H'},
    {UIWindow::XpBar,           "xp bar",           UIAnchor::FullWidthBottom,      'H'},
    {UIWindow::CombatAbilities, "combat abilities", UIAnchor::LeftOfGems,           'H'},
    {UIWindow::Inventory,       "inventory",        UIAnchor::PairRight,            'C'},
    {UIWindow::Spellbook,       "spellbook",        UIAnchor::CentreSlot,           'C'},
    {UIWindow::Skills,          "skills",           UIAnchor::CentreSlot,           'C'},
    {UIWindow::Tradeskill,      "tradeskill",       UIAnchor::CentreSlot,           'C'},
    {UIWindow::Merchant,        "merchant",         UIAnchor::PairLeft,             'C'},
    {UIWindow::Loot,            "loot",             UIAnchor::PairLeft,             'C'},
    {UIWindow::Bank,            "bank",             UIAnchor::PairLeft,             'O'},
    {UIWindow::Trade,           "trade",            UIAnchor::PairLeft,             'O'},
    {UIWindow::Give,            "give",             UIAnchor::PairLeft,             'O'},
    {UIWindow::Container,       "container",        UIAnchor::OracleStacked,        'O'},
    {UIWindow::ItemDisplay,     "item display",     UIAnchor::OracleAbsolute,       'O'},
    {UIWindow::Options,         "options",          UIAnchor::ScreenCentre,         'O'},
    {UIWindow::Quantity,        "quantity",         UIAnchor::OracleAbsolute,       'O'},
    {UIWindow::Confirmation,    "confirmation",     UIAnchor::ScreenCentre,         'O'},
    {UIWindow::Trainer,         "trainer",          UIAnchor::ScreenCentre,         'O'},
    {UIWindow::Tracking,        "tracking",         UIAnchor::ScreenCentre,         'O'},
    {UIWindow::Tip,             "tip",              UIAnchor::RightEdgeScaled,      'O'},
    {UIWindow::Friends,         "friends",          UIAnchor::OracleDefaultsIni,    'O'},
};

inline constexpr std::size_t kUILayoutRowCount =
    sizeof(kUILayoutTable) / sizeof(kUILayoutTable[0]);
static_assert(kUILayoutRowCount == static_cast<std::size_t>(UIWindow::Count),
              "every UIWindow has exactly one row");

constexpr const UILayoutRow& layoutRow(UIWindow w) {
    return kUILayoutTable[static_cast<std::size_t>(w)];
}

constexpr bool rowsAreInEnumOrder() {
    for (std::size_t i = 0; i < kUILayoutRowCount; ++i) {
        if (static_cast<std::size_t>(kUILayoutTable[i].window) != i) return false;
    }
    return true;
}
static_assert(rowsAreInEnumOrder(), "kUILayoutTable is indexed by UIWindow");

// ---------------------------------------------------------------------------
// Hotbar orientation (ruling item 3): the button ARRANGEMENT is a table value.
// The first orientation whose window fits beside the chat on the screen is
// used; 1x10 is the layout, the oracle's 2x5 the narrow-screen fallback (it is
// also what HotbarLayout's oracle test pins). Reordering this list is the
// whole of "Will can override the orientation in the table".
// ---------------------------------------------------------------------------
inline constexpr int kHotbarOrientations[] = {10, 2};  // columns

// ---------------------------------------------------------------------------
// The REQUIRED CO-OPEN SETS. No two members of a set may intersect, and every
// member must be wholly on screen, at every supported resolution
// (tests/test_ui_layout_coopen.cpp).
// ---------------------------------------------------------------------------
inline constexpr int kMaxSetMembers = 8;

struct CoOpenSet {
    const char* name;
    int count;
    UIWindow members[kMaxSetMembers];
};

#define EQT_HUD_ALWAYS_ON UIWindow::Chat, UIWindow::Hotbar, UIWindow::Target, \
    UIWindow::Group, UIWindow::Buffs, UIWindow::PlayerStatus

inline constexpr CoOpenSet kRequiredCoOpenSets[] = {
    {"inventory+trade",     2, {UIWindow::Inventory, UIWindow::Trade}},
    {"inventory+give",      2, {UIWindow::Inventory, UIWindow::Give}},
    {"inventory+bank",      2, {UIWindow::Inventory, UIWindow::Bank}},
    {"inventory+merchant",  2, {UIWindow::Inventory, UIWindow::Merchant}},
    {"inventory+loot",      2, {UIWindow::Inventory, UIWindow::Loot}},
    {"spellbook+spell gems", 2, {UIWindow::Spellbook, UIWindow::SpellGems}},
    // Each one-at-a-time centre popup with the always-on HUD.
    {"inventory+HUD",       7, {UIWindow::Inventory, EQT_HUD_ALWAYS_ON}},
    {"spellbook+HUD",       7, {UIWindow::Spellbook, EQT_HUD_ALWAYS_ON}},
    {"skills+HUD",          7, {UIWindow::Skills, EQT_HUD_ALWAYS_ON}},
    {"tradeskill+HUD",      7, {UIWindow::Tradeskill, EQT_HUD_ALWAYS_ON}},
    // The HUD itself never overlaps.
    {"HUD",                 8, {EQT_HUD_ALWAYS_ON, UIWindow::Pet, UIWindow::SpellGems}},
    {"HUD+casting bar",     7, {EQT_HUD_ALWAYS_ON, UIWindow::CastingBar}},
};

#undef EQT_HUD_ALWAYS_ON

inline constexpr std::size_t kRequiredCoOpenSetCount =
    sizeof(kRequiredCoOpenSets) / sizeof(kRequiredCoOpenSets[0]);

struct Resolution {
    int w;
    int h;
};
inline constexpr Resolution kSupportedResolutions[] = {
    {800, 600}, {1024, 768}, {1280, 720}, {1920, 1080}};

/** True when some required set names both windows. */
constexpr bool inRequiredSet(UIWindow a, UIWindow b) {
    for (const auto& s : kRequiredCoOpenSets) {
        bool hasA = false, hasB = false;
        for (int i = 0; i < s.count; ++i) {
            hasA = hasA || s.members[i] == a;
            hasB = hasB || s.members[i] == b;
        }
        if (hasA && hasB) return true;
    }
    return false;
}

/**
 * THE replacement for "centre popups are mutually exclusive": opening
 * `opening` closes `other` only when both are centre popups (role 'C') and no
 * required set lets them be open together. Popups not in any set keep the
 * pre-B1075 behaviour (each opener still decides WHICH windows it closes;
 * this only answers whether it may).
 */
constexpr bool closesOnOpen(UIWindow opening, UIWindow other) {
    return opening != other && layoutRow(opening).role == 'C' &&
           layoutRow(other).role == 'C' && !inRequiredSet(opening, other);
}

static_assert(!closesOnOpen(UIWindow::Merchant, UIWindow::Inventory),
              "inventory+merchant is a required co-open set (the B1075 gap)");
static_assert(!closesOnOpen(UIWindow::Loot, UIWindow::Inventory),
              "inventory+loot is a required co-open set");
static_assert(closesOnOpen(UIWindow::Skills, UIWindow::Spellbook),
              "popups in no shared set keep the one-at-a-time rule");

}  // namespace Graphics
}  // namespace EQT
