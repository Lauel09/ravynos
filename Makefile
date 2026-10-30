# ------------------------------------------------------------------------
#  RAVYNOS BUILD SYSTEM - TOP LEVEL
# ------------------------------------------------------------------------

PROD_VERSION = 0.7.1
PROD_FAMILY = Pre Alpha

# Target architecture is independent of the host. x86_64 is upstream's
# supported target and the one QEMU boots; arm64 is an experiment until its
# userland is complete. Select with: bmake TARGET_ARCH=arm64 world
TARGET_ARCH ?= x86_64
.if "${TARGET_ARCH}" == "x86_64"
    BuildArch = X86
    CpuArch = x86_64
    ARCH_CONFIGS = X86_64
.elif "${TARGET_ARCH}" == "arm64" || "${TARGET_ARCH}" == "aarch64"
    BuildArch = AArch64
    CpuArch = aarch64
    ARCH_CONFIGS = ARM64
.else
.error Unsupported TARGET_ARCH=${TARGET_ARCH}; use x86_64 or arm64
.endif
KERNEL_CONFIGS = RELEASE

ROOT_SOURCE_DIR = ${.CURDIR}

MACHINE_CPUARCH = ${CpuArch}
MK_UNIFIED_OBJDIR = no
MK_AUTO_OBJ = yes

.SYSPATH: ${ROOT_SOURCE_DIR}/BSD/share/mk
.include "./BSD/share/mk/sys.mk"
.include "./BSD/share/mk/src.tools.mk"
.include "./BSD/share/mk/bsd.linker.mk"
.include "./BSD/share/mk/bsd.compiler.mk"

_ROOT_BINARY_DIR = ${ROOT_SOURCE_DIR}/../build
ROOT_BINARY_DIR = ${_ROOT_BINARY_DIR:tA}
SRCTOP = ${ROOT_SOURCE_DIR}
OBJTOP = ${ROOT_BINARY_DIR}
SRCROOT = ${ROOT_SOURCE_DIR}
OBJROOT = ${ROOT_BINARY_DIR}
MAKEOBJDIRPREFIX = ${ROOT_BINARY_DIR}

# ------------------------------------------------------------------------
#  Top level targets
# ------------------------------------------------------------------------

world: version Developer Kernel Libraries/libfirehose_kernel Libraries BSD

# ------------------------------------------------------------------------

# Don't warn about ravynOS.sdk vs MacOSX.sdk naming
CFLAGS = -Wno-incompatible-sysroot

# If we are building from a different OS, we want to use the host's tools
# to build our toolchain.
.if "${.MAKE.OS}" == "Darwin"
# HOST_CC is what bootstraps the toolchain: mig, cctools, xar and rpcgen are
# compiled with it *before* llvm has produced ${TOOLCHAIN}/usr/bin/clang (llvm
# itself depends on ${TOOLCHAIN}/usr/libexec/migcom, so that order cannot be
# swapped).  Defining HOST_CC = ${CC} made bootstrap depend on the very
# compiler it is supposed to build: on any host without a system bundle at
# /Library/Developer/Toolchains/Default.xctoolchain -- every modern macOS
# runner -- CC resolves into the empty in-tree toolchain and the build dies
# with "…/usr/bin/clang: No such file or directory".  Use the system compiler
# when there is one, and keep the old behaviour when the toolchain exists.
.if exists(${TOOLCHAIN}/usr/bin/clang)
HOST_CC = ${CC}
HOST_CXX = ${CXX}
HOST_AR = ${AR}
.elif exists(/usr/bin/clang)
HOST_CC = /usr/bin/clang
HOST_CXX = /usr/bin/clang++
HOST_AR = /usr/bin/ar
.else
HOST_CC ?= /usr/bin/cc
HOST_CXX ?= /usr/bin/c++
HOST_AR ?= /usr/bin/ar
.endif
# Command Line Tools' yacc/lex are Xcode shims that refuse to run without a
# full Xcode selected; fall back to bison/flex in yacc/lex mode when present.
BISON_PATH != command -v bison 2>/dev/null || true
FLEX_PATH != command -v flex 2>/dev/null || true
.if !empty(BISON_PATH)
YACC = ${BISON_PATH} -y
.endif
.if !empty(FLEX_PATH)
LEX = ${FLEX_PATH}
.endif
.export YACC LEX
.else
HOST_CC ?= /usr/bin/cc
HOST_CXX ?= /usr/bin/c++
HOST_AR ?= /usr/bin/ar
.endif

# If we have a toolchain bundle, default to not building it again
# TOOLCHAIN is initially set to /Library/Developer/... by sys.mk
.if exists(${TOOLCHAIN})
MK_TOOLCHAIN ?= no
.else
MK_TOOLCHAIN ?= yes
.endif

