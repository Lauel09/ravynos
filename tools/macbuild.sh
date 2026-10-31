#!/bin/sh
# macOS-aware driver for the BSD-make world build.
#
# `bmake world` works, but on a developer Mac it fails in ways that need
# context: a missing Homebrew tool, a stale toolchain from the wrong target
# arch, a case-insensitive filesystem, or six hours of LLVM that then dies in
# cctools.  This wrapper adds the preflight checks, logging, per-stage resume
# and a one-line summary that make the Phase 1 loop bearable.  It never
# changes what is built: it runs exactly `bmake <stage>`.
#
# Usage:
#   tools/macbuild.sh [options]
#
# Options:
#   --stage NAME        version | Developer | Kernel | Libraries | BSD | world
#                       (default world)
#   --target-arch ARCH  x86_64 (default) or arm64
#   --jobs N            pass -j N (default: number of CPUs)
#   --log PATH          tee the build into PATH (default ../build-<stage>.log)
#   --dry-run           bmake -n only; parses the makefiles, builds nothing
#   --fresh             ignore the recorded stage state and start over
#   --audit             run the portability audit first and fail on findings
#   --keep-going        pass -k to bmake
#   --no-preflight      skip the host checks (for CI images that lack a tool)
#   -q                  quiet (no progress chatter; build output still shown)
#
# State is kept in <build dir>/.macbuild-state as "stage <epoch>" lines so an
# interrupted run can resume with `--stage` and an operator can see how long
# each stage took.
#
# Exit status: bmake's status; 2 for usage/preflight errors.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/lib/common.sh"

ROOT=$(repo_root)
STAGE=world
ARCH=x86_64
JOBS=
LOG=
DRY_RUN=0
FRESH=0
AUDIT=0
KEEP_GOING=0
PREFLIGHT=1
QUIET=${QUIET:-0}

while [ $# -gt 0 ]; do
    case "$1" in
        --stage)         STAGE=$2; shift ;;
        --target-arch)   ARCH=$2; shift ;;
        --jobs|-j)       JOBS=$2; shift ;;
        --log)           LOG=$2; shift ;;
        --dry-run|-n)    DRY_RUN=1 ;;
        --fresh)         FRESH=1 ;;
        --audit)         AUDIT=1 ;;
        --keep-going|-k) KEEP_GOING=1 ;;
        --no-preflight)  PREFLIGHT=0 ;;
        -q|--quiet)      QUIET=1 ;;
        -h|--help)       sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (try --help)" 2 ;;
    esac
    shift
done

case $STAGE in
    version|Developer|Kernel|Libraries|BSD|world) ;;
    *) die "unknown stage: $STAGE (version|Developer|Kernel|Libraries|BSD|world)" 2 ;;
esac
case $ARCH in
    x86_64|arm64|aarch64) ;;
    *) die "unsupported --target-arch: $ARCH (x86_64|arm64)" 2 ;;
esac
[ "$ARCH" = aarch64 ] && ARCH=arm64

BUILD_DIR=$ROOT/../build
STATE=$BUILD_DIR/.macbuild-state
[ -n "$LOG" ] || LOG="$(dirname "$ROOT")/build-$STAGE.log"

# The tree ships its own BSD make framework in BSD/share/mk (101 files, the
# full FreeBSD set).  The top-level Makefile adds it via `.SYSPATH:`, which is
# too late for sys.mk itself: without an explicit search path bmake reads a
# host sys.mk (or none) and then dies with
#     "Could not find bsd.mkopt.mk" / "bsd.opts.mk" / "bsd.init.mk"
#     "Fatal errors encountered -- cannot continue"
# which is exactly what `bmake world` did on both CI runners -- in five
# minutes, before it could build anything.  Setting MAKESYSPATH here also
# gives the preflight its answer, so a broken tree is reported as broken
# instead of as a host problem.
if [ -d "$ROOT/BSD/share/mk" ]; then
    MAKESYSPATH=$ROOT/BSD/share/mk
    export MAKESYSPATH
