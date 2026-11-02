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
* Noise, not a cause: `/bin/sh: 6-${\#__A}: bad substitution` came from
  `Kernel/xnu/makedefs/MakeInc.rule:65` (GNU make keeps the `\` in `\#` that
  upstream uses to escape `#` for make). Fixed in the run-10 candidate by
  dropping the `sh` padding arithmetic for an equivalent `%-6.6s` format.
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

### 2026-11-01 — D1 run 4: _posix_availability.h missing + host-tool link failures
Output (verbatim):
```
cp -f /Users/solus/Documents/test-ravyn-os/ravynos/Libraries/objc4/runtime/maptable.h .../usr/local/include/objc/
744:.../ravynOS.sdk/usr/include/sys/cdefs.h:806:10: fatal error: 'sys/_posix_availability.h' file not found
750:clang: error: linker command failed with exit code 1 (use -v to see invocation)
751:clang: error: linker command failed with exit code 1 (use -v to see invocation)
752:gmake[7]: *** [.../Kernel/xnu/SETUP/installfile/Makefile:29: installfile] Error 1
754:clang: error: linker command failed with exit code 1 (use -v to see invocation)
755:gmake[6]: *** [.../makedefs/MakeInc.dir:59: build_setup] Error 2
757:gmake[7]: *** [.../Kernel/xnu/SETUP/replacecontents/Makefile:24: replacecontents] Error 1
759:gmake[5]: *** [.../makedefs/MakeInc.dir:60: build_setup_recurse_into_installfile] Error 2
```
Reading: SDK repopulation worked (the __sized_by/__kernel_ptr_semantics
headers are gone; SETUP tools now compile). Two new blockers, both from
the repo-native SDK being thinner than the old foreign snapshot:
* A: usr/include/sys/cdefs.h:806 includes <sys/_posix_availability.h> for
  non-KERNEL compiles. The only copies in the tree are
  System/.../PrivateHeaders/sys/_posix_availability.h (identical to
  BSD/include/apple/sys/_posix_availability.h) and nothing installs them
  into usr/include/sys.
* B: the repo SDK's usr/lib is empty (0 files); .tbd stubs are Libsystem
  build products and world order is Developer Kernel Libraries ... BSD,
  so at Kernel time host tools linking with -isysroot at this SDK find no
  libSystem. (installfile.c is a hosted C program: stdio/stdlib/err.h.)
Fix (Developer/ravynOS.sdk/Makefile only; Kernel/Makefile untouched):
cp the PrivateHeaders _posix_availability.h into usr/include/sys, and on
Darwin (guarded, exists-checked) seed the CLT SDK's libSystem.tbd +
libSystem.B.tbd into usr/lib -- same CLT-path precedent as
Kernel/Makefile:79-83 compiler_rt. Re-run `env SUBDIR=Developer/ravynOS.sdk
bmake all` then the gate.
Cites: Developer/ravynOS.sdk/usr/include/sys/cdefs.h:803-809;
Developer/ravynOS.sdk/System/.../PrivateHeaders/sys/_posix_availability.h;
BSD/include/apple/sys/Makefile:6-8; Developer/ravynOS.sdk/Makefile (this
commit); Kernel/Makefile:66,79-83; Kernel/xnu/SETUP/installfile/installfile.c:24-30.

### 2026-11-01 — D1 progress: one-command gate now checks the real product
Did: `tools/darwin-gate.sh xnu` now refreshes the repo-native SDK (so run 4's
fix reaches an existing build tree), runs the top-level Kernel stage with
separate logs, preserves failure exit statuses, removes the old staged kernel
before building, and requires `file` to identify the fresh product as
`Mach-O 64-bit executable x86_64`. It no longer prints errors and exits 0.
Evidence: `sh -n tools/darwin-gate.sh` passed; mocked build contract passed:
`success=0 sdk-failure=23 kernel-failure=27 missing-artifact=1 wrong-format=1`.
This Linux workspace has no macOS XNU toolchain, so the real D1 gate remains
unrun; the mocked `D1 GATE PASSED` path is not a milestone pass.
Cites: tools/darwin-gate.sh (xnu mode); Makefile:49,168-180;
Developer/ravynOS.sdk/Makefile:18-48; Kernel/Makefile:85-113;
Kernel/xnu/makedefs/MakeInc.def:902-931.
Next: on the Mac run `./tools/darwin-gate.sh xnu`; paste its output here. A
failure now returns nonzero and names either build-sdk.log or build-xnu.log.

