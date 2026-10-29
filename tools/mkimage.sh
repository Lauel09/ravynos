#!/bin/sh
# Portable image builder: macOS + Linux, no root required.
#
# Replaces the FreeBSD-host-only ISO/build.sh for the boot-test pipeline.
# Modes:
#   --stub   minimal El Torito image carrying the boot smoke-test stub
#            (proves the xorriso -> QEMU -> serial-verdict loop; no OS content)
#
# Usage: mkimage.sh [--stub] -o out.iso
#
# Later modes will stage the real kernel/loader/ramdisk from the world build.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODE=stub
OUT=

while [ $# -gt 0 ]; do
    case "$1" in
        --stub) MODE=stub ;;
        -o) OUT=$2; shift ;;
        *) echo "usage: mkimage.sh [--stub] -o out.iso" >&2; exit 2 ;;
    esac
    shift
done
[ -n "$OUT" ] || { echo "mkimage: -o out.iso required" >&2; exit 2; }

case $MODE in
    stub)
        for tool in nasm xorriso; do
            command -v "$tool" >/dev/null 2>&1 || { echo "mkimage: missing tool: $tool" >&2; exit 2; }
        done
        ;;
esac

WORK=$(mktemp -d "${TMPDIR:-/tmp}/ravynos-mkimage.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM
mkdir -p "$WORK/iso/BOOT"

case $MODE in
    stub)
        nasm -f bin -o "$WORK/iso/BOOT/BOOTSTUB.BIN" "$SCRIPT_DIR/bootstub/bootstub.asm"
        xorriso -as mkisofs -quiet -V RAVYNOS-BOOTCHECK -o "$OUT" \
            -b BOOT/BOOTSTUB.BIN -no-emul-boot -boot-load-size 4 \
            -c BOOT/.BOOT.CAT "$WORK/iso"
        ;;
esac

echo "mkimage: wrote $OUT (mode: $MODE)"
