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
make                      # produces BOOTX64.EFI
make clean
```

Requirements: clang and `lld-link` (both ship with the ravynOS toolchain and
with LLVM/Homebrew).  `IMAGE_SUBSYSTEM_EFI_APPLICATION` is subsystem 10, which
is what `-Wl,-subsystem:10` selects.

## Package and boot

```sh
tools/mkimage.sh --stub --format both -o out/uefi \
    --efi-image EFI/BOOT/BOOTX64.EFI ...       # after staging the .efi
tools/bootcheck.sh out/uefi.img --uefi --expect RAVYNOS-LOADER-OK
```

`bootcheck.sh --uefi` needs OVMF firmware; it searches the usual Homebrew,
Debian and Fedora paths, and honours `OVMF_CODE`.

## Status

| Piece | State |
|---|---|
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
