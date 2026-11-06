/*
 * Copyright (c) 2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
#ifndef _SYS_ENDIAN_H_
#define _SYS_ENDIAN_H_
#include <machine/endian.h>
#include <sys/_endian.h>

/*
 * Big-endian / host conversions.  On this little-endian target these are
 * byte swaps; __DARWIN_OSSwapInt* handles the host endianness correctly.
 */
#if defined(__DARWIN_BYTE_ORDER) && __DARWIN_BYTE_ORDER == __DARWIN_LITTLE_ENDIAN
#define be64toh(x)	__DARWIN_OSSwapInt64(x)
#define be32toh(x)	__DARWIN_OSSwapInt32(x)
#define be16toh(x)	__DARWIN_OSSwapInt16(x)
#define htobe64(x)	__DARWIN_OSSwapInt64(x)
#define htobe32(x)	__DARWIN_OSSwapInt32(x)
#define htobe16(x)	__DARWIN_OSSwapInt16(x)
#endif
#endif /* _SYS_ENDIAN_H_ */
