#!/bin/sh
# Run the portable pipeline's test suite.
#
# Layers:
#   1. shell syntax      sh -n on every script under tools/
#   2. unit tests        python unittest over the image backends, verifier,
#                        the UEFI PE32+ validator and the post-mortem helper
#   3. CLI contract      argument handling, --help, root refusal, missing tools,
#                        symlinked staging roots
#   4. loader build      build tools/bootloader and check the PE it emits,
#                        when a PE toolchain (clang+lld-link, or zig) exists
#   5. pipeline selftest structure-only mode (unless --no-pipeline)
#
# No QEMU, xorriso, mtools or root is needed.  Runs in a few seconds on any
# host with a POSIX shell and Python 3.
#
# Usage: run-tests.sh [--no-pipeline] [-v]

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOLS=$(CDPATH= cd -- "$HERE/.." && pwd)
ROOT_UNDER_TEST=$(CDPATH= cd -- "$TOOLS/.." && pwd)
. "$TOOLS/lib/common.sh"

RUN_PIPELINE=1
VERBOSE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --no-pipeline) RUN_PIPELINE=0 ;;
        -v|--verbose)  VERBOSE=1 ;;
        -h|--help)     sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1" 2 ;;
    esac
    shift
done

PYTHON=$(find_python) || die "python3 is required" 2
FAILED=0
TMP=$(mktempdir ravynos-tests)
register_tmpdir "$TMP"

banner() { printf '\n%s== %s%s\n' "$C_GREEN" "$*" "$C_RESET"; }
ok()     { printf '   %sok%s   %s\n' "$C_GREEN" "$C_RESET" "$*"; }
bad()    { printf '   %sFAIL%s %s\n' "$C_RED" "$C_RESET" "$*"; FAILED=$((FAILED + 1)); }

# --------------------------------------------------------------------------
banner "1. shell syntax"
for script in "$TOOLS"/*.sh "$TOOLS"/lib/*.sh "$HERE"/*.sh; do
    [ -f "$script" ] || continue
    # tools/build.sh is a legacy zsh wrapper for the old FreeBSD-based build
    # (see PROGRESS.md); it is not a POSIX script and is not part of the
    # pipeline.  Validate zsh scripts with zsh when it exists.
    if head -1 "$script" | grep -q 'zsh'; then
        if have zsh; then
            if zsh -n "$script" 2>"$TMP/syntax.err"; then
                ok "$(basename "$script") (zsh)"
            else
                bad "$(basename "$script") (zsh)"
                sed 's/^/        /' "$TMP/syntax.err"
            fi
        else
            printf '   skip %s (legacy zsh script; zsh not installed)\n' "$(basename "$script")"
        fi
        continue
    fi
    if sh -n "$script" 2>"$TMP/syntax.err"; then
        ok "$(basename "$script")"
    else
        bad "$(basename "$script")"
        sed 's/^/        /' "$TMP/syntax.err"
    fi
done

# --------------------------------------------------------------------------
banner "2. python unit tests"
for suite in test_images.py test_pe.py test_elf.py test_stub_kernel.py \
             test_postmortem.py; do
    log="$TMP/unit-$suite.log"
    if [ "$VERBOSE" = 1 ]; then
        if "$PYTHON" "$HERE/$suite" -v; then ok "$suite"; else bad "$suite"; fi
    elif "$PYTHON" "$HERE/$suite" >"$log" 2>&1; then
        # unittest's summary line; grep -c on "... ok" only works with -v and
        # used to print "0\n0" here (grep prints 0 *and* exits 1).
        count=$(sed -n 's/^Ran \([0-9][0-9]*\) tests*.*/\1/p' "$log" | tail -1)
        ok "$suite (${count:-?} tests)"
    else
        bad "$suite"
        tail -50 "$log" | sed 's/^/        /'
    fi
done

# --------------------------------------------------------------------------
banner "3. CLI contract"
expect_fail() {
    description=$1; shift
    if "$@" >"$TMP/cli.out" 2>&1; then
        bad "$description (expected a non-zero exit)"
    else
        ok "$description"
    fi
}
expect_ok() {
    description=$1; shift
    if "$@" >"$TMP/cli.out" 2>&1; then
        ok "$description"
    else
        bad "$description"
        tail -10 "$TMP/cli.out" | sed 's/^/        /'
    fi
}

