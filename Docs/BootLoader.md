# The boot path: what boots the kernel, and how

**Status:** decision document + staged plan. No bootloader source exists in the
tree today. Everything below is constrained by two facts that can be checked in
this repository:

1. The kernel build produces a **Mach-O** executable (`Kernel/xnu` links with
   the in-tree cctools `ld`), not ELF and not multiboot.
2. XNU expects to be handed an Apple `boot_args` structure and a pointer to the
   firmware system table, declared in `Kernel/xnu/pexpert/pexpert/boot.h` and
   `Kernel/xnu/pexpert/pexpert/i386/efi.h`.

Together those rules out every "free" boot path and define the loader's job.

---

## 1. Options considered

| Option | Works with a Mach-O XNU? | QEMU support | Effort | Verdict |
|---|---|---|---|---|
| `qemu -kernel` | No — QEMU loads ELF or Linux bzImage | direct | none | rejected |
| Multiboot2 (GRUB/limine) | No — XNU has no multiboot entry | good | medium | rejected |
| BIOS stage-2 loader (real mode → long mode) | Yes, but you reimplement paging, A20, E820 parsing | good | high | defer |
| **UEFI application (`BOOTX64.EFI`)** | **Yes** — the firmware gives paging, memory map and console for free | **good (OVMF)** | medium | **primary** |
| Hybrid: BIOS El Torito + UEFI entry in one ISO | Yes | good | medium | **ship both** |

The ISO format already supports both routes at once: the El Torito *default*
entry carries a BIOS image and a *section header* with platform id `0xEF`
carries the UEFI image. `tools/mkimage.sh` emits that combination today
(`--boot-image` plus `--efi-image`), so the loader choice does not have to be
made permanent at image-build time.

**Decision:** a UEFI application is the primary loader, because the firmware
does the parts that are expensive to get right (paging, A20, memory map,
console). A BIOS stage-2 loader stays possible and the ISO keeps a slot for it,
but it is not the first target.

---

## 2. What the loader has to do

In order, with the milestone each step emits:

| # | Step | Emits |
|---|---|---|
| 1 | Locate the console (EFI `SerialIo` on the ACPI SPCR port, else `ConOut`) and write a banner | `RAVYNOS-LOADER-OK` |
| 2 | Open the volume it was loaded from; read `/BOOT/KERNEL` (Mach-O) | — |
| 3 | Allocate contiguous memory at the kernel's preferred base, copy `__TEXT`/`__DATA`, apply relocations, zero `__BSS` | `RAVYNOS-KERNEL-OK` |
| 4 | Build `boot_args`: memory map (`boot_args.MemoryMap`, `MemoryMapSize`), device tree, video info, EFI system table pointer, boot flags, command line | — |
| 5 | `ExitBootServices` (retry once if the memory map changed) | — |
| 6 | Enter the kernel with interrupts off, in the kernel's expected CPU state | — |
| 7 | Kernel mounts its root volume and prints progress | `RAVYNOS-ROOT-MOUNT-OK` |
| 8 | Kernel execs the first userspace process | `RAVYNOS-USERSPACE-OK` |

Steps 1–3 are what the *loader* owns; 7–8 are the kernel's. The markers are a
contract shared with `tools/bootcheck.sh` (see `Docs/Pipeline.md` §4) and should
be emitted even in early stubs, because that is what makes "how far did it
get?" answerable without a debugger.

### Why a serial console, not the screen

The boot verdict is machine-read. On QEMU, `-serial file:` gives us a log with
no display at all, which is what CI needs. A loader that only writes to
`ConOut` is invisible in that configuration. Two portable options:

* **ACPI SPCR** (preferred): the `Serial Port Console Redirection` table names
  the I/O port (0x3F8 for COM1) and baud rate. Real hardware and OVMF both
  provide it.
* **EFI `SerialIo` protocol** by GUID `BB25CF6F-F1D4-11D2-9A0C-0090273FC1FD`,
  then a fallback to hard-coded 0x3F8 with the same initialisation our stub
  uses (`tools/image/bootstub.py`).

---

## 3. Staged plan

