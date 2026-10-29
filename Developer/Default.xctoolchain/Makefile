# ------------------------------------------------------------------------
#  RAVYNOS BUILD SYSTEM - Developer Toolchain
# ------------------------------------------------------------------------

GNU_CONFIGURE_ARGS = --prefix=/usr --sysconfdir=/etc --bindir=/bin \
		--sbindir=/sbin --localstatedir=/var

# The shim headers under include/ stand in for missing Darwin headers on
# Linux. On a Darwin host they shadow the real SDK (e.g. incomplete Mach
# exception-port prototypes break LLVM), so only use them elsewhere.
.if "${.MAKE.OS}" != "Darwin"
CFLAGS += -I${.CURDIR}/include
.endif
.export CFLAGS

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


SUBDIR = gmake mig unifdef cctools/libstuff cctools/libmacho xar rpcgen \
	 dtrace_ctf/libctf dtrace_ctf/libelf dtrace_ctf/libdwarf llvm \
	 cctools dtrace_ctf/tools plktool xcbuild lzvndecode iig

.include <bsd.subdir.mk>

