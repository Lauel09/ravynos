# Darwin Boot Plan — the ONLY active work item

**Goal:** our UEFI loader (`tools/bootloader/uefi_stub.c`) loads the real XNU
kernel built from `Kernel/xnu`, and XNU prints `Darwin Kernel Version` on the
serial port under QEMU+OVMF on this Mac.

Written 2026-10-31 by a review pass. Everything below marked *Verified* was
reproduced by command on that date. Everything marked *Check* is a belief
that you MUST confirm by reading the cited file before relying on it.

---

## 0. Rules for whoever works this file (read every session)

1. **Work milestones in order. One milestone at a time.** Do not start D(n+1)
   until D(n)'s gate line is pasted into the log (§4).
2. **A milestone is done only when its gate command's real output is pasted
   in §4.** No output → not done. "Should work" is not evidence.
3. **Never invent** a struct field, offset, constant, symbol or file path.
   Before you use one, `grep` for it and cite `file:line` in your log entry.
   If grep finds nothing, it does not exist — stop and write that down.
4. **No new tooling, no new docs, no refactors** outside the milestone. The
   repo already has too much infrastructure; the kernel has never loaded.
5. If the same error survives 3 fix attempts, stop, log the exact error and
   what you tried under "Blocked", and move to the next *Check* item instead
   of guessing a 4th time.
6. Long builds: run with output to a log file (`> build-xnu-N.log 2>&1`) and
   read only the first error: `grep -n -m5 -E "error:|\*\*\* |No rule" FILE`.
   Do not paste whole logs into context.
7. Keep every macOS-specific change `.MAKE.OS`/`uname`-guarded (see
   `ROADMAP.md` guiding rules).

## 1. State as of 2026-10-31 (Verified)

* **The OVMF "#UD at RIP 0xB0000" blocker does NOT reproduce locally.** With
  Homebrew QEMU + `edk2-x86_64-code.fd`, both `mkimage` `.img` and `.iso`
  boot and print `RAVYNOS-ENTRY`/`RAVYNOS-LOADER-OK`. It is a CI-runner
  firmware problem. **Ignore it for this plan**; boot locally (§3).
* **Fixed:** `SIMPLE_FILE_SYSTEM_PROTOCOL_GUID` in `uefi_stub.c` was wrong
  (it is `964E5B22-6459-11D2-8E39-00A0C969723B`), so the loader never found
  any kernel. After the fix the full chain works locally:
  `RAVYNOS-KERNEL-LOAD … → ExitBootServices → RAVYNOS-KERNEL-OK → RAVYNOS-KSHELL-OK`.
  This proves memory map, ExitBootServices and the jump. Reuse that code.
* **Fixed (untested by a full build):** `Kernel/Makefile` passed
  `DEVELOPER_DIR=/Library/Developer/CommandLineTools` to XNU's gmake. On macOS
  that is Apple's CLT, so ravynOS `xcrun` returned
  `error: unable to find sdk: 'macosx'` and NM, LIPO, LIBTOOL, NMEDIT,
  DSYMUTIL… all became that error string (`build-world-attempt33.log`). Now on
  Darwin it uses `${ROOT_BINARY_DIR}/Developer`, and a new `xnu_devdir`
  target symlinks `Developer/Toolchains → Platforms/ravynOS.platform/Developer/Toolchains`
  (the ravynOS xcrun looks for toolchains in `$DEVELOPER_DIR/Toolchains`).
  Verified by hand:
  `DEVELOPER_DIR=…/build/Developer …/usr/bin/xcrun -sdk macosx -find nm` → a real path.
* Last XNU build failure (`build-world-attempt33.log`, before the fix):
  `No rule to make target 'installfile.o'` (also kextsymboltool, decomment,
  setsegname, replacecontents) in `xnu/SETUP/*`, then
  `cp: …/xnu/sym/kernel.dSYM: No such file or directory`.
