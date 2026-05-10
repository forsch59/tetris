#include "render_system.hpp"
#include <cstdarg>
#include <cstdio>

UILayout RenderSystem::CalculateLayout(int render_w, int render_h) {
    UILayout layout;
    layout.padding = SDL_min((float)render_w, (float)render_h) * 0.015f; 
    layout.section_padding = layout.padding * 0.5f; 
    
    float s2_w = (float)render_w * 0.70f;
    float s1_w = (float)render_w * 0.15f;
    float s3_w = (float)render_w * 0.15f;
    
    layout.section1 = { .x = layout.section_padding, .y = layout.section_padding, .w = s1_w - layout.section_padding * 2.0f, .h = (float)render_h - layout.section_padding * 2.0f };
    layout.section2 = { .x = s1_w, .y = layout.section_padding, .w = s2_w, .h = (float)render_h - layout.section_padding * 2.0f };
    layout.section3 = { .x = s1_w + s2_w + layout.section_padding, .y = layout.section_padding, .w = s3_w - layout.section_padding * 2.0f, .h = (float)render_h - layout.section_padding * 2.0f };
    
    layout.text_scale = (layout.section1.w / 100.0f);
    if (layout.text_scale < 0.4f) layout.text_scale = 0.4f;

    float item_w = layout.section1.w;
    
    layout.menu_btn = { .x = layout.section1.x, .y = layout.section1.y, .w = item_w, .h = (float)render_h * 0.06f };
    
    float btn_h = (float)render_h * 0.08f; 
    layout.left_btn = { .x = layout.section1.x, .y = layout.section1.y + layout.section1.h - btn_h, .w = item_w, .h = btn_h };
    layout.drop_btn = { .x = layout.section1.x, .y = layout.left_btn.y - layout.padding - btn_h, .w = item_w, .h = btn_h };
    layout.pwr_btn = { .x = layout.section1.x, .y = layout.drop_btn.y - layout.padding - btn_h, .w = item_w, .h = btn_h };
    
    float item_w3 = layout.section3.w;
    float s3_x = layout.section3.x;
    layout.right_btn = { .x = s3_x, .y = layout.section3.y + layout.section_padding + layout.section3.h - btn_h - layout.section_padding, .w = item_w3, .h = btn_h };
    layout.rot_btn = { .x = s3_x, .y = layout.right_btn.y - layout.padding - btn_h, .w = item_w3, .h = btn_h };
    
    float next_y = layout.section_padding;
    float next_h = (float)render_h * 0.25f; 
    layout.next_area = { .x = s3_x, .y = next_y, .w = item_w3, .h = next_h };

    float timer_level_h = 40.0f * layout.text_scale;
    float opp_y = layout.next_area.y + layout.next_area.h + layout.padding + timer_level_h;
    float opp_h = layout.rot_btn.y - layout.padding - opp_y;
    layout.opp_grid = { .x = s3_x, .y = opp_y, .w = item_w3, .h = opp_h };

    return layout;
}

