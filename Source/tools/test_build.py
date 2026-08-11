#!/usr/bin/env python3
"""Static release checks for ForceNodes DLLs and the current WRSR build."""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_FILE_DLL = 0x2000
IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040
IMAGE_DLLCHARACTERISTICS_NX_COMPAT = 0x0100
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_WRITE = 0x80000000


class PEError(RuntimeError):
    pass


@dataclass(frozen=True)
class Section:
    name: str
    virtual_size: int
    virtual_address: int
    raw_size: int
    raw_offset: int
    characteristics: int


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if len(self.data) < 0x100 or self.data[:2] != b"MZ":
            raise PEError("missing DOS header")
        self.pe_offset = self.u32(0x3C)
        if self.pe_offset + 24 > len(self.data) or self.data[self.pe_offset:self.pe_offset+4] != b"PE\0\0":
            raise PEError("missing PE signature")
        coff = self.pe_offset + 4
        self.machine = self.u16(coff)
        self.section_count = self.u16(coff + 2)
        self.timestamp = self.u32(coff + 4)
        self.optional_size = self.u16(coff + 16)
        self.characteristics = self.u16(coff + 18)
        self.optional = coff + 20
        if self.optional + self.optional_size > len(self.data):
            raise PEError("truncated optional header")
        self.magic = self.u16(self.optional)
        if self.magic != 0x20B:
            raise PEError(f"expected PE32+, got magic 0x{self.magic:04X}")
        self.image_version = (self.u16(self.optional + 44), self.u16(self.optional + 46))
        self.subsystem = self.u16(self.optional + 68)
        self.dll_characteristics = self.u16(self.optional + 70)
        self.image_size = self.u32(self.optional + 56)
        self.checksum = self.u32(self.optional + 64)
        self.directory_count = min(self.u32(self.optional + 108), 16)
        self.directories: list[tuple[int, int]] = []
        for i in range(self.directory_count):
            off = self.optional + 112 + i * 8
            self.directories.append((self.u32(off), self.u32(off + 4)))
        while len(self.directories) < 16:
            self.directories.append((0, 0))
        section_table = self.optional + self.optional_size
        if section_table + self.section_count * 40 > len(self.data):
            raise PEError("truncated section table")
        self.sections: list[Section] = []
        for i in range(self.section_count):
            off = section_table + i * 40
            name = self.data[off:off+8].split(b"\0", 1)[0].decode("ascii", "replace")
            self.sections.append(Section(
                name=name,
                virtual_size=self.u32(off + 8),
                virtual_address=self.u32(off + 12),
                raw_size=self.u32(off + 16),
                raw_offset=self.u32(off + 20),
                characteristics=self.u32(off + 36),
            ))

    def u16(self, off: int) -> int:
        if off < 0 or off + 2 > len(self.data):
            raise PEError(f"read beyond file at 0x{off:X}")
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off: int) -> int:
        if off < 0 or off + 4 > len(self.data):
            raise PEError(f"read beyond file at 0x{off:X}")
        return struct.unpack_from("<I", self.data, off)[0]

    def u64(self, off: int) -> int:
        if off < 0 or off + 8 > len(self.data):
            raise PEError(f"read beyond file at 0x{off:X}")
        return struct.unpack_from("<Q", self.data, off)[0]

    def rva_to_offset(self, rva: int) -> int:
        if rva == 0:
            raise PEError("null RVA")
        for sec in self.sections:
            span = max(sec.virtual_size, sec.raw_size)
            if sec.virtual_address <= rva < sec.virtual_address + span:
                delta = rva - sec.virtual_address
                if delta >= sec.raw_size:
                    raise PEError(f"RVA 0x{rva:X} has no file backing")
                off = sec.raw_offset + delta
                if off >= len(self.data):
                    raise PEError(f"RVA 0x{rva:X} points beyond file")
                return off
        if rva < self.u32(self.optional + 60):
            return rva
        raise PEError(f"unmapped RVA 0x{rva:X}")

    def c_string_rva(self, rva: int, limit: int = 512) -> str:
        off = self.rva_to_offset(rva)
        end = self.data.find(b"\0", off, min(len(self.data), off + limit))
        if end < 0:
            raise PEError(f"unterminated string at RVA 0x{rva:X}")
        return self.data[off:end].decode("ascii", "replace")

    def imports(self) -> dict[str, list[str]]:
        rva, size = self.directories[1]
        if not rva or not size:
            return {}
        off = self.rva_to_offset(rva)
        result: dict[str, list[str]] = {}
        while True:
            if off + 20 > len(self.data):
                raise PEError("truncated import descriptor")
            original, _, _, name_rva, first = struct.unpack_from("<IIIII", self.data, off)
            off += 20
            if not any((original, name_rva, first)):
                break
            dll = self.c_string_rva(name_rva)
            thunk_rva = original or first
            thunk_off = self.rva_to_offset(thunk_rva)
            names: list[str] = []
            while True:
                value = self.u64(thunk_off)
                thunk_off += 8
                if value == 0:
                    break
                if value & (1 << 63):
                    names.append(f"ordinal:{value & 0xFFFF}")
                else:
                    name_off = self.rva_to_offset(value)
                    end = self.data.find(b"\0", name_off + 2, min(len(self.data), name_off + 514))
                    if end < 0:
                        raise PEError("unterminated import name")
                    names.append(self.data[name_off+2:end].decode("ascii", "replace"))
            result[dll] = names
        return result

    def exports(self) -> list[str]:
        rva, size = self.directories[0]
        if not rva or size < 40:
            return []
        off = self.rva_to_offset(rva)
        number_of_names = self.u32(off + 24)
        names_rva = self.u32(off + 32)
        if number_of_names > 10000:
            raise PEError("implausible export count")
        names_off = self.rva_to_offset(names_rva)
        return [self.c_string_rva(self.u32(names_off + i * 4)) for i in range(number_of_names)]

    def executable_sections(self) -> Iterable[Section]:
        return (s for s in self.sections if s.characteristics & IMAGE_SCN_MEM_EXECUTE)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def check(condition: bool, message: str, failures: list[str]) -> None:
    prefix = "PASS" if condition else "FAIL"
    print(f"{prefix}: {message}")
    if not condition:
        failures.append(message)