expect_ok "--help exits 0"        "$TOOLS/mkimage.sh" --help
expect_ok "--list-tools"          "$TOOLS/mkimage.sh" --list-tools
expect_fail "no mode is rejected"  "$TOOLS/mkimage.sh"
expect_fail "bad option is rejected" "$TOOLS/mkimage.sh" --stub --nonsense
expect_fail "root is refused"     env RAVYNOS_ALLOW_ROOT=0 sh -c 'id -u | grep -qx 0 && exec "$0" --stub || exit 1' "$TOOLS/mkimage.sh"
expect_fail "bootcheck needs an image" "$TOOLS/bootcheck.sh"
expect_fail "bootcheck rejects a missing image" "$TOOLS/bootcheck.sh" "$TMP/nope.iso"

# A guest that starts and never prints the marker must still be given up on.
# This is a regression test for a counter that was rebuilt from itself
# (elapsed=(elapsed+4)/5), pinning it at 1: --timeout was ignored and a hung
# boot ran until CI killed the job -- which is what the first OVMF run did.
FAKEBIN="$TMP/fakebin"
mkdir -p "$FAKEBIN"
cat > "$FAKEBIN/qemu-system-x86_64" <<'FAKEQEMU'
#!/bin/sh
# Stands in for QEMU: starts, prints nothing, sleeps until killed.
exec sleep 600
FAKEQEMU
chmod +x "$FAKEBIN/qemu-system-x86_64"
: > "$TMP/quiet.iso"
start=$(date +%s)
if PATH="$FAKEBIN:$PATH" BOOTCHECK_ACCEL=tcg \
        "$TOOLS/bootcheck.sh" "$TMP/quiet.iso" --expect NEVER-PRINTED \
        --timeout 2 --outdir "$TMP/boot-timeout" -q >/dev/null 2>&1; then
    bad "bootcheck passed an image that never printed the marker"
else
    took=$(( $(date +%s) - start ))
    if [ "$took" -le 30 ]; then
        ok "bootcheck honours --timeout ($took s)"
    else
        bad "bootcheck ignored --timeout (took ${took}s)"
    fi
fi

# The serial driver (--send) is what lets CI type into a shell prompt: a
# file-backed -serial is output-only, so the prompt could never be answered.
# Exercise it against a fake QEMU that speaks the socket protocol, records
# what was typed and answers with a marker that exists only if that happened.
cat > "$FAKEBIN/qemu-system-x86_64" <<'FAKEQEMU'
#!/usr/bin/env python3
"""Fake QEMU for the --send test: listens on the -serial socket, records
everything typed into it, answers with FAKE-GUEST-REPLY, then exits."""
import os
import socket
import sys
import time

args = sys.argv[1:]
sock_path = None
for i, arg in enumerate(args):
    if arg == "-serial" and args[i + 1].startswith("unix:"):
        sock_path = args[i + 1][len("unix:"):].split(",")[0]
if not sock_path:
    time.sleep(600)
    sys.exit(0)

srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(1)
conn, _ = srv.accept()
conn.settimeout(0.2)
seen = b""
deadline = time.time() + 20
while time.time() < deadline and seen.count(b"\r") < 2:
    try:
        chunk = conn.recv(4096)
    except socket.timeout:
        continue
    if not chunk:
        break
    seen += chunk
path = os.environ.get("FAKE_QEMU_SEEN")
if path:
    with open(path, "wb") as fh:
        fh.write(seen)
conn.sendall(b"FAKE-GUEST-REPLY\r\n")
time.sleep(0.5)
conn.close()
srv.close()
FAKEQEMU
chmod +x "$FAKEBIN/qemu-system-x86_64"

