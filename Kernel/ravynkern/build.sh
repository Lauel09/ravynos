#!/bin/sh
# Build the ravynOS stub kernel (K1, Docs/KernelHandoff.md).
#
#   ./build.sh [-o PATH] [--clean] [-q]
#
# Freestanding x86-64 ELF linked at 1 MiB.  No libc, no stdlib, no dynamic
# relocations: the UEFI loader copies every PT_LOAD segment verbatim and jumps
# to e_entry with the firmware page tables still active.
#
# Two toolchains are supported, the same way tools/bootloader/build.sh does it:
#
#   * clang + ld.lld -- what the ravynOS toolchain and Xcode provide;
#   * gcc + GNU ld  -- a bare Linux container has these and nothing else.
#
# The layouts differ slightly (ld.lld takes --image-base, GNU ld -Ttext), and
# both are acceptable: what the loader needs is a *single* PT_LOAD whose
# virtual address is >= 1 MiB with .bss inside MemSiz.  build.sh checks that
# with tools/image/elf.py before declaring success, so a silently wrong layout
# fails here instead of as a boot that jumps into free memory.
#
# Environment:
#   CC, LD    override the compilers (both must be set to skip detection)
#   OUT       output path (default ./KERNEL.ELF)
#   QUIET=1   print only errors
#
# Exit status: 0 built, 1 build failed, 2 no usable toolchain.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-"$HERE/KERNEL.ELF"}
OBJ="$HERE/main.o"
SRC="$HERE/main.c"
QUIET=${QUIET:-0}

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

CFLAGS="-ffreestanding -fno-stack-protector -fno-builtin -nostdlib
 -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -mcmodel=kernel
 -O2 -Wall -Wextra -Wno-unused-parameter"

if [ -n "${CC:-}" ] && [ -n "${LD:-}" ]; then
    toolchain=explicit
elif have clang && have ld.lld; then
    CC=clang
    LD=ld.lld
    toolchain=clang-lld
elif have gcc && have ld; then
    CC=gcc
    LD=ld
    toolchain=gcc-ld
else
    printf 'build: no usable C toolchain (need clang+ld.lld or gcc+ld)\n' >&2
    exit 2
fi

say "build: $toolchain ($CC + $LD)"
# shellcheck disable=SC2086  # deliberate word splitting of the flag list
"$CC" $CFLAGS -c -o "$OBJ" "$SRC" ||
    { printf 'build: compilation failed\n' >&2; exit 1; }

case $toolchain in
    clang-lld)
        "$LD" -m elf_x86_64 -N --image-base=0x100000 -e _start \
            --build-id=none -z noexecstack -o "$OUT" "$OBJ" ||
            { printf 'build: link failed\n' >&2; exit 1; }
        ;;
    *)
        # GNU ld has no --image-base for ELF; -Ttext sets where .text lands and
        # -N (omagic) keeps the headers inside the one loadable segment.
        "$LD" -m elf_x86_64 -N -e _start -Ttext=0x100000 \
            --build-id=none -z noexecstack -o "$OUT" "$OBJ" 2>/dev/null ||
        "$LD" -m elf_x86_64 -N -e _start -Ttext=0x100000 -o "$OUT" "$OBJ" ||
            { printf 'build: link failed\n' >&2; exit 1; }
        ;;
esac

[ -f "$OUT" ] || { printf 'build: linker produced no %s\n' "$OUT" >&2; exit 1; }

# The layout is a contract with the loader; check it here rather than
# discovering it as a jump into unmapped memory.
if have python3 && [ -f "$HERE/../../tools/image/elf.py" ]; then
    python3 "$HERE/../../tools/image/elf.py" "$OUT" \
        --require-load --min-vaddr 0x100000 --max-end 0x200000 -q ||
        { printf 'build: %s is not a loadable kernel image\n' "$OUT" >&2; exit 1; }
fi

size=$(wc -c < "$OUT" | tr -d ' ')
say "build: wrote $OUT ($size bytes)"
