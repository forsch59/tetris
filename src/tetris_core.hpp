#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <random>
#include <atomic>
#include <memory>
#include <cstring>
#include "network.hpp"

struct PieceInfo {
    int type;
    int crystal_r; // -1 if no crystal
    int crystal_c;
};

const int SHAPES[7][4][4] = {
    {{0, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // I
    {{1, 1, 0, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // O
    {{0, 1, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // T
    {{0, 0, 1, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // L
    {{1, 0, 0, 0}, {1, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // J
    {{0, 1, 1, 0}, {1, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}, // S
    {{1, 1, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}  // Z
};

class SharedPieceQueue {
public:
    std::mt19937 rng;
    std::uniform_int_distribution<int> piece_dist;
    std::uniform_int_distribution<int> crystal_dist;
    std::vector<PieceInfo> sequence;
    std::shared_ptr<NetworkClient> net;
    bool seed_initialized = false;

    SharedPieceQueue(std::shared_ptr<NetworkClient> n) : piece_dist(0, 6), crystal_dist(0, 4), net(n) {}

    void ensure_seed() {
        if (!seed_initialized && net && net->is_seed_ready()) {
            rng.seed(net->get_seed());
            seed_initialized = true;
        }
    }

    PieceInfo get_piece_at(int index) {
        ensure_seed();
        while (index >= (int)sequence.size()) {
            int t = piece_dist(rng);
            int cry_r = -1, cry_c = -1;
            if (crystal_dist(rng) == 0) {
                // Determine which block of the base shape gets the crystal
                std::vector<std::pair<int, int>> blocks;
                for(int r=0; r<4; r++)
                    for(int c=0; c<4; c++)
                        if(SHAPES[t][r][c]) blocks.push_back({r, c});
                
                if (!blocks.empty()) {
                    std::uniform_int_distribution<int> b_dist(0, (int)blocks.size() - 1);
                    auto p = blocks[b_dist(rng)];
                    cry_r = p.first;
                    cry_c = p.second;
                }
            }
            sequence.push_back({t, cry_r, cry_c});
        }
        return sequence[index];
    }
};

struct Block {
    int color = 0; // 0 = empty, 1-7 = piece colors
    bool has_crystal = false;
};

struct Piece {
    int type = 0;
    int rotation = 0;
    int x = 0, y = 0;
    int shape[4][4] = {{0}};
    bool has_crystal[4][4] = {{false}};
};

class TetrisBoard {
public:
    static constexpr int WIDTH = 10;
    static constexpr int HEIGHT = 20;

    std::array<std::array<Block, WIDTH>, HEIGHT> grid{};
    Piece current_piece;
    int current_piece_index = -1;
    int crystals = 0;
    int stored_powerups = 0;
    int score = 0;
    bool game_over = false;
    bool waiting_for_spawn = true;
    bool board_active = true;
    bool spawn_request_pending = false;

    std::shared_ptr<SharedPieceQueue> shared_queue;

    TetrisBoard() {}

    void set_shared_queue(std::shared_ptr<SharedPieceQueue> q, bool active = true) {
        shared_queue = q;
        board_active = active;
        if (board_active) {
            waiting_for_spawn = true;
            spawn_request_pending = false;
        }
    }
    
    void request_spawn() {
        if (!shared_queue || !shared_queue->net || spawn_request_pending) return;
        spawn_request_pending = true;
        waiting_for_spawn = true;
        shared_queue->net->queue_lock_action();
        SDL_Log("[GAME] Requesting new piece from server...");
    }

    void pack_grid(uint8_t* out) {
        std::memset(out, 0, 100);
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                int idx = y * WIDTH + x;
                uint8_t val = (uint8_t)grid[y][x].color & 0x07;
                if (grid[y][x].has_crystal) val |= 0x08;
                if (idx % 2 == 0) {
                    out[idx / 2] |= val;
                } else {
                    out[idx / 2] |= (val << 4);
                }
            }
        }
    }

    void unpack_grid(const uint8_t* in) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                int idx = y * WIDTH + x;
                uint8_t val;
                if (idx % 2 == 0) {
                    val = in[idx / 2] & 0x0F;
                } else {
                    val = (in[idx / 2] >> 4) & 0x0F;
                }
                grid[y][x].color = val & 0x07;
                grid[y][x].has_crystal = (val & 0x08) != 0;
            }
        }
    }

    uint16_t get_crystal_mask() {
        uint16_t mask = 0;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                if (current_piece.has_crystal[r][c]) {
                    mask |= (1 << (r * 4 + c));
                }
            }
        }
        return mask;
    }

    void set_crystal_mask(uint16_t mask) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                current_piece.has_crystal[r][c] = (mask & (1 << (r * 4 + c))) != 0;
            }
        }
    }

    void process_network() {
        if (!shared_queue || !shared_queue->net) return;

        if (shared_queue->net->is_game_over()) {
            game_over = true;
        }

        if (!board_active) {
            const auto& opp = shared_queue->net->get_opponent_state();
            current_piece.type = opp.piece_type;
            current_piece.rotation = opp.piece_rot;
            current_piece.x = opp.piece_x;
            current_piece.y = opp.piece_y;
            set_crystal_mask(opp.piece_crystal_mask);
            update_shape_only();
            unpack_grid(opp.grid);
            return;
        }
        
        if (!shared_queue->net->is_opponent_ready()) return;

        if (waiting_for_spawn && !spawn_request_pending) {
            request_spawn();
        }
        
        if (spawn_request_pending) {
            int granted_index = shared_queue->net->get_claimed_index();
            if (granted_index != -1) {
                spawn_piece(granted_index);
                waiting_for_spawn = false;
                spawn_request_pending = false;
            }
        }
        
        static uint32_t last_sync = 0;
        uint32_t now = SDL_GetTicks();
        if (now - last_sync > 50) { // 20Hz update rate
            uint8_t packed[100];
            pack_grid(packed);
            shared_queue->net->send_state(current_piece.type, current_piece.rotation, current_piece.x, current_piece.y, get_crystal_mask(), packed);
            last_sync = now;
        }
    }

    void update_shape_only() {
        int t = current_piece.type - 1;
        if (t < 0 || t >= 7) {
            std::memset(current_piece.shape, 0, sizeof(current_piece.shape));
            std::memset(current_piece.has_crystal, 0, sizeof(current_piece.has_crystal));
            return;
        }
        int N = (current_piece.type == 1) ? 4 : (current_piece.type == 2 ? 2 : 3);
        int old_shape[4][4];
        std::memcpy(old_shape, SHAPES[t], sizeof(old_shape));
        for(int rot=0; rot < current_piece.rotation; rot++) {
            int new_shape[4][4] = {0};
            for(int r=0; r<N; r++)
                for(int c=0; c<N; c++)
                    new_shape[c][N-1-r] = old_shape[r][c];
            std::memcpy(old_shape, new_shape, sizeof(old_shape));
        }
        std::memcpy(current_piece.shape, old_shape, sizeof(old_shape));
    }

    void apply_rotation() {
        int t = current_piece.type - 1;
        if (t < 0 || t >= 7) return;
        int N = (current_piece.type == 1) ? 4 : (current_piece.type == 2 ? 2 : 3);

        PieceInfo info = {t, -1, -1};
        if (shared_queue) {
            info = shared_queue->get_piece_at(current_piece_index);
        }
        
        int work_shape[4][4];
        bool work_crystal[4][4];
        
        std::memcpy(work_shape, SHAPES[t], sizeof(work_shape));
        std::memset(work_crystal, 0, sizeof(work_crystal));
        if (info.crystal_r != -1) {
            work_crystal[info.crystal_r][info.crystal_c] = true;
        }

        for(int rot=0; rot < current_piece.rotation; rot++) {
            int next_shape[4][4] = {0};
            bool next_crystal[4][4] = {false};
            for(int r=0; r<N; r++) {
                for(int c=0; c<N; c++) {
                    next_shape[c][N-1-r] = work_shape[r][c];
                    next_crystal[c][N-1-r] = work_crystal[r][c];
                }
            }
            std::memcpy(work_shape, next_shape, sizeof(work_shape));
            std::memcpy(work_crystal, next_crystal, sizeof(work_crystal));
        }

        std::memcpy(current_piece.shape, work_shape, sizeof(work_shape));
        std::memcpy(current_piece.has_crystal, work_crystal, sizeof(work_crystal));
    }

    void spawn_piece(int index) {
        current_piece_index = index;
        PieceInfo info = shared_queue->get_piece_at(index);
        current_piece = {info.type + 1, 0, WIDTH / 2 - 2, 0};
        
        apply_rotation();
        
        if (check_collision()) {
            game_over = true;
            if (board_active) {
                shared_queue->net->send_game_over();
            }
        }
    }

    void update() {
        if (game_over || waiting_for_spawn || !shared_queue || !shared_queue->net || !shared_queue->net->is_opponent_ready()) return;
        current_piece.y++;
        if (check_collision()) {
            current_piece.y--;
            lock_piece();
            clear_lines();
            if (board_active) {
                request_spawn();
            }
        }
    }

    void move(int dx) {
        if (waiting_for_spawn || !shared_queue || !shared_queue->net || !shared_queue->net->is_opponent_ready()) return;
        current_piece.x += dx;
        if (check_collision()) current_piece.x -= dx;
    }

    void rotate() {
        if (waiting_for_spawn || !shared_queue || !shared_queue->net || !shared_queue->net->is_opponent_ready()) return;
        int old_rot = current_piece.rotation;
        current_piece.rotation = (current_piece.rotation + 1) % 4;
        apply_rotation();
        if (check_collision()) {
            current_piece.rotation = old_rot;
            apply_rotation();
        }
    }

    void activate_powerup() {
        if (stored_powerups > 0 && shared_queue && shared_queue->net) {
            shared_queue->net->send_activate_powerup();
            stored_powerups = 0;
        }
    }

    bool check_collision() {
        for(int r=0; r<4; r++) {
            for(int c=0; c<4; c++) {
                if(current_piece.shape[r][c]) {
                    int nx = current_piece.x + c;
                    int ny = current_piece.y + r;
                    if(nx < 0 || nx >= WIDTH || ny >= HEIGHT) return true;
                    if(ny >= 0 && grid[ny][nx].color != 0) return true;
                }
            }
        }
        return false;
    }

    void lock_piece() {
        for(int r=0; r<4; r++) {
            for(int c=0; c<4; c++) {
                if(current_piece.shape[r][c]) {
                    int nx = current_piece.x + c;
                    int ny = current_piece.y + r;
                    if(ny >= 0 && ny < HEIGHT && nx >= 0 && nx < WIDTH) {
                        grid[ny][nx].color = current_piece.type;
                        if(current_piece.has_crystal[r][c]) {
                            grid[ny][nx].has_crystal = true;
                        }
                    }
                }
            }
        }
    }

    void clear_lines() {
        for(int y=HEIGHT-1; y>=0; y--) {
            bool full = true;
            for(int x=0; x<WIDTH; x++) if(grid[y][x].color == 0) full = false;
            if(full) {
                for(int x=0; x<WIDTH; x++) {
                    if(grid[y][x].has_crystal) {
                        crystals++;
                        if (stored_powerups < 4) stored_powerups++;
                    }
                }
                for(int yy=y; yy>0; yy--) grid[yy] = grid[yy-1];
                for(int x=0; x<WIDTH; x++) { grid[0][x].color = 0; grid[0][x].has_crystal = false; }
                y++;
            }
        }
    }
};
