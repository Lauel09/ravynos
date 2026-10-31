# The portable image pipeline

`tools/mkimage.sh` + `tools/bootcheck.sh` turn a build tree into a bootable
image and a pass/fail verdict, **on macOS and on Linux, without root**.

This document is the reference for that pipeline: what it replaces, how the
pieces fit, what the boot verdict actually means, and how to debug it when
something goes wrong.

---

## 1. Why a new pipeline

Upstream's `ISO/build.sh` is FreeBSD-host-only and requires root. Every step it
takes needs either a kernel facility (loop devices, ZFS, GEOM) or a
FreeBSD-only program, and several steps mount filesystems. None of that is
available on a Mac, and requiring root makes the pipeline unusable in CI.

| Upstream step (`ISO/build.sh`) | Portable replacement | Where |
|---|---|---|
| `makefs` for the ISO | `xorriso -as mkisofs` | `tools/mkimage.sh` |
| `mdconfig` + `newfs_msdos` + `mount -t msdosfs` for the ESP | `mformat`/`mcopy`, or the built-in FAT16 writer | `tools/image/fat.py` |
| `zpool`/`zfs`/`mkuzip` for `system.uzip` | dropped: the tree is staged plainly | — |
| `makefs` UFS + `gzip` ramdisk | not produced; the payload format is a loader decision | `Docs/BootLoader.md` |
| `chroot … pw useradd` for the live user | static overlay files generated from templates | `ISO/overlays/` |
| `gpart` (GPT) | the built-in GPT writer (mtools only fills the FAT) | `tools/image/fat.py` |
| `etdump` for El Torito verification | the built-in structural verifier | `tools/image/verify.py` |
| `install -o root` chowns | none: output is owned by the invoking user | — |

`mkimage.sh` refuses to run as root. That is deliberate: a pipeline that
silently depends on privileges will eventually be run as root and hide the bug.

---

## 2. Architecture

```
             ┌──────────────┐
  sources →  │ bmake world  │ → ../build/{Developer,Kernel,Libraries,...}
             └──────┬───────┘
                    │  (a staged sysroot: tools/macbuild.sh --stage)
                    ▼
             ┌──────────────┐   copy, reject symlinks/specials
             │  staging dir │ ──────────────────────────────┐
             └──────┬───────┘                               │
        ┌───────────┴───────────┐                           │
        ▼                       ▼                           ▼
  ┌───────────┐          ┌────────────┐            ┌────────────────┐
  │ ISO 9660  │          │ GPT + FAT  │            │ manifest +     │
  │ El Torito │          │   ESP      │            │ sha256 + json  │
  └─────┬─────┘          └─────┬──────┘            └────────────────┘
        │                      │
        ▼                      ▼
  tools/image/verify.py  (structure, no QEMU)
        │
        ▼
  tools/bootcheck.sh  → QEMU → serial log → milestones → verdict (0/1)
```

Every writer has two implementations:

| Format | Preferred | Fallback | Notes |
|---|---|---|---|
| ISO | `xorriso` | `tools/image/iso9660.py` | The Python backend is a complete ISO 9660 + El Torito writer: PVD, both path tables, level 2 or 8.3 identifiers, BIOS and UEFI catalog entries. |
| IMG | `mtools` (FAT) + built-in GPT writer | `tools/image/fat.py` | The Python backend writes FAT16 + primary/backup GPT itself; nothing is mounted. |

Selection is automatic (`--backend auto`) and can be forced with
`--backend xorriso|python|mtools`. The fallbacks exist for three reasons:

1. CI runners and minimal containers often lack xorriso/mtools.
2. A dependency-free path makes the pipeline auditable: the format layout is
   ours, in ~700 readable lines, instead of hidden in a C tool.
3. When a format bug appears, having an independent *verifier* and a
   from-scratch writer makes bisecting the problem possible without guessing
   at xorriso's flags.

Both writers are validated against each other: `tools/tests/test_images.py`
builds with every backend and parses the result with `tools/image/verify.py`,
which is written independently of both.

---

## 3. Commands

### 3.1 Building an image

