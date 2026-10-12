// B534 (#181) -- the output-layer camera route is a second, stale camera
// vocabulary; this test is the net that stays red while any part of it is
// still in the tree and green once the whole route is gone.
//
// What was deleted (see refactor/B534/README.md for the producer/consumer
// evidence):
//   * eqt::output::CameraMode { Free, Follow, FirstPerson } in
//     include/client/output/output_renderer.h, and with it the interface's
//     setCameraMode()/cycleCameraMode() empty virtual defaults;
//   * GraphicalRenderer's m_cameraMode member, setCameraMode() override,
//     getCameraMode(), the cycle body and getCameraModeString()
//     (graphical_renderer.{h,cpp} -- the TU keeps only its namespace,
//     its two camera bodies were the class's only non-virtual methods);
//   * GraphicalMode::{set,cycle,get}CameraMode, its consumeAction(
//     InputAction::ToggleCameraMode) arm, and the
//     eqt::input::InputAction::ToggleCameraMode enumerator itself --
//     after that arm there was no other consumer, and no input handler
//     (GraphicsInputHandler's RA->InputAction map, ConsoleInputHandler's
//     key/command tables) ever produced it.
//
// The LIVE camera route is untouched by this unit and is named here so a
// future sweep does not take it with the dead one: F9 ->
// HotkeyAction::ToggleCameraMode (hotkey_manager.h) ->
// RendererAction::ToggleCameraMode (queued by RendererEventReceiver, drained
// in FrameComponent::processCommonInput) ->
// PlayerCameraComponent::cycleCameraMode() over the official six-camera set
// (T461), pinned behaviourally by test_camera_mode_wiring,
// test_irrlicht_camera_command, test_slash_commands_component and
// test_player_camera_component.
//
// The idiom is test_input_action_bridge.cpp's own
// InputActionEnumTest.T368_NoConsumerlessUITogglesRemain: read the affected
// files as text and fail on the route's tokens, whole-line where the names
// also appear in tombstone comments.
#include "client/input/input_handler.h"
#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << path;
    std::string all((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    return all;
}

// One occurrence of any of `needles` anywhere in the file counts; the
// expected occurrences are exactly `allow` (usually 0 -- the route is gone,
// a tombstone comment naming it is allowed and counted explicitly).
int countNeedles(const std::string& text, const char* const* needles, size_t n) {
    int total = 0;
    for (size_t i = 0; i < n; ++i) {
        std::string needle(needles[i]);
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            ++total;
            pos += needle.size();
        }
    }
    return total;
}

TEST(B534OutputCameraRouteTest, OutputRendererHeaderDeclaresNoCameraVocabulary) {
    const std::string text = slurp(OUTPUT_RENDERER_HEADER);
    // The whole route -- enum, the two virtual defaults, every
    // "CameraMode" spelling -- must be absent. No tombstone comment is kept
    // in this header, so the budget is exactly zero.
    const char* const needles[] = {"CameraMode"};
    EXPECT_EQ(countNeedles(text, needles, 1), 0)
        << "include/client/output/output_renderer.h declares camera "
           "vocabulary again (the eqt::output::CameraMode route, B534). "
           "Camera state belongs to EQT::Graphics::CameraMode "
           "(include/client/graphics/player/camera_mode.h) and "
           "PlayerCameraComponent.";
}

TEST(B534OutputCameraRouteTest, GraphicalRendererKeptNoCameraMembersOrMethods) {
    const std::string text = slurp(GRAPHICAL_RENDERER_HEADER);
    const char* const needles[] = {"CameraMode"};
    EXPECT_EQ(countNeedles(text, needles, 1), 0)
        << "include/client/output/graphical_renderer.h holds camera state or "
           "methods again (m_cameraMode / setCameraMode / getCameraMode / "
           "getCameraModeString, B534).";
    // The TU that rotated the old three is now an empty namespace; its
    // camera bodies must be gone from the source.
    const std::string cpp = slurp(GRAPHICAL_RENDERER_CPP);
    EXPECT_EQ(countNeedles(cpp, needles, 1), 0)
        << "src/client/output/graphical_renderer.cpp still rotates the "
           "output-layer camera modes (B534 deleted its cycle/string "
           "bodies -- the only non-virtual methods in the class).";
}