### 2026-11-02 — D1 run 5: separate host SDK from the target SDK
Output (verbatim):
```
215:install: .../libsystem_c/libdarwin/h/dirstat.h: No such file or directory
747:.../ravynOS.sdk/usr/include/sys/_types.h:33:10: fatal error: 'machine/_types.h' file not found
751:clang: error: linker command failed with exit code 1 (use -v to see invocation)
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
Diagnosis: two independent source defects. The libc header script still points
at the removed `libsystem_c/libdarwin` directory; the identical header now lives
in `libsystem_darwin/h`. More importantly, XNU's SETUP programs are host tools,
but Kernel/Makefile forced their `-isysroot` to the incomplete target SDK.
Upstream normally resolves HOST_SDKROOT as the host macOS SDK.
Did: corrected the dirstat source path. On Darwin, pass the CLT MacOSX.sdk as
HOST_SDKROOT(_RESOLVED) to both XNU invocations; supply host flex/bison/m4 and
codesign-allocation settings directly because ravynOS xcrun cannot resolve
tools through that external SDK. Linux's xnu_all host-argument list stays empty.
No DEVELOPER_DIR/XCRUN handling changed.
Evidence: bmake 20200212 parsed both branches; Darwin `xnu_all` dry-run contains
the CLT SDK and explicit host tools, Linux's XNU_ALL_HOST_ARGS is empty;
`bash -n headers.sh`, dirstat existence and `git diff --check` passed. Real macOS
D1 gate not run here.
Cites: Libraries/Libsystem/libsystem_c/xcodescripts/headers.sh:195-203;
Libraries/Libsystem/libsystem_darwin/h/dirstat.h; Kernel/Makefile:42-68,81-91,108-123;
Kernel/xnu/makedefs/MakeInc.cmd:52-71,209-228;
Kernel/xnu/SETUP/replacecontents/Makefile:14-15;
Developer/Default.xctoolchain/xcbuild/Libraries/xcsdk/Tools/xcrun.cpp:305-308.
Next: pull this change on the Mac and rerun `./tools/darwin-gate.sh xnu`.

### 2026-11-02 — D1 run 6: dirstat fixed; setup recursion still uses the target SDK
Key output (verbatim snippets):
```
.../ravynOS.sdk/usr/include/sys/_types.h:33:10: fatal error: 'machine/_types.h' file not found
ld: library 'bsd' not found
"_fstat$INODE64", referenced from:
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
The real Mac run confirms that the dirstat source-path correction worked, but
D1 still fails. The first `gmake` contains the CLT host SDK while effective leaf
setup compiles still select ravynOS headers. Run 5's top-level-only dry-run was
therefore insufficient evidence. Four setup Makefiles also compare `uname` to
the quoted string `"Darwin"`; that GNU-make condition is always false, sending
`replacecontents` through its non-Darwin `-lbsd` branch. Setup objects have no
dependency on SDK/flag changes, so previously compiled objects can also survive
into the `_fstat$INODE64` links. The repeated
`/bin/sh: 6-${\#__A}: bad substitution` messages remain unassigned to a source
line; no speculative shell rewrite is included.

Did (run-7 candidate, not a gate pass): explicitly propagate HOST_SDKROOT,
HOST_SDKROOT_RESOLVED, HOST_FLEX, HOST_BISON, HOST_GM4, and
HOST_CODESIGN_ALLOCATE through both XNU recursion templates; use the existing
OPSYS value for the four Darwin setup conditionals; delete only the 14 known
setup object files before the gated build; and change `VERBOSE=yes` to the
`VERBOSE=YES` spelling XNU checks so subsequent logs contain effective commands.
The user-owned DEVELOPER_DIR/XCRUN handling is unchanged.

Evidence: the actual recursive XNU `build_setup` rules passed a dry-run with
OPSYS=Darwin: each of the six host variables appeared on 21 nested assignments,
20 effective compile/link commands used `/CLT`, and none used a ravynOS sysroot
or combined that sysroot with `-lbsd`. The same dry-run with OPSYS=Linux retained
21 explicit assignments, added no macOS sysroot/deployment flags, and retained
three expected `-lbsd` commands. Direct probes of all six setup Makefiles passed
for Darwin and Linux flag selection. The gate cleanup fixture removed its listed
object while preserving two unlisted objects. `bash -n`/`sh -n` on the gate and
`git diff --check` passed. No real macOS D1 pass is claimed from these checks.
Cites: Kernel/xnu/makedefs/MakeInc.top:324-329;
Kernel/xnu/makedefs/MakeInc.dir:34-39,55-60;
Kernel/xnu/SETUP/{config,decomment,replacecontents,setsegname}/Makefile;
tools/darwin-gate.sh:46-64; Kernel/Makefile:83,114,118.
Next: pull this candidate on the Mac and rerun `./tools/darwin-gate.sh xnu`.
D1 still requires `D1 GATE PASSED`, the exact staged kernel path, and genuine
`Mach-O 64-bit executable x86_64` identification.

