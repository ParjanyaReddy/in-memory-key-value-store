#include <gtest/gtest.h>

#include <string>

TEST(SmokeTest, BasicAssertionsWork) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_TRUE(true);
}

TEST(SmokeTest, StringManipulationWorks) {
    std::string greeting = "redis_clone";
    EXPECT_EQ(greeting.length(), 11);
    EXPECT_EQ(greeting.substr(0, 5), "redis");
}
