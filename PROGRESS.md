# RavynOS Fork — Progress Log

**Last updated:** 2026-10-30 (rev 4 — Phase 1 build loop started; Phase 2 stub loop proven)
**Workspace:** `/path/to/test-ravyn-os`
**Mission:** Improve the RavynOS codebase from our own fork, developed macOS-first on this MacBook (Apple Silicon, macOS 26.3.1), with a **portable** pipeline (macOS + Linux) from source → bootable IMG/ISO → automated boot verdict, and need-driven reverse engineering with [rea](https://github.com/morluto/rea) on this machine when a gap justifies it.

> **Status: Phase 1 in progress.** Host toolchain build is being driven failure-by-failure on macOS (10 attempts so far). Phase 2's root-free stub loop is proven end to end (`tools/pipeline-selftest.sh` → PASS). Phase 0 still needs the user's `gh auth login` for the fork remote.

---

## 1. Repository state

| Item | Status |
|---|---|
| Source | `ravynos/` — clone of `ravynsoft/ravynos`, based on `darwin` branch HEAD `60aa4ff4b` "OpenGL and CoreText frameworks" (2026-09-06) |
| History | ✅ **Full history fetched** (was depth-1): 745 commits, upstream tags (`SDK_0.7.0.x`, `linux_tc_0.2`, `kernel`, `demo-vm`), `.git` ≈ 1.5 GB |
| Local branch | ✅ `fork/darwin-dev` created off `darwin`; baseline tag `baseline-60aa4ff4b` |
| Fork remote | ⏳ **pending** — `gh` is installed but not authenticated; run `gh auth login`, then `gh repo fork ravynsoft/ravynos --clone=false && git remote add fork <url>` |
| Version in tree | `PROD_VERSION 0.7.1`, `PROD_FAMILY Pre Alpha` (top-level `Makefile`) |
| CI in repo | None. `.github/` contains only `FUNDING.yml`. Legacy scripts still reference Cirrus CI |

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

## 2. Build system — current reality

- Entry point is a **BSD-make-only top-level `Makefile`**: builds a host toolchain (LLVM etc.) first, then `world` = `version Developer Kernel Libraries/libfirehose_kernel Libraries BSD`.
- **Target arch is now explicit and host-independent:** `TARGET_ARCH ?= x86_64` (upstream-supported, QEMU-bootable) maps to `BuildArch=X86`, `CpuArch=x86_64`, `ARCH_CONFIGS=X86_64`. `bmake TARGET_ARCH=arm64 world` selects the AArch64 experiment. This removes the old host-`MACHINE` auto-selection that produced an AArch64 toolchain on this arm64 Mac while `ARCH_CONFIGS` stayed X86_64.
- `Docs/BUILDING.md` was stale (told macOS users to run `cmake -S ravynos`); **fixed** to document the real `bmake world` entry point, Homebrew prerequisites, target-vs-host arch and the portable pipeline. CMake/Ninja remain internal to the toolchain build (LLVM at `Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt`).
- `tools/build.sh` is a legacy zsh wrapper for the **old FreeBSD-based build** — historical reference only.

## 3. macOS host status (this MacBook)

### 3.1 Toolchain prerequisites — ✅ all installed via Homebrew (2026-10-30)

| Tool | Status |
|---|---|
| Apple clang 17.0.0 (Xcode) | ✅ matches `BUILDING.md` requirement |
| **bmake** (BSD make) | ✅ `brew install bmake` |
| **cmake 4.4.5 / ninja 1.13.2** | ✅ |
| **gmake 4.4.1** | ✅ via `brew install make` (Homebrew formula is `make`; binary is `gmake` — `gmake` is *not* a formula name on macOS) |
| **xorriso 1.5.8 / mtools 4.0.49** | ✅ for the portable image pipeline |
| **nasm** | ✅ assembles the loader-only boot stub |
| **QEMU 11.1.3** (`qemu-system-x86_64`) | ✅ for boot smoke tests |
| Python 3.9.6 (system) | present; swap to brew python if xcbuild rejects it |

### 3.2 Phase 1 build loop on macOS (2026-10-30)

`bmake -n version` parses cleanly (GNU make fails with `missing separator`). Real
`bmake world` attempts are logged at the workspace root
(`build-world-attempt1.log` … `build-world-attempt10.log`).

| Attempt | Failure | Fix / commit |
|---|---|---|
| 1 | `chown root:wheel` on installed gmake fails as non-root | `823f5d04a7` — skip root chown in user builds |
| 2 | same chown in `mig` (and `unifdef`) | `f93f763a71` — guard those chowns too |
| 3 | `yacc`/`bison` Xcode shim refuses CLT-only host | top-level Makefile falls back to Homebrew `bison -y`/`flex` |
| 4 | toolchain shim `TargetConditionals.h` rejects arm64 clang | `98e004a59a` — recognize arm64/arm64e |
| 5 | `xar`: `openssl/evp.h` not found (SDK has no OpenSSL headers) | `aecec6615e` — use `brew --prefix openssl@3` (paired with `c01b63e8c1`, which scopes `-I/usr/include/libxml2` to Linux) |
| 6 | libctf's bundled `qsort_r` conflicts with Darwin's declaration | **fixed, uncommitted** — build `qsort.c` only off Darwin; `array.c` already matches Darwin's thunk-first signature |
| 7 | CMake rejects `-fno-common` split out of unquoted `-DCMAKE_C_FLAGS` | **fixed, uncommitted** — quote the flags in `llvm/Makefile` |
| 8 | LLVM host build picks ravynOS shim Mach headers and loses `task_get_exception_ports` | **fixed, uncommitted** — add the shim include dir only on non-Darwin hosts |
| 9 | (stopped deliberately) LLVM was configured AArch64-only; target changed to x86_64 | — |
| 10 | **running now** — clean x86_64 toolchain rebuild (`LLVM_TARGETS_TO_BUILD=X86`, triple `x86_64-apple-darwin`) | — |

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
and **passes in ~1 s** on this Mac (commit `8f1b044718`). The stub needs `nasm`
in addition to QEMU/xorriso. `bootcheck.sh` now keeps serial and QEMU logs,
detects early QEMU exit and reaps the process cleanly.

Next Phase 2 step: add the real staging mode to `mkimage.sh` once Phase 1
produces kernel/loader/userland artifacts; the ISO plumbing itself is no
longer the risk.

A first CI slice is also in place: `.github/workflows/pipeline.yml` runs
`tools/pipeline-selftest.sh` on `ubuntu-latest` and `macos-latest` (installing
xorriso/nasm/QEMU), which will exercise the Linux half of portability as soon
as the branch is pushed to the fork. It is unvalidated until that first run.

## 5. Reverse-engineering workspace (`work/`)

| Asset | Status |
|---|---|
| `rea` CLI | ✅ v6.7.0 (Homebrew) |
| Ghidra provider | ✅ **wired & healthy (2026-10-30)**: `GHIDRA_INSTALL_DIR=/path/to/test-ravyn-os/work/ghidra_12.1.4_PUBLIC` exported in `~/.zshrc`; `rea doctor` reports `ghidra`, `ghidra-platform`, `ghidra-architecture` all healthy. Homebrew's Ghidra 12.2 left installed (harmless — env var takes precedence). Remaining doctor warnings are optional engines (Hopper/IDA/JADX/Kuna) and other clients' registrations — not needed |
| `ghidra_12.1.4.zip` | ✅ deleted after verification (reclaimed ~570 MB) |
| Doctrine | Smallest subject that answers the question; need-driven targets only. See `work/README.md` |

No investigation currently open; the first gets nominated by the first real gap Phases 1–2 produce.

## 6. Current blocker / work list (ordered)

1. **[user action]** `gh auth login`, then run `tools/publish-to-github.sh` (or the two manual commands) to create and push the fork; the branch and baseline tag are ready locally.
2. **[Phase 1, active]** Let attempt 10 (clean x86_64 toolchain) run; fix failures in order. Commit the three validated macbuild fixes (libctf `qsort_r`, quoted LLVM C flags, Darwin shim-header scope) plus the `TARGET_ARCH` Makefile change once attempt 10 passes their stage.
3. **[Phase 1, next]** Expect work in LLVM → cctools/xcbuild → SDK → kernel → userland. Keep every patch `uname`/`.MAKE.OS`-guarded and log failure→fix pairs here.
4. **[Phase 2, next]** Stub loop is done; add real staging mode to `mkimage.sh` when kernel/loader artifacts exist. Keep `tools/pipeline-selftest.sh` green as a cheap regression test.
5. No CI in the repo yet (Phase 3).

---

## Changelog

- **2026-10-30 (rev 6) — published:** fork created at https://github.com/Lauel09/ravynos (parent ravynsoft/ravynos), default branch `fork/darwin-dev`, description and topics set. Branch and baseline tag pushed. CI workflow runs green on both Ubuntu and macOS (stub self-test ~3-4 min). `tools/bootview.sh` added for manual inspection.
- **2026-10-30 (rev 4) — Phase 1 build loop + Phase 2 stub proven:** ten `bmake world` attempts; fixed non-root chowns, yacc/lex shims, arm64 `TargetConditionals`, xar OpenSSL/libxml2, libctf `qsort_r`, quoted LLVM CMake flags, and shim-header shadowing. Target arch made explicit (`TARGET_ARCH=x86_64` default, arm64 opt-in). Portable pipeline scaffolded and its stub loop self-tested end to end (PASS). `Docs/BUILDING.md` corrected. Attempt 10 (clean x86_64 toolchain) running at time of writing.
- **2026-10-30 (rev 3) — Phase 0 executed:** all Homebrew prerequisites installed (note: formula is `make`, not `gmake`, on macOS); clone unshallowed to full 745-commit history; `fork/darwin-dev` branch + `baseline-60aa4ff4b` tag created; rea wired to Ghidra 12.1.4 via `GHIDRA_INSTALL_DIR` in `~/.zshrc` (doctor green); redundant Ghidra zip deleted. `bmake -n version` parses; `bmake -n world` dry-run surfaced first two Phase 1 findings (xar libxml2 include path; xar↔libstuff ordering). Pending: `gh auth login` + fork creation (user action).
- **2026-10-30 (rev 2):** Books RE pilot retired and deleted; RE doctrine rewritten (smallest-subject-first). Portable macOS+Linux image pipeline promoted to primary work item. `work/README.md` added.
- **2026-10-30 (rev 1):** Initial audit: repo, build system, macOS host, ISO pipeline, RE workspace assessed; first macOS build failure captured; blockers enumerated; `progress.md` and `roadmap.md` created.

*Next actions live in [`roadmap.md`](roadmap.md). Update this file whenever a blocker clears or a new one is found.*
