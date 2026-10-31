#!/usr/bin/env python3
"""Validate that a file is a PE32+ UEFI image.

A UEFI boot image is not "some .efi file": it is a PE32+ executable whose
COFF machine is a supported firmware architecture, whose optional-header
subsystem is an EFI one (10 = application, 11/12 = drivers, 13 = ROM), and
whose entry point lives in an executable section.  Firmware does not diagnose
any of that for us -- it either refuses the image, or jumps somewhere and the
machine hangs -- so a wrong payload shows up as an opaque boot timeout.

This module makes that a millisecond answer, with no external tools, so that

* ``tools/mkimage.sh`` refuses to build an ISO whose El Torito UEFI entry
  points at something the firmware cannot load, and
* ``tools/image/verify.py`` can check the payload *inside* the finished ISO
  and inside a GPT/FAT ESP, not just the structures around it.

Deliberately independent of the writers: it parses the on-disk headers itself.

Usage
-----
    pe.py <file.efi> [--require-subsystem N] [--json PATH] [-q]

Exit status: 0 = valid UEFI image, 1 = not one, 2 = usage/IO error.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys

# COFF machine types.  Only the two UEFI spec architectures are accepted;
# 32-bit (0x014C) is not a legal UEFI image and is called out separately.
MACHINES = {
    0x8664: "x86_64",
    0xAA64: "aarch64",
    0x014C: "i386",
    0x01C4: "arm",
    0x5064: "riscv64",
    0x6264: "loongarch64",
}

SUBSYSTEMS = {
    10: "efi_application",
    11: "efi_boot_service_driver",
    12: "efi_runtime_driver",
    13: "efi_rom",
}

UEFI_MACHINES = (0x8664, 0xAA64)
UEFI_SUBSYSTEMS = tuple(SUBSYSTEMS)

PE32_MAGIC = 0x10B
PE32_PLUS_MAGIC = 0x20B

IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_DLL = 0x2000

DATA_DIRECTORY_NAMES = (
    "export", "import", "resource", "exception", "certificate", "relocation",
    "debug", "architecture", "global_ptr", "tls", "load_config", "bound_import",
    "iat", "delay_import", "com_descriptor", "reserved",
)


class PEError(Exception):
    """The file is not a valid UEFI PE32+ image."""


def _unpack(fmt: str, data: bytes, off: int, what: str):
    size = struct.calcsize(fmt)
    if off < 0 or off + size > len(data):
        raise PEError(f"{what}: header field at offset {off} runs past the end "
                      f"of the {len(data)}-byte file")
    return struct.unpack_from(fmt, data, off)


def parse(data: bytes) -> dict:
    """Parse a PE32+ image and return a summary, or raise PEError."""
    if len(data) < 64:
        raise PEError(f"file is {len(data)} bytes, too small to hold a DOS header")
    if data[0:2] != b"MZ":
        raise PEError("missing the DOS 'MZ' signature: not a PE image")
    (e_lfanew,) = _unpack("<I", data, 0x3C, "DOS header")
    if e_lfanew < 0x40 or e_lfanew + 4 > len(data):
        raise PEError(f"PE header offset {e_lfanew} is outside the file")
    if data[e_lfanew:e_lfanew + 4] != b"PE\x00\x00":
        raise PEError(f"no 'PE\\0\\0' signature at offset {e_lfanew}")

    coff = e_lfanew + 4
    (machine, nsections, _timestamp, _symtab, _nsyms,
     opt_size, characteristics) = _unpack("<HHIIIHH", data, coff, "COFF header")
    if nsections == 0:
        raise PEError("COFF header declares zero sections")
    if nsections > 96:
        raise PEError(f"COFF header declares {nsections} sections (limit 96)")

    opt = coff + 20
    if opt_size < 112:
        raise PEError(f"optional header is {opt_size} bytes; a PE32+ header "
                      "needs at least 112")
    if opt + opt_size > len(data):
        raise PEError("optional header runs past the end of the file")
    (magic,) = _unpack("<H", data, opt, "optional header")
    if magic == PE32_MAGIC:
        raise PEError("image is PE32 (32-bit); UEFI requires PE32+ "
                      "(subsystem images are 64-bit)")
    if magic != PE32_PLUS_MAGIC:
        raise PEError(f"optional header magic is {magic:#x}, expected "
                      f"{PE32_PLUS_MAGIC:#x} (PE32+)")

    (entry_rva,) = _unpack("<I", data, opt + 16, "optional header")
    (image_base,) = _unpack("<Q", data, opt + 24, "optional header")
    (section_align,) = _unpack("<I", data, opt + 32, "optional header")
    (file_align,) = _unpack("<I", data, opt + 36, "optional header")
    (size_of_image,) = _unpack("<I", data, opt + 56, "optional header")
    (size_of_headers,) = _unpack("<I", data, opt + 60, "optional header")
    (subsystem,) = _unpack("<H", data, opt + 68, "optional header")
    (num_dirs,) = _unpack("<I", data, opt + 108, "optional header")

    if not 0x200 <= file_align <= 0x10000 or file_align & (file_align - 1):
        raise PEError(f"file alignment {file_align:#x} is not a power of two "
                      "in 0x200..0x10000")
    if section_align == 0 or section_align & (section_align - 1):
        raise PEError(f"section alignment {section_align:#x} is not a power "
                      "of two")
    if size_of_image == 0 or size_of_image % section_align:
        raise PEError(f"size of image {size_of_image:#x} is not a non-zero "
                      f"multiple of the section alignment {section_align:#x}")
    if size_of_headers == 0 or size_of_headers > len(data):
        raise PEError(f"size of headers {size_of_headers} is empty or larger "
                      f"than the {len(data)}-byte file")
    if size_of_headers > size_of_image:
        raise PEError("size of headers exceeds size of image")

    if machine not in MACHINES:
        raise PEError(f"COFF machine {machine:#06x} is not a known "
                      "architecture")

    section_table = opt + opt_size
    sections = []
    for i in range(nsections):
        off = section_table + i * 40
        if off + 40 > len(data):
            raise PEError(f"section header {i} runs past the end of the file")
        raw_name = data[off:off + 8].rstrip(b"\x00")
        name = raw_name.decode("ascii", "replace")
        (vsize, vaddr, rawsize, rawptr, _relocs, _lines, nrelocs,
         _nlines, chars) = _unpack("<IIIIIIHHI", data, off + 8,
                                   f"section {name or i}")
        if rawsize:
            if rawptr == 0:
                raise PEError(f"section {name or i} has raw data of {rawsize} "
                              "bytes at offset 0")
            # PE file alignment means the raw data may be rounded up, but it
            # must still start inside the file and the declared bytes must fit.
            if rawptr + rawsize > len(data):
                raise PEError(f"section {name or i} declares {rawsize} raw "
                              f"bytes at offset {rawptr}, past the end of the "
                              f"{len(data)}-byte file")
            if rawptr < size_of_headers:
                raise PEError(f"section {name or i} raw data overlaps the "
                              "headers")
        sections.append({
            "name": name,
            "virtual_address": vaddr,
            "virtual_size": vsize,
            "raw_size": rawsize,
            "raw_offset": rawptr,
            "executable": bool(chars & IMAGE_SCN_MEM_EXECUTE),
        })

    if not any(s["executable"] for s in sections):
        raise PEError("no section is marked executable (IMAGE_SCN_MEM_EXECUTE)")
    if entry_rva == 0:
        raise PEError("address of entry point is zero")
    if entry_rva >= size_of_image:
        raise PEError(f"entry point {entry_rva:#x} is outside the image "
                      f"({size_of_image:#x} bytes)")
    entry_section = None
    for s in sections:
        span = max(s["virtual_size"], s["raw_size"])
        if s["virtual_address"] <= entry_rva < s["virtual_address"] + span:
            entry_section = s
            break
    if entry_section is None:
        raise PEError(f"entry point {entry_rva:#x} is not inside any section")
    if not entry_section["executable"]:
        raise PEError(f"entry point {entry_rva:#x} is in {entry_section['name']}, "
                      "which is not executable")

    data_directories = []
    for i in range(min(num_dirs, 16)):
        off = opt + 112 + i * 8
        if off + 8 > opt + opt_size:
            break
        rva, size = _unpack("<II", data, off, f"data directory {i}")
        if rva or size:
            data_directories.append({
                "index": i,
                "name": DATA_DIRECTORY_NAMES[i],
                "rva": rva,
                "size": size,
            })

    return {
        "format": "pe32+",
        "machine": machine,
        "machine_name": MACHINES[machine],
        "subsystem": subsystem,
        "subsystem_name": SUBSYSTEMS.get(subsystem, f"unknown-{subsystem}"),
        "uefi": machine in UEFI_MACHINES and subsystem in UEFI_SUBSYSTEMS,
        "entry_rva": entry_rva,
        "entry_section": entry_section["name"],
        "image_base": image_base,
        "image_size": size_of_image,
        "size_of_headers": size_of_headers,
        "section_alignment": section_align,
        "file_alignment": file_align,
        "characteristics": characteristics,
        "executable_image": bool(characteristics & IMAGE_FILE_EXECUTABLE_IMAGE),
        "dll": bool(characteristics & IMAGE_FILE_DLL),
        "sections": sections,
        "data_directories": data_directories,
        "bytes": len(data),
    }


def validate(data: bytes, require_subsystem: int | None = None) -> dict:
    """Parse *data* and enforce the UEFI constraints firmware enforces.

    ``require_subsystem=10`` additionally insists on an EFI *application*,
    which is what an El Torito boot entry or an ESP ``BOOTX64.EFI`` must be.
    """
    info = parse(data)

    if info["machine"] not in UEFI_MACHINES:
        raise PEError(
            f"COFF machine is {info['machine_name']} ({info['machine']:#06x}); "
            "firmware can only load x86_64 (0x8664) or aarch64 (0xaa64) UEFI "
            "images")
    if info["subsystem"] not in UEFI_SUBSYSTEMS:
        raise PEError(
            f"subsystem is {info['subsystem']} "
            f"({info['subsystem_name']}); a UEFI image must use 10 "
            "(efi_application), 11 (boot service driver), 12 (runtime driver) "
            "or 13 (ROM)")
    if not info["executable_image"]:
        raise PEError("IMAGE_FILE_EXECUTABLE_IMAGE is not set, so firmware "
                      "will not treat this as a loadable image")
    if require_subsystem is not None and info["subsystem"] != require_subsystem:
        wanted = SUBSYSTEMS.get(require_subsystem, str(require_subsystem))
        raise PEError(
            f"subsystem is {info['subsystem']} ({info['subsystem_name']}), but "
            f"{require_subsystem} ({wanted}) is required here")
    return info


def validate_file(path: str, require_subsystem: int | None = None) -> dict:
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise PEError(f"{path}: {exc}") from exc
    info = validate(data, require_subsystem)
    info["path"] = path
    return info


def summarise(info: dict) -> None:
    print(f"pe: {info['path']}")
    print(f"  machine     {info['machine_name']} ({info['machine']:#06x})")
    print(f"  subsystem   {info['subsystem']} ({info['subsystem_name']})")
    print(f"  entry       {info['entry_rva']:#x} in {info['entry_section']}")
    print(f"  image       {info['image_size']} bytes "
          f"({len(info['sections'])} sections)")
    if info["data_directories"]:
        dirs = ", ".join(d["name"] for d in info["data_directories"])
        print(f"  directories {dirs}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Validate a PE32+ UEFI image (no external tools)")
    ap.add_argument("file", help="the .efi / .img payload to inspect")
    ap.add_argument("--require-subsystem", type=int, metavar="N",
                    help="also require this subsystem (10 = EFI application)")
    ap.add_argument("--json", metavar="PATH",
                    help="write the parse result as JSON")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="print nothing on success")
    args = ap.parse_args(argv)

    try:
        info = validate_file(args.file, args.require_subsystem)
    except PEError as exc:
        print(f"pe: invalid UEFI image: {exc}", file=sys.stderr)
        return 1

    if not args.quiet:
        summarise(info)
    if args.json:
        try:
            with open(args.json, "w", encoding="utf-8") as fh:
                json.dump(info, fh, indent=2, sort_keys=True)
                fh.write("\n")
        except OSError as exc:
            print(f"pe: cannot write {args.json}: {exc}", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
