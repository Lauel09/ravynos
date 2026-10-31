# Build the ravynOS boot shell (SHELL.EFI).
#
# Same toolchain contract as tools/bootloader/Makefile: freestanding clang
# for the Windows/UEFI ABI + lld-link with -subsystem:efi_application.
# See that file's comments for why each flag is mandatory.

PROG =  SHELL.EFI
OBJ =   shell_efi.o
SRCS =  shell_efi.c

# Assigned, not ?=: make predefines CC=cc and LD=ld, and Apple's ld cannot
# link a PE32+ image.  Command-line overrides still win.
CC = clang
LD = lld-link

CFLAGS += -target x86_64-unknown-windows \
          -ffreestanding -fno-stack-protector -fno-builtin \
          -mno-red-zone -fshort-wchar -O2 -Wall -Wextra \
          -Wno-unused-parameter

LDFLAGS += -machine:x64 -subsystem:efi_application -entry:efi_main \
           -nodefaultlib

all: $(PROG)

$(PROG): $(OBJ)
	$(LD) $(LDFLAGS) -out:$@ $^
	@echo "built $@"

$(OBJ): $(SRCS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) $(OBJ)

.PHONY: all clean
