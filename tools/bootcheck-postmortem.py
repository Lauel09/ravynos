#!/usr/bin/env python3
"""Ask a still-running QEMU guest what it was doing when the boot went quiet.

A UEFI boot that dies before it prints anything leaves one useful witness: the
firmware's own exception dump (EDK2 prints it on the serial port), and one much
better one: the machine itself.  bootcheck.sh leaves the guest alive on a
failed boot, so its HMP monitor is still reachable; this script walks up to it
and records the CPU state plus the memory mapping at the faulting address.

Why this exists: the OVMF failure this was written for reports

    RIP - 00000000000B0000, CS - 0000000000000038
    !!!! Can't find image information. !!!!

which says *where* the CPU was, not *what* it was executing.  `info mtree`
answers that: 0xA0000-0xBFFFF is VGA low memory on a default QEMU machine, so a
RIP there means firmware jumped to a garbage pointer rather than running a
loaded image.  `x/…i` then confirms it by decoding whatever is mapped there.

The dump is plain text so it can be attached to a CI log and read by a human
without any of this tooling.

Usage:
    bootcheck-postmortem.py --monitor SOCK --out FILE [--serial LOG]
                            [--timeout SECONDS] [--json PATH]

Exit status: 0 dumped, 1 the monitor could not be used, 2 usage error.
"""

import argparse
import json
import os
import re
import socket
import sys
import time

PROMPT = b"(qemu)"


class Monitor:
    """A one-shot HMP client.

    QEMU's human monitor has no framing beyond its prompt: send a line, read
    until the prompt comes back.  Reading in a loop with a deadline is enough,
    and it keeps this file dependency-free.
    """

    def __init__(self, path, timeout=3.0, connect_deadline=5.0):
        self.sock = None
        self.timeout = timeout
        deadline = time.time() + connect_deadline
        last = None
        while time.time() < deadline:
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.settimeout(timeout)
                sock.connect(path)
                self.sock = sock
                break
            except OSError as exc:          # not listening yet, or gone
                last = exc
                try:
                    sock.close()
                except OSError:
                    pass
                time.sleep(0.1)
        if self.sock is None:
            raise OSError("cannot connect to %s: %s" % (path, last))
        # Swallow the banner so the first response starts clean.
        try:
            self.sock.recv(65536)
        except OSError:
            pass

    def cmd(self, command, timeout=None):
        """Send one HMP command, return its output as text."""
        if self.sock is None:
            return ""
        timeout = self.timeout if timeout is None else timeout
        try:
            self.sock.sendall((command + "\n").encode())
        except OSError:
            return ""
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            if PROMPT in buf:
                break
        text = buf.decode("utf-8", "replace")
        # Strip the echoed command and the prompt run-out; keep the payload.
        lines = [ln for ln in text.splitlines() if ln.strip() and ln.strip() != command]
        while lines and lines[-1].strip().startswith("(qemu)"):
            lines.pop()
        lines = [ln.replace("(qemu)", "").rstrip() for ln in lines]
        return "\n".join(lines)

    def close(self):
        try:
            if self.sock is not None:
                self.sock.close()
        except OSError:
            pass
        self.sock = None


def parse_rip(registers):
    """Pull the instruction pointer out of `info registers`.

    Both spellings are in the wild: `RIP=00000000000b0000` from the HMP
    register dump, and `RIP: 0x…`/`rip 0x…` from other QEMU fronts.  Returning
    None is fine -- the rest of the dump is still worth having.
    """
    for line in registers.splitlines():
        m = re.search(r"\b(?:RIP|EIP)\s*[=:]\s*(?:0x)?([0-9a-fA-F]+)", line)
        if m:
            return int(m.group(1), 16)
    return None


