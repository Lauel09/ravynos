#!/usr/bin/env python3
"""Unit tests for tools/bootcheck-postmortem.py.

The post-mortem helper is only ever used when a boot has already gone wrong, so
it is the one tool that cannot be debugged by watching it work.  These tests
stand up a fake human monitor on a unix socket -- the same socket QEMU creates
with `-monitor unix:PATH,server,nowait` -- and check that the helper speaks HMP
correctly and reports the *interesting* fact about a faulting address: which
memory region owns it.

    python3 tools/tests/test_postmortem.py -v
"""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
SCRIPT = os.path.join(TOOLS, "bootcheck-postmortem.py")

# The register dump and memory tree of the OVMF failure this tool was written
# for: a #UD reported at 0xB0000 with no image information.
REGISTERS = """RAX=0000000000000000 RBX=0000000000000000
RIP=00000000000b0000 RFLAGS=0000000000000246
CS =0038 DS =0030 ES =0030 SS =0030
"""

MTREE = """address-space: memory
  0000000000000000-00000000ffffffff (prio 0, i/o): system
    0000000000000000-000000000009ffff (prio 0, ram): pc.ram
    00000000000a0000-00000000000bffff (prio 0, i/o): vga-lowmem
    00000000000c0000-00000000000fffff (prio 0, rom): pc.rom
"""


class FakeMonitor(threading.Thread):
    """Minimal HMP server: greets, echoes commands, answers from a dict."""

    def __init__(self, path, answers):
        super().__init__(daemon=True)
        self.path = path
        self.answers = answers
        self.commands = []
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.bind(path)
        self.sock.listen(1)
        self.sock.settimeout(5)

    def run(self):
        try:
            conn, _ = self.sock.accept()
        except OSError:
            return
        with conn:
            conn.settimeout(5)
            conn.sendall(b"QEMU 8.2.0 monitor - type 'help' for more information\r\n(qemu) ")
            buf = b""
            while True:
                try:
                    chunk = conn.recv(4096)
                except OSError:
                    return
                if not chunk:
                    return
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    cmd = line.decode().strip()
                    if not cmd:
                        continue
                    self.commands.append(cmd)
                    body = self.answers.get(cmd.split()[0], "")
                    for key, value in self.answers.items():
                        if cmd == key:
                            body = value
                            break
                    payload = (cmd + "\r\n" + (body + "\r\n" if body else "") +
                               "(qemu) ")
                    conn.sendall(payload.encode())

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class PostmortemTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.sock_path = os.path.join(self.dir, "monitor.sock")
        self.answers = {
            "info registers": REGISTERS,
            "info mtree": MTREE,
            "xp": "00000000000b0000: 0xff 0xff 0xff 0xff 0x00 0x00 0x00 0x00",
            "x": "0x00000000000b0000:  invalid",
            "info status": "VM status: running",
        }
        self.server = FakeMonitor(self.sock_path, self.answers)
        self.server.start()

    def tearDown(self):
        self.server.close()
        self.tmp.cleanup()

    def run_tool(self, *extra):
        out = os.path.join(self.dir, "postmortem.log")
        rec = os.path.join(self.dir, "postmortem.json")
        proc = subprocess.run(
            [sys.executable, SCRIPT, "--monitor", self.sock_path,
             "--out", out, "--json", rec] + list(extra),
            capture_output=True, text=True, timeout=60)
        return proc, out, rec

    def test_dump_names_the_owner_of_the_fault_address(self):
        proc, out, rec = self.run_tool()
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with open(out, encoding="utf-8") as fh:
            text = fh.read()
        # The whole point: 0xB0000 is VGA low memory, not code.
        self.assertIn("00000000000b0000", text)
        self.assertIn("vga-lowmem", text)
        self.assertIn("garbage jump", text)
        with open(rec, encoding="utf-8") as fh:
            record = json.load(fh)
        self.assertEqual(record["rip"], 0xB0000)
        self.assertIn("VGA", record["owner"])

    def test_commands_sent_to_the_monitor(self):
        self.run_tool()
        self.assertIn("info registers", self.server.commands)
        self.assertIn("info mtree", self.server.commands)
        self.assertTrue(any(c.startswith("xp /32xb 0xb0000") for c in self.server.commands),
                        self.server.commands)
        self.assertTrue(any(c.startswith("x /8i 0xb0000") for c in self.server.commands),
                        self.server.commands)
        # The monitor must be left alone: no quit, no reset.
        self.assertFalse(any(c.strip() in ("quit", "system_reset", "stop")
                             for c in self.server.commands))

    def test_serial_tail_is_appended(self):
        serial = os.path.join(self.dir, "serial.log")
        with open(serial, "w", encoding="utf-8") as fh:
            fh.write("BdsDxe: starting Boot0001\n!!!! X64 Exception Type - 06(#UD) !!!!\n")
        proc, out, _ = self.run_tool("--serial", serial)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with open(out, encoding="utf-8") as fh:
            text = fh.read()
        self.assertIn("Exception Type - 06", text)

    def test_missing_monitor_is_an_error_not_a_traceback(self):
        proc = subprocess.run(
            [sys.executable, SCRIPT, "--monitor",
             os.path.join(self.dir, "nope.sock"), "--out",
             os.path.join(self.dir, "o.log")],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("cannot connect", proc.stderr)
        self.assertNotIn("Traceback", proc.stderr)


if __name__ == "__main__":
    unittest.main()
