// T404 Leg C (board B254): memorising a spell into an ALREADY-OCCUPIED gem
// left fable casting the OLD spell.
//
// MEASURED T401 (review/T401/ SS6.4): after #scribespells filled the bar,
// `#memspell 93 0` produced the correct pair of server responses --
//   MemorizeSpell response: slot=0 spell_id=467 scribing=2   (un-memorise)
//   MemorizeSpell response: slot=0 spell_id=93  scribing=1   (memorise)
// -- and `/cast 1` then sent "Sent CastSpell packet: spell=467, gem=0",
// "Beginning cast of 'Lightning Storm' (ID: 467)". A player clicking a gem
// got a different spell than its icon showed.
//
// The handler (ZoneComponent::OnMemorizeSpell, zone_dispatch.cpp) acted only
// on `scribing == 0` -- a scribe INTO THE SPELLBOOK. There was no arm for
// scribing 1 or 2 at all, so nothing the server said about a GEM ever reached
// SpellManager::m_spell_gems, which moved only on our own local memorise
// timer and on the PlayerProfile bulk load. It is not an ordering bug and not
// a guard bug: the arms did not exist.
//
// These tests replay the wire pair in T401's exact order.

#include <gtest/gtest.h>

#include "client/eq.h"
#include "client/spell/spell_manager.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace {

EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) return env_path;
    return WILLEQ_DB_PATH;
}

// MemorizeSpell_Struct: slot(4) spell_id(4) scribing(4) unknown0(4), after the
// 2-byte opcode. The handler requires >= 18 bytes.
std::vector<uint8_t> MemorizeSpellPayload(uint32_t slot, uint32_t spell_id,
                                          uint32_t scribing) {
    return zonetest::BuildPayload(18, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, slot);
        p.PutUInt32(6, spell_id);
        p.PutUInt32(10, scribing);
        p.PutUInt32(14, 0);
    });
}

// Two ids that exist in the shipped spell db. 467 = Lightning Storm and 93 are
// the two T401 measured; assert they resolve rather than picking blind.
constexpr uint32_t kOldSpell = 467;
constexpr uint32_t kNewSpell = 93;

class MemorizeIntoOccupiedGemTest : public ::testing::Test {
protected:
    MemorizeIntoOccupiedGemTest() : eq(zonetest::MakeEq()) {}

    void SetUp() override {
        if (!g_db_available) GTEST_SKIP() << "willeq.db not found";
        ASSERT_TRUE(eq.InitializeSpellSystem(""));
        mgr = eq.GetSpellManager();
        ASSERT_NE(mgr, nullptr);
        ASSERT_NE(mgr->getDatabase().getSpell(kOldSpell), nullptr) << kOldSpell;
        ASSERT_NE(mgr->getDatabase().getSpell(kNewSpell), nullptr) << kNewSpell;

        // The bar as #scribespells left it: gem 0 holds the old spell.
        uint32_t gems[EQ::MAX_SPELL_GEMS];
        for (auto& g : gems) g = EQ::SPELL_UNKNOWN;
        gems[0] = kOldSpell;
        mgr->setSpellGems(gems, EQ::MAX_SPELL_GEMS);
        ASSERT_EQ(mgr->getMemorizedSpell(0), kOldSpell);
    }

    void Replay(uint32_t slot, uint32_t spell_id, uint32_t scribing) {
        zonetest::ReplayZone(eq, HC_OP_MemorizeSpell,
                             MemorizeSpellPayload(slot, spell_id, scribing));
    }

    EverQuest eq;
    EQ::SpellManager* mgr = nullptr;
};

// The headline: the two packets in the order the server sent them.
TEST_F(MemorizeIntoOccupiedGemTest, UnmemoriseThenMemoriseLeavesTheNewSpell) {
    Replay(0, kOldSpell, 2);
    Replay(0, kNewSpell, 1);

    EXPECT_EQ(mgr->getMemorizedSpell(0), kNewSpell)
        << "gem 0 still holds " << mgr->getMemorizedSpell(0)
        << "; T401's /cast 1 sent spell=467 for exactly this reason";
    EXPECT_EQ(mgr->getGemState(0), EQ::GemState::Ready);
}

// A bare memorise into an occupied gem (no preceding un-memorise) must also
// land, so the fix cannot be "only replace what the un-memorise emptied".
TEST_F(MemorizeIntoOccupiedGemTest, MemoriseAloneOverwritesAnOccupiedGem) {
    Replay(0, kNewSpell, 1);
    EXPECT_EQ(mgr->getMemorizedSpell(0), kNewSpell);
    EXPECT_EQ(mgr->getGemState(0), EQ::GemState::Ready);
}

// The un-memorise arm on its own empties the gem.
TEST_F(MemorizeIntoOccupiedGemTest, UnmemoriseAloneEmptiesTheGem) {
    Replay(0, kOldSpell, 2);
    EXPECT_EQ(mgr->getMemorizedSpell(0), EQ::SPELL_UNKNOWN);
    EXPECT_EQ(mgr->getGemState(0), EQ::GemState::Empty);
}

// Order matters and is respected: a memorise followed by an un-memorise for
// the same slot leaves it EMPTY, not holding the new spell. This is the arm
// that would break if the handler batched or reordered.
TEST_F(MemorizeIntoOccupiedGemTest, MemoriseThenUnmemoriseLeavesTheGemEmpty) {
    Replay(0, kNewSpell, 1);
    Replay(0, kOldSpell, 2);
    EXPECT_EQ(mgr->getMemorizedSpell(0), EQ::SPELL_UNKNOWN);
}

// T401's control: an EMPTY gem takes the new spell (this arm appeared to work
// live only because nothing had to be displaced).
TEST_F(MemorizeIntoOccupiedGemTest, MemoriseIntoAnEmptyGemStillWorks) {
    Replay(1, kNewSpell, 1);
    EXPECT_EQ(mgr->getMemorizedSpell(1), kNewSpell);
    EXPECT_EQ(mgr->getMemorizedSpell(0), kOldSpell) << "gem 0 must be untouched";
}

// A slot beyond the gem bar is a SPELLBOOK slot, not a gem: it must not be
// clamped into gem 7 or write anything.
TEST_F(MemorizeIntoOccupiedGemTest, ASpellbookSlotIsNotAGem) {
    Replay(200, kNewSpell, 1);
    for (uint8_t g = 1; g < EQ::MAX_SPELL_GEMS; ++g) {
        EXPECT_EQ(mgr->getMemorizedSpell(g), EQ::SPELL_UNKNOWN) << "gem " << int(g);
    }
    EXPECT_EQ(mgr->getMemorizedSpell(0), kOldSpell);
}

}  // namespace

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
