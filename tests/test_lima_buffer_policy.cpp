#include "client/graphics/lima_buffer_policy.h"

#include <gtest/gtest.h>
#include <fstream>
#include <iterator>

// B988 (Will 2026-10-05): Lima's BO cache is ON by default; `nobocache` is
// opt-in through WILLEQ_LIMA_NOBOCACHE=1 (B986's policy, unchanged when set).

namespace {
using EQT::Graphics::LimaBoCachePolicy;
using EQT::Graphics::configureLimaBufferPolicy;

class SavedEnv {
    const char* name_;
    bool had_ = false;
    std::string saved_;
public:
    explicit SavedEnv(const char* name) : name_(name) {
        const char* value = std::getenv(name_);
        had_ = value != nullptr;
        if (value) saved_ = value;
        ::unsetenv(name_);
    }
    ~SavedEnv() {
        if (had_) ::setenv(name_, saved_.c_str(), 1);
        else ::unsetenv(name_);
    }
};

class LimaBufferPolicy : public ::testing::Test {
    SavedEnv limaDebug_{"LIMA_DEBUG"};
    SavedEnv optIn_{"WILLEQ_LIMA_NOBOCACHE"};
};

TEST_F(LimaBufferPolicy, DefaultLeavesUnsetLimaDebugUnset) {
    const auto r = configureLimaBufferPolicy(true);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.policy, LimaBoCachePolicy::CacheOn);
    EXPECT_EQ(std::getenv("LIMA_DEBUG"), nullptr);
}

TEST_F(LimaBufferPolicy, DefaultLeavesExistingFlagsUntouched) {
    for (const char* input : {"", "shaderdb", "bo_cache,shaderdb", "notnobocache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        const auto r = configureLimaBufferPolicy(true);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.policy, LimaBoCachePolicy::CacheOn) << input;
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), input);
    }
}

TEST_F(LimaBufferPolicy, SwitchOtherThanOneIsOff) {
    for (const char* value : {"", "0", "no"}) {
        ::setenv("WILLEQ_LIMA_NOBOCACHE", value, 1);
        ::unsetenv("LIMA_DEBUG");
        EXPECT_EQ(configureLimaBufferPolicy(true).policy, LimaBoCachePolicy::CacheOn) << value;
        EXPECT_EQ(std::getenv("LIMA_DEBUG"), nullptr) << value;
    }
}

TEST_F(LimaBufferPolicy, CallerNoBoCacheWorksWithoutSwitch) {
    for (const char* input : {"all", "nobocache", "shaderdb,nobocache", "nobocache shaderdb",
                              "shaderdb\tnobocache\nbo_cache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        const auto r = configureLimaBufferPolicy(true);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.policy, LimaBoCachePolicy::NoBoCacheCaller) << input;
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), input);
    }
}

TEST_F(LimaBufferPolicy, OptInReleasesFreedBuffers) {
    ::setenv("WILLEQ_LIMA_NOBOCACHE", "1", 1);
    const auto r = configureLimaBufferPolicy(true);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.policy, LimaBoCachePolicy::NoBoCacheSwitch);
    EXPECT_STREQ(std::getenv("LIMA_DEBUG"), "nobocache");
}

TEST_F(LimaBufferPolicy, OptInPreservesDebugOptionsAndIsIdempotent) {
    ::setenv("WILLEQ_LIMA_NOBOCACHE", "1", 1);
    for (const char* input : {"", "shaderdb", "bo_cache,shaderdb", "notnobocache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        ASSERT_TRUE(configureLimaBufferPolicy(true).ok);
        const std::string expected = std::string(input) + (*input ? "," : "") + "nobocache";
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), expected.c_str());
        ASSERT_TRUE(configureLimaBufferPolicy(true).ok);
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), expected.c_str());
    }
    // Already disabled (incl. Mesa's special "all"): kept exactly as given.
    for (const char* input : {"all", "nobocache", "shaderdb,nobocache", "nobocache shaderdb",
                              "shaderdb\tnobocache\nbo_cache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        ASSERT_TRUE(configureLimaBufferPolicy(true).ok);
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), input);
    }
}

TEST_F(LimaBufferPolicy, NonDrmDoesNotChangeEnvironmentEvenWithSwitch) {
    for (const char* sw : {static_cast<const char*>(nullptr), "1"}) {
        if (sw) ::setenv("WILLEQ_LIMA_NOBOCACHE", sw, 1);
        ::unsetenv("LIMA_DEBUG");
        auto r = configureLimaBufferPolicy(false);
        ASSERT_TRUE(r.ok);
        EXPECT_EQ(r.policy, LimaBoCachePolicy::NotDrm);
        EXPECT_EQ(std::getenv("LIMA_DEBUG"), nullptr);
        ::setenv("LIMA_DEBUG", "shaderdb", 1);
        ASSERT_TRUE(configureLimaBufferPolicy(false).ok);
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), "shaderdb");
    }
}

TEST(LimaBufferPolicyOrder, ConfiguredBeforeDeviceCreation) {
    std::ifstream file(std::string(B986_SOURCE_DIR) + "/src/client/graphics/renderer/renderer_init.cpp");
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    const auto policy = source.find("configureLimaBufferPolicy(config_.constrainedConfig.useDRM)");
    const auto logged = source.find("describeLimaBufferPolicy(limaPolicy.policy)");
    const auto create = source.find("device_ = irr::createDeviceEx(params)");
    ASSERT_NE(policy, std::string::npos);
    ASSERT_NE(logged, std::string::npos);
    ASSERT_NE(create, std::string::npos);
    EXPECT_LT(policy, create);
    EXPECT_LT(logged, create);
}

TEST(LimaBufferPolicyOrder, BenchmarkFollowsTheClientPolicy) {
    std::ifstream file(std::string(B986_SOURCE_DIR) + "/tools/hwcaps/nametag_drawcost.cpp");
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    const auto policy = source.find("configureLimaBufferPolicy(o.drm)");
    const auto create = source.find("irr::createDeviceEx(params)");
    ASSERT_NE(policy, std::string::npos);
    ASSERT_NE(create, std::string::npos);
    EXPECT_LT(policy, create);
}
} // namespace
