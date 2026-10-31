#!/bin/sh
# Darwin gate: build + boot checks for the UEFI loader (run from the repo
# root on macOS).  Milestone order and rules: Docs/DarwinBoot.md §0.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

if [ "${1:-}" = "xnu" ]; then
    # D1: kernel-only build; smallest target that rebuilds xnu is
    # `xnu_all` in Kernel/Makefile:81 (world lists Developer Kernel, …).
    : > "$ROOT/build-xnu.log"
    if command -v bmake >/dev/null 2>&1; then
        bmake -C Kernel xnu_all >"$ROOT/build-xnu.log" 2>&1 || true
    else
        make -C Kernel xnu_all >"$ROOT/build-xnu.log" 2>&1 || true
    fi
    # Show only the first real errors; do not dump the whole log.
    grep -n -m8 -E "error:|\*\*\* |No rule|unable to find sdk" \
        "$ROOT/build-xnu.log" || true
    exit 0
fi

# a. build loader + stub kernel
"$ROOT/tools/bootloader/build.sh" -q
"$ROOT/Kernel/ravynkern/build.sh" -q

# b. build the stub Mach-O kernel
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STUB="$TMP/stub_macho"
clang -arch x86_64 -static -nostdlib -Wl,-e,_start \
    -Wl,-image_base,0xffffff8000200000 -Wl,-pagezero_size,0 \
    -o "$STUB" "$ROOT/tools/bootloader/test/stub_macho.S"
otool -l "$STUB" | grep -A3 -E "LC_SEGMENT_64|LC_UNIXTHREAD"

# c. boot twice with the §3 QEMU line; 25 s each, then kill QEMU
run_boot() {
    dir="$1"; kernel_src="$2"; kernel_dst="$3"
    mkdir -p "$dir/EFI/BOOT"
    cp "$ROOT/tools/bootloader/BOOTX64.EFI" "$dir/EFI/BOOT/BOOTX64.EFI"
    mkdir -p "$(dirname "$dir/$kernel_dst")"
    cp "$kernel_src" "$dir/$kernel_dst"
    cp /opt/homebrew/share/qemu/edk2-i386-vars.fd "$dir/vars.fd"
    qemu-system-x86_64 \
        -machine q35 -m 2048 \
        -display none -monitor none \
        -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
        -drive if=pflash,format=raw,file="$dir/vars.fd" \
        -drive format=raw,file=fat:rw:"$dir" \
        -serial file:"$dir/serial.log" &
    qpid=$!
    sleep 25
    kill "$qpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    # d. print only the filtered serial tail
    grep -a -E "RAVYNOS|loader:|handoff|Exception|RIP" "$dir/serial.log" | tail -15 || true
}

run_boot "$TMP/boot_stub" "$STUB" "System/Library/Kernels/kernel"
run_boot "$TMP/boot_elf" "$ROOT/Kernel/ravynkern/KERNEL.ELF" "EFI/KERNEL.ELF"
