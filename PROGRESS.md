# RavynOS Fork — Progress Log

**Last updated:** 2026-10-30 (rev 12 — portable pipeline complete through real staging; CI matrix; macOS build driver + portability audit)
**Workspace:** `/path/to/test-ravyn-os`
**Mission:** Improve the RavynOS codebase from our own fork, developed macOS-first on this MacBook (Apple Silicon, macOS 26.3.1), with a **portable** pipeline (macOS + Linux) from source → bootable IMG/ISO → automated boot verdict, and need-driven reverse engineering with [rea](https://github.com/morluto/rea) on this machine when a gap justifies it.

> **Status (rev 11):** Phase 1's toolchain milestone is done (`bmake Developer`
> exits 0 on macOS) and the loop is now tooled: `tools/macbuild.sh` (preflight,
> logging, per-stage resume, `--audit`) and `tools/macbuild-audit.py` (static
> host-hazard scan, clean at high severity). Next is SDK → kernel → userland.
> Phase 2 is complete except for real-image *content*: `tools/mkimage.sh` has
> real staging mode, ISO and `.img`, xorriso/mtools or dependency-free Python
> backends, an independent structural verifier and a test suite; the QEMU
> verdict is multi-milestone. Phase 3's three workflows are written and await
> their first CI run. Phase 0 complete: the fork is published at
> https://github.com/Lauel09/ravynos.

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

**Validation:** the selftest passes structure-only in ~1 s on Linux; the ISO
writer is cross-checked against pycdlib and the FAT writer against pyfatfs
during development, and every artifact is validated by `verify.py` in CI.
Reproducibility is asserted by a CI job on both host families.

**Loader:** `Docs/BootLoader.md` records the decision (UEFI application is
primary; the ISO already carries a BIOS slot *and* a UEFI catalog entry) and a
five-step plan.  Step B's skeleton is in tree: `tools/bootloader/uefi_stub.c`
plus a `Makefile` — a freestanding PE32+ application with the UEFI tables
declared inline (no gnu-efi, no EDK2), printing `RAVYNOS-LOADER-OK` over the
serial protocol with a `ConOut` mirror.  It has not been compiled here; the
next step is to build it and boot it under OVMF.

**Remaining for the M2 exit criterion:** a real staged sysroot from the Phase 1
build, and a loader that emits `RAVYNOS-LOADER-OK` … `RAVYNOS-USERSPACE-OK`.

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

1. **[active]** `bmake Developer` (attempt 14) — LLVM is cached and parallel; expecting to pass `cctools` and expose the next failure.
2. Commit the accumulated `macbuild:` fixes (libtapi exports, libtapi.so→dylib, shim-header filtering, XNU `MACHINE`, parallel LLVM) as a single milestone commit once the toolchain builds.
3. **[Phase 1, next]** SDK → kernel → userland. Keep every patch `uname`/`.MAKE.OS`-guarded and log failure→fix pairs here.
4. **[Phase 2, next]** Real staging mode in `mkimage.sh` once artifacts exist; keep `tools/pipeline-selftest.sh` green.
5. No build-from-source CI matrix yet (only the pipeline self-test) — Phase 3 proper comes after M1.

---

## Changelog

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