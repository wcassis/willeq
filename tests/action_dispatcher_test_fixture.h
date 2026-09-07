// tests/action_dispatcher_test_fixture.h -- the shared ActionDispatcherTest
// fixture for the test_action_dispatcher family.
//
// T453 split tests/test_action_dispatcher.cpp (1,217 lines, 63 cases) into
// three sources on ONE target, T446's recipe: a case name is Suite.Case, so
// three binaries all carrying suite ActionDispatcherTest would register the
// same ctest name three times. GTest requires every case in a suite to share
// one fixture TYPE, and an anonymous-namespace fixture in a header gives each
// TU its own type and aborts at startup -- so the fixture moved from the
// original's anonymous namespace into the named one below. Everything else
// here is the original's preamble verbatim, and the using-directives stay at
// global scope so every moved test body resolves exactly the names it did.
// Tests for eqt::action::ActionDispatcher (T12).
//
// Priorities per refactor/T12_action_layer.md:
//   1. Routing correctness across all action families (breadth, with
//      per-member routing strings inside families that have near-identical
//      members — that's where copy-paste mis-wiring hides).
//   2. Guard behavior: no-handler (regression guard, expected to pass —
//      not a bug hunt), handler-without-zone vs handler-with-zone (real
//      behavior: checkHandler()-only methods succeed without zone
//      connection, checkZoneConnection() methods do not).

#pragma once

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include <gtest/gtest.h>

using namespace eqt::action;
using namespace eqt::action::test;
using eqt::state::GameState;

namespace action_dispatcher_test {
class ActionDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;

    void connectHandler() { dispatcher->setActionHandler(&handler); }
    void connectZone() { state.world().setZoneConnected(true); }
};
}  // namespace action_dispatcher_test

using action_dispatcher_test::ActionDispatcherTest;
