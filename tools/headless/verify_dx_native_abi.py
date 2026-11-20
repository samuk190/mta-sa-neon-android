#!/usr/bin/env python3
"""Read-only ABI checks for the user's GTA 2.10 ARM64 binary (never bundled).

This verifies symbols/instructions used by DX/2DFX. It does not execute hooks,
prove thread ordering, or replace device testing.
"""
import pathlib
import re
import struct
import sys

root = pathlib.Path(__file__).resolve().parents[2]
binary = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / "app/src/main/jniLibs/arm64-v8a/libGTASA.so"
data = binary.read_bytes()
header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
assert header[0][:6] == b"\x7fELF\x02\x01" and header[2] == 183, "requires ELF64 little-endian AArch64"
sections = [struct.unpack_from("<IIQQQQIIQQ", data, header[6] + i * header[11]) for i in range(header[12])]
segments = [struct.unpack_from("<IIQQQQQQ", data, header[5] + i * header[9]) for i in range(header[10])]
symbols = {}
for section in sections:
    if section[1] != 11:
        continue
    strings = sections[section[6]]
    for offset in range(section[4], section[4] + section[5], section[9]):
        name, info, other, index, address, size = struct.unpack_from("<IBBHQQ", data, offset)
        if index:
            start = strings[4] + name
            symbols[data[start:data.index(b"\0", start)].decode()] = (address, size)

def word(symbol, offset):
    address = symbols[symbol][0] + offset
    for segment in segments:
        if segment[0] == 1 and segment[3] <= address < segment[3] + segment[5] - 3:
            return struct.unpack_from("<I", data, segment[2] + address - segment[3])[0]
    raise AssertionError(f"unmapped instruction: {symbol}+{offset:#x}")

checks = 0
for file in ("Dx3DNative.cpp", "ModelEffectsNative.cpp"):
    source = (root / "app/src/main/cpp/gtasa" / file).read_text()
    for symbol in set(re.findall(r'"(_Z\w+|Scene|g2dEffectPluginOffset)"', source)):
        assert symbol in symbols, f"missing export: {symbol} ({file})"
        checks += 1

guards = {
    "_ZN14CBaseModelInfo11Get2dEffectEi": {0: 0xf81d0ff5, 4: 0xa9014ff4, 8: 0xa9027bfd, 12: 0x910083fd, 0x18: 0x39409e95},
    "_ZN14CBaseModelInfo13Init2dEffectsEv": {8: 0x39009c1f},
    "_ZN14CBaseModelInfo11Add2dEffectEP9C2dEffect": {0x24: 0x39009e68},
    "_Z16RwTextureSetNameP9RwTexturePKc": {0x1c: 0x91008260},
    "_Z14RwCameraCreatev": {0x68: 0x3d800e60},
    "_Z17RwCameraEndUpdateP8RwCamera": {0: 0xf9401c01, 4: 0xd61f0020},
    "_Z14RwRasterCreateiiii": {0x74: 0x29035a77},
    "_Z19_rwOpenGLRasterLockPvS_i": {0x8c: 0xb98026e9, 0x2f8: 0xb9402668},
    "_ZN7CEntity14CreateRwObjectEv": {0x38: 0xf9402d08},
    "_ZN7CEntity6RenderEv": {0xb8: 0xf9403808},
    "_ZN7CEntity22ProcessLightsForEntityEv": {0x14c: 0x39409e88, 0x254: 0x79404ae8},
}
for symbol, instructions in guards.items():
    for offset, expected in instructions.items():
        actual = word(symbol, offset)
        assert actual == expected, f"ABI mismatch: {symbol}+{offset:#x}: {actual:#010x}, expected {expected:#010x}"
        checks += 1
assert symbols["_ZN10CModelInfo16ms_modelInfoPtrsE"][1] == 20000 * 8
print(f"DX/2DFX native ABI: {checks + 1} checks passed (symbols/instructions only)")
