#include "Config.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace cqg {

Config loadConfig(const std::string& path) {
    Config config;
    std::ifstream file(path);
    
    if (!file.is_open()) {
        throw std::runtime_error("[Config] Cannot open file: " + path);
    }
    
    try {
        json j;
        file >> j;
        
        // Read fields; fall back to defaults when absent
        config.trading_pairs = j.value("trading_pairs", std::vector<std::string>{"btcusdt", "ethusdt"});
        config.aggregation_window_ms = j.value("aggregation_window_ms", 1000);
        config.serialization_interval_ms = j.value("serialization_interval_ms", 5000);
        config.output_file = j.value("output_file", "market_data.log");
        
        // Validate required constraints
        if (config.aggregation_window_ms <= 0) {
            throw std::runtime_error("[Config] aggregation_window_ms must be > 0");
        }
        if (config.serialization_interval_ms <= 0) {
            throw std::runtime_error("[Config] serialization_interval_ms must be > 0");
        }
        
    } catch (const json::exception& e) {
        throw std::runtime_error("[Config] JSON parse error: " + std::string(e.what()));
    }
    
    return config;
}

} // namespace cqg