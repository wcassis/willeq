#include <gtest/gtest.h>
#include "common/util/data_verification.h"
#include <cmath>
#include <limits>

class DataVerificationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test ValueWithin for integers
TEST_F(DataVerificationTest, ValueWithin_Int_InRange) {
    EXPECT_TRUE(EQ::ValueWithin(5, 0, 10));
    EXPECT_TRUE(EQ::ValueWithin(0, 0, 10));
    EXPECT_TRUE(EQ::ValueWithin(10, 0, 10));
}

TEST_F(DataVerificationTest, ValueWithin_Int_OutOfRange) {
    EXPECT_FALSE(EQ::ValueWithin(-1, 0, 10));
    EXPECT_FALSE(EQ::ValueWithin(11, 0, 10));
}

TEST_F(DataVerificationTest, ValueWithin_Int_NegativeRange) {
    EXPECT_TRUE(EQ::ValueWithin(-5, -10, 0));
    EXPECT_TRUE(EQ::ValueWithin(-10, -10, 0));
    EXPECT_TRUE(EQ::ValueWithin(0, -10, 0));
    EXPECT_FALSE(EQ::ValueWithin(-11, -10, 0));
    EXPECT_FALSE(EQ::ValueWithin(1, -10, 0));
}

// Test ValueWithin for floats
TEST_F(DataVerificationTest, ValueWithin_Float_InRange) {
    EXPECT_TRUE(EQ::ValueWithin(5.5f, 0.0f, 10.0f));
    EXPECT_TRUE(EQ::ValueWithin(0.0f, 0.0f, 10.0f));
    EXPECT_TRUE(EQ::ValueWithin(10.0f, 0.0f, 10.0f));
}

TEST_F(DataVerificationTest, ValueWithin_Float_OutOfRange) {
    EXPECT_FALSE(EQ::ValueWithin(-0.001f, 0.0f, 10.0f));
    EXPECT_FALSE(EQ::ValueWithin(10.001f, 0.0f, 10.0f));
}

// Test Clamp for integers
TEST_F(DataVerificationTest, Clamp_Int_InRange) {
    EXPECT_EQ(EQ::Clamp(5, 0, 10), 5);
    EXPECT_EQ(EQ::Clamp(0, 0, 10), 0);
    EXPECT_EQ(EQ::Clamp(10, 0, 10), 10);
}

TEST_F(DataVerificationTest, Clamp_Int_BelowMin) {
    EXPECT_EQ(EQ::Clamp(-5, 0, 10), 0);
    EXPECT_EQ(EQ::Clamp(-100, 0, 10), 0);
}

TEST_F(DataVerificationTest, Clamp_Int_AboveMax) {
    EXPECT_EQ(EQ::Clamp(15, 0, 10), 10);
    EXPECT_EQ(EQ::Clamp(100, 0, 10), 10);
}

// Test Clamp for floats
TEST_F(DataVerificationTest, Clamp_Float_InRange) {
    EXPECT_FLOAT_EQ(EQ::Clamp(5.5f, 0.0f, 10.0f), 5.5f);
}

TEST_F(DataVerificationTest, Clamp_Float_BelowMin) {
    EXPECT_FLOAT_EQ(EQ::Clamp(-5.0f, 0.0f, 10.0f), 0.0f);
}

TEST_F(DataVerificationTest, Clamp_Float_AboveMax) {
    EXPECT_FLOAT_EQ(EQ::Clamp(15.0f, 0.0f, 10.0f), 10.0f);
}

// Test IsValid for various types (if implemented)
TEST_F(DataVerificationTest, IsValidPosition) {
    // Valid position range
    EXPECT_TRUE(EQ::ValueWithin(0.0f, -32000.0f, 32000.0f));
    EXPECT_TRUE(EQ::ValueWithin(1000.0f, -32000.0f, 32000.0f));
    EXPECT_TRUE(EQ::ValueWithin(-1000.0f, -32000.0f, 32000.0f));

    // Invalid position (out of EQ world bounds)
    EXPECT_FALSE(EQ::ValueWithin(100000.0f, -32000.0f, 32000.0f));
}

TEST_F(DataVerificationTest, IsValidHeading) {
    // EQ headings are 0-512
    EXPECT_TRUE(EQ::ValueWithin(0.0f, 0.0f, 512.0f));
    EXPECT_TRUE(EQ::ValueWithin(256.0f, 0.0f, 512.0f));
    EXPECT_TRUE(EQ::ValueWithin(512.0f, 0.0f, 512.0f));
    EXPECT_FALSE(EQ::ValueWithin(-1.0f, 0.0f, 512.0f));
    EXPECT_FALSE(EQ::ValueWithin(513.0f, 0.0f, 512.0f));
}

TEST_F(DataVerificationTest, IsValidHP) {
    // HP should be non-negative
    EXPECT_TRUE(EQ::ValueWithin(0, 0, 100000));
    EXPECT_TRUE(EQ::ValueWithin(100, 0, 100000));
    EXPECT_FALSE(EQ::ValueWithin(-1, 0, 100000));
}

// Edge cases
TEST_F(DataVerificationTest, EdgeCase_ZeroRange) {
    EXPECT_TRUE(EQ::ValueWithin(5, 5, 5));
    EXPECT_FALSE(EQ::ValueWithin(4, 5, 5));
    EXPECT_FALSE(EQ::ValueWithin(6, 5, 5));
}

TEST_F(DataVerificationTest, EdgeCase_MaxInt) {
    int max_int = std::numeric_limits<int>::max();
    int min_int = std::numeric_limits<int>::min();
    EXPECT_TRUE(EQ::ValueWithin(0, min_int, max_int));
    EXPECT_TRUE(EQ::ValueWithin(max_int, min_int, max_int));
    EXPECT_TRUE(EQ::ValueWithin(min_int, min_int, max_int));
}

TEST_F(DataVerificationTest, EdgeCase_InfinityFloat) {
    float inf = std::numeric_limits<float>::infinity();
    float neg_inf = -std::numeric_limits<float>::infinity();
    // Infinity should be outside any reasonable range
    EXPECT_FALSE(EQ::ValueWithin(inf, -1000.0f, 1000.0f));
    EXPECT_FALSE(EQ::ValueWithin(neg_inf, -1000.0f, 1000.0f));
}
