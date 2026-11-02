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

# plktool builds the prelinked kernel cache and is required on every host, not
# just Linux: Kernel/Extensions/Makefile:kernelcache invokes ${TOOLS}/plktool,
# and on Darwin the old ".else all: .PHONY" branch built nothing at all, so that
# step would have failed with "plktool: command not found" once the kexts were
# done.  The two branches differed only in where libkxld.a comes from: xnu's kxld
# is always built first (top-level SUBDIR puts Developer .WAIT Kernel, and
# Kernel/Makefile:139 builds xnu/libkern/kxld before Extensions), and it writes
# ${TOOLCHAIN}/usr/lib/libkxld.a on both hosts.
${TOOLCHAIN}/usr/lib/libkxld.a:
	${MAKE} -C ${ROOT_SOURCE_DIR}/Kernel/xnu/libkern/kxld all

all: ${TOOLS}/plktool

# Same hazard that produced a stale iig.o: bmake does not know that these
# objects depend on the headers in ../include (they are pulled in with
# -include), so a header fix would otherwise be linked against old objects.
PATCH_HDRS = ${.CURDIR}/../include/Availability.h \
	     ${.CURDIR}/../include/AvailabilityInternal.h \
	     ${.CURDIR}/../include/AvailabilityMacros.h \
	     ${.CURDIR}/../include/AvailabilityVersions.h \
	     ${.CURDIR}/plktool.h

${OBJS}: ${PATCH_HDRS}

${TOOLS}/plktool: ${TOOLCHAIN}/usr/lib/libkxld.a ${OBJS} ${.CURDIR}/Makefile
	${HOST_CXX} -o ${.TARGET} ${OBJS} ${LDFLAGS}
