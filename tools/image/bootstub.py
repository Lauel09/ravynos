#!/usr/bin/env python3
"""Generate the El Torito boot stub without nasm.

The pipeline's stub image needs a tiny 16-bit real-mode boot program that
writes a marker to COM1 and halts.  ``tools/bootstub/bootstub.asm`` is the
readable source and is used whenever nasm is installed; this file is a
hand-assembled equivalent so the pipeline (and its QEMU boot test) also works
on a host with nothing but a Python interpreter -- a bare CI container, for
example.

The generated code is identical in behaviour to the .asm:

    init COM1 at 115200 8N1; for each marker string: print it, then delay
    (except after the last one), then halt forever.

Usage
-----
    bootstub.py -o BOOTSTUB.BIN [--marker TEXT ...] [--load-size 2048]
                [--delay-loops N] [--print-hex]

Exit status: 0 on success, 2 on usage error.
"""

from __future__ import annotations

import argparse
import struct
import sys

ORG = 0x7C00
DEFAULT_MARKERS = ["RAVYNOS-BOOTCHECK-STUB-OK"]


class Assembler:
    """A minimal 16-bit x86 emitter: labels, relative and absolute fixups."""

    def __init__(self) -> None:
        self.code = bytearray()
        self.labels: dict[str, int] = {}
        self._fixups: list[tuple[int, str, str, int]] = []  # (pos, label, kind, base)

    # -- emitting ----------------------------------------------------------
    def label(self, name: str) -> None:
        if name in self.labels:
            raise ValueError(f"duplicate label: {name}")
        self.labels[name] = len(self.code)

    def emit(self, *values: int) -> None:
        self.code.extend(v & 0xFF for v in values)

    def emit_word(self, value: int) -> None:
        self.code += struct.pack("<H", value & 0xFFFF)

    def _rel(self, opcode: int, label: str) -> None:
        self.code.append(opcode)
        self._fixups.append((len(self.code), label, "rel8", len(self.code) + 1))
        self.code.append(0x00)

    def je(self, label: str) -> None:
        self._rel(0x74, label)

    def jz(self, label: str) -> None:
        self._rel(0x74, label)

    def jmp_short(self, label: str) -> None:
        self._rel(0xEB, label)

    def loop(self, label: str) -> None:
        self._rel(0xE2, label)

    def call(self, label: str) -> None:
        # rel16 call: E8 lo hi
        self.code.append(0xE8)
        self._fixups.append((len(self.code), label, "rel16", len(self.code) + 2))
        self.code += b"\x00\x00"

    def mov_si(self, label: str) -> None:
        self.emit(0xBE)
        self._fixups.append((len(self.code), label, "abs16", 0))
        self.code += b"\x00\x00"

    def resolve(self, origin: int = ORG) -> bytes:
        for pos, label, kind, base in self._fixups:
            if label not in self.labels:
                raise ValueError(f"undefined label: {label}")
            target = origin + self.labels[label]
            if kind == "rel8":
                delta = target - (origin + base)
                if not -128 <= delta <= 127:
                    raise ValueError(f"rel8 out of range for {label}: {delta}")
                self.code[pos] = delta & 0xFF
            elif kind == "rel16":
                delta = target - (origin + base)
                struct.pack_into("<H", self.code, pos, delta & 0xFFFF)
            else:
                struct.pack_into("<H", self.code, pos, target & 0xFFFF)
        return bytes(self.code)


