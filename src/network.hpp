#pragma once

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <cstdint>
#include <string>
#include <vector>

#pragma pack(push, 1)

enum class PacketType : uint8_t {
    C_CONNECT = 1,      // Client connects
    S_MATCH_START = 2,  // Server says match started
    C_LOCK_PIECE = 3,   // Client wants a new piece
    S_GRANT_PIECE = 4,  // Server grants piece index
    C_STATE_UPDATE = 5, // Client sends board state
    S_STATE_BROADCAST = 6, // Server broadcasts opponent state
    S_WEAK_CONNECTION = 7 // Server warns about lag
};

struct PacketHeader {
    PacketType type;
    uint8_t client_id;
    uint32_t sequence;
};

// 8-byte generic command packet (header is 6 bytes, so we have 2 bytes extra or use 8 total)
struct CommandPacket {
    PacketHeader header;
    uint16_t data;
};

struct GameStatePacket {
    PacketHeader header;
    int8_t piece_type;
    int8_t piece_rot;
    int8_t piece_x;
    int8_t piece_y;
    uint8_t grid[100]; // 10x20 grid, 4 bits per cell = 100 bytes
};

#pragma pack(pop)

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool connect(const char* host, uint16_t port);
    void update();
    
    void queue_lock_action();
    void send_state(int8_t type, int8_t rot, int8_t x, int8_t y, const uint8_t* grid_data);

    bool is_connected() const { return connected; }
    bool is_opponent_ready() const { return opponent_ready; }
    bool is_seed_ready() const { return seed_ready; }
    bool has_weak_connection() const { return weak_conn; }
    
    uint32_t get_seed() const { return seed; }
    int get_claimed_index() {
        int idx = granted_index;
        granted_index = -1;
        return idx;
    }
    
    const GameStatePacket& get_opponent_state() const { return opponent_state; }

private:
    NET_StreamSocket* sock = nullptr;
    NET_Address* addr = nullptr;
    bool connected = false;
    bool opponent_ready = false;
    bool seed_ready = false;
    bool weak_conn = false;
    uint32_t seed = 0;
    int granted_index = -1;
    uint32_t sequence_counter = 0;
    uint8_t my_id = 0;

    GameStatePacket opponent_state{};

    void handle_packet(const uint8_t* data, int len);
};
