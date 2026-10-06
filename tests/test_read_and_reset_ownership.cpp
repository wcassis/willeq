// T315 -- the fourth instance of the read-and-reset double-consumer pattern,
// and the guard that stops a fifth.
//
// THE RULE:
//
//   A destructive read-and-reset accessor with more than one caller is a
//   defect by construction, unless the accumulator is per-consumer.
//
// Three prior units each found one instance the hard way (T230, T235, and
// T312's mouse delta). This unit found the fourth:
// RendererEventReceiver::wasLeftButtonClicked() / wasLeftButtonReleased()
// are atomic exchange(false) one-shots that had TWO callers on TWO threads:
//
//   * renderer_frame.cpp's processFrameInput() (RENDER thread) -- the real
//     consumer. It drives pendingUIClick_, the static-UI hit test, and
//     handleMouseTargeting().
//   * graphics_input_handler.cpp's updateFromEventReceiver() (GAME thread),
//     which stored the result in InputState::leftButtonClicked/Released --
//     a struct with NO reader anywhere in the tree.
//
// Being atomic, that is not a race. It is worse in a quieter way: whichever
// thread wins the exchange KEEPS the click, and when the game thread won,
// nothing at all happened -- a click on an inventory slot, a loot row or a
// spell gem that silently did nothing.
//
// This file holds two things:
//   1. ClickOwnershipTest -- deterministic, fails 0-vs-100 on the pre-fix
//      tree, in the shape of T312's MouseDeltaOwnershipTest.
//   2. ReadAndResetSweepGuard -- a source-scanning guard whose table of
//      accessor -> expected call-site count IS the sweep's documentation
//      (the tests/test_duplicate_state_stores.cpp kKnownRemaining
//      precedent). Adding a second caller to any of them breaks the build's
//      test run, not a real session.
//
// No graphics device is needed: RendererEventReceiver is a bare
// irr::IEventReceiver, and GraphicsInputHandler only ever talks to it
// (the T66/T233/T235/T312 no-device idiom).

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "client/graphics/irrlicht_renderer.h"
#include "client/input/graphics_input_handler.h"
#include "client/input/hotkey_manager.h"

namespace EQT {
namespace Graphics {
namespace {

namespace fs = std::filesystem;

irr::SEvent LMouseDown(int x, int y) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_MOUSE_INPUT_EVENT;
    ev.MouseInput.Event = irr::EMIE_LMOUSE_PRESSED_DOWN;
    ev.MouseInput.X = x;
    ev.MouseInput.Y = y;
    return ev;
}

irr::SEvent LMouseUp(int x, int y) {
    irr::SEvent ev{};
    ev.EventType = irr::EET_MOUSE_INPUT_EVENT;
    ev.MouseInput.Event = irr::EMIE_LMOUSE_LEFT_UP;
    ev.MouseInput.X = x;
    ev.MouseInput.Y = y;
    return ev;
}

}  // namespace

// --- 1. the click double consumer --------------------------------------
//
// Deterministic; no threads needed, and no sanitizer. The game-thread poll
// is interleaved between the click arriving and the render thread's frame,
// which is exactly what happens when a game tick lands between two frames.
// Pre-fix the UI side sees ZERO of 100 clicks; post-fix it sees all 100.
TEST(ClickOwnershipTest, GameThreadPollDoesNotStealUIClick) {
    eqt::input::HotkeyManager::instance().resetToDefaults();

    RendererEventReceiver receiver;
    eqt::input::GraphicsInputHandler handler(&receiver);

    constexpr int kClicks = 100;
    int uiSawClick = 0;
    int uiSawRelease = 0;

    for (int i = 0; i < kClicks; ++i) {
        receiver.OnEvent(LMouseDown(40 + i, 90));
        handler.update();  // game tick between the click and the frame
        // The render thread's consumer (renderer_frame.cpp processFrameInput).
        if (receiver.wasLeftButtonClicked()) ++uiSawClick;

        receiver.OnEvent(LMouseUp(40 + i, 90));
        handler.update();
        if (receiver.wasLeftButtonReleased()) ++uiSawRelease;
    }

    EXPECT_EQ(uiSawClick, kClicks);
    EXPECT_EQ(uiSawRelease, kClicks);
}

// The same thing under real contention: a game thread polling flat out
// while the render thread posts a click and consumes it. Every posted click
// must reach the UI-side consumer. Pre-fix this loses a large fraction of
// them (the exact number varies, which is why the deterministic test above
// is the primary evidence); post-fix it is exact.
TEST(ClickOwnershipTest, ConcurrentGameThreadPollLosesNoClick) {
    eqt::input::HotkeyManager::instance().resetToDefaults();

    RendererEventReceiver receiver;
    std::atomic<bool> stop{false};

    std::thread gameThread([&receiver, &stop]() {
        eqt::input::GraphicsInputHandler handler(&receiver);
        while (!stop.load(std::memory_order_relaxed)) {
            handler.update();
        }
    });

    constexpr int kClicks = 2000;
    int uiSawClick = 0;
    for (int i = 0; i < kClicks; ++i) {
        receiver.OnEvent(LMouseDown(10, 10));
        if (receiver.wasLeftButtonClicked()) ++uiSawClick;
        receiver.OnEvent(LMouseUp(10, 10));
        (void)receiver.wasLeftButtonReleased();
    }

    stop.store(true, std::memory_order_relaxed);
    gameThread.join();

    EXPECT_EQ(uiSawClick, kClicks);
}

namespace {

// Strip // and /* */ comments so a call site mentioned in prose (there are
// several -- every one of these fixes left a note behind) is not counted.
std::string stripComments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool inLine = false, inBlock = false, inStr = false, inChr = false;
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        const char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (inLine) {
            if (c == '\n') { inLine = false; out.push_back(c); }
            continue;
        }
        if (inBlock) {
            if (c == '*' && n == '/') { inBlock = false; ++i; }
            else if (c == '\n') { out.push_back(c); }
            continue;
        }
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inStr = false;
            continue;
        }
        if (inChr) {
            if (c == '\\') { ++i; continue; }
            if (c == '\'') inChr = false;
            continue;
        }
        if (c == '/' && n == '/') { inLine = true; ++i; continue; }
        if (c == '/' && n == '*') { inBlock = true; ++i; continue; }
        if (c == '"') { inStr = true; continue; }
        if (c == '\'') { inChr = true; continue; }
        out.push_back(c);
    }
    return out;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Count `->accessor(` and `.accessor(` occurrences -- i.e. calls made
