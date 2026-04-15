// tests/AggregatorTests.cpp
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include "core/Aggregator.hpp"

// Window size used in all timing tests
static constexpr int64_t TEST_WINDOW_MS = 100;

// Helper: start aggregator, add trades via lambda, wait for first callback,
// stop and return the received stats snapshot.
static std::map<std::string, cqg::TradeStats> runAndCollect(
    cqg::Aggregator& agg,
    std::function<void()> addTrades)
{
    std::map<std::string, cqg::TradeStats> result;
    std::atomic<bool> called{false};

    agg.setAggregationCallback([&](const std::map<std::string, cqg::TradeStats>& stats) {
        if (!called.exchange(true)) {
            result = stats;
        }
    });

    agg.start();
    addTrades();

    // Wait up to 5 * window for callback
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(5 * TEST_WINDOW_MS);
    while (!called && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    agg.stop();

    return result;
}

// ─── addTrade: basic accumulation ────────────────────────────────────────────

// Test 1: tradeCount increments for each addTrade call
TEST(AggregatorTest, TradeCountAccumulates) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500; // all within window [0, TEST_WINDOW_MS)

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t);
        agg.addTrade("BTCUSDT", 200.0, 1.0, false, t);
        agg.addTrade("BTCUSDT", 300.0, 1.0, false, t);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_EQ(stats.at("BTCUSDT").tradeCount, 3);
}

// Test 2: totalVolume = sum(price * quantity)
TEST(AggregatorTest, VolumeAccumulates) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 2.0, false, t); // 200
        agg.addTrade("BTCUSDT", 50.0,  3.0, false, t); // 150
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").totalVolume, 350.0);
}

// Test 3: minPrice and maxPrice tracked correctly
TEST(AggregatorTest, MinMaxPriceTracked) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 300.0, 1.0, false, t);
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t);
        agg.addTrade("BTCUSDT", 200.0, 1.0, false, t);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").minPrice, 100.0);
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").maxPrice, 300.0);
}

// ─── addTrade: isBuyerMaker semantics ────────────────────────────────────────

// Test 4: isBuyerMaker=false → buyerInitiated++
TEST(AggregatorTest, BuyerInitiatedWhenNotBuyerMaker) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t);
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_EQ(stats.at("BTCUSDT").buyerInitiated,  2);
    EXPECT_EQ(stats.at("BTCUSDT").sellerInitiated, 0);
}

// Test 5: isBuyerMaker=true → sellerInitiated++
TEST(AggregatorTest, SellerInitiatedWhenBuyerMaker) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 1.0, true, t);
        agg.addTrade("BTCUSDT", 100.0, 1.0, true, t);
        agg.addTrade("BTCUSDT", 100.0, 1.0, true, t);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_EQ(stats.at("BTCUSDT").sellerInitiated, 3);
    EXPECT_EQ(stats.at("BTCUSDT").buyerInitiated,  0);
}

// Test 6: mixed isBuyerMaker values
TEST(AggregatorTest, MixedBuyerSellerCounts) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t); // buyer
        agg.addTrade("BTCUSDT", 100.0, 1.0, true,  t); // seller
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, t); // buyer
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    EXPECT_EQ(stats.at("BTCUSDT").buyerInitiated,  2);
    EXPECT_EQ(stats.at("BTCUSDT").sellerInitiated, 1);
}

// ─── Window boundary ─────────────────────────────────────────────────────────

// Test 7: trade in a new window resets stats
// Add 2 trades in window [0, windowMs), then 1 trade in window [windowMs, 2*windowMs).
// The new-window trade resets the counter → callback should see tradeCount=1.
TEST(AggregatorTest, NewWindowResetsStats) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t w0 = TEST_WINDOW_MS / 2;        // inside window 0
    int64_t w1 = TEST_WINDOW_MS + 50;       // inside window 1

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 1.0, false, w0);
        agg.addTrade("BTCUSDT", 200.0, 1.0, false, w0);
        // This trade is in a different window → resets
        agg.addTrade("BTCUSDT", 300.0, 1.0, false, w1);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    // Only the last trade (window 1) should be visible
    EXPECT_EQ(stats.at("BTCUSDT").tradeCount, 1);
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").minPrice, 300.0);
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").maxPrice, 300.0);
}

