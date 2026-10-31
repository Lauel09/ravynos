# Build the ravynOS stub kernel (K1, Docs/KernelHandoff.md).
#
# Freestanding x86-64 ELF linked at 1 MiB with host clang + lld. No libc,
# no stdlib, no relocations: the loader copies PT_LOAD segments verbatim
# and jumps to e_entry with paging still on (firmware page tables).
#
# Works with both GNU make and bmake.  CC/LD are assigned (not ?=) because
# both drivers predefine CC=cc and LD=ld; a plain `make` would otherwise use
# Apple's linker, which does not understand the ELF flags below.  Command-line
# overrides still win: `make CC=... LD=...`.

PROG =  KERNEL.ELF
OBJ =   main.o
SRCS =  main.c

CC = clang
LD = ld.lld

CFLAGS += -target x86_64-unknown-none \
          -ffreestanding -fno-stack-protector -fno-builtin -nostdlib \
          -mno-red-zone -mno-sse -mcmodel=kernel \
          -O2 -Wall -Wextra -Wno-unused-parameter

LDFLAGS += -m elf_x86_64 -N --image-base=0x100000 \
           -e _start --build-id=none -z noexecstack

all: $(PROG)

$(PROG): $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "built $@"

$(OBJ): $(SRCS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) $(OBJ)

.PHONY: all clean