Each step has an exit criterion that one command can check, so progress is
never a matter of opinion.

### Step A — milestone stub (today)

`tools/mkimage.sh --stub-milestones` builds an El Torito image whose 16-bit
stub prints all four markers and halts. It exists to prove the *verdict
machinery*, not the OS.

**Exit:** `tools/pipeline-selftest.sh` passes on macOS and Linux, including the
milestone chain.

### Step B — UEFI "hello" application

A ~200-line freestanding PE32+ application, built with clang
(`-target x86_64-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone`),
that prints `RAVYNOS-LOADER-OK` over `SerialIo`, then `EFI_SUCCESS`.

Deliverables (in tree, not yet built on this host): `tools/bootloader/uefi_stub.c`,
`tools/bootloader/Makefile` and `tools/bootloader/README.md`.  The skeleton
declares the UEFI tables it uses inline — no gnu-efi, no EDK2 checkout — prints
`RAVYNOS-LOADER-OK` over the serial protocol with a `ConOut` mirror, and returns.
Package it with `mkimage.sh --efi-image EFI/BOOT/BOOTX64.EFI`.

**Exit:** `tools/bootcheck.sh out/uefi.iso --uefi --expect RAVYNOS-LOADER-OK`
passes under OVMF. This validates the ISO's UEFI catalog entry, the ESP
construction, and the firmware handoff in one shot — before any kernel work.

### Step C — load a payload from the image

Extend the stub to open the volume, `ReadFile('/BOOT/KERNEL')`, verify a
checksum stored beside it, print its size/model, and emit
`RAVYNOS-KERNEL-OK` *only after* the bytes are in memory and validated.

**Exit:** the same bootcheck with `--milestones --expect-any` sees the first two
markers; a deliberately corrupted `BOOT/KERNEL` in the image must make it fail.

### Step D — Mach-O load and handoff

Load the kernel's segments, build `boot_args`, `ExitBootServices`, jump. This
is where XNU start-up work begins; failures here are kernel bring-up failures,
not loader-format failures, and the markers separate the two.

**Exit:** `RAVYNOS-KERNEL-OK` then a kernel banner on serial.

### Step E — real image in the pipeline

`tools/mkimage.sh --root ../build/stage --boot-image BOOT/LOADER
--efi-image EFI/BOOT/BOOTX64.EFI --format both --verify` then
`tools/bootcheck.sh --milestones` becomes the M2 exit criterion.

---

## 4. Constraints and pitfalls

* **No ELF anywhere.** If a future kernel build can emit an ELF or a flat
  binary, `qemu -kernel` becomes viable for early bring-up. It does not today,
  and adding a second output format to the kernel build is more work than the
  UEFI stub.
* **Memory map churn.** `ExitBootServices` fails if the map changed since
  `GetMemoryMap`; the loader must re-read and retry. This is the single most
  common cause of "it works in QEMU, hangs on hardware".
* **Allocation below 4 GiB.** Early kernel page tables and some device
  structures assume addresses under 4 GiB; `AllocatePages(AllocateMaxAddress)`
  with `0xFFFFFFFF` as the limit.
* **Firmware-allocated pool is not contiguous.** Kernel segments need
  `AllocatePages`, not `AllocatePool`.
* **The ISO filesystem is not guaranteed to be readable by firmware.** Many
  UEFI implementations do not parse ISO 9660. That is why UEFI boot goes
  through the **ESP** (the El Torito UEFI image is file-backed by convention,
  but a firmware may only expose the FAT volume). Keep the loader on the FAT
  volume and read the kernel from the *same* FAT volume in step C, not from the
  ISO.
* **Determinism.** Images are reproducible (see `Docs/Pipeline.md` §5); a
  loader that embeds a build timestamp breaks that. Pass `SOURCE_DATE_EPOCH`
  into the loader build as well.

---

## 5. What the pipeline needs from the loader

Nothing beyond what it already supports:

1. A file to place as the El Torito BIOS image (`--boot-image`) and/or the UEFI
   image (`--efi-image`).
2. Serial output at 115200 8N1 using the markers above.

Everything else — kernel location, payload format, boot arguments — is between
the loader and the kernel.
