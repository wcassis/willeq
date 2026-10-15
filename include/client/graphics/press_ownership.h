// press_ownership.h - who owns a held mouse button: the static UI or the
// camera (B1066, GitLab #386).
//
// A mouse press is ONE gesture with ONE owner, decided by where it LANDED:
//
//   - a press on a static-UI window (any panel the UI hit-tests, the HUD
//     panels included) is UI-OWNED until its release. The UI acts on it (a
//     right press on an item slot opens the item display, a left press clicks
//     the slot), and the camera's button-drag mouse-look ignores it, however
//     far the pointer then travels.
//   - a press on the 3D view is CAMERA-OWNED until its release, and keeps
//     looking when the pointer later crosses a window (the press owner
//     decides, not the pointer's current position -- the official client's
//     behaviour as #386 states it; not yet re-measured on the oracle, see
//     refactor/B1066/README.md).
//
// Before B1066 the event receiver latched one HELD flag per button on every
// press wherever it landed, the static UI consumed only the CLICK latch, and
// mouse-look read the held flag -- so a right-click on an inventory item while
// the mouse moved a few pixels also turned the view (Will, 2026-10-14).
//
// Ownership cannot be decided inside the event receiver: the hit test that
// knows "this pixel is a window" is the static UI's own, which runs once per
// frame in FrameComponent::renderStaticUI(). So a new press is PENDING until
// that frame settles it with resolve(); a pending press is neither owner's
// yet, so it neither looks nor counts as a UI hold. The receiver resets the
// mouse-look baseline at the press, so a press that resolves to the camera
// loses none of the motion made while it was pending.
//
// Header-only and device-free (no Irrlicht), so the unit test
// (tests/test_press_ownership.cpp) runs in every build configuration.

#pragma once

#include <atomic>
#include <cstdint>

namespace EQT {
namespace Graphics {

enum class PressButton : uint8_t { Left = 0, Right = 1 };

enum class PressOwner : uint8_t {
    None = 0,   // not held
    Pending,    // held; the frame has not yet said where it landed
    Camera,     // landed on the 3D view: mouse-look follows it
    Ui,         // landed on a static-UI window: mouse-look ignores it
};

class PressOwnership {
public:
    /** A new press of `b`. Pending until the next resolve(). */
    void press(PressButton b) { slot(b).store(PressOwner::Pending, std::memory_order_relaxed); }

    /**
     * A move event reports `b` held although no press was seen (the press
     * happened outside the window, or before focus). Treated as a new press
     * when the button is not already held; an owned press keeps its owner.
     */
    void heldSeen(PressButton b) {
        PressOwner expected = PressOwner::None;
        slot(b).compare_exchange_strong(expected, PressOwner::Pending, std::memory_order_relaxed);
    }

    /** `b` was released: whoever owned it, the gesture is over. */
    void release(PressButton b) { slot(b).store(PressOwner::None, std::memory_order_relaxed); }

    /**
     * Once per frame, after the static UI's hit test: every PENDING press is
     * given to the UI when the pointer is over a window, else to the camera.
     * An already-owned press is never moved. Returns true when it settled one.
     */
    bool resolve(bool pointerOnUi) {
        const PressOwner to = pointerOnUi ? PressOwner::Ui : PressOwner::Camera;
        bool any = false;
        for (auto& s : owner_) {
            PressOwner expected = PressOwner::Pending;
            any |= s.compare_exchange_strong(expected, to, std::memory_order_relaxed);
        }
        return any;
    }

    PressOwner owner(PressButton b) const { return slot(b).load(std::memory_order_relaxed); }
    /** Physically held, whoever owns it (the item display's lifetime reads this). */
    bool held(PressButton b) const { return owner(b) != PressOwner::None; }
    /** Held AND camera-owned: the only state button-drag mouse-look may read. */
    bool cameraHeld(PressButton b) const { return owner(b) == PressOwner::Camera; }

private:
    std::atomic<PressOwner>& slot(PressButton b) { return owner_[static_cast<int>(b)]; }
    const std::atomic<PressOwner>& slot(PressButton b) const { return owner_[static_cast<int>(b)]; }

    std::atomic<PressOwner> owner_[2] = {PressOwner::None, PressOwner::None};
};

}  // namespace Graphics
}  // namespace EQT
