#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <SDL2/SDL.h>

#include "chippy.h"

uint8_t V[16]; 
uint8_t main_memory[4097];
uint8_t screen_buffer[32][64];
uint8_t key_states[16];
uint16_t call_stack[256];

const uint8_t fontset[80] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,
        0x20, 0x60, 0x20, 0x20, 0x70,
        0xF0, 0x10, 0xF0, 0x80, 0xF0,
        0xF0, 0x10, 0xF0, 0x10, 0xF0,
        0x90, 0x90, 0xF0, 0x10, 0x10,
        0xF0, 0x80, 0xF0, 0x10, 0xF0,
        0xF0, 0x80, 0xF0, 0x90, 0xF0,
        0xF0, 0x10, 0x20, 0x40, 0x40,
        0xF0, 0x90, 0xF0, 0x90, 0xF0,
        0xF0, 0x90, 0xF0, 0x10, 0xF0,
        0xF0, 0x90, 0xF0, 0x90, 0x90,
        0xE0, 0x90, 0xE0, 0x90, 0xE0,
        0xF0, 0x80, 0x80, 0x80, 0xF0,
        0xE0, 0x90, 0x90, 0x90, 0xE0,
        0xF0, 0x80, 0xF0, 0x80, 0xF0,
        0xF0, 0x80, 0xF0, 0x80, 0x80
};

uint8_t stack_pointer;
uint16_t I, current_instruction, program_counter = 512;
uint8_t delay_timer, sound_timer;

const char *romname;
char window_title[64];

SDL_Window   *window;
SDL_Renderer *renderer;

int16_t audio_buffer[376550];
SDL_AudioDeviceID audio_device;
SDL_AudioSpec     audio_spec;

SDL_Event event;

//
// deinit - cleanly exits the system
//
void deinit(void) {
        fputs("cleaning up..        ", stdout);

        if (renderer)
                SDL_DestroyRenderer(renderer);

        if (window)
                SDL_DestroyWindow(window);

        if (audio_device)
                SDL_CloseAudioDevice(audio_device);

        SDL_Quit();
        puts("OK");
}

//
// panic - handles errors and causes a clean exit
//
void panic(ExitCode err) {
        fputs("Panicking: ", stderr);
        switch (err) {
                case USER_EXIT:
                        fprintf(stderr, "recieved SIGTERM\n");
                        break;
                case ERR_LOAD_ROM_FAIL:
                        fprintf(stderr, "failed to load ROM: `%s`.\n", romname);
                        break;
                case ERR_SDL_FAIL:
                        fprintf(stderr, "SDL2 error: %s\n", SDL_GetError());
                        break;
                case ERR_ILLEGAL_INSTRUCTION:
                        fprintf(stderr, "illegal instruction: `0x%X` at: 0x%X\n",
                                current_instruction, program_counter);
                        break;
                case ERR_INVALID_ARGUMENT:
                        fprintf(stderr, "invalid X argument `0x%X` to instruction: `0x%X` at: 0x%X\n",
                                X, current_instruction, program_counter);
                        break;
                case ERR_STACK_OVERFLOW:
                        fputs("stack overflow\n", stderr);
                        break;
                case ERR_INVALID_RETURN:
                        fprintf(stderr, "0x00EE (ret) instruction at 0x%X but no subroutine is being executed\n",
                                program_counter);
                        break;
                case ERR_INVALID_SPRITE:
                        fprintf(stderr,
                                "drawing %d byte sprite stored at 0x%X involves reading beyond main memory\n", N, I);

                // NO DEFAULT
        }

        deinit();
        exit(err);
}

