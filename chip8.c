#include <stdio.h>
#include "SDL.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID devID;
} sdl_t;

typedef struct {
    uint32_t window_width;      // SDL Window Width
    uint32_t window_height;     // SDL window Height
    uint32_t fg_color;          // foreground color RGBA8888
    uint32_t bg_color;          // background color RGBA8888
    uint32_t scale_factor;
    bool pixel_outlines;        // Draw pixel "outlines" yes/no
    uint32_t insts_per_second;  // CHIP8 CPU "clock rate" or hz
    uint32_t square_wave_freq;  // Frequency of square wave sound e.g. 440hz for middle A
    uint32_t audio_sample_rate;
    int16_t volume;             // How loud or not
    float color_lerp_rate;      // Amount to lerp colors by, between [0.1, 1.0]
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

//CHIP8 Instructions
typedef struct {
    uint16_t opcode;     
    uint16_t NNN;          //12 bit address/constants
    uint8_t NN;            //8 bit constant
    uint8_t N;             //4 bit constant
    uint8_t X;             //4 bit register identifier
    uint8_t Y;             //4 bit register identifier
} instruction_t;

//CHIP8 Machine Project
typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];        
    bool display[64 * 32];     //Emulate the original Chip8 resolution
    uint32_t pixel_color[64 * 32];     // CHIP8 pixel colors to draw 
    uint16_t stack[12];        //Subroutine stack
    uint16_t *stack_ptr;       //Stack pointer
    uint8_t V[16];             //Data registers V0-VF
    uint16_t I;                //Index registers
    uint16_t PC;               //Program Counter
    uint8_t delay_timer;       //Decrements at 60hz when > 0
    uint8_t sound_timer;       //Decrements at 60hz and plays tone when > 0
    bool keypad[16];           //Hexadecimal keypad 0x0 - 0xF
    const char *rom_name;      //Currently running ROM
    instruction_t inst;        //Currently executing instructions
    bool draw;                 // Update the screen yes/no
} chip8_t;

// Color "lerp" helper function
uint32_t color_lerp(const uint32_t start_color, const uint32_t end_color, const float t) {
    const uint8_t s_r = (start_color >> 24) & 0xFF;
    const uint8_t s_g = (start_color >> 16) & 0xFF;
    const uint8_t s_b = (start_color >>  8) & 0xFF;
    const uint8_t s_a = (start_color >>  0) & 0xFF;

    const uint8_t e_r = (end_color >> 24) & 0xFF;
    const uint8_t e_g = (end_color >> 16) & 0xFF;
    const uint8_t e_b = (end_color >>  8) & 0xFF;
    const uint8_t e_a = (end_color >>  0) & 0xFF;

    const uint8_t ret_r = ((1-t)*s_r) + (t*e_r);
    const uint8_t ret_g = ((1-t)*s_g) + (t*e_g);
    const uint8_t ret_b = ((1-t)*s_b) + (t*e_b);
    const uint8_t ret_a = ((1-t)*s_a) + (t*e_a);

    return (ret_r << 24) | (ret_g << 16) | (ret_b << 8) | ret_a;
}


//SDL Audio Callback
void audio_callback(void* userdata, uint8_t* stream, int len) {
    config_t* config = (config_t*) userdata;

    int16_t* audio_data = (int16_t*) stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;   // hz / freq = period
    const int32_t half_square_wave_period = square_wave_period / 2;

    // We are filling out 2 bytes at a time (int16_t), len is in bytes,
    //   so divide by 2
    // If the current chunk of audio for the square wave is the crest of the wave, 
    //   this will add the volume, otherwise it is the trough of the wave, and will add
    //   "negative" volume
    for (int i = 0; i < len / 2; i ++) {
        audio_data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? 
                        config->volume : 
                        -config->volume;
    }
}

