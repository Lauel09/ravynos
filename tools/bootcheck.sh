#!/bin/sh
# Portable boot smoke test: boot an image under QEMU headless and turn the
# serial log into a pass/fail verdict.  macOS + Linux, no root required.
#
# Usage:
#   bootcheck.sh <image.iso|image.img> [options]
#
# Options:
#   --expect MARKER     marker that must appear on the serial console
#                       (repeatable; all must appear unless --expect-any).
#                       Default: RAVYNOS-BOOTCHECK-STUB-OK
#   --milestones        shorthand for the documented four-stage chain:
#                       loader -> kernel -> root mount -> userspace
#   --expect-any        succeed when any expectation is seen, not all
#   --timeout SECONDS   give up after this long (default 60)
#   --mem SIZE          guest RAM (default 2G)
#   --kernel PATH       boot PATH directly with -kernel instead of the image's
#                       bootloader (early kernel bring-up)
#   --append STRING     kernel command line for -kernel boots
#   --uefi              boot with OVMF firmware (needed for .img/ESP boots)
#   --screenshot        capture a framebuffer screenshot via the QEMU monitor
#   --outdir DIR        where serial.log / qemu.log / screenshot land; a fresh
#                       temp dir by default, always printed
#   --json PATH         write a machine-readable verdict record
#   -q                  quiet (verdict and log paths only)
#
# Environment:
#   BOOTCHECK_TIMEOUT   default for --timeout
#   BOOTCHECK_MEM       default for --mem
#   BOOTCHECK_ACCEL     kvm | hvf | tcg (default: best available)
#   BOOTCHECK_QEMU      QEMU binary (default qemu-system-x86_64)
#   QEMU_FLAGS          extra QEMU arguments, word-split on purpose
#   OVMF_CODE           firmware path for --uefi
#
# Exit status: 0 = all expectations seen, 1 = failure/timeout, 2 = usage or
# missing-tool error.  Serial and QEMU logs are always preserved.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/lib/common.sh"

IMAGE=
EXPECT=
TIMEOUT=${BOOTCHECK_TIMEOUT:-60}
MEM=${BOOTCHECK_MEM:-2G}
QEMU=${BOOTCHECK_QEMU:-qemu-system-x86_64}
KERNEL=
APPEND=
UEFI=0
SCREENSHOT=0
OUTDIR=
JSON_OUT=
EXPECT_ANY=0
QUIET=${QUIET:-0}

MILESTONES="RAVYNOS-LOADER-OK RAVYNOS-KERNEL-OK RAVYNOS-ROOT-MOUNT-OK RAVYNOS-USERSPACE-OK"

add_expect() {
    if [ -z "$EXPECT" ]; then EXPECT=$1; else EXPECT="$EXPECT
$1"; fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --expect)     add_expect "$2"; shift ;;
        --milestones) for m in $MILESTONES; do add_expect "$m"; done ;;
        --expect-any) EXPECT_ANY=1 ;;
        --timeout)    TIMEOUT=$2; shift ;;
        --mem)        MEM=$2; shift ;;
        --kernel)     KERNEL=$2; shift ;;
        --append)     APPEND=$2; shift ;;
        --uefi)       UEFI=1 ;;
        --screenshot) SCREENSHOT=1 ;;
        --outdir)     OUTDIR=$2; shift ;;
        --json)       JSON_OUT=$2; shift ;;
        -q|--quiet)   QUIET=1 ;;
        -h|--help)    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)           printf 'bootcheck: unknown option: %s\n' "$1" >&2; exit 2 ;;
        *)
            if [ -z "$IMAGE" ]; then IMAGE=$1
            else printf 'bootcheck: unexpected argument: %s\n' "$1" >&2; exit 2; fi ;;
    esac
    shift
done

[ -n "$IMAGE" ] || { printf 'usage: bootcheck.sh <image> [options]\n' >&2; exit 2; }
[ -f "$IMAGE" ] || { printf 'bootcheck: no such image: %s\n' "$IMAGE" >&2; exit 2; }
[ -n "$EXPECT" ] || EXPECT=RAVYNOS-BOOTCHECK-STUB-OK
have "$QEMU" || { printf 'bootcheck: missing %s\n' "$QEMU" >&2; exit 2; }
case $TIMEOUT in ''|*[!0-9]*) printf 'bootcheck: --timeout must be seconds\n' >&2; exit 2 ;; esac

if [ -z "$OUTDIR" ]; then
    OUTDIR=$(mktemp -d "${TMPDIR:-/tmp}/ravynos-bootcheck.XXXXXX") || exit 2
else
    mkdir -p "$OUTDIR" || exit 2
fi
LOG="$OUTDIR/serial.log"
QEMU_LOG="$OUTDIR/qemu.log"
SHOT="$OUTDIR/screenshot.ppm"
MON="$OUTDIR/monitor.sock"
: > "$LOG"
: > "$QEMU_LOG"
rm -f "$MON"

