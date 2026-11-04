# RavynOS Fork — Progress Log

> **⚠ ACTIVE PLAN (2026-10-31): `Docs/DarwinBoot.md` — goal: XNU prints
> `Darwin Kernel Version` via our loader. Work only its milestones D1→D6, log
> there. Everything below is background; the OVMF `#UD 0xB0000` "blocker" does
> not reproduce locally and is deprioritised.**

**Last updated:** 2026-11-03 (rev 36 — the `#UD 0xB0000` blocker does not reproduce locally with current media; two real loader/image defects fixed and boot-verified)
**Workspace:** `/path/to/test-ravyn-os`
**Mission:** Improve the RavynOS codebase from our own fork, developed macOS-first on this MacBook (Apple Silicon, macOS 26.3.1), with a **portable** pipeline (macOS + Linux) from source → bootable IMG/ISO → automated boot verdict, and need-driven reverse engineering with [rea](https://github.com/morluto/rea) on this machine when a gap justifies it.

> **Status (rev 17):** the *chain* exists and is verified everywhere except
> the one place it matters — firmware. `tools/bootloader` (Step B/C/D), the
> kernel console (`Kernel/ravynkern`) and an independent EFI shell
> (`tools/shell`) all build, are checked as PE/ELF against the loader's own
> contract, and are re-verified *inside* the finished ISO and ESP rather than
> at the source. CI boots three media (ISO, GPT/ESP, bare FAT16) and then types
> `help`/`ver`/`mem`/`exit` into the prompt over a serial socket.
> **What still fails:** OVMF never enters the loader. Every boot ends at
> `X64 Exception Type - 06 (#UD)`, `RIP 0xB0000`, `!!!! Can't find image
> information !!!!` — a garbage jump in firmware, not our code — so no boot
> has yet printed one of our markers. Phase 1's toolchain milestone (`bmake
> Developer` exits 0 on macOS) is done and Phase 2 is complete except real
> image *content*; Phase 0 complete (fork published at
> https://github.com/Lauel09/ravynos).
---

## 1. Repository state

| Item | Status |
|---|---|
| Source | `ravynos/` — clone of `ravynsoft/ravynos`, based on `darwin` branch HEAD `60aa4ff4b` "OpenGL and CoreText frameworks" (2026-09-06) |
| History | ✅ **Full history fetched** (was depth-1): 745 commits, upstream tags (`SDK_0.7.0.x`, `linux_tc_0.2`, `kernel`, `demo-vm`), `.git` ≈ 1.5 GB |
| Local branch | ✅ `fork/darwin-dev` created off `darwin`; baseline tag `baseline-60aa4ff4b` |
| Fork remote | ✅ **Published** at https://github.com/Lauel09/ravynos (parent `ravynsoft/ravynos`); default branch `fork/darwin-dev`; description and topics set |
| Version in tree | `PROD_VERSION 0.7.1`, `PROD_FAMILY Pre Alpha` (top-level `Makefile`) |
| CI in repo | ✅ `.github/workflows/pipeline.yml` — stub self-test on Ubuntu + macOS, both green |

The `darwin` branch is the "new generation" RavynOS: an XNU-kernel, Darwin-layout system (replacing the older FreeBSD/GNUstep approach whose scripts and docs still linger in `tools/`, `ISO/`, `Docs/__OLD__`).

### Codebase layout (verified)

| Path | Contents |
|---|---|
| `Developer/` | Self-hosted toolchain sources: `Default.xctoolchain/` (LLVM/clang, cctools, xcbuild, mig, xar…), `ravynOS.platform/`, `ravynOS.sdk/` |
| `Kernel/` | `xnu/` kernel, `Extensions/` (kexts), `IOKitUser/`, `kext_tools/` |
| `Frameworks/` | AppKit, CoreFoundation, CoreGraphics, CoreText, OpenGL, QuartzCore, Foundation, Security, … |
| `Libraries/` | System libraries incl. `libfirehose_kernel` |
| `BSD/` | FreeBSD-derived userland (bin, sbin, lib, usr.bin, `share/mk` BSD make framework) |
| `ISO/` | Live-CD/ISO assembly — **FreeBSD-host-only today**; replaced by our portable pipeline (§4) |
| `SystemLibrary/`, `SysApps/`, `CoreServices/`, `PrivateFrameworks/` | System resources and apps |
| `tools/` | Portable image pipeline, boot smoke tests, publish helper |

## 2. Build system — current reality

- Entry point is a **BSD-make-only top-level `Makefile`**: builds a host toolchain (LLVM etc.) first, then `world` = `version Developer Kernel Libraries/libfirehose_kernel Libraries BSD`.
- **Target arch is now explicit and host-independent:** `TARGET_ARCH ?= x86_64` (upstream-supported, QEMU-bootable) maps to `BuildArch=X86`, `CpuArch=x86_64`, `ARCH_CONFIGS=X86_64`. `bmake TARGET_ARCH=arm64 world` selects the AArch64 experiment. This removes the old host-`MACHINE` auto-selection that produced an AArch64 toolchain on this arm64 Mac while `ARCH_CONFIGS` stayed X86_64.
- `Docs/BUILDING.md` was stale (told macOS users to run `cmake -S ravynos`); **fixed** to document the real `bmake world` entry point, Homebrew prerequisites, target-vs-host arch and the portable pipeline. CMake/Ninja remain internal to the toolchain build (LLVM at `Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt`).
- `tools/build.sh` is a legacy zsh wrapper for the **old FreeBSD-based build** — historical reference only.
- **Threading:** LLVM stage now uses `gmake -j` with all cores (or an explicit top-level `-j`). Remaining stages are still serial; revisit if they become the bottleneck. User explicitly wants multi-threaded compilation throughout.

## 3. macOS host status (this MacBook)

### 3.1 Toolchain prerequisites — ✅ all installed via Homebrew (2026-10-30)

| Tool | Status |
|---|---|
| Apple clang 17.0.0 (Xcode) | ✅ matches `BUILDING.md` requirement |
| **bmake** (BSD make) | ✅ `brew install bmake` |
| **cmake 4.4.5 / ninja 1.13.2** | ✅ |
| **gmake 4.4.1** | ✅ via `brew install make` (Homebrew formula is `make`; binary is `gmake` — `gmake` is *not* a formula name on macOS) |
| **bison / flex** | ✅ yacc/lex shims fall back to these |
| **xorriso 1.5.8 / mtools 4.0.49** | ✅ for the portable image pipeline |
| **nasm** | ✅ assembles the loader-only boot stub |
| **QEMU 11.1.3** (`qemu-system-x86_64`) | ✅ for boot smoke tests |
| Python 3.9.6 (system) | present; swap to brew python if xcbuild rejects it |
| **gh** | ✅ authenticated as `Lauel09` |

### 3.2 Phase 1 build loop on macOS (2026-10-30)

`bmake -n version` parses cleanly (GNU make fails with `missing separator`). Real
`bmake world` / `bmake Developer` attempts are logged at the workspace root
(`build-world-attempt*.log`, `build-developer-attempt*.log`).

| Attempt | Failure | Fix (in working tree unless noted) |
|---|---|---|
| 1 | `chown root:wheel` on installed gmake fails as non-root | committed `823f5d04a7` |
| 2 | same chown in `mig` (and `unifdef`) | committed `f93f763a71` |
| 3 | `yacc`/`bison` Xcode shim refuses CLT-only host | committed `213846a15c` |
| 4 | toolchain shim `TargetConditionals.h` rejects arm64 clang | committed `98e004a59a` |
| 5 | `xar`: `openssl/evp.h` not found (SDK has no OpenSSL headers) | committed `aecec6615e` + `c01b63e8c1` |
| 6 | libctf's bundled `qsort_r` conflicts with Darwin's declaration | committed `d4577f2cd8` |
| 7 | CMake rejects `-fno-common` split out of unquoted `-DCMAKE_C_FLAGS` | committed `82101f5078` |
| 8 | LLVM host build picks ravynOS shim Mach headers and loses `task_get_exception_ports` | committed `77228f03a7` (first version; later refined) |
| 9 | LLVM configured AArch64-only; target changed to x86_64 | committed `afbce1ba10` |
| 10 | clean x86_64 toolchain rebuild; reached cctools | failed at `cctools/misc/checksyms.c` (`stuff/bool.h`) after the global shim include was removed |
| 11 | restored shim include globally; LLVM then picked it up again (`task_get_exception_ports`) | fixed in working tree by filtering the include from LLVM flags only |
| 12 | LLVM shim filtering did not match the absolute include path (`:S` typo) | fixed in working tree with a wildcard exclusion (`:N`) |
| 13 | reached `cctools/ld64`; failed because it expects `libtapi.so` but LLVM now emits `libtapi.dylib` | fixed in working tree: `llvm/Makefile` and `cctools/ld64/src/Makefile` use `.dylib` |
| 14 | `cctools/ld64` — `CPU_SUBTYPE_X86_ALL` undeclared | fixed (use `CPU_SUBTYPE_X86_64_ALL`) |
| 15 | `cctools/ld64` — `reallocf` undeclared in `ResponseFiles.cpp` | **in progress** (replace with `realloc` + free-on-failure) |
| 16 | **stopped at user request** — resume with `bmake Developer` | — |

Uncommitted working-tree changes at time of writing:
- `Developer/Default.xctoolchain/cctools/ld64/src/Makefile` — `libtapi.so` → `libtapi.dylib`
- `Developer/Default.xctoolchain/cctools/ld64/src/ld/Options.cpp` — `CPU_SUBTYPE_X86_ALL` → `CPU_SUBTYPE_X86_64_ALL`
- `Developer/Default.xctoolchain/cctools/ld64/src/ld/ResponseFiles.cpp` — `reallocf` → `realloc` (+ free on failure)
- `Developer/Default.xctoolchain/llvm/Makefile` — wildcard exclusion of the shim include; parallel `gmake -j`

The arch conflict is resolved in the top-level Makefile: `TARGET_ARCH ?= x86_64`
is host-independent; on this arm64 Mac it cross-builds the supported x86_64
target. `bmake TARGET_ARCH=arm64 world` remains the opt-in experiment.

Preliminary finding from the dry run: `xar` relies on build ordering for
`libstuff.a` rather than declaring it. The real build creates it first, so no
change was needed; revisit if a parallel build exposes a race.

### 3.3 Audit-driven portability fixes (rev 12)

`tools/macbuild-audit.py` (see `Docs/macOS-Host.md` §5) found the following
cross-host hazards without running a build. Both were fixed additively; neither
changes behaviour on FreeBSD/Linux where the original spelling worked.

| Where | Hazard | Fix |
|---|---|---|
| `BSD/share/mk/bsd.crunchgen.mk` | `sed -i ''` (BSD syntax) fails under GNU sed | rewrite through `${OUTMK}.tmp` with a plain `sed -e` + `mv` |
| `BSD/usr.bin/man/man.sh` | `stat -f %i` (BSD) fails under GNU coreutils in the hardlink self-test | POSIX `ls -i | awk` |

Deliberate, annotated exceptions (`# audit:allow`) remain in `GNU/bin/dmidecode/Makefile`
(host detection for a GNU-make `?=` default) and `tools/lib/common.sh` (host
description, never target selection). The audit is clean at `--severity medium`
over the whole tree.

## 4. Bootable image pipeline status — **Phase 2, complete except content**

`ISO/build.sh` remains FreeBSD-host-only and root-requiring; it is retired for
our purposes and replaced by the portable pipeline. Substitution table and
design: `ROADMAP.md` Phase 2; reference documentation: `Docs/Pipeline.md`.

**Delivered (rev 11):**

| Piece | Status |
|---|---|
| `tools/mkimage.sh` | stub / milestone-stub / **`--root` real staging** modes; `--format iso|img|both`; `--backend auto|xorriso|python|mtools`; manifest, `.sha256`, `.build.json`; refuses root; `--print-plan`, `--verify` |
| `tools/image/iso9660.py` | complete ISO 9660 + El Torito writer (PVD, both path tables, level 1/2 identifiers, BIOS *and* UEFI catalog entries), no dependencies |
| `tools/image/fat.py` | FAT16 + primary/backup GPT writer with VFAT long names, deterministic GUIDs, auto cluster sizing |
| `tools/image/bootstub.py` | hand-assembled 16-bit stub generator (COM1 marker, multi-marker support) so the QEMU path needs no nasm |
| `tools/image/verify.py` | independent structural verifier: ISO descriptors/tree/El Torito checksum, GPT CRCs, FAT chains — milliseconds, no QEMU |
| `tools/bootcheck.sh` | multi-marker milestones, KVM/HVF/TCG auto-select, OVMF/`.img`, `--kernel` direct boot, screenshot, JSON verdict, logs preserved |
| `tools/pipeline-selftest.sh` | build → verify → reproducibility → content-change → two QEMU boots; `--no-qemu` for a fast CI gate |
| `tools/tests/` | 30 unit tests + CLI contract + shell-syntax layer; no host tools required |

**Validation — what a machine with QEMU actually did (rev 13):**

The pipeline was written but never run on a host that could boot anything.
It has now been run by GitHub runners, and the boot path has a verdict.

| Fact | Evidence | Host |
|---|---|---|
| Stub ISO boots, marker on COM1 | `RAVYNOS-BOOTCHECK-STUB-OK` in `serial.log` | ubuntu-latest, x64, TCG |
| Same stub ISO boots | identical marker | macos-latest, arm64, TCG |
| Four-stage milestone chain | `RAVYNOS-LOADER-OK`, `-KERNEL-OK`, `-ROOT-MOUNT-OK`, `-USERSPACE-OK` | both hosts |
| xorriso ISO is reproducible | sha256 `8aaf4d89…` from two builds 2 s apart | ubuntu xorriso 1.5.6 |
| xorriso ISO reprod. on macOS | job green with xorriso installed | macos xorriso 1.5.8.pl02 |
| Python ISO/IMG reproducible | sha256 `b7b0bccc…` twice | ubuntu |
| HVF is unusable for an x86 guest on Apple Silicon | `qemu-system-x86_64: -accel hvf: invalid accelerator hvf`, then TCG boots | macos-latest |
| **Not** reproducible: mtools `.img` | two builds differ at byte 17825745 | ubuntu |

Independent cross-check (rev 13, run on Debian 12 x86_64 with pycdlib and
pyfatfs from PyPI, neither used by the writers): pycdlib parses the generated
ISO and reports boot_indicator `0x88`, boot media `0` (no emulation), load RBA
26, 4 sectors — byte-for-byte the same facts `verify.py` reports for the same
file — and reads `BOOT/BOOTSTUB.BIN` (2048) and `README.TXT` (105) back;
pyfatfs mounts the generated GPT/FAT16 image at offset 1 MiB and reads
`/EFI/BOOT/BOOTX64.EFI` and `/README.TXT`. So the writers agree with
independent implementations, not only with our own verifier.

The markers come from a hand-written 16-bit stub (`tools/bootstub/`), **not**
from XNU: this proves the ISO → El Torito → QEMU → serial → verdict loop, not
an operating system.  No boot has used KVM (GitHub runners do not expose
`/dev/kvm`), the UEFI/ESP path has never been booted, and `-kernel` has never
been given a real kernel.

Getting there took three CI rounds and nine defects, all of them upstream of
QEMU — which is why the previous "pipeline complete" claim was wrong:

1. `$(case … esac)` fails to parse under macOS's bash-3.2 `/bin/sh` (while
   `sh -n` accepts it): every macOS job died at line 148.
