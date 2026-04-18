#!/bin/bash
./build/binance_service & PID=$!; trap "kill $PID" INT; sleep 1 && tail -f logs/market_data.log; kill $PID
