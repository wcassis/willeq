// T424 (board B351): the UI sprite atlas roster, pinned in one place.
//
// T416's census found six of the twenty-one `UISprite` regions with no
// consumer anywhere in src/, tools/ or tests/: `ButtonNormal`,
// `ButtonPressed`, `ButtonDisabled`, `PanelBorder`, `ScrollIndicator` and
// `SlotBorderSelected`. None of them is oracle art -- every one is a
// procedural gradient or a 1px border invented by `tools/ui_atlas_builder`
// for the WindowManager UI that was deleted in `0f25cb57`, and none can be
// wired to a caller without INVENTING a look (which is board B350's open
// question, not a defect fix): a 64x24 gradient stretched onto a button of
// another size, or a 32x32 one-pixel border stretched onto a 400px panel,
// draws chrome nobody measured against the official client. They cost atlas
// space and pack bytes for nothing, so T424 deleted them from the builder,
// the JSON name map and the enum together.
//
// The roster lives in four places that cannot be allowed to drift:
//   1. `UISprite` in include/client/graphics/ui/ui_atlas.h  (the enum)
//   2. `UIAtlas::mapSprite`'s nameMap in src/.../ui_atlas.cpp (JSON -> enum)
//   3. `generate()` + `generateCoins()` in tools/ui_atlas_builder.cpp (pixels)
//   4. the `ui_atlas.json` raw entry inside the shared willeq.eqe1
// A name in (1) with no (3) is a sprite that resolves to the empty rect and
// draws a degenerate quad; a name in (3) with no (1) is atlas space nothing
// can address. This test asserts all four agree, and that the six T424
// removed are gone from every one of them.
//
// File-scraping is deliberate: (3) is a tool this test does not link and (4)
// is a shared asset, so the only thing all four have in common is their text.

#include <gtest/gtest.h>

#include "client/graphics/eqe1_loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

const std::string kRoot = std::string(WILLEQ_SOURCE_DIR) + "/";
const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

// The roster, as JSON sprite names in atlas order. Fifteen since T424.
const std::vector<std::string> kRoster = {
    "slot_background",
    "slot_border_normal",
    "slot_border_hover",
    "bar_hp",
    "bar_mana",
    "bar_stamina",
    "bar_xp",
    "bar_casting",
    "bar_background",
    "panel_background",
    "white",
    "coin_platinum",
    "coin_gold",
    "coin_silver",
    "coin_copper",
};

// The six T424 removed (board B351). Named here so a re-addition has to argue
// with this list rather than quietly grow the atlas again.
const std::vector<std::string> kRetired = {
    "slot_border_selected",
    "button_normal",
    "button_pressed",
    "button_disabled",
    "panel_border",
    "scroll_indicator",
};

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The enum's identifier for a JSON sprite name: snake_case -> PascalCase.
std::string enumeratorFor(const std::string& jsonName) {
    std::string out;
    bool up = true;
    for (char c : jsonName) {
        if (c == '_') { up = true; continue; }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    // The two the mechanical rule gets wrong.
    if (out == "BarHp") return "BarHP";
    if (out == "BarXp") return "BarXP";
    return out;
}

}  // namespace

TEST(UISpriteRoster, TheEnumHoldsExactlyTheRoster) {
    const std::string hdr = slurp(kRoot + "include/client/graphics/ui/ui_atlas.h");
    ASSERT_FALSE(hdr.empty()) << "cannot read ui_atlas.h";

    for (const std::string& name : kRoster) {
        EXPECT_NE(hdr.find("    " + enumeratorFor(name) + ",\n"), std::string::npos)
            << name << " (" << enumeratorFor(name) << ") is not an enumerator of "
               "UISprite, so nothing can address its atlas region";
    }
    for (const std::string& name : kRetired) {
        EXPECT_EQ(hdr.find("    " + enumeratorFor(name) + ",\n"), std::string::npos)
            << enumeratorFor(name) << " is back in the UISprite enum. T424 removed "
               "it as an unconsumed procedural leftover (board B351); giving the "
               "UI real button/border chrome is board B350's oracle-art question, "
               "and it starts from window_pieces01.tga, not from this gradient.";
    }
}