2. `mcopy … . ::` → `Cannot create entry named . or ..`: the mtools backend
   never worked.
3. `fat.py wrap` read `args.timestamp`, which only `build` defines:
   `AttributeError` on every call, so `--backend mtools` could never produce
   an image.
4. `brew install` with an empty package list failed the reproducibility job
   during setup.
5. `mkimage --root` passed `-b ""` to xorriso: `--root` failed on any host
   with xorriso, i.e. every runner.
6. Staged mtimes leaked into the image, so "reproducible" held only when two
   builds landed in the same second.
7. The reproducibility job installed no image tools, so it only ever tested
   the Python fallback.
8. Acceleration was chosen from `-accel help` (what the binary was built
   with), not from what the host can run.
9. The boot job's verdict step echoed to a stdout nobody could read, and the
   run logs themselves are served from a host some clients cannot reach.

The last two are why the failures above took three rounds to see: GitHub
serves run logs from a signed-URL blob host, so `gh run view --log` fails
where `api.github.com` works. `tools/ci-log-export.sh` now republishes each
job's log as a commit comment through `if: failure()` — that is the channel
these findings came from, and the boot evidence is republished on success too.

**Loader (rev 15):** `Docs/BootLoader.md` records the decision (UEFI
application is primary; the ISO carries a BIOS slot *and* a UEFI catalog entry)
and a five-step plan.  Step B's source is `tools/bootloader/uefi_stub.c` — a
freestanding PE32+ application with the UEFI tables declared inline (no gnu-efi,
no EDK2) that prints `RAVYNOS-LOADER-OK` over the serial protocol with a
`ConOut` mirror.

