# ------------------------------------------------------------------------
#  RAVYNOS BUILD SYSTEM - Developer Toolchain
# ------------------------------------------------------------------------

GNU_CONFIGURE_ARGS = --prefix=/usr --sysconfdir=/etc --bindir=/bin \
		--sbindir=/sbin --localstatedir=/var

# The shim headers under include/ stand in for missing Darwin headers on
# Linux. They must stay on the global include path for cctools and other
# toolchain components; LLVM filters this directory out of its own flags
# below so the shims cannot shadow the real SDK.
CFLAGS += -I${.CURDIR}/include
.export CFLAGS

# Bootstrap archiver: libraries under this directory (cctools/libstuff,
# libmacho, dtrace_ctf, ...) are archived before llvm exists, but sys.mk
# points AR/RANLIB at ${TOOLCHAIN}/usr/bin, which is empty until llvm
# installs them.  Without this, the toolchain build gets past the compiler
# and then fails on the first `ar`.
.if !exists(${TOOLS}/clang)
AR = /usr/bin/ar
RANLIB = /usr/bin/ranlib
NM = /usr/bin/nm
.export AR RANLIB NM
.endif

all: ${TOOLCHAIN} ${.OBJDIR}/tapi/Version.inc

${TOOLCHAIN}:
	mkdir -p ${TOOLCHAIN}/usr/libexec
	mkdir -p ${TOOLCHAIN}/usr/include
	mkdir -p ${TOOLCHAIN}/usr/lib
	mkdir -p ${TOOLCHAIN}/usr/bin
	cp -f ${.CURDIR}/ToolchainInfo.plist ${TOOLCHAIN}/

${.OBJDIR}/tapi/Version.inc: ${.CURDIR}/llvm/tapi/include/tapi/Version.inc.in
	mkdir -p ${.OBJDIR}/tapi
	sed -e "s/@TAPI_VERSION_MAJOR@/${LLVM_VERSION_MAJOR}/" \
	   -e "s/@TAPI_VERSION_MINOR@/${LLVM_VERSION_MINOR}/" \
	   -e "s/@TAPI_VERSION_PATCH@/${LLVM_VERSION_PATCH}/" \
	   -e "s/@TAPI_VERSION@/${LLVM_VERSION}/" ${.ALLSRC} > ${.TARGET}


SUBDIR = mig unifdef cctools/libstuff cctools/libmacho xar rpcgen \
	 dtrace_ctf/libctf dtrace_ctf/libelf dtrace_ctf/libdwarf llvm \
	 cctools dtrace_ctf/tools plktool xcbuild lzvndecode iig

.include <bsd.subdir.mk>

