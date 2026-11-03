MK_WERROR = no
LIB = CrashReporterClient
SRCS = CRClient.c CRClientCPP.cpp
OBJS = ${SRCS:S/.c$/.o/:S/.cpp$/.o/}
CFLAGS = --sysroot=${RAVYN_SDKROOT} -mmacos-version-min=${MACOS_VERSION_MIN} \
        -Wno-nullability-completeness
LDFLAGS =
SUBDIR =

all: ${.OBJDIR}/lib${LIB}.dylib

${.OBJDIR}/lib${LIB}.a: ${OBJS}
	rm -f ${.TARGET}
	mkdir -p ${.TARGET:H} ${RAVYN_SDKROOT}/usr/lib
	${AR} rcs ${.TARGET} CRClientCPP.o CRClient.o
	cp -f ${.TARGET} ${RAVYN_SDKROOT}/usr/lib/system/

${.OBJDIR}/lib${LIB}.dylib: ${.OBJDIR}/lib${LIB}.a
	# This dylib only defines the CR* entry points and touches no libc or
	# dyld symbols, so it must not link -lsystem_c/-lsystem_platform/-ldyld:
	# those libraries are built later by Libraries/Libsystem, and asking for
	# them here makes the first Libraries pass fail with
	#   ld: library not found for -lsystem_c
	${CC} -o ${.TARGET} -shared -dylib -umbrella System -nostdlib \
	  -Wl,-install_name,/usr/lib/system/libCrashReporterClient.dylib \
	  -Wl,-current_version,1.0.0 \
	  -mmacos-version-min=${MACOS_VERSION_MIN} \
	  -Wl,-force_load,${.OBJDIR}/lib${LIB}.a ${LDFLAGS}
	cp -f ${.TARGET} ${RAVYN_SDKROOT}/usr/lib/system/
	mkdir -p ${SYSROOT_DIR}/usr/lib/system
	cp -f ${.TARGET} ${SYSROOT_DIR}/usr/lib/system/

.include <bsd.lib.mk>