def mtree_window(text, address, window=4):
    """Lines of `info mtree` whose address ranges contain `address`.

    The full tree for a 2 GiB guest is thousands of lines and says nothing on
    its own; the few ranges covering one address say everything (is it RAM, an
    alias, a device, or nothing at all?).
    """
    hits = []
    span = re.compile(
        r"^\s*([0-9a-fA-F]{4,16})-([0-9a-fA-F]{4,16})")
    for line in text.splitlines():
        m = span.match(line)
        if not m:
            continue
        try:
            lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        except ValueError:
            continue
        if lo <= address <= hi:
            hits.append(line.rstrip())
    if not hits:
        # Nothing claims the address: that is itself the answer, but print the
        # neighbourhood so a reader can see where the hole is.
        near = []
        for line in text.splitlines():
            m = span.match(line)
            if not m:
                continue
            lo, hi = int(m.group(1), 16), int(m.group(2), 16)
            if abs(lo - address) < (window * 0x10000) or abs(hi - address) < (window * 0x10000):
                near.append(line.rstrip())
        return near[:40]
    return hits[:20]


def describe_region(lines):
    """One-line summary of what owns the faulting address."""
    if not lines:
        return "unmapped (no memory region claims this address)"
    joined = " ".join(lines).lower()
    for name, label in (
        ("vga-lowmem", "VGA low memory (0xA0000-0xBFFFF): not code, garbage jump"),
        ("vga", "VGA memory: not code, garbage jump"),
        ("ram", "RAM"),
        ("pflash", "firmware flash"),
    ):
        if name in joined:
            return label
    return "see mapping below"


def dump(monitor, serial_tail="", want_json=None):
    out = []
    registers = monitor.cmd("info registers")
    out.append("== info registers ==")
    out.append(registers or "(no answer)")
    rip = parse_rip(registers)
    window = []

    if rip is not None:
        out.append("")
        out.append("== fault address 0x%016x ==" % rip)
        window = mtree_window(monitor.cmd("info mtree", timeout=10), rip)
        out.append("owner: %s" % describe_region(window))
        for line in window:
            out.append("  " + line)
        out.append("")
        out.append("== xp /32xb 0x%016x (physical) ==" % rip)
        out.append(monitor.cmd("xp /32xb 0x%x" % rip) or "(no answer)")
        out.append("")
        out.append("== x /8i 0x%016x ==" % rip)
        out.append(monitor.cmd("x /8i 0x%x" % rip) or "(no answer)")
    else:
        out.append("")
        out.append("== fault address == (no RIP in the register dump)")

    out.append("")
    out.append("== info status ==")
    out.append(monitor.cmd("info status") or "(no answer)")

    if serial_tail:
        out.append("")
        out.append("== serial tail (last 20 lines) ==")
        out.extend(serial_tail.rstrip("\n").splitlines()[-20:])

    text = "\n".join(out) + "\n"
    record = {"schema": 1, "rip": rip, "owner": describe_region(window)}
    if want_json:
        try:
            with open(want_json, "w", encoding="utf-8") as fh:
                json.dump(record, fh, indent=2, sort_keys=True)
                fh.write("\n")
        except OSError:
            pass
    return text


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=True, description=__doc__.splitlines()[0])
    ap.add_argument("--monitor", required=True, help="QEMU HMP unix socket")
    ap.add_argument("--out", required=True, help="where to write the dump")
    ap.add_argument("--serial", default="", help="serial log to tail into the dump")
    ap.add_argument("--json", default="", help="also write a one-line summary record")
    ap.add_argument("--timeout", type=float, default=3.0, help="per-command seconds")
    args = ap.parse_args(argv)

    serial_tail = ""
    if args.serial:
        try:
            with open(args.serial, "rb") as fh:
                fh.seek(0, os.SEEK_END)
                size = fh.tell()
                fh.seek(max(0, size - 4096))
                serial_tail = fh.read().decode("utf-8", "replace")
        except OSError:
            serial_tail = ""

    try:
        monitor = Monitor(args.monitor, timeout=args.timeout)
    except OSError as exc:
        sys.stderr.write("postmortem: %s\n" % exc)
        return 1

    try:
        text = dump(monitor, serial_tail, want_json=args.json)
    finally:
        monitor.close()

    header = "# QEMU post-mortem (monitor %s)\n" % args.monitor
    try:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(header)
            fh.write(text)
    except OSError as exc:
        sys.stderr.write("postmortem: cannot write %s: %s\n" % (args.out, exc))
        return 1

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
