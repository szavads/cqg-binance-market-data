// tests/WebSocketClientTests.cpp
#include <gtest/gtest.h>
#include "network/TradeParser.hpp"

namespace {

// Helpers
std::string makeTrade(const std::string& symbol, const std::string& price,
                      const std::string& qty, bool buyerMaker, int64_t time)
{
    return R"({"e":"trade","s":")" + symbol +
           R"(","p":")" + price +
           R"(","q":")" + qty +
           R"(","m":)" + (buyerMaker ? "true" : "false") +
           R"(,"T":)" + std::to_string(time) + "}";
}

// ─── Tests ────────────────────────────────────────────────────────────────────

TEST(TradeParserTest, ParsesValidTrade) {
    auto result = cqg::parseTrade(makeTrade("BTCUSDT", "43012.1", "0.5", false, 1700000000000));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->symbol, "BTCUSDT");
    EXPECT_DOUBLE_EQ(result->price, 43012.1);
    EXPECT_DOUBLE_EQ(result->quantity, 0.5);
    EXPECT_FALSE(result->isBuyerMaker);
    EXPECT_EQ(result->exchangeTimeMs, 1700000000000LL);
}

TEST(TradeParserTest, BuyerMakerTrue) {
    auto result = cqg::parseTrade(makeTrade("ETHUSDT", "2300.0", "1.0", true, 0));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->isBuyerMaker);
}

TEST(TradeParserTest, NonTradeEventReturnsNullopt) {
    // "kline" event — should be ignored
    auto result = cqg::parseTrade(R"({"e":"kline","s":"BTCUSDT"})");
    EXPECT_FALSE(result.has_value());
}

TEST(TradeParserTest, MissingEventTypeReturnsNullopt) {
    auto result = cqg::parseTrade(R"({"s":"BTCUSDT","p":"100","q":"1","m":false,"T":0})");
    EXPECT_FALSE(result.has_value());
}

TEST(TradeParserTest, MalformedJsonReturnsNullopt) {
    auto result = cqg::parseTrade("{not valid json}");
    EXPECT_FALSE(result.has_value());
}

TEST(TradeParserTest, EmptyPayloadReturnsNullopt) {
    auto result = cqg::parseTrade("");
    EXPECT_FALSE(result.has_value());
}

TEST(TradeParserTest, MissingFieldsUseDefaults) {
    // Minimal trade event — missing s, p, q, T
    auto result = cqg::parseTrade(R"({"e":"trade"})");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->symbol, "");
    EXPECT_DOUBLE_EQ(result->price, 0.0);
    EXPECT_DOUBLE_EQ(result->quantity, 0.0);
    EXPECT_FALSE(result->isBuyerMaker);
    EXPECT_EQ(result->exchangeTimeMs, 0LL);
}

} // namespace
