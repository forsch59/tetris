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

    bool down_btn_pressed = false;
    uint64_t last_down_press_time = 0;
    uint64_t down_btn_hold_start = 0;
    uint64_t last_soft_drop_time = 0;
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
    window_h = 800;
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
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_FINGER_UP) {
        app->down_btn_pressed = false;
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
            float center_panel_w = (float)render_w * 0.50f;
            float left_panel_w = (float)render_w * 0.25f;
            float right_panel_w = (float)render_w * 0.25f;

            if (center_panel_w > (float)render_h * 0.6f) {
                center_panel_w = (float)render_h * 0.6f;
                left_panel_w = (render_w - center_panel_w) * 0.5f;
                right_panel_w = left_panel_w;
            }

            float ui_padding = SDL_min((float)render_w, (float)render_h) * 0.02f;
            float menu_btn_w = left_panel_w - ui_padding * 2.0f;
            float menu_btn_h = (float)render_h * 0.05f;
            float menu_btn_x = ui_padding;
            float menu_btn_y = ui_padding;
            
            if (x >= menu_btn_x && x <= menu_btn_x + menu_btn_w && y >= menu_btn_y && y <= menu_btn_y + menu_btn_h) {
                app->state = GameState::MENU;
                return SDL_APP_CONTINUE;
            }

            float btn_size = SDL_min(left_panel_w, right_panel_w) * 0.45f;
            float btn_spacing = ui_padding;
            
            float left_col_x = ui_padding;
            float right_col_x = (float)render_w - btn_size - ui_padding;
            
            // Elements on the left should be top-left aligned
            // Elements on the right should be top-right aligned
            
            // Left Panel Buttons (Left, Rotate) - below opponent grid or just bottom?
            // "alignment should be top left for everything left of the grid"
            // Let's stack them at the bottom of the left panel.
            float left_btn_y = (float)render_h - btn_size - ui_padding;
            float rot_btn_y = left_btn_y - btn_size - btn_spacing;

            // Right Panel Buttons (Right, Drop, Power)
            float right_btn_y = (float)render_h - btn_size - ui_padding;
            float drop_btn_y = right_btn_y - btn_size - btn_spacing;
            float pwr_btn_y = drop_btn_y - btn_size - btn_spacing;

            auto in_rect = [&](float rx, float ry, float rw, float rh) {
                return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
            };

            if (in_rect(left_col_x, left_btn_y, btn_size, btn_size)) {
                app->board1.move(-1);
            } else if (in_rect(right_col_x, right_btn_y, btn_size, btn_size)) {
                app->board1.move(1);
            } else if (in_rect(left_col_x, rot_btn_y, btn_size, btn_size)) {
                app->board1.rotate();
            } else if (in_rect(right_col_x, drop_btn_y, btn_size, btn_size)) {
                uint64_t now = SDL_GetTicks();
                if (now - app->last_down_press_time < 250) { // Double tap
                    app->board1.hard_drop();
                    app->down_btn_pressed = false;
                } else {
                    app->board1.update(); // Initial soft drop
                    app->down_btn_pressed = true;
                    app->down_btn_hold_start = now;
                    app->last_soft_drop_time = now;
                }
                app->last_down_press_time = now;
            } else if (in_rect(right_col_x, pwr_btn_y, btn_size, btn_size)) {
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

    // Screen-relative layout dimensions
    // Use a more compact layout to reduce horizontal gaps
    float center_panel_w = (float)render_w * 0.50f;
    float left_panel_w = (float)render_w * 0.25f;
    float right_panel_w = (float)render_w * 0.25f;
    
    // On very wide screens, cap the panel widths
    if (center_panel_w > (float)render_h * 0.6f) {
        center_panel_w = (float)render_h * 0.6f;
        left_panel_w = (render_w - center_panel_w) * 0.5f;
        right_panel_w = left_panel_w;
    }

    float ui_padding = SDL_min((float)render_w, (float)render_h) * 0.02f;
    float ui_panel_w = left_panel_w;
    float window_scale = (float)render_h / 800.0f; // Kept for compatibility where needed

    // Base text scale on left panel width
    float ui_text_scale = (left_panel_w / 150.0f);
    if (ui_text_scale < 0.4f) ui_text_scale = 0.4f;

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

        // Handle held down button for soft drop
        if (app->down_btn_pressed && current_time >= app->global_freeze_until) {
            if (current_time - app->down_btn_hold_start > 200) { // Delay before repeat
                if (current_time - app->last_soft_drop_time > 50) { // Repeat rate
                    app->board1.update();
                    app->last_soft_drop_time = current_time;
                }
            }
        }

        if (app->board1.game_over || app->board2.game_over) app->state = GameState::GAME_OVER;

        // Status info will be rendered last, centered over the board area


        // Pre-calculate button positions so they can be used for relative layout
        float btn_size = SDL_min(left_panel_w, right_panel_w) * 0.45f;
        float btn_spacing = ui_padding;
        
        float left_col_x = ui_padding;
        float right_col_x = (float)render_w - btn_size - ui_padding;

        float left_btn_y = (float)render_h - btn_size - ui_padding;
        float rot_btn_y = left_btn_y - btn_size - btn_spacing;

        float right_btn_y = (float)render_h - btn_size - ui_padding;
        float drop_btn_y = right_btn_y - btn_size - btn_spacing;
        float pwr_btn_y = drop_btn_y - btn_size - btn_spacing;

        float next_area_h = pwr_btn_y - ui_padding - 20.0f * ui_text_scale;

        // Adjust draw_board to take panels into account
        // align_h: -1 = left, 0 = center, 1 = right
        // align_v: -1 = top, 0 = center, 1 = bottom
        auto draw_board_rel = [&](TetrisBoard& board, float panel_x, float panel_y, float panel_w, float panel_h, int align_h = 0, int align_v = -1) {
            float padding_x = ui_padding;
            float padding_y = ui_padding;
            float grid_area_w = panel_w - padding_x * 2.0f;
            float grid_area_h = panel_h - padding_y * 2.0f;
            float cell_w = grid_area_w / TetrisBoard::WIDTH;
            float cell_h = grid_area_h / TetrisBoard::HEIGHT;
            float cell_size = (cell_w < cell_h) ? cell_w : cell_h;
            
            float offset_x;
            if (align_h == -1) offset_x = panel_x + padding_x;
            else if (align_h == 1) offset_x = panel_x + panel_w - padding_x - cell_size * TetrisBoard::WIDTH;
            else offset_x = panel_x + padding_x + (grid_area_w - cell_size * TetrisBoard::WIDTH) / 2.0f;

            float y_offset;
            if (align_v == -1) y_offset = panel_y + padding_y;
            else if (align_v == 1) y_offset = panel_y + panel_h - padding_y - cell_size * TetrisBoard::HEIGHT;
            else y_offset = panel_y + padding_y + (grid_area_h - cell_size * TetrisBoard::HEIGHT) / 2.0f;

            for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
                for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
                    SDL_FRect rect = {offset_x + x * cell_size, y_offset + y * cell_size, cell_size - 1.0f, cell_size - 1.0f};
                    int color_idx = board.grid[y][x].color;
                    if (color_idx != 0) {
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
        };

        draw_board_rel(app->board1, left_panel_w, 0.0f, center_panel_w, (float)render_h, 0);

        // LEFT PANEL: MENU AND INFO - TOP LEFT ALIGNMENT
        float menu_btn_w = left_panel_w - ui_padding * 2.0f;
        float menu_btn_h = (float)render_h * 0.05f; // 5% of height
        float menu_btn_x = ui_padding;
        float menu_btn_y = ui_padding;

        SDL_SetRenderDrawColor(app->renderer, 200, 50, 50, 255);
        SDL_FRect play_exit_btn = {menu_btn_x, menu_btn_y, menu_btn_w, menu_btn_h};
        SDL_RenderFillRect(app->renderer, &play_exit_btn);
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_SetRenderScale(app->renderer, ui_text_scale, ui_text_scale);
        
        // Menu text in button
        SDL_RenderDebugTextFormat(app->renderer, (menu_btn_x + ui_padding * 0.5f) / ui_text_scale, (menu_btn_y + (menu_btn_h - 12.0f * ui_text_scale) * 0.5f) / ui_text_scale, "%s", "MENU");
        
        float stacked_x = ui_padding / ui_text_scale;
        float stacked_y = (menu_btn_y + menu_btn_h + ui_padding) / ui_text_scale;

        // Client text
#if CLIENT_ID == 1
        SDL_RenderDebugTextFormat(app->renderer, stacked_x, stacked_y, "%s", "Client 1");
#elif CLIENT_ID == 2
        SDL_RenderDebugTextFormat(app->renderer, stacked_x, stacked_y, "%s", "Client 2");
#else
        SDL_RenderDebugTextFormat(app->renderer, stacked_x, stacked_y, "%s", "Tetris Battle");
#endif

        // Level
        SDL_RenderDebugTextFormat(app->renderer, stacked_x, stacked_y + 12.0f, "Level: %d", level);
        
        // Time
        int sec = (int)(elapsed_ms / 1000) % 60;
        int min = (int)(elapsed_ms / 60000);
        SDL_RenderDebugTextFormat(app->renderer, stacked_x, stacked_y + 24.0f, "Time: %02d:%02d", min, sec);
        
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        // Opponent grid under time - TOP LEFT ALIGNMENT
        float opp_grid_y = (stacked_y + 40.0f) * ui_text_scale;
        float opp_grid_h = (float)render_h - opp_grid_y - ui_padding;
        draw_board_rel(app->board2, 0.0f, opp_grid_y, left_panel_w, opp_grid_h, -1);

        // RIGHT PANEL: NEXT PIECES - TOP RIGHT ALIGNMENT
        float next_cell_size = (right_panel_w / 6.0f);
        float next_offset_x = (float)render_w - ui_padding - next_cell_size * 4.0f;
        float next_offset_y = ui_padding + 15.0f * ui_text_scale;
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        float next_text_scale = ui_text_scale;
        SDL_SetRenderScale(app->renderer, next_text_scale, next_text_scale);
        SDL_RenderDebugTextFormat(app->renderer, ((float)render_w - ui_padding) / next_text_scale - 40.0f, ui_padding / next_text_scale, "Next:");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        if (app->board1.shared_queue && app->net_client) {
            int next_index = app->net_client->get_global_next_index();
            int num_next = 3;
            // Adjust next piece size if it doesn't fit
            float required_h = num_next * (next_cell_size * 4.5f);
            if (required_h > next_area_h) {
                next_cell_size *= (next_area_h / required_h);
            }

            for (int i = 0; i < num_next; i++) {
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

        SDL_FRect left_btn = {left_col_x, left_btn_y, btn_size, btn_size};
        SDL_FRect right_btn = {right_col_x, right_btn_y, btn_size, btn_size};
        SDL_FRect rot_btn = {left_col_x, rot_btn_y, btn_size, btn_size};
        SDL_FRect drop_btn = {right_col_x, drop_btn_y, btn_size, btn_size};
        SDL_FRect pwr_btn = {right_col_x, pwr_btn_y, btn_size, btn_size};
        
        SDL_RenderFillRect(app->renderer, &left_btn);
        SDL_RenderFillRect(app->renderer, &right_btn);
        SDL_RenderFillRect(app->renderer, &rot_btn);
        SDL_RenderFillRect(app->renderer, &drop_btn);
        SDL_SetRenderDrawColor(app->renderer, 255, 0, 255, 255);
        SDL_RenderFillRect(app->renderer, &pwr_btn);

        float label_scale = 2.0f * ui_scale * window_scale;
        SDL_SetRenderScale(app->renderer, label_scale, label_scale);
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        
        auto draw_label = [&](float bx, float by, const char* text) {
            float tx = (bx + btn_size * 0.35f) / label_scale;
            float ty = (by + btn_size * 0.35f) / label_scale;
            SDL_RenderDebugTextFormat(app->renderer, tx, ty, "%s", text);
        };

        draw_label(left_col_x, left_btn_y, "<");
        draw_label(right_col_x, right_btn_y, ">");
        draw_label(left_col_x, rot_btn_y, "R");
        draw_label(right_col_x, drop_btn_y, "v");
        char pwr_txt[2];
        SDL_snprintf(pwr_txt, sizeof(pwr_txt), "%d", app->board1.stored_powerups);
        draw_label(right_col_x, pwr_btn_y, pwr_txt);
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        // Render status messages last, centered over the board area
        if (app->net_client) {
            float overlay_scale = ui_text_scale * 1.5f;
            SDL_SetRenderScale(app->renderer, overlay_scale, overlay_scale);
            float board_center_x = (left_panel_w + center_panel_w * 0.5f) / overlay_scale;
            float board_center_y = ((float)render_h * 0.4f) / overlay_scale;

            if (!app->net_client->is_connected()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 100, 100, 255);
                SDL_RenderDebugTextFormat(app->renderer, board_center_x - 80.0f, board_center_y, "%s", "Waiting for server...");
            } else if (!app->net_client->is_opponent_ready()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255);
                int cd = app->net_client->get_countdown();
                if (cd > 0) SDL_RenderDebugTextFormat(app->renderer, board_center_x - 70.0f, board_center_y, "Match starts in: %d", cd);
                else SDL_RenderDebugTextFormat(app->renderer, board_center_x - 80.0f, board_center_y, "%s", "Waiting for opponent...");
            }
            
            if (app->net_client->has_weak_connection()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 165, 0, 255);
                SDL_RenderDebugTextFormat(app->renderer, board_center_x - 70.0f, board_center_y + 25.0f, "%s", "Weak Connection!");
            }
            if (SDL_GetTicks() < app->global_freeze_until) {
                SDL_SetRenderDrawColor(app->renderer, 0, 255, 255, 255);
                SDL_RenderDebugTextFormat(app->renderer, board_center_x - 90.0f, board_center_y + 50.0f, "%s", "POWER ACTIVE - FROZEN!");
            }
            SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
        }
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
