#!/usr/bin/env python3
"""Portable, rootless HFS+/HFSX volume and MBR (0xAF Apple_HFS) image writer.

Layout and on-disk invariants follow Apple's XNU and diskdev_cmds sources in
this repository:
  - Kernel/Extensions/hfs/core/hfs_format.h (HFSPlusVolumeHeader, BTHeaderRec,
    BTNodeDescriptor, HFSPlusCatalogFolder/File/Thread, HFSPlusForkData)
  - Kernel/Extensions/hfs/core/hfs_vfsutils.c:334-465,668-676 (VH validation,
    HFSX kHFSBinaryCompare -> cat_binarykeycompare)
  - Kernel/Extensions/hfs/core/hfs_endian.c:250-600 (BTNode offset & key checks)
  - Kernel/Extensions/hfs/core/hfs_catalog.c:3835-3858,4183-4236 (key ordering
    and catalog record construction)
  - Kernel/Extensions/IOStorageFamily/IOFDiskPartitionScheme.cpp:415-505 and
    Info.plist:139-140 (MBR type 0xAF -> IOMedia "Content" = "Apple_HFS",
    matched by IOFindBSDRoot() at IOKitBSDInit.cpp:585 without boot-uuid)
  - BSD/sbin/newfs_hfs/makehfs.c:670-966,1050-1695 (VH, MSB-first allocation
    bitmap, extents/attributes/catalog B-trees)

No root privileges, host mounts, or external tools are used.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import os
from pathlib import Path
import stat
import struct
import sys
import unicodedata
from typing import Dict, List, Optional, Sequence, Tuple

SECTOR_SIZE = 512
DEFAULT_BLOCK_SIZE = 4096
CATALOG_NODE_SIZE = 8192
ATTR_NODE_SIZE = 8192
EXTENTS_NODE_SIZE = 4096

# Signatures & versions (Kernel/Extensions/hfs/core/hfs_format.h:51-62)
K_HFSPLUS_SIGWORD = 0x482B   # 'H+'
K_HFSX_SIGWORD = 0x4858      # 'HX'
K_HFSPLUS_VERSION = 0x0004
K_HFSX_VERSION = 0x0005
K_HFSPLUS_MOUNT_VERSION = 0x31302E30  # '10.0'

# Volume attributes (hfs_format.h:595-626)
K_HFS_VOLUME_UNMOUNTED_MASK = 0x00000100
K_HFS_UNUSED_NODE_FIX_MASK = 0x80000000

# Fixed deterministic Mac HFS timestamp (>= kHFSUnusedNodesFixDate 0xC5EF2480)
# 0xDC000000 = 2082844800 + 1607628800 (2020-12-10T19:33:20Z)
DEFAULT_HFS_TIMESTAMP = 0xDC000000

# Reserved Catalog Node IDs (hfs_format.h:276-290)
K_HFS_ROOT_PARENT_ID = 1
K_HFS_ROOT_FOLDER_ID = 2
K_HFS_EXTENTS_FILE_ID = 3
K_HFS_CATALOG_FILE_ID = 4
K_HFS_BAD_BLOCK_FILE_ID = 5
K_HFS_ALLOCATION_FILE_ID = 6
K_HFS_STARTUP_FILE_ID = 7
K_HFS_ATTRIBUTES_FILE_ID = 8
K_HFS_FIRST_USER_CNID = 16

# Catalog record types & flags (hfs_format.h:310-335)
K_HFSPLUS_FOLDER_RECORD = 1
K_HFSPLUS_FILE_RECORD = 2
K_HFSPLUS_FOLDER_THREAD_RECORD = 3
K_HFSPLUS_FILE_THREAD_RECORD = 4

K_HFS_THREAD_EXISTS_MASK = 0x0002
K_HFS_HAS_FOLDER_COUNT_MASK = 0x0010

# Symlink Finder type/creator (hfs_format.h:130-131, hfs_vnops.c:6601-6602)
K_SYMLINK_FILE_TYPE = 0x736C6E6B    # 'slnk'
K_SYMLINK_CREATOR = 0x72686170      # 'rhap'

# POSIX mode constants (matching Darwin/POSIX sys/stat.h)
S_IFMT_MASK = 0o170000
S_IFDIR_MODE = 0o040000
S_IFREG_MODE = 0o100000
S_IFLNK_MODE = 0o120000

# B-Tree constants (hfs_format.h:575-586, 718-776)
K_BT_LEAF_NODE = -1
K_BT_INDEX_NODE = 0
K_BT_HEADER_NODE = 1
K_BT_MAP_NODE = 2

K_BT_BIG_KEYS_MASK = 0x00000002
K_BT_VARIABLE_INDEX_KEYS_MASK = 0x00000004

K_HFS_CASE_FOLDING = 0xCF
K_HFS_BINARY_COMPARE = 0xBC

K_HFSPLUS_EXTENT_KEY_MAX_LEN = 10
K_HFSPLUS_CATALOG_KEY_MIN_LEN = 6
K_HFSPLUS_CATALOG_KEY_MAX_LEN = 516
K_HFSPLUS_ATTR_KEY_MAX_LEN = 266
K_BTREE_HEADER_USER_BYTES = 128

# MBR constants (IOFDiskPartitionScheme.h:68-105, Info.plist:139-140)
MBR_SIGNATURE = 0xAA55
MBR_TYPE_APPLE_HFS = 0xAF
MBR_PART_START_LBA = 2048  # 1 MiB alignment


class HFSFormatError(ValueError):
    """Raised when staging inputs or verified HFS+ structures are invalid."""


def _is_apple_nfd_excluded(cp: int) -> bool:
    return (
        0x2000 <= cp <= 0x2FFF
        or 0xF900 <= cp <= 0xFAFF
        or 0x2F800 <= cp <= 0x2FAFF
    )


def encode_hfs_unistr255(name: str) -> Tuple[int, ...]:
    """Encode a POSIX filename into HFS+ decomposed UTF-16 code units."""
    if not name:
        return ()
    # In POSIX VFS on HFS+, ':' on disk represents '/' and '/' is separator;
    # swap ':' -> '/' if ever present in a POSIX component.
    mapped = name.replace(":", "/")
    out_chars: List[str] = []
    for ch in mapped:
        cp = ord(ch)
        if _is_apple_nfd_excluded(cp):
            out_chars.append(ch)
        else:
            out_chars.append(unicodedata.normalize("NFD", ch))
    decomposed = "".join(out_chars)
    raw = decomposed.encode("utf-16-be")
    if len(raw) > 255 * 2:
        raise HFSFormatError(f"HFS+ filename exceeds 255 UTF-16 units: {name!r}")
    count = len(raw) // 2
    return struct.unpack(f">{count}H", raw)


def decode_hfs_unistr255(units: Sequence[int]) -> str:
    if not units:
        return ""
    raw = struct.pack(f">{len(units)}H", *units)
    return raw.decode("utf-16-be").replace("/", ":")


def _casefold_unit(u: int) -> int:
    # FastUnicodeCompare ASCII + Latin-1 folding for optional HFS+ ('H+') mode.
    if 0x0041 <= u <= 0x005A:
        return u + 0x20
    if 0x00C0 <= u <= 0x00D6 or 0x00D8 <= u <= 0x00DE:
        return u + 0x20
    return u


def catalog_sort_key(
    parent_id: int, units: Tuple[int, ...], case_sensitive: bool
) -> Tuple[int, Tuple[int, ...]]:
    if case_sensitive:
        # Matches cat_binarykeycompare (hfs_catalog.c:3835-3858)
        return (parent_id, units)
    return (parent_id, tuple(_casefold_unit(u) for u in units))


def pack_catalog_key(parent_id: int, units: Sequence[int]) -> bytes:
    key_len = K_HFSPLUS_CATALOG_KEY_MIN_LEN + 2 * len(units)
    if key_len > K_HFSPLUS_CATALOG_KEY_MAX_LEN:
        raise HFSFormatError(f"catalog key too long: {key_len}")
    hdr = struct.pack(">HIH", key_len, parent_id, len(units))
    if not units:
        return hdr
    return hdr + struct.pack(f">{len(units)}H", *units)


def pack_bsd_info(
    uid: int, gid: int, file_mode: int, special: int = 1, admin_flags: int = 0, owner_flags: int = 0
) -> bytes:
    b = struct.pack(
        ">IIBBHI",
        uid & 0xFFFFFFFF,
        gid & 0xFFFFFFFF,
        admin_flags & 0xFF,
        owner_flags & 0xFF,
        file_mode & 0xFFFF,
        special & 0xFFFFFFFF,
    )
    assert len(b) == 16
    return b


def pack_fork_data(
    logical_size: int,
    clump_size: int,
    total_blocks: int,
    extents: Sequence[Tuple[int, int]] = (),
) -> bytes:
    padded = list(extents[:8]) + [(0, 0)] * (8 - len(extents[:8]))
    parts = [struct.pack(">QII", logical_size, clump_size, total_blocks)]
    for sb, bc in padded:
        parts.append(struct.pack(">II", sb, bc))
    b = b"".join(parts)
    assert len(b) == 80
    return b


def pack_catalog_folder(
    folder_id: int,
    valence: int,
    folder_count: int,
    timestamp: int,
    uid: int,
    gid: int,
    mode: int,
    case_sensitive: bool,
) -> bytes:
    flags = K_HFS_THREAD_EXISTS_MASK
    if case_sensitive:
        flags |= K_HFS_HAS_FOLDER_COUNT_MASK
    bsd = pack_bsd_info(uid, gid, (mode & 0o7777) | S_IFDIR_MODE, special=1)
    b = struct.pack(
        ">hHIIIIIII16s16s16sII",
        K_HFSPLUS_FOLDER_RECORD,
        flags,
        valence,
        folder_id,
        timestamp,
        timestamp,
        timestamp,
        timestamp,
        0,  # backupDate
        bsd,
        b"\x00" * 16,  # userInfo (FndrDirInfo)
        b"\x00" * 16,  # finderInfo (FndrOpaqueInfo)
        0,  # textEncoding
        folder_count if case_sensitive else 0,
    )
    assert len(b) == 88
    return b


def pack_catalog_file(
    file_id: int,
    timestamp: int,
    uid: int,
    gid: int,
    mode: int,
    is_symlink: bool,
    logical_size: int,
    start_block: int,
    block_count: int,
    block_size: int,
) -> bytes:
    if is_symlink:
        file_mode = S_IFLNK_MODE | 0o777
        user_info = struct.pack(">IIH6s", K_SYMLINK_FILE_TYPE, K_SYMLINK_CREATOR, 0, b"\x00" * 6)
    else:
        file_mode = (mode & 0o7777) | S_IFREG_MODE
        user_info = b"\x00" * 16
    bsd = pack_bsd_info(uid, gid, file_mode, special=1)
    exts = [(start_block, block_count)] if block_count > 0 else []
    data_fork = pack_fork_data(
        logical_size=logical_size,
        clump_size=0,
        total_blocks=block_count,
        extents=exts,
    )
    rsrc_fork = pack_fork_data(0, 0, 0, ())
    b = struct.pack(
        ">hHIIIIIII16s16s16sII80s80s",
        K_HFSPLUS_FILE_RECORD,
        K_HFS_THREAD_EXISTS_MASK,
        0,  # reserved1
        file_id,
        timestamp,
        timestamp,
        timestamp,
        timestamp,
        0,  # backupDate
        bsd,
        user_info,
        b"\x00" * 16,  # finderInfo
        0,  # textEncoding
        0,  # reserved2
        data_fork,
        rsrc_fork,
    )
    assert len(b) == 248
    return b


def pack_catalog_thread(is_folder: bool, parent_id: int, units: Sequence[int]) -> bytes:
    rec_type = K_HFSPLUS_FOLDER_THREAD_RECORD if is_folder else K_HFSPLUS_FILE_THREAD_RECORD
    hdr = struct.pack(">hhIH", rec_type, 0, parent_id, len(units))
    if not units:
        return hdr
    return hdr + struct.pack(f">{len(units)}H", *units)


@dataclass
class StagedEntry:
    rel_path: str
    name: str
    is_dir: bool
    is_symlink: bool
    mode: int
    uid: int
    gid: int
    data: bytes = b""
    cnid: int = 0
    parent_cnid: int = 0
    children: List["StagedEntry"] = field(default_factory=list)
    start_block: int = 0
    block_count: int = 0


def _collect_staging_tree(
    root_dir: Optional[Path],
    uid: int,
    gid: int,
    volume_name: str,
) -> Tuple[StagedEntry, List[StagedEntry]]:
    root_entry = StagedEntry(
        rel_path="",
        name=volume_name,
        is_dir=True,
        is_symlink=False,
        mode=0o755,
        uid=uid,
        gid=gid,
        cnid=K_HFS_ROOT_FOLDER_ID,
        parent_cnid=K_HFS_ROOT_PARENT_ID,
    )
    all_entries: List[StagedEntry] = [root_entry]
    if root_dir is None:
        return root_entry, all_entries

    if not root_dir.is_dir():
        raise HFSFormatError(f"staging root is not a directory: {root_dir}")

    st_root = os.lstat(root_dir)
    root_entry.mode = stat.S_IMODE(st_root.st_mode) or 0o755

    next_cnid = K_HFS_FIRST_USER_CNID

    def walk_dir(host_dir: Path, parent: StagedEntry) -> None:
        nonlocal next_cnid
        names = sorted(os.listdir(host_dir))
        for name in names:
            host_path = host_dir / name
            lst = os.lstat(host_path)
            fmode = stat.S_IFMT(lst.st_mode)
            imode = stat.S_IMODE(lst.st_mode)
            rel = f"{parent.rel_path}/{name}" if parent.rel_path else name
            if fmode == stat.S_IFDIR:
                entry = StagedEntry(
                    rel_path=rel,
                    name=name,
                    is_dir=True,
                    is_symlink=False,
                    mode=imode or 0o755,
                    uid=uid,
                    gid=gid,
                    cnid=next_cnid,
                    parent_cnid=parent.cnid,
                )
                next_cnid += 1
                parent.children.append(entry)
                all_entries.append(entry)
                walk_dir(host_path, entry)
            elif fmode == stat.S_IFLNK:
                target = os.readlink(host_path)
                target_bytes = target.encode("utf-8")
                entry = StagedEntry(
                    rel_path=rel,
                    name=name,
                    is_dir=False,
                    is_symlink=True,
                    mode=0o777,
                    uid=uid,
                    gid=gid,
                    data=target_bytes,
                    cnid=next_cnid,
                    parent_cnid=parent.cnid,
                )
                next_cnid += 1
                parent.children.append(entry)
                all_entries.append(entry)
            elif fmode == stat.S_IFREG:
                data = host_path.read_bytes()
                entry = StagedEntry(
                    rel_path=rel,
                    name=name,
                    is_dir=False,
                    is_symlink=False,
                    mode=imode,
                    uid=uid,
                    gid=gid,
                    data=data,
                    cnid=next_cnid,
                    parent_cnid=parent.cnid,
                )
                next_cnid += 1
                parent.children.append(entry)
                all_entries.append(entry)
            else:
                # Ignore sockets/FIFOs/device nodes during rootless staging
                continue

    walk_dir(root_dir, root_entry)
    return root_entry, all_entries


def _pack_btree_node(
    kind: int,
    height: int,
    records: Sequence[bytes],
    node_size: int,
    flink: int = 0,
    blink: int = 0,
) -> bytes:
    buf = bytearray(node_size)
    num_records = len(records)
    struct.pack_into(
        ">IIbBHH",
        buf,
        0,
        flink,
        blink,
        kind,
        height,
        num_records,
        0,
    )
    offset = 14  # sizeof(BTNodeDescriptor)
    offsets: List[int] = [offset]
    for rec in records:
        rec_len = len(rec)
        if rec_len & 1:
            rec = rec + b"\x00"
            rec_len += 1
        buf[offset : offset + rec_len] = rec
        offset += rec_len
        offsets.append(offset)

    table_start = node_size - 2 * (num_records + 1)
    if offset > table_start:
        raise HFSFormatError(
            f"B-tree node overflow: data end {offset} > offset table {table_start}"
        )
    # Reverse offset table at end of node: [free_offset, ..., rec1_offset, rec0_offset]
    for idx, rec_off in enumerate(offsets):
        pos = node_size - 2 * (idx + 1)
        struct.pack_into(">H", buf, pos, rec_off)
    return bytes(buf)


def _mark_bitmap_bits(bitmap: bytearray, start_bit: int, count: int) -> None:
    # MSB-first bitmap per makehfs.c:940-965 and BTree map records
    for b in range(start_bit, start_bit + count):
        bitmap[b >> 3] |= 1 << (7 - (b & 7))


def _build_btree(
    leaf_items: Sequence[Tuple[bytes, bytes]],
    node_size: int,
    max_key_length: int,
    key_compare_type: int,
    attributes: int,
    clump_size: int,
    min_total_nodes: int,
) -> bytes:
    """Build a complete HFS+ B-tree (header node, leaf/index levels, map nodes)."""
    if not leaf_items:
        # Empty B-tree (used for extentsFile and attributesFile)
        total_nodes = max(min_total_nodes, 2)
        tree_depth = 0
        root_node = 0
        first_leaf = 0
        last_leaf = 0
        leaf_records = 0
        internal_nodes: List[bytes] = []
    else:
        # 1. Pack leaf nodes (height = 1)
        leaf_groups: List[List[Tuple[bytes, bytes]]] = []
        cur_group: List[Tuple[bytes, bytes]] = []
        cur_bytes = 14 + 2  # BTNodeDescriptor + free-space offset slot
        for key_b, val_b in leaf_items:
            rec_len = len(key_b) + len(val_b)
            if rec_len & 1:
                rec_len += 1
            needed = rec_len + 2  # record bytes + 2-byte offset slot
            if cur_group and (cur_bytes + needed > node_size):
                leaf_groups.append(cur_group)
                cur_group = []
                cur_bytes = 14 + 2
            cur_group.append((key_b, val_b))
            cur_bytes += needed
        if cur_group:
            leaf_groups.append(cur_group)

        # Assign node numbers starting at 1 for leaf nodes, then index levels above
        next_node_num = 1
        leaf_node_nums = [next_node_num + i for i in range(len(leaf_groups))]
        next_node_num += len(leaf_groups)
        first_leaf = leaf_node_nums[0]
        last_leaf = leaf_node_nums[-1]
        leaf_records = len(leaf_items)

        internal_nodes_by_num: Dict[int, bytes] = {}
        for idx, grp in enumerate(leaf_groups):
            node_num = leaf_node_nums[idx]
            flink = leaf_node_nums[idx + 1] if idx + 1 < len(leaf_groups) else 0
            blink = leaf_node_nums[idx - 1] if idx > 0 else 0
            recs = [k + v for k, v in grp]
            internal_nodes_by_num[node_num] = _pack_btree_node(
                K_BT_LEAF_NODE, 1, recs, node_size, flink=flink, blink=blink
            )

        # Build index levels if current level has > 1 node
        cur_level_FirstKeys: List[Tuple[bytes, int]] = [
            (grp[0][0], leaf_node_nums[idx]) for idx, grp in enumerate(leaf_groups)
        ]
        height = 1
        while len(cur_level_FirstKeys) > 1:
            height += 1
            idx_groups: List[List[Tuple[bytes, int]]] = []
            cur_igrp: List[Tuple[bytes, int]] = []
            cur_ibytes = 14 + 2
            for first_key, child_num in cur_level_FirstKeys:
                rec_len = len(first_key) + 4
                if rec_len & 1:
                    rec_len += 1
                needed = rec_len + 2
                if cur_igrp and (cur_ibytes + needed > node_size):
                    idx_groups.append(cur_igrp)
                    cur_igrp = []
                    cur_ibytes = 14 + 2
                cur_igrp.append((first_key, child_num))
                cur_ibytes += needed
            if cur_igrp:
                idx_groups.append(cur_igrp)

            lvl_nums = [next_node_num + i for i in range(len(idx_groups))]
            next_node_num += len(idx_groups)
            for idx, igrp in enumerate(idx_groups):
                node_num = lvl_nums[idx]
                flink = lvl_nums[idx + 1] if idx + 1 < len(idx_groups) else 0
                blink = lvl_nums[idx - 1] if idx > 0 else 0
                recs = [fk + struct.pack(">I", cn) for fk, cn in igrp]
                internal_nodes_by_num[node_num] = _pack_btree_node(
                    K_BT_INDEX_NODE, height, recs, node_size, flink=flink, blink=blink
                )
            cur_level_FirstKeys = [
                (igrp[0][0], lvl_nums[idx]) for idx, igrp in enumerate(idx_groups)
            ]

        tree_depth = height
        root_node = cur_level_FirstKeys[0][1]
        internal_nodes = [
            internal_nodes_by_num[n] for n in range(1, next_node_num)
        ]
        # Leave free nodes so runtime writes (e.g. devfs/mount updates) never split immediately
        total_nodes = max(min_total_nodes, 1 + len(internal_nodes) + 8)

    # Header node map capacity (makehfs.c:1562-1579)
    header_map_bytes = node_size - 14 - 106 - K_BTREE_HEADER_USER_BYTES - 8
    header_map_bits = header_map_bytes * 8
    map_node_bytes = node_size - 14 - 4 - 2  # long-word aligned per WriteMapNodes
    map_node_bits = map_node_bytes * 8

    # Compute map nodes if total_nodes > header_map_bits
    map_nodes_count = 0
    if total_nodes > header_map_bits:
        map_nodes_count = (
            total_nodes - header_map_bits + map_node_bits - 1
        ) // map_node_bits

    used_nodes = 1 + len(internal_nodes) + map_nodes_count
    if used_nodes > total_nodes:
        total_nodes = used_nodes + 8
        if total_nodes > header_map_bits:
            map_nodes_count = (
                total_nodes - header_map_bits + map_node_bits - 1
            ) // map_node_bits
            used_nodes = 1 + len(internal_nodes) + map_nodes_count

    free_nodes = total_nodes - used_nodes

    bt_header_rec = struct.pack(
        ">HIIIIHHIIHIBBI64s",
        tree_depth,
        root_node,
        leaf_records,
        first_leaf,
        last_leaf,
        node_size,
        max_key_length,
        total_nodes,
        free_nodes,
        0,  # reserved1
        clump_size,
        0,  # btreeType
        key_compare_type,
        attributes,
        b"\x00" * 64,  # reserved3[16]
    )
    assert len(bt_header_rec) == 106

    full_map_bytes = header_map_bytes + map_nodes_count * map_node_bytes
    node_bitmap = bytearray(full_map_bytes)
    _mark_bitmap_bits(node_bitmap, 0, used_nodes)

    first_map_node_num = (1 + len(internal_nodes)) if map_nodes_count > 0 else 0
    header_node = _pack_btree_node(
        K_BT_HEADER_NODE,
        0,
        [
            bt_header_rec,
            b"\x00" * K_BTREE_HEADER_USER_BYTES,
            bytes(node_bitmap[:header_map_bytes]),
        ],
        node_size,
        flink=first_map_node_num,
        blink=0,
    )

    map_nodes: List[bytes] = []
    for i in range(map_nodes_count):
        this_num = first_map_node_num + i
        next_map = (this_num + 1) if (i + 1 < map_nodes_count) else 0
        start = header_map_bytes + i * map_node_bytes
        chunk = bytes(node_bitmap[start : start + map_node_bytes])
        map_nodes.append(
            _pack_btree_node(K_BT_MAP_NODE, 0, [chunk], node_size, flink=next_map, blink=0)
        )

    unused_count = total_nodes - used_nodes
    out = bytearray()
    out.extend(header_node)
    for n in internal_nodes:
        out.extend(n)
    for mn in map_nodes:
        out.extend(mn)
    out.extend(b"\x00" * (unused_count * node_size))
    assert len(out) == total_nodes * node_size
    return bytes(out)


def _encode_chs_max() -> bytes:
    # Standard LBA-only CHS tuple (1023, 254, 63) -> (0xFE, 0xFF, 0xFF)
    return bytes((0xFE, 0xFF, 0xFF))


def build_mbr_sector(part_start_lba: int, part_sectors: int, bootable: bool = True) -> bytes:
    """Build a 512-byte MBR sector with partition 1 = 0xAF ('Apple_HFS').

    IOFDiskPartitionScheme::isPartitionCorrupt (IOFDiskPartitionScheme.cpp:415)
    requires (bootid & 0x7F) == 0, and instantiateMediaObject (:483-501) maps
    '0xAF' -> 'Apple_HFS' via IOStorageFamily/Info.plist:139-140.
    """
    if part_start_lba <= 0 or part_sectors <= 0:
        raise HFSFormatError("invalid MBR partition geometry")
    if part_start_lba + part_sectors > 0xFFFFFFFF:
        raise HFSFormatError("MBR partition exceeds 32-bit LBA limit")
    mbr = bytearray(SECTOR_SIZE)
    boot_id = 0x80 if bootable else 0x00
    chs = _encode_chs_max()
    entry = struct.pack(
        "<B3sB3sII",
        boot_id,
        chs,
        MBR_TYPE_APPLE_HFS,
        chs,
        part_start_lba,
        part_sectors,
    )
    assert len(entry) == 16
    mbr[446 : 446 + 16] = entry
    struct.pack_into("<H", mbr, 510, MBR_SIGNATURE)
    return bytes(mbr)


def build_hfs_volume(
    root_dir: Optional[Path] = None,
    *,
    volume_name: str = "ravynOS",
    size_bytes: Optional[int] = None,
    free_headroom_bytes: int = 16 * 1024 * 1024,
    block_size: int = DEFAULT_BLOCK_SIZE,
    case_sensitive: bool = True,
    uid: int = 0,
    gid: int = 0,
    timestamp: int = DEFAULT_HFS_TIMESTAMP,
) -> bytes:
    """Build a standalone HFSX (default) or HFS+ volume byte image."""
    if block_size < SECTOR_SIZE or (block_size & (block_size - 1)) != 0:
        raise HFSFormatError(f"invalid HFS+ block_size: {block_size}")
    if CATALOG_NODE_SIZE % block_size != 0:
        raise HFSFormatError("block_size must divide 8192-byte catalog node size")

    root_entry, all_entries = _collect_staging_tree(root_dir, uid, gid, volume_name)

    # Compute data blocks needed for all regular files and symlinks
    file_entries = [e for e in all_entries if not e.is_dir]
    dir_entries = [e for e in all_entries if e.is_dir]
    data_blocks_total = 0
    for fe in file_entries:
        fe.block_count = (len(fe.data) + block_size - 1) // block_size
        data_blocks_total += fe.block_count

    # Estimate catalog B-tree size by doing a preliminary build with placeholder start_block=0
    def make_catalog_items() -> List[Tuple[Tuple[int, Tuple[int, ...]], bytes, bytes]]:
        keyed: List[Tuple[Tuple[int, Tuple[int, ...]], bytes, bytes]] = []
        seen_keys: Dict[Tuple[int, Tuple[int, ...]], str] = {}
        for e in all_entries:
            units = encode_hfs_unistr255(e.name)
            sk = catalog_sort_key(e.parent_cnid, units, case_sensitive)
            if sk in seen_keys:
                raise HFSFormatError(
                    f"duplicate catalog key {e.rel_path!r} vs {seen_keys[sk]!r}"
                )
            seen_keys[sk] = e.rel_path or "/"
            rec_key = pack_catalog_key(e.parent_cnid, units)
            if e.is_dir:
                folder_cnt = sum(1 for c in e.children if c.is_dir)
                rec_val = pack_catalog_folder(
                    folder_id=e.cnid,
                    valence=len(e.children),
                    folder_count=folder_cnt,
                    timestamp=timestamp,
                    uid=e.uid,
                    gid=e.gid,
                    mode=e.mode,
                    case_sensitive=case_sensitive,
                )
            else:
                rec_val = pack_catalog_file(
                    file_id=e.cnid,
                    timestamp=timestamp,
                    uid=e.uid,
                    gid=e.gid,
                    mode=e.mode,
                    is_symlink=e.is_symlink,
                    logical_size=len(e.data),
                    start_block=e.start_block,
                    block_count=e.block_count,
                    block_size=block_size,
                )
            keyed.append((sk, rec_key, rec_val))

            # Thread record: key = (e.cnid, ())
            tsk = catalog_sort_key(e.cnid, (), case_sensitive)
            t_key = pack_catalog_key(e.cnid, ())
            t_val = pack_catalog_thread(e.is_dir, e.parent_cnid, units)
            keyed.append((tsk, t_key, t_val))

        keyed.sort(key=lambda item: item[0])
        return keyed

    prelim_items = [(k, v) for _, k, v in make_catalog_items()]
    prelim_cat = _build_btree(
        prelim_items,
        node_size=CATALOG_NODE_SIZE,
        max_key_length=K_HFSPLUS_CATALOG_KEY_MAX_LEN,
        key_compare_type=K_HFS_BINARY_COMPARE if case_sensitive else K_HFS_CASE_FOLDING,
        attributes=K_BT_BIG_KEYS_MASK | K_BT_VARIABLE_INDEX_KEYS_MASK,
        clump_size=max(CATALOG_NODE_SIZE * 8, block_size * 4),
        min_total_nodes=8,
    )
    catalog_blocks = len(prelim_cat) // block_size
    extents_blocks = max(8, (EXTENTS_NODE_SIZE * 8) // block_size)
    attr_blocks = max(8, (ATTR_NODE_SIZE * 4) // block_size)

    headroom_blocks = (free_headroom_bytes + block_size - 1) // block_size
    # Iterate once to converge bitmap_blocks with total_blocks
    if size_bytes is not None:
        if size_bytes % block_size != 0:
            size_bytes = ((size_bytes + block_size - 1) // block_size) * block_size
        total_blocks = size_bytes // block_size
    else:
        base_blocks = (
            2  # block 0 (VH) + last block (AltVH)
            + extents_blocks
            + attr_blocks
            + catalog_blocks
            + data_blocks_total
            + max(headroom_blocks, 16)
        )
        bits_per_block = block_size * 8
        bitmap_blocks = (base_blocks + bits_per_block - 1) // bits_per_block
        total_blocks = base_blocks + bitmap_blocks

    bits_per_block = block_size * 8
    bitmap_blocks = (total_blocks + bits_per_block - 1) // bits_per_block

    # Assign contiguous block extents in canonical order:
    #   block 0: VolumeHeader
    #   [alloc_start ..): allocationFile
    #   [ext_start ..): extentsFile
    #   [attr_start ..): attributesFile
    #   [cat_start ..): catalogFile
    #   [data_start ..): file & symlink data forks
    #   block total_blocks - 1: Alternate VolumeHeader
    next_block = 1
    alloc_start = next_block
    next_block += bitmap_blocks
    ext_start = next_block
    next_block += extents_blocks
    attr_start = next_block
    next_block += attr_blocks
    cat_start = next_block
    next_block += catalog_blocks

    for fe in file_entries:
        if fe.block_count > 0:
            fe.start_block = next_block
            next_block += fe.block_count
        else:
            fe.start_block = 0

    # Ensure at least 1 block for AltVH at total_blocks - 1
    if next_block >= total_blocks:
        if size_bytes is not None:
            raise HFSFormatError(
                f"staged content requires {(next_block + 1) * block_size} bytes, "
                f"exceeding fixed size {size_bytes}"
            )
        total_blocks = next_block + max(headroom_blocks, 16) + 1
        new_bmb = (total_blocks + bits_per_block - 1) // bits_per_block
        assert new_bmb == bitmap_blocks

    # Build final B-trees now that fe.start_block values are finalized
    final_items = [(k, v) for _, k, v in make_catalog_items()]
    catalog_bytes = _build_btree(
        final_items,
        node_size=CATALOG_NODE_SIZE,
        max_key_length=K_HFSPLUS_CATALOG_KEY_MAX_LEN,
        key_compare_type=K_HFS_BINARY_COMPARE if case_sensitive else K_HFS_CASE_FOLDING,
        attributes=K_BT_BIG_KEYS_MASK | K_BT_VARIABLE_INDEX_KEYS_MASK,
        clump_size=catalog_blocks * block_size,
        min_total_nodes=len(prelim_cat) // CATALOG_NODE_SIZE,
    )
    assert len(catalog_bytes) == catalog_blocks * block_size

    extents_bytes = _build_btree(
        [],
        node_size=EXTENTS_NODE_SIZE,
        max_key_length=K_HFSPLUS_EXTENT_KEY_MAX_LEN,
        key_compare_type=0,
        attributes=K_BT_BIG_KEYS_MASK,
        clump_size=extents_blocks * block_size,
        min_total_nodes=(extents_blocks * block_size) // EXTENTS_NODE_SIZE,
    )
    assert len(extents_bytes) == extents_blocks * block_size

    attr_bytes = _build_btree(
        [],
        node_size=ATTR_NODE_SIZE,
        max_key_length=K_HFSPLUS_ATTR_KEY_MAX_LEN,
        key_compare_type=0,
        attributes=K_BT_BIG_KEYS_MASK | K_BT_VARIABLE_INDEX_KEYS_MASK,
        clump_size=attr_blocks * block_size,
        min_total_nodes=(attr_blocks * block_size) // ATTR_NODE_SIZE,
    )
    assert len(attr_bytes) == attr_blocks * block_size

    # Allocation bitmap: blocks [0 .. next_block - 1] + [total_blocks - 1] in use
    bitmap_bytes = bytearray(bitmap_blocks * block_size)
    _mark_bitmap_bits(bitmap_bytes, 0, next_block)
    _mark_bitmap_bits(bitmap_bytes, total_blocks - 1, 1)

    used_blocks = next_block + 1  # includes AltVH block at total_blocks - 1
    free_blocks = total_blocks - used_blocks
    next_cnid = max((e.cnid for e in all_entries), default=2) + 1
    if next_cnid < K_HFS_FIRST_USER_CNID:
        next_cnid = K_HFS_FIRST_USER_CNID

    sig = K_HFSX_SIGWORD if case_sensitive else K_HFSPLUS_SIGWORD
    ver = K_HFSX_VERSION if case_sensitive else K_HFSPLUS_VERSION
    vh_attrs = K_HFS_VOLUME_UNMOUNTED_MASK | K_HFS_UNUSED_NODE_FIX_MASK

    alloc_fork = pack_fork_data(
        bitmap_blocks * block_size,
        bitmap_blocks * block_size,
        bitmap_blocks,
        [(alloc_start, bitmap_blocks)],
    )
    ext_fork = pack_fork_data(
        extents_blocks * block_size,
        extents_blocks * block_size,
        extents_blocks,
        [(ext_start, extents_blocks)],
    )
    cat_fork = pack_fork_data(
        catalog_blocks * block_size,
        catalog_blocks * block_size,
        catalog_blocks,
        [(cat_start, catalog_blocks)],
    )
    attr_fork = pack_fork_data(
        attr_blocks * block_size,
        attr_blocks * block_size,
        attr_blocks,
        [(attr_start, attr_blocks)],
    )
    startup_fork = pack_fork_data(0, 0, 0, ())

    # Deterministic non-zero volume UUID in finderInfo[24..31] (makehfs.c:776)
    finder_info = bytearray(32)
    finder_info[24:32] = b"RAVYNOS1"

    vh = struct.pack(
        ">HH17IQ32s80s80s80s80s80s",
        sig,
        ver,
        vh_attrs,
        K_HFSPLUS_MOUNT_VERSION,
        0,  # journalInfoBlock (clean unmounted non-journaled volume)
        timestamp,  # createDate
        timestamp,  # modifyDate
        0,  # backupDate
        timestamp,  # checkedDate
        len(file_entries),  # fileCount
        len(dir_entries) - 1,  # folderCount (excludes root folder per makehfs.c:700)
        block_size,
        total_blocks,
        free_blocks,
        next_block,  # nextAllocation
        block_size * 4,  # rsrcClumpSize
        block_size * 4,  # dataClumpSize
        next_cnid,
        1,  # writeCount
        1,  # encodingsBitmap
        bytes(finder_info),
        alloc_fork,
        ext_fork,
        cat_fork,
        attr_fork,
        startup_fork,
    )
    assert len(vh) == 512

    vol = bytearray(total_blocks * block_size)
    # Primary VH at byte offset 1024 (sector 2)
    vol[1024 : 1024 + 512] = vh
    vol[alloc_start * block_size : (alloc_start + bitmap_blocks) * block_size] = bitmap_bytes
    vol[ext_start * block_size : (ext_start + extents_blocks) * block_size] = extents_bytes
    vol[attr_start * block_size : (attr_start + attr_blocks) * block_size] = attr_bytes
    vol[cat_start * block_size : (cat_start + catalog_blocks) * block_size] = catalog_bytes

    for fe in file_entries:
        if fe.block_count > 0:
            off = fe.start_block * block_size
            vol[off : off + len(fe.data)] = fe.data

    # Alternate VH at 1024 bytes before end of volume (totalSectors - 2)
    alt_off = len(vol) - 1024
    vol[alt_off : alt_off + 512] = vh
    return bytes(vol)


def write_hfs_image(
    output_path: Path,
    root_dir: Optional[Path] = None,
    *,
    partition: str = "mbr",
    volume_name: str = "ravynOS",
    size_bytes: Optional[int] = None,
    free_headroom_bytes: int = 16 * 1024 * 1024,
    block_size: int = DEFAULT_BLOCK_SIZE,
    case_sensitive: bool = True,
    uid: int = 0,
    gid: int = 0,
    timestamp: int = DEFAULT_HFS_TIMESTAMP,
) -> Dict[str, int]:
    """Write a complete MBR-partitioned (0xAF Apple_HFS) or bare HFS+/HFSX image."""
    if partition not in ("mbr", "none"):
        raise HFSFormatError(f"unsupported partition mode: {partition!r}")

    vol_size = size_bytes
    if vol_size is not None and partition == "mbr":
        overhead = MBR_PART_START_LBA * SECTOR_SIZE
        if vol_size <= overhead + block_size * 16:
            raise HFSFormatError("image size too small for MBR + HFS+ volume")
        vol_size = vol_size - overhead

    vol_bytes = build_hfs_volume(
        root_dir,
        volume_name=volume_name,
        size_bytes=vol_size,
        free_headroom_bytes=free_headroom_bytes,
        block_size=block_size,
        case_sensitive=case_sensitive,
        uid=uid,
        gid=gid,
        timestamp=timestamp,
    )
    assert len(vol_bytes) % SECTOR_SIZE == 0
    part_sectors = len(vol_bytes) // SECTOR_SIZE

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "wb") as f:
        if partition == "mbr":
            mbr = build_mbr_sector(MBR_PART_START_LBA, part_sectors, bootable=True)
            f.write(mbr)
            f.write(b"\x00" * ((MBR_PART_START_LBA - 1) * SECTOR_SIZE))
            f.write(vol_bytes)
        else:
            f.write(vol_bytes)

    total_bytes = output_path.stat().st_size
    return {
        "image_bytes": total_bytes,
        "volume_bytes": len(vol_bytes),
        "part_start_lba": MBR_PART_START_LBA if partition == "mbr" else 0,
        "part_sectors": part_sectors,
    }


@dataclass
class VerifiedEntry:
    path: str
    cnid: int
    parent_cnid: int
    is_dir: bool
    is_symlink: bool
    mode: int
    uid: int
    gid: int
    size: int
    data: bytes


def verify_hfs_image(image_path: Path) -> Dict[str, object]:
    """Independently parse and validate an MBR or bare HFS+/HFSX image."""
    raw = image_path.read_bytes()
    if len(raw) < 16 * SECTOR_SIZE or len(raw) % SECTOR_SIZE != 0:
        raise HFSFormatError("image size is too small or not a multiple of 512B")

    # Detect MBR vs bare HFS+/HFSX
    part_offset = 0
    part_size = len(raw)
    has_mbr = False
    vh_probe_sig = struct.unpack_from(">H", raw, 1024)[0]
    if vh_probe_sig not in (K_HFSPLUS_SIGWORD, K_HFSX_SIGWORD):
        mbr_sig = struct.unpack_from("<H", raw, 510)[0]
        if mbr_sig != MBR_SIGNATURE:
            raise HFSFormatError("neither bare HFS+/HFSX nor valid 0xAA55 MBR")
        boot_id, _, systid, _, relsect, numsect = struct.unpack_from("<B3sB3sII", raw, 446)
        if boot_id & 0x7F:
            raise HFSFormatError(f"corrupt MBR bootid: 0x{boot_id:02x}")
        if systid != MBR_TYPE_APPLE_HFS:
            raise HFSFormatError(f"expected MBR partition type 0xAF, got 0x{systid:02x}")
        if relsect == 0 or numsect == 0:
            raise HFSFormatError("empty MBR partition")
        part_offset = relsect * SECTOR_SIZE
        part_size = numsect * SECTOR_SIZE
        if part_offset + part_size > len(raw):
            raise HFSFormatError("MBR partition extends past end of image")
        has_mbr = True

    vol = memoryview(raw)[part_offset : part_offset + part_size]
    vh_bytes = bytes(vol[1024 : 1024 + 512])
    alt_vh_bytes = bytes(vol[len(vol) - 1024 : len(vol) - 512])
    if vh_bytes != alt_vh_bytes:
        raise HFSFormatError("primary and alternate HFSPlusVolumeHeader differ")

    unpacked = struct.unpack(
        ">HH17IQ32s80s80s80s80s80s", vh_bytes
    )
    (
        sig,
        ver,
        vh_attrs,
        _last_mnt,
        jib,
        _cr,
        _md,
        _bk,
        _ck,
        file_count,
        folder_count,
        block_size,
        total_blocks,
        free_blocks,
        _next_alloc,
        _rsrc_clump,
        _data_clump,
        next_cnid,
        _wr_cnt,
        _enc_bmp,
        _fndr,
        alloc_fork_b,
        ext_fork_b,
        cat_fork_b,
        attr_fork_b,
        _startup_fork_b,
    ) = unpacked

    if sig == K_HFSX_SIGWORD:
        if ver != K_HFSX_VERSION:
            raise HFSFormatError(f"invalid HFSX version {ver}")
        case_sensitive = True
    elif sig == K_HFSPLUS_SIGWORD:
        if ver != K_HFSPLUS_VERSION:
            raise HFSFormatError(f"invalid HFS+ version {ver}")
        case_sensitive = False
    else:
        raise HFSFormatError(f"unknown HFS signature 0x{sig:04x}")

    if jib == 0 and (vh_attrs & K_HFS_VOLUME_UNMOUNTED_MASK) == 0:
        raise HFSFormatError("non-journaled volume lacks kHFSVolumeUnmountedMask")
    if block_size < 512 or (block_size & (block_size - 1)) != 0:
        raise HFSFormatError(f"invalid blockSize {block_size}")
    if total_blocks * block_size != len(vol):
        raise HFSFormatError("totalBlocks * blockSize != partition byte size")

    def read_fork(fork_b: bytes) -> bytes:
        lsize, _clump, tblks = struct.unpack_from(">QII", fork_b, 0)
        out = bytearray()
        for i in range(8):
            sb, bc = struct.unpack_from(">II", fork_b, 16 + i * 8)
            if bc:
                if sb + bc > total_blocks:
                    raise HFSFormatError("fork extent out of volume bounds")
                out.extend(vol[sb * block_size : (sb + bc) * block_size])
        if len(out) != tblks * block_size or lsize > len(out):
            raise HFSFormatError("fork block count mismatch")
        return bytes(out[:lsize])

    alloc_bmp = read_fork(alloc_fork_b)
    # Check VH (block 0) and AltVH (total_blocks - 1) marked allocated
    for must_blk in (0, total_blocks - 1):
        if not (alloc_bmp[must_blk >> 3] & (1 << (7 - (must_blk & 7)))):
            raise HFSFormatError(f"reserved block {must_blk} not marked in bitmap")

    used_bits = 0
    for b in range(total_blocks):
        if alloc_bmp[b >> 3] & (1 << (7 - (b & 7))):
            used_bits += 1
    if total_blocks - used_bits != free_blocks:
        raise HFSFormatError(
            f"freeBlocks ({free_blocks}) != bitmap free ({total_blocks - used_bits})"
        )

    cat_data = read_fork(cat_fork_b)
    # Parse catalog header node (node 0)
    h_flink, h_blink, h_kind, h_height, h_nrecs, _ = struct.unpack_from(
        ">IIbBHH", cat_data, 0
    )
    if h_kind != K_BT_HEADER_NODE or h_height != 0 or h_nrecs != 3:
        raise HFSFormatError("invalid catalog B-tree header node descriptor")

    (
        tree_depth,
        root_node,
        leaf_records,
        first_leaf,
        last_leaf,
        node_size,
        max_key_len,
        total_nodes,
        free_nodes,
        _r1,
        _clump,
        _bttype,
        key_cmp_type,
        bt_attrs,
        _r3,
    ) = struct.unpack_from(">HIIIIHHIIHIBBI64s", cat_data, 14)

    if len(cat_data) != total_nodes * node_size:
        raise HFSFormatError("catalog fork size != totalNodes * nodeSize")
    if case_sensitive and key_cmp_type != K_HFS_BINARY_COMPARE:
        raise HFSFormatError("HFSX catalog must use kHFSBinaryCompare")

    def parse_node_records(node_num: int) -> Tuple[int, int, int, int, List[bytes]]:
        if node_num < 0 or node_num >= total_nodes:
            raise HFSFormatError(f"node_num {node_num} out of range")
        base = node_num * node_size
        node = cat_data[base : base + node_size]
        fl, bl, knd, hgt, nrecs, _ = struct.unpack_from(">IIbBHH", node, 0)
        if node_num != 0 and (fl == node_num or bl == node_num):
            raise HFSFormatError(f"node {node_num} links to itself")
        offs: List[int] = []
        for idx in range(nrecs + 1):
            off = struct.unpack_from(">H", node, node_size - 2 * (idx + 1))[0]
            # hfs_endian.c:352-372 even & strictly increasing check
            if off & 1:
                raise HFSFormatError(f"odd record offset 0x{off:04x} in node {node_num}")
            if off < 14 or off > node_size - 2 * (nrecs + 1):
                raise HFSFormatError(f"out-of-bounds offset 0x{off:04x} in node {node_num}")
            if offs and off <= offs[-1]:
                raise HFSFormatError(f"non-increasing offsets in node {node_num}")
            offs.append(off)
        recs = [node[offs[i] : offs[i + 1]] for i in range(nrecs)]
        return fl, bl, knd, hgt, recs

    # Validate index tree from root_node down to first_leaf
    cur = root_node
    for expected_h in range(tree_depth, 1, -1):
        _, _, knd, hgt, irecs = parse_node_records(cur)
        if knd != K_BT_INDEX_NODE or hgt != expected_h or not irecs:
            raise HFSFormatError(f"bad index node {cur} at height {expected_h}")
        # Follow first child pointer
        klen = struct.unpack_from(">H", irecs[0], 0)[0]
        cur = struct.unpack_from(">I", irecs[0], 2 + klen)[0]
    if cur != first_leaf:
        raise HFSFormatError(f"leftmost index walk reached {cur}, expected {first_leaf}")

    # Walk all leaf nodes via fLink chain, verifying bLink, key order, and records
    leaf_num = first_leaf
    prev_leaf = 0
    seen_leaves = 0
    prev_sort_key: Optional[Tuple[int, Tuple[int, ...]]] = None
    raw_records: List[Tuple[int, Tuple[int, ...], bytes]] = []

    while leaf_num != 0:
        fl, bl, knd, hgt, lrecs = parse_node_records(leaf_num)
        if knd != K_BT_LEAF_NODE or hgt != 1 or bl != prev_leaf:
            raise HFSFormatError(f"corrupt leaf link/header on node {leaf_num}")
        for rec in lrecs:
            klen, parent_id, ulen = struct.unpack_from(">HIH", rec, 0)
            if klen != 6 + 2 * ulen:
                raise HFSFormatError("inconsistent catalog keyLength vs unicode length")
            units = struct.unpack_from(f">{ulen}H", rec, 8) if ulen else ()
            sk = catalog_sort_key(parent_id, units, case_sensitive)
            if prev_sort_key is not None and sk <= prev_sort_key:
                raise HFSFormatError(f"catalog keys not strictly sorted: {prev_sort_key} >= {sk}")
            prev_sort_key = sk
            raw_records.append((parent_id, units, rec[2 + klen :]))
        prev_leaf = leaf_num
        leaf_num = fl
        seen_leaves += 1
        if seen_leaves > total_nodes:
            raise HFSFormatError("cycle in leaf fLink chain")

    if prev_leaf != last_leaf:
        raise HFSFormatError(f"leaf chain ended at {prev_leaf}, expected {last_leaf}")
    if len(raw_records) != leaf_records:
        raise HFSFormatError("leafRecords header mismatch")

    folders: Dict[int, Tuple[int, str, int, int, int, int, int]] = {}
    files: List[Tuple[int, int, str, int, int, int, bool, bytes]] = []
    threads: Dict[int, Tuple[int, int, str]] = {}

    for parent_id, units, payload in raw_records:
        rtype = struct.unpack_from(">h", payload, 0)[0]
        name = decode_hfs_unistr255(units)
        if rtype == K_HFSPLUS_FOLDER_RECORD:
            (
                _rt,
                flags,
                valence,
                fid,
                _cr2,
                _cm,
                _am,
                _ac,
                _bk2,
                bsd_b,
                _ui,
                _fi,
                _te,
                fcount,
            ) = struct.unpack(">hHIIIIIII16s16s16sII", payload)
            uid_v, gid_v, _, _, fmode, _ = struct.unpack(">IIBBHI", bsd_b)
            folders[fid] = (parent_id, name, valence, fcount, uid_v, gid_v, fmode)
        elif rtype == K_HFSPLUS_FILE_RECORD:
            (
                _rt,
                flags,
                _res1,
                fid,
                _cr2,
                _cm,
                _am,
                _ac,
                _bk2,
                bsd_b,
                ui_b,
                _fi,
                _te,
                _res2,
                dfork_b,
                _rfork_b,
            ) = struct.unpack(">hHIIIIIII16s16s16sII80s80s", payload)
            uid_v, gid_v, _, _, fmode, _ = struct.unpack(">IIBBHI", bsd_b)
            is_lnk = (fmode & S_IFMT_MASK) == S_IFLNK_MODE
            if is_lnk:
                fd_type, fd_creator = struct.unpack_from(">II", ui_b, 0)
                if (fd_type, fd_creator) != (K_SYMLINK_FILE_TYPE, K_SYMLINK_CREATOR):
                    raise HFSFormatError("symlink missing slnk/rhap Finder type/creator")
            fdata = read_fork(dfork_b)
            files.append((fid, parent_id, name, uid_v, gid_v, fmode, is_lnk, fdata))
        elif rtype in (K_HFSPLUS_FOLDER_THREAD_RECORD, K_HFSPLUS_FILE_THREAD_RECORD):
            if len(units) != 0:
                raise HFSFormatError("thread record key must have empty nodeName")
            _rt, _res, t_parent, t_ulen = struct.unpack_from(">hhIH", payload, 0)
            t_units = struct.unpack_from(f">{t_ulen}H", payload, 10) if t_ulen else ()
            threads[parent_id] = (rtype, t_parent, decode_hfs_unistr255(t_units))
        else:
            raise HFSFormatError(f"unknown catalog recordType {rtype}")

    if K_HFS_ROOT_FOLDER_ID not in folders:
        raise HFSFormatError("missing root folder (CNID 2)")
    if len(folders) - 1 != folder_count:
        raise HFSFormatError("VolumeHeader folderCount mismatch")
    if len(files) != file_count:
        raise HFSFormatError("VolumeHeader fileCount mismatch")

    # Reconstruct full paths and verify thread records
    def build_path(cnid: int) -> str:
        if cnid == K_HFS_ROOT_FOLDER_ID:
            return "/"
        p_id, nm, *_ = folders[cnid]
        parent_p = build_path(p_id)
        return f"/{nm}" if parent_p == "/" else f"{parent_p}/{nm}"

    entries: Dict[str, VerifiedEntry] = {}
    for fid, (p_id, nm, val, fcnt, uid_v, gid_v, fmode) in folders.items():
        if fid not in threads or threads[fid] != (K_HFSPLUS_FOLDER_THREAD_RECORD, p_id, nm):
            raise HFSFormatError(f"missing/mismatched folder thread for CNID {fid}")
        if fid >= next_cnid:
            raise HFSFormatError(f"folder CNID {fid} >= nextCatalogID {next_cnid}")
        p = build_path(fid)
        entries[p] = VerifiedEntry(
            path=p,
            cnid=fid,
            parent_cnid=p_id,
            is_dir=True,
            is_symlink=False,
            mode=fmode,
            uid=uid_v,
            gid=gid_v,
            size=0,
            data=b"",
        )

    for fid, p_id, nm, uid_v, gid_v, fmode, is_lnk, fdata in files:
        if fid not in threads or threads[fid] != (K_HFSPLUS_FILE_THREAD_RECORD, p_id, nm):
            raise HFSFormatError(f"missing/mismatched file thread for CNID {fid}")
        if fid >= next_cnid:
            raise HFSFormatError(f"file CNID {fid} >= nextCatalogID {next_cnid}")
        parent_p = build_path(p_id)
        p = f"/{nm}" if parent_p == "/" else f"{parent_p}/{nm}"
        entries[p] = VerifiedEntry(
            path=p,
            cnid=fid,
            parent_cnid=p_id,
            is_dir=False,
            is_symlink=is_lnk,
            mode=fmode,
            uid=uid_v,
            gid=gid_v,
            size=len(fdata),
            data=fdata,
        )

    return {
        "has_mbr": has_mbr,
        "case_sensitive": case_sensitive,
        "block_size": block_size,
        "total_blocks": total_blocks,
        "free_blocks": free_blocks,
        "tree_depth": tree_depth,
        "file_count": file_count,
        "folder_count": folder_count,
        "entries": entries,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build or verify a portable HFS+/HFSX image (default MBR 0xAF Apple_HFS)."
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_build = sub.add_parser("build", help="Create an HFS+/HFSX image from a staging directory")
    p_build.add_argument("-o", "--output", type=Path, required=True, help="Output image path")
    p_build.add_argument("--root", type=Path, default=None, help="Staging root directory")
    p_build.add_argument("--label", default="ravynOS", help="HFS+ volume name")
    p_build.add_argument(
        "--partition",
        choices=("mbr", "none"),
        default="mbr",
        help="Partition wrapper: 'mbr' (type 0xAF -> Apple_HFS) or 'none'",
    )
    p_build.add_argument("--size", type=int, default=None, help="Total image size in bytes")
    p_build.add_argument(
        "--free-mb",
        type=int,
        default=16,
        help="Minimum free headroom in MiB when auto-sizing (default: 16)",
    )
    p_build.add_argument(
        "--hfsplus",
        action="store_true",
        help="Emit case-insensitive HFS+ ('H+' v4) instead of case-sensitive HFSX ('HX' v5)",
    )

    p_verify = sub.add_parser("verify", help="Verify an HFS+/HFSX image and print summary")
    p_verify.add_argument("image", type=Path, help="Path to HFS+/HFSX image")

    args = parser.parse_args(argv)
    if args.cmd == "build":
        info = write_hfs_image(
            args.output,
            args.root,
            partition=args.partition,
            volume_name=args.label,
            size_bytes=args.size,
            free_headroom_bytes=args.free_mb * 1024 * 1024,
            case_sensitive=not args.hfsplus,
        )
        print(
            f"hfs: wrote {args.output} ({info['image_bytes']} bytes, "
            f"partition={args.partition}, sectors={info['part_sectors']})"
        )
        return 0

    info = verify_hfs_image(args.image)
    entries: Dict[str, VerifiedEntry] = info["entries"]  # type: ignore[assignment]
    print(
        f"VERIFY OK: hfs({'HFSX' if info['case_sensitive'] else 'HFS+'}, "
        f"mbr={info['has_mbr']}, depth={info['tree_depth']}, "
        f"folders={info['folder_count']}, files={info['file_count']}, "
        f"entries={len(entries)})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
