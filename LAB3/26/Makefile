CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

BIN_DIR := bin
BUILD_DIR := build
INCLUDES := -Iinclude

COMMON_SRC := src/modulation.cpp
COMMON_OBJ := $(BUILD_DIR)/modulation.o

.PHONY: all clean test

all: $(BIN_DIR)/modulation_demo $(BIN_DIR)/test_modulation

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(COMMON_OBJ): $(COMMON_SRC) include/modulation.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN_DIR)/modulation_demo: src/modulation_cli.cpp $(COMMON_OBJ) include/modulation.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

$(BIN_DIR)/test_modulation: tests/test_modulation.cpp $(COMMON_OBJ) include/modulation.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

test: all
	./$(BIN_DIR)/test_modulation

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
