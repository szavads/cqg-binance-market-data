\# CQG Test Task: Binance Market Data Service



\## Architecture Overview



```mermaid

graph LR

&#x20;   A\[Binance WebSocket] -->|Raw JSON| B(WebSocketClient)

&#x20;   B -->|Parsed Trade| C{Aggregator}

&#x20;   C -->|Time Window| D\[FileWriter]

&#x20;   D -->|CSV/Log| E\[Output File]

&#x20;   F\[Config] --> B

&#x20;   F --> C

&#x20;   F --> D

