#!/bin/bash

cleanup() {
    kill -9 $SERVER_PID $T1_PID $T2_PID >/dev/null 2>&1
    sleep 0.1
    trap - INT
    kill -INT $$
}

trap cleanup INT

# Kill stale server and clients
pkill -f "python3 backend/server.py" || true
pkill -f "./build/Tetris" || true

cmake -B build -S . && cmake --build build -j$(nproc)

python3 backend/server.py &
SERVER_PID=$!

./build/Tetris1 &
T1_PID=$!

./build/Tetris2 &
T2_PID=$!

wait $SERVER_PID $T1_PID $T2_PID
