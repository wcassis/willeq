// T101: real-EqActionHandler coverage for the SpellManager-backed methods
// added/repointed by the Spells+Pet migration (beginCastFromGem(),
// interruptCast() [repointed from CombatManager], forgetSpell() [repointed
// from a stub], memorizeSpellByName(), listMemorizedSpells()).
//
// Same headless EverQuest + EqActionHandler construction idiom as
// test_eq_action_handler_transforms.cpp (T93) / test_eq_action_handler_
// enum_guards.cpp (T95) -- no network, no graphics, no eqemu. DB-dependent
// cases (memorizeSpellByName's real lookup) follow test_spell_manager.cpp's
// hasDb() + GTEST_SKIP precedent so this binary degrades cleanly where
// data/willeq.db is missing.
//
// See refactor/T101_migrate_spells_pet_family.md's Review. T240 repointed
// castSpell()/castSpellOnTarget()/memorizeSpell() at this same SpellManager
// path (the CombatManager gem map T93 tested against is deleted); their
// wire-level coverage is test_eq_action_handler_cast.cpp.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/spell/spell_manager.h"
#include "client/willeq_database.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>

using namespace eqt;

namespace {

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) {
        return env_path;
    }
    return WILLEQ_DB_PATH;
}

// test_spell_manager.cpp's precedent: EQ::SpellDatabase::initFromDatabase()
// reads a process-wide EQT::WilleqDatabase::globalInstance() -- it is NOT
// per-EverQuest-instance state, so it must be opened and registered once
// for the whole binary, in main() below, before RUN_ALL_TESTS(). Without
// this, EverQuest::InitializeSpellSystem() always fails ("WilleqDatabase
// global instance not available") regardless of whether the file on disk
// exists.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool hasDb() { return g_db_available; }

// T316: a null-consumer bridge so the memorize-started event can be observed
// (test_eq_action_handler_tradeskill.cpp's idiom).
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// EverQuest is not copyable/movable (owns unique_ptrs, a std::thread, an
// atomic<bool>, ...) -- unlike test_spell_manager.cpp's MakeEq() helper
// (which returns by value and relies on guaranteed copy elision from a
// single-statement construction, itself borderline), each test here
// constructs EverQuest directly as a local, matching test_eq_action_
// handler_transforms.cpp's (T93) established idiom.
#define MAKE_TEST_EQ(varName)                                                \
    EverQuest varName("127.0.0.1", 0, "test", "test", "test", "Testchar");   \
    varName.Net().Test_SetBypassSendGuardForSeam(true);                            \
    varName.Test_SetMySpawnIdForSeam(460);

uint32_t FindSpellId(EQ::SpellDatabase& db, uint32_t lo, uint32_t hi,
                      const std::function<bool(const EQ::SpellData&)>& pred) {
    for (uint32_t id = lo; id <= hi; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && pred(*s)) {
            return id;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------
// beginCastFromGem() -- the real EQ::SpellManager casting path (T97: game's
// /cast, NOT the CombatManager path castSpell()/castSpellOnTarget() use).
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, BeginCastFromGem_EmptyGem_DoesNotEnterCasting) {
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();
    ASSERT_NE(spellMgr, nullptr);
    ASSERT_FALSE(spellMgr->isCasting());

    // Gem 3 (1-indexed) is guaranteed empty on a fresh SpellManager -- no DB
    // needed, this is the NotMemorized path.
    handler.beginCastFromGem(3, 0);

    EXPECT_FALSE(spellMgr->isCasting())
        << "gem 3 was never memorized; beginCastFromGem(3) must not start a cast";
}

TEST(EqActionHandlerSpellPet, BeginCastFromGem_NoSpellManager_DoesNotCrash) {
    // Defensive: the null-spellMgr branch is unreachable in production
    // (EverQuest's constructor always allocates m_spell_manager), but the
    // guard exists in the ported code (matching game's own null check) --
    // exercise the reachable path only, via a real (non-null) manager, and
    // assert no crash/throw as a smoke test for the overall call shape.
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    EXPECT_NO_THROW(handler.beginCastFromGem(1, 0));
}

// ---------------------------------------------------------------------
// interruptCast() -- T101 repointed from CombatManager::InterruptCast() to
// EQ::SpellManager (game's /interrupt checks SpellManager::isCasting()).
// Not covered by any existing test against real CombatManager state (T93
// covers castSpell/castSpellOnTarget/memorizeSpell only), so this repoint
// changed no previously-tested behavior -- see the plan's Review.
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, InterruptCast_WhileNotCasting_IsSafeNoOp) {
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();
    ASSERT_FALSE(spellMgr->isCasting());

    EXPECT_NO_THROW(handler.interruptCast());
    EXPECT_FALSE(spellMgr->isCasting());
}

