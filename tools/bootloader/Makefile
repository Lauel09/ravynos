# Build the UEFI loader skeleton.
#
#   make            build BOOTX64.EFI
#   make clean
#
# A UEFI application is a PE32+ executable with subsystem 10
# (IMAGE_SUBSYSTEM_EFI_APPLICATION) and no CRT, so the build is two commands:
# compile freestanding for the Windows/UEFI ABI, then link with lld-link.
#
# Overrides: CC, LD (lld-link), CFLAGS, LDFLAGS.

PROG=	BOOTX64.EFI
OBJ=	uefi_stub.o
SRCS=	uefi_stub.c

CC?=	clang
LD?=	lld-link

# -mno-red-zone is mandatory: UEFI runs with interrupts enabled and firmware
# may use the red zone.  -fshort-wchar makes CHAR16 a 16-bit wchar_t.
CFLAGS+=	-target x86_64-unknown-windows \
		-ffreestanding -fno-stack-protector -fno-builtin \
		-mno-red-zone -fshort-wchar -O2 -Wall -Wextra \
		-Wno-unused-parameter
LDFLAGS+=	-machine:x64 -subsystem:10 -entry:efi_main -nodefaultlib \
		-nxcompat:no

all: ${PROG}

${PROG}: ${OBJ}
	${LD} ${LDFLAGS} -out:${.TARGET} ${.ALLSRC}
	@echo "built ${.TARGET}"

${OBJ}: ${SRCS}
	${CC} ${CFLAGS} -c -o ${.TARGET} ${.ALLSRC}

clean:
	rm -f ${PROG} ${OBJ}

.PHONY: all clean
