#!/usr/bin/env python3
"""ELF64 validator for ravynOS kernel payloads.

The UEFI loader does not run an ELF interpreter: it copies every ``PT_LOAD``
segment of ``/EFI/KERNEL.ELF`` to its link address, zero-fills the tail and
jumps to ``e_entry``.  Anything the loader cannot do that way is a boot that
hangs or jumps into free memory, so the properties it depends on are checked
here -- at kernel build time, again when the image is assembled, and once more
inside the finished image (``tools/image/verify.py``):

  * ELF64, little endian, ``ET_EXEC``, ``EM_X86_64``;
  * at least one ``PT_LOAD`` (``--require-load``);
  * every segment's file extent inside the file, ``MemSiz >= FileSiz``;
  * ``e_entry`` inside a loadable segment;
  * the image fits a window the firmware can reserve, e.g. above 1 MiB and
    below the 4 GiB line the early page tables cover (``--min-vaddr``,
    ``--max-end``);
  * no overlapping segments (the loader copies them in order; overlaps would
    silently clobber whichever came second).

Usage:
  elf.py KERNEL.ELF [--require-load] [--min-vaddr N] [--max-end N]
                    [--json PATH] [-q]

Exit status: 0 valid, 1 invalid, 2 usage.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys

ET_EXEC = 2
EM_X86_64 = 0x3E
PT_LOAD = 1
PT_DYNAMIC = 6
PT_INTERP = 3


class ElfError(Exception):
    pass


def parse(path: str) -> dict:
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise ElfError(f"cannot read {path}: {exc}") from exc
    return parse_bytes(data, path)


def parse_bytes(data: bytes, name: str = "<memory>") -> dict:
    """Parse an ELF64 image already in memory (used by the image verifier)."""
    if len(data) < 64:
        raise ElfError("file is shorter than an ELF64 header")
    if data[:4] != b"\x7fELF":
        raise ElfError("not an ELF file (bad magic)")
    if data[4] != 2:
        raise ElfError(f"ELF class {data[4]} is not 64-bit")
    if data[5] != 1:
        raise ElfError(f"ELF data encoding {data[5]} is not little-endian")

    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)

    phdrs = []
    if e_phnum:
        if e_phoff + e_phnum * e_phentsize > len(data):
            raise ElfError("program header table runs past the end of the file")
        if e_phentsize < 56:
            raise ElfError(f"program header size {e_phentsize} < 56")
        for i in range(e_phnum):
            base = e_phoff + i * e_phentsize
            (p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz,
             p_align) = struct.unpack_from("<IIQQQQQQ", data, base)
            phdrs.append(dict(type=p_type, flags=p_flags, offset=p_offset,
                              vaddr=p_vaddr, paddr=p_paddr, filesz=p_filesz,
                              memsz=p_memsz, align=p_align))

    return dict(data=data, file=name, type=e_type, machine=e_machine,
                version=e_version, entry=e_entry, phdrs=phdrs,
                sections=e_shnum, ehsize=e_ehsize)


def problems(info: dict, require_load: bool, min_vaddr: int | None,
             max_end: int | None) -> list[str]:
    out: list[str] = []
    data = info["data"]

    if info["type"] != ET_EXEC:
        out.append(f"e_type is {info['type']}, expected ET_EXEC (2): the "
                   "loader does not relocate")
    if info["machine"] != EM_X86_64:
        out.append(f"e_machine is {info['machine']}, expected EM_X86_64 "
                   f"({EM_X86_64})")

    loads = [p for p in info["phdrs"] if p["type"] == PT_LOAD]
    if require_load and not loads:
        out.append("no PT_LOAD segment: there is nothing for the loader to copy")

    for idx, p in enumerate(info["phdrs"]):
        kind = {PT_DYNAMIC: "PT_DYNAMIC", PT_INTERP: "PT_INTERP"}.get(p["type"])
        if kind:
            out.append(f"segment {idx} is {kind}: the kernel must be "
                       "freestanding and statically linked")
        if p["type"] != PT_LOAD:
            continue
        if p["offset"] + p["filesz"] > len(data):
            out.append(f"PT_LOAD at {p['vaddr']:#x}: file extent "
                       f"{p['offset']:#x}+{p['filesz']:#x} runs past the end "
                       f"of the {len(data)}-byte file")
        if p["memsz"] < p["filesz"]:
            out.append(f"PT_LOAD at {p['vaddr']:#x}: MemSiz {p['memsz']:#x} < "
                       f"FileSiz {p['filesz']:#x}")
        if min_vaddr is not None and p["vaddr"] < min_vaddr:
            out.append(f"PT_LOAD at {p['vaddr']:#x} is below the required "
                       f"minimum {min_vaddr:#x}")
        if max_end is not None and p["vaddr"] + p["memsz"] > max_end:
            out.append(f"PT_LOAD ends at {p['vaddr'] + p['memsz']:#x}, past "
                       f"the allowed maximum {max_end:#x}")

    if loads:
        inside = any(p["vaddr"] <= info["entry"] < p["vaddr"] + p["memsz"]
                     for p in loads)
        if not inside:
            out.append(f"e_entry {info['entry']:#x} is not inside any PT_LOAD "
                       "segment")

        ordered = sorted(loads, key=lambda p: p["vaddr"])
        for a, b in zip(ordered, ordered[1:]):
            if a["vaddr"] + a["memsz"] > b["vaddr"]:
                out.append(f"PT_LOAD segments overlap: {a['vaddr']:#x}+"
                           f"{a['memsz']:#x} and {b['vaddr']:#x}+{b['memsz']:#x}")

    return out


def summary(info: dict) -> str:
    loads = [p for p in info["phdrs"] if p["type"] == PT_LOAD]
    lines = [f"elf: {info['file']}",
             f"  entry         {info['entry']:#x}",
             f"  segments      {len(loads)} loadable of {len(info['phdrs'])}"]
    for p in loads:
        flags = "".join(c for bit, c in ((4, "R"), (2, "W"), (1, "X"))
                        if p["flags"] & bit)
        lines.append(f"    {p['vaddr']:>#12x} .. {p['vaddr'] + p['memsz']:>#12x}"
                     f"  filesz {p['filesz']:#x}  memsz {p['memsz']:#x}"
                     f"  {flags or '-'}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="elf.py", description="validate a ravynOS kernel ELF64 image")
    ap.add_argument("path")
    ap.add_argument("--require-load", action="store_true",
                    help="fail when there is no PT_LOAD segment")
    ap.add_argument("--min-vaddr", type=lambda s: int(s, 0), default=None,
                    help="minimum virtual address for a PT_LOAD (e.g. 0x100000)")
    ap.add_argument("--max-end", type=lambda s: int(s, 0), default=None,
                    help="maximum end address for a PT_LOAD (e.g. 0x100000000)")
    ap.add_argument("--json", metavar="PATH", help="write a machine-readable report")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args(argv)

    try:
        info = parse(args.path)
    except ElfError as exc:
        print(f"elf: {exc}", file=sys.stderr)
        return 1

    issues = problems(info, args.require_load, args.min_vaddr, args.max_end)

    if not args.quiet:
        print(summary(info))
        if issues:
            print("elf: NOT a loadable kernel image:", file=sys.stderr)
            for issue in issues:
                print(f"  - {issue}", file=sys.stderr)
        else:
            print("elf: layout OK")

    if args.json:
        record = {
            "schema": 1,
            "file": args.path,
            "valid": not issues,
            "entry": info["entry"],
            "machine": info["machine"],
            "segments": [
                {k: p[k] for k in ("type", "flags", "offset", "vaddr", "filesz",
                                   "memsz")}
                for p in info["phdrs"] if p["type"] == PT_LOAD
            ],
            "problems": issues,
        }
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(record, fh, indent=2, sort_keys=True)
            fh.write("\n")

    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
