#include <gtest/gtest.h>
#include "common/util/data_verification.h"
#include <cmath>
#include <limits>

class DataVerificationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// T458: the ValueWithin cases that stood here pinned EQ::ValueWithin, which
// had no production caller once src/client/position.cpp's own same-named
// template went with IsWithinAxisAlignedBox (the B503 cascade); the function
// and those eleven cases were deleted together.

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
