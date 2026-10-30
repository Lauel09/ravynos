#!/usr/bin/env python3
"""Dependency-free ISO 9660 + El Torito image writer.

This is the fallback backend for ``tools/mkimage.sh``.  It exists so the
portable image pipeline works on a host that has *neither* xorriso *nor*
mtools -- a plain Python 3 interpreter is enough.  When xorriso is present
``mkimage.sh`` prefers it (it is battle-tested and can add Rock Ridge/Joliet);
this module covers the bare container/CI-runner case and is the reference
implementation the pipeline's tests validate against.

What it writes
--------------
* ISO 9660 (ECMA-119) primary volume descriptor.
* Both-endian numeric fields, both (L and M) path tables, and directory
  records packed so that no record ever crosses a 2048-byte sector boundary.
* El Torito "no emulation" boot catalog with an optional BIOS x86 entry and
  an optional UEFI entry (platform id 0xEF in a section header entry).
* ISO level 2 identifiers by default (30 chars, d-characters); ``--level 1``
  gives 8.3.  Collisions resolve deterministically with ``_N`` suffixes.

Deliberately *not* written: Rock Ridge, Joliet, multi-extent files, El Torito
emulation modes, and the ISO 9660 boot info table.  None are needed to boot
through BIOS int13 or UEFI block I/O, and each is a way to get the format
subtly wrong.

Byte layout references
----------------------
Field offsets below follow ECMA-119 (1988), section 8.4, and the El Torito
specification v1.0, section 2.  They were cross-checked against pycdlib (used
only as a test oracle, never as a build dependency).

Usage
-----
    iso9660.py build <srcdir> -o out.iso [--volume-id ID]
                     [--boot-image REL] [--boot-load-size N]
                     [--efi-image REL] [--efi-load-size N]
                     [--level 1|2] [--timestamp EPOCH]
                     [--print-manifest PATH]

Exit status: 0 on success, 1 on a rejected input, 2 on usage error.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import time

SECTOR = 2048
D_CHARS = frozenset("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
DEFAULT_EPOCH = 315532800  # 1980-01-01T00:00:00Z, the ISO epoch


# --------------------------------------------------------------------------
# Field encoding helpers
# --------------------------------------------------------------------------
def both32(value: int) -> bytes:
    """7.2.3: 32-bit numeric, little-endian then big-endian."""
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"both32 out of range: {value}")
    return struct.pack("<I", value) + struct.pack(">I", value)


def both16(value: int) -> bytes:
    """7.2.3: 16-bit numeric, little-endian then big-endian."""
    if not 0 <= value <= 0xFFFF:
        raise ValueError(f"both16 out of range: {value}")
    return struct.pack("<H", value) + struct.pack(">H", value)


def dir_date(epoch: int) -> bytes:
    """7.1.1: 7-byte recording date/time; tz offset 0 == UTC."""
    t = time.gmtime(epoch)
    return bytes([
        (t.tm_year - 1900) & 0xFF, t.tm_mon, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec, 0,
    ])


def vol_date(epoch: int) -> bytes:
    """8.4.26.1: 17-byte volume descriptor date/time (hundredths dropped)."""
    t = time.gmtime(epoch)
    return (f"{t.tm_year:04d}{t.tm_mon:02d}{t.tm_mday:02d}"
            f"{t.tm_hour:02d}{t.tm_min:02d}{t.tm_sec:02d}00"
            ).encode("ascii") + b"\x00"


def pad(text: str, width: int) -> bytes:
    """Space-padded a-characters (identifiers)."""
    raw = text.encode("ascii", "replace")[:width]
    return raw + b" " * (width - len(raw))


def zero_pad(text: str, width: int) -> bytes:
    """Zero-padded d-characters (El Torito identifiers)."""
    raw = text.encode("ascii", "replace")[:width]
    return raw + b"\x00" * (width - len(raw))


def sectors_for(nbytes: int) -> int:
    return (nbytes + SECTOR - 1) // SECTOR


# --------------------------------------------------------------------------
# Directory tree
# --------------------------------------------------------------------------
class IsoFile:
    __slots__ = ("name", "path", "ident", "data", "extent", "size", "epoch")

    def __init__(self, name: str, path: str, data: bytes, epoch: int):
        self.name = name
        self.path = path          # staged path, forward slashes
        self.data = data
        self.size = len(data)
        self.extent = 0
        self.epoch = epoch
        self.ident = b""

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"IsoFile({self.path!r}, {self.size}B @{self.extent})"


class IsoDir:
    __slots__ = ("name", "parent", "children", "dirs", "files", "extent",
                 "size", "number", "ident", "epoch")

    def __init__(self, name: str, parent: "IsoDir | None", epoch: int):
        self.name = name
        self.parent = parent
        self.children: list[IsoDir | IsoFile] = []
        self.dirs: list[IsoDir] = []
        self.files: list[IsoFile] = []
        self.extent = 0
        self.size = 0
        self.number = 0
        self.ident = b""
        self.epoch = epoch

    def add(self, entry: "IsoDir | IsoFile") -> None:
        self.children.append(entry)
        (self.dirs if isinstance(entry, IsoDir) else self.files).append(entry)


def mangle(name: str, level: int) -> str:
    """Map a file name onto ISO d-characters (level 2, or classic 8.3)."""
    upper = name.upper()
    base, dot, ext = upper.rpartition(".")
    if not dot:
        base, ext = upper, ""
    keep = lambda s: "".join(c if c in D_CHARS else "_" for c in s)
    base, ext = keep(base) or "_", keep(ext)
    if level == 1:
        base, ext = base[:8], ext[:3]
    else:
        ext = ext[:8]
        base = base[: max(1, 30 - (len(ext) + 1 if ext else 0))]
    return f"{base}.{ext}" if ext else base


def unique_ident(ident: str, used: set[str], level: int) -> str:
    """Resolve identifier collisions inside one directory deterministically."""
    if ident.upper() not in used:
        used.add(ident.upper())
        return ident
    stem, dot, ext = ident.rpartition(".")
    if not dot:
        stem, ext = ident, ""
    n = 1
    while True:
        suffix = f"_{n}"
        if level == 1:
            candidate = f"{stem[: 8 - len(suffix)]}{suffix}"
            candidate += f".{ext}" if ext else ""
        else:
            room = 30 - len(suffix) - (len(ext) + 1 if ext else 0)
            candidate = f"{stem[: max(1, room)]}{suffix}"
            candidate += f".{ext}" if ext else ""
        if candidate.upper() not in used:
            used.add(candidate.upper())
            return candidate
        n += 1
        if n > 999:  # pragma: no cover - pathological input
            raise ValueError(f"too many identifier collisions for {ident!r}")


def build_tree(srcdir: str, level: int, epoch: int) -> IsoDir:
    """Read ``srcdir`` into IsoDir/IsoFile objects without following symlinks.

    Symlinks are rejected loudly rather than dereferenced: a staged root full
    of symlinks would make image contents depend on the build host, which is
    exactly what the portable pipeline must avoid.
    """
    def walk(path: str, name: str, parent: IsoDir | None, rel: str) -> IsoDir:
        node = IsoDir(name, parent, epoch)
        used: set[str] = set()
        for entry in sorted(os.listdir(path), key=str.upper):
            full = os.path.join(path, entry)
            child_rel = f"{rel}/{entry}" if rel else entry
            if os.path.islink(full):
                raise ValueError(f"refusing to stage symlink: {full}")
            if os.path.isdir(full):
                child = walk(full, entry, node, child_rel)
                child.ident = unique_ident(mangle(entry, level), used, level).encode()
                node.add(child)
            elif os.path.isfile(full):
                with open(full, "rb") as fh:
                    data = fh.read()
                f = IsoFile(entry, child_rel, data, epoch)
                f.ident = (unique_ident(mangle(entry, level), used, level)
                           + ";1").encode()
                node.add(f)
            else:
                raise ValueError(f"refusing to stage special file: {full}")
        node.dirs.sort(key=lambda d: d.ident)
        node.files.sort(key=lambda f: f.ident)
        node.children = node.dirs + node.files
        return node

    if not os.path.isdir(srcdir):
        raise ValueError(f"not a directory: {srcdir}")
    return walk(srcdir, "", None, "")


def find_entry(root: IsoDir, relpath: str) -> IsoFile:
    """Resolve a slash-separated staged path to its IsoFile."""
    wanted = [p for p in relpath.replace(os.sep, "/").split("/") if p]
    node = root
    for i, part in enumerate(wanted):
        match = None
        for child in node.children:
            if child.name.upper() == part.upper():
                match = child
                break
        if match is None:
            raise ValueError(f"boot image not found in staged tree: {relpath}")
        if i == len(wanted) - 1:
            if isinstance(match, IsoDir):
                raise ValueError(f"path is a directory, not a file: {relpath}")
            return match
        if isinstance(match, IsoFile):
            raise ValueError(f"not a directory: {part} in {relpath}")
        node = match
    raise ValueError("empty boot image path")


# --------------------------------------------------------------------------
# Records
# --------------------------------------------------------------------------
def assign_numbers(root: IsoDir) -> list[IsoDir]:
    """Number directories breadth-first so every parent precedes its children."""
    ordered = [root]
    root.number = 1
    i = 0
    while i < len(ordered):
        node = ordered[i]
        for child in node.dirs:
            child.number = len(ordered) + 1
            ordered.append(child)
        i += 1
    return ordered


def dir_record(ident: bytes, extent: int, size: int, is_dir: bool, epoch: int) -> bytes:
    """7.3: one directory record.  Field 1 (length) includes the padding byte."""
    rec = bytearray(1)        # record length, filled in at the end
    rec += b"\x00"            # extended attribute record length
    rec += both32(extent)     # location of extent
    rec += both32(size)       # data length
    rec += dir_date(epoch)    # recording date and time
    rec += b"\x02" if is_dir else b"\x00"
    rec += b"\x00"            # file unit size (not interleaved)
    rec += b"\x00"            # interleave gap size
    rec += both16(1)          # volume sequence number
    rec += bytes([len(ident)])
    rec += ident
    if len(rec) % 2:
        rec += b"\x00"
    rec[0] = len(rec)
    return bytes(rec)


def dir_data(node: IsoDir) -> bytes:
    """Pack a directory's records into whole sectors, never splitting a record."""
    parent = node.parent or node
    records = [
        dir_record(b"\x00", node.extent, node.size, True, node.epoch),
        dir_record(b"\x01", parent.extent, parent.size, True, node.epoch),
    ]
    records += [
        dir_record(child.ident, child.extent, child.size,
                   isinstance(child, IsoDir), node.epoch)
        for child in node.children
    ]
    out = bytearray()
    for rec in records:
        # 6.8.1.1: a record must lie entirely within one logical sector.
        room = SECTOR - (len(out) % SECTOR)
        if len(rec) > room:
            out += b"\x00" * room
        out += rec
    out += b"\x00" * (-len(out) % SECTOR)
    return bytes(out)


