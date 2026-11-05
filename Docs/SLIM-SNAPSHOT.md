# Slim snapshot: what was trimmed, and how to restore it

This repository is a single-commit snapshot of the
[ravynOS](https://github.com/ravynsoft/ravynos) tree (plus the macOS host-build
and boot-pipeline work from the `arena/*` branches of `Lauel09/ravynos`, tip
`cb9dce7a36`), trimmed to fit a small repository size limit. Two things are
missing; one must be restored from upstream before the toolchain can build, the
other should never be restored.

## 1. Vendored LLVM source (`Developer/Default.xctoolchain/llvm/`)

This directory holds the LLVM/clang source used to build the host toolchain.
Without it, `bmake Developer` (and therefore `bmake world`) cannot start.

Restore it from the upstream `darwin` branch (baseline `60aa4ff4b`):

```sh
git remote add upstream https://github.com/ravynsoft/ravynos
git fetch upstream darwin
git checkout upstream/darwin -- Developer/Default.xctoolchain/llvm
```

Then follow `Docs/BUILDING.md` (or `Docs/macOS-Host.md` for the macOS driver) as
usual.

## 2. `BSD/lib/ncurses/obj_s/` (~2.9 GB)

GCC RTL debug dumps (`*.ce1`, `*.reload`, …) that upstream committed by
accident. They are not source and are not needed; do **not** restore them.

## What is included

Everything else — `Kernel/`, `Frameworks/`, `Libraries/`, `BSD/`, `Developer/`
(minus LLVM), `CoreServices/`, `SysApps/`, `SystemLibrary/`, `tools/`, `Docs/`,
`ISO/` — is present. Entry points: `Docs/BUILDING.md`; macOS driver
`Docs/macOS-Host.md`; portable image pipeline `Docs/Pipeline.md`.