def build_stub(markers: list[str], delay_loops: int = 8) -> bytes:
    """Assemble the stub.  Returns the raw code (not yet padded)."""
    a = Assembler()

    a.emit(0xFA)              # cli
    a.emit(0x31, 0xC0)        # xor ax, ax
    a.emit(0x8E, 0xD8)        # mov ds, ax
    a.emit(0x8E, 0xC0)        # mov es, ax
    a.emit(0x8E, 0xD0)        # mov ss, ax
    a.emit(0xBC)
    a.emit_word(ORG)          # mov sp, 0x7C00

    # --- COM1: 115200 baud, 8N1 -----------------------------------------
    a.emit(0xBA)
    a.emit_word(0x3FB)        # mov dx, 0x3FB  (LCR)
    a.emit(0xB0, 0x80)        # mov al, 0x80   (enable DLAB)
    a.emit(0xEE)              # out dx, al
    a.emit(0xBA)
    a.emit_word(0x3F8)        # mov dx, 0x3F8  (DLL)
    a.emit(0xB0, 0x01)        # mov al, 1      (divisor low: 115200)
    a.emit(0xEE)
    a.emit(0xBA)
    a.emit_word(0x3F9)        # mov dx, 0x3F9  (DLM)
    a.emit(0x30, 0xC0)        # xor al, al     (divisor high)
    a.emit(0xEE)
    a.emit(0xBA)
    a.emit_word(0x3FB)        # mov dx, 0x3FB
    a.emit(0xB0, 0x03)        # mov al, 3      (8N1)
    a.emit(0xEE)

    a.mov_si("markers")
    a.label("next_marker")
    a.call("print_string")
    a.emit(0x80, 0x3C, 0x00)  # cmp byte [si], 0
    a.je("halt")
    if delay_loops > 0:
        a.emit(0xB9)
        a.emit_word(delay_loops)   # mov cx, delay_loops
        a.label("delay_outer")
        a.emit(0x51)               # push cx
        a.emit(0xB9)
        a.emit_word(0xFFFF)        # mov cx, 0xFFFF
        a.label("delay_inner")
        a.emit(0xE2, 0xFE)         # loop delay_inner (rel8 -2)
        a.emit(0x59)               # pop cx
        a.loop("delay_outer")      # loop delay_outer
    a.jmp_short("next_marker")

    a.label("halt")
    a.emit(0xF4)              # hlt
    a.jmp_short("halt")       # jmp halt

    a.label("print_string")
    a.emit(0xAC)              # lodsb
    a.emit(0x0A, 0xC0)        # or al, al
    a.jz("ps_done")
    a.emit(0x88, 0xC4)        # mov ah, al
    a.label("ps_wait")
    a.emit(0xBA)
    a.emit_word(0x3FD)        # mov dx, 0x3FD (LSR)
    a.emit(0xEC)              # in al, dx
    a.emit(0xA8, 0x20)        # test al, 0x20 (THR empty)
    a.jz("ps_wait")
    a.emit(0x88, 0xE0)        # mov al, ah
    a.emit(0xBA)
    a.emit_word(0x3F8)        # mov dx, 0x3F8 (THR)
    a.emit(0xEE)              # out dx, al
    a.jmp_short("print_string")
    a.label("ps_done")
    a.emit(0xC3)              # ret

    # --- data ------------------------------------------------------------
    a.label("markers")
    for marker in markers:
        a.code += marker.encode("ascii") + b"\r"
        a.code += b"\n\x00"
    a.code += b"\x00"          # empty string terminates the table

    return a.resolve()


def build_image(markers: list[str], load_size: int = 2048,
                delay_loops: int = 8) -> bytes:
    code = build_stub(markers, delay_loops)
    if len(code) > load_size:
        raise ValueError(
            f"stub is {len(code)} bytes, larger than the {load_size}-byte "
            "El Torito load size; raise --load-size")
    return code + bytes(load_size - len(code))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Generate the boot smoke-test stub")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--marker", action="append", default=None,
                    help="marker line to print (repeatable)")
    ap.add_argument("--load-size", type=int, default=2048,
                    help="size of the boot image in bytes (default 2048)")
    ap.add_argument("--delay-loops", type=int, default=8,
                    help="busy-wait multiplier between markers (0 disables)")
    ap.add_argument("--print-hex", action="store_true")
    args = ap.parse_args(argv)

    markers = args.marker or DEFAULT_MARKERS
    if args.load_size % 512 or args.load_size <= 0:
        ap.error("--load-size must be a positive multiple of 512")
    try:
        image = build_image(markers, args.load_size, args.delay_loops)
    except ValueError as exc:
        print(f"bootstub: {exc}", file=sys.stderr)
        return 1
    if args.print_hex:
        print(image.hex())
    else:
        with open(args.output, "wb") as fh:
            fh.write(image)
        print(f"bootstub: wrote {args.output} ({len(image)} bytes, "
              f"{len(markers)} marker(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
