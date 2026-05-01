#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

struct AppContext {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_AppResult app_quit = SDL_APP_CONTINUE;
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

    if (!SDL_CreateWindowAndRenderer("Tetris", 0, 0, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        return SDL_Fail();
    }

    // Let's scale the whole renderer up by 2x so we can actually read the debug text
    SDL_SetRenderScale(renderer, 2.0f, 2.0f);

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
    
    // 3. Draw the text at X: 100, Y: 100
    SDL_RenderDebugText(app->renderer, 100.0f, 100.0f, "Hello, Android and Linux!");

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
