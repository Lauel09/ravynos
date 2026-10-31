# RavynOS Fork — Progress Log

**Last updated:** 2026-10-31 (rev 17 — loader → kernel → prompt built and verified in-image; CI boots three media under OVMF; the remaining failure is firmware never *starting* the image)
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

1. **[Phase 1, in flight]** `bmake world` on both runners, first attempt that
   gets past the first five minutes (driver fixes above). Until it reports,
   every statement about LLVM, the SDK, XNU or the userland is inherited from
   earlier notes rather than observed.

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
EOF
echo written