It now *builds* and is *checked*, which it never was before:

| Piece | State |
|---|---|
| `tools/bootloader/build.sh` | clang+lld-link, or a zig binary as fallback; validates its own output |
| `tools/bootloader/Makefile` | rewritten to run under GNU make **and** bmake (it used bmake-only `.TARGET`/`.ALLSRC`, so the documented `make` had never worked on Linux) |
| entry symbol | `EfiMain` — lld-link's default, and the only option zig's linker accepts |
| `tools/image/pe.py` | new validator: PE32+ magic, machine, EFI subsystem, entry inside an executable section, section bounds |
| `mkimage --efi-image` | refuses a payload that is not a PE32+ EFI application |
| `--efi-load-size` | **`auto`** (was 1 sector): El Torito's sector count is what firmware loads, so the old default announced a 3.5 KiB loader as 512 bytes and would have booted a truncated image |
| `verify.py --verify` | re-checks the payload inside the finished ISO (El Torito entry) and ESP (FAT), including that the sector count covers the file |
| `bootcheck --uefi` | handles split `OVMF_CODE`/`OVMF_VARS` firmware, copying the variable store |
| CI job `uefi` | clang+lld-link build → PE check → ISO+ESP → OVMF boots both, marker required |

Local proof available without QEMU: `tools/bootloader/build.sh` produced a
3584-byte PE32+ EFI application here (via the zig fallback), `mkimage --root
--format both --efi-image` emitted an ISO whose catalog announces 7 sectors for
that 3584-byte payload, and both artifacts pass the extended verifier.  The
boot itself is the CI job's verdict — this host has no QEMU and no Debian
mirror.

**Rev 17 — the chain, and the one thing still wrong with it.** Since rev 15
the loader stopped being a skeleton: it finds a kernel on the boot volume
(`\EFI\KERNEL.ELF`, `\BOOT\KERNEL`, …), copies every `PT_LOAD` to its link
address inside the 1 MiB–4 GiB identity map, reserves a 64 KiB boot stack,
writes the `RVNK` bootinfo block at `0x90000`, runs `ExitBootServices` and jumps
with `rdi = &bootinfo`. The kernel brings up the 16550 itself, prints
`RAVYNOS-KSHELL-OK` and reads commands (`help ver mem cpus echo reboot halt`);
`tools/shell` is a second EFI application that reaches the same console without
the loader. `tools/image/verify.py` now parses the kernel *inside* the finished
ISO payload and ESP, and `bootcheck --send` types a script into `COM1`, so "the
user can run commands" is a machine-checkable claim. `Docs/Shell.md` is new.

Three defects were fixed by inspection because nothing could reach them yet
(rev 17 changelog): `EFI_SERIAL_IO_PROTOCOL` was missing its `Revision`
member, so every method pointer was one field out of place; the kernel range
was reserved with a non-page-aligned address, which `AllocateAddress` rejects;
and the bootinfo page was written without being reserved.

**Remaining for the M2 exit criterion:** a real staged sysroot from the Phase 1
build (`mkimage.sh --root` has only ever been fed toy trees), and firmware that
actually starts our image — see §6.

**CI:** `.github/workflows/pipeline.yml` (tests, per-backend structure,
reproducibility, QEMU boots on Ubuntu + macOS), `build.yml`
(`bmake world` → `mkimage` → `bootcheck`, toolchain cache keyed on
`Developer/**`), `nightly.yml` (from scratch, files an issue on failure), and
`.github/actions/install-host-tools` for the shared matrix setup.

## 5. Reverse-engineering workspace (`work/`)

| Asset | Status |
|---|---|
| `rea` CLI | ✅ v6.7.0 (Homebrew) |
| Ghidra provider | ✅ **wired & healthy (2026-10-30)**: `GHIDRA_INSTALL_DIR=/path/to/test-ravyn-os/work/ghidra_12.1.4_PUBLIC` exported in `~/.zshrc`; `rea doctor` reports `ghidra`, `ghidra-platform`, `ghidra-architecture` all healthy. Homebrew's Ghidra 12.2 left installed (harmless — env var takes precedence). Remaining doctor warnings are optional engines (Hopper/IDA/JADX/Kuna) and other clients' registrations — not needed |
| `ghidra_12.1.4.zip` | ✅ deleted after verification (reclaimed ~570 MB) |
| Doctrine | Smallest subject that answers the question; need-driven targets only. See `work/README.md` |

`work/` now exists in-tree with the doctrine, a template investigation
(`work/_template/`: `NOTES.md`, a buildable probe, a provenance file) and the
evidence conventions. No investigation is open; the first is nominated by the
first real gap Phases 1–2 produce. See `work/README.md`.

## 6. Current blocker / work list (ordered)

1. **[Phase 1, in flight]** `bmake world` on the macOS host now builds the
   full toolchain, **XNU and every kernel extension**, `IOKitUser`,
   `libc++`/`libc++abi`, compiler-rt and objc4; the current stop is the
   `Libraries/dyld` bootstrap (rev 38, `libdyld.dylib` then the `dyld`
   executable). The Linux runner still needs its own observed attempt. Until
   the remaining userland links, no claim is made about the BSD stage.

0. **[Phase 1, next]** `bmake world` on a macOS runner with the cached toolchain,
   then a real staging root through `mkimage.sh --root` — the first time the
   staging mode will see anything but a two-file toy tree.  This is the next
   real signal; everything downstream of it is still stub-fed.
2. **[open defect]** mtools `.img` output is not byte-reproducible (two builds
   differ 48 bytes from the end of the FAT volume).  The `auto` backend no
   longer selects mtools, and the reproducibility job now prints the byte
   offsets and a hexdump window around the first difference so the cause can
   be identified without another guessing round.
3. **[BLOCKER, Phase 2]** OVMF never starts `BOOTX64.EFI`. Three media (El
   Torito ISO, GPT/ESP, bare FAT16) all end at `#UD` with `RIP 0xB0000` and
   `!!!! Can't find image information !!!!`, i.e. the CPU is in `vga-lowmem`
   (`0xA0000-0xBFFFF`), not in any loaded image — a garbage branch inside
   firmware, before anything of ours runs. BDS's name for the boot option
   ("UEFI Non-Block Boot Device" / "UEFI QEMU DVD-ROM") says it has a device
   path but no file node. Next probes, all now instrumented: the loader prints
   `RAVYNOS-ENTRY` as the very first instruction-level action (so silence
   proves the image was never entered), `bootcheck --postmortem` dumps the
   guest's registers plus the owner of `RIP` from QEMU's monitor, the CI job
   records the firmware build/hashes and boots the same bare volume twice
   (q35/virtio and pc/ide), and both the ISO and GPT boots are now attached to
   the exported log.
4. **[Phase 1]** SDK → kernel → userland. Keep every patch `uname`/`.MAKE.OS`-guarded and log failure→fix pairs here.
5. Loader Step D for the *real* kernel: the ELF path (Step C) works, but XNU
   is Mach-O, so `boot_args` handoff and a Mach-O image loader are the next
   code after the boot blocker above. The 64-bit Mach-O path needs cctools,
   which no host here has yet.

---

## Changelog

- **2026-11-03 (rev 39) — Gate D6 PASSED: built XNU boots via `BOOTX64.EFI` and prints `Darwin Kernel Version 19.6.0` on serial.**
  Populating a 64-byte nonzero `/chosen/random-seed` in `tools/bootloader/uefi_stub.c`
  (`fill_random_seed()`, CPUID-checked `RDRAND` with SplitMix64-mixed `RDTSC`
  fallback for `-cpu Penryn`, commit `ba3dc57c1`, covered by
  `tools/tests/test_device_tree.py`) cleared the `prng_random.c:65` early panic
  (previously misread as post-fault DT wiping because `PE_get_random_seed()`
  intentionally scrubs `/chosen/random-seed` to zero in place at
  `pexpert/gen/pe_gen.c:165`). Under `tools/xnu-boot.sh`, XNU now completes
  `early_random()`, `Idle_PTs_init()`, `cpu_desc_load()`, `tsc_init()`, and
  VM/IPC/PMAP bootstrap, printing `Darwin Kernel Version 19.6.0` and reaching
  `Initializing EFI runtime services` (`osfmk/i386/AT386/model_dep.c:503`).
  Next fault: `#GP` (`v=0d`, `RIP=0xae557bfa39129d48`, `RSP=0xffffff8000194e40`)
  when `efi_init()` (`model_dep.c:529-545`) maps raw `EFI_MEMORY_RUNTIME`
  descriptors (`VirtualStart == 0`) at `0xffffff8000000000`, overwriting
  `__HIB` and the active boot stack. Superseded from `PROGRESS.md`: the three
  pre-D6 `PE_get_random_seed`/GS-base probe steps and the oldest `Recent changes`
  bullet (`44c2437550`, already logged in rev 36 below).

