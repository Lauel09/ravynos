/*
 * Copyright (c) 2000-2006 Apple Computer, Inc. All rights reserved.
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
 * terms of an Apple operating system software license.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * Mach standard types (C header version)
 */

#ifndef	__MACH_STD_TYPES_H__
#define __MACH_STD_TYPES_H__

#include <stdint.h>

typedef int32_t		integer_t;
typedef uint32_t	natural_t;
typedef uint64_t	vm_address_t;
typedef uint64_t	vm_offset_t;
typedef uint64_t	vm_size_t;
typedef uint32_t	mach_port_type_t;
typedef uint32_t	mach_msg_size_t;
typedef uint32_t	mach_msg_type_name_t;
typedef int32_t		mach_msg_options_t;
typedef uint32_t	mach_msg_bits_t;
typedef int32_t		mach_msg_id_t;
typedef uint32_t	mach_port_rights_t;
typedef uint32_t	mach_port_seqno_t;
typedef uint32_t	mach_port_mscount_t;
typedef uint32_t	mach_port_msgcount_t;
typedef uint32_t	mach_port_name_t;
typedef uint32_t	mach_port_t;
typedef uint32_t	mach_port_right_t;
typedef uint32_t	mach_port_urefs_t;
typedef uint32_t	mach_port_delta_t;
typedef int32_t		kern_return_t;
typedef int			boolean_t;

/*
 * Mach message header (used by mach_msg traps)
 */
typedef struct {
	mach_msg_bits_t	bits;
	mach_msg_size_t	size;
	mach_port_t	remote_port;
	mach_port_t	local_port;
	mach_port_t	voucher_port;
	mach_msg_id_t	id;
} mach_msg_header_t;

#define	TRUE		1
#define	FALSE		0

#endif	/* __MACH_STD_TYPES_H__ */
