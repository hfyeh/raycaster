#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "raycaster.h"
#include "raycaster_fixed.h"
#include "raycaster_float.h"
#include "renderer.h"

/**
 * Helper to update the SDL texture with the pixel buffer.
 * Locks the texture, copies the pixels, and renders it to the screen.
 *
 * @param dx Horizontal offset on screen (0 for left half, SCREEN_WIDTH+1 for right half).
 */
static void draw_buffer(SDL_Renderer *sdlRenderer,
                        SDL_Texture *sdlTexture,
                        uint32_t *fb,
                        int dx)
{
    int pitch = 0;
    void *pixelsPtr;
    // Lock texture for write access
    if (SDL_LockTexture(sdlTexture, NULL, &pixelsPtr, &pitch)) {
        fprintf(stderr, "Unable to lock texture");
        exit(1);
    }
    // Copy the framebuffer data
    memcpy(pixelsPtr, fb, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    SDL_UnlockTexture(sdlTexture);

    // Define the destination rectangle on the window
    SDL_Rect r;
    r.x = dx * SCREEN_SCALE;
    r.y = 0;
    r.w = SCREEN_WIDTH * SCREEN_SCALE;
    r.h = SCREEN_HEIGHT * SCREEN_SCALE;

    // Render the texture to the backbuffer
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &r);
}

/**
 * Processes SDL events (keyboard input, window close).
 * Updates movement/rotation direction based on key state.
 *
 * @return true if the application should quit, false otherwise.
 */
static bool process_event(const SDL_Event *event,
                          int *moveDirection,
                          int *rotateDirection)
{
    if (event->type == SDL_QUIT) {
        return true;
    } else if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
               event->key.repeat == 0) {
        SDL_KeyboardEvent k = event->key;
        bool p = event->type == SDL_KEYDOWN; // pressed?
        switch (k.keysym.sym) {
        case SDLK_ESCAPE:
            return true;
            break;
        case SDLK_UP:
            *moveDirection = p ? 1 : 0;
            break;
        case SDLK_DOWN:
            *moveDirection = p ? -1 : 0;
            break;
        case SDLK_LEFT:
            *rotateDirection = p ? -1 : 0;
            break;
        case SDLK_RIGHT:
            *rotateDirection = p ? 1 : 0;
            break;
        default:
            break;
        }
    }
    return false;
}

/**
 * Main Entry Point.
 * Sets up SDL, Game, RayCaster, and runs the main loop.
 * Renders two views side-by-side: Fixed-Point and Floating-Point implementation.
 */
int main(int argc, char *args[])
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    } else {
        // Create window: Double width to show both renderers side-by-side
        SDL_Window *sdlWindow =
            SDL_CreateWindow("RayCaster [fixed-point vs. floating-point]",
                             SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             SCREEN_SCALE * (SCREEN_WIDTH * 2 + 1),
                             SCREEN_SCALE * SCREEN_HEIGHT, SDL_WINDOW_SHOWN);

        if (sdlWindow == NULL) {
            printf("Window could not be created! SDL_Error: %s\n",
                   SDL_GetError());
        } else {
            Game game = GameConstruct();

            // --- Floating Point RayCaster Setup ---
            RayCaster *floatCaster = RayCasterFloatConstruct();
            Renderer floatRenderer = RendererConstruct(floatCaster);
            uint32_t floatBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

            // --- Fixed Point RayCaster Setup ---
            RayCaster *fixedCaster = RayCasterFixedConstruct();
            Renderer fixedRenderer = RendererConstruct(fixedCaster);
            uint32_t fixedBuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

            int moveDirection = 0;
            int rotateDirection = 0;
            bool isExiting = false;

            // Timing variables
            const Uint64 tickFrequency = SDL_GetPerformanceFrequency();
            Uint64 tickCounter = SDL_GetPerformanceCounter();
            SDL_Event event;

            /* FPS calculation with averaging */
            uint32_t fpsCounter = 0;
            uint64_t fpsAccumulator = 0;
            uint32_t displayFPS = 0;

            // SDL Renderer and Textures
            SDL_Renderer *sdlRenderer = SDL_CreateRenderer(
                sdlWindow, -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            SDL_Texture *fixedTexture = SDL_CreateTexture(
                sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
            SDL_Texture *floatTexture = SDL_CreateTexture(
                sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

            // --- Main Game Loop ---
            while (!isExiting) {
                // Render both views
                RendererTraceFrame(&floatRenderer, &game, floatBuffer);
                RendererTraceFrame(&fixedRenderer, &game, fixedBuffer);

                /* Draw FPS on both buffers */
                RendererDrawFPS(fixedBuffer, displayFPS);
                RendererDrawFPS(floatBuffer, displayFPS);

                // Draw to screen: Fixed on left, Float on right
                draw_buffer(sdlRenderer, fixedTexture, fixedBuffer, 0);
                draw_buffer(sdlRenderer, floatTexture, floatBuffer,
                            SCREEN_WIDTH + 1);

                SDL_RenderPresent(sdlRenderer);

                // Event Handling
                if (SDL_PollEvent(&event)) {
                    isExiting =
                        process_event(&event, &moveDirection, &rotateDirection);
                }

                // Timing & Game Update
                const Uint64 nextCounter = SDL_GetPerformanceCounter();
                const Uint64 ticks = nextCounter - tickCounter;
                tickCounter = nextCounter;

                /* Update FPS with averaging over 60 frames */
                fpsAccumulator += ticks;
                fpsCounter++;
                if (fpsCounter >= 60) {
                    displayFPS = (uint32_t) (tickFrequency * fpsCounter /
                                             fpsAccumulator);
                    fpsAccumulator = 0;
                    fpsCounter = 0;
                }

                // Update game state (movement)
                // normalize ticks to ~256 units per second for fixed point?
                // actually ticks / (freq >> 8)  =>  (ticks * 256) / freq.
                // seconds argument in GameMove is effectively "fractional seconds * 256".
                GameMove(&game, moveDirection, rotateDirection,
                         ticks / (SDL_GetPerformanceFrequency() >> 8));
            }

            // Cleanup
            SDL_DestroyTexture(floatTexture);
            SDL_DestroyTexture(fixedTexture);
            SDL_DestroyRenderer(sdlRenderer);
            SDL_DestroyWindow(sdlWindow);

            // Destruct RayCasters
            floatCaster->Destruct(floatCaster);
            fixedCaster->Destruct(fixedCaster);
        }
    }

    SDL_Quit();
    return 0;
}
