#include "client/graphics/lima_buffer_policy.h"

#include <gtest/gtest.h>
#include <fstream>
#include <iterator>

namespace {
class LimaBufferPolicy : public ::testing::Test {
    bool had_ = false;
    std::string saved_;
protected:
    void SetUp() override {
        const char* value = std::getenv("LIMA_DEBUG");
        had_ = value != nullptr;
        if (value) saved_ = value;
        ::unsetenv("LIMA_DEBUG");
    }
    void TearDown() override {
        if (had_) ::setenv("LIMA_DEBUG", saved_.c_str(), 1);
        else ::unsetenv("LIMA_DEBUG");
    }
};

TEST_F(LimaBufferPolicy, DrmReleasesFreedBuffers) {
    ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(true));
    EXPECT_STREQ(std::getenv("LIMA_DEBUG"), "nobocache");
}

TEST_F(LimaBufferPolicy, NonDrmDoesNotChangeEnvironment) {
    ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(false));
    EXPECT_EQ(std::getenv("LIMA_DEBUG"), nullptr);
    ::setenv("LIMA_DEBUG", "shaderdb", 1);
    ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(false));
    EXPECT_STREQ(std::getenv("LIMA_DEBUG"), "shaderdb");
}

TEST_F(LimaBufferPolicy, PreservesDebugOptionsAndIsIdempotent) {
    for (const char* input : {"", "shaderdb", "bo_cache,shaderdb", "notnobocache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(true));
        const std::string expected = std::string(input) + (*input ? "," : "") + "nobocache";
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), expected.c_str());
        ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(true));
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), expected.c_str());
    }
    for (const char* input : {"all", "nobocache", "shaderdb,nobocache", "nobocache shaderdb",
                              "shaderdb\tnobocache\nbo_cache"}) {
        ::setenv("LIMA_DEBUG", input, 1);
        ASSERT_TRUE(EQT::Graphics::configureLimaBufferPolicy(true));
        EXPECT_STREQ(std::getenv("LIMA_DEBUG"), input);
    }
}

TEST(LimaBufferPolicyOrder, ConfiguredBeforeDeviceCreation) {
    std::ifstream file(std::string(B986_SOURCE_DIR) + "/src/client/graphics/renderer/renderer_init.cpp");
    ASSERT_TRUE(file.good());
    const std::string source((std::istreambuf_iterator<char>(file)), {});
    const auto policy = source.find("configureLimaBufferPolicy(config_.constrainedConfig.useDRM)");
    const auto create = source.find("device_ = irr::createDeviceEx(params)");
    ASSERT_NE(policy, std::string::npos);
    ASSERT_NE(create, std::string::npos);
    EXPECT_LT(policy, create);
}
} // namespace
