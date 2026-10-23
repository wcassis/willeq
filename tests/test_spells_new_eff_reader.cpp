/*
 * B1127 (#447): the official client draws spellsnew.eff, not spells.eff.
 *
 * Proven on the oracle (review/B1127/README.md):
 *   - 1656 Thunderbold and 14 Strike share spells.eff record 25 (GENR red
 *     crosses) but the oracle draws Thunderbold as FIRE (landing hue 44) and
 *     Strike as red crosses (hue 15): their field 145 differs (85
 *     InvokeLight85: spelcc/spelcf/spelce; 115: spelra).
 *   - 1899 Imbue Fire Opal has NO spells.eff record (field 123 = -1) and the
 *     oracle draws green 8-point stars (landing hue 106): field 145 = 83, Voc
 *     Earth V2, genb20.bmp / spelbb.tga.
 *   - 458 / 382 (field 145 = 121) draw gena20.bmp's cyan star (hue 197);
 *     GENA20_SPRITE's spela3.tga does not exist.
 *   - every GENI00 record of the B1130 survey draws soft violet flares
 *     (flare_blsp50a.png), never speli1's skulls.
 * These cases pin the reader's layout, the fields this unit USES (emitter
 * index per stage slot, the EDD texture and its frame count) and the
 * selector's routing. RED on e91342c6: none of this existed.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/spell_visual_selection.h"
#include "client/spell/spells_eff_reader.h"
#include "client/spell/spells_new_eff_reader.h"

using namespace EQ;

namespace {

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

class SpellsNewEffFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!std::ifstream(clientDir() + "/spellsnew.eff").good()) {
            GTEST_SKIP() << "spellsnew.eff not under " << clientDir();
        }
        ASSERT_TRUE(reader.load(clientDir())) << reader.loadError();
        ASSERT_TRUE(oldReader.load(clientDir() + "/spells.eff")) << oldReader.loadError();
    }
    SpellsNewEffReader reader;
    SpellsEffReader oldReader;
};

std::vector<std::string> names(const SpellVisualStage& st) {
    std::vector<std::string> out;
    for (const auto& n : st.sprite_names) {
        if (!n.empty()) out.push_back(n);
    }
    return out;
}

}  // namespace

TEST(SpellsNewEffReaderTest, MissingFilesFailToLoad) {
    SpellsNewEffReader r;
    EXPECT_FALSE(r.load("/nonexistent/dir"));
    EXPECT_FALSE(r.isLoaded());
    EXPECT_EQ(r.getRecord(82), nullptr);
    EXPECT_TRUE(r.stageTextures(82, 0).empty());
}

TEST_F(SpellsNewEffFileTest, TheShippedFilesHaveTheDecodedShape) {
    EXPECT_EQ(reader.recordCount(), 418u);    // 112,024 / 268
    EXPECT_EQ(reader.emitterCount(), 2118u);  // (881,096 - 8) / 416
    ASSERT_NE(reader.getRecord(121), nullptr);
    EXPECT_EQ(reader.getRecord(121)->name, "Wrath of Alkabor121");
    EXPECT_EQ(reader.getRecord(82)->name, "ElemMastery V2");
    // Field 145's "none" values and the out-of-range ones resolve to nothing.
    EXPECT_EQ(reader.getRecord(0), nullptr);
    EXPECT_EQ(reader.getRecord(-1), nullptr);
    EXPECT_EQ(reader.getRecord(3411), nullptr);
}

TEST_F(SpellsNewEffFileTest, EmitterTexturesAndFrameCounts) {
    // The padded name "gena20.bmp......." is trimmed; case is folded.
    ASSERT_NE(reader.getEmitter(552), nullptr);
    EXPECT_EQ(reader.getEmitter(552)->texture, "gena20.bmp");
    EXPECT_EQ(reader.getEmitter(552)->frames, 1);
    EXPECT_EQ(reader.getEmitter(291)->texture, "spelua.png");
    EXPECT_EQ(reader.getEmitter(291)->frames, 16);
    EXPECT_EQ(reader.getEmitter(322)->texture, "flare_blsp50a.png");
    EXPECT_EQ(reader.getEmitter(322)->frames, 4);
}

TEST_F(SpellsNewEffFileTest, StageTexturesOfTheGateSpells) {
    // 458 Fire Spiral of Al'Kabor: field 145 = 121, all gena20.
    EXPECT_EQ(reader.stageTextures(121, 0), std::vector<std::string>{"gena20.bmp"});
    EXPECT_TRUE(reader.stageTextures(121, 1).empty());
    EXPECT_EQ(reader.stageTextures(121, 2),
              (std::vector<std::string>{"gena20.bmp", "gena20.bmp", "gena20.bmp"}));
    // 54 Frost Bolt / 656 Shock of Ice: field 145 = 82.
    EXPECT_EQ(reader.stageTextures(82, 0),
              (std::vector<std::string>{"spelua.png", "spelua.png", "spelua.png", "spelua.png"}));
    EXPECT_EQ(reader.stageTextures(82, 2),
              (std::vector<std::string>{"spelua.png", "spelub.png", "spelub.png", "spelab.tga"}));
    // 1656 Thunderbold: field 145 = 85, fire.
    for (const auto& t : reader.stageTextures(85, 2)) {
        EXPECT_TRUE(t == "spelcc.png" || t == "spelcf.png") << t;
    }
    // A GENI00 record's spells (record 12 Lull: field 145 = 87) are flares.
    for (int stage : {0, 2}) {
        for (const auto& t : reader.stageTextures(87, stage)) EXPECT_EQ(t, "flare_blsp50a.png");
    }
}

TEST_F(SpellsNewEffFileTest, TheSelectorDrawsTheFieldFortyFiveRecord) {
    SpellVisualSelector selector(&oldReader, &reader);

    // 54 Frost Bolt: spells.eff record 23 (Source GENU00 = spelu1), field 145 = 82.
    const SpellVisualResolution frost = selector.resolve(23, 82);
    ASSERT_EQ(frost.source, SpellVisualSource::Resolved);
    EXPECT_EQ(frost.spell_affect_index, 23);  // still keys the B1131/B1132 anchors
    EXPECT_EQ(frost.spell_anim, 82);
    EXPECT_TRUE(frost.source_stage.from_spell_anim);
    EXPECT_EQ(names(frost.source_stage),
              (std::vector<std::string>{"spelua.png", "spelua.png", "spelua.png", "spelua.png"}));
    EXPECT_EQ(names(frost.target_stage),
              (std::vector<std::string>{"spelua.png", "spelub.png", "spelub.png", "spelab.tga"}));
    EXPECT_FALSE(frost.hasTravelStage());  // record 82 names no Middle emitter
    EXPECT_EQ(frost.target_stage.attach_tag, "Target");  // spells.eff's tag is kept

    // 458: GENA20 (spela3.tga, not shipped) everywhere in spells.eff; gena20.bmp drawn.
    const SpellVisualResolution spiral = selector.resolve(33, 121);
    EXPECT_EQ(names(spiral.source_stage), std::vector<std::string>{"gena20.bmp"});
    EXPECT_EQ(names(spiral.target_stage).size(), 3u);

    // 1656 Thunderbold vs 14 Strike: one spells.eff record, two arts.
    const SpellVisualResolution thunder = selector.resolve(25, 85);
    const SpellVisualResolution strike = selector.resolve(25, 115);
    EXPECT_NE(names(thunder.target_stage), names(strike.target_stage));
    for (const auto& t : names(strike.target_stage)) {
        EXPECT_TRUE(t == "spelra.png" || t == "spelx1.tga") << t;
    }

    // 1899 Imbue Fire Opal: no spells.eff record, yet the oracle draws it.
    const SpellVisualResolution imbue = selector.resolve(-1, 83);
    EXPECT_EQ(imbue.source, SpellVisualSource::Resolved);
    EXPECT_TRUE(imbue.hasSourceStage());
    for (const auto& t : names(imbue.source_stage)) {
        EXPECT_TRUE(t == "genb20.bmp" || t == "spelbb.tga") << t;
    }

    // Field 145 = 0 keeps the T72 spells.eff resolution untouched.
    const SpellVisualResolution plain = selector.resolve(23, 0);
    EXPECT_FALSE(plain.source_stage.from_spell_anim);
    EXPECT_EQ(plain.source_stage.sprite_names[0], "GENU00_SPRITE");
    // ...and so does a selector with no spellsnew reader.
    EXPECT_EQ(SpellVisualSelector(&oldReader).resolve(23, 82).source_stage.sprite_names[0],
              "GENU00_SPRITE");
}
