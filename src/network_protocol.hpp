#pragma once

#include <cstdint>

enum class PacketType : uint8_t {
    C_SET_CHARACTER = 1,
    S_MATCH_START = 2,
    C_LOCK_PIECE = 3,
    S_GRANT_PIECE = 4,
    C_STATE_UPDATE = 5,
    S_STATE_BROADCAST = 6,
    S_WEAK_CONNECTION = 7,
    S_COUNTDOWN = 8,
    S_NEXT_PIECE_UPDATE = 9,
    C_GAME_OVER = 10,
    S_GAME_OVER = 11,
    C_SEND_GARBAGE = 15,
    S_GARBAGE_SIGNAL = 16,
    C_REQUEST_POWER = 17,
    S_POWER_ACTIVATED = 18,
    S_POWER_DEACTIVATED = 19,
};

#pragma pack(push, 1)

struct PacketHeader {
    uint8_t type;
    uint8_t client_id;
    uint32_t sequence;
};

struct CommandPacket {
    PacketHeader header;
    uint16_t data;
};

struct GameStatePayload {
    int8_t piece_type;
    int8_t piece_rot;
    int8_t piece_x;
    int8_t piece_y;
    uint16_t piece_crystal_mask;
    uint8_t grid[100];
};

struct StateUpdatePacket {
    PacketHeader header;
    int8_t piece_type;
    int8_t piece_rot;
    int8_t piece_x;
    int8_t piece_y;
    uint16_t crystal_mask;
    uint8_t grid[100];
};

#pragma pack(pop)
