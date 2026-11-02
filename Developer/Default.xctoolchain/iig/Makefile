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
# libc++ declares the codecvt<char, char, mbstate_t> facet as an extern template
# under an ABI tag chosen by the deployment target, so compiling with the
# default target and linking against the SDK's libc++ asks for a mangled name
# that dylib does not export.  Every other target in this tree passes
# -mmacos-version-min (rvn.kext.mk, rvn.framework.mk, and the SDK recipe's own
# CLT copies rely on the same 10.15 baseline, and the top level already exports
# MACOS_VERSION_MIN); the host tools were simply missed.  Apply it to both the
# compile and the link so the two agree.
.if defined(MACOS_VERSION_MIN)
CXXFLAGS += -mmacos-version-min=${MACOS_VERSION_MIN}
LDFLAGS  += -mmacos-version-min=${MACOS_VERSION_MIN}
.endif

all: ${TOOLS}/iig

${TOOLS}/iig: ${OBJS}
	${CXX} -o ${.TARGET} ${OBJS} ${LDFLAGS}

