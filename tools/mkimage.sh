#!/bin/sh
# Portable image builder: macOS + Linux, root-free, mount-free.
#
# Replaces the FreeBSD-host-only ISO/build.sh for the boot-test pipeline.  It
# never calls mdconfig, zpool, gpart, makefs, mkimg, newfs_msdos, mount or
# chroot, and it refuses to run as root: everything is done with xorriso,
# mtools or the dependency-free Python backends in tools/image/.
#
# Modes (pick one):
#   --stub                 loader-only smoke-test image that writes a marker to
#                          COM1 (proves the xorriso -> QEMU -> verdict loop)
#   --stub-milestones      same, but emits a full boot-milestone sequence so
#                          the multi-marker verdict logic can be exercised
#   --root DIR             package a staged sysroot (kernel, loader, userland)
#                          exactly as it appears in DIR
#
# Output formats:
#   --format iso           El Torito hybrid ISO (default)
#   --format img           GPT disk with a FAT ESP (dd-able)
#   --format both          both of the above
#
# Examples:
#   tools/mkimage.sh --stub -o out/stub.iso
#   tools/mkimage.sh --root ../build/stage --boot-image BOOT/BOOTX64.BIN \
#                    --format both -o out/ravynos.iso --verify
#
# Every output gets a sibling .manifest.txt (deterministic, one line per file)
# and a .sha256 unless --no-checksum is given; --json writes a machine-readable
# build record for CI.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/lib/common.sh"

MODE=
ROOT_DIR=
OUT=
FORMAT=iso
SIZE_MB=
BACKEND=auto
BOOT_IMAGE=
EFI_IMAGE=
BOOT_LOAD_SIZE=4
EFI_LOAD_SIZE=1
KERNEL=
VOLUME_ID=RAVYNOS
FAT_LABEL=RAVYNOS
TIMESTAMP_SPEC=git
MANIFEST=
JSON_OUT=
CHECKSUM=1
VERIFY=0
DRY_RUN=0
QUIET=${QUIET:-0}
STUB_MARKER=

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Options:
  -o PATH                output path (default: ravynos-<version>-<sha>.<ext>)
  --format iso|img|both  output format(s), default iso
  --size-mb N            .img size in MiB (default: computed, minimum 16)
  --backend auto|xorriso|python|mtools
  --boot-image REL       staged path of the BIOS El Torito boot image
  --efi-image REL        staged path of the UEFI boot image
  --boot-load-size N     BIOS load size in 512-byte sectors (default 4)
  --efi-load-size N      UEFI load size in 512-byte sectors (default 1)
  --kernel PATH          copy PATH into the image as BOOT/KERNEL
  --volume-id ID         ISO volume identifier (default RAVYNOS)
  --label ID             FAT volume label (default RAVYNOS)
  --timestamp SPEC       epoch seconds, "SOURCE_DATE_EPOCH", or "git"
                         (default: git -- the commit time, so rebuilds match)
  --manifest PATH        write the text manifest here
  --json PATH            write a machine-readable build record here
  --no-checksum          do not write .sha256 files
  --verify               structurally verify the artifact(s) after building
  --print-plan           print what would be done, build nothing
  --list-tools           print host tool availability and exit
  -q                     quiet
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --stub)            MODE=stub ;;
        --stub-milestones) MODE=stub-milestones ;;
        --root)            MODE=root; ROOT_DIR=$2; shift ;;
        -o|--output)       OUT=$2; shift ;;
        --format)          FORMAT=$2; shift ;;
        --size-mb)         SIZE_MB=$2; shift ;;
        --backend)         BACKEND=$2; shift ;;
        --boot-image)      BOOT_IMAGE=$2; shift ;;
        --efi-image)       EFI_IMAGE=$2; shift ;;
        --boot-load-size)  BOOT_LOAD_SIZE=$2; shift ;;
        --efi-load-size)   EFI_LOAD_SIZE=$2; shift ;;
        --kernel)          KERNEL=$2; shift ;;
        --volume-id)       VOLUME_ID=$2; shift ;;
        --label)           FAT_LABEL=$2; shift ;;
        --timestamp)       TIMESTAMP_SPEC=$2; shift ;;
        --manifest)        MANIFEST=$2; shift ;;
        --json)            JSON_OUT=$2; shift ;;
        --no-checksum)     CHECKSUM=0 ;;
        --verify)          VERIFY=1 ;;
        --print-plan)      DRY_RUN=1 ;;
        -q|--quiet)        QUIET=1 ;;
        -h|--help)         usage; exit 0 ;;
        --list-tools)      MODE=list-tools ;;
        *)                 die "unknown option: $1 (try --help)" 2 ;;
    esac
    shift
