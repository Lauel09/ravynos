#!/bin/sh
# Shared POSIX-sh helpers for the portable pipeline.
#
# Sourced by tools/mkimage.sh, tools/bootcheck.sh, tools/pipeline-selftest.sh
# and anything else under tools/ that has to run identically on macOS and
# Linux.  Keep this file dependency-free: sh, coreutils/POSIX tools, and the
# occasional `python3`.  No bashisms, no GNU-only flags.

# --------------------------------------------------------------------------
# Basic output
# --------------------------------------------------------------------------
# Colour only when stdout is a terminal and the caller did not ask for quiet.
if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_RESET=$(printf '\033[0m')
    C_RED=$(printf '\033[31m')
    C_YELLOW=$(printf '\033[33m')
    C_GREEN=$(printf '\033[32m')
    C_DIM=$(printf '\033[2m')
else
    C_RESET= C_RED= C_YELLOW= C_GREEN= C_DIM=
fi

# log MESSAGE...  Informational line, suppressed by QUIET=1.
log() {
    [ "${QUIET:-0}" = "1" ] && return 0
    printf '%s\n' "$*"
}

# step MESSAGE...  A visible phase marker; never suppressed.
step() {
    printf '%s==>%s %s\n' "$C_GREEN" "$C_RESET" "$*"
}

# warn MESSAGE...  Non-fatal problem.
warn() {
    printf '%swarning:%s %s\n' "$C_YELLOW" "$C_RESET" "$*" >&2
}

# die MESSAGE...  Fatal problem; exits 1 (or $2 when given).
die() {
    printf '%serror:%s %s\n' "$C_RED" "$C_RESET" "$*" >&2
    exit "${2:-1}"
}

# --------------------------------------------------------------------------
# Tool discovery
# --------------------------------------------------------------------------
# have COMMAND  true when the command exists and is executable.
have() {
    command -v "$1" >/dev/null 2>&1
}

# need COMMAND...  Die unless every command is available.
need() {
    missing=
    for tool in "$@"; do
        have "$tool" || missing="$missing $tool"
    done
    [ -z "$missing" ] || die "missing required tool(s):$missing"
}

# tool_version COMMAND  first line of --version output, or "unknown".
tool_version() {
    if have "$1"; then
        "$1" --version 2>/dev/null | head -1 || echo "unknown"
    else
        echo "absent"
    fi
}

# --------------------------------------------------------------------------
# Repository facts
# --------------------------------------------------------------------------
# repo_root  absolute path of the checkout containing this script.
#
# This used to be `dirname $0 /../..`, which is right for a script in
# tools/lib/ and wrong by one level for a script in tools/ itself.  Every
# tools/*.sh caller therefore got the directory *above* the checkout:
# macbuild.sh ran `bmake world` in the repository's parent, where there is no
# Makefile, and VERSION/git-sha lookups silently produced "unknown".  Walk up
# to the top level instead, identified by the two things only the root has.
repo_root() {
    [ -n "${RAVYNOS_ROOT:-}" ] && { printf '%s\n' "$RAVYNOS_ROOT"; return 0; }
    case $0 in
        */*) _dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) ;;
        *)   _dir=$(pwd) ;;
    esac
    _p=$_dir
    while [ -n "$_p" ] && [ "$_p" != "/" ]; do
        if [ -f "$_p/Makefile" ] && [ -d "$_p/BSD/share/mk" ]; then
            printf '%s\n' "$_p"
            return 0
        fi
        _p=$(CDPATH= cd -- "$_p/.." && pwd)
    done
    CDPATH= cd -- "$_dir/../.." && pwd
}

# prod_version  the PROD_VERSION string from the top-level Makefile.
prod_version() {
    _root=$(repo_root)
    sed -n 's/^PROD_VERSION[[:space:]]*=[[:space:]]*//p' "$_root/Makefile" 2>/dev/null |
        head -1 | tr -d '[:space:]'
}

