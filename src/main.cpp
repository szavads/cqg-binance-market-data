#include <iostream>
#include <thread>
#include <boost/asio.hpp>
#include "app.h"
#include "network/WebSocketClient.hpp"

int main()
{
	std::cout << greeting_message() << std::endl;

	boost::asio::io_context io_context;
    cqg::WebSocketClient client(io_context);
    
    // Подписка на торговые пары
    client.subscribe({"btcusdt@trade", "ethusdt@trade"});
    
    // Callback для обработки трейдов
    client.setTradeCallback([](const std::string& symbol, 
                               double price, 
                               double quantity, 
                               bool isBuyerMaker,
                               int64_t exchangeTime) {
        std::cout << "[" << symbol << "] Price: " << price 
                  << ", Qty: " << quantity 
                  << ", BuyerMaker: " << (isBuyerMaker ? "SELL" : "BUY")
                  << std::endl;
    });
    
    // Запуск в отдельном потоке
    std::thread wsThread([&client]() {
        client.start();
    });
    
    // Даем поработать 30 секунд для теста
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    // Graceful shutdown
    client.stop();
    wsThread.join();
    
    std::cout << "Service stopped gracefully" << std::endl;	

	return 0;
}

