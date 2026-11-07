SUBDIR =
PROG= launchd

# When built standalone (not via top-level bmake), compute SDK path.
.if !defined(RAVYN_SDKROOT)
RAVYN_SDKROOT = ${.CURDIR}/../../../Developer/ravynOS.sdk
.endif

.PATH: .
.PATH: ${.CURDIR}
.PATH: ${.CURDIR}/support
.PATH: ${.CURDIR}/uuid
.PATH: ${.CURDIR}/init

MK_PIE= no
BASE_INCLUDE=  -I. -I${.CURDIR} -I${.CURDIR}/init -I${.CURDIR}/../../include/apple
BASE_INCLUDE+= -I${.CURDIR}/../../sys -I${MACHINE_INCLUDES}
DEFINES= -D__APPLE__ -fblocks -DLIBC_NO_LIBCRASHREPORTERCLIENT -DPRIVATE
MIG_FLAGS= ${BASE_INCLUDE} 	${DEFINES}
#override default CFLAGS
# Do NOT add -target/-arch x86_64 here: with SDKROOT and MACOSX_DEPLOYMENT_TARGET both
# in the environment, clang aborts with "conflicting deployment targets ... present in
# environment" even when the values are equal (reproduced with the ravynOS toolchain
# clang at build/.../Default.xctoolchain/usr/bin/clang). The toolchain's default target
# is x86_64-apple-darwin, so --sysroot + -mmacos-version-min (the house pattern used by
# BSD/sbin/mount_hfs) yields x86_64 objects without the conflict.
CFLAGS= ${MIG_FLAGS}  -D__MigTypeCheck -nostdinc --sysroot=${RAVYN_SDKROOT} -mmacos-version-min=${MACOS_VERSION_MIN} -isystem ${RAVYN_SDKROOT}/usr/include -include ${.CURDIR}/../../include/sys/mach/availability_stub.h
CFLAGS+= -I${.CURDIR}/../../contrib/openbsm -I${.CURDIR}/../../lib/liblaunch -I${.CURDIR}/../../../BSD/old-launchd/liblaunch
CFLAGS+= -fPIC -DHAVE_INTTYPES_H

DEBUG_FLAGS+= -g -O0
MK_WERROR=no

# Apple's launchd is dynamically linked (dyld + /usr/lib/libSystem.B.dylib);
# a static link can't resolve the libSystem.B.tbd umbrella at all.
# libSystem.B.dylib is a partial umbrella that does not re-export liblaunch/
# libxpc, and ld64 refuses direct linking against those umbrella-only dylibs.
# libSystem.B.dylib in sysroot has been patched with LC_REEXPORT_DYLIB entries
# for liblaunch.dylib and libxpc.dylib; libdyld.dylib has been patched to load
# both and to clear MH_TWOLEVEL. launchd is linked with -flat_namespace so
# dyld resolves _vproc_*/_xpc_* symbols from the re-exported libraries at boot.
LDFLAGS+= -lSystem -Wl,-flat_namespace -undefined dynamic_lookup

helper.h launchd_helperServer.h: ${.CURDIR}/helper.defs
	mig ${MIG_FLAGS} -user /dev/null -server /dev/null -header helper.h \
	-sheader launchd_helperServer.h ${.CURDIR}/helper.defs

jobServer.h jobServer.c job.h: ${.CURDIR}/job.defs helper.h
	mig ${MIG_FLAGS} -sheader jobServer.h ${.CURDIR}/job.defs
job_reply.h job_replyUser.c job_replyServer.c job_replyServer.h: ${.CURDIR}/job_reply.defs
	mig ${MIG_FLAGS} -header job_reply.h -sheader job_replyServer.h ${.CURDIR}/job_reply.defs
${.CURDIR}/job_forward.h job_forwardUser.c job_forwardServer.h: ${.CURDIR}/job_forward.defs
	mig ${MIG_FLAGS} -header job_forward.h -sheader job_forwardServer.h ${.CURDIR}/job_forward.defs
notify.h notifyServer.c notifyServer.h: ${.CURDIR}/notify.defs
	mig ${MIG_FLAGS} -header notify.h -sheader notifyServer.h ${.CURDIR}/notify.defs
mach_excServer.h mach_excServer.c: ${.CURDIR}/mach_exc.defs
	mig ${MIG_FLAGS} -sheader mach_excServer.h ${.CURDIR}/mach_exc.defs
internal.h internalServer.h internalServer.c internalUser.c: ${.CURDIR}/internal.defs
	mig ${MIG_FLAGS} -header internal.h -sheader internalServer.h ${.CURDIR}/internal.defs



SRCS= jobServer.c mach_excServer.c internalServer.c
SRCS+= job_forwardUser.c  job_replyUser.c job_replyServer.c notifyServer.c
SRCS+= internalUser.c
SRCS+= launchd.c core.c kill2.c ktrace.c ipc.c log.c runtime.c
SRCS+= init.c
# Nothing beyond the libSystem umbrella (sysroot libSystem.B re-exports
# dispatch/pthread/xpc, and the audit syscalls live in libsystem_kernel).
# Apple's /sbin/launchd additionally links libbsm; OpenBSM is in-tree, so
# inline the pieces launchd references (audit_token_to_au32() via
# bsm_wrappers.c: au_to_* token builders and the BSM errno table are
# pulled by audit_write/audit_submit).
.PATH: ${.CURDIR}/../../../Libraries/openbsm/libbsm
SRCS+= bsm_wrappers.c bsm_token.c bsm_errno.c
# For <config/config.h> (OpenBSM's build selection) and <compat/...> fallbacks.
CFLAGS+= -I${.CURDIR}/../../../Libraries/openbsm


MAN=	launchd.conf.5 launchd.plist.5 launchd.8

launchd.1:
	MAN=

CLEANFILES+= *Server.c *User.c job.h helper.h job_reply.h job_forward.h notify.h \
	internal.h mach_exc.h *Server.h *~ cscope.*

# Install launchd into the sysroot so XNU's load_init_program("/sbin/launchd")
# finds it at boot.  Mirrors BSD/sbin/mount_hfs's to-sysroot target.
all: to-sysroot
to-sysroot: ${PROG}
	mkdir -p ${SYSROOT_DIR}/sbin
	cp -f ${PROG} ${SYSROOT_DIR}/sbin/

.include <bsd.prog.mk>
