/*
 * Availability macro stubs for standalone builds.
 * Nullifies Apple availability attributes that the host compiler may not
 * understand when parsing ravynOS SDK headers.
 */
#ifndef __RAVYN_AVAILABILITY_STUB_H__
#define __RAVYN_AVAILABILITY_STUB_H__

#define __OSX_AVAILABLE(...)
#define __IOS_AVAILABLE(...)
#define __TVOS_AVAILABLE(...)
#define __WATCHOS_AVAILABLE(...)
#define __OSX_AVAILABLE_STARTING(...)
#define __IOS_AVAILABLE_STARTING(...)
#define __MAC_OS_X_VERSION_MAX_ALLOWED 109999
#define __IPHONE_OS_VERSION_MAX_ALLOWED 199999

#endif /* __RAVYN_AVAILABILITY_STUB_H__ */
