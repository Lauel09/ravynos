# The prompt: kernel console and boot shell

Two programs in this tree give an operator a prompt before any userland
exists, and both are deliberately small enough to debug from a serial log:

| Program | Runs where | Source | Built by |
|---|---|---|---|
| kernel command console | after `ExitBootServices`, ring 0, no firmware services | `Kernel/ravynkern/main.c` | `Kernel/ravynkern/build.sh` |
| boot shell (`EFI/SHELL.EFI`) | still a UEFI application, firmware services available | `tools/shell/shell_efi.c` | `tools/shell/build.sh` |

The loader decides which one you get: if it finds a kernel payload it hands
off to the kernel console; if there is none (or the handoff is refused) it
chain-loads `EFI/SHELL.EFI` with `LoadImage`/`StartImage`.

---

## 1. Console policy (why output is written to the UART directly)

The first CI runs of the UEFI job showed nothing at all from our code: OVMF's
own `BdsDxe` lines appeared, then an exception dump, and no banner. The loader
was writing only through the EFI **Serial I/O protocol**. Firmware is not
obliged to install that protocol — OVMF's firmware debug output reaches COM1
through `DebugLib`, not through `SerialIo` — so a program that writes only
through the protocol can be completely silent on the machine testing it.

Both programs now drive the 16550 UART at `0x3F8` directly:

* output: `outb(0x3F8, c)` after polling the LSR transmit-empty bit; no
  protocol, no allocation, cannot fail;
* input: poll the LSR receive-ready bit (kernel console, shell) and, in the
  shell, also `ConIn->CheckEvent`/`ReadKeyStroke`, so a physical keyboard
  works as well as a serial terminal;
* `ConOut` mirroring is enabled only when the firmware has **no** Serial I/O
  protocol, because when it has one the firmware console *is* the serial
  terminal and mirroring would print every line twice.

Milestone markers printed on this channel are the ones `tools/bootcheck.sh`
greps for:

| Marker | Meaning |
|---|---|
| `RAVYNOS-LOADER-OK` | the loader is running and can drive the UART |
| `RAVYNOS-KERNEL-LOAD <n> bytes, image …` | an ELF payload was validated and staged |
| `RAVYNOS-KERNEL-OK` | the kernel is running, no boot services |
| `RAVYNOS-KSHELL-OK` | the kernel console is reading commands |
| `RAVYNOS-SHELL-OK` | the UEFI shell's interactive loop is up |
| `RAVYNOS-KSHELL-EXIT` / `RAVYNOS-SHELL-EXIT` | the loop ended |

---

## 2. Kernel command console

```
RAVYNOS-KERNEL-OK
ravynkern: entered in long mode with paging on
bootinfo: magic RVNK v1, 5 usable region(s), descriptor 48 bytes
type 'help' for commands
RAVYNOS-KSHELL-OK
ravyn# ver
ravynOS kernel (ravynkern) -- stub bring-up build
  arch    x86_64, ring 0, no boot services
  console COM1 16550 @115200 8n1
  bootinfo magic 52564e4b version 1
```

| Command | What it does |
|---|---|
| `help` | list the commands |
| `ver` | kernel identity, bootinfo magic/version |
| `mem` | the firmware memory regions the loader harvested, with sizes |
| `cpus` | CPUID vendor string, signature, logical processor count |
| `echo <text>` | print text |
| `reboot` | pulse the 8042 reset line, then halt |
| `halt` | stop the CPU; the UART stays connected |

Editing: `\r`/`\n` submits, backspace erases, `^C` clears the line, `ESC`
leaves the console. There is no line history and no job control — those belong
to a userland shell, not to the kernel.

## 3. Boot shell (`EFI/SHELL.EFI`)

```
ravynOS boot shell -- type 'help' for commands
RAVYNOS-SHELL-OK
\ravyn> ls \EFI
listing \EFI
  <DIR>  BOOT
  5184  KERNEL.ELF
  15872  SHELL.EFI
  3 entries, 21056 bytes of files
```

| Command | What it does |
|---|---|
| `help` | list the commands |
| `ver` | shell/firmware identity, console channels, whether a volume is mounted |
| `ls [dir]` | directory listing with sizes and `<DIR>` markers |
| `cat <file>` | print a text file, truncated at 4 KiB |
| `cd <dir>` / `cd ..` / `cd \` | change directory |
| `pwd` | print the current directory |
| `kernel` | parse `\EFI\KERNEL.ELF`'s header and print its entry point |
| `memmap` | firmware memory map, conventional regions only |
| `echo <text>` | print text |
| `reboot` | UEFI `ResetSystem(EfiResetCold)` |
| `exit` | leave the shell (also `ESC`) |

The volume it lists is the one the image was started from, via
`LoadedImage->DeviceHandle` → `SimpleFileSystem`, so `ls`/`cat`/`kernel` are
also a check that the ESP the firmware mounted is the one we built.

---

## 4. Driving a prompt from CI

`tools/bootcheck.sh --send FILE` attaches QEMU's serial port to a unix socket
instead of a file and types one line of `FILE` at a time (ignoring blank lines
and `#` comments), with `--send-delay` before the first line and
`--send-interval` between lines. Guest output is copied into the usual
`serial.log`, so every marker check still works.

```sh
cat > cmds.txt <<'EOF'
help
ver
mem
exit
EOF
tools/bootcheck.sh out/uefi.iso --uefi --send cmds.txt \
    --send-delay 25 --send-interval 3 \
    --expect RAVYNOS-KSHELL-OK --timeout 200
```

A file-backed `-serial` is output-only, which is why the option exists at all:
before it, no prompt could be answered in a headless run. The mechanics are
covered by a test with a fake QEMU (`tools/tests/run-tests.sh`), so the driver
is exercised even on a host without QEMU.

Timing is the only soft edge: the delay has to be long enough for firmware to
reach our prompt. The CI job uses 25 s for a firmware boot and the marker
checks make a too-short delay visible as a missing answer rather than a silent
pass.

---

## 5. Where this goes next

* The kernel console is the seed of the single-user mode: once the kernel owns
  a driver for the boot volume, `ls`/`cat` there can read the same ESP.
* The boot shell stays useful after the kernel works: it is the fallback when
  the kernel refuses the handoff, and the place to inspect an image whose
  kernel does not start.
* Neither program is the "Darwin shell": a real `sh` in userland needs the
  kernel to load a Mach-O binary, which is the next milestone (see
  `Docs/KernelHandoff.md` §Design decisions, item 1).
