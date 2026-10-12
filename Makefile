CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -g -O2
LDFLAGS ?=

SRC = src/main.c \
      src/terminal.c \
      src/input.c \
      src/buffer.c \
      src/cursor.c \
      src/leap.c \
      src/render.c \
      src/editor.c

OBJ = $(SRC:.c=.o)
TARGET = memo

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET) $(LDFLAGS)

TEST_SRC = tests/test_all.c src/buffer.c src/cursor.c src/leap.c src/render.c src/terminal.c src/input.c src/editor.c
TEST_TARGET = test_runner

test: $(TEST_SRC)
	$(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) $(LDFLAGS)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET)
