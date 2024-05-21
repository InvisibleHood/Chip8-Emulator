#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
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
    bool pixel_outlines; //draw outlines
    uint32_t insts_per_second; //CHIP8 CPU "clock rate" or hz
} config_t;

typedef enum {
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

typedef struct {
    uint16_t opcode;
    uint16_t NNN; //12 bit
    uint8_t NN;  //8
    uint8_t N;   //4
    uint8_t X;   //4
    uint8_t Y;  //4
} instruction_t;

typedef struct {
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];  //Emulate original CHIP8 resolution pixels
    uint16_t stack[12];  //subroutine stack
    uint16_t *stack_ptr; //stack pointer
    uint8_t V[16];  //data registers    v0-vf
    uint16_t I;     //index register
    uint16_t PC;
    uint8_t delay_timer;    //decrements at 60hz when >0
    uint8_t sound_timer;     //decrements at 60hz  and plays tone when >0
    bool keypad[16];        //hexadecimal keypad 0x0-0xf
    const char *rom_name;     //currently running rom
    instruction_t inst;     //current instruction
} chip8_t;

bool init_sdl(sdl_t *sdl, const config_t config) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        SDL_Log("initialize failed! %s\n",SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("CHIP8 EMULATOR", SDL_WINDOWPOS_CENTERED, 
                                    SDL_WINDOWPOS_CENTERED, 
                                    config.window_width*config.scale_factor, config.window_height*config.scale_factor, 
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

//initialize config
bool init_config(config_t *config, const int argc, char **argv) {
    *config = (config_t) {
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFFFFFFFF,
        .bg_color = 0x00000000,
        .scale_factor = 30,
        .pixel_outlines = true,
        .insts_per_second = 600,
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


void update_screen(const sdl_t sdl, const config_t config, const chip8_t chip8) {
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    //Grab color value to draw
    const uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    const uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    const uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    const uint8_t fg_a = (config.fg_color & 0xFF);

    const uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    const uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    const uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    const uint8_t bg_a = (config.bg_color & 0xFF);

    //loop through display pixels, draw a rectangle per pixel to the SDL window.
    for (uint32_t i = 0; i < sizeof chip8.display; i++) {
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

//handle user input
// Handle user input
// CHIP8 Keypad  QWERTY 
// 123C          1234
// 456D          qwer
// 789E          asdf
// A0BF          zxcv
void handle_input(chip8_t *chip8) {
    SDL_Event event;
    while(SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                chip8->state = QUIT;
                return;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE: // esc
                        chip8->state = QUIT;
                        return;

                    case SDLK_SPACE: // PRESS SPACE PAUSE
                        if(chip8->state == RUNNING) {
                            chip8->state = PAUSED;
                            puts("==== PUASED ====");
                        } else {
                            chip8->state = RUNNING;
                        }
                        return;

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

            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
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

            default:
                break;
        }
    }
}


//initialize chip8
bool init_chip8(chip8_t *chip8, const char rom_name[]) {
    const uint32_t entry_point = 0x200; //game starting point
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
    //load font 
    memcpy(&chip8->ram[0],font,sizeof(font));

    //load game
    FILE *rom = fopen(rom_name,"rb");
    if(!rom) { //GAME FAILED
        SDL_Log("rom open failed %s\n", rom_name);
        return false;
    }
    //CHECK SIZE OF ROM
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof chip8->ram - entry_point;
    rewind(rom);

    if(rom_size>max_size) {
        SDL_Log("Rom size too large");
        return false;
    }

    if(fread(&chip8->ram[entry_point],rom_size,1,rom)!=1) {
        SDL_Log("could not read the rom");
        return false;
    }



    chip8->state = RUNNING; //SET STATE
    chip8->PC = entry_point; //SET PC AT ROM
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    return true;

}

#ifdef DEBUG
void printf_debug_infor(chip8_t *chip8) {
    printf("Address: 0x%04X, Opcode: 0x%04X Desc: ", chip8->PC - 2, chip8->inst.opcode);

    switch ((chip8->inst.opcode >>12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                printf("Clear scrren\n");
            } else if (chip8->inst.NN == 0xEE) {//return from subroutine
                printf("Return from subroutine tp address 0x%04X\n",
                       *(chip8->stack_ptr - 1));
            } else {
                printf("Unimplemented Opcode.\n");
            }
            break;

        case 0x01:
        //0x1NNN: jump to address NNN
            printf("Jump to address NNN (0x%04X)\n",
            chip8->inst.NNN);
            break;

        case 0x02://0x2NNN call subroutine at NNN
            *chip8->stack_ptr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
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
        // 0x6XNN: Set register VX to NN
            printf("Set register V%X = NN (0x%02X)\n",
                chip8->inst.X, chip8->inst.NN);
            break;

        case 0x07:
        // 0x7XNN: set register VX += NN
            printf("Set register V%X (0x%02X) += NN (0x%02X). Result: 0x%02X\n ",
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
        //0x9XY0: Skips the next instruction if VX does not equal VY. (Usually the next instruction is a jump to skip a code block)
            printf("Check if V%X (0x%02X) != V%X (0x%02X), skip next instruction if true\n",
                   chip8->inst.X, chip8->V[chip8->inst.X], 
                   chip8->inst.Y, chip8->V[chip8->inst.Y]);
            break;

        case 0x0A:
        //0xANNN: Set index register I to NNN
            printf("Set I to NNN (0x%04X)\n", chip8->inst.NNN);
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
        //0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
        // Screen pixels are XOR'd with sprite bits,
        // VF (Carry flag) is set if any screen pixels are set off; This is
        // useful for collision detection or other reasons.
            printf("Draw N (%u) height sprite at coords V%X (0x%02X), V%X (0x%02X)"
                    "from memory location I (0x%04X). Set VF = 1 if any pixels are turned off.\n",
                    chip8->inst.N, chip8->inst.X, chip8->V[chip8->inst.X], chip8->inst.Y,
                    chip8->V[chip8->inst.Y],chip8->I);
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
            printf("Unimplemented Opcode.\n");
            break;

    }

    return;
}
#endif

//emulate 1 chip8 instruction
void emulate_instruction(chip8_t *chip8, const config_t config) {
    chip8->inst.opcode = (chip8->ram[chip8 ->PC] << 8) | chip8->ram[chip8->PC+1];
    chip8->PC += 2; // increment pc

    //OPCODE:DXYN
    chip8->inst.NNN = chip8->inst.opcode & 0x0FFF;
    chip8->inst.NN = chip8->inst.opcode & 0x0FF;
    chip8->inst.N = chip8->inst.opcode & 0x0F;
    chip8->inst.X = (chip8->inst.opcode >>8) & 0x0F;
    chip8->inst.Y = (chip8->inst.opcode >>4) & 0x0F;

#ifdef DEBUG
    printf_debug_infor(chip8);
#endif


    switch ((chip8->inst.opcode >>12) & 0x0F) {
        case 0x00:
            if (chip8->inst.NN == 0xE0) {
                memset(&chip8->display[0], false, sizeof chip8->display);
            } else if (chip8->inst.NN == 0xEE) {//return from subroutine
                chip8->PC = *--chip8->stack_ptr;
            } else {
                printf("unimplemented code, invalide opcode\n");
            }
            break;

        case 0x01:
        //0x1NNN: jump to address NNN
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x02:
        //0x2NNN call subroutine at NNN
            *chip8->stack_ptr++ = chip8->PC;
            chip8->PC = chip8->inst.NNN;
            break;

        case 0x03:
        //0x3XNN if VX == NN, skip next instruction
            if (chip8->V[chip8->inst.X] == chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;

        case 0x04:
        //0x4XNN if VX != NN, skip next instruction
            if (chip8->V[chip8->inst.X] != chip8->inst.NN) {
                chip8->PC += 2;
            }
            break;

        case 0x05:
        //0x5XY0 if VX == VY, skip next instruction
            if (chip8->inst.N != 0) break;//not 5XY0
            if (chip8->V[chip8->inst.X] == chip8->V[chip8->inst.Y]) {
                chip8->PC += 2;
            }
            break;

        case 0x06:
        // 0x6XNN: Set register VX to NN
            chip8->V[chip8->inst.X] = chip8->inst.NN;
            break;

        case 0x07:
        // 0x7XNN: set register VX += NN
            chip8->V[chip8->inst.X] += chip8->inst.NN;
            break;

        case 0x08:
            if (chip8->inst.N == 0) { 
                // 0x8XY0 Sets VX to the value of VY.
                chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x01) {
                // 0x8XY1 Sets VX to VX or VY. (bitwise OR operation)
                chip8->V[chip8->inst.X] |= chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x02) {
                // 0x8XY2 Sets VX to VX and VY. (bitwise AND operation)
                chip8->V[chip8->inst.X] &= chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x03) {
                // 0x8XY3 Sets VX to VX xor VY
                chip8->V[chip8->inst.X] ^= chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x04) {
                // 0x8XY4 Adds VY to VX. VF is set to 1 when there's an overflow, and to 0 when there is not
                if((chip8->V[chip8->inst.X]+chip8->V[chip8->inst.Y])<chip8->V[chip8->inst.X]) {
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[0xF] = 0;
                }
                chip8->V[chip8->inst.X] += chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x05) {
                // 0x8XY5 VY is subtracted from VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VX >= VY and 0 if not)
                if(chip8->V[chip8->inst.X] >= chip8->V[chip8->inst.Y]) {
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[0xF] = 0;
                }
                chip8->V[chip8->inst.X] -= chip8->V[chip8->inst.Y];
            } else if (chip8->inst.N == 0x06) {
                // 0x8XY6 Stores the least significant bit of VX in VF and then shifts VX to the right by 1
                chip8->V[0xF] = chip8->V[chip8->inst.X] & 0x1;
                chip8->V[chip8->inst.X] >>= 1;
            } else if (chip8->inst.N == 0x07) {
                // 0x8XY7 Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX)
                if(chip8->V[chip8->inst.Y] >= chip8->V[chip8->inst.X]) {
                    chip8->V[0xF] = 1;
                } else {
                    chip8->V[0xF] = 0;
                }
                chip8->V[chip8->inst.X] = chip8->V[chip8->inst.Y] - chip8->V[chip8->inst.X];
            } else if (chip8->inst.N == 0x0E) {
                // 0x8XYE Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX)
                chip8->V[0xF] = chip8->V[chip8->inst.X] >> 7;
                chip8->V[chip8->inst.X] <<= 1;
            } else {
                printf("unimplemented code, invalide opcode\n");
            }
            break;

        case 0x09:
        //0x9XY0: Skips the next instruction if VX does not equal VY. (Usually the next instruction is a jump to skip a code block)
            if (chip8->inst.N != 0) break;//not 5XY0
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
        //0xCXNN: Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN
            chip8->V[chip8->inst.X] = (rand()%256) & chip8->inst.NN;
            break;

        case 0x0D: {
        //0xDXYN: Draw N-height sprite at coords X,Y; Read from memory location I;
        // Screen pixels are XOR'd with sprite bits,
        // VF (Carry flag) is set if any screen pixels are set off; This is
        // useful for collision detection or other reasons.
            uint8_t X_coord = chip8->V[chip8->inst.X] % config.window_width;
            uint8_t Y_coord = chip8->V[chip8->inst.Y] % config.window_height;
            const uint8_t orig_X = X_coord;
            
            chip8->V[0xF] = 0;

            for (uint8_t i = 0; i < chip8->inst.N; i++) {
                //Get next byte/row of sprite data
                const uint8_t sprite_data = chip8->ram[chip8->I + i];
                X_coord = orig_X;

                for (int8_t j = 7; j >= 0; j--) {
                    // If sprite pixel/bit is on and display pixel is on, set carry flag
                    bool *pixel = &chip8->display[Y_coord*config.window_width + X_coord];
                    const bool sprite_bit = (sprite_data & (1<< j));
                    if (sprite_bit  && *pixel) {
                        chip8->V[0xF] = 1; //Set
                    }

                    *pixel ^= sprite_bit;

                    //stop drawing if hit right edge of screen
                    if(++X_coord >= config.window_width) break;
                }
                //stop drawing entire sprite if hit bottom edge of screen
                if (++Y_coord >= config.window_height) break;
            }
            break;
        }
            
        case 0x0E:
            if (chip8->inst.NN == 0x9E) {
                // 0xEX9E: Skips the next instruction if the key stored in VX is pressed (usually the next instruction is a jump to skip a code block)
                if(chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC +=2;
                }
            } else if (chip8->inst.NN == 0xA1) {
                // 0XEXA1: Skips the next instruction if the key stored in VX is not pressed (usually the next instruction is a jump to skip a code block)
                if(!chip8->keypad[chip8->V[chip8->inst.X]]) {
                    chip8->PC +=2;
                }
            } else {
                printf("erro opcode for 0x0E");
            }
            break;

        case 0x0F:
            if (chip8->inst.NN == 0x0A) {
                //0xFX0A: A key press is awaited, and then stored in VX (blocking operation, all instruction halted until next key event)
                bool temp = false;
                for(size_t i = 0; i < sizeof chip8->keypad; i++) {
                    if(chip8->keypad[i]) {
                        temp = true;
                        chip8->V[chip8->inst.X] = i;
                    }
                }
                if(!temp) {
                    chip8->PC -=2;
                }
            } else if (chip8->inst.NN == 0x1E) {
                //0xFX1E: Adds VX to I. VF is not affected
                chip8->I += chip8->V[chip8->inst.X];
            } else if (chip8->inst.NN == 0x07) {
                //0xFX07: Sets VX to the value of the delay timer
                chip8->V[chip8->inst.X] = chip8->delay_timer;
            } else if (chip8->inst.NN == 0x15) {
                //0XFX15: Sets the delay timer to VX.
                chip8->delay_timer = chip8->V[chip8->inst.X];
            } else if (chip8->inst.NN == 0x18) {
                //0XFX18: Sets the sound timer to VX.
                chip8->sound_timer = chip8->V[chip8->inst.X];
            } else if (chip8->inst.NN == 0x29) {
                //0xFX29: Sets I to the location of the sprite for the character in VX. Characters 0-F (in hexadecimal) are represented by a 4x5 font
                chip8->I = chip8->V[chip8->inst.X] * 5;
            } else if (chip8->inst.NN == 0x33) {
                // 0xFX33: Store BCD representation of VX at memory offset from I;
                    //   I = hundred's place, I+1 = ten's place, I+2 = one's place
                uint8_t numb = chip8->V[chip8->inst.X];
                chip8->ram[chip8->I+2] = numb % 10;
                numb /= 10;
                chip8->ram[chip8->I+1] = numb % 10;
                numb /= 10;
                chip8->ram[chip8->I] = numb;
            } else if (chip8->inst.NN == 0x55) {
                // 0xFX55: Stores from V0 to VX (including VX) in memory, starting at address I. The offset from I is increased by 1 for each value written, but I itself is left unmodified
                for (size_t i = 0; i <= chip8->inst.X; i++) {
                    chip8->ram[chip8->I+i] = chip8->V[i];
                }
            } else if (chip8->inst.NN == 0x65) {
                // 0xFX65: Fills from V0 to VX (including VX) with values from memory, starting at address I. The offset from I is increased by 1 for each value read, but I itself is left unmodified
                for (size_t i = 0; i <= chip8->inst.X; i++) {
                    chip8->V[i] = chip8->ram[chip8->I+i];
                }
            }
            break;

        default:
            printf("Umimplemented Opcode.\n");
            break;

    }

    return;
}


void update_timers(chip8_t *chip8) {
    if (chip8->delay_timer > 0) chip8->delay_timer--;

    if (chip8->sound_timer > 0) {
        chip8->sound_timer--;
    } else {

    }
}
int main(int argc, char **argv){
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <rom_name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
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

    chip8_t chip8 = {0};

    const char *rom_name = argv[1];
    if(!init_chip8(&chip8, rom_name)) {
        exit(EXIT_FAILURE);
    }

    clear_screen(sdl,config);//initialize screen color

    srand(time(NULL));

    //main loop
    while(chip8.state!=QUIT) {
        handle_input(&chip8);

        if (chip8.state == PAUSED) continue;

        const uint64_t before_frame = SDL_GetPerformanceCounter();
        
        for (uint32_t i = 0; i< config.insts_per_second / 60; i++) {
            emulate_instruction(&chip8,config);
        }
        
        const uint64_t after_frame = SDL_GetPerformanceCounter();

        const double time_elapsed = (double) ((after_frame - before_frame) / 1000)/ SDL_GetPerformanceFrequency();       
        SDL_Delay(16.67f > time_elapsed ? 16.67f - time_elapsed: 0);

        update_screen(sdl, config, chip8);
        update_timers(&chip8);
    }

    //FINISH AND CLEAN UP
    final_cleanup(sdl);

    exit(EXIT_SUCCESS);
    return 0;
}