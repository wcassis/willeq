/**
 * T330 (B043) -- the four debug hotkeys that had never done anything.
 *
 * L04 measured it while counting callers: RendererEventReceiver::OnEvent
 * queues RendererAction::ToggleWireframe / ToggleHUD / ToggleNameTags /
 * ToggleOldModels, hotkey_manager.cpp binds all four keys, and
 * DiagnosticsComponent implements all four toggles -- but
 * IrrlichtRenderer::processCommonInput()'s switch had NO arm for any of them.
 * Every press fell through its `default: break;`. This is the T313/T315
 * unwired-handler pattern for the fourth time: the feature existed end to end
 * except for the one line that joins the halves.
 *
 * The fix is four switch arms. What this file pins:
 *
 *   - Behavioural, through the exact production consumer
 *     (IrrlichtRenderer::processCommonInput on a device-less renderer, the
 *     idiom of tests/test_ui_hide_command.cpp): wireframe, name tags and old
 *     models each flip their observable state when the action is drained.
 *     Name tags and old models are guarded on ctx_.entityRenderer, which is
 *     only constructed by init(); the test installs a device-less
 *     EntityRenderer (its constructor and the two toggles' callees only store
 *     pointers and walk empty caches) so the guard is satisfied.
 *   - Source-pinned: the HUD arm. toggleHUD() writes IrrlichtRenderer's
 *     private hudEnabled_ (L13 moved it to DiagnosticsComponent), which has
 *     no public reader, so the arm is asserted
 *     as text like this suite already does for other single-site invariants
 *     -- stated plainly rather than dressed up as behavioural coverage.
 *   - Source-pinned: the receiver still queues all four, so the arms cannot be
 *     orphaned from the other end.
 *
 * STILL OPEN after this unit (class B, boarded under B043): the second route,
 * GraphicalMode::toggleWireframe/toggleHUD/toggleNameTags, calls through an
 * output::IOutputRenderer* and IrrlichtRenderer does not derive from that
 * interface, so those reach the header's empty virtual defaults. Out of scope.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/race_model_loader.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<RendererEvent> One(RendererAction a) {
    return std::vector<RendererEvent>{RendererEvent(a)};
}

class DebugHotkeyArmsTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;

    // The two entity-backed toggles no-op without ctx_.entityRenderer, which
    // init() builds from the Irrlicht device. Nothing they touch dereferences
    // the driver/smgr/filesystem, so a null-constructed one is enough.
    void installEntityRenderer() {
        renderer.Context().entityRenderer =
            std::make_unique<EntityRenderer>(nullptr, nullptr, nullptr);
    }
};

// --- Wireframe: RA::ToggleWireframe -> ctx_.wireframeMode ---

TEST_F(DebugHotkeyArmsTest, ToggleWireframeFlipsWireframeMode) {
    ASSERT_FALSE(renderer.Context().wireframeMode);
    renderer.Frame().processCommonInput(One(RendererAction::ToggleWireframe));
    EXPECT_TRUE(renderer.Context().wireframeMode)
        << "the wireframe hotkey fell through processCommonInput's default again (B043)";
    renderer.Frame().processCommonInput(One(RendererAction::ToggleWireframe));
    EXPECT_FALSE(renderer.Context().wireframeMode);
}

// --- Name tags: RA::ToggleNameTags -> config.constrainedConfig.nameTagsEnabled ---

TEST_F(DebugHotkeyArmsTest, ToggleNameTagsFlipsTheNameTagFlag) {
    installEntityRenderer();
    ASSERT_TRUE(renderer.Context().config.constrainedConfig.nameTagsEnabled);
    renderer.Frame().processCommonInput(One(RendererAction::ToggleNameTags));
    EXPECT_FALSE(renderer.Context().config.constrainedConfig.nameTagsEnabled)
        << "the name-tag hotkey fell through processCommonInput's default again (B043)";
    renderer.Frame().processCommonInput(One(RendererAction::ToggleNameTags));
    EXPECT_TRUE(renderer.Context().config.constrainedConfig.nameTagsEnabled);
}

// --- Old models: RA::ToggleOldModels -> RaceModelLoader::isUsingOldModels() ---

TEST_F(DebugHotkeyArmsTest, ToggleOldModelsFlipsTheModelEra) {
    installEntityRenderer();
    ASSERT_TRUE(renderer.Diagnostics().isUsingOldModels());
    renderer.Frame().processCommonInput(One(RendererAction::ToggleOldModels));
    EXPECT_FALSE(renderer.Diagnostics().isUsingOldModels())
        << "the old-models hotkey fell through processCommonInput's default again (B043)";
    renderer.Frame().processCommonInput(One(RendererAction::ToggleOldModels));
    EXPECT_TRUE(renderer.Diagnostics().isUsingOldModels());
}

// A queued action must not disturb the neighbouring toggles' state -- the arms
// are four separate cases, not one fallthrough.
TEST_F(DebugHotkeyArmsTest, EachArmTouchesOnlyItsOwnState) {
    installEntityRenderer();
    renderer.Frame().processCommonInput(One(RendererAction::ToggleWireframe));
    EXPECT_TRUE(renderer.Context().wireframeMode);
    EXPECT_TRUE(renderer.Context().config.constrainedConfig.nameTagsEnabled);
    EXPECT_TRUE(renderer.Diagnostics().isUsingOldModels());
    EXPECT_FALSE(renderer.isAllUIHidden());
}

// --- The arms, as text (the HUD one has no public reader) ---

TEST(DebugHotkeyArmSourceInvariants, AllFourArmsExistInProcessCommonInput) {
    // L11 (batch correction 21): processCommonInput() moved from
    // IrrlichtRenderer to FrameComponent and the diagnostics receiver moved
    // with it (diagnostics_-> became owner_.Diagnostics().). L12 moved the
    // receiver AGAIN -- the sibling-component pointers went onto
    // RendererContext, so it is now ctx_.Diagnostics(). ALL THREE spellings
    // are accepted and exactly one must match per arm; a guard that knows
    // only the previous spelling passes vacuously on the day the arm is
    // deleted from moved code, which is the whole failure this test exists to
    // prevent (it caught L12's rewrite, third sighting of correction 21).
    // The path moved to src/client/graphics/frame/; the basename is
    // unchanged.
    const std::string src =
        readFile(kRoot / "src/client/graphics/frame/renderer_frame_input.cpp");
    const char* actions[] = {"ToggleWireframe", "ToggleHUD", "ToggleNameTags", "ToggleOldModels"};
    const char* methods[] = {"toggleWireframe", "toggleHUD", "toggleNameTags", "toggleOldModels"};
    for (int i = 0; i < 4; ++i) {
        const std::string owned =
            std::string("case RA::") + actions[i] + ": diagnostics_->" + methods[i] + "(); break;";
        const std::string component = std::string("case RA::") + actions[i] +
                                      ": owner_.Diagnostics()." + methods[i] + "(); break;";
        const std::string contextual = std::string("case RA::") + actions[i] +
                                       ": ctx_.Diagnostics()." + methods[i] + "(); break;";
        int found = (src.find(owned) != std::string::npos ? 1 : 0) +
                    (src.find(component) != std::string::npos ? 1 : 0) +
                    (src.find(contextual) != std::string::npos ? 1 : 0);
        EXPECT_EQ(found, 1)
            << "missing from processCommonInput's switch, so the hotkey does nothing: "
            << actions[i];
    }
}

TEST(DebugHotkeyArmSourceInvariants, TheReceiverStillQueuesAllFour) {
    const std::string src = readFile(kRoot / "src/client/graphics/renderer/renderer_event_receiver.cpp");
    for (const char* q : {"case HA::ToggleWireframe: actionQueue_.push_back({RA::ToggleWireframe});",
                          "case HA::ToggleHUD: actionQueue_.push_back({RA::ToggleHUD});",
                          "case HA::ToggleNameTags: actionQueue_.push_back({RA::ToggleNameTags});",
                          "case HA::ToggleOldModels: actionQueue_.push_back({RA::ToggleOldModels});"}) {
        EXPECT_NE(src.find(q), std::string::npos)
            << "the key no longer reaches the renderer action, so the new arm is orphaned: " << q;
    }
}

}  // namespace