void RenderSystem::RenderCenteredText(SDL_Renderer* renderer, float cx, float cy, float scale, const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = SDL_strlen(buf);
    float tw = len * 8.0f;
    float th = 8.0f;

    SDL_SetRenderScale(renderer, scale, scale);
    SDL_RenderDebugText(renderer, (cx / scale) - (tw / 2.0f), (cy / scale) - (th / 2.0f), buf);
    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

void RenderSystem::DrawBoardRel(AppContext& app, TetrisBoard& board, const SDL_FRect& area, int align_h, int align_v) {
    float panel_x = area.x;
    float panel_y = area.y;
    float panel_w = area.w;
    float panel_h = area.h;
    float grid_area_w = panel_w;
    float grid_area_h = panel_h;
    float cell_w = grid_area_w / TetrisBoard::WIDTH;
    float cell_h = grid_area_h / TetrisBoard::HEIGHT;
    float cell_size = (cell_w < cell_h) ? cell_w : cell_h;
    
    float offset_x;
    if (align_h == -1) offset_x = panel_x;
    else if (align_h == 1) offset_x = panel_x + panel_w - cell_size * TetrisBoard::WIDTH;
    else offset_x = panel_x + (grid_area_w - cell_size * TetrisBoard::WIDTH) / 2.0f;

    float y_offset;
    if (align_v == -1) y_offset = panel_y;
    else if (align_v == 1) y_offset = panel_y + panel_h - cell_size * TetrisBoard::HEIGHT;
    else y_offset = panel_y + (grid_area_h - cell_size * TetrisBoard::HEIGHT) / 2.0f;

    for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
        for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
            SDL_FRect rect = {.x = offset_x + x * cell_size, .y = y_offset + y * cell_size, .w = cell_size - 1.0f, .h = cell_size - 1.0f};
            int color_idx = board.grid[y][x].color;
            if (color_idx != 0) {
                SDL_SetRenderDrawColor(app.renderer.get(), 225, 215, 185, 255);
                SDL_RenderFillRect(app.renderer.get(), &rect);
                if (board.grid[y][x].has_crystal) {
                    SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 0, 255);
                    float c_margin = cell_size * 0.2f;
                    SDL_FRect crect = {.x = offset_x + x * cell_size + c_margin, .y = y_offset + y * cell_size + c_margin, .w = cell_size - c_margin * 2.0f, .h = cell_size - c_margin * 2.0f};
                    SDL_RenderFillRect(app.renderer.get(), &crect);
                }
            } else {
                SDL_SetRenderDrawColor(app.renderer.get(), 80, 80, 80, 255);
                SDL_RenderFillRect(app.renderer.get(), &rect);
            }
        }
    }
    if (board.current_piece.type > 0 && board.current_piece.type <= 7) {
        const auto& c = TETROMINO_DEFS[board.current_piece.type].color;

        int orig_y = board.current_piece.y;
        int ghost_y = orig_y;
        while (true) {
            board.current_piece.y++;
            if (board.check_collision()) {
                board.current_piece.y--;
                ghost_y = board.current_piece.y;
                break;
            }
        }
        board.current_piece.y = orig_y;

        if (ghost_y > orig_y) {
            SDL_SetRenderDrawBlendMode(app.renderer.get(), SDL_BLENDMODE_BLEND);
            for(int r=0; r<4; r++) {
                for(int c_idx=0; c_idx<4; c_idx++) {
                    if(board.current_piece.shape[r][c_idx]) {
                        SDL_FRect rect = {.x = offset_x + (board.current_piece.x + c_idx) * cell_size, .y = y_offset + (ghost_y + r) * cell_size, .w = cell_size - 1.0f, .h = cell_size - 1.0f};
                        SDL_SetRenderDrawColor(app.renderer.get(), 225, 215, 185, 64);
                        SDL_RenderFillRect(app.renderer.get(), &rect);
                    }
                }
            }
            SDL_SetRenderDrawBlendMode(app.renderer.get(), SDL_BLENDMODE_NONE);
        }

        for (int r = 0; r < 4; r++) {
            for (int c_idx = 0; c_idx < 4; c_idx++) {
                if (board.current_piece.shape[r][c_idx]) {
                    SDL_FRect rect = {.x = offset_x + (board.current_piece.x + c_idx) * cell_size,
                                      .y = y_offset + (board.current_piece.y + r) * cell_size,
                                      .w = cell_size - 1.0f,
                                      .h = cell_size - 1.0f};
                    SDL_SetRenderDrawColor(app.renderer.get(), c.r, c.g, c.b, c.a);
                    SDL_RenderFillRect(app.renderer.get(), &rect);
                    if (board.current_piece.has_crystal[r][c_idx]) {
                        SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 0, 255);
                        float c_margin = cell_size * 0.2f;
                        SDL_FRect crect = {.x = offset_x + (board.current_piece.x + c_idx) * cell_size + c_margin,
                                          .y = y_offset + (board.current_piece.y + r) * cell_size + c_margin,
                                          .w = cell_size - c_margin * 2.0f,
                                          .h = cell_size - c_margin * 2.0f};
                        SDL_RenderFillRect(app.renderer.get(), &crect);
                    }
                }
            }
        }
    }
}

