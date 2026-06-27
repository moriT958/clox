CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -MMD -MP

BUILD_DIR = build
TARGET = clox
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