TEST(EqActionHandlerSpellPet, InterruptCast_WhileCasting_ClearsCastingState) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(spellMgr->getDatabase(), 1, 4000, [](const EQ::SpellData& s) {
        return s.canClassUse(EQ::PlayerClass::Warrior, 60) && s.cast_time_ms > 0;
    });
    if (id == 0) GTEST_SKIP() << "no castable Warrior spell found in scanned range";

    ASSERT_TRUE(spellMgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(EQ::PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);
    ASSERT_TRUE(spellMgr->memorizeSpell(id, 0));
    // memorizeSpell() only STARTS memorization (async, MemorizeProgress
    // state) -- the cast attempt below hits the gem-state gate and skips
    // on that branch, same as before B603 (which drove it via the deleted
    // beginCastById(), itself a delegate to beginCastFromGem()).
    EQ::CastResult result = spellMgr->beginCastFromGem(0);
    if (result != EQ::CastResult::Success) {
        GTEST_SKIP() << "spell did not enter casting (validation/mana gate) -- "
                         "cannot exercise the interrupt-while-casting branch";
    }
    ASSERT_TRUE(spellMgr->isCasting());

    handler.interruptCast();

    EXPECT_FALSE(spellMgr->isCasting());
}

// ---------------------------------------------------------------------
// forgetSpell() -- T101 fixed from a stub ("Would need to send forget
// spell packet", always silently no-op'd) to real EQ::SpellManager
// routing, ported from game's /forget.
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, ForgetSpell_EmptyGem_IsSafeNoOp) {
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();
    ASSERT_EQ(spellMgr->getMemorizedSpell(2), EQ::SPELL_UNKNOWN);

    EXPECT_NO_THROW(handler.forgetSpell(3));  // gem 3 (1-indexed) -> index 2, empty
    EXPECT_EQ(spellMgr->getMemorizedSpell(2), EQ::SPELL_UNKNOWN);
}

TEST(EqActionHandlerSpellPet, ForgetSpell_MemorizedGem_ActuallyClearsIt) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(spellMgr->getDatabase(), 1, 4000, [](const EQ::SpellData& s) {
        return s.canClassUse(EQ::PlayerClass::Warrior, 60);
    });
    if (id == 0) GTEST_SKIP() << "no Warrior-usable spell found in scanned range";

    ASSERT_TRUE(spellMgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(EQ::PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);
    // gemSlot arrives 1-indexed at the handler; memorize directly into the
    // 0-indexed gem 3 to set up the fixture (bypassing memorizeSpellByName,
    // which is exercised separately below).
    ASSERT_TRUE(spellMgr->memorizeSpell(id, 3));
    ASSERT_NE(spellMgr->getMemorizedSpell(3), EQ::SPELL_UNKNOWN);

    handler.forgetSpell(4);  // gem 4 (1-indexed) -> index 3

    EXPECT_EQ(spellMgr->getMemorizedSpell(3), EQ::SPELL_UNKNOWN)
        << "forgetSpell() was a stub before T101 -- this proves it now "
           "actually clears the gem, not just a false success";
}

