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
 * __darwin_* types expected by the <sys/_types/*.h> sub-headers, each of
 * which includes <machine/types.h> for them.  x86_64 is LP64.
 * Guarded with #ifndef so this header can be included alongside
 * <machine/_types.h> (which defines some of the same types) without a
 * redefinition error.
 */

/* ptr1 - ptr2 (consumed by sys/_types/_ptrdiff_t.h via <stddef.h>) */
#ifndef __darwin_ptrdiff_t
typedef long            __darwin_ptrdiff_t;
#endif

/* intptr_t (consumed by sys/_types/_intptr_t.h) */
#ifndef __darwin_intptr_t
typedef long            __darwin_intptr_t;
#endif

/* sizeof() (consumed by sys/_types/_rsize_t.h, _sigaltstack.h, _ucontext.h,
   _ucontext64.h) */
#ifndef __darwin_size_t
typedef unsigned long   __darwin_size_t;
#endif

/* va_list: __builtin_va_list is a clang builtin even under -nostdinc
   (consumed by sys/_types/_va_list.h) */
#ifndef __darwin_va_list
typedef __builtin_va_list   __darwin_va_list;
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

/* opaque mbstate conversion state (consumed by sys/_types/_mbstate_t.h) */
#ifndef __darwin_mbstate_t
typedef union {
    char        __mbstate8[128];
    long long   _mbstateL;                   /* for alignment */
} __mbstate_t;
typedef __mbstate_t     __darwin_mbstate_t;
#endif

#endif /* _I386_TYPES_H_ */
