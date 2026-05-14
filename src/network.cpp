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

    // Safety check: if pending queue grows too large (> 512 KB), something is wrong.
    if (NET_GetStreamSocketPendingWrites(sock.get()) > 512 * 1024) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] DISCONNECTED: Pending write queue overflow (>512KB)", CLIENT_ID);
        connected = false;
        state = ConnectionState::DISCONNECTED;
        return;
    }

    uint8_t buffer[1024];
    int bytes_read;
    
    // SDL_net 3 stream sockets are reliable.
    while (connected && (bytes_read = NET_ReadFromStreamSocket(sock.get(), buffer, sizeof(buffer))) > 0) {
        if (recv_buf_len + bytes_read > (int)sizeof(recv_buf)) {
            SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Receive buffer overflow!");
            connected = false;
            return;
        }
        std::copy_n(buffer, bytes_read, recv_buf + recv_buf_len);
        recv_buf_len += bytes_read;

        while (recv_buf_len >= 2) {
            uint16_t pkt_len = SDL_Swap16LE(*(uint16_t*)recv_buf);
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
}

void NetworkClient::handle_packet(const uint8_t* data, int len) {
    if (len < (int)sizeof(PacketHeader)) return;
    
    const PacketHeader* header = (const PacketHeader*)data;
    
    if (header->type != PacketType::S_STATE_BROADCAST) {
        SDL_Log("[NET %d RECV] Type: %d, Len: %d", CLIENT_ID, (int)header->type, len);
    }

    switch (header->type) {
        case PacketType::S_MATCH_START: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                my_id = p->header.client_id;
                seed = p->data;
                seed_ready = true;
                opponent_ready = true;
                countdown_val = -1;
                SDL_Log("[NET %d] MATCH_START! MyID: %d, Seed: %d", CLIENT_ID, my_id, seed);
            }
            break;
        }
        case PacketType::S_COUNTDOWN: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                countdown_val = p->data;
            }
            break;
        }
        case PacketType::S_GRANT_PIECE: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                granted_index = p->data;
                SDL_Log("[NET %d RECV] S_GRANT_PIECE: index=%d", CLIENT_ID, granted_index);
            }
            break;
        }
        case PacketType::S_NEXT_PIECE_UPDATE: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                global_next_index = p->data;
                SDL_Log("[NET %d RECV] S_NEXT_PIECE_UPDATE: next_global=%d", CLIENT_ID, global_next_index);
            }
            break;
        }
        case PacketType::S_STATE_BROADCAST: {
            if (len >= (int)sizeof(GameStatePacket)) {
                const GameStatePacket* p = (const GameStatePacket*)data;
                if (p->header.client_id != my_id) {
                    opponent_state = *p;
                }
            }
            break;
        }
        case PacketType::S_WEAK_CONNECTION: {
            weak_conn = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_CUSTOM, "Weak Connection detected!");
            break;
        }
        case PacketType::S_GAME_OVER: {
            remote_game_over = true;
            loser_id = header->client_id;
            SDL_Log("[NET %d] GAME OVER received from server. Loser: %d", CLIENT_ID, loser_id);
            break;
        }
        case PacketType::S_GARBAGE_SIGNAL: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                pending_garbage += p->data;
                SDL_Log("[NET %d] GARBAGE signal received: %d lines", CLIENT_ID, p->data);
            }
            break;
        }
        default:
            break;
    }
}

bool NetworkClient::send_packet(const void* data, size_t len) {
    if (!sock || !connected) return false;
    const PacketHeader* header = (const PacketHeader*)data;
    if (header->type != PacketType::C_STATE_UPDATE) {
        SDL_Log("[NET %d SEND] Type: %d, Len: %d", CLIENT_ID, (int)header->type, (int)len);
    }
    uint16_t pkt_len = SDL_Swap16LE((uint16_t)len);
    if (!NET_WriteToStreamSocket(sock.get(), &pkt_len, 2)) return false;
    if (!NET_WriteToStreamSocket(sock.get(), data, len)) return false;
    return true;
}

void NetworkClient::send_command(PacketType type, uint16_t data) {
    if (!connected) return;
    if (type == PacketType::C_LOCK_PIECE) {
        SDL_Log("[NET %d] Sending C_LOCK_PIECE (requesting new piece)", CLIENT_ID);
    }
    CommandPacket p;
    p.header.type = type;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.data = data;
    if (!send_packet(&p, sizeof(p))) {
        connected = false;
    }
}

void NetworkClient::send_state(int8_t type, int8_t rot, int8_t x, int8_t y, uint16_t crystal_mask, const uint8_t* grid_data) {
    if (!connected || !sock) return;
    GameStatePacket p;
    p.header.type = PacketType::C_STATE_UPDATE;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.piece_type = type;
    p.piece_rot = rot;
    p.piece_x = x;
    p.piece_y = y;
    p.piece_crystal_mask = crystal_mask;
    std::copy_n(grid_data, 100, p.grid);
    if (!send_packet(&p, sizeof(p))) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET] Disconnected from server (write failure)");
    }
    weak_conn = false; 
}
