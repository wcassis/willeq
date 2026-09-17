// T314 -- creature-restricted spells test the body type the SERVER sent,
// from ONE table.
//
// Two hand-rolled copies of EverQuest's body-type numbering were live in
// src/client/spell/ and both were wrong:
//
//   SpellManager::isValidCreatureTarget() -- local BT_* constants off by one
//   for every value: BT_HUMANOID = 0 (0 is "no data"), BT_UNDEAD = 2 (2 is
//   Lycanthrope), BT_GIANT = 3 (3 is Undead), BT_ANIMAL = 20 (20 is Luggald),
//   BT_PLANT = 24 (24 is Summoned), BT_DRAGON = 25 (25 is Plant).
//
//   SpellTypeProcessor::isValidTarget() -- a third, differently wrong set:
//   bare 21 for Plant (21 is Animal), bare 22 for Giant (22 is Insect), and
//   Undead / Summoned / Animal not checked AT ALL ("Would need body type
//   check - placeholder" -> return true), so those three accepted everything.
//   T324 (2026-09-02) DELETED that second consumer outright -- it had zero
//   callers and the server resolves spell targets -- so the three cases that
//   asserted against it are gone from this file. What remains covers the one
//   consumer a player can reach.
//
// Both now go through EQ::BodyTypeSatisfiesTargetType(), which spells its
// constants out of client/eq_body_types.h -- the T308 header transcribed from
// the server that stamps the value, eqemu common/bodytypes.h. Checked against
// that file in this unit: every value in the client header matches the
// server's, so nothing here needed the header changed.
//
// The accept sets are eqemu's own, Mob::DetermineSpellTargets()
// (zone/spells.cpp ~1936-1995):
//   Undead/UndeadAE  : Undead(3), SummonedUndead(8), Vampire(12)
//   Summoned/*AE     : Summoned(24), Summoned2(27), Summoned3(28)
//   Animal           : Animal(21) and nothing else -- Insect is NOT an animal
//   Plant            : Plant(25)
//   Giant            : Giant(4), with RaidGiant(9) folded in
//   Dragon           : Dragon(26), with VeliousDragon(30) folded in
//
// The REJECT rows are the point. A table that only proves each restriction
// accepts its own body type passes on the old code too, because the old code
// accepted far too much: Animal took Insect, Undead/Summoned/Animal in the
// processor took literally anything. Every accepted value below is paired
// with a neighbouring value that must be refused -- including the two the old
// code actually confused, Plant(25)/Insect(21) and Giant(4)/Monster(22).

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/eq_body_types.h"
#include "client/spell/spell_body_type_target.h"
#include "zone_stream_test_utils.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace zonetest;
using namespace EQ;
namespace BT = EQT::BodyType;

