CFLAGS=-std=c17 -Wall -Wextra -Werror
all:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc chip8.c -o chip8-debug $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG

# CFLAGS=-std=c17 -Wall -Wextra -Werror -D_REENTRANT
# SDLFLAGS=`sdl2-config --cflags --libs`

# all:
#     gcc chip8.c -o chip8 $(CFLAGS) $(SDLFLAGS)

# debug:
#     gcc chip8.c -o chip8-debug $(CFLAGS) $(SDLFLAGS) -DDEBUG
