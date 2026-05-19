#include "game_controller.hpp"

#include "power_system.hpp"
#include "render_system.hpp"

#include <cstdlib>

#ifndef CLIENT_ID
#define CLIENT_ID 1
#endif

namespace GameConfig {
constexpr uint64_t DOUBLE_TAP_THRESHOLD_MS = 250;
constexpr uint64_t LEVEL_UP_INTERVAL_MS = 30000;
constexpr uint64_t NETWORK_SYNC_INTERVAL_MS = 50;
constexpr uint64_t SOFT_DROP_INTERVAL_MS = 50;
constexpr uint64_t HOLD_INITIAL_DELAY_MS = 200;
} // namespace GameConfig

namespace {
void HandleMenuInput(AppContext& app, SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_RETURN) {
        GameController::ResetGame(app);
        return;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN || event->type == SDL_EVENT_FINGER_DOWN) {
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

        if (x >= 50.0f * ui_scale && x <= 250.0f * ui_scale && y >= 150.0f * ui_scale && y <= 210.0f * ui_scale) {
            app.app_quit = SDL_APP_SUCCESS;
        }
    }
}

void HandlePlayingInput(AppContext& app, SDL_Event* event, uint64_t current_time) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            app.state = GameState::MENU;
            return;
        }
        if (!app.net_client || !app.opponent_ready)
            return;

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
            if (!app.waiting_for_power_response && app.board1.crystals > 0) {
                app.net_client->send_command(PacketType::C_REQUEST_POWER, app.board1.crystals);
                app.waiting_for_power_response = true;
                SDL_Log("[APP] Requested to send power with %d crystals", app.board1.crystals);
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

        int render_w, render_h;
        SDL_GetRenderOutputSize(app.renderer.get(), &render_w, &render_h);
        UILayout layout = RenderSystem::CalculateLayout(render_w, render_h);

        auto in_rect = [&](const SDL_FRect& r) { return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; };

        if (in_rect(layout.menu_btn)) {
            app.state = GameState::MENU;
            return;
        }

        if (!app.net_client || !app.opponent_ready)
            return;

        if (in_rect(layout.left_btn)) {
            app.board1.move(-1);
        } else if (in_rect(layout.right_btn)) {
            app.board1.move(1);
        } else if (in_rect(layout.rot_btn)) {
            app.board1.rotate();
        } else if (in_rect(layout.drop_btn)) {
            if (current_time - app.last_down_press_time < GameConfig::DOUBLE_TAP_THRESHOLD_MS) { // Double tap
                app.board1.hard_drop();
                app.down_btn_pressed = false;
            } else {
                app.board1.update(); // Initial soft drop
                app.down_btn_pressed = true;
                app.down_btn_hold_start = current_time;
                app.last_soft_drop_time = current_time;
            }
            app.last_down_press_time = current_time;
        } else if (in_rect(layout.pwr_btn)) {
            if (!app.waiting_for_power_response && app.board1.crystals > 0) {
                app.net_client->send_command(PacketType::C_REQUEST_POWER, app.board1.crystals);
                app.waiting_for_power_response = true;
                SDL_Log("[APP] Requested to send power with %d crystals", app.board1.crystals);
            }
        }
    }
}

void HandleGameOverInput(AppContext& app, SDL_Event* event) {
    if (event->type == SDL_EVENT_KEY_DOWN) {
        if (event->key.key == SDLK_ESCAPE) {
            app.state = GameState::MENU;
        } else if (event->key.key == SDLK_RETURN) {
            GameController::ResetGame(app);
        }
    }
}