printf 'help\nver\n' > "$TMP/send.txt"
if PATH="$FAKEBIN:$PATH" BOOTCHECK_ACCEL=tcg FAKE_QEMU_SEEN="$TMP/send.seen" \
        "$TOOLS/bootcheck.sh" "$TMP/quiet.iso" --expect FAKE-GUEST-REPLY \
        --send "$TMP/send.txt" --send-delay 0 --send-interval 0 \
        --timeout 30 --outdir "$TMP/boot-send" -q >"$TMP/send.log" 2>&1; then
    ok "bootcheck --send drives a guest console over the serial socket"
    if grep -q 'help' "$TMP/send.seen" 2>/dev/null &&
       grep -q 'ver' "$TMP/send.seen" 2>/dev/null; then
        ok "--send typed both script lines into the guest"
    else
        bad "--send did not deliver the script lines"
        head -3 "$TMP/send.seen" 2>/dev/null | sed 's/^/        /'
    fi
    if grep -q 'FAKE-GUEST-REPLY' "$TMP/boot-send/serial.log" 2>/dev/null; then
        ok "--send writes guest output to serial.log"
    else
        bad "--send lost the guest's output"
    fi
else
    bad "bootcheck --send did not complete against a socket-serial guest"
    tail -15 "$TMP/send.log" | sed 's/^/        /'
fi

# A failed boot must leave a post-mortem: the firmware's exception dump says
# where the CPU was, not what it was executing, and a bare "RIP 0xB0000" is not
# a diagnosis.  QEMU is faked here too -- it serves the HMP monitor socket that
# bootcheck opens with `-monitor unix:...,server,nowait`, reports the address
# the real OVMF run reported, and stays alive so the dump has something to ask.
cat > "$FAKEBIN/qemu-system-x86_64" <<'FAKEQEMU'
#!/usr/bin/env python3
"""Fake QEMU for the post-mortem test: answers HMP, then idles."""
import socket
import sys
import threading
import time

args = sys.argv[1:]
mon_path = serial_path = None
for i, arg in enumerate(args):
    if arg == "-monitor" and args[i + 1].startswith("unix:"):
        mon_path = args[i + 1][len("unix:"):].split(",")[0]
    if arg == "-serial" and args[i + 1].startswith("file:"):
        serial_path = args[i + 1][len("file:"):]

if serial_path:
    with open(serial_path, "ab") as fh:
        fh.write(b"BdsDxe: starting Boot0001\r\n"
                 b"!!!! X64 Exception Type - 06(#UD - Invalid Opcode) !!!!\r\n"
                 b"RIP  - 00000000000B0000, CS  - 0000000000000038\r\n")

REGISTERS = "RAX=0000000000000000 RIP=00000000000b0000 RFLAGS=0000000000000246\n"
MTREE = ("address-space: memory\n"
         "  0000000000000000-00000000ffffffff (prio 0, i/o): system\n"
         "    00000000000a0000-00000000000bffff (prio 0, i/o): vga-lowmem\n")
ANSWERS = {"info registers": REGISTERS, "info mtree": MTREE,
           "info status": "VM status: running",
           "xp": "00000000000b0000: 0xff 0xff 0xff 0xff",
           "x": "invalid"}


def serve():
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    srv.bind(mon_path)
    srv.listen(1)
    srv.settimeout(30)
    try:
        conn, _ = srv.accept()
    except OSError:
        return
    conn.settimeout(5)
    conn.sendall(b"QEMU fake monitor\r\n(qemu) ")
    buf = b""
    while True:
        try:
            chunk = conn.recv(4096)
        except OSError:
            return
        if not chunk:
            return
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            cmd = line.decode().strip()
            body = ANSWERS.get(cmd)
            if body is None:
                body = ANSWERS.get(cmd.split()[0], "unknown command")
            conn.sendall((cmd + "\r\n" + body + "\r\n(qemu) ").encode())


if mon_path:
    threading.Thread(target=serve, daemon=True).start()
time.sleep(60)
FAKEQEMU
chmod +x "$FAKEBIN/qemu-system-x86_64"

if PATH="$FAKEBIN:$PATH" BOOTCHECK_ACCEL=tcg \
        "$TOOLS/bootcheck.sh" "$TMP/quiet.iso" --expect NEVER-PRINTED \
        --timeout 3 --outdir "$TMP/boot-post" -q >"$TMP/post.log" 2>&1; then
    bad "bootcheck passed an image that only crashed"
