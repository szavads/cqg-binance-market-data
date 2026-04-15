
#include "FileWriter.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace cqg {

FileWriter::FileWriter(const std::string& filename, int64_t intervalMs)
    : filename_(filename)
    , intervalMs_(intervalMs)
    , isRunning_(true) {}

FileWriter::~FileWriter() {
    stop();
}

void FileWriter::write(const std::map<std::string, TradeStats>& stats) {
    // Форматирование timestamp (hardware time)
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");
    std::string timestamp = ss.str();
    
    // Запись в файл
    std::ofstream file(filename_, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "[FileWriter] Error opening file: " << filename_ << std::endl;
        return;
    }
    
    file << "timestamp=" << timestamp;
    for (const auto& [symbol, s] : stats) {
        file << " symbol=" << symbol
             << " trades=" << s.tradeCount
             << " volume=" << std::fixed << std::setprecision(2) << s.totalVolume
             << " min=" << std::setprecision(1) << s.minPrice
             << " max=" << std::setprecision(1) << s.maxPrice
             << " buy=" << s.buyerInitiated
             << " sell=" << s.sellerInitiated;
    }
    file << std::endl;
    
    std::cout << "[FileWriter] Data written at " << timestamp << std::endl;
}

void FileWriter::stop() {
    isRunning_ = false;
}

} // namespace cqg