void ProcessNetworkEvents(AppContext& app) {
    if (!app.net_client)
        return;
    app.net_client->update();

    NetworkEvent event;
    while (app.net_client->poll_event(event)) {
        switch (event.type) {
            case PacketType::S_MATCH_START:
                if (!app.shared_queue->seed_initialized) {
                    app.shared_queue->init_seed(event.data);
                }
                app.opponent_ready = true;
                break;
            case PacketType::S_COUNTDOWN:
                app.countdown_val = event.data;
                break;
            case PacketType::S_GARBAGE_SIGNAL:
                app.board1.add_garbage_rows(event.data);
                break;
            case PacketType::S_GAME_OVER:
                app.board1.game_over = true;
                app.board2.game_over = true;
                break;
            case PacketType::S_GRANT_PIECE:
                SDL_Log("[APP] Spawn request fulfilled with index %d", event.data);
                app.board1.spawn_piece(event.data);
                app.board1.waiting_for_spawn = false;
                app.board1.spawn_request_pending = false;
                app.board1_spawn_requested = false;
                break;
            case PacketType::S_STATE_BROADCAST:
                if (!app.board2.board_active && event.has_state) {
                    app.board2.current_piece.type = event.state.piece_type;
                    app.board2.current_piece.rotation = event.state.piece_rot;
                    app.board2.current_piece.x = event.state.piece_x;
                    app.board2.current_piece.y = event.state.piece_y;
                    app.board2.set_crystal_mask(event.state.piece_crystal_mask);
                    app.board2.update_shape_only();
                    app.board2.unpack_grid(event.state.grid);
                }
                break;
            case PacketType::S_NEXT_PIECE_UPDATE:
                app.global_next_index = event.data;
                break;
            case PacketType::S_WEAK_CONNECTION:
                app.weak_conn = true;
                SDL_LogWarn(SDL_LOG_CATEGORY_CUSTOM, "Weak Connection detected!");
                break;
            case PacketType::S_POWER_ACTIVATED: {
                uint16_t power_id = event.data;
                const PowerDefinition& power = get_power_definition(power_id);

                if (event.client_id == app.net_client->get_id()) {
                    // We successfully activated a power
                    app.waiting_for_power_response = false;
                    app.board1.crystals = 0; // Reset crystals
                    SDL_Log("[APP] Power %d activated successfully by us!", power_id);
                    // Execute power locally (e.g., if is_self_inflicted or opponent-directed logic)
                } else {
                    // Opponent activated a power
                    app.board2.crystals = 0; // Reset opponent's crystals
                    SDL_Log("[APP] Opponent activated power %d!", power_id);
                    // Execute incoming power effect
                    if (!power.is_self_inflicted) {
                        app.board1.discard_current_piece();
                    }
                }

                if (power.is_frozen) {
                    // TODO: trigger cinematic freeze state
                }
                break;
            }
            case PacketType::S_POWER_DEACTIVATED: {
                uint16_t power_id = event.data;
                SDL_Log("[APP] Power %d deactivated", power_id);
                // Revert power effect
                break;
            }
            default:
                break;
        }
    }
}

void SyncNetworkState(AppContext& app, uint64_t current_time) {
    if (!app.net_client || !app.board1.board_active || !app.opponent_ready)
        return;

    if (app.net_client->is_connected() && !app.character_sent) {
        app.net_client->send_command(PacketType::C_SET_CHARACTER, static_cast<uint16_t>(app.client_character));
        app.character_sent = true;
    }

    if (app.board1.waiting_for_spawn && !app.board1.spawn_request_pending) {
        app.board1.request_spawn();
    }

    if (app.board1.spawn_request_pending && !app.board1_spawn_requested) {
        app.net_client->send_command(PacketType::C_LOCK_PIECE);
        app.board1_spawn_requested = true;
    }

    if (app.board1.game_over && !app.game_over_sent) {
        app.net_client->send_command(PacketType::C_GAME_OVER);
        app.game_over_sent = true;
    }

    if (app.board1.pending_outgoing_garbage > 0) {
        app.net_client->send_command(PacketType::C_SEND_GARBAGE, app.board1.pending_outgoing_garbage);
        app.board1.pending_outgoing_garbage = 0;
    }

    if (current_time - app.last_sync > GameConfig::NETWORK_SYNC_INTERVAL_MS) {
        uint8_t packed[100];
        app.board1.pack_grid(packed);
        app.net_client->send_state(app.board1.current_piece.type, app.board1.current_piece.rotation, app.board1.current_piece.x,
                                   app.board1.current_piece.y, app.board1.get_crystal_mask(), packed);
        app.last_sync = current_time;
        app.weak_conn = false;
    }
}