//
// init - initialises internal values and SDL2 objects
//
void init(void) {
        // copy fontset to main_memory
        memcpy(main_memory, fontset, 80);

        // set window title
        snprintf(window_title, 64, "Chippy - %s", romname);

        srand(time(NULL));

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_TIMER))
                panic(ERR_SDL_FAIL);

        audio_spec = (SDL_AudioSpec){
                .freq = 44300,
                .format = AUDIO_S16SYS,
                .channels = 1,
                .samples = 1024,
                .callback = NULL
        };

        if ((audio_device = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0))) {
                // fill audio_buffer with 4.2 seconds of audio
                float x = 0;
                for (int i = 0; i < 186060; ++i, x += 0.01)
                        audio_buffer[i] = sin(x * 4) * 8000;

                SDL_PauseAudioDevice(audio_device, 0);
        } else
                fprintf(stderr, "%s\nfailed to open audio device, continuing with no audio..\n", SDL_GetError());

        window   = SDL_CreateWindow(window_title, 100, 100, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

        if (!(window && renderer))
                panic(ERR_SDL_FAIL);

        SDL_ShowCursor(SDL_DISABLE);

        // draw black background
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
}

//
// load_rom - loads ROM from disk
//
void load_rom(void) {
        fputs("loading ROM..        ", stdout);

        FILE *ROM;
        if (!((ROM = fopen(romname, "rb")) && fread(main_memory + 512, 1, 4096 - 512, ROM))) {
                fclose(ROM);
                panic(ERR_LOAD_ROM_FAIL);
        }
        fclose(ROM);
        
        puts("OK");
}

//
// draw_sprite - writes to the screen buffer a sprite nbytes in size at position (col, row)
//
static void draw_sprite(uint8_t col, uint8_t row, uint8_t nbytes) {
        // zero the collision flag
        V[0xF] = 0;

        uint8_t bitstate;
        for (int i = 0; i < nbytes; ++i, ++row) {
                for (int bit = 7; bit >= 0; --bit, ++col) {
                        bitstate = (main_memory[I + i] >> bit) & 1;
                        if (bitstate && screen_buffer[row % 32][col % 64])
                                V[0xF] = 1;
                        screen_buffer[row % 32][col % 64] ^= bitstate;
                }
                col -= 8;
        }
}

//
// handle_keyevent - sets the appropriate key_states index for a key press/release
//
int handle_keyevent(SDL_Event event) {
        int key;
        switch (event.key.keysym.sym) {
                case SDLK_1:
                        key = 0x1;
                        break;
                case SDLK_2:
                        key = 0x2;
                        break;
                case SDLK_3:
                        key = 0x3;
                        break;
                case SDLK_4:
                        key = 0xC;
                        break;
                case SDLK_q:
                        key = 0x4;
                        break;
                case SDLK_w:
                        key = 0x5;
                        break;
                case SDLK_e:
                        key = 0x6;
                        break;
                case SDLK_r:
                        key = 0xD;
                        break;
                case SDLK_a:
                        key = 0x7;
                        break;
                case SDLK_s:
                        key = 0x8;
                        break;
                case SDLK_d:
                        key = 0x9;
                        break;
                case SDLK_f:
                        key = 0xE;
                        break;
                case SDLK_z:
                        key = 0xA;
                        break;
                case SDLK_x:
                        key = 0x0;
                        break;
                case SDLK_c:
                        key = 0xB;
                        break;
                case SDLK_v:
                        key = 0xF;
                        break;

                default:
                        return -1;
        }

        key_states[key] = (event.type == SDL_KEYDOWN);
        return key;
}

//
// wait_for_keypress - halt the system until a key is pressed, store key in register
//
static int wait_for_keypress(void) {
        while (SDL_WaitEvent(&event)) {
                switch (event.type) {
                        case SDL_QUIT:
                                panic(USER_EXIT);
                        case SDL_KEYDOWN: {
                                int key_pressed = handle_keyevent(event);
                                if (key_pressed != -1)
                                        return key_pressed;
                        }
                }
        }

        panic(ERR_SDL_FAIL);
}

