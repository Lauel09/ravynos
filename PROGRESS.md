# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep it concise; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain, XNU + kexts, IOKitUser, libc++/compiler-rt, objc4, `libdyld.dylib`, and all Libsystem local objects. `bmake Libraries` completes (all 24 libsystem dylibs, `libSystem.B.dylib`, `dyld`, zlib). Libsystem recursion unblocked (`b24e956f50`, 2026-11-05). Libsystem internal-MIG-header race fixed (`8eec9044b3`).
- **Boot (D6 PASSED; `/` mounted via `hfs.kext` + `devfs` + PID 1 `load_init_program` reached):** `BOOTX64.EFI` stages 30 `.kext` bundles (`Driver-<addr>`) + `/ACPI/RSDP` (`f5ff2c74b`, `d7504302e`), `OSKext::loadExecutable()` links them live from `kernel_task` (`48ca4834c`), and `tools/image/hfs.py` + `tools/xnu-boot.sh` (`95762e5ec`, `ac748b4b8`, `91a1cc852`) attach a portable rootless MBR `0xAF` (`Apple_HFS`) HFSX disk (`root.img`) on AHCI port 1. XNU matches `BSD root: disk1s1`, mounts `/` (`hfs: mounted ravynOS on device b(14, 3)`), mounts `devfs` on `/dev`, finishes `bsd_init`, and reaches PID 1 `load_init_program("/sbin/launchd")` (`errno 2` until userland is built).
- **Open defects:** mtools `.img` output is not byte-reproducible (`auto` backend no longer selects mtools). Darwin prelinked kernelcache is skipped (`plktool` is Linux-only), bypassed cleanly via live `kxld` linking of `BOOTX64.EFI` `Driver-<addr>` kexts.
- **In progress — BSD userland:** Frameworks deferred from `bmake BSD` (needs ICU, not needed for single-user shell). Libedit built without `-lncurses` (termcap functions provided by libSystem). Missing SDK headers being stubbed (`sys/syslog.h`, `sys/time.h`, `launch.h`, `mach/_types.h` stubs, `sys/_types.h` compat). `vproc_priv.h` availability macros stubbed. Launchd MIG step fixed. Remaining: SDK header gaps (`machine/_types.h`, `sys/_visible.h`, `__darwin_*` types) block launchd and sh compilation.
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. **Clear remaining SDK header gaps** blocking launchd + sh compilation (`machine/_types.h`, `sys/_visible.h`, `__darwin_*` types, `TargetConditionals.h` compatibility with clang 17).
2. **`bmake BSD`** — build `/sbin/launchd` and `/bin/sh`.
3. **Stage userland and boot to a shell.** `tools/xnu-boot.sh` auto-copies `/sbin/launchd`, `/bin/sh`, `/usr/lib/dyld`, `libSystem.B.dylib` and `/usr/lib/system/*.dylib` into `$OUT/root.img`; boot with `boot-args=-s` for Apple single-user mode.

**Boot-spirit policy (agreed 2026-11-05):** the shell milestone is Apple's single-user mode (`boot-args=-s` → real `/sbin/launchd` → real `/bin/sh`); no static init / busybox substitutes. Deferred convergence: prelinked kernelcache, `boot-uuid` handoff, GPT root, real dyld shared cache. Full text in `Docs/DarwinBoot.md` §"Boot-spirit policy". Estimate: P50 ≈ 5 / P90 ≈ 8–9 sessions.

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
- 2026-11-05: Boot-spirit policy agreed — launchd-faithful track only (`boot-args=-s` single-user shell; no static init/busybox substitutes), spirit-convergence items deferred (kernelcache, `boot-uuid`, GPT, shared cache); upstream = ravynsoft `darwin` branch, same Darwin-base effort
- 2026-11-05: dyld split landed; libSystem bootstrap fixed (PATH quoting, `.elif` parse, CLT TBD seeding, config/recursion/dylib ordering) (`c1f73a0133`, `b946210d8d`, `d0ab37b85d`, `8cdd2abafa`, `4a7828d3a0`)
- 2026-11-04: Portable rootless MBR `0xAF` HFSX `root.img` (`tools/image/hfs.py`) mounts `/` (`hfs: mounted ravynOS`) + `devfs` and reaches PID 1 `load_init_program("/sbin/launchd")` (`95762e5ec`, `ac748b4b8`, `91a1cc852`)
- 2026-11-04: Live `kxld` linking of 30 booter kexts + `ACPIPlatformExpert` + PCI + `RavynAHCIPort` + `IOStorageFamily` -> `IOFindBSDRoot()`, 25s / 0 panics (`f5ff2c74b`, `d7504302e`, `48ca4834c`)
- 2026-11-04: Bare XNU reaches `StartIOKit()` / `IOPanicPlatform::start` with zero CPU faults after boot-window relocation + EFI runtime strip (`719fa5b2ba`, `52d36895fd`)
- 2026-11-03: D6 GATE PASSED — XNU prints `Darwin Kernel Version 19.6.0` on serial (`ba3dc57c1`)
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
