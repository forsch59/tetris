#include "game_controller.hpp"
#include "render_system.hpp"

#ifndef CLIENT_ID
#define CLIENT_ID 1
#endif

void GameController::ResetGame(AppContext& app) {
    app.state = GameState::PLAYING;
    
    SDL_Log("[APP] Starting match, reset network...");
    app.net_client.reset();
    app.net_client = std::make_shared<NetworkClient>();
#if defined(ANDROID)
    SDL_Log("[APP] Connecting to emulator host (10.0.2.2)...");
    app.net_client->connect("10.0.2.2", 12345);
#else
    SDL_Log("[APP] Connecting to localhost...");
    app.net_client->connect("127.0.0.1", 12345);
#endif
    
    app.shared_queue = std::make_shared<SharedPieceQueue>();
    app.board1 = TetrisBoard();
    app.board2 = TetrisBoard();
    
    app.board1.set_shared_queue(app.shared_queue, true);
    app.board2.set_shared_queue(app.shared_queue, false);
    
    app.match_started = false;
    app.game_over_sent = false;
    app.board1_spawn_requested = false;
}

void GameController::HandleInput(AppContext& app, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        app.app_quit = SDL_APP_SUCCESS;
    } else if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            if (app.state == GameState::PLAYING || app.state == GameState::GAME_OVER) {
                app.state = GameState::MENU;
            }
        } else if (app.state == GameState::MENU || app.state == GameState::GAME_OVER) {
            if (event->key.key == SDLK_RETURN) {
                ResetGame(app);
            }
        } else if (app.state == GameState::PLAYING) {
            if (!app.net_client || !app.net_client->is_opponent_ready()) return;

            if (event->key.key == SDLK_LEFT) {
                app.board1.move(-1);
            } else if (event->key.key == SDLK_RIGHT) {
                app.board1.move(1);
            } else if (event->key.key == SDLK_UP) {
                app.board1.rotate();
            } else if (event->key.key == SDLK_DOWN) {
                app.board1.update(); // Soft drop
            } else if (event->key.key == SDLK_SPACE) {
                app.board1.hard_drop();
            } else if (event->key.key == SDLK_LCTRL || event->key.key == SDLK_RCTRL) {
                // Placeholder for power activation
            }
        }
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_UP || event->type == SDL_EVENT_FINGER_UP) {
        app.down_btn_pressed = false;
    } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_FINGER_DOWN) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.which == SDL_TOUCH_MOUSEID) {
            return;
        }

        float x, y;
        float ui_scale = SDL_GetWindowDisplayScale(app.window.get());
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            x = event->button.x * ui_scale;
            y = event->button.y * ui_scale;
        } else {
            int w, h;
            SDL_GetRenderOutputSize(app.renderer.get(), &w, &h);
            x = event->tfinger.x * w;
            y = event->tfinger.y * h;
        }

        if (app.state == GameState::MENU) {
            if (x >= 50.0f * ui_scale && x <= 250.0f * ui_scale && y >= 150.0f * ui_scale && y <= 210.0f * ui_scale) {
                app.app_quit = SDL_APP_SUCCESS;
            }
        } else if (app.state == GameState::PLAYING) {
            int render_w, render_h;
            SDL_GetRenderOutputSize(app.renderer.get(), &render_w, &render_h);
            UILayout layout = RenderSystem::CalculateLayout(render_w, render_h);

            auto in_rect = [&](const SDL_FRect& r) {
                return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
            };

            if (in_rect(layout.menu_btn)) {
                app.state = GameState::MENU;
                return;
            }

            if (!app.net_client || !app.net_client->is_opponent_ready()) return;

            if (in_rect(layout.left_btn)) {
                app.board1.move(-1);
            } else if (in_rect(layout.right_btn)) {
                app.board1.move(1);
            } else if (in_rect(layout.rot_btn)) {
                app.board1.rotate();
            } else if (in_rect(layout.drop_btn)) {
                uint64_t now = SDL_GetTicks();
                if (now - app.last_down_press_time < 250) { // Double tap
                    app.board1.hard_drop();
                    app.down_btn_pressed = false;
                } else {
                    app.board1.update(); // Initial soft drop
                    app.down_btn_pressed = true;
                    app.down_btn_hold_start = now;
                    app.last_soft_drop_time = now;
                }
                app.last_down_press_time = now;
            } else if (in_rect(layout.pwr_btn)) {
                // Placeholder for power activation
            }
        }
    }
}

