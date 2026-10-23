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
#include "spells_new_eff_anatomy.h"

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

// ===========================================================================
// B1144 (#464): the per-slot words (emitter, minLevel, anchor, attachPoint)
// and the client's slot gate (eqgame.exe 0x495cc3, docs/spellsnew_format.md).
// RED on fb6d6481: the words were exposed raw as b/actor/d and no gate existed.
// ===========================================================================

namespace {
using A = SpellsNewEffAnchor;
using P = SpellsNewEffAttach;
std::vector<int> indices(const std::vector<SpellsNewEffDrawnSlot>& v) {
    std::vector<int> out;
    for (const auto& d : v) out.push_back(d.index);
    return out;
}
}  // namespace

TEST(SpellsNewEffGate, AnNpcOrUnknownCasterCountsAsLevelOne) {
    EXPECT_EQ(SpellsNewEffGateLevel(true, false, 60), 60);
    EXPECT_EQ(SpellsNewEffGateLevel(true, true, 60), 1);
    EXPECT_EQ(SpellsNewEffGateLevel(false, false, 60), 1);
    SpellsNewEffSlot s{276, 24, 1, 1};
    EXPECT_FALSE(s.drawnAtLevel(23));
    EXPECT_TRUE(s.drawnAtLevel(24));
    EXPECT_FALSE((SpellsNewEffSlot{0, 0, 1, 1}).drawnAtLevel(60));  // no emitter
}

