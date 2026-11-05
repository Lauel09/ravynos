/*
 * Mach notification port types and routines (stub for standalone build)
 */

#ifndef	__MACH_NOTIFY_H__
#define __MACH_NOTIFY_H__

#include <mach/std_types.h>

/*
 * Notification routines
 */
extern kern_return_t mach_notify_port_deleted(
	mach_port_t		port,
	mach_port_t		port_name);

extern kern_return_t mach_notify_port_destroyed(
	mach_port_t		port);

extern kern_return_t mach_notify_no_senders(
	mach_port_t		port,
	mach_port_t		port_name);

extern kern_return_t mach_notify_no_senders_once(
	mach_port_t		port,
	mach_port_t		port_name,
	mach_msg_options_t	option);

extern kern_return_t mach_notify_send_possible(
	mach_port_t		port);

extern kern_return_t mach_notify_sync_request(
	mach_port_t		port,
	mach_port_t		*notify_port);

#endif	/* __MACH_NOTIFY_H__ */
