#!/bin/sh
# Boot the built XNU kernel through our UEFI loader in QEMU and print a short
# verdict: filtered serial output plus the first CPU exceptions.  This is the
# Docs/DarwinBoot.md §3 fat:rw recipe as one command (D6 inner loop).
#
# Usage: tools/xnu-boot.sh [--kernel PATH] [--secs N] [--cpu MODEL] [--gdb]
#                          [--no-build] [--out DIR]
#   --kernel PATH  Mach-O kernel (default ../build/sysroot/System/Library/Kernels/kernel)
#   --secs N       let QEMU run N seconds, then kill it (default 25)
#   --cpu MODEL    QEMU -cpu model (default Penryn: XNU checks for an Intel vendor)
#   --gdb          start QEMU halted with a gdbstub on :1234 and wait for it to
#                  exit; attach with the lldb line printed below
#   --no-build     reuse tools/bootloader/BOOTX64.EFI instead of rebuilding it
#   --out DIR      work directory (default out/arena/boot)
#
# Files left in --out: serial.log (COM1), qemu-int.log (QEMU -d int,cpu_reset).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT/../build"
KERNEL="$BUILD_DIR/sysroot/System/Library/Kernels/kernel"
EXT_SRC="$BUILD_DIR/sysroot/System/Library/Extensions"
HFS_ENC_SRC="$BUILD_DIR$ROOT/Kernel/Extensions/hfs/hfs_encodings/hfs_encodings.kext"
SECS=25
CPU=Penryn
GDB=0
BUILD=1
OUT="$ROOT/out/arena/boot"
FW=${QEMU_FW_DIR:-/opt/homebrew/share/qemu}

while [ $# -gt 0 ]; do
    case $1 in
        --kernel) KERNEL=$2; shift 2 ;;
        --secs) SECS=$2; shift 2 ;;
        --cpu) CPU=$2; shift 2 ;;
        --gdb) GDB=1; shift ;;
        --no-build) BUILD=0; shift ;;
        --out) OUT=$2; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "xnu-boot: unknown option $1" >&2; exit 2 ;;
    esac
done

[ -f "$KERNEL" ] || { echo "xnu-boot: no kernel at $KERNEL (build it: tools/macbuild.sh --stage Kernel)" >&2; exit 2; }
for f in "$FW/edk2-x86_64-code.fd" "$FW/edk2-i386-vars.fd"; do
    [ -f "$f" ] || { echo "xnu-boot: missing firmware $f (set QEMU_FW_DIR)" >&2; exit 2; }
done

if [ "$BUILD" = 1 ]; then
    "$ROOT/tools/bootloader/build.sh" -q
fi

rm -rf "$OUT"
mkdir -p "$OUT/fat/EFI/BOOT" "$OUT/fat/System/Library/Kernels" "$OUT/fat/System/Library/Extensions"
cp "$ROOT/tools/bootloader/BOOTX64.EFI" "$OUT/fat/EFI/BOOT/BOOTX64.EFI"
cp "$KERNEL" "$OUT/fat/System/Library/Kernels/kernel"
kext_count=0
if [ -d "$EXT_SRC" ]; then
    for kx in "$EXT_SRC"/*.kext; do
        [ -d "$kx" ] || continue
        cp -RL "$kx" "$OUT/fat/System/Library/Extensions/"
        kext_count=$((kext_count + 1))
    done
fi
if [ -d "$HFS_ENC_SRC" ] && [ ! -d "$OUT/fat/System/Library/Extensions/hfs_encodings.kext" ]; then
    cp -RL "$HFS_ENC_SRC" "$OUT/fat/System/Library/Extensions/"
    kext_count=$((kext_count + 1))
fi
find "$OUT/fat/System/Library/Extensions" -name '*.dSYM' -exec rm -rf {} + 2>/dev/null || true
chmod -R u+rwX "$OUT/fat"
cp "$FW/edk2-i386-vars.fd" "$OUT/vars.fd"

echo "xnu-boot: kernel $(shasum -a 256 "$KERNEL" | cut -c1-16) $(ls -l "$KERNEL" | awk '{print $6, $7, $8}')"
echo "xnu-boot: loader $(shasum -a 256 "$OUT/fat/EFI/BOOT/BOOTX64.EFI" | cut -c1-16), kext_bundles $kext_count, cpu $CPU"

set -- qemu-system-x86_64 -machine q35 -cpu "$CPU" -m 2048 \
    -display none -monitor none -no-reboot -no-shutdown \
    -drive if=pflash,format=raw,readonly=on,file="$FW/edk2-x86_64-code.fd" \
    -drive if=pflash,format=raw,file="$OUT/vars.fd" \
    -drive format=raw,file=fat:rw:"$OUT/fat" \
    -serial file:"$OUT/serial.log" \
    -d int,cpu_reset -D "$OUT/qemu-int.log"

if [ "$GDB" = 1 ]; then
    echo "xnu-boot: QEMU halted, gdbstub on localhost:1234. In another terminal:"
    echo "  lldb \"$KERNEL\" -o 'gdb-remote 1234'"
    echo "  (kslide is 0, so kernel symbols resolve at their link addresses; use"
    echo "   'breakpoint set -H -n <func>' for hardware breakpoints)"
    echo "xnu-boot: stop QEMU with 'kill' in lldb or Ctrl-C here."
    "$@" -S -s || true
else
    "$@" &
    qpid=$!
    i=0
    while [ "$i" -lt "$SECS" ] && kill -0 "$qpid" 2>/dev/null; do
        sleep 1; i=$((i + 1))
    done
    kill "$qpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    echo "xnu-boot: QEMU ran ${i}s"
fi

echo "---- serial (filtered, last 35) ----"
grep -a -E "RAVYNOS|Darwin|panic|Panic|loader:|handoff|kernel:|Exception|RIP|seed|ACPI|Kext|kext|PCI|IOPanic|BSD root|mount" \
    "$OUT/serial.log" 2>/dev/null | tail -35 || true
echo "---- serial (raw, last 15 lines) ----"
tail -15 "$OUT/serial.log" 2>/dev/null | LC_ALL=C tr -cd '\11\12\15\40-\176' || true
echo
echo "---- first CPU exceptions (vectors 0x00-0x1f) ----"
if [ -s "$OUT/qemu-int.log" ]; then
    grep -n -m5 -E ' v=[01][0-9a-f] ' "$OUT/qemu-int.log" || echo "(none)"
    first=$(grep -n -m1 -E ' v=[01][0-9a-f] ' "$OUT/qemu-int.log" | cut -d: -f1 || true)
    if [ -n "$first" ]; then
        echo "---- registers at the first exception ----"
        sed -n "$((first + 1)),$((first + 22))p" "$OUT/qemu-int.log"
    fi
    grep -a -m1 -E 'Triple fault' "$OUT/qemu-int.log" || true
else
    echo "(no qemu-int.log)"
fi
echo "xnu-boot: full logs in $OUT/serial.log and $OUT/qemu-int.log"
