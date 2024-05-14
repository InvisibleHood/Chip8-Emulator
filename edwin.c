#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "SDL.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

typedef struct {
    uint32_t window_width;
    uint32_t window_height;
    uint32_t fg_color; //forground color
    uint32_t bg_color; //background color
    uint32_t scale_factor;
} config_t;



bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("initialize failed! %s\n",SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8 EMULATOR", SDL_WINDOWPOS_CENTERED, 
                                    SDL_WINDOWPOS_CENTERED, 
                                    config.window_width * config.scale_factor, config.window_height  * config.scale_factor, 
                                    0);

    if(!sdl->window) {
        SDL_Log("create window failed %s", SDL_GetError());
        return false;
    }     

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);  
    if(!sdl->renderer) {
        SDL_Log("create renderer failed %s", SDL_GetError());
        return false;
    }                            
    return true;
}
void final_cleanup(const sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}
void process_events(bool* running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        }
    }
}

//initialize config
bool init_config(config_t *config, const int argc, char **argv) {
    *config = (config_t) {
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFF00FF00,
        .bg_color = 0xFF00FF00,
        .scale_factor = 20
    };
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
    }
    return true;
}


//clear screen/ sdl window to background color
void clear_screen(sdl_t sdl, const config_t config) {
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;


    SDL_SetRenderDrawColor(sdl.renderer, r,g,b,a);
    SDL_RenderClear(sdl.renderer);
}


void update_screen(const sdl_t sdl) {
    SDL_RenderPresent(sdl.renderer);
}
int main(int argc, char **argv){
    printf("reached");
    (void)argc;
    (void)argv;


    config_t config = {0};
    if(!init_config(&config, argc, argv)) {
        exit(EXIT_FAILURE);
    }
    sdl_t sdl = {0};
    if(!init_sdl(&sdl, config)) {
        exit(EXIT_FAILURE);
    }

    clear_screen(sdl,config);//initialize screen color

    //main loop
    bool running = true;
    while(running) {
        SDL_Delay(16);
        process_events(&running);

        update_screen(sdl);
    }

    //FINISH AND CLEAN UP
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
    return 0;
}