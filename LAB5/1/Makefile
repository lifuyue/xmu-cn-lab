CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGET := mac_address_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
