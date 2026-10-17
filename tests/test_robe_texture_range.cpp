/**
 * B1084 (#404): the ONE robe predicate, table-tested against the measured
 * fact.
 *
 * The measured range is 10..16 (seven robe materials), not the old 11..16.
 * Evidence (refactor/B1084/README.md):
 *  - global_chr.s3d carries exactly seven robe texture groups, clk04..clk10
 *    (six pages each: clk0401..clk0406 .. clk1001..clk1006). A range of 11..16
 *    (six materials) would leave clk04 with no material and material 10 with
 *    no texture; the seven groups only partition cleanly across 10..16.
 *  - globaldaf_chr.s3d carries dafch10xx..dafch16xx body textures (materials
 *    10-16) and nothing for 05-09.
 *  - The peq items table's robe-named chest items exist for exactly materials
 *    10,11,12,13,14,15,16.
 *  - The official client draws the robe body mesh + texture for material 10
 *    (Testcaster, Crimson Robe of Alendine, item 11641) -- the oracle still.
 *
 * This test is the regression net for a SECOND spelling of the range: any
 * consumer that stops going through isRobeTexture() and hard-codes its own
 * boundary diverges from the table here.
 */
#include <gtest/gtest.h>

#include "client/graphics/eq/equipment_textures.h"

using EQT::Graphics::TextureType;
using EQT::Graphics::getRobeClkTextureNumber;
using EQT::Graphics::getRobeIndex;
using EQT::Graphics::isNPCOnlyTexture;
using EQT::Graphics::isPlayerRaceTexture;
using EQT::Graphics::isRobeTexture;

namespace {

// The measured table, row by row, for the ids the client ever classifies.
// 0..4: naked + three armor textures. 5..9: NPC-only. 10..16: robes (clk04..clk10).
// 17..23: Velious. 240: VeliousHelmets.
struct Expectation {
    uint8_t id;
    bool robe;
    bool npcOnly;
    bool playerRace;
    uint8_t robeIndex;     // 0 outside the robe range
    uint8_t robeClkNumber; // 0 outside the robe range
};

Expectation expect(uint8_t id, bool robe = false, bool npc = false,
                   bool player = false, uint8_t idx = 0, uint8_t clk = 0) {
    return {id, robe, npc, player, idx, clk};
}

}  // namespace

// Every id in 0..24 plus 240, checked against the measured table.
TEST(RobeTextureRange, WholeTable0To24And240) {
    Expectation table[] = {
        expect(0, false, false, true),    // Naked (skin)
        expect(1, false, false, true),    // Leather
        expect(2, false, false, true),    // Chain
        expect(3, false, false, true),    // Plate
        expect(4, false, false, true),    // (race texture 4)
        expect(5, false, true, false),    // NPC-only
        expect(6, false, true, false),
        expect(7, false, true, false),
        expect(8, false, true, false),
        expect(9, false, true, false),
        // --- robes 10..16 (clk04..clk10), the B1084 correction ---
        expect(10, true, false, true, 1, 4),
        expect(11, true, false, true, 2, 5),
        expect(12, true, false, true, 3, 6),
        expect(13, true, false, true, 4, 7),
        expect(14, true, false, true, 5, 8),
        expect(15, true, false, true, 6, 9),
        expect(16, true, false, true, 7, 10),
        // --- Velious 17..23 ---
        expect(17, false, false, true),
        expect(18, false, false, true),
        expect(19, false, false, true),
        expect(20, false, false, true),
        expect(21, false, false, true),
        expect(22, false, false, true),
        expect(23, false, false, true),
        expect(24, false, false, false),
        // 240: VeliousHelmets, a player race texture
        expect(240, false, false, true),
    };

    for (const auto& e : table) {
        SCOPED_TRACE("id " + std::to_string(e.id));
        EXPECT_EQ(isRobeTexture(e.id), e.robe)
            << "isRobeTexture(" << e.id << ")";
        EXPECT_EQ(isNPCOnlyTexture(e.id), e.npcOnly)
            << "isNPCOnlyTexture(" << e.id << ")";
        EXPECT_EQ(isPlayerRaceTexture(e.id), e.playerRace)
            << "isPlayerRaceTexture(" << e.id << ")";
        EXPECT_EQ(getRobeIndex(e.id), e.robeIndex)
            << "getRobeIndex(" << e.id << ")";
        EXPECT_EQ(getRobeClkTextureNumber(e.id), e.robeClkNumber)
            << "getRobeClkTextureNumber(" << e.id << ")";
    }
}

// The load-bearing boundary: material 10 is a robe (the naked-body bug),
// and the two predicates that used to disagree now agree.
TEST(RobeTextureRange, Material10IsARobe) {
    EXPECT_TRUE(isRobeTexture(10))
        << "material 10 (Crimson Robe of Alendine) must be a robe; the "
           "official client draws the robe mesh + texture for it";
    EXPECT_FALSE(isNPCOnlyTexture(10))
        << "10 is not NPC-only; it is the first robe material";
    EXPECT_EQ(getRobeClkTextureNumber(10), 4)
        << "material 10 is the plain robe, clk04";
    EXPECT_EQ(getRobeClkTextureNumber(16), 10)
        << "material 16 is the last robe, clk10";
    EXPECT_EQ(getRobeIndex(10), 1);
    EXPECT_EQ(getRobeIndex(16), 7);
}

// The full 256-id space must be mutually exclusive: no id is both a robe and
// NPC-only, and the robe/NPC/player sets do not overlap where they must not.
TEST(RobeTextureRange, NoOverlapAcrossAll256) {
    for (unsigned id = 0; id < 256; ++id) {
        if (isRobeTexture(id)) {
            EXPECT_FALSE(isNPCOnlyTexture(id))
                << "id " << id << " is both a robe and NPC-only";
        }
        // Robes are exactly 10..16, so nothing outside that window is a robe.
        const bool inWindow = id >= 10 && id <= 16;
        EXPECT_EQ(isRobeTexture(id), inWindow)
            << "id " << id << " robe predicate outside the measured 10..16";
    }
}
