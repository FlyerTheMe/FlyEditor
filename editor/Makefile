CC = gcc
CFLAGS = -Wall -Wextra -g -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS = -lncursesw -ltinfo -lutil
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = editor

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)
