// tests/FileWriterTests.cpp
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <string>
#include "storage/FileWriter.hpp"

namespace fs = std::filesystem;

// Helper: unique temp file path
static fs::path tempFilePath() {
    auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("cqg_fw_test_" + std::to_string(ticks) + ".log");
}

// Helper: read entire file content
static std::string readFile(const fs::path& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// Helper: build a TradeStats with given values
static cqg::TradeStats makeStats(const std::string& symbol,
                                  int64_t trades,
                                  double volume,
                                  double minP, double maxP,
                                  int64_t buy, int64_t sell) {
    cqg::TradeStats s;
    s.symbol        = symbol;
    s.tradeCount    = trades;
    s.totalVolume   = volume;
    s.minPrice      = minP;
    s.maxPrice      = maxP;
    s.buyerInitiated   = buy;
    s.sellerInitiated  = sell;
    return s;
}

// Test 1: Constructor creates the file on disk
TEST(FileWriterTest, ConstructorCreatesFile) {
    // Arrange
    fs::path path = tempFilePath();

    // Act
    {
        cqg::FileWriter writer(path.string(), 10000);
        writer.stop();
    }

    // Assert
    EXPECT_TRUE(fs::exists(path));

    // Cleanup
    fs::remove(path);
}

// Test 2: Constructor throws for invalid path
TEST(FileWriterTest, ConstructorThrowsOnInvalidPath) {
    fs::path bad = fs::temp_directory_path() / "no_such_dir" / "fw_test.log";

    EXPECT_THROW(
        cqg::FileWriter writer(bad.string(), 1000),
        std::runtime_error
    );
}

// Test 3: write() + flush via short interval → data appears in file
TEST(FileWriterTest, WriteFlushesDataToFile) {
    // Arrange
    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 100 /*ms*/);

        std::map<std::string, cqg::TradeStats> stats;
        stats["BTCUSDT"] = makeStats("BTCUSDT", 10, 5.00, 100.0, 200.0, 6, 4);

        // Act
        writer.write(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        writer.stop();
    }

    // Assert: file contains expected fields
    std::string content = readFile(path);
    EXPECT_NE(content.find("timestamp="), std::string::npos);
    EXPECT_NE(content.find("symbol=BTCUSDT"), std::string::npos);
    EXPECT_NE(content.find("trades=10"),      std::string::npos);
    EXPECT_NE(content.find("buy=6"),          std::string::npos);
    EXPECT_NE(content.find("sell=4"),         std::string::npos);

    // Cleanup
    fs::remove(path);
}

// Test 4: stats with no data (tradeCount == 0) are not written
TEST(FileWriterTest, SkipsEmptyStats) {
    // Arrange
    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 100);

        cqg::TradeStats empty{}; // tradeCount = 0
        std::map<std::string, cqg::TradeStats> stats;
        stats["ETHUSDT"] = empty;

        // Act
        writer.write(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        writer.stop();
    }

    // Assert: no trade data written (session header is allowed)
    std::string content = readFile(path);
    EXPECT_EQ(content.find("timestamp="), std::string::npos);

    // Cleanup
    fs::remove(path);
}

// Test 5: multiple symbols appear in the same record
TEST(FileWriterTest, MultipleSymbolsInOneRecord) {
    // Arrange
    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 100);

        std::map<std::string, cqg::TradeStats> stats;
        stats["BTCUSDT"] = makeStats("BTCUSDT", 5,  1.0, 100.0, 200.0, 3, 2);
        stats["ETHUSDT"] = makeStats("ETHUSDT", 7, 14.0, 300.0, 400.0, 4, 3);

        // Act
        writer.write(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        writer.stop();
    }

    // Assert
    std::string content = readFile(path);
    EXPECT_NE(content.find("symbol=BTCUSDT"), std::string::npos);
    EXPECT_NE(content.find("symbol=ETHUSDT"), std::string::npos);

    // Cleanup
    fs::remove(path);
}

// Test 6: output format — timestamp on first line, then symbol/buy lines
TEST(FileWriterTest, OutputFormatMatchesSpec) {
    // Arrange
    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 100);

        std::map<std::string, cqg::TradeStats> stats;
        stats["BTCUSDT"] = makeStats("BTCUSDT", 154, 23.51, 43012.1, 43189.4, 82, 72);

        // Act
        writer.write(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        writer.stop();
    }

    // Assert: lines appear in order: timestamp → symbol line → buy/sell line
    std::string content = readFile(path);
    auto posTimestamp = content.find("timestamp=");
    auto posSymbol    = content.find("symbol=BTCUSDT");
    auto posBuy       = content.find("buy=82");

    ASSERT_NE(posTimestamp, std::string::npos);
    ASSERT_NE(posSymbol,    std::string::npos);
    ASSERT_NE(posBuy,       std::string::npos);

    EXPECT_LT(posTimestamp, posSymbol);
    EXPECT_LT(posSymbol,    posBuy);

    // Cleanup
    fs::remove(path);
}

