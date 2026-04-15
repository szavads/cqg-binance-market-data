// src/network/TradeParser.hpp
#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace cqg {

// Parsed representation of a single Binance @trade event
struct TradeEvent {
    std::string symbol;
    double      price;
    double      quantity;
    bool        isBuyerMaker;  // true = seller-initiated trade
    int64_t     exchangeTimeMs;
};

// Parse a raw Binance WebSocket payload into a TradeEvent.
// Returns std::nullopt if the payload is not a trade event or is malformed.
std::optional<TradeEvent> parseTrade(const std::string& payload);

} // namespace cqg
