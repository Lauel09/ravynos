#!/usr/bin/env python3
"""Unit tests for tools/image/pe.py, the UEFI PE32+ validator.

The validator exists so that a wrong UEFI payload is caught while the caller
still knows which file it passed -- firmware only answers with a hang.  These
tests build synthetic PE headers field by field, so every failure mode can be
reproduced without a compiler:

    python3 tools/tests/test_pe.py -v
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
IMAGE = os.path.join(TOOLS, "image")
sys.path.insert(0, IMAGE)

import pe  # noqa: E402

PE_OFFSET = 0x80
OPT_SIZE = 240
SECTION_SIZE = 0x200
SECTION_RVA = 0x1000
SIZE_OF_IMAGE = 0x2000
SIZE_OF_HEADERS = 0x200

# Characteristics: executable image + large address aware.
COFF_CHARACTERISTICS = 0x0022
SECTION_CHARACTERISTICS = 0x60000020  # code | execute | read


def minimal_pe(*, machine: int = 0x8664, magic: int = 0x20B,
               subsystem: int = 10, entry_rva: int = SECTION_RVA,
               size_of_image: int = SIZE_OF_IMAGE,
               section_characteristics: int = SECTION_CHARACTERISTICS,
               raw_size: int = SECTION_SIZE,
               padding: int = 0) -> bytes:
    """A one-section PE32+ image, valid by default.

    Mirrors what lld-link emits for a freestanding EFI application: headers,
    one executable .text section, no imports, no relocations.
    """
    file_align = 0x200
    raw_ptr = SIZE_OF_HEADERS
    total = raw_ptr + raw_size + padding
    buf = bytearray(total)

    # DOS header.
    buf[0:2] = b"MZ"
    struct.pack_into("<I", buf, 0x3C, PE_OFFSET)

    # PE signature + COFF header.
    buf[PE_OFFSET:PE_OFFSET + 4] = b"PE\x00\x00"
    coff = PE_OFFSET + 4
    struct.pack_into("<HHIIIHH", buf, coff,
                     machine, 1, 0, 0, 0, OPT_SIZE, COFF_CHARACTERISTICS)

    # PE32+ optional header.
    opt = coff + 20
    struct.pack_into("<H", buf, opt, magic)
    struct.pack_into("<I", buf, opt + 16, entry_rva)
    struct.pack_into("<Q", buf, opt + 24, 0x400000)     # image base
    struct.pack_into("<I", buf, opt + 32, 0x1000)       # section alignment
    struct.pack_into("<I", buf, opt + 36, file_align)
    struct.pack_into("<I", buf, opt + 56, size_of_image)
    struct.pack_into("<I", buf, opt + 60, SIZE_OF_HEADERS)
    struct.pack_into("<H", buf, opt + 68, subsystem)
    struct.pack_into("<I", buf, opt + 108, 16)          # number of data dirs

    # Section table: one .text.
    sec = opt + OPT_SIZE
    buf[sec:sec + 8] = b".text\x00\x00\x00"
    struct.pack_into("<IIIIIIHHI", buf, sec + 8,
                     raw_size, SECTION_RVA, raw_size, raw_ptr,
                     0, 0, 0, 0, section_characteristics)

    # Payload: a `ret` at the entry point.
    buf[raw_ptr:raw_ptr + 1] = b"\xc3"
    return bytes(buf)


def patch(data: bytes, offset: int, fmt: str, value) -> bytes:
    out = bytearray(data)
    struct.pack_into(fmt, out, offset, value)
    return bytes(out)


class ValidatorTests(unittest.TestCase):
    def test_minimal_image_is_valid(self) -> None:
        info = pe.validate(minimal_pe(), require_subsystem=10)
        self.assertEqual(info["format"], "pe32+")
        self.assertEqual(info["machine_name"], "x86_64")
        self.assertEqual(info["subsystem_name"], "efi_application")
        self.assertTrue(info["uefi"])
        self.assertEqual(info["entry_rva"], SECTION_RVA)
        self.assertEqual(info["entry_section"], ".text")

    def test_aarch64_image_is_valid(self) -> None:
        info = pe.validate(minimal_pe(machine=0xAA64))
        self.assertEqual(info["machine_name"], "aarch64")

    def test_driver_subsystems_are_accepted(self) -> None:
        for subsystem in (11, 12, 13):
            with self.subTest(subsystem=subsystem):
                info = pe.validate(minimal_pe(subsystem=subsystem))
                self.assertTrue(info["uefi"])

    def test_require_subsystem_is_enforced(self) -> None:
        with self.assertRaises(pe.PEError):
            pe.validate(minimal_pe(subsystem=11), require_subsystem=10)

    def test_32_bit_image_is_rejected(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "PE32"):
            pe.validate(minimal_pe(magic=0x10B))

    def test_non_efi_subsystem_is_rejected(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "subsystem"):
            pe.validate(minimal_pe(subsystem=3))   # console application

    def test_non_uefi_machine_is_rejected(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "machine"):
            pe.validate(minimal_pe(machine=0x014C))  # i386

    def test_not_a_pe_file(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "MZ"):
            pe.validate(b"hello, this is not an executable at all" * 4)

    def test_truncated_headers_are_rejected(self) -> None:
        data = minimal_pe()
        with self.assertRaises(pe.PEError):
            pe.validate(data[:PE_OFFSET + 8])

    def test_entry_outside_the_image_is_rejected(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "outside the image"):
            pe.validate(minimal_pe(entry_rva=0x9000))

    def test_zero_entry_point_is_rejected(self) -> None:
        with self.assertRaisesRegex(pe.PEError, "entry point is zero"):
            pe.validate(minimal_pe(entry_rva=0))

    def test_entry_in_a_non_executable_section_is_rejected(self) -> None:
        data = minimal_pe(section_characteristics=0x40000040)  # read | data
        with self.assertRaisesRegex(pe.PEError, "not executable|executable"):
            pe.validate(data)

    def test_section_past_the_end_of_the_file_is_rejected(self) -> None:
        data = minimal_pe()[:SECTION_SIZE + 16]
        with self.assertRaises(pe.PEError):
            pe.validate(data)

    def test_bad_file_alignment_is_rejected(self) -> None:
        data = minimal_pe()
        data = patch(data, PE_OFFSET + 4 + 20 + 36, "<I", 0x123)
        with self.assertRaisesRegex(pe.PEError, "alignment"):
            pe.validate(data)

    def test_zero_size_of_image_is_rejected(self) -> None:
        with self.assertRaises(pe.PEError):
            pe.validate(minimal_pe(size_of_image=0))

    def test_characteristics_must_mark_an_executable(self) -> None:
        data = minimal_pe()
        data = patch(data, PE_OFFSET + 4 + 18, "<H", 0x0000)
        with self.assertRaisesRegex(pe.PEError, "EXECUTABLE"):
            pe.validate(data)

    def test_empty_file_is_rejected(self) -> None:
        with self.assertRaises(pe.PEError):
            pe.validate(b"")


class CommandLineTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ravynos-pe-test.")
        self.efi = os.path.join(self.tmp, "BOOTX64.EFI")
        with open(self.efi, "wb") as fh:
            fh.write(minimal_pe())

    def tearDown(self) -> None:
        for name in os.listdir(self.tmp):
            os.unlink(os.path.join(self.tmp, name))
        os.rmdir(self.tmp)

    def run_cli(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, os.path.join(IMAGE, "pe.py"), *args],
            capture_output=True, text=True)

    def test_cli_accepts_a_valid_image_and_writes_json(self) -> None:
        out = os.path.join(self.tmp, "pe.json")
        result = self.run_cli(self.efi, "--require-subsystem", "10",
                              "--json", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(out, encoding="utf-8") as fh:
            record = json.load(fh)
        self.assertEqual(record["subsystem_name"], "efi_application")
        self.assertEqual(record["path"], self.efi)

    def test_cli_rejects_a_text_file(self) -> None:
        bad = os.path.join(self.tmp, "not.efi")
        with open(bad, "wb") as fh:
            fh.write(b"not an efi file" * 40)
        result = self.run_cli(bad)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid UEFI image", result.stderr)

    def test_cli_rejects_a_missing_file(self) -> None:
        result = self.run_cli(os.path.join(self.tmp, "nope.efi"))
        self.assertEqual(result.returncode, 1)

    def test_cli_quiet_mode_prints_nothing(self) -> None:
        result = self.run_cli(self.efi, "-q")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
