
#pragma once

#include <string>
#include <map>
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
    
    // Запись агрегированных данных (добавляет в буфер)
    void write(const std::map<std::string, TradeStats>& stats);
    
    // Остановка (с дожиданием завершения записи и закрытием файла)
    void stop();
    
    // Проверка статуса
    bool isRunning() const { return isRunning_; }
    
    // Проверка наличия ошибок файла
    bool hasError() const;

private:
    // Внутренний метод форматирования строки
    std::string formatOutput(const std::map<std::string, TradeStats>& stats);
    
    // Поток записи
    void writerLoop();
    
    std::ofstream fileStream_;
    
    std::string filename_;
    int64_t intervalMs_;
    std::atomic<bool> isRunning_;
    std::thread writerThread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    
    // Буфер для отложенной записи
    std::map<std::string, TradeStats> pendingStats_;
};

} // namespace cqg