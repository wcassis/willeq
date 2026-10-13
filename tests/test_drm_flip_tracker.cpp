#include "docker/irrlicht-drm/drm_flip_tracker.h"
#include <gtest/gtest.h>

namespace {
struct Bo { int id; };
using Tracker = eqt_drm::FlipTracker<Bo>;

TEST(DRMFlipTracker, NormalFlipReleasesDisplacedBufferOnce) {
    Bo first{1}, second{2};
    Tracker tracker;
    EXPECT_EQ(tracker.onSetCrtcSync(&first).previous, nullptr);
    tracker.onFlipSubmitted(&second);
    EXPECT_TRUE(tracker.pending());
    EXPECT_EQ(tracker.onFlipComplete(tracker.serial()), &first);
    EXPECT_FALSE(tracker.pending());
    EXPECT_EQ(tracker.onFlipComplete(tracker.serial()), nullptr);
    EXPECT_EQ(tracker.previous(), &second);
}

TEST(DRMFlipTracker, FallbackSetCrtcReleasesPreviousImmediately) {
    Bo first{1}, second{2};
    Tracker tracker;
    tracker.onSetCrtcSync(&first);
    const auto releases = tracker.onSetCrtcSync(&second);
    EXPECT_EQ(releases.previous, &first);
    EXPECT_EQ(releases.retired, nullptr);
    EXPECT_EQ(tracker.onCrtcRestored().previous, &second);
    EXPECT_EQ(tracker.onCrtcRestored().previous, nullptr);
}

TEST(DRMFlipTracker, LostEventRecoversOnlyAfterThirdTimeoutAndSync) {
    Bo first{1}, second{2}, third{3};
    Tracker tracker;
    tracker.onSetCrtcSync(&first);
    tracker.onFlipSubmitted(&second);
    const auto lostSerial = tracker.serial();
    EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Wait);
    EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Wait);
    EXPECT_TRUE(tracker.pending());
    EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Recover);
    EXPECT_TRUE(tracker.pending());
    const auto releases = tracker.onSetCrtcSync(tracker.previous());
    EXPECT_EQ(releases.retired, &first);
    EXPECT_EQ(releases.previous, nullptr);
    EXPECT_FALSE(tracker.pending());
    EXPECT_EQ(tracker.onFlipComplete(lostSerial), nullptr);

    tracker.onFlipSubmitted(&third);
    EXPECT_EQ(tracker.onFlipComplete(lostSerial), nullptr);
    EXPECT_TRUE(tracker.pending());
    EXPECT_EQ(tracker.onFlipComplete(tracker.serial()), &second);
    EXPECT_EQ(tracker.onFlipComplete(tracker.serial()), nullptr);
}

TEST(DRMFlipTracker, SecondLostEventRecoversAndNoDoubleRelease) {
    Bo first{1}, second{2}, third{3};
    Tracker tracker;
    tracker.onSetCrtcSync(&first);
    for (Bo* bo : {&second, &third}) {
        tracker.onFlipSubmitted(bo);
        EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Wait);
        EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Wait);
        EXPECT_EQ(tracker.onTimeout(), Tracker::TimeoutAction::Recover);
        const auto releases = tracker.onSetCrtcSync(tracker.previous());
        EXPECT_NE(releases.retired, nullptr);
        EXPECT_EQ(releases.previous, nullptr);
        EXPECT_EQ(tracker.onFlipComplete(tracker.serial()), nullptr);
    }
    const auto final = tracker.onCrtcRestored();
    EXPECT_EQ(final.retired, nullptr);
    EXPECT_EQ(final.previous, &third);
    EXPECT_EQ(tracker.onCrtcRestored().previous, nullptr);
}
} // namespace
