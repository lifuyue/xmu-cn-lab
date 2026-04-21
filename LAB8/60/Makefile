CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

TARGET := classwise_demo

.PHONY: all clean

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)
