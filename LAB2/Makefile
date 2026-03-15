CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

BIN_DIR := bin
BUILD_DIR := build
INCLUDES := -Iinclude

COMMON_SRC := src/rs232c.cpp
COMMON_OBJ := $(BUILD_DIR)/rs232c.o

.PHONY: all clean test

# 一次性构建 LAB2 的演示程序和测试程序。
all: $(BIN_DIR)/rs232c_demo $(BIN_DIR)/test_rs232c

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 先编译公共协议实现，供其他可执行文件复用。
$(COMMON_OBJ): $(COMMON_SRC) include/rs232c.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 构建命令行演示程序。
$(BIN_DIR)/rs232c_demo: src/rs232c_cli.cpp $(COMMON_OBJ) include/rs232c.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

# 构建自动化测试程序。
$(BIN_DIR)/test_rs232c: tests/test_rs232c.cpp $(COMMON_OBJ) include/rs232c.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@

# 先构建，再运行全部测试。
test: all
	./$(BIN_DIR)/test_rs232c

# 删除构建产物，让目录回到源码状态。
clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)
