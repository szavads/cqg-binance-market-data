// src/network/WebSocketClient.hpp
#pragma once

#include <boost/asio.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <functional>
#include <string>
#include <vector>
#include <memory>

namespace cqg {

// Callback для обработки полученного трейда
using TradeCallback = std::function<void(const std::string& symbol, 
                                          double price, 
                                          double quantity, 
                                          bool isBuyerMaker,
                                          int64_t exchangeTime)>;

class WebSocketClient {
public:
    WebSocketClient(boost::asio::io_context& io_context);
    ~WebSocketClient();

    // Подписка на торговые пары (например, "btcusdt@trade")
    void subscribe(const std::vector<std::string>& streams);
    
    // Запуск клиента (неблокирующий)
    void start();
    
    // Остановка (graceful shutdown)
    void stop();
    
    // Установка callback для обработки трейдов
    void setTradeCallback(TradeCallback callback);

private:
    // WebSocket типы
    using WsClient = websocketpp::client<websocketpp::config::asio_tls_client>;
    using ConnectionHandle = websocketpp::connection_hdl;
    
    // Внутренние методы
    void onOpen(ConnectionHandle hdl);
    void onMessage(ConnectionHandle hdl, WsClient::message_ptr msg);
    void onClose(ConnectionHandle hdl);
    void onError(ConnectionHandle hdl);
    
    // Reconnect logic
    void scheduleReconnect();
    
    // Состояние
    WsClient wsClient_;
    std::vector<std::string> streams_;
    TradeCallback tradeCallback_;
    bool isRunning_;
    int reconnectAttempts_;
    static constexpr int MAX_RECONNECT_DELAY_MS = 60000;
    static constexpr int RECONNECT_BASE_DELAY_MS = 1000;
};

} // namespace cqg