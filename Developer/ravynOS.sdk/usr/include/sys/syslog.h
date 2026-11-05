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

#define	LOG_PRIMASK	0x07
#define	LOG_PRI(p)	((p) & LOG_PRIMASK)
#define	LOG_MAKEPRI(fac, pri)	(((fac) | (pri)) & 0xff)

#ifdef __cplusplus
extern "C" {
#endif

int	openlog(const char *, int, int);
void	syslog(int, const char *, ...) __attribute__((__printf__(2, 3)));
void	closelog(void);
int	setlogmask(int);

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_SYSLOG_H_ */
