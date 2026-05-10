#pragma once

#include <SDL3/SDL.h>
#include "app_context.hpp"

class RenderSystem {
public:
    static UILayout CalculateLayout(int render_w, int render_h);
    static void RenderCenteredText(SDL_Renderer* renderer, float cx, float cy, float scale, const char* fmt, ...);
    static void Render(AppContext& app);

private:
    static void DrawBoardRel(AppContext& app, TetrisBoard& board, const SDL_FRect& area, int align_h, int align_v);
};
