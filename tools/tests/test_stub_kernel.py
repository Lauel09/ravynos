#!/usr/bin/env python3
"""Unit tests for the K1 stub kernel ELF (Docs/KernelHandoff.md).

Verifies the loader-facing contract of Kernel/ravynkern/KERNEL.ELF without
booting anything: single PT_LOAD covering 0x100000..entry, entry inside an
executable segment, zero-filled .bss beyond FileSiz (boot stack), and the
BOOTKSTACK_TOP symbol resolving to boot_kstack + 64 KiB.  Only stdlib is
used; run directly or via tools/tests/run-tests.sh.
"""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
KERNEL_ELF = os.path.join(REPO, "Kernel", "ravynkern", "KERNEL.ELF")

PT_LOAD = 1


def parse_elf64(path):
    data = open(path, "rb").read()
    assert data[:4] == b"\x7fELF" and data[4] == 2, "not ELF64"
    e_type, e_machine = struct.unpack_from("<HH", data, 16)
    e_entry, = struct.unpack_from("<Q", data, 24)
    e_phoff, = struct.unpack_from("<Q", data, 32)
    e_shoff, = struct.unpack_from("<Q", data, 40)
    e_phnum, = struct.unpack_from("<H", data, 56)
    e_shnum, = struct.unpack_from("<H", data, 60)
    e_shstrndx, = struct.unpack_from("<H", data, 62)
    phdrs = []
    for i in range(e_phnum):
        t, flags, off, vaddr, paddr, fsz, msz, al = struct.unpack_from(
            "<IIQQQQQQ", data, e_phoff + i * 56)
        phdrs.append(dict(type=t, flags=flags, off=off, vaddr=vaddr,
                          paddr=paddr, fsz=fsz, msz=msz, align=al))
    shdrs = []
    for i in range(e_shnum):
        name, stype, flags, addr, off, size, link, info, align, entsize = \
            struct.unpack_from("<IIQQQQIIQQ", data, e_shoff + i * 64)
        shdrs.append(dict(name=name, type=stype, addr=addr, off=off,
                          size=size, link=link, entsize=entsize))
    return dict(data=data, e_type=e_type, e_machine=e_machine,
                e_entry=e_entry, phdrs=phdrs, shdrs=shdrs,
                e_shstrndx=e_shstrndx)


class StubKernelElfTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(KERNEL_ELF):
            raise unittest.SkipTest("KERNEL.ELF not built; run make in "
                                    "Kernel/ravynkern first")
        cls.ef = parse_elf64(KERNEL_ELF)

    def test_machine_and_type(self):
        self.assertEqual(self.ef["e_machine"], 0x3E, "EM_X86_64 expected")

    def test_single_pt_load_at_one_mib(self):
        loads = [p for p in self.ef["phdrs"] if p["type"] == PT_LOAD]
        self.assertEqual(len(loads), 1,
                         "K2 parser handles exactly one PT_LOAD")
        seg = loads[0]
        # -N (omagic) makes vaddr==fileoff and the segment starts at the first
        # allocated section (.rodata pointer pool); what must hold for K2 is:
        # base >= 1 MiB and the whole image spans entry..boot stack top.
        self.assertGreaterEqual(seg["vaddr"], 0x100000)
        self.assertLessEqual(seg["vaddr"] + seg["msz"], 0x120000,
                             "kernel image must fit its reserved window")
        self.assertTrue(seg["flags"] & 1, "PF_X: single segment must be exec")

    def test_entry_inside_segment(self):
        seg = [p for p in self.ef["phdrs"] if p["type"] == PT_LOAD][0]
        self.assertGreaterEqual(self.ef["e_entry"], seg["vaddr"])
        self.assertLess(self.ef["e_entry"], seg["vaddr"] + seg["msz"])

    def test_bss_zero_fill_covers_boot_stack(self):
        # MemSiz - FileSiz must be >= 64 KiB (boot_kstack) and the tail bytes
        # are absent from the file image (loader zero-fills them).
        seg = [p for p in self.ef["phdrs"] if p["type"] == PT_LOAD][0]
        self.assertGreaterEqual(seg["msz"] - seg["fsz"], 65536)

    def test_banner_string_present(self):
        self.assertIn(b"RAVYNOS-KERNEL-OK", self.ef["data"])

    def test_symtab_contract(self):
        """_start defined; BOOTKSTACK_TOP constant inside the loaded image."""
        out = subprocess.run(["nm", KERNEL_ELF], capture_output=True,
                             text=True, check=True).stdout
        syms = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                syms[parts[2]] = int(parts[0], 16)
        self.assertIn("_start", syms)
        self.assertIn("boot_kstack", syms)
        self.assertIn("BOOTKSTACK_TOP", syms)
        seg = [p for p in self.ef["phdrs"] if p["type"] == PT_LOAD][0]
        # K2/K3 read the 8-byte pointer at BOOTKSTACK_TOP from the copied
        # segment; it must therefore live inside FileSiz (not .bss).
        self.assertGreaterEqual(syms["BOOTKSTACK_TOP"], seg["vaddr"])
        self.assertLess(syms["BOOTKSTACK_TOP"], seg["vaddr"] + seg["fsz"])
        # boot_kstack itself lives in the zero-filled tail (.bss).
        self.assertGreaterEqual(syms["boot_kstack"], seg["vaddr"])
        self.assertLessEqual(syms["boot_kstack"] + 65536,
                             seg["vaddr"] + seg["msz"])

    def test_bootkstack_value(self):
        """The stored pointer equals boot_kstack + 64 KiB."""
        seg = [p for p in self.ef["phdrs"] if p["type"] == PT_LOAD][0]
        out = subprocess.run(["nm", KERNEL_ELF], capture_output=True,
                             text=True, check=True).stdout
        syms = {}
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                syms[parts[2]] = int(parts[0], 16)
        ptr_off = seg["off"] + (syms["BOOTKSTACK_TOP"] - seg["vaddr"])
        val, = struct.unpack_from("<Q", self.ef["data"], ptr_off)
        self.assertEqual(val, syms["boot_kstack"] + 65536)


if __name__ == "__main__":
    unittest.main(verbosity=2)
