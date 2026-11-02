# ------------------------------------------------------------------------
#  RAVYNOS BUILD SYSTEM - kernel profiling runtime (libclang_rt.profile_osx)
# ------------------------------------------------------------------------
#
# XNU's PGO support calls the LLVM instrumentation runtime: bsd/pgo/
# profile_runtime.c (get_size_for_buffer/write_buffer) and libkern/c++/
# OSKext.cpp (the _internal variants) reference __llvm_profile_*, and
# Kernel/xnu/makedefs/MakeInc.kernel links -lclang_rt.profile_osx to satisfy
# them.
#
# The *userspace* libclang_rt.profile_osx.a cannot be used for that.  Its
# InstrProfilingFile.o needs open()/mmap()/getenv(), and PROF_ERR/PROF_WARN in
# InstrProfilingPort.h expand to fprintf(stderr, ...), so pulling
# InstrProfilingBuffer.o/InstrProfilingWriter.o out of it drags libc into a
# kernel that is linked with -nostdlib -fapple-kext:
#
#   Undefined symbols for architecture x86_64:
#     "___stderrp", referenced from:
#         _lprofBufferWriter in InstrProfilingWriter.c.o
#     "_fprintf", referenced from:
#         ...
#
# Only the freestanding subset Apple also compiles into libclang_rt.cc_kext.a
# is kernel safe.  That subset is PROFILE_SOURCES in Developer/
# Default.xctoolchain/llvm/compiler-rt/cmake/Modules/CompilerRTDarwinUtils.cmake
# (built there with -mkernel -DKERNEL_USE).  Its objects reference nothing from
# libc except memcpy/memset, which the kernel itself exports
# (Kernel/xnu/config/Libkern.exports).  Build exactly those sources here and
# stage the archive beside libcc_kext.a -- the directory XNU already searches
# for kernel archives on every host.
#
# Keep this file list in sync with CompilerRTDarwinUtils.cmake: adding e.g.
# InstrProfilingFile.c or InstrProfilingValue.c reintroduces libc dependencies
# and the kernel link fails again with undefined ___stderrp/_fprintf.

PROFILE_SRC = ${ROOT_SOURCE_DIR}/Developer/Default.xctoolchain/llvm/compiler-rt

LIB  = clang_rt.profile_osx
# bsd.lib.mk derives ${OBJS} from ${SRCS}.
SRCS = InstrProfiling.c InstrProfilingBuffer.c InstrProfilingPlatformDarwin.c \
       InstrProfilingWriter.c InstrProfilingInternal.c InstrProfilingVersionVar.c

.PATH: ${PROFILE_SRC}/lib/profile

MK_WERROR = no
MK_MAN = no
SUBDIR =

# Mirror the flags Apple uses for these objects (CompilerRTDarwinUtils.cmake,
# darwin_add_builtin_libraries: CFLAGS ... -mkernel, DEFS KERNEL_USE), against
# the target sysroot: this runtime is compiled for the kernel, not the host.
DEFINES = -DKERNEL_USE=1 -DVISIBILITY_HIDDEN

INCLUDES = -I${PROFILE_SRC}/lib/profile -I${PROFILE_SRC}/include

CFLAGS = --sysroot=${RAVYN_SDKROOT} ${DEFINES} ${INCLUDES} \
	 -mmacos-version-min=${MACOS_VERSION_MIN} -mkernel \
	 -fPIC -O3 -fvisibility=hidden -fomit-frame-pointer -Wall \
	 -Wno-unused-command-line-argument -Wno-incompatible-sysroot

all: ${RAVYN_SDKROOT}/usr/local/lib/kernel/${LIB}.a

${RAVYN_SDKROOT}/usr/local/lib/kernel/${LIB}.a: ${OBJS}
	mkdir -p ${RAVYN_SDKROOT}/usr/local/lib/kernel
	rm -f ${.TARGET}
	${TOOLS}/libtool -static \
	  -o ${.TARGET} -no_warning_for_no_symbols ${OBJS}

.include <bsd.lib.mk>
