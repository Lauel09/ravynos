#!/bin/sh
# Run an ISO under QEMU with a window, for manual inspection.
#
# Usage: bootview.sh <image.iso>
#
# Environment:
#   BOOTVIEW_MEM  guest RAM (default 2G)

set -eu

ISO=${1:?usage: bootview.sh <image.iso>}
MEM=${BOOTVIEW_MEM:-2G}

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "bootview: missing qemu-system-x86_64" >&2
    exit 2
}

exec qemu-system-x86_64 -m "$MEM" -cdrom "$ISO" -serial stdio
