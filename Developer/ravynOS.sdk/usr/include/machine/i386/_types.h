/*
 * Copyright (c) 2003-2007 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
#ifndef _I386__TYPES_H_
#define _I386__TYPES_H_

/*
 * Base C integer types that <sys/_types.h> (included before this header's
 * consumers run) references via typedefs such as __darwin_blkcnt_t.
 * These are not compiler builtins under -nostdint (only __int128_t is), so
 * the arch header provides them.  x86_64 is LP64: int is 32-bit, long is
 * 64-bit.  Matches the arm64 counterpart's __darwin_natural_t width.
 */
typedef signed char     __int8_t;
typedef signed short    __int16_t;
typedef signed int      __int32_t;
typedef signed long     __int64_t;
typedef unsigned char   __uint8_t;
typedef unsigned short  __uint16_t;
typedef unsigned int    __uint32_t;
typedef unsigned long   __uint64_t;

/*
 * The __darwin_* types below are expected by the <sys/_types/*.h> sub-headers,
 * each of which includes <machine/_types.h> for them.  x86_64 is LP64.
 * Guarded with #ifndef so this header can be included alongside
 * <machine/types.h> (which defines some of the same types) without a
 * redefinition error.
 */
#ifndef __darwin_size_t
typedef unsigned long   __darwin_size_t;   /* sizeof()  (via sys/_types/_size_t.h) */
#endif

/* va_list: __builtin_va_list is a clang builtin even under -nostdinc */
#ifndef __darwin_va_list
typedef __builtin_va_list   __darwin_va_list;   /* va_list (via sys/_types/_va_list.h) */
#endif

/* wchar_t / wint_t underlying types: expected by <sys/_types/_wchar_t.h> and
 * <sys/_types/_wint_t.h>, which include <machine/_types.h> for these.
 * On x86_64 macOS wchar_t is a signed 32-bit int; __WCHAR_TYPE__/__WINT_TYPE__
 * are clang builtins that expand to it even under -nostdinc.
 */
#ifndef __darwin_wchar_t
typedef __WCHAR_TYPE__   __darwin_wchar_t;   /* wchar_t */
#endif
#ifndef __darwin_wint_t
typedef __WINT_TYPE__    __darwin_wint_t;    /* wint_t */
#endif

/* ct_rune_t / rune_t: int so EOF (-1) can be naturally assigned. */
typedef int             __darwin_ct_rune_t;  /* ct_rune_t (via sys/_types/_ct_rune_t.h) */
typedef __darwin_wchar_t __darwin_rune_t;    /* rune_t (via sys/_types/_rune_t.h) */

/* natural_t / mach port name type: 32-bit unsigned on x86_64 */
typedef unsigned int    __darwin_natural_t;

/* Miscellaneous scalar types expected from machine/_types.h */
typedef unsigned long   __darwin_clock_t;    /* clock()  (via sys/_types/_clock_t.h) */
typedef long            __darwin_ssize_t;    /* byte count or error  (via sys/_types/_ssize_t.h) */
typedef long            __darwin_time_t;     /* time()  (via sys/_types/_time_t.h) */

#endif /* _I386__TYPES_H_ */