- **2026-11-03 (rev 38) — Phase 1 reaches (and passes) XNU, then the userland link graph.**
  Continued `tools/macbuild.sh --audit --target-arch x86_64` through
  `work/logs/world-{14..17}.log`. Four more failure → fix pairs:
  1. `libCrashReporterClient.dylib` had duplicate `CRClient.o` members and
     objc4 referenced C++-mangled `__Z20CRGetCrashLogMessagev`: the SDK header
     lacked `__BEGIN_DECLS` and the C++ TU duplicated the C implementation →
     add C linkage and build only `CRClient.c` with a local `CRC_OBJS`
     (`0b3d7f8fce`).
  2. `libcxx_dylib` then still saw the stale C++-mangled reference because
     CMake missed the `__has_include(<CrashReporterClient.h>)` dependency
     behind `--sysroot` → enable `CMAKE_DEPENDS_USE_COMPILER`
     (`55bb508aba`).
  3. `libobjc.A.dylib` referenced the now-correct C symbols but linked before
     `Libsystem`, and the seeded `libSystem` TBD has no CR exports:
     `Undefined symbols ... _CRGet/CRSetCrashLogMessage` → link the static
     `libCrashReporterClient.a` by path (`7098d71464`).
  4. `libdyld.dylib` linked unbuilt individual `-lsystem_malloc` et al.:
     `ld: library not found for -lsystem_malloc` → use the existing upward
     links, seed the CLT TBDs for `libsystem_*`/dispatch/xpc, and link the
     static CrashReporterClient (`76918cbcec`).
  The macOS host now builds the entire toolchain (LLVM, cctools, xcbuild,
  iig), XNU and all kernel extensions, `IOKitUser`, `libc++`/`libc++abi`,
  compiler-rt and objc4. The last full log, `work/logs/world-17.log`, stops
  at `Libraries/dyld` target `libdyld.dylib` with
  `ld: library not found for -lsystem_malloc`; that fix is committed and a
  targeted `libdyld.dylib` build now succeeds. The next targeted step exposes
  the `Libraries/dyld/dyld` executable failing on `ld: library not found
  for -lc`, not yet confirmed by a full world run.

- **2026-11-03 (rev 37) — Phase 1 `bmake world` reaches the userland libraries.**
  Continued the same documented command, `tools/macbuild.sh --audit
  --target-arch x86_64`, through `work/logs/world-{6..11}.log`. Six more
  distinct failures:
  1. `IOHIDKeys.h` includes `<IOKit/hidsystem/IOHIDParameter.h>`, but
     `copy_headers` staged only the five `hidsystem.subproj` headers:
     `fatal error: 'IOKit/hidsystem/IOHIDParameter.h' file not found` → stage
     the public `HID_IOKITSYS` set (`IOHIDParameter`, `IOHIDShared`,
     `IOHIDTypes`, `IOLLEvent`, `ev_keymap`) from
     `IOHIDSystem/IOKit/hidsystem` (`209dd80149`).
  2. The `runtime` recipe left `PATH=...` unquoted; a host PATH containing a
     space (VMware Fusion) became a command:
     `sh: line 1: Fusion.app/Contents/Public:/Library/TeX/texbin: No such file
     or directory` → quote the assignment in the runtime/libunwind/libcxx
     recipes (`081338826a`).
  3. `libcxx_dylib` needed `CRSetCrashLogMessage` but the SDK `libSystem` did
     not provide it: `Undefined symbols ... CRSetCrashLogMessage(char const*)`
     → build `CrashReporterClient` first and link it (`2c635fb627`).
  4. `CrashReporterClient`'s dylib linked `-lsystem_c/-lsystem_platform/-ldyld`
     before `Libsystem` built them: `ld: library not found for -lsystem_c` →
     drop the unnecessary flags; the source references no libc/dyld symbols
     (`195e1614b0`).
  5. `compiler_rt` linked the same unbuilt `libsystem_*` sub-libraries:
     `ld: library not found for -lsystem_platform` → link the `libSystem` TBD
     instead, and make `Libsystem` wait for `libcompiler_rt` and `libc++.a`
     (`1611ccc38c`).
  6. `-lCrashReporterClient` then selected the dylib, which `ld` rejects:
     `cannot link directly with ...libCrashReporterClient.dylib. Link against
     the umbrella framework 'System.framework' instead` → link the static
     `.a` by path (`78043ebe03`).
  `world-11` now builds the whole `Developer` stage, XNU and every kext,
  `IOKitUser`, `libc++`/`libc++abi`, compiler-rt, and reaches
  `Libraries/objc4`. It stops linking `Libraries/objc4/libobjc.A.dylib`:
  `Undefined symbols ... "CRGetCrashLogMessage()"` and
  `"CRSetCrashLogMessage(char const*)"`, referenced from `_objc_crashlog` in
  `runtime/objc-errors.o` (declared at `runtime/objc-os.h:242-243`). Best
  hypothesis: objc4's `LDFLAGS` only adds `-lSystem`, and `System.B` has not
  been built/re-exported at that point, so the CR entry points are absent.
  Not fixed here (six-failure limit).

- **2026-11-03 (rev 36) — Phase 1 `bmake world` runs past the toolchain.**
  Installed the documented Homebrew prerequisites (`bmake`, `cmake`, `ninja`,
  `make`, `bison`, `flex`, `libxml2`, `zlib`, `xorriso`, `mtools`, `nasm`) and
  ran the command from `Docs/macOS-Host.md`, `tools/macbuild.sh --audit
  --target-arch x86_64` (which is `bmake TARGET_ARCH=x86_64 world`), logging to
  `work/logs/world-{1..5}.log`. Four distinct failures fixed:
  1. LLVM invoked `${TOOLS}/gmake`, but `gmake` was dropped from the toolchain
     `SUBDIR` list: `.../Toolchains/Default.xctoolchain/usr/bin/gmake: No such
     file or directory` → use the host Homebrew `gmake` on Darwin
     (`045a360367`).
  2. bmake exported its jobserver as `MAKEFLAGS="-j N -J fd,fd"` and GNU make
     rejected the BSD-only flag: `: invalid option -- J` → clear `MAKEFLAGS`
     for the LLVM `gmake` call (`93d2ae28dd`).
  3. `Kernel/Makefile` used the same missing `${TOOLS}/gmake` for `installhdrs`
     and the xnu build: `*** [installhdrs] Error code 1` → resolve `GMAKE` to
     the host `gmake` in `sys.mk` on Darwin only; Linux keeps
     `${TOOLS}/gmake` (`814ff5999e`).
  4. `libclang_rt_profile` raced `installhdrs` under `-j` and compiled before
     the staged SDK had `<machine/_types.h>`: `.../usr/include/sys/_types.h:33:10:
     fatal error: 'machine/_types.h' file not found` → make `installhdrs` an
     explicit prerequisite (`44c2437550`).
  The build now completes the whole `Developer` stage (LLVM, cctools, xcbuild,
  iig), `libfirehose_kernel`, kernel header installation and reaches
  `Kernel/IOKitUser`. It stops at
  `.../ravynOS.sdk/System/Library/Frameworks/IOKit.framework/Headers/hid/IOHIDKeys.h:29:10:
  fatal error: 'IOKit/hidsystem/IOHIDParameter.h' file not found`, included
  from `Kernel/IOKitUser/hid.subproj/IOHIDEventObjc.m` via `IOHIDElement.h`.
  Best hypothesis: the IOKit header staging does not copy the `hidsystem`
  subtree into the SDK. Not fixed here (four-failure limit).