bool init_SDL(sdl_t* sdl, config_t *config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0 ){
        SDL_Log("Could not initialize SDL subsystem! %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                            config->window_width * config->scale_factor, config->window_height * config->scale_factor, 0);

    if (sdl->window == NULL) {
        SDL_Log("Could not create SDL Window! %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
    if (sdl->renderer == NULL) {
        SDL_Log("Could not create SDL Renderer! %s\n", SDL_GetError());
        return false;
    }

    sdl->want = (SDL_AudioSpec){
        .freq = 44100,              //44100hz "CD" quality
        .format = AUDIO_S16LSB,     //Signed 16 bit little endian 
        .channels = 1,               //Mono 1 channel
        .samples = 4096,            
        .callback = audio_callback,
        .userdata = config,        //Userdata passed to audio callback
    };

    sdl->devID = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);

    if (sdl->devID == 0) {
        SDL_Log("Could not get an Audio Device %s\n", SDL_GetError());
        return false;
    }

    if ((sdl->want.format != sdl->have.format) 
            || (sdl->want.channels != sdl->have.channels)) {
        SDL_Log("Could not get desired Audio Spec %s\n", SDL_GetError());
        return false;
    }



    return true;
}


bool init_config_from_args(config_t* config, const int argc, char** argv) {
    //*config = (config_t) {64, 32, 0xFFFFFFFF, 0x00000000, 30, true, 500};
    *config = (config_t) {
        .window_width = 64,         //CHIP8 origuinal X resolution
        .window_height = 32,        //CHIP8 origuinal Y resolution
        .fg_color = 0xFFFFFFFF,
        .bg_color = 0x00000000,     // Correct color for yellow
        .scale_factor = 20,
        .pixel_outlines = true,
        .insts_per_second = 600,    // Number of instructions to emulate in 1 second (clock rate of CPU)
        .square_wave_freq = 440,    // 440hz for middle A
        .audio_sample_rate = 44100, // CD quality, 44100hz
        .volume = 3000,             // INT16_MAX would be max volume
        .color_lerp_rate = 0.7,
    };
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
        if (strncmp(argv[i], "--scale-factor", strlen("--scale-factor")) == 0) {
            i = i + 1;
            config->scale_factor = (uint32_t)strtol(argv[i], NULL, 10);
        }
    }
    return true;
}

bool init_chip8(chip8_t* chip8,config_t* config, const char rom_name[]) {
    const uint16_t entry_point = 0x200;   //Chip8 ROM will be loaded to 0x200
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };

    memset(chip8, 0, sizeof(chip8_t));

    //load Font
    memcpy(&chip8->ram, font, sizeof(font));

    //load ROM
    FILE* rom = fopen(rom_name, "rb");
    if (rom == NULL) {
        SDL_Log("Rom file %s is invalid or does not exist %s\n", rom_name, SDL_GetError());
        return false;
    }
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if (max_size < rom_size) {
        SDL_Log("Rom file %s is too big! Rom size: %zu, Max size allowed: %zu %s\n", 
            rom_name, rom_size, max_size, SDL_GetError());
        return false;
    }

    if (fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1) {
        SDL_Log("Could not read Rom file %s into CHIP8 Memory %s\n", 
            rom_name, SDL_GetError());
        return false;
    }

    fclose(rom);
    
    chip8->state = RUNNING;
    chip8->PC = entry_point;
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0]; //or &chip8->stack[0]
    memset(chip8->pixel_color, config->bg_color, sizeof chip8->pixel_color);
    return true;
}

//Clear screen / SDL Window to background color
void clear_screen(const sdl_t sdl, const config_t config) {
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color & 0xFF);

    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    if (SDL_RenderClear(sdl.renderer) != 0) {
        SDL_Log("SDL_RenderClear Error: %s", SDL_GetError());
    }
}



