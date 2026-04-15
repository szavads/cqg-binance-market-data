
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

// Структура для агрегированной статистики по паре
struct TradeStats {
    std::string symbol;
    int64_t tradeCount = 0;
    double totalVolume = 0.0;      // sum(price * quantity)
    double minPrice = std::numeric_limits<double>::max();
    double maxPrice = std::numeric_limits<double>::lowest();
    int64_t buyerInitiated = 0;    // m = false (buyer is taker)
    int64_t sellerInitiated = 0;   // m = true (seller is taker)
    int64_t windowStartTime = 0;   // exchange timestamp (ms)
    
    bool hasData() const { return tradeCount > 0; }
    
    // Сброс статистики для нового окна
    void reset(int64_t newWindowStart) {
        tradeCount = 0;
        totalVolume = 0.0;
        minPrice = std::numeric_limits<double>::max();
        maxPrice = std::numeric_limits<double>::lowest();
        buyerInitiated = 0;
        sellerInitiated = 0;
        windowStartTime = newWindowStart;
    }
};

// Callback для передачи агрегированных данных в FileWriter
using AggregationCallback = std::function<void(const std::map<std::string, TradeStats>&)>;

class Aggregator {
public:
    // windowMs: длительность окна агрегации (например, 1000 мс)
    explicit Aggregator(int64_t windowMs);
    ~Aggregator();

    // Добавление трейда (вызывается из WebSocket callback)
    void addTrade(const std::string& symbol, 
                  double price, 
                  double quantity, 
                  bool isBuyerMaker,  // true = seller-initiated
                  int64_t exchangeTimeMs);

    // Установка callback для отдачи агрегированных данных
    void setAggregationCallback(AggregationCallback callback);

    // Запуск таймера агрегации (вызывать в отдельном потоке)
    void start();
    
    // Остановка
    void stop();

private:
    // Внутренний метод для сброса окон
    void processWindows();

    int64_t windowMs_;
    std::map<std::string, TradeStats> stats_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    AggregationCallback callback_;
    std::atomic<bool> isRunning_;
    std::thread workerThread_;
    
    // Для отслеживания последнего окна по каждому символу
    std::map<std::string, int64_t> lastWindowStart_;
};

} // namespace cqg