// Record 1 (BardResto24, 7 Hymn of Restoration) as the file holds it.
TEST_F(SpellsNewEffFileTest, RecordOneSlotWords) {
    const SpellsNewEffRecord* r = reader.getRecord(1);
    ASSERT_NE(r, nullptr);
    const auto& src = r->stages[kSpellsNewEffSource].slots;
    EXPECT_EQ(src[0].emitter, 359);
    EXPECT_EQ(src[0].minLevel, 24);
    EXPECT_EQ(src[0].anchorKind(), A::Caster);
    EXPECT_EQ(src[0].attach(), P::Chest);
    EXPECT_EQ(src[3].emitter, 362);
    EXPECT_EQ(src[3].minLevel, 0);
    EXPECT_EQ(src[3].attach(), P::RightHand);  // B1137 leg A: the glow at the right hand
    // Middle: one slot, LHAND -- the one cell record 93 differs in (RHAND).
    EXPECT_EQ(r->stages[kSpellsNewEffMiddle].slots[0].attach(), P::LeftHand);
    EXPECT_EQ(reader.getRecord(93)->stages[kSpellsNewEffMiddle].slots[0].attach(), P::RightHand);
    const auto& tgt = r->stages[kSpellsNewEffTarget].slots;
    for (const auto& s : tgt) EXPECT_EQ(s.anchorKind(), A::Target);
    EXPECT_EQ(tgt[2].attach(), P::Default);
    EXPECT_EQ(tgt[2].minLevel, 39);
    // The gate: a level-1 caster draws the Source's one level-0 slot.
    EXPECT_EQ(indices(reader.drawnSlots(1, kSpellsNewEffSource, 1)), std::vector<int>{3});
    EXPECT_EQ(indices(reader.drawnSlots(1, kSpellsNewEffSource, 24)), (std::vector<int>{0, 3}));
    EXPECT_EQ(indices(reader.drawnSlots(1, kSpellsNewEffSource, 60)), (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(indices(reader.drawnSlots(1, kSpellsNewEffTarget, 1)), std::vector<int>{0});
    EXPECT_EQ(reader.emitterSlotCount(1, kSpellsNewEffTarget), 4u);
}

// Record 93 (Breeze V2) is record 1 but for the Middle cell (B1137).
TEST_F(SpellsNewEffFileTest, RecordNinetyThreeIsRecordOneButForTheMiddleCell) {
    const auto* a = reader.getRecord(1);
    const auto* b = reader.getRecord(93);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    for (int st : {kSpellsNewEffSource, kSpellsNewEffTarget}) {
        for (size_t k = 0; k < 4; ++k) {
            const auto& x = a->stages[st].slots[k];
            const auto& y = b->stages[st].slots[k];
            EXPECT_EQ(x.emitter, y.emitter);
            EXPECT_EQ(x.minLevel, y.minLevel);
            EXPECT_EQ(x.anchor, y.anchor);
            EXPECT_EQ(x.attachPoint, y.attachPoint);
        }
    }
}

// Record 113 (HorrifyingApp V2, 290 / B1130 row 27): its landing is round the
// CASTER -- every Target emitter slot is anchor 1 (B1132 measured it so).
TEST_F(SpellsNewEffFileTest, RecordOneThirteenLandsOnTheCaster) {
    const auto slots = reader.drawnSlots(113, kSpellsNewEffTarget, 60);
    ASSERT_EQ(slots.size(), 3u);
    for (const auto& d : slots) EXPECT_EQ(d.slot.anchorKind(), A::Caster) << d.index;
    // At level 1 only its level-0 slot (index 1, DEFAULT) is drawn.
    EXPECT_EQ(indices(reader.drawnSlots(113, kSpellsNewEffTarget, 1)), std::vector<int>{1});
}

// Record 326 (Discipline - Blue; B1134's row 51): no Target stage at all, so
// the client draws no landing; its Source is four level-0 slots.
TEST_F(SpellsNewEffFileTest, RecordThreeTwentySixHasNoTargetStage) {
    for (int lv : {1, 60, 255}) EXPECT_TRUE(reader.drawnSlots(326, kSpellsNewEffTarget, lv).empty());
    EXPECT_EQ(reader.emitterSlotCount(326, kSpellsNewEffTarget), 0u);
    EXPECT_FALSE(reader.getRecord(326)->stages[kSpellsNewEffTarget].hasContent());
    const auto src = reader.drawnSlots(326, kSpellsNewEffSource, 1);
    ASSERT_EQ(src.size(), 4u);
    EXPECT_EQ(src[2].slot.attach(), P::LeftHand);
    EXPECT_EQ(src[3].slot.attach(), P::RightHand);
    EXPECT_EQ(src[0].texture, reader.getEmitter(1818)->texture);
}

// The 40 B1130 records: every Source / Target emitter slot carries the attach
// point and level (Source) or anchor and attach point (Target) B1141 listed in
// review/B1141/crosscheck.tsv -- hands as LHAND/RHAND, the halo as HEAD, the
// whole-body and ring records as CHEST/DEFAULT.
TEST_F(SpellsNewEffFileTest, TheSurveyRecordsAnatomyIsTheFiles) {
    const auto& rows = spells_new_eff_anatomy::Rows();
    ASSERT_EQ(rows.size(), 40u);
    int hands = 0, heads = 0;
    for (const auto& row : rows) {
        const auto src = reader.drawnSlots(row.f145, kSpellsNewEffSource, 255);
        ASSERT_EQ(src.size(), row.source.size()) << "f145 " << row.f145;
        for (size_t i = 0; i < src.size(); ++i) {
            EXPECT_EQ(src[i].slot.attachPoint, row.source[i].first) << "f145 " << row.f145 << " src " << i;
            EXPECT_EQ(src[i].slot.minLevel, row.source[i].second) << "f145 " << row.f145 << " src " << i;
            EXPECT_EQ(src[i].slot.anchorKind(), A::Caster) << "f145 " << row.f145;
            hands += src[i].slot.attach() == P::LeftHand || src[i].slot.attach() == P::RightHand;
            heads += src[i].slot.attach() == P::Head;
        }
        const auto tgt = reader.drawnSlots(row.f145, kSpellsNewEffTarget, 255);
        ASSERT_EQ(tgt.size(), row.target.size()) << "f145 " << row.f145;
        for (size_t i = 0; i < tgt.size(); ++i) {
            EXPECT_EQ(tgt[i].slot.anchor, row.target[i].first) << "f145 " << row.f145 << " tgt " << i;
            EXPECT_EQ(tgt[i].slot.attachPoint, row.target[i].second) << "f145 " << row.f145 << " tgt " << i;
        }
    }
    EXPECT_GT(hands, 0);
    EXPECT_GT(heads, 0);
    // B1130's head record (row 22 = f145 112): Source on HEAD; hands rows on both hands.
    const auto halo = reader.drawnSlots(112, kSpellsNewEffSource, 60);
    EXPECT_EQ(halo[0].slot.attach(), P::Head);
    EXPECT_EQ(halo[1].slot.attach(), P::Head);
    for (int f145 : {80, 86, 84, 98, 99, 100}) {  // B1130 class "hands": rows 0, 2, 18, 43, 42, 41
        bool l = false, r = false;
        for (const auto& d : reader.drawnSlots(f145, kSpellsNewEffSource, 60)) {
            l |= d.slot.attach() == P::LeftHand;
            r |= d.slot.attach() == P::RightHand;
        }
        EXPECT_TRUE(l && r) << f145;
    }
}

// All 418 records: the words the gate and the placement read take only the
// values the code handles. Source and Target slots are never on the missile
// (anchor 2 is the Middle stage's), and Source slots are never on the target.
TEST_F(SpellsNewEffFileTest, EveryRecordsSlotWordsAreInTheDecodedRanges) {
    size_t slots = 0;
    for (int32_t r = 1; r < static_cast<int32_t>(reader.recordCount()); ++r) {
        const auto* rec = reader.getRecord(r);
        ASSERT_NE(rec, nullptr);
        for (int st = 0; st < 3; ++st) {
            for (const auto& s : rec->stages[st].slots) {
                if (!s.hasEmitter()) continue;
                ++slots;
                EXPECT_TRUE(s.minLevel == 0 || s.minLevel == 1 || s.minLevel == 2 || s.minLevel == 24 ||
                            s.minLevel == 39) << r << " level " << s.minLevel;
                EXPECT_GE(s.anchor, 0);
                EXPECT_LE(s.anchor, 3);
                EXPECT_GE(s.attachPoint, 0);
                EXPECT_LE(s.attachPoint, 12);
                if (st == kSpellsNewEffSource) {
                    EXPECT_TRUE(s.anchorKind() == A::Caster || s.anchorKind() == A::World) << r;
                }
                if (st == kSpellsNewEffTarget) EXPECT_NE(s.anchorKind(), A::Missile) << r;
            }
        }
    }
    EXPECT_EQ(slots, 1128u + 33u + 1212u);  // B1137's census: Source 1128, Middle 33, Target 1212
}
