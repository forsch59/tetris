#pragma once

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#pragma once

#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <queue>

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
    C_SEND_GARBAGE = 15,     // Client sends garbage to opponent
    S_GARBAGE_SIGNAL = 16,   // Server notifies about incoming garbage
    C_REQUEST_POWER = 17,    // Client requests to send a power
    S_POWER_ACTIVATED = 18,  // Server broadcasts power activation (data = power_id)
    S_POWER_DEACTIVATED = 19 // Server broadcasts power deactivation (data = power_id)
};

struct GameStatePayload {
    int8_t piece_type;
    int8_t piece_rot;
    int8_t piece_x;
    int8_t piece_y;
    uint16_t piece_crystal_mask; // 16 bits for 4x4 crystal matrix
    uint8_t grid[100]; // 10x20 grid, 4 bits per cell = 100 bytes
};

struct NetworkEvent {
    PacketType type;
    uint8_t client_id;
    uint32_t sequence;
    
    bool has_state = false;
    uint16_t data = 0;
    GameStatePayload state{};
};

class NetworkClient {
public:
    enum class ConnectionState {
        DISCONNECTED,
        RESOLVING,
        CONNECTING,
        CONNECTED
    };

    NetworkClient();
    ~NetworkClient();

    bool connect(const char* host, uint16_t port);
    void update();
    
    void send_command(PacketType type, uint16_t data = 0);
    void send_state(int8_t type, int8_t rot, int8_t x, int8_t y, uint16_t crystal_mask, const uint8_t* grid_data);

    bool is_connected() const { return connected; }
    uint8_t get_id() const { return my_id; }
    
    // Event polling
    bool poll_event(NetworkEvent& out_event);

private:
    std::unique_ptr<NET_StreamSocket, void(*)(NET_StreamSocket*)> sock{nullptr, NET_DestroyStreamSocket};
    std::unique_ptr<NET_Address, void(*)(NET_Address*)> addr{nullptr, NET_UnrefAddress};
    ConnectionState state = ConnectionState::DISCONNECTED;
    uint16_t target_port = 0;
    bool connected = false;
    uint32_t sequence_counter = 0;
    uint8_t my_id = 0;

    std::queue<NetworkEvent> event_queue;

    uint8_t recv_buf[4096];
    int recv_buf_len = 0;

    void handle_packet(const uint8_t* data, int len);
    bool send_packet(const uint8_t* data, size_t len);
};
