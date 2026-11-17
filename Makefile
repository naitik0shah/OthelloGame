CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -Iinclude
BUILD_DIR := out
CORE_SRC := src/common.c src/validation_capture.c src/cli_platform.c src/cli_render.c src/cli_commands.c src/storage.c src/stats.c src/misc.c
CORE_OBJ := $(CORE_SRC:src/%.c=$(BUILD_DIR)/%.o)
TESTS := test_move_validation test_capture test_game_storage test_command_parser test_misc

.PHONY: all clean test
all: $(BUILD_DIR)/othello

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/cli_%.o: src/cli_%.c include/common.h include/cli_internal.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.c include/common.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: src/main.c include/common.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/othello: $(CORE_OBJ) $(BUILD_DIR)/main.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%: tests/%.c $(CORE_OBJ) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: $(TESTS:%=$(BUILD_DIR)/%)
	@set -e; for t in $(TESTS); do ./$(BUILD_DIR)/$$t; done

clean:
	rm -rf $(BUILD_DIR)/*