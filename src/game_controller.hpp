#pragma once

#include <SDL3/SDL.h>
#include "app_context.hpp"

class GameController {
public:
    static void HandleInput(AppContext& app, SDL_Event* event);
    static void Update(AppContext& app);
    static void ResetGame(AppContext& app);
};
