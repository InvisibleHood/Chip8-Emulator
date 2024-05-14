#include <stdio.h>
#include "SDL.h"
#include <stdbool.h>

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
} sdl_t;

typedef struct {
    u_int32_t window_width;     //SDL Window Width
    u_int32_t window_height;    //SDL window Height
    u_int32_t fg_color;         //foreground color RGBA8888
    u_int32_t bg_color;         //background color RGBA8888
    u_int32_t scale_factor;
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

//CHIP8 Machine Project
typedef struct {
    emulator_state_t state;
} chip8_t;

bool init_SDL(sdl_t* sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0 ){
        SDL_Log("Could not initialize SDL subsystem! %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                            config.window_width * config.scale_factor, config.window_height * config.scale_factor, 0);

    if (sdl->window == NULL) {
        SDL_Log("Could not create SDL Window! %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (sdl->renderer == NULL) {
        SDL_Log("Could not create SDL Renderer! %s\n", SDL_GetError());
        return false;
    }

    return true;
}


bool init_config_from_args(config_t* config, const int argc, char** argv) {
    *config = (config_t) {64, 32, 0xFFFFFF00, 0xFFFF00FF, 20};
    // *config = (config_t) {
    //     .window_width = 64,       //CHIP8 origuinal X resolution
    //     .window_height = 32,      //CHIP8 origuinal Y resolution
    //     .fg_color = 0xFFFFFF00,
    //     .bg_color = 0xFFFFFF00, // Correct color for yellow
    //     .scale_factor = 20,
    // };
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
    }
    return true;
}

bool init_chip8(chip8_t* chip8) {
    chip8->state = RUNNING;
    return true;
}

//Clear screen / SDL Window to background color
void clear_screen(const sdl_t sdl, const config_t config) {
    const u_int8_t r = (config.bg_color >> 24) & 0xFF;
    const u_int8_t g = (config.bg_color >> 16) & 0xFF;
    const u_int8_t b = (config.bg_color >> 8) & 0xFF;
    const u_int8_t a = (config.bg_color & 0xFF);

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    if (SDL_RenderClear(sdl.renderer) != 0) {
        SDL_Log("SDL_RenderClear Error: %s", SDL_GetError());
    }
}



void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}



void final_clean_up(sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

void process_events(chip8_t *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        printf("NO in while loop\n");
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYUP:
                printf("IM UP\n");
                break;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        //escape key, Exit window & End program
                        chip8->state = QUIT; 
                        return;
                }
                break;
            default:
                break;
        }
    }
}



int main(int argc, char** argv) { 
    //Initialized Config
    config_t config = {0};
    if (!init_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    //Initialize SDL
    sdl_t sdl = {0};
    if (!init_SDL(&sdl, config)) exit(EXIT_FAILURE);

    //Initialized Chip 8 Machine
    chip8_t chip8 = {0};
    if (!init_chip8(&chip8)) exit(EXIT_FAILURE);

    //Initialize screen color to background color
    
    

    //main emulator loop
    while (chip8.state != QUIT) {
        process_events(&chip8);
        SDL_Delay(16); // Control frame rate

        clear_screen(sdl, config); // Keep this here if the display should continually update
        update_screen(sdl);
    }


    //Final clean-up
    final_clean_up(sdl);

   
    exit(EXIT_SUCCESS);
}