// through a RendererEventReceiver instance. The declaration in the header
// and the out-of-line definition (`RendererEventReceiver::accessor(`) match
// neither form, so they are never counted.
int countCallSites(const std::string& code, const std::string& accessor) {
    int n = 0;
    for (const std::string& form : {"->" + accessor + "(", "." + accessor + "("}) {
        size_t at = 0;
        while ((at = code.find(form, at)) != std::string::npos) { ++n; at += form.size(); }
    }
    return n;
}

struct SweepEntry {
    const char* accessor;
    int expectedCallSites;
    const char* note;
};

// ===== THE SWEEP TABLE (T315) =====
//
// Every destructive read-and-reset accessor on RendererEventReceiver, with
// the number of call sites in src/ that each is allowed to have. One is the
// only safe number; the single exception is documented in place.
//
// This table is the deliverable. If you add a caller, this test fails and
// you must either remove it or make the accumulator per-consumer -- and
// then say so here.
const SweepEntry kSweep[] = {
    {"wasKeyPressed", 1,
     "game thread (GraphicsInputHandler's Jump check). T312."},
    {"getMouseDeltaX", 1, "render thread (mouse-look). Second caller deleted by T312."},
    {"getMouseDeltaY", 1, "render thread (mouse-look). Second caller deleted by T312."},
    {"wasLeftButtonClicked", 1, "render thread (processFrameInput). Second caller deleted by T315."},
    {"wasLeftButtonReleased", 1, "render thread (processFrameInput). Second caller deleted by T315."},
    {"wasRightButtonClicked", 1, "render thread (renderStaticUI). B442: the item windows' right click."},
    {"drainActions", 1, "render thread (processFrameInput). Render-only queue."},
    {"drainBridgeActions", 1, "game thread (GraphicsInputHandler). Mutex-guarded since T312."},
    {"getSpellGemCastRequest", 1, "game thread. The render-thread discard was removed earlier."},
    {"getHotbarActivationRequest", 1, "game thread. Ditto."},
    {"getCameraZoomDelta", 1, "render thread (processInputDeltas)."},
    // T439 (board B432): renamed from get{Music,Effects}VolumeDelta. They
    // carry an integer number of the oracle's 0..20 slider STEPS now, not a
    // 0..1 fraction, because the volume intents became absolute; +/-2 steps
    // is the old +/-0.1f exactly. Same one consumer, same thread.
    {"getMusicVolumeStepDelta", 1, "render thread (processInputDeltas)."},
    {"getEffectsVolumeStepDelta", 1, "render thread (processInputDeltas)."},
    // The one documented exception: two textual call sites, ONE consumer.
    // Both are in the same if/else in processFrameInput() -- the UI branch
    // consumes the notch, the else branch drains it so it cannot accumulate
    // while the new UI is off. They are mutually exclusive, so no notch is
    // ever read twice.
    {"getMouseWheelDelta", 2, "render thread only; if/else in processFrameInput, mutually exclusive."},
    // T421 (board B365): a SECOND textual call site, ONE consumer -- the
    // getMouseWheelDelta exception above, exactly. processChatInput() calls
    // processTradeskillSearchInput() FIRST and returns immediately when it
    // reports it holds the keyboard, so the two drains are mutually
    // exclusive by construction and no key is ever read twice. The two are
    // also the same widget: both loops feed applyTextEntryKey()
    // (include/client/graphics/ui/text_entry.h), which is why there is no
    // second entry mechanism to keep in step.
    // B433: processChatInput() early-returns into processOptionsKeyCapture()
    // (the Options Keys page) exactly like the tradeskill search boxes, so the
    // capture is a THIRD mutually exclusive drain of the same frame, never a
    // second consumer racing the first. It reads enter/slash/escape only to
    // discard them while it holds the keyboard.
    // B897: the Friends window's name Editboxes are a FOURTH drain of the
    // same shape -- processChatInput() early-returns into
    // processFriendsNameInput() while one of them holds the caret.
    {"popKeyEvent", 4, "render thread only; processChatInput early-returns into processOptionsKeyCapture (B433), processTradeskillSearchInput or processFriendsNameInput (B897), mutually exclusive. T241/T421."},
    {"enterKeyPressed", 2, "render thread (processChatInput, or processOptionsKeyCapture which it returns into, B433). T241."},
    {"slashKeyPressed", 2, "render thread (processChatInput, or processOptionsKeyCapture which it returns into, B433). T241."},
    {"escapeKeyPressed", 2, "render thread (processChatInput, or processOptionsKeyCapture which it returns into, B433). T241."},
};

}  // namespace

