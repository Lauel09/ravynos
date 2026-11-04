# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep this file under 600 words; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain (LLVM, cctools, xcbuild, iig), XNU + all kexts, `IOKitUser`, `libc++`/`libc++abi`, compiler-rt and objc4. Stop is `Libraries/dyld`: `libdyld.dylib` fixed (`76918cbcec`); the `dyld` executable fails `ld: library not found for -lc` because `Libraries/Makefile:11` builds `dyld` before `Libsystem` installs `/usr/local/lib/dyld/{libc,libpthread,libplatform}.a`.
- **Boot (D6 PASSED; live kext linking + ACPI/PCI/AHCI + `IOFindBSDRoot` reached):** `BOOTX64.EFI` stages 30 `.kext` bundles (`Driver-<addr>`), sets root `compatible="ACPI"` + `/ACPI/RSDP`, and relocates the 256 KiB boot-data window above staged kexts (`f5ff2c74b`, `d7504302e`). With `OSKext::loadExecutable()` allowing `kernel_task` linking (`48ca4834c`), XNU runs 25s in QEMU with 0 panics: links all kexts live via `kxld`, attaches `ACPIPlatformExpert`, `AppleAPIC`, `q35` PCI (`6 devices`), `RavynAHCIPort`, and `IOStorageFamily`, and waits in `IOFindBSDRoot()` for an `Apple_HFS` root volume.
- **Open defects:** mtools `.img` output is not byte-reproducible (`auto` backend no longer selects mtools). Darwin prelinked kernelcache is skipped (`plktool` is Linux-only), bypassed cleanly via live `kxld` linking of `BOOTX64.EFI` `Driver-<addr>` kexts.
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. Add a portable rootless HFS+/HFSX image writer (`tools/image/hfs.py`, MBR type `0xAF` -> `IOFDiskPartitionScheme` `"Content" = "Apple_HFS"`) and attach `$OUT/root.img` on AHCI in `tools/xnu-boot.sh` so `IOFindBSDRoot()` + `hfs.kext` mount `/` and reach `load_init_program("/sbin/launchd")`.
2. Split `Libraries/dyld` (`libdyld.dylib` before `Libsystem`, `/usr/lib/dyld` after `Libsystem`) to resolve the `-lc` cycle, then build `Libraries` + `BSD` (`/sbin/launchd` + `/bin/sh`).
3. Stage built userland into `$OUT/root.img` and boot ravynOS to an interactive shell in QEMU.

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
- 2026-11-04: Live `kxld` linking of 30 booter kexts + `ACPIPlatformExpert` + PCI + `RavynAHCIPort` + `IOStorageFamily` -> `IOFindBSDRoot()`, 25s / 0 panics (`f5ff2c74b`, `d7504302e`, `48ca4834c`)
- 2026-11-04: Bare XNU reaches `StartIOKit()` / `IOPanicPlatform::start` with zero CPU faults after boot-window relocation + EFI runtime strip (`719fa5b2ba`, `52d36895fd`)
- 2026-11-03: D6 GATE PASSED — XNU prints `Darwin Kernel Version 19.6.0` on serial (`ba3dc57c1`)
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
- 2026-11-03: XNU executes in 64-bit long mode; fault root-caused to `PE_get_random_seed()` = 0 (`cc832cf4eb`)
