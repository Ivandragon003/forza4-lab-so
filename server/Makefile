CC = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = server

all: $(TARGET)

$(TARGET): main.c game.c network.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c game.c network.c

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean