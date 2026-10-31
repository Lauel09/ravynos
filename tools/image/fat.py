#!/usr/bin/env python3
"""Dependency-free FAT16 + GPT disk image writer.

Second fallback backend for ``tools/mkimage.sh``: builds the dd-able ``.img``
variant of a ravynOS image -- a GPT-partitioned disk with a FAT filesystem
holding the staged boot tree -- without root, mounts, ``mdconfig``, ``gpart``
or mtools.  When mtools is installed the pipeline can use it instead
(``mkimage.sh --backend mtools``); this module is the reference implementation
and the fallback for bare CI runners.

Scope, and why it is enough
---------------------------
* Primary + backup GPT, protective MBR, CRC32-protected headers and entry
  arrays, ESP type GUID, 2048-sector (1 MiB) partition alignment.
* FAT16, 2 FATs, 512-byte sectors, cluster size chosen automatically (2 KiB
  by default).  FAT16 requires between 4085 and 65524 clusters; requests that
  cannot satisfy that are rejected loudly instead of silently down-converting
  to a different FAT type.
* Subdirectories, cluster chains, 8.3 short names *and* VFAT long file names
  (UEFI firmware reads both), correct ``.``/``..`` entries.
* Deterministic output: fixed volume serial, DOS timestamps derived from one
  epoch, GPT GUIDs derived from the volume label.  Two runs on two hosts
  produce byte-identical images.

Not implemented: FAT32, exFAT, sparse files, multiple partitions, hybrid MBR.
None are needed for a bootable ESP; FAT32 only matters if the boot tree
outgrows FAT16's 2 GiB limit, which is checked and rejected.

Usage
-----
    fat.py build <srcdir> -o disk.img [--size-mb N] [--offset-sectors 2048]
                   [--label RAVYNOS] [--partition-name "EFI System Partition"]
                   [--no-gpt] [--timestamp EPOCH] [--print-manifest PATH]

Exit status: 0 on success, 1 on a rejected input, 2 on usage error.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import time
import uuid
import zlib

SECTOR = 512
ESP_TYPE_GUID = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
INVALID_8_3 = set('"*+,/:;<=>?[\\]|. ') | {chr(c) for c in range(0x20)}
DEFAULT_EPOCH = 315532800  # 1980-01-01T00:00:00Z, the FAT epoch

FAT16_MIN_CLUSTERS = 4085
FAT16_MAX_CLUSTERS = 65524
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = GPT_ENTRIES * GPT_ENTRY_SIZE // SECTOR  # 32
FIRST_USABLE_LBA = 2 + GPT_ENTRY_SECTORS                      # 34


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------
def sectors_for(nbytes: int, sector: int = SECTOR) -> int:
    return (nbytes + sector - 1) // sector


def deterministic_guid(seed: str) -> uuid.UUID:
    """A stable GUID, so repeated builds are byte-identical."""
    digest = hashlib.sha256(("ravynos-image:" + seed).encode()).digest()
    return uuid.UUID(bytes=digest[:16], version=4)


def dos_datetime(epoch: int) -> tuple[int, int]:
    """(date, time) in FAT's 16-bit DOS format."""
    t = time.gmtime(epoch)
    year = min(max(t.tm_year, 1980), 2107)
    date = ((year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    dtime = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return date, dtime


# --------------------------------------------------------------------------
# Staging tree
# --------------------------------------------------------------------------
class FatFile:
    __slots__ = ("name", "path", "data", "cluster", "size")

    def __init__(self, name: str, path: str, data: bytes):
        self.name = name
        self.path = path
        self.data = data
        self.size = len(data)
        self.cluster = 0


class FatDir:
    __slots__ = ("name", "path", "parent", "children", "cluster", "nclusters")

    def __init__(self, name: str, path: str, parent: "FatDir | None"):
        self.name = name
        self.path = path
        self.parent = parent
        self.children: list[FatDir | FatFile] = []
        self.cluster = 0
        self.nclusters = 0


def scan_tree(srcdir: str) -> FatDir:
    """Read ``srcdir`` into FatDir/FatFile objects; reject symlinks.

    Symlinks would make the image content depend on the host that built it,
    which defeats the point of a reproducible portable pipeline.
    """
    def walk(path: str, name: str, parent: FatDir | None, rel: str) -> FatDir:
        node = FatDir(name, rel, parent)
        for entry in sorted(os.listdir(path), key=str.upper):
            full = os.path.join(path, entry)
            child_rel = f"{rel}/{entry}" if rel else entry
            if os.path.islink(full):
                raise ValueError(f"refusing to stage symlink: {full}")
            if os.path.isdir(full):
                node.children.append(walk(full, entry, node, child_rel))
            elif os.path.isfile(full):
                with open(full, "rb") as fh:
                    node.children.append(FatFile(entry, child_rel, fh.read()))
            else:
                raise ValueError(f"refusing to stage special file: {full}")
        return node

    if not os.path.isdir(srcdir):
        raise ValueError(f"not a directory: {srcdir}")
    return walk(srcdir, "", None, "")


# --------------------------------------------------------------------------
# FAT name handling
# --------------------------------------------------------------------------
def lfn_checksum(short_name: bytes) -> int:
    total = 0
    for byte in short_name:
        total = (((total & 1) << 7) + (total >> 1) + byte) & 0xFF
    return total


def short_name_for(name: str, used: set[bytes]) -> tuple[bytes, bool]:
    """Return (11-byte 8.3 name, needs_lfn).

    A name that already is a valid upper-case 8.3 name is used verbatim and
    needs no long-name entries; anything else gets a ``~N`` tail plus LFN
    entries so the original spelling survives.
    """
    upper = name.upper()
    base, dot, ext = upper.rpartition(".")
    if not dot:
        base, ext = upper, ""

    def clean(s: str) -> str:
        return "".join(c for c in s if c not in INVALID_8_3)

    valid_base, valid_ext = clean(base), clean(ext)
    fits = (bool(valid_base) and len(valid_base) <= 8 and len(valid_ext) <= 3
            and valid_base == base and valid_ext == ext and name == upper)
    if fits:
        candidate = f"{valid_base:<8}{valid_ext:<3}".encode("ascii")
        if candidate not in used:
            used.add(candidate)
            return candidate, False

    stem = valid_base or "_"
    n = 1
    while True:
        tail = f"~{n}"
        alias = f"{stem[: 8 - len(tail)]}{tail:<8}"[:8]
        candidate = f"{alias:<8}{valid_ext[:3]:<3}".encode("ascii")
        if candidate not in used:
            used.add(candidate)
            return candidate, True
        n += 1
        if n > 999999:  # pragma: no cover - pathological input
            raise ValueError(f"cannot find a free 8.3 alias for {name!r}")


def lfn_entry_count(name: str) -> int:
    """How many 13-character long-name entries a name occupies."""
    n = len(name.encode("utf-16-le")) // 2
    return (n + (1 if n % 13 == 0 else 0) + 12) // 13


def lfn_entries(name: str, short_name: bytes) -> list[bytes]:
    """VFAT long-name entries for ``name``, in on-disk order.

    Per the FAT specification the final entry is padded with 0xFFFF, and a NUL
    terminator is included only when the name length is an exact multiple of
    13 code units.  Both Linux's vfat driver and UEFI's FAT driver read that
    form; padding with 0x0000 instead (which some writers do) leaves readers
    that do not trim at NUL -- pyfatfs among them -- with a name full of NULs.
    """
    checksum = lfn_checksum(short_name)
    encoded = name.encode("utf-16-le")
    units = [encoded[i:i + 2] for i in range(0, len(encoded), 2)]
    if len(units) % 13 == 0:
        units.append(b"\x00\x00")
    while len(units) % 13:
        units.append(b"\xff\xff")
    chunks = [b"".join(units[i:i + 13]) for i in range(0, len(units), 13)]
    if len(chunks) > 20:
        raise ValueError(f"long name too long for VFAT: {name!r}")

    entries = []
    for index, chunk in enumerate(chunks, start=1):
        seq = index | (0x40 if index == len(chunks) else 0)
        e = bytearray(32)
        e[0] = seq
        e[1:11] = chunk[0:10]
        e[11] = 0x0F  # long-name attribute
        e[12] = 0x00  # type
        e[13] = checksum
        e[14:26] = chunk[10:22]
        e[26:28] = b"\x00\x00"  # first cluster, unused
        e[28:32] = chunk[22:26]
        entries.append(bytes(e))
    entries.reverse()  # the last chunk is written first
    return entries


def dir_entry(name11: bytes, attr: int, cluster: int, size: int,
              date: int, dtime: int) -> bytes:
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    e[12] = 0  # NT reserved
    e[13] = 0  # creation time, tenths of a second
    e[14:16] = struct.pack("<H", dtime)
    e[16:18] = struct.pack("<H", date)
    e[18:20] = struct.pack("<H", date)  # last access date
    e[20:22] = b"\x00\x00"              # first cluster, high word
    e[22:24] = struct.pack("<H", dtime)  # write time
    e[24:26] = struct.pack("<H", date)   # write date
    e[26:28] = struct.pack("<H", cluster & 0xFFFF)
    e[28:32] = struct.pack("<I", size)
    return bytes(e)


# --------------------------------------------------------------------------
# FAT16 volume
# --------------------------------------------------------------------------
def fat16_geometry(total_sectors: int, spc: int, reserved: int,
                   root_entries: int, nfats: int) -> tuple[int, int]:
    """Return (sectors_per_fat, cluster_count) for a FAT16 volume.

    Follows the iterative sizing rule used by mkfs.fat: the FAT must be able
    to describe every cluster plus the two reserved entries.
    """
    root_sectors = sectors_for(root_entries * 32)
    fat_sectors = 1
    for _ in range(64):
        data_sectors = total_sectors - (reserved + nfats * fat_sectors + root_sectors)
        if data_sectors <= 0:
            return fat_sectors, 0
        clusters = data_sectors // spc
        needed = sectors_for((clusters + 2) * 2)
        if needed == fat_sectors:
            return fat_sectors, clusters
        fat_sectors = needed
    return fat_sectors, 0


class Fat16Volume:
    """A FAT16 filesystem image built from a staging tree."""

    def __init__(self, tree: FatDir, total_sectors: int, label: str,
                 epoch: int, spc: int | None = None,
                 reserved: int = 1, root_entries: int = 512, nfats: int = 2):
        self.tree = tree
        self.total_sectors = total_sectors
        self.label = label[:11].upper()
        self.epoch = epoch
        self.reserved = reserved
        self.root_entries = root_entries
        self.nfats = nfats
        self.bps = SECTOR

        if spc is not None:
            self.spc = spc
        else:
            # Largest cluster that still yields a legal FAT16 volume: fewer
            # FAT sectors, less cluster-boundary waste, and a smaller image.
            chosen = None
            for candidate in (64, 32, 16, 8, 4, 2, 1):
                _, clusters = fat16_geometry(total_sectors, candidate,
                                             reserved, root_entries, nfats)
                if FAT16_MIN_CLUSTERS <= clusters <= FAT16_MAX_CLUSTERS:
                    chosen = candidate
                    break
            if chosen is None:
                raise ValueError(
                    f"volume of {total_sectors} sectors ({total_sectors * SECTOR // 1024} KiB) "
                    "cannot be a legal FAT16 volume; FAT16 needs 4085..65524 "
                    "clusters. Increase the image size.")
            self.spc = chosen

        self.sectors_per_fat, self.cluster_count = fat16_geometry(
            total_sectors, self.spc, reserved, root_entries, nfats)
        if not FAT16_MIN_CLUSTERS <= self.cluster_count <= FAT16_MAX_CLUSTERS:
            raise ValueError(
                f"FAT16 cluster count out of range: {self.cluster_count} "
                f"(need {FAT16_MIN_CLUSTERS}..{FAT16_MAX_CLUSTERS})")
        self.cluster_bytes = self.spc * SECTOR
        self.root_dir_sectors = sectors_for(root_entries * 32)
        self.fat_start = self.reserved
        self.root_start = self.reserved + nfats * self.sectors_per_fat
        self.data_start = self.root_start + self.root_dir_sectors
        self.data_sectors = self.cluster_count * self.spc
        self.used_sectors = self.data_start + self.data_sectors
        if self.used_sectors > total_sectors:
            raise ValueError("FAT16 geometry overflows the volume")

        self.fat = [0] * (self.cluster_count + 2)
        self.fat[0] = 0xFFF8  # media descriptor
        self.fat[1] = 0xFFFF  # clean shutdown / no errors
        self._next = 2
        self._allocated_bytes = 0
        self.entries: dict[int, bytes] = {}   # cluster -> directory data
        self.filedata: dict[int, bytes] = {}  # cluster -> file data

    # -- allocation --------------------------------------------------------
    def _alloc(self, nclusters: int) -> int:
        if nclusters == 0:
            return 0
        first = self._next
        if first + nclusters - 1 > self.cluster_count + 1:
            raise ValueError(
                "staged tree does not fit in the image; increase --size-mb")
        for i in range(nclusters):
            cluster = first + i
            self.fat[cluster] = cluster + 1 if i < nclusters - 1 else 0xFFFF
        self._next += nclusters
        self._allocated_bytes += nclusters * self.cluster_bytes
        return first

    def _dir_record_bytes(self, node: FatDir, is_root: bool) -> int:
        used: set[bytes] = set()
        nbytes = 0
        if not is_root:
            nbytes += 2 * 32  # "." and ".."
        for child in node.children:
            _, needs_lfn = short_name_for(child.name, used)
            nbytes += 32 * (lfn_entry_count(child.name) if needs_lfn else 0)
            nbytes += 32
        if is_root:
            nbytes += 32  # volume label entry
        return nbytes

    # -- build -------------------------------------------------------------
    def build(self) -> bytes:
        dirs: list[FatDir] = []
        files: list[FatFile] = []

        def collect(node: FatDir) -> None:
            dirs.append(node)
            for child in node.children:
                if isinstance(child, FatDir):
                    collect(child)
                else:
                    files.append(child)

        collect(self.tree)

        # Allocate directory clusters first (children need their parent's
        # cluster number when "." and ".." are generated), then file data.
        for node in dirs:
            if node is self.tree:
                continue
            nbytes = self._dir_record_bytes(node, is_root=False)
            cluster = self._alloc(max(1, sectors_for(nbytes, self.cluster_bytes)))
            node.cluster = cluster
            node.nclusters = max(1, sectors_for(nbytes, self.cluster_bytes))

        for f in files:
            nclusters = sectors_for(f.size, self.cluster_bytes) if f.size else 0
            f.cluster = self._alloc(nclusters)

        # Now that every cluster is assigned, materialise the entries.
        for node in dirs:
            self.entries[node.cluster] = self._dir_entries(node, node is self.tree)
        for f in files:
            if f.size:
                self.filedata[f.cluster] = f.data

        return self._emit()

    def _dir_entries(self, node: FatDir, is_root: bool) -> bytes:
        date, dtime = dos_datetime(self.epoch)
        out = bytearray()
        used: set[bytes] = set()
        if is_root:
            label11 = f"{self.label:<11}"[:11].encode("ascii", "replace")
            out += dir_entry(label11, 0x08, 0, 0, date, dtime)
        else:
            out += dir_entry(b".          ", 0x10, node.cluster, 0, date, dtime)
            parent_cluster = node.parent.cluster if node.parent is not None and \
                node.parent is not self.tree else 0
            out += dir_entry(b"..         ", 0x10, parent_cluster, 0, date, dtime)

        for child in node.children:
            short_name, needs_lfn = short_name_for(child.name, used)
            if needs_lfn:
                out += b"".join(lfn_entries(child.name, short_name))
            if isinstance(child, FatDir):
                out += dir_entry(short_name, 0x10, child.cluster,
                                 child.nclusters * self.cluster_bytes, date, dtime)
            else:
                out += dir_entry(short_name, 0x20, child.cluster,
                                 child.size, date, dtime)
        if len(out) > self.root_dir_sectors * SECTOR and is_root:
            raise ValueError(
                f"root directory needs {len(out)} bytes but FAT16 allows "
                f"{self.root_dir_sectors * SECTOR}")
        return bytes(out)

    # -- emit --------------------------------------------------------------
    def _fat_bytes(self) -> bytes:
        entries = self.fat[: self.cluster_count + 2]
        capacity = self.sectors_per_fat * SECTOR // 2
        if len(entries) > capacity:  # pragma: no cover - geometry guarantees it
            raise ValueError(
                f"FAT too small: {len(entries)} entries in {capacity} slots")
        entries += [0] * (capacity - len(entries))
        return struct.pack(f"<{capacity}H", *entries)

    def _boot_sector(self) -> bytes:
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x3C\x90"                 # jmp short +0x3C, nop
        bs[3:11] = b"RAVYNOS "                    # OEM name
        struct.pack_into("<H", bs, 11, self.bps)
        bs[13] = self.spc
        struct.pack_into("<H", bs, 14, self.reserved)
        bs[16] = self.nfats
        struct.pack_into("<H", bs, 17, self.root_entries)
        if self.total_sectors < 0x10000:
            struct.pack_into("<H", bs, 19, self.total_sectors)
        else:
            struct.pack_into("<H", bs, 19, 0)
            struct.pack_into("<I", bs, 32, self.total_sectors)
        bs[21] = 0xF8                             # media descriptor: fixed disk
        struct.pack_into("<H", bs, 22, self.sectors_per_fat)
        struct.pack_into("<H", bs, 24, 32)        # sectors per track
        struct.pack_into("<H", bs, 26, 64)        # heads
        struct.pack_into("<I", bs, 28, 0)         # hidden sectors (set by GPT)
        bs[36] = 0x80                             # drive number
        bs[37] = 0
        bs[38] = 0x29                             # extended boot signature
        serial = int.from_bytes(
            hashlib.sha256(("ravynos-fat:" + self.label).encode()).digest()[:4],
            "little")
        struct.pack_into("<I", bs, 39, serial)
        bs[43:54] = f"{self.label:<11}"[:11].encode("ascii", "replace")
        bs[54:62] = b"FAT16   "
        # Legacy-boot trap.  Some firmware fallback paths (OVMF without a
        # matching Boot#### variable: "UEFI Misc Device") map the first ESP
        # sectors at 0xB0000 and jump there *without parsing any PE*.  The
        # old payload was `int 0x18`, which under OVMF's 1:1 high mapping
        # executed an unrelated BIOS vector instead of trapping.  We place a
        # guaranteed invalid opcode (UD2, 0F FF) at the very entry point so
        # the CPU faults deterministically into the firmware exception
        # handler; ravynOS' BOOTX64.EFI detects that handoff context and
        # rescues the boot from the firmware stack.  See Docs/BootLoader.md
        # §"Legacy-handoff rescue".
        bs[62] = 0x0F   # UD2 -- reserved opcode, always #UD
        bs[63] = 0xFF
        bs[64:66] = b"\x00\x00"
        bs[510:512] = b"\x55\xAA"
        return bytes(bs)

    def _emit(self) -> bytes:
        image = bytearray(self.total_sectors * SECTOR)

        def put_sector(index: int, data: bytes) -> None:
            start = index * SECTOR
            image[start:start + len(data)] = data

        put_sector(0, self._boot_sector())
        fat = self._fat_bytes()
        for i in range(self.nfats):
            put_sector(self.fat_start + i * self.sectors_per_fat, fat)

        if self.entries.get(0):
            put_sector(self.root_start, self.entries[0])

        def cluster_offset(cluster: int) -> int:
            return (self.data_start + (cluster - 2) * self.spc) * SECTOR

        for cluster, data in self.entries.items():
            if cluster == 0:
                continue
            image[cluster_offset(cluster):cluster_offset(cluster) + len(data)] = data
        for cluster, data in self.filedata.items():
            image[cluster_offset(cluster):cluster_offset(cluster) + len(data)] = data

        return bytes(image)


# --------------------------------------------------------------------------
# GPT
# --------------------------------------------------------------------------
def protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR)
    entry = bytearray(16)
    entry[0] = 0x00       # not bootable
    entry[1:4] = b"\x00\x02\x00"  # CHS of first sector (0/0/2)
    entry[4] = 0xEE       # GPT protective
    entry[5:8] = b"\xFF\xFF\xFF"
    struct.pack_into("<I", entry, 8, 1)  # starting LBA
    struct.pack_into("<I", entry, 12, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    return bytes(mbr)


def gpt_header(current_lba: int, backup_lba: int, first_usable: int,
               last_usable: int, disk_guid: uuid.UUID,
               entries_lba: int, entries_crc: int) -> bytes:
    hdr = bytearray(SECTOR)
    hdr[0:8] = b"EFI PART"
    struct.pack_into("<I", hdr, 8, 0x00010000)   # revision 1.0
    struct.pack_into("<I", hdr, 12, 92)          # header size
    struct.pack_into("<Q", hdr, 24, current_lba)
    struct.pack_into("<Q", hdr, 32, backup_lba)
    struct.pack_into("<Q", hdr, 40, first_usable)
    struct.pack_into("<Q", hdr, 48, last_usable)
    hdr[56:72] = disk_guid.bytes_le
    struct.pack_into("<Q", hdr, 72, entries_lba)
    struct.pack_into("<I", hdr, 80, GPT_ENTRIES)
    struct.pack_into("<I", hdr, 84, GPT_ENTRY_SIZE)
    struct.pack_into("<I", hdr, 88, entries_crc)
    struct.pack_into("<I", hdr, 16, zlib.crc32(bytes(hdr[:92])) & 0xFFFFFFFF)
    return bytes(hdr)


def gpt_entries(partition_start: int, partition_end: int,
                part_guid: uuid.UUID, name: str) -> bytes:
    entries = bytearray(GPT_ENTRIES * GPT_ENTRY_SIZE)
    e = bytearray(GPT_ENTRY_SIZE)
    e[0:16] = ESP_TYPE_GUID.bytes_le
    e[16:32] = part_guid.bytes_le
    struct.pack_into("<Q", e, 32, partition_start)
    struct.pack_into("<Q", e, 40, partition_end)
    struct.pack_into("<Q", e, 48, 0)  # attributes
    encoded = name.encode("utf-16-le")[:72]
    e[56:56 + len(encoded)] = encoded
    entries[0:GPT_ENTRY_SIZE] = e
    return bytes(entries)


def wrap_in_gpt(fat: bytes, partition_start: int, total_sectors: int,
                label: str, partition_name: str) -> bytes:
    """Place a FAT image at ``partition_start`` inside a GPT-partitioned disk."""
    part_sectors = sectors_for(len(fat))
    partition_end = partition_start + part_sectors - 1
    last_usable = total_sectors - 1 - GPT_ENTRY_SECTORS - 1
    if partition_end > last_usable:
        raise ValueError(
            f"partition ends at LBA {partition_end} but the last usable LBA is "
            f"{last_usable}; increase the image size")

    disk_guid = deterministic_guid(f"{label}:disk")
    part_guid = deterministic_guid(f"{label}:esp")
    entries = gpt_entries(partition_start, partition_end, part_guid, partition_name)
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF

    image = bytearray(total_sectors * SECTOR)
    image[0:SECTOR] = protective_mbr(total_sectors)

    # Primary header at LBA 1, entry array at LBA 2..33.
    image[SECTOR:2 * SECTOR] = gpt_header(
        1, total_sectors - 1, FIRST_USABLE_LBA, last_usable,
        disk_guid, 2, entries_crc)
    image[2 * SECTOR:(2 + GPT_ENTRY_SECTORS) * SECTOR] = entries

    start = partition_start * SECTOR
    image[start:start + len(fat)] = fat

    # Backup entry array and backup header at the end of the disk.
    backup_entries_lba = total_sectors - 1 - GPT_ENTRY_SECTORS
    image[backup_entries_lba * SECTOR:
          (backup_entries_lba + GPT_ENTRY_SECTORS) * SECTOR] = entries
    image[(total_sectors - 1) * SECTOR:total_sectors * SECTOR] = gpt_header(
        total_sectors - 1, 1, FIRST_USABLE_LBA, last_usable,
        disk_guid, backup_entries_lba, entries_crc)
    return bytes(image)


# --------------------------------------------------------------------------
# Planning
# --------------------------------------------------------------------------
def estimate_tree_bytes(tree: FatDir) -> int:
    """Upper-bound estimate of the bytes a tree occupies in the filesystem."""
    total = 0
    stack = [tree]
    while stack:
        node = stack.pop()
        total += 32 * (len(node.children) + (1 if node is tree else 2)) * 2
        for child in node.children:
            if isinstance(child, FatDir):
                stack.append(child)
            else:
                total += child.size + 8192  # slack for cluster rounding
    return total


def choose_image_sectors(tree: FatDir, size_mb: int | None,
                         offset_sectors: int) -> int:
    """Pick a disk size that holds the tree and is a legal FAT16 volume."""
    needed = estimate_tree_bytes(tree) + 2 * 1024 * 1024  # slack + metadata
    floor_sectors = 16 * 1024 * 1024 // SECTOR  # 16 MiB
    if size_mb is not None:
        return size_mb * 1024 * 1024 // SECTOR
    total = max(floor_sectors, sectors_for(needed) + offset_sectors + 34)
    # Round the disk up to a whole MiB so partition math stays predictable.
    mib = 1024 * 1024 // SECTOR
    return ((total + mib - 1) // mib) * mib


def build_disk_image(tree: FatDir, *, size_mb: int | None = None,
                     offset_sectors: int = 2048, label: str = "RAVYNOS",
                     partition_name: str = "EFI System Partition",
                     epoch: int = DEFAULT_EPOCH, gpt: bool = True,
                     spc: int | None = None) -> tuple[bytes, dict]:
    """Build a (.img) disk image carrying ``tree`` in a FAT16 volume.

    An explicit ``size_mb`` is honoured exactly and fails loudly if the tree
    does not fit; an automatic size grows in 4 MiB steps until it does, so a
    bigger userland never silently truncates the image.
    """
    total_sectors = choose_image_sectors(tree, size_mb, offset_sectors)
    mib = 1024 * 1024 // SECTOR
    attempts = 1 if size_mb is not None else 64
    last_error = "staged tree does not fit"
    for _ in range(attempts):
        partition_sectors = (total_sectors - offset_sectors - 1 - GPT_ENTRY_SECTORS
                             if gpt else total_sectors)
        if partition_sectors <= 0:
            raise ValueError("image is too small for a GPT partition table")
        try:
            fat = Fat16Volume(tree, partition_sectors, label, epoch, spc=spc)
            data = fat.build()
            if gpt:
                data = wrap_in_gpt(data, offset_sectors, total_sectors,
                                   label, partition_name)
            break
        except ValueError as exc:
            last_error = str(exc)
            if size_mb is not None:
                raise
            total_sectors += 4 * mib
    else:  # pragma: no cover - only reachable with an enormous tree
        raise ValueError(last_error)

    meta = {
        "total_sectors": total_sectors,
        "partition_start_lba": offset_sectors if gpt else 0,
        "partition_sectors": partition_sectors,
        "sectors_per_cluster": fat.spc,
        "cluster_count": fat.cluster_count,
        "sectors_per_fat": fat.sectors_per_fat,
        "used_clusters": fat._next - 2,
        "fat_bytes": partition_sectors * SECTOR,
        "gpt": gpt,
        "label": fat.label,
    }
    return data, meta


def manifest_text(meta: dict) -> str:
    lines = ["# ravynos disk image manifest"]
    for key in ("label", "total_sectors", "partition_start_lba",
                "partition_sectors", "sectors_per_cluster", "cluster_count",
                "sectors_per_fat", "used_clusters", "gpt"):
        lines.append(f"{key} {meta[key]}")
    lines.append(f"bytes {meta.get('total_sectors', 0) * SECTOR}")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def cmd_build(args: argparse.Namespace) -> int:
    try:
        tree = scan_tree(args.srcdir)
        data, meta = build_disk_image(
            tree,
            size_mb=args.size_mb,
            offset_sectors=args.offset_sectors,
            label=args.label,
            partition_name=args.partition_name,
            epoch=args.epoch,
            gpt=not args.no_gpt,
            spc=args.sectors_per_cluster,
        )
    except (ValueError, OSError) as exc:
        print(f"fat: {exc}", file=sys.stderr)
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
            fh.write(manifest_text(meta))
    print(f"fat: wrote {args.output} ({meta['total_sectors'] * SECTOR // 1024} KiB, "
          f"FAT16 {meta['cluster_count']} clusters of "
          f"{meta['sectors_per_cluster'] * SECTOR} B"
          + (", GPT" if meta["gpt"] else ", no partition table") + ")")
    return 0


def cmd_wrap(args: argparse.Namespace) -> int:
    """Wrap an existing FAT image (e.g. from mtools) in a GPT partition table."""
    try:
        with open(args.fat, "rb") as fh:
            fat = fh.read()
        if len(fat) % SECTOR:
            raise ValueError(f"FAT image size {len(fat)} is not a multiple of {SECTOR}")
        part_sectors = len(fat) // SECTOR
        mib = 1024 * 1024 // SECTOR
        total = args.offset_sectors + part_sectors + 1 + GPT_ENTRY_SECTORS
        total = ((total + mib - 1) // mib) * mib
        if args.size_mb is not None:
            requested = args.size_mb * 1024 * 1024 // SECTOR
            if requested < args.offset_sectors + part_sectors + 1 + GPT_ENTRY_SECTORS:
                raise ValueError(
                    f"--size-mb {args.size_mb} is too small for a "
                    f"{part_sectors}-sector filesystem")
            total = requested
        data = wrap_in_gpt(fat, args.offset_sectors, total, args.label,
                           args.partition_name)
    except (ValueError, OSError) as exc:
        print(f"fat: {exc}", file=sys.stderr)
        return 1
    tmp = args.output + ".tmp"
    try:
        with open(tmp, "wb") as fh:
            fh.write(data)
        os.replace(tmp, args.output)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)
    print(f"fat: wrapped {args.fat} in GPT -> {args.output} "
          f"({total * SECTOR // 1024} KiB, partition at LBA {args.offset_sectors})")
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Dependency-free FAT16 + GPT writer")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build a disk image from a staging directory")
    b.add_argument("srcdir")
    b.add_argument("-o", "--output", required=True)
    b.add_argument("--size-mb", type=int)
    b.add_argument("--offset-sectors", type=int, default=2048)
    b.add_argument("--label", default="RAVYNOS")
    b.add_argument("--partition-name", default="EFI System Partition")
    b.add_argument("--no-gpt", action="store_true",
                   help="write a bare FAT image (no MBR/GPT)")
    b.add_argument("--sectors-per-cluster", type=int,
                   choices=(1, 2, 4, 8, 16, 32, 64))
    b.add_argument("--timestamp",
                   help="fixed epoch seconds or the literal SOURCE_DATE_EPOCH")
    b.add_argument("--print-manifest", metavar="PATH")
    b.set_defaults(func=cmd_build)

    w = sub.add_parser("wrap", help="wrap an existing FAT image in a GPT disk")
    w.add_argument("--fat", required=True)
    w.add_argument("-o", "--output", required=True)
    w.add_argument("--offset-sectors", type=int, default=2048)
    w.add_argument("--size-mb", type=int)
    w.add_argument("--label", default="RAVYNOS")
    w.add_argument("--partition-name", default="EFI System Partition")
    w.set_defaults(func=cmd_wrap)

    args = ap.parse_args(argv)
    # Only the `build` subparser defines --timestamp.  Reading args.timestamp
    # unconditionally made `wrap` -- the mtools backend's GPT step -- die with
    # AttributeError before writing anything, on every host, since it landed.
    raw_timestamp = getattr(args, "timestamp", None)
    if raw_timestamp:
        raw = raw_timestamp
        if raw.upper() == "SOURCE_DATE_EPOCH":
            env = os.environ.get("SOURCE_DATE_EPOCH", "")
            args.epoch = int(env) if env.isdigit() else DEFAULT_EPOCH
        else:
            args.epoch = int(raw)
    else:
        args.epoch = int(time.time())
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
