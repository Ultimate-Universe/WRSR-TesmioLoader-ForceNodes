# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import struct
import sys

if len(sys.argv) not in (2, 3):
    raise SystemExit('usage: python patch_engine_v1_6.py <v0.3.4-engine.dll> [output.dll]')
source = Path(sys.argv[1])
p = Path(sys.argv[2]) if len(sys.argv) == 3 else source.with_name('ForceNodes.Engine.v0.3.5.dll')
data = bytearray(source.read_bytes())

def rva_to_off(rva):
    # .text: RVA 0x1000, raw 0x400; .rdata: RVA 0x7000, raw 0x6400
    if 0x1000 <= rva < 0x6ec4:
        return rva - 0x1000 + 0x400
    if 0x7000 <= rva < 0x833b:
        return rva - 0x7000 + 0x6400
    raise ValueError(hex(rva))

def patch_bytes(rva, old, new):
    off=rva_to_off(rva)
    got=bytes(data[off:off+len(old)])
    if got!=old:
        raise SystemExit(f'byte mismatch at RVA {rva:#x}: {got.hex()} != {old.hex()}')
    if len(new)!=len(old):
        raise SystemExit('length mismatch')
    data[off:off+len(new)]=new

# Disable the retired recalibration branch unconditionally, even when an old
# INI still contains refresh_key. The branch now jumps directly to its normal
# latch-reset exit.
rva=0x30ce
old=b'\x0F\x8E\xA0\x00\x00\x00'
target=0x3174
rel=target-(rva+5)
new=b'\xE9'+struct.pack('<i',rel)+b'\x90'
patch_bytes(rva,old,new)

# After the injected compact row, skip the retired recalibration row and land
# directly on the former M4/M5 row (repurposed as ESC EXIT MODES).
rva=0x6ebf
old=b'\xE9\x36\xCA\xFF\xFF'
target=0x392e
rel=target-(rva+5)
new=b'\xE9'+struct.pack('<i',rel)
patch_bytes(rva,old,new)

# After printing ESC EXIT MODES, skip the old duplicate ESC row.
rva=0x3946
old=b'\xF3\x41\x0F\x58\xF8'
target=0x3963
rel=target-(rva+5)
new=b'\xE9'+struct.pack('<i',rel)
patch_bytes(rva,old,new)

def patch_utf16(old_text,new_text):
    old=(old_text+'\0').encode('utf-16le')
    new=(new_text+'\0').encode('utf-16le')
    pos=data.find(old)
    if pos<0:
        raise SystemExit(f'UTF16 string not found: {old_text!r}')
    if len(new)>len(old):
        raise SystemExit(f'UTF16 replacement too long: {new_text!r}')
    data[pos:pos+len(old)]=new+b'\0'*(len(old)-len(new))

# Key labels are intentionally omitted because every action is now independently
# rebindable. The HUD only shows mode state and the available edit actions.
patch_utf16('[1] OVERLAY   ON','OVERLAY   ON')
patch_utf16('[1] OVERLAY   OFF','OVERLAY   OFF')
patch_utf16('[2] FORCE     ON','FORCE     ON')
patch_utf16('[2] FORCE     OFF','FORCE     OFF')
patch_utf16('[3] GRID      OFF','GRID      OFF')
patch_utf16('[3] GRID   SQUARE','GRID   SQUARE')
patch_utf16('M4 ADD   M5 REMOVE','ESC EXIT MODES')

def patch_ascii(old_text,new_text):
    old=(old_text+'\0').encode('ascii')
    new=(new_text+'\0').encode('ascii')
    pos=data.find(old)
    if pos<0:
        raise SystemExit(f'ASCII string not found: {old_text!r}')
    if len(new)>len(old):
        raise SystemExit(f'ASCII replacement too long ({len(new)}>{len(old)}): {new_text!r}')
    data[pos:pos+len(old)]=new+b'\0'*(len(old)-len(new))

patch_ascii('0.3.4-engine-core','0.3.5-engine-core')
patch_ascii('ForceNodes  installed v0.3.4  dual square streams, O(1) node-removal scan',
            'ForceNodes  installed v0.3.5  configurable inputs, O(1) node-removal scan')
patch_ascii('ForceNodes  Ctrl+N1 overlay, Ctrl+N2 force, Ctrl+N3 grid, Ctrl+N4 recalib, Esc exit',
            'ForceNodes  configurable bindings active; recalibration removed; Esc exits modes')
patch_ascii('ForceNodes  edit buttons       Mouse4=X1 add  Mouse5=X2 remove',
            'ForceNodes  edit inputs read from ForceNodes.ini')
patch_ascii('ForceNodes  Mouse4/5 edge-latched; crosswalk/building connectors protected; HUD=%s',
            'ForceNodes  add/remove edge-latched; protected connectors; HUD=%s')

p.write_bytes(data)
print(f'patched {p} ({len(data)} bytes)')
