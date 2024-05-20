#include <stdio.h>
#include "SDL.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
} sdl_t;

typedef struct {
    u_int32_t window_width;     // SDL Window Width
    u_int32_t window_height;    // SDL window Height
    u_int32_t fg_color;         // foreground color RGBA8888
    u_int32_t bg_color;         // background color RGBA8888
    u_int32_t scale_factor;
    bool pixel_outlines;        // Draw pixel "outlines" yes/no
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

//CHIP8 Instructions
typedef struct {
    u_int16_t opcode;     
    u_int16_t NNN;          //12 bit address/constants
    u_int8_t NN;            //8 bit constant
    u_int8_t N;             //4 bit constant
    u_int8_t X;             //4 bit register identifier
    u_int8_t Y;             //4 bit register identifier
} instruction_t;

//CHIP8 Machine Project
typedef struct {
    emulator_state_t state;
    u_int8_t ram[4096];        
    bool display[64 * 32];     //Emulate the original Chip8 resolution
    u_int16_t stack[12];       //Subroutine stack
    u_int16_t *stack_ptr;      //Stack pointer
    u_int8_t V[16];            //Data registers V0-VF
    u_int16_t I;               //Index registers
    u_int16_t PC;              //Program Counter
    u_int8_t delay_timer;      //Decrements at 60hz when > 0
    u_int8_t sound_timer;      //Decrements at 60hz and plays tone when > 0
    bool keypad[16];           //Hexadecimal keypad 0x0 - 0xF
    const char *rom_name;      //Currently running ROM
    instruction_t inst;        //Currently executing instructions
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
    *config = (config_t) {64, 32, 0xFFFFFFFF, 0x00000000, 20, true};
    // *config = (config_t) {
    //     .window_width = 64,       //CHIP8 origuinal X resolution
    //     .window_height = 32,      //CHIP8 origuinal Y resolution
    //     .fg_color = 0xFFFFFF00,
    //     .bg_color = 0xFFFFFF00, // Correct color for yellow
    //     .scale_factor = 20,
    //     .pixel_outlines = true;
    // };
    for (int i = 1; i < argc; i++) {
        (void)argv[i];
    }
    return true;
}

bool init_chip8(chip8_t* chip8, const char rom_name[]) {
    const u_int16_t entry_point = 0x200;   //Chip8 ROM will be loaded to 0x200
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
    chip8->stack_ptr = chip8->stack;
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



void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    //Grab color value to draw
    const u_int8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const u_int8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const u_int8_t fg_b = (config.fg_color >> 8) & 0xFF;
    const u_int8_t fg_a = (config.fg_color & 0xFF);

    const u_int8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const u_int8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const u_int8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const u_int8_t bg_a = (config.bg_color & 0xFF);

    //loop through display pixels, draw a rectangle per pixel to the SDL window.
    for (u_int32_t i = 0; i < sizeof chip8.display; i++) {
        //Translate 1D index value i to 2D X/Y coordinates
        //X = i % window width
        //Y = i / window width
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if (chip8.display[i]) {
            //Pixel is on, draw foreground color
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // If user requested drawing pixel outlines, draw those here
            if (config.pixel_outlines) {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        } else {
            //Pixel is off, draw background color
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

        }
    }
    SDL_RenderPresent(sdl.renderer);
}



void final_clean_up(sdl_t sdl) {
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

void process_events(chip8_t *chip8) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) { //any event (operation) is automatically added at the backend the SDL pulls it out
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYUP:
                break;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        //escape key, Exit window & End program
                        chip8->state = QUIT; 
                        return;
                    case SDLK_SPACE: 
                        if (chip8->state == RUNNING) {
                            chip8->state = PAUSED;   // Pause
                            puts("=====PAUSED=====");
                        }
                        else {
                            chip8->state = RUNNING; // Resume
                            
                        }
                        return;
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
            *chip8->stack_ptr++ = chip8->PC;   //post increment, the current stack_ptr will store the current instruction and then move on to the next slot on the stack
            chip8->PC = chip8->inst.NNN;   //jumps to the next instruction
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

        case 0x0A:
            //0xANNN: Set index register I to NNN
            printf("Set I to NNN (0x%04X) \n", chip8->inst.NNN);
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

        default:
            printf("Umimplemented Opcode.\n");
            break;
    }
}
#endif

void emulate_instruction(chip8_t* chip8, config_t config) {
    //Get next opcode from RAM
    chip8->inst.opcode = (chip8->ram[chip8->PC]) << 8 | (chip8->ram[chip8->PC + 1]);
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

        case 0x0A:
            //0xANNN: Set index register I to NNN
            chip8->I = chip8->inst.NNN;
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
            for (u_int8_t i = 0; i < chip8->inst.N; i++) {
                //Get next Byte/row of sprite data(The combination of pixels to show the pattern on the screen)
                const u_int8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;

                for (int8_t j = 7; j >= 0; j--) { //The reason we use reversed order is because for each bit in the byte of sprite_data, the most significant bit is the first bit we need to interpret based on the graphical terms
                    //If sprite pixel/bit is on and display pixel is on, set carry flag 
                    bool* pixel = &chip8->display[Y_coord * config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1 << j));

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
            break;
        }

        default:
            printf("Umimplemented Opcode.\n");
            break;
    }
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
    if (!init_SDL(&sdl, config)) exit(EXIT_FAILURE);

    //Initialized Chip 8 Machine
    chip8_t chip8 = {0};
    const char* rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name)) exit(EXIT_FAILURE);

    //Initialize screen color to background color
    
    

    //main emulator loop
    while (chip8.state != QUIT) {
        process_events(&chip8);
        if (chip8.state == PAUSED) continue;
        //Get_time();

        //Emulate CHIP8 Instruction
        emulate_instruction(&chip8, config);


        SDL_Delay(16); // Control frame rate

        clear_screen(sdl, config); // Keep this here if the display should continually update
        update_screen(sdl, config, chip8);
    }


    //Final clean-up
    final_clean_up(sdl);

   
    exit(EXIT_SUCCESS);
}

