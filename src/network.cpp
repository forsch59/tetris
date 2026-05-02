#include "network.hpp"
#include <SDL3/SDL.h>
#include <cstring>

#ifndef CLIENT_ID
#define CLIENT_ID 0
#endif

NetworkClient::NetworkClient() {
    NET_Init();
}

NetworkClient::~NetworkClient() {
    if (sock) NET_DestroyStreamSocket(sock);
    if (addr) NET_UnrefAddress(addr);
    NET_Quit();
}

bool NetworkClient::connect(const char* host, uint16_t port) {
    SDL_Log("[NET %d] Resolving %s...", CLIENT_ID, host);
    addr = NET_ResolveHostname(host);
    if (!addr) return false;
    
    if (NET_WaitUntilResolved(addr, 5000) != NET_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Host resolution failed", CLIENT_ID);
        return false;
    }

    SDL_Log("[NET %d] Connecting to %s:%d...", CLIENT_ID, host, port);
    sock = NET_CreateClient(addr, port);
    if (!sock) {
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Socket creation failed: %s", CLIENT_ID, SDL_GetError());
        return false;
    }

    if (NET_WaitUntilConnected(sock, 5000) == NET_SUCCESS) {
        connected = true;
        SDL_Log("[NET %d] CONNECTED to server", CLIENT_ID);
        return true;
    }
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] Connection timed out: %s", CLIENT_ID, SDL_GetError());
    return false;
}

void NetworkClient::update() {
    if (!sock || !connected) return;

    uint8_t buffer[1024];
    int bytes_read;
    
    // SDL_net 3 stream sockets are reliable.
    while (connected && (bytes_read = NET_ReadFromStreamSocket(sock, buffer, sizeof(buffer))) > 0) {
        if (recv_buf_len + bytes_read > (int)sizeof(recv_buf)) {
            SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Receive buffer overflow!");
            connected = false;
            return;
        }
        std::memcpy(recv_buf + recv_buf_len, buffer, bytes_read);
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
                SDL_Log("[NET %d] Granted Piece Index: %d", CLIENT_ID, granted_index);
            }
            break;
        }
        case PacketType::S_NEXT_PIECE_UPDATE: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                global_next_index = p->data;
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
        case PacketType::S_POWERUP_SIGNAL: {
            if (header->client_id != my_id) {
                powerup_received = true;
                SDL_Log("[NET %d] POWERUP signal received from opponent", CLIENT_ID);
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
    if (!NET_WriteToStreamSocket(sock, &pkt_len, 2)) return false;
    if (!NET_WriteToStreamSocket(sock, data, len)) return false;
    return true;
}

void NetworkClient::queue_lock_action() {
    if (!connected) return;
    CommandPacket p;
    p.header.type = PacketType::C_LOCK_PIECE;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.data = 0;
    if (!send_packet(&p, sizeof(p))) {
        connected = false;
    }
}

void NetworkClient::send_game_over() {
    if (!connected) return;
    CommandPacket p;
    p.header.type = PacketType::C_GAME_OVER;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.data = 0;
    send_packet(&p, sizeof(p));
}

void NetworkClient::send_activate_powerup() {
    if (!connected) return;
    CommandPacket p;
    p.header.type = PacketType::C_ACTIVATE_POWERUP;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.data = 0;
    if (send_packet(&p, sizeof(p))) {
        powerup_sent = true;
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
    std::memcpy(p.grid, grid_data, 100);
    if (!send_packet(&p, sizeof(p))) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET] Disconnected from server (write failure)");
    }
    weak_conn = false; 
}
