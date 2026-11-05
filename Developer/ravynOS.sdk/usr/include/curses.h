/*
 * curses.h stub for ravynOS SDK.
 *
 * libedit is built without -lncurses/-lcurses.  Its config.h defines
 * HAVE_CURSES_H / HAVE_NCURSES_H, so it tries to include these headers.
 * Since no curses library is linked, these stubs exist only to satisfy the
 * #include directives; libedit's termcap path uses the functions declared in
 * termcap.h instead.
 */
#ifndef _CURSES_H_
#define _CURSES_H_

#endif /* _CURSES_H_ */
