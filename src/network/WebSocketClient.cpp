// src/network/WebSocketClient.cpp
#include "WebSocketClient.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

namespace cqg {

WebSocketClient::WebSocketClient(boost::asio::io_context& io_context)
    : isRunning_(false)
    , reconnectAttempts_(0) {
    
    // Инициализация WebSocket клиента
    wsClient_.init_asio(&io_context);
    
    // 🔧 Настройка TLS для wss:// соединений
    wsClient_.set_tls_init_handler([](websocketpp::connection_hdl) {
        auto ctx = std::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23);
        
        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3);
            ctx->set_verify_mode(boost::asio::ssl::verify_none); // Для упрощения (в production лучше verify_peer)
            // TODO: добавить CA-сертификаты для проверки сервера
            // ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            // ctx->set_default_verify_paths(); // Использовать системные CA-сертификаты
        } catch (const std::exception& e) {
            std::cerr << "[TLS] Error: " << e.what() << std::endl;
        }
        
        return ctx;
    });
    

    // Отключаем логирование (можно включить для отладки)
    wsClient_.clear_access_channels(websocketpp::log::alevel::all);
    
    // Регистрация обработчиков событий
    wsClient_.set_open_handler([this](ConnectionHandle hdl) { onOpen(hdl); });
    wsClient_.set_message_handler([this](ConnectionHandle hdl, WsClient::message_ptr msg) { 
        onMessage(hdl, msg); 
    });
    wsClient_.set_close_handler([this](ConnectionHandle hdl) { onClose(hdl); });
    wsClient_.set_fail_handler([this](ConnectionHandle hdl) { onError(hdl); });
}

WebSocketClient::~WebSocketClient() {
    stop();
}

void WebSocketClient::subscribe(const std::vector<std::string>& streams) {
    streams_ = streams;
}

void WebSocketClient::setTradeCallback(TradeCallback callback) {
    tradeCallback_ = std::move(callback);
}

void WebSocketClient::start() {
    if (isRunning_) return;
    
    isRunning_ = true;
    reconnectAttempts_ = 0;
    
    // Формируем URL для Binance WebSocket
    // Пример: wss://stream.binance.com:9443/ws/btcusdt@trade/ethusdt@trade
    std::string streamPath;
    for (size_t i = 0; i < streams_.size(); ++i) {
        streamPath += streams_[i];
        if (i < streams_.size() - 1) streamPath += "/";
    }
    
    std::string url = "wss://stream.binance.com:9443/ws/" + streamPath;
    
    try {
        websocketpp::lib::error_code ec;
        auto con = wsClient_.get_connection(url, ec);
        
        if (ec) {
            std::cerr << "Connection error: " << ec.message() << std::endl;
            scheduleReconnect();
            return;
        }
        
        wsClient_.connect(con);
        wsClient_.run(); // Блокирующий вызов (запускается в отдельном потоке)
        
    } catch (const std::exception& e) {
        std::cerr << "Start error: " << e.what() << std::endl;
        scheduleReconnect();
    }
}

void WebSocketClient::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    wsClient_.stop_listening();
    // TODO: close left connections gracefully
    // wsClient_.close_all_connections(websocketpp::close::status::normal, "Shutdown");
}

void WebSocketClient::onOpen(ConnectionHandle hdl) {
    std::cout << "[WebSocket] Connected to Binance" << std::endl;
    reconnectAttempts_ = 0; // Сброс счетчика при успешном подключении
}

void WebSocketClient::onMessage(ConnectionHandle hdl, WsClient::message_ptr msg) {
    if (msg->get_opcode() != websocketpp::frame::opcode::text) {
        return;
    }
    
    try {
        auto json = nlohmann::json::parse(msg->get_payload());
        
        // Парсинг сообщения о трейде
        // Формат Binance: {"e":"trade","E":123456789,"s":"BTCUSDT","t":12345,"p":"0.001","q":"100","m":true,...}
        if (json.contains("e") && json["e"] == "trade") {
            std::string symbol = json.value("s", "");
            double price = std::stod(json.value("p", "0"));
            double quantity = std::stod(json.value("q", "0"));
            bool isBuyerMaker = json.value("m", false); // true = seller-initiated
            int64_t exchangeTime = json.value("T", 0);
            
            if (tradeCallback_) {
                tradeCallback_(symbol, price, quantity, isBuyerMaker, exchangeTime);
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Parse error: " << e.what() << std::endl;
    }
}

void WebSocketClient::onClose(ConnectionHandle hdl) {
    std::cout << "[WebSocket] Connection closed" << std::endl;
    if (isRunning_) {
        scheduleReconnect();
    }
}

void WebSocketClient::onError(ConnectionHandle hdl) {
    std::cerr << "[WebSocket] Connection error" << std::endl;
    if (isRunning_) {
        scheduleReconnect();
    }
}

void WebSocketClient::scheduleReconnect() {
    if (!isRunning_) return;
    
    // Exponential Backoff: 1s, 2s, 4s, 8s, ... max 60s
    int delayMs = std::min(
        RECONNECT_BASE_DELAY_MS * (1 << reconnectAttempts_),
        MAX_RECONNECT_DELAY_MS
    );
    
    reconnectAttempts_++;
    
    std::cout << "[WebSocket] Reconnecting in " << delayMs << "ms (attempt " 
              << reconnectAttempts_ << ")" << std::endl;
    
    // Планируем повторное подключение
    // В реальной реализации нужно использовать таймер ASIO
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    
    if (isRunning_) {
        start(); // Рекурсивный вызов (упрощенно)
    }
}

} // namespace cqg