fi

# --------------------------------------------------------------------------
# Preflight
# --------------------------------------------------------------------------
preflight() {
    step "preflight ($(host_summary))"
    failures=0
    note() { printf '  %-34s %s\n' "$1" "$2"; }
    problem() { printf '  %-34s %s\n' "$1" "$2"; failures=$((failures + 1)); }

    if have bmake; then
        # 2>&1, not 2>/dev/null: "bmake: stopped in ..." with the reason
        # discarded is how a fatal makefile error looked like a missing tool.
        if bmake_version=$(bmake -V MAKE_VERSION 2>&1); then
            # Keep only the version number: the host's linker probe can emit
            # warnings on the same stream.
            note "bmake" "$(printf '%s\n' "$bmake_version" | tail -1)"
        else
            note "bmake" "present but cannot read this tree:"
            printf '%s\n' "$bmake_version" | head -6 | sed 's/^/        /'
            failures=$((failures + 1))
        fi
    fi
    if ! have bmake; then problem "bmake" "MISSING -- brew install bmake"; fi

    if have gmake; then note "gmake (GNU make)" "$(gmake --version 2>/dev/null | head -1)"
    else note "gmake (GNU make)" "missing (needed by the LLVM configure step)"; fi

    for tool in cmake ninja; do
        if have "$tool"; then note "$tool" "$("$tool" --version 2>/dev/null | head -1)"
        else note "$tool" "missing"; fi
    done

    py=$(find_python) || py=
    if [ -n "$py" ]; then note "python3" "$("$py" --version 2>&1)"
    else problem "python3" "MISSING"; fi

    # yacc/lex: Xcode's shims refuse to run with Command Line Tools only.
    if have bison && have flex; then
        note "bison/flex" "$(bison --version 2>/dev/null | head -1)"
    else
        note "bison/flex" "missing -- the yacc/lex shims will fail with CLT-only hosts"
    fi

    if [ "$(uname -s)" = Darwin ]; then
        if xcode-select -p >/dev/null 2>&1; then
            note "Xcode toolchain" "$(xcode-select -p)"
        else
            problem "Xcode toolchain" "no active developer directory"
        fi
    fi

    # Case-insensitive APFS collides with kernel/userland sources that differ
    # only by case; the build must live on a case-sensitive volume.
    probe="$ROOT/.case-probe-$$"
    if printf x > "$probe" 2>/dev/null; then
        if [ -e "$(printf '%s' "$probe" | tr 'a-z' 'A-Z')" ]; then
            warn "source tree is on a case-insensitive filesystem; a full world build may collide"
            note "filesystem" "case-insensitive (consider a case-sensitive sparse bundle)"
        else
            note "filesystem" "case-sensitive (good)"
        fi
        rm -f "$probe"
    fi

    if [ -d "$BUILD_DIR/Developer" ]; then
        size=$(du -sh "$BUILD_DIR/Developer" 2>/dev/null | cut -f1)
        note "cached toolchain" "$size in ${BUILD_DIR#"$ROOT"/}"
    else
        note "cached toolchain" "none (LLVM will be built: expect hours)"
    fi

    if [ "$(uname -s)" = Darwin ]; then
        avail=$(df -Pm "$(dirname "$BUILD_DIR")" 2>/dev/null | awk 'NR==2 {print $4}')
        if [ -n "${avail:-}" ] && [ "$avail" -lt 20480 ]; then
            warn "only ${avail} MiB free next to the build directory; LLVM needs ~20 GiB"
        else
            note "free space" "${avail:-unknown} MiB"
        fi
    fi

    if [ "$failures" -gt 0 ]; then
        die "$failures preflight check(s) failed; install the tools listed above" 2
    fi
}

# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------
if [ "$FRESH" = 1 ] && [ -f "$STATE" ]; then
    log "clearing recorded stage state"
    rm -f "$STATE"