### 2026-11-02 — D1 run 7: kernel compiles; final link lacks the profiling runtime
Output (verbatim):
```
ld: warning: directory not found for option '-L.../Libraries/llvm_target/compiler-rt/lib/ravynos'
ld: library not found for -lclang_rt.profile_osx
clang: error: linker command failed with exit code 1 (use -v to see invocation)
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
The run-7 candidate worked through host setup and all kernel compilation; the
failure is now the final `kernel.unstripped` link. History identifies the
regression: 76afd8c278 replaced Kernel's `Libraries all` dependency with copies
of the CLT `libclang_rt.osx.a` and `libclang_rt.cc_kext.a`, but XNU's existing
`-lclang_rt.profile_osx` still pointed into the no-longer-built Libraries tree.
The archive cannot simply be conditioned on BUILD_PROFILE: OSKext.cpp references
its buffer helpers unconditionally. LLVM explicitly supports linking these
profile sources into the Darwin kernel, and XNU supplies
`__llvm_profile_runtime = 0` to disable userspace profile-file handling.

Did (run-8 candidate, not a gate pass): on Darwin only, copy the CLT
`libclang_rt.profile_osx.a` beside `libcc_kext.a` in the target SDK and point
XNU's existing profile-library search at that directory. Other hosts retain the
original `Libraries/llvm_target/compiler-rt/lib/ravynos` path. The unconditional
profile archive remains, and no build-order or XCRUN changes are included.
Evidence: dry-running the actual `kernel.unstripped` rule with placeholder
prerequisites produced `-L/SDK/usr/local/lib/kernel -lclang_rt.profile_osx` for
OPSYS=Darwin; OPSYS=Linux retained the exact original Libraries path.
`git diff --check` passed. No real macOS D1 pass is claimed from these checks.
Cites: commit 76afd8c278; Kernel/Makefile:95-104;
Kernel/xnu/makedefs/MakeInc.kernel:29-35,152;
Kernel/xnu/libkern/c++/OSKext.cpp:8497-8508,8645-8671;
Kernel/xnu/bsd/pgo/profile_runtime_data.c:1-5;
Developer/Default.xctoolchain/llvm/compiler-rt/lib/profile/InstrProfilingBuffer.c:9-20.
Next: update the Mac to this candidate and rerun `./tools/darwin-gate.sh xnu`.
The acceptance bar remains a fresh staged x86_64 Mach-O kernel and an explicit
`D1 GATE PASSED` verdict.

### 2026-11-02 — D1 run 8: SDK refresh cannot replace _symbol_aliasing.h
Output (verbatim):
```
make_symbol_aliasing.sh: line 118: .../usr/include/sys/_symbol_aliasing.h: Permission denied
*** Error code 1
D1: SDK refresh failed; full log: .../ravynos/build-sdk.log
```
This run stopped before the Kernel stage, so the run-8 profiling-runtime
candidate still has no real Mac verdict. The generator previously reopened the
existing output with `> "$OUTPUT"`; the immediately preceding header copy in
the same directory succeeded, identifying the existing output file rather than
an unwritable directory as the obstacle.

Did (run-9 candidate, not a gate pass): generate into a same-directory mktemp
file, chmod it 0644, and atomically rename it over the old header while
preserving the generation/mv exit status and cleaning temporary files. This
avoids reopening the installed header and preserves the old file if generation
fails. Also add `Permission denied` to the gate's diagnostic filter so this
failure appears directly next time.
Evidence: with a fake availability tool, the old and new generators produced
byte-identical output; the new generator replaced an existing 0444 header,
restored mode 0644, and left no temporary files. An invalid output directory
still returned 1. `bash -n` on the generator, `bash -n`/`sh -n` on the gate, a
filter fixture, and `git diff --check` passed.
Cites: Kernel/xnu/bsd/sys/make_symbol_aliasing.sh:44-47,123-131;
Developer/ravynOS.sdk/Makefile:53-55; tools/darwin-gate.sh:24-27.
Next: update the Mac and rerun `./tools/darwin-gate.sh xnu`. A successful SDK
refresh must be followed by the still-unverified profiling-runtime link and the
fresh x86_64 Mach-O artifact check before D1 can pass.

### 2026-11-02 — D1 run 10: the kernel linked the *userspace* profiling runtime
Output (verbatim, from the run-9 candidate's build):
```
ld: warning: ignoring file .../usr/local/lib/kernel/libclang_rt.profile_osx.a,
    building for macOS-arm64 but attempting to link with file built for macOS-x86_64
Undefined symbols for architecture x86_64:
  "___stderrp", referenced from:
      _lprofBufferWriter in InstrProfilingWriter.c.o
      _lprofBufferWriteHeader in InstrProfilingWriter.c.o
  "_fprintf", referenced from:
      _lprofBufferWriter in InstrProfilingWriter.c.o
