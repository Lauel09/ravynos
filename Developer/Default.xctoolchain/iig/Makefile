CXX := ${HOST_CXX}
SRCS = iig.cpp
OBJS = iig.o
CXXFLAGS += -std=gnu++17

# iig is a *host* C++ program, so it is built against the host's libc++.  It is
# the only host tool here that instantiates std::ostringstream, and it is the
# only one that failed to link:
#   Undefined symbols for architecture arm64:
#     "std::__1::codecvt<char, char, __mb_state_t>::id", referenced from:
#         std::__1::has_facet[abi:ne200100]<...codecvt<char, char, __mb_state_t>>...
# The mangled name was in fact caused by the __mb_state_t/__mbstate_t mismatch
# documented below -- fixing that is what made the link work, and this flag was
# not the cure.  It is kept because it is still correct: every other target in
# this tree passes -mmacos-version-min (rvn.kext.mk, rvn.framework.mk, the SDK
# recipe's own CLT copies all rely on the same 10.15 baseline, and the top level
# already exports MACOS_VERSION_MIN), and a host tool that links against the
# SDK's libc++ should agree with it on the deployment target.  Apply it to both
# the compile and the link so the two cannot drift apart.
.if defined(MACOS_VERSION_MIN)
CXXFLAGS += -mmacos-version-min=${MACOS_VERSION_MIN}
LDFLAGS  += -mmacos-version-min=${MACOS_VERSION_MIN}
.endif

# iig.cpp includes <sstream>, which pulls in the tree's own <_types.h> through
# the toolchain include directory.  That header used to spell its union
# __mb_state_t while Apple, xnu's bsd/{arm,i386}/_types.h, the cctools ppc copy
# and this SDK's System.framework all spell it __mbstate_t, so the object was
# built against an mbstate_t that is not the one libc++ was compiled with, and
# the link demanded a mangled facet name that does not exist:
#   "std::__1::codecvt<char, char, __mb_state_t>::id", referenced from
#   std::__1::has_facet[abi:ne200100]<... __mb_state_t>...
# The headers are fixed; depend on them so an existing iig.o is rebuilt rather
# than silently reused with the old type (the failing run showed a bare link
# with no compile step).
TOOLCHAIN_HDRS = ${.CURDIR:H}/include/arm/_types.h \
		 ${.CURDIR:H}/include/i386/_types.h \
		 ${.CURDIR:H}/include/machine/_types.h \
		 ${.CURDIR:H}/include/sys/_types.h

all: ${TOOLS}/iig

${OBJS}: ${.CURDIR}/iig.cpp ${TOOLCHAIN_HDRS}

${TOOLS}/iig: ${OBJS}
	${CXX} -o ${.TARGET} ${OBJS} ${LDFLAGS}