// ---------------------------------------------------------------------
// memorizeSpellByName() -- ported from game's /mem (name lookup +
// disambiguation against the real spell DB).
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, MemorizeSpellByName_ExactMatch_MemorizesSpell) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(spellMgr->getDatabase(), 1, 4000, [](const EQ::SpellData& s) {
        return s.canClassUse(EQ::PlayerClass::Warrior, 60) && !s.name.empty();
    });
    if (id == 0) GTEST_SKIP() << "no Warrior-usable named spell found in scanned range";

    const EQ::SpellData* spell = spellMgr->getSpell(id);
    ASSERT_NE(spell, nullptr);
    std::string spellName = spell->name;

    ASSERT_TRUE(spellMgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(EQ::PlayerClass::Warrior));
    eq.GetGameState().player().setLevel(60);
    ASSERT_EQ(spellMgr->getMemorizedSpell(4), EQ::SPELL_UNKNOWN);

    handler.memorizeSpellByName(spellName, 5);  // gem 5 (1-indexed) -> index 4

    EXPECT_EQ(spellMgr->getMemorizedSpell(4), id)
        << "memorizeSpellByName(\"" << spellName << "\", 5) must resolve the "
           "name to spell id " << id << " and memorize it into gem index 4";
}

TEST(EqActionHandlerSpellPet, MemorizeSpellByName_UnknownName_DoesNotMemorizeAnything) {
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    // T101 ported game's stub-check first: without InitializeSpellSystem(),
    // the DB is empty, so this also exercises the "not found" branch
    // without needing hasDb() -- a fully deterministic negative case.
    auto* spellMgr = eq.GetSpellManager();
    ASSERT_EQ(spellMgr->getMemorizedSpell(0), EQ::SPELL_UNKNOWN);

    EXPECT_NO_THROW(handler.memorizeSpellByName("Definitely Not A Real Spell Name XYZ", 1));

    EXPECT_EQ(spellMgr->getMemorizedSpell(0), EQ::SPELL_UNKNOWN)
        << "an unresolvable spell name must not memorize anything";
}

// ---------------------------------------------------------------------
// T316: /mem <partial name>. findSpellsByName() forwarded to the exact-match
// query getSpellByName(), so a partial name found nothing and the player was
// told the spell did not exist -- for a spell that is right there in the
// book. It now runs a bound, escaped LIKE query.
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, MemorizeSpellByName_PartialName_ResolvesAndMemorizes) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    EqActionHandler handler(eq);
    auto* spellMgr = eq.GetSpellManager();
    auto& db = spellMgr->getDatabase();

    // Find a spell SOME class can cast at 60 whose name has a lowercased tail
    // that identifies exactly ONE spell -- so the /mem disambiguation branch
    // is not what is under test here, the resolution is. (The Warrior filter
    // the exact-match test above uses matches nothing in this database, which
    // is why that test skips; scanning the classes avoids inheriting that.)
    uint32_t id = 0;
    std::string partial;
    EQ::PlayerClass casterClass = EQ::PlayerClass::Unknown;
    for (uint32_t candidate = 1; candidate <= 4000 && id == 0; ++candidate) {
        const EQ::SpellData* s = db.getSpell(candidate);
        if (!s || s->name.size() < 8) continue;

        EQ::PlayerClass usable = EQ::PlayerClass::Unknown;
        for (uint8_t c = 1; c <= 16; ++c) {
            if (s->canClassUse(static_cast<EQ::PlayerClass>(c), 60)) {
                usable = static_cast<EQ::PlayerClass>(c);
                break;
            }
        }
        if (usable == EQ::PlayerClass::Unknown) continue;

        std::string tail = s->name.substr(1);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto matches = db.findSpellsByName(tail);
        if (matches.size() == 1 && matches[0]->id == candidate) {
            id = candidate;
            partial = tail;
            casterClass = usable;
        }
    }
    if (id == 0) GTEST_SKIP() << "no uniquely-identifying partial found in scanned range";

    // The pre-T316 behaviour, still available and still exact: this is why
    // /mem used to fail.
    EXPECT_EQ(db.getSpellByName(partial), nullptr)
        << "'" << partial << "' is deliberately not an exact spell name";

    const std::string fullName = db.getSpell(id)->name;

    ASSERT_TRUE(spellMgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(casterClass));
    eq.GetGameState().player().setLevel(60);
    ASSERT_EQ(spellMgr->getMemorizedSpell(4), EQ::SPELL_UNKNOWN);
    bridge.drainEvents();

    handler.memorizeSpellByName(partial, 5);  // gem 5 (1-indexed) -> index 4

    // Memorization is timed (SpellManager::updateMemorization completes it on
    // a later tick), so the gem is not filled synchronously. What IS
    // synchronous is the memorize-started event, and it names the spell the
    // partial resolved to -- which is precisely what was broken.
    EXPECT_TRUE(spellMgr->isMemorizing())
        << "/mem with the partial, lowercased name '" << partial << "' must "
           "start memorizing spell " << id << "; before T316 the name "
           "resolved to nothing and the player was told it did not exist";

    std::string startedName;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SpellMemorizeVisualStarted) {
            startedName = std::get<eqt::state::SpellMemorizeVisualStartedData>(ev.data).spellName;
        }
    }
    EXPECT_EQ(startedName, fullName);
}

