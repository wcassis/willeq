// B961 (GitLab #279): the device-side tally, exercised on fake bos (no GBM,
// no DRM) the way test_drm_flip_tracker.cpp does. The class is the client's
// source for the scanout framebuffers' bytes: the device registers a bo on
// its first lock, the gbm_bo_set_user_data callback removes it on free, and
// the published pair is the sum of the registered set.
#include "docker/irrlicht-drm/scanout_buffer_tally.h"
#include <gtest/gtest.h>
#include <cstdint>

namespace {
using Tally = eqt_drm::ScanoutBufferTally;

TEST(ScanoutBufferTally, RegisterAccumulatesStridesAndCounts) {
    Tally t;
    EXPECT_EQ(t.bufs(), 0u);
    EXPECT_EQ(t.bytes(), 0u);

    t.registerBo((void*)0x1000, 1280, 720);
    t.registerBo((void*)0x2000, 1280, 720);
    EXPECT_EQ(t.bufs(), 2u);
    EXPECT_EQ(t.bytes(), 2u * 1280u * 720u);

    // A third distinct buffer (the GBM pool is three).
    t.registerBo((void*)0x3000, 800, 480);
    EXPECT_EQ(t.bufs(), 3u);
    EXPECT_EQ(t.bytes(), 2u * 1280u * 720u + 800u * 480u);
}

TEST(ScanoutBufferTally, ReRegisteringTheSameBoDoesNotDoubleCount) {
    Tally t;
    t.registerBo((void*)0x1000, 1280, 720);
    // The device calls registerBo on every first-lock; a bo that stays
    // registered across frames must not grow the total.
    t.registerBo((void*)0x1000, 1280, 720);
    EXPECT_EQ(t.bufs(), 1u);
    EXPECT_EQ(t.bytes(), 1280u * 720u);
}

TEST(ScanoutBufferTally, ReRegisterWithNewSizeUpdatesTheByteCount) {
    Tally t;
    t.registerBo((void*)0x1000, 1280, 720);
    // A bo is never re-locked at a different size in practice, but the
    // class names the update rather than ignoring it (a mode change with
    // the pool kept would be exactly this).
    t.registerBo((void*)0x1000, 800, 480);
    EXPECT_EQ(t.bufs(), 1u);
    EXPECT_EQ(t.bytes(), 800u * 480u);
}

TEST(ScanoutBufferTally, DestroyRemovesOnlyItsOwnBo) {
    Tally t;
    t.registerBo((void*)0x1000, 1280, 720);
    t.registerBo((void*)0x2000, 640, 480);
    t.destroyCallback((void*)0x1000, nullptr);
    EXPECT_EQ(t.bufs(), 1u);
    EXPECT_EQ(t.bytes(), 640u * 480u);
    // An unknown bo is a no-op (a callback out of order must not evict).
    t.destroyCallback((void*)0x9999, nullptr);
    EXPECT_EQ(t.bufs(), 1u);
    t.destroyCallback((void*)0x2000, nullptr);
    EXPECT_EQ(t.bufs(), 0u);
    EXPECT_EQ(t.bytes(), 0u);
}

TEST(ScanoutBufferTally, ClearEmptiesTheWholeTable) {
    Tally t;
    t.registerBo((void*)0x1000, 1280, 720);
    t.registerBo((void*)0x2000, 640, 480);
    t.clear();
    EXPECT_EQ(t.bufs(), 0u);
    EXPECT_EQ(t.bytes(), 0u);
    // The slots are reusable after a clear.
    t.registerBo((void*)0x3000, 320, 240);
    EXPECT_EQ(t.bufs(), 1u);
    EXPECT_EQ(t.bytes(), 320u * 240u);
}

TEST(ScanoutBufferTally, NullBoIsIgnored) {
    Tally t;
    t.registerBo(nullptr, 1280, 720);
    t.destroyCallback(nullptr, nullptr);
    EXPECT_EQ(t.bufs(), 0u);
    EXPECT_EQ(t.bytes(), 0u);
}

// The C export the ledger reads: the device's strong definition reads the
// member tally, the core TU's weak default answers {0,0}. Here the tally is
// exercised through the exact accessors the strong definition calls
// (CIrrDeviceFB.cpp's eqtGetScanoutBufferTally is `bufs = t.bufs(); bytes =
// t.bytes();`).
TEST(ScanoutBufferTally, PublishedPairIsTheAccessorsTheCEXportReads) {
    Tally t;
    uint32_t bufs = 99;
    uint64_t bytes = 99;
    // Before the device publishes, the pair is zero -- the same answer the
    // weak default gives a deviceless binary.
    bufs = t.bufs();
    bytes = t.bytes();
    EXPECT_EQ(bufs, 0u);
    EXPECT_EQ(bytes, 0u);
    t.registerBo((void*)0x1000, 1280, 720);
    bufs = t.bufs();
    bytes = t.bytes();
    EXPECT_EQ(bufs, 1u);
    EXPECT_EQ(bytes, 1280u * 720u);
}

} // namespace
