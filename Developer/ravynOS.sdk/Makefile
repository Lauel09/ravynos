.include <bsd.own.mk>

PLATFORM_DIR = ${ROOT_BINARY_DIR}/Developer/Platforms/ravynOS.platform
FPATH = /System/Library/Frameworks/System.framework
libc_path = ${ROOT_SOURCE_DIR}/Libraries/Libsystem/libsystem_c
objc_path = ${ROOT_SOURCE_DIR}/Libraries/objc4

# On Darwin, host tools (xnu SETUP) link with -isysroot at this SDK and
# need libSystem for that.  usr/lib is only filled later by the Libsystem
# build (world order: Developer Kernel Libraries ... BSD), so seed the
# CLT SDK's libSystem TBDs -- same precedent as Kernel/Makefile's
# compiler_rt copy from /Library/Developer/CommandLineTools.
CLTSDK = /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
.if "${.MAKE.OS}" == "Darwin" && exists(${CLTSDK}/usr/lib/libSystem.tbd) && exists(${CLTSDK}/usr/lib/libSystem.B.tbd)
CLT_LIBSYSTEM_COPY = cp -f ${CLTSDK}/usr/lib/libSystem.tbd ${CLTSDK}/usr/lib/libSystem.B.tbd ${RAVYN_SDKROOT}/usr/lib/
.else
CLT_LIBSYSTEM_COPY = :
.endif

# Kernel/IOKitUser links IOKit.framework during the Kernel pass, which runs
# before Libraries and Frameworks (.WAIT between them), yet its LDFLAGS name
# CoreFoundation, Foundation, objc, c++, z and System.  None of those exist in
# this SDK yet at that point, so the link stops at the first one it cannot
# resolve -- successively "library not found for -lkernel" (now removed), then
# "framework not found CoreFoundation", then Foundation, then libobjc, and so
# on.  Seed the CLT SDK's TBD text stubs for them, exactly as libSystem is
# already seeded above: a TBD carries symbols only, needs no binary, and is
# replaced by the real artifact when Libraries and Frameworks install over it.
CLT_STUB_LIBS = libobjc.tbd libc++.tbd libz.tbd
CLT_STUB_FMWKS = CoreFoundation Foundation

# IOKitUser stages the user-space <IOKit/hid/IOHIDElement.h>, which includes
# <CoreFoundation/CoreFoundation.h>, and Kernel/Extensions/IOHIDFamily then
# compiles <IOKit/hid/IOHIDEventService.h> against it during the same Kernel
# pass.  Frameworks/CoreFoundation is built later (and `world` does not list
# Frameworks at all), so seed the public headers from this tree now, as the
# .tbd stubs above are seeded.  The framework build replaces the bundle and
# installs the same list from Frameworks/CoreFoundation/Makefile.
CF_SOURCE = ${ROOT_SOURCE_DIR}/Frameworks/CoreFoundation
CF_HEADERS = CFArray.h CFBag.h CFBase.h CFBinaryHeap.h CFBitVector.h \
	CFBundle.h CFByteOrder.h CFCalendar.h CFCharacterSet.h CFData.h \
	CFDate.h CFDateFormatter.h CFDictionary.h CFError.h CFLocale.h \
	CFMessagePort.h CFMachPort.h CFNumber.h CFNumberFormatter.h CFPlugIn.h \
	CFPlugInCOM.h CFPreferences.h CFPropertyList.h CFRunLoop.h CFSet.h \
	CFSocket.h CFStream.h CFString.h CFStringEncodingExt.h CFTimeZone.h \
	CFTree.h CFURL.h CFURLAccess.h CFURLEnumerator.h CFUUID.h \
	CFUserNotification.h CFXMLNode.h CFXMLParser.h CFXPCBridge.h \
	CFAvailability.h CFUtilities.h CoreFoundation.h

all: ${PLATFORM_DIR} ${TOOLS}/unifdef .EXEC
	mkdir -p ${RAVYN_SDKROOT}${FPATH}/Versions/B
	mkdir -p ${RAVYN_SDKROOT}/usr/local/include
	mkdir -p ${RAVYN_SDKROOT}/usr/local/lib
	mkdir -p ${RAVYN_SDKROOT}/usr/local/bin
	mkdir -p ${RAVYN_SDKROOT}/usr/include
	mkdir -p ${RAVYN_SDKROOT}/usr/lib
	mkdir -p ${RAVYN_SDKROOT}/usr/bin
	cp -f ${.CURDIR}/SDKSettings.plist ${.CURDIR}/SDKSettings.json \
		${.CURDIR}/Entitlements.plist ${RAVYN_SDKROOT}/
	ln -sf Versions/Current/PrivateHeaders ${RAVYN_SDKROOT}${FPATH}/PrivateHeaders
	ln -sf Versions/Current/Resources ${RAVYN_SDKROOT}${FPATH}/Resources
	ln -sf B ${RAVYN_SDKROOT}${FPATH}/Versions/Current
	ln -sf Versions/Current/System ${RAVYN_SDKROOT}${FPATH}/System
	ln -sf ../../../../../../usr/lib/libSystem.B.dylib ${RAVYN_SDKROOT}${FPATH}/Versions/B/System
	ln -sf libSystem.B.dylib ${RAVYN_SDKROOT}/usr/lib/libSystem.dylib
	mkdir -p ${RAVYN_SDKROOT}/System/Library/Frameworks/Kernel.framework/Versions/A
	ln -sf Versions/A/Headers ${RAVYN_SDKROOT}/System/Library/Frameworks/Kernel.framework/Headers
	ln -sf Versions/A/PrivateHeaders ${RAVYN_SDKROOT}/System/Library/Frameworks/Kernel.framework/PrivateHeaders
	# Compatibility symlinks
	ln -sf ravynOS.platform ${ROOT_BINARY_DIR}/Developer/Platforms/MacOSX.platform
	ln -sf ravynOS.sdk ${ROOT_BINARY_DIR}/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
	cp -Rf ${.CURDIR}/usr ${.CURDIR}/System ${RAVYN_SDKROOT}/
	# usr/include/os/ ships base.h, lock.h, availability.h and
	# state_private.h but not object.h, yet dispatch/dispatch.h, xpc/xpc.h and
	# the asl_*.h headers in this same SDK all #include <os/object.h>, and
	# IOKitLib.c reaches it through dispatch from plain C.  Take the complete
	# header from this SDK's own usr/local/include/kernel/os/, which is the
	# copy IOKitUser/Makefile force-includes into its Objective-C++ objects;
	# BSD/include/os/object.h is a stale 188-line variant of the same file
	# with the same __OS_OBJECT__ guard and no OS_OBJECT_DECL_CLASS, so
	# staging it left dispatch/object.h unable to declare dispatch_object_t.
	cp -f ${.CURDIR}/usr/local/include/kernel/os/object.h \
		${RAVYN_SDKROOT}/usr/include/os/
