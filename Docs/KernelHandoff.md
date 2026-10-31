# Kernel Handoff Plan (Steps C/D of the boot milestone chain)

**Goal:** extend the proven loader→shell chain so the image boots, the loader
loads a kernel payload, transfers control after `ExitBootServices()`, and the
kernel prints `RAVYNOS-KERNEL-OK` on the serial port. That marker is already in
the `bootcheck.sh` milestone contract (`Docs/Pipeline.md` §4), so a PASS verdict
automatically covers the new stage.

This is deliberately **not** booting XNU. Full XNU (`Kernel/xnu`, 68 MB source)
requires the cross toolchain + SDK from Phase 1 and Apple's i386 boot ABI
(EFI_BOOT_PROTOCOL / boot_info). The roadmap sequence is: prove the handoff
mechanism with a minimal freestanding kernel first, then grow it toward XNU
compatibility. Same principle as Step B: smallest verifiable thing first.

## Design decisions

1. **Payload format: raw ELF64 (x86-64), not Mach-O.**
   * Why: lld emits ELF for `x86_64-unknown-none`; no relocator needed — the
     loader parses the program headers and copies PT_LOAD segments verbatim.
     A Mach-O payload would force us to write a full slide/loader path before
     we have any kernel at all.
   * Forward compatibility: real XNU on EFI is a Mach-O loaded via Apple's
     BootX/EFI_BOOT_PROTOCOL style handoff. When Phase 1 delivers an XNU build,
     we add a Mach-O loader beside this one; the memory-map/ExitBootServices
     code below is reused unchanged. Documented as a known delta, not hidden.
2. **Kernel link address: fixed 0x00100000 (1 MiB)**, non-relocatable in v1.
   Loader reserves exactly that range; avoids relocation-type work entirely.
3. **Payload location:** `\EFI\KERNEL.ELF` first, then `\BOOT\KERNEL`,
   `\EFI\KERNEL` and `\KERNEL.ELF`. `mkimage --kernel PATH` stages the file
   as *both* `EFI/KERNEL.ELF` and `BOOT/KERNEL`, and also inside the FAT
   volume that wraps the El Torito UEFI entry — firmware mounts only that
   volume, so a kernel placed outside it cannot be read by the loader that
   boots from inside it.
4. **Boot stack:** the loader allocates 64 KiB with `AllocatePages` after
   `ExitBootServices` and jumps with `rsp` at its top. The kernel's static
   `boot_kstack` stays as a fallback for a loader that passes its own. (An
   earlier plan had the kernel publish `BOOTKSTACK_TOP` and the loader read
   the symbol table; the loader never did, and a stack that the kernel owns is
   the wrong way round: it must be memory the firmware cannot reuse.)
5. **Handoff protocol (v1, our own — documented so XNU can replace it later):**
   * Loader calls `GetMemoryMap()` LAST before `ExitBootServices()` (spec rule:
     map key invalidates on any boot-services call after it).
   * Loader converts EFI_CONVENTIONAL_MEMORY into a simple physical-memory
     record list, writes it plus a small `bootinfo` struct at a fixed low
     address (0x90000, below 1 MiB, outside kernel and above legacy area use),
     sets `rdi = &bootinfo`, and jumps to the ELF entry point in long mode with
     paging ON using the firmware page tables (identity map preserved).
   * Kernel is freestanding C, `-ffreestanding -nostdlib`, ms_abi NOT required
     (System V is the native long-mode ABI); talks to the COM1 16550 directly
     via `outb(0x3F8, …)` — same UART QEMU's `-serial file:` captures, so no
     EFI services are needed post-handoff.
6. **Console continuity:** the shell remains available if the kernel payload
   is absent or fails to load ("no kernel" → drop back to the shell's
   prompt). Keeps the existing SHELL-OK behavior intact and makes regressions
   bisectable. See `Docs/Shell.md` for both prompts and for the rule that all
   of this output goes to the 16550 UART directly rather than through a
   firmware protocol.

## Milestone contract

* Loader prints `RAVYNOS-KERNEL-LOAD <bytes> bytes, image <lo>-<hi>, entry
  <addr>` when the ELF has been validated and staged, and a specific refusal
  message when it has not.
* Kernel prints `RAVYNOS-KERNEL-OK` (already a bootcheck milestone), dumps the
  bootinfo summary, then prints `RAVYNOS-KSHELL-OK` and enters the command
  console (`Docs/Shell.md` §2). Root-mount/userspace markers stay unmet until
  later phases; bootcheck treats partial chains per its multi-marker logic.

## Steps (each independently committable)

| # | Deliverable | Verification |
|---|---|---|
| K1 | `Kernel/ravynkern/` — freestanding stub kernel: ELF64 entry, COM1 init, bootinfo parse, `RAVYNOS-KERNEL-OK`, then the command console (`help`, `ver`, `mem`, `cpus`, `echo`, `reboot`, `halt`). `build.sh` accepts clang+ld.lld or gcc+GNU ld; the committed `KERNEL.ELF` is gone. | `tools/image/elf.py` accepts the layout; `tools/tests/test_elf.py` (13 cases) and `test_stub_kernel.py` cover the contract. |
| K2 | Loader: minimal ELF64 parser (header, phdrs, PT_LOAD copy, entry extract) in `uefi_stub.c` + `loadkern()` reading `/EFI/KERNEL.ELF` off the ESP via SimpleFileSystem (path reuse from shell chain-load). | Builds BOOTX64.EFI clean; objdump sanity; unit-testable parser fed by fixture bytes. |
| K3 | Loader: memory-map harvest → bootinfo struct at 0x90000, `ExitBootServices()`, long-mode jump to entry with `rdi=&bootinfo`. Guarded: only taken when K2 succeeded. | Code review vs UEFI spec §7.2/§8.4 ordering rules; serial trace up to handoff. |
| K4 | `mkimage.sh`: `--kernel` stages the payload as `EFI/KERNEL.ELF` + `BOOT/KERNEL` and inside the El Torito FAT wrap; `--shell` stages `EFI/SHELL.EFI`. `verify.py` parses the kernel inside the finished ISO/ESP and enforces the same ELF rules. | Done: the ISO and IMG both carry a validated kernel, and `verify.py img` prints it. |
| K5 | Boot verdict: `bootcheck.sh --uefi --expect RAVYNOS-KERNEL-OK`, plus `--send commands.txt` to prove the prompt answers. OVMF is booted from three media (ISO, GPT/ESP, bare FAT) so a firmware failure can be attributed. | **Open**: no OVMF run has yet entered the loader — the boot option BDS starts has no file node. See `PROGRESS.md` rev 17. |

## Risks / mitigations

* **ExitBootServices map-key race** → always GetMemoryMap immediately prior; retry once on EFI_INVALID_PARAMETER per spec practice.
* **OVMF keeps paging/EPT state surprises** → identity map is guaranteed present post-ExitBootServices on OVMF (it boots in paging already); kernel must NOT assume NX/W^X enforcement — fine for stub.
* **1 MiB region reserved?** → AllocatePages before EBS; if allocation fails, print `RAVYNOS-KERNEL-NOMEM` and return to shell (verdict stays FAIL, diagnosable).
* **Quota-limited verification** → each step ships with a cheap static check (readelf/objdump/unit test) so the user can run the expensive QEMU boot locally.

## Out of scope (explicitly)

XNU sources untouched; no Mach-O loader yet; no root filesystem; no APFS/HFS+
(yet — ESP stays FAT per UEFI requirement, matching real Macs); no arm64.