namespace {

struct BodyCase {
    const char* label;
    SpellTargetType target_type;
    uint8_t bodytype;
    bool expected;
};

// Accept AND reject rows, side by side, so no restriction can quietly widen.
const BodyCase kCases[] = {
    // --- Animal. The old SpellManager copy also took Insect.
    {"Animal spell / Animal(21)",            SpellTargetType::Animal,   BT::Animal,         true},
    {"Animal spell / Insect(22)",            SpellTargetType::Animal,   BT::Insect,         false},
    {"Animal spell / Luggald(20)",           SpellTargetType::Animal,   BT::Luggald,        false},
    {"Animal spell / Humanoid(1)",           SpellTargetType::Animal,   BT::Humanoid,       false},

    // --- Undead. The processor accepted everything here.
    {"Undead spell / Undead(3)",             SpellTargetType::Undead,   BT::Undead,         true},
    {"Undead spell / SummonedUndead(8)",     SpellTargetType::Undead,   BT::SummonedUndead, true},
    {"Undead spell / Vampire(12)",           SpellTargetType::Undead,   BT::Vampire,        true},
    {"Undead spell / Lycanthrope(2)",        SpellTargetType::Undead,   BT::Lycanthrope,    false},
    {"Undead spell / Giant(4)",              SpellTargetType::Undead,   BT::Giant,          false},
    {"Undead spell / Humanoid(1)",           SpellTargetType::Undead,   BT::Humanoid,       false},
    {"UndeadAE spell / Undead(3)",           SpellTargetType::UndeadAE, BT::Undead,         true},
    {"UndeadAE spell / Humanoid(1)",         SpellTargetType::UndeadAE, BT::Humanoid,       false},

    // --- Summoned.
    {"Summoned spell / Summoned(24)",        SpellTargetType::Summoned, BT::Summoned,       true},
    {"Summoned spell / Summoned2(27)",       SpellTargetType::Summoned, BT::Summoned2,      true},
    {"Summoned spell / Summoned3(28)",       SpellTargetType::Summoned, BT::Summoned3,      true},
    {"Summoned spell / Monster(23)",         SpellTargetType::Summoned, BT::Monster,        false},
    {"Summoned spell / Dragon2(29)",         SpellTargetType::Summoned, BT::Dragon2,        false},
    {"SummonedAE spell / Summoned(24)",      SpellTargetType::SummonedAE, BT::Summoned,     true},
    {"SummonedAE spell / Plant(25)",         SpellTargetType::SummonedAE, BT::Plant,        false},

    // --- Plant. The processor asked for 21, which is Animal.
    {"Plant spell / Plant(25)",              SpellTargetType::Plant,    BT::Plant,          true},
    {"Plant spell / Insect(22)",             SpellTargetType::Plant,    BT::Insect,         false},
    {"Plant spell / Animal(21)",             SpellTargetType::Plant,    BT::Animal,         false},
    {"Plant spell / Summoned(24)",           SpellTargetType::Plant,    BT::Summoned,       false},
    {"Plant spell / Dragon(26)",             SpellTargetType::Plant,    BT::Dragon,         false},

    // --- Giant. The processor asked for 22, which is Insect; the manager
    // asked for 3 (Undead) plus 8 (SummonedUndead, mislabelled "bane giant").
    {"Giant spell / Giant(4)",               SpellTargetType::Giant,    BT::Giant,          true},
    {"Giant spell / RaidGiant(9)",           SpellTargetType::Giant,    BT::RaidGiant,      true},
    {"Giant spell / Monster(23)",            SpellTargetType::Giant,    BT::Monster,        false},
    {"Giant spell / Insect(22)",             SpellTargetType::Giant,    BT::Insect,         false},
    {"Giant spell / Undead(3)",              SpellTargetType::Giant,    BT::Undead,         false},
    {"Giant spell / Construct(5)",           SpellTargetType::Giant,    BT::Construct,      false},

    // --- Dragon. 26 was the ONE value the processor had right.
    {"Dragon spell / Dragon(26)",            SpellTargetType::Dragon,   BT::Dragon,         true},
    {"Dragon spell / VeliousDragon(30)",     SpellTargetType::Dragon,   BT::VeliousDragon,  true},
    {"Dragon spell / Plant(25)",             SpellTargetType::Dragon,   BT::Plant,          false},
    {"Dragon spell / Summoned2(27)",         SpellTargetType::Dragon,   BT::Summoned2,      false},

    // --- Unrestricted target types ignore the body type entirely.
    {"Single spell / Humanoid(1)",           SpellTargetType::Single,   BT::Humanoid,       true},
    {"Single spell / Dragon(26)",            SpellTargetType::Single,   BT::Dragon,         true},
    {"Self spell / Unknown(0)",              SpellTargetType::Self,     BT::Unknown,        true},
};

std::vector<uint8_t> NpcWithBodyType(uint32_t id, const std::string& name, uint8_t bodytype) {
    return NewSpawnPayloadAt(id, name, 0, 10, 0, /*level=*/5, /*npc_type=*/1,
                             /*char_class=*/0, /*hp_percent=*/100, /*race=*/54, bodytype);
}

SpellData MakeSpell(SpellTargetType tt) {
    SpellData s;
    s.id = 1;
    s.name = "Test Spell";
    s.target_type = tt;
    s.mana_cost = 0;
    s.range = 0;
    s.cast_time_ms = 1000;
    return s;
}

}  // namespace

// ===================================================================
// The shared predicate
// ===================================================================

TEST(SpellBodyTypeTarget, AcceptAndRejectTable) {
    for (const BodyCase& c : kCases) {
        EXPECT_EQ(BodyTypeSatisfiesTargetType(c.target_type, c.bodytype, /*is_corpse=*/false),
                  c.expected)
            << c.label;
    }
}

