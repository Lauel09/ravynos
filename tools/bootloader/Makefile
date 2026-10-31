# Build the UEFI loader skeleton.
#
#   make            build BOOTX64.EFI
#   make clean
#
# The work lives in build.sh so that the same commands run under GNU make
# (Linux CI) and bmake (this tree's own build system).  The previous version
# used bmake-only automatic variables (.TARGET, .ALLSRC, .PHONY is fine), so
# the documented `make` failed on every Linux host with ".TARGET is not
# defined" -- the loader was never built anywhere.
#
# Toolchain detection, the zig fallback and the PE32+ validation are in
# build.sh; see tools/bootloader/README.md.

PROG=BOOTX64.EFI

all: ${PROG}

${PROG}: uefi_stub.c build.sh
	./build.sh -o ${PROG}

clean:
	rm -f ${PROG} BOOTX64.pdb uefi_stub.o

.PHONY: all clean
