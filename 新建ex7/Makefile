CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := license_server software_a

.PHONY: all clean

all: $(TARGETS)

license_server: src/license_server.cpp src/net.hpp
	$(CXX) $(CXXFLAGS) src/license_server.cpp -o $@

software_a: src/software_a.cpp src/net.hpp
	$(CXX) $(CXXFLAGS) src/software_a.cpp -o $@

clean:
	rm -f $(TARGETS)
