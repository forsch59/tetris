#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer("Tetris", 0, 0, SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
        SDL_Log("Window/Renderer creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Optional: The default debug font is incredibly tiny (8x8 pixels).
    // Let's scale the whole renderer up by 2x so we can actually read it!
    SDL_SetRenderScale(renderer, 2.0f, 2.0f);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // 1. Set background color to Dark Gray and Clear the screen
        SDL_SetRenderDrawColor(renderer, 40, 40, 45, 255); 
        SDL_RenderClear(renderer);

        // 2. Set text color to White (Red 255, Green 255, Blue 255, Alpha 255)
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        
        // 3. Draw the text at X: 100, Y: 100
        SDL_RenderDebugText(renderer, 100.0f, 100.0f, "Hello, Android and Linux!");

        // 4. Swap the buffers to display everything
        SDL_RenderPresent(renderer);

        SDL_Delay(16); 
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
