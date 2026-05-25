CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := 15/ftp_mode_demo

.PHONY: all test clean

all: $(TARGETS)

15/ftp_mode_demo: 15/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: all
	./15/ftp_mode_demo

clean:
	rm -f $(TARGETS)