# --------------------------------------------------------------------------
# Acceleration: TCG is always correct; KVM/HVF are faster when available.
# --------------------------------------------------------------------------
ACCEL=${BOOTCHECK_ACCEL:-}
if [ -z "$ACCEL" ]; then
    case $(uname -s) in
        Darwin)
            if "$QEMU" -accel help 2>/dev/null | grep -qw hvf; then ACCEL=hvf; fi ;;
        Linux)
            if [ -r /dev/kvm ] && [ -w /dev/kvm ] &&
                "$QEMU" -accel help 2>/dev/null | grep -qw kvm; then ACCEL=kvm; fi ;;
    esac
fi

# --------------------------------------------------------------------------
# Firmware for .img (ESP) boots
# --------------------------------------------------------------------------
FW=
if [ "$UEFI" = 1 ]; then
    for candidate in \
        "${OVMF_CODE:-}" \
        /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
        /usr/local/share/qemu/edk2-x86_64-code.fd \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/ovmf/OVMF.fd \
        /usr/share/qemu/OVMF.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd; do
        if [ -n "$candidate" ] && [ -f "$candidate" ]; then FW=$candidate; break; fi
    done
    if [ -z "$FW" ]; then
        printf 'bootcheck: --uefi requested but no OVMF firmware found ' >&2
        printf '(set OVMF_CODE)\n' >&2
        exit 2
    fi
    log "bootcheck: firmware $FW"
fi

# --------------------------------------------------------------------------
# Launch
#
# Built with `set --` so paths with spaces survive and no word splitting is
# needed for anything except the deliberately word-split QEMU_FLAGS.
# --------------------------------------------------------------------------
set -- -m "$MEM"
if [ -n "$KERNEL" ]; then
    [ -f "$KERNEL" ] || { printf 'bootcheck: no such kernel: %s\n' "$KERNEL" >&2; exit 2; }
    set -- "$@" -kernel "$KERNEL"
    [ -n "$APPEND" ] && set -- "$@" -append "$APPEND"
else
    case $IMAGE in
        *.img|*.raw) set -- "$@" -drive "file=$IMAGE,format=raw,if=virtio" ;;
        *)           set -- "$@" -cdrom "$IMAGE" -boot d ;;
    esac
fi
[ -n "$ACCEL" ] && set -- "$@" -accel "$ACCEL"
[ -n "$FW" ] && set -- "$@" -drive "if=pflash,format=raw,readonly=on,file=$FW"
set -- "$@" -display none -monitor "unix:$MON,server,nowait" \
    -serial "file:$LOG" -no-reboot

log "bootcheck: $QEMU -m $MEM (accel ${ACCEL:-tcg}), timeout ${TIMEOUT}s"
# shellcheck disable=SC2086  # QEMU_FLAGS is intentionally word-split
"$QEMU" "$@" ${QEMU_FLAGS:-} >"$QEMU_LOG" 2>&1 &
QPID=$!

finished=0
cleanup() {
    if [ "$finished" = 0 ]; then
        kill "$QPID" 2>/dev/null || true
        wait "$QPID" 2>/dev/null || true
    fi
    return 0
}
trap cleanup EXIT INT TERM

# True while the guest is running.
#
# kill -0 cannot be used: a QEMU that already exited is a zombie until reaped,
# and zombies still answer signal 0.  ps state is the portable way to tell.
guest_running() {
    _state=$(ps -p "$QPID" -o stat= 2>/dev/null | tr -d ' ')
    [ -n "$_state" ] || return 1
    case $_state in Z*) return 1 ;; esac
    return 0
}

# Send one command to the QEMU monitor (screenshot, quit).
mon_cmd() {
    PY=$(find_python) || return 1
    "$PY" - "$MON" "$1" <<'PY' 2>/dev/null || return 1
import socket, sys, time
path, command = sys.argv[1], sys.argv[2]
sock = None
for _ in range(30):
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(3)
        sock.connect(path)
        break
    except OSError:
        time.sleep(0.1)
if sock is None:
    sys.exit(1)
time.sleep(0.1)
try:
    sock.recv(65536)          # banner
except OSError:
    pass
sock.sendall((command + "\n").encode())
time.sleep(0.4)
try:
    sock.recv(65536)          # response
except OSError:
    pass
sock.close()
PY
}

