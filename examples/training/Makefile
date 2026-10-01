CC ?= cc
CFLAGS := -std=c11 -O0 -g -Wall -Wextra -fno-inline -fno-pie -fno-stack-protector
LDFLAGS := -no-pie
TARGETS := build/01_taint_flow build/02_symbolic_xor build/03_file_snapshot

.PHONY: all clean
all: $(TARGETS)

build:
	mkdir -p build

build/%: %.c | build
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -rf build
