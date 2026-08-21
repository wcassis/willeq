// Shared fixture for the test_command_processor family.
//
// T165: test_command_processor.cpp reached 3168 lines -- past
// scripts/refactor/check_file_sizes.sh's 3000-line FAIL ceiling -- and the
// ceiling had started deflecting real work: T153 could not add its
// /appearance cases here and had to open test_appearance_command.cpp for
// them. The tests are now split across test_command_processor*.cpp, all of
// them sources of the SINGLE test_command_processor target, so the split
// costs no extra binary and no extra ctest discovery pass.
//
// The fixture lives in a NAMED namespace on purpose. GTest requires every
// test in a suite to share one fixture TYPE; an anonymous-namespace fixture
// in a header would give each translation unit its own distinct type and
// TEST_F(CommandProcessorTest, ...) spread across files would abort at
// startup with "All tests in the same test suite must use the same test
// fixture class".

#pragma once

#include "action_test_fakes.h"
#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/output/output_renderer.h"
#include "client/state/game_state.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace eqt {
namespace tests {

using eqt::action::ActionDispatcher;
using eqt::action::CommandProcessor;
using eqt::action::test::RecordingActionHandler;
using eqt::state::GameState;

// Minimal recording IOutputRenderer fake. Only displaySystemMessage and the
// timestamp toggle are exercised by CommandProcessor; everything else is a
// harmless no-op stub required to satisfy the pure-virtual surface.
class RecordingOutputRenderer : public eqt::output::IOutputRenderer {
public:
    std::vector<std::string> systemMessages;
    bool showTimestamps = true;

    bool initialize(const eqt::output::RendererConfig&) override { return true; }
    void shutdown() override {}
    bool isRunning() const override { return true; }
    bool update(float) override { return true; }
    void connectToGameState(eqt::state::GameState&) override {}
    void disconnectFromGameState() override {}
    void loadZone(const std::string&) override {}
    void unloadZone() override {}
    const std::string& getCurrentZoneName() const override {
        static const std::string empty;
        return empty;
    }
    void setLoadingProgress(float, const std::wstring&) override {}
    void setZoneReady(bool) override {}
    bool isZoneReady() const override { return true; }
    void setExpectedEntityCount(size_t) override {}
    void notifyEntityLoaded() override {}
    void setNetworkReady(bool) override {}
    void setPlayerSpawnId(uint16_t) override {}
    void setPlayerPosition(float, float, float, float) override {}
    void setCharacterInfo(const std::wstring&, int, const std::wstring&) override {}
    void updateCharacterStats(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void setCurrentTarget(uint16_t, const std::string&, uint8_t, uint8_t) override {}
    void updateCurrentTargetHP(uint8_t) override {}
    void clearCurrentTarget() override {}
    void displayChatMessage(const std::string&, const std::string&, const std::string&) override {}
    void displaySystemMessage(const std::string& message) override {
        systemMessages.push_back(message);
    }
    void displayCombatMessage(const std::string&) override {}
    void requestQuit() override {}

    void setShowTimestamps(bool show) override { showTimestamps = show; }
    bool getShowTimestamps() const override { return showTimestamps; }
};

class CommandProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher = std::make_unique<ActionDispatcher>(state);
        dispatcher->setActionHandler(&handler);
        state.world().setZoneConnected(true);
        processor = std::make_unique<CommandProcessor>(state, *dispatcher);
    }

    GameState state;
    RecordingActionHandler handler;
    std::unique_ptr<ActionDispatcher> dispatcher;
    std::unique_ptr<CommandProcessor> processor;
};

}  // namespace tests
}  // namespace eqt
