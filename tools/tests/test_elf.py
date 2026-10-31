#!/usr/bin/env python3
"""Unit tests for tools/image/elf.py (the kernel payload validator).

The loader copies PT_LOAD segments verbatim and jumps to e_entry, so every
property elf.py checks stands between "the guest boots" and "the guest jumps
into free memory".  These cases build minimal ELF64 images in memory and
assert one problem each, so a regression in the validator is caught without a
kernel build or a boot.
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "image"))

import elf  # noqa: E402  (tools/image/elf.py)


ET_EXEC, ET_DYN = 2, 3
EM_X86_64 = 0x3E
PT_LOAD, PT_DYNAMIC = 1, 6


def build_elf(segments, entry=0x100000, etype=ET_EXEC, machine=EM_X86_64,
              body_size=0x100):
    """Assemble an ELF64 with the given PT_LOAD list.

    `segments` entries are (vaddr, filesz, memsz, offset, flags) tuples; the
    file is large enough to hold the largest offset+filesz.
    """
    ehsize, phentsize = 64, 56
    phoff = ehsize
    data_off = phoff + phentsize * len(segments)
    total = max([data_off] + [off + filesz for (_, filesz, _, off, _) in segments])
    buf = bytearray(total + body_size)

    ident = bytearray(16)
    ident[0:4] = b"\x7fELF"
    ident[4] = 2  # ELFCLASS64
    ident[5] = 1  # LSB
    ident[6] = 1
    buf[0:16] = ident
    struct.pack_into("<HHIQQQIHHHHHH", buf, 16,
                     etype, machine, 1, entry, phoff, 0, 0,
                     ehsize, phentsize, len(segments), 64, 0, 0)
    for i, (vaddr, filesz, memsz, off, flags) in enumerate(segments):
        struct.pack_into("<IIQQQQQQ", buf, phoff + i * phentsize,
                         PT_LOAD, flags, off, vaddr, vaddr, filesz, memsz, 0x1000)
    return bytes(buf)


def write(data: bytes) -> str:
    fh = tempfile.NamedTemporaryFile(delete=False, suffix=".elf")
    fh.write(data)
    fh.close()
    return fh.name


class ElfValidatorTest(unittest.TestCase):
    def check(self, data, **kwargs):
        path = write(data)
        try:
            info = elf.parse(path)
            return elf.problems(info, kwargs.get("require_load", False),
                                kwargs.get("min_vaddr"), kwargs.get("max_end"))
        finally:
            os.unlink(path)

    def test_valid_kernel_layout(self):
        issues = self.check(build_elf([(0x100000, 0x400, 0x11000, 0x1000, 5)]),
                            require_load=True, min_vaddr=0x100000,
                            max_end=0x100000000)
        self.assertEqual(issues, [])

    def test_bad_magic(self):
        path = write(b"not an elf at all" + b"\0" * 64)
        try:
            with self.assertRaises(elf.ElfError):
                elf.parse(path)
        finally:
            os.unlink(path)

    def test_not_executable(self):
        issues = self.check(build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)],
                                      etype=ET_DYN))
        self.assertTrue(any("ET_EXEC" in i for i in issues), issues)

    def test_wrong_machine(self):
        issues = self.check(build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)],
                                      machine=0xB7))
        self.assertTrue(any("EM_X86_64" in i for i in issues), issues)

    def test_no_pt_load(self):
        issues = self.check(build_elf([]), require_load=True)
        self.assertTrue(any("PT_LOAD" in i for i in issues), issues)

    def test_entry_outside_segment(self):
        issues = self.check(build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)],
                                      entry=0x200000))
        self.assertTrue(any("e_entry" in i for i in issues), issues)

    def test_memsz_smaller_than_filesz(self):
        issues = self.check(build_elf([(0x100000, 0x800, 0x400, 0x1000, 5)]))
        self.assertTrue(any("MemSiz" in i for i in issues), issues)

    def test_file_extent_past_end(self):
        data = build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)])
        # Claim a bigger file extent than the image has.
        broken = bytearray(data)
        struct.pack_into("<Q", broken, 64 + 32, 0x1000 + 0x100000)
        issues = self.check(bytes(broken))
        self.assertTrue(any("past the end" in i for i in issues), issues)

    def test_below_one_mib(self):
        issues = self.check(build_elf([(0x8000, 0x400, 0x400, 0x1000, 5)]),
                            min_vaddr=0x100000)
        self.assertTrue(any("below" in i for i in issues), issues)

    def test_past_4gib(self):
        issues = self.check(
            build_elf([(0x100000, 0x400, 0x100000000, 0x1000, 5)],
                      entry=0x100000),
            max_end=0x100000000)
        self.assertTrue(any("past the allowed maximum" in i for i in issues),
                        issues)

    def test_overlapping_segments(self):
        issues = self.check(build_elf([(0x100000, 0x400, 0x800, 0x1000, 5),
                                       (0x100400, 0x400, 0x400, 0x2000, 5)]))
        self.assertTrue(any("overlap" in i for i in issues), issues)

    def test_rejects_dynamic_linking(self):
        data = bytearray(build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)]))
        # Turn the single header into PT_DYNAMIC and keep the entry valid.
        struct.pack_into("<I", data, 64, PT_DYNAMIC)
        struct.pack_into("<I", data, 64 + 56, PT_LOAD)
        struct.pack_into("<IIQQQQQQ", data, 64 + 56,
                         PT_LOAD, 5, 0x1000, 0x100000, 0x100000, 0x400, 0x400,
                         0x1000)
        issues = self.check(bytes(data))
        self.assertTrue(any("PT_DYNAMIC" in i for i in issues), issues)

    def test_cli_exit_codes(self):
        good = write(build_elf([(0x100000, 0x400, 0x400, 0x1000, 5)]))
        bad = write(build_elf([(0x1000, 0x400, 0x400, 0x1000, 5)]))
        try:
            self.assertEqual(elf.main([good, "--require-load", "-q"]), 0)
            self.assertEqual(
                elf.main([bad, "--require-load", "--min-vaddr", "0x100000",
                          "-q"]), 1)
        finally:
            os.unlink(good)
            os.unlink(bad)


if __name__ == "__main__":
    unittest.main(verbosity=2)
