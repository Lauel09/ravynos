# Probe build.  Deliberately BSD-make compatible: the tree's entry point is
# bmake, and a probe is built on the Mac where bmake is already installed.
#
#   bmake              build ./probe
#   bmake FRAMEWORK=IOKit   additionally link a framework
#   bmake clean

PROG=	probe

CFLAGS+=	-Wall -Wextra -Wno-unused-parameter -O0 -g
.if defined(FRAMEWORK) && !empty(FRAMEWORK)
LDFLAGS+=	-framework ${FRAMEWORK}
.endif

all: ${PROG}

${PROG}: probe.c
	${CC} ${CFLAGS} -o ${.TARGET} ${.ALLSRC} ${LDFLAGS}

clean:
	rm -f ${PROG} *.dSYM