// Test 7: stop() is idempotent — calling twice does not crash
TEST(FileWriterTest, StopIsIdempotent) {
    fs::path path = tempFilePath();

    EXPECT_NO_THROW({
        cqg::FileWriter writer(path.string(), 10000);
        writer.stop();
        writer.stop();
    });

    fs::remove(path);
}

// Test 8: hasError() returns false for a healthy writer
TEST(FileWriterTest, HasErrorFalseForHealthyWriter) {
    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 10000);
        EXPECT_FALSE(writer.hasError());
        writer.stop();
    }

    fs::remove(path);
}

// Helper: build stats with explicit windowEndTimeMs
static cqg::TradeStats makeStatsWithWindow(const std::string& symbol,
                                            int64_t trades,
                                            double volume,
                                            double minP, double maxP,
                                            int64_t buy, int64_t sell,
                                            int64_t windowEndTimeMs) {
    cqg::TradeStats s = makeStats(symbol, trades, volume, minP, maxP, buy, sell);
    s.windowEndTimeMs = windowEndTimeMs;
    return s;
}

// Test 9: timestamp in output is derived from windowEndTimeMs, not local clock
TEST(FileWriterTest, TimestampComesFromExchangeWindowEnd) {
    // Arrange: windowEndTimeMs = 2025-01-02T02:04:05Z in milliseconds
    // Verified: 1735783445 * 1000 ms → "2025-01-02T02:04:05Z"
    constexpr int64_t exchangeEndMs = 1735783445LL * 1000;

    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 100);

        std::map<std::string, cqg::TradeStats> stats;
        stats["BTCUSDT"] = makeStatsWithWindow("BTCUSDT", 1, 1.0, 100.0, 100.0, 1, 0,
                                               exchangeEndMs);

        writer.write(stats);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        writer.stop();
    }

    std::string content = readFile(path);
    // The timestamp must match the exchange window end, not the local wall clock
    EXPECT_NE(content.find("timestamp=2025-01-02T02:04:05Z"), std::string::npos)
        << "Full output:\n" << content;

    fs::remove(path);
}

// Test 10: two write() calls with the same windowEndTimeMs are merged into one block
TEST(FileWriterTest, SameWindowEndMergesIntoOneBlock) {
    constexpr int64_t sharedWindowEndMs = 1735783445LL * 1000;

    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 500 /*ms — long enough to accumulate both writes*/);

        // First write: only BTCUSDT
        std::map<std::string, cqg::TradeStats> batch1;
        batch1["BTCUSDT"] = makeStatsWithWindow("BTCUSDT", 3, 6.0, 100.0, 102.0, 2, 1,
                                                sharedWindowEndMs);
        writer.write(batch1);

        // Second write: only ETHUSDT — same window
        std::map<std::string, cqg::TradeStats> batch2;
        batch2["ETHUSDT"] = makeStatsWithWindow("ETHUSDT", 5, 10.0, 200.0, 205.0, 3, 2,
                                                sharedWindowEndMs);
        writer.write(batch2);

        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        writer.stop();
    }

    std::string content = readFile(path);

    // There must be exactly one timestamp block
    size_t firstPos = content.find("timestamp=");
    ASSERT_NE(firstPos, std::string::npos);
    EXPECT_EQ(content.find("timestamp=", firstPos + 1), std::string::npos)
        << "Expected one timestamp block, got two:\n" << content;

    // Both symbols must appear in that single block
    EXPECT_NE(content.find("symbol=BTCUSDT"), std::string::npos);
    EXPECT_NE(content.find("symbol=ETHUSDT"), std::string::npos);

    fs::remove(path);
}

// Test 11: two write() calls with different windowEndTimeMs produce two separate blocks
TEST(FileWriterTest, DifferentWindowEndsProduceSeparateBlocks) {
    constexpr int64_t windowEnd1Ms = 1735783445LL * 1000; // 2026-01-02T03:04:05Z
    constexpr int64_t windowEnd2Ms = 1735783446LL * 1000; // 2026-01-02T03:04:06Z

    fs::path path = tempFilePath();
    {
        cqg::FileWriter writer(path.string(), 500);

        std::map<std::string, cqg::TradeStats> batch1;
        batch1["BTCUSDT"] = makeStatsWithWindow("BTCUSDT", 1, 1.0, 100.0, 100.0, 1, 0,
                                                windowEnd1Ms);
        writer.write(batch1);

        std::map<std::string, cqg::TradeStats> batch2;
        batch2["BTCUSDT"] = makeStatsWithWindow("BTCUSDT", 2, 2.0, 101.0, 101.0, 0, 2,
                                                windowEnd2Ms);
        writer.write(batch2);

        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        writer.stop();
    }

    std::string content = readFile(path);

    // Two distinct timestamp lines expected
    size_t pos1 = content.find("timestamp=2025-01-02T02:04:05Z");
    size_t pos2 = content.find("timestamp=2025-01-02T02:04:06Z");

    EXPECT_NE(pos1, std::string::npos) << "Missing first window timestamp\n" << content;
    EXPECT_NE(pos2, std::string::npos) << "Missing second window timestamp\n" << content;

    fs::remove(path);
}
