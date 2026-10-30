# Building on a macOS host

This is the operating manual for Phase 1: making `bmake world` complete on a
Mac without breaking the Linux build.

The short version:

```sh
brew install bmake cmake ninja make bison flex openssl@3 libxml2 zlib \
             xorriso mtools nasm qemu
tools/macbuild.sh --audit --target-arch x86_64
```

`tools/macbuild.sh` runs the host checks, then exactly `bmake TARGET_ARCH=… world`,
teeing the output to a log and recording per-stage state so an interrupted run
is resumable.

---

## 1. Host prerequisites

| Tool | Why | Install |
|---|---|---|
| Xcode Command Line Tools | the host C/C++ compiler | `xcode-select --install` |
| `bmake` | the tree's entry point is BSD make | `brew install bmake` |
| `gmake` | LLVM's configure step needs GNU make | `brew install make` (binary is `gmake`) |
| `cmake`, `ninja` | the toolchain sub-builds | `brew install cmake ninja` |
| `bison`, `flex` | the CLT `yacc`/`lex` shims refuse to run without full Xcode | `brew install bison flex` |
| `openssl@3` | `xar` needs OpenSSL headers the SDK no longer ships | `brew install openssl@3` |
| `libxml2`, `zlib` | `xar`, libctf | `brew install libxml2 zlib` |
| `xorriso`, `mtools`, `nasm` | portable image pipeline | `brew install xorriso mtools nasm` |
| `qemu` | boot verdicts | `brew install qemu` |
| Python 3 | pipeline backends and the audit | system Python is fine |

A full Xcode install is not required, but several failures below are *caused*
by having only the Command Line Tools. `tools/macbuild.sh` warns about each.

---

## 2. Target architecture

The target is always explicit and host-independent:

```sh
bmake world                    # TARGET_ARCH defaults to x86_64
bmake TARGET_ARCH=arm64 world  # the AArch64 experiment
tools/macbuild.sh --target-arch arm64
```

Do not let `uname -m` leak into the choice: an Apple Silicon host that
auto-selects `arm64` while the kernel config stays `X86_64` produces a toolchain
that cannot build the tree. `tools/macbuild-audit.py` flags exactly that pattern
(`arch-assumption`).

---

## 3. The build loop

One iteration is: **build → first error → smallest guarded fix → commit → log**.

```sh
tools/macbuild.sh                 # world, logged, resumable
tools/macbuild.sh --stage Developer
tools/macbuild.sh --dry-run       # parse the makefiles only
tools/macbuild.sh --stage world --keep-going
```

After a failure it prints the tail of the log and the four next actions
(find the first `Stop.`, guard the fix, commit with the `macbuild:` prefix, log
the pair). The state file `<build>/.macbuild-state` records when each stage
started, succeeded or failed, which is what makes a six-hour LLVM rebuild
survivable.

### Conventions for a fix

1. **Additive, never substitutive.** macOS support is added *around* the
   existing logic; Linux must keep building unchanged.
2. **Guard on the host**, using `.MAKE.OS` in makefiles and `uname -s` in
   scripts. Never assume a fix is universal because it is right on a Mac.
3. **One fix per commit**, prefixed `macbuild:`, with the failure text quoted
   in the message — the log line is the useful part six months later.
4. **Record it** in `PROGRESS.md` §3.2 as a failure → fix row, with the commit
   hash.
5. **Prefer not patching vendored code.** LLVM, cctools and xcbuild are third
   party; a change there is a future rebase conflict. Exhaust local options
   (flags, environment, a shim header) first, and when a vendored patch is
   unavoidable, say why in the commit and keep it minimal.

---

## 4. Failure classes seen so far

These are the eight distinct host assumptions fixed during the first build
attempts, grouped by cause. They recur, so the pattern matters more than the
individual fix.