// ─── Multiple symbols ─────────────────────────────────────────────────────────

// Test 8: each symbol tracked independently
TEST(AggregatorTest, MultipleSymbolsTrackedIndependently) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    int64_t t = 500;

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("BTCUSDT", 100.0, 2.0, false, t);
        agg.addTrade("BTCUSDT", 200.0, 1.0, false, t);
        agg.addTrade("ETHUSDT", 10.0,  5.0, true,  t);
    });

    ASSERT_TRUE(stats.count("BTCUSDT"));
    ASSERT_TRUE(stats.count("ETHUSDT"));

    EXPECT_EQ(stats.at("BTCUSDT").tradeCount,    2);
    EXPECT_DOUBLE_EQ(stats.at("BTCUSDT").totalVolume, 400.0); // 100*2 + 200*1

    EXPECT_EQ(stats.at("ETHUSDT").tradeCount,    1);
    EXPECT_EQ(stats.at("ETHUSDT").sellerInitiated, 1);
}

// ─── Callback filtering ───────────────────────────────────────────────────────

// Test 9: callback is NOT called when no trades were added
TEST(AggregatorTest, CallbackNotCalledWithNoData) {
    cqg::Aggregator agg(TEST_WINDOW_MS);
    std::atomic<int> callCount{0};

    agg.setAggregationCallback([&](const std::map<std::string, cqg::TradeStats>&) {
        callCount++;
    });

    agg.start();
    // Wait two full windows without adding any trades
    std::this_thread::sleep_for(std::chrono::milliseconds(2 * TEST_WINDOW_MS + 30));
    agg.stop();

    EXPECT_EQ(callCount.load(), 0);
}

// Test 10: symbol field in TradeStats is populated correctly
TEST(AggregatorTest, SymbolFieldSetCorrectly) {
    cqg::Aggregator agg(TEST_WINDOW_MS);

    auto stats = runAndCollect(agg, [&]() {
        agg.addTrade("ETHUSDT", 50.0, 1.0, false, 500);
    });

    ASSERT_TRUE(stats.count("ETHUSDT"));
    EXPECT_EQ(stats.at("ETHUSDT").symbol, "ETHUSDT");
}

// Test 11: stats are reset after processWindows — second callback sees fresh data only
TEST(AggregatorTest, StatsResetAfterCallback) {
    cqg::Aggregator agg(TEST_WINDOW_MS);

    std::vector<std::map<std::string, cqg::TradeStats>> calls;
    std::atomic<int> callCount{0};

    agg.setAggregationCallback([&](const std::map<std::string, cqg::TradeStats>& s) {
        calls.push_back(s);
        callCount++;
    });

    agg.start();

    // Add 3 trades in first window
    agg.addTrade("BTCUSDT", 100.0, 1.0, false, 500);
    agg.addTrade("BTCUSDT", 200.0, 1.0, false, 500);
    agg.addTrade("BTCUSDT", 300.0, 1.0, false, 500);

    // Wait for first callback
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(5 * TEST_WINDOW_MS);
    while (callCount < 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Add 1 trade in a new exchange-time window
    agg.addTrade("BTCUSDT", 999.0, 1.0, false, TEST_WINDOW_MS * 10 + 500);

    // Wait for second callback
    deadline = std::chrono::steady_clock::now()
             + std::chrono::milliseconds(5 * TEST_WINDOW_MS);
    while (callCount < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    agg.stop();

    ASSERT_GE(calls.size(), size_t(2));
    // First callback: 3 trades
    EXPECT_EQ(calls[0].at("BTCUSDT").tradeCount, 3);
    // Second callback: only 1 trade — proves stats were reset between windows
    EXPECT_EQ(calls[1].at("BTCUSDT").tradeCount, 1);
    EXPECT_DOUBLE_EQ(calls[1].at("BTCUSDT").minPrice, 999.0);
}
