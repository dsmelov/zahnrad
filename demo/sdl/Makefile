# Install
BIN = demo

# Compiler
CC = clang
DCC = gcc

# Flags
CFLAGS = -std=c89 -pedantic -O2

SRC = ../../zahnrad.c sdl.c
OBJ = $(SRC:.c=.o)

ifeq ($(OS),Windows_NT)
BIN := $(BIN).exe
LIBS = -lmingw32 -lSDL2main -lSDL2 -lopengl32 -lm -lGLU32 -lGLEW32
else
LIBS = -lSDL2 -lGL -lm -lGLU -lGLEW
endif

# Modes
.PHONY: clang
clang: CC = clang
clang: $(BIN)

.PHONY: gcc
gcc: CC = gcc
gcc: $(BIN)

$(BIN):
	@mkdir -p bin
	rm -f bin/$(BIN) $(OBJS)
	$(CC) $(SRC) $(CFLAGS) -o bin/$(BIN) $(LIBS)