done

# --------------------------------------------------------------------------
# Host survey
# --------------------------------------------------------------------------
print_tools() {
    step "host tools"
    printf '  %-22s %s\n' "host" "$(host_summary)"
    printf '  %-22s %s\n' "python3" "$(find_python || echo 'MISSING (required for the python backend)')"
    printf '  %-22s %s\n' "xorriso" "$(tool_version xorriso)"
    printf '  %-22s %s\n' "mformat (mtools)" "$(tool_version mformat)"
    printf '  %-22s %s\n' "nasm" "$(tool_version nasm)"
    printf '  %-22s %s\n' "qemu-system-x86_64" "$(tool_version qemu-system-x86_64)"
    printf '  %-22s %s\n' "git" "$(tool_version git)"
}

if [ "$MODE" = list-tools ]; then
    print_tools
    exit 0
fi

[ -n "$MODE" ] || die "pick a mode: --stub, --stub-milestones or --root DIR" 2

# Root-free by construction: running as root hides permission bugs and would
# let a stray mount succeed, which the portable pipeline must never need.
if [ "$(id -u)" = "0" ] && [ "${RAVYNOS_ALLOW_ROOT:-0}" != "1" ]; then
    die "refusing to run as root; the pipeline is designed to need no privileges"
fi

PYTHON=$(find_python) || true
if { [ "$BACKEND" = python ] || [ "$BACKEND" = auto ] || [ "$VERIFY" = 1 ]; } && [ -z "${PYTHON:-}" ]; then
    die "python3 is required for the selected backend/verification (or pass --backend xorriso)"
fi

EPOCH=$(case $TIMESTAMP_SPEC in
    git|"") default_epoch ;;
    SOURCE_DATE_EPOCH|source-date-epoch) default_epoch ;;
    *) printf '%s\n' "$TIMESTAMP_SPEC" ;;
esac)
case $EPOCH in ''|*[!0-9]*) die "--timestamp must be epoch seconds, 'git' or SOURCE_DATE_EPOCH" 2 ;; esac

VERSION=$(prod_version); [ -n "$VERSION" ] || VERSION=unknown
SHA=$(git_sha 7)

# --------------------------------------------------------------------------
# Staging
# --------------------------------------------------------------------------
STAGE=$(mktempdir ravynos-stage)
register_tmpdir "$STAGE"
mkdir -p "$STAGE/root"

stage_stub() {
    if [ "$MODE" = stub-milestones ]; then
        STUB_MARKER=RAVYNOS-USERSPACE-OK
        marker_args="--marker RAVYNOS-LOADER-OK --marker RAVYNOS-KERNEL-OK --marker RAVYNOS-ROOT-MOUNT-OK --marker RAVYNOS-USERSPACE-OK"
    else
        STUB_MARKER=RAVYNOS-BOOTCHECK-STUB-OK
        marker_args="--marker RAVYNOS-BOOTCHECK-STUB-OK"
    fi
    mkdir -p "$STAGE/root/BOOT"
    [ -n "$BOOT_IMAGE" ] || BOOT_IMAGE=BOOT/BOOTSTUB.BIN
    if have nasm && [ "$MODE" = stub ]; then
        # Keep the readable .asm authoritative when an assembler exists.
        nasm -f bin -o "$STAGE/root/$BOOT_IMAGE" "$SCRIPT_DIR/bootstub/bootstub.asm"
        log "staged boot stub with nasm (tools/bootstub/bootstub.asm)"
    else
        [ -n "${PYTHON:-}" ] || die "need nasm or python3 to build the boot stub"
        # shellcheck disable=SC2086 -- the marker list is intentionally split
        "$PYTHON" "$SCRIPT_DIR/image/bootstub.py" -o "$STAGE/root/$BOOT_IMAGE" $marker_args \
            || die "boot stub generation failed"
        log "staged boot stub with the python generator"
    fi
    cat > "$STAGE/root/README.TXT" <<EOF
ravynOS boot smoke-test image
built-at $(epoch_iso8601 "$EPOCH")
version $VERSION
commit $SHA
This image carries no operating system; it exists to prove the portable
image pipeline (tools/mkimage.sh -> tools/bootcheck.sh) end to end.
EOF
}

