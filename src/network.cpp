#include "network.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

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
    if (len < sizeof(PacketHeader)) return; // Minimum bytes for header
    
    const PacketHeader* hdr = reinterpret_cast<const PacketHeader*>(data);
    
    NetworkEvent event;
    event.type = static_cast<PacketType>(hdr->type);
    event.client_id = hdr->client_id;
    event.sequence = ntohl(hdr->sequence);

    if (event.type != PacketType::S_STATE_BROADCAST) {
        SDL_Log("[NET %d RECV] Type: %d, Len: %d", CLIENT_ID, (int)event.type, len);
    }
    
    // We update my_id if it's MATCH_START so we know what to send out
    if (event.type == PacketType::S_MATCH_START) {
        my_id = event.client_id;
    }

    if (event.type == PacketType::S_STATE_BROADCAST && len >= sizeof(StateUpdatePacket)) {
        event.has_state = true;
        const StateUpdatePacket* state_pkt = reinterpret_cast<const StateUpdatePacket*>(data);
        event.state.piece_type = state_pkt->piece_type;
        event.state.piece_rot = state_pkt->piece_rot;
        event.state.piece_x = state_pkt->piece_x;
        event.state.piece_y = state_pkt->piece_y;
        event.state.piece_crystal_mask = ntohs(state_pkt->crystal_mask);
        std::copy_n(state_pkt->grid, 100, event.state.grid);
    } else if (len >= sizeof(CommandPacket)) {
        const CommandPacket* cmd_pkt = reinterpret_cast<const CommandPacket*>(data);
        event.data = ntohs(cmd_pkt->data);
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
    
    CommandPacket pkt;
    pkt.header.type = static_cast<uint8_t>(type);
    pkt.header.client_id = my_id;
    pkt.header.sequence = htonl(sequence_counter++);
    pkt.data = htons(data);

    if (!send_packet(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt))) {
        connected = false;
    }
}

void NetworkClient::send_state(int8_t type, int8_t rot, int8_t x, int8_t y, uint16_t crystal_mask, const uint8_t* grid_data) {
    if (!connected || !sock) return;
    
    StateUpdatePacket pkt;
    pkt.header.type = static_cast<uint8_t>(PacketType::C_STATE_UPDATE);
    pkt.header.client_id = my_id;
    pkt.header.sequence = htonl(sequence_counter++);
    
    pkt.piece_type = type;
    pkt.piece_rot = rot;
    pkt.piece_x = x;
    pkt.piece_y = y;
    pkt.crystal_mask = htons(crystal_mask);
    std::copy_n(grid_data, 100, pkt.grid);

    if (!send_packet(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt))) {
        connected = false;
        SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "[NET] Disconnected from server (write failure)");
    }
}
