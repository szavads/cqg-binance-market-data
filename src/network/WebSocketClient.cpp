// src/network/WebSocketClient.cpp
#include "WebSocketClient.hpp"
#include "TradeParser.hpp"
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>

namespace cqg {

WebSocketClient::WebSocketClient(boost::asio::io_context& io_context)
    : isRunning_(false)
    , reconnectAttempts_(0) {
    
    // Attach websocketpp to the shared io_context
    wsClient_.init_asio(&io_context);

    // TLS context for wss:// connections
    wsClient_.set_tls_init_handler([](websocketpp::connection_hdl) {
        auto ctx = std::make_shared<boost::asio::ssl::context>(
            boost::asio::ssl::context::sslv23);
        
        try {
            ctx->set_options(boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3);
#ifdef _WIN32
            // Windows: Conan OpenSSL is not integrated with the system certificate store.
            // Use verify_none for local development only.
            ctx->set_verify_mode(boost::asio::ssl::verify_none);
#else
            // Linux: OpenSSL reads system CAs from /etc/ssl/certs out of the box.
            ctx->set_verify_mode(boost::asio::ssl::verify_peer);
            ctx->set_default_verify_paths();
#endif
        } catch (const std::exception& e) {
            spdlog::error("[TLS] Error: {}", e.what());
        }
        
        return ctx;
    });
    

    // Suppress websocketpp internal logging (re-enable for debugging)
    wsClient_.clear_access_channels(websocketpp::log::alevel::all);

    // Register event handlers
    wsClient_.set_open_handler([this](ConnectionHandle hdl) { onOpen(hdl); });
    wsClient_.set_message_handler([this](ConnectionHandle hdl, WsClient::message_ptr msg) { 
        onMessage(hdl, msg); 
    });
    wsClient_.set_close_handler([this](ConnectionHandle hdl) { onClose(hdl); });
    wsClient_.set_fail_handler([this](ConnectionHandle hdl) { onError(hdl); });

    // Binance sends a ping frame every 20s; must reply with pong within 1 minute.
    // Returning true from the ping handler makes websocketpp send pong automatically.
    // Spec: recommended pong payload is empty, so we ignore the incoming payload.
    wsClient_.set_ping_handler([this](ConnectionHandle hdl, std::string) {
        return true;
    });
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
    
    connect();      // Initiate connection (does not block)
    wsClient_.run(); // Blocking: returns only when wsClient_.stop() is called
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
            spdlog::error("[WebSocket] Connection error: {}", ec.message());
            scheduleReconnect();
            return;
        }
        wsClient_.connect(con);
    } catch (const std::exception& e) {
        spdlog::error("[WebSocket] Connect error: {}", e.what());
        scheduleReconnect();
    }
}

void WebSocketClient::stop() {
    if (!isRunning_) return;
    
    isRunning_ = false;
    
    // Send a clean WebSocket close frame
    if (hdlValid_) {
        websocketpp::lib::error_code ec;
        wsClient_.close(hdl_, websocketpp::close::status::going_away, "Shutdown", ec);
        hdlValid_ = false;
    }

    // Stop the io_context so that wsClient_.run() returns
    wsClient_.stop();
}

void WebSocketClient::onOpen(ConnectionHandle hdl) {
    hdl_ = hdl;
    hdlValid_ = true;
    spdlog::info("[WebSocket] Connected to Binance");
    reconnectAttempts_ = 0;
}

void WebSocketClient::onMessage(ConnectionHandle hdl, WsClient::message_ptr msg) {
    if (msg->get_opcode() != websocketpp::frame::opcode::text) {
        return;
    }

    auto event = parseTrade(msg->get_payload());
    if (event && tradeCallback_) {
        tradeCallback_(event->symbol, event->price, event->quantity,
                       event->isBuyerMaker, event->exchangeTimeMs);
    }
}

void WebSocketClient::onClose(ConnectionHandle hdl) {
    hdlValid_ = false;
    spdlog::info("[WebSocket] Connection closed");
    if (isRunning_) {
        scheduleReconnect();
    }
}

void WebSocketClient::onError(ConnectionHandle hdl) {
    hdlValid_ = false;
    spdlog::error("[WebSocket] Connection error");
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
    
    spdlog::warn("[WebSocket] Reconnecting in {}ms (attempt {})", delayMs, reconnectAttempts_);
    
    // Use websocketpp's built-in timer, that is  non-blocking, avoids recursive start() calls
    wsClient_.set_timer(delayMs, [this](websocketpp::lib::error_code const& ec) {
        if (!ec && isRunning_) {
            connect();
        }
    });
}

} // namespace cqg