stage_root() {
    [ -d "$ROOT_DIR" ] || die "staging root is not a directory: $ROOT_DIR"
    [ -n "$(ls -A "$ROOT_DIR" 2>/dev/null)" ] || die "staging root is empty: $ROOT_DIR"
    # Copy the tree so --kernel can add to it without touching the caller's
    # directory, and so a partially written root cannot produce a torn image.
    cp -R "$ROOT_DIR/." "$STAGE/root/" || die "failed to copy $ROOT_DIR"
    if [ -n "$KERNEL" ]; then
        [ -f "$KERNEL" ] || die "kernel not found: $KERNEL"
        mkdir -p "$STAGE/root/BOOT"
        cp "$KERNEL" "$STAGE/root/BOOT/KERNEL"
        log "staged $(human_size "$(file_size "$KERNEL")") kernel as BOOT/KERNEL"
    fi
    # BIOS and UEFI boot images are different programs: a PE32+ EFI
    # application cannot be an El Torito BIOS boot image and vice versa, so
    # they are detected separately and never fall back into each other.
    if [ -z "$BOOT_IMAGE" ]; then
        for candidate in BOOT/LOADER BOOT/BOOT.BIN BOOT/BOOTSTUB.BIN; do
            if [ -f "$STAGE/root/$candidate" ]; then BOOT_IMAGE=$candidate; break; fi
        done
    fi
    if [ -z "$EFI_IMAGE" ]; then
        for candidate in EFI/BOOT/BOOTX64.EFI BOOT/BOOTX64.EFI EFI/BOOT/BOOTAA64.EFI; do
            if [ -f "$STAGE/root/$candidate" ]; then EFI_IMAGE=$candidate; break; fi
        done
    fi
    if [ -n "$BOOT_IMAGE" ] && [ ! -f "$STAGE/root/$BOOT_IMAGE" ]; then
        die "--boot-image $BOOT_IMAGE is not present in the staged root"
    fi
    if [ -n "$EFI_IMAGE" ] && [ ! -f "$STAGE/root/$EFI_IMAGE" ]; then
        die "--efi-image $EFI_IMAGE is not present in the staged root"
    fi
}

step "staging ($MODE)"
case $MODE in
    stub|stub-milestones) stage_stub ;;
    root)                 stage_root ;;
esac

STAGED_FILES=$(find "$STAGE/root" -type f | wc -l | tr -d ' ')
STAGED_BYTES=$(find "$STAGE/root" -type f -exec cat {} + 2>/dev/null | wc -c | tr -d ' ')
log "  $STAGED_FILES file(s), $(human_size "$STAGED_BYTES")"

# --------------------------------------------------------------------------
# Backend selection
# --------------------------------------------------------------------------
[ "$FORMAT" = iso ] || [ "$FORMAT" = img ] || [ "$FORMAT" = both ] ||
    die "--format must be iso, img or both" 2

want_iso=0; want_img=0
case $FORMAT in
    iso)  want_iso=1 ;;
    img)  want_img=1 ;;
    both) want_iso=1; want_img=1 ;;
esac

ISO_BACKEND=
IMG_BACKEND=
if [ "$want_iso" = 1 ]; then
    case $BACKEND in
        auto|xorriso)
            if have xorriso; then ISO_BACKEND=xorriso
            elif [ "$BACKEND" = xorriso ]; then die "--backend xorriso requested but xorriso is missing"
            else ISO_BACKEND=python; fi ;;
        python) ISO_BACKEND=python ;;
        mtools) die "--backend mtools cannot write ISO images; use xorriso or python" 2 ;;
        *) die "unknown --backend: $BACKEND" 2 ;;
    esac
fi
if [ "$want_img" = 1 ]; then
    case $BACKEND in
        auto)      if have mformat && have mcopy; then IMG_BACKEND=mtools; else IMG_BACKEND=python; fi ;;
        mtools)    need mformat mcopy; IMG_BACKEND=mtools ;;
        python)    IMG_BACKEND=python ;;
        xorriso)   die "--backend xorriso cannot write .img; use mtools or python" 2 ;;
        *)         die "unknown --backend: $BACKEND" 2 ;;
    esac
fi

# --------------------------------------------------------------------------
# Output naming
# --------------------------------------------------------------------------
BASE=
if [ -n "$OUT" ]; then
    case $OUT in
        *.iso|*.img) BASE=${OUT%.*} ;;
        *) BASE=$OUT ;;
    esac