def path_table(ordered: list[IsoDir], big_endian: bool) -> bytes:
    """9.4: path table, one entry per directory, parent number < own number."""
    fmt32 = ">I" if big_endian else "<I"
    fmt16 = ">H" if big_endian else "<H"
    out = bytearray()
    for node in ordered:
        ident = b"\x00" if node.parent is None else node.ident
        out += bytes([len(ident), 0])          # length, extended attr length
        out += struct.pack(fmt32, node.extent)  # location of extent
        out += struct.pack(fmt16, node.number)  # parent directory number
        out += ident
        if len(ident) % 2:
            out += b"\x00"
    return bytes(out)


# --------------------------------------------------------------------------
# Volume descriptors
# --------------------------------------------------------------------------
def primary_volume_descriptor(volume_id: str, total_sectors: int,
                              path_table_size: int, l_path_lba: int,
                              m_path_lba: int, root: IsoDir, epoch: int) -> bytes:
    """ECMA-119 8.4.  Every offset below is from the start of the sector."""
    d = bytearray(SECTOR)
    d[0] = 1                       # 8.4.1 type 1 = primary
    d[1:6] = b"CD001"              # 8.4.2 standard identifier
    d[6] = 1                       # 8.4.3 version
    d[7] = 0                       # 8.4.4 unused
    d[8:40] = pad("RAVYNOS", 32)   # 8.4.5 system identifier
    d[40:72] = pad(volume_id, 32)  # 8.4.6 volume identifier
    # 8.4.7 unused 1: 72..79 stays zero
    d[80:88] = both32(total_sectors)       # 8.4.8 volume space size
    # 8.4.9 escape sequences: 88..119 stays zero (no Joliet)
    d[120:124] = both16(1)                 # 8.4.10 volume set size
    d[124:128] = both16(1)                 # 8.4.11 volume sequence number
    d[128:132] = both16(SECTOR)            # 8.4.12 logical block size
    d[132:140] = both32(path_table_size)   # 8.4.13 path table size
    d[140:144] = struct.pack("<I", l_path_lba)   # 8.4.14 type L path table
    d[144:148] = struct.pack("<I", 0)            # 8.4.15 optional type L
    d[148:152] = struct.pack(">I", m_path_lba)   # 8.4.16 type M path table
    d[152:156] = struct.pack(">I", 0)            # 8.4.17 optional type M
    d[156:190] = dir_record(b"\x00", root.extent, root.size, True, epoch)
    d[190:318] = pad("", 128)      # 8.4.19 volume set identifier
    d[318:446] = pad("", 128)      # 8.4.20 publisher identifier
    d[446:574] = pad("RAVYNOS PORTABLE PIPELINE", 128)  # 8.4.21 data preparer
    d[574:702] = pad("RAVYNOS PORTABLE PIPELINE", 128)  # 8.4.22 application
    d[702:739] = pad("", 37)       # 8.4.23 copyright file identifier
    d[739:776] = pad("", 37)       # 8.4.24 abstract file identifier
    d[776:813] = pad("", 37)       # 8.4.25 bibliographic file identifier
    d[813:830] = vol_date(epoch)   # 8.4.26 creation
    d[830:847] = vol_date(epoch)   # 8.4.27 modification
    d[847:864] = b"0" * 16 + b"\x00"  # 8.4.28 expiration: none
    d[864:881] = b"0" * 16 + b"\x00"  # 8.4.29 effective: immediately
    d[881] = 1                     # 8.4.30 file structure version
    d[882] = 0                     # 8.4.31 reserved
    # 8.4.32 application use (883..1394) and reserved (1395..2047) stay zero
    return bytes(d)


