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

- **Phase 0** — fork/environment bootstrap: done except creating the fork
  remote (`gh auth login`, then `gh repo fork`).
- **Phase 1** — building on macOS: active. Ten `bmake world` attempts logged;
  the host toolchain (LLVM, cctools, xar, libctf, mig, …) is compiling with an
  explicit `x86_64` target cross-built from this arm64 Mac. See
  [`PROGRESS.md`](PROGRESS.md).
- **Phase 2** — portable image pipeline: **proven end to end in stub mode.**
  `tools/mkimage.sh --stub` builds an El Torito ISO and `tools/bootcheck.sh`
  boots it under QEMU and reports a serial marker; `tools/pipeline-selftest.sh`
  runs both and passes in ~1 s on this Mac. Real staging mode waits on Phase 1
  artifacts.
- **Phase 3** — a first CI slice exists:
  [`.github/workflows/pipeline.yml`](.github/workflows/pipeline.yml) runs the
  stub self-test on Ubuntu and macOS.
- **Phases 4–5** — reverse-engineering loop and parity demos: not started;
  doctrine and workspace are in [`work/README.md`](work/README.md) and
  [`ROADMAP.md`](ROADMAP.md).

Full plan and exit criteria: [`ROADMAP.md`](ROADMAP.md).

## Quick start

```sh
# Build (BSD make; default target is x86_64 even on Apple Silicon)
cd /path/to/ravynos
bmake world
# Cross-build the AArch64 experiment instead:
bmake TARGET_ARCH=arm64 world

# Portable pipeline (no root, no mounts)
tools/mkimage.sh --stub -o stub.iso
tools/bootcheck.sh stub.iso
tools/pipeline-selftest.sh
```

Prerequisites on macOS: `brew install bmake cmake ninja make bison flex qemu
xorriso mtools nasm openssl@3`. See [`Docs/BUILDING.md`](Docs/BUILDING.md).

## Repository layout

| Path | Contents |
|---|---|
| `Developer/` | Host toolchain (LLVM, cctools, xcbuild, mig, …) and `ravynOS.sdk` |
| `Kernel/` | XNU kernel and kexts |
| `Frameworks/`, `Libraries/`, `BSD/` | System frameworks, libraries and userland |
| `ISO/` | Legacy FreeBSD-host ISO assembly (superseded by `tools/`) |
| `tools/` | Portable image pipeline and boot smoke tests |
| `work/` | Reverse-engineering evidence and notes (need-driven) |

## Conventions

- Host-specific build fixes: prefix `macbuild:`, one fix per commit, logged in
  `PROGRESS.md`.
- Linux build stays green; host-specific fixes are guarded.
- No investigation starts without a one-sentence gap; every one closes with a
  commit or a documented "no change needed".

## License

This fork inherits the licensing of upstream ravynOS and its components. See
the per-component license files in the tree (for example `ISO/LICENSE`).