else
    case $MODE in
        stub|stub-milestones) BASE="ravynos-stub-${SHA}" ;;
        root)                 BASE="ravynos-${VERSION}-${SHA}" ;;
    esac
fi
mkdir -p "$(dirname -- "$BASE")" 2>/dev/null || true
[ -n "$MANIFEST" ] || MANIFEST="${BASE}.manifest.txt"
[ -n "$JSON_OUT" ] || JSON_OUT="${BASE}.build.json"

if [ "$DRY_RUN" = 1 ]; then
    step "plan"
    printf '  mode           %s\n' "$MODE"
    printf '  stage          %s file(s), %s\n' "$STAGED_FILES" "$(human_size "$STAGED_BYTES")"
    printf '  format(s)      %s\n' "$FORMAT"
    if [ "$want_iso" = 1 ]; then printf '  iso            %s.iso  (backend: %s)\n' "$BASE" "$ISO_BACKEND"; fi
    if [ "$want_img" = 1 ]; then printf '  img            %s.img  (backend: %s)\n' "$BASE" "$IMG_BACKEND"; fi
    printf '  boot image     %s\n' "${BOOT_IMAGE:-<none>}"
    [ -n "$EFI_IMAGE" ] && printf '  efi image      %s\n' "$EFI_IMAGE"
    printf '  timestamp      %s\n' "$EPOCH"
    printf '  volume id      %s\n' "$VOLUME_ID"
    exit 0
fi

# --------------------------------------------------------------------------
# Writers
# --------------------------------------------------------------------------
write_iso() {
    out=$1
    if [ "$ISO_BACKEND" = xorriso ]; then
        set -- -as mkisofs -quiet -V "$VOLUME_ID" -o "$out" \
            -b "$BOOT_IMAGE" -no-emul-boot -boot-load-size "$BOOT_LOAD_SIZE"
        if [ -n "$EFI_IMAGE" ]; then
            set -- "$@" -eltorito-alt-boot -e "$EFI_IMAGE" -no-emul-boot
        fi
        # A fixed timestamp keeps repeated builds of the same tree identical.
        SOURCE_DATE_EPOCH=$EPOCH xorriso "$@" \
            -publisher "ravynOS portable pipeline" \
            "$STAGE/root" || die "xorriso failed"
    else
        set -- --volume-id "$VOLUME_ID" --timestamp "$EPOCH" \
            --print-manifest "$MANIFEST"
        [ -n "$BOOT_IMAGE" ] && set -- "$@" --boot-image "$BOOT_IMAGE" \
            --boot-load-size "$BOOT_LOAD_SIZE"
        [ -n "$EFI_IMAGE" ] && set -- "$@" --efi-image "$EFI_IMAGE" \
            --efi-load-size "$EFI_LOAD_SIZE"
        "$PYTHON" "$SCRIPT_DIR/image/iso9660.py" build "$STAGE/root" -o "$out" "$@" ||
            die "python ISO writer failed"
    fi
}

write_img() {
    out=$1
    if [ "$IMG_BACKEND" = mtools ]; then
        fat_img="$STAGE/fat.img"
        # mtools needs the geometry up front; 32 sectors/track, 64 heads is
        # the conventional geometry every tool understands.
        sectors=$(( ${SIZE_MB:-16} * 1024 * 1024 / 512 ))
        mformat -i "$fat_img" -C -T "$sectors" -h 64 -s 32 -v "$FAT_LABEL" -N 0 :: ||
            die "mformat failed"
        ( cd "$STAGE/root" && mcopy -i "$fat_img" -s -Q -m . :: ) || die "mcopy failed"
        size_opt=
        [ -n "$SIZE_MB" ] && size_opt="--size-mb $SIZE_MB"
        # shellcheck disable=SC2086
        "$PYTHON" "$SCRIPT_DIR/image/fat.py" wrap --fat "$fat_img" -o "$out" \
            --offset-sectors 2048 --label "$FAT_LABEL" $size_opt ||
            die "GPT wrap failed"
    else
        set -- --label "$FAT_LABEL" --timestamp "$EPOCH"
        [ -n "$SIZE_MB" ] && set -- "$@" --size-mb "$SIZE_MB"
        "$PYTHON" "$SCRIPT_DIR/image/fat.py" build "$STAGE/root" -o "$out" "$@" ||
            die "python FAT/GPT writer failed"
    fi
}