# git_sha [LENGTH]  short git SHA of the source tree, or "unknown".
git_sha() {
    _len=${1:-7}
    _root=$(repo_root)
    _sha=$(git -C "$_root" rev-parse --short="$_len" HEAD 2>/dev/null || true)
    printf '%s\n' "${_sha:-unknown}"
}

# default_epoch  a reproducible build timestamp.
#
# SOURCE_DATE_EPOCH wins (the cross-distro reproducibility convention).  With
# no such variable we use the commit time, so image bytes depend only on the
# source commit -- never on when somebody happened to run the build.
default_epoch() {
    if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
        printf '%s\n' "$SOURCE_DATE_EPOCH"
        return 0
    fi
    _epoch=$(git -C "$(repo_root)" log -1 --format=%ct 2>/dev/null || true)
    printf '%s\n' "${_epoch:-315532800}"
}

# epoch_iso8601 EPOCH  UTC timestamp, portable across BSD and GNU date.
epoch_iso8601() {
    date -u -r "$1" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null ||
        date -u -d "@$1" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null ||
        printf '@%s' "$1"
}

# --------------------------------------------------------------------------
# Filesystem helpers
# --------------------------------------------------------------------------
# mktempdir PREFIX  create a temporary directory and print its path.
#
# The caller must pass the result to register_tmpdir: cleanup handlers belong
# to the calling shell, and a trap set inside a command substitution would fire
# when that subshell exits, deleting the directory immediately.
mktempdir() {
    mktemp -d "${TMPDIR:-/tmp}/${1:-ravynos}.XXXXXX" || die "mktemp failed"
}

_TMPDIRS=
cleanup_tmpdirs() {
    _saved_rc=$?
    for _d in $_TMPDIRS; do
        [ -n "$_d" ] && rm -rf "$_d"
    done
    return $_saved_rc
}

# register_tmpdir DIR  remove DIR (and anything else registered) on exit.
register_tmpdir() {
    case " $_TMPDIRS " in
        *" $1 "*) return 0 ;;
    esac
    _TMPDIRS="$_TMPDIRS $1"
    trap cleanup_tmpdirs EXIT INT TERM
}

# unregister_tmpdirs  hand the directories over to the caller (--keep).
unregister_tmpdirs() {
    _TMPDIRS=
    trap - EXIT INT TERM
}

# abspath PATH  absolute path without requiring the target to exist.
abspath() {
    case $1 in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd)" "$1" ;;
    esac
}

# sha256_of FILE  hex digest using whichever tool the host provides.
sha256_of() {
    if have sha256sum; then
        sha256sum "$1" | cut -d' ' -f1
    elif have shasum; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif have openssl; then
        openssl dgst -sha256 "$1" | sed 's/.*= //'
    else
        echo "unavailable"
    fi
}

# human_size BYTES  e.g. "12.3 MiB".
human_size() {
    _b=${1:-0}
    if [ "$_b" -ge 1048576 ]; then
        _m=$((_b / 1048576))
        _r=$(((_b % 1048576) * 10 / 1048576))
        printf '%s.%s MiB' "$_m" "$_r"
    elif [ "$_b" -ge 1024 ]; then
        printf '%s KiB' "$((_b / 1024))"
    else
        printf '%s B' "$_b"
    fi
}

# file_size FILE  size in bytes, portable across BSD and GNU stat.
file_size() {
    # Try BSD stat first, then GNU; each is queried with the other's flag, so
    # the failure path is expected rather than a portability bug.
    # audit:allow gnu-only-flag (deliberate fallback chain)
    stat -f%z "$1" 2>/dev/null || stat -c%s "$1" 2>/dev/null || echo 0
}

# find_python  a Python 3 interpreter, or empty.
find_python() {
    for candidate in python3 python; do
        if have "$candidate"; then
            if "$candidate" -c 'import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)' \
                >/dev/null 2>&1; then
                printf '%s\n' "$candidate"
                return 0
            fi
        fi
    done
    return 1
}

# host_summary  one-line description used in build manifests.
host_summary() {
    # audit:allow arch-assumption (this *describes* the host; it never picks a target)
    printf '%s %s (%s)' "$(uname -s)" "$(uname -r)" "$(uname -m)"
}
