#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "app_context.hpp"
#include "game_controller.hpp"
#include "render_system.hpp"

#ifndef CLIENT_ID
#define CLIENT_ID 1
#endif

SDL_AppResult SDL_Fail() {
    SDL_LogError(SDL_LOG_CATEGORY_CUSTOM, "Error: %s", SDL_GetError());
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return SDL_Fail();
    }

    char window_title[32];
    SDL_snprintf(window_title, sizeof(window_title), "Tetris Client %d", CLIENT_ID);
    
    SDL_WindowFlags window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    int window_w = 0;
    int window_h = 0;

#if defined(ANDROID)
    window_flags |= SDL_WINDOW_FULLSCREEN;
#else
    window_flags |= SDL_WINDOW_RESIZABLE;
    window_w = 800;
    window_h = 800;
#endif

    SDL_Window* raw_window = nullptr;
    SDL_Renderer* raw_renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(window_title, window_w, window_h, window_flags, &raw_window, &raw_renderer)) {
        return SDL_Fail();
    }

    auto app = std::make_unique<AppContext>();
    if (argc > 1 && argv[1][0] != '\0') {
        app->client_character = std::atoi(argv[1]);
    }
    app->window.reset(raw_window);
    app->renderer.reset(raw_renderer);

    *appstate = app.release();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppContext*>(appstate);
    uint64_t current_time = SDL_GetTicks();
    GameController::HandleInput(*app, event, current_time);
    return app->app_quit;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppContext*>(appstate);
    uint64_t current_time = SDL_GetTicks();
    GameController::Update(*app, current_time);
    RenderSystem::Render(*app);
    return app->app_quit;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = static_cast<AppContext*>(appstate);
    delete app;
    SDL_Quit();
}
