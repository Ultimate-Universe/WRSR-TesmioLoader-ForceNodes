# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import hashlib
import struct

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out" / "ForceNodes.dll"
ENGINE = ROOT.parent / "binary-dependency" / "ForceNodes.Engine.dll"
SRC = (ROOT / "ForceNodes.cpp").read_text()
API = (ROOT / "ForceNodes_API.h").read_text()


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def inspect_pe(path):
    data = path.read_bytes()
    assert data[:2] == b"MZ"
    pe = u32(data, 0x3C)
    assert data[pe:pe + 4] == b"PE\0\0"
    coff = pe + 4
    section_count = u16(data, coff + 2)
    optional_size = u16(data, coff + 16)
    optional = coff + 20
    assert u16(data, optional) == 0x20B
    directories = optional + 112
    export_rva, _ = struct.unpack_from("<II", data, directories)
    import_rva, import_size = struct.unpack_from("<II", data, directories + 8)
    sections = []
    for index in range(section_count):
        offset = optional + optional_size + index * 40
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))

    def file_offset(rva):
        for virtual_address, size, raw_pointer in sections:
            if virtual_address <= rva < virtual_address + size:
                return raw_pointer + rva - virtual_address
        return rva

    exports = set()
    if export_rva:
        export = file_offset(export_rva)
        name_count = u32(data, export + 24)
        names = file_offset(u32(data, export + 32))
        for index in range(name_count):
            name_offset = file_offset(u32(data, names + index * 4))
            name_end = data.index(0, name_offset)
            exports.add(data[name_offset:name_end].decode())
    return data, exports, import_size if import_rva else 0


data, exports, imports = inspect_pe(OUT)
assert exports == {"TsmPluginApiVersion", "TsmPluginInit", "TsmPluginStart"}
assert imports == 0
assert b"1.6.0-stable" in data
assert b"bind_overlay" in data
assert b"bind_remove_node" in data
assert b"recalibration removed" in data
assert "FN_BIND_SENTINEL_OVERLAY" in SRC
assert "*(int*)(g_core+0x9018)=0" in SRC
assert "BoundAddQuery" in SRC and "BoundRemoveQuery" in SRC
assert "FORCE_NODES_API_VERSION 6u" in API

engine, engine_exports, engine_imports = inspect_pe(ENGINE)
assert engine_exports == {"TsmPluginApiVersion", "TsmPluginInit"}
assert engine_imports == 0
for text in ["OVERLAY   ON", "OVERLAY   OFF", "FORCE     ON", "FORCE     OFF",
             "GRID      OFF", "GRID   SQUARE", "ESC EXIT MODES"]:
    assert text.encode("utf-16le") in engine
assert "[1] OVERLAY".encode("utf-16le") not in engine
assert "M4 ADD".encode("utf-16le") not in engine
assert b"0.3.5-engine-core" in engine
print(hashlib.sha256(data).hexdigest(), OUT.name)
print(hashlib.sha256(engine).hexdigest(), ENGINE.name)
print("ForceNodes v1.6.0 static checks passed")