- **2026-11-04 (rev 40) — Live booter-kext linking, ACPI/PCI/AHCI attach, and `IOFindBSDRoot` reached (25s, 0 panics).**
  `BOOTX64.EFI` stages 30 `.kext` bundles (`\System\Library\Extensions\*.kext`
  and `System.kext\PlugIns\*.kext`) as page-aligned `_BooterKextFileInfo`
  buffers under `/chosen/memory-map` (`Driver-<addr>`), sets root
  `compatible="ACPI"`, and populates `/ACPI/RSDP` from `EFI_ACPI_20_TABLE_GUID`
  (`f5ff2c74b`, `d7504302e`). Guarding `OSKext::loadExecutable()`'s
  `kOSKextManagementEntitlement` check with `current_task() != kernel_task &&`
  (`OSKext.cpp:5496`) and interface `kmod_info` cleanup (`OSKext.cpp:5792`,
  `48ca4834c`) allows XNU to link all booter kexts live via `kxld` without
  `plktool`. QEMU runs 25s with 0 panics through `pthread.kext`,
  `ACPIPlatformExpert` (`v2 XSDT`), `AppleAPIC`, `q35` PCI (`6 devices`),
  `RavynAHCIPort` (`pci8086,2922`), and `IOStorageFamily`, waiting in
  `IOFindBSDRoot()` (`IOKitBSDInit.cpp:581-621`) for an `Apple_HFS` root volume.
- **2026-11-04 (rev 39) — Post-D6 bare XNU reaches `StartIOKit` / `IOPanicPlatform`.**
  Stripping `EFI_MEMORY_RUNTIME` in `boot_args->MemoryMap` and pointing
  `ba->efiSystemTable` at a zeroed stub (`719fa5b2ba`) cleared the `efi_init()`
  `VirtualStart == 0` `#GP` and revealed `pmap_lowmem_finalize()` (`pmap.c:1143`)
  freeing `[eHIB, stext)` (`0x19e000..0x200000`) over `[0x1A0000, 0x1E0000)`.
  Relocating the 256 KiB boot-data window above `__LINKEDIT` (`0x15aa000`,
  staged in high `EfiLoaderData`, copied after `ExitBootServices`, included in
  `ba->ksize`, `52d36895fd`) boots bare XNU with zero CPU faults (2.01s uptime)
  to `IOPanicPlatform::start` (`IOPlatformExpert.cpp:1995`, `loaded kexts: (none)`).
- **2026-11-03 (rev 38) — D6 GATE PASSED + Phase 1 `CrashReporterClient` / `libdyld`.**
  `BOOTX64.EFI` populates a 64-byte nonzero `/chosen/random-seed` (`ba3dc57c1`),
  booting XNU under QEMU+OVMF (`-cpu Penryn`) to print `Darwin Kernel Version 19.6.0`
  on serial. On the macOS build side, `CrashReporterClient` linked as static `.a`
  (`78043ebe03`) and `libdyld.dylib` `-lsystem_malloc` fixed (`76918cbcec`).
- **2026-11-03 (rev 37) — XNU now executes in 64-bit long mode; first failure
  mapped to the boot random seed.** This tree's `_pstart` is entered from UEFI
  in long mode (`osfmk/x86_64/start.s:149`, `.code64` at `L_pstart_common`),
  so the loader's 32-bit trampoline was wrong. Commit `cc832cf4eb` keeps long
  mode/paging, copies the Mach-O segments, and jumps with `EAX=boot_args`.
  First fault moved from `v=0d` at `_pstart+0x39` (`mov %ax,%ss`, CS32, PG off)
  to `v=0e` at `0xffffff8000481cc4` (`_current_cpu_datap`, CS64, PG on),
  with `CR2=0xffffdeadbeefee00` (GS poison). The call chain is
  `panic -> panic_trap_to_debugger -> _current_processor -> _current_cpu_datap`;
  the panic frame carries `RSI=64`, `RDX=0`, so the root cause is
  `PE_get_random_seed()` returning 0 (`prng_random.c:65`), not the handoff.
  A 64-byte `/chosen/random-seed` experiment logged nonzero at handoff but did
  not move the fault and was reverted; next is to inspect `DTGetProperty` at
  the actual call. Details in `Docs/DarwinBoot.md` § "D6 handoff status
  (2026-11-03)".
- **2026-11-03 (rev 36) — the OVMF `#UD 0xB0000` blocker does not reproduce
  locally; two real defects found while bisecting.** A clean
  `tools/bootloader/build.sh` produces a 15,872-byte PE32+ EFI application
  (`x86_64-elf-objdump -x`: Machine 0x8664, Subsystem 10, ImageBase
  0x140000000, entry RVA 0x12c0, `.reloc` with 10 base fixups). The documented
  pipeline (`mkimage --root ... --efi-payload ...`, plus the CI kernel/shell)
  boots under Homebrew QEMU + `edk2-x86_64-code.fd`: the ISO, the GPT/ESP image
  with a fresh `edk2-i386-vars.fd`, a bare GPT-less FAT16 volume, and a
  code-only pflash boot all print `RAVYNOS-ENTRY`; the ISO with `KERNEL.ELF`
  continues through `RAVYNOS-KERNEL-OK` to `RAVYNOS-KSHELL-OK`. The bare volume
  is announced as `UEFI Non-Block Boot Device` -- the same BDS name rev 17 read
  as "a device path but no file node" -- and still boots, so that name is not
  evidence of a missing file node. Packaging the tracked 21 MB Mach-O
  `BOOT/KERNEL` now succeeds (24 MiB wrapper, `VERIFY OK`); the loader parses
  its 8 segments and reaches handoff, where the guest `#GP`/`#DF`s, i.e. the
  remaining work is the Mach-O/XNU handoff (D6), not firmware entry.
  Reconstructing the pre-rev-16
  media (a bare PE as the El Torito UEFI entry, `iso9660.py --efi-image
  EFI/BOOT/BOOTX64.EFI`) reproduces `BdsDxe: failed to load ... Not Found` and
  falls back to the internal shell, not `#UD`; `verify.py` already rejects that
  shape. The one local failure that looked like "firmware never starts the
  image" was the tracked `out/local/vars.fd`: with that NVRAM the GPT/ESP image
  appears as `FS0:` in the UEFI shell but BDS has no boot entry for it, while a
  fresh variable store boots it. So the remaining `#UD 0xB0000` is
  firmware/harness state, not this loader or these writers. Fixed and verified
  locally: (1) `mkimage` sized the `--efi-payload` FAT wrapper at a fixed 4 MiB
  while also copying the kernel/shell/extras into it, so the documented build
  failed on any tree bigger than ~4 MiB (the tracked stage's 21 MB
  `BOOT/KERNEL`, or a 6 MiB extra, reproduces `fat: staged tree does not fit`);
  it now lets `fat.py` size itself, and the 6 MiB case builds, verifies and
  boots to `RAVYNOS-KSHELL-OK`. (2) `EFI_LOADED_IMAGE_PROTOCOL` omitted
  `Reserved`/`LoadOptionsSize`/`LoadOptions`, shifting every field after
  `FilePath`; the log read `LoadOptions` as `ImageBase` and printed
  `image base 0x0 size 0` on every boot. With the UEFI layout restored
  (`_Static_assert` size 96) the same boot reports
  `image base 0x000000001dd39000 size 28672` and still reaches
  `RAVYNOS-KSHELL-OK`. Commits `2ceef99c0e`, `5dce5e36e5`. `run-tests.sh`
  still reports its two pre-existing failures (symlink-case entry naming and
  the loader test expecting a 0xEF *section* when xorriso writes the EFI image
  as the default entry); both reproduce on the unmodified files.
