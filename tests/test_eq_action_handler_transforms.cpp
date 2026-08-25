// T93: fake-substitution sweep -- regression tests for TRANSFORMS in
// EqActionHandler that RecordingActionHandler (tests/action_test_fakes.h)
// cannot catch, because it implements every IActionHandler method as a bare
// recording call with no conversion (the same shape of gap T90 found and
// fixed for setHeading()).
//
// See refactor/T93_fake_substitution_sweep.md for the full inventory and
// classification. This file covers the highest-ranked TRANSFORMS finding
// from that sweep: EqActionHandler::castSpell() / castSpellOnTarget() /
// memorizeSpell() all convert a 1-indexed UI gem slot (1..12, the number
// dispatched from ActionDispatcher, which RecordingActionHandler just
// records verbatim) into the 0-indexed SpellSlot enum
// (SPELL_GEM_1 == 0 .. SPELL_GEM_12 == 11) via `gemSlot - 1` before calling
// into CombatManager. test_action_dispatcher.cpp's
// CastSpellFamily_GemSlotBoundsAndRouting only proves ActionDispatcher
// forwards the RAW 1-indexed gemSlot to the fake unchanged -- it never
// constructs a real EqActionHandler, so the -1 conversion itself has zero
// direct coverage, exactly the seam-that-shipped-was-not-the-seam-tested
// pattern from T90.
//
// Uses the same headless EverQuest + EqActionHandler construction as
// test_setheading_conversion.cpp (T90) / test_position_frame_roundtrip.cpp
// (T88) -- no network, no graphics, no eqemu.

#include <gtest/gtest.h>

#include "client/combat.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"

using namespace eqt;

namespace {

// ---------------------------------------------------------------------
// castSpell()/memorizeSpell(): gemSlot is 1-indexed on the public/dispatcher
// surface but CombatManager::m_spell_gems is keyed by the 0-indexed
// SpellSlot enum (SPELL_GEM_1 = 0). Observable via CombatManager::
// IsCasting(): CastSpell(SpellSlot) only flips m_is_casting to true if the
// looked-up gem slot has a nonzero memorized spell id.
// ---------------------------------------------------------------------

TEST(EqActionHandlerTransforms, CastSpell_GemSlotIsOneIndexedToZeroIndexedSpellSlot) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    CombatManager* combat = eq.GetCombatManager();
    ASSERT_NE(combat, nullptr);
    ASSERT_FALSE(combat->IsCasting());

    // Memorize spell 100 into UI gem slot 2 ONLY -- gem 1 stays empty (0).
    handler.memorizeSpell(2, 100);

    // Gem 1 is empty: casting from it must fail (no state change). If the
    // -1 conversion were missing, gemSlot=1 would be cast straight through
    // as SpellSlot value 1, which IS gem 2 (SPELL_GEM_2) -- exactly the one
    // that WAS memorized -- and this would wrongly succeed.
    handler.castSpell(1);
    EXPECT_FALSE(combat->IsCasting())
        << "gem slot 1 was never memorized; castSpell(1) must map to "
           "SPELL_GEM_1 (index 0), which is empty, not SPELL_GEM_2";

    // Gem 2 IS memorized: casting from it must succeed.
    handler.castSpell(2);
    EXPECT_TRUE(combat->IsCasting())
        << "gem slot 2 was memorized with spell 100; castSpell(2) must map "
           "to SPELL_GEM_2 (index 1) and succeed";
}

TEST(EqActionHandlerTransforms, CastSpellOnTarget_SharesSameGemSlotConversion) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(460);
    EqActionHandler handler(eq);

    CombatManager* combat = eq.GetCombatManager();
    ASSERT_NE(combat, nullptr);

    // Memorize into gem 12 (the last, boundary slot -- SPELL_GEM_12 == 11)
    // ONLY; leave gem 11 empty, catching an off-by-one at the far end of
    // the range too, not just the near end covered by the test above.
    handler.memorizeSpell(12, 200);
    ASSERT_FALSE(combat->IsCasting());

    handler.castSpellOnTarget(11, 999);
    EXPECT_FALSE(combat->IsCasting())
        << "gem slot 11 was never memorized; must not succeed";

    handler.castSpellOnTarget(12, 999);
    EXPECT_TRUE(combat->IsCasting())
        << "gem slot 12 was memorized with spell 200; must map to "
           "SPELL_GEM_12 (index 11) and succeed";
}

// ---------------------------------------------------------------------
// targetSelf() (EqActionHandler): real self<->pet toggle branching logic
// that RecordingActionHandler's bare `record("targetSelf")` cannot exercise
// at all -- NOT covered here. Attempted during this sweep and dropped: the
// no-pet branch still routes through CombatManager::SetTarget(), which
// validates the target id against EverQuest::GetEntities() (a std::map this
// unit found no existing test seam to populate -- unlike GameState's
// entities(), which test_setheading_conversion.cpp's FaceEntity tests use,
// this is EverQuest's OWN internal m_entities map read directly by
// CombatManager). Adding a seam for it would be a second new production
// touch beyond the gem-slot tests above; per the plan's no-new-seams-unless-
// genuinely-unavoidable bar this was left for the next unit rather than
// added here. See refactor/T93_fake_substitution_sweep.md's Review and
// refactor/deferred.md for this as an explicit open item.
// T234: now covered in test_eq_action_handler_transforms_real.cpp, which
// populates m_entities through the real NewSpawn packet path instead of a
// new seam.
// ---------------------------------------------------------------------

}  // namespace
