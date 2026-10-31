# Build the ravynOS stub kernel (K1, Docs/KernelHandoff.md).
#
# A thin wrapper over build.sh, which owns the flags and the toolchain
# detection (clang + ld.lld, or gcc + GNU ld).  Keeping the logic in the shell
# script is what makes this Makefile work identically under GNU make and bmake:
# it used to spell out bmake-only automatic variables, which meant the
# documented `make` never worked on Linux.
#
#   make                 # builds ./KERNEL.ELF
#   make clean
#   make OUT=/tmp/k.elf  # alternative output path
#
# The build writes KERNEL.ELF next to main.c by default; that file is a build
# artefact (see the repository .gitignore) and is rebuilt by the pipeline.

OUT ?= KERNEL.ELF

all:
	@OUT="$(OUT)" ./build.sh

clean:
	@./build.sh --clean

test: all
	@python3 ../../tools/image/elf.py "$(OUT)" --require-load \
	    --min-vaddr 0x100000 --max-end 0x200000

.PHONY: all clean test