//
// update_screen - draws pixels read from screen_buffer to the screen
//
static void update_screen(void) {
        for (int row = 0; row < 32; ++row)
                for (int col = 0; col < 64; ++col) {
                        SDL_SetRenderDrawColor(renderer, 0, (screen_buffer[row][col]) ? 255 : 0, 0, 255);
                        SDL_RenderFillRect(renderer,
                                &(SDL_Rect){col * PIXEL_WIDTH, row * PIXEL_HEIGHT, PIXEL_WIDTH, PIXEL_HEIGHT});
                }
        SDL_RenderPresent(renderer);
}

//
// cycle - performs an emulator cycle
//
void cycle(void) {
        SDL_Delay(1000 / CLOCK_RATE);

        // fetch next instruction
        current_instruction = (main_memory[program_counter] << 8) | main_memory[program_counter + 1];

        // decode and execute fetched instruction
        switch (current_instruction & 0xF000) {
                case 0x0000:
                        switch (NN) {
#ifdef SUPERCHIP
                                case 0xC0:        // 0x00CN XXX
                                        break;
#endif
                                case 0xE0:        // 0x00E0
                                        memset(screen_buffer, 0, 64 * 32);
                                        update_screen();

                                        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                                        SDL_RenderClear(renderer);
                                        SDL_RenderPresent(renderer);
                                        break;
                                case 0xEE:        // 0x00EE
                                        if (stack_pointer == 0)
                                                panic(ERR_INVALID_RETURN);
                                        program_counter = call_stack[stack_pointer--];
                                        return;

                                default:
                                        if (current_instruction == 0x00000000) {
                                                deinit();
                                                exit(SUCCESS);
                                        }

                                        panic(ERR_ILLEGAL_INSTRUCTION);
                        }
                        break;
#ifdef SUPERCHIP
                case 0xF0:  
                        switch (N) {
                                case 0xB:        // 0x00FB XXX
                                        break;                 
                                case 0xC:        // 0x00FC XXX
                                        break;                 
                                case 0xE:        // 0x00FE XXX
                                        break;                 
                                case 0xF:        // 0x00FF XXX
                                        break;

                                default:
                                        panic(ERR_ILLEGAL_INSTRUCTION);
                        }
                        break;
#endif
                case 0x1000:        // 0x1NNN
                        program_counter = NNN;
                        return;
                case 0x2000:        // 0x2NNN
                        if (stack_pointer == 255)
                                panic(ERR_STACK_OVERFLOW);

                        call_stack[++stack_pointer] = program_counter + 2;
                        program_counter = NNN;
                        return;
                case 0x3000:        // 0x3XNN
                        if (V[X] == NN)
                                program_counter += 2;
                        break;
                case 0x4000:        // 0x4XNN
                        if (V[X] != NN)
                                program_counter += 2;
                        break;
                case 0x5000:        // 0x5XY0
                        if (V[X] == V[Y])
                                program_counter += 2;
                        break;
                case 0x6000:        // 0x6XNN
                        V[X] = NN;
                        break;
                case 0x7000:        // 0x7XNN
                        V[X] += NN;
                        break;
                case 0x8000:
                        switch (N) {
                                case 0x0:        // 0x8XY0
                                        V[X] = V[Y];
                                        break;
                                case 0x1:        // 0x8XY1
                                        V[X] |= V[Y];
                                        break;
                                case 0x2:        // 0x8XY2
                                        V[X] &= V[Y];
                                        break;
                                case 0x3:        // 0x8XY3
                                        V[X] ^= V[Y];
                                        break;
                                case 0x4:        // 0x8XY4
                                        V[0xF] = (V[X] + V[Y]) > 255;
                                        V[X] += V[Y];
                                        break;
                                case 0x5:        // 0x8XY5
                                        V[0xF] = (V[X] > V[Y]);
                                        V[X] -= V[Y];
                                        break;
                                case 0x6:        // 0x8YX6
                                        V[0xF] = (V[X] & 1);
                                        V[X] >>= 1;
                                        break;
                                case 0x7:        // 0x8XY7
                                        V[0xF] = (V[X] < V[Y]);
                                        V[X] = V[Y] - V[X];
                                        break;
                                case 0xE:        // 0x8XYE
                                        V[0xF] = (V[X] >> 7);
                                        V[X] <<= 1;
                                        break;

                                default:
                                        panic(ERR_ILLEGAL_INSTRUCTION);
                        }
                        break;
                case 0x9000:        // 0x9XY0
                        if (V[X] != V[Y])
                                program_counter += 2;
                        break;
                case 0xA000:        // 0xANNN
                        I = NNN;
                        break;
                case 0xB000:        // 0xBNNN
                        program_counter = V[0] + NNN;
                        return;
                case 0xC000:        // 0xCXNN
                        V[X] = rand() & NN;
                        break;
                case 0xD000:        // 0xDXYN
                        if (I <= 4096 - N) {
                                draw_sprite(V[X], V[Y], N);
                                update_screen();
                        } else
                                panic(ERR_INVALID_SPRITE);
                        break;
                case 0xE000:
                        switch(N) {
                                case 0xE:       // 0xEX9E
                                        if (key_states[V[X]])
                                                program_counter += 2;
                                        break;
                                case 0x1:       // 0xExA1
                                        if (!key_states[V[X]])
                                                program_counter += 2;
                                        break;

                                default:
                                        panic(ERR_ILLEGAL_INSTRUCTION);
                        }
                        break;
                case 0xF000:
                        switch(N) {
                                case 0x7:       // 0xFX07
                                        V[X] = delay_timer;
                                        break;
                                case 0xA:       // 0xFX0A
                                        V[X] = wait_for_keypress();
                                        break;
                                case 0x8:       // 0xFX18
                                        sound_timer = V[X];
                                        break;
                                case 0xE:       // 0xFX1E
                                        V[0xF] = I + V[X] > 0xFFF;
                                        I += V[X];
                                        break;
                                case 0x9:       // 0xFX29
                                        if (X <= 0xF)
                                                I = V[X] * 5;
                                        else
                                                panic(ERR_INVALID_ARGUMENT);
                                        break;
#ifdef SUPERCHIP
                                case 0x0:       // 0xFX30 XXX
                                        break;
#endif
                                case 0x3:       // 0xFX33
                                        main_memory[I]     = V[X] / 100;
                                        main_memory[I + 1] = V[X] % 100 / 10;
                                        main_memory[I + 2] = V[X] % 10;
                                        break;
                                case 0x5:
                                        switch(Y) {
                                                case 0x1:       // 0xFX15
                                                        delay_timer = V[X];
                                                        break;
                                                case 0x5:       // 0xFX55
                                                        for (int i = 0; i <= X; ++i)
                                                                main_memory[I + i] = V[i];
                                                        break;
                                                case 0x6:       // 0xFX65
                                                        for (int i = 0; i <= X; ++i)
                                                                V[i] = main_memory[I + i];
                                                        break;
#ifdef SUPERCHIP
                                                case 0x7:       // 0xFX75 XXX
                                                        break;
                                                case 0x8:       // 0xFX85 XXX
                                                        break;
#endif

                                                default:
                                                        panic(ERR_ILLEGAL_INSTRUCTION);
                                        }
                                        break;

                                default:
                                        panic(ERR_ILLEGAL_INSTRUCTION);
                        }
                        break;

                default:
                        panic(ERR_ILLEGAL_INSTRUCTION);
        }
        
        program_counter += 2;
}

//
// tick - updates timers, play audio while sound_timer > 0
//
void tick(void) {
        while (delay_timer > 0) {
                --delay_timer;
                SDL_Delay(16);
        }

        if (sound_timer > 0) {
                if (audio_device && SDL_GetQueuedAudioSize(audio_device) == 0)
                        SDL_QueueAudio(audio_device, audio_buffer, (float)sound_timer / 60 * 88600);

                --sound_timer;
        }
}