fi
if [ -f "$STATE" ] && [ "$DRY_RUN" = 0 ]; then
    log "previous stages:"
    sed 's/^/    /' "$STATE"
fi

record_stage() {
    mkdir -p "$BUILD_DIR" 2>/dev/null || true
    printf '%s %s\n' "$1" "$(date +%s)" >> "$STATE" 2>/dev/null || true
}

JOBS_ARG=
if [ -n "$JOBS" ]; then
    JOBS_ARG="-j$JOBS"
else
    cpus=$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2) )
    JOBS_ARG="-j$cpus"
fi

# --------------------------------------------------------------------------
# Audit
# --------------------------------------------------------------------------
if [ "$AUDIT" = 1 ]; then
    step "portability audit"
    PY=$(find_python) || die "python3 is needed for --audit" 2
    "$PY" "$SCRIPT_DIR/macbuild-audit.py" --root "$ROOT" --severity high || {
        die "portability audit reported high-severity findings" 2
    }
fi

# --------------------------------------------------------------------------
# Preflight and build
# --------------------------------------------------------------------------
[ "$PREFLIGHT" = 0 ] || preflight

set -- "TARGET_ARCH=$ARCH"
[ "$KEEP_GOING" = 1 ] && set -- "$@" -k
[ "$DRY_RUN" = 1 ] && set -- "$@" -n
[ -n "$JOBS_ARG" ] && set -- "$@" "$JOBS_ARG"
set -- "$@" "$STAGE"

step "bmake $*"
log "  log file: $LOG"
start=$(date +%s)

if [ "$DRY_RUN" = 1 ]; then
    ( cd "$ROOT" && bmake "$@" ) 2>&1 | tee "$LOG"
    rc=$?
    step "dry run finished (exit $rc)"
    exit "$rc"
fi

record_stage "start-$STAGE"
# Capture bmake's exit status, not tee's, while still logging.  The old line
# read ${PIPESTATUS[0]:-$?}, a bash array in a #!/bin/sh script: under dash
# (Linux /bin/sh) it silently evaluated to tee's status, so a failed build
# could be reported as a success.  Use pipefail where it exists, otherwise
# redirect and replay.
if ( set -o pipefail ) 2>/dev/null; then
    set -o pipefail
    ( cd "$ROOT" && bmake "$@" ) 2>&1 | tee "$LOG"
    rc=$?
else
    ( cd "$ROOT" && bmake "$@" ) > "$LOG" 2>&1
    rc=$?
    tail -n +1 "$LOG"
fi

elapsed=$(( $(date +%s) - start ))
if [ "$rc" = 0 ]; then
    record_stage "$STAGE"
    step "bmake $STAGE succeeded in ${elapsed}s"
    [ "$STAGE" = world ] && printf '  artifacts under %s\n' "$BUILD_DIR"
    exit 0
fi

record_stage "failed-$STAGE"
step "bmake $STAGE failed after ${elapsed}s"
# The first error, not just the tail: a build that fails in several places
# ends with "stopped in" noise, and the message that explains it scrolls out
# of the last twenty lines.  This is also what the CI log courier copies, so
# the useful line survives even when the full log does not.
printf '\nFirst errors in %s:\n' "$LOG"
grep -n -m 8 -E 'error:|Error code [0-9]|stop(ped)? in .*|No such file|Fatal errors|cannot find' "$LOG" 2>/dev/null |
    head -10 | cut -c1-240 | sed 's/^/  /' || true
printf '\nLast 20 lines of %s:\n' "$LOG"
tail -20 "$LOG" | sed 's/^/  /'
cat <<EOF

Next steps:
  1. Find the first error in $LOG (search for "Stop." or "Error 1").
  2. Fix it behind a host guard (.if \${.MAKE.OS} == Darwin / uname) so the
     Linux build is unaffected.
  3. Prefix the commit 'macbuild:' and log the failure -> fix pair in
     PROGRESS.md (section 3.2).
  4. Resume with: tools/macbuild.sh --stage $STAGE
EOF
exit "$rc"
