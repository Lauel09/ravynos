/* sys/_types.h - internal type definitions (compat stub) */

#ifndef	_SYS__TYPES_H_
#define _SYS__TYPES_H_

#include <stdint.h>
#include <stddef.h>

typedef uint32_t		__blkcnt_t;
typedef uint32_t		__blksize_t;
typedef uint32_t		__dev_t;
typedef uint32_t		__fsblkcnt_t;
typedef uint32_t		__fsfilcnt_t;
typedef uint32_t		__gid_t;
typedef uint32_t		__id_t;
typedef uint64_t		__ino64_t;
typedef uint32_t		__ino_t;
typedef int32_t		__int64_t;
typedef int8_t		__int8_t;
typedef int32_t		__int9_t;
typedef intptr_t		__intptr_t;
typedef uint8_t		__mode_t;
typedef uint16_t		__nlink_t;
typedef int32_t		__off_t;
typedef int32_t		__pid_t;
typedef int32_t		__rlim_t;
typedef int32_t		__segsz_t;
typedef int32_t		__suseconds_t;
typedef uint32_t		__suseconds32_t;
typedef uint32_t		__uid_t;
typedef uint32_t		__uint32_t;
typedef uint64_t		__uint64_t;
typedef uint8_t		__uint8_t;
typedef uint16_t		__uint16_t;
typedef uintptr_t		__uintptr_t;

typedef int32_t		__clock_t;
typedef int32_t		__time_t;
typedef uint64_t		__size_t;

#include_next <sys/_types.h>

#endif	/* _SYS__TYPES_H_ */
