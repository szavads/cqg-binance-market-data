// src/storage/FileWriter.cpp
#include "FileWriter.hpp"
#include <chrono>
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace cqg {

FileWriter::FileWriter(const std::string& filename, int64_t intervalMs)
    : filename_(filename)
    , intervalMs_(intervalMs)
    , isRunning_(true) {
    
    // Open the file once at construction time  held open for the service lifetime
    fileStream_.open(filename_, std::ios::out | std::ios::app);

    if (!fileStream_.is_open()) {
        throw std::runtime_error("[FileWriter] Cannot open file: " + filename_);
    }
    
    spdlog::info("[FileWriter] File opened: {}", filename_);
    
    // Start the background writer thread
    writerThread_ = std::thread(&FileWriter::writerLoop, this);
}

FileWriter::~FileWriter() {
    stop();
}

void FileWriter::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    cv_.notify_one(); // Wake the writer thread immediately instead of waiting for the interval

    // Join the thread so all pending data is flushed before closing the file
    if (writerThread_.joinable()) {
        writerThread_.join();
    }

    // Close the file only after the writer thread has finished
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
        fileStream_.close();
        spdlog::info("[FileWriter] File closed");
    }
}

void FileWriter::write(const std::map<std::string, TradeStats>& stats) {
    // Filter out empty symbols
    std::map<std::string, TradeStats> batch;
    for (const auto& [symbol, s] : stats) {
        if (s.hasData()) batch[symbol] = s;
    }
    if (batch.empty()) return;

    int64_t windowEndMs = batch.begin()->second.windowEndTimeMs;

    std::lock_guard<std::mutex> lock(mutex_);
    // Merge into an existing batch for the same window end time if one exists.
    // This handles late-arriving trades for an already-enqueued window.
    for (auto& existing : pendingBatches_) {
        if (!existing.empty() && existing.begin()->second.windowEndTimeMs == windowEndMs) {
            for (auto& [symbol, s] : batch) {
                existing[symbol] = std::move(s);
            }
            return;
        }
    }
    pendingBatches_.push_back(std::move(batch));
}

void FileWriter::writerLoop() {
    while (true) {
        // Wait for the flush interval; wakes early if stop() is called
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(intervalMs_),
                         [this] { return !isRunning_; });
        }
        
        // Grab all accumulated batches under lock
        std::vector<std::map<std::string, TradeStats>> batches;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingBatches_.empty()) {
                if (!isRunning_) break; // Nothing to write and stopping — exit
                continue;
            }
            batches = std::move(pendingBatches_);
        }

        // Write each window batch as a separate block
        for (const auto& batch : batches) {
            std::string output = formatOutput(batch);
            if (!output.empty() && fileStream_.is_open() && fileStream_.good()) {
                fileStream_ << output << "\n";
            }
        }
        fileStream_.flush(); // Single flush for all batches

        if (!isRunning_) break; // Data written — safe to exit now
    }
}

bool FileWriter::hasError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !fileStream_.is_open() || !fileStream_.good();
}

std::string FileWriter::formatOutput(const std::map<std::string, TradeStats>& stats) {
    if (stats.empty()) return "";

    // Use exchange-derived window end time as timestamp (deterministic, independent of local clock)
    int64_t windowEndMs = stats.begin()->second.windowEndTimeMs;
    auto tp = std::chrono::system_clock::time_point(
        std::chrono::milliseconds(windowEndMs));
    auto time_t_val = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_val), "%Y-%m-%dT%H:%M:%SZ");

    std::ostringstream out;
    out << "timestamp=" << ss.str();
    
    for (const auto& [symbol, s] : stats) {
        out << "\nsymbol=" << symbol
            << " trades=" << s.tradeCount
            << " volume=" << std::fixed << std::setprecision(2) << s.totalVolume
            << " min=" << std::setprecision(1) << s.minPrice
            << " max=" << std::setprecision(1) << s.maxPrice
            << " buy=" << s.buyerInitiated
            << " sell=" << s.sellerInitiated;
    }
    
    return out.str();
}

} // namespace cqg