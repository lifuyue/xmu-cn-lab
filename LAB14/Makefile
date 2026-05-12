CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

TARGETS := 16/multicast_sender 16/multicast_receiver 17/file_transfer

.PHONY: all test clean

all: $(TARGETS)

16/multicast_sender: 16/multicast_sender.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

16/multicast_receiver: 16/multicast_receiver.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

17/file_transfer: 17/file_transfer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

test: all
	dd if=/dev/zero of=/tmp/lab14_test_input.bin bs=1024 count=256 >/dev/null 2>&1
	./17/file_transfer server tcp 39017 /tmp/lab14_tcp_out.bin > /tmp/lab14_tcp_server.log & \
		server_pid=$$!; sleep 1; \
		./17/file_transfer client tcp 127.0.0.1 39017 /tmp/lab14_test_input.bin; \
		wait $$server_pid
	cmp /tmp/lab14_test_input.bin /tmp/lab14_tcp_out.bin
	rm -f /tmp/lab14_test_input.bin /tmp/lab14_tcp_out.bin /tmp/lab14_tcp_server.log

clean:
	rm -f $(TARGETS)
