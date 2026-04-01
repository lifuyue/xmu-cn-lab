CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS ?= -pthread

TARGET := csma_cd_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
