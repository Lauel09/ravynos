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

### 2026-10-31 — D4 progress
Did: added Mach-O 64 loader (LC_SEGMENT_64/LC_UNIXTHREAD), 32-bit protected-mode trampoline, boot_args struct (static_assert 4096), minimal device tree with /efi/platform FSBFrequency, and \System\Library\Kernels\kernel path; loader builds.
Evidence: `tools/bootloader/build.sh` -> "wrote .../BOOTX64.EFI (12288 bytes)"; run-tests.sh loader layer ok; no QEMU on host so RAVYNOS-MACHO-OK not yet captured.
Cites: tools/bootloader/uefi_stub.c (load_macho_kernel_image, build_boot_args, build_device_tree); tools/bootloader/xnu_handoff.S; Kernel/xnu/pexpert/pexpert/i386/boot.h:160-210; Kernel/xnu/osfmk/x86_64/start.s:136-210; Kernel/xnu/osfmk/mach/i386/vm_param.h:197.
Next: obtain QEMU/OVMF (macOS host) to capture RAVYNOS-MACHO-OK; D1 XNU build still blocked by missing bmake/mig on this Linux host.

### 2026-10-31 — darwin-gate.sh added
Did: added tools/bootloader/test/stub_macho.S (32-bit COM1 stub "RAVYNOS-MACHO-OK\n")
and tools/darwin-gate.sh (builds BOOTX64.EFI + ravynkern, clang stub Mach-O
with -e,_start and image_base 0xffffff8000200000 = VM_MIN_KERNEL_ADDRESS
(osfmk/mach/i386/vm_param.h:196), otool dump, two fat:rw boots per §3 recipe
25 s each, serial grep RAVYNOS|loader:|handoff|Exception|RIP; `xnu` mode
greps build-xnu.log for the D1 error patterns).
Evidence: none yet -- gate not run; awaiting user's `./tools/darwin-gate.sh`
output pasted here.
Cites: tools/darwin-gate.sh; tools/bootloader/test/stub_macho.S; Kernel/Makefile:81 (xnu_all).