```sh
# Loader-only smoke-test image (no OS content)
tools/mkimage.sh --stub -o out/stub.iso

# Four-stage milestone stub (exercises the full bootcheck verdict path)
tools/mkimage.sh --stub-milestones -o out/milestones.iso

# A real staged sysroot, ISO + dd-able disk image, structurally verified
tools/mkimage.sh --root ../build/stage \
    --boot-image BOOT/LOADER --efi-image EFI/BOOT/BOOTX64.EFI \
    --format both -o out/ravynos-0.7.1 --verify

# The UEFI loader on its own (Step B of Docs/BootLoader.md)
tools/bootloader/build.sh -o stage/EFI/BOOT/BOOTX64.EFI
tools/mkimage.sh --root stage --format both \
    --efi-image EFI/BOOT/BOOTX64.EFI -o out/uefi --verify
tools/bootcheck.sh out/uefi.img --uefi --expect RAVYNOS-LOADER-OK

# What would happen, without doing it
tools/mkimage.sh --root ../build/stage --print-plan

# Which host tools are available
tools/mkimage.sh --list-tools
```

`--efi-image` is validated before anything is written: the file must be a
PE32+ EFI application (`tools/image/pe.py`), and the El Torito sector count that
the firmware reads is taken from its size (`--efi-load-size auto`, the default;
a smaller explicit count is refused).  `--verify` re-checks the payload inside
the finished artifact, so a non-PE or truncated boot image is caught here rather
than as a QEMU timeout.

Artifacts, all beside the requested output path:

| File | Contents |
|---|---|
| `NAME.iso`, `NAME.img` | the images |
| `NAME.manifest.txt` | version, commit, host, epoch, every staged file, artifact hashes |
| `NAME.build.json` | machine-readable build record for CI |
| `NAME.iso.sha256` | checksum (disable with `--no-checksum`) |

### 3.2 Checking a boot

```sh
# Stub image
tools/bootcheck.sh out/stub.iso --expect RAVYNOS-BOOTCHECK-STUB-OK

# Real image: all four stages must be reached
tools/bootcheck.sh out/ravynos.iso --milestones --json out/verdict.json

# Disk image via UEFI firmware
tools/bootcheck.sh out/ravynos.img --uefi --milestones

# Direct kernel boot for early bring-up
tools/bootcheck.sh out/ravynos.iso --kernel ../build/kernel/mach_kernel \
    --append "-v serial=1"

# Grab a screenshot when it hangs
tools/bootcheck.sh out/ravynos.iso --milestones --screenshot --outdir out/boot
```

The verdict is one line, `BOOTCHECK PASS` or `BOOTCHECK FAIL`, and the exit
status is 0/1 so it can gate CI. Serial and QEMU logs are always preserved;
their paths are printed.

### 3.3 The whole loop

```sh
tools/pipeline-selftest.sh            # builds, verifies, boots (needs QEMU)
tools/pipeline-selftest.sh --no-qemu  # structure + reproducibility only
tools/tests/run-tests.sh              # shell syntax, unit tests, CLI contract
```

---

## 4. The boot milestone contract

The verdict is only as good as its markers, so they are a contract rather than
an implementation detail. Any loader — stub, UEFI app, or a future replacement
— writes these lines to the serial console (COM1, 115200 8N1) as it advances:

| Marker | Meaning | Failure implies |
|---|---|---|
| `RAVYNOS-LOADER-OK` | loader started executing and owns the console | firmware did not find or start the boot image |
| `RAVYNOS-KERNEL-OK` | kernel image located, loaded and entered | loader/format mismatch, corrupt payload, wrong load address |
| `RAVYNOS-ROOT-MOUNT-OK` | root volume identified and mounted | kernel/root-device or filesystem-driver problem |
| `RAVYNOS-USERSPACE-OK` | first userspace process reached `main` | dyld/libSystem/init problem |

Stages are cumulative: seeing `RAVYNOS-KERNEL-OK` without
`RAVYNOS-ROOT-MOUNT-OK` localises the failure between those two steps, which is
exactly what makes the verdict useful. `--expect` takes any marker, so a new
stage can be checked before the whole chain works.

The stub images emit the same markers, which means the *verdict machinery* is
tested on every CI run, long before a real loader exists.

---

## 5. Reproducibility

Image bytes are a function of (staged tree, volume id, timestamp), nothing
else:

