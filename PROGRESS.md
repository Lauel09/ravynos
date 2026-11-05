# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep this file under 600 words; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain, XNU + kexts, IOKitUser, libc++/compiler-rt, objc4, `libdyld.dylib`, and all Libsystem local objects. Libsystem recursion unblocked (`b24e956f50`, 2026-11-05): the ordering block had sat above the `SUBDIR` assignment so bmake iterated an empty list and registered zero `all_subdir_*` edges — verified by `bmake -n` plan and fixed by moving `SUBDIR` up; the dry-run plan now recurses all 20 members before the `libSystem.B.dylib` link. Upstream diff done: no fix to inherit (see `Docs/DarwinBoot.md` probe conclusion). Live gate: `--stage Libraries` run + then `bmake BSD` for `/sbin/launchd` (`Docs/DarwinBoot.md` 2026-11-05 entry).
- **Boot (D6 PASSED; `/` mounted via `hfs.kext` + `devfs` + PID 1 `load_init_program` reached):** `BOOTX64.EFI` stages 30 `.kext` bundles (`Driver-<addr>`) + `/ACPI/RSDP` (`f5ff2c74b`, `d7504302e`), `OSKext::loadExecutable()` links them live from `kernel_task` (`48ca4834c`), and `tools/image/hfs.py` + `tools/xnu-boot.sh` (`95762e5ec`, `ac748b4b8`, `91a1cc852`) attach a portable rootless MBR `0xAF` (`Apple_HFS`) HFSX disk (`root.img`) on AHCI port 1. XNU matches `BSD root: disk1s1`, mounts `/` (`hfs: mounted ravynOS on device b(14, 3)`), mounts `devfs` on `/dev`, finishes `bsd_init`, and reaches PID 1 `load_init_program("/sbin/launchd")` (`errno 2` until userland is built).
- **Open defects:** mtools `.img` output is not byte-reproducible (`auto` backend no longer selects mtools). Darwin prelinked kernelcache is skipped (`plktool` is Linux-only), bypassed cleanly via live `kxld` linking of `BOOTX64.EFI` `Driver-<addr>` kexts.
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. **First, before the Libsystem probe:** diff upstream's working userland against ours to harvest link fixes — upstream's `demo-vm` release (2026-07-25/08-12, discussion #565) boots real XNU + `dyld` + BSD `ash` under OVMF with an HFS `disk.img`, proving their linked `libSystem`/`_launch` stack functioned, and that branch is only ~103 commits behind `darwin`. Diff upstream `demo-vm` and `darwin` `Libraries/Libsystem` (+ `Libraries/Makefile`, `Libraries/dyld`) against ours on the SUBDIR recursion / `libSystem.B.dylib` link ordering that stops our build at `-llaunch`; inherit the fix instead of re-deriving it. (Context: the demo VM itself is hand-staged, shortcut-track — fake `launchd`, read-only fs, serial-only input, no `/System|/Library|/Applications`, no top-level `/sys` on real macOS — our Boot-spirit policy still governs our target.)
2. Diagnose the Libsystem SUBDIR recursion that only entered `copyfile` (19 other members never ran; `libSystem.B.dylib` link fails `-llaunch`), then `bmake Libraries` + `BSD` (`/sbin/launchd` + `/bin/sh`).
2. Stage built userland into `$OUT/root.img` (`tools/xnu-boot.sh` already auto-copies `/sbin/launchd`, `/bin/sh`, `/usr/lib/dyld`, `libSystem.B.dylib`, and `/usr/lib/system/*.dylib`) and boot ravynOS to a shell in QEMU.
3. **Boot-spirit policy (agreed 2026-11-05, full text in `Docs/DarwinBoot.md` §"Boot-spirit policy"):** the shell milestone is Apple's single-user mode (`boot-args=-s` → real `/sbin/launchd` → real `/bin/sh`); no static init / busybox substitutes. Deferred convergence: prelinked kernelcache, `boot-uuid` handoff, GPT root, real dyld shared cache. Estimate: P50 ≈ 5 / P90 ≈ 8–9 sessions.

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
