#include <gtest/gtest.h>
#include "common/util/random.h"
#include <set>
#include <map>
#include <cmath>

class RandomTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test Int with range
TEST_F(RandomTest, Int_InRange) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(1, 100);
        EXPECT_GE(value, 1);
        EXPECT_LE(value, 100);
    }
}

TEST_F(RandomTest, Int_SingleValue) {
    EQ::Random random;
    for (int i = 0; i < 100; i++) {
        int value = random.Int(42, 42);
        EXPECT_EQ(value, 42);
    }
}

TEST_F(RandomTest, Int_ZeroToN) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(0, 10);
        EXPECT_GE(value, 0);
        EXPECT_LE(value, 10);
    }
}

TEST_F(RandomTest, Int_NegativeRange) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(-10, -1);
        EXPECT_GE(value, -10);
        EXPECT_LE(value, -1);
    }
}

// Test Real for double range
TEST_F(RandomTest, Real_InRange) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        double value = random.Real(0.0, 1.0);
        EXPECT_GE(value, 0.0);
        EXPECT_LT(value, 1.0);  // Real is [low, high)
    }
}

TEST_F(RandomTest, Real_CustomRange) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        double value = random.Real(-100.0, 100.0);
        EXPECT_GE(value, -100.0);
        EXPECT_LT(value, 100.0);
    }
}

// Test Roll probability (int version - percent chance)
TEST_F(RandomTest, Roll_AlwaysPass) {
    EQ::Random random;
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(random.Roll(100));  // 100% chance
    }
}

TEST_F(RandomTest, Roll_NeverPass) {
    EQ::Random random;
    for (int i = 0; i < 100; i++) {
        EXPECT_FALSE(random.Roll(0));  // 0% chance
    }
}

TEST_F(RandomTest, Roll_FiftyPercent) {
    EQ::Random random;
    int successes = 0;
    for (int i = 0; i < 10000; i++) {
        if (random.Roll(50)) {  // 50% chance
            successes++;
        }
    }
    // Should be roughly 50% (4000-6000 range for 10000 trials)
    EXPECT_GT(successes, 4000);
    EXPECT_LT(successes, 6000);
}

// Test Roll probability (double version - 0.0-1.0)
TEST_F(RandomTest, Roll_Double_AlwaysPass) {
    EQ::Random random;
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(random.Roll(1.0));  // 100% chance
    }
}

TEST_F(RandomTest, Roll_Double_NeverPass) {
    EQ::Random random;
    for (int i = 0; i < 100; i++) {
        EXPECT_FALSE(random.Roll(0.0));  // 0% chance
    }
}

// Test distribution (basic uniformity check)
TEST_F(RandomTest, Distribution_Uniform) {
    EQ::Random random;
    std::map<int, int> counts;

    // Roll 10000 times on 1-10 range
    for (int i = 0; i < 10000; i++) {
        int value = random.Int(1, 10);
        counts[value]++;
    }

    // Each value should appear roughly 1000 times
    // Allow for 30% deviation
    for (int i = 1; i <= 10; i++) {
        EXPECT_GT(counts[i], 700);
        EXPECT_LT(counts[i], 1300);
    }
}

// Test uniqueness
TEST_F(RandomTest, Uniqueness) {
    EQ::Random random;
    std::set<int> values;

    // Generate 100 random numbers in range 1-10000
    for (int i = 0; i < 100; i++) {
        values.insert(random.Int(1, 10000));
    }

    // Should have mostly unique values (at least 90 unique out of 100)
    EXPECT_GT(values.size(), 90u);
}

// Test different instances produce different sequences
TEST_F(RandomTest, DifferentInstances) {
    EQ::Random random1;
    EQ::Random random2;

    // Different instances should produce different sequences
    // (unless they happen to have the same seed, which is unlikely)
    bool different = false;
    for (int i = 0; i < 100; i++) {
        if (random1.Int(1, 1000000) != random2.Int(1, 1000000)) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

// Test Roll0 (0 to max-1)
TEST_F(RandomTest, Roll0_InRange) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Roll0(10);  // 0-9
        EXPECT_GE(value, 0);
        EXPECT_LT(value, 10);
    }
}

TEST_F(RandomTest, Roll0_Zero) {
    EQ::Random random;
    // Roll0(0) or Roll0(1) should return 0
    EXPECT_EQ(random.Roll0(0), 0);
    EXPECT_EQ(random.Roll0(1), 0);
}

// Test d6, d20, d100 dice rolls
TEST_F(RandomTest, D6) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(1, 6);  // Standard D6
        EXPECT_GE(value, 1);
        EXPECT_LE(value, 6);
    }
}

TEST_F(RandomTest, D20) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(1, 20);  // Standard D20
        EXPECT_GE(value, 1);
        EXPECT_LE(value, 20);
    }
}

TEST_F(RandomTest, D100) {
    EQ::Random random;
    for (int i = 0; i < 1000; i++) {
        int value = random.Int(1, 100);  // Standard D100
        EXPECT_GE(value, 1);
        EXPECT_LE(value, 100);
    }
}

// Test thread safety (basic check - single instance single thread)
TEST_F(RandomTest, ThreadSafety) {
    EQ::Random random;
    bool valid = true;
    for (int i = 0; i < 10000; i++) {
        int value = random.Int(1, 100);
        if (value < 1 || value > 100) {
            valid = false;
            break;
        }
    }
    EXPECT_TRUE(valid);
}

// Test Reseed
TEST_F(RandomTest, Reseed) {
    EQ::Random random;
    random.Reseed();
    // Just verify reseed doesn't crash and still produces valid values
    int value = random.Int(1, 100);
    EXPECT_GE(value, 1);
    EXPECT_LE(value, 100);
}
