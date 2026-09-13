#include <gtest/gtest.h>
#include "common/util/strings.h"

class StringsTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test ToLower
TEST_F(StringsTest, ToLower) {
    EXPECT_EQ(Strings::ToLower("HELLO"), "hello");
    EXPECT_EQ(Strings::ToLower("Hello World"), "hello world");
    EXPECT_EQ(Strings::ToLower(""), "");
    EXPECT_EQ(Strings::ToLower("123"), "123");
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
