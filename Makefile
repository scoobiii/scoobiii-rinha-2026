CC      = clang
CFLAGS  = -O3 -march=native -Wall -Wextra -std=c11 \
          -ffast-math -funroll-loops \
          -Isrc
LDFLAGS = -lm -lpthread -lz

BIN = rinha-api

SRC = src/main.c src/ivf.c
OBJ = $(SRC:.c=.o)

all: $(BIN) preprocess

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

preprocess: tools/preprocess.c src/ivf.c src/ivf.h src/vectorizer.h
	$(CC) $(CFLAGS) -Isrc -o $@ tools/preprocess.c src/ivf.c $(LDFLAGS)

src/%.o: src/%.c src/ivf.h src/vectorizer.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(BIN) preprocess

.PHONY: all clean
