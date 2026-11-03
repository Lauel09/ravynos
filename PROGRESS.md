# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep this file under 600 words; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain (LLVM, cctools, xcbuild, iig), XNU + all kexts, `IOKitUser`, `libc++`/`libc++abi`, compiler-rt and objc4. Stop is `Libraries/dyld`: `libdyld.dylib` failure fixed (`76918cbcec`; targeted build succeeds); the next targeted step shows the `dyld` executable failing `ld: library not found for -lc` — not yet confirmed by a full world run.
- **Boot (D6 GATE PASSED):** `tools/bootloader/BOOTX64.EFI` supplies a 64-byte nonzero `/chosen/random-seed` (`ba3dc57c1`), booting built XNU under QEMU+OVMF (`-cpu Penryn`) past `early_random()`, `Idle_PTs_init()`, `cpu_desc_load()`, `tsc_init()`, and VM/IPC/PMAP bootstrap to print `Darwin Kernel Version 19.6.0` on serial. Post-D6 fault: `#GP` (`v=0d`, `RIP=0xae557bfa39129d48`, `RSP=0xffffff8000194e40`) right after `Initializing EFI runtime services` (`model_dep.c:503`) because raw `EFI_MEMORY_RUNTIME` descriptors have `VirtualStart == 0` and `efi_init()` maps them over `0xffffff8000000000` (`__HIB`/boot stack).
- **Open defects:** mtools `.img` output is not byte-reproducible (differing bytes are 64-bit stack addresses at the end of the FAT volume — uninitialised memory, upstream); `auto` backend no longer selects mtools. Darwin prelinked kernelcache is skipped (plktool is Linux-only).
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. Post-D6 `efi_init()` fix (agreed Option B): clear `EFI_MEMORY_RUNTIME` in the `boot_args->MemoryMap` copy (`i386_vm_init.c:530` still reserves non-`ConventionalMemory` types) and point `ba->efiSystemTable` at a zeroed boot-window stub so boot advances into `StartIOKit()` / `bsd_init()`.
2. Observe the next stop inside `StartIOKit()` / `bsd_init()` on bare `kernel`, then plan kext / `DriversPackage-` or prelinked kernelcache loading and `/efi/platform` + `RSDP` properties for `IOACPIFamily`.
3. Re-run or target `Libraries/dyld` to fix the `dyld` executable `-lc` link failure, then continue the `Libraries` and `BSD` world stages.

## Key commands
- Remote runner: `tools/arena-run.sh <branch> <task> [<task> ...]`
- Build: `tools/macbuild.sh --audit --target-arch x86_64` — logs `work/logs/world-N.log`
- Loader: `tools/bootloader/build.sh -q`
- Local XNU boot: `tools/xnu-boot.sh` (logs `out/arena/boot/serial.log`, `qemu-int.log`)
- Boot verdict: `tools/bootcheck.sh --uefi` (CI-style), `tools/darwin-gate.sh` (stub/XNU gate)
- Structural verify: `tools/image/verify.py <iso|img> …`

## Pointers
- `Docs/DarwinBoot.md` — boot plan (D1–D6 complete); §"Post-D6 handoff status (2026-11-03)" has the fault table and next fix
- `Docs/macOS-Host.md` — macOS prerequisites, build loop, failure classes, portability audit (§5)
- `Docs/BUILDING.md` — entry points (`bmake world`), Homebrew prereqs, target vs host arch
- `Docs/Pipeline.md` — portable image pipeline: design, commands (§3), reproducibility, CI
- `Docs/BootLoader.md` / `Docs/Shell.md` — loader decision + staged plan; kernel console and EFI shell
- `Docs/PROGRESS-archive.md` — full history and changelog (everything removed from this file)

## Recent changes
- 2026-11-03: D6 GATE PASSED — XNU prints `Darwin Kernel Version 19.6.0` on serial; post-D6 fault traced to `efi_init()` `VirtualStart == 0` (`ba3dc57c1`)
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
- 2026-11-03: XNU executes in 64-bit long mode; fault root-caused to `PE_get_random_seed()` = 0 (`cc832cf4eb`)
- 2026-11-03: OVMF `#UD 0xB0000` does not reproduce locally; FAT-wrapper sizing + `EFI_LOADED_IMAGE_PROTOCOL` layout fixed (`2ceef99c0e`, `5dce5e36e5`)
- 2026-11-03: Phase 1 world reaches userland libraries; CrashReporterClient linked as static `.a` (`78043ebe03`)