- **2026-11-02 (rev 35) — the DriverKit generator never saw `NATIVE` classes.**
  Run 28 failed at `EXPORT_HDRS/iokit/DriverKit/OSAction.h:61:14: expected
  unqualified-id` on `class NATIVE KERNEL OSAction`, with the compiler noting
  `expanded from macro 'KERNEL'` = `1`. That note is the answer, and it inverts
  rev 30's reading: `NATIVE` goes through `IIG_NATIVE` and becomes empty, but
  `KERNEL` is `-DKERNEL`, i.e. `1`, so the line preprocessed to `class 1
  OSAction` and column 14 was the `1`. Reproduced and then eliminated. Three
  defects stacked up: `findMainClass()` allowed only one optional `KERNEL`, so
  every `NATIVE` class failed to match, `buildHeader()` fell through to its
  not-found path, and the definition was emitted *verbatim* — unsanitised, which
  is why rev 30's correct regex had no effect; those six headers also carried 22
  unguarded `LOCALONLY` method specifiers, so the class line alone was not the
  end of it; and `%.h : %.iig` had no `${IIG}` prerequisite, so a tool fix could
  never invalidate stale output. Verified by building iig and running it over all
  fourteen `.iig` files: 23 defective lines in `-DKERNEL=1` mode before, 0 after,
  0 consumer-mode residue, all fourteen preprocess cleanly, and the eight
  already-working headers byte-identical. The class stays out of the
  `#if KERNEL` branch on purpose — `iokit/IOKit/IOService.h` includes
  `<DriverKit/IOService.h>` under `KERNEL`, so declaring it twice would collide,
  and `IOService` already proves the shape. MIG has the same `%.h : %.defs`
  pattern without `${MIG}`, left alone because those directories build today.
  Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 34) — plktool stays Linux-only; Extensions skips the cache.**
  Rev 33 enabled plktool's build rule on Darwin on the assumption that its
  absence was an oversight. It is not: plktool is a Linux port of kextcache
  needing OpenSSL (`openssl/evp.h`), glibc `<byteswap.h>`, `-lssl -lcrypto -lbsd`
  and 236 CoreFoundation call sites against the in-tree CF. Reverted to
  Linux-only with the reason recorded, and `Kernel/Extensions` now builds the 16
  kexts on every host while skipping the prelinked cache on Darwin with an
  explicit message. The D1 gate only inspects `System/Library/Kernels/kernel`, so
  this does not affect it; a Darwin prelinked cache needs Apple's kextcache or a
  real port, tracked separately. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 33) — audited the whole remaining Kernel path.**
  Instead of chasing the next failing line, swept the remaining stages at once.
  Found and fixed one more blocker: `plktool` had no build rule on Darwin
  (`.else` / `all: .PHONY`) though `Kernel/Extensions:kernelcache` runs it, so
  every kext could build and the prelinked cache would then fail with
  "plktool: command not found"; it now builds on both hosts from the same rule.
  Also gave its objects real header dependencies so a header fix is not silently
  linked against stale objects (the defect that made the iig fix look inert).
  Cleared as already-correct: all 16 Extensions makefiles exist, every `SRCS`
  resolves (three apparent misses are `.PATH` lookups), no other tool builds
  nothing on Darwin, and no code depends on the old `__mb_state_t`. Left alone:
  IOUSBFamily/HID/tests, which are in no `SUBDIR`. Still needs a real
  `D1 GATE PASSED`; a static audit cannot substitute for compiling 16 kexts.
- **2026-11-02 (rev 32) — `__mb_state_t` renamed to `__mbstate_t`.**
  Rev 31 blamed iig's missing deployment target; the undefined symbol actually
  named the type `__mb_state_t`, and that spelling existed in only two files:
  the host toolchain's `include/{arm,i386}/_types.h`. Apple, xnu's own
  `bsd/{arm,i386}/_types.h`, the cctools ppc copy, the SDK's System.framework
  and the consumers `BSD/include/{stdio,wchar,uchar}.h` all use `__mbstate_t`.
  iig was compiled against the divergent copy while linking against the host
  libc++, so it instantiated `codecvt<char, char, mbstate_t>` on a type the
  dylib never saw. Renamed both copies; `iig.o` now depends on the headers so it
  is actually rebuilt (the failing run relinked without recompiling). Still needs
  a real `D1 GATE PASSED`.
- **2026-11-02 (rev 31) — iig built with a deployment target.**
  The host tool `iig` failed to link on arm64 with
  `std::__1::codecvt<char, char, __mb_state_t>::id` undefined. It is a host C++
  program instantiating `std::ostringstream`, built with no
  `-mmacos-version-min`, so it compiled against libc++ headers configured for the
  compiler's default target and asked for an ABI-tagged (`abi:ne200100`) symbol
  the SDK's libc++ does not export. Every other target in the tree passes the
  flag (`rvn.kext.mk`, `rvn.framework.mk`) and `MACOS_VERSION_MIN` is already
  exported by the top level; the host tools were simply missed. Not a regression
  from rev 30 — `iig.cpp` used `std::ostringstream` before that change, which
  only forced the rebuild. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 30) — iig stops emitting `NATIVE` into DriverKit headers.**
  IOHIDSystem failed to compile because the *generated*
  `EXPORT_HDRS/iokit/DriverKit/OSAction.h` contained
  `class NATIVE KERNEL OSAction : public OSObject`. (Rev 35 corrects the cause
  given here: `NATIVE` was *not* the problem — it maps to `IIG_NATIVE`, which is
  empty outside the IIG phase. `KERNEL` was, because `-DKERNEL` makes it `1`.
  The patch below was also inert, because the class never reached this code.)
  The header is generated from `OSAction.iig` by the
  in-tree `iig` tool, whose `sanitizeDocumentationText()` stripped `LOCAL`/
  `LOCALONLY` and `class KERNEL` but not `NATIVE` — and its `class\s+KERNEL`
  pattern could not match when `NATIVE` sat in between. Fixed in the generator
  (not the exported copy, which would be overwritten). Verified by compiling the
  old and new regex chains against all four declaration forms present in the
  DriverKit `.iig` files. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 29) — corecrypto stops recursing into `user/`.**
  The Kernel pass reached `Kernel/Extensions` and failed with
  `bmake[4]: don't know how to make all` in `corecrypto/user`, which holds only
  `dummy.c` (a typedef that exists because Xcode refuses a target with no
  sources). `SUBDIR = user` is removed, matching the earlier fix for IOKitUser's
  `kext.subproj`. A rescan across all stages — this time matching lowercase
  `makefile` too, which is why the earlier sweep missed this file — finds no
  other `SUBDIR` naming a directory without a makefile (2400 files checked).
  Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 28) — SDK seeds link stubs for IOKit.framework.**
  With `-lkernel` gone the IOKit.framework link failed on the next missing
  input, `ld: framework not found CoreFoundation`; the SDK ships only
  `Kernel.framework` and `System.framework` and an empty `usr/lib`, yet the link
  names CoreFoundation, Foundation, objc, c++, z and System, all produced by the
  Libraries and Frameworks passes that run *after* Kernel. `Developer/ravynOS.sdk`
  now seeds the CLT SDK's TBD text stubs for those five, extending the
  `CLT_LIBSYSTEM_COPY` precedent already in that file. `-framework Foundation`
  is appended by `bsd.sys.mk:529` for any `.m`/`.mm` source, so it was going to
  be the next failure regardless. Chose stub seeding over reordering the world,
  which would re-run Libraries/Frameworks on every `bmake Kernel`. Still needs
  a real `D1 GATE PASSED`.
