CFLAGS=-std=c17 -Wall -Wextra -Werror
all:
	gcc chip8.c -o chip8 $(CFLAGS) `sdl2-config --cflags --libs`

debug:
	gcc chip8.c -o chip8-debug $(CFLAGS) `sdl2-config --cflags --libs` -g -DDEBUG

old:
	gcc old_chip8.c -o old $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG

edwin:
	gcc edwin.c -o edwin $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG

author:
	gcc author.c -o author $(CFLAGS) `sdl2-config --cflags --libs` -DDEBUG
