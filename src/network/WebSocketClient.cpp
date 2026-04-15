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
    
    // Настройка TLS для wss:// соединений
    wsClient_.set_tls_init_handler([](websocketpp::connection_hdl) {
        auto ctx = std::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23);
        
        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3);
#ifdef _WIN32
            // Windows: OpenSSL (Conan) не интегрирован с системным хранилищем сертификатов,
            // verify_peer без явных CA будет падать. Только для dev-среды используем verify_none.
            ctx->set_verify_mode(boost::asio::ssl::verify_none);
#else
            // Linux: OpenSSL читает системные CA из /etc/ssl/certs применяются и работают из коробки
            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            ctx->set_default_verify_paths();
#endif
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
    
    connect(); // Устанавливаем соединение (без вызова run)
    wsClient_.run(); // Блокирующий вызов — завершится когда wsClient_.stop() будет вызван
}

void WebSocketClient::connect() {
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
            std::cerr << "[WebSocket] Connection error: " << ec.message() << std::endl;
            scheduleReconnect();
            return;
        }
        wsClient_.connect(con);
    } catch (const std::exception& e) {
        std::cerr << "[WebSocket] Connect error: " << e.what() << std::endl;
        scheduleReconnect();
    }
}

void WebSocketClient::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    
    // Закрываем активное соединение
    if (hdlValid_) {
        websocketpp::lib::error_code ec;
        wsClient_.close(hdl_, websocketpp::close::status::going_away, "Shutdown", ec);
        hdlValid_ = false;
    }
    
    // Останавливаем io_context — wsClient_.run() вернётся и wsThread завершится
    wsClient_.stop();
}

void WebSocketClient::onOpen(ConnectionHandle hdl) {
    hdl_ = hdl;
    hdlValid_ = true;
    std::cout << "[WebSocket] Connected to Binance" << std::endl;
    reconnectAttempts_ = 0;
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
    hdlValid_ = false;
    std::cout << "[WebSocket] Connection closed" << std::endl;
    if (isRunning_) {
        scheduleReconnect();
    }
}

void WebSocketClient::onError(ConnectionHandle hdl) {
    hdlValid_ = false;
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
    
    // Используем встроенный таймер websocketpp — не блокирует io_context поток
    // и не вызывает start() рекурсивно
    wsClient_.set_timer(delayMs, [this](websocketpp::lib::error_code const& ec) {
        if (!ec && isRunning_) {
            connect();
        }
    });
}

} // namespace cqg