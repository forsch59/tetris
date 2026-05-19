#include "network.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>

#ifndef CLIENT_ID
#define CLIENT_ID 0
#endif

NetworkClient::NetworkClient() {
    NET_Init();
}

NetworkClient::~NetworkClient() {
    NET_Quit();
}

bool NetworkClient::connect(const char* host, uint16_t port) {
    SDL_Log("[NET %d] Resolving %s...", CLIENT_ID, host);
    addr.reset(NET_ResolveHostname(host));
    if (!addr) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Failed to start resolution: %s", CLIENT_ID, SDL_GetError());
        return false;
    }

    target_port = port;
    state = ConnectionState::RESOLVING;
    return true;
}

bool NetworkClient::poll_event(NetworkEvent& out_event) {
    if (event_queue.empty()) return false;
    out_event = event_queue.front();
    event_queue.pop();
    return true;
}

void NetworkClient::update() {
    if (state == ConnectionState::RESOLVING) {
        NET_Status status = NET_GetAddressStatus(addr.get());
        if (status == NET_SUCCESS) {
            SDL_Log("[NET %d] Resolved, connecting to port %d...", CLIENT_ID, target_port);
            sock.reset(NET_CreateClient(addr.get(), target_port));
            if (!sock) {
                SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Socket creation failed: %s", CLIENT_ID, SDL_GetError());
                state = ConnectionState::DISCONNECTED;
            } else {
                state = ConnectionState::CONNECTING;
            }
        } else if (status == NET_FAILURE) {
            SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Host resolution failed: %s", CLIENT_ID, SDL_GetError());
            state = ConnectionState::DISCONNECTED;
        }
        return;
    }

    if (state == ConnectionState::CONNECTING) {
        NET_Status status = NET_GetConnectionStatus(sock.get());
        if (status == NET_SUCCESS) {
            SDL_Log("[NET %d] CONNECTED to server", CLIENT_ID);
            connected = true;
            state = ConnectionState::CONNECTED;
        } else if (status == NET_FAILURE) {
            SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Connection failed: %s", CLIENT_ID, SDL_GetError());
            state = ConnectionState::DISCONNECTED;
        }
        return;
    }

    if (state != ConnectionState::CONNECTED || !sock) {
        return;
    }

    if (NET_GetStreamSocketPendingWrites(sock.get()) > 512 * 1024) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] DISCONNECTED: Pending write queue overflow (>512KB)", CLIENT_ID);
        connected = false;
        state = ConnectionState::DISCONNECTED;
        return;
    }

    void* wait_array[1];
    wait_array[0] = sock.get();
    int ready_count = NET_WaitUntilInputAvailable(wait_array, 1, 0); // Non-blocking multiplex poll

    if (ready_count > 0) {
        uint8_t buffer[1024];
        int bytes_read;
        
        while (connected && (bytes_read = NET_ReadFromStreamSocket(sock.get(), buffer, sizeof(buffer))) > 0) {
            if (recv_buf_len + bytes_read > (int)sizeof(recv_buf)) {
                SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Receive buffer overflow!");
                connected = false;
                return;
            }
            std::copy_n(buffer, bytes_read, recv_buf + recv_buf_len);
            recv_buf_len += bytes_read;

            while (recv_buf_len >= 2) {
                uint16_t pkt_len = (recv_buf[0] << 8) | recv_buf[1]; // Big Endian
                if (recv_buf_len < 2 + pkt_len) break;

                handle_packet(recv_buf + 2, pkt_len);
                
                int remaining = recv_buf_len - (2 + pkt_len);
                if (remaining > 0) {
                    std::memmove(recv_buf, recv_buf + 2 + pkt_len, remaining);
                }
                recv_buf_len = remaining;
            }
        }
        
        if (connected && bytes_read == -1) {
            connected = false;
            SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] DISCONNECTED from server (read failure)", CLIENT_ID);
        }
    } else if (ready_count < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Multiplex poll error: %s", CLIENT_ID, SDL_GetError());
    }
}

void NetworkClient::handle_packet(const uint8_t* data, int len) {
    if (len < 6) return; // Minimum 6 bytes for header
    
    NetworkEvent event;
    event.type = static_cast<PacketType>(data[0]);
    event.client_id = data[1];
    event.sequence = (data[2] << 24) | (data[3] << 16) | (data[4] << 8) | data[5];

    if (event.type != PacketType::S_STATE_BROADCAST) {
        SDL_Log("[NET %d RECV] Type: %d, Len: %d", CLIENT_ID, (int)event.type, len);
    }
    
    // We update my_id if it's MATCH_START so we know what to send out
    if (event.type == PacketType::S_MATCH_START) {
        my_id = event.client_id;
    }

    if (event.type == PacketType::S_STATE_BROADCAST && len >= 112) {
        event.has_state = true;
        const uint8_t* p = data + 6;
        event.state.piece_type = (int8_t)p[0];
        event.state.piece_rot = (int8_t)p[1];
        event.state.piece_x = (int8_t)p[2];
        event.state.piece_y = (int8_t)p[3];
        event.state.piece_crystal_mask = (p[4] << 8) | p[5];
        std::copy_n(p + 6, 100, event.state.grid);
    } else if (len >= 8) {
        event.data = (data[6] << 8) | data[7];
    }

    event_queue.push(event);
}

bool NetworkClient::send_packet(const uint8_t* data, size_t len) {
    if (!sock || !connected) return false;
    uint8_t pkt_len_buf[2];
    pkt_len_buf[0] = (len >> 8) & 0xFF; // Big Endian
    pkt_len_buf[1] = len & 0xFF;

    if (!NET_WriteToStreamSocket(sock.get(), pkt_len_buf, 2)) return false;
    if (!NET_WriteToStreamSocket(sock.get(), data, len)) return false;
    return true;
}

void NetworkClient::send_command(PacketType type, uint16_t data) {
    if (!connected) return;
    if (type == PacketType::C_LOCK_PIECE) {
        SDL_Log("[NET %d] Sending C_LOCK_PIECE (requesting new piece)", CLIENT_ID);
    }
    uint8_t buf[8];
    buf[0] = static_cast<uint8_t>(type);
    buf[1] = my_id;
    uint32_t seq = sequence_counter++;
    buf[2] = (seq >> 24) & 0xFF;
    buf[3] = (seq >> 16) & 0xFF;
    buf[4] = (seq >> 8) & 0xFF;
    buf[5] = seq & 0xFF;
    buf[6] = (data >> 8) & 0xFF;
    buf[7] = data & 0xFF;

    if (!send_packet(buf, 8)) {
        connected = false;
    }
}

void NetworkClient::send_state(int8_t type, int8_t rot, int8_t x, int8_t y, uint16_t crystal_mask, const uint8_t* grid_data) {
    if (!connected || !sock) return;
    uint8_t buf[112];
    buf[0] = static_cast<uint8_t>(PacketType::C_STATE_UPDATE);
    buf[1] = my_id;
    uint32_t seq = sequence_counter++;
    buf[2] = (seq >> 24) & 0xFF;
    buf[3] = (seq >> 16) & 0xFF;
    buf[4] = (seq >> 8) & 0xFF;
    buf[5] = seq & 0xFF;
    
    buf[6] = (uint8_t)type;
    buf[7] = (uint8_t)rot;
    buf[8] = (uint8_t)x;
    buf[9] = (uint8_t)y;
    buf[10] = (crystal_mask >> 8) & 0xFF;
    buf[11] = crystal_mask & 0xFF;
    std::copy_n(grid_data, 100, buf + 12);

    if (!send_packet(buf, 112)) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET] Disconnected from server (write failure)");
    }
}