# The python ISO writer needs a boot image to be bootable; warn rather than
# quietly shipping a non-bootable artifact, since that is the whole point.
if [ "$want_iso" = 1 ] && [ -z "$BOOT_IMAGE" ]; then
    warn "no boot image selected; the ISO will be valid but not bootable"
fi

ARTIFACTS=
make_one() {
    kind=$1
    out="$BASE.$kind"
    step "writing $out"
    case $kind in
        iso) write_iso "$out" ;;
        img) write_img "$out" ;;
    esac
    [ -f "$out" ] || die "$kind writer reported success but produced no file"
    bytes=$(file_size "$out")
    digest=$(sha256_of "$out")
    if [ "$CHECKSUM" = 1 ]; then
        printf '%s  %s\n' "$digest" "$(basename "$out")" > "$out.sha256"
    fi
    printf '  %s (%s), sha256 %s\n' "$out" "$(human_size "$bytes")" "$digest"
    ARTIFACTS="$ARTIFACTS $out"
}

if [ "$want_iso" = 1 ]; then make_one iso; fi
if [ "$want_img" = 1 ]; then make_one img; fi

# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------
verify_failed=0
if [ "$VERIFY" = 1 ]; then
    [ -n "${PYTHON:-}" ] || die "--verify needs python3"
    for artifact in $ARTIFACTS; do
        step "verifying $artifact"
        case $artifact in
            *.iso) kind=iso ;;
            *.img) kind=img ;;
            *) kind=auto ;;
        esac
        if ! "$PYTHON" "$SCRIPT_DIR/image/verify.py" "$kind" "$artifact"; then
            verify_failed=1
        fi
    done
fi

# --------------------------------------------------------------------------
# Manifest + build record
# --------------------------------------------------------------------------
{
    echo "# ravynOS image manifest"
    echo "version $VERSION"
    echo "commit $SHA"
    echo "mode $MODE"
    echo "host $(host_summary)"
    echo "epoch $EPOCH"
    echo "volume_id $VOLUME_ID"
    echo "staged_files $STAGED_FILES"
    echo "staged_bytes $STAGED_BYTES"
    if [ -n "$BOOT_IMAGE" ]; then echo "boot_image $BOOT_IMAGE"; fi
    if [ -n "$EFI_IMAGE" ]; then echo "efi_image $EFI_IMAGE"; fi
    for artifact in $ARTIFACTS; do
        echo "artifact $(basename "$artifact") $(file_size "$artifact") $(sha256_of "$artifact")"
    done
    echo "# staged content"
    ( cd "$STAGE/root" && find . -type f | sed 's|^\./||' | sort )
} > "$MANIFEST"

if [ -n "${PYTHON:-}" ]; then
    "$PYTHON" - "$JSON_OUT" "$VERSION" "$SHA" "$MODE" "$EPOCH" "$VOLUME_ID" \
        "$ISO_BACKEND" "$IMG_BACKEND" "$STAGED_FILES" "$STAGED_BYTES" \
        "$BOOT_IMAGE" "$EFI_IMAGE" $ARTIFACTS <<'PY'
import json, os, sys
(out, version, sha, mode, epoch, volume_id, iso_backend, img_backend,
 staged_files, staged_bytes, boot_image, efi_image, *artifacts) = sys.argv[1:]
record = {
    "schema": 1,
    "version": version,
    "commit": sha,
    "mode": mode,
    "host": os.uname().sysname + " " + os.uname().machine,
    "epoch": int(epoch),
    "volume_id": volume_id,
    "backends": {"iso": iso_backend or None, "img": img_backend or None},
    "staging": {"files": int(staged_files), "bytes": int(staged_bytes)},
    "boot_image": boot_image or None,
    "efi_image": efi_image or None,
    "artifacts": [],
}
for path in artifacts:
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    record["artifacts"].append({
        "path": path,
        "bytes": os.path.getsize(path),
        "sha256": h.hexdigest(),
    })
with open(out, "w", encoding="utf-8") as fh:
    json.dump(record, fh, indent=2, sort_keys=True)
    fh.write("\n")
PY
fi

step "done"
for artifact in $ARTIFACTS; do
    printf '  %s\n' "$artifact"
done
printf '  manifest %s\n' "$MANIFEST"
if [ -n "$STUB_MARKER" ]; then printf '  bootcheck expects: %s\n' "$STUB_MARKER"; fi
[ "$verify_failed" = 0 ] || exit 1
exit 0
