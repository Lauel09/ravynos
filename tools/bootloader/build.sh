#!/bin/sh
# Build the UEFI loader skeleton (tools/bootloader/uefi_stub.c).
#
#   ./build.sh [-o PATH] [--clean] [-q]
#
# A UEFI application is a PE32+ executable with an EFI subsystem and no CRT.
# The canonical toolchain is clang + lld-link; on hosts that have neither (a
# bare Linux container, this sandbox) a `zig` binary provides the same clang
# front end and a COFF linker, so the loader can still be built and its PE
# headers checked.  Both paths are exercised: CI uses clang/lld-link, and
# tools/tests/run-tests.sh builds with whatever it finds.
#
# Environment:
#   CC       C compiler to use (with LD, skips detection)
#   LD       COFF linker (default lld-link when CC is clang)
#   ZIG      zig binary to use instead of detecting one
#   OUT      output path (default ./BOOTX64.EFI)
#   QUIET=1  print only errors
#
# Exit status: 0 built, 1 build failed, 2 no usable toolchain.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-"$HERE/BOOTX64.EFI"}
QUIET=${QUIET:-0}
SRC="$HERE/uefi_stub.c"
OBJ="$HERE/uefi_stub.o"

clean=0
while [ $# -gt 0 ]; do
    case $1 in
        -o|--output) OUT=$2; shift ;;
        --clean)     clean=1 ;;
        -q|--quiet)  QUIET=1 ;;
        -h|--help)   sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           printf 'build: unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

say() {
    [ "$QUIET" = 1 ] && return 0
    printf '%s\n' "$*"
}

have() { command -v "$1" >/dev/null 2>&1; }

if [ "$clean" = 1 ]; then
    rm -f "$OUT" "$OBJ"
    say "build: cleaned $OUT"
    exit 0
fi

[ -f "$SRC" ] || { printf 'build: missing %s\n' "$SRC" >&2; exit 1; }

find_zig() {
    if [ -n "${ZIG:-}" ] && [ -x "${ZIG}" ]; then
        printf '%s\n' "$ZIG"
        return 0
    fi
    if have zig; then
        command -v zig
        return 0
    fi
    # `pip install ziglang` ships a complete zig; it is how the pipeline gets
    # a PE linker onto a host with no system LLVM.
    if have python3; then
        dir=$(python3 -c 'import os, ziglang; print(os.path.dirname(ziglang.__file__))' 2>/dev/null) || true
        if [ -n "${dir:-}" ] && [ -x "$dir/zig" ]; then
            printf '%s\n' "$dir/zig"
            return 0
        fi
    fi
    return 1
}

# -mno-red-zone is mandatory: firmware may use the red zone with interrupts
# enabled.  -fshort-wchar makes CHAR16 a real 16-bit wchar_t.
COMMON_FLAGS="-ffreestanding -fno-stack-protector -fno-builtin -mno-red-zone
 -fshort-wchar -O2 -Wall -Wextra -Wno-unused-parameter"

if [ -n "${CC:-}" ] && [ -n "${LD:-}" ]; then
    toolchain=explicit
elif have clang && have lld-link; then
    CC=clang
    LD=lld-link
    toolchain=clang-lld-link
else
    CC=
    LD=
    toolchain=
fi

if [ -n "${toolchain:-}" ]; then
    say "build: $toolchain ($CC + $LD)"
    # shellcheck disable=SC2086  # deliberate word splitting of the flag list
    "$CC" -target x86_64-unknown-windows $COMMON_FLAGS -c -o "$OBJ" "$SRC" ||
        { printf 'build: compilation failed\n' >&2; exit 1; }
    # The subsystem is spelled by name: newer lld-link releases reject the
    # numeric 10 ("unknown subsystem: 10"), which is how the first CI run of
    # this script failed.
    "$LD" -machine:x64 -subsystem:efi_application -entry:EfiMain \
        -nodefaultlib -nxcompat:no -out:"$OUT" "$OBJ" ||
        { printf 'build: link failed\n' >&2; exit 1; }
else
    ZIG_BIN=$(find_zig) || ZIG_BIN=
    if [ -z "$ZIG_BIN" ]; then
        printf 'build: no PE toolchain found.\n' >&2
        printf '  install clang + lld (Xcode, LLVM, or apt install clang lld),\n' >&2
        printf '  or a zig binary (pip install ziglang) as a fallback.\n' >&2
        exit 2
    fi
    say "build: zig ($ZIG_BIN)"
    # shellcheck disable=SC2086
    "$ZIG_BIN" cc -target x86_64-windows $COMMON_FLAGS -c -o "$OBJ" "$SRC" ||
        { printf 'build: compilation failed\n' >&2; exit 1; }
    # zig's COFF linker wants the subsystem spelled out and takes the default
    # entry symbol EfiMain; /entry: is not accepted.
    "$ZIG_BIN" cc -target x86_64-windows -nostdlib \
        -Wl,/subsystem:efi_application -o "$OUT" "$OBJ" ||
        { printf 'build: link failed\n' >&2; exit 1; }
    # zig's COFF linker always writes a .pdb next to the image; it is a build
    # artefact of no use to firmware and must not land in the repository.
    rm -f "${OUT%.*}.pdb"
fi

[ -f "$OUT" ] || { printf 'build: linker produced no %s\n' "$OUT" >&2; exit 1; }

# The build is only useful if the result is what firmware will accept; say so
# here rather than discovering it as a boot timeout.
if have python3 && [ -f "$HERE/../image/pe.py" ]; then
    python3 "$HERE/../image/pe.py" "$OUT" --require-subsystem 10 -q ||
        { printf 'build: %s is not a PE32+ EFI application\n' "$OUT" >&2; exit 1; }
fi

size=$(wc -c < "$OUT" | tr -d ' ')
say "build: wrote $OUT ($size bytes)"
