CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

TARGETS := icmp_time_client icmp_time_server test_icmp_time

.PHONY: all test clean

all: $(TARGETS)

icmp_time_client: icmp_time_client.c icmp_time_common.c icmp_time_common.h
	$(CC) $(CFLAGS) icmp_time_client.c icmp_time_common.c -o $@

icmp_time_server: icmp_time_server.c icmp_time_common.c icmp_time_common.h
	$(CC) $(CFLAGS) icmp_time_server.c icmp_time_common.c -o $@

test_icmp_time: test_icmp_time.c icmp_time_common.c icmp_time_common.h
	$(CC) $(CFLAGS) test_icmp_time.c icmp_time_common.c -o $@

test: test_icmp_time
	./test_icmp_time

clean:
	rm -f $(TARGETS)
