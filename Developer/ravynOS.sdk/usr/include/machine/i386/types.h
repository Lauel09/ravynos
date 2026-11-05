/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
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
#ifndef _I386_TYPES_H_
#define _I386_TYPES_H_

/*
 * Base C integer types defined here directly (using plain C types that are
 * always available under -nostdinc) so that <machine/types.h> can be included
 * before <machine/_types.h> (which also defines __uint8_t etc.).  x86_64 is
 * LP64.
 */
typedef unsigned char   __uint8_t;
typedef unsigned short  __uint16_t;
typedef unsigned int    __uint32_t;
typedef unsigned long long __uint64_t;   /* match flat i386/_types.h */

/* ptr1 - ptr2 (consumed by sys/_types/_ptrdiff_t.h via <stddef.h>).
 * Match flat i386/_types.h (from xnu, installed by headers.sh) which uses
 * __PTRDIFF_TYPE__ with a long fallback, so both headers agree when
 * included in either order. */
#if defined(__GNUC__) && defined(__PTRDIFF_TYPE__)
typedef __PTRDIFF_TYPE__        __darwin_ptrdiff_t;
#else
typedef long                    __darwin_ptrdiff_t;
#endif

/* intptr_t (consumed by sys/_types/_intptr_t.h) */
#ifndef __darwin_intptr_t
typedef long            __darwin_intptr_t;
#endif

/* sizeof() (consumed by sys/_types/_rsize_t.h, _sigaltstack.h, _ucontext.h,
   _ucontext64.h).  Match flat i386/_types.h (from xnu, installed by
   headers.sh) which uses __SIZE_TYPE__ with an unsigned long fallback. */
#if defined(__SIZE_TYPE__)
typedef __SIZE_TYPE__           __darwin_size_t;
#else
typedef unsigned long           __darwin_size_t;
#endif

/* va_list: __builtin_va_list is a clang builtin even under -nostdinc
   (consumed by sys/_types/_va_list.h).  Match flat i386/_types.h. */
#if (__GNUC__ > 2)
typedef __builtin_va_list       __darwin_va_list;       /* va_list */
#else
typedef void *                  __darwin_va_list;       /* va_list */
#endif

/* byte count or error (consumed by sys/_types/_ssize_t.h) */
#ifndef __darwin_ssize_t
typedef long            __darwin_ssize_t;
#endif

/* time() (consumed by sys/_types/_time_t.h) */
#ifndef __darwin_time_t
typedef long            __darwin_time_t;
#endif

/* clock() (consumed by sys/_types/_clock_t.h) */
#ifndef __darwin_clock_t
typedef unsigned long   __darwin_clock_t;
#endif

/* socklen_t (duh) (consumed by sys/_types/_socklen_t.h) */
#ifndef __darwin_socklen_t
typedef unsigned int    __darwin_socklen_t;
#endif

/* BSD u_int* types (u_int64_t etc. consumed by sys/types.h) */
#ifndef u_int8_t
typedef __uint8_t       u_int8_t;
#endif
#ifndef u_int16_t
typedef __uint16_t      u_int16_t;
#endif
#ifndef u_int32_t
typedef __uint32_t      u_int32_t;
#endif
#ifndef u_int64_t
typedef __uint64_t      u_int64_t;
#endif

#endif /* _I386_TYPES_H_ */
