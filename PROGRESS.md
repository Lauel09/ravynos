# RavynOS Fork — Progress Log

**Last updated:** 2026-10-30 (rev 8 — build loop through attempt 13; toolchain still in progress)
**Workspace:** `/path/to/test-ravyn-os`
**Mission:** Improve the RavynOS codebase from our own fork, developed macOS-first on this MacBook (Apple Silicon, macOS 26.3.1), with a **portable** pipeline (macOS + Linux) from source → bootable IMG/ISO → automated boot verdict, and need-driven reverse engineering with [rea](https://github.com/morluto/rea) on this machine when a gap justifies it.

> **Status: Phase 1 in progress.** The host toolchain builds through LLVM and now fails in `cctools/ld64` on a TAPI library-name mismatch (`libtapi.so` vs `libtapi.dylib`); that fix is in the working tree and not yet committed. Phase 2's root-free stub loop is proven end to end and its CI runs green on both Ubuntu and macOS. Phase 0 is complete: the fork is published at https://github.com/Lauel09/ravynos.

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

## 4. Bootable image pipeline status — **active work item (Phase 2)**

`ISO/build.sh` is **FreeBSD-host-only** and requires root (`mdconfig`, `zpool`, `gpart`, `mkuzip`, `makefs`, `mkimg`, `newfs_msdos`, `mount msdosfs`, `chroot`, `etdump`). **Decision (2026-10-30): do not port it.** Replace with a portable pipeline (`tools/mkimage.sh` + `tools/bootcheck.sh`) on `xorriso` + `mtools` + QEMU — no root, no mounts, no chroot, identical on macOS and Linux. Substitution table and design: `roadmap.md` Phase 2.

**Implemented and proven (stub mode):** `tools/mkimage.sh --stub` builds an
El Torito no-emulation ISO carrying `tools/bootstub/bootstub.asm`; QEMU boots
it headless and `tools/bootcheck.sh` greps COM1 for
`RAVYNOS-BOOTCHECK-STUB-OK`. `tools/pipeline-selftest.sh` runs the whole loop
and **passes in ~1 s** on this Mac. The stub needs `nasm` in addition to
QEMU/xorriso. `bootcheck.sh` keeps serial and QEMU logs, detects early QEMU
exit and reaps the process cleanly; `BOOTCHECK_MEM` overrides the 2 GB default.
`tools/bootview.sh` provides a graphical inspector.

Next Phase 2 step: add the real staging mode to `mkimage.sh` once Phase 1
produces kernel/loader/userland artifacts; the ISO plumbing itself is no
longer the risk. No bootloader source exists in the tree yet, so the real image
will need a loader decision (QEMU `-kernel` direct boot vs. an EFI loader).

CI: `.github/workflows/pipeline.yml` runs `tools/pipeline-selftest.sh` on
`ubuntu-latest` and `macos-latest` and is **green on both**.

## 5. Reverse-engineering workspace (`work/`)

| Asset | Status |
|---|---|
| `rea` CLI | ✅ v6.7.0 (Homebrew) |
| Ghidra provider | ✅ **wired & healthy (2026-10-30)**: `GHIDRA_INSTALL_DIR=/path/to/test-ravyn-os/work/ghidra_12.1.4_PUBLIC` exported in `~/.zshrc`; `rea doctor` reports `ghidra`, `ghidra-platform`, `ghidra-architecture` all healthy. Homebrew's Ghidra 12.2 left installed (harmless — env var takes precedence). Remaining doctor warnings are optional engines (Hopper/IDA/JADX/Kuna) and other clients' registrations — not needed |
| `ghidra_12.1.4.zip` | ✅ deleted after verification (reclaimed ~570 MB) |
| Doctrine | Smallest subject that answers the question; need-driven targets only. See `work/README.md` |

No investigation currently open; the first gets nominated by the first real gap Phases 1–2 produce.

## 6. Current blocker / work list (ordered)

1. **[active]** `bmake Developer` (attempt 14) — LLVM is cached and parallel; expecting to pass `cctools` and expose the next failure.
2. Commit the accumulated `macbuild:` fixes (libtapi exports, libtapi.so→dylib, shim-header filtering, XNU `MACHINE`, parallel LLVM) as a single milestone commit once the toolchain builds.
3. **[Phase 1, next]** SDK → kernel → userland. Keep every patch `uname`/`.MAKE.OS`-guarded and log failure→fix pairs here.
4. **[Phase 2, next]** Real staging mode in `mkimage.sh` once artifacts exist; keep `tools/pipeline-selftest.sh` green.
5. No build-from-source CI matrix yet (only the pipeline self-test) — Phase 3 proper comes after M1.

---

## Changelog

- **2026-10-30 (rev 9) — session backup:** attempts 10–15 logged; toolchain now reaches `cctools/ld64` and fails on `reallocf` (BSD libc helper missing on macOS) in `ResponseFiles.cpp`. Uncommitted fixes in tree: `libtapi.so`→`libtapi.dylib`, `CPU_SUBTYPE_X86_ALL`→`CPU_SUBTYPE_X86_64_ALL`, and the `reallocf` call sites. Fork published; CI green on both hosts. All builds stopped at user request; resume with `bmake Developer`.
- **2026-10-30 (rev 7) — published + CI green:** fork at https://github.com/Lauel09/ravynos; CI pipeline self-test green on Ubuntu and macOS; `tools/bootview.sh` added.
- **2026-10-30 (rev 6) — published:** fork created, default branch set, branch and tag pushed.
- **2026-10-30 (rev 4) — Phase 1 build loop + Phase 2 stub proven:** ten `bmake world` attempts; fixed non-root chowns, yacc/lex shims, arm64 `TargetConditionals`, xar OpenSSL/libxml2, libctf `qsort_r`, quoted LLVM CMake flags, and shim-header shadowing. Target arch made explicit (`TARGET_ARCH=x86_64` default, arm64 opt-in). Portable pipeline scaffolded and its stub loop self-tested end to end (PASS). `Docs/BUILDING.md` corrected.
- **2026-10-30 (rev 3) — Phase 0 executed:** all Homebrew prerequisites installed; clone unshallowed to full 745-commit history; `fork/darwin-dev` branch + `baseline-60aa4ff4b` tag created; rea wired to Ghidra 12.1.4; redundant Ghidra zip deleted. `bmake -n version` parses; dry run surfaced first Phase 1 findings.
- **2026-10-30 (rev 2):** Books RE pilot retired and deleted; RE doctrine rewritten (smallest-subject-first). Portable macOS+Linux image pipeline promoted to primary work item. `work/README.md` added.
- **2026-10-30 (rev 1):** Initial audit: repo, build system, macOS host, ISO pipeline, RE workspace assessed; first macOS build failure captured; blockers enumerated; `progress.md` and `roadmap.md` created.

*Next actions live in [`roadmap.md`](roadmap.md). Update this file whenever a blocker clears or a new one is found.*
EOF
echo written