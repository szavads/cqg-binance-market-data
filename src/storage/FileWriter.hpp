
#pragma once

#include <string>
#include <map>
#include <thread>
#include <atomic>
#include "../core/Aggregator.hpp"

namespace cqg {

class FileWriter {
public:
    FileWriter(const std::string& filename, int64_t intervalMs);
    ~FileWriter();
    
    void write(const std::map<std::string, TradeStats>& stats);
    void stop();

private:
    std::string filename_;
    int64_t intervalMs_;
    std::atomic<bool> isRunning_;
    std::thread writerThread_;
};

} // namespace cqg