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
    // Thread-safe: buffer incoming stats for the next flush
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [symbol, s] : stats) {
        if (s.hasData()) {
            pendingStats_[symbol] = s;
        }
    }
}

void FileWriter::writerLoop() {
    while (true) {
        // Wait for the flush interval; wakes early if stop() is called
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(intervalMs_),
                         [this] { return !isRunning_; });
        }
        
        // Grab pending data under lock
        std::map<std::string, TradeStats> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingStats_.empty()) {
                if (!isRunning_) break; // Nothing to write and stopping — exit
                continue;
            }
            snapshot = std::move(pendingStats_);
        }
        
        // Format and write to file outside the lock
        std::string output = formatOutput(snapshot);

        if (!output.empty() && fileStream_.is_open() && fileStream_.good()) {
            fileStream_ << output << std::endl;
            fileStream_.flush(); // Ensure data reaches disk
        }

        if (!isRunning_) break; // Data written — safe to exit now
    }
}

bool FileWriter::hasError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !fileStream_.is_open() || !fileStream_.good();
}

std::string FileWriter::formatOutput(const std::map<std::string, TradeStats>& stats) {
    if (stats.empty()) return "";
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    std::string timestamp = ss.str();
    
    std::ostringstream out;
    out << "timestamp=" << timestamp;
    
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