.if "${.MAKE.OS}" == "Darwin"
	for lib in ${CLT_STUB_LIBS}; do \
	  if [ -f ${CLTSDK}/usr/lib/$$lib ]; then \
	    cp -f ${CLTSDK}/usr/lib/$$lib ${RAVYN_SDKROOT}/usr/lib/; \
	  fi; \
	done
.for fmwk in ${CLT_STUB_FMWKS}
	if [ -f ${CLTSDK}/System/Library/Frameworks/${fmwk}.framework/${fmwk}.tbd ]; then \
	  mkdir -p ${RAVYN_SDKROOT}/System/Library/Frameworks/${fmwk}.framework; \
	  cp -f ${CLTSDK}/System/Library/Frameworks/${fmwk}.framework/${fmwk}.tbd \
	    ${RAVYN_SDKROOT}/System/Library/Frameworks/${fmwk}.framework/; \
	fi
.endfor
.endif
	mkdir -p ${RAVYN_SDKROOT}/System/Library/Frameworks/CoreFoundation.framework/Headers
.for hdr in ${CF_HEADERS}
	cp -f ${CF_SOURCE}/${hdr} \
		${RAVYN_SDKROOT}/System/Library/Frameworks/CoreFoundation.framework/Headers/
.endfor
	# sys/cdefs.h:806 does #include <sys/_posix_availability.h> for
	# non-KERNEL compiles.  The only copies in this tree are under
	# System.framework PrivateHeaders (identical to
	# BSD/include/apple/sys/_posix_availability.h) and nothing installs
	# them into usr/include/sys, where cdefs.h looks; expose it like a
	# stock Apple SDK does.
	cp -f ${.CURDIR}/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders/sys/_posix_availability.h \
		${RAVYN_SDKROOT}/usr/include/sys/
	${CLT_LIBSYSTEM_COPY}
	cp -f ${ROOT_SOURCE_DIR}/Libraries/Libsystem/private/pthread/tsd_private.h \
		${RAVYN_SDKROOT}/usr/local/include/pthread/
	${ROOT_SOURCE_DIR}/Kernel/xnu/bsd/sys/make_symbol_aliasing.sh \
		${RAVYN_SDKROOT} ${RAVYN_SDKROOT}/usr/include/sys/_symbol_aliasing.h
	env SRCROOT=${libc_path} DSTROOT=${RAVYN_SDKROOT} \
		SDK_INSTALL_HEADERS_ROOT=. "PATH=${TOOLS}:${PATH}" ARCHS="${CpuArch} " \
		VARIANT_PLATFORM_NAME=macosx bash ${libc_path}/xcodescripts/headers.sh
	cd ${ROOT_SOURCE_DIR}/Libraries/Libsystem/libsystem_pthread && \
		env DSTROOT=${RAVYN_SDKROOT} SDK_INSTALL_HEADERS_ROOT=. \
		sh xcodescripts/install-sys-headers.sh
	mkdir -p ${RAVYN_SDKROOT}/usr/include/objc
	cp -fR ${objc_path}/runtime/runtime.h ${objc_path}/runtime/objc.h \
		${objc_path}/runtime/objc-sync.h ${objc_path}/runtime/objc-exception.h \
		${objc_path}/runtime/objc-auto.h ${objc_path}/runtime/objc-api.h \
		${objc_path}/runtime/message.h ${objc_path}/runtime/NSObject.h \
		${objc_path}/runtime/NSObjCRuntime.h ${objc_path}/runtime/Module \
		${RAVYN_SDKROOT}/usr/include/objc/
	mkdir -p ${RAVYN_SDKROOT}/usr/local/include/objc
	cp -f ${objc_path}/runtime/maptable.h ${objc_path}/runtime/NSObject-internal.h \
		${objc_path}/runtime/objc-abi.h ${objc_path}/runtime/objc-block-trampolines.h \
		${objc_path}/runtime/objc-gdb.h ${objc_path}/runtime/objc-internal.h \
		${objc_path}/runtime/objc-ptrauth.h ${objc_path}/runtime/hashtable.h \
		${objc_path}/runtime/hashtable2.h ${objc_path}/runtime/objc-class.h \
		${RAVYN_SDKROOT}/usr/local/include/objc/

SUBDIR = rpcsvc

.include <bsd.subdir.mk>