- **2026-11-02 (rev 27) — IOKit.framework no longer links `-lkernel`.**
  Every IOKitUser source now compiles; the framework link then failed with
  `ld: library not found for -lkernel`. That library is `libkernel.a`, the
  libsyscall wrapper archive built by
  `Libraries/Libsystem/libsystem_kernel` — but the top level builds
  `Developer .WAIT Kernel Libraries ...`, so Kernel links before Libraries has
  produced it, and IOKitUser references none of its symbols anyway (`kxld_*`
  comes from the locally built `libkxld.a`). The flag is dropped; `-lcc_kext`
  stays because `Kernel/Makefile:compiler_rt` stages it during the same pass.
  Upstream carries the same defect. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 26) — `kHIDPage_AppleVendor` defined once.**
  `IOHIDUsageTables.h` declared it in the same enumerator as
  `kHIDPage_VendorDefinedStart = 0xFF00`, duplicating the entry in
  `AppleHIDUsageTables.h` and breaking any translation unit that sees both
  (IOKitUser's ObjC++ files include both). Apple's upstream file ends at
  `kHIDPage_VendorDefinedStart`, so the extra entry was a local addition and is
  removed; every consumer still gets the constant from
  `AppleHIDUsageTables.h`, which IOKitUser force-includes. A sweep of all 140
  staged headers found no other duplicate enumerator and no shared include
  guards. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 25) — the right os/object.h this time.**
  Rev 24 staged the correct file name from the wrong tree:
  `BSD/include/os/object.h` is a stale 188-line variant that shares the
  `__OS_OBJECT__` guard with the real header but defines no
  `OS_OBJECT_DECL_CLASS`, so `dispatch/object.h` failed with "unknown type name
  'OS_OBJECT_DECL_CLASS'" and no `dispatch_object_t`. The SDK now stage
  `usr/local/include/kernel/os/object.h` (271 lines, the copy
  `Kernel/IOKitUser/Makefile:151` already force-includes) instead. The trigger
  is `IOKitLib.c`, a plain C file that pulls dispatch and therefore needs the
  non-ObjC branch. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 24) — SDK stages `<os/object.h>`.**
  The D1 gate failed on `dispatch/dispatch.h:64: 'os/object.h' file not found`,
  which is an SDK-content gap rather than a kernel one: `usr/include/os/` had
  `base.h`, `lock.h`, `availability.h` and `state_private.h` but not
  `object.h`, while `dispatch.h`, `xpc/xpc.h` and the `asl_*.h` headers in the
  same SDK all include it. The tree's copy is `BSD/include/os/object.h`, whose
  `os/Makefile` INCS list had never been wired into this SDK's install; it is
  now copied explicitly. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 23) — the header-glob shortcut was reverted.**
  Replacing IOKitUser's per-file header lists with `cp dir/*.h` (rev 22) was a
  regression: the tree has two `IOHIDEvent.h` files -- the user-space C API in
  `hid.subproj` and the kernel C++ class `class IOHIDEvent : public OSObject`
  in `IOHIDFamily/IOHIDFamily` -- and the glob put the class header last, so it
  won the `<IOKit/hid/IOHIDEvent.h>` lookup and `.m` sources failed to compile.
  Eight basenames collide this way, and `hid.subproj/IOHIDTypes.h` is a 0-byte
  placeholder that was overwriting the real one. The validated per-file list is
  restored, plus `IOHIDLibObsolete.h` and the real `hidsystem/IOHIDTypes.h`.
  A checker now enforces: nothing empty staged, no kernel-class header reached
  from a pure `.c`/`.m`, no unsatisfied `<IOKit/...>` include. Still needs a
  real `D1 GATE PASSED`.
- **2026-11-02 (rev 22) — IOKit headers staged as trees, not lists.**
  Runs 13-14 fixed one missing `IOKit/hid` header per build; run 15 hit
  `IOHIDParameter.h`. The enumeration in `copy_headers` was simply never
  complete, so it now copies the whole public directories (hid.subproj,
  IOHIDFamily/IOHIDFamily, IOHIDFamily/IOHIDLib, IOHIDSystem/IOKit/hidsystem,
  kext.subproj, IOGraphics/IOKit/{graphics,i2c}) instead of naming files. The
  closure model — rebuilt to join recipe continuations and to search all of
  `Kernel/` for a missing header — models 212 staged headers and reports zero
  unsatisfied `<IOKit/...>` includes; it also caught a gap the hand-picked
  search path had hidden (`IOHIDLibObsolete.h` lives in IOHIDFamily/IOHIDLib).
  Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 21) — IOKit/hid header closure staged in one go.**
  Run 13 fixed one missing header; the compiler then asked for the next
  (`IOHIDEventFieldDefs.h`). Instead of iterating, the full closure was
  computed from the Makefile's own copy lists, the target's `-I` set and a
  transitive scan of 831 files: fifteen `IOHIDFamily/IOHIDFamily` headers and
  `hid.subproj/IOHIDTypes.h` (as `<IOKit/hidsystem/...>`) are now staged, and
  the model reports zero remaining gaps. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 20) — IOKitUser's header staging was incomplete.**
  With the kernel linking, `Kernel` reached IOKitUser and stopped on
  `'IOKit/hid/IOHIDEvent.h' file not found`. IOKitUser's `copy_headers` is a
  hand-maintained copy list that never staged `IOHIDEvent.h` (it lives in
  `hid.subproj/`) nor the `IOHIDEventTypes.h` that header includes; clang
  resolves `<IOKit/...>` inside `IOKit.framework` and so found neither. Both are
  now copied into the framework's `Headers/hid`. Pre-existing, only reachable
  now that the earlier failures are gone. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 19) — the kernel links.** `Kernel` now gets past
  `kernel.unstripped` (the freestanding profiling runtime works) and proceeds to
  IOKitUser, where it hit a pre-existing defect: `SUBDIR = kext.subproj` in
  `Kernel/IOKitUser/Makefile` recursed into a directory that has no Makefile
  (`bmake[3]: don't know how to make all`). `kext.subproj` is a source
  directory compiled by the parent's `SRCS`; the file is byte-identical to
  upstream, so nothing had ever reached that line — the kernel link always
  failed first. `SUBDIR` is now empty. Still needs a real `D1 GATE PASSED`.
- **2026-11-02 (rev 18) — the kernel was linking the *userspace* PGO runtime.**
  D1's link failure after run 9 was `___stderrp`/`_fprintf` from
  `libclang_rt.profile_osx.a(InstrProfilingWriter.c.o)`. The CLT archive is the
  userspace runtime: `PROF_ERR`/`PROF_WARN` expand to `fprintf(stderr, …)` and
  `InstrProfilingFile.c` wants `getenv`/`open`/`mmap`, none of which exist in a
  `-nostdlib -fapple-kext` kernel link. New `Libraries/libclang_rt_profile`
  builds the six-file freestanding subset LLVM itself puts in
  `libclang_rt.cc_kext.a` (`-mkernel -DKERNEL_USE`) and stages it as
  `libclang_rt.profile_osx.a` beside `libcc_kext.a`; the CLT profile copy is
  gone and `XNU_PROFILE_RT_DIR` is that directory on every host. Also fixed the
  long-standing `/bin/sh: 6-${\#__A}: bad substitution` — GNU make keeps the
  backslash in upstream's `\#`, so `CMD_MC` always came out empty; it is now
  one `printf %-6.6s` whose output is byte-identical to the old expansion.
  **Unverified on hardware: still needs a real `D1 GATE PASSED`.** The first
  attempt at that library staged `clang_rt.profile_osx.a` (no `lib` prefix, so
  `-l` would have missed it) and compiled nothing, because the archive rule
  was declared before `.include <bsd.lib.mk>`, which is what derives `${OBJS}`
  from `${SRCS}`; the rule now delegates to `bsd.lib.mk` and only copies the
  finished archive into the SDK.
- **2026-10-31 (rev 17) — the chain exists; firmware still won't start it.**
  (1) *Chain*: loader → kernel console → prompt, plus an independent shell, all
  built and verified, with `verify.py` parsing the kernel *inside* the finished
  ISO and ESP (`entry 0x100140, 1 PT_LOAD`), and CI driving the prompt with
  `bootcheck --send` (`help ver mem exit`). New `Docs/Shell.md`. (2) *Three
  media*: the `uefi` job now boots the El Torito ISO, the GPT/ESP image and a
  bare GPT-less FAT16 volume. All three produce the *same* `#UD` at `RIP
  0xB0000`, which rules out both FAT writers and the GPT writer: the fault is
  before our payload. (3) *Diagnosis tooling*, because three rounds of guessing
  is enough: the loader prints `RAVYNOS-ENTRY` from its entry point with the
  UART as its first action; `bootcheck` keeps a failed guest alive and
  `tools/bootcheck-postmortem.py` reads its registers and the memory region
  owning `RIP` from the QEMU monitor (for this crash: `0xA0000-0xBFFFF is
  vga-lowmem — not code`); `--disk-interface` A/Bs virtio against IDE; the CI
  job records the qemu/OVMF versions and firmware hashes. (4) *Three latent
  loader bugs* fixed by inspection, since nothing could reach them yet:
  `EFI_SERIAL_IO_PROTOCOL` lacked `Revision` (so `Reset()` called `0x10000`),
  the kernel range was reserved at a non-page-aligned address
  (`AllocateAddress` rejects it), and the bootinfo page at `0x90000` was
  written without being reserved. (5) *A self-inflicted CI failure*, recorded
  because it cost a run: a shell line continuation written as `\\` in the new
  firmware-inventory step made bash run `/usr/share/edk2/ovmf` as a command, so
  the step failed and every boot was skipped. Tests: 36 (images) + 21 (PE) + 13
  (ELF) + 7 (kernel) + 4 (post-mortem), `ALL TESTS PASSED`.