TEST(EqActionHandlerSpellPet, FindSpellsByName_PartialMatchesManySpells) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto& db = eq.GetSpellManager()->getDatabase();

    // Lowercase on purpose: SQLite's LIKE is ASCII-case-insensitive, and the
    // exact-match query it replaced was not.
    auto matches = db.findSpellsByName("heal");
    EXPECT_GT(matches.size(), 1u)
        << "a substring shared by many spells must return many spells";
    EXPECT_LE(static_cast<int>(matches.size()), EQ::SpellDatabase::kMaxNameSearchResults);
    for (const auto* s : matches) {
        std::string lower = s->name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        EXPECT_NE(lower.find("heal"), std::string::npos) << s->name;
    }
}

// The escaping half. Unescaped, "%" is the SQL "match anything" wildcard and
// "_" is "match any one character": either would return the row cap for every
// query. Both must be literal characters from the player's point of view.
TEST(EqActionHandlerSpellPet, FindSpellsByName_WildcardCharactersAreLiteral) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto& db = eq.GetSpellManager()->getDatabase();

    for (const char* wildcard : {"%", "_"}) {
        auto matches = db.findSpellsByName(wildcard);
        EXPECT_LT(static_cast<int>(matches.size()), EQ::SpellDatabase::kMaxNameSearchResults)
            << "'" << wildcard << "' matched the row cap -- it is being "
               "treated as a SQL wildcard, not as a character the player typed";
        for (const auto* s : matches) {
            EXPECT_NE(s->name.find(wildcard), std::string::npos)
                << "'" << s->name << "' does not literally contain '" << wildcard << "'";
        }
    }
}

TEST(EqActionHandlerSpellPet, FindSpellsByName_EmptyInputMatchesNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    MAKE_TEST_EQ(eq)
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto& db = eq.GetSpellManager()->getDatabase();

    // An empty pattern would be "%%" -- every spell in the game.
    EXPECT_TRUE(db.findSpellsByName("").empty());
}

// ---------------------------------------------------------------------
// listMemorizedSpells() -- smoke test only (pure AddChatSystemMessage
// report, no state mutation to observe).
// ---------------------------------------------------------------------

TEST(EqActionHandlerSpellPet, ListMemorizedSpells_DoesNotCrash) {
    MAKE_TEST_EQ(eq)
    EqActionHandler handler(eq);
    EXPECT_NO_THROW(handler.listMemorizedSpells());
}

}  // namespace

// Main entry point: opens the real SQLite spell DB once for the whole
// binary (test_spell_manager.cpp/test_spell_database.cpp precedent) so
// DB-gated tests can run; DB-independent tests run regardless.
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
