# Binance Market Data Service

A C++ Linux service that connects to the Binance WebSocket API, subscribes to trade streams, and aggregates real-time market statistics to a file.

## Architecture

The service is split into four independent components with unidirectional data flow:

```
Binance WebSocket API
        │  (JSON over wss://)
        ▼
  WebSocketClient          – connects, parses raw JSON, fires TradeCallback
        │  (symbol, price, qty, isBuyerMaker, exchangeTimeMs)
        ▼
    Aggregator              – accumulates stats per symbol in tumbling time windows
        │  (map<symbol, TradeStats> per window)
        ▼
    FileWriter              – buffers and serialises stats to disk at hardware-clock intervals
        │
        ▼
   market_data.log
```

### Component responsibilities

| Component | Responsibility |
|---|---|
| `WebSocketClient` | TLS WebSocket connection to `stream.binance.com`. Parses Binance trade events (`@trade` stream). Exponential-backoff reconnect using websocketpp timers (no blocking sleep, no recursive calls). Graceful shutdown via `wsClient_.stop()`. |
| `Aggregator` | Maintains per-symbol `TradeStats` (tradeCount, totalVolume, minPrice, maxPrice, buyerInitiated, sellerInitiated). Window boundaries are derived from **exchange timestamps** — each trade is bucketed as `(exchangeTimeMs / windowMs) * windowMs`. A dedicated joinable thread fires `processWindows()` every `windowMs` using `condition_variable::wait_for` for instant shutdown response. |
| `FileWriter` | Receives aggregated stats from `Aggregator` via callback, buffers them in `pendingStats_`, and flushes to a file every `serialization_interval_ms` (hardware clock). Uses a joinable background thread with `condition_variable` for immediate wakeup on `stop()`. The output file is opened once at construction and closed after the writer thread joins. |
| `Config` | Loads `config.json` at startup. Validates required fields. Throws `std::runtime_error` on missing file or invalid values. |

### Threading model

```
main thread
  ├─ wsThread          → WebSocketClient::start() → wsClient_.run() (io_context loop)
  ├─ Aggregator::workerThread_   → processWindows() every windowMs
  └─ FileWriter::writerThread_   → flush pendingStats_ every intervalMs
```

All shared state is protected by `std::mutex`. `isRunning_` flags are `std::atomic<bool>`. Shutdown sequence: `aggregator.stop()` → `client.stop()` → `writer.stop()` → `wsThread.join()`.

### Design decisions

**Output file opened once for the lifetime of the service.**  
`FileWriter` opens the file in its constructor and holds the `std::ofstream` open until `stop()` is called and the writer thread has joined. This eliminates repeated `open`/`close` overhead on every flush interval and guarantees that a file-system error (e.g. disk full, path removed) is detected immediately at startup rather than silently mid-run. The `std::ofstream` is flushed after every write batch and closed only after all pending data has been written to disk.

### Why these libraries

- **websocketpp + Boost.Asio** — header-only WebSocket library with TLS support, integrates naturally with Boost.Asio's io_context. Required by the task (boost).
- **nlohmann/json** — single-header JSON parser, clean API, exception-safe.
- **OpenSSL** — TLS backend for wss:// connections via Boost.Asio SSL context.
- **GTest** — industry-standard unit testing framework for C++.
- **spdlog** — reserved for structured logging (currently using std::cout; pluggable without interface changes).

## Project Structure

```
.
├── CMakeLists.txt
├── conanfile.txt
├── config/
│   └── config.json
├── src/
│   ├── main.cpp
│   ├── config/
│   │   ├── Config.hpp
│   │   └── Config.cpp
│   ├── core/
│   │   ├── Aggregator.hpp
│   │   └── Aggregator.cpp
│   ├── network/
│   │   ├── WebSocketClient.hpp
│   │   └── WebSocketClient.cpp
│   └── storage/
│       ├── FileWriter.hpp
│       └── FileWriter.cpp
└── tests/
    ├── CMakeLists.txt
    ├── AggregatorTests.cpp
    ├── ConfigTests.cpp
    └── FileWriterTests.cpp
```

## Configuration

`config/config.json`:

```json
{
  "trading_pairs": ["btcusdt", "ethusdt"],
  "aggregation_window_ms": 1000,
  "serialization_interval_ms": 5000,
  "output_file": "market_data.log"
}
```

| Field | Description |
|---|---|
| `trading_pairs` | List of Binance stream names (lowercase). Streams `<pair>@trade` are subscribed automatically. |
| `aggregation_window_ms` | Duration of one aggregation window in milliseconds (uses exchange timestamp). |
| `serialization_interval_ms` | How often aggregated stats are flushed to file (hardware clock). |
| `output_file` | Output file path (relative to working directory). |

## Output Format

```
timestamp=2026-01-12T14:23:20Z
symbol=BTCUSDT trades=154 volume=23.51 min=43012.1 max=43189.4
buy=82 sell=72
symbol=ETHUSDT trades=231 volume=112.7 min=2289.2 max=2301.8
buy=120 sell=111
```

Records with no trades in the current window are skipped.

## Build

Install dependencies with Conan, then build with CMake:

```bash
pip install conan
conan install . --output-folder=build --build=missing -s build_type=Release
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

```bash
./build/Release/binance_service
```

The service handles `SIGINT` and `SIGTERM` for graceful shutdown (flushes pending data before exit).

## Testing

```bash
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

32 unit tests across four suites:

| Suite | Tests | What is covered |
|---|---|---|
| `ConfigTest` | 6 | Valid load, defaults, invalid path, invalid JSON, validation |
| `FileWriterTest` | 8 | File creation, error handling, flush, format, shutdown |
| `AggregatorTest` | 11 | Trade accumulation, window reset, buy/sell semantics, callback, stats reset after flush |
| `TradeParserTest` | 7 | Valid trade, buyer/seller flags, non-trade events, missing fields, malformed JSON, empty payload |

## Docker

Build and run the service in a container (Linux target, TLS verify_peer enabled):

```bash
docker build -t binance_service .
docker run --rm binance_service
```

To override the config without rebuilding the image:

```bash
docker run --rm \
  -v $(pwd)/config/config.json:/app/config/config.json \
  binance_service
```

To persist the output log on the host:

```bash
docker run --rm \
  -v $(pwd)/config/config.json:/app/config/config.json \
  -v $(pwd)/logs:/app \
  binance_service
```

