# ncurses uses autotools, which on macOS defaults its C preprocessor to
# /lib/cpp -- a path that does not exist under the CLT-only host toolchain
# (clang -E is the preprocessor there).  libedit in this fork is built
# without -lncurses (termcap functions come from libSystem), so ncurses is
# not needed for the single-user shell milestone on Darwin.  Keep it on Linux
# where /lib/cpp exists and the autotools flow works.
.if "${.MAKE.OS}" == "Darwin"
SUBDIR = bzip2 gzip libedit libutil
.else
SUBDIR = bzip2 gzip ncurses libedit libutil
.endif

.include <bsd.subdir.mk>

