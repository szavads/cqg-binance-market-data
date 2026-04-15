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
    
    // 1. Открываем файл СРАЗУ в конструкторе
    fileStream_.open(filename_, std::ios::out | std::ios::app);
    
    if (!fileStream_.is_open()) {
        // Критичная ошибка - лучше выбросить исключение
        throw std::runtime_error("[FileWriter] Cannot open file: " + filename_);
    }
    
    spdlog::info("[FileWriter] File opened: {}", filename_);
    
    // 2. Запускаем поток записи
    writerThread_ = std::thread(&FileWriter::writerLoop, this);
}

FileWriter::~FileWriter() {
    stop();
}

void FileWriter::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    cv_.notify_one(); // Будим поток немедленно, не ждём окончания sleep
    
    // 3. Ждём завершения потока (чтобы дописать все данные)
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
    
    // 4. Закрываем файл ТОЛЬКО после остановки потока
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileStream_.is_open()) {
        fileStream_.flush();
        fileStream_.close();
        spdlog::info("[FileWriter] File closed");
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
    while (true) {
        // Ждём интервал, но выходим досрочно если stop() разбудил нас
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(intervalMs_),
                         [this] { return !isRunning_; });
        }
        
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
        
        if (!isRunning_) break; // Данные записаны — теперь можно выйти
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