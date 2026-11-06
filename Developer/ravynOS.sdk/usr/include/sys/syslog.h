/*
 * syslog.h - system log interface (stub for standalone build)
 */

#ifndef	_SYS_SYSLOG_H_
#define _SYS_SYSLOG_H_

#include <sys/types.h>
#include <stdarg.h>

/* Priority levels */
#define	LOG_EMERG	0	/* system is unusable */
#define	LOG_ALERT	1	/* action must be taken immediately */
#define	LOG_CRIT	2	/* critical conditions */
#define	LOG_ERR		3	/* error conditions */
#define	LOG_WARNING	4	/* warning conditions */
#define	LOG_NOTICE	5	/* normal but significant condition */
#define	LOG_INFO	6	/* informational */
#define	LOG_DEBUG	7	/* debug-level messages */

/* Facility codes */
#define	LOG_KERN	(0<<3)
#define	LOG_USER	(1<<3)
#define	LOG_MAIL	(2<<3)
#define	LOG_DAEMON	(3<<3)
#define	LOG_AUTH	(4<<3)
#define	LOG_SYSLOG	(5<<3)
#define	LOG_LPR		(6<<3)
#define	LOG_NEWS	(7<<3)
#define	LOG_UUCP	(8<<3)
#define	LOG_CRON	(9<<3)
#define	LOG_AUTHPRIV	(10<<3)
#define	LOG_FTP		(11<<3)

/* Options for openlog */
#define	LOG_PID		0x01	/* log the pid with each message */
#define	LOG_CONSOLE	0x02	/* log on the system console */
#define	LOG_CONS	LOG_CONSOLE	/* deprecated synonym for LOG_CONSOLE */
#define	LOG_NDELAY	0x04	/* don't delay open */
#define	LOG_ODELAY	0x08	/* obsolete */
#define	LOG_NOWAIT	0x10	/* don't wait for child process to exit */

#define	LOG_PRIMASK	0x07
#define	LOG_PRI(p)	((p) & LOG_PRIMASK)
#define	LOG_MAKEPRI(fac, pri)	(((fac) | (pri)) & 0xff)
#define	LOG_MASK(pri)	(1 << (pri))
#define	LOG_UPTO(pri)	((1 << ((pri) + 1)) - 1)

#ifdef __cplusplus
extern "C" {
#endif

int	openlog(const char *, int, int);
void	syslog(int, const char *, ...) __attribute__((__printf__(2, 3)));
void	vsyslog(int, const char *, va_list) __attribute__((__printf__(2, 0)));
void	closelog(void);
int	setlogmask(int);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_SYSLOG_H_ */