def boot_record_descriptor(catalog_lba: int) -> bytes:
    """El Torito 2.1: Boot Record Volume Descriptor."""
    d = bytearray(SECTOR)
    d[0] = 0                                    # boot record indicator
    d[1:6] = b"CD001"                           # ISO 9660 identifier
    d[6] = 1                                    # version
    d[7:39] = zero_pad("EL TORITO SPECIFICATION", 32)  # boot system id
    # bytes 39..70: unused
    d[71:75] = struct.pack("<I", catalog_lba)   # absolute pointer to catalog
    return bytes(d)


def catalog_entry(lba: int, load_sectors: int, *, media: int = 0,
                  load_segment: int = 0) -> bytes:
    """A 32-byte El Torito boot entry (x86, no emulation by default)."""
    e = bytearray(32)
    e[0] = 0x88                                 # bootable
    e[1] = media                                # 0 = no emulation
    e[2:4] = struct.pack("<H", load_segment)
    e[4] = 0                                    # system type
    e[5] = 0                                    # unused
    e[6:8] = struct.pack("<H", load_sectors)    # virtual/loaded sectors
    e[8:12] = struct.pack("<I", lba)            # load RBA (2048-byte units)
    # bytes 12..31: selection criteria, unused for a boot entry
    return bytes(e)


