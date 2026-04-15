
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace cqg {

struct Config {
    std::vector<std::string> trading_pairs;
    int64_t aggregation_window_ms;
    int64_t serialization_interval_ms;
    std::string output_file;

    // Default values used when fields are absent in the config file
    Config() 
        : aggregation_window_ms(1000)
        , serialization_interval_ms(5000)
        , output_file("market_data.log") {}
};

// Load and validate config from a JSON file; throws std::runtime_error on failure
Config loadConfig(const std::string& path);

} // namespace cqg