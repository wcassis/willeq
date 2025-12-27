#include <gtest/gtest.h>
#include "common/util/strings.h"

class StringsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test BeginsWith
TEST_F(StringsTest, BeginsWith_PositiveMatch) {
    EXPECT_TRUE(Strings::BeginsWith("hello world", "hello"));
    EXPECT_TRUE(Strings::BeginsWith("test", "test"));
    EXPECT_TRUE(Strings::BeginsWith("abc", ""));
}

TEST_F(StringsTest, BeginsWith_NegativeMatch) {
    EXPECT_FALSE(Strings::BeginsWith("hello world", "world"));
    EXPECT_FALSE(Strings::BeginsWith("test", "testing"));
    EXPECT_FALSE(Strings::BeginsWith("", "test"));
}

// Test EndsWith
TEST_F(StringsTest, EndsWith_PositiveMatch) {
    EXPECT_TRUE(Strings::EndsWith("hello world", "world"));
    EXPECT_TRUE(Strings::EndsWith("test", "test"));
    EXPECT_TRUE(Strings::EndsWith("abc", ""));
}

TEST_F(StringsTest, EndsWith_NegativeMatch) {
    EXPECT_FALSE(Strings::EndsWith("hello world", "hello"));
    EXPECT_FALSE(Strings::EndsWith("test", "testing"));
    EXPECT_FALSE(Strings::EndsWith("", "test"));
}

// Test Contains
TEST_F(StringsTest, Contains_PositiveMatch) {
    EXPECT_TRUE(Strings::Contains("hello world", "lo wo"));
    EXPECT_TRUE(Strings::Contains("test", "es"));
    EXPECT_TRUE(Strings::Contains("abc", ""));
}

TEST_F(StringsTest, Contains_NegativeMatch) {
    EXPECT_FALSE(Strings::Contains("hello world", "xyz"));
    EXPECT_FALSE(Strings::Contains("test", "xyz"));
    EXPECT_FALSE(Strings::Contains("", "test"));
}

// Test ToLower/ToUpper
TEST_F(StringsTest, ToLower) {
    EXPECT_EQ(Strings::ToLower("HELLO"), "hello");
    EXPECT_EQ(Strings::ToLower("Hello World"), "hello world");
    EXPECT_EQ(Strings::ToLower(""), "");
    EXPECT_EQ(Strings::ToLower("123"), "123");
}

TEST_F(StringsTest, ToUpper) {
    EXPECT_EQ(Strings::ToUpper("hello"), "HELLO");
    EXPECT_EQ(Strings::ToUpper("Hello World"), "HELLO WORLD");
    EXPECT_EQ(Strings::ToUpper(""), "");
    EXPECT_EQ(Strings::ToUpper("123"), "123");
}

// Test Trim (takes non-const reference)
TEST_F(StringsTest, Trim) {
    std::string s1 = "  hello  ";
    EXPECT_EQ(Strings::Trim(s1), "hello");

    std::string s2 = "\t\nhello\r\n";
    EXPECT_EQ(Strings::Trim(s2), "hello");

    std::string s3 = "hello";
    EXPECT_EQ(Strings::Trim(s3), "hello");

    std::string s4 = "";
    EXPECT_EQ(Strings::Trim(s4), "");
}

// Test Split
TEST_F(StringsTest, Split) {
    auto result = Strings::Split("a,b,c", ",");
    EXPECT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "c");
}

TEST_F(StringsTest, Split_EmptyString) {
    auto result = Strings::Split("", ",");
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "");
}

TEST_F(StringsTest, Split_NoDelimiter) {
    auto result = Strings::Split("hello", ",");
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "hello");
}

// Test Join
TEST_F(StringsTest, Join) {
    std::vector<std::string> parts = {"a", "b", "c"};
    EXPECT_EQ(Strings::Join(parts, ","), "a,b,c");
    EXPECT_EQ(Strings::Join(parts, " "), "a b c");
}

TEST_F(StringsTest, Join_Empty) {
    std::vector<std::string> parts;
    EXPECT_EQ(Strings::Join(parts, ","), "");
}

// Test IsNumber
TEST_F(StringsTest, IsNumber) {
    EXPECT_TRUE(Strings::IsNumber("123"));
    EXPECT_TRUE(Strings::IsNumber("0"));
    EXPECT_TRUE(Strings::IsNumber("-123"));
    EXPECT_FALSE(Strings::IsNumber("abc"));
    EXPECT_FALSE(Strings::IsNumber("12.34"));
    // Note: Implementation returns true for empty string (vacuous truth)
    EXPECT_TRUE(Strings::IsNumber(""));
}

// Test IsFloat
TEST_F(StringsTest, IsFloat) {
    EXPECT_TRUE(Strings::IsFloat("123.45"));
    EXPECT_TRUE(Strings::IsFloat("0.0"));
    EXPECT_TRUE(Strings::IsFloat("-123.45"));
    EXPECT_TRUE(Strings::IsFloat("123"));
    EXPECT_FALSE(Strings::IsFloat("abc"));
    // Note: Implementation returns true for empty string (vacuous truth)
    EXPECT_TRUE(Strings::IsFloat(""));
}

// Test ToInt/ToFloat
TEST_F(StringsTest, ToInt) {
    EXPECT_EQ(Strings::ToInt("123"), 123);
    EXPECT_EQ(Strings::ToInt("-456"), -456);
    EXPECT_EQ(Strings::ToInt("0"), 0);
    EXPECT_EQ(Strings::ToInt("invalid"), 0);
}

TEST_F(StringsTest, ToFloat) {
    EXPECT_FLOAT_EQ(Strings::ToFloat("123.45"), 123.45f);
    EXPECT_FLOAT_EQ(Strings::ToFloat("-456.78"), -456.78f);
    EXPECT_FLOAT_EQ(Strings::ToFloat("0.0"), 0.0f);
    EXPECT_FLOAT_EQ(Strings::ToFloat("invalid"), 0.0f);
}

// Test Replace
TEST_F(StringsTest, Replace) {
    EXPECT_EQ(Strings::Replace("hello world", "world", "there"), "hello there");
    EXPECT_EQ(Strings::Replace("aaa", "a", "b"), "bbb");
    EXPECT_EQ(Strings::Replace("test", "x", "y"), "test");
}

// Test Repeat
TEST_F(StringsTest, Repeat) {
    EXPECT_EQ(Strings::Repeat("ab", 3), "ababab");
    EXPECT_EQ(Strings::Repeat("x", 5), "xxxxx");
    // Note: Implementation returns original string when n <= 1
    EXPECT_EQ(Strings::Repeat("ab", 1), "ab");
}

// Test Escape
TEST_F(StringsTest, Escape) {
    EXPECT_EQ(Strings::Escape("hello"), "hello");
}

// Test NumberToWords (if available)
TEST_F(StringsTest, NumberToWords) {
    auto result = Strings::NumberToWords(42);
    EXPECT_FALSE(result.empty());
}
