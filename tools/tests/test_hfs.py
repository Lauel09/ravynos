#!/usr/bin/env python3
"""Unit tests for tools/image/hfs.py (portable rootless HFS+/HFSX + MBR 0xAF)."""

from pathlib import Path
import os
import struct
import sys
import tempfile
import unittest

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools" / "image"))

import hfs  # noqa: E402


class TestHFSImageWriter(unittest.TestCase):
    def test_empty_mbr_and_bare_volumes_verify(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            mbr_img = tmp / "empty-mbr.img"
            bare_img = tmp / "empty-bare.img"

            hfs.write_hfs_image(mbr_img, None, partition="mbr", free_headroom_bytes=65536)
            hfs.write_hfs_image(bare_img, None, partition="none", free_headroom_bytes=65536)

            res_mbr = hfs.verify_hfs_image(mbr_img)
            self.assertTrue(res_mbr["has_mbr"])
            self.assertTrue(res_mbr["case_sensitive"])
            self.assertEqual(res_mbr["file_count"], 0)
            self.assertEqual(res_mbr["folder_count"], 0)
            self.assertEqual(res_mbr["tree_depth"], 1)
            self.assertIn("/", res_mbr["entries"])

            res_bare = hfs.verify_hfs_image(bare_img)
            self.assertFalse(res_bare["has_mbr"])
            self.assertEqual(res_bare["file_count"], 0)

    def test_mbr_partition_type_matches_iofdisk_apple_hfs(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            img = Path(td) / "root.img"
            hfs.write_hfs_image(img, None, partition="mbr", free_headroom_bytes=65536)
            raw = img.read_bytes()
            # MBR signature 0xAA55 at 510, partition 1 at 446
            self.assertEqual(struct.unpack_from("<H", raw, 510)[0], 0xAA55)
            boot_id, _, systid, _, relsect, numsect = struct.unpack_from("<B3sB3sII", raw, 446)
            self.assertEqual(boot_id & 0x7F, 0)  # IOFDiskPartitionScheme.cpp:415
            self.assertEqual(systid, 0xAF)       # IOStorageFamily/Info.plist:139-140 -> Apple_HFS
            self.assertEqual(relsect, 2048)
            self.assertEqual((relsect + numsect) * 512, len(raw))

    def test_dirs_files_symlinks_permissions_and_case_sensitivity(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            stage = tmp / "stage"
            (stage / "dev").mkdir(parents=True)
            (stage / "sbin").mkdir()
            (stage / "private" / "tmp").mkdir(parents=True)
            os.chmod(stage / "private" / "tmp", 0o1777)

            # Case-sensitive siblings ('Makefile' and 'makefile')
            (stage / "Makefile").write_bytes(b"UPPER\n")
            (stage / "makefile").write_bytes(b"lower\n")

            # Empty file + multi-block (>64 KiB) binary payload
            (stage / "empty").write_bytes(b"")
            big_payload = bytes((i * 37) & 0xFF for i in range(70_000))
            launchd = stage / "sbin" / "launchd"
            launchd.write_bytes(big_payload)
            os.chmod(launchd, 0o755)

            # Symbolic link (/tmp -> private/tmp)
            os.symlink("private/tmp", stage / "tmp")

            img = tmp / "staged.img"
            hfs.write_hfs_image(img, stage, partition="mbr", free_headroom_bytes=128 * 1024)
            info = hfs.verify_hfs_image(img)
            entries = info["entries"]

            self.assertIn("/dev", entries)
            self.assertTrue(entries["/dev"].is_dir)
            self.assertEqual(entries["/private/tmp"].mode, hfs.S_IFDIR_MODE | 0o1777)
            self.assertEqual(entries["/Makefile"].data, b"UPPER\n")
            self.assertEqual(entries["/makefile"].data, b"lower\n")
            self.assertEqual(entries["/empty"].size, 0)
            self.assertEqual(entries["/sbin/launchd"].data, big_payload)
            self.assertEqual(entries["/sbin/launchd"].mode, hfs.S_IFREG_MODE | 0o755)
            self.assertTrue(entries["/tmp"].is_symlink)
            self.assertEqual(entries["/tmp"].data, b"private/tmp")

    def test_multilevel_catalog_btree_splits_into_index_and_leaves(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            stage = tmp / "many"
            (stage / "usr" / "bin").mkdir(parents=True)
            # 120 files -> 240+ catalog records (>60 KiB) -> forces height >= 2
            for i in range(120):
                p = stage / "usr" / "bin" / f"tool_{i:04d}_with_longer_posix_name"
                p.write_bytes(f"payload-{i}\n".encode("ascii"))

            img = tmp / "many.img"
            hfs.write_hfs_image(img, stage, partition="none", free_headroom_bytes=65536)
            info = hfs.verify_hfs_image(img)
            self.assertGreaterEqual(info["tree_depth"], 2)
            self.assertEqual(info["file_count"], 120)
            entries = info["entries"]
            self.assertEqual(
                entries["/usr/bin/tool_0119_with_longer_posix_name"].data,
                b"payload-119\n",
            )

    def test_byte_reproducibility_and_hfsplus_mode(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            stage = tmp / "stage"
            (stage / "dev").mkdir(parents=True)
            (stage / "hello.txt").write_bytes(b"deterministic\n")

            img1 = tmp / "a.img"
            img2 = tmp / "b.img"
            hfs.write_hfs_image(img1, stage, partition="mbr", free_headroom_bytes=65536)
            hfs.write_hfs_image(img2, stage, partition="mbr", free_headroom_bytes=65536)
            self.assertEqual(img1.read_bytes(), img2.read_bytes())

            # Case-insensitive HFS+ ('H+' v4) mode
            img_hp = tmp / "hfsplus.img"
            hfs.write_hfs_image(
                img_hp, stage, partition="none", case_sensitive=False, free_headroom_bytes=65536
            )
            info_hp = hfs.verify_hfs_image(img_hp)
            self.assertFalse(info_hp["case_sensitive"])

    def test_verifier_rejects_corrupt_alt_vh_and_mbr_bootid(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            img = tmp / "corrupt.img"
            hfs.write_hfs_image(img, None, partition="mbr", free_headroom_bytes=65536)
            orig = bytearray(img.read_bytes())

            # Corrupt MBR bootid bit 0 (IOFDiskPartitionScheme.cpp:415)
            bad_mbr = bytearray(orig)
            bad_mbr[446] = 0x01
            img.write_bytes(bad_mbr)
            with self.assertRaises(hfs.HFSFormatError):
                hfs.verify_hfs_image(img)

            # Corrupt Alternate VolumeHeader byte
            bad_avh = bytearray(orig)
            bad_avh[-1024] ^= 0xFF
            img.write_bytes(bad_avh)
            with self.assertRaises(hfs.HFSFormatError):
                hfs.verify_hfs_image(img)

    def test_cli_build_and_verify(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            tmp = Path(td)
            stage = tmp / "root"
            (stage / "dev").mkdir(parents=True)
            (stage / "sbin").mkdir()
            img = tmp / "cli.img"
            rc_build = hfs.main(
                ["build", "-o", str(img), "--root", str(stage), "--free-mb", "1"]
            )
            self.assertEqual(rc_build, 0)
            rc_verify = hfs.main(["verify", str(img)])
            self.assertEqual(rc_verify, 0)


if __name__ == "__main__":
    unittest.main()