TEST(UISpriteRoster, TheJsonNameMapHoldsExactlyTheRoster) {
    const std::string src = slurp(kRoot + "src/client/graphics/ui/ui_atlas.cpp");
    ASSERT_FALSE(src.empty()) << "cannot read ui_atlas.cpp";

    for (const std::string& name : kRoster) {
        EXPECT_NE(src.find("\"" + name + "\""), std::string::npos)
            << name << " has no entry in UIAtlas::mapSprite's nameMap, so the "
               "builder's region would be dropped with a warning and the sprite "
               "would draw as the empty rect";
    }
    for (const std::string& name : kRetired) {
        EXPECT_EQ(src.find("\"" + name + "\""), std::string::npos)
            << name << " is back in UIAtlas::mapSprite's nameMap (board B351)";
    }
}

TEST(UISpriteRoster, TheBuilderRegistersExactlyTheRoster) {
    const std::string src = slurp(kRoot + "tools/ui_atlas_builder.cpp");
    ASSERT_FALSE(src.empty()) << "cannot read ui_atlas_builder.cpp";

    for (const std::string& name : kRoster) {
        EXPECT_NE(src.find("\"" + name + "\""), std::string::npos)
            << name << " is not registered by tools/ui_atlas_builder";
    }
    for (const std::string& name : kRetired) {
        EXPECT_EQ(src.find("\"" + name + "\""), std::string::npos)
            << name << " is still drawn and registered by tools/ui_atlas_builder, "
               "so it costs atlas space and pack bytes with no consumer "
               "(board B351)";
    }
}

TEST(UISpriteRoster, ThePackedAtlasJsonHoldsExactlyTheRoster) {
    std::ifstream probe(kWilleqEqe1, std::ios::binary);
    if (!probe.good()) {
        GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
    }
    probe.close();

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1)) << "failed to open " << kWilleqEqe1;

    const uint8_t* data = nullptr;
    uint32_t size = 0;
    ASSERT_TRUE(loader.getRawData("ui_atlas.json", data, size))
        << "willeq.eqe1 has no 'ui_atlas.json' raw entry";
    const std::string json(reinterpret_cast<const char*>(data), size);

    for (const std::string& name : kRoster) {
        EXPECT_NE(json.find("\"" + name + "\""), std::string::npos)
            << name << " is missing from the shipped ui_atlas.json";
    }
    for (const std::string& name : kRetired) {
        EXPECT_EQ(json.find("\"" + name + "\""), std::string::npos)
            << name << " is still in the SHIPPED ui_atlas.json inside willeq.eqe1. "
               "The builder changed but the pack was not rebuilt (board B351): "
               "rerun scripts/build_willeq_pack.sh and update "
               "data/willeq.eqe1.sha256 in the same commit.";
    }
}

// The point of the whole exercise: every region the atlas ships is addressed
// by production code. `SlotBorderNormal` / `SlotBorderHover` reach drawSprite
// through a `borderSprite` local, and the coins through
// `InventoryLayout::coinSprite`, so the search is for the ENUMERATOR, not for
// a literal drawSprite call.
TEST(UISpriteRoster, EveryShippedSpriteHasAConsumer) {
    static const char* const kConsumerTUs[] = {
        "src/client/graphics/ui/static_panels.cpp",
        "src/client/graphics/ui/ui_renderer.cpp",
        "src/client/graphics/ui/tradeskill_panel.cpp",
        "src/client/graphics/ui/combat_ability_panel.cpp",
        "src/client/graphics/ui/loot_panel.cpp",
        "include/client/graphics/ui/inventory_layout.h",
    };
    std::string all;
    for (const char* tu : kConsumerTUs) all += slurp(kRoot + tu);
    ASSERT_FALSE(all.empty());

    for (const std::string& name : kRoster) {
        const std::string sym = "UISprite::" + enumeratorFor(name);
        // CoinGold/Silver/Copper are reached as CoinPlatinum + denom.
        if (name == "coin_gold" || name == "coin_silver" || name == "coin_copper") continue;
        EXPECT_NE(all.find(sym), std::string::npos)
            << sym << " ships in the atlas but no UI translation unit names it. "
               "That is the B351 shape: a region costing atlas space and pack "
               "bytes with nothing to draw it. Either wire it or delete it from "
               "the builder, the nameMap and the enum together.";
    }
}
