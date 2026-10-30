# Building ravynOS From Source

This guide covers the current Darwin-based ravynOS tree. The build is driven by
**BSD make** from the top level; there is no top-level CMake project. CMake and
Ninja are used internally to build the host toolchain (LLVM, cctools, xcbuild).

The build creates a host toolchain first, then uses it to build the kernel, the
ravynOS SDK and the userland. LLVM is the slow part; its build tree is cached
under `<build_dir>` and increments are much faster.

The build directory is `../build` relative to the source checkout (it can be
overridden with `ROOT_BINARY_DIR`, but the default keeps source and objects
separate).

## Host requirements

* clang 17 or newer
* cmake 3.15+ and ninja
* GNU make (`gmake`)
* BSD make (`bmake`)
* Python 3
* OpenSSL development headers
* libxml2, zlib

### macOS

Install the build tools with Homebrew:

```sh
brew install bmake cmake ninja make openssl@3
```

`make` is the Homebrew formula; its binary is `gmake`. `bmake` is the entry
point for this tree.

A full Xcode installation is not required, but the Command Line Tools must be
installed (`xcode-select --install`). The tree's yacc/lex shims fall back to
Homebrew `bison`/`flex` when present.

Build:

```sh
cd /path/to/ravynos
bmake world
```

The target architecture is independent of the host. It defaults to **x86_64**,
including on Apple Silicon, because that is the supported target and the one
the QEMU boot tests use. To try the arm64 experiment:

```sh
bmake TARGET_ARCH=arm64 world
```

### Linux

The Linux host path uses the same BSD make entry point:

```sh
cd /path/to/ravynos
bmake world
```

Install `bmake`, `cmake`, `ninja`, `gmake`, `bison`, `flex`, `csh`, `binutils`,
`libdispatch`, `libxml2`, `openssl`, `zlib` and `lld`. Make `cc`/`c++` point at
clang to avoid accidentally using gcc. The Linux path is kept working as an
invariant; report regressions rather than papering over them with
Darwin-specific changes.

## Portable image pipeline

The FreeBSD-only `ISO/build.sh` is not used on macOS or Linux. The replacement
pipeline is root-free, mount-free and host-neutral:

```sh
tools/mkimage.sh --stub -o stub.iso        # loader-only smoke-test image
tools/mkimage.sh --stub --format both      # ISO + dd-able disk image
tools/bootcheck.sh stub.iso                # QEMU boot verdict
tools/pipeline-selftest.sh                 # the whole loop, end to end
tools/tests/run-tests.sh                   # unit tests, CLI contract, syntax
```

`mkimage.sh` prefers `xorriso` (ISO) and `mtools` (FAT ESP) but ships
dependency-free Python backends, so a host with only Python 3 can still build
and verify images.  `bootcheck.sh` uses `qemu-system-x86_64`.  No mounts,
chroot, `mdconfig`, `zpool` or root privileges are involved — the pipeline
refuses to run as root on purpose.

Full reference: [`Docs/Pipeline.md`](Pipeline.md).  Real-image mode
(`--root DIR`, plus the bootloader decision) is described there and in
[`Docs/BootLoader.md`](BootLoader.md).

## Building on macOS

`bmake world` works directly, but the driver adds host checks, logging, resume
state and a portability audit:

```sh
tools/macbuild.sh --audit            # preflight + static portability audit
tools/macbuild.sh                    # bmake TARGET_ARCH=x86_64 world, logged
tools/macbuild.sh --stage Developer  # one stage
tools/macbuild.sh --dry-run          # parse the makefiles, build nothing
```

See [`Docs/macOS-Host.md`](macOS-Host.md) for prerequisites, the failure
taxonomy and the fix conventions (`macbuild:` commits, host guards, Linux
stays green).

## Troubleshooting

* If the host `yacc`/`lex` shims refuse to run with Command Line Tools, install
  `bison` and `flex` with Homebrew; the top-level Makefile picks them up.
* `xar` needs OpenSSL headers, which the macOS SDK no longer ships. The
  Makefile asks Homebrew for `openssl@3` automatically.
* To start over on the toolchain only, remove
  `<build_dir>/Developer/Platforms/ravynOS.platform/Developer/Toolchains` and
  the matching CMake build directory under `<build_dir>`; a full LLVM rebuild
  is expensive, so keep it cached when possible.
