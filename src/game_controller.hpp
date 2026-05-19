#pragma once

#include <SDL3/SDL.h>
#include "app_context.hpp"

class GameController {
public:
    static void HandleInput(AppContext& app, SDL_Event* event, uint64_t current_time);
    static void Update(AppContext& app, uint64_t current_time);
    static void ResetGame(AppContext& app, std::shared_ptr<NetworkClient> custom_client = nullptr);
};