void RenderSystem::Render(AppContext& app) {
    int render_w, render_h;
    SDL_GetRenderOutputSize(app.renderer.get(), &render_w, &render_h);
    float ui_scale = SDL_GetWindowDisplayScale(app.window.get());
    UILayout layout = CalculateLayout(render_w, render_h);

    SDL_SetRenderDrawColor(app.renderer.get(), 40, 40, 45, 255); 
    SDL_RenderClear(app.renderer.get());

    if (app.state == GameState::MENU) {
        float menu_text_scale = 2.0f * ui_scale;
#if CLIENT_ID == 1
        RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.3f, menu_text_scale, "CLIENT 1 - TETRIS BATTLE");
#elif CLIENT_ID == 2
        RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.3f, menu_text_scale, "CLIENT 2 - TETRIS BATTLE");
#else
        RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.3f, menu_text_scale, "TETRIS BATTLE");
#endif
        RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.4f, menu_text_scale, "Press ENTER to Start");

        SDL_SetRenderDrawColor(app.renderer.get(), 200, 50, 50, 255);
        SDL_FRect exit_btn = {.x = render_w * 0.5f - 100.0f * ui_scale, .y = render_h * 0.6f, .w = 200.0f * ui_scale, .h = 60.0f * ui_scale};
        SDL_RenderFillRect(app.renderer.get(), &exit_btn);
        
        SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 255, 255);
        RenderCenteredText(app.renderer.get(), exit_btn.x + exit_btn.w * 0.5f, exit_btn.y + exit_btn.h * 0.5f, menu_text_scale, "EXIT");

    } else if (app.state == GameState::GAME_OVER) {
        float go_text_scale = 2.0f * ui_scale;
        if (app.net_client && app.net_client->is_game_over()) {
            if (app.net_client->am_i_winner()) {
                SDL_SetRenderDrawColor(app.renderer.get(), 0, 255, 0, 255);
                RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.4f, go_text_scale, "YOU WIN!");
            } else {
                SDL_SetRenderDrawColor(app.renderer.get(), 255, 50, 50, 255);
                RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.4f, go_text_scale, "YOU LOSE!");
            }
        } else {
            RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.4f, go_text_scale, "GAME OVER!");
        }
        SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 255, 255);
        RenderCenteredText(app.renderer.get(), render_w * 0.5f, render_h * 0.5f, go_text_scale, "Press ENTER to Restart");
    } else if (app.state == GameState::PLAYING) {
        SDL_SetRenderDrawColor(app.renderer.get(), 30, 30, 35, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.section1);
        SDL_SetRenderDrawColor(app.renderer.get(), 45, 45, 50, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.section2);
        SDL_SetRenderDrawColor(app.renderer.get(), 30, 30, 35, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.section3);

        DrawBoardRel(app, app.board1, layout.section2, 0, 0);

        SDL_SetRenderDrawColor(app.renderer.get(), 200, 50, 50, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.menu_btn);
        
        SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 255, 255);
        RenderCenteredText(app.renderer.get(), layout.menu_btn.x + layout.menu_btn.w * 0.5f, layout.menu_btn.y + layout.menu_btn.h * 0.5f, layout.text_scale, "MENU");
        
        SDL_SetRenderDrawColor(app.renderer.get(), 255, 0, 255, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.pwr_btn);
        SDL_SetRenderDrawColor(app.renderer.get(), 100, 100, 100, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.drop_btn);
        SDL_RenderFillRect(app.renderer.get(), &layout.left_btn);

        DrawBoardRel(app, app.board2, layout.opp_grid, 0, 0);

        if (app.board1.shared_queue && app.net_client) {
            int next_index = app.net_client->get_global_next_index();
            int num_next = 3;
            float next_cell_size = (layout.next_area.w / 5.0f);
            float next_offset_y = layout.next_area.y + 15.0f * layout.text_scale;
            
            for (int i = 0; i < num_next; i++) {
                PieceInfo info = app.board1.shared_queue->get_piece_at(next_index + i);
                const auto& def = TETROMINO_DEFS[info.type + 1];
                float current_next_y = next_offset_y + i * (next_cell_size * 4.5f);
                for(int r=0; r<4; r++) {
                    for(int c=0; c<4; c++) {
                        if(def.shape[r][c]) {
                            SDL_FRect rect = {.x = layout.next_area.x + c * next_cell_size, .y = current_next_y + r * next_cell_size, .w = next_cell_size - 1.0f, .h = next_cell_size - 1.0f};
                            SDL_SetRenderDrawColor(app.renderer.get(), def.color.r, def.color.g, def.color.b, def.color.a);
                            SDL_RenderFillRect(app.renderer.get(), &rect);
                            if (r == info.crystal_r && c == info.crystal_c) {
                                SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 0, 255);
                                float c_margin = next_cell_size * 0.2f;
                                SDL_FRect crect = {.x = layout.next_area.x + c * next_cell_size + c_margin, .y = current_next_y + r * next_cell_size + c_margin, .w = next_cell_size - c_margin * 2.0f, .h = next_cell_size - c_margin * 2.0f};
                                SDL_RenderFillRect(app.renderer.get(), &crect);
                            }
                        }
                    }
                }
            }
        }

        SDL_SetRenderDrawColor(app.renderer.get(), 100, 100, 100, 255);
        SDL_RenderFillRect(app.renderer.get(), &layout.rot_btn);
        SDL_RenderFillRect(app.renderer.get(), &layout.right_btn);

        float label_scale = 2.0f * ui_scale * ((float)render_h / 800.0f);
        SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 255, 255);
        
        RenderCenteredText(app.renderer.get(), layout.left_btn.x + layout.left_btn.w * 0.5f, layout.left_btn.y + layout.left_btn.h * 0.5f, label_scale, "<");
        RenderCenteredText(app.renderer.get(), layout.right_btn.x + layout.right_btn.w * 0.5f, layout.right_btn.y + layout.right_btn.h * 0.5f, label_scale, ">");
        RenderCenteredText(app.renderer.get(), layout.rot_btn.x + layout.rot_btn.w * 0.5f, layout.rot_btn.y + layout.rot_btn.h * 0.5f, label_scale, "R");
        RenderCenteredText(app.renderer.get(), layout.drop_btn.x + layout.drop_btn.w * 0.5f, layout.drop_btn.y + layout.drop_btn.h * 0.5f, label_scale, "v");
        char pwr_txt[4];
        SDL_snprintf(pwr_txt, sizeof(pwr_txt), "%d", app.board1.stored_powerups);
        RenderCenteredText(app.renderer.get(), layout.pwr_btn.x + layout.pwr_btn.w * 0.5f, layout.pwr_btn.y + layout.pwr_btn.h * 0.5f, label_scale, pwr_txt);

        if (app.net_client) {
            float overlay_scale = layout.text_scale * 1.5f;
            float board_center_x = (layout.section2.x + layout.section2.w * 0.5f);
            float board_center_y = ((float)render_h * 0.4f);

            if (!app.net_client->is_connected()) {
                SDL_SetRenderDrawColor(app.renderer.get(), 255, 100, 100, 255);
                RenderCenteredText(app.renderer.get(), board_center_x, board_center_y, overlay_scale, "Waiting for server...");
            } else if (!app.net_client->is_opponent_ready()) {
                SDL_SetRenderDrawColor(app.renderer.get(), 255, 255, 0, 255);
                int cd = app.net_client->get_countdown();
                if (cd > 0) RenderCenteredText(app.renderer.get(), board_center_x, board_center_y, overlay_scale, "Match starts in: %d", cd);
                else RenderCenteredText(app.renderer.get(), board_center_x, board_center_y, overlay_scale, "Waiting for opponent...");
            }
            
            if (app.net_client->has_weak_connection()) {
                SDL_SetRenderDrawColor(app.renderer.get(), 255, 165, 0, 255);
                RenderCenteredText(app.renderer.get(), board_center_x, board_center_y + 25.0f * overlay_scale, overlay_scale, "Weak Connection!");
            }
            if (SDL_GetTicks() < app.global_freeze_until) {
                SDL_SetRenderDrawColor(app.renderer.get(), 0, 255, 255, 255);
                RenderCenteredText(app.renderer.get(), board_center_x, board_center_y + 50.0f * overlay_scale, overlay_scale, "POWER ACTIVE - FROZEN!");
            }
        }
    }

    SDL_RenderPresent(app.renderer.get());
}
