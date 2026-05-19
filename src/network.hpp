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

#include "network_protocol.hpp"

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
