CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGET := fragment_packet_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
