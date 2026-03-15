CXX ?= clang++
PKG_CONFIG ?= pkg-config

# 这个实验项目使用的基础编译选项。
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
OPENCV_CFLAGS := $(shell $(PKG_CONFIG) --cflags opencv4 2>/dev/null)
OPENCV_LIBDIR := $(shell $(PKG_CONFIG) --variable=libdir opencv4 2>/dev/null)
# 只链接本实验真正用到的 OpenCV 模块，避免不必要的依赖。
OPENCV_LIBS := -L$(OPENCV_LIBDIR) -lopencv_core -lopencv_imgcodecs -lopencv_highgui -lopencv_videoio -lopencv_imgproc

BIN_DIR := bin
BUILD_DIR := build
INCLUDES := -Iinclude $(OPENCV_CFLAGS)
LDFLAGS := $(OPENCV_LIBS)

COMMON_SRC := src/color_channel.cpp
COMMON_OBJ := $(BUILD_DIR)/color_channel.o

.PHONY: all clean test check-opencv

# 一次性构建实验所需的全部可执行文件。
all: check-opencv $(BIN_DIR)/sender $(BIN_DIR)/receiver $(BIN_DIR)/test_roundtrip

# 如果 pkg-config 找不到 OpenCV，就尽早停止并给出提示。
check-opencv:
	@if ! $(PKG_CONFIG) --exists opencv4; then \
		echo "OpenCV 4 was not found. Install it first, for example: brew install opencv"; \
		exit 1; \
	fi

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# 先编译公共协议实现，后续多个可执行文件都复用这个目标文件。
$(COMMON_OBJ): $(COMMON_SRC) include/color_channel.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 构建发送端程序。
$(BIN_DIR)/sender: src/sender.cpp $(COMMON_OBJ) include/color_channel.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@ $(LDFLAGS)

# 构建接收端程序。
$(BIN_DIR)/receiver: src/receiver.cpp $(COMMON_OBJ) include/color_channel.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@ $(LDFLAGS)

# 构建自动化 round-trip 测试程序。
$(BIN_DIR)/test_roundtrip: tests/test_roundtrip.cpp $(COMMON_OBJ) include/color_channel.hpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< $(COMMON_OBJ) -o $@ $(LDFLAGS)

# 先完成构建，再执行基于文件模拟的自动化测试。
test: all
	./$(BIN_DIR)/test_roundtrip

# 清理构建产物和测试生成文件，让目录回到仅源码状态。
clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR) output