.if ${MK_TOOLCHAIN} == "yes"
TOOLCHAIN = ${ROOT_BINARY_DIR}/Developer/Platforms/ravynOS.platform/Developer/Toolchains/Default.xctoolchain
.endif
TOOLS = ${TOOLCHAIN}/usr/bin
DEVEL = ${ROOT_SOURCE_DIR}/Developer

# The linker built by cctools is installed as ld64; point LD at it on
# Darwin so the rest of the build does not fall back to the host linker.
.if "${.MAKE.OS}" == "Darwin"
LD = ${TOOLCHAIN}/usr/bin/ld64
.export LD
.endif

DARWIN_VERSION != head -1 ${ROOT_SOURCE_DIR}/Kernel/xnu/config/MasterVersion

LLVM_VERSION_MAJOR != grep 'set.LLVM_VERSION_MAJOR' Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt | sed -E 's/^.* ([0-9]+).*$$/\1/'
LLVM_VERSION_MINOR != grep 'set.LLVM_VERSION_MINOR' Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt | sed -E 's/^.* ([0-9]+).*$$/\1/'
LLVM_VERSION_PATCH != grep 'set.LLVM_VERSION_PATCH' Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt | sed -E 's/^.* ([0-9]+).*$$/\1/'

LLVM_VERSION = ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}
LLVM_MAJOR = ${LLVM_VERSION_MAJOR}
LLVM_MINOR = ${LLVM_VERSION_MINOR}
LLVM_PATCH = ${LLVM_VERSION_PATCH}

PROD_MAJOR != echo ${PROD_VERSION} | sed 's/^([0-9]+\.\*)/\\1/'
PROD_MINOR != echo ${PROD_VERSION} | sed 's/^[0-9]+\.([0-9]+)\./\\1/'
PROD_PATCH != echo ${PROD_VERSION} | sed 's/([0-9]+)$$/\\1/'

DARWIN_MAJOR != echo ${DARWIN_VERSION} | sed 's/^([0-9]+\.\*)/\\1/'
DARWIN_MINOR != echo ${DARWIN_VERSION} | sed 's/^[0-9]+\.([0-9]+)\./\\1/'
DARWIN_PATCH != echo ${DARWIN_VERSION} | sed 's/([0-9]+)$$/\\1/'

MACOSX_DEPLOYMENT_TARGET = 10.15
MACOS_VERSION_MIN = 10.15

RUNTIME_SPEC_PATH = ${ROOT_SOURCE_DIR}/Developer/xcbuild/Specifications

XNU_SOURCE_DIR = ${ROOT_SOURCE_DIR}/Kernel/xnu
KEXT_SOURCE_DIR = ${ROOT_SOURCE_DIR}/Kernel/Extensions
SDK_SOURCE_DIR = ${ROOT_SOURCE_DIR}/Developer/ravynOS.sdk
PLATFORM_SOURCE_DIR = ${ROOT_SOURCE_DIR}/Developer/ravynOS.platform
RAVYN_SDKROOT = ${ROOT_BINARY_DIR}/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk
RAVYN_SDKROOT_MACOSX = ${ROOT_BINARY_DIR}/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
SYSROOT_DIR = ${ROOT_BINARY_DIR}/sysroot

TARGET_TRIPLE = ${CpuArch}-apple-darwin${DARWIN_MAJOR}

SUBDIR ?= Developer .WAIT Kernel Libraries Frameworks BSD

.export ROOT_SOURCE_DIR ROOT_BINARY_DIR TARGET_ARCH ARCH_CONFIGS KERNEL_CONFIGS \
	PROD_VERSION PROD_FAMILY CFLAGS DEVEL DARWIN_VERSION \
	LLVM_VERSION_MAJOR LLVM_VERSION_MINOR LLVM_VERSION_PATCH \
	LLVM_VERSION PROD_MAJOR PROD_MINOR PROD_PATCH DARWIN_MAJOR \
	DARWIN_MINOR DARWIN_PATCH MACOS_VERSION_MIN MACOSX_DEPLOYMENT_TARGET \
	RUNTIME_SPEC_PATH OPSYS MACHINE BuildArch CpuArch XNU_SOURCE_DIR \
	KEXT_SOURCE_DIR SDK_SOURCE_DIR PLATFORM_SOURCE_DIR RAVYN_SDKROOT \
	RAVYN_SDKROOT_MACOSX SYSROOT_DIR TARGET_TRIPLE MAKEOBJDIRPREFIX \
	TOOLCHAIN TOOLS SRCTOP OBJTOP SRCROOT OBJROOT MK_AUTO_OBJ MK_UNIFIED_OBJDIR \
	HOST_CC HOST_CXX HOST_LD

version:
	${MAKE} -C ${.CURDIR}/SystemLibrary SystemVersion.plist

.include "./BSD/share/mk/bsd.subdir.mk"
