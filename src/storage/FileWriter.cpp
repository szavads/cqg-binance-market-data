// src/storage/FileWriter.cpp
#include "FileWriter.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace cqg {

FileWriter::FileWriter(const std::string& filename, int64_t intervalMs)
    : filename_(filename)
    , intervalMs_(intervalMs)
    , isRunning_(true) {
    
    // 1. Открываем файл СРАЗУ в конструкторе
    fileStream_.open(filename_, std::ios::out | std::ios::app);
    
    if (!fileStream_.is_open()) {
        // Критичная ошибка - лучше выбросить исключение
        throw std::runtime_error("[FileWriter] Cannot open file: " + filename_);
    }
    
    std::cout << "[FileWriter] File opened successfully: " << filename_ << std::endl;
    
    // 2. Запускаем поток записи
    writerThread_ = std::thread(&FileWriter::writerLoop, this);
}

FileWriter::~FileWriter() {
    stop();
}

void FileWriter::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    
    // 3. Ждём завершения потока (чтобы дописать все данные)
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
    
    // 4. Закрываем файл ТОЛЬКО после остановки потока
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
        fileStream_.close();
        std::cout << "[FileWriter] File closed" << std::endl;
    }
}

void FileWriter::write(const std::map<std::string, TradeStats>& stats) {
    // 5. Thread-safe добавление в буфер
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [symbol, s] : stats) {
        if (s.hasData()) {
            pendingStats_[symbol] = s;
        }
    }
}

void FileWriter::writerLoop() {
    while (isRunning_) {
        // Ждём интервал
        std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs_));
        // После пробуждения продолжаем даже если isRunning_ стал false — чтобы сбросить буфер
        
        // 6. Захватываем мьютекс и забираем данные
        std::map<std::string, TradeStats> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingStats_.empty()) {
                if (!isRunning_) break; // Нет данных и стоп — выходим
                continue;
            }
            snapshot = std::move(pendingStats_);
        }
        
        // 7. Форматируем
        std::string output = formatOutput(snapshot);
        
        // 8. Пишем в файл (вне мьютекса, но внутри потока)
        if (!output.empty() && fileStream_.is_open() && fileStream_.good()) {
            fileStream_ << output << std::endl;
            fileStream_.flush(); // Гарантия записи
        }
    }
}

bool FileWriter::hasError() const {
    return !fileStream_.is_open() || !fileStream_.good();
}

std::string FileWriter::formatOutput(const std::map<std::string, TradeStats>& stats) {
    if (stats.empty()) return "";
    
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_s(&tm_buf, &time_t_now);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
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