def section_header(platform: int, entries: int, final: bool = True) -> bytes:
    h = bytearray(32)
    h[0] = 0x91 if final else 0x90             # last / more section header
    h[1] = platform                            # 0x00 x86, 0xEF UEFI
    h[2:4] = struct.pack("<H", entries)
    return bytes(h)


def boot_catalog(bios: tuple[int, int] | None, efi: tuple[int, int] | None) -> bytes:
    """El Torito 2.1 boot catalog: validation entry + default + sections."""
    cat = bytearray(SECTOR)
    val = bytearray(32)
    val[0] = 0x01                  # header id
    val[1] = 0x00                  # platform id: 80x86
    val[4:28] = zero_pad("RAVYNOS", 24)
    val[30] = 0x55
    val[31] = 0xAA
    words = struct.unpack("<16H", bytes(val))
    val[28:30] = struct.pack("<H", (-sum(words)) & 0xFFFF)
    cat[0:32] = val

    # The initial/default entry at offset 32 is mandatory even for a
    # UEFI-only image; an unbootable placeholder is the spec-sanctioned way to
    # satisfy that when there is no BIOS image.
    off = 32
    cat[off:off + 32] = (catalog_entry(*bios) if bios
                         else bytes(32))
    off += 32
    if efi is not None:
        cat[off:off + 32] = section_header(0xEF, 1, final=True)
        off += 32
        cat[off:off + 32] = catalog_entry(*efi)
        off += 32
    return bytes(cat)