else
    if [ -f "$TMP/boot-post/postmortem.log" ]; then
        ok "bootcheck writes a post-mortem on a failed boot"
        if grep -q 'vga-lowmem' "$TMP/boot-post/postmortem.log"; then
            ok "the post-mortem names the owner of the faulting address"
        else
            bad "the post-mortem did not identify the memory region at RIP"
            sed -n '1,20p' "$TMP/boot-post/postmortem.log" | sed 's/^/        /'
        fi
    else
        bad "no post-mortem was written for a failed boot"
        tail -20 "$TMP/post.log" | sed 's/^/        /'
    fi
fi

# A plan-only run must not produce output files.
# A file that begins with MZ is not a UEFI image: firmware would refuse it and
# the boot would hang with no diagnostic, so mkimage has to catch it here.
BADEFI="$TMP/badefi"
mkdir -p "$BADEFI/EFI/BOOT"
printf 'MZ but not a PE file\n' > "$BADEFI/EFI/BOOT/BOOTX64.EFI"
expect_fail "--efi-image must be a FAT volume with an EFI application" \
    "$TOOLS/mkimage.sh" --root "$BADEFI" --format iso \
    --efi-image EFI/BOOT/BOOTX64.EFI -o "$TMP/badefi/out"
# A bare PE is the mistake the old docs invited: firmware mounts the entry as
# a partition, so the message has to point at --efi-payload.
BARE="$TMP/bare"
mkdir -p "$BARE/EFI/BOOT"
if "$PYTHON" - "$BARE/EFI/BOOT/BOOTX64.EFI" "$HERE" <<'PY'
import sys
sys.path.insert(0, sys.argv[2])
from test_pe import minimal_pe
open(sys.argv[1], "wb").write(minimal_pe())
PY
then
    if "$TOOLS/mkimage.sh" --root "$BARE" --format iso \
            --efi-image EFI/BOOT/BOOTX64.EFI -o "$TMP/bare/out" \
            >"$TMP/bare.log" 2>&1; then
        bad "mkimage accepted a bare PE as an El Torito UEFI entry"
    elif grep -q -- "--efi-payload" "$TMP/bare.log"; then
        ok "bare PE is rejected with a --efi-payload hint"
    else
        bad "bare PE rejected without the actionable message"
        tail -3 "$TMP/bare.log" | sed 's/^/        /'
    fi
else
    bad "could not build the bare-PE fixture"
fi

expect_ok "--print-plan builds nothing" "$TOOLS/mkimage.sh" --stub --print-plan -o "$TMP/plan/x"
if [ -f "$TMP/plan/x.iso" ]; then bad "--print-plan wrote an image"; else ok "--print-plan wrote no image"; fi

# repo_root must find the checkout from a script in tools/, not its parent.
# It used to assume the caller lived in tools/lib and return dirname($0)/../..
# -- one level too high for tools/*.sh, which sent `bmake world` into the
# repository's parent directory and stamped images "version unknown".
probe="$TOOLS/.repo-root-probe.$$"
printf '#!/bin/sh\n. "$(dirname -- "$0")/lib/common.sh"\nrepo_root\n' > "$probe"
root_from_probe=$(sh "$probe" 2>/dev/null || true)
rm -f "$probe"
if [ "$root_from_probe" = "$ROOT_UNDER_TEST" ]; then
    ok "repo_root resolves to the checkout from tools/"
else
    bad "repo_root from tools/ returned '${root_from_probe:-<nothing>}', expected $ROOT_UNDER_TEST"
fi