void GameController::Update(AppContext& app) {
    if (app.skip_menu && app.state == GameState::MENU) {
        app.skip_menu = false;
        ResetGame(app);
    }

    if (app.state == GameState::PLAYING) {
        uint64_t current_time = SDL_GetTicks();
        if (app.net_client) {
            app.net_client->update();

            if (app.net_client->is_seed_ready() && !app.shared_queue->seed_initialized) {
                app.shared_queue->init_seed(app.net_client->get_seed());
            }

            int pending_garbage = app.net_client->get_pending_garbage();
            if (pending_garbage > 0) {
                app.board1.add_garbage_rows(pending_garbage);
            }

            if (app.net_client->is_game_over()) {
                app.board1.game_over = true;
                app.board2.game_over = true;
            }

            if (!app.board2.board_active) {
                const auto& opp = app.net_client->get_opponent_state();
                app.board2.current_piece.type = opp.piece_type;
                app.board2.current_piece.rotation = opp.piece_rot;
                app.board2.current_piece.x = opp.piece_x;
                app.board2.current_piece.y = opp.piece_y;
                app.board2.set_crystal_mask(opp.piece_crystal_mask);
                app.board2.update_shape_only();
                app.board2.unpack_grid(opp.grid);
            }

            if (app.board1.board_active && app.net_client->is_opponent_ready()) {
                if (app.board1.waiting_for_spawn && !app.board1.spawn_request_pending) {
                    app.board1.request_spawn();
                }

                if (app.board1.spawn_request_pending && !app.board1_spawn_requested) {
                    app.net_client->send_command(PacketType::C_LOCK_PIECE);
                    app.board1_spawn_requested = true;
                }

                if (app.board1.spawn_request_pending) {
                    int granted_index = app.net_client->get_claimed_index();
                    if (granted_index != -1) {
                        SDL_Log("[APP] Spawn request fulfilled with index %d", granted_index);
                        app.board1.spawn_piece(granted_index);
                        app.board1.waiting_for_spawn = false;
                        app.board1.spawn_request_pending = false;
                        app.board1_spawn_requested = false;
                    }
                }

                if (app.board1.game_over && !app.game_over_sent) {
                    app.net_client->send_command(PacketType::C_GAME_OVER);
                    app.game_over_sent = true;
                }

                if (app.board1.pending_outgoing_garbage > 0) {
                    app.net_client->send_command(PacketType::C_SEND_GARBAGE, app.board1.pending_outgoing_garbage);
                    app.board1.pending_outgoing_garbage = 0;
                }

                if (current_time - app.last_sync > 50) {
                    uint8_t packed[100];
                    app.board1.pack_grid(packed);
                    app.net_client->send_state(app.board1.current_piece.type, app.board1.current_piece.rotation,
                                                  app.board1.current_piece.x, app.board1.current_piece.y,
                                                  app.board1.get_crystal_mask(), packed);
                    app.last_sync = current_time;
                }
            }
        }

        app.board1.tick(current_time);
        app.board2.tick(current_time);
        
        uint64_t elapsed_ms = 0;
        if (app.net_client && app.net_client->is_opponent_ready()) {
            if (!app.match_started) {
                app.match_start_time = current_time;
                app.match_started = true;
            }
            elapsed_ms = current_time - app.match_start_time;
        } else {
            app.match_started = false;
        }

        int level = (int)(elapsed_ms / 30000) + 1;
        uint64_t drop_interval = 500;
        if (level > 1) {
            if (level >= 20) drop_interval = 50;
            else drop_interval = 500 - (level - 1) * 23;
        }

        static Uint64 last_time = 0;
        if (current_time - last_time > drop_interval && 
            app.net_client && app.net_client->is_opponent_ready()) {
            app.board1.update();
            app.board2.update();
            last_time = current_time;
        }

        if (app.down_btn_pressed) {
            if (current_time - app.down_btn_hold_start > 200) {
                if (current_time - app.last_soft_drop_time > 50) {
                    app.board1.update();
                    app.last_soft_drop_time = current_time;
                }
            }
        }

        if (app.board1.game_over || app.board2.game_over) app.state = GameState::GAME_OVER;
    }
}
