#include <iostream>
#include <thread>
#include <csignal>
#include <boost/asio.hpp>
#include "config/Config.hpp"
#include "network/WebSocketClient.hpp"
#include "storage/FileWriter.hpp"

// Глобальный флаг для graceful shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signum) {
    std::cout << "\n[Signal] Interrupt received (" << signum << "), shutting down..." << std::endl;
    g_running = false;
}


int main()
{
	// Загрузка конфигурации (из config.json)
	cqg::Config config = cqg::loadConfig("config/config.json");
    std::cout << "[Config] Loaded: Window=" << config.aggregation_window_ms 
              << "ms, Interval=" << config.serialization_interval_ms << "ms" << std::endl;
    int64_t windowMs = config.aggregation_window_ms;
    int64_t serializationIntervalMs = config.serialization_interval_ms;
    std::vector<std::string> pairs = config.trading_pairs;

	boost::asio::io_context io_context;
	cqg::Aggregator aggregator(windowMs);
    cqg::WebSocketClient client(io_context);
    
	// FileWriter для записи в файл
    cqg::FileWriter writer(config.output_file, serializationIntervalMs);
    
 	// Связываем Aggregator → FileWriter
    aggregator.setAggregationCallback([&writer](const std::map<std::string, cqg::TradeStats>& stats) {
        writer.write(stats);
    });
    
    // Связываем WebSocket → Aggregator
    client.setTradeCallback([&aggregator](const std::string& symbol, 
                                           double price, 
                                           double quantity, 
                                           bool isBuyerMaker,
                                           int64_t exchangeTime) {
        aggregator.addTrade(symbol, price, quantity, isBuyerMaker, exchangeTime);
    });
    
	
    // Подписка на торговые пары
    std::vector<std::string> streams;
    for (const auto& pair : pairs) {
        streams.push_back(pair + "@trade");
    }
    client.subscribe(streams);

    // Запуск агрегатора
    aggregator.start();

	// Запуск WebSocket в отдельном потоке
    std::thread wsThread([&client]() {
        client.start();
    });
    
	// Ожидание сигнала завершения
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

	// Graceful shutdown
    std::cout << "[Main] Stopping services..." << std::endl;
    aggregator.stop();
    client.stop();
    writer.stop();
    
    wsThread.join();
    
    std::cout << "[Main] Service stopped gracefully" << std::endl;
    return 0;
}

