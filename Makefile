# Makefile universal – roda em ARM64 (Termux) e x86_64 (Linux/Docker)
CC       = gcc
BASE_CFLAGS = -Wall -Wextra -std=c11 -ffast-math -funroll-loops -Isrc
LDFLAGS  = -lm -lpthread

# Detecta arquitetura
ARCH := $(shell uname -m)
ifeq ($(ARCH),aarch64)
    # ARM64 (Termux, Apple Silicon, Raspberry Pi)
    CFLAGS = $(BASE_CFLAGS) -O3 -march=armv8.2-a+fp16+rcpc+dotprod -DARM_NEON
else ifeq ($(ARCH),armv7l)
    # ARM32 (mais raro)
    CFLAGS = $(BASE_CFLAGS) -O3 -march=armv7-a+neon -mfpu=neon -DARM_NEON
else ifeq ($(ARCH),x86_64)
    # x86_64 (servidores, Docker, WSL)
    HAS_AVX2 := $(shell grep -q avx2 /proc/cpuinfo && echo 1 || echo 0)
    ifeq ($(HAS_AVX2),1)
        CFLAGS = $(BASE_CFLAGS) -O3 -march=native -mavx2 -mfma -DAVX2
    else
        CFLAGS = $(BASE_CFLAGS) -O3 -march=x86-64-v2
    endif
else
    $(warning Arquitetura desconhecida, usando flags genéricas)
    CFLAGS = $(BASE_CFLAGS) -O3
endif

BIN = scoobiii-rinha
SRC = src/main.c src/ivf.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean help

all: $(BIN) preprocess

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

preprocess: tools/preprocess.c src/ivf.h src/vectorizer.h
	$(CC) $(CFLAGS) -Isrc -o $@ tools/preprocess.c src/ivf.c $(LDFLAGS) -lz

src/%.o: src/%.c src/ivf.h src/vectorizer.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(BIN) preprocess

help:
	@echo "Uso: make [target]"
	@echo ""
	@echo "Targets disponíveis:"
	@echo "  all        Compila o binário principal e o preprocess (padrão)"
	@echo "  preprocess Compila apenas a ferramenta de geração do índice IVF"
	@echo "  clean      Remove objetos e binários"
	@echo "  help       Exibe esta mensagem"
	@echo ""
	@echo "Arquitetura detectada: $(ARCH)"
	@echo "Flags de compilação: $(CFLAGS)"
	@echo ""
	@echo "Para gerar o índice IVF com 1024 clusters:"
	@echo "  ./preprocess resources/references.json.gz data/ivf.bin 1024"
	@echo ""
	@echo "Para executar o servidor (Termux/Linux):"
	@echo "  taskset -c 4-7 ./$(BIN) /data/data/com.termux/files/usr/tmp/rinha.sock data/ivf.bin"
