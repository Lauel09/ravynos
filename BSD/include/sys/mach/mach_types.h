/*
 * Mach kernel interface type declarations (C header stub)
 */

#ifndef	__MACH_MACH_TYPES_H__
#define __MACH_MACH_TYPES_H__

#include <mach/std_types.h>

typedef int32_t		kern_return_t;
typedef mach_port_t	task_t;
typedef mach_port_t	thread_t;
typedef mach_port_t	vm_map_t;
typedef mach_port_t	host_t;
typedef mach_port_t	processor_t;
typedef mach_port_t	processor_set_t;
typedef mach_port_t	ledger_t;
typedef mach_port_t	memory_object_t;
typedef struct {
	uint32_t		val[2];
}			security_token_t;
typedef struct {
	uint32_t		val[8];
}			audit_token_t;
typedef struct {
	integer_t		seconds;
	integer_t		microseconds;
}			time_value_t;

#endif	/* __MACH_MACH_TYPES_H__ */