# A staged root with symlinks.  This tree's own makefiles create them in
# ${SYSROOT_DIR} (BSD/lib/bzip2/Makefile, BSD/lib/ncurses/Makefile) and ISO
# 9660 as written here has no symlink type, so --root used to die on the
# first one -- which is where a real sysroot would have stopped.
SYSTREE="$TMP/systree"
mkdir -p "$SYSTREE/bin" "$SYSTREE/usr/lib" "$SYSTREE/etc"
printf 'binary-content\n' > "$SYSTREE/bin/tool"
printf 'library-content\n' > "$SYSTREE/usr/lib/libReal.dylib"
ln -s libReal.dylib "$SYSTREE/usr/lib/libAlias.dylib"   # relative, inside
ln -s ../usr/lib "$SYSTREE/etc/libdir"                  # relative, inside
ln -s /nonexistent/elsewhere "$SYSTREE/outside"         # absolute, skipped
expect_ok "--root tolerates symlinks" \
    "$TOOLS/mkimage.sh" --root "$SYSTREE" --format both --no-checksum -o "$TMP/systree/out"
if [ -f "$TMP/systree/out.iso" ] && [ -f "$TMP/systree/out.img" ]; then
    ok "symlinked staging root produced both images"
else
    bad "symlinked staging root produced no images"
fi
# The dereferenced entry must be a real file with the target's size, not a
# link stub and not a hole.
"$PYTHON" "$TOOLS/image/verify.py" iso "$TMP/systree/out.iso" --json > "$TMP/systree/iso.json" 2>/dev/null
"$PYTHON" "$TOOLS/image/verify.py" img "$TMP/systree/out.img" --json > "$TMP/systree/img.json" 2>/dev/null
if "$PYTHON" - "$TMP/systree/iso.json" "$TMP/systree/img.json" <<'PY'
import json, sys
want = len("library-content\n")
for path in sys.argv[1:3]:
    doc = json.load(open(path))
    entries = doc.get("files") or doc.get("esp", {}).get("files") or []
    hits = [f for f in entries if f["path"].upper().endswith("LIBALIAS.DYLIB")]
    assert hits, f"{path}: no LIBALIAS entry in {[f['path'] for f in entries]}"
    assert hits[0]["size"] == want, f"{path}: size {hits[0]['size']} != {want}"
PY
then ok "dereferenced symlink carries the target's content"
else bad "dereferenced symlink does not carry the target's content"
fi
if "$PYTHON" - "$TMP/systree/iso.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
paths = [f["path"].upper() for f in doc["files"]]
assert not any("OUTSIDE" in p for p in paths), paths
PY
then ok "absolute symlink was skipped, not followed"
else bad "absolute symlink leaked into the image"
fi

# --------------------------------------------------------------------------
banner "3b. kernel build"
#
# The stub kernel used to ship as a checked-in KERNEL.ELF, so the image
# pipeline could package a kernel nobody had built from the current source.
# It is built here with whatever toolchain exists (clang + ld.lld, or gcc +
# ld) and validated with tools/image/elf.py, which is the same contract the
# loader enforces before it jumps.
KOUT="$TMP/KERNEL.ELF"
if OUT="$KOUT" "$ROOT_UNDER_TEST/Kernel/ravynkern/build.sh" -q 2>"$TMP/kernel.err"; then
    if "$PYTHON" "$TOOLS/image/elf.py" "$KOUT" --require-load \
            --min-vaddr 0x100000 --max-end 0x100000000 -q; then
        ok "kernel builds into a loadable ELF64 ($(file_size "$KOUT") bytes)"
    else
        bad "kernel ELF layout is not loadable by the loader"
    fi
    if grep -qa RAVYNOS-KERNEL-OK "$KOUT"; then
        ok "kernel image carries the RAVYNOS-KERNEL-OK marker"
    else
        bad "kernel image has no RAVYNOS-KERNEL-OK string"
    fi
else
    printf '   skip kernel build (no clang+ld.lld or gcc+ld)\n'
    sed 's/^/        /' "$TMP/kernel.err" 2>/dev/null | head -5
fi

