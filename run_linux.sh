#!/bin/bash
set -e

# Color codes
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

cleanup() {
    kill -9 $SERVER_PID $T1_PID $T2_PID >/dev/null 2>&1
    sleep 0.1
    trap - INT
    kill -INT $$
}

trap cleanup INT

# Kill stale server and clients
echo -e "${YELLOW}Cleaning up stale processes...${NC}"
pkill -f "python3 backend/server.py" || true
pkill -f "./build/Tetris" || true

# Force color output and run build
export CLICOLOR_FORCE=1
export CMAKE_COLOR_DIAGNOSTICS=ON

echo -e "${BLUE}Configuring build...${NC}"
cmake -B build -S . 

echo -e "${BLUE}Building project with clang-tidy...${NC}"
cmake --build build -j$(nproc)

echo -e "${GREEN}Starting backend server...${NC}"
python3 backend/server.py &
SERVER_PID=$!

echo -e "${GREEN}Starting Tetris Client 1...${NC}"
./build/Tetris1 &
T1_PID=$!

echo -e "${GREEN}Starting Tetris Client 2...${NC}"
./build/Tetris2 &
T2_PID=$!

echo -e "${BLUE}Wait for processes to finish...${NC}"
wait $SERVER_PID $T1_PID $T2_PID