TEST(B534OutputCameraRouteTest, GraphicalModeConsumesNoCameraToggle) {
    const std::string header = slurp(GRAPHICAL_MODE_HEADER);
    const std::string cpp = slurp(GRAPHICAL_MODE_CPP);
    const char* const needles[] = {
        "output::CameraMode", "setCameraMode", "cycleCameraMode",
        "getCameraMode"};
    EXPECT_EQ(countNeedles(header, needles, 4), 0)
        << "include/client/mode/graphical_mode.h declares the camera route "
           "again (B534).";
    EXPECT_EQ(countNeedles(cpp, needles, 4), 0)
        << "src/client/mode/graphical_mode.cpp carries the camera route "
           "again (B534) -- including the consumeAction( "
           "InputAction::ToggleCameraMode) arm.";
}

TEST(B534OutputCameraRouteTest, InputActionHasNoToggleCameraModeEnumerator) {
    const std::string header = slurp(INPUT_HANDLER_HEADER);
    // Whole-line, T368's idiom: a tombstone comment may name the dead
    // enumerator; an enum body line may not.
    bool enumerator = false;
    for (size_t start = 0; ; ) {
        size_t end = header.find('\n', start);
        if (end == std::string::npos) end = header.size();
        std::string line = header.substr(start, end - start);
        size_t b = line.find_first_not_of(" \t");
        if (b != std::string::npos) {
            size_t e = line.find_last_not_of(" \t");
            std::string trimmed = line.substr(b, e - b + 1);
            if (trimmed == "ToggleCameraMode,") enumerator = true;
        }
        if (end == header.size()) break;
        start = end + 1;
    }
    EXPECT_FALSE(enumerator)
        << "eqt::input::InputAction::ToggleCameraMode is an enumerator again "
           "(B534 deleted it: after GraphicalMode's arm went, nothing "
           "produced it and nothing consumed it; the live F9 route is "
           "HotkeyAction/RendererAction::ToggleCameraMode).";
}

TEST(B534OutputCameraRouteTest, NoInputHandlerProducesToggleCameraMode) {
    // The producer half of the proof: even before the enumerator was gone,
    // no live handler set it. This half stays green afterwards too -- it
    // pins that the two handlers' RA/key/command maps did not pick up the
    // name.
    const char* const needles[] = {
        "InputAction::ToggleCameraMode", "ToggleCameraMode"};
    const std::string gfx = slurp(GRAPHICS_INPUT_HANDLER_CPP);
    const std::string con = slurp(CONSOLE_INPUT_HANDLER_CPP);
    EXPECT_EQ(countNeedles(gfx, needles, 2), 0)
        << "src/client/input/graphics_input_handler.cpp maps a renderer "
           "action onto InputAction::ToggleCameraMode (B534: nothing ever "
           "did).";
    EXPECT_EQ(countNeedles(con, needles, 2), 0)
        << "src/client/input/console_input_handler.cpp sets "
           "InputAction::ToggleCameraMode (B534: nothing ever did).";
}

TEST(B534OutputCameraRouteTest, TheLiveCameraRouteSurvives) {
    // The guard's own negative control: the F9 route this unit must NOT
    // have touched. Every line here is a token that exists on fable
    // (hotkey_manager.h:57, hotkey_manager.cpp:132/619,
    // renderer_frame_input.cpp:243, player_camera_component.h:230) and must
    // keep existing, so a sweep that grabs the LIVE enum or the consumer
    // arm goes red here.
    const char* const needles[] = {"ToggleCameraMode"};
    EXPECT_EQ(countNeedles(slurp(HOTKEY_MANAGER_HEADER), needles, 1), 1)
        << "HotkeyAction::ToggleCameraMode (F9, T461) is gone from "
           "hotkey_manager.h -- that is the LIVE camera route, not B534's.";
    EXPECT_GE(countNeedles(slurp(HOTKEY_MANAGER_CPP), needles, 1), 2)
        << "HotkeyManager lost its ToggleCameraMode name table / F9 default "
           "binding (T461) -- live route.";
    const char* const armNeedles[] = {
        "case RA::ToggleCameraMode: ctx_.PlayerCamera().cycleCameraMode();"};
    EXPECT_EQ(countNeedles(slurp(RENDERER_FRAME_INPUT_CPP), armNeedles, 1), 1)
        << "FrameComponent::processCommonInput lost its RA::ToggleCameraMode "
           "arm (renderer_frame_input.cpp) -- live route.";
    EXPECT_GE(countNeedles(slurp(PLAYER_CAMERA_COMPONENT_HEADER), needles, 1), 1)
        << "PlayerCameraComponent::cycleCameraMode() (T461, the official "
           "six-camera set) is gone from player_camera_component.h -- live "
           "route.";
}

} // namespace
