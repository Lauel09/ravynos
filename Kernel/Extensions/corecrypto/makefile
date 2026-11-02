MK_WERROR = no
SUBDIR =
KEXT = corecrypto
KERNEL_PRIVATE = yes
INFO_PLIST = kext/Info.plist
BUNDLE_IDENTIFIER = com.apple.kec.corecrypto
BUNDLE_VERSION = 1.0.0
MAIN_FUNCTION = cc_kext_start
ANTIMAIN_FUNCTION = cc_kext_stop

SRCS = \
    cc_clear.c \
    cc_cmp_safe.c \
    cc_debug.c \
    cc_digest.c \
    kext/cc_functions.c \
    kext/cc_kext_main.c \
    algorithms/cckprng.c \
    algorithms/aes_ecb.c \
    algorithms/sha512.cc \
    algorithms/md5_impl.cpp \
    algorithms/yarrow/comp.c \
    algorithms/yarrow/smf.c \
    algorithms/sha256.cc \
    algorithms/sha1.c \
    algorithms/yarrow/sha1mod.c \
    algorithms/yarrow/prng.c \
    algorithms/aes128.c \
    algorithms/pdcrypto_digest_init.c \
    algorithms/aes_cbc.c \
    algorithms/md5.c \
    algorithms/yarrow/yarrowUtils.c \
    algorithms/pdcrypto_digest_final.c \
    algorithms/sha1_impl.cc \
    algorithms/pdcrypto_digest_update.c \
    algorithms/pdcrypto_dummy.c

CFLAGS = -DXNU_KERNEL_PRIVATE=1 -DPRIVATE=1 -DCC_KERNEL=1 \
	-Wno-sign-conversion -Wno-int-conversion \
	-Wno-implicit-function-declaration \
	-Wno-incompatible-library-redeclaration \
	-Wno-undef-prefix \
	-I${.CURDIR} -I${.CURDIR}/kext -I${.CURDIR}/include \
	-I${ROOT_SOURCE_DIR}/Kernel/xnu/bsd \
	-I${ROOT_SOURCE_DIR}/Kernel/xnu/libkern \
	-I${ROOT_SOURCE_DIR}/Kernel/xnu/osfmk \
	-I${.OBJDIR}/include

all: copyheaders

copyheaders:
	mkdir -p ${.OBJDIR}/include/corecrypto
	cp -f ${.CURDIR}/cc_abort.h ${.OBJDIR}/include/corecrypto

# user/ is not a buildable subdirectory: it holds a single dummy.c whose only
# content is "typedef int the_c_standard_does_not_like_empty_modules;" and a
# comment explaining that Xcode refuses a target with no source files.  The
# kext itself is assembled from SRCS above.  Listing user/ in SUBDIR made
# bsd.subdir.mk recurse into it and fail with
#   bmake[4]: don't know how to make all. Stop
#     bmake[4]: stopped making "all" in .../Kernel/Extensions/corecrypto/user
# The same mistake was made with IOKitUser's kext.subproj (see
# Kernel/IOKitUser/Makefile); this is not a build directory.
SUBDIR =
#SUBDIR += user

.include <rvn.kext.mk>
.include <bsd.subdir.mk>