def verify_plugin(path: Path, expected_exports: set[str], allowed_dlls: set[str], version_text: bytes) -> list[str]:
    print(f"\n== {path.name} ==")
    failures: list[str] = []
    try:
        pe = PE(path)
    except (OSError, PEError) as exc:
        print(f"FAIL: cannot parse PE: {exc}")
        return [f"{path.name}: {exc}"]
    imports = pe.imports()
    exports = set(pe.exports())
    import_names = {name.lower() for name in imports}
    check(pe.machine == IMAGE_FILE_MACHINE_AMD64, "x86-64 machine", failures)
    check(bool(pe.characteristics & IMAGE_FILE_DLL), "PE is marked as a DLL", failures)
    check(pe.magic == 0x20B, "PE32+ optional header", failures)
    check(pe.image_version == (1, 8), "image version is 1.8", failures)
    check(pe.checksum != 0, "release checksum is present", failures)
    check(bool(pe.dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE), "ASLR enabled", failures)
    check(bool(pe.dll_characteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA), "high-entropy ASLR enabled", failures)
    check(bool(pe.dll_characteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT), "NX compatibility enabled", failures)
    check(bool(pe.directories[1][0]), "conventional PE import table is present", failures)
    check(bool(pe.directories[2][0]), "Windows version-resource metadata is present", failures)
    check(bool(pe.directories[3][0]), "x64 exception/unwind table is present", failures)
    check(pe.directories[14] == (0, 0), "no CLR/.NET runtime header", failures)
    check(not any((s.characteristics & IMAGE_SCN_MEM_EXECUTE) and (s.characteristics & IMAGE_SCN_MEM_WRITE) for s in pe.sections),
          "no writable executable section", failures)
    check(exports == expected_exports,
          f"exports are exactly {', '.join(sorted(expected_exports))}", failures)
    check(import_names <= allowed_dlls,
          f"dependencies limited to {', '.join(sorted(allowed_dlls))}; found {', '.join(sorted(import_names))}", failures)
    check(version_text in pe.data, f"embedded version text {version_text.decode(errors='replace')}", failures)
    check(b".pdb" not in pe.data.lower(), "no local PDB path embedded", failures)
    print("Imports:")
    for dll, names in imports.items():
        print(f"  {dll}: {', '.join(names)}")
    print("Sections:", ", ".join(s.name for s in pe.sections))
    print("SHA-256:", sha256(path))
    return [f"{path.name}: {item}" for item in failures]


