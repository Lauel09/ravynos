CC := ${HOST_CC}
CXX := ${HOST_CXX}

.PATH: ${ROOT_SOURCE_DIR}/Kernel/IOKitUser
SRCS =	plktool.c \
	macho.c \
	kext.c \
	IOCFSerialize.c
OBJS = ${SRCS:%.c=%.o}

CFLAGS = -I${ROOT_SOURCE_DIR}/Kernel/kext_tools/FILES/3rd \
	-I${ROOT_SOURCE_DIR}/Developer/Default.xctoolchain/include \
	-I${ROOT_SOURCE_DIR}/Kernel/xnu/libkern \
	-I${ROOT_SOURCE_DIR}/Kernel/IOKitUser/kext.subproj \
	-I${ROOT_SOURCE_DIR}/Kernel/IOKitUser \
	-include${.CURDIR}/../include/Availability.h \
	-include${.CURDIR}/../include/AvailabilityInternal.h \
	-include${.CURDIR}/../include/AvailabilityMacros.h \
	-include${.CURDIR}/../include/AvailabilityVersions.h \
	-D__APPLE_CPP__ -D__LITTLE_ENDIAN__ -w -g -O0

# This uses Open CF-Lite, not our CoreFoundation. Make sure you have
# it installed! (opencflite in AUR)
LDFLAGS = -L${TOOLCHAIN}/usr/lib \
	  -lkxld -lmacho -lstuff -lCoreFoundation -luuid -lssl -lcrypto \
	  -lz -lbsd -lc++

# plktool is a Linux port of Apple's kextcache, not a host-neutral tool.  It
# includes <openssl/evp.h>, <openssl/sha.h> and glibc's <byteswap.h>, links
# -lssl -lcrypto -lbsd, and calls 53 CoreFoundation symbols (236 call sites)
# against the in-tree CF rather than Apple's, with CFBundlePriv.h taken from
# Frameworks/CoreFoundation.  On macOS none of that is available, so the build
# stops at the very first include:
#   plktool.h:39:10: fatal error: 'openssl/evp.h' file not found
# Porting it means replacing six EVP_* SHA-256 calls with CommonCrypto and
# reconciling the CF dependency -- not something that can be validated without a
# macOS host.  So it stays unbuilt here, and Kernel/Extensions/Makefile skips the
# prelinked cache on Darwin and says so, rather than failing after every kext has
# already been built.
.if "${.MAKE.OS}" == "Linux"
all: ${TOOLS}/plktool

${TOOLCHAIN}/usr/lib/libkxld.a: .PHONY
	${MAKE} -C ${ROOT_SOURCE_DIR}/Kernel/xnu/libkern/kxld all

${TOOLS}/plktool: ${TOOLCHAIN}/usr/lib/libkxld.a ${OBJS} ${.CURDIR}/Makefile
	${HOST_CXX} -o ${.TARGET} ${OBJS} ${LDFLAGS}
.else
all: .PHONY
${TOOLS}/plktool: .PHONY
.endif
