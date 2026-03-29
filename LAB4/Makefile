CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

BIN_DIR := bin
BUILD_DIR := build
INCLUDES := -Iinclude

COMMON_SRC := src/parity_check.cpp
COMMON_OBJ := $(BUILD_DIR)/parity_check.o

.PHONY: all clean test

all: $(BIN_DIR)/parity_check_demo $(BIN_DIR)/test_parity_check

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(COMMON_OBJ): $(COMMON_SRC) include/parity_check.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN_DIR)/parity_check_demo: src/parity_check_cli.cpp $(COMMON_OBJ) include/parity_check.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

$(BIN_DIR)/test_parity_check: tests/test_parity_check.cpp $(COMMON_OBJ) include/parity_check.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

test: all
	./$(BIN_DIR)/test_parity_check

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