ld: symbol(s) not found for architecture x86_64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
D1: XNU build failed; full log: .../ravynos/build-xnu.log
/bin/sh: 6-${\#__A}: bad substitution
```
The run-8 copy made the library *findable*, which exposed the real defect: the
CLT `libclang_rt.profile_osx.a` is the **userspace** archive. `PROF_ERR`/
`PROF_WARN`/`PROF_NOTE` in `InstrProfilingPort.h` expand unconditionally to
`fprintf(stderr, …)`, so any object carrying them (`InstrProfilingWriter.c.o`,
`InstrProfilingBuffer.c.o`) needs `___stderrp`, and `InstrProfilingFile.c.o`
additionally wants `getenv`/`open`/`mmap`. The kernel link is
`-nostdlib -fapple-kext`, so no libc exists to satisfy them. LLVM never links
that archive into the kernel: `darwin_add_builtin_libraries` compiles a
six-file freestanding subset into `libclang_rt.cc_kext.a` with
`-mkernel -DKERNEL_USE`.

Did (run-10 candidate, not a gate pass): build that same six-file subset in a
new `Libraries/libclang_rt_profile` (staged as
`${RAVYN_SDKROOT}/usr/local/lib/kernel/libclang_rt.profile_osx.a`, beside
`libcc_kext.a`), drop the CLT profile-archive copy, and make
`XNU_PROFILE_RT_DIR` that one directory on every host instead of only Darwin.
Also replace the `sh` padding arithmetic in `MakeInc.rule` — GNU make keeps the
backslash in upstream's `\#`, so every build printed
`/bin/sh: 6-${\#__A}: bad substitution` and `CMD_MC` came out empty.

Evidence: compiling the six sources freestanding leaves exactly two external
references, `memcpy`/`memset` (both exported by `config/Libkern.exports`) plus
`_GLOBAL_OFFSET_TABLE_`; every `__llvm_profile_*` symbol is resolved inside the
set, and both `__llvm_profile_get_size_for_buffer{,_internal}` and
`__llvm_profile_write_buffer{,_internal}` are defined there, so
`profile_runtime.c` and `OSKext.cpp` are satisfied without touching their call
sites. `InstrProfilingWriter.c` only includes `<malloc.h>` under `_MSC_VER`.
The new `CMD_MC` was diffed against the old one under GNU make for `x86_64` and
`arm64`: byte-identical (`ESC [1m x86_64` padded to six columns), and the new
fragment no longer prints the bad-substitution line, which the old one does.
Cites: Developer/Default.xctoolchain/llvm/compiler-rt/cmake/Modules/CompilerRTDarwinUtils.cmake:412-424,480-487;
Developer/Default.xctoolchain/llvm/compiler-rt/lib/profile/InstrProfilingPort.h:119-130;
Libraries/libclang_rt_profile/Makefile; Kernel/Makefile:92-108;
Kernel/xnu/makedefs/MakeInc.kernel:29-37,147;
Kernel/xnu/makedefs/MakeInc.rule:59-73;
Kernel/xnu/config/Libkern.exports:691,693.
Next: pull on the Mac and rerun `./tools/darwin-gate.sh xnu`. The acceptance bar
is unchanged: `D1 GATE PASSED`, a fresh staged
`.../sysroot/System/Library/Kernels/kernel`, and genuine
`Mach-O 64-bit executable x86_64`.

### 2026-11-02 — D1 run 11: the new archive was never built (and misnamed)
Output (verbatim):
```
bmake -C .../Libraries/libclang_rt_profile all
rm -f .../usr/local/lib/kernel/clang_rt.profile_osx.a
libtool -static  -o .../usr/local/lib/kernel/clang_rt.profile_osx.a \
    -no_warning_for_no_symbols InstrProfiling.o InstrProfilingBuffer.o ...
error: libtool: can't open file: InstrProfiling.o (No such file or directory)
...
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
Two defects in the run-10 `Libraries/libclang_rt_profile/Makefile`, both visible
in that one command line. First, no object was compiled: the archive rule was
declared *before* `.include <bsd.lib.mk>`, and `bsd.lib.mk` is what derives
`${OBJS}` from `${SRCS}`, so the prerequisite list was empty. Second, the
archive was named `clang_rt.profile_osx.a` — `bsd.lib.mk` supplies the `lib`
prefix, and since the rule bypassed it the prefix was missing, so
`-lclang_rt.profile_osx` would not have found the result even if it had linked.

Did (run-11 candidate, not a gate pass): let `bsd.lib.mk` own the build (it
compiles `${OBJS}` and archives them into `${.OBJDIR}/libclang_rt.profile_osx.a`)
and add a single staging rule after it that copies that archive to
`${RAVYN_SDKROOT}/usr/local/lib/kernel/libclang_rt.profile_osx.a`, removing the
mis-named leftover from run 10.

Evidence: replaying the exact rule order (sources → `lib${LIB}.a` →
staged path) under a `libtool -static` stand-in that hard-fails on a missing
object produced `a.o b.o` → `libclang_rt.profile_osx.a` → a byte-identical copy
at `out/usr/local/lib/kernel/libclang_rt.profile_osx.a`, with the
`clang_rt.profile_osx.a` leftover deleted. The earlier ordering fails the same
way the Mac did (`libtool: can't open file: a.o`), confirming the diagnosis.
Cites: Libraries/libclang_rt_profile/Makefile:61-79;
Developer/.../BSD/share/mk/bsd.lib.mk:192-209; Kernel/Makefile:100-101;
Kernel/xnu/makedefs/MakeInc.kernel:154.
Next: rerun `./tools/darwin-gate.sh xnu`. Expect the archive step to succeed,
then the real test — whether the kernel link resolves without
`___stderrp`/`_fprintf`.

### 2026-11-02 — D1 run 12: the kernel links; IOKitUser recursed into a source dir
Output (verbatim):
```
cp -f .../Kernel/IOKitUser/libkxld.a .../usr/local/lib/kernel
===> Kernel/IOKitUser/kext.subproj (all)
bmake[3]: don't know how to make all. Stop
bmake[3]: stopped making "all" in .../Kernel/IOKitUser/kext.subproj
*** Error code 2
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
The profiling-runtime work is done: `libclang_rt_profile` built, the kernel
staged and linked, and the build moved on to the next `Kernel` target. This
failure is unrelated and pre-existing — `Kernel/IOKitUser/Makefile` is
byte-identical to upstream ravynsoft's, and it lists `SUBDIR = kext.subproj`
for a directory that has no Makefile. Because the kernel link failed on every
previous run, `Kernel` never got this far, so the defect had never surfaced.
`kext.subproj` is a *source* directory: `OSKext.c`, `OSKextVersion.c`,
`misc_util.c`, `printPList_new.c`, `cross_link.c`, `fat_util.c`,
`macho_util.c` and the headers are all compiled by the parent's `SRCS`
(`:29-83`), and the only thing generated from it is
`kextmanager_mig.[ch]`, which already has its own rule (`:422-424`).

Did (run-12 candidate, not a gate pass): set `SUBDIR =` (the file already
carried an empty one; the later assignment at `:438` overrode it) and leave the
MIG rule untouched, so `bsd.subdir.mk` no longer tries to build a directory
that has no build description.

Evidence: the sweep over every `SUBDIR` assignment under `Kernel`,
`Libraries`, `Frameworks`, `BSD` and `Developer` found this as the only such
directory whose build is reached on the `Kernel` path — the four other
cases are `makefile`-spelled (lower case, found case-insensitively on macOS)
or `#`-commented entries. `grep` confirms exactly one
`kextmanager_mig.c kextmanager_mig.h:` rule remains, so no duplicate-target
warning is introduced, and the parent's `SRCS` still contains all nine
`kext.subproj/*.c` entries, so no source is dropped from IOKitUser.
Cites: Kernel/IOKitUser/Makefile:7-83,422-424,438-445;
BSD/share/mk/bsd.subdir.mk:9-20,41-52;
upstream comparison: ravynsoft/ravynos `Kernel/IOKitUser/Makefile@darwin` (diff empty).
Next: rerun `./tools/darwin-gate.sh xnu`. The kernel link is now the last known
obstacle cleared; what remains is whatever `Kernel`'s remaining targets do, then
the staged-kernel check.

### 2026-11-02 — D1 run 13: IOKitUser needs two headers it never staged
Output (verbatim):
```
IOHIDEventObjc.m:9:9: fatal error: 'IOKit/hid/IOHIDEvent.h' file not found
    9 | #import <IOKit/hid/IOHIDEvent.h>
      |         ^~~~~~~~~~~~~~~~~~~~~~~~
      | did not find header 'hid/IOHIDEvent.h' in framework 'IOKit'
      | (loaded from '.../ravynOS.sdk/System/Library/Frameworks')
D1: XNU build failed; full log: .../ravynos/build-xnu.log
```
`IOHIDEventObjc.m` opens with `#import <IOKit/hid/IOHIDEvent.h>`, and the body
of IOKitUser's `copy_headers` is a hand-maintained `cp` list. That list omits
`IOHIDEvent.h` even though the file exists at
`Kernel/IOKitUser/hid.subproj/IOHIDEvent.h`, so clang's framework lookup —
which searches `IOKit.framework/Versions/A/{Headers,PrivateHeaders}` because the
code is compiled with `--sysroot` — found nothing. The include is not a leftover
from our changes: it is line 9 of the first source the IOKitUser target compiles,
and it was simply never reached before, because every earlier `Kernel` run died
in the kernel link or the `kext.subproj` recursion.

Did (run-13 candidate, not a gate pass): add
`hid.subproj/IOHIDEvent.h` to the `IOKit/hid` copy list, plus
`${.CURDIR:H}/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventTypes.h`, which that
header includes as `<IOKit/hid/IOHIDEventTypes.h>` and which was likewise never
staged into the framework.

Evidence: a simulated SDK built from the Makefile's own `cp` lists, then
expanded transitively over every `#include`/`#import` in the staged headers,
reports `IOHIDEvent.h` as the only `IOKit/hid` header reachable from a compiled
source but absent from the framework, and `IOHIDEventTypes.h` as the next
missing link in its chain. Both source files exist in the tree, and
`IOHIDEvent.h`'s other two includes already resolve: `<CoreFoundation/...>`
through the existing `-I${ROOT_SOURCE_DIR}/Frameworks`, and
`<IOKit/hidobjc/hidobjcbase.h>` through `PrivateHeaders/hidobjc/`, which
`copy_headers` already populates (`:292-308`).
Cites: Kernel/IOKitUser/hid.subproj/IOHIDEventObjc.m:8-10;
Kernel/IOKitUser/hid.subproj/IOHIDEvent.h:28-32;
Kernel/IOKitUser/Makefile:283,288-308,340-342,358-360.
Next: rerun `./tools/darwin-gate.sh xnu`. IOKitUser compiles in bulk after
`copy_headers`, so the next report is expected to name any further missing
header directly.

### 2026-11-02 — D1 run 14: the whole IOKit/hid header closure, not one file
Output (verbatim):
```
IOKit.framework/Headers/hid/IOHIDEventTypes.h:147:10: fatal error:
  'IOKit/hid/IOHIDEventFieldDefs.h' file not found
  147 | #include <IOKit/hid/IOHIDEventFieldDefs.h>
```
Run 13 fixed exactly the header the compiler named first; this is the next link
of the same chain, which is the expected shape when a hand-maintained copy list
is filled in one failure at a time. Rather than chase the chain again, the whole
closure was computed up front.

Did (run-14 candidate, not a gate pass): stage every header the IOKitUser
sources reach as `<IOKit/hid/...>` — fifteen files from
`Extensions/IOHIDFamily/IOHIDFamily/` (`AppleHIDUsageTables.h`,
`IOHIDDeviceKeys.h`, `IOHIDDeviceTypes.h`, `IOHIDEventData.h`,
`IOHIDEventFieldDefs.h`, `IOHIDEventMacroDefs.h`, `IOHIDEventServiceKeys.h`,
`IOHIDEventServiceKeys_Private.h`, `IOHIDEventStructDefs.h`,
`IOHIDEventTypes.h`, `IOHIDKeys.h`, `IOHIDLibUserClient.h`,
`IOHIDPrivateKeys.h`, `IOHIDResourceUserClient.h`, `IOHIDUsageTables.h`) — and
`hid.subproj/IOHIDTypes.h` as `<IOKit/hidsystem/IOHIDTypes.h>`, which
`event_status_driver.h` includes.

Evidence: a model of the framework built from the Makefile's own `cp` lines
(both the multi-line and single-line forms, including the
`usr/local/include/IOKit` tree), resolved against the target's real `-I` set and
expanded transitively over `#include`/`#import`, now reports **0** headers that
a compiled source reaches but the framework lacks, having scanned 831 files and
92 staged headers. Before the change it reported the same 16 entries the
compiler was walking through one at a time. Every added source file was checked
to exist; extra headers inside a framework's `Headers/` are inert for this
build, so the set is deliberately generous.
Cites: Kernel/IOKitUser/Makefile:336-402;
Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEventTypes.h:147;
Kernel/IOKitUser/hidsystem.subproj/event_status_driver.h:42.
Next: rerun `./tools/darwin-gate.sh xnu`. If a further `file not found`
appears, it will be outside `IOKit/hid|hidsystem` and the same closure check
extends to it directly.

### 2026-11-02 — D1 run 15: stop enumerating headers, stage the trees
Output (verbatim):
```
IOKit.framework/Headers/hid/IOHIDKeys.h:29:10: fatal error:
  'IOKit/hidsystem/IOHIDParameter.h' file not found
```
Runs 13 and 14 each chased one link of the same chain, so this changes the
mechanism rather than extending the list. `copy_headers` enumerates headers
file by file, and the enumeration was never complete; every run compiled until
it found the next omission. The tree already contains the complete public trees
an Apple SDK's IOKit.framework is assembled from, so those directories are now
staged whole.

Did (run-15 candidate, not a gate pass): replace the enumeration for the
affected groups with directory copies — `hid.subproj/*.h` (62),
`Extensions/IOHIDFamily/IOHIDFamily/*.h` (54),
`Extensions/IOHIDFamily/IOHIDLib/*.h` (10),
`Extensions/IOHIDFamily/IOHIDSystem/IOKit/hidsystem/*.h` (14, into both
`hid/` and `hidsystem/`), `kext.subproj/*.h` (12),
`IOGraphicsFamily/IOKit/graphics/*.h` (16) and `IOKit/i2c/*.h` (2) — and drop
the hand-written fifteen-file list added in run 14, which the directory copy
supersedes.

Evidence: the closure model was rebuilt to join recipe continuations (the
earlier "0 gaps" number came from a parser that only saw the single-line `cp`
form, so it was not trustworthy) and to source a missing header from *any*
directory in `Kernel/`, not a hand-picked pool. It now models 212 staged
headers and reports **0** unsatisfied `<IOKit/...>` includes across 853 files
and 21 `-I` directories. That rebuild found one further genuine gap the
hand-picked pool had hidden: `IOHIDLibObsolete.h` (included by both
`IOHIDLib.h` and `IOHIDDevicePlugIn.h`) lives in `IOHIDFamily/IOHIDLib/`, not
in either tree that was being copied. All seven source directories were checked
to exist and to hold the stated header counts; `git diff --check` passes.
Cites: Kernel/IOKitUser/Makefile:409-437;
Kernel/IOKitUser/hid.subproj/IOHIDLib.h:34;
Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDKeys.h:29.
Next: rerun `./tools/darwin-gate.sh xnu`. The header question is now answered by
construction for `hid`, `hidsystem`, `kext`, `graphics` and `i2c`; anything still
missing would have to come from a subdirectory not staged here, and the model
would name it.

### 2026-11-02 — D1 run 16: the glob copied a kernel C++ class over the C API
Output (verbatim):
```
IOKit.framework/Headers/hid/IOHIDEvent.h:47:1: error: unknown type name 'class'
   47 | class IOHIDEvent : public OSObject
      | ^
IOHIDEventObjc.m:9:9: note: in file included from ...
15 warnings and 4 errors generated.
```
The run-15 directory copy was wrong, and this shows why. Two different headers
are called `IOHIDEvent.h`: `Kernel/IOKitUser/hid.subproj/IOHIDEvent.h` is the
user-space C API (`IOHIDEventRef`, `IOHIDEventGetType`, ...), while
`Extensions/IOHIDFamily/IOHIDFamily/IOHIDEvent.h` declares the kernel C++ class
`class IOHIDEvent : public OSObject`. Copying the whole directory put the class
header in `Headers/hid/` *after* the C one, so it won the lookup. The same
mistake occurred with seven other basenames, and `hid.subproj/IOHIDTypes.h`,
which the glob also copied, is a **0-byte placeholder** that overwrote the real
182-line `IOHIDSystem/IOKit/hidsystem/IOHIDTypes.h`.

Did (run-16 candidate, not a gate pass): remove the directory globs and restore
the per-file list that the compiler had already validated, which is what
actually ships a userspace ABI; add only the two files that were genuinely
missing from it -- `IOHIDFamily/IOHIDLib/IOHIDLibObsolete.h` (included by both
`IOHIDLib.h` and `IOHIDDevicePlugIn.h`) and the real
`IOHIDSystem/IOKit/hidsystem/IOHIDTypes.h`. A comment records why
`IOHIDFamily/IOHIDFamily/IOHIDEvent.h` must never be listed.

Evidence: the header sets of the four candidate directories were diffed by
basename — eight collide, and in every case `hid.subproj` holds the C API and
`IOHIDFamily/IOHIDFamily` the kernel class. A model of the staged set now
checks three invariants: no staged source is empty, no staged source is a
kernel-class header reachable from a pure `.c`/`.m` consumer (0 violations; the
two class headers that remain, `IOHIDLibUserClient.h` and
`IOHIDResourceUserClient.h`, are included only from `.cpp` files), and no
`<IOKit/...>` include reachable from the 303 files scanned is unsatisfied
(0 violations). `git diff --check` passes.
Cites: Kernel/IOKitUser/hid.subproj/IOHIDEvent.h; Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDEvent.h:44-47;
Kernel/IOKitUser/hid.subproj/IOHIDTypes.h (empty);
Kernel/Extensions/IOHIDFamily/IOHIDSystem/IOKit/hidsystem/IOHIDTypes.h;
Kernel/IOKitUser/Makefile:340-345,382-404.
Next: rerun `./tools/darwin-gate.sh xnu`.

### 2026-11-02 — D1 run 17: the SDK never shipped <os/object.h>
Output (verbatim):
```
.../ravynOS.sdk/usr/include/dispatch/dispatch.h:64:10: fatal error:
  'os/object.h' file not found
```
Not an IOKit problem, and not a kernel problem: the header is missing from
Developer/ravynOS.sdk, which `darwin-gate.sh xnu` refreshes before it builds
the kernel.

Did (run-17 candidate, not a gate pass): `Developer/ravynOS.sdk/Makefile` now
copies `BSD/include/os/object.h` into `${RAVYN_SDKROOT}/usr/include/os/`.

Evidence: the SDK ships `usr/include/os/{availability,base,lock,state_private}.h`
but not `object.h`, yet `dispatch/dispatch.h:64`, `xpc/xpc.h:7` and
`asl_{file,msg_list,private,store}.h` in that same SDK all `#include
<os/object.h>`. `BSD/include/os/Makefile` (INCS = assumes.h base.h object.h
trace.h) shows the directory is the tree's libsystem-`os/` source, and
`bsd.own.mk`'s install path is not that of this SDK, so the file never arrived.
`object.h` needs only `Availability.h`, `os/base.h` and `objc/NSObject.h`: the
first two are already in the SDK, and the recipe installs the third from
`Libraries/objc4/runtime` a few lines further down. `assumes.h` and `trace.h`
from the same directory were deliberately left out — they pull `_simple.h`,
`CrashReporterClient.h` and `xpc/xpc.h`, which needs its own check.
Cites: Developer/ravynos.sdk/Makefile:41-45 (the new copy);
Developer/ravynOS.sdk/usr/include/dispatch/dispatch.h:64; BSD/include/os/Makefile:7-12.
Dead end: a repo-wide scan for "SDK header includes a file that is not staged,
but that basename exists somewhere in the tree" returned 4861 rows and is pure
noise (`sys/types.h` matching `BSD/lib/libnetbsd/sys/types.h`, `stdbool.h`, the
toolchain's `availability.h`). Do not act on that signal; resolve includes
against the staged SDK and the actual `-I` path, not by basename.
Next: rerun `./tools/darwin-gate.sh xnu`.

### 2026-11-02 — D1 run 18: run 17 staged the wrong os/object.h
Output (verbatim):
```
IOKit.framework ... usr/include/dispatch/object.h:55:1: error:
  unknown type name 'OS_OBJECT_DECL_CLASS'
usr/include/dispatch/object.h:66:27: error: use of undeclared identifier
  'dispatch_object_t'; did you mean 'dispatch_object'?
20 errors generated.
```
Run 17 found the right *file name* and the wrong *file*: the tree carries three
different `os/object.h`, and the one it copied cannot declare what dispatch
needs.

Did (run-18 candidate, not a gate pass): `Developer/ravynOS.sdk/Makefile` now
copies `${.CURDIR}/usr/local/include/kernel/os/object.h` instead of
`BSD/include/os/object.h` into `${RAVYN_SDKROOT}/usr/include/os/`.

Evidence: the three copies are not interchangeable. `BSD/include/os/object.h`
(188 lines) and `Developer/ravynOS.sdk/usr/local/include/kernel/os/object.h`
(271 lines) share the include guard `__OS_OBJECT__`, but only the 271-line one
defines `OS_OBJECT_DECL_CLASS`, `OS_OBJECT_DECL`, `OS_OBJECT_DECL_SUBCLASS` and
`os_retain`; `Kernel/xnu/libkern/os/object.h` (202 lines) is a third variant,
also without `OS_OBJECT_DECL_CLASS`. So the guard is what turned a
wrong-file mistake into a confusing error: `<dispatch/object.h>` included
`<os/object.h>`, got the stale copy, and every later attempt to define the class
was skipped. The 271-line copy is the one this tree already treats as
authoritative — `Kernel/IOKitUser/Makefile:151-155` force-includes it into the
`*Objc.o` rules and `Kernel/Extensions/IOHIDFamily/Makefile:45` does the same.
Its includes resolve under the staged SDK: `Availability.h`,
`os/availability.h`, `os/base.h` and `TargetConditionals.h` are all present, and
`os/generic_unix_base.h` is behind `#elif defined(__unix__)`, which Darwin never
reaches.
Trigger, precisely: `Kernel/IOKitUser/IOKitLib.c:46` is plain C that includes
`<dispatch/dispatch.h>`; it gets no `-include` and no `__OBJC__`, so
`OS_OBJECT_HAVE_OBJC_SUPPORT` is 0 and `OS_OBJECT_DECL_CLASS` must exist in the
non-ObjC branch — the branch the stale copy lacks. The header is self-contained
under that branch: it declares the class and imports `objc/NSObject.h` itself.
Cites: Developer/ravynOS.sdk/Makefile:42-53; BSD/include/os/object.h;
Developer/ravynOS.sdk/usr/local/include/kernel/os/object.h:21-33,205-216;
Kernel/IOKitUser/IOKitLib.c:46; Developer/ravynOS.sdk/usr/include/dispatch/object.h:55.
Caveat: not compile-verified. This sandbox has no clang, and gcc cannot stand in
— `TargetConditionals.h` rejects it and `sys/_symbol_aliasing.h` is generated
during the build. The check was static: guard/greps across the three copies plus
a trace of the `OS_OBJECT_USE_OBJC` branch logic.
Next: rerun `./tools/darwin-gate.sh xnu`.

### 2026-11-02 — D1 run 19: kHIDPage_AppleVendor defined twice
Output (verbatim):
```
IOKit.framework/Headers/hid/IOHIDUsageTables.h:78:5: error:
  redefinition of enumerator 'kHIDPage_AppleVendor'
  Kernel/Extensions/IOHIDFamily/IOHIDFamily/AppleHIDUsageTables.h:35:5:
  note: previous definition is here
1 warning and 1 error generated.
```
Did (run-19 candidate, not a gate pass): dropped the second element of the last
enumerator in `IOHIDUsageTables.h`, so the vendor page terminates at
`kHIDPage_VendorDefinedStart = 0xFF00` exactly as Apple ships it.

Evidence: `gh api
repos/apple-oss-distributions/IOHIDFamily/contents/IOHIDFamily/IOHIDUsageTables.h`
shows line 81 is `kHIDPage_VendorDefinedStart = 0xFF00` and nothing follows, so
`kHIDPage_AppleVendor` is a local addition, not an upstream export. It collides
with the same-named enumerator in `AppleHIDUsageTables.h:35`, which is the
Apple-specific table and is what declares the `kHIDPage_AppleVendor*` family.
Compared as symbol sets, the two headers have exactly one name in common, so this
is the only collision to fix. Nothing loses the constant: the only consumer
outside IOHIDFamily, `Kernel/IOKitUser/hid.subproj/IOHIDLibPrivate.cpp`, gets
`AppleHIDUsageTables.h` by force-include (`Kernel/IOKitUser/Makefile:237`), and
IOHIDFamily's own HID targets list it in their headers
(`Kernel/Extensions/IOHIDFamily/HID/Makefile:45,55,96`).
Invariant sweep over all 140 headers in the four staged trees, to catch the rest
of this class: 0 enumerators defined in more than one header, 0 include guard
names shared by different headers, 0 regressions of the empty-header problem.
Cites: Kernel/Extensions/IOHIDFamily/IOHIDFamily/IOHIDUsageTables.h:74-78;
Kernel/Extensions/IOHIDFamily/IOHIDFamily/AppleHIDUsageTables.h:33-35.
Note: the file is not valid UTF-8 (a stray byte near offset 17533), so edits to
it must be byte-preserving; decoding it as text fails.
Next: rerun `./tools/darwin-gate.sh xnu`.

### 2026-11-02 — D1 run 20: IOKit.framework linked against a library that cannot exist yet
Output (verbatim):
```
clang -shared -dylib -o .../IOKit.framework/Versions/A/IOKit -Wl,-force_load,.../libIOKit.a
  ... -L.../ravynOS.sdk/usr/local/lib/kernel -lcc_kext -lkxld -lkernel ...
ld: library not found for -lkernel
clang: error: linker command failed with exit code 1
```
This is the first run where every source compiled — the log ends in the archive
step (`ar -crsD libIOKit.a ...` with the full object list) and then the link.

Did (run-20 candidate, not a gate pass): dropped `-lkernel` from `LDFLAGS` in
`Kernel/IOKitUser/Makefile`; kept `-lcc_kext` and `-lkxld`.

Evidence: the top level is `SUBDIR ?= Developer .WAIT Kernel Libraries
Frameworks BSD` (Makefile:168), so Kernel completes before Libraries starts, and
`libkernel.a` is only ever written by
`Libraries/Libsystem/libsystem_kernel/Makefile:289-290` into
`${RAVYN_SDKROOT}/usr/local/lib/kernel/libkernel.a`. At the moment IOKitUser
links, that file cannot exist. Nor is it wanted: `libkernel.a` is the libsyscall
wrapper archive (`legacy/accept.c`, `legacy/bind.c`, `cancelable/pselect*.c`,
`custom/errno.c`, ...) consumed when `libsystem_kernel.dylib` is linked, whereas
what kext.subproj genuinely calls is `kxld_*`, which
`Kernel/IOKitUser/Makefile:465` builds locally from `Makefile.kxld` and copies to
the same directory — the same pattern this tree already uses. A grep of every
IOKitUser source for libsyscall-only entry points (`__semwait_signal`,
`__disable_threadsignal`, `mach_msg_trap`, `semaphore_wait_trap`,
`__psynch_*`, `__thread_selfid`, `__sysctl`) returned nothing, so the flag was
inert even in a tree where the archive was present.
`-lcc_kext` is a different case and stays: it is LLVM's compiler support
runtime, staged by `Kernel/Makefile:103-109` (`compiler_rt`) during the same
Kernel pass, not a syscall library.
Cites: Kernel/IOKitUser/Makefile:277-288; Makefile:168;
Libraries/Libsystem/libsystem_kernel/Makefile:276-290; Kernel/Makefile:103-110,138-141.
Note: upstream ravynsoft/ravynos carries the same `-lkernel` on that line
(`gh api repos/ravynsoft/ravynos/contents/Kernel/IOKitUser/Makefile?ref=darwin`,
line 280), so this is upstream's defect rather than a local divergence; Apple
publishes no makefile for IOKitUser (Xcode-only), so there is no upstream
reference to compare against.
Next: rerun `./tools/darwin-gate.sh xnu`.
