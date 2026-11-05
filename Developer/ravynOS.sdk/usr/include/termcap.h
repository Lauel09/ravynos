/*
 * termcap.h stub for ravynOS SDK.
 *
 * libedit is built without -lncurses; termcap functions (tgetent, tgetstr,
 * tgoto, tputs, ...) are declared as extern in libedit/src/terminal.c and are
 * expected to be provided by libSystem at link time.  This stub header exists
 * solely so the #include <termcap.h> guarded by HAVE_TERMCAP_H resolves.
 */
#ifndef _TERMCAP_H_
#define _TERMCAP_H_

#include <sys/cdefs.h>

__BEGIN_DECLS
int	tgetent(char *, const char *);
int	tgetflag(char *);
int	tgetnum(char *);
int	tputs(const char *, int, int (*)(int));
char	*tgoto(const char *, int, int);
char	*tgetstr(char *, char **);
__END_DECLS

#endif /* _TERMCAP_H_ */