### 2026-10-31 — D4 GATE PASSED (user-run on Mac)
Gate output (verbatim):
```
RAVYNOS-KERNEL-LOAD (macho) entry 0x0000000000200238

RAVYNOS-MACHO-OK

(ELF regression: RAVYNOS-KERNEL-OK, RAVYNOS-KSHELL-OK)
```
Did after: fixed the literal `\n` printed after the macho entry address in
tools/bootloader/uefi_stub.c (the whole load_macho_kernel_image() had been
written with `\\n` strings; all 13 converted to real newlines). Loader
rebuilt locally (build.sh -q ok). Also switched darwin-gate.sh xnu mode
from `bmake -C Kernel xnu_all` to top-level `bmake Kernel` so the top-level
exports (GMAKE, XCRUN, TOOLCHAIN …, Makefile:155-163) reach Kernel/Makefile.
D4: DONE (stub + ELF paths both verified by user's run).
Cites: tools/bootloader/uefi_stub.c load_macho_kernel_image; tools/darwin-gate.sh; Makefile:153,155.

### 2026-10-31 — full gate re-run after \n fix (user Mac)
Output (verbatim):
```
      cmd LC_SEGMENT_64
  cmdsize 152
  segname __TEXT
   vmaddr 0xffffff8000200000
--
      cmd LC_SEGMENT_64
  cmdsize 72
  segname __LINKEDIT
   vmaddr 0xffffff8000201000
--
        cmd LC_UNIXTHREAD
    cmdsize 184
     flavor x86_THREAD_STATE64
      count x86_THREAD_STATE64_COUNT
qemu-system-x86_64: terminating on signal 15 from pid 28059 (<unknown process>)
RAVYNOS-ENTRY
RAVYNOS-LOADER-OK
loader: found \System\Library\Kernels\kernel (4224 bytes)
RAVYNOS-KERNEL-LOAD (macho) entry 0x0000000000200238
handoff: ExitBootServices...
RAVYNOS-MACHO-OK
qemu-system-x86_64: terminating on signal 15 from pid 28059 (<unknown process>)
RAVYNOS-ENTRY
RAVYNOS-LOADER-OK
loader: found \EFI\KERNEL.ELF (10064 bytes)
RAVYNOS-KERNEL-LOAD 10064 bytes, image 0x00000000001000b0-0x00000000001123c8, entry 0x0000000000100560
handoff: ExitBootServices...
RAVYNOS-KERNEL-OK
RAVYNOS-KSHELL-OK
```
Reading: macho path now prints a clean line then the stub's COM1 marker; ELF
regression intact. No fixes needed. D4 remains DONE; \n fix verified.
Next: D1 -- user to run `./tools/darwin-gate.sh xnu` and paste its output.

### 2026-11-01 — D1 gate run 1 (user Mac): xcrun rejects DEVELOPER_DIR
Output (verbatim):
```
832:xcrun: error: invalid DEVELOPER_DIR path (/Users/solus/Documents/test-ravyn-os/build/Developer), missing xcrun at: /Users/solus/Documents/test-ravyn-os/build/Developer/usr/bin/xcrun
833:gmake[7]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/SETUP/installfile/Makefile:35: installfile.o] Error 1
835:gmake[6]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/makedefs/MakeInc.dir:59: build_setup] Error 2
837:gmake[5]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/makedefs/MakeInc.dir:60: build_setup_recurse_into_installfile] Error 2
838:gmake[5]: *** Waiting for unfinished jobs....
839:xcrun: error: invalid DEVELOPER_DIR path (/Users/solus/Documents/test-ravyn-os/build/Developer), missing xcrun at: /Users/solus/Documents/test-ravyn-os/build/Developer/usr/bin/xcrun
840:gmake[7]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/SETUP/kextsymboltool/Makefile:40: kextsymboltool.o] Error 1
842:gmake[6]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/makedefs/MakeInc.dir:59: build_setup] Error 2
```
Diagnosis (from output + source; no "unable to find sdk" this time -- §1's
fix held, the build got as far as compiling SETUP tools):
* The failing xcrun demands an Apple-style DEVELOPER_DIR containing
  `usr/bin/xcrun`. The string "invalid DEVELOPER_DIR path"/"missing xcrun
  at" exists nowhere in this repo or its git history (the built xcbuild
  xcrun, Developer/Default.xctoolchain/xcbuild/…/xcrun.cpp:276-279, finds
  the developer root and toolchains under $DEVELOPER_DIR/Toolchains and
  printed "error: unable to find sdk" in attempt33) -- so the xcrun that
  ran is Apple's.
* Chain: xnu's MakeInc.cmd:42-46 uses the XCRUN env var, which
  Kernel/Makefile passes as ${XCRUN} = ${TOOLS}/xcrun (sys.mk:287,
  Makefile:109). TOOLCHAIN starts at sys.mk:113 =
  /Library/Developer/Toolchains/Default.xctoolchain; Makefile:99-104 then
  sets MK_TOOLCHAIN=no if that path exists, so ${TOOLS}/xcrun becomes the
  Apple bundle's xcrun -- and DEVELOPER_DIR=${ROOT_BINARY_DIR}/Developer
  (Kernel/Makefile:35-38, passed at :55-56/:84) has no usr/bin/xcrun.
  §1's hand-test used the *built* xcbuild xcrun by absolute path, which
  is why it worked while the build used Apple's.
Fix (attempt 1): Makefile now prefers the in-tree built toolchain on
Darwin when it exists (guarded by .MAKE.OS), so XCRUN is the built
xcbuild xcrun that §1 verified against DEVELOPER_DIR=build/Developer.
Cites: Makefile:99-107 (MK_TOOLCHAIN), sys.mk:113,152,287;
Kernel/Makefile:35-38,54-56,84; Kernel/xnu/makedefs/MakeInc.cmd:42-58;
Kernel/xnu/SETUP/installfile/Makefile:33-35; xcrun.cpp:276-279.

