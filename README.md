# RavynOS Fork — macOS-first, portable pipeline, need-driven RE

This repository is a **fork of [ravynsoft/ravynos](https://github.com/ravynsoft/ravynos)**,
branched from the upstream `darwin` branch at `60aa4ff4b` ("OpenGL and
CoreText frameworks"). The default working branch here is `fork/darwin-dev`.

## Why this fork

RavynOS is the "new generation" RavynOS: an XNU-kernel, Darwin-layout system.
This fork exists to do three things, in order:

1. **Build on macOS.** The upstream tree is documented for Linux/FreeBSD-style
   hosts and assumes a full Xcode install. This fork adds guarded, host-specific
   fixes so `bmake world` completes on an Apple Silicon Mac without breaking the
   Linux build. Every such fix is prefixed `macbuild:` and guarded with
   `uname`/`.MAKE.OS` checks.
2. **Ship a portable pipeline.** A root-free, mount-free, chroot-free one-command
   path from source to a bootable IMG/ISO to an automated boot verdict, using
   only `xorriso`, `mtools`, `qemu` and `nasm` — identical on macOS and Linux.
   It replaces the upstream FreeBSD-host-only, root-requiring `ISO/build.sh`.
3. **Improve OS fidelity by reverse engineering, need-driven and
   smallest-subject-first.** Investigations are nominated only by concrete
   gaps found while building or booting; each ends in a code change or a
   documented "no change needed". Base layers that are open source (XNU, dyld,
   launchd, CoreFoundation, libdispatch) are read at source, never decompiled.

## Current status

- **Phase 0** — fork/environment bootstrap: complete.
- **Phase 1** — building on macOS: active. The host toolchain milestone is
  done; the failure→fix log lives in [`PROGRESS.md`](PROGRESS.md) §3.2 and the
  conventions in [`Docs/macOS-Host.md`](Docs/macOS-Host.md).
  `tools/macbuild.sh` drives the loop (preflight, logging, per-stage resume,
  `--audit`), and `tools/macbuild-audit.py` statically finds the host
  assumptions that break a build before one is attempted.
- **Phase 2** — portable image pipeline: **working, with real staging mode.**
  `tools/mkimage.sh` builds El Torito ISOs (BIOS and/or UEFI entries) and
  GPT/FAT ESP disk images, root-free, from a stub *or* a staged sysroot, with
  xorriso/mtools *or* dependency-free Python backends. `tools/bootcheck.sh`
  turns a QEMU boot into a multi-milestone verdict with logs, screenshots and
  JSON. `tools/image/verify.py` validates image structure independently of the
  writers. Reference: [`Docs/Pipeline.md`](Docs/Pipeline.md).
- **Phase 3** — CI: [`pipeline.yml`](.github/workflows/pipeline.yml) runs
  tests, per-backend structure checks, reproducibility and real QEMU boots on
  Ubuntu and macOS; [`build.yml`](.github/workflows/build.yml) and
  [`nightly.yml`](.github/workflows/nightly.yml) chain `bmake world` →
  `mkimage` → `bootcheck`.
- **Phase 4** — reverse-engineering loop: workspace and templates are in
  [`work/README.md`](work/README.md); no investigation is open until Phase 1/2
  nominate a real gap.
- **Phase 5** — parity demos: not started.

Full plan and exit criteria: [`ROADMAP.md`](ROADMAP.md).

## Quick start

```sh
# Build (BSD make; the target is x86_64 even on Apple Silicon)
cd /path/to/ravynos
tools/macbuild.sh                 # preflight + bmake world, logged and resumable
bmake world                       # or run bmake directly

# Portable pipeline (no root, no mounts)
tools/mkimage.sh --stub -o stub.iso
tools/bootcheck.sh stub.iso
tools/pipeline-selftest.sh        # the whole loop
tools/tests/run-tests.sh          # unit tests + CLI contract

# A real image once a staged sysroot exists
tools/mkimage.sh --root ../build/stage --format both -o out/ravynos --verify
```

Prerequisites on macOS: `brew install bmake cmake ninja make bison flex qemu
xorriso mtools nasm openssl@3 libxml2 zlib`. See
[`Docs/BUILDING.md`](Docs/BUILDING.md).

## Repository layout

| Path | Contents |
|---|---|
| `Developer/` | Host toolchain (LLVM, cctools, xcbuild, mig, …) and `ravynOS.sdk` |
| `Kernel/` | XNU kernel and kexts |
| `Frameworks/`, `Libraries/`, `BSD/` | System frameworks, libraries and userland |
| `ISO/` | Legacy FreeBSD-host ISO assembly (superseded by `tools/`) |
| `tools/` | Portable image pipeline, image writers/verifier, boot smoke tests, macOS build driver |
| `tools/image/` | Dependency-free ISO 9660/El Torito, FAT16/GPT writers and the structural verifier |
| `work/` | Reverse-engineering workspace: doctrine, templates, evidence (need-driven) |
| `Docs/` | Build, pipeline, bootloader and macOS-host documentation |

## Conventions

- Host-specific build fixes: prefix `macbuild:`, one fix per commit, logged in
  `PROGRESS.md`.
- Linux build stays green; host-specific fixes are guarded.
- No investigation starts without a one-sentence gap; every one closes with a
  commit or a documented "no change needed".

## License

This fork inherits the licensing of upstream ravynOS and its components. See
the per-component license files in the tree (for example `ISO/LICENSE`).

---

## About this slim snapshot

This is a single-commit snapshot derived from the [ravynOS](https://github.com/ravynsoft/ravynos) project (including the macOS host-build and boot-pipeline work from the `arena/*` branches of Lauel09/ravynos, tip `cb9dce7a36`), trimmed to fit small repository size limits. Left out:

- `Developer/Default.xctoolchain/llvm/` — vendored LLVM source. **It must be fetched from upstream before building the toolchain** — see [`Docs/SLIM-SNAPSHOT.md`](Docs/SLIM-SNAPSHOT.md).
- `BSD/lib/ncurses/obj_s/` — ~2.9 GB of GCC RTL debug dumps (`*.ce1`, `*.reload`, ...) that upstream committed by accident; not source and not needed.

Full history and the complete tree live upstream.