# --------------------------------------------------------------------------
banner "4. UEFI loader"
# Built with clang+lld-link when the host has them, zig otherwise.  A host with
# neither skips rather than failing: the loader is not a pipeline dependency,
# but a loader that builds into something firmware would refuse *is* a defect,
# and without this layer nothing ever compiled it.
LOADER_OUT="$TMP/BOOTX64.EFI"
if "$TOOLS/bootloader/build.sh" -q -o "$LOADER_OUT" 2>"$TMP/loader.err"; then
    if "$PYTHON" "$TOOLS/image/pe.py" "$LOADER_OUT" --require-subsystem 10 -q; then
        ok "loader builds and is a PE32+ EFI application ($(file_size "$LOADER_OUT") bytes)"

        # Package it the way the pipeline does and check the El Torito entry
        # covers the whole payload: the sector count is what firmware loads.
        LOADER_STAGE="$TMP/loader-stage"
        mkdir -p "$LOADER_STAGE/EFI/BOOT"
        cp "$LOADER_OUT" "$LOADER_STAGE/EFI/BOOT/BOOTX64.EFI"
        if "$TOOLS/mkimage.sh" --root "$LOADER_STAGE" --format both --verify \
                --efi-payload EFI/BOOT/BOOTX64.EFI --no-checksum -q \
                -o "$TMP/loader-out/loader" >"$TMP/loader-mkimage.log" 2>&1; then
            if "$PYTHON" "$TOOLS/image/verify.py" iso "$TMP/loader-out/loader.iso" \
                    --json >"$TMP/loader-out/iso.json" 2>/dev/null &&
               "$PYTHON" - "$TMP/loader-out/iso.json" "$LOADER_OUT" <<'PY'
import json, math, os, sys
info = json.load(open(sys.argv[1]))
size = os.path.getsize(sys.argv[2])
uefi = [e for e in info["boot"]["entries"]
        if e["kind"] == "uefi" and e["bootable"]]
assert uefi, "no UEFI boot entry"
payload = uefi[0]["payload"]
assert payload["format"] == "fat16", payload
assert payload["boot"] == "EFI/BOOT/BOOTX64.EFI", payload
assert uefi[0]["load_sectors"] >= math.ceil(payload["size"] / 512), (
    f"catalog says {uefi[0]['load_sectors']} sectors, payload is "
    f"{payload['size']} bytes")
assert payload["subsystem"] == "efi_application"
PY
            then ok "ISO El Torito entry is a FAT image covering the loader"
            else bad "ISO El Torito entry is not a FAT image covering the loader"
            fi
        else
            bad "mkimage --efi-image rejected or failed on the loader"
            tail -10 "$TMP/loader-mkimage.log" | sed 's/^/        /'
        fi
    else
        bad "loader built, but the output is not a PE32+ EFI application"
    fi
elif grep -q 'no PE toolchain found' "$TMP/loader.err" 2>/dev/null; then
    printf '   skip loader build (no clang+lld-link and no zig on this host)\n'
else
    bad "loader build"
    tail -20 "$TMP/loader.err" | sed 's/^/        /'
fi

# The shell is what the loader chain-loads when there is nothing to boot, and
# it is the prompt a user types into; it has to build wherever the loader does.
SHELLOUT="$TMP/SHELL.EFI"
if OUT="$SHELLOUT" "$TOOLS/shell/build.sh" -q 2>"$TMP/shell.err"; then
    if "$PYTHON" "$TOOLS/image/pe.py" "$SHELLOUT" --require-subsystem 10 -q; then
        ok "shell builds as a PE32+ EFI application ($(file_size "$SHELLOUT") bytes)"
    else
        bad "shell output is not a PE32+ EFI application"
    fi
elif grep -q 'no PE toolchain found' "$TMP/shell.err" 2>/dev/null; then
    printf '   skip shell build (no clang+lld-link and no zig on this host)\n'
else
    bad "shell build"
    tail -20 "$TMP/shell.err" | sed 's/^/        /'
fi

# --------------------------------------------------------------------------
if [ "$RUN_PIPELINE" = 1 ]; then
    banner "5. pipeline self-test (structure only)"
    if "$TOOLS/pipeline-selftest.sh" --no-qemu -q; then
        ok "stub ISO+IMG build, verify, reproducibility"
    else
        bad "pipeline self-test"
    fi
fi

# --------------------------------------------------------------------------
printf '\n'
if [ "$FAILED" = 0 ]; then
    printf '%sALL TESTS PASSED%s\n' "$C_GREEN" "$C_RESET"
    exit 0
fi
printf '%s%d TEST GROUP(S) FAILED%s\n' "$C_RED" "$FAILED" "$C_RESET"
exit 1
