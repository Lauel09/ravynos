#!/usr/bin/env python3
"""D5 gate: round-trip the loader's minimal flattened device tree.

The writer here mirrors build_device_tree() in tools/bootloader/uefi_stub.c
and the reader mirrors Device_tree.c (skipProperties, skipTree, FindChild):

    node     := u32 nProperties, u32 nChildren, property*, node*
    property := char name[32], u32 length, u8 value[length], pad to 4

A node is named by the *value* of its first "name" property, exactly as
FindChild() reads it with DTGetProperty(child, "name", ...).  The reader is
strict: every property name must be NUL-terminated, every value must fit, and
the root must consume the entire blob.
"""

import struct
import unittest

PROP_NAME_LEN = 32
FSB_FREQUENCY = 100000000
RANDOM_SEED_LEN = 64


def pad4(n):
    return (n + 3) & ~3


def prop(name, value):
    raw = name.encode("ascii")
    if len(raw) >= PROP_NAME_LEN:
        raise ValueError("property name too long: %r" % name)
    name_field = raw + b"\0" * (PROP_NAME_LEN - len(raw))
    out = name_field + struct.pack("<I", len(value)) + value
    return out + b"\0" * (pad4(len(out)) - len(out))


def node(properties, children):
    out = struct.pack("<II", len(properties), len(children))
    for name, value in properties:
        out += prop(name, value)
    for child in children:
        out += child
    return out


def build_loader_blob(seed=None):
    """The exact node/property layout of build_device_tree()."""
    if seed is None:
        seed = bytes((i + 1) & 0xFF for i in range(RANDOM_SEED_LEN))
    memory_map = node([("name", b"memory-map\0")], [])
    chosen = node(
        [
            ("name", b"chosen\0"),
            ("random-seed", seed),
        ],
        [memory_map],
    )
    platform = node(
        [
            ("name", b"platform\0"),
            ("FSBFrequency", struct.pack("<Q", FSB_FREQUENCY)),
        ],
        [],
    )
    efi = node([("name", b"efi\0")], [platform])
    return node([("name", b"\0")], [chosen, efi])


def parse(blob):
    """Return {path: properties}; raise ValueError on any malformed tree."""

    def read(off, limit):
        if off + 8 > limit:
            raise ValueError("truncated node header at %d" % off)
        nprops, nchildren = struct.unpack_from("<II", blob, off)
        off += 8
        props = {}
        for _ in range(nprops):
            if off + PROP_NAME_LEN + 4 > limit:
                raise ValueError("truncated property at %d" % off)
            raw_name = blob[off:off + PROP_NAME_LEN]
            if b"\0" not in raw_name:
                raise ValueError("unterminated property name at %d" % off)
            name = raw_name.split(b"\0", 1)[0].decode("ascii")
            length = struct.unpack_from("<I", blob, off + PROP_NAME_LEN)[0]
            start = off + PROP_NAME_LEN + 4
            end = start + length
            if end > limit:
                raise ValueError("property value past end at %d" % off)
            props[name] = blob[start:end]
            off = start + pad4(length)
        children = []
        for _ in range(nchildren):
            off, child, grandchildren = read(off, limit)
            if "name" not in child:
                raise ValueError("child without a name")
            name = child["name"].split(b"\0", 1)[0].decode("ascii")
            children.append((name, child, grandchildren))
        return off, props, children

    off, root_props, root_children = read(0, len(blob))
    if off != len(blob):
        raise ValueError("trailing bytes after root")

    nodes = {}

    def walk(path, props, children):
        nodes[path] = props
        prefix = "" if path == "/" else path
        for name, child_props, grandchildren in children:
            walk(prefix + "/" + name, child_props, grandchildren)

    walk("/", root_props, root_children)
    return nodes


class DeviceTreeTests(unittest.TestCase):
    def test_loader_blob_is_exact_and_round_trips(self):
        nodes = parse(build_loader_blob())
        self.assertEqual(
            set(nodes),
            {"/", "/chosen", "/chosen/memory-map", "/efi", "/efi/platform"},
        )
        self.assertEqual(nodes["/"]["name"], b"\0")
        self.assertEqual(nodes["/chosen"]["name"], b"chosen\0")
        self.assertEqual(len(nodes["/chosen"]["random-seed"]), RANDOM_SEED_LEN)
        self.assertTrue(any(nodes["/chosen"]["random-seed"]))
        self.assertEqual(nodes["/chosen/memory-map"]["name"], b"memory-map\0")
        self.assertEqual(nodes["/efi"]["name"], b"efi\0")
        self.assertEqual(nodes["/efi/platform"]["name"], b"platform\0")
        self.assertEqual(
            nodes["/efi/platform"]["FSBFrequency"],
            struct.pack("<Q", FSB_FREQUENCY),
        )

    def test_node_counts_and_root_name(self):
        blob = build_loader_blob()
        # root: 1 property (name), 2 children (chosen, efi)
        self.assertEqual(struct.unpack_from("<II", blob, 0), (1, 2))
        # The first property's name is "name"; its value is the empty root name.
        self.assertEqual(blob[8:12], b"name")
        self.assertEqual(struct.unpack_from("<I", blob, 40)[0], 1)
        self.assertEqual(blob[44], 0)

    def test_truncated_value_is_rejected(self):
        with self.assertRaises(ValueError):
            parse(build_loader_blob()[:-1])

    def test_unterminated_name_is_rejected(self):
        bad = bytearray(build_loader_blob())
        bad[8:40] = b"x" * PROP_NAME_LEN
        with self.assertRaises(ValueError):
            parse(bytes(bad))


if __name__ == "__main__":
    unittest.main()
