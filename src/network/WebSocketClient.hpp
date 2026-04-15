// src/network/WebSocketClient.hpp
#pragma once

#include <boost/asio.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

namespace cqg {

// Callback fired on each parsed Binance trade event
using TradeCallback = std::function<void(const std::string& symbol, 
                                          double price, 
                                          double quantity, 
                                          bool isBuyerMaker,
                                          int64_t exchangeTime)>;

class WebSocketClient {
public:
    WebSocketClient(boost::asio::io_context& io_context);
    ~WebSocketClient();

    // Subscribe to Binance trade streams (e.g. "btcusdt@trade")
    void subscribe(const std::vector<std::string>& streams);
    
    // Connect and start the io_context loop (blocks until stop() is called)
    void start();
    
    // Close the connection and stop the io_context loop
    void stop();
    
    // Set the callback invoked for each received trade
    void setTradeCallback(TradeCallback callback);

private:
    // WebSocket types
    using WsClient = websocketpp::client<websocketpp::config::asio_tls_client>;
    using ConnectionHandle = websocketpp::connection_hdl;
    
    // Event handlers
    void onOpen(ConnectionHandle hdl);
    void onMessage(ConnectionHandle hdl, WsClient::message_ptr msg);
    void onClose(ConnectionHandle hdl);
    void onError(ConnectionHandle hdl);
    
    // Reconnect logic
    void scheduleReconnect();
    
    // Establish a WebSocket connection (does not call run())
    void connect();
    
    // State
    WsClient wsClient_;
    std::vector<std::string> streams_;
    TradeCallback tradeCallback_;
    std::atomic<bool> isRunning_;
    int reconnectAttempts_;
    ConnectionHandle hdl_;
    bool hdlValid_ = false;
    static constexpr int MAX_RECONNECT_DELAY_MS = 60000;
    static constexpr int RECONNECT_BASE_DELAY_MS = 1000;
};

} // namespace cqg