void UpdateGameLogic(AppContext& app, uint64_t current_time) {
    app.board1.tick(current_time);
    app.board2.tick(current_time);

    uint64_t elapsed_ms = 0;
    if (app.net_client && app.opponent_ready) {
        if (!app.match_started) {
            app.match_start_time = current_time;
            app.match_started = true;
        }
        elapsed_ms = current_time - app.match_start_time;
    } else {
        app.match_started = false;
    }

    int level = (int)(elapsed_ms / GameConfig::LEVEL_UP_INTERVAL_MS) + 1;
    uint64_t drop_interval = 500;
    if (level > 1) {
        if (level >= 20)
            drop_interval = 50;
        else
            drop_interval = 500 - (level - 1) * 23;
    }

    static Uint64 last_time = 0;
    if (current_time - last_time > drop_interval && app.net_client && app.opponent_ready) {
        app.board1.update();
        app.board2.update();
        last_time = current_time;
    }

    if (app.down_btn_pressed) {
        if (current_time - app.down_btn_hold_start > GameConfig::HOLD_INITIAL_DELAY_MS) {
            if (current_time - app.last_soft_drop_time > GameConfig::SOFT_DROP_INTERVAL_MS) {
                app.board1.update();
                app.last_soft_drop_time = current_time;
            }
        }
    }

    if (app.board1.game_over || app.board2.game_over)
        app.state = GameState::GAME_OVER;
}
} // namespace

void GameController::ResetGame(AppContext& app, std::shared_ptr<NetworkClient> custom_client) {
    app.state = GameState::PLAYING;

    SDL_Log("[APP] Starting match, reset network...");

    if (custom_client) {
        app.net_client = custom_client;
    } else {
        app.net_client = std::make_shared<NetworkClient>();
        const char* port_str = std::getenv("TETRIS_PORT");
        uint16_t port = port_str ? std::atoi(port_str) : 12345;

#if defined(ANDROID)
        SDL_Log("[APP] Connecting to emulator host (10.0.2.2:%d)...", port);
        app.net_client->connect("10.0.2.2", port);
#else
        SDL_Log("[APP] Connecting to localhost (%d)...", port);
        app.net_client->connect("127.0.0.1", port);
#endif
    }

    app.shared_queue = std::make_shared<SharedPieceQueue>();
    app.board1 = TetrisBoard();
    app.board2 = TetrisBoard();

    app.board1.set_shared_queue(app.shared_queue, true);
    app.board2.set_shared_queue(app.shared_queue, false);

    app.match_started = false;
    app.opponent_ready = false;
    app.game_over_sent = false;
    app.board1_spawn_requested = false;
    app.character_sent = false;
}

void GameController::HandleInput(AppContext& app, SDL_Event* event, uint64_t current_time) {
    if (event->type == SDL_EVENT_QUIT) {
        app.app_quit = SDL_APP_SUCCESS;
        return;
    }

    switch (app.state) {
        case GameState::MENU:
            HandleMenuInput(app, event);
            break;
        case GameState::PLAYING:
            HandlePlayingInput(app, event, current_time);
            break;
        case GameState::GAME_OVER:
            HandleGameOverInput(app, event);
            break;
    }
}

void GameController::Update(AppContext& app, uint64_t current_time) {
    if (app.skip_menu && app.state == GameState::MENU) {
        app.skip_menu = false;
        ResetGame(app);
    }

    if (app.state == GameState::PLAYING) {
        ProcessNetworkEvents(app);
        SyncNetworkState(app, current_time);
        UpdateGameLogic(app, current_time);
    }
}
