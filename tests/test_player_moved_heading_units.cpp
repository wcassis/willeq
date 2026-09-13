// T460 (board B521) -- PlayerMovedData::heading on the renderer bridge is the
// EQ 0..512 unit, and every publisher must send that unit.
//
// THE CONTRACT. IrrlichtBridge::applyEvent(PlayerMoved) hands d.heading
// straight to IrrlichtRenderer::setPlayerPosition() ->
// PlayerCameraComponent::setPlayerPosition(), which stores it as the
// renderer's playerHeading and feeds CameraController::setFollowPosition()
// with it -- in "EQ heading 0-512" (its own log line says so). The renderer's
// own PlayerPositionChanged intent carries the same unit back, and
// BridgeComponent::OnGraphicsMovement() divides by 512/360 on receipt.
//
// THE DEFECT. Six publishers convert the internal degrees
// (PlayerFacade::HeadingDeg(), 0..360) with `* 512 / 360`. Two did not:
//   * MovementComponent::UpdateMovement() -- the per-tick console movement
//     integrator (/moveto, /follow, auto-attack chase), and
//   * WarpToPosition() (/warp).
// Each published DEGREES, so every tick of a game-driven move pointed the
// camera at heading_deg/512*360 -- a different direction -- until the render
// thread's own integration wrote the right one back: two camera headings,
// alternating while moving.
//
// Seam: zone_stream_test_utils.h's bare EverQuest + TestBridge, driven through
// the real MovementComponent / CommandsComponent entry points.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "client/eq.h"
#include "client/commands/commands_component.h"
#include "client/movement/movement_component.h"
#include "client/state/event_bus.h"
#include "zone_stream_test_utils.h"

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif

using namespace zonetest;
namespace fs = std::filesystem;

namespace {

constexpr uint16_t kMySpawn = 460;

// Due +x from the origin: CalculateHeading() gives 90 degrees, i.e. 128 in the
// 512 unit. 90 is not a value where the two units coincide.
constexpr float kEastDeg = 90.0f;
constexpr float kEast512 = 128.0f;

float LastPlayerMovedHeading(const std::vector<eqt::state::GameEvent>& events, int* count) {
    float last = -1.0f;
    int n = 0;
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::PlayerMoved) {
            ++n;
            last = std::get<eqt::state::PlayerMovedData>(e.data).heading;
        }
    }
    *count = n;
    return last;
}

std::string readText(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Pre-fix RED (published 90).
TEST(PlayerMovedHeadingUnits, TheConsoleMovementIntegratorPublishesThe512Unit) {
    TestBridge bridge;
    EverQuest eq = MakeEq();
    eq.setBridge(&bridge);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kMySpawn);

    eq.Movement().MoveTo(100.0f, 0.0f, 0.0f);
    ASSERT_NEAR(eq.GetHeading(), kEastDeg, 0.01f);
    bridge.drainEvents();

    eq.Movement().UpdateMovement();

    int moved = 0;
    const float heading = LastPlayerMovedHeading(bridge.drainEvents(), &moved);
    ASSERT_GE(moved, 1) << "UpdateMovement() published no PlayerMoved for an active /moveto";
    EXPECT_NEAR(heading, kEast512, 0.01f)
        << "UpdateMovement() published heading " << heading
        << "; the renderer reads PlayerMovedData::heading as EQ 0..512 (degrees 90 = 128)";
}

// Pre-fix RED (published 90).
TEST(PlayerMovedHeadingUnits, WarpPublishesThe512Unit) {
    TestBridge bridge;
    EverQuest eq = MakeEq();
    eq.setBridge(&bridge);
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kMySpawn);

    eq.Movement().MoveTo(100.0f, 0.0f, 0.0f);  // face east
    ASSERT_NEAR(eq.GetHeading(), kEastDeg, 0.01f);
    bridge.drainEvents();

    eq.Commands().RequestWarp("10 20 30");

    int moved = 0;
    const float heading = LastPlayerMovedHeading(bridge.drainEvents(), &moved);
    ASSERT_GE(moved, 1) << "/warp published no PlayerMoved";
    EXPECT_NEAR(heading, kEast512, 0.01f)
        << "/warp published heading " << heading << "; expected the 0..512 unit";
}

