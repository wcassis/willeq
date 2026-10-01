// B954 (GitLab #272): COpenGLES2Driver::createScreenShot() must not leak the
// frame it returns.
//
// The defect. createScreenShot() read the frame into `new u8[w*h*4]` and
// handed it to createImageFromData(..., ownForeignMemory=true,
// deleteMemory=false). CImage frees its data only when deleteMemory is true
// (CImage::~CImage), so every call leaked one whole frame, whatever the caller
// did with the image. RdpComponent::captureFrameForRDP() calls it every
// rendered frame while an RDP client is connected (~1.9 MB/frame at 800x600),
// and B952's Pi pixel test was OOM-killed at anon-rss 289 MB by it.
//
// The net. Take kCalls screenshots on a real headless GLES2 device, dropping
// each image as every caller does, and require the process's live heap to
// grow by less than ONE frame -- the pre-fix code grows it by kCalls frames.
// Measured two ways: glibc's in-use bytes (mallinfo2 uordblks + hblkhd, which
// sees the frame whether glibc served it from the heap or from mmap) and, when
// the build has it, the memory ledger's live total (it charges every block).
//
// Red/green, measured: with deleteMemory back to false the in-use growth is
// kCalls x kW x kH x 4 bytes and both assertions fail.
//
// Self-skips when a GLES2 device is unavailable, like
// test_alpha_blend_companion.cpp.

#include <gtest/gtest.h>

#include "client/core/memory_ledger.h"

#include <irrlicht.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr irr::u32 kW = 512;
constexpr irr::u32 kH = 384;
constexpr int kCalls = 16;
constexpr int64_t kFrameBytes = static_cast<int64_t>(kW) * kH * 4;

struct Heap {
    int64_t inUse = 0;
    int64_t ledger = 0;
    int64_t rssAnon = 0;
};

Heap measure() {
    const eqt::memory::Snapshot s = eqt::memory::snapshot("b954_screenshot_leak");
    Heap h;
    h.inUse = static_cast<int64_t>(s.mallocInUseBytes);
    h.ledger = static_cast<int64_t>(s.ledgerTotalBytes);
    h.rssAnon = static_cast<int64_t>(s.rssAnonBytes);
    return h;
}

}  // namespace

class ScreenshotLeakTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;  // surfaceless-EGL idiom
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) - skipping.";
        }
        driver_ = device_->getVideoDriver();
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // beginScene -> endScene -> createScreenShot -> drop: the RDP capture's
    // shape. Returns false when a screenshot came back null or the wrong size.
    bool shoot(int n) {
        for (int i = 0; i < n; ++i) {
            driver_->beginScene(true, true, irr::video::SColor(255, 10, 20, 30));
            driver_->endScene();
            irr::video::IImage* img = driver_->createScreenShot();
            if (!img) return false;
            const bool ok = img->getDimension().Width == kW && img->getDimension().Height == kH;
            img->drop();
            if (!ok) return false;
        }
        return true;
    }
};

TEST_F(ScreenshotLeakTest, RepeatedScreenshotsDoNotGrowTheHeap) {
    ASSERT_EQ(driver_->getScreenSize().Width, kW);
    ASSERT_EQ(driver_->getScreenSize().Height, kH);
    // Warm up: the driver's and llvmpipe's first readback allocate state that
    // lives as long as the context.
    ASSERT_TRUE(shoot(2));
    const Heap before = measure();
    ASSERT_TRUE(shoot(kCalls));
    const Heap after = measure();

    const int64_t inUseGrowth = after.inUse - before.inUse;
    const int64_t ledgerGrowth = after.ledger - before.ledger;
    std::printf("[b954] frame=%lld B calls=%d in_use_growth=%lld B ledger_growth=%lld B "
                "rss_anon_growth=%lld B ledger_enabled=%d\n",
                static_cast<long long>(kFrameBytes), kCalls,
                static_cast<long long>(inUseGrowth), static_cast<long long>(ledgerGrowth),
                static_cast<long long>(after.rssAnon - before.rssAnon),
                eqt::memory::enabled() ? 1 : 0);

    EXPECT_LT(inUseGrowth, kFrameBytes)
        << kCalls << " createScreenShot() calls grew glibc's in-use heap by " << inUseGrowth
        << " B; one leaked frame is " << kFrameBytes << " B (the pre-B954 leak is "
        << kCalls * kFrameBytes << " B).";
    if (eqt::memory::enabled()) {
        EXPECT_LT(ledgerGrowth, kFrameBytes)
            << "the memory ledger's live total grew by " << ledgerGrowth << " B over " << kCalls
            << " screenshots.";
    }
}

// The image a caller gets must still own readable pixels after the fix: the
// buffer is freed by the image's destructor, not before.
TEST_F(ScreenshotLeakTest, ScreenshotStillCarriesTheFrame) {
    driver_->beginScene(true, true, irr::video::SColor(255, 10, 20, 30));
    driver_->endScene();
    irr::video::IImage* img = driver_->createScreenShot();
    ASSERT_NE(img, nullptr);
    const irr::video::SColor c = img->getPixel(kW / 2, kH / 2);
    img->drop();
    EXPECT_EQ(c.getRed(), 10u);
    EXPECT_EQ(c.getGreen(), 20u);
    EXPECT_EQ(c.getBlue(), 30u);
}