# --------------------------------------------------------------------------
# Poll
# --------------------------------------------------------------------------
elapsed=0
lines_shown=0
while [ "$elapsed" -lt "$TIMEOUT" ]; do
    total=$(wc -l < "$LOG" 2>/dev/null | tr -d ' ')
    total=${total:-0}
    if [ "$total" -gt "$lines_shown" ]; then
        tail -n $((total - lines_shown)) "$LOG" 2>/dev/null | sed 's/^/  | /'
        lines_shown=$total
    fi

    found=0
    missing=
    for m in $EXPECT; do
        if grep -Fq -- "$m" "$LOG" 2>/dev/null; then
            found=$((found + 1))
        else
            missing="$missing $m"
        fi
    done
    if [ "$EXPECT_ANY" = 1 ] && [ "$found" -gt 0 ]; then break; fi
    if [ "$EXPECT_ANY" = 0 ] && [ -z "$missing" ]; then break; fi

    guest_running || break

    if sleep 0.2 2>/dev/null; then
        ticks=5
    else
        sleep 1
        ticks=1
    fi
    elapsed=$((elapsed + 1))
    [ "$ticks" = 5 ] && elapsed=$(( (elapsed + 4) / 5 ))
done

running=1
guest_running || running=0

if [ "$running" = 1 ] && [ "$SCREENSHOT" = 1 ]; then
    if mon_cmd "screendump $SHOT"; then
        log "bootcheck: screenshot $SHOT"
    else
        warn "could not take a screenshot (monitor socket $MON)"
    fi
fi
if [ "$running" = 1 ]; then
    mon_cmd "quit" || kill "$QPID" 2>/dev/null || true
fi
wait "$QPID" 2>/dev/null
QSTATUS=$?
finished=1

# --------------------------------------------------------------------------
# Verdict
# --------------------------------------------------------------------------
found=0
missing=
for m in $EXPECT; do
    if grep -Fq -- "$m" "$LOG" 2>/dev/null; then
        found=$((found + 1))
    else
        missing="$missing $m"
    fi
done

if [ "$EXPECT_ANY" = 1 ]; then
    [ "$found" -gt 0 ] && pass=1 || pass=0
else
    [ -z "$missing" ] && pass=1 || pass=0
fi

if [ "$pass" = 1 ]; then
    seen=$(for m in $EXPECT; do
        grep -Fq -- "$m" "$LOG" 2>/dev/null && printf '%s ' "$m"
    done)
    printf '%sBOOTCHECK PASS%s: %s(accel %s)\n' \
        "$C_GREEN" "$C_RESET" "$seen" "${ACCEL:-tcg}"
    rc=0
elif [ "$running" = 0 ]; then
    printf '%sBOOTCHECK FAIL%s: QEMU exited (status %s) before:%s\n' \
        "$C_RED" "$C_RESET" "$QSTATUS" "${missing:- (marker)}"
    rc=1
else
    printf '%sBOOTCHECK FAIL%s: timed out after %ss waiting for:%s\n' \
        "$C_RED" "$C_RESET" "$TIMEOUT" "${missing:- (marker)}"
    rc=1
fi

printf 'serial log: %s\n' "$LOG"
printf 'qemu log:   %s\n' "$QEMU_LOG"
if [ "$rc" != 0 ]; then
    if [ -s "$LOG" ]; then
        printf -- '---- serial tail ----\n'
        tail -30 "$LOG"
    else
        printf -- '---- serial log is empty ----\n'
    fi
    if [ -s "$QEMU_LOG" ]; then
        printf -- '---- qemu tail ----\n'
        tail -20 "$QEMU_LOG"
    fi
    [ -f "$SHOT" ] && printf 'screenshot: %s\n' "$SHOT"
fi

if [ -n "$JSON_OUT" ]; then
    PY=$(find_python) || PY=
    markers_arg=$(printf '%s' "$EXPECT" | tr '\n' '\001')
    if [ -n "$PY" ]; then
        "$PY" - "$JSON_OUT" "$IMAGE" "$rc" "$elapsed" "${ACCEL:-tcg}" \
            "$QSTATUS" "$LOG" "$QEMU_LOG" "$missing" "$markers_arg" \
            "$SCREENSHOT" "$SHOT" <<'PY' || warn "could not write $JSON_OUT"
import json, os, sys
(out, image, rc, elapsed, accel, qstatus, log, qemu_log, missing,
 markers_arg, screenshot, shot) = sys.argv[1:]
record = {
    "schema": 1,
    "image": os.path.abspath(image),
    "image_bytes": os.path.getsize(image),
    "verdict": "pass" if rc == "0" else "fail",
    "expectations": [m for m in markers_arg.split("\x01") if m],
    "missing": missing.split(),
    "elapsed_seconds": int(elapsed),
    "acceleration": accel,
    "qemu_exit_status": int(qstatus),
    "serial_log": log,
    "qemu_log": qemu_log,
    "serial_bytes": os.path.getsize(log) if os.path.exists(log) else 0,
    "screenshot": shot if screenshot == "1" and os.path.exists(shot) else None,
}
with open(out, "w", encoding="utf-8") as fh:
    json.dump(record, fh, indent=2, sort_keys=True)
    fh.write("\n")
PY
        printf 'verdict json: %s\n' "$JSON_OUT"
    else
        warn "python3 not found; skipping the JSON verdict"
    fi
fi

exit "$rc"
