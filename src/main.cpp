#include <thread>
#include <atomic>
#include <csignal>
#include <boost/asio.hpp>
#include <spdlog/spdlog.h>
#include "config/Config.hpp"
#include "network/WebSocketClient.hpp"
#include "storage/FileWriter.hpp"

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    g_running = false;
}


int main()
{
    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // Load configuration from config.json
    cqg::Config config;
    try {
        config = cqg::loadConfig("config/config.json");
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        return 1;
    }
    spdlog::info("[Config] Loaded: Window={}ms, Interval={}ms",
                 config.aggregation_window_ms, config.serialization_interval_ms);
    int64_t windowMs = config.aggregation_window_ms;
    int64_t serializationIntervalMs = config.serialization_interval_ms;
    std::vector<std::string> pairs = config.trading_pairs;

    boost::asio::io_context io_context;
    cqg::Aggregator aggregator(windowMs);
    cqg::WebSocketClient client(io_context);

    // File writer for serializing aggregated stats
    cqg::FileWriter writer(config.output_file, serializationIntervalMs);

    // Wire Aggregator -> FileWriter
    aggregator.setAggregationCallback([&writer](const std::map<std::string, cqg::TradeStats>& stats) {
        writer.write(stats);
    });
    
    // Wire WebSocket -> Aggregator
    client.setTradeCallback([&aggregator](const std::string& symbol, 
                                           double price, 
                                           double quantity, 
                                           bool isBuyerMaker,
                                           int64_t exchangeTime) {
        aggregator.addTrade(symbol, price, quantity, isBuyerMaker, exchangeTime);
    });

    // Subscribe to trade streams
    std::vector<std::string> streams;
    for (const auto& pair : pairs) {
        streams.push_back(pair + "@trade");
    }
    client.subscribe(streams);

    // Start aggregator
    aggregator.start();

    // Run WebSocket client on a dedicated thread
    std::thread wsThread([&client]() {
        client.start();
    });
    
    // Wait for shutdown signal
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    spdlog::info("[Signal] Shutting down...");

    // Graceful shutdown
    spdlog::info("[Main] Stopping services...");
    aggregator.stop();
    client.stop();
    writer.stop();
    
    wsThread.join();
    
    spdlog::info("[Main] Service stopped gracefully");
    return 0;
}

