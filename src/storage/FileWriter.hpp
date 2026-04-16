
#pragma once

#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include "core/Aggregator.hpp"

namespace cqg {

class FileWriter {
public:
    FileWriter(const std::string& filename, int64_t intervalMs);
    ~FileWriter();
    
    // Buffer incoming stats; flushed to file by the background thread
    void write(const std::map<std::string, TradeStats>& stats);
    
    // Stop the writer thread; blocks until all pending data is flushed and file is closed
    void stop();
    
    // Returns true while the writer thread is active
    bool isRunning() const { return isRunning_; }
    
    // Returns true if the output file cannot be written
    bool hasError() const;

private:
    // Format a stats map into the output string
    std::string formatOutput(const std::map<std::string, TradeStats>& stats);
    
    // Background thread: wakes every intervalMs and flushes pendingStats_
    void writerLoop();
    
    std::ofstream fileStream_;
    
    std::string filename_;
    int64_t intervalMs_;
    std::atomic<bool> isRunning_;
    std::thread writerThread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // Pending stats waiting to be written on the next flush interval.
    // Each entry is one completed aggregation window.
    std::vector<std::map<std::string, TradeStats>> pendingBatches_;
};

} // namespace cqg