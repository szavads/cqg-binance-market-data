#include <iostream>
#include <thread>
#include <csignal>
#include <boost/asio.hpp>
#include "app.h"
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
	std::cout << greeting_message() << std::endl;

	// TODO: Загрузка конфигурации (из config.json)
    int64_t windowMs = 1000;        // 1 секунда
    int64_t serializationIntervalMs = 5000;  // 5 секунд
    std::vector<std::string> pairs = {"btcusdt", "ethusdt"};

	boost::asio::io_context io_context;
	cqg::Aggregator aggregator(windowMs);
    cqg::WebSocketClient client(io_context);
    
	// FileWriter для записи в файл
    cqg::FileWriter writer("market_data.log", serializationIntervalMs);
    
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

