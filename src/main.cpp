#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "tetris_core.hpp"

#ifndef CLIENT_ID
#define CLIENT_ID 1
#endif

enum class GameState {
    MENU,
    PLAYING,
    GAME_OVER
};

struct AppContext {
    std::shared_ptr<NetworkClient> net_client;
    std::shared_ptr<SharedPieceQueue> shared_queue;
    TetrisBoard board1;
    TetrisBoard board2;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
    GameState state = GameState::MENU;
    bool skip_menu = true; // Flag for testing
    uint64_t match_start_time = 0;
    bool match_started = false;
    uint64_t global_freeze_until = 0;
};

SDL_AppResult SDL_Fail() {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return SDL_Fail();
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    char window_title[32];
    SDL_snprintf(window_title, sizeof(window_title), "Tetris Client %d", CLIENT_ID);
    
    SDL_WindowFlags window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    int window_w = 0;
    int window_h = 0;

#if defined(ANDROID)
    window_flags |= SDL_WINDOW_FULLSCREEN;
#else
    window_w = 800;
    window_h = 600;
#endif

    if (!SDL_CreateWindowAndRenderer(window_title, window_w, window_h, window_flags, &window, &renderer)) {
        return SDL_Fail();
    }

    *appstate = new AppContext{
        .window = window,
        .renderer = renderer,
    };

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = (AppContext*)appstate;

    if (event->type == SDL_EVENT_QUIT) {
        app->app_quit = SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            if (app->state == GameState::PLAYING || app->state == GameState::GAME_OVER) {
                app->state = GameState::MENU;
            }
        } else if (app->state == GameState::MENU || app->state == GameState::GAME_OVER) {
            if (event->key.key == SDLK_RETURN) {
                app->state = GameState::PLAYING;
                
                SDL_Log("[APP] Starting match, reset network...");
                app->net_client.reset();
                app->net_client = std::make_shared<NetworkClient>();
#if defined(ANDROID)
                SDL_Log("[APP] Connecting to emulator host (10.0.2.2)...");
                app->net_client->connect("10.0.2.2", 12345);
#else
                SDL_Log("[APP] Connecting to localhost...");
                app->net_client->connect("127.0.0.1", 12345);
#endif
                
                app->shared_queue = std::make_shared<SharedPieceQueue>(app->net_client);
                app->board1 = TetrisBoard();
                app->board2 = TetrisBoard();
                
                app->board1.set_shared_queue(app->shared_queue, true);
                app->board2.set_shared_queue(app->shared_queue, false);
            }
        } else if (app->state == GameState::PLAYING) {
            if (SDL_GetTicks() < app->global_freeze_until) return SDL_APP_CONTINUE;

            if (event->key.key == SDLK_LEFT) {
                app->board1.move(-1);
            } else if (event->key.key == SDLK_RIGHT) {
                app->board1.move(1);
            } else if (event->key.key == SDLK_UP) {
                app->board1.rotate();
            } else if (event->key.key == SDLK_DOWN) {
                app->board1.update(); // Soft drop
            } else if (event->key.key == SDLK_SPACE) {
                app->board1.hard_drop();
            } else if (event->key.key == SDLK_LCTRL || event->key.key == SDLK_RCTRL) {
                if (app->net_client) {
                    const auto& defs = app->net_client->get_power_defs();
                    int best_id = -1;
                    int max_cost = -1;
                    for (const auto& d : defs) {
                        if (app->board1.stored_powerups >= (int)d.cost && (int)d.cost > max_cost) {
                            max_cost = d.cost;
                            best_id = d.id;
                        }
                    }
                    if (best_id != -1) {
                        app->board1.stored_powerups -= max_cost;
                        app->net_client->send_activate_powerup(best_id);
                    }
                }
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_FINGER_DOWN) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.which == SDL_TOUCH_MOUSEID) {
            return SDL_APP_CONTINUE;
        }

        float x, y;
        float ui_scale = SDL_GetWindowDisplayScale(app->window);
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            x = event->button.x * ui_scale;
            y = event->button.y * ui_scale;
        } else {
            int w, h;
            SDL_GetRenderOutputSize(app->renderer, &w, &h);
            x = event->tfinger.x * w;
            y = event->tfinger.y * h;
        }

        if (app->state == GameState::MENU) {
            if (x >= 50.0f * ui_scale && x <= 250.0f * ui_scale && y >= 150.0f * ui_scale && y <= 210.0f * ui_scale) {
                app->app_quit = SDL_APP_SUCCESS;
            }
        } else if (app->state == GameState::PLAYING) {
            int render_w, render_h;
            SDL_GetRenderOutputSize(app->renderer, &render_w, &render_h);
            float right_area = (float)render_w * 0.70f + 10.0f * ui_scale;
            if (x >= right_area && x <= right_area + 100.0f * ui_scale && y >= 20.0f * ui_scale && y <= 70.0f * ui_scale) {
                app->state = GameState::MENU;
                return SDL_APP_CONTINUE;
            }

            if (y >= 500.0f * ui_scale && y <= 750.0f * ui_scale) {
                if (x >= 0.0f && x <= 100.0f * ui_scale) {
                    app->board1.move(-1);
                } else if (x > 100.0f * ui_scale && x <= 200.0f * ui_scale) {
                    app->board1.move(1);
                } else if (x > 200.0f * ui_scale && x <= 300.0f * ui_scale) {
                    app->board1.rotate();
                } else if (x > 300.0f * ui_scale && x <= 450.0f * ui_scale) {
                    app->board1.update();
                }
            }
            if (y >= 600.0f * ui_scale && y <= 680.0f * ui_scale && x >= 410.0f * ui_scale && x <= 490.0f * ui_scale) {
                if (SDL_GetTicks() < app->global_freeze_until) return SDL_APP_CONTINUE;
                if (app->net_client) {
                    const auto& defs = app->net_client->get_power_defs();
                    int best_id = -1;
                    int max_cost = -1;
                    for (const auto& d : defs) {
                        if (app->board1.stored_powerups >= (int)d.cost && (int)d.cost > max_cost) {
                            max_cost = d.cost;
                            best_id = d.id;
                        }
                    }
                    if (best_id != -1) {
                        app->board1.stored_powerups -= max_cost;
                        app->net_client->send_activate_powerup(best_id);
                    }
                }
            }
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = (AppContext*)appstate;
    SDL_SetRenderDrawColor(app->renderer, 40, 40, 45, 255); 
    SDL_RenderClear(app->renderer);
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    
    if (app->skip_menu && app->state == GameState::MENU) {
        app->state = GameState::PLAYING;
        app->skip_menu = false;
        app->net_client.reset();
        app->net_client = std::make_shared<NetworkClient>();
#if defined(ANDROID)
        app->net_client->connect("10.0.2.2", 12345);
#else
        app->net_client->connect("127.0.0.1", 12345);
#endif
        app->shared_queue = std::make_shared<SharedPieceQueue>(app->net_client);
        app->board1.set_shared_queue(app->shared_queue, true);
        app->board2.set_shared_queue(app->shared_queue, false);
    }

    float ui_scale = SDL_GetWindowDisplayScale(app->window);
    int render_w, render_h;
    SDL_GetRenderOutputSize(app->renderer, &render_w, &render_h);

    if (app->state == GameState::MENU) {
        SDL_SetRenderScale(app->renderer, 2.0f * ui_scale, 2.0f * ui_scale);
#if CLIENT_ID == 1
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "CLIENT 1 - TETRIS BATTLE");
#elif CLIENT_ID == 2
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "CLIENT 2 - TETRIS BATTLE");
#else
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "TETRIS BATTLE");
#endif
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 120.0f, "%s", "Press ENTER to Start");

        SDL_SetRenderDrawColor(app->renderer, 200, 50, 50, 255);
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
        SDL_FRect exit_btn = {50.0f * ui_scale, 150.0f * ui_scale, 200.0f * ui_scale, 60.0f * ui_scale};
        SDL_RenderFillRect(app->renderer, &exit_btn);
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_SetRenderScale(app->renderer, 2.0f * ui_scale, 2.0f * ui_scale);
        SDL_RenderDebugTextFormat(app->renderer, 45.0f, 85.0f, "%s", "EXIT");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

    } else if (app->state == GameState::GAME_OVER) {
        SDL_SetRenderScale(app->renderer, 2.0f * ui_scale, 2.0f * ui_scale);
        if (app->net_client && app->net_client->is_game_over()) {
            if (app->net_client->am_i_winner()) {
                SDL_SetRenderDrawColor(app->renderer, 0, 255, 0, 255);
                SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "YOU WIN!");
            } else {
                SDL_SetRenderDrawColor(app->renderer, 255, 50, 50, 255);
                SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "YOU LOSE!");
            }
        } else {
            SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "GAME OVER!");
        }
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 120.0f, "%s", "Press ENTER to Restart");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    } else if (app->state == GameState::PLAYING) {
        Uint64 current_time = SDL_GetTicks();
        if (app->net_client) {
            app->net_client->update();
            auto events = app->net_client->get_powerup_events();
            for (const auto& ev : events) {
                const auto& defs = app->net_client->get_power_defs();
                for (const auto& d : defs) {
                    if (d.id == ev.power_id) {
                        app->global_freeze_until = SDL_GetTicks() + d.freeze_time_ms;
                        bool is_activator = (ev.activator_id == app->net_client->get_my_id());
                        bool target_me = (d.target == 0 && is_activator) || (d.target == 1 && !is_activator);
                        if (target_me) app->board1.apply_power_effect(d.effect_type, d.effect_param);
                        break;
                    }
                }
            }
        }
        
        app->board1.process_network();
        app->board2.process_network();

        if (current_time >= app->global_freeze_until) {
            app->board1.tick(current_time);
            app->board2.tick(current_time);
        }
        
        uint64_t elapsed_ms = 0;
        if (app->net_client && app->net_client->is_opponent_ready()) {
            if (!app->match_started) {
                app->match_start_time = current_time;
                app->match_started = true;
            }
            elapsed_ms = current_time - app->match_start_time;
        } else {
            app->match_started = false;
        }

        int level = (int)(elapsed_ms / 30000) + 1;
        uint64_t drop_interval = 500;
        if (level > 1) {
            if (level >= 20) drop_interval = 50;
            else drop_interval = 500 - (level - 1) * 23;
        }

        static Uint64 last_time = 0;
        if (current_time >= app->global_freeze_until && current_time - last_time > drop_interval) {
            app->board1.update();
            app->board2.update();
            last_time = current_time;
        }

        if (app->board1.game_over || app->board2.game_over) app->state = GameState::GAME_OVER;

        SDL_SetRenderScale(app->renderer, 2.0f * ui_scale, 2.0f * ui_scale);
#if CLIENT_ID == 1
        SDL_RenderDebugTextFormat(app->renderer, 5.0f, 5.0f, "%s", "Client 1");
#elif CLIENT_ID == 2
        SDL_RenderDebugTextFormat(app->renderer, 5.0f, 5.0f, "%s", "Client 2");
#else
        SDL_RenderDebugTextFormat(app->renderer, 5.0f, 5.0f, "%s", "Tetris Battle");
#endif

        if (app->net_client) {
            if (!app->net_client->is_connected()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 100, 100, 255);
                SDL_RenderDebugTextFormat(app->renderer, 5.0f, 25.0f, "%s", "Waiting for server...");
            } else if (!app->net_client->is_opponent_ready()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
                int cd = app->net_client->get_countdown();
                if (cd > 0) SDL_RenderDebugTextFormat(app->renderer, 5.0f, 25.0f, "Match starts in: %d", cd);
                else SDL_RenderDebugTextFormat(app->renderer, 5.0f, 25.0f, "%s", "Waiting for opponent...");
            }
            if (app->net_client->has_weak_connection()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 165, 0, 255);
                SDL_RenderDebugTextFormat(app->renderer, 5.0f, 45.0f, "%s", "Weak Connection!");
            }
            if (SDL_GetTicks() < app->global_freeze_until) {
                SDL_SetRenderDrawColor(app->renderer, 0, 255, 255, 255);
                SDL_RenderDebugTextFormat(app->renderer, 5.0f, 65.0f, "%s", "POWER ACTIVE - FROZEN!");
            }
        }
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        float right_area = (float)render_w * 0.70f + 10.0f * ui_scale;
        SDL_SetRenderDrawColor(app->renderer, 200, 50, 50, 255);
        SDL_FRect play_exit_btn = {right_area, 20.0f * ui_scale, 100.0f * ui_scale, 50.0f * ui_scale};
        SDL_RenderFillRect(app->renderer, &play_exit_btn);
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_SetRenderScale(app->renderer, 1.5f * ui_scale, 1.5f * ui_scale);
        SDL_RenderDebugTextFormat(app->renderer, (right_area + 20.0f * ui_scale) / (1.5f * ui_scale), (35.0f * ui_scale) / (1.5f * ui_scale), "%s", "MENU");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        auto draw_board = [&](TetrisBoard& board, float offset_screen_x, float scale_factor) {
            float padding = 20.0f * ui_scale;
            float grid_area_w = ((float)render_w * 0.70f - padding * 2.0f) * scale_factor;
            float grid_area_h = ((float)render_h - padding * 2.0f) * scale_factor;
            float cell_w = grid_area_w / TetrisBoard::WIDTH;
            float cell_h = grid_area_h / TetrisBoard::HEIGHT;
            float cell_size = (cell_w < cell_h) ? cell_w : cell_h;
            float offset_x = offset_screen_x + padding + (grid_area_w - cell_size * TetrisBoard::WIDTH) / 2.0f;
            float y_offset = padding + (grid_area_h - cell_size * TetrisBoard::HEIGHT) / 2.0f;

            for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
                for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
                    SDL_FRect rect = {offset_x + x * cell_size, y_offset + y * cell_size, cell_size - 1.0f, cell_size - 1.0f};
                    int color_idx = board.grid[y][x].color;
                    if (color_idx != 0) {
                        // Locked blocks are beige
                        SDL_SetRenderDrawColor(app->renderer, 225, 215, 185, 255);
                        SDL_RenderFillRect(app->renderer, &rect);
                        if (board.grid[y][x].has_crystal) {
                            SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
                            float c_margin = cell_size * 0.2f;
                            SDL_FRect crect = {offset_x + x * cell_size + c_margin, y_offset + y * cell_size + c_margin, cell_size - c_margin * 2.0f, cell_size - c_margin * 2.0f};
                            SDL_RenderFillRect(app->renderer, &crect);
                        }
                    } else {
                        SDL_SetRenderDrawColor(app->renderer, 80, 80, 80, 255);
                        SDL_RenderFillRect(app->renderer, &rect);
                    }
                }
            }
            if (board.current_piece.type > 0 && board.current_piece.type <= 7) {
                const auto& c = TETROMINO_DEFS[board.current_piece.type].color;
                for(int r=0; r<4; r++) {
                    for(int c_idx=0; c_idx<4; c_idx++) {
                        if(board.current_piece.shape[r][c_idx]) {
                            SDL_FRect rect = {offset_x + (board.current_piece.x + c_idx) * cell_size, y_offset + (board.current_piece.y + r) * cell_size, cell_size - 1.0f, cell_size - 1.0f};
                            SDL_SetRenderDrawColor(app->renderer, c.r, c.g, c.b, c.a);
                            SDL_RenderFillRect(app->renderer, &rect);
                            if (board.current_piece.has_crystal[r][c_idx]) {
                                SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
                                float c_margin = cell_size * 0.2f;
                                SDL_FRect crect = {offset_x + (board.current_piece.x + c_idx) * cell_size + c_margin, y_offset + (board.current_piece.y + r) * cell_size + c_margin, cell_size - c_margin * 2.0f, cell_size - c_margin * 2.0f};
                                SDL_RenderFillRect(app->renderer, &crect);
                            }
                        }
                    }
                }
            }
            float pwr_w = cell_size * 0.8f;
            float pwr_x = offset_x + cell_size * TetrisBoard::WIDTH + 5.0f;
            for (int i = 0; i < TetrisBoard::HEIGHT; ++i) {
                SDL_FRect p_rect = {pwr_x, y_offset + (TetrisBoard::HEIGHT - 1 - i) * cell_size, pwr_w, cell_size - 1.0f};
                if (i < board.stored_powerups) {
                    SDL_SetRenderDrawColor(app->renderer, 255, 0, 255, 255);
                    SDL_RenderFillRect(app->renderer, &p_rect);
                } else {
                    SDL_SetRenderDrawColor(app->renderer, 80, 80, 80, 255);
                    SDL_RenderFillRect(app->renderer, &p_rect);
                }
            }
        };

        draw_board(app->board1, 0.0f, 1.0f);
        draw_board(app->board2, (float)render_w * 0.75f, 0.4f);

        float next_cell_size = 20.0f * ui_scale;
        float next_offset_x = (float)render_w * 0.75f;
        float next_offset_y = 100.0f * ui_scale;
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(app->renderer, next_offset_x, next_offset_y - 20.0f * ui_scale, "Next:");
        
        float info_y = next_offset_y + 15 * 20.0f * ui_scale;
        SDL_RenderDebugTextFormat(app->renderer, next_offset_x, info_y, "Level: %d", level);
        int sec = (int)(elapsed_ms / 1000) % 60;
        int min = (int)(elapsed_ms / 60000);
        SDL_RenderDebugTextFormat(app->renderer, next_offset_x, info_y + 20.0f * ui_scale, "Time: %02d:%02d", min, sec);

        if (app->board1.shared_queue && app->net_client) {
            int next_index = app->net_client->get_global_next_index();
            for (int i = 0; i < 3; i++) {
                PieceInfo info = app->board1.shared_queue->get_piece_at(next_index + i);
                const auto& def = TETROMINO_DEFS[info.type + 1];
                float current_next_y = next_offset_y + i * (next_cell_size * 4.5f);
                for(int r=0; r<4; r++) {
                    for(int c=0; c<4; c++) {
                        if(def.shape[r][c]) {
                            SDL_FRect rect = {next_offset_x + c * next_cell_size, current_next_y + r * next_cell_size, next_cell_size - 1.0f, next_cell_size - 1.0f};
                            SDL_SetRenderDrawColor(app->renderer, def.color.r, def.color.g, def.color.b, def.color.a);
                            SDL_RenderFillRect(app->renderer, &rect);
                            if (r == info.crystal_r && c == info.crystal_c) {
                                SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
                                float c_margin = next_cell_size * 0.2f;
                                SDL_FRect crect = {next_offset_x + c * next_cell_size + c_margin, current_next_y + r * next_cell_size + c_margin, next_cell_size - c_margin * 2.0f, next_cell_size - c_margin * 2.0f};
                                SDL_RenderFillRect(app->renderer, &crect);
                            }
                        }
                    }
                }
            }
        }
        
        SDL_SetRenderDrawColor(app->renderer, 100, 100, 100, 255);
        SDL_FRect left_btn = {10.0f * ui_scale, 600.0f * ui_scale, 80.0f * ui_scale, 80.0f * ui_scale};
        SDL_FRect right_btn = {110.0f * ui_scale, 600.0f * ui_scale, 80.0f * ui_scale, 80.0f * ui_scale};
        SDL_FRect rot_btn = {210.0f * ui_scale, 600.0f * ui_scale, 80.0f * ui_scale, 80.0f * ui_scale};
        SDL_FRect drop_btn = {310.0f * ui_scale, 600.0f * ui_scale, 80.0f * ui_scale, 80.0f * ui_scale};
        SDL_FRect pwr_btn = {410.0f * ui_scale, 600.0f * ui_scale, 80.0f * ui_scale, 80.0f * ui_scale};
        
        SDL_RenderFillRect(app->renderer, &left_btn);
        SDL_RenderFillRect(app->renderer, &right_btn);
        SDL_RenderFillRect(app->renderer, &rot_btn);
        SDL_RenderFillRect(app->renderer, &drop_btn);
        SDL_SetRenderDrawColor(app->renderer, 255, 0, 255, 255);
        SDL_RenderFillRect(app->renderer, &pwr_btn);

        SDL_SetRenderScale(app->renderer, 2.0f * ui_scale, 2.0f * ui_scale);
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(app->renderer, 15.0f, 315.0f, "%s", "<");
        SDL_RenderDebugTextFormat(app->renderer, 65.0f, 315.0f, "%s", ">");
        SDL_RenderDebugTextFormat(app->renderer, 115.0f, 315.0f, "%s", "R");
        SDL_RenderDebugTextFormat(app->renderer, 165.0f, 315.0f, "%s", "v");
        SDL_RenderDebugTextFormat(app->renderer, 215.0f, 315.0f, "%s", "P");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    }

    SDL_RenderPresent(app->renderer);
    return app->app_quit;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = (AppContext*)appstate;
    if (app) {
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        delete app;
    }
    SDL_Quit();
}
