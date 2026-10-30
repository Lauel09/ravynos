#!/bin/sh
# Run the portable pipeline's test suite.
#
# Layers:
#   1. shell syntax      sh -n on every script under tools/
#   2. unit tests        python unittest over the image backends and verifier
#   3. CLI contract      argument handling, --help, root refusal, missing tools
#   4. pipeline selftest structure-only mode (unless --no-pipeline)
#
# No QEMU, xorriso, mtools or root is needed.  Runs in a few seconds on any
# host with a POSIX shell and Python 3.
#
# Usage: run-tests.sh [--no-pipeline] [-v]

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOLS=$(CDPATH= cd -- "$HERE/.." && pwd)
. "$TOOLS/lib/common.sh"

RUN_PIPELINE=1
VERBOSE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-pipeline) RUN_PIPELINE=0 ;;
        -v|--verbose)  VERBOSE=1 ;;
        -h|--help)     sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" 2 ;;
    esac
    shift
done

PYTHON=$(find_python) || die "python3 is required" 2
FAILED=0
TMP=$(mktempdir ravynos-tests)
register_tmpdir "$TMP"

banner() { printf '\n%s== %s%s\n' "$C_GREEN" "$*" "$C_RESET"; }
ok()     { printf '   %sok%s   %s\n' "$C_GREEN" "$C_RESET" "$*"; }
bad()    { printf '   %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$*"; FAILED=$((FAILED + 1)); }

# --------------------------------------------------------------------------
banner "1. shell syntax"
for script in "$TOOLS"/*.sh "$TOOLS"/lib/*.sh "$HERE"/*.sh; do
    [ -f "$script" ] || continue
    # tools/build.sh is a legacy zsh wrapper for the old FreeBSD-based build
    # (see PROGRESS.md); it is not a POSIX script and is not part of the
    # pipeline.  Validate zsh scripts with zsh when it exists.
    if head -1 "$script" | grep -q 'zsh'; then
        if have zsh; then
            if zsh -n "$script" 2>"$TMP/syntax.err"; then
                ok "$(basename "$script") (zsh)"
            else
                bad "$(basename "$script") (zsh)"
                sed 's/^/        /' "$TMP/syntax.err"
            fi
        else
            printf '   skip %s (legacy zsh script; zsh not installed)\n' "$(basename "$script")"
        fi
        continue
    fi
    if sh -n "$script" 2>"$TMP/syntax.err"; then
        ok "$(basename "$script")"
    else
        bad "$(basename "$script")"
        sed 's/^/        /' "$TMP/syntax.err"
    fi
done

# --------------------------------------------------------------------------
banner "2. python unit tests"
if [ "$VERBOSE" = 1 ]; then
    if "$PYTHON" "$HERE/test_images.py" -v; then ok "unit tests"; else bad "unit tests"; fi
else
    if "$PYTHON" "$HERE/test_images.py" >"$TMP/unit.log" 2>&1; then
        count=$(grep -c '\.\.\. ok' "$TMP/unit.log" 2>/dev/null || echo 0)
        ok "$count unit tests"
    else
        bad "unit tests"
        tail -50 "$TMP/unit.log" | sed 's/^/        /'
    fi
fi

# --------------------------------------------------------------------------
banner "3. CLI contract"
expect_fail() {
    description=$1; shift
    if "$@" >"$TMP/cli.out" 2>&1; then
        bad "$description (expected a non-zero exit)"
    else
        ok "$description"
    fi
}
expect_ok() {
    description=$1; shift
    if "$@" >"$TMP/cli.out" 2>&1; then
        ok "$description"
    else
        bad "$description"
        tail -10 "$TMP/cli.out" | sed 's/^/        /'
    fi
}

expect_ok "--help exits 0"        "$TOOLS/mkimage.sh" --help
expect_ok "--list-tools"          "$TOOLS/mkimage.sh" --list-tools
expect_fail "no mode is rejected"  "$TOOLS/mkimage.sh"
expect_fail "bad option is rejected" "$TOOLS/mkimage.sh" --stub --nonsense
expect_fail "root is refused"     env RAVYNOS_ALLOW_ROOT=0 sh -c 'id -u | grep -qx 0 && exec "$0" --stub || exit 1' "$TOOLS/mkimage.sh"
expect_fail "bootcheck needs an image" "$TOOLS/bootcheck.sh"
expect_fail "bootcheck rejects a missing image" "$TOOLS/bootcheck.sh" "$TMP/nope.iso"

# A plan-only run must not produce output files.
expect_ok "--print-plan builds nothing" "$TOOLS/mkimage.sh" --stub --print-plan -o "$TMP/plan/x"
if [ -f "$TMP/plan/x.iso" ]; then bad "--print-plan wrote an image"; else ok "--print-plan wrote no image"; fi

# --------------------------------------------------------------------------
if [ "$RUN_PIPELINE" = 1 ]; then
    banner "4. pipeline self-test (structure only)"
    if "$TOOLS/pipeline-selftest.sh" --no-qemu -q; then
        ok "stub ISO+IMG build, verify, reproducibility"
    else
        bad "pipeline self-test"
    fi
fi

# --------------------------------------------------------------------------
printf '\n'
if [ "$FAILED" = 0 ]; then
    printf '%sALL TESTS PASSED%s\n' "$C_GREEN" "$C_RESET"
    exit 0
fi
printf '%s%d TEST GROUP(S) FAILED%s\n' "$C_RED" "$FAILED" "$C_RESET"
exit 1
