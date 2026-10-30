# RavynOS Fork — Roadmap

**Last updated:** 2026-10-30 (rev 4: portable pipeline complete through real staging + .img; CI matrix; macOS build driver and portability audit)
**Mission:** A macOS-first fork of RavynOS that (a) builds on this MacBook, (b) ships a **portable** one-command pipeline — source → bootable IMG/ISO → automated boot smoke test — that runs identically on macOS and any suitably equipped Linux box, and (c) improves OS fidelity by reverse-engineering Apple behavior on this machine with [rea](https://github.com/morluto/rea), strictly need-driven and smallest-subject-first.

**Guiding rules**

- Never break the Linux build; macOS support is *additive*. Host-specific fixes are `uname`/`.MAKE.OS`-guarded.
- **The image pipeline must stay portable**: no FreeBSD-only tools, no root, no mounts, no chroot. Anything that needs those gets redesigned or contained behind an optional path.
- Every phase ends with something verifiable (a command that exits 0, an artifact that boots, a written conclusion).
- RE is a means, not the product: no investigation starts without a concrete gap it answers, and each ends in a code change or a documented "no change needed". Base layers that are open source (XNU, dyld, launchd, CoreFoundation, libdispatch) get read, not decompiled.

---

## Phase 0 — Fork & environment bootstrap *(complete except fork remote)*

**Goal:** turn the workspace into a real fork with a complete, pipeline-ready Mac.

1. **Create our fork**
   - `gh repo fork ravynsoft/ravynos --clone=false`, then in `ravynos/`: `git remote add fork <our-fork-url>`.
   - Fetch full history (`git fetch --unshallow origin` — today's clone is depth 1), branch `fork/darwin-dev` off `darwin`, tag the baseline: `git tag baseline-60aa4ff4b`.
   > ✅ Done 2026-10-30 (see `progress.md` rev 3) — branch/tag exist; only the fork remote awaits `gh auth login`.
2. **Install host prerequisites** (Homebrew on macOS, apt/dnf equivalents on Linux) — ✅ done 2026-10-30:
   ```
   brew install bmake cmake ninja make      # build prerequisites ("make" = GNU make as gmake; gmake is NOT a formula name on macOS)
   brew install bison flex                  # yacc/lex work without a full Xcode install
   brew install qemu xorriso mtools nasm    # image pipeline + boot smoke tests
   ```
   Optionally `brew install python@3.12` if xcbuild rejects the system 3.9.
3. **Fix the rea ↔ Ghidra version mismatch** — ✅ done 2026-10-30: `export GHIDRA_INSTALL_DIR=/path/to/test-ravyn-os/work/ghidra_12.1.4_PUBLIC` (added to `~/.zshrc`); `rea doctor` Ghidra checks green; redundant 570 MB zip deleted.
4. **Sanity-check the BSD make entry point:** `cd ravynos && bmake -n version` must parse without GNU make's `missing separator` error. — ✅ done 2026-10-30.

**Exit criteria:** fork remote exists and `fork/darwin-dev` pushes; `bmake -n` parses; `rea doctor` green; `qemu-system-x86_64`, `xorriso`, and `mtools` all run.

## Phase 1 — Make it build on macOS

**Goal:** `bmake world` completes on this MacBook.

**Status (2026-10-30, rev 4):** toolchain milestone reached — `bmake Developer`
exits 0 on macOS. The loop is now tooled rather than manual: `tools/macbuild.sh`
runs preflight checks (bmake/gmake/cmake/ninja/bison/flex, Xcode path, case-
sensitive filesystem, free space), logs the build, records per-stage state so an
interrupted LLVM build is resumable, and prints the next actions on failure.
`tools/macbuild-audit.py` statically finds the host assumptions that break a
build (non-root chowns, FreeBSD-only tools, GNU-only flags, host-arch leakage)
before one is attempted, and is clean at `--severity high` over the build
surface. Remaining: SDK → kernel → userland. Failure→fix table: `PROGRESS.md`
§3.2; conventions: `Docs/macOS-Host.md`.

Approach: run the build, fix failures host-by-host, keep every patch guarded so Linux is unaffected. Expected work areas (in likely order):

1. **Arch decision first.** ✅ **Decided and implemented:** `TARGET_ARCH ?= x86_64` is host-independent and maps to `BuildArch=X86`, `CpuArch=x86_64`, `ARCH_CONFIGS=X86_64` (cross-build from this arm64 Mac; upstream's supported target and the one QEMU boots). `bmake TARGET_ARCH=arm64 world` remains the tracked AArch64 experiment. This replaced the old host-`MACHINE` auto-selection, which produced an AArch64 toolchain while `ARCH_CONFIGS` stayed X86_64.
2. **Toolchain build (`Developer/`)** — LLVM + cctools + xcbuild via cmake/ninja; hours on M-series, cache it. Watch for: Darwin-host assumptions in `bsd.compiler.mk`/`bsd.linker.mk`, Apple vs. open-source clang flag differences, cctools `ar`/`ranlib` vs. host, install-name/`@rpath` issues.
3. **Kernel (`Kernel/xnu`) & kexts** — mig/iig from the toolchain; expect header/search-path fixes. The kernel product never runs on macOS — only the build must succeed.
4. **Frameworks / Libraries / BSD userland** against `ravynOS.sdk`. If case-insensitive APFS collides with kernel/userland sources, build inside a case-sensitive sparse bundle (`hdiutil create -type SPARSE -fs "Case-sensitive APFS"`).
5. **Cherry-pick upstream** before starting — after unshallowing, check whether our failures are already fixed upstream.

Conventions: one fix per commit, prefix `macbuild:`, log every failure→fix pair in `progress.md`.

**Exit criteria:** `bmake world` (or a documented `tools/macbuild.sh` subset) exits 0 on macOS with kernel + SDK + userland artifacts under `../build`; Linux build unaffected.

## Phase 2 — Portable image & boot pipeline *(primary work item)*

**Goal:** `tools/mkimage.sh` + `tools/bootcheck.sh` run **unchanged on macOS and Linux**, no root, producing `ravynos-<version>-<git-short>.iso` (+ `.img`) and a pass/fail boot verdict. Work starts in parallel with Phase 1 — the pipeline can be scaffolded and validated with stub content (loader-only image, even a foreign kernel) before the full OS build is green, so QEMU-verdict plumbing is proven early.

**Status (2026-10-30, rev 4): done except real-image content.** Every deliverable
below exists and is tested:

* `tools/mkimage.sh` — stub, milestone-stub and **real staging (`--root`)** modes;
  ISO **and** `.img` (`--format both`); explicit backends (`xorriso`, `mtools`,
  or dependency-free `python`); manifests, `.sha256`, and a machine-readable
  build record. Refuses to run as root; never mounts, chroots or needs
  FreeBSD-only tools.
* Dependency-free writers — `tools/image/iso9660.py` (ISO 9660 + El Torito, BIOS
  and UEFI entries, level 1/2 names, both path tables) and
  `tools/image/fat.py` (FAT16 + primary/backup GPT, VFAT long names), so the
  pipeline runs on a host with nothing but Python 3. `tools/image/bootstub.py`
  assembles the boot stub without nasm.
* `tools/image/verify.py` — independent structural verification in milliseconds
  (ISO descriptors, directory tree, El Torito checksum and extents; GPT CRCs and
  FAT chains), so CI gates on structure before spending time on QEMU.
* `tools/bootcheck.sh` — multi-marker milestone chain, KVM/HVF/TCG selection,
  OVMF/`.img` and `--kernel` direct boot, screenshots, JSON verdict, logs always
  preserved.
* `tools/tests/run-tests.sh` + `tools/tests/test_images.py` — 30 unit tests plus
  a CLI contract, shell-syntax and self-test layer; runs with no host tools.
* Reproducibility — images are a function of (tree, volume id, commit epoch);
  asserted by a CI job on both host families.

Remaining for the exit criterion: a real staged sysroot to package (Phase 1) and
a loader that emits the milestone markers (`Docs/BootLoader.md`).

The current `ISO/build.sh` is FreeBSD-host-only and requires root: `mdconfig`, `zpool`/`gpart`, `mkuzip`, `makefs`, `mkimg`, `newfs_msdos`, `mount msdosfs`, `chroot`, `etdump`. We do **not** port that script — we replace it with a portable pipeline built on tools that exist via brew *and* standard Linux package managers:

| FreeBSD-only step (today) | Portable replacement | Notes |
|---|---|---|
| `makefs` (ISO) | `xorriso` | Hybrid ISO; embeds the boot program directly |
| `mdconfig` + `newfs_msdos` + `mount -t msdosfs` (ESP image) | `mtools` (`mformat`/`mcopy`) | Formats and populates a FAT image file **without mounting** — the key to root-free, host-independent operation |
| `zpool`/`zfs`/`mkuzip` (system.uzip) | **dropped** | Plain directory tree staged into the image; optional squashfs later iff the kernel grows support |
| `makefs` UFS + `gzip` (ramdisk) | ramdisk template or tar/cpio payload | Exact format decided by what our loader/kernel consumes — implementation-time decision, recorded in `tools/mkimage.sh` docs |
| `chroot … pw useradd` (live user) | static overlay files | Generate `master.passwd`/`group` entries directly from templates in `ISO/overlays/`; no chroot anywhere |
| `gpart` (GPT, for `.img`) | prebuilt image template or `sgdisk`/`parted` | Only needed for the dd-able `.img` variant; ISO path needs none |
| `etdump` (El Torito verify) | `xorriso` internal checks | Optional verification step |

Deliverables:

1. `tools/mkimage.sh` — stages the built tree (kernel, loader, ramdisk, overlays) and emits the ISO/IMG. Deterministic inputs → reproducible file names including git short SHA. Dies loudly if a FreeBSD-only tool would be needed.
2. `tools/bootcheck.sh` — QEMU smoke test: `qemu-system-x86_64 -m 2G -cdrom <iso> -nographic -serial file:serial.log` with a timeout; grep the serial log for boot milestones (loader → kernel banner → root mount → userspace marker); print a one-line verdict, exit 0/1. Always keep `serial.log` and a screenshot for failures.
3. Optional `tools/bootview.sh` — same boot with a display, for manual inspection.
4. Once proven, propose the portable pipeline upstream — it removes FreeBSD-host+root from everyone's release path, not just ours.

**Exit criteria:** from a clean checkout on **both** macOS and a Linux box (CI container counts), `tools/mkimage.sh && tools/bootcheck.sh` runs without root and prints a boot verdict; incremental wall time small enough for per-iteration use (target < ~15 min).

## Phase 3 — CI pipeline

**Goal:** every push gets built and boot-verdicted automatically — on both host families, which Phase 2's portability makes cheap.

**Status (2026-10-30, rev 4): three workflows written, pending first CI run.**
`pipeline.yml` runs unit tests, per-backend structure checks (python/xorriso/
mtools), cross-host reproducibility and real QEMU boots on Ubuntu + macOS;
`build.yml` chains `bmake world` → `mkimage` → `bootcheck` with a toolchain cache
keyed on `Developer/**`, per-push activation gated on the `BUILD_ON_PUSH`
repository variable until M1 is green; `nightly.yml` rebuilds from scratch and
files an issue on failure. A reusable `install-host-tools` action keeps the
matrices consistent.

1. GitHub Actions `.github/workflows/build.yml`:
   - Job 1 **build** — matrix: `macos-latest` + `ubuntu-latest`. Cache the toolchain dir keyed on a hash of `Developer/`; keep Linux green as the invariant.
   - Job 2 **image** — runs the portable `tools/mkimage.sh` on the Linux runner (fast, cheap) using artifacts from job 1.
   - Job 3 **bootcheck** — `tools/bootcheck.sh` against the produced ISO; publish ISO + serial log as artifacts; PR status reflects the boot verdict.
2. Nightly full-from-scratch build to catch cache rot.
3. Later: boot-verdict badge, per-PR artifact retention policy.

**Exit criteria:** a PR that fails to build *or boot* shows red on both runners; ISO downloadable from the run; routine incremental builds tractable via cache hits.

## Phase 4 — RE-driven improvement loop (rea) — need-driven, smallest-subject-first

**Goal:** a repeatable loop that turns a *specific observed gap* into a fix in the fork, using this MacBook as ground truth and rea (Ghidra 12.1.4) as the microscope.

**Doctrine (settled):** no standing big-target pilots. Targets are nominated by Phase 1/2/3 failures and compat gaps, and each investigation uses the smallest subject that answers the question — a tiny self-authored probe binary beats an app-plus-framework stack every time. The earlier Books pilot was retired for exactly this reason (see `work/README.md`).

The loop:

1. **Nominate a gap** — from a build error, boot failure, API mismatch, or TODO in-tree. One sentence, written down first.
2. **Pick the smallest subject**: in order of preference —
   a. **Self-authored probe**: a 50-line Mac app/CLI we write to exercise exactly the API/behavior in question; observe it on macOS; rea-analyze *our own binary* plus the Apple framework path it hits (dispatch tables, call refs, pseudocode).
   b. **A single small Apple binary** when cloning one utility/daemon's behavior.
   c. **Data formats**: plist/keyed-archive/bundle-layout edge cases via tiny sample files.
   d. **Apple IPS crash reports** (rea capability) when a behavior question surfaces via failure.
3. **Analyze with rea** into `work/<topic>/evidence/*.json`; record provenance (source path on this Mac + sha256).
4. **Conclude** in `work/<topic>/NOTES.md`: question, evidence ids, answer, and the exact fork change it justifies.
5. **Implement** in `Frameworks/`/`Libraries/`/kernel, commit referencing the note (`re: work/<topic>`).
6. **Verify** — compiles via Phase 1; behavior re-checked via the probe once Phase 2 boots userspace; outcome logged in `progress.md`.

Standing rules: only analyze software we own/are entitled to inspect; open-source components get read at source, not decompiled; every investigation closes with a commit or a documented no-change.

**Exit criteria (ongoing):** first completed cycle = one gap → one probe → one note → one merged fix.

## Phase 5 — System fidelity & parity (longer term)

- Boot to userspace reliably in QEMU; then on real x86_64 hardware.
- Run real macOS software end-to-end — start source-compat (compile a Mac app against `ravynOS.sdk`), then binary-compat investigations.
- arm64/arm64e bring-up branch (natural fit with this arm64e host).
- Upstream contribution pass: split generally-useful `macbuild:` fixes and the portable pipeline into PRs to `ravynsoft/ravynos`.

---

## Milestones & exit criteria (summary)

| # | Milestone | Verifiable exit |
|---|---|---|
| M0 | Fork + Mac env ready | push to fork works; `bmake -n` parses; `rea doctor` green; xorriso/mtools/qemu present |
| M1 | Builds on macOS | `bmake world` (or `tools/macbuild.sh`) exit 0 |
| M2 | **Portable image pipeline** | `tools/mkimage.sh` + `tools/bootcheck.sh` give a boot verdict, root-free, on macOS **and** Linux — **stub proven; real-image content pending M1 + loader** |
| M3 | CI | PRs auto-built on mac+linux; ISO artifact + boot verdict on every push |
| M4 | RE loop live | one gap → probe → note → merged fix, full evidence trail |
| M5 | Parity demos | a real Mac app runs under ravynOS (QEMU or hardware) |

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| LLVM/toolchain build breaks in many places on Darwin host | Fix incrementally; cache aggressively; consult upstream history after unshallowing |
| Case-insensitive APFS collides with kernel/userland sources | Case-sensitive sparse bundle for the build dir |
| Loader/kernel expects UFS-convolved artifacts we can't produce portably | Decide ramdisk payload format at implementation time; template + cpio/tar paths available; worst case a tiny portable formatter lives in `tools/` |
| Cross-build (arm64 host → x86_64 target) unsupported in tree | Confine to explicit `ARCH_CONFIGS`/triple variables; native-arm64 branch as fallback |
| Long feedback loops kill iteration speed | Bootcheck on a loader/minimal image for the inner loop; full ISO on CI/nightly |
| Upstream drift vs. fork divergence | Regular fetch/rebase cadence; contribute generic fixes and the portable pipeline back (Phase 5) |
| RE scope creep | Doctrine is fixed: no target without a one-sentence gap; smallest subject first; every investigation closes |

## Quick start (what to run right now)

```
# Phase 0 — ✅ complete except the fork remote. To finish it:
gh auth login
gh repo fork ravynsoft/ravynos --clone=false
cd /path/to/test-ravyn-os/ravynos && git remote add fork <your-fork-url>

# Phase 1 — build loop; fix top-down, prefix commits "macbuild:".
# The driver adds preflight, logging and resume; TARGET_ARCH defaults to x86_64.
cd /path/to/ravynos && tools/macbuild.sh                 # or: bmake world
tools/macbuild.sh --audit --stage Developer              # static hazard check

# Phase 2 — the portable pipeline.
tools/mkimage.sh --stub --format both -o out/stub        # ISO + disk image
tools/mkimage.sh --root ../build/stage --format both \
    --boot-image BOOT/LOADER --efi-image EFI/BOOT/BOOTX64.EFI --verify
tools/bootcheck.sh out/stub.iso --expect RAVYNOS-BOOTCHECK-STUB-OK
tools/pipeline-selftest.sh                               # whole loop
tools/tests/run-tests.sh                                 # unit + CLI tests
```
