#!/usr/bin/env python3
"""Unit tests for the dependency-free image backends.

Run directly (``python3 tools/tests/test_images.py``) or via
``tools/tests/run-tests.sh``.  Only the standard library is used, so these run
on any host with Python 3 -- including a bare CI container.

The tests deliberately exercise the *boundaries* of the writers: empty trees,
zero-length and multi-cluster files, long names, name collisions, deep nesting,
sector-boundary directory packing, error paths (symlinks, boot images outside
the tree), and reproducibility.  Every artefact is cross-checked with
``tools/image/verify.py``, which parses the formats independently of the code
that wrote them.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
IMAGE = os.path.join(TOOLS, "image")
sys.path.insert(0, IMAGE)

import fat as fatmod          # noqa: E402
import iso9660                # noqa: E402
import verify                 # noqa: E402

EPOCH = 315532800


def write_tree(root: str, files: dict[str, bytes]) -> None:
    for rel, data in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(data)


class TempDirTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.mkdtemp(prefix="ravynos-test.")

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def path(self, *parts: str) -> str:
        return os.path.join(self.tmp, *parts)


# --------------------------------------------------------------------------
# ISO 9660 + El Torito
# --------------------------------------------------------------------------
class IsoWriterTests(TempDirTest):
    def build(self, files: dict[str, bytes], out: str = "out.iso", **kwargs) -> str:
        stage = self.path("stage")
        os.makedirs(stage, exist_ok=True)
        write_tree(stage, files)
        boot = kwargs.pop("boot_image", None)
        epoch = kwargs.pop("epoch", EPOCH)
        img = iso9660.IsoImage(
            iso9660.build_tree(stage, 2, epoch), "RAVYNOS-TEST", epoch,
            boot_image=boot, **kwargs)
        data = img.build()
        target = self.path(out)
        with open(target, "wb") as fh:
            fh.write(data)
        return target

    def test_minimal_iso_verifies(self) -> None:
        out = self.build({"HELLO.TXT": b"hello\n"})
        info = verify.verify_iso(out)
        self.assertEqual(info["volume_id"], "RAVYNOS-TEST")
        self.assertEqual({f["path"] for f in info["files"]}, {"HELLO.TXT"})
        self.assertIsNone(info["boot"])

    def test_nested_directories_and_binaries(self) -> None:
        payload = bytes(range(256)) * 40  # 10 KiB, spans several sectors
        out = self.build({
            "BOOT/BOOTSTUB.BIN": b"\xF4" * 2048,
            "System/Library/Extensions/foo.kext/Contents/Info.plist": b"<plist/>",
            "DATA/BLOB.BIN": payload,
            "DATA/empty": b"",
        }, boot_image="BOOT/BOOTSTUB.BIN", boot_load_size=4)
        info = verify.verify_iso(out)
        paths = {f["path"] for f in info["files"]}
        self.assertIn("DATA/BLOB.BIN", paths)
        self.assertIn("DATA/EMPTY", paths)
        self.assertIsNotNone(info["boot"])
        entry = [e for e in info["boot"]["entries"] if e["bootable"]][0]
        self.assertGreaterEqual(entry["load_sectors"], 4)

        # Byte-exact round trip of the payload through the recorded extent.
        with open(out, "rb") as fh:
            blob = fh.read()
        record = next(f for f in info["files"] if f["path"] == "DATA/BLOB.BIN")
        start = record["lba"] * 2048
        self.assertEqual(blob[start:start + len(payload)], payload)

    def test_efi_entry_is_emitted(self) -> None:
        out = self.build({"BOOT/BOOTX64.EFI": b"MZ" + b"\x00" * 510},
                         efi_image="BOOT/BOOTX64.EFI", efi_load_size=1)
        info = verify.verify_iso(out)
        kinds = {e["kind"] for e in info["boot"]["entries"]}
        self.assertIn("uefi", kinds)

    def test_long_names_and_collisions(self) -> None:
        files = {
            "a-very-long-file-name-indeed.txt": b"1",
            "a-very-long-file-name-indeed.bin": b"2",
            "lowercase.dat": b"3",
            "SPACES IN NAME.txt": b"4",
        }
        out = self.build(files)
        info = verify.verify_iso(out)
        self.assertEqual(len(info["files"]), len(files))
        sizes = sorted(f["size"] for f in info["files"])
        self.assertEqual(sizes, [1, 1, 1, 1])

    def test_deterministic(self) -> None:
        files = {"A.TXT": b"a" * 4000, "B/C.TXT": b"c"}
        first = self.build(files, "one.iso")
        second = self.build(files, "two.iso")
        with open(first, "rb") as fh1, open(second, "rb") as fh2:
            self.assertEqual(fh1.read(), fh2.read())

    def test_symlink_is_rejected(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        with open(os.path.join(stage, "real.txt"), "w") as fh:
            fh.write("x")
        os.symlink("real.txt", os.path.join(stage, "link.txt"))
        with self.assertRaises(ValueError):
            iso9660.build_tree(stage, 2, EPOCH)

    def test_missing_boot_image_is_rejected(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"A.TXT": b"a"})
        root = iso9660.build_tree(stage, 2, EPOCH)
        with self.assertRaises(ValueError):
            iso9660.IsoImage(root, "X", EPOCH, boot_image="NOPE.BIN")

    def test_directory_records_never_cross_a_sector(self) -> None:
        # 60 files with long names give directory records that must be padded
        # at sector boundaries rather than split.
        files = {f"file-with-a-long-name-{i:02d}.txt": b"x" * (i + 1)
                 for i in range(60)}
        out = self.build(files)
        info = verify.verify_iso(out)
        self.assertEqual(len(info["files"]), 60)

    def test_level1_names_are_8_3(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"long-file-name.text": b"x"})
        root = iso9660.build_tree(stage, 1, EPOCH)
        img = iso9660.IsoImage(root, "X", EPOCH)
        data = img.build()
        target = self.path("l1.iso")
        with open(target, "wb") as fh:
            fh.write(data)
        info = verify.verify_iso(target)
        name = info["files"][0]["path"]
        base, _, ext = name.partition(".")
        self.assertLessEqual(len(base), 8, name)
        self.assertLessEqual(len(ext), 3, name)


# --------------------------------------------------------------------------
# FAT16 + GPT
# --------------------------------------------------------------------------
class FatWriterTests(TempDirTest):
    def build(self, files: dict[str, bytes], out: str = "disk.img",
              **kwargs) -> tuple[str, dict]:
        stage = self.path("stage")
        os.makedirs(stage, exist_ok=True)
        write_tree(stage, files)
        tree = fatmod.scan_tree(stage)
        data, meta = fatmod.build_disk_image(tree, epoch=EPOCH, label="RAVYNOS",
                                             **kwargs)
        target = self.path(out)
        with open(target, "wb") as fh:
            fh.write(data)
        return target, meta

    def test_minimal_image_verifies(self) -> None:
        out, meta = self.build({"EFI/BOOT/BOOTX64.EFI": b"MZ" + b"\x00" * 100})
        info = verify.verify_img(out)
        self.assertTrue(info["partitions"][0]["is_esp"])
        self.assertEqual({f["path"] for f in info["esp"]["files"]},
                         {"EFI/BOOT/BOOTX64.EFI"})
        self.assertGreaterEqual(meta["cluster_count"], 4085)

    def test_multi_cluster_files_round_trip(self) -> None:
        blob = os.urandom(70 * 1024)
        out, _ = self.build({"big.bin": blob, "small.bin": b"x" * 3})
        info = verify.verify_img(out)
        self.assertEqual(max(f["size"] for f in info["esp"]["files"]), len(blob))

        # Read the payload back through the FAT chain independently.
        with open(out, "rb") as fh:
            image = fh.read()
        esp = info["partitions"][0]
        part = image[esp["start_lba"] * 512:(esp["end_lba"] + 1) * 512]
        volume = verify.verify_fat16(part, "test")
        boot = part[0:512]
        reserved = int.from_bytes(boot[14:16], "little")
        fat_sectors = int.from_bytes(boot[22:24], "little")
        nfats = boot[16]
        root_entries = int.from_bytes(boot[17:19], "little")
        spc = boot[13]
        data_start = (reserved + nfats * fat_sectors
                      + (root_entries * 32 + 511) // 512)
        fat_entries = list(__import__("struct").unpack(
            f"<{fat_sectors * 256}H", part[reserved * 512:
                                          (reserved + fat_sectors) * 512]))
        record = next(f for f in volume["files"] if f["path"] == "big.bin")
        chain, cluster = [], record["clusters"][0]
        while True:
            chain.append(cluster)
            nxt = fat_entries[cluster]
            if nxt >= 0xFFF8:
                break
            cluster = nxt
        got = b"".join(
            part[(data_start + (c - 2) * spc) * 512:
                 (data_start + (c - 2) * spc + spc) * 512]
            for c in chain)[:len(blob)]
        self.assertEqual(got, blob)

    def test_long_names_survive(self) -> None:
        names = [
            "SystemVersion.plist",
            "com.apple.Boot.plist",
            "lowercase-name.txt",
            "UPPER.TXT",
            "a" * 60 + ".txt",
        ]
        out, _ = self.build({n: n.encode() for n in names})
        info = verify.verify_img(out)
        got = {f["path"] for f in info["esp"]["files"]}
        self.assertEqual(got, set(names))

    def test_identical_names_in_different_directories(self) -> None:
        out, _ = self.build({
            "A/README.TXT": b"a",
            "B/README.TXT": b"b",
            "C/README.TXT": b"c",
        })
        info = verify.verify_img(out)
        self.assertEqual(len(info["esp"]["files"]), 3)

    def test_empty_file_and_empty_directory(self) -> None:
        stage = self.path("stage")
        os.makedirs(os.path.join(stage, "EMPTYDIR"))
        write_tree(stage, {"EMPTY.TXT": b"", "REAL.TXT": b"x"})
        tree = fatmod.scan_tree(stage)
        data, _ = fatmod.build_disk_image(tree, epoch=EPOCH)
        target = self.path("empty.img")
        with open(target, "wb") as fh:
            fh.write(data)
        info = verify.verify_img(target)
        self.assertIn("EMPTY.TXT", {f["path"] for f in info["esp"]["files"]})

    def test_deterministic(self) -> None:
        files = {"EFI/BOOT/BOOTX64.EFI": os.urandom(4096), "x.txt": b"y"}
        first, _ = self.build(files, "one.img")
        second, _ = self.build(files, "two.img")
        with open(first, "rb") as fh1, open(second, "rb") as fh2:
            self.assertEqual(fh1.read(), fh2.read())

    def test_gpt_backup_matches_primary(self) -> None:
        out, meta = self.build({"A.TXT": b"a"})
        with open(out, "rb") as fh:
            image = fh.read()
        total = len(image) // 512
        primary = image[512:1024]
        backup = image[(total - 1) * 512:total * 512]
        self.assertEqual(primary[0:8], b"EFI PART")
        self.assertEqual(backup[0:8], b"EFI PART")
        # The headers point at different entry-array copies (primary near the
        # start, backup near the end), so compare the arrays themselves.
        self.assertNotEqual(primary[72:80], backup[72:80])
        self.assertEqual(image[2 * 512:34 * 512],
                         image[(total - 33) * 512:(total - 1) * 512])

    def test_symlinks_and_specials_are_rejected(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"real.txt": b"x"})
        os.symlink("real.txt", os.path.join(stage, "link.txt"))
        with self.assertRaises(ValueError):
            fatmod.scan_tree(stage)
        os.unlink(os.path.join(stage, "link.txt"))
        os.mkfifo(os.path.join(stage, "fifo"))
        with self.assertRaises(ValueError):
            fatmod.scan_tree(stage)

    def test_volume_too_small_is_rejected(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"A.TXT": b"a"})
        tree = fatmod.scan_tree(stage)
        with self.assertRaises(ValueError):
            fatmod.build_disk_image(tree, size_mb=1, epoch=EPOCH)

    def test_explicit_size_is_honoured(self) -> None:
        out, meta = self.build({"A.TXT": b"a"}, size_mb=24)
        self.assertEqual(os.path.getsize(out), 24 * 1024 * 1024)
        self.assertEqual(meta["total_sectors"], 24 * 1024 * 1024 // 512)

    def test_no_gpt_variant(self) -> None:
        out, meta = self.build({"A.TXT": b"a"}, gpt=False)
        with open(out, "rb") as fh:
            head = fh.read(512)
        self.assertEqual(head[510:512], b"\x55\xAA")
        self.assertEqual(head[450], 0)  # no protective 0xEE partition
        self.assertFalse(meta["gpt"])


# --------------------------------------------------------------------------
# Boot stub generator
# --------------------------------------------------------------------------
class BootStubTests(TempDirTest):
    def test_size_and_markers(self) -> None:
        import bootstub
        image = bootstub.build_image(["RAVYNOS-ONE-OK", "RAVYNOS-TWO-OK"])
        self.assertEqual(len(image), 2048)
        self.assertIn(b"RAVYNOS-ONE-OK", image)
        self.assertIn(b"RAVYNOS-TWO-OK", image)

    def test_deterministic(self) -> None:
        import bootstub
        self.assertEqual(bootstub.build_image(["X"]), bootstub.build_image(["X"]))

    def test_undersized_load_size_is_rejected(self) -> None:
        import bootstub
        # ~500 bytes of marker plus ~90 bytes of code cannot fit in 512.
        with self.assertRaises(ValueError):
            bootstub.build_image(["A" * 500], load_size=512)


# --------------------------------------------------------------------------
# Verifier negative cases: corruption must be caught
# --------------------------------------------------------------------------
class VerifierTests(TempDirTest):
    def make_iso(self) -> str:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"BOOT/BOOTSTUB.BIN": b"\xF4" * 2048, "A.TXT": b"a"})
        root = iso9660.build_tree(stage, 2, EPOCH)
        img = iso9660.IsoImage(root, "V", EPOCH,
                               boot_image="BOOT/BOOTSTUB.BIN", boot_load_size=4)
        target = self.path("corrupt.iso")
        with open(target, "wb") as fh:
            fh.write(img.build())
        return target

    def test_bad_identifier_is_caught(self) -> None:
        path = self.make_iso()
        with open(path, "r+b") as fh:
            fh.seek(16 * 2048 + 1)
            fh.write(b"XXXXX")
        with self.assertRaises(verify.VerifierError):
            verify.verify_iso(path)

    def test_truncated_image_is_caught(self) -> None:
        path = self.make_iso()
        with open(path, "r+b") as fh:
            fh.truncate(20 * 2048)
        with self.assertRaises(verify.VerifierError):
            verify.verify_iso(path)

    def test_catalog_checksum_is_checked(self) -> None:
        path = self.make_iso()
        info = verify.verify_iso(path)
        catalog_lba = info["boot"]["catalog_lba"]
        with open(path, "r+b") as fh:
            fh.seek(catalog_lba * 2048 + 4)
            fh.write(b"XXXX")  # corrupt the ID string, invalidating the checksum
        with self.assertRaises(verify.VerifierError):
            verify.verify_iso(path)

    def test_gpt_crc_is_checked(self) -> None:
        stage = self.path("stage2")
        os.makedirs(stage)
        write_tree(stage, {"A.TXT": b"a"})
        tree = fatmod.scan_tree(stage)
        data, _ = fatmod.build_disk_image(tree, epoch=EPOCH)
        path = self.path("bad.img")
        broken = bytearray(data)
        broken[512 + 56] ^= 0xFF  # flip a byte inside the disk GUID
        with open(path, "wb") as fh:
            fh.write(broken)
        with self.assertRaises(verify.VerifierError):
            verify.verify_img(path)


# --------------------------------------------------------------------------
# CLI smoke tests: the scripts are what CI actually calls
# --------------------------------------------------------------------------
class CommandLineTests(TempDirTest):
    def run_tool(self, *args: str) -> subprocess.CompletedProcess:
        return subprocess.run(list(args), capture_output=True, text=True)

    def test_iso_cli_build_and_verify(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"BOOT/BOOTSTUB.BIN": b"\xF4" * 2048})
        out = self.path("cli.iso")
        result = self.run_tool(sys.executable, os.path.join(IMAGE, "iso9660.py"),
                               "build", stage, "-o", out,
                               "--boot-image", "BOOT/BOOTSTUB.BIN",
                               "--timestamp", str(EPOCH))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_tool(sys.executable, os.path.join(IMAGE, "verify.py"),
                               "iso", out)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_fat_cli_build_and_verify(self) -> None:
        stage = self.path("stage")
        os.makedirs(stage)
        write_tree(stage, {"EFI/BOOT/BOOTX64.EFI": b"MZ"})
        out = self.path("cli.img")
        result = self.run_tool(sys.executable, os.path.join(IMAGE, "fat.py"),
                               "build", stage, "-o", out, "--size-mb", "16",
                               "--timestamp", str(EPOCH))
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.run_tool(sys.executable, os.path.join(IMAGE, "verify.py"),
                               "img", out)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_verify_rejects_a_text_file(self) -> None:
        path = self.path("not-an-image.iso")
        with open(path, "w") as fh:
            fh.write("this is not an ISO")
        result = self.run_tool(sys.executable, os.path.join(IMAGE, "verify.py"),
                               "iso", path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("VERIFY FAIL", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
