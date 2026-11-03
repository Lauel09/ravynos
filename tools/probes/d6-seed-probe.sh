#!/bin/sh
# D6 probe: inspect /chosen/random-seed at PE_get_random_seed entry (before
# pe_gen.c:166 zeroes it) and record whether execution reaches i386_init or
# another panic_trap_to_debugger.  Also symbolicates any prior xnu-boot fault.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KERNEL="$ROOT/../build/sysroot/System/Library/Kernels/kernel"
OUT="$ROOT/out/arena/probe"
FW=${QEMU_FW_DIR:-/opt/homebrew/share/qemu}
PORT=12345

echo "== unit test: test_device_tree.py =="
python3 "$ROOT/tools/tests/test_device_tree.py" 2>&1

if [ -s "$ROOT/out/arena/boot/qemu-int.log" ]; then
    echo "== prior boot first-exception symbolication =="
    first=$(grep -n -m1 -E ' v=[01][0-9a-f] ' "$ROOT/out/arena/boot/qemu-int.log" | cut -d: -f1 || true)
    if [ -n "$first" ]; then
        block=$(sed -n "${first},$((first + 6))p" "$ROOT/out/arena/boot/qemu-int.log")
        ip=$(printf '%s\n' "$block" | sed -n 's/.*pc=\([0-9a-fA-F]*\).*/0x\1/p' | head -n1)
        rdi=$(printf '%s\n' "$block" | sed -n 's/.*RDI=\([0-9a-fA-F]*\).*/0x\1/p' | head -n1)
        echo "ip=$ip rdi=$rdi"
        if [ -n "$ip" ]; then
            lldb -b "$KERNEL" -o "image lookup -a $ip" 2>&1 | grep -E 'Summary:|LineEntry:' || true
        fi
        if [ -n "$rdi" ]; then
            python3 - "$KERNEL" "$rdi" <<'PY' || true
import struct, sys
data = open(sys.argv[1], "rb").read()
va = int(sys.argv[2], 16)
_, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IIIIIIII", data, 0)
off = 32
for _ in range(ncmds):
    cmd, cmdsize = struct.unpack_from("<II", data, off)
    if cmd == 0x19:
        _, _, _, vmaddr, vmsize, fileoff, filesize, _, _, _, _ = struct.unpack_from(
            "<II16sQQQQIIII", data, off
        )
        if vmaddr <= va < vmaddr + filesize:
            foff = fileoff + (va - vmaddr)
            end = data.find(b"\0", foff, min(len(data), foff + 200))
            if end != -1:
                print("RDI cstring:", repr(data[foff:end].decode("latin1")))
            break
    off += cmdsize
PY
        fi
    fi
fi

rm -rf "$OUT"
mkdir -p "$OUT/fat/EFI/BOOT" "$OUT/fat/System/Library/Kernels"
cp "$ROOT/tools/bootloader/BOOTX64.EFI" "$OUT/fat/EFI/BOOT/BOOTX64.EFI"
cp "$KERNEL" "$OUT/fat/System/Library/Kernels/kernel"
cp "$FW/edk2-i386-vars.fd" "$OUT/vars.fd"

qemu-system-x86_64 -machine q35 -cpu Penryn -m 2048 \
    -display none -monitor none -no-reboot -no-shutdown \
    -drive if=pflash,format=raw,readonly=on,file="$FW/edk2-x86_64-code.fd" \
    -drive if=pflash,format=raw,file="$OUT/vars.fd" \
    -drive format=raw,file=fat:rw:"$OUT/fat" \
    -serial file:"$OUT/serial.log" \
    -S -gdb tcp::"$PORT" &
qpid=$!

( sleep 35 >/dev/null 2>&1 && kill "$qpid" 2>/dev/null || true ) >/dev/null 2>&1 &
wpid=$!
cleanup() {
    kill "$wpid" "$qpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1

echo "== lldb live probe =="
lldb -b "$KERNEL" \
    -o "gdb-remote $PORT" \
    -o "breakpoint set -H -n PE_get_random_seed" \
    -o "breakpoint set -H -n panic_trap_to_debugger" \
    -o "breakpoint set -H -n i386_init" \
    -o "continue" \
    -o "register read rip rdi rsi cr3" \
    -o "memory read -f x -s 4 -c 26 0xffffff80001a0064" \
    -o "continue" \
    -o "register read rip rdi rsi rdx cr3" \
    -o "bt 5" \
    -o "memory read -f x -s 4 -c 16 0xffffff80001a0088" \
    -o "x/s \$rdi" 2>&1 |
    grep -v '^$' | tail -n 75 || true
