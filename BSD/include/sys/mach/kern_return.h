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
 * @OSF_COPYRIGHT@
 */
/*
 * Mach kernel return values
 */

#ifndef	__MACH_KERN_RETURN_H__
#define __MACH_KERN_RETURN_H__

#include <mach/boolean.h>
#include <mach/std_types.h>

/*
 * Return type.  This is a machine dependent type.
 */
typedef int		kern_return_t;

/*
 *	Standard return values
 */
#define	KERN_SUCCESS		0
#define	KERN_INVALID_ADDRESS	1
#define	KERN_INVALID_MEMORY	1
#define	KERN_INVALID_RIGHT	2
#define	KERN_INVALID_ARGUMENT	3
#define	KERN_FAILURE		4
#define	KERN_INVALID_REPLY	5
#define	KERN_INVALID_PORT	6
#define	KERN_INVALID_CAPABILITY	7
#define	KERN_INVALID_VALUE	8
#define	KERN_UREFERENCE		9
#define	KERN_NO_SPACE		10
#define	KERN_INVALID_TASK	11
#define	KERN_INVALID_THREAD	12
#define	KERN_INVALID_PORT_SET	13
#define	KERN_INVALID_PURGABLE_DEST	14
#define	KERN_INVALID_CTL	15
#define	KERN_PRIV_INVALID	16

#define	KERN_RESOURCE_SHORTAGE		100
#define	KERN_NOT_RECEIVER		101
#define	KERN_NO_ACCESS			102
#define	KERN_TIMEOUT			103
#define	KERN_NOT_WAITING		104
#define	KERN_ABORTED			105

#define	KERN_INVALID_NOTIFY		106
#define	KERN_INVALID_DATA		107
#define	KERN_PROTECTION_FAILURE	108
#define	KERN_NO_DATA			109
#define	KERN_INVALID_ADDRESS_FILTER	110
#define	KERN_INVALID_PID		113

#define	KERN_IO_ERROR			128

#endif	/* __MACH_KERN_RETURN_H__ */
