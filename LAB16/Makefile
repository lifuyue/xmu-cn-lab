CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := 17/email_verify_demo

.PHONY: all test clean

all: $(TARGETS)

17/email_verify_demo: 17/main.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: all
	./17/email_verify_demo 8080

clean:
	rm -f $(TARGETS)