// The census as a test. Every renderer-bridge publish of PlayerMoved under
// src/client must assign `.heading` from an expression carrying the 512/360
// conversion (inline, or through a local computed with it). A new publisher
// is counted, so it cannot be added without passing through this check.
//
// Excluded by design: src/client/state/player_state.cpp publishes
// PlayerMoved on the STATE LAYER's own EventBus (GameState::events(), its only
// subscriber is AutomatedMode's catch-all), not on the renderer bridge, and in
// that bus's own unit -- degrees, PlayerState::m_heading.
TEST(PlayerMovedHeadingUnits, EveryBridgePublisherConvertsTo512) {
    const fs::path root = fs::path(WILLEQ_SOURCE_DIR) / "src" / "client";
    ASSERT_TRUE(fs::is_directory(root)) << root;

    const std::string kPublish = "GameEventType::PlayerMoved, std::move(";
    const std::regex headingAssign(R"(\.heading\s*=\s*([^;]+);)");

    std::vector<std::string> sites;
    std::vector<std::string> wrong;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") continue;
        const std::string rel = fs::relative(entry.path(), root).generic_string();
        if (rel == "state/player_state.cpp") continue;
        const std::string text = readText(entry.path());

        for (size_t at = text.find(kPublish); at != std::string::npos;
             at = text.find(kPublish, at + 1)) {
            const size_t decl = text.rfind("PlayerMovedData", at);
            ASSERT_NE(decl, std::string::npos) << rel;
            const std::string block = text.substr(decl, at - decl);
            const int line = 1 + static_cast<int>(std::count(text.begin(), text.begin() + at, '\n'));
            const std::string site = rel + ":" + std::to_string(line);
            sites.push_back(site);

            std::smatch m;
            if (!std::regex_search(block, m, headingAssign)) {
                wrong.push_back(site + " (no .heading assignment)");
                continue;
            }
            const std::string rhs = m[1].str();
            if (rhs.find("512") != std::string::npos) continue;

            // A named local: its definition before the payload must convert.
            const std::regex def("\\b" + rhs + R"(\s*=\s*[^;]*512)");
            const size_t from = decl > 2000 ? decl - 2000 : 0;
            const std::string before = text.substr(from, decl - from);
            if (!std::regex_search(before, def)) {
                wrong.push_back(site + " (.heading = " + rhs + ")");
            }
        }
    }

    std::string wrongList;
    for (const auto& w : wrong) wrongList += "\n  " + w;
    EXPECT_TRUE(wrong.empty())
        << "PlayerMoved publishers not converting to the 0..512 unit:" << wrongList;

    // bridge_component.cpp, bridge_component_intents.cpp, entities_component.cpp,
    // entities_component_updates.cpp, movement_update.cpp, zone_entry.cpp,
    // zone_component.cpp, commands_movement.cpp.
    std::string all;
    for (const auto& s : sites) all += "\n  " + s;
    EXPECT_EQ(sites.size(), 8u)
        << "the census changed -- confirm each new/removed publisher's unit, then update:" << all;
}

// The consumer's half of the contract, pinned where it is stated.
TEST(PlayerMovedHeadingUnits, TheConsumerReadsThe512Unit) {
    const fs::path src = fs::path(WILLEQ_SOURCE_DIR);
    const std::string bridge = readText(src / "src/client/bridge/irrlicht_bridge.cpp");
    EXPECT_NE(bridge.find("renderer_->setPlayerPosition(d.x, d.y, d.z, d.heading);"), std::string::npos)
        << "IrrlichtBridge no longer forwards PlayerMovedData::heading verbatim; re-establish the unit";
    const std::string camera = readText(src / "src/client/graphics/player/renderer_camera.cpp");
    EXPECT_NE(camera.find("EQ heading 0-512"), std::string::npos)
        << "PlayerCameraComponent::setPlayerPosition() no longer states its heading unit";
}

}  // namespace
