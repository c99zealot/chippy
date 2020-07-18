#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "chippy.h"

int main(int argc, char **argv) {
        if (argc < 2) {
                fprintf(stderr, "usage: %s <path to chip8 rom>\n", argv[0]);
                exit(ERR_TOO_FEW_ARGS);
        } 

        romname = argv[1];

        init();
        load_rom();

        SDL_Event event;
        while (program_counter <= 4095) {
                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                                case SDL_QUIT:
                                        panic(USER_EXIT);
                                case SDL_KEYDOWN:    // FALLTHROUGH
                                case SDL_KEYUP:
                                        handle_keyevent(event);

                                // NO DEFAULT
                        }
                }

                cycle();
                tick();

                SDL_PumpEvents();
        }
        deinit();

	return SUCCESS;
}
