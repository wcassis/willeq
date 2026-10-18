#pragma once

#include <cstdint>

namespace eqt_drm {

// Buffer ownership only. KMS/GBM operations happen in the device after their
// success is known; a pending flip never makes its displaced buffer releasable.
template <typename Buffer>
class FlipTracker {
public:
    enum class TimeoutAction { Wait, Recover };

    bool pending() const { return pending_; }
    Buffer* previous() const { return previous_; }
    std::uintptr_t serial() const { return serial_; }
    std::uintptr_t nextSerial() const { return serial_ + 1 ? serial_ + 1 : 1; }

    void onFlipSubmitted(Buffer* bo) {
        retired_ = previous_;
        previous_ = bo;
        pending_ = true;
        timeouts_ = 0;
        serial_ = nextSerial();
    }

    // A stale event from before recovery cannot complete a later flip.
    Buffer* onFlipComplete(std::uintptr_t serial) {
        if (!pending_ || serial != serial_)
            return nullptr;
        pending_ = false;
        timeouts_ = 0;
        Buffer* release = retired_;
        retired_ = nullptr;
        return release;
    }

    TimeoutAction onTimeout() {
        if (!pending_)
            return TimeoutAction::Wait;
        if (timeouts_ < 3)
            ++timeouts_;
        return timeouts_ == 3 ? TimeoutAction::Recover : TimeoutAction::Wait;
    }

    // Call only after a successful synchronous SetCrtc. For recovery bo is
    // previous_; for initial/fallback SetCrtc it is a newly locked front bo.
    struct Releases { Buffer* retired; Buffer* previous; };
    Releases onSetCrtcSync(Buffer* bo) {
        Releases releases{retired_, previous_ == bo ? nullptr : previous_};
        retired_ = nullptr;
        previous_ = bo;
        pending_ = false;
        timeouts_ = 0;
        return releases;
    }

    // Only after the saved CRTC has been restored at shutdown.
    Releases onCrtcRestored() {
        Releases releases{retired_, previous_};
        retired_ = nullptr;
        previous_ = nullptr;
        pending_ = false;
        timeouts_ = 0;
        return releases;
    }

private:
    bool pending_ = false;
    Buffer* retired_ = nullptr;
    Buffer* previous_ = nullptr;
    unsigned timeouts_ = 0;
    std::uintptr_t serial_ = 0;
};

} // namespace eqt_drm
