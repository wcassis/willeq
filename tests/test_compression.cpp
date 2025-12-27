#include <gtest/gtest.h>
#include "common/util/compression.h"
#include <string>
#include <vector>
#include <cstring>

class CompressionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test basic compression/decompression roundtrip
TEST_F(CompressionTest, RoundTrip_SmallData) {
    std::string original = "Hello, World! This is a test string for compression.";
    std::vector<char> input(original.begin(), original.end());

    // Estimate buffer size and allocate
    uint32_t out_len_max = EQ::EstimateDeflateBuffer(input.size());
    std::vector<char> compressed(out_len_max);

    // Compress
    uint32_t compressed_size = EQ::DeflateData(input.data(), input.size(), compressed.data(), out_len_max);
    ASSERT_GT(compressed_size, 0u);

    // Decompress (we need to know the expected size, use larger buffer)
    std::vector<char> decompressed(input.size() * 2);
    uint32_t decompressed_size = EQ::InflateData(compressed.data(), compressed_size, decompressed.data(), decompressed.size());
    ASSERT_EQ(decompressed_size, input.size());

    // Compare
    std::string result(decompressed.data(), decompressed_size);
    EXPECT_EQ(result, original);
}

TEST_F(CompressionTest, RoundTrip_LargeData) {
    // Create 10KB of repetitive data (should compress well)
    std::string pattern = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string original;
    for (int i = 0; i < 400; i++) {
        original += pattern;
    }

    std::vector<char> input(original.begin(), original.end());

    // Estimate buffer size and allocate
    uint32_t out_len_max = EQ::EstimateDeflateBuffer(input.size());
    std::vector<char> compressed(out_len_max);

    // Compress
    uint32_t compressed_size = EQ::DeflateData(input.data(), input.size(), compressed.data(), out_len_max);
    ASSERT_GT(compressed_size, 0u);
    EXPECT_LT(compressed_size, input.size());  // Should be smaller than original

    // Decompress
    std::vector<char> decompressed(input.size() * 2);
    uint32_t decompressed_size = EQ::InflateData(compressed.data(), compressed_size, decompressed.data(), decompressed.size());
    ASSERT_EQ(decompressed_size, input.size());

    // Compare
    EXPECT_EQ(memcmp(decompressed.data(), input.data(), input.size()), 0);
}

TEST_F(CompressionTest, RoundTrip_RandomData) {
    // Create random data (won't compress well)
    std::vector<char> input(1024);
    for (size_t i = 0; i < input.size(); i++) {
        input[i] = static_cast<char>(rand() % 256);
    }

    // Estimate buffer size and allocate
    uint32_t out_len_max = EQ::EstimateDeflateBuffer(input.size());
    std::vector<char> compressed(out_len_max);

    // Compress
    uint32_t compressed_size = EQ::DeflateData(input.data(), input.size(), compressed.data(), out_len_max);
    ASSERT_GT(compressed_size, 0u);

    // Decompress
    std::vector<char> decompressed(input.size() * 2);
    uint32_t decompressed_size = EQ::InflateData(compressed.data(), compressed_size, decompressed.data(), decompressed.size());
    ASSERT_EQ(decompressed_size, input.size());

    // Compare
    EXPECT_EQ(memcmp(decompressed.data(), input.data(), input.size()), 0);
}

TEST_F(CompressionTest, EstimateDeflateBuffer) {
    // Test that estimate gives reasonable values
    EXPECT_GT(EQ::EstimateDeflateBuffer(100), 100u);
    EXPECT_GT(EQ::EstimateDeflateBuffer(1000), 1000u);
    EXPECT_GT(EQ::EstimateDeflateBuffer(10000), 10000u);
}

TEST_F(CompressionTest, SingleByte) {
    std::vector<char> input = {'X'};

    uint32_t out_len_max = EQ::EstimateDeflateBuffer(input.size());
    std::vector<char> compressed(out_len_max);

    uint32_t compressed_size = EQ::DeflateData(input.data(), input.size(), compressed.data(), out_len_max);
    ASSERT_GT(compressed_size, 0u);

    std::vector<char> decompressed(10);
    uint32_t decompressed_size = EQ::InflateData(compressed.data(), compressed_size, decompressed.data(), decompressed.size());
    ASSERT_EQ(decompressed_size, 1u);
    EXPECT_EQ(decompressed[0], 'X');
}
