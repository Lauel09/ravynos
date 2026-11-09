# Post-D7 Plan and Risk Register

Written 2026-11-09, after the D7 gate passed. Findings come from reading source
and built binaries and from the D7 boot logs. Items marked *not measured* have
not been run.

## 1. Where D7 actually landed

Proved (`Docs/DarwinBoot.md`, run-log "D7 attempt 2"): real XNU, real dyld, real
`/sbin/launchd` as PID 1, `launchd[1] has started up in single-user mode`, then
a `#` prompt from real `/bin/sh` on the serial console.

Not yet proved or still incomplete:

- **The shell has never read input.** `tools/xnu-boot.sh:143` uses
  `-serial file:`, which is output-only. Only the prompt has been observed.
- **The boot default is 25 seconds** (`SECS=25`), too short for scripted use.
- **The root is read-only.** The sysroot `sbin/` holds only `launchd` and
  `mount_hfs`. `fsck_hfs` and `newfs_hfs` have source in `BSD/sbin` but are not
  in the sysroot, and `BSD/sbin` has no generic `mount` or `fsck`.
- **Only `/bin/sh` is staged from `bin/`**, so the shell has few commands.
- **launchd log lines land on the console after the prompt** (a `MIG request`
  line follows `#`), which will interleave with interactive use.
- **The shell comes from `init/init.c`, not from launchctl.** Commit
  `ae36f65eae` re-enabled `init_pre_kevent()`. Boot-spirit item 2 permits this,
  but launchd also still kickstarts `com.apple.launchctl.System`
  (`core.c:7193`), and `/bin/launchctl` does not exist.

## 2. Agreed decisions

- **Compatibility policy** (`Docs/DarwinBoot.md`, agreed 2026-11-09, permanent):
  Apple's observable behavior is the spec; x86_64 PCs; ABI baseline macOS 10.15.
- Port the existing `BSD/bin/launchctl` and vendor jansson (MIT) as a logged
  temporary step only. It speaks JSON, while Apple's format is plists, so real
  plist support is required before D9 counts as converged. Apple's older
  launchctl in `BSD/old-launchd` needs CoreFoundation, which is deferred.
- Push `fork/darwin-dev` to `origin` for backup.

Existing conflicts with the policy, each needing a real replacement:

| Item | Conflict | Replaced by |
|---|---|---|
| `ccrng` zero-fill and `_ccrng_uniform` returning 0 | Fake success | Real entropy, before D10 daemons |
| `init_pre_kevent()` shell route | Not Apple's launchctl route | D9 |
| JSON `launchctl` | Not Apple's plist format | Plist support, D9 onward |
| `-undefined dynamic_lookup` in launchd | Hides missing symbols | Zero audit results, then remove the flag |

## 3. Proposed milestones

Same rules as `Docs/DarwinBoot.md` §0: in order, and done only when real gate
output is pasted into a run-log.

| # | Milestone | Gate |
|---|---|---|
| D8 | Usable single-user shell | Commands sent over serial run; `ls /` works; root remounted read-write; a written file reads back |
| D9 | Apple bootstrap route | `launchctl bootstrap -S System -s` gives the shell, the `init_pre_kevent()` call is disabled again, and jobs load from plists |
| D10 | Minimal multi-user | Real entropy; `/etc/bootstrap` runs; `notifyd` and `syslogd` start from launchd plists and survive |
| D11 | Real XPC | A small client and server exchange messages through launchd over Mach |
| D12 | Frameworks | ICU, then CoreFoundation, then Foundation build and a test tool runs in the guest |
| D13 | Graphics | WindowServer draws through `IOGOPFramebuffer` and receives IOHID input |

Apple-convergence items stay as listed in boot-spirit item 4: kernelcache,
`boot-uuid`, GPT root, shared cache.

## 4. Symbol audit

`tools/symaudit.py` simulates dyld's load and bind closure over the sysroot and
lists names no image can supply. It predicted the `_bootstrap_look_up2` crash
before that boot. Lazy symbols fail only when called, so an entry is a future
crash on whichever path reaches it.

