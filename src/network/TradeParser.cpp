// src/network/TradeParser.cpp
#include "TradeParser.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace cqg {

std::optional<TradeEvent> parseTrade(const std::string& payload) {
    try {
        auto json = nlohmann::json::parse(payload);

        if (!json.contains("e") || json["e"] != "trade") {
            return std::nullopt;
        }

        TradeEvent event;
        event.symbol        = json.value("s", "");
        event.price         = std::stod(json.value("p", "0"));
        event.quantity      = std::stod(json.value("q", "0"));
        event.isBuyerMaker  = json.value("m", false);
        event.exchangeTimeMs = json.value("T", int64_t{0});

        return event;

    } catch (const std::exception& e) {
        spdlog::error("[WebSocket] Parse error: {}", e.what());
        return std::nullopt;
    }
}

} // namespace cqg
