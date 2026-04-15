#include "Aggregator.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace cqg {

Aggregator::Aggregator(int64_t windowMs)
    : windowMs_(windowMs)
    , isRunning_(false) {}

Aggregator::~Aggregator() {
    stop();
}

void Aggregator::setAggregationCallback(AggregationCallback callback) {
    callback_ = std::move(callback);
}

void Aggregator::addTrade(const std::string& symbol, 
                          double price, 
                          double quantity, 
                          bool isBuyerMaker,
                          int64_t exchangeTimeMs) {
    // Thread-safe добавление данных
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Определяем начало окна для этого трейда
    int64_t windowStart = (exchangeTimeMs / windowMs_) * windowMs_;
    
    // Если это новый символ или новое окно — сбрасываем
    auto it = lastWindowStart_.find(symbol);
    if (it == lastWindowStart_.end() || it->second != windowStart) {
        stats_[symbol].reset(windowStart);
        lastWindowStart_[symbol] = windowStart;
    }
    
    // Обновляем статистику
    auto& stats = stats_[symbol];
    stats.symbol = symbol;
    stats.tradeCount++;
    stats.totalVolume += price * quantity;
    stats.minPrice = std::min(stats.minPrice, price);
    stats.maxPrice = std::max(stats.maxPrice, price);
    
    // Binance: m = true → buyer is maker → seller-initiated trade
    if (isBuyerMaker) {
        stats.sellerInitiated++;
    } else {
        stats.buyerInitiated++;
    }
}

void Aggregator::start() {
    isRunning_ = true;
    workerThread_ = std::thread([this]() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(windowMs_),
                             [this] { return !isRunning_.load(); });
            }
            processWindows();
            if (!isRunning_) break;
        }
    });
}

void Aggregator::stop() {
    if (!isRunning_) return;
    isRunning_ = false;
    cv_.notify_one();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void Aggregator::processWindows() {
    // Thread-safe копирование данных для отправки
    std::map<std::string, TradeStats> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = stats_;
    }
    
    // Фильтруем только пары с данными и вызываем callback
    std::map<std::string, TradeStats> filtered;
    for (const auto& [symbol, stats] : snapshot) {
        if (stats.hasData()) {
            filtered[symbol] = stats;
        }
    }
    
    if (!filtered.empty() && callback_) {
        callback_(filtered);
        
        // Сбрасываем статистику после отправки
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [symbol, stats] : stats_) {
            if (stats.hasData()) {
                stats.reset((stats.windowStartTime + windowMs_));
            }
        }
    }
}

} // namespace cqg