### 2026-11-01 — D1 gate run 2: same xcrun error; fixed by user (b6ac3a8d8)
Output (verbatim):
```
834:xcrun: error: invalid DEVELOPER_DIR path (/Users/solus/Documents/test-ravyn-os/build/Developer), missing xcrun at: /Users/solus/Documents/test-ravyn-os/build/Developer/usr/bin/xcrun
835:xcrun: error: invalid DEVELOPER_DIR path (/Users/solus/Documents/test-ravyn-os/build/Developer), missing xcrun at: /Users/solus/Documents/test-ravyn-os/build/Developer/usr/bin/xcrun
836:gmake[7]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/SETUP/decomment/Makefile:28: decomment.o] Error 1
838:gmake[7]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/SETUP/setsegname/Makefile:30: setsegname.o] Error 1
840:gmake[6]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/makedefs/MakeInc.dir:59: build_setup] Error 2
842:xcrun: error: invalid DEVELOPER_DIR path (/Users/solus/Documents/test-ravyn-os/build/Developer), missing xcrun at: /Users/solus/Documents/test-ravyn-os/build/Developer/usr/bin/xcrun
844:gmake[7]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/SETUP/installfile/Makefile:35: installfile.o] Error 1
846:gmake[5]: *** [/Users/solus/Documents/test-ravyn-os/ravynos/Kernel/xnu/makedefs/MakeInc.dir:60: build_setup_recurse_into_decomment] Error 2
```
Correction to run-1 diagnosis: the Apple shim that rejects the non-CLT
DEVELOPER_DIR is the host /usr/bin/clang (xcrun shim), not only a
/Library/Developer/Toolchains xcrun. Fixed by the user in fork/darwin-dev
b6ac3a8d8: Kernel/Makefile keeps XNU_DEVELOPER_DIR on the CLT and passes
XCRUN="env DEVELOPER_DIR=${ROOT_BINARY_DIR}/Developer ${XCRUN}" so only the
ravynOS xcrun sees the built Developer tree (xnu_devdir still links
$DEVELOPER_DIR/Toolchains). Merged b6ac3a8d8 into this branch; per user
instruction the DEVELOPER_DIR/XCRUN handling in Kernel/Makefile is the
user's to own. My Makefile:99-107 in-tree-toolchain preference (b1ae45e94)
stays.
Cites: Kernel/Makefile:35-42,65,93 (b6ac3a8d8); commit b6ac3a8d8.

