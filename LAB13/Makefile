CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := 34/rip_update 35/dijkstra

.PHONY: all test clean

all: $(TARGETS)

34/rip_update: 34/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

35/dijkstra: 35/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: all
	printf '4\nN1 1 R1\nN2 0 Direct\nN3 6 R4\nN4 8 R5\n4\nN2 2 R5\nN3 3 R6\nN4 7 R7\nN5 3 R8\n' | ./34/rip_update R3
	printf '(R1,R2,2) (R1,R3,5) (R2,R3,1) (R3,R4,4) (R2,R4,7)\n' | ./35/dijkstra R1

clean:
	rm -f $(TARGETS)
