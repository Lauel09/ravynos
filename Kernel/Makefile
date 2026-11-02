# ------------------------------------------------------------------------
#  RAVYNOS BUILD SYSTEM - Kernel and Extensions
# ------------------------------------------------------------------------

AVAILABILITY_PL = ${RAVYN_SDKROOT}/usr/local/libexec/availability.pl
AVAILABILITY_PL_PATH = ${RAVYN_SDKROOT}/usr/local/libexec/availability.pl
SEED_DEFINES = -DRC_ENABLE_XNU_PRODUCT_INFO_FILTER
PATH := ${TOOLCHAIN}/usr/bin:${PATH}

.if "${KERNEL_CONFIGS}" == "DEBUG"
DEBUG = .debug
.else
DEBUG =
.endif

.if "${.MAKE.OS}" ==  "Linux"
HOST_CODESIGN = /bin/true
IIG = /bin/true
HOST_LD ?= /usr/bin/ld
DKPATH = ${.OBJDIR}/xnu/${KERNEL_CONFIGS}_${ARCH_CONFIGS}/iokit/DriverKit

${DKPATH}: .NOTMAIN
	mkdir -p ${DKPATH}
	cp -Rf ${.CURDIR}/xnu/iokit/DriverKit/*.h ${DKPATH}/
TARGETS = ${DKPATH}
.endif
TARGETS += xnu_all

MAKEFLAGS=
.MAKEFLAGS=

PLATFORM_DIR=${ROOT_BINARY_DIR}/Developer/Platforms/ravynOS.platform

# On a macOS host, /Library/Developer/CommandLineTools is Apple's CLT, which the
# ravynOS xcrun cannot use: every `xcrun -sdk macosx -find nm` (NM, LIPO,
# HOST_CC, ...) then expands to "error: unable to find sdk". Point only our
# xcrun at the Developer tree we built (it looks for toolchains in
# $DEVELOPER_DIR/Toolchains). DEVELOPER_DIR itself must stay the CLT: the host
# /usr/bin/clang is Apple's xcrun shim and rejects any other directory.
XNU_DEVELOPER_DIR=/Library/Developer/CommandLineTools
# SETUP programs are host executables.  They must use the host macOS SDK,
# not the incompletely populated ravynOS target SDK that XNU itself is
# installing headers into.  Keep the Linux/default xcrun resolution unchanged.
XNU_INSTALL_HOST_SDKROOT=${RAVYN_SDKROOT}
XNU_INSTALL_HOST_SDKROOT_RESOLVED=
XNU_INSTALL_HOST_TOOL_ARGS=
XNU_ALL_HOST_ARGS=
.if "${.MAKE.OS}" == "Darwin"
RAVYN_DEVELOPER_DIR=${ROOT_BINARY_DIR}/Developer
XNU_XCRUN=env DEVELOPER_DIR=${RAVYN_DEVELOPER_DIR} ${XCRUN}
XNU_INSTALL_HOST_SDKROOT=${XNU_DEVELOPER_DIR}/SDKs/MacOSX.sdk
XNU_INSTALL_HOST_SDKROOT_RESOLVED=${XNU_INSTALL_HOST_SDKROOT}
# The ravynOS xcrun only knows SDKs in RAVYN_DEVELOPER_DIR, so do not ask it
# to resolve host tools through the external Apple SDK used for -isysroot.
XNU_INSTALL_HOST_TOOL_ARGS=HOST_GM4=m4
XNU_ALL_HOST_ARGS=HOST_SDKROOT=${XNU_INSTALL_HOST_SDKROOT} \
	HOST_SDKROOT_RESOLVED=${XNU_INSTALL_HOST_SDKROOT_RESOLVED} \
	HOST_FLEX=flex HOST_BISON=bison HOST_GM4=m4 \
	HOST_CODESIGN_ALLOCATE=/bin/true
.else
XNU_XCRUN=${XCRUN}
.endif

xnu_devdir: .PHONY
.if "${.MAKE.OS}" == "Darwin"
	ln -sfn Platforms/ravynOS.platform/Developer/Toolchains ${RAVYN_DEVELOPER_DIR}/Toolchains
.endif

all: ${TARGETS}
installhdrs: .PHONY xnu_devdir
	mkdir -p ${ROOT_BINARY_DIR}${.CURDIR}/xnu
	touch ${ROOT_BINARY_DIR}${.CURDIR}/xnu/.mach_kernel.timestamp
	touch ${ROOT_BINARY_DIR}${.CURDIR}/xnu/.symbolset.timestamp
	PATH="${PATH}" SHELL=/bin/bash ${GMAKE} -C ${.CURDIR}/xnu LOGCOLORS=y PLATFORM=MacOSX BUILD_LTO=0 \
		BUILD_PROFILE=0 BUILD_WERROR=0 DO_CTFMERGE=0 \
		SDKVERSION=${MACOS_VERSION_MIN} SRCROOT=${.CURDIR}/xnu \
		OBJROOT=${.OBJDIR}/xnu DSTROOT=${RAVYN_SDKROOT} \
		MIGCOM=${MIGCOM} MIG=${MIG} IIG=${IIG} MIGCC=${CC} \
		CC=${CC} CXX=${CXX} HOST_OS_VERSION=${MACOS_VERSION_MIN} \
		UNIFDEF=${UNIFDEF} CTFCONVERT=${CTFCONVERT} \
		CTFMERGE=${CTFMERGE} CTFINSERT=${CTFINSERT} \
		HOST_SDKROOT=${XNU_INSTALL_HOST_SDKROOT} \
		HOST_SDKROOT_RESOLVED=${XNU_INSTALL_HOST_SDKROOT_RESOLVED} VERBOSE=YES \
		AVAILABILITY_PL=${AVAILABILITY_PL} SDKROOT=macosx \
		AVAILABILITY_PL_PATH=${AVAILABILITY_PL_PATH} XCRUN="${XNU_XCRUN}" \
		KERNEL_CONFIGS=${KERNEL_CONFIGS} ARCH_CONFIGS=${ARCH_CONFIGS} \
		DEVELOPER_DIR=${XNU_DEVELOPER_DIR} HOST_FLEX=flex \
		SYMROOT=${ROOT_BINARY_DIR} HOST_CC=${HOST_CC} HOST_BISON=bison \
		HOST_CXX=${HOST_CXX} HOST_LD=${HOST_LD} \
		HOST_CODESIGN_ALLOCATE=/bin/true ${XNU_INSTALL_HOST_TOOL_ARGS} installhdrs

libfirehose_kernel: .PHONY
	${MAKE} -C ${ROOT_SOURCE_DIR}/Libraries/libfirehose_kernel all

# XNU's PGO support needs the LLVM instrumentation runtime, but only the
# freestanding subset Apple also puts in libclang_rt.cc_kext.a; the userspace
# libclang_rt.profile_osx.a drags in libc (fprintf/__stderrp).  Build the
# kernel-safe subset from the vendored compiler-rt sources instead of copying
# the CLT archive.  See Libraries/libclang_rt_profile/Makefile.
libclang_rt_profile: .PHONY
	${MAKE} -C ${ROOT_SOURCE_DIR}/Libraries/libclang_rt_profile all

compiler_rt: .PHONY
	mkdir -p ${RAVYN_SDKROOT}/usr/lib ${RAVYN_SDKROOT}/usr/local/lib/kernel
	cp -f /Library/Developer/CommandLineTools/usr/lib/clang/17/lib/darwin/libclang_rt.osx.a \
	  ${RAVYN_SDKROOT}/usr/lib/libclang_rt.osx.a
	cp -f /Library/Developer/CommandLineTools/usr/lib/clang/17/lib/darwin/libclang_rt.cc_kext.a \
	  ${RAVYN_SDKROOT}/usr/local/lib/kernel/libcc_kext.a

xnu_all: xnu_devdir installhdrs libfirehose_kernel compiler_rt libclang_rt_profile
	PATH="${PATH}" SHELL=/bin/bash ${GMAKE} -C ${.CURDIR}/xnu LOGCOLORS=y \
		PLATFORM=MacOSX BUILD_LTO=0 BUILD_WERROR=0 DO_CTFMERGE=0 \
		SDKVERSION=${MACOS_VERSION_MIN} SRCROOT=${.CURDIR}/xnu \
		OBJROOT=${.OBJDIR}/xnu DSTROOT=${RAVYN_SDKROOT} \
		SYMROOT=${.OBJDIR}/xnu/sym BUILD_STATIC_LINK=1 \
		MIGCOM=${MIGCOM} MIG=${MIG} IIG=${IIG} CC=${CC} CXX=${CXX} \
		UNIFDEF=${UNIFDEF} CTFCONVERT=${CTFCONVERT} LD="${CC} --ld-path=${LD}" \
		CTFMERGE=${CTFMERGE} CTFINSERT=${CTFINSERT} AR=${AR} \
		NM=${NM} NMEDIT=${NMEDIT} DSYMUTIL=${DSYMUTIL} LIPO=${LIPO} \
	    STRIP=${STRIPBIN} AS=${AS} AVAILABILITY_PL=${AVAILABILITY_PL} \
		MACHINE=${CpuArch} XCRUN="${XNU_XCRUN}" SDKROOT=macosx \
		HOST_OS_VERSION=${MACOS_VERSION_MIN} VERBOSE=YES \
		AVAILABILITY_PL_PATH=${AVAILABILITY_PL_PATH} \
		DEVELOPER_DIR=${XNU_DEVELOPER_DIR} \
		${XNU_ALL_HOST_ARGS} \
		LIBTOOL=${TOOLCHAIN}/usr/bin/libtool VERBOSE=YES \
		KERNEL_CONFIGS=${KERNEL_CONFIGS} ARCH_CONFIGS=${ARCH_CONFIGS} \
		HOST_CC=${HOST_CC} HOST_CXX=${HOST_CXX} HOST_LD=${HOST_LD} all install
.for target_dir in ${RAVYN_SDKROOT}/System/Library/Kernels \
  ${SYSROOT_DIR}/System/Library/Kernels
	mkdir -p ${target_dir} ${target_dir:H}/Extensions
	cp -Rf ${.OBJDIR}/xnu/sym/kernel${DEBUG}.dSYM ${target_dir}/
	cp -f ${.OBJDIR}/xnu/sym/kernel${DEBUG} ${target_dir}/
	cp -Rf ${.OBJDIR}/xnu/sym/System.kext ${target_dir:H}/Extensions/
.endfor

# NOTE! This kxld build is for the host toolchain. The target kxld is built
# later in IOKitUser
SUBDIR = xnu/libkern/kmod xnu/libkern/kxld
SUBDIR += IOKitUser
SUBDIR += Extensions

.include <bsd.subdir.mk>
