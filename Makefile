# Liberal_OS — top-level wrapper Makefile.
#
# Delegates to xv6-src/Makefile for xv6 build/run targets and exposes the
# harness commands documented in CLAUDE.md §5.

XV6_DIR := xv6-src

.PHONY: all build qemu qemu-gdb autotest regression clean

all: build

build:
	$(MAKE) -C $(XV6_DIR)

qemu:
	$(MAKE) -C $(XV6_DIR) qemu

qemu-gdb:
	$(MAKE) -C $(XV6_DIR) qemu-gdb

autotest:
	bash tests/autotest.sh

regression:
	bash tests/regression.sh

clean:
	$(MAKE) -C $(XV6_DIR) clean