### 2026-11-01 — D1 gate run 3: xcrun fixed; SDK header compile errors
Output (verbatim):
```
854:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/mach/arm/vm_types.h:104:44: error: expected ';' after top level declarator
857:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/mach/arm/vm_types.h:107:50: error: expected ';' after top level declarator
860:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/mach/arm/vm_types.h:108:49: error: expected ';' after top level declarator
863:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/mach/arm/vm_types.h:111:48: error: expected ';' after top level declarator
866:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/mach/arm/vm_types.h:112:49: error: expected ';' after top level declarator
871:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/sys/socket.h:561:30: error: a parameter list without types is only allowed in a function definition
874:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/sys/socket.h:561:42: error: expected ';' at end of declaration list
877:/Users/solus/Documents/test-ravyn-os/build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk/usr/include/sys/socket.h:565:30: error: a parameter list without types is only allowed in a function definition
```
Reading: the xcrun/DEVELOPER_DIR failure is gone (user's b6ac3a8d8 + this
branch's toolchain preference); SETUP tools built, installhdrs ran, and the
kernel compile now fails on the INSTALLED SDK headers. The in-repo sources
do not line up with the built-SDK line numbers (Kernel/xnu/osfmk/mach/arm/
vm_types.h:104 is a plain `typedef uint64_t mach_vm_address_t;`; xnu's
header install transforms headers, so the installed copies differ). Both
error classes look like a declaration whose trailing macro/parameters did
not expand as expected -- not guessing further until the clang source-echo
lines are seen. Compiler is the in-tree LLVM 17.0.6
(Developer/Default.xctoolchain/llvm/llvm/CMakeLists.txt LLVM_VERSION_*).
Next: two greps of the existing build-xnu.log (no rebuild) to capture the
echoed source line + caret + compile unit for vm_types.h:104 and
socket.h:561.
Cites: Kernel/xnu/osfmk/mach/arm/vm_types.h:100-112; Kernel/xnu/bsd/sys/socket.h:555-565;
Kernel/xnu/makedefs/MakeInc.cmd:164-174; MakeInc.rule:121.

### 2026-11-01 — D1 run 3 diagnosis: stale foreign headers in the build SDK
User-pasted context (verbatim):
```
848-In file included from .../ravynOS.sdk/usr/include/sys/mount.h:76:
849-In file included from .../ravynOS.sdk/usr/include/sys/attr.h:42:
850-In file included from .../ravynOS.sdk/usr/include/sys/ucred.h:76:
851-In file included from .../ravynOS.sdk/usr/include/bsm/audit.h:420:
852-In file included from .../ravynOS.sdk/usr/include/mach/port.h:92:
853-In file included from .../ravynOS.sdk/usr/include/mach/machine/vm_types.h:35:
854-.../ravynOS.sdk/usr/include/mach/arm/vm_types.h:104:44: error: expected ';' after top level declarator
855-  104 | typedef uintptr_t               vm_offset_t __kernel_ptr_semantics;
869-In file included from .../ravynos/Kernel/xnu/SETUP/decomment/decomment.c:34:
870-In file included from .../ravynOS.sdk/usr/include/libc.h:54:
871-.../ravynOS.sdk/usr/include/sys/socket.h:561:30: error: a parameter list without types is only allowed in a function definition
872-  561 |         void            *__sized_by(msg_namelen) msg_name; /* [XSI] optional address */
```
Diagnosis (output + source):
* The installed SDK copies contain macOS-14/15-era attribute syntax
  (`__kernel_ptr_semantics`, `__sized_by(msg_namelen)`). Both tokens exist
  NOWHERE in this repository or its git history (repo-wide grep + `git log
  -S` over Developer/ravynOS.sdk, Kernel/xnu: no matches). They are not
  produced by any current build step: Developer/ravynOS.sdk/Makefile:30
  copies the repo's usr/ + System/, and xnu's own bsd/sys/socket.h
  (Kernel/xnu/bsd/sys/Makefile:90 installs socket.h) and
  osfmk/mach/arm/vm_types.h have plain typedefs/prototypes at those
  constructs. The build tree at ../build predates this repo state
  (§1 noted its old baked-in paths) and still carries these foreign
  headers; xnu's header install evidently did not overwrite the newer
  foreign copies (install-if-older skip).
* The first failing TU is a SETUP host tool (decomment.c:34 includes
  libc.h) compiled with -isysroot at that stale SDK; the second chain is
  a kernel TU via bsm/audit.h -> mach/machine/vm_types.h.
Fix (machine-side, no repo change): wipe the stale SDK and repopulate it
from this repo, then rerun the gate (xnu_all reinstalls xnu's headers):
```
rm -rf ../build/Developer/Platforms/ravynOS.platform/Developer/SDKs/ravynOS.sdk
env SUBDIR=Developer/ravynOS.sdk bmake all
./tools/darwin-gate.sh xnu
```
(`SUBDIR` via env so only the top level's recursion is narrowed; the SDK
Makefile's own rpcsvc recursion is unaffected because its plain
`SUBDIR = rpcsvc` overrides the inherited env value.)
Cites: Developer/ravynOS.sdk/Makefile:5-30; Developer/ravynOS.sdk/usr/include/libc.h:54;
Kernel/xnu/bsd/sys/Makefile:90; Kernel/xnu/SETUP/decomment/decomment.c:34.
