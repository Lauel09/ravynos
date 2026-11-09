# Progress
Live status only. History: Docs/PROGRESS-archive.md. Keep it concise; move superseded text to the archive.

## Goal
Build XNU + userland on the macOS host, and boot ravynOS to a shell in QEMU.

## Status
- **Build (macOS host):** `tools/macbuild.sh --audit --target-arch x86_64` builds the full toolchain, XNU + kexts, IOKitUser, libc++/compiler-rt, objc4, `libdyld.dylib`, and all Libsystem local objects. **`bmake Libraries` PASSED (2026-11-06, exit 0)** — all 24 libsystem dylibs, `libSystem.B.dylib`, `dyld`, zlib, with the `$DARWIN_EXTSN` variant symbols intact. Fixes this stage: SDK `os/assumes.h` + `os/base_private.h` regenerated from Libsystem; `__DARWIN_ALIAS_STARTING` passthrough restored in `sys/cdefs.h` (variant `$DARWIN_EXTSN` labels lost in the prior session's gutting → duplicate `_popen` etc. at umbrella link); libsyscall `mach/mach.h` staged into the SDK (declares `mach_msg_send`, `mach_task_self`).
- **Boot (D6 PASSED; `/` mounted via `hfs.kext` + `devfs` + PID 1 `load_init_program` reached):** `BOOTX64.EFI` stages 30 `.kext` bundles (`Driver-<addr>`) + `/ACPI/RSDP` (`f5ff2c74b`, `d7504302e`), `OSKext::loadExecutable()` links them live from `kernel_task` (`48ca4834c`), and `tools/image/hfs.py` + `tools/xnu-boot.sh` (`95762e5ec`, `ac748b4b8`, `91a1cc852`) attach a portable rootless MBR `0xAF` (`Apple_HFS`) HFSX disk (`root.img`) on AHCI port 1. XNU matches `BSD root: disk1s1`, mounts `/` (`hfs: mounted ravynOS on device b(14, 3)`), mounts `devfs` on `/dev`, finishes `bsd_init`, and reaches PID 1 `load_init_program("/sbin/launchd")`. **D7 PASSED 2026-11-09:** launchd[1] starts in single-user mode and execs the real `/bin/sh` prompt on the serial console (`Docs/DarwinBoot.md` run-log).
- **Open defects:** `_ccrng` stub returns zeros, not real randomness; libsystem_c `_ccrng_uniform` always returns 0 — both need a real RNG after D7. mtools `.img` output is not byte-reproducible (`auto` backend no longer selects mtools). Darwin prelinked kernelcache is skipped (`plktool` is Linux-only), bypassed cleanly via live `kxld` linking of `BOOTX64.EFI` `Driver-<addr>` kexts.
- **BSD userland complete:** `bmake BSD` exit 0 — `/sbin/launchd` and `/bin/sh` are x86_64 Mach-O in `../build/sysroot`, alongside `/usr/lib/dyld`, `libSystem.B.dylib`, and `/usr/lib/system/*.dylib`. Fixes this stage: launchd makefile (drop `-target` conflict guard → mount_hfs `--sysroot`+`-mmacos-version-min` pattern, add `-lSystem -undefined dynamic_lookup`, add `to-sysroot` install target); `BSD/include/apple/sys/resource.h` (remove 7 duplicate types that redefined via `#include_next`); SDK header additions (`bsm/audit.h` `au_evname_map_t`+`A_GETEVENT`/`A_SETEVENT`, `sys/syslog.h` `LOG_CONSOLE`/`vsyslog`/`LOG_MASK`/`LOG_UPTO`, `sys/reboot.h` `RB_POWEROFF`/`RB_PAUSE`, `sys/endian.h` `beXXtoh`, `bsm/audit_errno.h` `BSM_ERRNO_*`); launchd source (`core.c` `(bool)` casts on bit-field args to `job_assumes`, `memcmp` for `uuid_compare`, `#include <uuid/uuid.h>`, remove manual `boolean_t` typedef in `log.c`); `BSD/old-launchd/liblaunch/launch.h` (add missing `LAUNCH_KEY_*` keys).
- The OVMF `#UD 0xB0000` "blocker" did not reproduce locally (2026-11-03): ISO, GPT/ESP and bare FAT16 all boot to `RAVYNOS-ENTRY`.

## Next steps
1. **D7 PASSED (2026-11-09).** `tools/xnu-boot.sh --boot-args -s` boots XNU
   with `Darwin Kernel Version 19.6.0`, `BSD root: disk1s1`,
   `hfs: mounted ravynOS`, `devfs` on `/dev`, `load_init_program("/sbin/launchd")`,
   `launchd[1] has started up in single-user mode`, then the real `/bin/sh`
   `#` prompt on the serial console. Five boots, one cause per boot; commits
   `b9bbed58c7`, `f7fb73723d`, `9c8fde74b4`, `ae36f65eae`, `f4d468b0e6`.
   Files touched: `Libraries/dyld/src/dyldAPIsInLibSystem.cpp`,
   `BSD/sbin/launchd/runtime.c`, `Libraries/Libsystem/libsystem_notify/Makefile`,
   `Libraries/Libsystem/libsystem_asl/Makefile`,
   `Libraries/Libsystem/libxpc/xpc_pipe.c`, `BSD/sbin/launchd/launchd.c`,
   `BSD/sbin/launchd/init/init.c`, `tools/xnu-boot.sh`. Verdict and serial
   lines: `Docs/DarwinBoot.md` run-log `2026-11-09 — D7 attempt 2`.
2. **Real entropy:** replace the zero-filling `_ccrng` stub in libdyld and
   launchd with a real RNG, and make `libsystem_c` `_ccrng_uniform` return a
   value (currently always 0). Both are open defects pre-dating D7.
3. **Root tooling / convergence:** `/bin/launchctl` still cannot build (missing
   libmach, libosxsupport, libjansson, BlocksRuntime); after that, restore the
   Apple launchctl single-user bootstrap and stage `/sbin/fsck`, `/sbin/mount`
   so the shell can `mount -uw /`. Then resume deferred D7 convergence
   (kernelcache, `boot-uuid`, GPT root, shared cache).

**Boot-spirit policy (agreed 2026-11-05):** the shell milestone is Apple's single-user mode (`boot-args=-s` → real `/sbin/launchd` → real `/bin/sh`); no static init / busybox substitutes. Deferred convergence: prelinked kernelcache, `boot-uuid` handoff, GPT root, real dyld shared cache. Full text in `Docs/DarwinBoot.md` §"Boot-spirit policy". Estimate: P50 ≈ 5 / P90 ≈ 8–9 sessions.

**Compatibility policy (agreed 2026-11-09, permanent):** Apple's observable behavior is the spec; match its ABI exactly, use Apple source where open, write our own where closed; no fake success; prove with tests against real macOS. Target: x86_64 PCs, ABI baseline macOS 10.15. Full text in `Docs/DarwinBoot.md` §"Compatibility policy".

## Key commands
- Remote runner: `tools/arena-run.sh <branch> <task> [<task> ...]`
- Build: `tools/macbuild.sh --audit --target-arch x86_64` — logs `work/logs/world-N.log`
- Loader: `tools/bootloader/build.sh -q`
- Local XNU boot: `tools/xnu-boot.sh` (logs `out/arena/boot/serial.log`, `qemu-int.log`)
- Boot verdict: `tools/bootcheck.sh --uefi` (CI-style), `tools/darwin-gate.sh` (stub/XNU gate)
- Structural verify: `tools/image/verify.py <iso|img> …`

## Pointers
- `Docs/PostD7Plan.md` — milestones D8–D13, symbol audit results (`tools/symaudit.py`), risk register
- `Docs/DarwinBoot.md` — boot plan (D1–D6 complete); §"Post-D6 handoff status (2026-11-04)" has the fault table and next fix
- `Docs/macOS-Host.md` — macOS prerequisites, build loop, failure classes, portability audit (§5)
- `Docs/BUILDING.md` — entry points (`bmake world`), Homebrew prereqs, target vs host arch
- `Docs/Pipeline.md` — portable image pipeline: design, commands (§3), reproducibility, CI
- `Docs/BootLoader.md` / `Docs/Shell.md` — loader decision + staged plan; kernel console and EFI shell
- `Docs/PROGRESS-archive.md` — full history and changelog (everything removed from this file)

## Recent changes
- 2026-11-09: D7 GATE PASSED — launchd[1] single-user mode → real `/bin/sh` prompt on serial. Fixes: ccrng corecrypto ABI; `-llaunch` before `-lxpc` in notify/asl; libxpc MIG `xpc_pipe_try_receive`; launchd `init_pre_kevent` console path; stage `libedit.dylib` (`b9bbed58c7`, `f7fb73723d`, `9c8fde74b4`, `ae36f65eae`, `f4d468b0e6`)
- 2026-11-06: `bmake Libraries` PASSED (exit 0; all 24 libsystem dylibs + dyld + zlib, `$DARWIN_EXTSN` variants intact). Fixes: SDK `os/assumes.h`/`os/base_private.h` staged from Libsystem; `__DARWIN_ALIAS_STARTING` passthrough restored in `sys/cdefs.h` (Developer SDK + kernel copy); libsyscall `mach/mach.h` staged into SDK. Loader: optional ESP `\boot-args` file + `tools/xnu-boot.sh --boot-args` for the single-user milestone
- 2026-11-05: Boot-spirit policy agreed — launchd-faithful track only (`boot-args=-s` single-user shell; no static init/busybox substitutes), spirit-convergence items deferred (kernelcache, `boot-uuid`, GPT, shared cache); upstream = ravynsoft `darwin` branch, same Darwin-base effort
- 2026-11-05: dyld split landed; libSystem bootstrap fixed (PATH quoting, `.elif` parse, CLT TBD seeding, config/recursion/dylib ordering) (`c1f73a0133`, `b946210d8d`, `d0ab37b85d`, `8cdd2abafa`, `4a7828d3a0`)
- 2026-11-04: Portable rootless MBR `0xAF` HFSX `root.img` (`tools/image/hfs.py`) mounts `/` (`hfs: mounted ravynOS`) + `devfs` and reaches PID 1 `load_init_program("/sbin/launchd")` (`95762e5ec`, `ac748b4b8`, `91a1cc852`)
- 2026-11-04: Live `kxld` linking of 30 booter kexts + `ACPIPlatformExpert` + PCI + `RavynAHCIPort` + `IOStorageFamily` -> `IOFindBSDRoot()`, 25s / 0 panics (`f5ff2c74b`, `d7504302e`, `48ca4834c`)
- 2026-11-04: Bare XNU reaches `StartIOKit()` / `IOPanicPlatform::start` with zero CPU faults after boot-window relocation + EFI runtime strip (`719fa5b2ba`, `52d36895fd`)
- 2026-11-03: D6 GATE PASSED — XNU prints `Darwin Kernel Version 19.6.0` on serial (`ba3dc57c1`)
- 2026-11-03: Phase 1 world reaches and passes XNU + userland link graph; libdyld `-lsystem_malloc` fixed (`76918cbcec`)
