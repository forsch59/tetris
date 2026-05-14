#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include "network.hpp"
#include "tetris_core.hpp"

enum class GameState {
    MENU,
    PLAYING,
    GAME_OVER
};

struct UILayout {
    SDL_FRect section1, section2, section3;
    SDL_FRect menu_btn;
    SDL_FRect opp_grid;
    SDL_FRect rot_btn;
    SDL_FRect left_btn;
    SDL_FRect next_area;
    SDL_FRect pwr_btn;
    SDL_FRect drop_btn;
    SDL_FRect right_btn;
    float padding;
    float section_padding;
    float text_scale;
};

struct AppContext {
    std::shared_ptr<NetworkClient> net_client;
    std::shared_ptr<SharedPieceQueue> shared_queue;
    TetrisBoard board1;
    TetrisBoard board2;
    std::unique_ptr<SDL_Window, void(*)(SDL_Window*)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, void(*)(SDL_Renderer*)> renderer{nullptr, SDL_DestroyRenderer};
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    GameState state = GameState::MENU;
    bool skip_menu = true; // Flag for testing
    uint64_t match_start_time = 0;
    bool match_started = false;
    
    bool board1_spawn_requested = false;
    bool game_over_sent = false;
    uint32_t last_sync = 0;

    bool down_btn_pressed = false;
    uint64_t last_down_press_time = 0;
    uint64_t down_btn_hold_start = 0;
    uint64_t last_soft_drop_time = 0;
};
