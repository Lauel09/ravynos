#!/bin/sh
# Portable boot smoke test: runs an ISO under QEMU headless and checks the
# serial log for a boot milestone. macOS + Linux, no root required.
#
# Usage: bootcheck.sh <image.iso> [milestone-marker]
#
# Environment:
#   BOOTCHECK_TIMEOUT  seconds to wait for the milestone (default 30)
#
# Exit: 0 = milestone seen, 1 = timeout/failure. Serial log path is printed
# and preserved for debugging.

set -u

ISO=${1:?usage: bootcheck.sh <image.iso> [milestone-marker]}
MILESTONE=${2:-RAVYNOS-BOOTCHECK-STUB-OK}
TIMEOUT=${BOOTCHECK_TIMEOUT:-30}

for tool in qemu-system-x86_64; do
    command -v "$tool" >/dev/null 2>&1 || { echo "bootcheck: missing tool: $tool" >&2; exit 2; }
done

OUTDIR=$(mktemp -d "${TMPDIR:-/tmp}/ravynos-bootcheck.XXXXXX")
LOG="$OUTDIR/serial.log"

QPID=
cleanup() {
    [ -n "$QPID" ] && kill "$QPID" 2>/dev/null
    return 0
}
trap cleanup EXIT INT TERM

# Headless: no display, serial to file, monitor off. SeaBIOS prints nothing
# on serial by default — any serial output comes from the image itself.
qemu-system-x86_64 -m 512 -cdrom "$ISO" \
    -display none -monitor none \
    -serial "file:$LOG" -no-reboot &
QPID=$!

elapsed=0
while [ "$elapsed" -lt "$TIMEOUT" ]; do
    if grep -q "$MILESTONE" "$LOG" 2>/dev/null; then
        echo "BOOTCHECK PASS: '$MILESTONE' seen after ${elapsed}s"
        echo "serial log: $LOG"
        exit 0
    fi
    elapsed=$((elapsed + 1))
    sleep 1
done

echo "BOOTCHECK FAIL: '$MILESTONE' not seen within ${TIMEOUT}s"
echo "serial log: $LOG"
[ -f "$LOG" ] && { echo "---- serial tail ----"; tail -20 "$LOG"; }
exit 1