| Class | Symptom | Fix pattern |
|---|---|---|
| **Non-root install** | `chown root:wheel` fails | drop the chown, or guard it on `id -u` |
| **CLT-only shims** | `yacc: ... unable to find a suitable SDK` | use Homebrew `bison -y` / `flex` when present |
| **SDK header gaps** | `openssl/evp.h` not found | add Homebrew include/lib paths, guarded on Darwin |
| **Shim header shadowing** | host build loses a system function (`task_get_exception_ports`) | exclude the ravynOS shim include directory from *host* compiler flags only |
| **Darwin libc declarations** | `reallocf`, `qsort_r` conflicts or missing | provide a local declaration, or use the POSIX equivalent |
| **Missing TAPI naming** | `libtapi.so` vs `libtapi.dylib` | match the host's dynamic-library suffix |
| **CMake flag splitting** | `-fno-common` rejected as a malformed argument | quote whole flag strings in `-DCMAKE_*_FLAGS` |
| **Host/target arch mismatch** | AArch64 toolchain, `ARCH_CONFIGS=X86_64` | make `TARGET_ARCH` explicit (done) |

`PROGRESS.md` §3.2 keeps the authoritative numbered list with commit hashes;
this table is the taxonomy.

---

## 5. The portability audit

Before trying a build, ask the tree what it thinks of the host:

```sh
python3 tools/macbuild-audit.py                  # everything, grouped by file
python3 tools/macbuild-audit.py --severity high  # CI gate
python3 tools/macbuild-audit.py --json > audit.json
python3 tools/macbuild-audit.py --list-checks
```

It reports, statically:

| Check | Severity | Meaning |
|---|---|---|
| `chown-root` | high | privileged ownership change in a build path |
| `freebsd-tool` | high | `mdconfig`, `zpool`, `gpart`, `makefs`, `mkimg`, … |
| `gnu-only-flag` | high | `readlink -f`, `stat -c`, `date -d`, `sed -i` without suffix, … |
| `bsd-only-flag` | medium | `stat -f`, `md5 -r` |
| `arch-assumption` | medium | `uname -m` choosing a target |
| `xcode-shim` | low | `/usr/bin/yacc`, `xcrun xcodebuild` |
| `hardcoded-path` | low | MacPorts/Homebrew prefixes read without a probe |
| `shell-portability` | low | non-POSIX `sh` constructs |

Findings in vendored upstream trees, interactive developer targets, test
suites, install-time overlay scripts and the retired `ISO/build.sh` are skipped
by design; see `SKIP_PATTERNS` and `ALLOWLIST` in the script. A deliberate,
portable exception is marked inline:

```sh
# audit:allow gnu-only-flag (deliberate fallback chain)
stat -f%z "$1" 2>/dev/null || stat -c%s "$1"
```

`tools/macbuild.sh --audit` fails the run on any high finding, so CI can gate on
it once the remaining tree is clean.

---

## 6. Host hazards outside the compiler

* **Case-insensitive APFS.** Kernel and userland sources contain names that
  differ only by case. If the build fails with impossible-looking "file exists"
  or wrong-file errors, move the checkout to a case-sensitive volume:
  `hdiutil create -type SPARSE -fs "Case-sensitive APFS" -size 200g build.sparse`.
  `tools/macbuild.sh` probes for this and warns.
* **Disk space.** A clean LLVM build needs on the order of 20 GiB next to the
  build directory; the driver warns when free space is below that.
* **Toolchain cache.** `<build>/Developer` is the expensive artifact. Keep it
  between runs; delete only `<build>/Developer/Platforms/.../Toolchains` when
  you need a real toolchain rebuild.
* **`TMPDIR`.** Long paths break some toolchain steps; keep `TMPDIR` short if
  you see "file name too long" from CMake.
* **Homebrew on Apple Silicon** lives in `/opt/homebrew`, not `/usr/local`. Any
  new probe must ask `brew --prefix`, not assume a path.

---

## 7. When it works

`bmake world` exits 0 and leaves artifacts under `../build`. That is M1. The
next steps are Phase 2: stage a sysroot, build a real image, and let the boot
verdict — not the build log — say whether the system works
(`Docs/Pipeline.md`).