# --------------------------------------------------------------------------
# Image assembly
# --------------------------------------------------------------------------
class IsoImage:
    """Deterministic ISO 9660 image assembled from a staging directory."""

    def __init__(self, root: IsoDir, volume_id: str, epoch: int,
                 boot_image: str | None = None, boot_load_size: int = 4,
                 efi_image: str | None = None, efi_load_size: int = 1):
        self.root = root
        self.volume_id = volume_id
        self.epoch = epoch
        self.boot_file = find_entry(root, boot_image) if boot_image else None
        self.efi_file = find_entry(root, efi_image) if efi_image else None
        self.boot_load_size = boot_load_size
        self.efi_load_size = efi_load_size
        self.manifest: list[tuple[str, int, int]] = []
        self._layout()

    # -- layout ------------------------------------------------------------
    def _layout(self) -> None:
        self.ordered = assign_numbers(self.root)
        has_boot = self.boot_file is not None or self.efi_file is not None

        # Path table size is extent-independent, so it can be computed before
        # directories get their extents.
        pt = path_table(self.ordered, big_endian=False)
        self.path_table_size = len(pt)
        pt_sectors = max(1, sectors_for(len(pt)))

        lba = 16
        self.pvd_lba = lba
        lba += 1
        if has_boot:
            self.br_lba = lba
            lba += 1
        self.terminator_lba = lba
        lba += 1
        if has_boot:
            self.catalog_lba = lba
            lba += 1
        self.l_path_lba = lba
        lba += pt_sectors
        self.m_path_lba = lba
        lba += pt_sectors

        for node in self.ordered:
            node.extent = lba
            node.size = len(dir_data(node))  # length is extent-independent
            lba += node.size // SECTOR
        for node in self.ordered:
            for f in node.files:
                f.extent = lba
                self.manifest.append((f.path, f.extent, f.size))
                lba += sectors_for(f.size)

        self.total_sectors = lba
        if self.total_sectors > 0xFFFFFFFF:
            raise ValueError("image exceeds the 32-bit ISO 9660 extent limit")

    # -- emit --------------------------------------------------------------
    def build(self) -> bytes:
        image = bytearray(self.total_sectors * SECTOR)

        def put(lba: int, data: bytes) -> None:
            # Extents are padded with the zero bytes the image was allocated
            # with; only the bounds are checked here.
            start = lba * SECTOR
            end = start + len(data)
            if end > len(image):
                raise ValueError(f"write past end of image at LBA {lba}")
            image[start:end] = data

        put(self.pvd_lba, primary_volume_descriptor(
            self.volume_id, self.total_sectors, self.path_table_size,
            self.l_path_lba, self.m_path_lba, self.root, self.epoch))
        if self.boot_file is not None or self.efi_file is not None:
            put(self.br_lba, boot_record_descriptor(self.catalog_lba))
            put(self.catalog_lba, boot_catalog(
                (self.boot_file.extent, self.boot_load_size) if self.boot_file else None,
                (self.efi_file.extent, self.efi_load_size) if self.efi_file else None))
        term = bytearray(SECTOR)
        term[0] = 255
        term[1:6] = b"CD001"
        term[6] = 1
        put(self.terminator_lba, bytes(term))

        put(self.l_path_lba, path_table(self.ordered, big_endian=False))
        put(self.m_path_lba, path_table(self.ordered, big_endian=True))
        for node in self.ordered:
            put(node.extent, dir_data(node))
        for node in self.ordered:
            for f in node.files:
                if f.size:
                    put(f.extent, f.data)
        return bytes(image)

    def manifest_text(self, boot_note: str = "") -> str:
        lines = [
            "# ravynos iso manifest",
            f"volume {self.volume_id}",
            f"bytes {self.total_sectors * SECTOR}",
            f"sectors {self.total_sectors}",
        ]
        if self.boot_file is not None:
            lines.append(f"bios_boot {self.boot_file.path} "
                         f"lba={self.boot_file.extent} "
                         f"load_sectors={self.boot_load_size}")
        if self.efi_file is not None:
            lines.append(f"uefi_boot {self.efi_file.path} "
                         f"lba={self.efi_file.extent} "
                         f"load_sectors={self.efi_load_size}")
        if boot_note:
            lines.append(f"note {boot_note}")
        lines.append("# lba size path")
        for path, extent, size in self.manifest:
            lines.append(f"{extent:08d} {size:10d} {path}")
        return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def parse_epoch(value: str) -> int:
    if value.upper() == "SOURCE_DATE_EPOCH":
        raw = os.environ.get("SOURCE_DATE_EPOCH", "")
        return int(raw) if raw.isdigit() else DEFAULT_EPOCH
    return int(value)


