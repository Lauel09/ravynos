# UEFI loader skeleton (Step B of `Docs/BootLoader.md`)

A freestanding PE32+ UEFI application that prints the boot milestone markers to
the serial console and returns.  It is deliberately tiny and has **no
dependencies**: the handful of UEFI types it needs are declared in the source,
so it builds with clang + lld-link and nothing else — no gnu-efi, no EDK2 tree,
no SDK.

What it proves, once it boots under OVMF:

1. `tools/mkimage.sh --efi-image` emits a correct UEFI El Torito catalog entry;
2. the ISO/ESP handoff works, including the FAT volume the firmware exposes;
3. firmware reaches our entry point and we can drive a console that a headless
   CI run can read.

That is the whole point: it isolates *loader plumbing* from *kernel work*, so a
failure in Step D (Mach-O loading) cannot be confused with a failure in Step B.

## Build

```sh
cd tools/bootloader
./build.sh                # produces BOOTX64.EFI
./build.sh --clean
make                      # same thing, under GNU make or bmake
```

Requirements: clang and `lld-link` (they ship with the ravynOS toolchain, Xcode
and LLVM/Homebrew), **or** a `zig` binary — `pip install ziglang` provides one —
whose bundled clang and COFF linker build the same PE32+ image.  `build.sh`
picks whichever exists, builds, and then runs `tools/image/pe.py` on the output
so a wrong subsystem or a non-PE result fails here rather than as a boot
timeout.  `IMAGE_SUBSYSTEM_EFI_APPLICATION` is subsystem 10.

The entry symbol is `EfiMain`: that is lld-link's default entry for a PE image,
and zig's linker accepts no `/entry:` override, so one source file links with
both.  The Makefile exists only as a thin wrapper — it works under GNU make and
bmake alike because all the flags live in `build.sh`.

## Package and boot

```sh
stage=$(mktemp -d)
mkdir -p "$stage/EFI/BOOT"
./build.sh -o "$stage/EFI/BOOT/BOOTX64.EFI"
tools/mkimage.sh --root "$stage" --format both \
    --efi-image EFI/BOOT/BOOTX64.EFI -o out/uefi --verify
tools/bootcheck.sh out/uefi.img --uefi --expect RAVYNOS-LOADER-OK
```

`mkimage.sh` refuses an `--efi-image` that is not a PE32+ EFI application, and
sizes the El Torito entry from the file (`--efi-load-size auto`); firmware reads
exactly that many sectors, so a smaller count would boot a truncated image.

`bootcheck.sh --uefi` needs OVMF firmware; it searches the usual Homebrew,
Debian and Fedora paths, honours `OVMF_CODE`, and — for split `CODE`/`VARS`
firmware — copies the matching variable store instead of writing to the
system's.  The `uefi` job in `.github/workflows/pipeline.yml` runs this whole
sequence on every push and boots both the `.img` and the `.iso`.

## Status

| Piece | State |
|---|---|
| builds into a PE32+ EFI application | verified (`tools/image/pe.py`, CI) |
| OVMF boots the ESP image and the ISO | CI job `uefi` |
| entry point, console discovery, markers | implemented |
| `ExitBootServices` sequence | implemented |
| reading a file from the boot volume | **TODO** (Step C) |
| Mach-O segment loading, `boot_args`, handoff | **TODO** (Step D) |

The milestone markers and their meanings are a contract with
`tools/bootcheck.sh`; see `Docs/Pipeline.md` §4 and `Docs/BootLoader.md` §2.

## Notes for the next step

* Read the kernel from the **FAT volume the loader was started from**, not from
  the ISO: some firmware does not implement ISO 9660 at all.
* `AllocatePages` (not `AllocatePool`) for kernel segments, with
  `AllocateMaxAddress` below 4 GiB.
* Re-read the memory map and retry `ExitBootServices` once: the map can change
  between `GetMemoryMap` and the call.
* Keep the loader free of build timestamps so images stay reproducible
  (`Docs/Pipeline.md` §5).
