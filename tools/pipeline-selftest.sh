#!/bin/sh
# End-to-end check of the portable image pipeline's own plumbing.
#
# Builds the loader-only stub ISO with mkimage.sh, boots it under QEMU and
# waits for the serial marker. Proves xorriso -> El Torito -> QEMU -> serial
# verdict works on this host without root, mounts or chroot.
#
# Usage: pipeline-selftest.sh
#
# Environment:
#   BOOTCHECK_TIMEOUT  seconds to wait for the marker (default 30)

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/ravynos-pipeline-selftest.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

echo "pipeline-selftest: building stub ISO"
"$SCRIPT_DIR/mkimage.sh" --stub -o "$WORK/stub.iso"

echo "pipeline-selftest: booting stub ISO"
"$SCRIPT_DIR/bootcheck.sh" "$WORK/stub.iso" RAVYNOS-BOOTCHECK-STUB-OK

echo "pipeline-selftest: PASS"
