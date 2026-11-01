#!/bin/sh
# Darwin gate: build + boot checks for the UEFI loader (run from the repo
# root on macOS).  Milestone order and rules: Docs/DarwinBoot.md §0.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

if [ "${1:-}" = "xnu" ]; then
    # D1: refresh the repo-native SDK first so its Makefile changes reach an
    # existing build tree, then run only the top-level Kernel stage. Running
    # `-C Kernel` directly would miss the top-level exports (GMAKE, XCRUN,
    # TOOLCHAIN, ...). Keep the two logs separate: an SDK failure must not be
    # mistaken for an XNU failure, and neither may be hidden by `|| true`.
    if command -v bmake >/dev/null 2>&1; then
        BMAKE=bmake
    elif command -v make >/dev/null 2>&1; then
        BMAKE=make
    else
        echo "D1: neither bmake nor make is installed" >&2
        exit 2
    fi

    show_build_errors() {
        log=$1
        grep -n -m8 -E "error:|\*\*\* |No rule|unable to find sdk|No such file" \
            "$log" || tail -20 "$log"
    }

    echo "D1: refreshing Developer/ravynOS.sdk (build-sdk.log)"
    if env SUBDIR=Developer/ravynOS.sdk "$BMAKE" all \
            >"$ROOT/build-sdk.log" 2>&1; then
        :
    else
        rc=$?
        show_build_errors "$ROOT/build-sdk.log"
        echo "D1: SDK refresh failed; full log: $ROOT/build-sdk.log" >&2
        exit "$rc"
    fi

    # Do not let a previous successful build satisfy the artifact check if
    # this invocation returns success without installing a fresh kernel.
    KERNEL="$ROOT/../build/sysroot/System/Library/Kernels/kernel"
    rm -f "$KERNEL"

    # XNU's setup objects do not depend on HOST_SDKROOT or compiler flags.
    # Recompile only the known objects affected by the host-SDK correction;
    # do not disturb unrelated build products.
    SETUP_OBJROOT="$ROOT/../build$ROOT/Kernel/xnu/SETUP"
    rm -f \
        "$SETUP_OBJROOT/config/externs.o" \
        "$SETUP_OBJROOT/config/main.o" \
        "$SETUP_OBJROOT/config/mkheaders.o" \
        "$SETUP_OBJROOT/config/mkioconf.o" \
        "$SETUP_OBJROOT/config/mkmakefile.o" \
        "$SETUP_OBJROOT/config/openp.o" \
        "$SETUP_OBJROOT/config/searchp.o" \
        "$SETUP_OBJROOT/config/lexer.yy.o" \
        "$SETUP_OBJROOT/config/parser.o" \
        "$SETUP_OBJROOT/decomment/decomment.o" \
        "$SETUP_OBJROOT/installfile/installfile.o" \
        "$SETUP_OBJROOT/replacecontents/replacecontents.o" \
        "$SETUP_OBJROOT/setsegname/setsegname.o" \
        "$SETUP_OBJROOT/kextsymboltool/kextsymboltool.o"

    echo "D1: building Kernel (build-xnu.log)"
    if "$BMAKE" Kernel >"$ROOT/build-xnu.log" 2>&1; then
        :
    else
        rc=$?
        show_build_errors "$ROOT/build-xnu.log"
        echo "D1: XNU build failed; full log: $ROOT/build-xnu.log" >&2
        exit "$rc"
    fi

    # Kernel/Makefile:xnu_all copies the RELEASE kernel here after XNU's own
    # `all install` completes. Check that exact staged product, not an
    # object-directory kernel left by an earlier attempt.
    if [ ! -f "$KERNEL" ]; then
        echo "D1: build returned success but staged kernel is missing: $KERNEL" >&2
        exit 1
    fi
    description=$(file "$KERNEL")
    echo "$description"
    case $description in
        *"Mach-O 64-bit executable x86_64"*)
            echo "D1 GATE PASSED: $KERNEL"
            ;;
        *)
            echo "D1: staged kernel is not an x86_64 Mach-O executable" >&2
            exit 1
            ;;
    esac
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
