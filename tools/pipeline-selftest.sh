#!/bin/sh
# End-to-end check of the portable image pipeline's own plumbing.
#
# Builds loader-only stub images, verifies their structure, proves the output
# is reproducible, and -- unless --no-qemu is given -- boots them under QEMU
# and checks the serial marker.  Everything runs without root, mounts or
# chroot; the only host tools needed are a POSIX shell, python3, and for the
# boot step QEMU.
#
# Usage:
#   pipeline-selftest.sh [--no-qemu] [--keep] [-q]
#
# Options:
#   --no-qemu   structure-only run: fast (a few seconds), no QEMU required.
#               This is the CI gate that catches image-assembly regressions.
#   --keep      keep the temporary work directory and print its path.
#   -q          quiet.
#
# Exit status: 0 = pass, 1 = failure, 2 = missing prerequisite.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/lib/common.sh"

USE_QEMU=1
KEEP=0
QUIET=${QUIET:-0}

while [ $# -gt 0 ]; do
    case "$1" in
        --no-qemu) USE_QEMU=0 ;;
        --keep)    KEEP=1 ;;
        -q|--quiet) QUIET=1 ;;
        -h|--help) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (try --help)" 2 ;;
    esac
    shift
done

step "pipeline self-test"
PYTHON=$(find_python) || die "python3 is required" 2
[ "$USE_QEMU" = 0 ] || have qemu-system-x86_64 || {
    warn "qemu-system-x86_64 is not installed; running the structure-only test"
    warn "pass --no-qemu explicitly in CI so this never silently degrades"
    USE_QEMU=0
}

WORK=$(mktempdir ravynos-pipeline-selftest)
register_tmpdir "$WORK"
if [ "$KEEP" = 1 ]; then
    # Detach from the cleanup trap so the caller can inspect the artifacts.
    unregister_tmpdirs
fi

fail() {
    printf '%sSELFTEST FAIL%s: %s\n' "$C_RED" "$C_RESET" "$*"
    [ "$KEEP" = 1 ] && printf 'work dir kept: %s\n' "$WORK"
    exit 1
}

# --------------------------------------------------------------------------
# 1. Build both formats
# --------------------------------------------------------------------------
step "1/6 build stub ISO and IMG"
"$SCRIPT_DIR/mkimage.sh" --stub --format both -o "$WORK/stub" --verify -q \
    || fail "mkimage --stub --format both"
[ -f "$WORK/stub.iso" ] || fail "no ISO produced"
[ -f "$WORK/stub.img" ] || fail "no IMG produced"

# --------------------------------------------------------------------------
# 2. Structural verification (independent parser, no xorriso/QEMU)
# --------------------------------------------------------------------------
step "2/6 verify structure"
"$PYTHON" "$SCRIPT_DIR/image/verify.py" iso "$WORK/stub.iso" --json \
    > "$WORK/iso.json" || fail "ISO failed structural verification"
"$PYTHON" "$SCRIPT_DIR/image/verify.py" img "$WORK/stub.img" --json \
    > "$WORK/img.json" || fail "IMG failed structural verification"
"$PYTHON" - "$WORK/iso.json" "$WORK/img.json" <<'PY' || fail "image contents are wrong"
import json, sys
iso, img = (json.load(open(p)) for p in sys.argv[1:3])
assert iso["valid"] and img["valid"], "verifier reported invalid"
assert iso["boot"], "stub ISO has no El Torito boot catalog"
boot = [e for e in iso["boot"]["entries"] if e["bootable"]]
assert boot, "stub ISO has no bootable El Torito entry"
assert boot[0]["load_sectors"] >= 4, "boot entry loads too few sectors"
iso_paths = {f["path"] for f in iso["files"]}
assert "BOOT/BOOTSTUB.BIN" in iso_paths, f"boot stub missing from ISO: {iso_paths}"
img_paths = {f["path"] for f in img["esp"]["files"]}
assert "BOOT/BOOTSTUB.BIN" in img_paths, f"boot stub missing from IMG: {img_paths}"
assert img["esp"]["cluster_count"] >= 4085, "IMG is not a legal FAT16 volume"
print("   contents match between ISO and IMG")
PY

# --------------------------------------------------------------------------
# 3. Reproducibility: same inputs -> identical bytes
# --------------------------------------------------------------------------
step "3/6 verify reproducibility"
"$SCRIPT_DIR/mkimage.sh" --stub -o "$WORK/again.iso" --no-checksum -q \
    || fail "second mkimage run"
if cmp -s "$WORK/stub.iso" "$WORK/again.iso"; then
    log "   ISO build is byte-for-byte reproducible"
else
    fail "two builds of the same tree produced different ISOs"
fi

# --------------------------------------------------------------------------
# 4. Change detection: different content -> different image
# --------------------------------------------------------------------------
step "4/6 verify that content changes are visible"
mkdir -p "$WORK/extra"
printf 'difference\n' > "$WORK/extra/EXTRA.TXT"
cp -R "$WORK/stub.iso" "$WORK/baseline.iso"
"$SCRIPT_DIR/mkimage.sh" --root "$WORK/extra" -o "$WORK/extra.iso" --no-checksum -q \
    || fail "mkimage --root"
if cmp -s "$WORK/baseline.iso" "$WORK/extra.iso"; then
    fail "a different staging tree produced an identical ISO"
fi
"$PYTHON" "$SCRIPT_DIR/image/verify.py" iso "$WORK/extra.iso" >/dev/null \
    || fail "staged-root ISO failed verification"

# --------------------------------------------------------------------------
# 5. Boot the stub ISO
# --------------------------------------------------------------------------
if [ "$USE_QEMU" = 1 ]; then
    step "5/6 boot stub ISO under QEMU"
    BOOTCHECK_TIMEOUT=${BOOTCHECK_TIMEOUT:-60} \
        "$SCRIPT_DIR/bootcheck.sh" "$WORK/stub.iso" \
        --expect RAVYNOS-BOOTCHECK-STUB-OK \
        --outdir "$WORK/boot-stub" --json "$WORK/boot-stub.json" -q \
        || fail "stub ISO did not boot"

    step "6/6 boot the milestone stub and check the whole chain"
    "$SCRIPT_DIR/mkimage.sh" --stub-milestones -o "$WORK/milestones.iso" -q \
        || fail "milestone stub build"
    BOOTCHECK_TIMEOUT=${BOOTCHECK_TIMEOUT:-60} \
        "$SCRIPT_DIR/bootcheck.sh" "$WORK/milestones.iso" --milestones \
        --outdir "$WORK/boot-milestones" --json "$WORK/boot-milestones.json" -q \
        || fail "milestone stub did not reach all four stages"
else
    step "5/6 boot checks skipped (--no-qemu)"
    step "6/6 milestone boot skipped (--no-qemu)"
fi

step "SELFTEST PASS"
printf '  iso   %s\n' "$(sha256_of "$WORK/stub.iso")"
printf '  img   %s\n' "$(sha256_of "$WORK/stub.img")"
[ "$KEEP" = 1 ] && printf '  work  %s\n' "$WORK"
exit 0
