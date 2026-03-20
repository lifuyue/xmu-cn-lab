CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

BIN_DIR := bin
BUILD_DIR := build
INCLUDES := -Iinclude

COMMON_SRC := src/multiplex.cpp
COMMON_OBJ := $(BUILD_DIR)/multiplex.o

.PHONY: all clean test

all: $(BIN_DIR)/multiplex_demo $(BIN_DIR)/test_multiplex

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(COMMON_OBJ): $(COMMON_SRC) include/multiplex.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BIN_DIR)/multiplex_demo: src/multiplex_cli.cpp $(COMMON_OBJ) include/multiplex.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

$(BIN_DIR)/test_multiplex: tests/test_multiplex.cpp $(COMMON_OBJ) include/multiplex.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

test: all
	./$(BIN_DIR)/test_multiplex

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