void update_screen(const sdl_t sdl, const config_t config, chip8_t* chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    //Grab color value to draw
    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color & 0xFF);

    //loop through display pixels, draw a rectangle per pixel to the SDL window.
    for (uint32_t i = 0; i < sizeof chip8->display; i++) {
        //Translate 1D index value i to 2D X/Y coordinates
        //X = i % window width
        //Y = i / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8->display[i]) {
            if (chip8->pixel_color[i] != config.fg_color) {
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i], config.fg_color, config.color_lerp_rate);
            }

            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
            const uint8_t b = (chip8->pixel_color[i] >>  8) & 0xFF;
            const uint8_t a = (chip8->pixel_color[i] >>  0) & 0xFF;

            //Pixel is on, draw foreground color
            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // If user requested drawing pixel outlines, draw those here
            if (config.pixel_outlines) {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        } else {
            //Pixel is off, draw background color
            if (chip8->pixel_color[i] != config.bg_color) {
                chip8->pixel_color[i] = color_lerp(chip8->pixel_color[i], config.bg_color, config.color_lerp_rate);
            }

            const uint8_t r = (chip8->pixel_color[i] >> 24) & 0xFF;
            const uint8_t g = (chip8->pixel_color[i] >> 16) & 0xFF;
            const uint8_t b = (chip8->pixel_color[i] >>  8) & 0xFF;
            const uint8_t a = (chip8->pixel_color[i] >>  0) & 0xFF;

            //Pixel is on, draw foreground color
            SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

// Handle user input
// CHIP8 Keypad  QWERTY 
// 123C          1234
// 456D          qwer
// 789E          asdf
// A0BF          zxcv
void process_events(config_t *config, chip8_t *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) { //any event (operation) is automatically added at the backend the SDL pulls it out
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) { //The sym member of SDL_Keysym is an SDL_Keycode value that represents the specific key that was pressed or released.
                    // Map qwerty keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = false; break;
                    case SDLK_2: chip8->keypad[0x2] = false; break;
                    case SDLK_3: chip8->keypad[0x3] = false; break;
                    case SDLK_4: chip8->keypad[0xC] = false; break;

                    case SDLK_q: chip8->keypad[0x4] = false; break;
                    case SDLK_w: chip8->keypad[0x5] = false; break;
                    case SDLK_e: chip8->keypad[0x6] = false; break;
                    case SDLK_r: chip8->keypad[0xD] = false; break;

                    case SDLK_a: chip8->keypad[0x7] = false; break;
                    case SDLK_s: chip8->keypad[0x8] = false; break;
                    case SDLK_d: chip8->keypad[0x9] = false; break;
                    case SDLK_f: chip8->keypad[0xE] = false; break;

                    case SDLK_z: chip8->keypad[0xA] = false; break;
                    case SDLK_x: chip8->keypad[0x0] = false; break;
                    case SDLK_c: chip8->keypad[0xB] = false; break;
                    case SDLK_v: chip8->keypad[0xF] = false; break;

                    default: break;
                }
                break;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        //escape key, Exit window & End program
                        chip8->state = QUIT; 
                        break;
                    case SDLK_SPACE: 
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;   // Pause
                            puts("=====PAUSED=====");
                        }
                        else {
                            chip8->state = RUNNING; // Resume
                            
                        }
                        break;

                    case SDLK_EQUALS:
                        // '=': Reset CHIP8 machine for the current ROM
                        init_chip8(chip8, config, chip8->rom_name);
                        break;

                    case SDLK_j:
                        // 'j': Decrease color lerp rate
                        if (config->color_lerp_rate > 0.1)
                            config->color_lerp_rate -= 0.1;
                        break;

                    case SDLK_k:
                        // 'k': Increase color lerp rate
                        if (config->color_lerp_rate < 1.0)
                            config->color_lerp_rate += 0.1;
                        break;

                    case SDLK_o:
                        // 'o': Decrease Volume
                        if (config->volume > 0)
                            config->volume -= 500;
                        break;

                    case SDLK_p:
                        // 'p': Increase Volume
                        if (config->volume < INT16_MAX)
                            config->volume += 500;
                        break;

                    //Map QWERTY keys to CHIP8 keypad
                    case SDLK_1: chip8->keypad[0x1] = true; break;
                    case SDLK_2: chip8->keypad[0x2] = true; break;
                    case SDLK_3: chip8->keypad[0x3] = true; break;
                    case SDLK_4: chip8->keypad[0xC] = true; break;

                    case SDLK_q: chip8->keypad[0x4] = true; break;
                    case SDLK_w: chip8->keypad[0x5] = true; break;
                    case SDLK_e: chip8->keypad[0x6] = true; break;
                    case SDLK_r: chip8->keypad[0xD] = true; break;

                    case SDLK_a: chip8->keypad[0x7] = true; break;
                    case SDLK_s: chip8->keypad[0x8] = true; break;
                    case SDLK_d: chip8->keypad[0x9] = true; break;
                    case SDLK_f: chip8->keypad[0xE] = true; break;

                    case SDLK_z: chip8->keypad[0xA] = true; break;
                    case SDLK_x: chip8->keypad[0x0] = true; break;
                    case SDLK_c: chip8->keypad[0xB] = true; break;
                    case SDLK_v: chip8->keypad[0xF] = true; break;
                    
                    default: break;
                }
                break;
            default:
                break;
        }
    }
}



