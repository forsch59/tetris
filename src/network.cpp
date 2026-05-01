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
        handle_packet(buffer, bytes_read);
    }
    
    if (connected && bytes_read == -1) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET %d] DISCONNECTED from server (read failure)", CLIENT_ID);
    }
}

void NetworkClient::handle_packet(const uint8_t* data, int len) {
    if (len < (int)sizeof(PacketHeader)) return;
    
    const PacketHeader* header = (const PacketHeader*)data;
    
    switch (header->type) {
        case PacketType::S_MATCH_START: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                my_id = p->header.client_id;
                seed = p->data;
                seed_ready = true;
                opponent_ready = true;
                SDL_Log("[NET %d] MATCH_START! ServerID: %d, Seed: %d", CLIENT_ID, my_id, seed);
            }
            break;
        }
        case PacketType::S_GRANT_PIECE: {
            if (len >= (int)sizeof(CommandPacket)) {
                const CommandPacket* p = (const CommandPacket*)data;
                granted_index = p->data;
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
        default:
            break;
    }
}

void NetworkClient::queue_lock_action() {
    if (!connected) return;
    CommandPacket p;
    p.header.type = PacketType::C_LOCK_PIECE;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.data = 0;
    NET_WriteToStreamSocket(sock, &p, sizeof(p));
}

void NetworkClient::send_state(int8_t type, int8_t rot, int8_t x, int8_t y, const uint8_t* grid_data) {
    if (!connected || !sock) return;
    GameStatePacket p;
    p.header.type = PacketType::C_STATE_UPDATE;
    p.header.client_id = my_id;
    p.header.sequence = sequence_counter++;
    p.piece_type = type;
    p.piece_rot = rot;
    p.piece_x = x;
    p.piece_y = y;
    std::memcpy(p.grid, grid_data, 100);
    if (!NET_WriteToStreamSocket(sock, &p, sizeof(p))) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET] Disconnected from server (write failure)");
    }
    weak_conn = false; 
}
