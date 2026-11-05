/*
 * termcap_stub.c - stub implementations of termcap functions for libedit.
 *
 * libedit is built without -lncurses/-lcurses; the termcap functions it uses
 * (tgetent, tgetflag, tgetnum, tgetstr, tgoto, tputs) are declared extern in
 * terminal.c.  These stubs provide minimal implementations so libedit links
 * and can be used in single-user mode.  They are intentionally minimal:
 * tgetent reports "no terminal" (callers check the return), tgetflag/tgetnum
 * return 0/not found, tgetstr returns NULL, tgoto returns the capability
 * string unchanged, and tputs is a no-op.
 */
#include <stdio.h>

int
tgetent(char *bp, const char *name)
{
	(void)bp; (void)name;
	return -1;              /* no termcap entry found */
}

int
tgetflag(char *id)
{
	(void)id;
	return 0;               /* boolean capability not present */
}

int
tgetnum(char *id)
{
	(void)id;
	return -1;              /* numeric capability not present */
}

char *
tgetstr(char *id, char **area)
{
	(void)id; (void)area;
	return NULL;            /* string capability not present */
}

char *
tgoto(const char *cap, int col, int row)
{
	(void)cap; (void)col; (void)row;
	return (char *)cap;     /* no padding, return as-is */
}

int
tputs(const char *str, int affcnt, int (*putc)(int))
{
	(void)str; (void)affcnt; (void)putc;
	return 0;               /* no-op */
}