def cmd_build(args: argparse.Namespace) -> int:
    epoch = parse_epoch(args.timestamp) if args.timestamp else int(time.time())
    try:
        root = build_tree(args.srcdir, args.level, epoch)
        img = IsoImage(root, args.volume_id, epoch,
                       boot_image=args.boot_image,
                       boot_load_size=args.boot_load_size,
                       efi_image=args.efi_image,
                       efi_load_size=args.efi_load_size)
        data = img.build()
    except (ValueError, OSError) as exc:
        print(f"iso9660: {exc}", file=sys.stderr)
        return 1

    tmp = args.output + ".tmp"
    try:
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, args.output)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)

    if args.print_manifest:
        with open(args.print_manifest, "w", encoding="utf-8") as fh:
            fh.write(img.manifest_text())
    print(f"iso9660: wrote {args.output} ({img.total_sectors} sectors, "
          f"{len(data) / (1024 * 1024):.2f} MiB)")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="Dependency-free ISO 9660 + El Torito writer")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build an ISO from a staging directory")
    b.add_argument("srcdir")
    b.add_argument("-o", "--output", required=True)
    b.add_argument("--volume-id", default="RAVYNOS")
    b.add_argument("--boot-image", help="staged path of the BIOS El Torito image")
    b.add_argument("--boot-load-size", type=int, default=4,
                   help="512-byte sectors the BIOS loads (default 4)")
    b.add_argument("--efi-image", help="staged path of the UEFI boot image")
    b.add_argument("--efi-load-size", type=int, default=1,
                   help="512-byte sectors the firmware loads (default 1)")
    b.add_argument("--level", type=int, choices=(1, 2), default=2)
    b.add_argument("--timestamp",
                   help="fixed epoch seconds or the literal SOURCE_DATE_EPOCH")
    b.add_argument("--print-manifest", metavar="PATH")
    b.set_defaults(func=cmd_build)
    args = ap.parse_args(argv)
    if args.cmd == "build" and min(args.boot_load_size, args.efi_load_size) < 1:
        ap.error("load sizes are counted in 512-byte sectors and must be >= 1")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
