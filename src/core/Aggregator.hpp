
#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <functional>
#include <limits>

namespace cqg {

// Per-symbol aggregated trade statistics for one time window
struct TradeStats {
    std::string symbol;
    int64_t tradeCount = 0;
    double totalVolume = 0.0;      // sum(price * quantity)
    double minPrice = std::numeric_limits<double>::max();
    double maxPrice = std::numeric_limits<double>::lowest();
    int64_t buyerInitiated = 0;    // m = false (buyer is taker)
    int64_t sellerInitiated = 0;   // m = true (seller is taker)
    int64_t windowStartTime = 0;    // exchange timestamp of window open (ms)
    int64_t windowEndTimeMs = 0;    // exchange timestamp of window close (ms)
    
    bool hasData() const { return tradeCount > 0; }
    
    // Reset stats for a new window
    void reset(int64_t newWindowStart) {
        tradeCount = 0;
        totalVolume = 0.0;
        minPrice = std::numeric_limits<double>::max();
        maxPrice = std::numeric_limits<double>::lowest();
        buyerInitiated = 0;
        sellerInitiated = 0;
        windowStartTime = newWindowStart;
        windowEndTimeMs = 0;
    }
};

// Callback invoked by Aggregator with completed window stats
using AggregationCallback = std::function<void(const std::map<std::string, TradeStats>&)>;

class Aggregator {
public:
    // windowMs: aggregation window duration in milliseconds (e.g. 1000)
    explicit Aggregator(int64_t windowMs);
    ~Aggregator();

    // Called from the WebSocket callback on each incoming trade
    void addTrade(const std::string& symbol, 
                  double price, 
                  double quantity, 
                  bool isBuyerMaker,  // true = seller-initiated
                  int64_t exchangeTimeMs);

    // Set the callback to receive completed window stats
    void setAggregationCallback(AggregationCallback callback);

    // Start the background aggregation thread
    void start();
    
    // Stop the aggregation thread and flush the last window
    void stop();

private:
    // Flush all symbols with data and invoke the callback
    void processWindows();

    int64_t windowMs_;
    std::map<std::string, TradeStats> stats_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    AggregationCallback callback_;
    std::atomic<bool> isRunning_;
    std::thread workerThread_;
    
    // Tracks the current window start timestamp per symbol
    std::map<std::string, int64_t> lastWindowStart_;
};

} // namespace cqg