CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := 25/nslookup_flow

.PHONY: all test clean

all: $(TARGETS)

25/nslookup_flow: 25/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: all
	./25/nslookup_flow www.xmu.edu.cn A

clean:
	rm -f $(TARGETS)