TEST(ReadAndResetSweepGuard, NoAccessorGrowsASecondCaller) {
    const fs::path src = fs::path(WILLEQ_REPO_ROOT) / "src";
    ASSERT_TRUE(fs::is_directory(src)) << src;

    // Concatenate every production translation unit, comments stripped.
    std::string all;
    for (const auto& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        const auto ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
        all += stripComments(readFile(e.path()));
        all.push_back('\n');
    }
    ASSERT_GT(all.size(), 100000u) << "source scan found almost nothing";

    for (const auto& entry : kSweep) {
        const int found = countCallSites(all, entry.accessor);
        EXPECT_EQ(found, entry.expectedCallSites)
            << "RendererEventReceiver::" << entry.accessor << "() is a destructive "
            << "read-and-reset. Expected " << entry.expectedCallSites
            << " call site(s) in src/, found " << found << ".\n"
            << "  Documented ownership: " << entry.note << "\n"
            << "A second caller STEALS the value from the first -- this is the "
            << "T230/T235/T312/T315 defect, four times over. Either remove the "
            << "new caller, or make the accumulator per-consumer, and update "
            << "kSweep in tests/test_read_and_reset_ownership.cpp.";
    }
}

// InputState was deleted by T315: it was written every game tick by three
// handlers and read by nobody (InputHandler::getState() had zero callers).
// Its click fields were the sink the stolen clicks vanished into. Pin the
// deletion so it cannot be reintroduced as "harmless bookkeeping".
TEST(ReadAndResetSweepGuard, NoInputStateSnapshotStructReturns) {
    const fs::path root = fs::path(WILLEQ_REPO_ROOT);
    for (const char* rel : {"include/client/input/input_handler.h",
                            "include/client/input/graphics_input_handler.h",
                            "include/client/input/console_input_handler.h",
                            "include/client/input/null_input_handler.h",
                            "src/client/input/graphics_input_handler.cpp",
                            "src/client/input/console_input_handler.cpp",
                            "src/client/input/null_input_handler.cpp"}) {
        const std::string code = stripComments(readFile(root / rel));
        EXPECT_EQ(code.find("InputState"), std::string::npos)
            << rel << " reintroduces InputState -- a write-only snapshot whose "
            << "click fields ate real mouse clicks (T315). If a consumer now "
            << "genuinely needs polled input state, wire the consumer first "
            << "and say which gesture proves it.";
    }
}

}  // namespace Graphics
}  // namespace EQT
