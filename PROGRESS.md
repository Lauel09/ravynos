# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep this file under 600 words; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain (LLVM, cctools, xcbuild, iig), XNU + all kexts, `IOKitUser`, `libc++`/`libc++abi`, compiler-rt and objc4. Stop is `Libraries/dyld`: `libdyld.dylib` failure fixed (`76918cbcec`; targeted build succeeds); the next targeted step shows the `dyld` executable failing `ld: library not found for -lc` — not yet confirmed by a full world run.
- **Boot (D6 PASSED; `StartIOKit` / `IOPanicPlatform` reached):** `tools/bootloader/BOOTX64.EFI` supplies `/chosen/random-seed` (`ba3dc57c1`), strips `EFI_MEMORY_RUNTIME` + stubs `ba->efiSystemTable` (`719fa5b2ba`), and stages the 256 KiB boot-data window above `__LINKEDIT` (`0x15aa000`, included in `ba->ksize`, `52d36895fd`). Bare XNU boots under QEMU+OVMF (`-cpu Penryn`) with zero CPU faults (2.01s uptime) through Mach VM/PMAP/IPC/scheduler, `bsd_early_init()`, and `StartIOKit()` up to `IOPanicPlatform::start` (`Unable to find driver for this platform: ""`, `loaded kexts: (none)`).
- **Open defects:** mtools `.img` output is not byte-reproducible (differing bytes are 64-bit stack addresses at the end of the FAT volume — uninitialised memory, upstream); `auto` backend no longer selects mtools. Darwin prelinked kernelcache is skipped (`plktool` is Linux-only); `BOOTX64.EFI` kext staging via `/chosen/memory-map` `Driver-<addr>` is the agreed Darwin path.
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. Stage `\System\Library\Extensions\*.kext` (and `System.kext\PlugIns\*.kext`) in `BOOTX64.EFI` as page-aligned `_BooterKextFileInfo` buffers under `/chosen/memory-map` (`Driver-<addr>`), set root DT `compatible="ACPI"`, and populate `/ACPI/RSDP` from `EFI_ACPI_20_TABLE_GUID` so `IOACPIFamily`'s `ACPIPlatformExpert` attaches.
2. Observe IOKit PCI/storage matching with `IOACPIFamily` + `IOPCIFamily` + `IOStorageFamily` loaded, then advance toward BSD root mount (`vfs_mountroot()`).
3. Re-run or target `Libraries/dyld` to fix the `dyld` executable `-lc` link failure, then continue `Libraries` and `BSD` world stages for `/sbin/launchd` + `/bin/sh`.

## Key commands
- Remote runner: `tools/arena-run.sh <branch> <task> [<task> ...]`
- Build: `tools/macbuild.sh --audit --target-arch x86_64` — logs `work/logs/world-N.log`
- Loader: `tools/bootloader/build.sh -q`
- Local XNU boot: `tools/xnu-boot.sh` (logs `out/arena/boot/serial.log`, `qemu-int.log`)
- Boot verdict: `tools/bootcheck.sh --uefi` (CI-style), `tools/darwin-gate.sh` (stub/XNU gate)
- Structural verify: `tools/image/verify.py <iso|img> …`

## Pointers
- `Docs/DarwinBoot.md` — boot plan (D1–D6 complete); §"Post-D6 handoff status (2026-11-04)" has the fault table and next fix
- `Docs/macOS-Host.md` — macOS prerequisites, build loop, failure classes, portability audit (§5)
- `Docs/BUILDING.md` — entry points (`bmake world`), Homebrew prereqs, target vs host arch
- `Docs/Pipeline.md` — portable image pipeline: design, commands (§3), reproducibility, CI
- `Docs/BootLoader.md` / `Docs/Shell.md` — loader decision + staged plan; kernel console and EFI shell
- `Docs/PROGRESS-archive.md` — full history and changelog (everything removed from this file)

## Recent changes
- 2026-11-04: Bare XNU reaches `StartIOKit()` / `IOPanicPlatform::start` with zero CPU faults after boot-window relocation + EFI runtime strip (`719fa5b2ba`, `52d36895fd`)
- 2026-11-03: D6 GATE PASSED — XNU prints `Darwin Kernel Version 19.6.0` on serial (`ba3dc57c1`)
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
- 2026-11-03: XNU executes in 64-bit long mode; fault root-caused to `PE_get_random_seed()` = 0 (`cc832cf4eb`)
- 2026-11-03: OVMF `#UD 0xB0000` does not reproduce locally; FAT-wrapper sizing + `EFI_LOADED_IMAGE_PROTOCOL` layout fixed (`2ceef99c0e`, `5dce5e36e5`)
