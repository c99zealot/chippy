#ifndef CHIPPY_H_INCLUDED
        #define CHIPPY_H_INCLUDED 1

        #include <stdint.h>

        #include <SDL2/SDL.h>

        #define X ((current_instruction & 0x0f00) >> 8)
        #define Y ((current_instruction & 0x00f0) >> 4)

        #define N   (current_instruction & 0x000f)
        #define NN  (current_instruction & 0x00ff)
        #define NNN (current_instruction & 0x0fff)

        enum {
                CLOCK_RATE    = 1000,
                WINDOW_WIDTH  = 640,
                WINDOW_HEIGHT = 320,
                PIXEL_WIDTH   = WINDOW_WIDTH  / 64,
                PIXEL_HEIGHT  = WINDOW_HEIGHT / 32
        };

        typedef enum {
                SUCCESS,
                USER_EXIT,

                ERR_TOO_FEW_ARGS,
                ERR_SDL_FAIL,
                ERR_LOAD_ROM_FAIL,
                ERR_ILLEGAL_INSTRUCTION,
                ERR_INVALID_ARGUMENT,
                ERR_INVALID_RETURN,
                ERR_STACK_OVERFLOW,
                ERR_INVALID_SPRITE
        } ExitCode;

        extern void deinit(void);
        extern void panic(ExitCode err);
        extern void init(void);
        extern void load_rom(void);
        extern void cycle(void);
        extern void tick(void);

        extern SDL_Event event;
        extern int handle_keyevent(SDL_Event event);

        extern const char *romname;
        extern uint16_t program_counter;
#endif
