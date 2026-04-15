CC = gcc
CFLAGS = -std=c99 -Wall -D_XOPEN_SOURCE=700
TARGET = slosh
SRC = slosh_skeleton.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
