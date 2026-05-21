CC = gcc
CFLAGS = -O3 -march=native -pthread -Wall -Wextra -I./src
LDFLAGS = -lm

TARGET = scoobiii-rinha
SRCS = src/main.c src/vectorizer.c src/server.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
$(CC) $(CFLAGS) -c $< -o $@

clean:
rm -f $(TARGET) $(OBJS)

run: $(TARGET)
./$(TARGET)