TEST(SpellBodyTypeTarget, CorpseRestrictionUsesCorpseStateNotBodyType) {
    EXPECT_TRUE(BodyTypeSatisfiesTargetType(SpellTargetType::Corpse, BT::Humanoid, true));
    EXPECT_FALSE(BodyTypeSatisfiesTargetType(SpellTargetType::Corpse, BT::Humanoid, false));
}

TEST(SpellBodyTypeTarget, RestrictedTargetTypesAreExactlyTheOnesThatCanRefuse) {
    for (int raw = 0; raw <= 50; ++raw) {
        auto tt = static_cast<SpellTargetType>(raw);
        bool everRefuses = false;
        for (int body = 0; body <= 70; ++body) {
            if (!BodyTypeSatisfiesTargetType(tt, static_cast<uint8_t>(body), false)) {
                everRefuses = true;
                break;
            }
        }
        EXPECT_EQ(TargetTypeRestrictsCreature(tt), everRefuses)
            << "target type " << raw << " disagrees with its own accept set";
    }
}

// ===================================================================
// Consumer 1 -- SpellManager::isValidCreatureTarget()
// ===================================================================

TEST(SpellManagerBodyType, GiantSpellAcceptsGiantAndRejectsMonster) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(100, "a_hill_giant", BT::Giant)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(101, "a_monster", BT::Monster)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Giant);
    EXPECT_TRUE(mgr->isValidCreatureTarget(spell, 100));
    EXPECT_FALSE(mgr->isValidCreatureTarget(spell, 101))
        << "Monster(23) accepted by a giant-only spell";
}

TEST(SpellManagerBodyType, PlantSpellAcceptsPlantAndRejectsAnimal) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(100, "a_treant", BT::Plant)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(101, "a_wolf", BT::Animal)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Plant);
    EXPECT_TRUE(mgr->isValidCreatureTarget(spell, 100));
    EXPECT_FALSE(mgr->isValidCreatureTarget(spell, 101))
        << "Animal(21) accepted by a plant-only spell";
}

// The old BT_ANIMAL/BT_INSECT pair was {20, 21} -- Luggald and Animal. So the
// old code took Animal(21) by accident (under the name "insect") and took
// Luggald(20) outright, while Insect(22), the thing it thought it was
// allowing, was never in the set at all. eqemu ST_Animal wants Animal alone.
TEST(SpellManagerBodyType, AnimalSpellTakesAnimalOnlyNotLuggaldOrInsect) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(100, "a_wolf", BT::Animal)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(101, "a_luggald", BT::Luggald)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(102, "a_wasp", BT::Insect)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Animal);
    EXPECT_TRUE(mgr->isValidCreatureTarget(spell, 100));
    EXPECT_FALSE(mgr->isValidCreatureTarget(spell, 101))
        << "Luggald(20) accepted by an animal-only spell (the old BT_ANIMAL)";
    EXPECT_FALSE(mgr->isValidCreatureTarget(spell, 102))
        << "Insect(22) accepted by an animal-only spell";
}

// The user-reachable refusal, end to end through validateCast().
TEST(SpellManagerBodyType, ValidateCastRefusesUndeadSpellOnALivingHumanoid) {
    EverQuest eq = MakeEq();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(100, "a_guard", BT::Humanoid)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(101, "a_skeleton", BT::Undead)));
    auto* mgr = eq.GetSpellManager();

    SpellData spell = MakeSpell(SpellTargetType::Undead);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::InvalidTarget);
    EXPECT_EQ(mgr->validateCast(spell, 101), CastResult::Success);
}

