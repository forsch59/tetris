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
    S_WEAK_CONNECTION = 7, // Server warns about lag
    S_COUNTDOWN = 8,       // Server sends countdown tick
    S_NEXT_PIECE_UPDATE = 9, // Server sends global next piece index
    C_GAME_OVER = 10,      // Client says I lost
    S_GAME_OVER = 11,      // Server says someone lost
    C_ACTIVATE_POWERUP = 12, // Client activates a powerup
    S_POWERUP_SIGNAL = 13   // Server signals a powerup was activated
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
    uint16_t piece_crystal_mask; // 16 bits for 4x4 crystal matrix
    uint8_t grid[100]; // 10x20 grid, 4 bits per cell = 100 bytes
};

#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 6, "PacketHeader size mismatch");
static_assert(sizeof(CommandPacket) == 8, "CommandPacket size mismatch");
static_assert(sizeof(GameStatePacket) == 112, "GameStatePacket size mismatch");

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool connect(const char* host, uint16_t port);
    void update();
    
    void queue_lock_action();
    void send_game_over();
    void send_state(int8_t type, int8_t rot, int8_t x, int8_t y, uint16_t crystal_mask, const uint8_t* grid_data);
    void send_activate_powerup();

    bool is_connected() const { return connected; }
    bool is_opponent_ready() const { return opponent_ready; }
    bool is_seed_ready() const { return seed_ready; }
    bool is_game_over() const { return remote_game_over; }
    bool am_i_winner() const { return remote_game_over && loser_id != my_id; }
    bool has_weak_connection() const { return weak_conn; }
    int get_countdown() const { return countdown_val; }
    int get_global_next_index() const { return global_next_index; }
    bool has_received_powerup() {
        bool val = powerup_received;
        powerup_received = false;
        return val;
    }
    bool has_sent_powerup() {
        bool val = powerup_sent;
        powerup_sent = false;
        return val;
    }
    
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
    bool remote_game_over = false;
    uint8_t loser_id = 0;
    bool weak_conn = false;
    uint32_t seed = 0;
    int granted_index = -1;
    uint32_t sequence_counter = 0;
    uint8_t my_id = 0;
    int countdown_val = -1;
    int global_next_index = 0;
    bool powerup_received = false;
    bool powerup_sent = false;

    uint8_t recv_buf[4096];
    int recv_buf_len = 0;

    GameStatePacket opponent_state{};

    void handle_packet(const uint8_t* data, int len);
    bool send_packet(const void* data, size_t len);
};
