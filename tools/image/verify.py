#!/usr/bin/env python3
"""Independent structural verifier for the portable image pipeline.

``tools/bootcheck.sh`` answers "does it boot?" -- which needs QEMU and takes
tens of seconds.  This verifier answers "is the artifact a well-formed
bootable image at all?" in milliseconds and with no external tools, so CI can
gate on it before (and independently of) the QEMU run, and so a failure can be
attributed to image assembly rather than to the guest.

It parses the ISO 9660 / El Torito and GPT / FAT16 structures from scratch --
deliberately *not* by importing the writers in this directory, so a bug in a
writer cannot hide behind the same bug in its verifier.

Usage
-----
    verify.py iso <image.iso> [--json]
    verify.py img <image.img> [--json]
    verify.py auto <image> [--json]

Exit status: 0 = valid, 1 = invalid (problems are printed to stdout),
2 = usage error.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib

SECTOR = 2048
FAT_SECTOR = 512
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
ESP_TYPE_GUID_LE = bytes.fromhex("28732ac11ff8d211ba4b00a0c93ec93b")


class VerifierError(Exception):
    pass


def fail(msg: str) -> None:
    raise VerifierError(msg)


def read_u32le(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def check_both32(buf: bytes, off: int, what: str) -> int:
    le = read_u32le(buf, off)
    be = struct.unpack_from(">I", buf, off + 4)[0]
    if le != be:
        fail(f"{what}: little-endian {le} != big-endian {be}")
    return le


def check_both16(buf: bytes, off: int, what: str) -> int:
    le = struct.unpack_from("<H", buf, off)[0]
    be = struct.unpack_from(">H", buf, off + 2)[0]
    if le != be:
        fail(f"{what}: little-endian {le} != big-endian {be}")
    return le


# --------------------------------------------------------------------------
# ISO 9660 / El Torito
# --------------------------------------------------------------------------
def parse_dir_record(data: bytes, pos: int, end: int) -> dict:
    if pos + 33 > end:
        fail(f"directory record at offset {pos} is truncated")
    length = data[pos]
    if length < 33:
        fail(f"directory record at offset {pos} has length {length}")
    if pos + length > end:
        fail(f"directory record at offset {pos} runs past its extent")
    rec = data[pos:pos + length]
    extent = check_both32(rec, 2, "directory record extent")
    size = check_both32(rec, 10, "directory record size")
    flags = rec[25]
    ident_len = rec[32]
    if 33 + ident_len > length:
        fail("directory record identifier overruns the record")
    ident = rec[33:33 + ident_len]
    if ident_len == 1 and ident[0] in (0, 1):
        name = "." if ident[0] == 0 else ".."
    else:
        if ident_len == 0:
            fail("directory record has an empty identifier")
        name = ident.decode("ascii", "replace")
        if name.upper().endswith(";1"):
            name = name[:-2]
    return {"name": name, "extent": extent, "size": size, "flags": flags,
            "length": length, "is_dir": bool(flags & 0x02)}


def parse_el_torito(image: bytes, catalog_lba: int, total_sectors: int) -> dict:
    off = catalog_lba * SECTOR
    if off + 64 > len(image):
        fail(f"El Torito catalog at LBA {catalog_lba} is outside the image")
    cat = image[off:off + SECTOR]
    if cat[0] != 0x01:
        fail(f"El Torito validation entry header id is {cat[0]:#x}, expected 0x01")
    if cat[30] != 0x55 or cat[31] != 0xAA:
        fail("El Torito validation entry keys are not 0x55AA")
    words = struct.unpack("<16H", cat[0:32])
    if sum(words) & 0xFFFF:
        fail("El Torito validation entry checksum is wrong")
    platform = cat[1]
    result = {"catalog_lba": catalog_lba, "platform": platform, "entries": []}

    def entry(off: int, label: str) -> dict | None:
        e = cat[off:off + 32]
        if not any(e):
            return None
        indicator = e[0]
        media = e[1]
        load_segment = struct.unpack_from("<H", e, 2)[0]
        load_count = struct.unpack_from("<H", e, 6)[0]
        lba = struct.unpack_from("<I", e, 8)[0]
        info = {"kind": label, "bootable": bool(indicator & 0x88),
                "media": media, "load_segment": load_segment,
                "load_sectors": load_count, "load_lba": lba}
        if indicator & 0x88:
            if media != 0:
                info["warning"] = f"non-zero emulation media {media}"
            if lba + max(1, (load_count * 512 + SECTOR - 1) // SECTOR) > total_sectors:
                fail(f"{label} boot entry points past the end of the image")
            if load_count == 0:
                fail(f"{label} boot entry loads zero sectors")
            if load_segment not in (0, 0x07C0) and label == "bios":
                info["warning"] = (f"unusual load segment {load_segment:#x} "
                                   "(0 and 0x7C0 are the portable choices)")
        return info

    default = entry(32, "bios")
    if default is not None:
        result["entries"].append(default)
    pos = 64
    while pos + 32 <= SECTOR:
        header = cat[pos:pos + 32]
        if not any(header):
            break
        if header[0] not in (0x90, 0x91):
            break
        count = struct.unpack_from("<H", header, 2)[0]
        platform_id = header[1]
        pos += 32
        for _ in range(count):
            e = entry(pos, "uefi" if platform_id == 0xEF else f"platform-{platform_id:#x}")
            if e is not None:
                result["entries"].append(e)
            pos += 32
        if header[0] == 0x91:
            break
    if not result["entries"]:
        fail("El Torito catalog has no boot entries")
    return result


def verify_iso(path: str) -> dict:
    with open(path, "rb") as fh:
        image = fh.read()
    if len(image) < 18 * SECTOR:
        fail("file is smaller than the minimum ISO volume descriptor set")
    if len(image) % SECTOR:
        fail(f"image size {len(image)} is not a multiple of {SECTOR}")

    pvd = image[16 * SECTOR:17 * SECTOR]
    if pvd[0] != 1:
        fail(f"volume descriptor at sector 16 has type {pvd[0]}, expected 1 (primary)")
    if pvd[1:6] != b"CD001":
        fail(f"volume descriptor identifier is {pvd[1:6]!r}, expected b'CD001'")
    if pvd[6] != 1:
        fail(f"volume descriptor version is {pvd[6]}, expected 1")
    total_sectors = check_both32(pvd, 80, "volume space size")
    if total_sectors != len(image) // SECTOR:
        fail(f"declared space size {total_sectors} sectors != file size "
             f"{len(image) // SECTOR} sectors")
    block_size = check_both16(pvd, 128, "logical block size")
    if block_size != SECTOR:
        fail(f"logical block size is {block_size}, expected {SECTOR}")
    path_table_size = check_both32(pvd, 132, "path table size")
    l_path = read_u32le(pvd, 140)
    m_path = struct.unpack_from(">I", pvd, 148)[0]
    if path_table_size == 0:
        fail("path table size is zero")
    for name, lba in (("L", l_path), ("M", m_path)):
        if lba + (path_table_size + SECTOR - 1) // SECTOR > total_sectors:
            fail(f"{name} path table at LBA {lba} runs past the end of the image")

    root = parse_dir_record(pvd, 156, 190)
    if not root["is_dir"]:
        fail("root directory record is not marked as a directory")
    if root["size"] % SECTOR:
        fail("root directory size is not a multiple of the logical block size")

    # Walk every descriptor to find the El Torito boot record (if any).
    catalog_lba = None
    lba = 16
    saw_terminator = False
    while lba < total_sectors:
        vd = image[lba * SECTOR:(lba + 1) * SECTOR]
        if vd[1:6] != b"CD001":
            fail(f"volume descriptor at LBA {lba} lost the CD001 identifier")
        if vd[0] == 255:
            saw_terminator = True
            break
        if vd[0] == 0:
            if vd[7:39].rstrip(b"\x00") != b"EL TORITO SPECIFICATION":
                fail("boot record descriptor has an unexpected system identifier")
            catalog_lba = read_u32le(vd, 71)
        lba += 1
    if not saw_terminator:
        fail("volume descriptor set has no terminator")

    files: list[dict] = []
    visited: set[int] = set()

    def walk_dir(rec: dict, prefix: str, depth: int) -> None:
        if depth > 32:
            fail("directory tree is deeper than 32 levels (loop?)")
        extent, size = rec["extent"], rec["size"]
        if extent + size // SECTOR > total_sectors:
            fail(f"directory {prefix or '/'} extends past the end of the image")
        data = image[extent * SECTOR: extent * SECTOR + size]
        if len(data) != size:
            fail(f"directory {prefix or '/'} is truncated")
        if extent in visited:
            # An empty directory can legitimately share its parent's extent in
            # some writers; anything else is a loop.
            if size > SECTOR:
                fail(f"directory {prefix or '/'} is linked into a loop")
        visited.add(extent)
        pos = 0
        while pos < size:
            length = data[pos]
            if length == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            child = parse_dir_record(data, pos, size)
            pos += length
            if child["name"] in (".", ".."):
                continue
            name = f"{prefix}/{child['name']}" if prefix else child["name"]
            child_extent, child_size = child["extent"], child["size"]
            if child_size and child_extent + (child_size + SECTOR - 1) // SECTOR > total_sectors:
                fail(f"{name}: extent runs past the end of the image")
            if child["is_dir"]:
                walk_dir(child, name, depth + 1)
            else:
                files.append({"path": name, "lba": child_extent,
                              "size": child_size})
                if child_size:
                    # The recording date and flags must at least be sane.
                    flags = child["flags"]
                    if flags & ~0x0F:
                        fail(f"{name}: reserved directory record flags are set")

    walk_dir(root, "", 0)
    result = {
        "format": "iso9660",
        "path": path,
        "volume_id": pvd[40:72].decode("ascii", "replace").rstrip(),
        "sectors": total_sectors,
        "bytes": len(image),
        "root_entries": len(files),
        "files": files,
        "boot": None,
        "problems": [],
    }
    if catalog_lba is not None:
        result["boot"] = parse_el_torito(image, catalog_lba, total_sectors)
    return result


# --------------------------------------------------------------------------
# GPT + FAT16
# --------------------------------------------------------------------------
def gpt_header_check(header: bytes, what: str) -> dict:
    if header[0:8] != b"EFI PART":
        fail(f"{what}: signature is {header[0:8]!r}, expected b'EFI PART'")
    revision = struct.unpack_from("<I", header, 8)[0]
    if revision != 0x00010000:
        fail(f"{what}: revision is {revision:#x}, expected 0x00010000")
    header_size = struct.unpack_from("<I", header, 12)[0]
    if not 92 <= header_size <= FAT_SECTOR:
        fail(f"{what}: header size {header_size} is out of range")
    stored_crc = struct.unpack_from("<I", header, 16)[0]
    body = bytearray(header[:header_size])
    body[16:20] = b"\x00\x00\x00\x00"
    if zlib.crc32(bytes(body)) & 0xFFFFFFFF != stored_crc:
        fail(f"{what}: header CRC32 mismatch")
    fields = {
        "current_lba": struct.unpack_from("<Q", header, 24)[0],
        "backup_lba": struct.unpack_from("<Q", header, 32)[0],
        "first_usable": struct.unpack_from("<Q", header, 40)[0],
        "last_usable": struct.unpack_from("<Q", header, 48)[0],
        "entries_lba": struct.unpack_from("<Q", header, 72)[0],
        "entry_count": struct.unpack_from("<I", header, 80)[0],
        "entry_size": struct.unpack_from("<I", header, 84)[0],
        "entries_crc": struct.unpack_from("<I", header, 88)[0],
    }
    if fields["entry_size"] != GPT_ENTRY_SIZE:
        fail(f"{what}: partition entry size {fields['entry_size']} != 128")
    if fields["entry_count"] not in (128,):
        fail(f"{what}: partition entry count {fields['entry_count']} != 128")
    if fields["first_usable"] < 2 + (fields["entry_count"] * fields["entry_size"]) // FAT_SECTOR:
        fail(f"{what}: first usable LBA overlaps the entry array")
    return fields


def verify_fat16(part: bytes, label: str) -> dict:
    bs = part[0:FAT_SECTOR]
    if bs[510:512] != b"\x55\xAA":
        fail(f"{label}: boot sector does not end with 0x55AA")
    bytes_per_sector = struct.unpack_from("<H", bs, 11)[0]
    if bytes_per_sector != FAT_SECTOR:
        fail(f"{label}: bytes per sector is {bytes_per_sector}, expected 512")
    spc = bs[13]
    if spc == 0 or spc & (spc - 1):
        fail(f"{label}: sectors per cluster {spc} is not a power of two")
    reserved = struct.unpack_from("<H", bs, 14)[0]
    nfats = bs[16]
    root_entries = struct.unpack_from("<H", bs, 17)[0]
    total16 = struct.unpack_from("<H", bs, 19)[0]
    total32 = struct.unpack_from("<I", bs, 32)[0]
    total = total16 or total32
    if total == 0:
        fail(f"{label}: total sector count is zero")
    if total * bytes_per_sector > len(part):
        fail(f"{label}: filesystem claims {total} sectors but the partition "
             f"holds {len(part) // bytes_per_sector}")
    fat_sectors = struct.unpack_from("<H", bs, 22)[0]
    if reserved == 0 or nfats == 0 or fat_sectors == 0:
        fail(f"{label}: reserved/FAT counts are zero")
    root_sectors = (root_entries * 32 + FAT_SECTOR - 1) // FAT_SECTOR
    data_start = reserved + nfats * fat_sectors + root_sectors
    clusters = (total - data_start) // spc
    if not 4085 <= clusters <= 65524:
        fail(f"{label}: {clusters} clusters is not a legal FAT16 count "
             "(4085..65524)")
    if spc * bytes_per_sector != 512 * spc:
        fail(f"{label}: cluster size mismatch")

    fat_off = reserved * bytes_per_sector
    fat = part[fat_off:fat_off + fat_sectors * bytes_per_sector]
    entries = list(struct.unpack(f"<{len(fat) // 2}H", fat))

    root_off = (reserved + nfats * fat_sectors) * bytes_per_sector
    root_data = part[root_off:root_off + root_sectors * bytes_per_sector]

    def cluster_offset(cluster: int) -> int:
        return (data_start + (cluster - 2) * spc) * bytes_per_sector

    def cluster_chain(first: int, size: int, what: str) -> list[int]:
        need = (size + spc * bytes_per_sector - 1) // (spc * bytes_per_sector)
        chain, cluster, seen = [], first, set()
        while len(chain) < need:
            if cluster in seen:
                fail(f"{what}: cluster chain loops at {cluster}")
            if not 2 <= cluster < len(entries):
                fail(f"{what}: cluster {cluster} is outside the FAT")
            seen.add(cluster)
            chain.append(cluster)
            nxt = entries[cluster]
            if nxt >= 0xFFF8:
                break
            if nxt == 0 or nxt == 0xFFF7:
                fail(f"{what}: cluster chain hits a free/bad cluster {nxt:#x}")
            cluster = nxt
        if len(chain) < need:
            fail(f"{what}: cluster chain has {len(chain)} clusters, needs {need}")
        return chain

    def read_clusters(chain: list[int], size: int) -> bytes:
        data = b"".join(part[cluster_offset(c):cluster_offset(c) + spc * bytes_per_sector]
                        for c in chain)
        return data[:size]

    files: list[dict] = []
    dirs_seen: set[int] = set()

    def parse_entries(data: bytes, path: str, depth: int,
                      follow_dirs: bool) -> None:
        if depth > 64:
            fail("directory tree is deeper than 64 levels (loop?)")
        lfn_parts: list[str] = []
        pos = 0
        while pos + 32 <= len(data):
            e = data[pos:pos + 32]
            pos += 32
            if e[0] == 0x00:
                break  # end of directory
            if e[0] == 0xE5:
                lfn_parts = []
                continue
            attr = e[11]
            if attr == 0x0F:
                chunk = e[1:11] + e[14:26] + e[28:32]
                text = chunk.decode("utf-16-le", "replace")
                text = text.split("\x00")[0]
                text = text.rstrip("\uffff")
                lfn_parts.insert(0, text)
                continue
            short = e[0:11].decode("ascii", "replace")
            base = short[0:8].rstrip()
            ext = short[8:11].rstrip()
            name = ".".join(x for x in (base, ext) if x)
            if lfn_parts:
                name = "".join(lfn_parts)
            lfn_parts = []
            if attr & 0x08:
                continue  # volume label
            cluster = struct.unpack_from("<H", e, 26)[0]
            size = struct.unpack_from("<I", e, 28)[0]
            if name in (".", ".."):
                continue
            full = f"{path}/{name}" if path else name
            if attr & 0x10:
                if not follow_dirs or cluster == 0:
                    continue
                if cluster in dirs_seen:
                    continue
                dirs_seen.add(cluster)
                chain = cluster_chain(cluster, spc * bytes_per_sector * 1, full)
                # A directory's true length is a whole number of clusters.
                chain = []
                c = cluster
                while True:
                    chain.append(c)
                    if c in chain[:-1]:
                        fail(f"{full}: directory cluster chain loops")
                    nxt = entries[c]
                    if nxt >= 0xFFF8:
                        break
                    if nxt == 0:
                        fail(f"{full}: directory chain hits a free cluster")
                    c = nxt
                    if len(chain) > clusters:
                        fail(f"{full}: directory chain is unbounded")
                raw = b"".join(part[cluster_offset(c2):cluster_offset(c2) + spc * bytes_per_sector]
                               for c2 in chain)
                parse_entries(raw, full, depth + 1, follow_dirs)
            else:
                if size == 0:
                    files.append({"path": full, "size": 0, "clusters": []})
                    continue
                chain = cluster_chain(cluster, size, full)
                files.append({"path": full, "size": size, "clusters": chain})
                read_clusters(chain, size)  # bounds-check the data as well

    parse_entries(root_data, "", 0, follow_dirs=True)
    return {
        "label": bs[43:54].decode("ascii", "replace").rstrip(),
        "cluster_count": clusters,
        "sectors_per_cluster": spc,
        "sectors_per_fat": fat_sectors,
        "files": files,
        "free_clusters": sum(1 for e in entries[2:clusters + 2] if e == 0),
    }


def verify_img(path: str) -> dict:
    with open(path, "rb") as fh:
        image = fh.read()
    if len(image) < 3 * FAT_SECTOR:
        fail("file is too small to hold a partition table")
    if len(image) % FAT_SECTOR:
        fail(f"image size {len(image)} is not a multiple of {FAT_SECTOR}")
    total_sectors = len(image) // FAT_SECTOR

    mbr = image[0:FAT_SECTOR]
    if mbr[510:512] != b"\x55\xAA":
        fail("MBR does not end with the 0x55AA signature")
    protective = None
    for i in range(4):
        e = mbr[446 + i * 16: 462 + i * 16]
        if e[4] == 0xEE:
            protective = {
                "start_lba": struct.unpack_from("<I", e, 8)[0],
                "sectors": struct.unpack_from("<I", e, 12)[0],
            }
            break
    if protective is None:
        fail("MBR has no 0xEE protective partition (not a GPT disk)")
    if protective["start_lba"] != 1:
        fail("protective partition does not start at LBA 1")
    if protective["sectors"] not in (total_sectors - 1, 0xFFFFFFFF):
        fail(f"protective partition covers {protective['sectors']} sectors, "
             f"disk has {total_sectors - 1}")

    primary = gpt_header_check(image[FAT_SECTOR:2 * FAT_SECTOR], "primary GPT header")
    if primary["current_lba"] != 1:
        fail("primary GPT header is not at LBA 1")
    if primary["backup_lba"] != total_sectors - 1:
        fail("primary GPT header points at the wrong backup LBA")
    entries_off = primary["entries_lba"] * FAT_SECTOR
    entries = image[entries_off:entries_off + GPT_ENTRY_COUNT * GPT_ENTRY_SIZE]
    if len(entries) != GPT_ENTRY_COUNT * GPT_ENTRY_SIZE:
        fail("partition entry array is truncated")
    if zlib.crc32(entries) & 0xFFFFFFFF != primary["entries_crc"]:
        fail("partition entry array CRC32 mismatch")

    backup = gpt_header_check(image[(total_sectors - 1) * FAT_SECTOR:total_sectors * FAT_SECTOR],
                              "backup GPT header")
    if backup["current_lba"] != total_sectors - 1:
        fail("backup GPT header is not at the last LBA")
    if backup["backup_lba"] != 1:
        fail("backup GPT header does not point back at LBA 1")
    if backup["entries_crc"] != primary["entries_crc"]:
        fail("backup GPT entry array CRC differs from the primary")
    backup_off = backup["entries_lba"] * FAT_SECTOR
    backup_entries = image[backup_off:backup_off + GPT_ENTRY_COUNT * GPT_ENTRY_SIZE]
    if zlib.crc32(backup_entries) & 0xFFFFFFFF != backup["entries_crc"]:
        fail("backup partition entry array CRC32 mismatch")
    if backup_entries != entries:
        fail("backup partition entry array differs from the primary")

    partitions = []
    for i in range(GPT_ENTRY_COUNT):
        e = entries[i * GPT_ENTRY_SIZE:(i + 1) * GPT_ENTRY_SIZE]
        if not any(e[0:16]):
            continue
        start = struct.unpack_from("<Q", e, 32)[0]
        end = struct.unpack_from("<Q", e, 40)[0]
        if start < primary["first_usable"] or end > primary["last_usable"]:
            fail(f"partition {i} [{start}, {end}] lies outside the usable range "
                 f"[{primary['first_usable']}, {primary['last_usable']}]")
        if end < start:
            fail(f"partition {i} ends before it starts")
        name = e[56:128].decode("utf-16-le", "replace").split("\x00")[0]
        partitions.append({
            "index": i,
            "type_guid": e[0:16].hex(),
            "is_esp": e[0:16] == ESP_TYPE_GUID_LE,
            "start_lba": start,
            "end_lba": end,
            "sectors": end - start + 1,
            "name": name,
        })
    if not partitions:
        fail("GPT has no partitions")
    esp = next((p for p in partitions if p["is_esp"]), None)
    if esp is None:
        fail("GPT has no EFI System Partition")

    part = image[esp["start_lba"] * FAT_SECTOR:(esp["end_lba"] + 1) * FAT_SECTOR]
    fat = verify_fat16(part, "ESP")
    fat["files"].sort(key=lambda f: f["path"])
    for f in fat["files"]:
        f.pop("clusters", None)
    return {
        "format": "gpt-fat16",
        "path": path,
        "sectors": total_sectors,
        "bytes": len(image),
        "partitions": partitions,
        "esp": fat,
        "problems": [],
    }


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def summarise(info: dict) -> None:
    if info["format"] == "iso9660":
        print(f"iso: {info['path']}")
        print(f"  volume id     {info['volume_id']}")
        print(f"  size          {info['bytes']} bytes ({info['sectors']} sectors)")
        print(f"  files         {info['root_entries']}")
        for f in info["files"][:20]:
            print(f"    {f['size']:>10}  {f['path']}")
        if len(info["files"]) > 20:
            print(f"    ... {len(info['files']) - 20} more")
        boot = info["boot"]
        if boot is None:
            print("  boot          none (not bootable)")
        else:
            print(f"  boot catalog  LBA {boot['catalog_lba']} "
                  f"(platform {boot['platform']})")
            for e in boot["entries"]:
                print(f"    {e['kind']:<5} lba={e['load_lba']:<8} "
                      f"sectors={e['load_sectors']:<5} "
                      f"bootable={'yes' if e['bootable'] else 'no'}")
                if "warning" in e:
                    print(f"          warning: {e['warning']}")
    else:
        print(f"img: {info['path']}")
        print(f"  size          {info['bytes']} bytes ({info['sectors']} sectors)")
        for p in info["partitions"]:
            print(f"  partition {p['index']}   {p['sectors']} sectors at LBA "
                  f"{p['start_lba']}  {'ESP' if p['is_esp'] else p['type_guid']}"
                  f"  {p['name']!r}")
        esp = info["esp"]
        print(f"  FAT16         label {esp['label']!r}, {esp['cluster_count']} "
              f"clusters of {esp['sectors_per_cluster'] * FAT_SECTOR} B, "
              f"{esp['free_clusters']} free")
        print(f"  files         {len(esp['files'])}")
        for f in esp["files"][:20]:
            print(f"    {f['size']:>10}  /{f['path']}")
        if len(esp["files"]) > 20:
            print(f"    ... {len(esp['files']) - 20} more")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Verify pipeline image structure")
    ap.add_argument("kind", choices=("iso", "img", "auto"))
    ap.add_argument("image")
    ap.add_argument("--json", action="store_true", help="emit the report as JSON")
    args = ap.parse_args(argv)

    kind = args.kind
    if kind == "auto":
        kind = "img" if args.image.lower().endswith(".img") else "iso"
    try:
        info = verify_iso(args.image) if kind == "iso" else verify_img(args.image)
    except (VerifierError, OSError, struct.error) as exc:
        if args.json:
            print(json.dumps({"path": args.image, "valid": False,
                              "error": str(exc)}, indent=2))
        else:
            print(f"VERIFY FAIL: {exc}")
        return 1
    if args.json:
        print(json.dumps({"valid": True, **info}, indent=2))
    else:
        summarise(info)
        print("VERIFY OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
