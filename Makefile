CC=cc
CFLAGS=-std=c99 -lSDL2 -lm

chippy: src/main.c src/chippy.c src/chippy.h
	@$(CC) $(CFLAGS) -DDEBUG -o chippy src/*.c

clean:
	@rm chippy