PATTERNS: dict[str, tuple[bytes, str, int]] = {
    "frame": (bytes.fromhex("48 8B C4 48 89 58 20 55 56 57 41 55 41 56 48 8D A8 00 00 00 00 48 81 EC 00 00 00 00 0F 29 70 C8"), "xxxxxxxxxxxxxxxxx????xxx????xxxx", 0x2F0F10),
    "split": (bytes.fromhex("48 8B C4 44 89 40 18 48 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 88 48 81 EC 00 00 00 00"), "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????", 0x5446D0),
    "merge": (bytes.fromhex("48 8B C4 48 89 48 08 55 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81 EC 30 01 00 00 48 C7 45 E0 FE"), "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 0x53E630),
    "refresh": (bytes.fromhex("40 53 48 83 EC 20 83 3D 00 00 00 00 02 48 8B D9 0F 8C 00 00 00 00 83 B9 58 02 00 00 00 0F 85"), "xxxxxxxx????xxxxxx????xxxxxxxxx", 0x5518B0),
    "insert24": (bytes.fromhex("4C 89 4C 24 20 4C 89 44 24 18 53 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 48 C7 44 24 30 FE FF FF FF 4D 8B C8 4C 8B E2 48 8B F9 4C 8B 19 4D 2B C3 49 BD AB AA AA AA AA AA AA 2A 49 8B C5 49 F7 E8 4C 8B F2 49 C1 FE 02"), "x" * 72, 0x576260),
    "reserve-byte": (bytes.fromhex("48 83 EC 28 4C 8B 51 10 4C 8B C2 48 8B 51 08 49 8B C2 48 2B C2 4C 8B C9 49 3B C0"), "x" * 27, 0x1E3C0),
    "render": (bytes.fromhex("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 81 EC 00 00 00 00 80 3D"), "xxxxxxxxxxxxxxxxxxx????xx", 0x409660),
}


def scan_pattern(data: bytes, pattern: bytes, mask: str) -> list[int]:
    fixed = [(i, value) for i, (value, marker) in enumerate(zip(pattern, mask)) if marker == "x"]
    hits: list[int] = []
    limit = len(data) - len(pattern) + 1
    for offset in range(max(limit, 0)):
        if all(data[offset + i] == value for i, value in fixed):
            hits.append(offset)
    return hits


def verify_game(path: Path) -> list[str]:
    print(f"\n== Current game signature audit: {path.name} ==")
    failures: list[str] = []
    pe = PE(path)
    for label, (pattern, mask, expected_rva) in PATTERNS.items():
        hits: list[int] = []
        for sec in pe.executable_sections():
            raw = pe.data[sec.raw_offset:sec.raw_offset + sec.raw_size]
            for local in scan_pattern(raw, pattern, mask):
                hits.append(sec.virtual_address + local)
        ok = hits == [expected_rva]
        check(ok, f"{label} signature unique at exe+0x{expected_rva:X}; hits={','.join(hex(x) for x in hits) or 'none'}", failures)
    print("SHA-256:", sha256(path))
    return [f"game: {item}" for item in failures]


C3D_EXPORTS = {
    "?GetKeyDown@C3D_INPUT@@QEAA_NH@Z",
    "?GetMouseSolid@C3D_INPUT@@QEAA?AVC3DVECTOR3@@XZ",
    "?GetMouseLeftPress@C3D_INPUT@@QEAA_NXZ",
    "?GetMouseRightPress@C3D_INPUT@@QEAA_NXZ",
    "?GetMouseX1Press@C3D_INPUT@@QEAA_NXZ",
    "?GetMouseX2Press@C3D_INPUT@@QEAA_NXZ",
    "??0C3D_NODE@@QEAA@XZ",
    "?CreateFromPositionRotationScale@C3D_NODE@@QEAAXVC3DVECTOR3@@00@Z",
    "?PrintLeftUnicodeNoArg@C3D_FONTMANAGER@@QEAAXPEAVC3D_FONT@@MMKPEB_W@Z",
}


def verify_c3d(path: Path) -> list[str]:
    print(f"\n== C3D export audit: {path.name} ==")
    failures: list[str] = []
    exports = set(PE(path).exports())
    for name in sorted(C3D_EXPORTS):
        check(name in exports, f"C3D export present: {name}", failures)
    print("SHA-256:", sha256(path))
    return [f"C3D: {item}" for item in failures]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugins", type=Path, required=True, help="folder containing the three runtime files")
    parser.add_argument("--game", type=Path, help="optional current SOVIET64.exe for signature verification")
    parser.add_argument("--c3d", type=Path, help="optional current C3DDLL64.dll for export verification")
    parser.add_argument("--write-sha", type=Path, help="write hashes for runtime files")
    args = parser.parse_args()

    plugins = args.plugins
    main_dll = plugins / "ForceNodes.dll"
    shim_dll = plugins / "ForceNodes.Engine.dll"
    ini = plugins / "ForceNodes.ini"
    failures: list[str] = []
    for path in (main_dll, shim_dll, ini):
        if not path.is_file():
            failures.append(f"missing runtime file: {path}")
    if failures:
        for item in failures:
            print("FAIL:", item)
        return 1

    failures += verify_plugin(
        main_dll,
        {"TsmPluginApiVersion", "TsmPluginInit", "TsmPluginStart"},
        {"kernel32.dll", "ucrtbase.dll"},
        b"1.8.0",
    )
    failures += verify_plugin(
        shim_dll,
        {"TsmPluginApiVersion", "TsmPluginInit"},
        {"kernel32.dll"},
        b"1.8.0",
    )
    ini_data = ini.read_bytes()
    check(ini_data.startswith(b"; ForceNodes v1.8.0"), "configuration version header is v1.8.0", failures)
    check(b"bind_overlay = CTRL+NUMPAD8" in ini_data, "default overlay binding is Ctrl+Numpad8", failures)
    check(b"advanced_render_context_rva = 0" in ini_data,
          "render-context zero keeps automatic/default semantics", failures)
    check(b"advanced_sphere_mesh_slot_rva = 0" in ini_data,
          "marker-mesh zero keeps automatic/default semantics", failures)
    main_data = main_dll.read_bytes()
    check(struct.pack("<I", 0x9D4F10) in main_data,
          "known-good render-context fallback exe+0x9D4F10 is embedded", failures)
    check(struct.pack("<I", 0x9963C0) in main_data,
          "known-good marker-mesh fallback exe+0x9963C0 is embedded", failures)
    check("FORCE NODES".encode("utf-16le") in main_data,
          "right-side ForceNodes HUD title is embedded", failures)
    check("[8] OVERLAY   ON".encode("utf-16le") in main_data and
          "ESC EXIT MODES".encode("utf-16le") in main_data,
          "right-side ForceNodes HUD status rows are embedded", failures)
    if args.game:
        failures += verify_game(args.game)
    if args.c3d:
        failures += verify_c3d(args.c3d)

    if args.write_sha:
        args.write_sha.parent.mkdir(parents=True, exist_ok=True)
        lines = [f"{sha256(path)}  {path.name}" for path in (main_dll, shim_dll, ini)]
        args.write_sha.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")

    if failures:
        print("\nSTATIC CHECKS FAILED")
        for item in failures:
            print(" -", item)
        return 1
    print("\nALL STATIC CHECKS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
