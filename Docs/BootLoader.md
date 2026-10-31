# The boot path: what boots the kernel, and how

**Status:** decision document + staged plan. Step A (milestone stub) is done.
Step B (UEFI application) builds with clang+lld-link *or* zig and has a CI job
that boots it under OVMF from the ISO, the GPT/ESP image and a bare FAT volume
— see §3. Step C (read a payload off the volume) and Step D (ELF load +
handoff) are implemented and packaged, but no OVMF run has yet observed the
loader *running*: the boot option BDS starts has no file node, i.e. firmware
never mounted the volume. That is the open defect; `PROGRESS.md` rev 17 records
what was ruled out and how each boot is now diagnosed. Everything below is constrained by two facts
that can be checked in this repository:

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

A ~350-line freestanding PE32+ application, built with clang
(`-target x86_64-unknown-windows -ffreestanding -fshort-wchar -mno-red-zone`),
that prints `RAVYNOS-LOADER-OK` over `SerialIo`, then `EFI_SUCCESS`.

In tree: `tools/bootloader/uefi_stub.c`, `build.sh`, a Makefile that works under
both GNU make and bmake, and `README.md`.  The skeleton declares the UEFI tables
it uses inline — no gnu-efi, no EDK2 checkout — prints `RAVYNOS-LOADER-OK` over
the serial protocol with a `ConOut` mirror, and returns.  Package it with
`mkimage.sh --efi-image EFI/BOOT/BOOTX64.EFI`.

Three things had to be true before this step could be judged, and the first two
turned out to be broken:

1. **The loader had to build.**  The Makefile used bmake-only automatic
   variables (`.TARGET`, `.ALLSRC`), so the documented `make` failed on every
   Linux host — the loader had never been compiled anywhere.  `build.sh` now
   owns the flags and toolchain detection (clang+lld-link, or a zig binary as
   a fallback), and the entry symbol is the conventional `EfiMain`, which both
   linkers accept.
2. **The payload had to be loadable.**  `--efi-image` was never checked, and
   `--efi-load-size` defaulted to **1 sector**: the El Torito catalog announced
   a 3.5 KiB EFI application as 512 bytes, so firmware would have loaded a
   truncated image and the boot would have hung with no diagnostic.  The sector
   count is now derived from the file (`auto`), an explicit count smaller than
   the file is refused, and `tools/image/pe.py` validates the PE32+ / EFI
   subsystem headers at image-build time and again inside the finished ISO/ESP.
3. **The firmware path had to be exercised.**  `bootcheck.sh --uefi` handles
   split `OVMF_CODE`/`OVMF_VARS` firmware (copying the variable store instead
   of writing to the system one), and the `uefi` job in `pipeline.yml` builds
   the loader with clang+lld-link, verifies the PE headers, stages an ESP,
   builds both images, and boots each under OVMF.

**Exit:** `tools/bootcheck.sh out/uefi.img --uefi --expect RAVYNOS-LOADER-OK`
passes under OVMF, for the disk image *and* for the ISO's UEFI El Torito entry.
That validates the ESP construction and the catalog entry in one shot — before
any kernel work.  The CI job is the arbiter; a local host with QEMU and OVMF
can run the same two commands.

### Step C — load a payload from the image *(implemented)*

The loader opens the volume it was started from, tries `\EFI\KERNEL.ELF`,
`\BOOT\KERNEL`, `\EFI\KERNEL` and `\KERNEL.ELF` in that order, names the
file it found, and validates the ELF before touching memory: ELF64/`ET_EXEC`/
`EM_X86_64`, every `PT_LOAD` extent inside the file, `MemSiz >= FileSiz`, no
overlapping segments, linked at or above 1 MiB and below the 4 GiB identity
map, and `e_entry` inside a loadable segment. A payload that fails is refused
with a specific message instead of being jumped into.

`mkimage --kernel` stages the payload as both `EFI/KERNEL.ELF` and
`BOOT/KERNEL`, *and* inside the FAT volume that wraps the El Torito UEFI entry,
because firmware only mounts that volume — a kernel left outside it is
invisible to this loader. `tools/image/elf.py` is the same check as a program,
run at kernel build time, at image build time, and again by `verify.py` on the
bytes inside the finished ISO/ESP.

**Exit:** a deliberately corrupted kernel must make the boot log say why; the
loader prints `RAVYNOS-KERNEL-LOAD …` only after the payload is validated.

### Step D — ELF load and handoff *(implemented for the stub kernel)*

`AllocatePages(AllocateAddress)` reserves the link range, `CopyMem` + `SetMem`
stage each `PT_LOAD`, a 64 KiB boot stack is allocated, `bootinfo` is written
at `0x90000` (conventional-memory list, harvested from the last `GetMemoryMap`
before `ExitBootServices`), then `ExitBootServices` is attempted with the key
from that same call and the CPU jumps to `e_entry` with `rdi = &bootinfo`.

Mach-O and Apple's `boot_args` remain future work: the stub kernel is ELF, and
loading a Mach-O XNU is a separate loader beside this one. The
`ExitBootServices`/memory-map/boot-stack code is reusable unchanged.

**Exit:** `RAVYNOS-KERNEL-OK` and `RAVYNOS-KSHELL-OK` on the serial console,
and the prompt answers `help` typed over the serial socket
(`tools/bootcheck.sh --send`).

### Step E — real image in the pipeline

`tools/mkimage.sh --root ../build/stage --boot-image BOOT/LOADER
--efi-image EFI/BOOT/BOOTX64.EFI --format both --verify` then
`tools/bootcheck.sh --milestones` becomes the M2 exit criterion.

---

## 4. Constraints and pitfalls

* **Payload format.** The stub kernel is ELF64 because lld and GNU ld both
  emit it for `x86_64-unknown-none` with no relocator needed. Real XNU links
  as Mach-O with the in-tree cctools `ld`; a Mach-O loader is the next piece of
  work and reuses the memory-map/`ExitBootServices` path above.
* **Serial output cannot depend on a firmware protocol.** The loader drives the
  16550 at `0x3F8` directly for exactly this reason — see `Docs/Shell.md` §1.
  A loader that prints only through `EFI_SERIAL_IO_PROTOCOL` can be silent on
  a host that has none, which is indistinguishable from never having run.
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
