# === Project configuration ===
TARGET := myproject
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

CC := gcc
CFLAGS := -Wall -Wextra -std=c17 -I$(INC_DIR) -Itests/unity
LDLIBS := -lm

# === Collect all source files ===
SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_SRCS := $(wildcard tests/*.c)
LIB_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))

# === Default target ===
$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

# === Compile each .c into .o ===
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# === Convenience targets ===
.PHONY: run clean test

run: $(BUILD_DIR)/$(TARGET)
	./$(BUILD_DIR)/$(TARGET)

clean:
	rm -rf $(BUILD_DIR)


test: $(BUILD_DIR)/runTests
	./$(BUILD_DIR)/runTests

$(BUILD_DIR)/runTests: $(TEST_SRCS) $(LIB_SRCS) tests/unity/unity.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
