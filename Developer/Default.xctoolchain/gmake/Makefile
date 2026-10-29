GNU_CONFIGURE_ARGS = --prefix=/usr --sysconfdir=/etc --bindir=/bin \
		--sbindir=/sbin --localstatedir=/var
BUILDDIR = ${ROOT_BINARY_DIR}${.CURDIR}
BUILD_UID != id -u

.if "${.MAKE.OS}" == "Linux"
BINGRP = 0
.endif

all: ${TOOLS}/gmake

${BUILDDIR}:
	mkdir -p ${BUILDDIR}

${TOOLS}/gmake: ${BUILDDIR}
	cd ${BUILDDIR} && \
	CC=${HOST_CC} CFLAGS="" OUTDIR=${BUILDDIR} \
	       ${.CURDIR}/configure ${GNU_CONFIGURE_ARGS} \
	       && ./build.sh
	cp -f ${BUILDDIR}/make ${.TARGET}
# chown to root:wheel requires root; user builds keep their own ownership
.if "${BUILD_UID}" == "0" && !defined(NO_ROOT)
	chown ${BINOWN}:${BINGRP} ${.TARGET}
.endif
	chmod ${BINMODE} ${.TARGET}

.include <bsd.own.mk>

