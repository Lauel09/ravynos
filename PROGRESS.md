# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep this file under 600 words; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain (LLVM, cctools, xcbuild, iig), XNU + all kexts, `IOKitUser`, `libc++`/`libc++abi`, compiler-rt and objc4. Stop is `Libraries/dyld`: `libdyld.dylib` failure fixed (`76918cbcec`; targeted build succeeds); the next targeted step shows the `dyld` executable failing `ld: library not found for -lc` — not yet confirmed by a full world run.
- **Boot:** loader reaches XNU in 64-bit long mode (`cc832cf4eb`). Current fault: `v=0e` at `_current_cpu_datap+0x14` (`mov %gs:0,%rax`, CR2 `0xffffdeadbeefee00`, GS poison); panic frame points at `"Expected 64 seed bytes"` (`prng_random.c:65`) — `PE_get_random_seed()` returns 0, so `panic()` faults before printing.
- **Open defects:** mtools `.img` output is not byte-reproducible (differing bytes are 64-bit stack addresses at the end of the FAT volume — uninitialised memory, upstream); `auto` backend no longer selects mtools. Darwin prelinked kernelcache is skipped (plktool is Linux-only). No real `D1 GATE PASSED` yet.
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`; the remaining fault is the D6 handoff, not firmware.

## Next steps
1. Break at `PE_get_random_seed`/`DTGetProperty` via lldb on the QEMU gdbstub (`-S -s`); read the returned size and property bytes at the actual call.
2. If the boot window (`0x1A0000-0x1E0000`) is wiped before `early_random()`, move/reserve device tree + `boot_args` above `kaddr+ksize`.
3. Diagnostics only: point `MSR_IA32_GS_BASE` at a zeroed page before the jump so `panic()` can print; fix the next fault after.
4. Re-run the full world build; confirm and fix the `dyld` executable `-lc` failure, then the BSD stage.
5. Chase `Darwin Kernel Version` in the serial log (the D6 gate), then retire the D1 gate claim.

## Key commands
- Build: `tools/macbuild.sh --audit --target-arch x86_64` — logs `work/logs/world-N.log`
- Loader: `tools/bootloader/build.sh -q`
- Local boot: `Docs/DarwinBoot.md` §3 recipe (`mkimage.sh --root … --efi-payload … --format img` + `qemu-system-x86_64 … -serial file:out/local/serial.log`)
- Boot verdict: `tools/bootcheck.sh --uefi` (CI-style), `tools/darwin-gate.sh` (stub/XNU gate)
- Structural verify: `tools/image/verify.py <iso|img> …`
- Boot logs: `out/local/serial.log`, `work/logs/world-*.log`

## Pointers
- `Docs/DarwinBoot.md` — boot plan; §"D6 handoff status (2026-11-03)" has the fault table and next probes; §3 local boot recipe
- `Docs/macOS-Host.md` — macOS prerequisites, build loop, failure classes, portability audit (§5)
- `Docs/BUILDING.md` — entry points (`bmake world`), Homebrew prereqs, target vs host arch
- `Docs/Pipeline.md` — portable image pipeline: design, commands (§3), reproducibility, CI
- `Docs/BootLoader.md` / `Docs/Shell.md` — loader decision + staged plan; kernel console and EFI shell
- `Docs/PROGRESS-archive.md` — full history and changelog (everything removed from this file)

## Recent changes
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
- 2026-11-03: XNU executes in 64-bit long mode; fault root-caused to `PE_get_random_seed()` = 0 (`cc832cf4eb`)
- 2026-11-03: OVMF `#UD 0xB0000` does not reproduce locally; FAT-wrapper sizing + `EFI_LOADED_IMAGE_PROTOCOL` layout fixed (`2ceef99c0e`, `5dce5e36e5`)
- 2026-11-03: Phase 1 world reaches userland libraries; CrashReporterClient linked as static `.a` (`78043ebe03`)
- 2026-11-03: Phase 1 world runs past the toolchain (LLVM, cctools, xcbuild, iig) (`44c2437550`)
