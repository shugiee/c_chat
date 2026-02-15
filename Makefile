# === Project configuration ===
CLIENT := chat-client
SERVER := chat-server
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build

CC := gcc
CFLAGS := -Wall -Wextra -std=c23 -I$(INC_DIR) -Itests/unity
LDLIBS := -lm
CLIENT_LDLIBS := $(LDLIBS) -lncurses
SERVER_LDLIBS := $(LDLIBS) -lpq

# === Collect all source files ===
SRCS := $(shell find $(SRC_DIR) -name '*.c')
CLIENT_MAIN := $(SRC_DIR)/client_main.c
SERVER_MAIN := $(SRC_DIR)/server_main.c
MAIN_SRCS := $(CLIENT_MAIN) $(SERVER_MAIN)
COMMON_SRCS := $(filter-out $(MAIN_SRCS),$(SRCS))
COMMON_OBJS := $(COMMON_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJ := $(CLIENT_MAIN:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
SERVER_OBJ := $(SERVER_MAIN:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_SRCS := $(wildcard tests/*.c)

# === Compile targets ===
all: $(BUILD_DIR)/$(CLIENT) $(BUILD_DIR)/$(SERVER)

$(BUILD_DIR)/$(CLIENT): $(CLIENT_OBJ) $(COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(CLIENT_LDLIBS)

$(BUILD_DIR)/$(SERVER): $(SERVER_OBJ) $(COMMON_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(SERVER_LDLIBS)

# === Compile each .c into .o ===
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# === Convenience targets ===
.PHONY: all run run-server run-client clean test run-db stop-db

run-server: $(BUILD_DIR)/$(SERVER)
	./$(BUILD_DIR)/$(SERVER)

run-client: $(BUILD_DIR)/$(CLIENT)
	./$(BUILD_DIR)/$(CLIENT)

run-db:
	docker compose -f db/docker-compose.yml up -d

stop-db:
	docker compose -f db/docker-compose.yml down

clean:
	rm -rf $(BUILD_DIR)


test: $(BUILD_DIR)/runTests
	./$(BUILD_DIR)/runTests

$(BUILD_DIR)/runTests: $(TEST_SRCS) $(COMMON_OBJS) tests/unity/unity.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
