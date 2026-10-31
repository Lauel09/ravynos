# Build the ravynOS boot shell (SHELL.EFI).
#
# A thin wrapper over build.sh, which owns the flags and the toolchain
# detection (clang + lld-link, or a zig binary).  Keeping the flags out of the
# Makefile is what makes `make` work under GNU make and bmake alike.
#
#   make                 # builds ./SHELL.EFI
#   make clean

OUT ?= SHELL.EFI

all:
	@OUT="$(OUT)" ./build.sh

clean:
	@./build.sh --clean

.PHONY: all clean
