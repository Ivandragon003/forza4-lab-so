CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11
TARGET = server
SRCS = main.c game.c network.c session.c commands.c game_queries.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
