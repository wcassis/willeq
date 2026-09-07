// Shared seam + private-access preamble for the test_combat_manager family.
//
// T446: test_combat_manager.cpp reached 1,316 lines. The cases are now split
// across test_combat_manager_*.cpp, all of them sources of the SINGLE
// test_combat_manager target (T165's precedent from the
// test_command_processor family), so the split costs no extra binary, no
// extra ctest discovery pass, and -- because every case keeps its suite AND
// its name -- no ctest test name changes at all.
//
// This header exists mainly because the T17 #define-private-public trick
// below has to run BEFORE the first inclusion of combat.h in every TU that
// wants it: combat.h is #pragma once, so whichever TU-local include comes
// first fixes the class body's access specifiers for that translation unit.
// Routing all of them through one header makes that ordering impossible to
// get wrong in a new sibling TU.
//
// T23 — CombatManager coverage (spell/combat batch, unit 1 of 3).
//
// Seam: settled as Option A (refactor/T23_spell_combat_seam_scoping.md,
// Will's ruling) -- no interface extraction. A bare
// EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar") constructs a
// LIVE CombatManager reachable via the existing public eq.GetCombatManager().
// Fixtures are staged by replaying spawn packets (zone_stream_test_utils.h,
// T18-promoted helpers) -- no network, no graphics, no live server.
//
// Timing: CombatManager reads steady_clock::now() in ~12 places with no
// injectable clock, and Update() is wall-clock gated (target scans only
// every 1000ms). Per plan, no sleep-based tests. Every test here drives a
// deterministic public entry point directly. The lone exception is the
// ProcessConsiderResponse test, which needs CombatManager::Update() to have
// actually run ScanForTargets() once to populate m_potential_targets; that
// private timing gate is backdated via the T17 `#define private public`
// technique (see CombatManagerConsiderFixture below) rather than sleeping
// past it.
//
// Note: tests/test_combat.cpp already covers combat *enums and structs*
// only -- left untouched, not disturbed by this file.

#pragma once

#include <gtest/gtest.h>

// T17-style private access, needed only for the ProcessConsiderResponse
// fixture (backdating m_last_target_scan so Update() actually scans instead
// of sleeping past the 1000ms gate). combat.h is #pragma once, so this
// first inclusion determines the class body's access specifiers for the
// whole translation unit -- eq.h's later (transitive) include of combat.h
// is a no-op.
#define private public
#define protected public
#include "client/combat.h"
#undef private
#undef protected

#include "zone_stream_test_utils.h"

#include <chrono>

using namespace zonetest;
