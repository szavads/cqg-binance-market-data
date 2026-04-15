#include <gtest/gtest.h>
#include "app.h"

TEST(AppTest, GreetingMessage) {
    EXPECT_EQ(greeting_message(), "Hello cqg_binance_service...");
}