#ifdef DEBUG
void print_debug_info(chip8_t* chip8) {
    printf("Address: 0x%04X, Opcode: 0x%04X Description: ", chip8->PC - 2, chip8->inst.opcode);

    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                //0x00E0: Clear the screen 
                printf("Clear screen\n");
            } else if (chip8->inst.NN == 0xEE) {
                //0x00EE: Return from subroutine 
                //Set program counter to last address on subroutine stack ("pop" it off the stack)
                printf("Return from subroutine to address 0x%04X\n", *(chip8->stack_ptr - 1));
            } else {
                printf("Umimplemented Opcode.\n");
            }
            break;

        case 0x01:
            //0x1NNN: Jumps to address NNN
            printf("Jumps to address NNN (0x%04X)\n", chip8->inst.NNN);
            break;
        
        case 0x02:
            //0x2NNN: Call subroutine at NNN   (like jump register in MIPS: jumps to next instructions)
            //Store current address to return to on subroutine stack 
            //  and set program counter to subroutine address so that 
            //  the next opcode is gotten from there
            printf("Call subroutine at NNN (0x%04X)\n",
                   chip8->inst.NNN);
            break;

        case 0x03:
            // 0x3XNN: Check if VX == NN, if so, skip the next instruction
            printf("Check if V%X (0x%02X) == NN (0x%02X), skip next instruction if true\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;

        case 0x04:
            // 0x4XNN: Check if VX != NN, if so, skip the next instruction
            printf("Check if V%X (0x%02X) != NN (0x%02X), skip next instruction if true\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN);
            break;

        case 0x05:
            // 0x5XY0: Check if VX == VY, if so, skip the next instruction
            printf("Check if V%X (0x%02X) == V%X (0x%02X), skip next instruction if true\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], 
                   chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 0x06:
            //0x6XNN: Set register VX to NN.
            printf("Set register V%X to NN (0x%02X)\n", 
            chip8->inst.X, chip8->inst.NN);
            break;

        case 0x07:
            // 0x7XNN: Set register VX += NN
            printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.NN,
                   chip8->V[chip8->inst.X] + chip8->inst.NN);
            break;

        case 0x08:
            switch(chip8->inst.N) {
                case 0:
                    // 0x8XY0: Set register VX = VY
                    printf("Set register V%X = V%X (0x%02X)\n",
                           chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y]);
                    break;

                case 1:
                    // 0x8XY1: Set register VX |= VY
                    printf("Set register V%X (0x%02X) |= V%X (0x%02X); Result: 0x%02X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->V[chip8->inst.X] | chip8->V[chip8->inst.Y]);
                    break;

                case 2:
                    // 0x8XY2: Set register VX &= VY
                    printf("Set register V%X (0x%02X) &= V%X (0x%02X); Result: 0x%02X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->V[chip8->inst.X] & chip8->V[chip8->inst.Y]);
                    break;

                case 3:
                    // 0x8XY3: Set register VX ^= VY
                    printf("Set register V%X (0x%02X) ^= V%X (0x%02X); Result: 0x%02X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->V[chip8->inst.X] ^ chip8->V[chip8->inst.Y]);
                    break;

                case 4:
                    // 0x8XY4: Set register VX += VY, set VF to 1 if carry
                    printf("Set register V%X (0x%02X) += V%X (0x%02X), VF = 1 if carry; Result: 0x%02X, VF = %X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y],
                           ((uint16_t)(chip8->V[chip8->inst.X] + chip8->V[chip8->inst.Y]) > 255));
                    break;

                case 5:
                    // 0x8XY5: Set register VX -= VY, set VF to 1 if there is not a borrow (result is positive/0)
                    printf("Set register V%X (0x%02X) -= V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->V[chip8->inst.X] - chip8->V[chip8->inst.Y],
                           (chip8->V[chip8->inst.Y] <= chip8->V[chip8->inst.X]));
                    break;

                case 6:
                    // 0x8XY6: Set register VX >>= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) >>= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->V[chip8->inst.X] & 1,
                           chip8->V[chip8->inst.X] >> 1);
                    break;

                case 7:
                    // 0x8XY7: Set register VX = VY - VX, set VF to 1 if there is not a borrow (result is positive/0)
                    printf("Set register V%X = V%X (0x%02X) - V%X (0x%02X), VF = 1 if no borrow; Result: 0x%02X, VF = %X\n",
                           chip8->inst.X, chip8->inst.Y, chip8->V[chip8->inst.Y],
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X],
                           (chip8->V[chip8->inst.X] <= chip8->V[chip8->inst.Y]));
                    break;

                case 0xE:
                    // 0x8XYE: Set register VX <<= 1, store shifted off bit in VF
                    printf("Set register V%X (0x%02X) <<= 1, VF = shifted off bit (%X); Result: 0x%02X\n",
                           chip8->inst.X, chip8->V[chip8->inst.X],
                           (chip8->V[chip8->inst.X] & 0x80) >> 7,
                           chip8->V[chip8->inst.X] << 1);
                    break;

                default:
                    // Wrong/unimplemented opcode
                    break;
            }
            break;

        case 0x09:
            // 0x9XY0: Check if VX != VY; Skip next instruction if so
            printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], 
                   chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 0x0A:
            // 0xANNN: Set index register I to NNN
            printf("Set I to NNN (0x%04X)\n",
                   chip8->inst.NNN);
            break;

        case 0x0B:
            // 0xBNNN: Jump to V0 + NNN
            printf("Set PC to V0 (0x%02X) + NNN (0x%04X); Result PC = 0x%04X\n",
                   chip8->V[0], chip8->inst.NNN, chip8->V[0] + chip8->inst.NNN);
            break;

        case 0x0C:
            // 0xCXNN: Sets register VX = rand() % 256 & NN (bitwise AND)
            printf("Set V%X = rand() %% 256 & NN (0x%02X)\n",
                   chip8->inst.X, chip8->inst.NN);
            break;


        case 0x0D:
            // 0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
            //   Screen pixels are XOR'd with sprite bits, 
            //   VF (Carry flag) is set if any screen pixels are set off; This is useful
            //   for collision detection or other reasons.
            printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X) "
                   "from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
                   chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
                   chip8->V[chip8->inst.Y], chip8->I);
            break;

        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E: Skip next instruction if key in VX is pressed
                printf("Skip next instruction if key in V%X (0x%02X) is pressed; Keypad value: %d\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);

            } else if (chip8->inst.NN == 0xA1) {
                // 0xEX9E: Skip next instruction if key in VX is not pressed
                printf("Skip next instruction if key in V%X (0x%02X) is not pressed; Keypad value: %d\n",
                       chip8->inst.X, chip8->V[chip8->inst.X], chip8->keypad[chip8->V[chip8->inst.X]]);
            }
            break;

        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A:
                    // 0xFX0A: VX = get_key(); Await until a keypress, and store in VX
                    printf("Await until a key is pressed; Store key in V%X\n",
                           chip8->inst.X);
                    break;

                case 0x1E:
                    // 0xFX1E: I += VX; Add VX to register I. For non-Amiga CHIP8, does not affect VF
                    printf("I (0x%04X) += V%X (0x%02X); Result (I): 0x%04X\n",
                           chip8->I, chip8->inst.X, chip8->V[chip8->inst.X],
                           chip8->I + chip8->V[chip8->inst.X]);
                    break;

                case 0x07:
                    // 0xFX07: VX = delay timer
                    printf("Set V%X = delay timer value (0x%02X)\n",
                           chip8->inst.X, chip8->delay_timer);
                    break;

                case 0x15:
                    // 0xFX15: delay timer = VX 
                    printf("Set delay timer value = V%X (0x%02X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;

                case 0x18:
                    // 0xFX18: sound timer = VX 
                    printf("Set sound timer value = V%X (0x%02X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X]);
                    break;

                case 0x29:
                    // 0xFX29: Set register I to sprite location in memory for character in VX (0x0-0xF)
                    printf("Set I to sprite location in memory for character in V%X (0x%02X). Result(VX*5) = (0x%02X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->V[chip8->inst.X] * 5);
                    break;

                case 0x33:
                    // 0xFX33: Store BCD representation of VX at memory offset from I;
                    //   I = hundred's place, I+1 = ten's place, I+2 = one's place
                    printf("Store BCD representation of V%X (0x%02X) at memory from I (0x%04X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;

                case 0x55:
                    // 0xFX55: Register dump V0-VX inclusive to memory offset from I;
                    //   SCHIP does not inrement I, CHIP8 does increment I
                    printf("Register dump V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;

                case 0x65:
                    // 0xFX65: Register load V0-VX inclusive from memory offset from I;
                    //   SCHIP does not inrement I, CHIP8 does increment I
                    printf("Register load V0-V%X (0x%02X) inclusive at memory from I (0x%04X)\n",
                           chip8->inst.X, chip8->V[chip8->inst.X], chip8->I);
                    break;

                default:
                    break;
            }
            break;

        default:
            printf("Umimplemented Opcode.\n");
            break;
    }
}
#endif

void emulate_instruction(chip8_t* chip8, config_t config) {
    //Get next opcode from RAM
    bool carry;
    chip8->inst.opcode = (chip8->ram[chip8->PC] << 8) | (chip8->ram[chip8->PC + 1]);
    chip8->PC += 2;

    //Fill out current instruction format
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >> 8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >> 4) & 0x0F;

#ifdef DEBUG
    print_debug_info(chip8);
#endif

    switch ((chip8->inst.opcode >> 12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                //0x00E0: Clear the screen 
                memset(&chip8->display[0], false, sizeof chip8->display);
                chip8->draw = true;         // Will update screen on next 60hz tick
            } else if (chip8->inst.NN == 0xEE) {
                //0x00EE: Return from subroutine 
                //Set program counter to last address on subroutine stack ("pop" it off the stack)
                chip8->PC = *--chip8->stack_ptr;  //pre-decrement to the previous instruction (jumps back to the previous instruction)
            } else {
                printf("Umimplemented/Invalid Opcode, may be 0xNNN for calling machine code routine for RCA1802.\n");
            }
            break;

        case 0x01:
            //0x1NNN: Jumps to address NNN
            chip8->PC = chip8->inst.NNN; // Set the program counter so that the next opcode is from NNN
            break;

        case 0x02:
            //0x2NNN: Call subroutine at NNN   (like jump register in MIPS: jumps to next instructions)
            //Store current address to return to on subroutine stack 
            //  and set program counter to subroutine address so that 
            //  the next opcode is gotten from there
            *chip8->stack_ptr++ = chip8->PC;   //post increment, the current stack_ptr will store the current instruction and then move on to the next slot on the stack
            chip8->PC = chip8->inst.NNN;   //jumps to the next instruction
            break;

        case 0x03:
            // 0x3XNN: Check if VX == NN, if so, skip the next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;

        case 0x04:
            // 0x4XNN: Check if VX != NN, if so, skip the next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;
            
        case 0x05:
            // 0x5XNN: Check if VX != NN, if so, skip the next instruction
            if (chip8->inst.N != 0) break; // Wrong opcode

            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;

        case 0x06:
            //0x6XNN: Set register VX to NN.
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;

        case 0x07:
            //0x7XNN: Set register VX += NN.
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x08:
            //0x8XNN: 
            if (chip8->inst.N == 0) {
                // 0x8XY0 Sets VX to the value of VY.
                chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 1) {
                // 0x8XY1 Sets VX to VX or VY. (bitwise OR operation)
                chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
                chip8->V[0xF] = 0;
            } else if (chip8->inst.N == 2) {
                // 0x8XY2 Sets VX to VX and VY. (bitwise AND operation)
                chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
                chip8->V[0xF] = 0;
            } else if (chip8->inst.N == 3) {
                // 0x8XY3 Sets VX to VX xor VY
                chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
                chip8->V[0xF] = 0;
            } else if (chip8->inst.N == 4) {
                // 0x8XY4 Adds VY to VX. VF is set to 1 when there's an overflow, and to 0 when there is not
                //printf("Chip8 VX value is %u, ");
                uint8_t orig_X = chip8->V[chip8->inst.X];
                chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
                if (orig_X > chip8->V[chip8->inst.X]) { //overflow
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[0xF] = 0;
                }
                printf("VX: 0x%X \n", chip8->inst.X);
            } else if (chip8->inst.N == 5) {
                // 0x8XY5 VY is subtracted from VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VX >= VY and 0 if not)
                if (chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y]) {
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];  //the order has to be before the chip8->V[0xF] = 0;
                    chip8->V[0xF] = 0;
                }
            } else if (chip8->inst.N == 6) {
                // 0x8XY6 Stores the least significant bit of VX in VF and then shifts VX to the right by 1
                carry = chip8->V[chip8->inst.Y] & 0x01;
                chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] >> 1;
                chip8->V[0xF] = carry;
            } else if (chip8->inst.N == 7) {
                // 0x8XY7 Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX)
                if (chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]) {
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
                    chip8->V[0xF] = 0;
                }
            } else if (chip8->inst.N == 0xE) {
                // 0x8XYE Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX)
                carry = chip8->V[chip8->inst.Y] >> 7; 
                chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] << 1;
                chip8->V[0xF] = carry;
            } else {
                printf("Umimplemented/Invalid Opcode for 0x08.\n");
            }
            break;

        case 0x09:
            //0x9XY0: Skips the next instruction if VX does not equal VY. (Usually the next instruction is a jump to skip a code block)
            if (chip8->inst.N != 0) break; // Wrong opcode

            if (chip8->V[chip8->inst.X] != chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;

        case 0x0A:
            //0xANNN: Set index register I to NNN
            chip8->I = chip8->inst.NNN;
            break;

        case 0x0B:
            //0xBNNN: Jumps to the address NNN plus V0
            chip8->PC = chip8->inst.NNN + chip8->V[0];
            break;

        case 0x0C:
            //0xBNNN: Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.[
            chip8->V[chip8->inst.X] = (rand() % 256) & chip8->inst.NN;
            break;
        
        case 0x0D: { //The reason we need a {} here because all cases share the same scope. It is valid to declare a variable at the start of the block before any executabel statement.   
            //In C, variable declarations must appear before any executable statements within the same block. 
            //  However, this rule applies to inner blocks, such as those within functions, if statements, loops, and switch cases, but not necessarily to the top-level block of a function. At the top level of a function, you can mix declarations and executable statements.
            
            //0xDXYN: Draw N-height sprite at coordinate X, Y. Read from memory location I;
            //  The sprite has a width of 8 pixels and a height of N pixels;
            //  Screen pixels are XOR'd with sprite bits 
            //  VF(Carry Flag) is set if any screen pixels are set off; This is useful 
            //  for collision detection
            uint8_t X_coord = (chip8->V[chip8->inst.X] % config.window_width);
            uint8_t Y_coord = (chip8->V[chip8->inst.Y] % config.window_height);
            const uint8_t orig_X = X_coord;

            chip8->V[0xF] = 0; //Initialize carry flag to 0
            
            //loop over all N rows of the sprite
            for (uint8_t i = 0; i < chip8->inst.N; i++) {
                //Get next Byte/row of sprite data(The combination of pixels to show the pattern on the screen)
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;

                for (int8_t j = 7; j >= 0; j--) { //The reason we use reversed order is because for each bit in the byte of sprite_data, the most significant bit is the first bit we need to interpret based on the graphical terms
                    //If sprite pixel/bit is on and display pixel is on, set carry flag 
                    bool* pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j)); //why bool does not work?

                    if (sprite_bit && *pixel) {  //both are 1->collision based on XOR 
                        chip8->V[0xF] = 1;
                    }

                    //XOR display pixel with sprite pixel/bit to set it on or off
                    *pixel ^= sprite_bit;

                    //stop drawing if hit the right edge of screen
                    if (++X_coord >= config.window_width) break;
                }

                //Stop drawing entire sprite if hit bottom edge of screen
                if (++Y_coord >= config.window_height) break;
            }
            chip8->draw = true;         // Will update screen on next 60hz tick
            break;
        }

        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                //Skips the next instruction if the key stored in VX is pressed
                if (chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC += 2;
                }
            } else if (chip8->inst.NN == 0xA1) {
                //Skips the next instruction if the key stored in VX is pressed
                if (!chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC += 2;
                }
            } else {
                printf("error code for 0x0E\n");
            }
            break;

        case 0x0F:
            switch (chip8->inst.NN) {
                case 0x0A: {
                    static bool any_key_pressed = false;   
                    static uint8_t pressed_key = 0xFF;
                    for (uint8_t i = 0; i < sizeof chip8->keypad; i++) {
                        if (chip8->keypad[i]) {
                            pressed_key = i;
                            any_key_pressed = true;
                            break;
                        }
                    }

                    // if (!any_key_pressed) {
                    //     chip8->PC -= 2;
                    // } else {
                    //     if (chip8->keypad[pressed_key]) {
                    //         chip8->PC -= 2;
                    //     } else {
                    //         chip8->V[chip8->inst.X] = pressed_key;
                    //         pressed_key = 0xFF;
                    //         any_key_pressed = false;
                    //     }
                    // }
                    if (any_key_pressed && !chip8->keypad[pressed_key]) {
                            chip8->V[chip8->inst.X] = pressed_key;
                            pressed_key = 0xFF;
                            any_key_pressed = false;
                    } else {
                        chip8->PC -= 2;
                    }
                    break;
                }
                case 0x1E:
                    chip8->I += chip8->V[chip8->inst.X];
                    break;

                case 0x07:
                    // 0xFX07: VX = delay timer
                    chip8->V[chip8->inst.X] = chip8->delay_timer;
                    break;

                case 0x15:
                    // 0xFX15: delay timer = VX
                    chip8->delay_timer = chip8->V[chip8->inst.X];
                    break;

                case 0x18:
                    // 0xFX18: sound timer = VX
                   chip8->sound_timer =  chip8->V[chip8->inst.X];
                    break;

                case 0x29:
                    // 0xFX29: Set register I to sprite location in memory for character in VX (0x0-0xF)
                    chip8->I = chip8->V[chip8->inst.X] * 5;
                    break;

                case 0x33: {
                    // 0xFX33: Store BCD representation of VX at memory offset from I;
                    //   I = hundred's place, I+1 = ten's place, I+2 = one's place
                    uint8_t bcd = chip8->V[chip8->inst.X];
                    chip8->ram[chip8->I + 2] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I + 1] = bcd % 10;
                    bcd /= 10;
                    chip8->ram[chip8->I] = bcd;
                    break;
                }

                case 0x55:
                    for (size_t i = 0; i <= chip8->inst.X; i++) {
                        chip8->ram[chip8->I + i] = chip8->V[i];
                    }
                    chip8->I += chip8->inst.X + 1;
                    break;

                case 0x65:
                    for (size_t i = 0; i <= chip8->inst.X; i++) {
                        chip8->V[i] = chip8->ram[chip8->I + i];
                    }
                    chip8->I += chip8->inst.X + 1;
                    break;

                default: 
                    break;
            }
            break;

        default:
            printf("Umimplemented Opcode.\n");
            break;
    }
}


