CC := ${HOST_CC}
BUILD_UID != id -u
CFLAGS += -std=c99 -Wno-implicit-function-declaration -Wno-int-conversion
SRCS = unifdef.c
OBJS = unifdef.o

.if "${.MAKE.OS}" == "Linux"
BINGRP = 0
.endif

all: ${TOOLS}/unifdef

${TOOLS}/unifdef: ${OBJS}
	${HOST_CC} -o ${.TARGET} ${OBJS}
# chown to root:wheel requires root; user builds keep their own ownership
.if "${BUILD_UID}" == "0" && !defined(NO_ROOT)
	chown ${BINOWN}:${BINGRP} ${.TARGET}
.endif
	chmod ${BINMODE} ${.TARGET}

.include <bsd.own.mk>
