CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

TARGET := ethernet_header_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