* Noise, not a cause: `/bin/sh: 6-${\#__A}: bad substitution` comes from
  `Kernel/xnu/makedefs/MakeInc.rule:65` (GNU make 4.4 keeps the `\` in `\#`).
  It only builds a log-line prefix (`CMD_MC`). Fix only if it blocks something.
* No Mach-O loader exists anywhere in the repo yet (grep for `MH_MAGIC_64`
  outside `Kernel/xnu`, `Developer`, `BSD`, `GNU` finds nothing).

## 2. Milestones

### D1 — XNU builds and emits a kernel binary
* Do: rerun only the kernel part of the build (find the smallest target in
  the top `Makefile`/`Kernel/Makefile` that rebuilds xnu; `world` lists
  `Developer Kernel …`, Developer is already built). First confirm in the log
  that NM/LIPO are no longer `error: unable to find sdk`.
* Next likely error: `No rule to make target 'installfile.o'`. Diagnose it, do
  not guess: run
  `gmake -C <OBJROOT>/…/SETUP/installfile -p 2>/dev/null | grep -n -E "^SOURCE|^%.o|^HOST_CC"`
  style probes, and read the `%.o` rule in `Kernel/xnu/makedefs/MakeInc.rule`
  to see which variable (SOURCE path, HOST_CC, HOST_SDKROOT) is wrong. Note the
  OBJROOT is `build/Users/solus/…/ravynos/Kernel/xnu` (absolute path doubled
  under `build/`) — check whether SOURCE is computed relative to that.
* **Gate:** `file <path-to-built-kernel>` prints
  `Mach-O 64-bit executable x86_64`. Record the exact path.

### D2 — Know the kernel image (read-only, no code)
Using `otool -l` (Apple, already on this Mac) or `llvm-objdump --macho -p` on
the D1 kernel, record in §4:
* every `LC_SEGMENT_64`: name, vmaddr, vmsize, fileoff, filesize;
* the entry: `LC_UNIXTHREAD` rip (or `LC_MAIN` — record which one exists);
* whether the file is `MH_EXECUTE` or `MH_FILESET` (`otool -h`, filetype 2 vs 12).
* **Check** in `Kernel/xnu/osfmk/x86_64/start.s`: the CPU mode the entry
  (`pstart`/`_pstart`) expects (look for `.code32`/`.code64`), which register
  carries the boot_args pointer, and whether paging must be off.
* **Check** the kernel's virtual→physical rule: find `KERNEL_BASE` /
  `KERNEL_BASE_OFFSET` / `VM_MIN_KERNEL_ADDRESS` in
  `Kernel/xnu/osfmk/i386/` and `osfmk/mach/i386/vm_param.h`; write down how a
  segment's vmaddr maps to the physical address it must be loaded at.
* **Gate:** the table + the 3 *Check* answers with `file:line` citations.

### D3 — boot_args, from the header, not from memory
* Read `Kernel/xnu/pexpert/pexpert/i386/boot.h` (`boot_args` struct,
  `kBootArgsVersion2`, `kBootArgsEfiMode64`, `Boot_Video`, `BOOT_LINE_LENGTH`).
* Add to `uefi_stub.c` a byte-for-byte copy of that struct (cite the line
  numbers in a comment) and a `_Static_assert(sizeof(...) == N)` where N is
  what `clang -arch x86_64` reports for the original header (write a 5-line
  test .c that includes it and prints `sizeof`/`offsetof`).
* Fields to fill (each **Check** in boot.h and in its consumer
  `Kernel/xnu/osfmk/i386/i386_vm_init.c` / `pexpert/i386/pe_init.c`):
  Revision/Version, efiMode=64, CommandLine (start with
  `-v debug=0x8 serial=3 keepsyms=1`), MemoryMap/Size/DescriptorSize (the raw
  EFI map from the existing handoff code), kaddr/ksize, efiSystemTable,
  efiRuntimeServicesPageStart/Count, physicalMemorySize, Video (zeros +
  text mode is fine for serial), deviceTreeP/Length (D5).
* **Gate:** the static_assert compiles; `tools/bootloader/build.sh` exits 0.

### D4 — Mach-O loader + entry trampoline, proven on a stub first
Do NOT wait for D1 to test this. Build a ~30-line stub Mach-O kernel:
an assembly entry in the same CPU mode D2 found, that writes `RAVYNOS-MACHO-OK`
to COM1 (`outb 0x3F8`, as `Kernel/ravynkern/main.c` does) and halts. Link it
with `clang -arch x86_64 -static -nostdlib -Wl,-e,<entry>` (plus a segment
address matching the D2 vmaddr layout) so it has the same load commands as XNU.
* Loader: parse `mach_header_64`, copy each `LC_SEGMENT_64` to its physical
  address (D2 rule) with `AllocatePages(AllocateAddress, …)`, take the entry
  from `LC_UNIXTHREAD`. Keep the ELF path working (dispatch on magic
  `0xFEEDFACF` vs `\x7fELF`). Add `\System\Library\Kernels\kernel` to
  `gKernelPaths` and stage it with `mkimage --kernel`.
* If D2 says the entry is 32-bit protected mode: after ExitBootServices the
  loader must, from code + GDT + stack located **below 4 GiB**, load a GDT
  with a 32-bit flat code/data segment, far-jump into it (compatibility mode),
  clear CR0.PG, clear EFER.LME (MSR 0xC0000080), set the boot_args register
  D2 found, and jump to the physical entry. Write it as a small `.S` file,
  not inline asm.
* **Gate:** serial log contains `RAVYNOS-MACHO-OK` from the stub.

### D5 — Minimal device tree
XNU on x86 reads a flattened device tree (`deviceTreeP`). **Check**
`Kernel/xnu/pexpert/gen/device_tree.c` and `pexpert/pexpert/device_tree.h` for
the node/property binary format, and grep the x86 consumers for which nodes
they look up (`grep -rn "DTLookupEntry\|SecureDTLookupEntry" Kernel/xnu/pexpert/i386 Kernel/xnu/osfmk/i386`).
Expect at least `/chosen`, `/chosen/memory-map`, `/efi/platform`
(FSBFrequency — **Check** `tsc_init` in `osfmk/i386/tsc.c`, it may panic
without it). Build only the nodes the grep proves are read.
* **Gate:** a unit test (`tools/tests/`) that round-trips the blob through
  your own parser in the same format as `device_tree.c`.

### D6 — Boot XNU
Stage the D1 kernel, boot locally (§3) with `-cpu Penryn` or another Intel
model (XNU checks the vendor). Iterate on panics one at a time; each panic
string goes in §4 with the fix.
* **Gate (= project goal):** serial log contains `Darwin Kernel Version`.
* Expected afterwards (not part of this goal): a panic about platform
  expert / ACPI / no root device. That is the next plan, not this one.

## 3. Local boot recipe (Verified 2026-10-31)

```sh
cd ravynos
S=$PWD/out/local; mkdir -p $S/stage/EFI/BOOT
tools/bootloader/build.sh -q && cp tools/bootloader/BOOTX64.EFI $S/stage/EFI/BOOT/
tools/mkimage.sh --root $S/stage --efi-payload EFI/BOOT/BOOTX64.EFI \
    --kernel <KERNEL> --format img -o $S/r
cp /opt/homebrew/share/qemu/edk2-i386-vars.fd $S/vars.fd
qemu-system-x86_64 -machine q35 -m 2048 -display none -monitor none \
  -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd \
  -drive if=pflash,format=raw,file=$S/vars.fd \
  -drive format=raw,file=$S/r.img -serial file:$S/serial.log &
sleep 25; kill $!; grep -a -E "RAVYNOS|Darwin|panic|loader:" $S/serial.log
```
Fastest inner loop (skips mkimage): put `EFI/BOOT/BOOTX64.EFI` and the kernel
in a directory and use `-drive format=raw,file=fat:rw:<dir>` instead.

## 4. Log (append newest at bottom; one entry per session)

Format — keep it this short:
```
### YYYY-MM-DD — D<n> <done|progress|blocked>
Did: <one line>
Evidence: <gate output or first error, verbatim, ≤5 lines>
Cites: <file:line for every constant/struct used>
Next: <one line>
```

### 2026-10-31 — D0 review
Did: found SFS GUID bug (loader→kernel handoff now works locally); fixed
XNU DEVELOPER_DIR on macOS; wrote this plan.
Evidence: `RAVYNOS-KERNEL-OK` … `RAVYNOS-KSHELL-OK` on local OVMF boot;
`xcrun -sdk macosx -find nm` resolves with the new DEVELOPER_DIR.
Next: D1.