* the default timestamp is the **git commit time** (or `SOURCE_DATE_EPOCH`);
  pass `--timestamp` to override. Two builds of one commit produce identical
  bytes on any host — the reproducibility job in
  `.github/workflows/pipeline.yml` asserts this on both operating systems;
* directory entries are sorted, identifiers are mangled deterministically,
  name collisions resolve by a stable suffix;
* FAT volume serials and GPT GUIDs are derived from the volume label by
  SHA-256, not from random values or the clock;
* symlinks and special files in the staging tree are **rejected** rather than
  followed, because following them would make the image depend on the build
  host.

Verify a rebuild yourself:

```sh
SOURCE_DATE_EPOCH=1600000000 tools/mkimage.sh --stub --format both -o /tmp/a/img
SOURCE_DATE_EPOCH=1600000000 tools/mkimage.sh --stub --format both -o /tmp/b/img
cmp /tmp/a/img.iso /tmp/b/img.iso && cmp /tmp/a/img.img /tmp/b/img.img
```

---

## 6. Structural verification

`tools/image/verify.py` parses an image the way a bootloader or firmware
would, and does not share code with the writers:

* **ISO**: primary volume descriptor, both-endian field agreement, path
  tables, the full directory tree (recursively, with loop detection), and the
  El Torito validation-entry checksum, boot indicator, load size and target
  extent.
* **IMG**: MBR signature and protective partition, primary **and** backup GPT
  headers (CRC32), both entry arrays (CRC32 and equality), the ESP type GUID,
  and the FAT16 boot parameters, cluster chains and directory tree.

```sh
python3 tools/image/verify.py iso out/ravynos.iso
python3 tools/image/verify.py img out/ravynos.img --json
```

This runs in milliseconds without QEMU, so CI gates on it before spending
minutes on a boot, and a failure is attributable to image assembly rather than
to the guest.

---

## 7. Failure triage

| Symptom | Likely cause | Next step |
|---|---|---|
| `mkimage: missing tool: nasm` | stub mode without nasm or python3 | `brew install nasm` / install python3 |
| `refusing to stage symlink` | the staging tree contains symlinks | stage a real tree (`cp -RL` is *not* a fix: it hides the problem) |
| `cannot be a legal FAT16 volume` | `.img` too small for FAT16 | raise `--size-mb`; 16 MiB is the floor |
| `partition ends at LBA … last usable` | image size smaller than the tree needs | let the size be automatic, or raise `--size-mb` |
| `VERIFY FAIL: … CRC32 mismatch` | a writer bug or a truncated image | check `df` (a full disk truncates writes silently), then file a test |
| `BOOTCHECK FAIL: QEMU exited before …` | firmware rejected the image | read `qemu.log`; check the boot catalog entry with `verify.py` |
| `BOOTCHECK FAIL: timed out` | guest hung or serial not wired | add `--screenshot`, check the guest is writing to COM1 |
| Boot works on Linux, not macOS | backend difference | force `--backend python` on both to isolate |

---

## 8. CI

| Workflow | What it proves |
|---|---|
| `pipeline.yml` → `tests` | shell syntax, unit tests, CLI contract on Ubuntu and macOS |
| `pipeline.yml` → `structure` | every backend produces a verifiable image, on both hosts |
| `pipeline.yml` → `reproduce` | two independent builds are byte-identical |
| `pipeline.yml` → `boot` | QEMU boots the stub and the four milestones, artifacts published |
| `build.yml` | `bmake world` → `mkimage` → `bootcheck` (per push when enabled) |
| `nightly.yml` | the same from scratch, no cache, files an issue on failure |

---

## 9. Extending the pipeline

* **New staged layout**: pass `--root DIR`; `--boot-image`/`--efi-image` select
  the loader inside it. Nothing else is required — the writers do not care what
  the files are.
* **A real bootloader**: see `Docs/BootLoader.md`. The pipeline already emits
  UEFI catalog entries and a FAT ESP, so the loader only needs to exist and
  print the milestone markers.
* **Another format**: add a writer under `tools/image/`, a verifier case in
  `verify.py`, and a test in `tools/tests/test_images.py`. The self-test's
  structure step will then cover it automatically.
