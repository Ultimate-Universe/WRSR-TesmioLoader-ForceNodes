#!/usr/bin/env python3
"""Set Windows PE image metadata and the standard checksum in-place."""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def set_checksum(path: Path, image_version: tuple[int, int] | None = None) -> int:
    data = bytearray(path.read_bytes())
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError("not a PE32+ image")
    if image_version is not None:
        major, minor = image_version
        if not (0 <= major <= 0xFFFF and 0 <= minor <= 0xFFFF):
            raise ValueError("image version components must fit in 16 bits")
        struct.pack_into("<HH", data, optional + 44, major, minor)
    checksum_offset = optional + 64
    struct.pack_into("<I", data, checksum_offset, 0)

    total = 0
    padded_length = len(data) + (len(data) & 1)
    for offset in range(0, padded_length, 2):
        word = data[offset] | ((data[offset + 1] if offset + 1 < len(data) else 0) << 8)
        total = (total + word) & 0xFFFFFFFF
        total = (total & 0xFFFF) + (total >> 16)
    total = (total & 0xFFFF) + (total >> 16)
    checksum = (total + len(data)) & 0xFFFFFFFF
    struct.pack_into("<I", data, checksum_offset, checksum)
    path.write_bytes(data)
    return checksum


if __name__ == "__main__":
    arguments = sys.argv[1:]
    version = None
    if arguments[:1] == ["--image-version"]:
        if len(arguments) < 3 or "." not in arguments[1]:
            raise SystemExit("usage: set_pe_checksum.py [--image-version MAJOR.MINOR] FILE...")
        major_text, minor_text = arguments[1].split(".", 1)
        version = (int(major_text), int(minor_text))
        arguments = arguments[2:]
    if not arguments:
        raise SystemExit("usage: set_pe_checksum.py [--image-version MAJOR.MINOR] FILE...")
    for filename in arguments:
        target = Path(filename)
        print(f"{target}: 0x{set_checksum(target, version):08X}")