// T486 (2026-09-14 audit, item 2): a corpse that was already lying there when
// we zoned in. eqemu's Corpse::FillSpawnStruct writes Spawn_Struct NPC=2 for
// EVERY corpse it spawns (zone/corpse.cpp), and no OP_Death / OP_MobRename
// ever follows for it -- those only fire at the moment of death. Before this
// unit the spawn parsers stored npc_type and left Entity::is_corpse false, so
// the three readers of the bare flag (validateCast's corpse-target check,
// the CorpseDecayStarted fade, the corpse-name suffix on appearance changes)
// saw a living mob: a cleric zoning in to rez a corpse it did not see die was
// refused client-side with InvalidTarget. The flag is now set from the NPC
// byte by the parser that reads it (IsCorpseNpcType, corpse_predicate.h).
TEST(SpellManagerCorpseTarget, APreExistingCorpseIsACorpseTargetWithoutAWitnessedDeath) {
    EverQuest eq = MakeEq();
    // The wire shape of a corpse already in the zone: NPC byte 2, the
    // Corpse::CalcCorpseName name, and nothing else to say it is dead.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
                               NewSpawnPayloadAt(100, "Oraclealpha's corpse42", 0, 10, 0,
                                                 /*level=*/5, /*npc_type=*/2,
                                                 /*char_class=*/0, /*hp_percent=*/0,
                                                 /*race=*/1, BT::Humanoid)));
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn, NpcWithBodyType(101, "a_guard", BT::Humanoid)));

    const auto& entities = eq.GetEntities();
    ASSERT_NE(entities.find(100), entities.end());
    EXPECT_TRUE(entities.at(100).is_corpse)
        << "the spawn parser stored npc_type=2 without setting is_corpse";
    EXPECT_FALSE(entities.at(101).is_corpse);

    auto* mgr = eq.GetSpellManager();
    SpellData spell = MakeSpell(SpellTargetType::Corpse);
    EXPECT_EQ(mgr->validateCast(spell, 100), CastResult::Success)
        << "a corpse we did not see die is refused as a corpse-spell target";
    EXPECT_EQ(mgr->validateCast(spell, 101), CastResult::InvalidTarget);
}

// ===================================================================
// The guard: the numbering has ONE home.
//
// Precedent: tests/test_duplicate_state_stores.cpp -- a source-scanning test
// so the invariant cannot drift away from the doc that states it. Fails on a
// reintroduced BT_* constant or on any body-type comparison against a bare
// integer anywhere under src/client/spell/ or include/client/spell/.
// ===================================================================

namespace {

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

std::vector<fs::path> SpellSources() {
    std::vector<fs::path> out;
    for (const char* dir : {"src/client/spell", "include/client/spell"}) {
        fs::path root = fs::path(WILLEQ_REPO_ROOT) / dir;
        if (!fs::exists(root)) continue;
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext == ".cpp" || ext == ".h" || ext == ".hpp") out.push_back(e.path());
        }
    }
    return out;
}

}  // namespace

TEST(SpellBodyTypeGuard, NoBTConstantOrBareBodyTypeIntegerSurvivesInSpellCode) {
    const std::vector<fs::path> files = SpellSources();
    ASSERT_FALSE(files.empty()) << "found no spell sources under " << WILLEQ_REPO_ROOT;

    // A BT_* identifier: the shape of both dead copies.
    const std::regex btConst(R"(\bBT_[A-Z0-9_]+\b)");
    // A body-type value compared against a bare integer literal, in either
    // operand order: `bodytype == 21`, `21 != x.bodytype`, `body_type >= 3`.
    const std::regex bareCmp(
        R"((\b[A-Za-z_][A-Za-z0-9_]*\.)?\bbody_?type\b\s*(==|!=|<=|>=|<|>)\s*[0-9]+)",
        std::regex::icase);
    const std::regex bareCmpRev(
        R"([0-9]+\s*(==|!=|<=|>=|<|>)\s*(\b[A-Za-z_][A-Za-z0-9_]*\.)?\bbody_?type\b)",
        std::regex::icase);

    std::ostringstream bad;
    for (const fs::path& f : files) {
        std::ifstream in(f);
        ASSERT_TRUE(in.is_open()) << f;
        std::string line;
        int n = 0;
        while (std::getline(in, line)) {
            ++n;
            // Comments are allowed to name the old wrong values -- that is how
            // the defect stays documented.
            std::string code = line;
            auto slashes = code.find("//");
            if (slashes != std::string::npos) code = code.substr(0, slashes);
            if (code.find_first_not_of(" \t*") == std::string::npos) continue;

            if (std::regex_search(code, btConst) ||
                std::regex_search(code, bareCmp) ||
                std::regex_search(code, bareCmpRev)) {
                bad << "  " << f.string() << ":" << n << ": " << line << "\n";
            }
        }
    }

    EXPECT_TRUE(bad.str().empty())
        << "body-type numbering re-entered src/client/spell/. Use\n"
        << "EQ::BodyTypeSatisfiesTargetType() / EQT::BodyType::* from\n"
        << "include/client/eq_body_types.h instead:\n" << bad.str();
}