void update_timer(sdl_t sdl, chip8_t *chip8) {
    if (chip8->delay_timer > 0) {
        chip8->delay_timer--;
    }

    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl.devID, 0);   //play the sound 
    } else {
        SDL_PauseAudioDevice(sdl.devID, 1);   //pause the sound
    }
}

void final_clean_up(sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_CloseAudioDevice(sdl.devID);
    SDL_Quit();
}


int main(int argc, char** argv) { 
    //Default Usage message for args
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom_name> \n", argv[0]);
        exit(EXIT_FAILURE);
    }
    //Initialized Config
    config_t config = {0};
    if (!init_config_from_args(&config, argc, argv)) exit(EXIT_FAILURE);

    //Initialize SDL
    sdl_t sdl = {0};
    if (!init_SDL(&sdl, &config)) exit(EXIT_FAILURE);

    //Initialized Chip 8 Machine
    chip8_t chip8 = {0};
    const char* rom_name = argv[1];
    if (!init_chip8(&chip8, &config, rom_name)) exit(EXIT_FAILURE);

    clear_screen(sdl, config); // Keep this here if the display should continually update

    // Seed random number generator
    srand(time(NULL)); //different seeds give difference sequence by rand()

    //main emulator loop
    while (chip8.state != QUIT) {
        process_events(&config, &chip8);
        if (chip8.state == PAUSED) continue;
        //Get_time();

        const u_int64_t star_frame_time = SDL_GetPerformanceCounter();

        //Emulate CHIP8 Instruction
        for (uint32_t i = 0; i < config.insts_per_second / 60; i++) {
            emulate_instruction(&chip8, config);

            // If drawing on CHIP8, only draw 1 sprite this frame (display wait)
            if (chip8.inst.opcode >> 12 == 0xD)
                break; 
        }

        const u_int64_t end_frame_time = SDL_GetPerformanceCounter();

        const double time_elapsed = (double) (((end_frame_time - star_frame_time) * 1000) / SDL_GetPerformanceFrequency());
        
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed : 0); // Control frame rate

        
        // Update window with changes every 60hz
        if (chip8.draw) {
            update_screen(sdl, config, &chip8);
        }

        // Update delay & sound timers every 60hz
        update_timer(sdl, &chip8);
    }


    //Final clean-up
    final_clean_up(sdl);

   
    exit(EXIT_SUCCESS);
}