- **2026-10-30 (rev 16) — CI ran the new UEFI job three times and taught us
  what the host could not.**  (1) lld-link rejects the numeric subsystem
  (`-subsystem:10` -> "unknown subsystem: 10"): use `efi_application`.  (2)
  `bootcheck`'s timeout counter pinned itself at 1, so a boot with no marker
  ignored `--timeout` and ran until CI killed the job — fixed, with a
  regression test that fakes QEMU and asserts the timeout fires in ~5 s.  (3)
  OVMF loaded the loader from the ESP and it raised `#UD` at the entry; the
  job now dumps `llvm-readobj`/`llvm-objdump` of the exact binary into the
  exported log (artifact downloads are blocked here).  (4) The ISO's El Torito
  UEFI entry was a bare PE, and OVMF answered `BdsDxe: failed to load ...
  Not Found`: EDK2 turns the entry into a *partition* and mounts it, so it must
  be a FAT volume.  `mkimage --efi-payload` now wraps a PE32+ application in a
  4 MiB FAT16 volume, `--efi-image` is validated as that shape, `verify.py`
  checks it (`verify.py efiboot`) and rejects a bare PE with the actionable
  hint.  Open: the loader's `#UD` on the ESP (disassembly of the clang build is
  in the next run's exported log), and the ISO boot verdict after the FAT fix.

- **2026-10-30 (rev 15) — Step B built, and two defects that made it
  unbootable.**  The UEFI loader had been "in tree" since rev 12 without ever
  being compiled: its Makefile used bmake-only automatic variables, so `make`
  failed on every Linux host.  Rewritten as a thin wrapper over a new
  `tools/bootloader/build.sh` that detects clang+lld-link or falls back to a
  zig binary (the only PE linker available in this sandbox), and the entry
  symbol became `EfiMain` so both linkers accept it; the output is validated
  immediately.  Building it exposed the second defect: `--efi-load-size`
  defaulted to **1** sector, so the El Torito catalog told firmware to load 512
  bytes of a 3584-byte application — a guaranteed truncated boot with no
  diagnostic.  The count is now derived from the file (`auto`), a smaller
  explicit value is refused, and the new `tools/image/pe.py` validates the
  PE32+/EFI headers at build time and again inside the finished ISO and ESP.
  `bootcheck --uefi` now handles split `OVMF_CODE`/`OVMF_VARS` firmware by
  copying the variable store.  A new CI job `uefi` builds the loader with
  clang+lld-link, checks the PE, stages an ESP, builds ISO+IMG and boots both
  under OVMF with `RAVYNOS-LOADER-OK` required.  Tests: 34 (images) + 21 (PE)
  unit tests, plus a loader-build layer in `run-tests.sh` that skips only when
  no PE toolchain exists.  Verified here: loader builds (3584 B, x86_64,
  subsystem 10, entry 0x1000), ISO catalog announces 7 sectors for it, ISO and
  IMG both pass the extended verifier; the OVMF boot verdict is CI's to give.

- **2026-10-30 (rev 14) — the build driver had never pointed at the tree.**
  With bmake built locally from a source mirror (this sandbox has no bmake and
  no Debian mirror), `bmake world`'s five-minute failure reproduced: `repo_root`
  in `tools/lib/common.sh` was `dirname $0/../..`, correct for `tools/lib/` and
  one level too high for `tools/`, so `macbuild.sh` ran bmake in the
  *repository's parent* and every image was stamped "version unknown / commit
  unknown / built-at 1980-01-01". Fixed by walking up to the directory that
  holds `Makefile` and `BSD/share/mk`. Two more driver bugs fell out: the tree's
  own `BSD/share/mk` was never on bmake's search path early enough (sys.mk's
  `.include <bsd.mkopt.mk>` could not resolve, "Fatal errors encountered"), and
  `${PIPESTATUS[0]}` — a bash array — was read in a `#!/bin/sh` script, so under
  dash a failed build reported tee's status. Also in this revision: `mkimage
  --root` dereferences relative symlinks inside the root and skips absolute ones
  with a warning (the tree's own staging creates symlinks, so `--root` could not
  package this project at all), and the mtools reproducibility defect is
  characterised — the differing bytes are 64-bit stack addresses at the end of
  the FAT volume, i.e. uninitialised memory, so it is upstream and now warns
  instead of failing the job.
- **2026-10-30 (rev 13) — the pipeline was made to run on machines with QEMU,
  and it did not run.** Three CI rounds, nine defects, all fixed and pushed:
  a bash-3.2 parse error that killed every macOS job, an `mcopy` invocation
  mtools rejects, an `AttributeError` that made `--backend mtools` impossible,
  `brew install` with no packages, `-b ""` to xorriso (made `--root` fail on
  every runner), mtimes leaking into images, a reproducibility job that only
  tested the Python fallback, acceleration chosen from `-accel help` rather
  than from what the host can run, and a boot-verdict step that wrote to a
  stream nobody reads. Also added `tools/ci-log-export.sh`, which republishes
  each job's log as a commit comment because GitHub's log-blob host is not
  reachable from every client. Result: 10/11 jobs green at `7cf8a69`, and a
  **QEMU boot verdict on real runners** for the first time — stub marker on
  Ubuntu x64 and macOS arm64 (TCG; HVF correctly rejected), and the four-stage
  milestone chain on both. Open: mtools output is not reproducible; the
  UEFI/ESP path has never booted; no real kernel has been loaded.
- **2026-10-30 (rev 12) — audit-driven portability fixes:** replaced a BSD-only
  `sed -i ''` in `bsd.crunchgen.mk` and a BSD-only `stat -f %i` in `man.sh` with
  portable equivalents, and annotated the two deliberate host-detection
  exceptions. The audit is now clean at `--severity medium` tree-wide.
- **2026-10-30 (rev 11) — pipeline completed, CI + build tooling:** `mkimage.sh`
  gained real staging mode, the GPT/FAT `.img` variant and explicit backends;
  dependency-free ISO/El-Torito, FAT16/GPT and boot-stub writers plus an
  independent verifier landed under `tools/image/`; `bootcheck.sh` became a
  multi-milestone verdict with JSON output, acceleration selection, OVMF
  support and screenshots; `tools/tests/` adds 30 unit tests, a CLI contract and
  a shell-syntax layer. CI: `pipeline.yml` rewritten (tests / structure /
  reproducibility / boot), plus `build.yml`, `nightly.yml` and a reusable
  `install-host-tools` action. Phase 1 tooling: `tools/macbuild.sh` and
  `tools/macbuild-audit.py` (clean at `--severity high`). Docs: `Docs/Pipeline.md`,
  `Docs/BootLoader.md`, `Docs/macOS-Host.md`, `work/README.md` + templates.
- **2026-10-30 (rev 10) — M1 toolchain milestone:** `bmake Developer` now exits 0 on macOS. All accumulated `macbuild:` fixes committed as a single milestone (cctools/ld64, dtrace_ctf, os/base.h, TAPI, LLVM, top-level `LD`/`TARGET_ARCH`). Next: `bmake world` for the kernel, SDK and userland.
- **2026-10-30 (rev 7) — published + CI green:** fork at https://github.com/Lauel09/ravynos; CI pipeline self-test green on Ubuntu and macOS; `tools/bootview.sh` added.
- **2026-10-30 (rev 6) — published:** fork created, default branch set, branch and tag pushed.
- **2026-10-30 (rev 4) — Phase 1 build loop + Phase 2 stub proven:** ten `bmake world` attempts; fixed non-root chowns, yacc/lex shims, arm64 `TargetConditionals`, xar OpenSSL/libxml2, libctf `qsort_r`, quoted LLVM CMake flags, and shim-header shadowing. Target arch made explicit (`TARGET_ARCH=x86_64` default, arm64 opt-in). Portable pipeline scaffolded and its stub loop self-tested end to end (PASS). `Docs/BUILDING.md` corrected.
- **2026-10-30 (rev 3) — Phase 0 executed:** all Homebrew prerequisites installed; clone unshallowed to full 745-commit history; `fork/darwin-dev` branch + `baseline-60aa4ff4b` tag created; rea wired to Ghidra 12.1.4; redundant Ghidra zip deleted. `bmake -n version` parses; dry run surfaced first Phase 1 findings.
- **2026-10-30 (rev 2):** Books RE pilot retired and deleted; RE doctrine rewritten (smallest-subject-first). Portable macOS+Linux image pipeline promoted to primary work item. `work/README.md` added.
- **2026-10-30 (rev 1):** Initial audit: repo, build system, macOS host, ISO pipeline, RE workspace assessed; first macOS build failure captured; blockers enumerated; `progress.md` and `roadmap.md` created.

*Next actions live in [`roadmap.md`](roadmap.md). Update this file whenever a blocker clears or a new one is found.*
