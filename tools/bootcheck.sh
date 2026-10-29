#!/bin/sh
# Portable boot smoke test: runs an ISO under QEMU headless and checks the
# serial log for a boot milestone. macOS + Linux, no root required.
#
# Usage: bootcheck.sh <image.iso> [milestone-marker]
#
# Environment:
#   BOOTCHECK_TIMEOUT  seconds to wait for the milestone (default 30)
#   BOOTCHECK_MEM      guest RAM (default 2G; the stub fits in far less)
#
# Exit: 0 = milestone seen, 1 = timeout/failure. Serial log path is printed
# and preserved for debugging.

set -u

ISO=${1:?usage: bootcheck.sh <image.iso> [milestone-marker]}
MILESTONE=${2:-RAVYNOS-BOOTCHECK-STUB-OK}
TIMEOUT=${BOOTCHECK_TIMEOUT:-30}
MEM=${BOOTCHECK_MEM:-2G}

for tool in qemu-system-x86_64; do
    command -v "$tool" >/dev/null 2>&1 || { echo "bootcheck: missing tool: $tool" >&2; exit 2; }
done

OUTDIR=$(mktemp -d "${TMPDIR:-/tmp}/ravynos-bootcheck.XXXXXX")
LOG="$OUTDIR/serial.log"
QEMU_LOG="$OUTDIR/qemu.log"

QPID=
cleanup() {
    if [ -n "$QPID" ]; then
        kill "$QPID" 2>/dev/null
        wait "$QPID" 2>/dev/null
    fi
    return 0
}
trap cleanup EXIT INT TERM

# Headless: no display, serial to file, monitor off. SeaBIOS prints nothing
# on serial by default — any serial output comes from the image itself.
# QEMU diagnostics go to their own file so the verdict stays one line.
qemu-system-x86_64 -m "$MEM" -cdrom "$ISO" \
    -display none -monitor none \
    -serial "file:$LOG" -no-reboot 2>"$QEMU_LOG" &
QPID=$!

elapsed=0
while [ "$elapsed" -lt "$TIMEOUT" ]; do
    if grep -Fq -- "$MILESTONE" "$LOG" 2>/dev/null; then
        echo "BOOTCHECK PASS: '$MILESTONE' seen after ${elapsed}s"
        echo "serial log: $LOG"
        exit 0
    fi
    if ! kill -0 "$QPID" 2>/dev/null; then
        echo "BOOTCHECK FAIL: QEMU exited before '$MILESTONE' appeared"
        echo "serial log: $LOG"
        echo "qemu log: $QEMU_LOG"
        [ -s "$LOG" ] && { echo "---- serial tail ----"; tail -20 "$LOG"; }
        [ -s "$QEMU_LOG" ] && { echo "---- qemu tail ----"; tail -20 "$QEMU_LOG"; }
        exit 1
    fi
    elapsed=$((elapsed + 1))
    sleep 1
done

echo "BOOTCHECK FAIL: '$MILESTONE' not seen within ${TIMEOUT}s"
echo "serial log: $LOG"
[ -f "$LOG" ] && { echo "---- serial tail ----"; tail -20 "$LOG"; }
exit 1
