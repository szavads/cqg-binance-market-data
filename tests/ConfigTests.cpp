// tests/ConfigTests.cpp
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include "config/Config.hpp"

namespace fs = std::filesystem;

// Helper function to create a temporary config file with a unique name
fs::path createTempConfigFile(const std::string& content) {
    auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path tempPath = fs::temp_directory_path() /
        ("cqg_test_" + std::to_string(ticks) + ".json");
    std::ofstream file(tempPath);
    file << content;
    return tempPath;
}

// Helper function to remove temporary file
void removeTempConfigFile(const fs::path& path) {
    if (fs::exists(path)) {
        fs::remove(path);
    }
}


// Test 1: Valid configuration loading
TEST(ConfigTest, LoadValidConfig) {
    // Arrange: Create a temporary config file with valid JSON
    std::string configContent = R"({
        "trading_pairs": ["btcusdt", "ethusdt", "bnbusdt"],
        "aggregation_window_ms": 2000,
        "serialization_interval_ms": 10000,
        "output_file": "test_output.log"
    })";
    
    fs::path tempPath = createTempConfigFile(configContent);
    
    // Act: Load the configuration
    cqg::Config config = cqg::loadConfig(tempPath.string());
    
    // Assert: Verify all values are loaded correctly
    EXPECT_EQ(config.trading_pairs.size(), size_t(3));
    EXPECT_EQ(config.trading_pairs[0], "btcusdt");
    EXPECT_EQ(config.trading_pairs[1], "ethusdt");
    EXPECT_EQ(config.trading_pairs[2], "bnbusdt");
    
    EXPECT_EQ(config.aggregation_window_ms, 2000);
    EXPECT_EQ(config.serialization_interval_ms, 10000);
    EXPECT_EQ(config.output_file, "test_output.log");
    
    // Cleanup
    removeTempConfigFile(tempPath);
}

// Test 2: Default values when fields are missing
TEST(ConfigTest, LoadConfigWithMissingFields) {
    // Arrange: Create a config with only some fields
    std::string configContent = R"({
        "trading_pairs": ["btcusdt"]
    })";
    
    fs::path tempPath = createTempConfigFile(configContent);
    
    // Act: Load the configuration (should use defaults for missing fields)
    cqg::Config config = cqg::loadConfig(tempPath.string());
    
    // Assert: Check defaults are applied
    EXPECT_EQ(config.trading_pairs.size(), size_t(1));
    EXPECT_EQ(config.trading_pairs[0], "btcusdt");
    EXPECT_EQ(config.aggregation_window_ms, 1000);  // Default
    EXPECT_EQ(config.serialization_interval_ms, 5000);  // Default
    EXPECT_EQ(config.output_file, "market_data.log");  // Default
    
    // Cleanup
    removeTempConfigFile(tempPath);
}

// Test 3: Invalid file path
TEST(ConfigTest, LoadConfigInvalidPath) {
    // Act & Assert: Should throw exception for non-existent file
    fs::path nonExistent = fs::temp_directory_path() / "cqg_nonexistent_dir" / "config.json";
    EXPECT_THROW(
        cqg::loadConfig(nonExistent.string()),
        std::runtime_error
    );
}

// Test 4: Invalid JSON format
TEST(ConfigTest, LoadConfigInvalidJson) {
    // Arrange: Create a file with invalid JSON
    std::string configContent = "{ invalid json content }";
    fs::path tempPath = createTempConfigFile(configContent);
    
    // Act & Assert: Should throw exception for invalid JSON
    EXPECT_THROW(
        cqg::loadConfig(tempPath.string()),
        std::runtime_error
    );
    
    // Cleanup
    removeTempConfigFile(tempPath);
}

// Test 5: Invalid aggregation_window_ms (negative value)
TEST(ConfigTest, LoadConfigInvalidWindowMs) {
    // Arrange: Create a config with invalid window value
    std::string configContent = R"({
        "trading_pairs": ["btcusdt"],
        "aggregation_window_ms": -100
    })";
    
    fs::path tempPath = createTempConfigFile(configContent);
    
    // Act & Assert: Should throw exception for invalid value
    EXPECT_THROW(
        cqg::loadConfig(tempPath.string()),
        std::runtime_error
    );
    
    // Cleanup
    removeTempConfigFile(tempPath);
}

// Test 6: Empty trading pairs list
TEST(ConfigTest, LoadConfigEmptyTradingPairs) {
    // Arrange: Create a config with empty pairs list
    std::string configContent = R"({
        "trading_pairs": [],
        "aggregation_window_ms": 1000
    })";
    
    fs::path tempPath = createTempConfigFile(configContent);
    
    // Act: Load the configuration
    cqg::Config config = cqg::loadConfig(tempPath.string());
    
    // Assert: Empty list is allowed (validation is optional)
    EXPECT_EQ(config.trading_pairs.size(), size_t(0));
    
    // Cleanup
    removeTempConfigFile(tempPath);
}