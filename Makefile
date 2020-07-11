CC=cc
CFLAGS=-std=c99 -lSDL2 -lm

chippy: src/main.c src/chippy.c src/chippy.h
	@$(CC) $(CFLAGS) -o chippy src/*.c

install:
	@install -s chippy /bin/chippy

clean:
	@rm chippy

uninstall:
	@rm bin/chippy
