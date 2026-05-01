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
    
    if (!SDL_CreateWindowAndRenderer(window_title, 0, 0, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
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
            if (event->key.key == SDLK_LEFT) {
                app->board1.move(-1);
            } else if (event->key.key == SDLK_RIGHT) {
                app->board1.move(1);
            } else if (event->key.key == SDLK_UP) {
                app->board1.rotate();
            } else if (event->key.key == SDLK_DOWN) {
                app->board1.update(); // Soft drop
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_FINGER_DOWN) {
        // Ignore simulated mouse events from touch to prevent double-firing
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.which == SDL_TOUCH_MOUSEID) {
            return SDL_APP_CONTINUE;
        }

        float x, y;
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            x = event->button.x;
            y = event->button.y;
        } else {
            int w, h;
            SDL_GetWindowSize(app->window, &w, &h);
            x = event->tfinger.x * w;
            y = event->tfinger.y * h;
        }

        if (app->state == GameState::MENU) {
            // Exit button hitbox in menu
            if (x >= 50.0f && x <= 250.0f && y >= 150.0f && y <= 210.0f) {
                app->app_quit = SDL_APP_SUCCESS;
            }
        } else if (app->state == GameState::PLAYING) {
            // Exit to menu button
            int window_width, window_height;
            SDL_GetWindowSize(app->window, &window_width, &window_height);
            float right_area = (float)window_width * 0.70f + 10.0f;
            if (x >= right_area && x <= right_area + 100.0f && y >= 20.0f && y <= 70.0f) {
                app->state = GameState::MENU;
                return SDL_APP_CONTINUE;
            }

            // Give them generous hitboxes perfectly aligned with the drawn rectangles
            if (y >= 500.0f && y <= 750.0f) {
                if (x >= 0.0f && x <= 100.0f) {
                    app->board1.move(-1);
                } else if (x > 100.0f && x <= 200.0f) {
                    app->board1.move(1);
                } else if (x > 200.0f && x <= 300.0f) {
                    app->board1.rotate();
                } else if (x > 300.0f && x <= 450.0f) {
                    app->board1.update(); // Soft drop
                }
            }
        }
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = (AppContext*)appstate;

    // 1. Set background color to Dark Gray and Clear the screen
    SDL_SetRenderDrawColor(app->renderer, 40, 40, 45, 255); 
    SDL_RenderClear(app->renderer);

    // 2. Set text color to White (Red 255, Green 255, Blue 255, Alpha 255)
    SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
    
    if (app->skip_menu && app->state == GameState::MENU) {
        app->state = GameState::PLAYING;
        app->skip_menu = false; // So returning to menu doesn't skip it again
        
        SDL_Log("[APP] Auto-starting match, reset network...");
        app->net_client.reset();
        app->net_client = std::make_shared<NetworkClient>();
#if defined(ANDROID)
        app->net_client->connect("10.0.2.2", 12345);
#else
        app->net_client->connect("127.0.0.1", 12345);
#endif
        app->shared_queue = std::make_shared<SharedPieceQueue>(app->net_client);
        
        // board1 is local player, active
        app->board1.set_shared_queue(app->shared_queue, true);
        
        // board2 is remote dummy player, inactive for now until network syncs board states
        app->board2.set_shared_queue(app->shared_queue, false);
    }

    if (app->state == GameState::MENU) {
        SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
#if CLIENT_ID == 1
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "CLIENT 1 - TETRIS BATTLE");
#elif CLIENT_ID == 2
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "CLIENT 2 - TETRIS BATTLE");
#else
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "TETRIS BATTLE");
#endif
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 120.0f, "%s", "Press ENTER to Start");

        // Draw exit button
        SDL_SetRenderDrawColor(app->renderer, 200, 50, 50, 255);
        SDL_FRect exit_btn = {25.0f, 150.0f, 100.0f, 30.0f}; // This is scaled by 2.0, so effective is 50..250, 300..360? Wait.
        // It's better to render rect at 1.0 scale to match hitboxes
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
        exit_btn = {50.0f, 150.0f, 200.0f, 60.0f};
        SDL_RenderFillRect(app->renderer, &exit_btn);
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
        SDL_RenderDebugTextFormat(app->renderer, 45.0f, 85.0f, "%s", "EXIT"); // 45*2=90, 85*2=170
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

    } else if (app->state == GameState::GAME_OVER) {
        SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 100.0f, "%s", "GAME OVER!");
        SDL_RenderDebugTextFormat(app->renderer, 50.0f, 120.0f, "%s", "Press ENTER to Restart");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    } else if (app->state == GameState::PLAYING) {
        if (app->net_client) {
            app->net_client->update();
        }
        
        app->board1.process_network();
        
        // Update logic
        static Uint64 last_time = 0;
        Uint64 current_time = SDL_GetTicks();
        if (current_time - last_time > 500) {
            app->board1.update();
            app->board2.update(); // Doesn't move if inactive, just drops
            last_time = current_time;
        }

        if (app->board1.game_over || app->board2.game_over) {
            app->state = GameState::GAME_OVER;
        }

        // Draw the text at X: 10, Y: 10
        SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
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
                SDL_RenderDebugTextFormat(app->renderer, 5.0f, 25.0f, "%s", "Waiting for opponent...");
            }
            if (app->net_client->has_weak_connection()) {
                SDL_SetRenderDrawColor(app->renderer, 255, 165, 0, 255);
                SDL_RenderDebugTextFormat(app->renderer, 5.0f, 45.0f, "%s", "Weak Connection!");
            }
        }
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        int window_width, window_height;
        SDL_GetWindowSize(app->window, &window_width, &window_height);

        // Exit button in the 30% space on the right
        float right_area = (float)window_width * 0.70f + 10.0f;
        SDL_SetRenderDrawColor(app->renderer, 200, 50, 50, 255);
        SDL_FRect play_exit_btn = {right_area, 20.0f, 100.0f, 50.0f};
        SDL_RenderFillRect(app->renderer, &play_exit_btn);
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_SetRenderScale(app->renderer, 1.5f, 1.5f);
        SDL_RenderDebugTextFormat(app->renderer, (right_area + 20.0f) / 1.5f, 35.0f / 1.5f, "%s", "MENU");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);

        // Helper to draw a board
        auto draw_board = [&](TetrisBoard& board, float offset_screen_x, float scale_factor) {
        float padding = 20.0f;
        float grid_area_w = ((float)window_width * 0.70f - padding * 2.0f) * scale_factor;
        float grid_area_h = ((float)window_height - padding * 2.0f) * scale_factor;

        float cell_w = grid_area_w / TetrisBoard::WIDTH;
        float cell_h = grid_area_h / TetrisBoard::HEIGHT;
        float cell_size = (cell_w < cell_h) ? cell_w : cell_h;

        float offset_x = offset_screen_x + padding + (grid_area_w - cell_size * TetrisBoard::WIDTH) / 2.0f;
        float y_offset = padding + (grid_area_h - cell_size * TetrisBoard::HEIGHT) / 2.0f;

        // Draw grid
        for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
            for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
                SDL_FRect rect = {offset_x + x * cell_size, y_offset + y * cell_size, cell_size - 1.0f, cell_size - 1.0f};
                if (board.grid[y][x].color != 0) {
                    SDL_SetRenderDrawColor(app->renderer, 0, 255, 0, 255); // Green for locked blocks
                    SDL_RenderFillRect(app->renderer, &rect);
                    if (board.grid[y][x].has_crystal) {
                        SDL_SetRenderDrawColor(app->renderer, 255, 255, 0, 255); // Yellow for crystal
                        float c_margin = cell_size * 0.2f;
                        SDL_FRect crect = {offset_x + x * cell_size + c_margin, y_offset + y * cell_size + c_margin, cell_size - c_margin * 2.0f, cell_size - c_margin * 2.0f};
                        SDL_RenderFillRect(app->renderer, &crect);
                    }
                } else {
                    SDL_SetRenderDrawColor(app->renderer, 80, 80, 80, 255); // Dark gray for empty
                    SDL_RenderFillRect(app->renderer, &rect);
                }
            }
        }
        // Draw current piece
        SDL_SetRenderDrawColor(app->renderer, 255, 0, 0, 255); // Red for current piece
        for(int r=0; r<4; r++) {
            for(int c=0; c<4; c++) {
                if(board.current_piece.shape[r][c]) {
                    SDL_FRect rect = {offset_x + (board.current_piece.x + c) * cell_size, y_offset + (board.current_piece.y + r) * cell_size, cell_size - 1.0f, cell_size - 1.0f};
                    SDL_RenderFillRect(app->renderer, &rect);
                }
            }
        }

        // Draw next piece indicator in the upper right
        float next_cell_size = 20.0f;
        float next_offset_x = (float)window_width * 0.75f;
        float next_offset_y = 100.0f;
        
        SDL_SetRenderDrawColor(app->renderer, 255, 255, 255, 255);
        SDL_RenderDebugTextFormat(app->renderer, next_offset_x, next_offset_y - 20.0f, "Next:");
        
        if (board.shared_queue) {
            int next_index = board.shared_queue->peek_next();
            int next_type = board.shared_queue->get_piece_at(next_index);
            
            SDL_SetRenderDrawColor(app->renderer, 255, 0, 0, 255);
            for(int r=0; r<4; r++) {
                for(int c=0; c<4; c++) {
                    if(SHAPES[next_type][r][c]) {
                        SDL_FRect rect = {next_offset_x + c * next_cell_size, next_offset_y + r * next_cell_size, next_cell_size - 1.0f, next_cell_size - 1.0f};
                        SDL_RenderFillRect(app->renderer, &rect);
                    }
                }
            }
        }
    };

        // Draw local board on the left
        draw_board(app->board1, 0.0f, 1.0f);
        
        // Draw remote board (dummy) smaller on the right, below the Next piece
        draw_board(app->board2, (float)window_width * 0.75f, 0.4f);
        
        // Draw simple touch buttons for Android testing
        SDL_SetRenderDrawColor(app->renderer, 100, 100, 100, 255);
        SDL_FRect left_btn = {10.0f, 600.0f, 80.0f, 80.0f};
        SDL_FRect right_btn = {110.0f, 600.0f, 80.0f, 80.0f};
        SDL_FRect rot_btn = {210.0f, 600.0f, 80.0f, 80.0f};
        SDL_FRect drop_btn = {310.0f, 600.0f, 80.0f, 80.0f};
        
        SDL_RenderFillRect(app->renderer, &left_btn);
        SDL_RenderFillRect(app->renderer, &right_btn);
        SDL_RenderFillRect(app->renderer, &rot_btn);
        SDL_RenderFillRect(app->renderer, &drop_btn);

        SDL_SetRenderScale(app->renderer, 2.0f, 2.0f);
        SDL_RenderDebugTextFormat(app->renderer, 15.0f, 315.0f, "%s", "<");
        SDL_RenderDebugTextFormat(app->renderer, 65.0f, 315.0f, "%s", ">");
        SDL_RenderDebugTextFormat(app->renderer, 115.0f, 315.0f, "%s", "R");
        SDL_RenderDebugTextFormat(app->renderer, 165.0f, 315.0f, "%s", "v");
        SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    }

    // 4. Swap the buffers to display everything
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