```sh
python3 -I tools/symaudit.py "$(cd ../build/sysroot; pwd)" \
    /sbin/launchd /bin/sh /usr/lib/dyld /bin/launchctl
```

Result on 2026-11-09: 17 unresolved.

| Symbols | Used by | Reached when | Fix |
|---|---|---|---|
| `_bootstrap_parent` (imported from libxpc, exported by liblaunch) | `libsystem_c` `daemon.c:64` | Any `daemon()` call | Same link-order fix as commit `f7fb73723d`, or re-export from libxpc |
| `_xpc_strerror` | launchd `core.c:10915,10964` | Job check-in errors | Implement in libxpc |
| `_sallocx` | launchd `shim.h:123-128` | Wherever the shim runs | FreeBSD jemalloc call; use `malloc_size` |
| `___udivti3` | launchd `runtime.c:1488` | Timebase numer != denom | `libcompiler_rt.dylib` exports nothing; link the clang builtins |
| `_au_*`, `_audit_quick_stop` | `init.c`, `launchd.c:590` | Audit logging, shutdown | libbsm is not in the sysroot; build it |
| `_cc_clear`, `_ccdigest_*`, `_ccsha*_di` | libdyld | Digest paths, likely the shared cache | Provide a userland corecrypto before enabling the cache |

Goal: reach zero, then remove `-undefined dynamic_lookup` from
`BSD/sbin/launchd/Makefile:42` so these become link errors.

## 5. Risk register

| Risk | Evidence | Mitigation |
|---|---|---|
| Mixed OS eras | launchd source is 10.10-era on XNU 19.6; Apple stopped publishing launchd after that | Treat launchd and libxpc as ours; mine PureDarwin/XPC and darling-libxpc |
| XPC is minimal | `xpc_pipe_routine_reply` returns `EPIPE`; receive handles MIG only | D11, with a test before any daemon depends on it |
| Normal boot is empty | No `notifyd`, `syslogd`, `configd`, or launchd plists in the sysroot; `usr/sbin` and `usr/libexec` empty | D10, smallest daemon set first |
| Predictable randomness | `ccrng` zero-fills; `_ccrng_uniform` always returns 0 | XNU `getentropy` before any daemon or network work |
| GUI is FreeBSD-bound | `WindowServer.m` imports `BSDFramebuffer.h`, `kvm.h`, `linux/input.h`, `login_cap.h`; top-level `Makefile` defers Frameworks behind ICU | D12 then D13; port display and input layers separately |
| Driver gaps | No networking kext source; `IOUSBFamily` and `RavynXHCIPort` have source but are not built; PS/2 has only the controller kext | Add a QEMU NIC driver; confirm keyboard input before D13 |
| Slow guest (*not measured*) | x86_64 on an arm64 host runs under TCG, and every process binds about 30 dylibs with no shared cache | Try `-accel tcg,thread=multi,tb-size=512`; measure process launch; use an x86 KVM runner for CI |
| Clean clones may not build | 679 untracked files under `Developer/ravynOS.sdk`; its Makefile copies `usr/` into the build SDK; samples match `BSD/include` byte for byte | Classify, then commit or generate them; prove with a clean clone |

## 6. Prior art

- [PureDarwin/XPC](https://github.com/PureDarwin/XPC): launchd 842.91.1 plus the
  iXsystems libxpc skeleton.
- [darling-libxpc](https://relatedrepos.com/gh/darlinghq/darling-libxpc): fork
  of NextBSD's libxpc.
- [darwin-dev, launchd in OS X 10.11](https://lists.apple.com/archives/darwin-dev/2016/Jan/msg00009.html):
  launchd moved into the closed libxpc project.
- [QEMU multi-threaded TCG](https://wiki.qemu-project.org/Features/tcg-multithread)
  and [Quickemu's TCG tuning](https://gh.hhj520.top/quickemu-project/quickemu/pull/1824).
