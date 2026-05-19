import ctypes

C_SET_CHARACTER = 1
S_MATCH_START = 2
C_LOCK_PIECE = 3
S_GRANT_PIECE = 4
C_STATE_UPDATE = 5
S_STATE_BROADCAST = 6
S_WEAK_CONNECTION = 7
S_COUNTDOWN = 8
S_NEXT_PIECE_UPDATE = 9
C_GAME_OVER = 10
S_GAME_OVER = 11
C_SEND_GARBAGE = 15
S_GARBAGE_SIGNAL = 16
C_REQUEST_POWER = 17
S_POWER_ACTIVATED = 18
S_POWER_DEACTIVATED = 19

PKT_NAMES = {
    1: "C_SET_CHARACTER",
    2: "S_MATCH_START",
    3: "C_LOCK_PIECE",
    4: "S_GRANT_PIECE",
    5: "C_STATE_UPDATE",
    6: "S_STATE_BROADCAST",
    7: "S_WEAK_CONNECTION",
    8: "S_COUNTDOWN",
    9: "S_NEXT_PIECE_UPDATE",
    10: "C_GAME_OVER",
    11: "S_GAME_OVER",
    15: "C_SEND_GARBAGE",
    16: "S_GARBAGE_SIGNAL",
    17: "C_REQUEST_POWER",
    18: "S_POWER_ACTIVATED",
    19: "S_POWER_DEACTIVATED",
}

class PacketHeader(ctypes.BigEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("type", ctypes.c_uint8),
        ("client_id", ctypes.c_uint8),
        ("sequence", ctypes.c_uint32),
    ]

class CommandPacket(ctypes.BigEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("header", PacketHeader),
        ("data", ctypes.c_uint16),
    ]

class GameStatePayload(ctypes.BigEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("piece_type", ctypes.c_int8),
        ("piece_rot", ctypes.c_int8),
        ("piece_x", ctypes.c_int8),
        ("piece_y", ctypes.c_int8),
        ("piece_crystal_mask", ctypes.c_uint16),
        ("grid", ctypes.c_uint8 * 100),
    ]

class StateUpdatePacket(ctypes.BigEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("header", PacketHeader),
        ("piece_type", ctypes.c_int8),
        ("piece_rot", ctypes.c_int8),
        ("piece_x", ctypes.c_int8),
        ("piece_y", ctypes.c_int8),
        ("crystal_mask", ctypes.c_uint16),
        ("grid", ctypes.c_uint8 * 100),
    ]

