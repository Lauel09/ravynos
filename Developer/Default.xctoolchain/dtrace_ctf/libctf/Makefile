CC := ${HOST_CC}
AR := ${HOST_AR}
CFLAGS += -I${.CURDIR}/../include -I${.CURDIR}/../include/sys -D__APPLE_CPP__ \
	  -D__LITTLE_ENDIAN__ -Wno-error -DCTF_OLD_VERSIONS
SRCS =	ctf_create.c \
	ctf_decl.c \
	ctf_error.c \
	ctf_hash.c \
	ctf_labels.c \
	ctf_lib.c \
	ctf_lookup.c \
	ctf_open.c \
	ctf_subr.c \
	ctf_types.c \
	ctf_util.c \
	fls.c

# The bundled qsort_r() uses FreeBSD's comparator-last signature and clashes
# with the system qsort_r() on Darwin. array.c already calls Darwin's
# comparator-last-thunk variant correctly, so only build the fallback where
# libc does not provide one (Linux).
.if "${.MAKE.OS}" != "Darwin"
SRCS += qsort.c
.endif
OBJS = ${SRCS:%.c=%.o}

all: ${TOOLCHAIN}/usr/lib/libctf.a

${TOOLCHAIN}/usr/lib/libctf.a: ${OBJS}
	${HOST_AR} rcs ${.TARGET} ${.ALLSRC}

