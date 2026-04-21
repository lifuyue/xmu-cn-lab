CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

TARGET := is_in_net_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
